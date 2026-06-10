/*
 * scps_trade.c — moteur de commerce inter-régional
 */
#include "scps_trade.h"
#include "scps_world.h"
#include <string.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static inline float clampf(float v,float lo,float hi){return v<lo?lo:(v>hi?hi:v);}
#define EPS 1e-4f

/* ====================================================================== */
/* CONSTRUCTION DU RÉSEAU                                                  */
/* ====================================================================== */

/* Propriétés d'une région utiles pour le calcul de transport. */
typedef struct {
    bool  coastal;
    bool  has_mountain;   /* au moins une province BIO_MOUNTAINS/PEAK */
    bool  has_river;      /* une province traversée par un gros fleuve */
    float pop;
} RegionGeo;

static void gather_region_geo(RegionGeo *geo, const World *w,
                               const WorldEconomy *e) {
    memset(geo, 0, SCPS_MAX_REG * sizeof(RegionGeo));

    for (int rid=0; rid<w->n_regions; rid++) {
        const Region *rg=&w->region[rid];
        if (e->region[rid].colonized) {
            for (int c=0;c<CLASS_COUNT;c++)
                geo[rid].pop += e->region[rid].strata[c].pop;
        }
        for (int k=0; k<rg->n_provinces; k++) {
            int pid=rg->province_ids[k];
            if (pid<0||pid>=w->n_provinces) continue;
            const Province *pv=&w->province[pid];
            if (pv->coastal) geo[rid].coastal=true;
            Biome b=pv->biome_dominant;
            if (b==BIO_MOUNTAINS||b==BIO_PEAK) geo[rid].has_mountain=true;
        }
    }
    /* Rivière : cellule avec fort débit dans la région */
    for (int i=0;i<SCPS_N;i++) {
        int rid=w->cell[i].region;
        if (rid>=0 && w->cell[i].river>180) geo[rid].has_river=true;
    }
}

/* Coût de transport entre ra et rb (fraction de la valeur). */
static float link_cost(int ra, int rb, const RegionGeo *geo) {
    /* Route maritime : les deux côtières → caravelles, pas chameaux. */
    if (geo[ra].coastal && geo[rb].coastal) return 0.12f;

    float cost = 0.32f;   /* terre standard (route) */

    /* Rivière partagée : rabais de navigation fluviale */
    if (geo[ra].has_river && geo[rb].has_river) cost -= 0.10f;
    else if (geo[ra].has_river || geo[rb].has_river) cost -= 0.05f;

    /* Passage montagneux : surcoût */
    if (geo[ra].has_mountain || geo[rb].has_mountain) cost += 0.20f;

    return clampf(cost, 0.05f, 0.80f);
}

/* Capacité d'un lien (unités/tick/bien) : grandit avec la population.
 * Interprétation : une population plus grande génère plus de marchands. */
static float link_capacity(int ra, int rb, const RegionGeo *geo) {
    float p = geo[ra].pop + geo[rb].pop;
    return 4.f + p * 0.002f;   /* ~4 unités de base + 2 par 1000 hab */
}

void trade_network_build(TradeNetwork *net, const World *w,
                         const WorldEconomy *e) {
    memset(net, 0, sizeof(*net));

    RegionGeo geo[SCPS_MAX_REG];
    gather_region_geo(geo, w, e);

    /* Matrice d'adjacence booléenne (110×110 = 12 100 bytes, stack-safe). */
    static bool adj[SCPS_MAX_REG][SCPS_MAX_REG];
    memset(adj, 0, sizeof(adj));

    /* 1. Adjacence géographique : scan des cellules (O(SCPS_N)). */
    static const int DX4[4]={1,-1,0,0};
    static const int DY4[4]={0,0,1,-1};
    for (int y=0;y<SCPS_H;y++) for (int x=0;x<SCPS_W;x++) {
        int ra=w->cell[scps_idx(x,y)].region;
        if (ra<0) continue;
        for (int d=0;d<4;d++) {
            int nx=x+DX4[d], ny=y+DY4[d];
            if (nx<0||nx>=SCPS_W||ny<0||ny>=SCPS_H) continue;
            int rb=w->cell[scps_idx(nx,ny)].region;
            if (rb<0||rb==ra) continue;
            if (ra<rb) adj[ra][rb]=true;
            else       adj[rb][ra]=true;
        }
    }

    /* 2. Routes maritimes : paires de régions côtières sur le même continent,
     *    à moins de MAX_SEA_DIST cellules entre leurs centres (évite un graphe
     *    quasi-complet et les traversées irréalistes d'un bout à l'autre). */
    #define MAX_SEA_DIST 160.f
    for (int ra=0;ra<w->n_regions;ra++) {
        if (!geo[ra].coastal||!e->region[ra].colonized) continue;
        for (int rb=ra+1;rb<w->n_regions;rb++) {
            if (!geo[rb].coastal||!e->region[rb].colonized) continue;
            if (w->region[ra].continent!=w->region[rb].continent) continue;
            float dx=(float)(w->region[ra].seed_x-w->region[rb].seed_x);
            float dy=(float)(w->region[ra].seed_y-w->region[rb].seed_y);
            if (dx*dx+dy*dy > MAX_SEA_DIST*MAX_SEA_DIST) continue;
            adj[ra][rb]=true;
        }
    }

    /* 3. Routes fluviales : scan des rivières significatives (flow_max ≥ 0.25).
     *    Chaque transition de région le long d'un fleuve crée un lien fluvial
     *    au coût 0.20 (entre maritime 0.12 et terrestre 0.32). */
    static bool river_adj[SCPS_MAX_REG][SCPS_MAX_REG];
    memset(river_adj, 0, sizeof(river_adj));
    for (int k=0; k<w->n_rivers; k++) {
        const River *rv=&w->river[k];
        if (rv->flow_max < 0.25f) continue;
        int prev_reg=-1;
        for (int s=0; s<rv->len; s++) {
            int idx=scps_idx(rv->x[s], rv->y[s]);
            int cur_reg=w->cell[idx].region;
            if (cur_reg<0){ prev_reg=-1; continue; }
            if (cur_reg!=prev_reg && prev_reg>=0) {
                int ra2=(prev_reg<cur_reg)?prev_reg:cur_reg;
                int rb2=(prev_reg<cur_reg)?cur_reg:prev_reg;
                river_adj[ra2][rb2]=true;
                adj[ra2][rb2]=true;   /* garantit la présence dans la liste */
            }
            prev_reg=cur_reg;
        }
    }

    /* 4. Conversion matrice → liste de liens. */
    for (int ra=0;ra<w->n_regions;ra++) {
        for (int rb=ra+1;rb<w->n_regions;rb++) {
            if (!adj[ra][rb]) continue;
            if (!e->region[ra].colonized||!e->region[rb].colonized) continue;
            if (net->n_links>=TRADE_MAX_LINKS) break;
            TradeLink *lk=&net->link[net->n_links++];
            lk->ra=(int16_t)ra; lk->rb=(int16_t)rb;
            lk->river_route = river_adj[ra][rb];
            lk->sea_route   = (!lk->river_route && geo[ra].coastal && geo[rb].coastal);
            /* Coût fluvial fixe (priorité sur calcul terrestre). */
            if (lk->river_route)
                lk->transport_cost = 0.20f;
            else
                lk->transport_cost = link_cost(ra, rb, geo);
            lk->capacity = link_capacity(ra, rb, geo);
        }
    }

    /* 5. Index de voisinage (tri par ra). */
    memset(net->neighbor_start,0,sizeof(net->neighbor_start));
    memset(net->neighbor_count,0,sizeof(net->neighbor_count));
    for (int i=0;i<net->n_links;i++){
        net->neighbor_count[net->link[i].ra]++;
        net->neighbor_count[net->link[i].rb]++;
    }
    int acc=0;
    for (int r=0;r<SCPS_MAX_REG;r++){
        net->neighbor_start[r]=acc;
        acc+=net->neighbor_count[r];
    }
}

/* ====================================================================== */
/* TICK DE COMMERCE                                                        */
/* ====================================================================== */

/* Comparateur pour qsort des flux (volume décroissant, pour le reporting). */
static int flow_cmp(const void *a, const void *b) {
    float va=fabsf(((const TradeFlow*)a)->value);
    float vb=fabsf(((const TradeFlow*)b)->value);
    return (va<vb)-(va>vb);
}

void trade_tick(WorldEconomy *e, TradeNetwork *net) {
    net->n_flows=0;

    for (int li=0; li<net->n_links; li++) {
        TradeLink *lk=&net->link[li];
        int ra=lk->ra, rb=lk->rb;
        if (ra<0 || ra>=e->n_regions || rb<0 || rb>=e->n_regions) continue;  /* lien corrompu : on saute */
        RegionEconomy *ea=&e->region[ra];
        RegionEconomy *eb=&e->region[rb];

        for (int r=1; r<RES_COUNT; r++) {
            float pa=ea->price[r], pb=eb->price[r];
            float diff=pa-pb;
            if (fabsf(diff) <= lk->transport_cost * fmaxf(pa,pb) + EPS) continue;

            /* ra plus cher : rb exporte vers ra. */
            int exporter=(diff>0)?rb:ra;
            int importer=(diff>0)?ra:rb;
            RegionEconomy *exp=&e->region[exporter];
            RegionEconomy *imp=&e->region[importer];

            float surplus=exp->stock[r];
            if (surplus<=EPS) continue;

            /* Volume limité par la capacité du lien et le surplus exportateur. */
            float vol=fminf(surplus, lk->capacity);
            /* Un importateur ne prend pas plus qu'il ne peut payer. */
            float pop_imp=0.f;
            for(int c=0;c<CLASS_COUNT;c++) pop_imp+=imp->strata[c].pop;
            float demand_est=pop_imp*0.01f;   /* estimation grossière */
            vol=fminf(vol,demand_est);
            if (vol<=EPS) continue;

            /* Transport : petite perte de fret. */
            float loss=lk->transport_cost*0.10f;
            float received=vol*(1.f-loss);

            exp->stock[r]-=vol;
            imp->stock[r]+=received;

            /* Revenu de l'exportateur (vente au prix de l'importateur
             * moins le coût de transport = marge marchande). */
            float sale_price=imp->price[r]*(1.f-lk->transport_cost);
            float revenue=vol*sale_price;
            /* Les bourgeois captent le profit commercial. */
            exp->strata[CLASS_BOURGEOIS].wealth+=revenue;
            /* L'importateur paie. */
            float cost_imp=received*imp->price[r];
            /* Le coût est réparti entre les strates au prorata du besoin. */
            float need_tot=0.f;
            for(int c=0;c<CLASS_COUNT;c++){
                float pop=exp->strata[c].pop;
                if(pop>0.f) need_tot+=imp->strata[c].wealth;
            }
            if(need_tot>EPS){
                for(int c=0;c<CLASS_COUNT;c++){
                    float share=(imp->strata[c].wealth/need_tot);
                    float pay=cost_imp*share;
                    imp->strata[c].wealth=fmaxf(0.f,imp->strata[c].wealth-pay);
                }
            }

            /* Convergence de prix : 20% vers la moyenne après le flux. */
            float mid=(pa+pb)*0.5f;
            ea->price[r]=ea->price[r]*0.80f+mid*0.20f;
            eb->price[r]=eb->price[r]*0.80f+mid*0.20f;

            /* Enregistrement du flux. */
            if (net->n_flows<TRADE_MAX_FLOWS) {
                TradeFlow *fl=&net->flow[net->n_flows++];
                fl->ra=(int16_t)(diff>0?rb:ra);   /* exportateur */
                fl->rb=(int16_t)(diff>0?ra:rb);   /* importateur */
                fl->good=(Resource)r;
                fl->volume=vol;
                fl->value=vol*fmaxf(pa,pb);
            }
        }
    }

    /* Tri des flux par valeur décroissante (facilite le reporting). */
    if (net->n_flows>1)
        qsort(net->flow, net->n_flows, sizeof(TradeFlow), flow_cmp);
}

/* ====================================================================== */
/* AFFICHAGE                                                               */
/* ====================================================================== */

void trade_print_region(const TradeNetwork *net, const WorldEconomy *e,
                        const World *w, int rid) {
    if (rid<0||rid>=e->n_regions) return;
    printf("\n┌─ Commerce région #%d « %s »\n",
           rid, w->region[rid].name[0]?w->region[rid].name:"—");

    float exports=0, imports=0;
    printf("│ Flux du dernier tick (top 20) :\n");
    bool any=false; int shown=0;
    for (int f=0;f<net->n_flows&&shown<20;f++) {
        const TradeFlow *fl=&net->flow[f];
        if (fl->ra!=rid && fl->rb!=rid) continue;
        bool is_exp=(fl->ra==rid);
        int partner=is_exp?fl->rb:fl->ra;
        printf("│  %s %5.1f %-20s → #%d %-18s (val %6.0f)\n",
               is_exp?"↑EXP":"↓IMP",
               fl->volume,
               resource_name(fl->good),
               partner,
               w->region[partner].name[0]?w->region[partner].name:"—",
               fl->value);
        if (is_exp) exports+=fl->value; else imports+=fl->value;
        any=true; shown++;
    }
    if (!any) printf("│  (aucun échange)\n");
    printf("│ Balance : exports %.0f  imports %.0f  net %+.0f\n",
           exports, imports, exports-imports);

    /* Voisins */
    printf("│ Liens commerciaux (%d voisins) :\n", net->neighbor_count[rid]);
    for (int li=0;li<net->n_links;li++) {
        const TradeLink *lk=&net->link[li];
        if (lk->ra!=rid && lk->rb!=rid) continue;
        int partner=(lk->ra==rid)?lk->rb:lk->ra;
        printf("│   #%d %-18s  coût %.0f%%  cap %.0f  %s\n",
               partner,
               w->region[partner].name[0]?w->region[partner].name:"—",
               lk->transport_cost*100.f,
               lk->capacity,
               lk->sea_route?"[mer]":lk->river_route?"[fleuve]":"[terre]");
    }
    printf("└────────────────────────────────────────────\n");
}

void trade_print_summary(const TradeNetwork *net, const WorldEconomy *e,
                         const World *w, int top_n) {
    (void)e;   /* réservé (cohérence d'API) : ce résumé ne lit que le réseau */
    if (top_n<=0) top_n=10;
    printf("\n══ RÉSEAU COMMERCIAL ══  %d liens  %d flux actifs\n",
           net->n_links, net->n_flows);
    printf("  Top %d routes (par valeur) :\n", top_n);
    int shown=0;
    for (int f=0;f<net->n_flows&&shown<top_n;f++,shown++) {
        const TradeFlow *fl=&net->flow[f];
        printf("  [%2d] #%-2d %-14s → #%-2d %-14s  %-20s  %.1f unités  %.0f val\n",
               shown+1,
               fl->ra, w->region[fl->ra].name[0]?w->region[fl->ra].name:"—",
               fl->rb, w->region[fl->rb].name[0]?w->region[fl->rb].name:"—",
               resource_name(fl->good),
               fl->volume, fl->value);
    }
}
