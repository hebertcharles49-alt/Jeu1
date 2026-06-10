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
#include <stdio.h>

/* ---- calibrage --------------------------------------------------------- */
#define IT_TRANSPORT   0.06f   /* coût de transport longue distance — la BASE de la fonction directionnelle */
#define IT_FLUV_DOWN   0.35f   /* descente : quasi gratuit (on flotte le bois, on descend le grain) */
#define IT_FLUV_UP     2.50f   /* remontée : le halage — cher, JAMAIS infini (plancher, pas mur)   */
#define IT_VOL_FLOOR   0.40f   /* la capacité à contre-courant : divisée, jamais nulle             */
#define IT_EXPORT_FRAC 0.25f   /* part du surplus que l'exportateur lâche par tick */
#define IT_PRICE_CONV  0.20f   /* convergence partielle des prix (lissage) */
#define IT_MARGIN_TO_GOLD 0.50f/* part de la valeur transportée qui devient de l'OR pour l'exportateur */

static float g_last_value = 0.f;   /* valeur totale échangée au dernier tick (reporting) */

/* COMMERCE ASYMÉTRIQUE — télémétrie par SENS : l'aval est le sens FACILE
 * (coût directionnel le plus bas) ; vrac = grain/bois/charbon ; précieux =
 * étoffe précieuse/orfèvrerie/remèdes/armes. Le tri ÉMERGE du test d'arbitrage. */
static float g_vol_down=0.f, g_vol_up=0.f;
static float g_bulk_down=0.f, g_bulk_up=0.f, g_prec_down=0.f, g_prec_up=0.f;
static int   g_nprec_up=0;     /* ÉVÉNEMENTS précieux à contre-courant (la niche du luxe) */
static inline bool it_is_bulk(int g){
    return g==RES_GRAIN||g==RES_WOOD||g==RES_COAL||g==RES_LIVESTOCK;
}
static inline bool it_is_precious(int g){
    return g==RES_PRECIOUS_CLOTH||g==RES_PRECIOUS_WARE||g==RES_REMEDE
         ||g==RES_ARMS||g==RES_ENCHANTED_ARMS;
}
/* coût de transport SELON LE SENS (fraction du prix moyen) : terrestre
 * symétrique (intact) ; maritime = f(jours du chemin directionnel — la volta) ;
 * fluvial = descente bon marché / remontée chère. */
static float it_transport_frac(const TradeRoute *rt, bool a_to_b){
    if (rt->maritime){
        float d = a_to_b ? rt->days_ab : rt->days_ba;
        if (d<=0.f) d=rt->sea_days;
        if (d<=0.f) return IT_TRANSPORT;
        return IT_TRANSPORT*(0.5f + d/40.f);
    }
    if (rt->fluvial){
        bool descend = (rt->fluvial==1) ? a_to_b : !a_to_b;
        return IT_TRANSPORT*(descend ? IT_FLUV_DOWN : IT_FLUV_UP);
    }
    return IT_TRANSPORT;
}
/* le VOLUME suit la facilité : capacité × g(sens) — pleine en aval, plancher
 * à contre-courant (on divise, on n'annule jamais). */
static float it_volume_mult(const TradeRoute *rt, bool a_to_b){
    if (rt->maritime){
        float d=a_to_b?rt->days_ab:rt->days_ba; if (d<=0.f) d=rt->sea_days;
        float o=a_to_b?rt->days_ba:rt->days_ab; if (o<=0.f) o=rt->sea_days;
        if (d<=0.f||o<=0.f) return 1.f;
        float m=(o+10.f)/(d+10.f);
        return m<IT_VOL_FLOOR?IT_VOL_FLOOR:(m>1.6f?1.6f:m);
    }
    if (rt->fluvial){
        bool descend=(rt->fluvial==1)?a_to_b:!a_to_b;
        return descend ? 1.4f*(1.f+0.5f*rt->flow) : IT_VOL_FLOOR;   /* le gros fleuve porte plus */
    }
    return 1.f;
}
/* ce sens est-il l'AVAL (le sens facile) ? — pour la télémétrie/les mots. */
static bool it_is_downstream(const TradeRoute *rt, bool a_to_b){
    return it_transport_frac(rt,a_to_b) < it_transport_frac(rt,!a_to_b)-1e-6f;
}

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
    g_vol_down=g_vol_up=0.f; g_bulk_down=g_bulk_up=0.f; g_prec_down=g_prec_up=0.f; g_nprec_up=0;
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

void intertrade_save(FILE *f){ fwrite(g_embargo,sizeof g_embargo,1,f); }
bool intertrade_load(FILE *f){ return fread(g_embargo,sizeof g_embargo,1,f)==1; }

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
            bool a_to_b=(pa<pb);                      /* le bien va du bon marché vers le cher */
            float cost=it_transport_frac(rt,a_to_b)*((pa+pb)*0.5f);
            if (fabsf(pa-pb) <= cost) continue;       /* marge trop mince POUR CE SENS → rien ne bouge */
            RegionEconomy *src=a_to_b?A:B, *dst=a_to_b?B:A;     /* on achète au moins cher */
            float vol=fminf(cap*it_volume_mult(rt,a_to_b), src->stock[g]*IT_EXPORT_FRAC);
            if (vol<=0.001f) continue;
            { bool down=it_is_downstream(rt,a_to_b);  /* télémétrie : le tri par sens ÉMERGE */
              if (rt->maritime||rt->fluvial){
                  if (down) g_vol_down+=vol; else g_vol_up+=vol;
                  if (it_is_bulk(g))     { if (down) g_bulk_down+=vol; else g_bulk_up+=vol; }
                  if (it_is_precious(g)) { if (down) g_prec_down+=vol; else { g_prec_up+=vol; g_nprec_up++; } }
              } }
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
/* COMMERCE ASYMÉTRIQUE — lecteurs (chronicle §6) : volumes par sens et leur
 * composition. L'aval doit charrier le VRAC ; la remontée, le PRÉCIEUX. */
void intertrade_asym_stats(float *vdown, float *vup,
                           float *bulk_down, float *bulk_up,
                           float *prec_down, float *prec_up){
    if (vdown)     *vdown=g_vol_down;
    if (vup)       *vup=g_vol_up;
    if (bulk_down) *bulk_down=g_bulk_down;
    if (bulk_up)   *bulk_up=g_bulk_up;
    if (prec_down) *prec_down=g_prec_down;
    if (prec_up)   *prec_up=g_prec_up;
}
int intertrade_precious_upstream_events(void){ return g_nprec_up; }

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
