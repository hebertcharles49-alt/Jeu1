#ifndef SCPS_PROSPERITY_H
#define SCPS_PROSPERITY_H
/*
 * scps_prosperity.h — Générateur de Prospérité (PE/SI)
 *
 * Calcule par pays :
 *   PE_interne  = énergie de la diversité interne
 *   PE_externe  = énergie des contacts commerciaux
 *   P_potentiel = PE_int + Σ PE(n)
 *   SI          = stabilité interne (K + H)
 *   P_réalisé   = P_potentiel × rendement
 *   Lumière     = β·P_pot·(K/10)
 *   tresor_tick = γ·P_réalisé
 *   croissance  = δ·P_réalisé·(L/10)
 *   surchauffe  = max(0, (P/10)·C + flux_faustien − K)
 */
#include "scps_types.h"
#include "scps_econ.h"
#include "scps_trade.h"
#include "scps_tech.h"
#include "scps_legitimacy.h"   /* L vivant, entrée de scps_order */

typedef enum {
    POLE_CALME = 0,
    POLE_FLORISSANT,
    POLE_BOUILLONNANT,
    POLE_EN_SURCHAUFFE
} PoleState;

typedef struct {
    float valeurs, subsistance, parente, religion;  /* area-weighted mean */
    float D_bar_int;   /* mean pairwise content distance internally */
    float D_inf_int;   /* max pairwise content distance internally */
} CulturalProfile;

typedef struct {
    CulturalProfile profile;
    float C;               /* connectivité [0..10] */
    float SI;              /* stabilité interne [0..10] */
    float PE_interne;
    float PE_externe;
    float P_potentiel;
    float rendement;
    float P_realise;
    float Lumiere;
    float surchauffe;
    float tresor_tick;
    float croissance_tick;
    PoleState pole;
    /* Sorties du moteur vérifié scps_order (§2.4) — la vraie stabilité. */
    float fragilite;       /* part de l'ordre tenue par la contrainte [0..10] */
    float fracture;        /* sécession latente : diverse ET non consentie     */
    float L;               /* légitimité pays agrégée (entrée vivante, exposée) */
    float K;               /* capacité EFFECTIVE (tech+race+bâti) — lue par l'IA */
    int   mode;            /* ScpsMode (stocké en int : n'expose pas scps_core) */
} CountryProsperity;

typedef struct {
    CountryProsperity country[SCPS_MAX_COUNTRY];
    int               n_countries;
} WorldProsperity;

void prosperity_init(WorldProsperity *wp, const World *w);
void prosperity_tick(WorldProsperity *wp, const World *w,
                     const WorldEconomy *econ, const TradeNetwork *net,
                     const TechState ts[],          /* ts[SCPS_MAX_COUNTRY], can be NULL */
                     const WorldLegitimacy *wl);    /* L vivant ; NULL = tech.L de repli */
void prosperity_print_country(const WorldProsperity *wp, const World *w, int cid);
void prosperity_print_summary(const WorldProsperity *wp, const World *w);
const char *pole_state_name(PoleState s);

#endif /* SCPS_PROSPERITY_H */
