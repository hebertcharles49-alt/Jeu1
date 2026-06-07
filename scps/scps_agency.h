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
#include "scps_econ.h"   /* ProvBuild, WorldEconomy */

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

/* Une construction en cours (file par pays/province). */
typedef struct {
    int     region;
    Edifice type;
    int     days_total, days_done;
    bool    active;
} BuildOrder;

#define SCPS_MAX_BUILDS 512
typedef struct {
    BuildOrder order[SCPS_MAX_BUILDS];
    int        n;
    int        day;       /* compteur de partie (jours écoulés) */
} AgencyState;

void agency_init(AgencyState *a);
/* Met une construction en file (false si pleine). */
bool agency_order_build(AgencyState *a, int region, Edifice e);
/* Avance de `days` jours : progresse les chantiers ; à l'achèvement, écrit le
 * delta dans econ->region[r].build (la coordonnée monte alors, lue au tick). */
void agency_advance(AgencyState *a, WorldEconomy *econ, int days);
/* Nombre de chantiers actifs sur une région (pour l'UI). */
int  agency_active_in_region(const AgencyState *a, int region);
int  agency_year(const AgencyState *a);

#endif /* SCPS_AGENCY_H */
