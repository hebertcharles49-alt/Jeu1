#ifndef SCPS_LABOR_H
#define SCPS_LABOR_H
/*
 * scps_labor.h — L'ÉCONOMIE DES POPULATIONS : main-d'œuvre, jobs, matériaux, marché
 *
 * « La pop doit servir à quelque chose. » Elle est la MAIN-D'ŒUVRE : tout
 * consomme de la pop par paquets de 100. Deux principes non négociables :
 *   1. La prod scale sur les JOBS REMPLIS, jamais sur le nombre de bâtiments.
 *      Un bâtiment vide ne produit rien ; un bâtiment amélioré (plus de slots)
 *      rempli produit plus. Le bâtiment est un contenant ; le job produit.
 *   2. Les sorties de bâtiment LISENT la géographie (le carrefour, la ressource,
 *      la fertilité déjà là), elles ne sont JAMAIS posées à la main. Un marché
 *      au désert dit « +0.3 » ; le même en région florissante « +15 ».
 *
 * Module autonome (sa propre comptabilité), mais qui RELIT la géographie du
 * worldgen partagé (Province/Cell) — la discipline du projet : on lit la
 * coordonnée, on n'assigne pas un modificateur.
 */
#include "scps_world.h"

#define POP_PER_SLOT            100   /* un slot d'emploi = 100 pop */
#define LAB_BUILDINGS_PER_PROV  6     /* 6 emplacements de bâtiment / province (§9) */
#define LAB_MAX_PROV            64

/* ---- Classes sociales : taxes + troupes (§8) -------------------------- */
typedef enum { LAB_LABORER=0, LAB_ARTISAN, LAB_ELITE, LAB_CLASS_COUNT } LaborClass;

/* ---- Ressources (topbar + secondaires + stratégiques) (§3) ------------ */
typedef enum {
    LR_FOOD=0, LR_GOLD, LR_MATERIALS,                       /* topbar */
    LR_BOIS, LR_ARGILE, LR_CALCAIRE, LR_PIERRE, LR_METAL,   /* brutes secondaires */
    LR_OUTILS,                                              /* stratégique */
    LR_COUNT
} LRes;

/* ---- Types de bâtiment — chacun LIT une coordonnée géo (§5) ----------- */
typedef enum {
    LB_NONE=0,
    LB_COLLECTOR,   /* nourriture : base plancher + bonus de FERTILITÉ */
    LB_GRANARY,     /* nourriture : lit la FERTILITÉ */
    LB_MARKET,      /* or : lit le FLUX commercial (carrefour) de la province */
    LB_SAWMILL,     /* bois   : lit la présence de forêt */
    LB_CLAYPIT,     /* argile : lit la présence de fleuve/marais */
    LB_QUARRY,      /* calcaire/pierre : lit le relief */
    LB_MINE,        /* métal  : lit la présence de minerai */
    LB_WORKSHOP,    /* matériaux : COMBINE des intrants (chaîne §4) */
    LB_TYPE_COUNT
} LBuildType;

typedef struct {
    LBuildType type;
    int        level;        /* 0..5 (six niveaux) */
    int        jobs_filled;  /* slots occupés (≤ capacité du niveau) */
} LBuilding;

typedef struct {
    int       prov;          /* id de province (pour les lectures géo) */
    bool      colonized;     /* possédée (développement plein) vs marge exploitée */
    long      pop;           /* le POOL démographique TOTAL (jamais réduit par l'emploi) */
    long      pop_by_class[LAB_CLASS_COUNT];   /* ÉMERGE des emplois (§capitale §4), jamais posé */
    long      pop_in_army;   /* enrôlés (paquets de 100) — assignés, PAS retirés du pool */
    LBuilding bld[LAB_BUILDINGS_PER_PROV];
    int       n_bld;
    /* ---- La CAPITALE (§capitale) : l'ossature administrative, OBLIGATOIRE et non
     * destructible, sur un emplacement DÉDIÉ (hors des 6 slots). AMÉLIORABLE : on la
     * paie de tier en tier (ressources de plus en plus précieuses) ; la POP ne fait
     * que PLAFONNER le tier atteignable. Elle délivre logement + services +
     * productivité, AU PRORATA des paquets de Nobles en poste à l'administration. */
    int       cap_tier;      /* tier BÂTI/payé (1..7) — plafonné par la pop */
    long      house_cap;     /* LOGEMENT délivré (plafond de pop) ; 0 si aucun Noble en poste */
    long      serv_cap;      /* SERVICES délivrés ; 0 si aucun Noble en poste */
    float     prod_mult;     /* PRODUCTIVITÉ locale (1 + 5 %/tier servi) */
} LProvince;

/* La pop est un POOL : un job ou un enrôlement est une AFFECTATION (une
 * référence dans le pool), pas un retrait. Une population engagée continue de se
 * reproduire — elle ne disparaît du pool qu'à la mort (événements). La topbar
 * montre la répartition : libre · en job · en armée (somme = total). */
typedef struct { long total, free, in_jobs, in_army; } PopBreakdown;

/* ---- Le marché : prix GÉNÉRÉ par la demande (§7) ---------------------- */
typedef struct { float demand, supply, price; } LMarket;

typedef struct {
    LProvince prov[LAB_MAX_PROV];
    int       n_prov;
    long      stock[LR_COUNT];
    long      flow [LR_COUNT];   /* net/jour du dernier tick (pour la topbar) */
    LMarket   market;
    long      treasury;         /* or */
    /* géo précalculée par province du monde (relue, jamais posée) */
    float     g_fert[SCPS_MAX_PROV];   /* fertilité moyenne [0..1] */
    float     g_flow[SCPS_MAX_PROV];   /* flux commercial (carrefour) */
    float     g_pres[SCPS_MAX_PROV][LR_COUNT]; /* présence de ressource [0..1] */
} LaborEcon;

/* ---- Cycle de vie ----------------------------------------------------- */
void labor_init(LaborEcon *e, const World *w);            /* relit la géo ; éco vide */
/* Le départ canonique (§1) : 4000 pop, 2 ateliers + 2 collecteurs, 200/200/200. */
void labor_seed_start(LaborEcon *e, int prov0);
void labor_tick(LaborEcon *e);                            /* la boucle §11 */

/* ---- INTÉGRATION : seeder depuis un VRAI pays du monde ---------------- *
 * Pose une province de main-d'œuvre par région possédée et peuplée (pop lue de
 * l'économie existante, bâtiments choisis sur la GÉO réelle de chaque province).
 * L'économie des populations devient alors celle d'un pays du monde. */
void  labor_seed_from_world(LaborEcon *e, const World *w, const WorldEconomy *econ, int cid);
/* Indice de prospérité [0..10] que l'économie PRODUIT (sécurité alimentaire +
 * revenu et matériaux par tête). Se projette sur la métrique Prospérité 0-100
 * (la même que voit le joueur) — le pont avec la membrane/les métriques. */
float labor_prosperity_index(const LaborEcon *e);

/* ---- Jobs & niveaux (§9) ---------------------------------------------- */
int  building_job_capacity_pop(int level);   /* capacité CUMULÉE en pop (max niv.5 = 2200) */
int  building_job_slots(int level);          /* en slots (= pop / 100) */

/* ---- La CAPITALE & la MOBILITÉ DE CLASSE (§capitale) ------------------ *
 * La capitale est OBLIGATOIRE (tier 1 dès la fondation) et AMÉLIORABLE : la pop
 * DÉBLOQUE le tier (plafond), une recette de plus en plus PRÉCIEUSE le PAIE. Elle
 * délivre logement + services + productivité AU PRORATA des paquets de NOBLES en
 * poste (paquets de 100). Les classes ne sont jamais posées : elles ÉMERGENT des
 * emplois (capitale → Nobles · ateliers → Bourgeois · le reste → Journaliers) ;
 * une pop monte/descend de classe — par 100 — pour suivre les emplois.           */
int  capitale_max_tier (long pop);            /* tier que la POP autorise : <2000→1 … 10000→7 */
const char *capitale_status(int tier);        /* le STATUT vient du TIER : Hameau … Métropole */
/* DÉFENSE provinciale PASSIVE apportée par la capitale : un niveau par tier (allonge
 * le SIÈGE comme un rempart — MAIS sans le bonus défenseur au combat). */
int  capitale_defense (int tier);
long capitale_admin_pop(int tier);            /* pop de Nobles employée à l'administration (tier·100) */
long capitale_housing  (int tier, long admin_pop); /* logement/service : min(paquets,tier)·1000 (gaté) */
float capitale_prodmult(int tier, long admin_pop); /* productivité : 1 + 0.05·min(paquets,tier) */
/* Coût d'amélioration vers `to_tier` (recette LRes de plus en plus précieuse). */
typedef struct { LRes a, b; long qa, qb; } CapCost;
CapCost capitale_upgrade_cost(int to_tier);
/* Améliore la capitale d'un tier SI la pop le débloque ET la recette est payable
 * (pompée au marché si manque). Renvoie true si l'amélioration a eu lieu. */
bool capitale_upgrade(LProvince *p, LaborEcon *e);
/* Fait ÉMERGER les classes des emplois (par 100) et délivre logement/services/
 * productivité (gatés par les paquets de Nobles en poste). N'achète RIEN (lecture). */
void capitale_mobility_tick(LProvince *p);

/* Pop SANS LOGEMENT / SANS SERVICE (au-delà de la capacité délivrée) : elle MONTE
 * l'agitation (surpeuplement & sous-service nourrissent la grogne). */
long  capitale_unhoused(const LProvince *p);  /* max(0, pop − logement) */
long  capitale_unserved(const LProvince *p);  /* max(0, pop − services) */
float capitale_unrest  (const LProvince *p);  /* part de pop mal logée/servie [0..1] → agitation */

/* ---- Sorties LUES de la géo (§5) — le hover montre ce +X réel --------- */
/* Sortie d'un bâtiment d'une province (potentiel : per-job × jobs remplis). */
float labor_building_output(const LaborEcon *e, int prov, int bld_idx, LRes *out_res);
/* Le marché lit le FLUX : désert ~+0.3 ; florissant ~+15. */
float labor_market_output(const LaborEcon *e, int prov, int jobs_filled);
float province_trade_flow(const LaborEcon *e, int prov);
float province_fertility (const LaborEcon *e, int prov);

/* ---- Nourriture & famine (§2) ----------------------------------------- */
long  labor_food_collected(const LaborEcon *e);   /* somme des collecteurs/greniers */
long  labor_food_consumed (const LaborEcon *e);    /* pop EMPLOYÉE × conso */
long  labor_food_balance  (const LaborEcon *e);    /* collecte − conso (net/jour) */

/* ---- Le marché & le prix dynamique (§7) ------------------------------- */
float labor_material_price(const LaborEcon *e);             /* prix temps réel (demande) */
long  labor_pump_market   (LaborEcon *e, long amount);     /* achète le manque → coût or */

/* ---- Population : le POOL et sa répartition (topbar) ------------------ */
long        labor_pop_total   (const LaborEcon *e);   /* le pool total */
long        labor_pop_employed(const LaborEcon *e);   /* Σ jobs_filled × 100 (affectés, pas retirés) */
long        labor_pop_in_army (const LaborEcon *e);
PopBreakdown labor_pop_breakdown(const LaborEcon *e); /* total = libre + en job + en armée */
void        labor_print_topbar(const LaborEcon *e);   /* or·nourriture·matériaux + pop libre/job/armée */

/* ---- Taxes par classe (§8) -------------------------------------------- */
float labor_taxes(const LProvince *p);

/* ---- Colonisation (§6) ------------------------------------------------ */
typedef struct { long materials, food; } ColonizeCost;
ColonizeCost labor_colonize_cost(const LaborEcon *e, int prov);
bool         labor_can_colonize (const LaborEcon *e, ColonizeCost cost);
bool         labor_colonize     (LaborEcon *e, int prov);

/* ---- Vitesse de développement de la pop (§10) ------------------------- */
/* Province pleinement colonisée (6 bâtiments) ET Prospérité 100 → ×1.15. */
float labor_pop_dev_speed(const LProvince *p, int prosperity_0_100);
bool  province_fully_colonized(const LProvince *p);

/* ---- Libellés --------------------------------------------------------- */
const char *lres_name  (LRes r);
const char *lbuild_name(LBuildType b);

#endif /* SCPS_LABOR_H */
