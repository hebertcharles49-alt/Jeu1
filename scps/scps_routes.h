#ifndef SCPS_ROUTES_H
#define SCPS_ROUTES_H
/*
 * scps_routes.h — ROUTES COMMERCIALES & MARITIMES (§7)
 *
 * Une route relie deux régions À TRAVERS une distance culturelle. Le rendement
 * suit la CLOCHE f(D̄) = D̄·(10−D̄)/25 (pic à D̄=5) : commercer avec un partenaire
 * INTERMÉDIAIRE rapporte le plus. Trop proche → rien à échanger ; trop loin →
 * porte fermée sauf forte P (ouverture/port).
 *
 * Maritime : nécessite un Port (build.P_open) ; atteint loin (autres sphères).
 * Une région sur plusieurs routes capte du PE concentré → devient un PÔLE
 * (Florissante → surchauffe), lu par la membrane.
 */
#include "scps_world.h"
#include "scps_econ.h"

typedef struct {
    int   ra, rb;          /* régions reliées */
    bool  maritime;        /* mer (port requis) vs terre */
    float capacity;        /* plafond d'échange (infrastructure) */
    int   days_total, days_done;
    bool  open;
    float yield;           /* PE/tick produit (cloche × porte) */
    float sea_days;        /* maritime : jours de mer port→port (la distance de COURANTS) */
    float pirate_press;    /* COURSE (coques §4) : pression nette après marchands/escorte —
                            * écrite par scps_navy, appliquée ici ; ≥ 90 = BLOCUS (lien coupé) */
    /* COMMERCE ASYMÉTRIQUE : le coût a un SENS. Maritime : jours par direction
     * (la volta, lue du champ). Fluvial : la route épouse un fleuve — descente
     * quasi gratuite, remontée chère ; un gros fleuve porte plus. */
    float days_ab, days_ba;   /* maritime : a→b / b→a (0 si terrestre)        */
    int8_t fluvial;           /* 0 non · 1 = ra en AMONT · 2 = rb en AMONT     */
    float flow;               /* débit du fleuve emprunté [0..1] (capacité)    */
} TradeRoute;

#define SCPS_MAX_ROUTES 256
typedef struct { TradeRoute route[SCPS_MAX_ROUTES]; int n; } RouteNetwork;

void routes_init(RouteNetwork *rn);

/* Ordonne une route entre deux régions peuplées. MARITIME (mer §7) : exige un
 * PORT RÉEL aux DEUX bouts et un chemin port→port sous le seuil de jours — la
 * distance maritime est une distance de courants, pas d'oiseau (w requis ;
 * NULL accepté pour une route de terre). Renvoie false si plein / invalide /
 * pas de port / mer infranchissable. */
bool routes_order(RouteNetwork *rn, const World *w, const WorldEconomy *econ,
                  int ra, int rb, bool maritime);

/* Avance de `days` jours : ouvre les routes mûres, recalcule le rendement via
 * la cloche f(D̄) × la porte de métabolisation, et dépose le PE de chaque route
 * sur ses extrémités (econ->region[r].route_pe), relu par prosperity_tick. */
void routes_advance(RouteNetwork *rn, const World *w, WorldEconomy *econ, int days);

float routes_pe_for_region   (const RouteNetwork *rn, int region);
int   routes_count_for_region(const RouteNetwork *rn, int region);

#endif /* SCPS_ROUTES_H */
