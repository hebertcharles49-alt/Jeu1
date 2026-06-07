#ifndef SCPS_AGENCY_H
#define SCPS_AGENCY_H
/*
 * scps_agency.h — LA COUCHE D'AGENCY : actions, temps, bâtiments (§1-§2)
 *
 * tick = 1 JOUR ; une partie = 250 ans ≈ 91 250 jours. Le jour est l'atome ;
 * tout se joue sur des mois, des années, des décennies.
 *
 * Règle non négociable : une action est un LEVIER qui déplace une COORDONNÉE
 * (K, P, H…), jamais un bonus plat. Un bâtiment n'ajoute pas « +10% » : c'est
 * de la densité institutionnelle réalisée, accumulée dans RegionEconomy.build
 * (ProvBuild), que le moteur d'ordre (prosperity_tick) et la légitimité LISENT.
 *
 * Ce module est l'ÉCRIVAIN de ces accumulateurs ; prosperity/legitimacy les
 * relisent. Le joueur voit des bâtiments nommés (membrane), dessous K/H/P bouge.
 */
#include "scps_econ.h"        /* ProvBuild, WorldEconomy, Resource */
#include "scps_world.h"       /* World (biome) */
#include "scps_legitimacy.h"  /* WorldLegitimacy (défrichement ronge L) */

#define SCPS_DAYS_PER_YEAR 365
#define SCPS_GAME_YEARS    250

/* Édifices — chacun déplace une coordonnée (cf. ProvBuild). */
typedef enum {
    EDI_TRIBUNAL = 0, EDI_CHANCELLERIE, EDI_ACADEMIE,  /* → K (Académie aussi P) */
    EDI_GARNISON, EDI_FORTERESSE, EDI_CITADELLE,        /* → H (ronge L) */
    EDI_PORT, EDI_CARAVANSERAIL,                        /* → P */
    EDI_MARCHE, EDI_ENTREPOT,                           /* → PE local */
    EDI_GRENIER, EDI_IRRIGATION,                        /* → food */
    EDIFICE_COUNT
} Edifice;

typedef struct {
    const char *name;
    int         days;     /* durée de construction (l'arc de 250 ans) */
    ProvBuild   delta;    /* ce qu'il ajoute à la province à l'achèvement */
} EdificeDef;

const EdificeDef *edifice_def(Edifice e);
const char       *edifice_name(Edifice e);

/* Trois familles d'action de province (le motif s'étend). */
typedef enum { AGY_BUILD = 0, AGY_CLEAR, AGY_EXPLOIT } ActionKind;

/* Une action en cours (file par pays/province). */
typedef struct {
    ActionKind kind;
    int        region;
    int        param;     /* Edifice (BUILD) | Resource (EXPLOIT) | inutilisé (CLEAR) */
    int        days_total, days_done;
    bool       active;
} BuildOrder;

#define SCPS_MAX_BUILDS 512
typedef struct {
    BuildOrder order[SCPS_MAX_BUILDS];
    int        n;
    int        day;       /* compteur de partie (jours écoulés) */
} AgencyState;

void agency_init(AgencyState *a);
/* Met une action en file (false si pleine). */
bool agency_order_build  (AgencyState *a, int region, Edifice e);
/* §4 Défrichement : convertit la terre → food, dérive la SUBSISTANCE locale vers
 * l'agriculture (impérialisme culturel sur la terre), et ronge L en niche
 * forestière (les peuples de la forêt voient leur monde rasé). */
bool agency_order_clear  (AgencyState *a, int region);
/* §3 Exploitation : un aménagement (mine/carrière…) monte l'extraction d'une
 * ressource (matériaux pour bâtir/armer, stratégiques pour la tech/valeur). */
bool agency_order_exploit(AgencyState *a, int region, Resource res);

/* Avance de `days` jours : progresse les chantiers ; à l'achèvement, applique
 * l'effet (déplace une coordonnée que le moteur LIT). */
void agency_advance(AgencyState *a, World *w, WorldEconomy *econ,
                    WorldLegitimacy *wl, int days);
/* Nombre de chantiers actifs sur une région (pour l'UI). */
int  agency_active_in_region(const AgencyState *a, int region);
int  agency_year(const AgencyState *a);

#endif /* SCPS_AGENCY_H */
