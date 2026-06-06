/*
 * scps_trade.h — RÉSEAU DE COMMERCE INTER-RÉGIONAL
 *
 * Le marché régional (scps_econ) est autarcique : les surplus restent dans
 * leur région, les déficits dépriment la satisfaction même si la région
 * voisine crève sous la laine. Ce module connecte les régions adjacentes.
 *
 * Principe physique :
 *   - Deux régions sont liées si leurs cellules se touchent (4-connexe) OU
 *     si elles sont toutes deux côtières (route maritime).
 *   - Le COÛT DE TRANSPORT est une fraction de la valeur transportée.
 *     Il dépend du terrain : mer < rivière < plaine < montagne.
 *   - La CAPACITÉ (unités/tick/bien) croît avec la population totale des
 *     deux régions (davantage de marchands = davantage de flux).
 *   - L'ARBITRAGE : si |prix_A − prix_B| > coût_transport → des marchands
 *     achètent dans la région bon marché et revendent dans la chère jusqu'à
 *     ce que la marge disparaisse ou que la capacité soit saturée.
 *   - PRIX : convergent partiellement à chaque flux (lissage 20%).
 *
 * Hiérarchie future :
 *   marché régional (autarcie locale)
 *   → commerce inter-régional (ce module, intra-pays)
 *   → commerce inter-pays (grandes routes marchandes, à venir)
 */
#ifndef SCPS_TRADE_H
#define SCPS_TRADE_H

#include "scps_types.h"
#include "scps_econ.h"

/* ---- Lien commercial entre deux régions -------------------------------- */
typedef struct {
    int16_t ra, rb;           /* indices de régions (ra < rb) */
    float   transport_cost;   /* fraction de la valeur par unité transportée */
    float   capacity;         /* unités max/tick/bien */
    bool    sea_route;        /* route maritime (bi-côtière) */
    bool    river_route;      /* route fluviale (longe un fleuve commun) */
} TradeLink;

/* Enregistrement d'un flux de commerce sur un tick.
 * Utilisé pour l'affichage (balance commerciale, routes actives). */
typedef struct {
    int16_t  ra, rb;          /* ra → rb si volume>0, rb → ra si volume<0 */
    Resource good;
    float    volume;          /* >0 = ra exporte vers rb */
    float    value;           /* volume * prix */
} TradeFlow;

#define TRADE_MAX_LINKS  (SCPS_MAX_REG * 6)  /* ~4-6 voisins par région */
#define TRADE_MAX_FLOWS  4096                 /* flux enregistrés par tick */

typedef struct {
    TradeLink  link[TRADE_MAX_LINKS];
    int        n_links;

    /* Index : pour la région r, les voisins sont dans
     * link[neighbor_start[r] .. neighbor_start[r]+neighbor_count[r]-1].
     * N'est valide qu'après trade_network_sort(). */
    int        neighbor_start[SCPS_MAX_REG];
    int        neighbor_count[SCPS_MAX_REG];

    /* Flux enregistrés au dernier trade_tick (pour reporting / UI) */
    TradeFlow  flow[TRADE_MAX_FLOWS];
    int        n_flows;
} TradeNetwork;

/* ---- API -------------------------------------------------------------- */

/* Construit le réseau à partir de la géographie et de l'économie.
 * Doit être rappelé si la pop change drastiquement (capacités évoluent). */
void trade_network_build(TradeNetwork *net, const World *w,
                         const WorldEconomy *e);

/* Un pas de commerce : échange les surplus, met à jour stock et prix.
 * À appeler APRÈS econ_tick(). */
void trade_tick(WorldEconomy *e, TradeNetwork *net);

/* Affiche la balance commerciale d'une région (imports/exports du dernier
 * tick) et ses voisins les plus importants. */
void trade_print_region(const TradeNetwork *net, const WorldEconomy *e,
                        const World *w, int region_id);

/* Affiche un résumé des principales routes commerciales (top N flux). */
void trade_print_summary(const TradeNetwork *net, const WorldEconomy *e,
                         const World *w, int top_n);

#endif /* SCPS_TRADE_H */
