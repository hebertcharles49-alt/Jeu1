/*
 * scps_culture.c — pools culturels & traits (voir scps_culture.h)
 *
 * Data-driven, autonome (ne dépend que de Biome). Le branchement sur les
 * provinces du World viendra ; ici on fabrique des fiches et on lit la
 * distance. Aucun « +modificateur » : on pose des coordonnées, on les lit.
 */
#include "scps_culture.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

static inline float clampf(float v,float lo,float hi){return v<lo?lo:(v>hi?hi:v);}
static inline float absf(float v){return v<0?-v:v;}

/* ===================================================================== */
/* ANCRES D'AXES                                                          */
/* ===================================================================== */

/* Éthos → ancre sur l'axe valeurs (spectre martial↔mercantile). */
static const float ETHOS_VAL[ETHOS_COUNT] = {
    [ETHOS_DOMINATEUR]=9.0f, [ETHOS_HONNEUR]=7.5f, [ETHOS_ORDRE]=6.0f,
    [ETHOS_BUREAUCRATE]=4.5f, [ETHOS_MERCANTILE]=3.0f, [ETHOS_PACIFISTE]=1.5f,
};

/* Mode de vie → subsistance + attracteurs (parenté, valeurs). L'attracteur
 * n'IMPOSE pas l'éthos hérité : c'est leur friction qui fait muter (§4). */
typedef struct { float subs; float par_attr; float val_attr; } LifeDef;
static const LifeDef LIFE[LIFE_COUNT] = {
    [LIFE_HUNTER]      = { 1.0f, 2.5f, 2.0f },  /* bilatéral, harmonie */
    [LIFE_PASTORAL]    = { 2.5f, 7.0f, 7.0f },  /* clan, honneur */
    [LIFE_HORTICULTURE]= { 4.0f, 6.0f, 4.0f },  /* lignager, communautaire */
    [LIFE_MINER]       = { 5.0f, 7.0f, 5.5f },  /* clans isolés, méfiance */
    [LIFE_FARMER]      = { 6.0f, 6.0f, 5.5f },  /* patrilinéaire, hiérarchie */
    [LIFE_SEAFARER]    = { 6.0f, 2.5f, 3.0f },  /* bilatéral, mercantile */
    [LIFE_INTENSIVE]   = { 7.5f, 8.0f, 4.5f },  /* endogamie, bureaucratie */
};

/* Structure → ancre parenté. */
static const float STRUCT_PAR[STRUCT_COUNT] = {
    [STRUCT_BILATERAL]=2.5f, [STRUCT_LIGNAGER]=6.0f,
    [STRUCT_CLANIQUE]=7.0f, [STRUCT_TRIBAL_ENDO]=8.0f,
};

/* Credo → ancre religion (prosélytisme : la position §6). */
static const float CREDO_REL[CREDO_COUNT] = {
    [CREDO_PLURALISTE]=2.0f, [CREDO_EVANGELISTE]=7.0f, [CREDO_PURIFICATEUR]=9.0f,
};

/* ===================================================================== */
/* LIBELLÉS                                                               */
/* ===================================================================== */
const char *ethos_name(Ethos e){
    static const char*N[ETHOS_COUNT]={"Dominateur","Honneur","Ordre",
        "Bureaucrate","Mercantile","Pacifiste"};
    return (e>=0&&e<ETHOS_COUNT)?N[e]:"?";
}
const char *lifeway_name(Lifeway l){
    static const char*N[LIFE_COUNT]={"Chasseurs-cueilleurs","Pasteurs",
        "Horticulteurs","Mineurs-montagnards","Agriculteurs","Marins-marchands",
        "Agriculteurs intensifs"};
    return (l>=0&&l<LIFE_COUNT)?N[l]:"?";
}
const char *structure_name(Structure s){
    static const char*N[STRUCT_COUNT]={"Bilatéral","Lignager","Clanique",
        "Tribal endogame"};
    return (s>=0&&s<STRUCT_COUNT)?N[s]:"?";
}
const char *credo_name(Credo c){
    static const char*N[CREDO_COUNT]={"Pluraliste","Évangéliste","Purificateur"};
    return (c>=0&&c<CREDO_COUNT)?N[c]:"?";
}
const char *religion_branch_name(ReligionBranch b){
    static const char*N[REL_BRANCH_COUNT]={"Animiste","Abrahamique","Dharmique","Sinique"};
    return (b>=0&&b<REL_BRANCH_COUNT)?N[b]:"?";
}
const char *martial_name(MartialTrait m){
    static const char*N[MART_COUNT]={"Embuscade","Horde montée","Razzia maritime",
        "Mur de boucliers","Ingénierie de siège","Levée massive",
        "Garnisons de col","Thalassocratie prédatrice"};
    return (m>=0&&m<MART_COUNT)?N[m]:"?";
}
const char *econ_name(EconTrait e){
    static const char*N[ECON_COUNT]={"Tribut / butin","Caravane","Rente agraire",
        "Guilde / manufacture","Pillage des ruines","Contrebande","Tribut des ports"};
    return (e>=0&&e<ECON_COUNT)?N[e]:"?";
}
const char *relation_name(CultureRelation r){
    static const char*N[CULT_REL_COUNT]={"Parents","Cousins-dérivés","Étrangers",
        "Jumeaux convergents","Ennemis-schismatiques"};
    return (r>=0&&r<CULT_REL_COUNT)?N[r]:"?";
}

/* ===================================================================== */
/* BIOME → MODE DE VIE (verrou matériel)                                  */
/* ===================================================================== */
Lifeway lifeway_for_biome(Biome b){
    switch(b){
        case BIO_FOREST: case BIO_WOODS:            return LIFE_HUNTER;
        case BIO_GLACIER:                            return LIFE_HUNTER;
        case BIO_STEPPE: case BIO_SAVANNA:
        case BIO_GRASSLAND: case BIO_DRYLANDS:       return LIFE_PASTORAL;
        case BIO_JUNGLE: case BIO_MARSH:             return LIFE_HORTICULTURE;
        case BIO_MOUNTAINS: case BIO_PEAK:
        case BIO_HIGHLANDS: case BIO_HILLS:
        case BIO_VOLCANO:                            return LIFE_MINER;
        case BIO_PLAINS:                             return LIFE_FARMER;
        case BIO_FARMLAND:                           return LIFE_INTENSIVE;
        case BIO_COAST: case BIO_SHALLOW:
        case BIO_COASTAL_DESERT: case BIO_MANGROVE:  return LIFE_SEAFARER;
        case BIO_DESERT:                             return LIFE_PASTORAL;
        default:                                     return LIFE_FARMER;
    }
}

/* ===================================================================== */
/* MOTEUR DE MUTATION — friction Éthos × Mode de vie (§4)                 */
/* ===================================================================== */

/* Éthos « naturel » qu'un mode de vie attire (pour mesurer l'incongruité).
 * Exposé : la génération du monde l'utilise comme centre de tirage de l'éthos. */
float lifeway_val_attr(Lifeway l){ return LIFE[l].val_attr; }
/* Ancre de subsistance d'un mode de vie — source unique de vérité, partagée
 * avec subsistance_for_biome() côté monde (plus d'échelle inversée). */
float lifeway_subs(Lifeway l){ return LIFE[l].subs; }
/* Éthos dont l'ancre VALEURS est la plus proche d'une valeur donnée. */
Ethos ethos_nearest(float value){
    Ethos best = ETHOS_ORDRE; float bd = 1e9f;
    for (int e=0;e<ETHOS_COUNT;e++){
        float d = absf(ETHOS_VAL[e]-value);
        if (d<bd){ bd=d; best=(Ethos)e; }
    }
    return best;
}

/* Structure cristallisée : attracteur dérivé du mode de vie, infléchi par
 * l'éthos (un éthos très mercantile relâche la parenté ; très martial la
 * resserre en clan). */
static Structure resolve_structure(Lifeway l, Ethos e){
    float par = LIFE[l].par_attr + (ETHOS_VAL[e]-5.0f)*0.4f;
    par = clampf(par,0.f,10.f);
    if (par < 4.0f)  return STRUCT_BILATERAL;
    if (par < 6.5f)  return STRUCT_LIGNAGER;
    if (par < 7.7f)  return STRUCT_CLANIQUE;
    return STRUCT_TRIBAL_ENDO;
}

/* Trait MARTIAL émergent : part de la capacité du biome (mobilité/terrain),
 * pliée par l'agressivité de l'éthos. Honore les exemples travaillés (§4). */
static MartialTrait resolve_martial(Lifeway l, Ethos e){
    bool aggressive = (ETHOS_VAL[e] >= 6.5f);   /* Dominateur/Honneur */
    bool mercantile = (e==ETHOS_MERCANTILE);
    bool pacifist   = (e==ETHOS_PACIFISTE);
    switch(l){
        case LIFE_PASTORAL:
            /* agressif → choc monté ; sinon (pacifiste/mercantile/ordonné) la
             * cavalerie sert à la razzia furtive plutôt qu'à la charge. */
            return aggressive ? MART_HORDE_MONTEE : MART_EMBUSCADE;
        case LIFE_SEAFARER:
            /* dominateur → thalassocratie de proie ; tous les autres (agressif,
             * mercantile à flotte marchande armée, doux) → razzia maritime. */
            return (e==ETHOS_DOMINATEUR) ? MART_THALASSO_PREDATRICE
                                         : MART_RAZZIA_MARITIME;
        case LIFE_MINER:
            /* enclavé : la garde devient garnisons de col */
            return MART_GARNISON_COL;
        case LIFE_FARMER:
            if (e==ETHOS_DOMINATEUR)                     return MART_LEVEE_MASSIVE;
            if (e==ETHOS_ORDRE || e==ETHOS_BUREAUCRATE)  return MART_MUR_BOUCLIERS;
            if (mercantile || pacifist)                  return MART_SIEGE; /* paye l'ingénierie plutôt que le sang */
            return MART_MUR_BOUCLIERS;
        case LIFE_INTENSIVE:
            return (e==ETHOS_DOMINATEUR) ? MART_LEVEE_MASSIVE : MART_SIEGE;
        case LIFE_HUNTER:
        case LIFE_HORTICULTURE:
        default:
            /* forêt/harmonie : guérilla ; le pacifiste se replie en col gardé. */
            return pacifist ? MART_GARNISON_COL : MART_EMBUSCADE;
    }
}

/* Trait ÉCONOMIQUE émergent : même logique. */
static EconTrait resolve_econ(Lifeway l, Ethos e){
    bool raider     = (e==ETHOS_HONNEUR || e==ETHOS_DOMINATEUR);
    bool mercantile = (e==ETHOS_MERCANTILE);
    switch(l){
        case LIFE_PASTORAL:
            return raider ? ECON_TRIBUT : ECON_CARAVANE;
        case LIFE_SEAFARER:
            /* dominateur → rente des ports captifs ; sinon carrefour caravanier
             * maritime (le mercantile y excelle, mais l'issue est la même). */
            return (e==ETHOS_DOMINATEUR) ? ECON_TRIBUT_PORTS : ECON_CARAVANE;
        case LIFE_MINER:
            /* commerce contraint par l'enclavement → contrebande */
            return mercantile ? ECON_CONTREBANDE : ECON_RENTE_AGRAIRE;
        case LIFE_FARMER:
        case LIFE_INTENSIVE:
            return mercantile ? ECON_GUILDE : ECON_RENTE_AGRAIRE;
        case LIFE_HUNTER:
        case LIFE_HORTICULTURE:
        default:
            return raider ? ECON_TRIBUT : ECON_RENTE_AGRAIRE;
    }
}

/* ===================================================================== */
/* NOMMAGE PROCÉDURAL DES HYBRIDES                                        */
/* ===================================================================== */
static const char *life_people(Lifeway l){
    static const char*N[LIFE_COUNT]={"Chasseurs","Cavaliers","Essarteurs",
        "Montagnards","Laboureurs","Marins","Riziculteurs"};
    return N[l];
}
static const char *life_locale(Lifeway l){
    static const char*N[LIFE_COUNT]={"des bois","des steppes","des clairières",
        "des cols","des plaines","des côtes","des vallées"};
    return N[l];
}
static const char *ethos_qualifier(Ethos e){
    static const char*N[ETHOS_COUNT]={"prédateurs","d'honneur","de l'ordre",
        "scribes","marchands","sereins"};
    return N[e];
}

static void name_culture(Culture *c){
    /* Incongruité = écart entre l'éthos hérité et l'éthos naturel du biome. */
    float incong = absf(c->valeurs - lifeway_val_attr(c->lifeway));
    if (incong >= 3.0f) {
        /* hybride nommé que le designer n'a pas pré-écrit */
        c->is_hybrid = true;
        snprintf(c->name,sizeof(c->name),"%s %s %s",
                 life_people(c->lifeway), ethos_qualifier(c->ethos),
                 life_locale(c->lifeway));
    } else {
        c->is_hybrid = false;
        snprintf(c->name,sizeof(c->name),"%s %s",
                 life_people(c->lifeway), ethos_qualifier(c->ethos));
    }
}

/* ===================================================================== */
/* CONSTRUCTION D'UNE CULTURE                                             */
/* ===================================================================== */
Culture culture_make(Biome biome, Ethos ethos, ReligionBranch branch, Credo credo){
    Culture c; memset(&c,0,sizeof(c));
    c.lifeway = lifeway_for_biome(biome);
    c.ethos   = ethos;
    c.credo   = credo;
    c.rel_branch = branch;

    /* Axes de contenu posés par les traits. */
    c.valeurs     = ETHOS_VAL[ethos];
    c.subsistance = LIFE[c.lifeway].subs;
    c.structure   = resolve_structure(c.lifeway, ethos);
    c.parente     = STRUCT_PAR[c.structure];
    c.religion    = CREDO_REL[credo] + branch*0.5f;   /* prosélytisme + branche */
    c.langue      = 5.0f;   /* horloge neutre au départ (dérive ensuite) */

    /* Traits émergents : résolus par la friction (§4). */
    c.martial = resolve_martial(c.lifeway, ethos);
    c.econ    = resolve_econ(c.lifeway, ethos);

    c.age = 0;
    name_culture(&c);
    return c;
}

/* ===================================================================== */
/* DEUX CULTURES PAR BIOME (éthos opposés — §8)                          */
/* ===================================================================== */
/* Le couple d'éthos opposés est choisi dans une sous-pool compatible avec
 * le mode de vie du biome (les deux bouts pertinents du spectre). */
static void biome_ethos_pair(Biome b, Ethos *ea, Ethos *eb){
    switch(b){
        case BIO_STEPPE: case BIO_SAVANNA: case BIO_GRASSLAND:
            *ea=ETHOS_HONNEUR;     *eb=ETHOS_DOMINATEUR; break;
        case BIO_MOUNTAINS: case BIO_PEAK: case BIO_HIGHLANDS: case BIO_HILLS:
        case BIO_VOLCANO:
            *ea=ETHOS_ORDRE;       *eb=ETHOS_MERCANTILE; break;
        case BIO_COAST: case BIO_SHALLOW: case BIO_COASTAL_DESERT: case BIO_MANGROVE:
            *ea=ETHOS_MERCANTILE;  *eb=ETHOS_DOMINATEUR; break;
        case BIO_PLAINS: case BIO_FARMLAND:
            *ea=ETHOS_BUREAUCRATE; *eb=ETHOS_HONNEUR; break;
        case BIO_FOREST: case BIO_WOODS:
            *ea=ETHOS_PACIFISTE;   *eb=ETHOS_HONNEUR; break;
        case BIO_DESERT: case BIO_DRYLANDS:
            *ea=ETHOS_MERCANTILE;  *eb=ETHOS_HONNEUR; break;
        case BIO_JUNGLE: case BIO_MARSH:
            /* opposition portée par le CREDO (pluraliste cités-rituels vs
             * purificateur cultes fermés) → éthos doux vs dominateur. */
            *ea=ETHOS_PACIFISTE;   *eb=ETHOS_DOMINATEUR; break;
        case BIO_GLACIER:
            *ea=ETHOS_ORDRE;       *eb=ETHOS_PACIFISTE; break;
        default:
            *ea=ETHOS_BUREAUCRATE; *eb=ETHOS_HONNEUR; break;
    }
}

void culture_pair_for_biome(Biome biome, Culture *a, Culture *b,
                            ReligionBranch branch){
    Ethos ea, eb; biome_ethos_pair(biome,&ea,&eb);
    /* Le credo suit l'éthos : pluraliste pour le bout doux, purificateur/
     * évangéliste pour le bout dur (cf. §8 jungle/marsh). */
    Credo ca = (ETHOS_VAL[ea]>=6.5f)?CREDO_EVANGELISTE:CREDO_PLURALISTE;
    Credo cb = (ETHOS_VAL[eb]>=8.0f)?CREDO_PURIFICATEUR
             : (ETHOS_VAL[eb]>=6.5f)?CREDO_EVANGELISTE:CREDO_PLURALISTE;
    if (a) *a = culture_make(biome, ea, branch, ca);
    if (b) *b = culture_make(biome, eb, branch, cb);
}

/* ===================================================================== */
/* DISTANCE & RELATION (§9)                                               */
/* ===================================================================== */
float culture_content_distance(const Culture *a, const Culture *b){
    /* D_inf = max des écarts sur les axes de CONTENU. On ne somme jamais :
     * une seule grande différence suffit à friction. */
    float dv = absf(a->valeurs     - b->valeurs);
    float ds = absf(a->subsistance - b->subsistance);
    float dp = absf(a->parente     - b->parente);
    float dr = absf(a->religion    - b->religion);
    float m = dv; if(ds>m)m=ds; if(dp>m)m=dp; if(dr>m)m=dr;
    return m;
}
float culture_clock_distance(const Culture *a, const Culture *b){
    return absf(a->langue - b->langue);
}

#define CLOCK_NEAR   3.0f
#define CONTENT_NEAR 3.0f

CultureRelation culture_relation(const Culture *a, const Culture *b){
    /* Cas religieux prioritaire : narcissisme des petites différences.
     * Branche proche (même arbre) + prosélytisme haut des deux côtés +
     * petit écart de credo → ils se haïssent le plus. */
    bool same_branch = (a->rel_branch == b->rel_branch);
    bool both_zealous = (a->credo!=CREDO_PLURALISTE && b->credo!=CREDO_PLURALISTE);
    if (same_branch && both_zealous && absf(a->religion-b->religion) < 4.0f)
        return REL_ENNEMIS_SCHISME;

    float clock   = culture_clock_distance(a,b);
    float content = culture_content_distance(a,b);
    bool clock_near   = (clock   < CLOCK_NEAR);
    bool content_near = (content < CONTENT_NEAR);

    if (clock_near &&  content_near) return REL_PARENTS;
    if (clock_near && !content_near) return REL_COUSINS_DERIVES;
    if (!clock_near && content_near) return REL_JUMEAUX_CONVERGENTS;
    return REL_ETRANGERS;
}

/* ===================================================================== */
/* SYNCRÉTISME (§9 + correction « gouffre » v3)                          */
/* ===================================================================== */
/* Correction du gouffre : rien n'est inintégrable. La porte du moteur ne
 * s'annule jamais (terme en K), donc une capacité institutionnelle assez
 * forte métabolise n'importe quelle distance. L'assimilation est difficile
 * (forte P+K) et lente (temps ∝ D∞), jamais impossible. Calibrable. */
#define SYNC_TIME_BASE   30.f   /* ticks plancher d'une fusion (générations) */
#define SYNC_TIME_SLOPE  18.f   /* ticks ajoutés par point de D∞             */

/* La même porte que la prospérité de contact (§2.3) : σ(0.8(P−D∞)+0.35(K−5)). */
static float sync_gate(float P, float K, float dinf){
    return 1.f / (1.f + expf(-(0.8f*(P - dinf) + 0.35f*(K - 5.f))));
}

SyncFeasibility culture_can_syncretize(const Culture *a, const Culture *b,
                                       float P, float K){
    float dinf  = culture_content_distance(a,b);   /* D∞ = friction de contenu */
    float porte = sync_gate(P, K, dinf);
    SyncFeasibility f;
    f.openness  = porte;
    f.feasible  = (porte >= 0.5f);   /* ouverte : jamais un mur — il suffit de plus de P+K */
    /* Temps ∝ D∞, accéléré par une porte large : une fusion lointaine prend des
     * générations même la porte ouverte. */
    f.time_ticks = (SYNC_TIME_BASE + SYNC_TIME_SLOPE * dinf) / (porte < 0.1f ? 0.1f : porte);
    return f;
}

/* Tiens A sous une élite B assez longtemps → fusion en hybride aux traits
 * combinés et MUTÉS (pas une moyenne tiède). Plus de mur de distance (v3) :
 * la porte (P,K) et le temps sont jugés par culture_can_syncretize ; ici on
 * produit la fiche hybride une fois la fusion advenue. */
bool culture_syncretize(const Culture *a, const Culture *b, Culture *out){
    if (!out) return false;
    Culture h; memset(&h,0,sizeof(h));
    /* Héritage (collant) vient surtout de A (le substrat populaire) ;
     * la forme (valeurs/credo) penche vers l'élite B. */
    h.langue      = a->langue*0.7f + b->langue*0.3f;          /* horloge : A domine */
    h.valeurs     = a->valeurs*0.4f + b->valeurs*0.6f;        /* éthos : élite B */
    h.subsistance = a->subsistance;                            /* corps : le biome de A */
    h.parente     = a->parente*0.5f + b->parente*0.5f;
    h.religion    = b->religion;                               /* credo de l'élite */
    h.rel_branch  = b->rel_branch;
    h.credo       = b->credo;

    /* Traits : on garde le mode de vie de A (verrou biome), l'éthos résolu
     * sur les valeurs fusionnées, et on REMUTE les émergents. */
    h.lifeway = a->lifeway;
    h.ethos     = ethos_nearest(h.valeurs);   /* éthos le plus proche des valeurs fusionnées */
    h.structure = resolve_structure(h.lifeway, h.ethos);
    h.parente   = STRUCT_PAR[h.structure]*0.5f + h.parente*0.5f;
    h.martial   = resolve_martial(h.lifeway, h.ethos);
    h.econ      = resolve_econ(h.lifeway, h.ethos);
    h.age       = 0;
    h.is_hybrid = true;
    snprintf(h.name,sizeof(h.name),"%s %s (syncrétiques)",
             life_people(h.lifeway), ethos_qualifier(h.ethos));
    *out = h;
    return true;
}

/* ===================================================================== */
/* DÉRIVE LENTE DE L'HORLOGE (§10)                                        */
/* ===================================================================== */
void culture_age_tick(Culture *c, float drift_rate){
    c->age++;
    /* L'horloge (langue) avance toujours, lentement et indépendamment du
     * contenu : deux cultures isolées divergent en cousinage sans changer
     * d'âme. Dérive bornée à [0..10]. */
    c->langue = clampf(c->langue + drift_rate, 0.f, 10.f);
}
