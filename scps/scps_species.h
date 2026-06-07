#ifndef SCPS_SPECIES_H
#define SCPS_SPECIES_H
/*
 * scps_species.h — ROSTER DE RACES & SYSTÈME DE TRAITS (façon Stellaris)
 *
 * Couche BIOLOGIQUE/TEMPÉRAMENTALE de l'espèce (démographie, productivité,
 * tempérament). Elle se SUPERPOSE à la fiche culturelle SCPS (PopCulture) —
 * ensemble elles définissent un peuple. Les traits ne sont PAS verrouillés par
 * race : on se compose un orque charismatique en échangeant un trait et en
 * rééquilibrant à 0.
 *
 * Système de points : 3 catégories (Physique/Social/Intellectuel), un trait
 * par catégorie. 12 traits/pool (6 atouts qui coûtent, 6 défauts qui rendent),
 * 1 ou 2 points. Départ +1 ; un build est ÉQUILIBRÉ quand la somme signée des
 * 3 traits vaut +1 (budget restant = 1 − Σ = 0).
 *
 * Membrane : le joueur lit des MOTS (« Robuste », « Belliqueux ») + leur
 * définition au survol — jamais les leviers chiffrés derrière.
 */
#include <stdbool.h>

/* ===================================================================== */
/* SPHÈRES — tiers de magnitude sur un seul continuum (gouffre corrigé)   */
/* ===================================================================== */
typedef enum { SPHERE_ANCIENS = 0, SPHERE_HOMMES, SPHERE_ETRANGERS, SPHERE_COUNT } Sphere;
/* Distance de sphère : un CONTINUUM (5 = demi-elfes courants, 7 = demi-orques
 * rares et tendus), jamais un mur — tout s'assimile à forte P+K et avec du
 * temps (cf. culture_can_syncretize). */
float       sphere_distance(Sphere a, Sphere b);
const char *sphere_name(Sphere s);

/* ===================================================================== */
/* TRAITS                                                                 */
/* ===================================================================== */
typedef enum { CAT_PHYSIQUE = 0, CAT_SOCIAL, CAT_INTELLECTUEL, CAT_COUNT } TraitCategory;

/* 36 traits = 3 catégories × (6 atouts puis 6 défauts appariés à l'antonyme). */
typedef enum {
    /* Physique — atouts */
    T_ROBUSTE = 0, T_REGENERANT, T_PROLIFIQUE, T_LONGEVIF, T_ENDURANT, T_SOBRE,
    /* Physique — défauts (antonymes appariés) */
    T_FRELE, T_CONVALESCENT, T_LENT_CROITRE, T_EPHEMERE, T_FRAGILE_CLIMAT, T_VORACE,
    /* Social — atouts */
    T_BELLIQUEUX, T_SOUDE, T_CHARISMATIQUE, T_OUVERT, T_DISCIPLINE, T_PROSELYTE,
    /* Social — défauts */
    T_DEBONNAIRE, T_FACTIEUX, T_REBUTANT, T_INSULAIRE, T_FRONDEUR, T_RESERVE,
    /* Intellectuel — atouts */
    T_INVENTIF, T_ARCANIQUE, T_STUDIEUX, T_BATISSEUR, T_ADAPTABLE, T_INDUSTRIEUX,
    /* Intellectuel — défauts */
    T_BORNE, T_SOURD_ARCANE, T_INCULTE, T_BROUILLON, T_TRADITIONALISTE, T_INDOLENT,
    TRAIT_COUNT
} TraitId;

/* Leviers (ancrés au moteur). Deltas : 0 = neutre. demographie/productivite/
 * derive sont RELATIFS (s'appliquent comme 1+x) ; les autres sont ABSOLUS
 * (additifs sur l'échelle 0..10 du moteur, ou portée diplo). */
typedef struct {
    float demographie;   /* croissance de pop */
    float productivite;  /* rendement éco / contribution PE */
    float influence;     /* portée diplo & culturelle */
    float coercition;    /* + H (militaire/contrôle) */
    float capacite;      /* + K (diversité métabolisée) */
    float permeabilite;  /* + P (porte d'assimilation) */
    float arcane;        /* affinité magie (+ = pente faustienne) */
    float derive;        /* drift d'horloge & timer d'assimilation */
    float fracture;      /* + = fracture aggravée ; − = amortie */
} SpeciesLeviers;

typedef struct {
    const char    *name;
    TraitCategory  cat;
    int            pts;       /* signé : +1/+2 atout, −1/−2 défaut */
    TraitId        antonym;
    SpeciesLeviers lev;       /* deltas */
    const char    *hover;     /* définition (membrane) */
} TraitDef;

const TraitDef *trait_def(TraitId t);
const char     *trait_name(TraitId t);
const char     *trait_hover(TraitId t);
const char     *category_name(TraitCategory c);

/* ===================================================================== */
/* ESPÈCES & BUILDS                                                       */
/* ===================================================================== */
typedef enum {
    RACE_ELFE = 0, RACE_NAIN, RACE_GNOME, RACE_HUMAIN, RACE_HALFELIN, RACE_ORQUE,
    RACE_COUNT
} SpeciesArchetype;

/* Un build = un trait par catégorie (indexé par TraitCategory). */
typedef struct { TraitId trait[CAT_COUNT]; } SpeciesBuild;

Sphere          species_sphere(SpeciesArchetype r);
const char     *species_name(SpeciesArchetype r);
SpeciesBuild    species_default_build(SpeciesArchetype r);

/* Budget restant d'un build (départ +1 − Σ coûts + Σ gains). 0 = équilibré. */
int             build_budget(const SpeciesBuild *b);
bool            build_is_balanced(const SpeciesBuild *b);   /* budget == 0 */
/* Valide : un trait par catégorie, équilibré, aucun antonyme en conflit. */
bool            build_is_valid(const SpeciesBuild *b);
/* Compose les leviers des 3 traits (somme des deltas). */
SpeciesLeviers  build_leviers(const SpeciesBuild *b);

#endif /* SCPS_SPECIES_H */
