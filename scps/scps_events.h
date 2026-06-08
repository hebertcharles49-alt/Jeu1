#ifndef SCPS_EVENTS_H
#define SCPS_EVENTS_H
/*
 * scps_events.h — ÉVÈNEMENTS & ÂGES : la dynamique du monde (§ chocs/politique/ères)
 *
 * Approche EU4, mais rien d'aléatoire hors-sol — tout est ANCRÉ :
 *   - les CHOCS suivent la GÉOGRAPHIE (la tectonique qui a fait les montagnes,
 *     les rivières, la pluviométrie, les routes) ;
 *   - les évènements POLITIQUES/CULTURELS suivent la FICHE (éthos/sphère/credo) :
 *     un même état du monde produit un récit différent par culture ;
 *   - les ÂGES suivent l'ÉTAT DU MONDE : un âge n'arrive pas à date, il est
 *     RECONNU quand le monde atteint un état émergent (et déplace une coordonnée
 *     globale + ouvre un palier de tech).
 *
 * Membrane : les effets déplacent des COORDONNÉES (K/H/L/food…) ou des MÉTRIQUES
 * (agitation, influence) ; les TEXTES parlent en mots diégétiques — jamais un
 * nom SCPS. (Les champs de delta sont du code, pas de l'affichage.)
 */
#include "scps_world.h"
#include "scps_econ.h"
#include "scps_legitimacy.h"
#include "scps_prosperity.h"
#include "scps_statecraft.h"
#include "scps_routes.h"
#include "scps_tech.h"

/* ===================================================================== */
/* CADRE D'ÉVÈNEMENT (data-driven)                                        */
/* ===================================================================== */
typedef enum { EV_PROVINCE = 0, EV_COUNTRY, EV_WORLD } EvScope;

/* L'effet d'une option : un paquet de déplacements de coordonnées/métriques.
 * 0 = neutre ; pop_mult = 1 = neutre. Rien ici n'est un « +X % » de jeu : ce
 * sont des coordonnées que le moteur relit (build/L/agitation/treasury…). */
typedef struct {
    float d_K_inst;     /* institutions bâties (région)      */
    float d_H_coerc;    /* coercition bâtie (région)         */
    float d_food_cap;   /* nourriture/fertilité (région)     */
    float d_L;          /* légitimité (région ou pays)       */
    float d_coercion;   /* coercition vécue (région, [0..1]) */
    float d_agitation;  /* agitation (métrique, région)      */
    float pop_mult;     /* multiplie la population (région)  */
    float d_treasury;   /* trésor (région/capitale)          */
    float d_influence;  /* influence (pays)                  */
    /* Échelle monde (Âges) : */
    float d_C_global;   /* connectivité mondiale             */
    float d_breach;     /* pression de brèche mondiale       */
    int   unlock_branch;/* TechTheme ouvert (-1 = aucune)  */
    int   unlock_tier;  /* palier ouvert                     */
} EvEffect;

typedef struct {
    const char *label;   /* le choix, en mots diégétiques */
    const char *blurb;   /* ce qu'il fait, en mots (jamais un nom SCPS) */
    EvEffect    eff;
    float       ai_chance;/* poids du choix pour l'IA (la fiche l'a déjà sélectionné) */
} EvOption;

typedef struct EventCtx EventCtx;   /* fwd : bundle de pointeurs systèmes */

typedef struct {
    int         id;
    EvScope     scope;
    const char *name;                                 /* titre diégétique */
    bool      (*trigger)(const EventCtx *, int subject);/* lit géo/coords/métriques/fiche */
    float       mtth_days;                            /* temps moyen avant déclenchement */
    float     (*mtth_mod)(const EventCtx *, int subject);/* facteurs d'accélération (ou NULL) */
    EvOption    options[4];
    int         n_options;
} EventDef;

/* Identifiants (chocs géo puis politiques/culturels par fiche). */
typedef enum {
    EVID_QUAKE = 0, EVID_FLOOD, EVID_DROUGHT, EVID_FIRE, EVID_PLAGUE,
    EVID_INTEG_DOMINATEUR, EVID_INTEG_MERCANTILE, EVID_INTEG_BUREAUCRATE, EVID_INTEG_ANCIEN,
    EVID_SUCCESSION, EVID_SCHISM,
    EVID_COUNT
} EvId;

/* ===================================================================== */
/* ÂGES — déclenchés par une LECTURE du monde (§4)                        */
/* ===================================================================== */
typedef enum {
    AGE_COMMERCE = 0, AGE_REASON, AGE_EMPIRES, AGE_BREACH,
    /* Âges STRUCTURELS — lisent la crise & les idées, poussent les ENTRÉES du
     * moteur d'ordre (I/L/H), laissent le verdict §2.4 faire les conséquences.
     * Chaîne causale : les Lumières d'abord (société de masse), puis le fork
     * Soulèvements (le consentement renverse) ↔ Ordre de Fer (la poigne écrase). */
    AGE_LUMIERES, AGE_SOULEVEMENTS, AGE_ORDRE_FER,
    AGE_COUNT
} AgeId;

typedef struct {
    bool  dawned[AGE_COUNT];
    bool  tier_open[THM_COUNT][8];   /* paliers de tech ouverts par les âges */
    float breach_pressure;           /* l'endgame faustien mondial */
    float research_mult;             /* palier de la Raison : recherche plus vive */
    float integration_mult;          /* palier des Empires : intégration accélérée */
    int   last_dawned;               /* -1 ou AgeId du dernier avènement */
    int   days_elapsed;              /* temps de jeu écoulé (cumul des ticks) */
    int   last_dawn_year;            /* an du dernier avènement (rythme : 1 âge / 30 ans mini) */
} AgesState;

/* ===================================================================== */
/* L'ÉTAT DU MODULE                                                       */
/* ===================================================================== */
typedef struct {
    float relief;     /* fraction de relief tectonique (montagnes/volcans) [0..1] */
    float river01;    /* débit fluvial max normalisé [0..1] */
    float rainfall;   /* pluviométrie moyenne [0..1] */
    float temp;       /* température moyenne [0..1] */
    float arid;       /* fraction de biomes arides [0..1] */
    float forest;     /* fraction de biomes boisés [0..1] */
    float lowland;    /* fraction de basses terres inondables [0..1] */
    int   cells;
} RegionGeo;

typedef struct {
    RegionGeo   geo[SCPS_MAX_REG];   /* la géo RELUE par région (le worldgen exposé) */
    AgesState   ages;
    uint32_t    rng;
    const char *last_name;           /* dernier évènement déclenché (UI/journal) */
    int         last_id;
    int         n_fired;             /* total déclenché (debug) */
} EventsState;

/* ===================================================================== */
/* API                                                                   */
/* ===================================================================== */
/* Relit la géo du worldgen en agrégats par région (failles via le relief,
 * rivières, pluie, aridité, forêt). À appeler après gen_population. */
void events_init(EventsState *ev, const World *w, uint32_t seed);

/* La boucle (§5) : chocs géo (à risque) + évènements (mtth par sujet, trigger
 * lit la fiche) + scan d'âges périodique. Mute le monde via les leviers. */
void world_events_tick(EventsState *ev, World *w, WorldEconomy *econ,
                       WorldLegitimacy *wl, WorldProsperity *wp, Statecraft *sc,
                       RouteNetwork *rn, const TechState ts[], int days);

/* ---- Lecteurs de RISQUE géo (relisent la géo ; 0 = la géo l'interdit) -- */
float events_quake_risk  (const EventsState *ev, int region);
float events_flood_risk  (const EventsState *ev, int region);
float events_drought_risk(const EventsState *ev, int region);
float events_fire_risk   (const EventsState *ev, int region);

/* Applique un choc géo précis à une région (effets ancrés). Pour la sim et le
 * banc d'essai (déterministe). */
void  events_strike(EventsState *ev, World *w, WorldEconomy *econ,
                    WorldLegitimacy *wl, WorldProsperity *wp, Statecraft *sc,
                    int region, EvId shock);

/* Peste : diffusion BFS le long des ROUTES ouvertes depuis une région-foyer.
 * Un empire fermé (sans routes) est épargné. Renvoie le nb de régions touchées. */
int   events_plague_spread(EventsState *ev, World *w, WorldEconomy *econ,
                           WorldLegitimacy *wl, Statecraft *sc, RouteNetwork *rn,
                           int seed_region);

/* ---- Évènements politiques/culturels : la VARIANTE par la fiche -------- */
/* Renvoie l'EvId de l'évènement d'intégration qui MATCHE la fiche du pays
 * propriétaire d'une région instable (ou -1). Même état → variante par éthos. */
int          events_match_political(const EventsState *ev, World *w, WorldEconomy *econ,
                                    WorldLegitimacy *wl, Statecraft *sc, int region);
const EventDef *event_def(int evid);

/* ---- Âges : scan d'interprétation du monde ---------------------------- */
/* Évalue les triggers d'âge ; fait advenir tout âge nouvellement éligible
 * (palier + coordonnée globale). Renvoie true si un âge est advenu. */
bool  events_check_ages(EventsState *ev, World *w, WorldEconomy *econ,
                        WorldProsperity *wp, WorldLegitimacy *wl, const TechState ts[]);
bool  ages_dawned(const EventsState *ev, AgeId a);
bool  ages_tier_open(const EventsState *ev, TechTheme br, int tier);
float ages_breach_pressure(const EventsState *ev);
const char *age_name(AgeId a);
/* Verdict du MOTEUR agrégé : combien de pays sont en mode révolutionnaire
 * (SI<5 & pression≥fracture) — la masse critique des Soulèvements, sans aucun
 * code de révolution dédié. */
int   events_count_revolutionary(const World *w, const WorldProsperity *wp);

/* ---- Garde-fou membrane : aucun nom SCPS dans les textes joueur -------- */
bool  events_text_clean(void);

#endif /* SCPS_EVENTS_H */
