/*
 * scps_econ.h — couche ÉCONOMIE & CLASSES SOCIALES (moteur, sans UI)
 *
 * Tout est intriqué autour de la POPULATION, répartie en trois strates :
 *
 *   LABORERS   — la masse. Fournit le travail (RGO + manufactures), demande
 *                des biens de base (vivres, étoffe, bois de feu).
 *   BOURGEOIS  — marchands/artisans. Possèdent et gèrent les manufactures,
 *                captent le profit, demandent des biens manufacturés.
 *   ELITES     — aristocratie. Vivent de la taxe et de la rente, produisent
 *                la TECH (recherche) et exigent des biens de luxe (joaillerie,
 *                étoffe précieuse, vin).
 *
 * Unité de simulation : la RÉGION (= le « marché régional » du cahier des
 * charges). Chaque région a :
 *   - une capacité d'extraction de matières premières (héritée de la
 *     géographie des provinces qui la composent) ;
 *   - des manufactures (laine → tissu, bois → papier, or → joaillerie…) ;
 *   - un entrepôt (stock par bien) et un PRIX par bien déterminé par la
 *     demande face au stock+offre.
 *
 * Boucle (econ_tick) :
 *   1. Extraction des matières premières (emploie des laborers).
 *   2. Manufacture : consomme intrants → produit biens finis (laborers +
 *      encadrement bourgeois).
 *   3. Revenus : salaires → laborers ; profit → bourgeois ; taxe → elites.
 *   4. Demande : besoin par tête × population, par strate.
 *   5. Marché : prix = base × demande/(stock+offre) ; allocation au budget ;
 *      satisfaction par strate.
 *   6. Mise à jour : stock reporté, satisfaction → croissance de pop, elites
 *      convertissent richesse+satisfaction en tech.
 */
#ifndef SCPS_ECON_H
#define SCPS_ECON_H

#include "scps_types.h"

/* ---- Strates sociales ------------------------------------------------- */
typedef enum {
    CLASS_LABORER = 0,
    CLASS_BOURGEOIS,
    CLASS_ELITE,
    CLASS_COUNT
} SocialClass;

typedef struct {
    float pop;            /* effectif */
    float wealth;         /* trésor accumulé (monnaie) */
    float satisfaction;   /* [0..1] : fraction des besoins couverts au dernier tick */
} PopStratum;

/* ---- Manufactures (transforment matières premières → biens finis) ------ */
typedef enum {
    BLD_TEXTILE = 0,   /* laine   → tissu               */
    BLD_SAWMILL,       /* bois    → fournitures navales */
    BLD_PAPERMILL,     /* bois    → papier              */
    BLD_WINERY,        /* sucre   → vin                 */
    BLD_JEWELER,       /* or+métal précieux → joaillerie (bien d'élite) */
    BLD_WEAVER_LUX,    /* tissu   → étoffe précieuse    */
    BLD_TYPE_COUNT
} BuildingType;

typedef struct {
    BuildingType type;
    float        level;     /* capacité (échelle de production) */
    float        workers;   /* emploi effectif au dernier tick  */
} Building;

#define ECON_MAX_BLD 6      /* une manufacture de chaque type par région */

/* ---- Économie d'une région -------------------------------------------- */
typedef struct {
    PopStratum strata[CLASS_COUNT];

    float      raw_cap[RES_COUNT];   /* extraction max/tick par matière première */
    Building   bld[ECON_MAX_BLD];
    int        n_bld;

    float      stock [RES_COUNT];    /* entrepôt régional */
    float      price [RES_COUNT];    /* prix de marché courant */
    float      demand[RES_COUNT];    /* demande agrégée (dernier tick) */
    float      supply[RES_COUNT];    /* offre agrégée (dernier tick) */

    float      treasury;             /* taxe captée par les élites (cumul) */
    float      tech;                 /* recherche cumulée */
    float      gdp;                  /* valeur produite au dernier tick */
    float      satisfaction;         /* satisfaction générale [0..1] */
    bool       active;               /* région terrestre peuplée */
} RegionEconomy;

/* Conteneur — possédé par l'appelant, séparé du World pour ne pas alourdir
 * son ABI. */
typedef struct {
    RegionEconomy region[SCPS_MAX_REG];
    int           n_regions;
    int           tick;
} WorldEconomy;

/* ---- API -------------------------------------------------------------- */

/* Initialise pops, capacités d'extraction et manufactures à partir de la
 * géographie/ressources du monde déjà généré. */
void econ_init(WorldEconomy *e, const World *w);

/* Avance la simulation d'un pas (un « tour »). */
void econ_tick(WorldEconomy *e);

/* Affiche un tableau récapitulatif d'une région sur stdout. */
void econ_print_region(const WorldEconomy *e, const World *w, int region_id);

/* Affiche un sommaire monde (totaux pop/tech/PIB + top régions). */
void econ_print_summary(const WorldEconomy *e, const World *w);

/* Libellés */
const char *social_class_name(SocialClass c);
const char *building_name(BuildingType b);

#endif /* SCPS_ECON_H */
