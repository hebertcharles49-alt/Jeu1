/*
 * scps_intertrade.c — commerce inter-pays (voir scps_intertrade.h)
 *
 * Réutilise les stocks & prix de marché des régions (scps_econ) : pas de nouvel
 * état lourd. Les biens remontent la pente de prix le long des routes ouvertes,
 * la guerre coupe le robinet (embargo).
 */
#include "scps_intertrade.h"
#include <math.h>
#include <string.h>

/* ---- calibrage --------------------------------------------------------- */
#define IT_TRANSPORT   0.06f   /* coût de transport longue distance (fraction du prix moyen) */
#define IT_EXPORT_FRAC 0.25f   /* part du surplus que l'exportateur lâche par tick */
#define IT_PRICE_CONV  0.20f   /* convergence partielle des prix (lissage) */
#define IT_MARGIN_TO_GOLD 0.50f/* part de la valeur transportée qui devient de l'OR pour l'exportateur */

static float g_last_value = 0.f;   /* valeur totale échangée au dernier tick (reporting) */

/* ---- DÉTAIL par pays × bien + embargos décrétés (sidebar / décisions) ------
 * Flux du DERNIER tick (≈ l'année écoulée) : volume importé/exporté par bien,
 * volume par paire de partenaires (→ partenaire dominant), or encaissé, valeur
 * par paire (le prix d'un embargo). L'embargo décrété est UNILATÉRAL à poser
 * mais coupe la paire entière (personne ne commerce à travers un blocus). */
static float  g_imp [SCPS_MAX_COUNTRY][RES_COUNT];
static float  g_expt[SCPS_MAX_COUNTRY][RES_COUNT];
static float  g_imp_best [SCPS_MAX_COUNTRY][RES_COUNT];  /* meilleur volume vu (élection du dominant) */
static float  g_expt_best[SCPS_MAX_COUNTRY][RES_COUNT];
static int16_t g_imp_from[SCPS_MAX_COUNTRY][RES_COUNT];
static int16_t g_expt_to [SCPS_MAX_COUNTRY][RES_COUNT];
static float  g_gold[SCPS_MAX_COUNTRY];
static float  g_pair[SCPS_MAX_COUNTRY][SCPS_MAX_COUNTRY];
static bool   g_embargo[SCPS_MAX_COUNTRY][SCPS_MAX_COUNTRY];   /* [qui décrète][contre qui] */

void intertrade_reset(void){
    memset(g_imp,0,sizeof g_imp);   memset(g_expt,0,sizeof g_expt);
    memset(g_imp_best,0,sizeof g_imp_best); memset(g_expt_best,0,sizeof g_expt_best);
    memset(g_imp_from,-1,sizeof g_imp_from); memset(g_expt_to,-1,sizeof g_expt_to);
    memset(g_gold,0,sizeof g_gold); memset(g_pair,0,sizeof g_pair);
    memset(g_embargo,0,sizeof g_embargo);
    g_last_value=0.f;
}
static void flows_clear(void){
    memset(g_imp,0,sizeof g_imp);   memset(g_expt,0,sizeof g_expt);
    memset(g_imp_best,0,sizeof g_imp_best); memset(g_expt_best,0,sizeof g_expt_best);
    memset(g_imp_from,-1,sizeof g_imp_from); memset(g_expt_to,-1,sizeof g_expt_to);
    memset(g_gold,0,sizeof g_gold); memset(g_pair,0,sizeof g_pair);
}
static inline bool cid_ok(int c){ return c>=0 && c<SCPS_MAX_COUNTRY; }

void  intertrade_order_embargo(int cid, int target, bool on){
    if (cid_ok(cid) && cid_ok(target) && cid!=target) g_embargo[cid][target]=on;
}
bool  intertrade_embargoed(int cid, int target){
    if (!cid_ok(cid) || !cid_ok(target)) return false;
    return g_embargo[cid][target] || g_embargo[target][cid];
}
float intertrade_import_vol (int cid,int g){ return (cid_ok(cid)&&g>0&&g<RES_COUNT)?g_imp [cid][g]:0.f; }
float intertrade_export_vol (int cid,int g){ return (cid_ok(cid)&&g>0&&g<RES_COUNT)?g_expt[cid][g]:0.f; }
int   intertrade_import_from(int cid,int g){ return (cid_ok(cid)&&g>0&&g<RES_COUNT)?g_imp_from[cid][g]:-1; }
int   intertrade_export_to  (int cid,int g){ return (cid_ok(cid)&&g>0&&g<RES_COUNT)?g_expt_to [cid][g]:-1; }
float intertrade_export_gold(int cid){ return cid_ok(cid)?g_gold[cid]:0.f; }
float intertrade_pair_value (int cid,int other){
    return (cid_ok(cid)&&cid_ok(other))? g_pair[cid][other] : 0.f;
}

static bool pair_at_peace(const DiploState *dp, int ca, int cb){
    return !dp || diplo_status(dp, ca, cb) != DIPLO_WAR;
}

void intertrade_tick(WorldEconomy *e, const RouteNetwork *rn, const DiploState *dp){
    g_last_value = 0.f;
    flows_clear();
    if (!e || !rn) return;
    for (int i=0;i<rn->n;i++){
        const TradeRoute *rt=&rn->route[i];
        if (!rt->open) continue;
        int ra=rt->ra, rb=rt->rb;
        if (ra<0||rb<0||ra>=e->n_regions||rb>=e->n_regions) continue;
        int ca=e->region[ra].owner, cb=e->region[rb].owner;
        if (ca<0||cb<0||ca==cb) continue;            /* intra-pays : déjà couvert par scps_trade */
        bool pact=diplo_trade_pact(dp,ca,cb);        /* cité marchande : route GARANTIE */
        if (!pact && !pair_at_peace(dp,ca,cb)) continue;      /* EMBARGO : guerre commerciale */
        if (!pact && intertrade_embargoed(ca,cb)) continue;   /* EMBARGO DÉCRÉTÉ (joueur/IA) */
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
            /* comptabilité des flux (sidebar) : qui exporte/importe quoi, vers/depuis qui */
            { int cs=(src==A)?ca:cb, cd=(dst==A)?ca:cb;
              if (cid_ok(cs)&&cid_ok(cd)){
                  g_expt[cs][g]+=vol; g_imp[cd][g]+=vol; g_gold[cs]+=value*IT_MARGIN_TO_GOLD;
                  g_pair[cs][cd]+=value; g_pair[cd][cs]+=value;
                  if (vol>g_expt_best[cs][g]){ g_expt_best[cs][g]=vol; g_expt_to [cs][g]=(int16_t)cd; }
                  if (vol>g_imp_best [cd][g]){ g_imp_best [cd][g]=vol; g_imp_from[cd][g]=(int16_t)cs; }
              } }
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
        if (!diplo_trade_pact(dp,ca,cb)){
            if (!pair_at_peace(dp,ca,cb)) continue;
            if (intertrade_embargoed(ca,cb)) continue;   /* l'embargo décrété ferme aussi la route */
        }
        n++;
    }
    return n;
}
