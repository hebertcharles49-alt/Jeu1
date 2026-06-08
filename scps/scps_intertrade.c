/*
 * scps_intertrade.c — commerce inter-pays (voir scps_intertrade.h)
 *
 * Réutilise les stocks & prix de marché des régions (scps_econ) : pas de nouvel
 * état lourd. Les biens remontent la pente de prix le long des routes ouvertes,
 * la guerre coupe le robinet (embargo).
 */
#include "scps_intertrade.h"
#include <math.h>

/* ---- calibrage --------------------------------------------------------- */
#define IT_TRANSPORT   0.06f   /* coût de transport longue distance (fraction du prix moyen) */
#define IT_EXPORT_FRAC 0.25f   /* part du surplus que l'exportateur lâche par tick */
#define IT_PRICE_CONV  0.20f   /* convergence partielle des prix (lissage) */
#define IT_MARGIN_TO_GOLD 0.50f/* part de la valeur transportée qui devient de l'OR pour l'exportateur */

static float g_last_value = 0.f;   /* valeur totale échangée au dernier tick (reporting) */

static bool pair_at_peace(const DiploState *dp, int ca, int cb){
    return !dp || diplo_status(dp, ca, cb) != DIPLO_WAR;
}

void intertrade_tick(WorldEconomy *e, const RouteNetwork *rn, const DiploState *dp){
    g_last_value = 0.f;
    if (!e || !rn) return;
    for (int i=0;i<rn->n;i++){
        const TradeRoute *rt=&rn->route[i];
        if (!rt->open) continue;
        int ra=rt->ra, rb=rt->rb;
        if (ra<0||rb<0||ra>=e->n_regions||rb>=e->n_regions) continue;
        int ca=e->region[ra].owner, cb=e->region[rb].owner;
        if (ca<0||cb<0||ca==cb) continue;            /* intra-pays : déjà couvert par scps_trade */
        if (!pair_at_peace(dp,ca,cb)) continue;      /* EMBARGO : guerre commerciale */
        RegionEconomy *A=&e->region[ra], *B=&e->region[rb];
        float cap=rt->capacity>0.f?rt->capacity:1.f;
        for (int g=1; g<RES_COUNT; g++){
            float pa=A->price[g], pb=B->price[g];
            float cost=IT_TRANSPORT*((pa+pb)*0.5f);
            if (fabsf(pa-pb) <= cost) continue;       /* marge trop mince → rien ne bouge */
            RegionEconomy *src=(pa<pb)?A:B, *dst=(pa<pb)?B:A;   /* on achète au moins cher */
            float vol=fminf(cap, src->stock[g]*IT_EXPORT_FRAC);
            if (vol<=0.001f) continue;
            src->stock[g]-=vol; dst->stock[g]+=vol;             /* le bien remonte la pente de prix */
            float value=vol*dst->price[g];
            src->treasury += value*IT_MARGIN_TO_GOLD;           /* l'exportateur encaisse l'or */
            g_last_value += value;
            float mid=(pa+pb)*0.5f;                              /* les prix convergent partiellement */
            A->price[g]+=(mid-A->price[g])*IT_PRICE_CONV;
            B->price[g]+=(mid-B->price[g])*IT_PRICE_CONV;
        }
    }
}

float intertrade_imports_value(const WorldEconomy *e){ (void)e; return g_last_value; }

int intertrade_active_routes(const WorldEconomy *e, const RouteNetwork *rn,
                             const DiploState *dp, int cid){
    if (!e||!rn) return 0;
    int n=0;
    for (int i=0;i<rn->n;i++){
        const TradeRoute *rt=&rn->route[i];
        if (!rt->open) continue;
        int ra=rt->ra, rb=rt->rb;
        if (ra<0||rb<0||ra>=e->n_regions||rb>=e->n_regions) continue;
        int ca=e->region[ra].owner, cb=e->region[rb].owner;
        if (ca==cb || (ca!=cid && cb!=cid)) continue;
        if (!pair_at_peace(dp,ca,cb)) continue;
        n++;
    }
    return n;
}
