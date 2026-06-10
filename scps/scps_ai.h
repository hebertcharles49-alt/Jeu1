#ifndef SCPS_AI_H
#define SCPS_AI_H
/*
 * scps_ai.h — LA BOUCLE DE DÉCISION IA (§13.1) : un lecteur de coordonnées qui
 *             choisit des leviers.
 *
 * Le point dur. Une IA qui n'a AUCUN privilège : elle LIT l'état (les mêmes
 * sorties que la membrane traduit en mots pour le joueur) et AGIT par la MÊME
 * couche d'action que le joueur (scps_agency / scps_routes / scps_diplo). Pas
 * de triche, pas de bonus caché, pas de script « si tour==42 alors attaque ».
 *
 * La PERSONNALITÉ n'est pas codée par-faction : elle ÉMERGE de la fiche
 * culturelle (PopCulture). On dérive une fois des POIDS depuis les axes/traits :
 *   valeurs (haut=Dominateur)      → appétit de conquête   (w_expand)
 *   inverse valeurs × trait éco     → appétit de commerce   (w_trade)
 *   éthos bâtisseur (Bureaucrate)   → appétit de bâtir du K (w_build)
 *   credo prosélyte (Purificateur)  → guerre sainte         (w_faith)
 *   arcane                          → pente faustienne       (w_faustian)
 * Deux voisins du même moule reçoivent un JITTER (graine) : ils ne jouent pas à
 * l'identique. Le même ai_step pilote tout le monde ; seuls les poids diffèrent.
 *
 * LE FREIN. Une coordonnée lue gouverne tout : la pression de consolidation.
 * Un acteur dont la SI tombe, dont la fragilité monte, ou dont la diversité
 * interne D∞ dépasse sa capacité K (surextension) CESSE d'attaquer et consolide
 * (paix, K). C'est ce qui empêche l'IA de se suicider en conquêtes — et ce qui
 * rend lisible « cet empire a trop avalé, il digère ».
 */
#include "scps_world.h"
#include "scps_econ.h"
#include "scps_prosperity.h"
#include "scps_legitimacy.h"
#include "scps_agency.h"
#include "scps_routes.h"
#include "scps_diplo.h"
#include "scps_tech.h"
#include <stdint.h>

/* ---- LA VUE : ce que l'IA LIT (coordonnées déjà calculées, zéro privilège) -- */
typedef struct {
    float SI;            /* stabilité interne [0..10] */
    float fragilite;     /* part de l'ordre tenue par la contrainte [0..10] */
    float fracture;      /* sécession latente */
    float L;             /* légitimité agrégée [0..10] */
    float K;             /* capacité EFFECTIVE (tech+race+bâti) */
    float Dinf_interne;  /* diversité interne max — le COÛT d'avaler du lointain */
    float PE;            /* prospérité réalisée */
    float tresor;        /* trésor disponible (somme régionale) */
    float food;          /* marge d'infrastructure alimentaire */
    float armee;         /* puissance militaire projetable */
    /* NOUVEAU — les BESOINS (l'IA était aveugle à ce qui manque) */
    Resource chain_gap;  /* intrant manquant d'un raffineur présent (RES_NONE si aucun) */
    Resource demand_gap; /* bien d'un panier de classe non comblé (variante culturelle comprise) */
    Resource strat_gap;  /* matière stratégique absente du pays (salpêtre/fer céleste/cristal) */
    float    gap_acuity; /* gravité agrégée du manque [0..1] */
    float    take_pressure; /* acuité d'un bien NON-productible localement → ne reste que PRENDRE/COMMERCER [0..1] */
    float    ethos_fracture; /* fracture de VALEURS interne (factions opposées) [0..1] — frein interne (§6) */
} AiView;

/* ---- Compteurs (preuve d'émergence : on tally par acteur) ------------------ */
typedef struct {
    int wars;            /* guerres DÉCLARÉES */
    int conquests;       /* régions conquises */
    int routes;          /* routes ouvertes */
    int builds_k;        /* édifices institutionnels (→ K) bâtis — RÉFORME */
    int builds_h;        /* citadelles (→ H) bâties sous la crise — SERRER (Ordre de Fer) */
    int builds_other;    /* greniers/marchés (food/PE) bâtis */
    int consolidations;  /* paix faites sous le frein (digestion) */
    int techs;           /* technologies recherchées (l'arbre vivant) */
    int techs_faustian;  /* dont des bouts FAUSTIENS (la pente arcanique) */
    int relocations;     /* ensemencements de pop pour combler une pénurie (peupler sa province-ressource) */
} AiStats;

/* ---- L'ACTEUR : une personnalité (poids de la fiche) + un rythme ----------- */
typedef struct {
    int      cid;            /* pays piloté */
    int      home_region;    /* région-capitale (où l'on bâtit) */

    /* Personnalité EFFECTIVE — la résultante des factions-éthos (§3), qui GLISSE
     * quand la composition change : socle de la culture régnante, MODULÉ par l'écart
     * entre le penchant du peuple et celui du trône (conquérir des orques monte la
     * conquête). Lue partout dans le moteur (agression, recherche, casus belli). */
    float    w_expand;       /* conquête    (faction Conquérants) */
    float    w_trade;        /* commerce    (faction Marchands) */
    float    w_build;        /* bâtir du K  (faction Légistes) */
    float    w_faith;        /* prosélytisme(faction Gardiens) */
    float    w_faustian;     /* pente arcanique (faction Transgresseurs) */
    float    w_base[5];      /* socle figé à l'init (culture du trône + jitter) — la résultante module ça */

    /* Pression accumulée : un seau qui se vide en ACTION — déterministe, ∝ poids
     * (un modèle de tension qui monte jusqu'à se décharger en levier). */
    float    credit_trade, credit_build, credit_war, credit_consolidate;

    /* Cadences DÉCALÉES (jour du prochain réveil) + hystérésis de paix. */
    int      next_econ_day;
    int      next_strat_day;
    int      next_research_day;  /* cadence de RECHERCHE (l'arbre de tech) */
    int      next_reloc_day;     /* §reloc : prochaine relocalisation autorisée (ensemencer, pas pomper) */
    int      next_interior_day;  /* §leviers : cadence des leviers intérieurs (mater/former/purger) */
    int      next_embargo_day;   /* §leviers : cadence de la guerre commerciale (Mercantile) */
    int      next_purge_ok_day;  /* §leviers : la purge est RARE — long verrou par pays */
    int      peace_lock_until;   /* après une consolidation : pas de guerre avant */
    bool     can_enslave;        /* §4c : l'Économie servile (TECH_ESCLAVAGE) débloquée ? */
    bool     has_creuset;        /* §leviers : Droit d'intégration (TECH_INTEGRATION) — forme mieux */

    uint32_t rng;            /* graine perso (jitter, départage) */
    AiStats  stats;
} AiActor;

/* Lie un acteur à un pays : calcule home_region, dérive les poids depuis la
 * fiche de la culture-capitale, décale les cadences (seed = jitter). */
void   ai_actor_init(AiActor *a, const World *w, const WorldEconomy *econ,
                     int cid, uint32_t seed);

/* Dérive la personnalité depuis la fiche (publique pour le banc d'essai). */
void   ai_derive_weights(AiActor *a, const PopCulture *self);

/* Lit l'état du pays dans une vue (aucun privilège : les mêmes coordonnées que
 * la membrane traduit pour le joueur). */
AiView ai_observe(const WorldProsperity *wp, const World *w,
                  const WorldEconomy *econ, int cid);

/* LE FREIN de survie [0..1] : fragile (SI basse), surextension (D∞/K>1), tendu
 * (fragilité haute). Haut = il faut consolider, pas conquérir. */
float  ai_consolidation_pressure(const AiView *v);

/* Appétit agressif net [0..~1.5] = (w_expand + foi) × (1 − frein). Exposé pour
 * prouver, au niveau de la DÉCISION, que l'appétit suit la fiche. */
float  ai_aggression(const AiActor *a, const AiView *v);

/* Un tick (1 JOUR) : l'acteur dort jusqu'à ses cadences, lit l'état, choisit un
 * levier, l'exécute par la MÊME couche d'action que le joueur. */
void   ai_step(AiActor *a, World *w, WorldEconomy *econ, WorldProsperity *wp,
               WorldLegitimacy *wl, AgencyState *ag, RouteNetwork *rn,
               DiploState *diplo, int day);

/* RECHERCHE (1 JOUR) : à sa cadence, l'empire accumule des points (rendement
 * Savoir × population) et déverrouille UN nœud choisi par ses BUTS + le PENCHANT
 * de sa race + le FREIN (faustien évité quand on est fragile). Aucun « si race ».
 * L'ACCÈS de race (sa population) débloque les orphelines → diffusion par conquête. */
void   ai_research_step(AiActor *a, TechState *ts, const World *w,
                        const WorldEconomy *econ, const WorldProsperity *wp, int day);

/* Masque des races présentes dans la population de l'empire (sa propre race +
 * conquises/migrées) → l'accès aux techs orphelines. Exposé pour le banc d'essai. */
unsigned ai_race_access(const World *w, const WorldEconomy *econ, int cid);

/* §syncrétique — RAFRAÎCHIT le cercle d'un empire : recalcule la profondeur de contact
 * par archétype, la met en cache (ts->arch_depth, lu par la membrane) et loquette les
 * nœuds de diffusion atteints. Appelée par ai_research_step ET par le visualiseur (pour
 * que le cercle du pays affiché soit à jour à l'image, hors cadence de recherche IA). */
void ai_sync_refresh(const World *w, const WorldEconomy *econ, TechState *ts, int cid);
/* Population totale de l'empire (assiette de recherche & d'échelle de coût). */
float    ai_country_population(const World *w, const WorldEconomy *econ, int cid);

/* Revenu de SAVOIR (points de recherche) par JOUR : l'assiette de pop × le
 * rendement du Savoir·Production. Sert au bandeau (Savoir accumulable + flux). */
float    ai_research_income(const TechState *ts, float pop);

#endif /* SCPS_AI_H */
