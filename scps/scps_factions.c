/*
 * scps_factions.c — LES FACTIONS PAR ÉTHOS (passe 1/N)
 *
 * Le spectre (six factions = axes IA + Communautaire) et son enracinement dans
 * les groupes culturels. Aucune mutation du moteur ici : on LIT les groupes et
 * on en tire un profil de factions. Les passes suivantes feront agir ce profil.
 */
#include "scps_factions.h"
#include <stdio.h>
#include "scps_species.h"   /* SpeciesArchetype */
#include <string.h>         /* memset (reset des stances) */

const char *faction_name(EthosFaction f){
    static const char *N[FAC_COUNT] = {
        "Conquérants", "Marchands", "Légistes", "Gardiens", "Transgresseurs", "Communautaires"
    };
    return (f>=0 && f<FAC_COUNT) ? N[f] : "?";
}

float class_clout(SocialClass k){
    /* Qui gouverne compte : l'élite pèse bien plus que la masse laborieuse. */
    switch (k){
        case CLASS_ELITE:    return 3.0f;
        case CLASS_BOURGEOIS:return 1.6f;
        default:             return 1.0f;   /* CLASS_LABORER */
    }
}

void group_ethos_lean(const PopCulture *c, float w[FAC_COUNT]){
    for (int f=0; f<FAC_COUNT; f++) w[f]=0.f;
    if (!c){ w[FAC_COMMUNAUTAIRE]=1.f; return; }

    /* 1) SOCLE — l'éthos de la culture donne la direction première. */
    switch (c->ethos){
        case ETHOS_DOMINATEUR:  w[FAC_CONQUERANT]+=1.0f; w[FAC_TRANSGRESSEUR]+=0.3f; break;
        case ETHOS_HONNEUR:     w[FAC_CONQUERANT]+=0.9f; w[FAC_GARDIEN]+=0.2f;       break; /* gloire + code */
        case ETHOS_ORDRE:       w[FAC_GARDIEN]+=0.7f;    w[FAC_LEGISTE]+=0.4f;       break; /* hiérarchie, tradition */
        case ETHOS_BUREAUCRATE: w[FAC_LEGISTE]+=1.0f;    break;
        case ETHOS_MERCANTILE:  w[FAC_MARCHAND]+=1.0f;   break;
        case ETHOS_PACIFISTE:   w[FAC_COMMUNAUTAIRE]+=1.0f; break;
        default: break;
    }
    /* 2) SIGNATURE de race — le penchant inné du peuple (§2). */
    switch (c->race){
        case RACE_ORQUE:    w[FAC_CONQUERANT]+=0.5f;    w[FAC_TRANSGRESSEUR]+=0.5f; break; /* guerre + interdit */
        case RACE_NAIN:     w[FAC_LEGISTE]+=0.4f;       w[FAC_TRANSGRESSEUR]+=0.4f; break; /* forge à runes */
        case RACE_HALFELIN: w[FAC_MARCHAND]+=0.4f;      w[FAC_COMMUNAUTAIRE]+=0.5f; break;
        case RACE_GNOME:    w[FAC_MARCHAND]+=0.5f;      w[FAC_COMMUNAUTAIRE]+=0.3f; break; /* négoce, bien commun */
        case RACE_ELFE:     w[FAC_TRANSGRESSEUR]+=0.4f; w[FAC_GARDIEN]+=0.3f;       break; /* arcane + tradition */
        case RACE_HUMAIN:   w[FAC_MARCHAND]+=0.2f;      w[FAC_LEGISTE]+=0.2f;       break; /* l'intégrateur */
        default: break;
    }
    /* 3) CREDO — la ferveur nourrit les Gardiens ; la tolérance, l'ouverture. */
    if      (c->credo==CREDO_PURIFICATEUR) w[FAC_GARDIEN]+=0.7f;
    else if (c->credo==CREDO_EVANGELISTE)  w[FAC_GARDIEN]+=0.4f;
    else { w[FAC_MARCHAND]+=0.15f; w[FAC_COMMUNAUTAIRE]+=0.15f; }   /* pluraliste : tolère, s'ouvre */

    /* normalise → un PROFIL de penchants (Σ=1). */
    float s=0.f; for (int f=0;f<FAC_COUNT;f++) s+=w[f];
    if (s>0.f) for (int f=0;f<FAC_COUNT;f++) w[f]/=s;
    else w[FAC_COMMUNAUTAIRE]=1.f;
}

/* ---- Agrégation : Σ groupes (pop pondérée par le poids de CLASSE × penchant) --- *
 * Le poids de classe vient désormais de la composition ÉMERGENTE du groupe (§pop
 * précise) : Σ_classe pop_by_class · clout(classe). Une vague de PROMOTIONS (plus de
 * Nobles, clout ×3) PÈSE donc plus lourd — bâtir déplace la politique interne. Repli
 * sur count·clout(klass) si l'émergence n'a pas encore couru. */
static void accumulate(const ProvincePop *pp, double acc[FAC_COUNT]){
    for (int i=0; i<pp->n_groups; i++){
        const PopGroup *g=&pp->groups[i];
        if (g->count<=0) continue;
        float lean[FAC_COUNT]; group_ethos_lean(&g->culture, lean);
        double wgt = (double)g->pop_by_class[CLASS_LABORER]  *(double)class_clout(CLASS_LABORER)
                   + (double)g->pop_by_class[CLASS_BOURGEOIS] *(double)class_clout(CLASS_BOURGEOIS)
                   + (double)g->pop_by_class[CLASS_ELITE]     *(double)class_clout(CLASS_ELITE);
        if (wgt <= 0.0) wgt = (double)g->count * (double)class_clout(g->klass);   /* repli pré-émergence */
        for (int f=0; f<FAC_COUNT; f++) acc[f] += wgt * lean[f];
    }
}

static EthosFaction finalize(double acc[FAC_COUNT], float out[FAC_COUNT]){
    double s=0.0; for (int f=0;f<FAC_COUNT;f++) s+=acc[f];
    int dom=FAC_COMMUNAUTAIRE; double best=-1.0;
    for (int f=0; f<FAC_COUNT; f++){
        out[f] = (s>0.0) ? (float)(acc[f]/s) : (f==FAC_COMMUNAUTAIRE?1.f:0.f);
        if (out[f] > best){ best=out[f]; dom=f; }
    }
    return (EthosFaction)dom;
}

EthosFaction faction_weights_of(const ProvincePop *provs, int n, float out[FAC_COUNT]){
    double acc[FAC_COUNT]={0};
    for (int p=0; p<n; p++) accumulate(&provs[p], acc);
    return finalize(acc, out);
}

EthosFaction country_faction_weights(const World *w, const WorldEconomy *econ, int cid,
                                     float out[FAC_COUNT]){
    double acc[FAC_COUNT]={0};
    if (cid>=0 && econ){
        for (int r=0; r<econ->n_regions; r++)
            if (econ->region[r].owner==cid && econ->region[r].culture.settled)
                accumulate(&econ->region[r].pop, acc);
    }
    (void)w;
    return finalize(acc, out);
}

/* ---- L'éthos effectif (§3) : la distribution → les cinq axes w_* ------- */
EthosWeights faction_effective_weights(const float w[FAC_COUNT]){
    EthosWeights e;
    /* Chaque axe = la part de sa faction. Le Communautaire RETIENT les aventures
     * (il bride expand & faustian — le bien-commun contre l'extraction/la guerre). */
    float restraint = w[FAC_COMMUNAUTAIRE];
    e.w_expand   = w[FAC_CONQUERANT]    * (1.f - 0.6f*restraint);
    e.w_trade    = w[FAC_MARCHAND];
    e.w_build    = w[FAC_LEGISTE];
    e.w_faith    = w[FAC_GARDIEN];
    e.w_faustian = w[FAC_TRANSGRESSEUR] * (1.f - 0.6f*restraint);
    return e;
}

/* ---- Cohésion vs fracture de valeurs (§6) ----------------------------- */
float faction_fracture(const float w[FAC_COUNT]){
    /* « Contesté » de la direction : la seconde faction talonne-t-elle la première ?
     * Une tête écrasante → 0 ; deux fortes au coude-à-coude (45/40) → ~1. Pondéré
     * par le poids cumulé des deux têtes (une paralysie de nains ne paralyse rien). */
    float s1=0.f, s2=0.f;
    for (int f=0; f<FAC_COUNT; f++){
        if (w[f] > s1){ s2=s1; s1=w[f]; }
        else if (w[f] > s2){ s2=w[f]; }
    }
    if (s1 <= 0.f) return 0.f;
    float contested = s2 / s1;            /* 0 (tête seule) .. 1 (au coude-à-coude) */
    float mass      = s1 + s2;            /* la dispute doit peser dans le pays */
    float fr = contested * mass;
    return fr<0.f ? 0.f : (fr>1.f ? 1.f : fr);
}
float faction_cohesion(const float w[FAC_COUNT]){ return 1.f - faction_fracture(w); }

/* ---- Opposition de valeurs & tension de coup (§5) --------------------- */
float faction_opposition(EthosFaction a, EthosFaction b){
    if (a==b) return 0.f;
    /* Table SYMÉTRIQUE des oppositions de valeurs (§1 « Oppose »).
     * Ordre : Conquérant, Marchand, Légiste, Gardien, Transgresseur, Communautaire. */
    static const float O[FAC_COUNT][FAC_COUNT] = {
        /* C */ { 0.f, 0.6f, 0.4f, 0.2f, 0.2f, 1.0f },
        /* M */ { 0.6f, 0.f, 0.2f, 0.9f, 0.5f, 0.3f },
        /* L */ { 0.4f, 0.2f, 0.f, 0.3f, 0.9f, 0.3f },
        /* G */ { 0.2f, 0.9f, 0.3f, 0.f, 1.0f, 0.5f },
        /* T */ { 0.2f, 0.5f, 0.9f, 1.0f, 0.f, 0.9f },
        /* U */ { 1.0f, 0.3f, 0.3f, 0.5f, 0.9f, 0.f },
    };
    if (a<0||a>=FAC_COUNT||b<0||b>=FAC_COUNT) return 0.f;
    return O[a][b];
}

float faction_coup_tension(const float w[FAC_COUNT], EthosFaction *out){
    int dom=0; for (int f=1; f<FAC_COUNT; f++) if (w[f]>w[dom]) dom=f;   /* la direction effective */
    float best=0.f; int bf=dom;
    for (int f=0; f<FAC_COUNT; f++){
        if (f==dom) continue;
        float t = w[f] * faction_opposition((EthosFaction)f, (EthosFaction)dom);  /* fort ET opposé */
        if (t>best){ best=t; bf=f; }
    }
    if (out) *out=(EthosFaction)bf;
    return best;
}

/* ===================================================================== */
/* LES LEVIERS COMME DES VOTES (§4) — stance par pays (état module)        */
/* ===================================================================== */
#define LEVER_BIAS_CAP  0.45f   /* une stance ne renverse pas la démographie, elle l'infléchit */
#define COUP_GRIEF_W    0.25f   /* poids du grief de politique dans la tension de coup (NUDGE, pas flot) */
static float g_lever_bias [SCPS_MAX_COUNTRY][FAC_COUNT];   /* le poids ajouté à la faction favorisée */
static float g_lever_grief[SCPS_MAX_COUNTRY][FAC_COUNT];   /* la rancœur des factions aliénées */

/* §C3 — CAPTURE DE L'ÉTAT : chaque concession gorge la faction gagnante. S'ACCUMULE,
 * décroît très lentement, ne rebondit pas. La somme = le « rot » (0..1) : moins
 * d'efficacité noble, K creusé. Lue à l'écran comme l'indice de Corruption (0-100). */
static float g_capture[SCPS_MAX_COUNTRY][FAC_COUNT];
#define CAPTURE_PER_CONCESSION 0.045f /* ce qu'une concession gorge à la faction gagnante */
#define CAPTURE_LEVER          0.06f  /* … qui gagne aussi en POUVOIR (un vote tenu) */
#define CAPTURE_MAX            0.85f  /* plafond du rot : un État jamais 100 % capturé */
#define CAPTURE_DECAY_FRAC     0.04f  /* la capture décroît à 4 % du rythme du grief (lent) */

void faction_save(FILE *f){
    fwrite(g_lever_bias, sizeof g_lever_bias, 1,f);
    fwrite(g_lever_grief,sizeof g_lever_grief,1,f);
    fwrite(g_capture,    sizeof g_capture,    1,f);
}
bool faction_load(FILE *f){
    return fread(g_lever_bias, sizeof g_lever_bias, 1,f)==1
        && fread(g_lever_grief,sizeof g_lever_grief,1,f)==1
        && fread(g_capture,    sizeof g_capture,    1,f)==1;
}
void faction_levers_reset(void){
    memset(g_lever_bias, 0, sizeof g_lever_bias);
    memset(g_lever_grief,0, sizeof g_lever_grief);
    memset(g_capture,    0, sizeof g_capture);
}
/* Une concession ACCORDÉE : la faction gagnante se gorge (capture↑) et gagne du
 * pouvoir (un vote). Le calme acheté aujourd'hui est une dette de demain. */
void faction_concede(int cid, EthosFaction winner){
    if (cid<0||cid>=SCPS_MAX_COUNTRY||winner<0||winner>=FAC_COUNT) return;
    g_capture[cid][winner] += CAPTURE_PER_CONCESSION;          /* s'accumule, ne rebondit pas */
    faction_lever_apply(cid, winner, CAPTURE_LEVER);           /* le captor monte en pouvoir */
}
/* Le « rot » 0..1 : part de l'État capturée (toutes factions), plafonnée. */
float faction_capture_total(int cid){
    if (cid<0||cid>=SCPS_MAX_COUNTRY) return 0.f;
    float s=0.f; for (int f=0;f<FAC_COUNT;f++) s+=g_capture[cid][f];
    return s<0.f?0.f:(s>CAPTURE_MAX?CAPTURE_MAX:s);
}
/* La métrique CORRUPTION (0-100) — le visage chiffré de la capture (l'écran). */
int faction_corruption_0_100(int cid){ return (int)(100.f*faction_capture_total(cid)+0.5f); }
/* La faction qui TIENT l'État (capture la plus haute) — pour le survol. */
EthosFaction faction_captor(int cid){
    if (cid<0||cid>=SCPS_MAX_COUNTRY) return FAC_COMMUNAUTAIRE;
    int best=0; float bv=g_capture[cid][0];
    for (int f=1;f<FAC_COUNT;f++) if (g_capture[cid][f]>bv){ bv=g_capture[cid][f]; best=f; }
    return (EthosFaction)best;
}
void faction_lever_apply(int cid, EthosFaction advanced, float strength){
    if (cid<0||cid>=SCPS_MAX_COUNTRY||advanced<0||advanced>=FAC_COUNT||strength<=0.f) return;
    float b=g_lever_bias[cid][advanced]+strength; if (b>LEVER_BIAS_CAP) b=LEVER_BIAS_CAP;
    g_lever_bias[cid][advanced]=b;                              /* la faction alignée gagne du poids */
    for (int f=0; f<FAC_COUNT; f++){                            /* … les opposées s'aigrissent */
        if (f==(int)advanced) continue;
        float g=g_lever_grief[cid][f] + strength*faction_opposition((EthosFaction)f,advanced);
        g_lever_grief[cid][f] = g>1.f ? 1.f : g;
    }
}
void faction_levers_decay(float rate){
    float r = rate<0.f?0.f:(rate>1.f?1.f:rate);
    float k  = 1.f - r;                                         /* la stance non entretenue s'efface */
    float kc = 1.f - r*CAPTURE_DECAY_FRAC;                      /* §C3 : la capture décroît TRÈS lentement */
    for (int c=0;c<SCPS_MAX_COUNTRY;c++) for (int f=0;f<FAC_COUNT;f++){
        g_lever_bias[c][f]*=k; g_lever_grief[c][f]*=k;
        g_capture[c][f]*=kc;
    }
}
float faction_grievance(int cid, EthosFaction f){
    if (cid<0||cid>=SCPS_MAX_COUNTRY||f<0||f>=FAC_COUNT) return 0.f;
    return g_lever_grief[cid][f];
}
/* ---- Engagement d'âge (§7) -------------------------------------------- */
#define ENGAGE_LEVER 0.10f   /* la pledge tenue RENFORCE le patron (un vote) */
#define ENGAGE_SAT   0.08f   /* … et APAISE (satisfaction de l'ordre ↑ un temps) */
EthosFaction age_patron(int age){
    switch (age){                                  /* cf. AgeId (scps_events.h) */
        case 0: return FAC_MARCHAND;        /* Commerce mondial : s'ouvrir au négoce */
        case 1: return FAC_LEGISTE;         /* Raison : codifier */
        case 2: return FAC_CONQUERANT;      /* Empires : l'expansion */
        case 3: return FAC_TRANSGRESSEUR;   /* Brèche : la puissance ultime */
        case 4: return FAC_LEGISTE;         /* Lumières : la raison, l'institution */
        case 5: return FAC_COMMUNAUTAIRE;   /* Soulèvements : le peuple, la réforme */
        case 6: return FAC_CONQUERANT;      /* Ordre de Fer : la poigne */
        default: return FAC_LEGISTE;
    }
}
void faction_age_engage(const World *w, WorldEconomy *econ, int cid, int age){
    (void)w;
    if (!econ || cid<0) return;
    faction_lever_apply(cid, age_patron(age), ENGAGE_LEVER);   /* la faction de l'heure s'avance */
    for (int r=0;r<econ->n_regions;r++) if (econ->region[r].owner==cid){
        float s=econ->region[r].satisfaction + ENGAGE_SAT;     /* cohésion du régime (apaise l'agitation) */
        econ->region[r].satisfaction = s>1.f?1.f:s;
    }
}
void faction_levers_on_coup(int cid){
    /* Le coup a basculé le régime : la rancœur accumulée se DÉCHARGE (sinon le pays
     * recouve aussitôt — un coup tous les deux ans). La pression politique est purgée. */
    if (cid<0||cid>=SCPS_MAX_COUNTRY) return;
    for (int f=0; f<FAC_COUNT; f++) g_lever_grief[cid][f]=0.f;
}

EthosFaction faction_effective_distribution(const World *w, const WorldEconomy *econ,
                                            int cid, float out[FAC_COUNT]){
    float base[FAC_COUNT]; country_faction_weights(w, econ, cid, base);
    double s=0.0;
    for (int f=0; f<FAC_COUNT; f++){
        float bias = (cid>=0&&cid<SCPS_MAX_COUNTRY) ? g_lever_bias[cid][f] : 0.f;
        float v = base[f] + bias; if (v<0.f) v=0.f;
        out[f]=v; s+=v;
    }
    int dom=FAC_COMMUNAUTAIRE; float best=-1.f;
    for (int f=0; f<FAC_COUNT; f++){
        out[f] = (s>0.0)?(float)(out[f]/s):(f==FAC_COMMUNAUTAIRE?1.f:0.f);
        if (out[f]>best){ best=out[f]; dom=f; }
    }
    return (EthosFaction)dom;
}

float faction_coup_tension_c(const World *w, const WorldEconomy *econ,
                             int cid, EthosFaction *out){
    /* La tension reste ancrée sur la distribution de BASE (la démographie, niveau §5,
     * borné) — surtout PAS sur la dominante biaisée par les leviers, sinon favoriser
     * un éthos rend ses opposés chroniquement aliénés → coups en boucle. Les leviers
     * n'AJOUTENT qu'un grief BORNÉ (∝ politique), purgé par un coup réussi. */
    float base[FAC_COUNT];
    EthosFaction dom = country_faction_weights(w, econ, cid, base);
    float best=0.f; int bf=dom;
    for (int f=0; f<FAC_COUNT; f++){
        if (f==(int)dom) continue;
        float grief = (cid>=0&&cid<SCPS_MAX_COUNTRY) ? g_lever_grief[cid][f] : 0.f;
        float t = base[f]*faction_opposition((EthosFaction)f,dom) + COUP_GRIEF_W*grief;
        if (t>best){ best=t; bf=f; }
    }
    if (out) *out=(EthosFaction)bf;
    return best;
}
