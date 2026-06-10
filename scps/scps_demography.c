/*
 * scps_demography.c — la clé de voûte : la province contient des groupes
 *
 * D interne PAR province (entre groupes), H jouable (suppression réversible),
 * assimilation incarnée (dérive durable via scps_modifier), L/fracture vécues.
 * On RÉUTILISE la formule de légitimité existante (mêmes constantes) → une
 * province mono-groupe reproduit les nombres d'aujourd'hui (non-régression).
 */
#include "scps_demography.h"
#include "scps_culture.h"   /* ethos_name */
#include "scps_labor.h"     /* capitale_max_tier : les emplois nobles dont la classe émerge (§pop précise) */
#include <string.h>
#include <math.h>
#include <stdio.h>

/* drift_id dynamiques (migration · conquête) : compteur unique, rebasé après
 * chargement (cf. demography_dyn_id_rebase). Le socle laisse la place aux ids
 * d'attache (1..n_groupes) loin en dessous. */
#define DYN_DRIFT_BASE 1000000
static int g_dyn_drift_id = DYN_DRIFT_BASE;

/* ---- Constantes de légitimité (miroir de scps_legitimacy : non-régression) */
#define K_ALIGN          1.0f
#define W_ALIGN          0.55f
#define W_AISANCE        0.45f
#define K_COERC          3.0f
#define K_H              0.4f
#define K_BUILD_H        0.6f
#define RELAX_RATE       0.04f
/* ---- Coercition (§3) — surface d'équilibrage -------------------------- */
#define COERCE_L_THRESH  4.0f   /* on ne réprime que les groupes restifs (L basse) */
#define SUPPRESS_PER_H   8.0f   /* baisse d'agitation par point de H (réversible) */
#define L_DROP_PER_H     0.06f  /* régner par la force ronge la L du groupe */
#define FRAG_PER_H       0.5f
/* ---- Assimilation (§5) ------------------------------------------------ */
#define YEARS_PER_DINF   10.0f  /* timer ∝ D∞ (gouffre) */
#define ASSIM_MIN_YEARS  12.0f
#define ASSIM_MAX_YEARS  200.0f
#define FUSE_EPS         0.30f   /* distance de contenu sous laquelle on fusionne */
/* ---- Conversion religieuse (§2) : la FOI converge vers le TRÔNE ------- *
 * L'assimilation tire la culture vers la dominante LOCALE ; la CONVERSION
 * tire la foi — l'axe doctrinal puis la BRANCHE sacrée — vers la COURONNE,
 * mais seulement sous un trône PROSÉLYTE. La bascule de branche exige que la
 * foi ait pris RACINE (années de règne) ET que l'axe ait convergé. */
#define FAITH_CONV_SOFT    0.015f /* évangélisme : fraction/an du gap doctrinal comblée */
#define FAITH_CONV_HARD    0.045f /* purification : conversion forcée, plus vive */
#define CONVERT_YEARS      60.0f  /* évangéliste : la branche bascule après deux générations */
#define CONVERT_YEARS_HARD 20.0f  /* purificateur : une génération suffit */
#define CONVERT_AXIS       1.5f   /* évangéliste : bascule quand l'axe a quasi convergé */
#define CONVERT_AXIS_HARD  4.0f   /* purificateur : bascule de force, axe encore distant */

static inline float clampf(float v,float lo,float hi){ return v<lo?lo:(v>hi?hi:v); }
static inline float absf(float v){ return v<0?-v:v; }

/* Distance de CONTENU (L∞, langue exclue) — la friction. */
/* La FOI est un axe ACTIF (§1/§3) : une autre BRANCHE de foi est une vraie
 * fracture, au-delà de l'axe religion (même foi → cohésion & assimilation vite ;
 * autre foi → résistance, fracture). */
#define FAITH_BRANCH_PEN 3.5f
static float content_dist(const PopCulture *a, const PopCulture *b){
    float dv=absf(a->valeurs-b->valeurs), ds=absf(a->subsistance-b->subsistance);
    float dp=absf(a->parente-b->parente), dr=absf(a->religion-b->religion);
    if (a->rel_branch!=b->rel_branch && dr<FAITH_BRANCH_PEN) dr=FAITH_BRANCH_PEN;
    float m=dv; if(ds>m)m=ds; if(dp>m)m=dp; if(dr>m)m=dr; return m;
}
static float agit_from_L(float L){ return clampf((6.f - L)*15.f, 0.f, 100.f); }

/* ===================================================================== */
/* FICHE EFFECTIVE = origine + dérive (recalcul, jamais mutation)         */
/* ===================================================================== */
static const PopCulture *dom_ruling_culture(const World *w, const WorldEconomy *econ, int cid){
    if (cid<0||cid>=w->n_countries) return NULL;
    int cp=w->country[cid].capital_prov; if(cp<0||cp>=w->n_provinces) return NULL;
    int cr=w->province[cp].region; if(cr<0||cr>=econ->n_regions) return NULL;
    return &econ->region[cr].culture;
}

PopCulture group_culture_effective(const PopGroup *g, const ModifierStack *drift){
    if (!g){ PopCulture z; memset(&z,0,sizeof z); return z; }  /* NULL-safe : cohérent avec le module défensif */
    PopCulture c = g->origin;
    if (drift){
        GroupDrift d = modstack_group_drift(drift, g->drift_id);
        c.valeurs     = clampf(c.valeurs     + d.dCv, 0.f, 10.f);
        c.subsistance = clampf(c.subsistance + d.dCs, 0.f, 10.f);
        c.parente     = clampf(c.parente     + d.dCp, 0.f, 10.f);
        c.religion    = clampf(c.religion    + d.dCr, 0.f, 10.f);
    }
    return c;
}
float group_agitation_effective(const PopGroup *g, const ModifierStack *drift){
    float suppress = drift ? modstack_group_drift(drift, g->drift_id).dAgit : 0.f;
    return clampf(g->agit_base - suppress, 0.f, 100.f);   /* la suppression MASQUE l'agitation vraie */
}

/* ===================================================================== */
/* LECTURES DE PROVINCE (§2)                                              */
/* ===================================================================== */
const PopGroup *province_dominant(const ProvincePop *pp){
    if (pp->n_groups<=0) return NULL;
    int best=0; for (int i=1;i<pp->n_groups;i++) if (pp->groups[i].count>pp->groups[best].count) best=i;
    return &pp->groups[best];
}
long province_total_pop(const ProvincePop *pp){
    long t=0; for (int i=0;i<pp->n_groups;i++) t+=pp->groups[i].count; return t;
}
/* D̄ interne = moyenne PONDÉRÉE des distances inter-groupes. */
float province_Dbar(const ProvincePop *pp, const ModifierStack *drift){
    double sw=0, sd=0;
    for (int i=0;i<pp->n_groups;i++){
        PopCulture ci=group_culture_effective(&pp->groups[i],drift);
        for (int j=i+1;j<pp->n_groups;j++){
            PopCulture cj=group_culture_effective(&pp->groups[j],drift);
            double w=(double)pp->groups[i].count*pp->groups[j].count;
            sd += w*content_dist(&ci,&cj); sw += w;
        }
    }
    return sw>0 ? (float)(sd/sw) : 0.f;
}
/* D∞ interne = MAILLON FAIBLE (max distance inter-groupes). */
float province_Dinf(const ProvincePop *pp, const ModifierStack *drift){
    float m=0;
    for (int i=0;i<pp->n_groups;i++){
        PopCulture ci=group_culture_effective(&pp->groups[i],drift);
        for (int j=i+1;j<pp->n_groups;j++){
            PopCulture cj=group_culture_effective(&pp->groups[j],drift);
            float d=content_dist(&ci,&cj); if(d>m)m=d;
        }
    }
    return m;
}
float province_L(const ProvincePop *pp){
    double num=0, den=0;
    for (int i=0;i<pp->n_groups;i++){ num+=(double)pp->groups[i].count*pp->groups[i].L; den+=pp->groups[i].count; }
    return den>0 ? (float)(num/den) : 5.f;
}
float province_agitation(const ProvincePop *pp, const ModifierStack *drift){
    double num=0, den=0;
    for (int i=0;i<pp->n_groups;i++){
        num+=(double)pp->groups[i].count*group_agitation_effective(&pp->groups[i],drift);
        den+=pp->groups[i].count;
    }
    return den>0 ? (float)(num/den) : 0.f;
}

/* ===================================================================== */
/* LÉGITIMITÉ PAR GROUPE (formule existante, clé sur culture vs couronne) */
/* ===================================================================== */
float group_L_target(const PopGroup *g, const ModifierStack *drift, const PopCulture *crown,
                     float satisfaction, float integ, float country_H, float coercion, float build_H){
    PopCulture eff = group_culture_effective(g, drift);
    float align   = crown ? (10.f - K_ALIGN*content_dist(&eff, crown)) : 5.f;
    float aisance = clampf(satisfaction*10.f, 0.f, 10.f);
    float ombre   = K_COERC*coercion + K_H*country_H + K_BUILD_H*build_H;
    float Lstar   = (W_ALIGN*align + W_AISANCE*aisance)*integ - ombre;
    return clampf(Lstar, 0.f, 10.f);
}
void group_L_tick(PopGroup *g, const ModifierStack *drift, const PopCulture *crown,
                  float satisfaction, float country_H, float coercion, float build_H){
    float Lstar = group_L_target(g, drift, crown, satisfaction, g->integration, country_H, coercion, build_H);
    g->L += (Lstar - g->L)*RELAX_RATE;
    g->L = clampf(g->L, 0.f, 10.f);
    g->agit_base = agit_from_L(g->L);
}

/* ===================================================================== */
/* H JOUABLE — SUPPRIME (réversible), n'assimile pas (§3)                  */
/* ===================================================================== */
CoercionEffect province_apply_coercion(ProvincePop *pp, ModifierStack *drift, float H){
    CoercionEffect e={0,0,0};
    const PopGroup *dom=province_dominant(pp);
    for (int i=0;i<pp->n_groups;i++){
        PopGroup *g=&pp->groups[i];
        if (g==dom || g->L>=COERCE_L_THRESH) continue;     /* on ne réprime que le restif */
        GroupDrift s={0,0,0,0, H*SUPPRESS_PER_H};
        modstack_accumulate_drift(drift, g->drift_id, s, true);   /* RÉVERSIBLE (saute si H tombe) */
        g->L = clampf(g->L - H*L_DROP_PER_H, 0.f, 10.f);          /* régner par la force ronge L */
        g->agit_base = agit_from_L(g->L);                         /* l'agitation VRAIE monte (masquée) */
        e.agitation_drop += H*SUPPRESS_PER_H; e.L_drop += H*L_DROP_PER_H; e.fragility_rise += H*FRAG_PER_H;
    }
    return e;
}
void province_lift_coercion(ProvincePop *pp, ModifierStack *drift){
    for (int i=0;i<pp->n_groups;i++) modstack_drop_reversible(drift, pp->groups[i].drift_id);
}

/* ===================================================================== */
/* ASSIMILATION — dérive DURABLE, timer ∝ D∞ (gouffre §5)                 */
/* ===================================================================== */
float assimilation_years(float Dinf, float P, float K){
    float metab = 0.5f + (P+K)/20.f;          /* P+K accélèrent (la capacité métabolise) */
    return clampf(Dinf*YEARS_PER_DINF/metab, ASSIM_MIN_YEARS, ASSIM_MAX_YEARS);
}
int assimilation_tick(ProvincePop *pp, ModifierStack *drift, float P, float K, float ypt){
    if (pp->n_groups<2) return 0;
    const PopGroup *dom=province_dominant(pp);
    int dom_idx=(int)(dom-pp->groups);
    PopCulture target=group_culture_effective(dom, drift);
    int fused=0;
    for (int i=pp->n_groups-1;i>=0;i--){
        if (i==dom_idx) continue;
        PopGroup *g=&pp->groups[i];
        PopCulture eff=group_culture_effective(g, drift);
        float d=content_dist(&eff,&target);
        if (d<FUSE_EPS){                                   /* fusion dans le dominant */
            pp->groups[dom_idx].count += g->count;
            int last=pp->n_groups-1;
            pp->groups[i]=pp->groups[last]; pp->n_groups--;
            if (dom_idx==last) dom_idx=i;
            fused++; continue;
        }
        float years=assimilation_years(d,P,K);
        float rate=ypt/fmaxf(years,1.f);
        GroupDrift step={ (target.valeurs    -eff.valeurs)    *rate,
                          (target.subsistance-eff.subsistance)*rate,
                          (target.parente    -eff.parente)    *rate,
                          (target.religion   -eff.religion)   *rate, 0.f };
        modstack_accumulate_drift(drift, g->drift_id, step, false);   /* DURABLE (métabolisé) */
        g->integration = clampf(g->integration + ypt/years, 0.f, 1.f);
    }
    return fused;
}

/* CONVERSION RELIGIEUSE (§2) — la foi des provinces converge vers le TRÔNE.
 * Distincte de l'assimilation (qui tire vers la dominante LOCALE) : ici l'axe
 * doctrinal de CHAQUE groupe dérive vers la couronne, et la branche sacrée
 * BASCULE une fois la foi enracinée (années de règne) et l'axe convergé. Un
 * trône pluraliste ne convertit personne — l'empire reste multi-confessionnel
 * (la tolérance est la non-conversion). Évangélisme : lent, n'achève que les
 * proches ; Purification : vif, bascule de force les confessions distantes. */
void faith_convert_tick(ProvincePop *pp, const PopCulture *crown,
                        float years_held, float ypt){
    if (!crown || crown->credo==CREDO_PLURALISTE) return;   /* tolérance : nulle conversion */
    bool  hard       = (crown->credo==CREDO_PURIFICATEUR);
    float rate       = (hard?FAITH_CONV_HARD   :FAITH_CONV_SOFT )*ypt;
    float need_years = (hard?CONVERT_YEARS_HARD :CONVERT_YEARS  );
    float need_axis  = (hard?CONVERT_AXIS_HARD  :CONVERT_AXIS   );
    for (int i=0;i<pp->n_groups;i++){
        PopGroup *g=&pp->groups[i];
        /* L'axe doctrinal de l'ORIGINE dérive vers le trône — mutation DIRECTE
         * (la foi convertie EST qui ils sont), pas la pile réversible : O(1) par
         * groupe, et cohérent avec la bascule de branche ci-dessous. */
        float gap = crown->religion - g->origin.religion;
        g->origin.religion = clampf(g->origin.religion + gap*rate, 0.f, 10.f);
        if (g->origin.rel_branch != crown->rel_branch
            && years_held >= need_years && absf(gap) <= need_axis){
            g->origin.rel_branch = crown->rel_branch;       /* la BRANCHE bascule (durable) */
            g->origin.credo      = crown->credo;            /* et embrasse le credo du trône */
        }
    }
}

/* ===================================================================== */
/* MIGRATION PASSIVE — emporte race + culture (§4)                        */
/* ===================================================================== */
bool migration_move(ProvincePop *from, ProvincePop *to, int gi, long amount, int new_drift_id){
    if (gi<0||gi>=from->n_groups) return false;
    PopGroup *src=&from->groups[gi];
    if (amount>src->count) amount=src->count;
    if (amount<=0) return false;
    /* groupe d'accueil = même race ET même culture d'origine ? sinon DIASPORA. */
    int dst=-1;
    for (int i=0;i<to->n_groups;i++)
        if (to->groups[i].race==src->race && content_dist(&to->groups[i].origin,&src->origin)<FUSE_EPS){ dst=i; break; }
    if (dst<0){
        if (to->n_groups>=DEMO_MAX_GROUPS) return false;
        PopGroup ng=*src;                    /* garde species/culture → minorité à l'arrivée */
        ng.count=amount; ng.diaspora=true; ng.integration=0.f; ng.drift_id=new_drift_id;
        to->groups[to->n_groups++]=ng;       /* crée du D interne dans la cible */
    } else {
        to->groups[dst].count += amount;
    }
    src->count -= amount;
    if (src->count<=0){ from->groups[gi]=from->groups[from->n_groups-1]; from->n_groups--; }
    return true;
}

/* ===================================================================== */
/* AGRÉGATION PAYS (§2, §6) — alimente scps_order (inchangé)              */
/* ===================================================================== */
/* On rassemble toutes les fiches effectives du pays, puis maillon faible. */
static int collect(const ProvincePop *provs, int n, const ModifierStack *drift,
                   PopCulture out[], long w[], int max){
    int k=0;
    for (int p=0;p<n && k<max;p++) for (int i=0;i<provs[p].n_groups && k<max;i++){
        out[k]=group_culture_effective(&provs[p].groups[i],drift); w[k]=provs[p].groups[i].count; k++;
    }
    return k;
}
float country_Dbar(const ProvincePop *provs, int n, const ModifierStack *drift){
    PopCulture c[DEMO_MAX_GROUPS*16]; long w[DEMO_MAX_GROUPS*16];
    int k=collect(provs,n,drift,c,w,DEMO_MAX_GROUPS*16);
    double sw=0,sd=0;
    for (int i=0;i<k;i++) for (int j=i+1;j<k;j++){ double ww=(double)w[i]*w[j]; sd+=ww*content_dist(&c[i],&c[j]); sw+=ww; }
    return sw>0?(float)(sd/sw):0.f;
}
float country_Dinf(const ProvincePop *provs, int n, const ModifierStack *drift){
    PopCulture c[DEMO_MAX_GROUPS*16]; long w[DEMO_MAX_GROUPS*16];
    int k=collect(provs,n,drift,c,w,DEMO_MAX_GROUPS*16);
    float m=0;
    for (int i=0;i<k;i++) for (int j=i+1;j<k;j++){ float d=content_dist(&c[i],&c[j]); if(d>m)m=d; }
    return m;
}
float country_L(const ProvincePop *provs, int n){
    double num=0,den=0;
    for (int p=0;p<n;p++) for (int i=0;i<provs[p].n_groups;i++){
        num+=(double)provs[p].groups[i].count*provs[p].groups[i].L; den+=provs[p].groups[i].count;
    }
    return den>0?(float)(num/den):5.f;
}

/* ===================================================================== */
/* COMPOSITION (§6) — la membrane : des mots, jamais de SCPS brut         */
/* ===================================================================== */
const char *labor_class_word(SocialClass k){
    switch(k){ case CLASS_ELITE: return "Noblesse"; case CLASS_BOURGEOIS: return "Artisans";
               default: return "Laboureurs"; }
}
int province_composition(const ProvincePop *pp, const ModifierStack *drift,
                         const PopCulture *crown, float P, float K,
                         GroupReadout out[], int max){
    static char etat_buf[DEMO_MAX_GROUPS][48];
    if (pp->n_groups<=0) return 0;                  /* province vide : nulle composition (province_dominant→NULL) */
    long total=province_total_pop(pp); if(total<1)total=1;
    const PopGroup *dom=province_dominant(pp);
    PopCulture domc=group_culture_effective(dom,drift);
    int n=0;
    for (int i=0;i<pp->n_groups && n<max;i++){
        const PopGroup *g=&pp->groups[i];
        PopCulture eff=group_culture_effective(g,drift);
        GroupReadout *r=&out[n];
        r->race    = species_name(g->race);
        r->culture = ethos_name(eff.ethos);
        r->religion= religion_branch_name(eff.rel_branch);
        r->klass   = labor_class_word(g->klass);
        r->percent = (int)(100*g->count/total);
        r->loyaute = band_humeur(g->L);
        if (g->diaspora){ r->etat="diaspora"; }
        else if (g==dom){ r->etat="natif"; }
        else {
            float d=content_dist(&eff, crown?crown:&domc);
            if (d<0.6f){ r->etat="natif"; }
            else {
                int yrs=(int)(assimilation_years(d,P,K)+0.5f);
                snprintf(etat_buf[n],sizeof etat_buf[n], "en assimilation (%d ans)", yrs);
                r->etat=etat_buf[n];
            }
        }
        n++;
    }
    return n;
}

/* ===================================================================== */
/* INTÉGRATION AU MOTEUR VIVANT (§7)                                      */
/* ===================================================================== */
/* ---- Migration vivante (surface d'équilibrage) ------------------------ */
#define MIG_GRADIENT   1.5f    /* il faut une cible NOTABLEMENT plus prospère */
#define MIG_FRACTION   200     /* 1/200 du groupe dominant par an (0.5 %) */
#define MIG_MIN        20

void demography_attach(World *w, WorldEconomy *econ, ModifierStack *drift){
    if (drift) memset(drift, 0, sizeof(*drift));     /* la pile de dérive du monde, à neuf */
    int id=1;
    for (int r=0; r<econ->n_regions; r++){
        RegionEconomy *re=&econ->region[r];
        ProvincePop *pp=&re->pop;
        memset(pp, 0, sizeof(*pp));
        pp->prov = (r<w->n_regions) ? w->region[r].province_ids[0] : -1;
        pp->prosperity = re->prosperity;
        if (!re->culture.settled) continue;
        long total = (long)(re->strata[CLASS_LABORER].pop + re->strata[CLASS_BOURGEOIS].pop
                          + re->strata[CLASS_ELITE].pop);
        if (total<1) total=1;
        PopGroup *g=&pp->groups[0]; memset(g,0,sizeof(*g));
        g->race=re->culture.race; g->origin_sphere=species_sphere(re->culture.race);
        g->origin=re->culture; g->culture=re->culture;     /* substrat = effective au départ */
        g->klass=CLASS_LABORER; g->count=total;
        g->pop_by_class[CLASS_LABORER]=total;        /* repli : tout Journalier avant la 1re émergence */
        g->pop_by_class[CLASS_BOURGEOIS]=0; g->pop_by_class[CLASS_ELITE]=0;
        g->L=7.f; g->agit_base=agit_from_L(7.f); g->integration=1.f;   /* natifs intégrés */
        g->diaspora=false; g->drift_id=id++;
        pp->n_groups=1;     /* MONO-GROUPE → non-régression (les nombres d'hier) */
    }
    g_dyn_drift_id=DYN_DRIFT_BASE;   /* nouvelle partie : le compteur dynamique repart au socle */
}

/* ---- drift_id DYNAMIQUES (migration · conquête) ------------------------- *
 * Un compteur UNIQUE et monotone : l'ancien schéma donnait 900000+région aux
 * colons de conquête → deux vagues sur la MÊME région partageaient un drift_id
 * (la dérive de l'une lue par l'autre). Le compteur n'étant pas sérialisé, on
 * le REBASE au-dessus du maximum chargé après game_load (sinon collision avec
 * les groupes d'une partie sauvegardée). */
int demography_dyn_id_next(void){ return g_dyn_drift_id++; }
void demography_dyn_id_rebase(const WorldEconomy *econ){
    int hi=DYN_DRIFT_BASE-1;
    for (int r=0;r<econ->n_regions;r++){
        const ProvincePop *pp=&econ->region[r].pop;
        for (int i=0;i<pp->n_groups;i++)
            if (pp->groups[i].drift_id>hi) hi=pp->groups[i].drift_id;
    }
    g_dyn_drift_id=hi+1;
}

/* ÉMERGENCE DE CLASSE (§pop précise) : la classe de CHAQUE groupe (race×culture×foi)
 * sort des EMPLOIS de la région — la capitale (tier·100 emplois NOBLES, le tier que
 * la pop débloque) + les ateliers (emploi BOURGEOIS ≈ ouvriers des manufactures),
 * répartis sur les groupes AU PRORATA, par paquets de 100. Σ pop_by_class = count.
 * Rien n'est posé : la structure d'emplois sculpte le tissu social, groupe par groupe. */
static void demography_emerge_classes(RegionEconomy *re){
    ProvincePop *pp=&re->pop;
    long total=0; for (int i=0;i<pp->n_groups;i++) total+=pp->groups[i].count;
    if (total<1){
        for (int i=0;i<pp->n_groups;i++){
            pp->groups[i].pop_by_class[CLASS_LABORER]=pp->groups[i].count;
            pp->groups[i].pop_by_class[CLASS_BOURGEOIS]=0;
            pp->groups[i].pop_by_class[CLASS_ELITE]=0;
        }
        return;
    }
    long elite_jobs   = (long)capitale_max_tier(total)*100;                  /* capitale : tier·100 */
    long artisan_jobs = 0;
    for (int b=0;b<re->n_bld;b++) artisan_jobs += (long)re->bld[b].workers;  /* ateliers : ouvriers */
    artisan_jobs = (artisan_jobs/100)*100;
    if (elite_jobs > total) elite_jobs=(total/100)*100;
    if (elite_jobs+artisan_jobs > total) artisan_jobs=((total-elite_jobs)/100)*100;
    for (int i=0;i<pp->n_groups;i++){
        PopGroup *g=&pp->groups[i];
        long e=((g->count*elite_jobs/total)/100)*100;       /* part noble de la bande */
        long a=((g->count*artisan_jobs/total)/100)*100;     /* part bourgeoise */
        if (e>g->count) e=(g->count/100)*100;
        if (e+a>g->count) a=((g->count-e)/100)*100;
        g->pop_by_class[CLASS_ELITE]=e;
        g->pop_by_class[CLASS_BOURGEOIS]=a;
        g->pop_by_class[CLASS_LABORER]=g->count-e-a;
    }
}

void demography_tick(World *w, WorldEconomy *econ, WorldLegitimacy *wl,
                     ModifierStack *drift, float P, float K, float dt){
    if (dt<=0.f) dt=1.f;
    /* 1. Par région : L par groupe, assimilation, rafraîchir le cache, sync dominante. */
    for (int r=0; r<econ->n_regions; r++){
        RegionEconomy *re=&econ->region[r];
        ProvincePop *pp=&re->pop;
        if (pp->n_groups<=0 || !re->culture.settled) continue;
        pp->prosperity = re->prosperity;
        const PopCulture *crown = (re->owner>=0) ? dom_ruling_culture(w,econ,re->owner) : &re->culture;
        for (int i=0;i<pp->n_groups;i++){
            group_L_tick(&pp->groups[i], drift, crown, re->satisfaction, 0.f, re->coercion, re->build.H_coerc);
            pp->groups[i].culture = group_culture_effective(&pp->groups[i], drift);
        }
        assimilation_tick(pp, drift, P, K, dt);                  /* dérive durable (∝ D∞), au pas dt */
        float yh = (wl && r < SCPS_MAX_REG) ? wl->years_held[r] : 100.f;
        faith_convert_tick(pp, crown, yh, dt);                  /* la FOI converge vers le trône (§2) */
        for (int i=0;i<pp->n_groups;i++)
            pp->groups[i].culture = group_culture_effective(&pp->groups[i], drift);
        const PopGroup *dom=province_dominant(pp);
        if (dom) re->culture = dom->culture;                     /* la dominante mène la province */
    }
    /* 2. Migration : les groupes affluent vers la prospérité voisine (round-robin). */
    for (int r=0; r<econ->n_regions; r++){
        RegionEconomy *re=&econ->region[r];
        if (re->pop.n_groups<=0 || !re->culture.settled) continue;
        int best=-1; float bestp=re->prosperity + MIG_GRADIENT;
        for (int s=0;s<econ->n_regions;s++)
            if (econ->adj[r][s] && econ->region[s].culture.settled && econ->region[s].pop.n_groups>0
                && econ->region[s].prosperity>bestp){ bestp=econ->region[s].prosperity; best=s; }
        if (best<0) continue;
        PopGroup *dom=(PopGroup*)province_dominant(&re->pop);
        long amount=dom->count/MIG_FRACTION;
        if (amount<MIG_MIN) continue;
        int gi=(int)(dom-re->pop.groups);
        migration_move(&re->pop, &econ->region[best].pop, gi, amount, demography_dyn_id_next());
    }
    /* 3. ÉMERGENCE DE CLASSE : la classe de chaque groupe sort des emplois (capitale
     *    + ateliers). Après migration/assimilation, le tissu social se recompose. */
    for (int r=0; r<econ->n_regions; r++){
        RegionEconomy *re=&econ->region[r];
        if (re->pop.n_groups>0 && re->culture.settled) demography_emerge_classes(re);
    }
}

void demography_on_conquest(World *w, WorldEconomy *econ, ModifierStack *drift, int region, int conqueror){
    (void)drift;
    if (region<0||region>=econ->n_regions) return;
    ProvincePop *pp=&econ->region[region].pop;
    if (pp->n_groups<=0) return;
    /* les conquis deviennent une minorité restive : l'intégration repart de zéro. */
    for (int i=0;i<pp->n_groups;i++){ pp->groups[i].integration=0.1f; pp->groups[i].L=2.0f; pp->groups[i].agit_base=agit_from_L(2.0f); }
    /* on dépose des colons de la couronne (culture du conquérant) → D INTERNE vécu. */
    const PopCulture *crown = dom_ruling_culture(w,econ,conqueror);
    if (crown && pp->n_groups<SCPS_MAX_GROUPS){
        long total=province_total_pop(pp);
        PopGroup g; memset(&g,0,sizeof g);
        g.race=crown->race; g.origin_sphere=species_sphere(crown->race);
        g.origin=*crown; g.culture=*crown; g.klass=CLASS_ELITE;
        g.count=total/5+50; g.L=7.f; g.agit_base=agit_from_L(7.f); g.integration=1.f;
        g.diaspora=true; g.drift_id=demography_dyn_id_next();
        pp->groups[pp->n_groups++]=g;
    }
}
