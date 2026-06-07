/*
 * scps_culture.h — POOLS CULTURELS & TRAITS (prototype)
 *
 * Voir « Pools culturels et traits — Document de conception ».
 *
 * Principe NON négociable : un trait n'est pas un modificateur ajouté, c'est
 * une COORDONNÉE D'AXE qu'on lit. Une culture EST une fiche : un vecteur sur
 * cinq axes (langue, valeurs, subsistance, parenté, religion). Les traits sont
 * les étiquettes lisibles de ces positions.
 *
 *   langue       — horloge phylogénétique (cousinage ressenti, jamais friction)
 *   valeurs      — trait ÉTHOS (hérité, collant — l'âme)
 *   subsistance  — trait MODE DE VIE (verrouillé par le biome — le corps)
 *   parenté      — trait STRUCTURE (attracteur dérivé, axe le + discriminant)
 *   religion     — couche CREDO (arbre propre + prosélytisme, transverse)
 *
 * Quatre traits saillants : Éthos (hérité), Mode de vie (biome), Martial et
 * Économique (ÉMERGENTS — résolus par la friction Éthos × Mode de vie, §4).
 *
 * Les traits Martial/Économique N'AJOUTENT PAS un 6ᵉ axe de distance : ce sont
 * des expressions de la fiche, pas des coordonnées propres.
 *
 * NOTE — valeurs chiffrées (ancres) indicatives, à calibrer. L'architecture
 * (axes, friction, lecture de distance) est fixe.
 */
#ifndef SCPS_CULTURE_H
#define SCPS_CULTURE_H

#include "scps_types.h"   /* Biome */
#include <stdbool.h>

/* ===================================================================== */
/* POOL ÉTHOS — axe VALEURS (spectre unique martial↔mercantile)          */
/* ===================================================================== */
typedef enum {
    ETHOS_DOMINATEUR = 0,  /* ~9   conquête, pousse H, coercitif-fragile */
    ETHOS_HONNEUR,         /* ~7.5 gloire, razzia, mauvais intégrateur   */
    ETHOS_ORDRE,           /* ~6   hiérarchie, discipline                */
    ETHOS_BUREAUCRATE,     /* ~4.5 bâtisseur de K, tient la diversité    */
    ETHOS_MERCANTILE,      /* ~3   profit, carrefours, exploite f(D)     */
    ETHOS_PACIFISTE,       /* ~1.5 consentement seul, ne fracture jamais */
    ETHOS_COUNT
} Ethos;

/* ===================================================================== */
/* POOL MODE DE VIE — axe SUBSISTANCE (verrouillé par le biome)          */
/* ===================================================================== */
typedef enum {
    LIFE_HUNTER = 0,       /* ~1    Forest/Jungle/Glacier  */
    LIFE_PASTORAL,         /* ~2.5  Steppe/Savanna/Dryland */
    LIFE_HORTICULTURE,     /* ~4    Jungle/Woods/Marsh     */
    LIFE_MINER,            /* ~5    Mountains/Highlands     */
    LIFE_FARMER,           /* ~6    Plains/Farmland/Hills   */
    LIFE_SEAFARER,         /* ~6m   Coastal                 */
    LIFE_INTENSIVE,        /* ~7.5  Farmland/vallées        */
    LIFE_COUNT
} Lifeway;

/* ===================================================================== */
/* POOL STRUCTURE — axe PARENTÉ (attracteur dérivé)                      */
/* ===================================================================== */
typedef enum {
    STRUCT_BILATERAL = 0,  /* ~2.5 intègre facilement, cohésion faible */
    STRUCT_LIGNAGER,       /* ~6   lignées de village                  */
    STRUCT_CLANIQUE,       /* ~7   loyauté, vendetta, dur à métaboliser*/
    STRUCT_TRIBAL_ENDO,    /* ~8   endogamie, mur de parenté           */
    STRUCT_COUNT
} Structure;

/* ===================================================================== */
/* COUCHE CREDO — axe RELIGION (transverse, parallèle)                   */
/* ===================================================================== */
typedef enum {                 /* arbre généalogique propre */
    REL_ANIMISTE = 0, REL_ABRAHAMIQUE, REL_DHARMIQUE, REL_SINIQUE,
    REL_BRANCH_COUNT
} ReligionBranch;

typedef enum {                 /* trait de prosélytisme (positions §6) */
    CREDO_PLURALISTE = 0,  /* bas : tolère, syncrétise, ↑f(D), ↓fracture */
    CREDO_EVANGELISTE,     /* haut : convertit, casus belli religieux    */
    CREDO_PURIFICATEUR,    /* haut+exclusif : homogénéise par la force,↑H */
    CREDO_COUNT
} Credo;

/* ===================================================================== */
/* POOLS ÉMERGENTS — MARTIAL & ÉCONOMIQUE (mutables, §4/§7)              */
/* ===================================================================== */
typedef enum {
    MART_EMBUSCADE = 0,    /* forêt + harmonie : guérilla            */
    MART_HORDE_MONTEE,     /* pastoral + martial : choc mobile        */
    MART_RAZZIA_MARITIME,  /* côtier + martial : pillage naval        */
    MART_MUR_BOUCLIERS,    /* agricole + ordre : formations denses    */
    MART_SIEGE,            /* sédentaire + bureaucrate : machines     */
    MART_LEVEE_MASSIVE,    /* agri. intensive + dominateur : armées   */
    MART_GARNISON_COL,     /* muté : enclavé défensif                 */
    MART_THALASSO_PREDATRICE, /* muté : thalassocratie de proie       */
    MART_COUNT
} MartialTrait;

typedef enum {
    ECON_TRIBUT = 0,       /* razzia : richesse par la guerre        */
    ECON_CARAVANE,         /* mercantile + désert/côte : carrefours  */
    ECON_RENTE_AGRAIRE,    /* agricole : taxation foncière           */
    ECON_GUILDE,           /* mercantile + sédentaire : manufacture  */
    ECON_PILLAGE_RUINES,   /* proximité ruine : charge faustienne    */
    ECON_CONTREBANDE,      /* muté : marché enclavé/parallèle        */
    ECON_TRIBUT_PORTS,     /* muté : rente des ports captifs         */
    ECON_COUNT
} EconTrait;

/* ===================================================================== */
/* LA FICHE CULTURE                                                      */
/* ===================================================================== */
typedef struct {
    /* Les cinq axes [0..10] — la SEULE chose lue pour la distance. */
    float langue, valeurs, subsistance, parente, religion;

    /* Traits saillants (étiquettes des positions). */
    Ethos        ethos;        /* hérité (axe valeurs) */
    Lifeway      lifeway;      /* verrouillé biome (axe subsistance) */
    Structure    structure;    /* dérivé (axe parenté) */
    Credo        credo;        /* couche religion */
    ReligionBranch rel_branch; /* arbre généalogique de la foi */
    MartialTrait martial;      /* émergent (muté) */
    EconTrait    econ;         /* émergent (muté) */

    int   age;                 /* ticks d'existence (dérive/syncrétisme) */
    bool  is_hybrid;           /* né d'une mutation incongrue ou d'un syncrétisme */
    char  name[64];            /* nom lisible (procédural) */
} Culture;

/* ===================================================================== */
/* RELATIONS LISIBLES (§9) — le joueur lit la structure, pas l'arithmétique */
/* ===================================================================== */
typedef enum {
    REL_PARENTS = 0,        /* horloge proche ET contenu proche */
    REL_COUSINS_DERIVES,    /* horloge proche, contenu lointain */
    REL_ETRANGERS,          /* horloge lointaine, contenu lointain */
    REL_JUMEAUX_CONVERGENTS,/* horloge lointaine, contenu jumeau */
    REL_ENNEMIS_SCHISME,    /* religion : branche proche, prosélytisme haut */
    CULT_REL_COUNT
} CultureRelation;

/* ===================================================================== */
/* API                                                                   */
/* ===================================================================== */

/* Construit une culture : mode de vie verrouillé par le biome, structure
 * dérivée, martial/éco résolus par la friction Éthos × Mode de vie (§4). */
Culture culture_make(Biome biome, Ethos ethos, ReligionBranch branch, Credo credo);

/* Les DEUX cultures d'un biome (même mode de vie, éthos opposés — §8). */
void culture_pair_for_biome(Biome biome, Culture *a, Culture *b,
                            ReligionBranch branch);

/* Le mode de vie verrouillé par un biome. */
Lifeway lifeway_for_biome(Biome biome);

/* ---- Ancres exposées (lecture seule) --------------------------------- *
 * Permettent à la génération du monde de choisir un éthos cohérent avec le
 * biome et de dériver la subsistance SANS dupliquer les tables internes
 * (évite la désynchronisation d'échelle entre modules). */
float lifeway_val_attr(Lifeway l);   /* éthos « naturel » attiré par ce mode de vie */
float lifeway_subs(Lifeway l);       /* ancre de subsistance du mode de vie [0..10] */
Ethos ethos_nearest(float value);    /* éthos dont l'ancre VALEURS est la plus proche */

/* ---- Distance & relation (§9) ---------------------------------------- */
/* D_inf : max des écarts sur les axes de CONTENU (valeurs, subsistance,
 * parenté, religion). C'est la friction. */
float culture_content_distance(const Culture *a, const Culture *b);
/* Horloge : écart de langue (cousinage ressenti, lu à part). */
float culture_clock_distance(const Culture *a, const Culture *b);
/* Classe la relation en vocabulaire lisible. */
CultureRelation culture_relation(const Culture *a, const Culture *b);

/* ---- Syncrétisme (§9 + correction « gouffre » v3) -------------------- *
 * SCPS est NON-ESSENTIALISTE : rien n'est inintégrable. Le gouffre est le
 * HAUT d'une échelle, pas un mur. La porte σ(0.8(P−D∞)+0.35(K−5)) ne s'annule
 * jamais (le terme en K reste) → la capacité institutionnelle métabolise
 * n'importe quelle distance, POURVU qu'il y en ait assez. L'assimilation
 * devient difficile (forte P+K) et lente (temps ∝ D∞), jamais impossible. */
typedef struct {
    bool  feasible;     /* la porte est-elle ouverte ? (jamais un mur : monte P/K) */
    float openness;     /* σ de la porte [0..1] */
    float time_ticks;   /* durée de fusion ∝ D∞ — lointain = des générations */
} SyncFeasibility;

/* Juge la faisabilité d'une fusion sous une perméabilité P et une capacité K.
 * Ne renvoie JAMAIS un « impossible » catégorique : la porte s'ouvre à forte
 * P+K, et le temps requis croît avec la distance de contenu D∞. */
SyncFeasibility culture_can_syncretize(const Culture *a, const Culture *b,
                                       float P, float K);

/* Réalise la fusion : A (substrat) sous élite B → hybride aux traits mutés.
 * Ne refuse plus pour cause de distance (cf. culture_can_syncretize pour la
 * porte et le temps). Renvoie false seulement si out est nul. */
bool culture_syncretize(const Culture *a, const Culture *b, Culture *out);

/* ---- Dérive lente de l'horloge (§10) --------------------------------- */
void culture_age_tick(Culture *c, float drift_rate);

/* ---- Libellés -------------------------------------------------------- */
const char *ethos_name(Ethos e);
const char *lifeway_name(Lifeway l);
const char *structure_name(Structure s);
const char *credo_name(Credo c);
const char *religion_branch_name(ReligionBranch b);
const char *martial_name(MartialTrait m);
const char *econ_name(EconTrait e);
const char *relation_name(CultureRelation r);

#endif /* SCPS_CULTURE_H */
