#ifndef SCPS_POPSIM_H
#define SCPS_POPSIM_H
/*
 * scps_popsim.h — LA POPULATION PRÉCISE : race × culture × foi × CLASSE émergente
 *
 * Banc d'essai isolé (demo-first) du modèle de population fin. Aujourd'hui une
 * province porte des GROUPES (race + culture + foi) à classe FIXE, et la classe
 * n'émerge qu'en AGRÉGAT (scps_labor : capitale → Nobles, ateliers → Bourgeois).
 * Ici on prouve le modèle UNIFIÉ : chaque BANDE = une identité (race × culture ×
 * foi), et sa composition de CLASSE ÉMERGE des emplois — par paquets de 100, au
 * prorata de la taille de la bande. Promouvoir/rétrograder n'est jamais posé :
 * c'est la structure d'emplois (capitale + ateliers) qui sculpte le tissu social,
 * bande par bande.
 *
 * Membrane : on lit des nombres tangibles (effectifs, parts) et des mots (race,
 * culture, foi, classe), jamais une coordonnée SCPS. Module autonome : il ne
 * touche pas l'éco vivante ; l'intégration (porter ceci dans PopGroup) vient après.
 */
#include "scps_species.h"   /* SpeciesArchetype, species_name */
#include "scps_culture.h"   /* Ethos, ReligionBranch, ethos_name, religion_branch_name */

#define POPSIM_MAX_BANDS  24       /* race × culture × foi : assez pour une province riche */
#define POPSIM_PACK       100      /* on promeut/emploie par paquets de 100 (comme les armées) */

/* Les trois classes (mêmes que l'éco : Journaliers / Bourgeois / Nobles). */
typedef enum { POPCL_LABORER=0, POPCL_ARTISAN, POPCL_ELITE, POPCL_COUNT } PopClass;

/* Une BANDE de population : une identité race × culture × foi, dont l'effectif se
 * RÉPARTIT entre classes selon les emplois (by_class émerge, n'est jamais posé). */
typedef struct {
    SpeciesArchetype race;
    Ethos            culture;              /* la clé de culture (l'éthos) */
    ReligionBranch   faith;                /* la branche de foi */
    long             count;                /* effectif total de la bande */
    long             by_class[POPCL_COUNT];/* ÉMERGENT : combien dans chaque classe */
} PopBand;

typedef struct {
    PopBand band[POPSIM_MAX_BANDS];
    int     n_bands;
    /* La STRUCTURE D'EMPLOIS dont les classes émergent (lue, ici, à la main) : */
    int     cap_tier;                      /* tier de la capitale → emplois NOBLES = tier·100 */
    long    artisan_jobs;                  /* emplois d'ATELIER (pop de Bourgeois demandée) */
} PopSim;

/* ---- Cycle ------------------------------------------------------------ */
void popsim_init(PopSim *p);
/* Ajoute (ou fusionne) une bande race × culture × foi avec `count` âmes. Renvoie
 * l'indice de la bande, ou -1 si plein. */
int  popsim_add_band(PopSim *p, SpeciesArchetype race, Ethos culture,
                     ReligionBranch faith, long count);
long popsim_total(const PopSim *p);

/* L'ÉMERGENCE : la classe de chaque bande se recalcule des EMPLOIS (capitale +
 * ateliers), par paquets de 100, au prorata de la taille de la bande. Plus de
 * Nobles si la capitale monte ; retour Journalier si un emploi disparaît. */
void popsim_emerge(PopSim *p);

/* ---- Lecteurs (membrane : nombres + mots) ----------------------------- */
long  popsim_class_total(const PopSim *p, PopClass c);     /* effectif d'une classe, toutes bandes */
long  popsim_faith_total(const PopSim *p, ReligionBranch f);
long  popsim_race_total (const PopSim *p, SpeciesArchetype r);
/* Effectif d'une classe DANS une foi donnée (le croisement précis foi × classe). */
long  popsim_faith_class(const PopSim *p, ReligionBranch f, PopClass c);
const char *popclass_name(PopClass c);

#endif /* SCPS_POPSIM_H */
