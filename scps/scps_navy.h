#ifndef SCPS_NAVY_H
#define SCPS_NAVY_H
/*
 * scps_navy.h — LA FLOTTE (briefs mer §5 & coques §2)
 *
 * Trois coques + la quatrième par CONVERSION :
 *   GUERRE     escorte / interception / blocus (bordées) — l'entretien le plus lourd
 *   TRANSPORT  porte les armées : 1 transport = 10 paquets = 1 000 hommes
 *   MARCHAND   protège les flux (+5 %/coque, plafond 50 %) contre la pression pirate
 *   PIRATE     un marchand CONVERTI au chantier (éthos de razzia) — réversible
 *
 * La flotte FERME la chaîne morte : construire consomme RES_NAVAL_SUPPLIES +
 * bois + or AU MARCHÉ du port (la Scierie navale a enfin un débouché), et
 * l'entretien consomme des fournitures au fil de l'eau (la demande se VOIT au
 * marché : les prix tirent la production).
 *
 * Membrane : les lecteurs rendent des nombres tangibles (coques, paquets, or).
 */
#include "scps_world.h"
#include "scps_econ.h"
#include "scps_routes.h"

typedef enum { HULL_WAR=0, HULL_TRANSPORT, HULL_MERCHANT, HULL_PIRATE, HULL_COUNT } HullType;

/* L'ÉQUIPAGE (le warhost de la mer) : une coque se LÈVE sur la population du
 * port — 50 journaliers par marchand/transport/pirate, 100 par navire de
 * combat. Les marins quittent les bras de la région au chantier ; ils y
 * REVIENNENT si la coque pourrit à quai ; ils SOMBRENT avec elle au combat. */
#define NAVY_CREW_LIGHT 50
#define NAVY_CREW_WAR   100
int navy_hull_crew(HullType t);

/* Mission des navires de combat (coques §3) — v1 : la flotte PORTE, le combat
 * naval détaillé vient avec la passe interception/blocus. */
typedef enum { NAVY_RADE=0, NAVY_ESCORTE, NAVY_INTERCEPTION, NAVY_BLOCUS } NavyMission;

typedef struct {
    int   hull[HULL_COUNT];   /* coques par type */
    int   build_hull;         /* type en chantier (-1 = aucun) ; un chantier à la fois */
    float build_days;         /* jours restants du chantier */
    int   home_port;          /* région-rade (port réel) ; -1 = aucun */
    int   at_sea;             /* transports RÉSERVÉS par une traversée en cours */
    float starve_days;        /* jours d'entretien IMPAYÉ cumulés (décay au-delà d'un an) */
    float colony_cd;          /* jours avant la prochaine colonisation outre-mer */
    int   mission;            /* NavyMission (v1 : posée, lue par la passe course) */
    int   mission_target;     /* selon mission : pays (blocus) / route (escorte) / région (zone) */
    int   nest_region;        /* PIRATE : la région-mer du repaire (-1) — posé par la passe course */
    float raid_cd;            /* COURSE : jours avant le prochain raid de ce commanditaire */
    /* télémétrie (chronicle §10) */
    int   built_total;        /* coques bâties (cumul sim) */
    float supplies_eaten;     /* fournitures navales consommées (chantier + entretien) */
    int   raids_done, prises, navals, disarmed;   /* la course, mesurée */
    float loot_gold, blocus_days;
    int   crew;                /* marins embarqués (levés sur la pop du port)  */
    int   intercepts;          /* convois ennemis interceptés (coulés)         */
    long  drowned;             /* paquets d'armée NOYÉS par l'interception     */
} Navy;

typedef struct NavyState { Navy n[SCPS_MAX_COUNTRY]; } NavyState;

void navy_init(NavyState *ns);

/* Le port RÉEL : l'édifice Port (build.port) sur une région CÔTIÈRE. */
bool navy_region_is_port(const World *w, const WorldEconomy *econ, int region);
/* La meilleure rade du pays (capitale portuaire d'abord, sinon la plus peuplée).
 * -1 si le pays n'a aucun port. */
int  navy_best_port(const World *w, const WorldEconomy *econ, int cid);

/* Commande une coque au chantier de la meilleure rade : achète la recette AU
 * MARCHÉ (fournitures navales + bois [+ métal pour la guerre]) en OR du trésor
 * régional. false si pas de port / chantier occupé / trésor insuffisant. */
bool navy_order_build(NavyState *ns, const World *w, WorldEconomy *econ, int cid, HullType t);
/* CONVERSION marchand ↔ pirate au chantier (coques §2) — peu coûteuse,
 * réversible : c'est sa nature. false si pas de port / pas de coque à convertir. */
bool navy_convert(NavyState *ns, const World *w, WorldEconomy *econ, int cid, bool to_pirate);

/* Prix en OR de la recette d'une coque au marché de la rade (UI/IA : affichable). */
float navy_build_gold(const WorldEconomy *econ, int region, HullType t);
const char *navy_hull_name(HullType t);

/* Avance de dt jours : chantier, entretien (consomme les fournitures de la rade
 * et REGISTRE la demande — le marché voit la flotte), décay si l'entretien
 * meurt de faim (> 1 an : une coque pourrit par an). */
void navy_tick(NavyState *ns, const World *w, WorldEconomy *econ, float dt_days);

/* Capacité d'emport LIBRE en paquets de 100 (10 par transport, moins l'engagé). */
int  navy_transport_packets_free(const NavyState *ns, int cid);

/* LA COLONISATION OUTRE-MER (mer §8) : le réflexe d'expansion existant,
 * contraint par le champ — un pays porté + transport disponible colonise la
 * meilleure région côtière vierge JOIGNABLE (jours de mer ≤ seuil), le score
 * préférant ce que les courants rapprochent. À appeler au pas mensuel. */
int  navy_colonize_tick(NavyState *ns, const World *w, WorldEconomy *econ, float dt_days);

/* Jours de mer port-à-port entre les rades de deux régions (< 0 si impossible). */
float navy_sea_days_regions(const World *w, int reg_a, int reg_b);

/* LA COURSE (coques) : doctrine par éthos (Honneur/Dominateur convertissent,
 * Mercantile protège puis escorte, Ordre patrouille/bloque), nids en eaux
 * mortes, raids (1/10 - balafre 1 an - immunité 5 ans), saignée des routes
 * (marchands plafond 50 %, escorte sans plafond), blocus, et le VERDICT
 * anti-piraterie (désarmement). `player` est exclu de la doctrine auto.
 * Cadence : mensuelle (dt_days=30). */
struct DiploState;
void navy_course_tick(NavyState *ns, const World *w, WorldEconomy *econ,
                      struct DiploState *dp, RouteNetwork *rn, uint32_t *rng,
                      int player, float dt_days);

/* L'INTERCEPTION (coques §3, le job FINI) : les navires de combat en mission
 * INTERCEPTION forcent la bataille aux CONVOIS hostiles qui traversent — un
 * transport sans escorte est une proie ; l'armée coulée SOMBRE (paquets noyés).
 * À appeler au pas mensuel, après la course. */
struct Campaign;
void navy_interception_tick(NavyState *ns, struct Campaign *camp, const World *w,
                            WorldEconomy *econ, struct DiploState *dp, uint32_t *rng);

#endif /* SCPS_NAVY_H */
