/*
 * scps_faith.c — la religion (voir scps_faith.h)
 *
 * Génération déterministe : on agrège les cultures du monde en foi par (branche ×
 * bande de l'axe religion). Chaque foi est la face sacrée de l'éthos MODAL de ses
 * fidèles ; son prosélytisme suit leur credo ; sa posture sur l'interdit (orthodoxe
 * ↔ culte) suit l'éthos sanctifié (le martial tolère mieux la puissance sombre que
 * l'ordre), avec un grain de graine. Les cultes profonds n'ÉMERGENT pas ici : ils
 * surgissent par schisme (stress / Brèche) — passe ultérieure.
 */
#include "scps_faith.h"
#include <string.h>
#include <math.h>
#include <stdio.h>

static inline float clampf(float v,float lo,float hi){return v<lo?lo:(v>hi?hi:v);}

/* ---- bande de l'axe religion (tertile) & clé de foi --------------------- */
static int religion_band(float religion){ return religion<3.34f?0 : (religion<6.67f?1:2); }
static int faith_key(ReligionBranch br, float religion){ return (int)br*3 + religion_band(religion); }

/* ---- posture sur l'interdit (orthodoxe bas ↔ culte haut), par éthos ----- *
 * Mostly orthodoxe au départ : le déchaînement (culte) viendra du schisme. */
static float ethos_stance_base(Ethos e){
    switch(e){
        case ETHOS_DOMINATEUR: return 0.36f;   /* la conquête flirte avec la puissance sombre */
        case ETHOS_HONNEUR:    return 0.30f;
        case ETHOS_MERCANTILE: return 0.26f;
        case ETHOS_PACIFISTE:  return 0.20f;
        case ETHOS_BUREAUCRATE:return 0.14f;
        case ETHOS_ORDRE:      return 0.10f;   /* la loi divine : strictement orthodoxe */
        default:               return 0.20f;
    }
}
static float credo_proselytism(Credo c){
    return (c==CREDO_PURIFICATEUR)?0.90f : (c==CREDO_EVANGELISTE)?0.60f : 0.20f;
}

/* ---- noms procéduraux --------------------------------------------------- */
const char *faith_branch_name(ReligionBranch b){
    switch(b){ case REL_ANIMISTE:return "animiste"; case REL_ABRAHAMIQUE:return "du Livre";
               case REL_DHARMIQUE:return "dharmique"; case REL_SINIQUE:return "des Ancêtres";
               default:return "?"; }
}
static const char *ethos_faith_word(Ethos e){
    switch(e){ case ETHOS_DOMINATEUR:return "le Glaive sacré"; case ETHOS_HONNEUR:return "la Voie de gloire";
               case ETHOS_ORDRE:return "la Loi divine"; case ETHOS_BUREAUCRATE:return "le Concile";
               case ETHOS_MERCANTILE:return "la Fortune bénie"; case ETHOS_PACIFISTE:return "la Communion";
               default:return "la Foi"; }
}

/* ---- accesseurs --------------------------------------------------------- */
const Faith *faith_get(const FaithSet *fs, int id){
    return (fs && id>=0 && id<fs->n) ? &fs->faith[id] : NULL;
}
const char *faith_name(const FaithSet *fs, int id){
    const Faith *f=faith_get(fs,id); return f?f->name:"sans foi";
}

/* ---- génération --------------------------------------------------------- */
void faith_generate(FaithSet *fs, const World *w, const WorldEconomy *econ, uint32_t seed){
    (void)w;
    memset(fs,0,sizeof(*fs));
    /* agrégats par clé (branche × bande) : compte, somme religion, histos éthos/credo. */
    long  cnt[FAITH_MAX]={0}; double rsum[FAITH_MAX]={0};
    int   ethist[FAITH_MAX][ETHOS_COUNT]; int crhist[FAITH_MAX][CREDO_COUNT];
    memset(ethist,0,sizeof ethist); memset(crhist,0,sizeof crhist);
    bool  present[FAITH_MAX]={false};
    for (int r=0;r<econ->n_regions;r++){
        const PopCulture *pc=&econ->region[r].culture;
        if (!pc->settled) continue;
        int key=faith_key(pc->rel_branch, pc->religion);
        if (key<0||key>=FAITH_MAX) continue;
        present[key]=true; cnt[key]++; rsum[key]+=pc->religion;
        if (pc->ethos>=0&&pc->ethos<ETHOS_COUNT) ethist[key][pc->ethos]++;
        if (pc->credo>=0&&pc->credo<CREDO_COUNT) crhist[key][pc->credo]++;
    }
    uint32_t rng = seed ? seed : 0x9e3779b9u;
    for (int key=0; key<FAITH_MAX; key++){
        if (!present[key]) continue;
        Faith *f=&fs->faith[fs->n];
        f->id=fs->n;
        f->branch=(ReligionBranch)(key/3);
        f->religion_center=(float)(rsum[key]/(double)cnt[key]);
        /* éthos MODAL = l'éthos que la foi sanctifie (sa faction). */
        int me=0; for (int e=1;e<ETHOS_COUNT;e++) if (ethist[key][e]>ethist[key][me]) me=e;
        f->sanctifies=(Ethos)me;
        int mc=0; for (int c=1;c<CREDO_COUNT;c++) if (crhist[key][c]>crhist[key][mc]) mc=c;
        f->proselytism=credo_proselytism((Credo)mc);
        /* posture : base de l'éthos + grain de graine (reste orthodoxe au départ). */
        rng ^= rng<<13; rng ^= rng>>17; rng ^= rng<<5;
        float jit=((rng>>8)&0xffff)/65535.f - 0.5f;   /* [-0.5..0.5] */
        f->forbidden_stance=clampf(ethos_stance_base(f->sanctifies)+0.12f*jit, 0.f, 1.f);
        snprintf(f->name,sizeof f->name,"%s (%s)", ethos_faith_word(f->sanctifies), faith_branch_name(f->branch));
        fs->n++;
        if (fs->n>=FAITH_MAX) break;
    }
}

int faith_of(const FaithSet *fs, const PopCulture *pc){
    if (!fs||fs->n==0||!pc) return -1;
    int key=faith_key(pc->rel_branch, pc->religion);
    /* la foi dont la clé (branche × bande) correspond ; sinon la plus proche en
     * branche+religion (robustesse après dérive). */
    int best=-1; float bestd=1e30f;
    for (int i=0;i<fs->n;i++){
        const Faith *f=&fs->faith[i];
        if (faith_key(f->branch, f->religion_center)==key) return i;
        float d=(f->branch==pc->rel_branch?0.f:5.f)+fabsf(f->religion_center-pc->religion);
        if (d<bestd){ bestd=d; best=i; }
    }
    return best;
}

float faith_distance(const FaithSet *fs, const PopCulture *a, const PopCulture *b){
    if (!a||!b) return 0.f;
    int fa=faith_of(fs,a), fb=faith_of(fs,b);
    if (fa>=0 && fa==fb) return 0.f;                 /* co-religionnaires */
    float dr=fabsf(a->religion-b->religion);
    float base=(a->rel_branch==b->rel_branch)? 3.5f : 8.f;   /* schisme vs autre branche */
    return clampf(base+0.2f*dr, 0.f, 10.f);
}

bool faith_forbids_faustian   (const Faith *f){ return f && f->forbidden_stance < 0.35f; }
bool faith_sacralizes_faustian(const Faith *f){ return f && f->forbidden_stance > 0.65f; }

FaithMood faith_mood(const FaithSet *fs, const PopCulture *crown, const PopCulture *group){
    if (!crown||!group) return FAITH_TIEDE;
    int fc=faith_of(fs,crown), fg=faith_of(fs,group);
    if (fc>=0 && fc==fg) return FAITH_DEVOT;                  /* même foi : dévot */
    if (crown->rel_branch==group->rel_branch) return FAITH_TIEDE; /* même branche : tiède */
    return FAITH_HERETIQUE;                                   /* autre branche : hérétique */
}
const char *faith_mood_word(FaithMood m){
    switch(m){ case FAITH_DEVOT:return "dévot"; case FAITH_HERETIQUE:return "hérétique"; default:return "tiède"; }
}
