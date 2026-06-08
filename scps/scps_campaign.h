#ifndef SCPS_CAMPAIGN_H
#define SCPS_CAMPAIGN_H
/*
 * scps_campaign.h — LES ARMÉES SONT SUR LA CARTE : la campagne dans le temps
 *
 * scps_army donnait les PRIMITIVES (déplacement §1, bataille §2, doctrine §3,
 * siège) ; scps_warhost faisait MOBILISER chaque pays (une force nationale posée
 * sur la capitale → mil_power). Mais ces forces ne BOUGEAIENT pas : la guerre
 * restait un diplo abstrait (score → budget → prix).
 *
 * Ce module pose l'ARMÉE DE CAMPAGNE : une force EXPÉDITIONNAIRE par pays, avec une
 * POSITION (une région), qui MARCHE de région en région (au pas du convoi, le
 * terrain décidant des jours — §1), ASSIÈGE une région ennemie en arrivant (14 j
 * si nue, jusqu'à 2 ans selon fortif/vivres/terrain — le siège), et LIVRE BATAILLE
 * (§2/§3, doctrine + phases + poursuite) quand deux armées hostiles se croisent.
 *
 * NON-INVASIF : la campagne ne TOUCHE PAS la conquête abstraite. Elle LIT l'éco
 * (terrain, fortifications, vivres, propriété) et fait vivre les armées sur la
 * carte ; la réduction d'une région est ENREGISTRÉE (taken / région réduite),
 * jamais appliquée à econ->owner ici — l'intégration (et l'UI §4) viendront
 * ensuite. Les prix/volume de conquête restent intacts.
 *
 * Granularité : la RÉGION (on réutilise econ->adj, la même adjacence que la
 * conquête). Membrane : les lecteurs renvoient des nombres tangibles (paquets de
 * 100, identifiant de région, mots de phase).
 */
#include "scps_world.h"
#include "scps_econ.h"
#include "scps_army.h"
#include "scps_diplo.h"

/* Phase d'une armée de campagne. */
typedef enum { FA_IDLE = 0, FA_MARCH, FA_SIEGE, FA_PHASE_COUNT } FieldPhase;

/* Une armée expéditionnaire posée sur la carte. */
typedef struct {
    bool       active;      /* déployée ? */
    int        owner;       /* pays */
    int        loc;         /* région occupée */
    int        dest;        /* région-but (marche/siège) ; -1 = aucune */
    int        next;        /* prochaine région de la marche en cours ; -1 = aucune */
    FieldPhase phase;
    float      days_left;   /* jours restants de l'étape (marche) ou du siège */
    float      leg_days;    /* durée totale de l'étape en cours (pour l'attrition) */
    ArmyState  force;       /* la composition (détachement) */
    /* journal (UI/IA) */
    int        taken;       /* régions RÉDUITES (sièges menés à terme) */
    int        legs;        /* étapes de marche franchies */
    int        battles;     /* batailles livrées */
} FieldArmy;

typedef struct {
    FieldArmy army[SCPS_MAX_COUNTRY];   /* une force expéditionnaire par pays */
    int       n_regions;
    /* table de terrain par région (bâtie à l'init depuis le World) */
    Biome     reg_biome [SCPS_MAX_REG];
    float     reg_height[SCPS_MAX_REG];
    bool      reg_river [SCPS_MAX_REG];   /* un cours d'eau notable à franchir (pénalité de choc) */
} Campaign;

/* Bâtit la table de terrain par région et remet les armées à zéro. */
void campaign_init(Campaign *c, const World *w, const WorldEconomy *econ);

/* Ordonne à la force expéditionnaire de `owner` de partir de `from_region` vers
 * `target_region` (région ennemie à réduire) en portant une COPIE de `src_force`
 * (p.ex. l'armée mobilisée du warhost). Calcule l'itinéraire (BFS sur l'adjacence
 * des régions praticables). Renvoie false si la cible est injoignable par terre
 * ou la force vide. */
bool campaign_order(Campaign *c, const WorldEconomy *econ, int owner,
                    int from_region, int target_region, const ArmyState *src_force);

/* Avance toutes les armées de `dt_days` jours : la marche (§1) étape par étape,
 * le siège à l'arrivée, la bataille (§2/§3) quand deux forces hostiles (en
 * guerre, lu de `dp`) partagent une région. NE MODIFIE PAS econ (lecture seule) :
 * la propriété des régions reste la vérité de la conquête abstraite. `rng` =
 * graine xorshift avancée en place. */
void campaign_tick(Campaign *c, const World *w, const WorldEconomy *econ,
                   const DiploState *dp, uint32_t *rng, float dt_days);

/* ---- Lecteurs (membrane : tangibles) ---------------------------------- */
bool        campaign_active       (const Campaign *c, int owner);
int         campaign_location     (const Campaign *c, int owner);  /* région ou -1 */
FieldPhase  campaign_phase        (const Campaign *c, int owner);
long        campaign_units        (const Campaign *c, int owner);  /* paquets de 100 */
int         campaign_taken        (const Campaign *c, int owner);  /* régions réduites */
const char *campaign_phase_name   (FieldPhase ph);

/* Composition d'une armée par GRAND TYPE d'arme (paquets de 100) — pour le survol
 * de l'UI §4 (« cav / inf / arch »). Tangible, jamais de coordonnée SCPS. */
typedef struct { long infanterie, archers, cavalerie, mages, total; } ArmyComposition;
ArmyComposition campaign_composition(const Campaign *c, int owner);

/* Mot de TAILLE d'une troupe (asymétrie d'information : on montre ce MOT pour une
 * armée ennemie, pas son décompte exact). « éclaireurs » → « horde ». */
const char *army_host_word(long paquets);

/* ---- RENFORT (« remplir ») — recompléter une armée en TERRITOIRE AMI -------- */
/* Peut-on renforcer l'armée de `owner` ? (active ET la région où elle se tient lui
 * appartient — on ne se renforce que chez soi). */
bool campaign_can_refill(const Campaign *c, const WorldEconomy *econ, int owner);
/* Coût d'un renfort (+1 paquet de 100 par type d'unité présent) : `men` hommes
 * levés, `mat` matériaux pour les armes (achetés au marché, or si manque). Lecture. */
void campaign_refill_cost(const Campaign *c, int owner, long *men, long *mat);
/* RENFORCE l'armée : +1 paquet par type d'unité (fabrique l'arme en pompant le
 * marché si besoin, lève la pop), payé sur l'éco `labor`. Renvoie les paquets ajoutés. */
int  campaign_refill(Campaign *c, int owner, LaborEcon *labor);

#endif /* SCPS_CAMPAIGN_H */
