/*
 * scps_species.c — roster de races & traits (voir scps_species.h)
 *
 * Data-driven et autonome. Les leviers (chiffres) sont calibrables ; la
 * structure (12 traits/pool, atouts/défauts appariés, équilibre à 0) est fixe.
 */
#include "scps_species.h"
#include <stddef.h>

/* ===================================================================== */
/* SPHÈRES                                                                */
/* ===================================================================== */
/* Continuum (pas un mur) : 5 = demi-elfes courants, 7 = demi-orques rares. */
static const float SPHERE_DIST[SPHERE_COUNT][SPHERE_COUNT] = {
    /*            Anciens Hommes Étrangers */
    /* Anciens */ { 0.f,   5.f,   7.f },
    /* Hommes  */ { 5.f,   0.f,   7.f },
    /* Étrangers*/ { 7.f,   7.f,   0.f },
};
float sphere_distance(Sphere a, Sphere b) {
    if (a<0||a>=SPHERE_COUNT||b<0||b>=SPHERE_COUNT) return 0.f;
    return SPHERE_DIST[a][b];
}
const char *sphere_name(Sphere s) {
    static const char *N[SPHERE_COUNT] = { "Anciens", "Hommes", "Étrangers" };
    return (s>=0&&s<SPHERE_COUNT)?N[s]:"?";
}
const char *category_name(TraitCategory c) {
    static const char *N[CAT_COUNT] = { "Physique", "Social", "Intellectuel" };
    return (c>=0&&c<CAT_COUNT)?N[c]:"?";
}

/* ===================================================================== */
/* TABLE DES 36 TRAITS                                                    */
/* ===================================================================== */
static const TraitDef TRAITS[TRAIT_COUNT] = {
/* ---- PHYSIQUE — atouts ---- */
[T_ROBUSTE]      = { "Robuste",      CAT_PHYSIQUE, +2, T_FRELE,
    { .coercition=+1.0f, .productivite=+0.20f },
    "Forte constitution : meilleur au combat et au travail de force." },
[T_REGENERANT]   = { "Régénérant",   CAT_PHYSIQUE, +2, T_CONVALESCENT,
    { .demographie=+0.20f, .coercition=+1.0f },
    "Guérit vite (sang de troll) : récupère ses pertes, tient au front." },
[T_PROLIFIQUE]   = { "Prolifique",   CAT_PHYSIQUE, +1, T_LENT_CROITRE,
    { .demographie=+0.15f },
    "Reproduction rapide : croissance de population élevée." },
[T_LONGEVIF]     = { "Longévif",     CAT_PHYSIQUE, +1, T_EPHEMERE,
    { .derive=-0.20f },
    "Vie longue : dérive lente, dirigeants durables, héritage stable." },
[T_ENDURANT]     = { "Endurant",     CAT_PHYSIQUE, +1, T_FRAGILE_CLIMAT,
    { .demographie=+0.10f },
    "Résiste au climat et à la disette : tient hors de sa niche." },
[T_SOBRE]        = { "Sobre",        CAT_PHYSIQUE, +1, T_VORACE,
    { .productivite=+0.10f },
    "Consomme peu : nourriture nette supérieure, entretien faible." },
/* ---- PHYSIQUE — défauts ---- */
[T_FRELE]        = { "Frêle",        CAT_PHYSIQUE, -2, T_ROBUSTE,
    { .coercition=-1.0f, .productivite=-0.20f },
    "Faible constitution : médiocre au combat et au travail." },
[T_CONVALESCENT] = { "Convalescent", CAT_PHYSIQUE, -2, T_REGENERANT,
    { .demographie=-0.20f, .coercition=-1.0f },
    "Guérison lente : encaisse mal les pertes." },
[T_LENT_CROITRE] = { "Lent à croître",CAT_PHYSIQUE, -1, T_PROLIFIQUE,
    { .demographie=-0.15f },
    "Reproduction lente : croissance de population faible." },
[T_EPHEMERE]     = { "Éphémère",     CAT_PHYSIQUE, -1, T_LONGEVIF,
    { .derive=+0.20f },
    "Vie courte : dérive rapide, dirigeants instables." },
[T_FRAGILE_CLIMAT]={ "Fragile au climat",CAT_PHYSIQUE,-1, T_ENDURANT,
    { .demographie=-0.10f },
    "Souffre hors de sa niche : croissance pénalisée en biome dur." },
[T_VORACE]       = { "Vorace",       CAT_PHYSIQUE, -1, T_SOBRE,
    { .productivite=-0.10f },
    "Consomme beaucoup : entretien lourd, pression alimentaire." },
/* ---- SOCIAL — atouts ---- */
[T_BELLIQUEUX]   = { "Belliqueux",   CAT_SOCIAL, +2, T_DEBONNAIRE,
    { .coercition=+1.0f },
    "Aime la guerre : bonus militaire, casus belli faciles." },
[T_SOUDE]        = { "Soudé",        CAT_SOCIAL, +2, T_FACTIEUX,
    { .fracture=-1.0f },
    "Solidarité large : tient la diversité, fracture amortie." },
[T_CHARISMATIQUE]= { "Charismatique",CAT_SOCIAL, +1, T_REBUTANT,
    { .influence=+0.5f },
    "Présence : meilleure diplomatie et rayonnement." },
[T_OUVERT]       = { "Ouvert",       CAT_SOCIAL, +1, T_INSULAIRE,
    { .permeabilite=+0.5f },
    "Hospitalier : perméabilité élevée, porte d'assimilation plus ouverte." },
[T_DISCIPLINE]   = { "Discipliné",   CAT_SOCIAL, +1, T_FRONDEUR,
    { .capacite=+0.5f },
    "Ordre intériorisé : soutient la capacité, agitation réduite." },
[T_PROSELYTE]    = { "Prosélyte",    CAT_SOCIAL, +1, T_RESERVE,
    { .influence=+0.25f, .derive=+0.20f },
    "Rayonne sa culture : assimile les autres plus vite." },
/* ---- SOCIAL — défauts ---- */
[T_DEBONNAIRE]   = { "Débonnaire",   CAT_SOCIAL, -2, T_BELLIQUEUX,
    { .coercition=-1.0f },
    "Rétif à la guerre : malus militaire, casus belli rares." },
[T_FACTIEUX]     = { "Factieux",     CAT_SOCIAL, -2, T_SOUDE,
    { .fracture=+1.0f },
    "Clans rivaux : fracture aggravée, diversité dure à tenir." },
[T_REBUTANT]     = { "Rebutant",     CAT_SOCIAL, -1, T_CHARISMATIQUE,
    { .influence=-0.5f },
    "Déplaisant : diplomatie et rayonnement pénalisés." },
[T_INSULAIRE]    = { "Insulaire",    CAT_SOCIAL, -1, T_OUVERT,
    { .permeabilite=-0.5f },
    "Méfiant : perméabilité basse, friction élevée, assimile mal." },
[T_FRONDEUR]     = { "Frondeur",     CAT_SOCIAL, -1, T_DISCIPLINE,
    { .capacite=-0.5f },
    "Indiscipliné : agitation, capacité plus difficile à tenir." },
[T_RESERVE]      = { "Réservé",      CAT_SOCIAL, -1, T_PROSELYTE,
    { .influence=-0.25f, .derive=-0.20f },
    "Sa culture ne s'exporte pas : assimile lentement." },
/* ---- INTELLECTUEL — atouts ---- */
[T_INVENTIF]     = { "Inventif",     CAT_INTELLECTUEL, +2, T_BORNE,
    { .productivite=+0.20f },
    "Innovation : recherche accélérée, rendement supérieur." },
[T_ARCANIQUE]    = { "Arcanique",    CAT_INTELLECTUEL, +2, T_SOURD_ARCANE,
    { .arcane=+1.0f },
    "Sensible à la magie : branche Magie facilitée — pente faustienne." },
[T_STUDIEUX]     = { "Studieux",     CAT_INTELLECTUEL, +1, T_INCULTE,
    { .productivite=+0.10f },
    "Soif de savoir : recherche améliorée." },
[T_BATISSEUR]    = { "Bâtisseur",    CAT_INTELLECTUEL, +1, T_BROUILLON,
    { .capacite=+0.5f },
    "Méthodique : capacité supérieure, tient plus de diversité." },
[T_ADAPTABLE]    = { "Adaptable",    CAT_INTELLECTUEL, +1, T_TRADITIONALISTE,
    { .derive=+0.20f },
    "Curieux : dérive rapide, prend la forme du biome, assimile vite." },
[T_INDUSTRIEUX]  = { "Industrieux",  CAT_INTELLECTUEL, +1, T_INDOLENT,
    { .productivite=+0.10f },
    "Appliqué : rendement économique régulier." },
/* ---- INTELLECTUEL — défauts ---- */
[T_BORNE]        = { "Borné",        CAT_INTELLECTUEL, -2, T_INVENTIF,
    { .productivite=-0.20f },
    "Esprit fermé : recherche et rendement pénalisés." },
[T_SOURD_ARCANE] = { "Sourd à l'arcane",CAT_INTELLECTUEL,-2, T_ARCANIQUE,
    { .arcane=-1.0f },
    "Imperméable à la magie : branche Magie très coûteuse." },
[T_INCULTE]      = { "Inculte",      CAT_INTELLECTUEL, -1, T_STUDIEUX,
    { .productivite=-0.10f },
    "Indifférent au savoir : recherche affaiblie." },
[T_BROUILLON]    = { "Brouillon",    CAT_INTELLECTUEL, -1, T_BATISSEUR,
    { .capacite=-0.5f },
    "Désordonné : capacité plus basse." },
[T_TRADITIONALISTE]={ "Traditionaliste",CAT_INTELLECTUEL,-1, T_ADAPTABLE,
    { .derive=-0.20f },
    "Rigide : dérive lente, change mal — mais héritage très stable." },
[T_INDOLENT]     = { "Indolent",     CAT_INTELLECTUEL, -1, T_INDUSTRIEUX,
    { .productivite=-0.10f },
    "Nonchalant : rendement économique faible." },
};

const TraitDef *trait_def(TraitId t){ return (t>=0&&t<TRAIT_COUNT)?&TRAITS[t]:NULL; }
const char     *trait_name(TraitId t){ return (t>=0&&t<TRAIT_COUNT)?TRAITS[t].name:"?"; }
const char     *trait_hover(TraitId t){ return (t>=0&&t<TRAIT_COUNT)?TRAITS[t].hover:""; }

/* ===================================================================== */
/* ROSTER — builds par défaut (tous équilibrés à 0)                       */
/* ===================================================================== */
static const Sphere RACE_SPHERE[RACE_COUNT] = {
    [RACE_ELFE]=SPHERE_ANCIENS, [RACE_NAIN]=SPHERE_ANCIENS, [RACE_GNOME]=SPHERE_ANCIENS,
    [RACE_HUMAIN]=SPHERE_HOMMES, [RACE_HALFELIN]=SPHERE_HOMMES, [RACE_ORQUE]=SPHERE_ETRANGERS,
};
Sphere species_sphere(SpeciesArchetype r){
    return (r>=0&&r<RACE_COUNT)?RACE_SPHERE[r]:SPHERE_HOMMES;
}
const char *species_name(SpeciesArchetype r){
    static const char *N[RACE_COUNT]={"Elfe","Nain","Gnome","Humain","Halfelin","Orque"};
    return (r>=0&&r<RACE_COUNT)?N[r]:"?";
}

/* Builds par défaut (un trait par catégorie ; Physique, Social, Intellectuel). */
static const SpeciesBuild ROSTER[RACE_COUNT] = {
    [RACE_ELFE]     = {{ T_LONGEVIF,   T_DEBONNAIRE, T_ARCANIQUE }},
    [RACE_NAIN]     = {{ T_ROBUSTE,    T_FACTIEUX,   T_BATISSEUR }},
    [RACE_GNOME]    = {{ T_FRELE,      T_CHARISMATIQUE, T_INVENTIF }},
    [RACE_HUMAIN]   = {{ T_PROLIFIQUE, T_FRONDEUR,   T_ADAPTABLE }},
    [RACE_HALFELIN] = {{ T_SOBRE,      T_OUVERT,     T_TRADITIONALISTE }},
    [RACE_ORQUE]    = {{ T_ENDURANT,   T_BELLIQUEUX, T_BORNE }},
};
SpeciesBuild species_default_build(SpeciesArchetype r){
    if (r>=0&&r<RACE_COUNT) return ROSTER[r];
    SpeciesBuild empty = {{ T_PROLIFIQUE, T_FRONDEUR, T_ADAPTABLE }};
    return empty;
}

/* ===================================================================== */
/* BUDGET, VALIDATION, LEVIERS                                            */
/* ===================================================================== */
int build_budget(const SpeciesBuild *b){
    int sum = 0;
    for (int c=0;c<CAT_COUNT;c++){
        TraitId t=b->trait[c];
        if (t>=0&&t<TRAIT_COUNT) sum += TRAITS[t].pts;
    }
    return 1 - sum;   /* départ +1 ; équilibré quand Σ pts == 1 */
}
bool build_is_balanced(const SpeciesBuild *b){ return build_budget(b)==0; }

bool build_is_valid(const SpeciesBuild *b){
    for (int c=0;c<CAT_COUNT;c++){
        TraitId t=b->trait[c];
        if (t<0||t>=TRAIT_COUNT) return false;
        if (TRAITS[t].cat != (TraitCategory)c) return false;   /* un trait par catégorie */
    }
    if (build_budget(b)!=0) return false;
    for (int i=0;i<CAT_COUNT;i++)
        for (int j=i+1;j<CAT_COUNT;j++)
            if (TRAITS[b->trait[i]].antonym == b->trait[j]) return false;   /* antonymes */
    return true;
}

SpeciesLeviers build_leviers(const SpeciesBuild *b){
    SpeciesLeviers L = {0};
    for (int c=0;c<CAT_COUNT;c++){
        TraitId t=b->trait[c];
        if (t<0||t>=TRAIT_COUNT) continue;
        const SpeciesLeviers *d=&TRAITS[t].lev;
        L.demographie  += d->demographie;  L.productivite += d->productivite;
        L.influence    += d->influence;    L.coercition   += d->coercition;
        L.capacite     += d->capacite;     L.permeabilite += d->permeabilite;
        L.arcane       += d->arcane;       L.derive       += d->derive;
        L.fracture     += d->fracture;
    }
    return L;
}
