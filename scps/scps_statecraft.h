#ifndef SCPS_STATECRAFT_H
#define SCPS_STATECRAFT_H
/*
 * scps_statecraft.h — INFLUENCE, OPINION, DIPLOMATES & RÉVOLTE
 *
 * La couche d'ÉTAT diplomatique, au-dessus des lecteurs de scps_diplo. Trois
 * systèmes de jeu, tous ANCRÉS sur des coordonnées existantes (jamais un +X
 * libre) :
 *
 *   INFLUENCE  (0-100, réserve à inertie) — la réputation : monte avec la
 *              prospérité, la taille et les accords TENUS, chute aux trahisons,
 *              s'érode sans entretien. Elle plafonne le nombre de missions
 *              diplomatiques simultanées (le « prestige » d'EU4, branché au réel).
 *
 *   OPINION    (−100..+100 par paire) — l'opinion d'un pays envers un autre,
 *              PROJECTION des lecteurs de relation (parenté/sphère, menace,
 *              complément, schisme) + l'historique (la trahison crève l'opinion,
 *              elle remonte lentement vers sa cible).
 *
 *   DIPLOMATES (vivier limité) — un personnel ; chaque mission OCCUPE un agent
 *              pendant des JOURS (modèle de temps), puis applique son effet par
 *              la couche d'action (route, alliance, intégration). L'Influence
 *              fixe la taille du vivier.
 *
 * Et la RÉVOLTE : une agitation de province SOUTENUE au-dessus du seuil bascule
 * en révolte (le consentement s'effondre) — apaisée par la stabilité, la
 * garnison (H) et la légitimité, exactement comme la membrane le LIT.
 *
 * Membrane : ce module est SIM (il lit des flottants SCPS comme diplo/prosperity).
 * Son API ne renvoie que des nombres de JEU (int 0-100, −100..100) — jamais un
 * flottant SCPS ni son nom. Le renderer peut donc l'appeler sans franchir la cloison.
 */
#include "scps_world.h"
#include "scps_econ.h"
#include "scps_prosperity.h"
#include "scps_legitimacy.h"
#include "scps_diplo.h"
#include "scps_routes.h"

/* ---- Diplomates -------------------------------------------------------- */
#define SC_MAX_DIPLOMATS   12
#define SC_BASE_DIPLOMATS  3      /* vivier de départ (avant bonus d'Influence) */

typedef enum {
    DIP_IDLE = 0,
    DIP_RELATIONS,   /* ~180 j — améliore l'opinion (lent, continu)            */
    DIP_ALLIANCE,    /* ~30 j  — ouvre une alliance (si l'opinion le porte)    */
    DIP_INTEGRATE,   /* ∝ D∞   — accélère la montée de Légitimité d'une région */
    DIP_ROUTE        /* ~90 j  — ouvre une route commerciale                   */
} DipMission;

typedef struct {
    DipMission mission;
    int        target;       /* pays (RELATIONS/ALLIANCE) | région (INTEGRATE/ROUTE) */
    int        home_region;  /* d'où part l'agent (capitale) */
    int        days_left;
} Diplomat;

typedef struct {
    Diplomat agents[SC_MAX_DIPLOMATS];
    int      count;          /* vivier disponible = base + bonus(Influence) */
} DiplomaticStaff;

/* ---- L'état de statecraft ---------------------------------------------- */
typedef struct {
    float           influence[SCPS_MAX_COUNTRY];                 /* 0..100, inertie */
    float           prestige [SCPS_MAX_COUNTRY];                 /* mémoire des accords (0..30) */
    float           opinion  [SCPS_MAX_COUNTRY][SCPS_MAX_COUNTRY];/* −100..100 */
    DiplomaticStaff staff    [SCPS_MAX_COUNTRY];
    float           agitation  [SCPS_MAX_REG];                   /* 0..100 soutenue */
    float           unrest_days[SCPS_MAX_REG];                   /* temps au-dessus du seuil */
    bool            revolt_fired[SCPS_MAX_REG];                  /* a basculé ce tick */
    int             n_countries;
} Statecraft;

void statecraft_init(Statecraft *sc, const World *w);

/* ---- Lecteurs (nombres de JEU, jamais un flottant SCPS) ---------------- */
int  statecraft_influence      (const Statecraft *sc, int cid);          /* 0..100 */
/* Variation d'INFLUENCE par JOUR (convergence vers le standing prospérité+taille+
 * prestige) — pour le bandeau (Influence accumulable + flux +N/j). */
float statecraft_influence_flux(const Statecraft *sc, const WorldEconomy *econ,
                                const WorldProsperity *wp, int cid);
int  statecraft_opinion        (const Statecraft *sc, int a, int b);     /* −100..100 */
int  statecraft_missions_cap   (const Statecraft *sc, int cid);          /* plafond simultané */
int  statecraft_missions_active(const Statecraft *sc, int cid);
int  statecraft_agitation      (const Statecraft *sc, int region);       /* 0..100 soutenue */
bool statecraft_revolt_fired   (const Statecraft *sc, int region);

/* ---- Événements qui déplacent l'Influence (la réputation suit l'acte) --- */
void statecraft_on_accord_kept(Statecraft *sc, int cid);   /* alliance tenue, paix honorée */
void statecraft_on_betrayal   (Statecraft *sc, int cid);   /* trahir un allié : chute nette */

/* ---- Missions : occupe un agent pendant des JOURS ---------------------- *
 * false si le vivier est saturé (plafond d'Influence) ou la cible invalide.  */
bool statecraft_send(Statecraft *sc, const World *w, const WorldEconomy *econ,
                     int cid, DipMission mission, int target);

/* ---- Un pas (jours) ---------------------------------------------------- *
 * Influence → standing (prospérité+taille+prestige) ; opinion → relation+histo ;
 * agents avancent et appliquent leur effet à l'échéance ; agitation soutenue →
 * révolte. econ/diplo/wl/rn peuvent être mutés (effets de mission, révolte). */
void statecraft_tick(Statecraft *sc, World *w, WorldEconomy *econ,
                     WorldProsperity *wp, WorldLegitimacy *wl,
                     DiploState *diplo, RouteNetwork *rn, int days);

const char *dip_mission_name(DipMission m);

#endif /* SCPS_STATECRAFT_H */
