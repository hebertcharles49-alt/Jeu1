/*
 * scps_navy.c — LA FLOTTE : trois coques + conversion, chantier, entretien,
 * colonisation outre-mer (briefs mer §5/§8 & coques §2).
 *
 * La flotte est LE consommateur de RES_NAVAL_SUPPLIES (chaîne bois → Scierie
 * navale → fournitures → coques) : l'achat suit le patron d'agency_build —
 * l'or paie le marché, le stock se consomme, la demande se REGISTRE (le marché
 * voit la flotte et la production tire).
 */
#include "scps_navy.h"
#include <string.h>
#include <math.h>

/* ── Surface d'équilibrage ──────────────────────────────────────────────── */
#define NAVY_MIN_PRICE       0.5f     /* plancher de prix (même rôle que BUILD_MIN_PRICE) */
#define NAVY_UPKEEP_WAR      1.5f     /* fournitures / an / coque                  */
#define NAVY_UPKEEP_OTHER    0.8f
#define NAVY_STARVE_YEAR     365.f    /* > 1 an sans entretien : une coque pourrit/an */
#define NAVY_COLONY_MAX_DAYS 90.f     /* le seuil de traversée colonisatrice (jours) */
#define NAVY_COLONY_CD       (2.f*365.f)  /* une colonie outre-mer / 2 ans / pays   */
#define NAVY_TRANSPORT_PKTS  10       /* 1 transport = 10 paquets = 1 000 hommes   */

typedef struct { float supplies, wood, metal; int days; } HullCost;
static const HullCost HULLS[HULL_COUNT]={
    [HULL_WAR]      ={ 30.f, 40.f, 25.f, 420 },
    [HULL_TRANSPORT]={ 20.f, 30.f,  0.f, 270 },
    [HULL_MERCHANT] ={ 15.f, 25.f,  0.f, 200 },
    [HULL_PIRATE]   ={  6.f,  8.f,  0.f,  60 },   /* la CONVERSION coûte peu : c'est sa nature */
};
const char *navy_hull_name(HullType t){
    static const char *N[HULL_COUNT]={"navire de combat","transport","marchand","pirate"};
    return (t>=0&&t<HULL_COUNT)?N[t]:"?";
}

void navy_init(NavyState *ns){
    memset(ns,0,sizeof *ns);
    for (int c=0;c<SCPS_MAX_COUNTRY;c++){
        ns->n[c].build_hull=-1; ns->n[c].home_port=-1;
        ns->n[c].mission_target=-1; ns->n[c].nest_region=-1;
    }
}

/* Une région est côtière si l'une de ses provinces touche la mer. */
static bool region_coastal(const World *w, int region){
    if (region<0 || region>=w->n_regions) return false;
    const Region *rg=&w->region[region];
    for (int k=0;k<rg->n_provinces;k++){
        int p=rg->province_ids[k];
        if (p>=0 && p<w->n_provinces && w->province[p].coastal) return true;
    }
    return false;
}
bool navy_region_is_port(const World *w, const WorldEconomy *econ, int region){
    if (region<0 || region>=econ->n_regions) return false;
    return econ->region[region].build.port>0.f && region_coastal(w,region);
}
int navy_best_port(const World *w, const WorldEconomy *econ, int cid){
    if (cid<0 || cid>=w->n_countries) return -1;
    int cap_reg=-1;
    { int cp=w->country[cid].capital_prov;
      if (cp>=0 && cp<w->n_provinces) cap_reg=w->province[cp].region; }
    if (cap_reg>=0 && navy_region_is_port(w,econ,cap_reg)
        && econ->region[cap_reg].owner==cid) return cap_reg;
    int best=-1; float bpop=-1.f;
    for (int r=0;r<econ->n_regions;r++){
        const RegionEconomy *re=&econ->region[r];
        if (re->owner!=cid || !navy_region_is_port(w,econ,r)) continue;
        float pop=0.f; for (int c=0;c<CLASS_COUNT;c++) pop+=re->strata[c].pop;
        if (pop>bpop){ bpop=pop; best=r; }
    }
    return best;
}

float navy_build_gold(const WorldEconomy *econ, int region, HullType t){
    if (t<0||t>=HULL_COUNT||region<0||region>=econ->n_regions) return 0.f;
    const RegionEconomy *re=&econ->region[region];
    const HullCost *h=&HULLS[t];
    float gold=0.f, p;
    p=re->price[RES_NAVAL_SUPPLIES]; if (p<NAVY_MIN_PRICE) p=NAVY_MIN_PRICE; gold+=h->supplies*p;
    p=re->price[RES_WOOD];           if (p<NAVY_MIN_PRICE) p=NAVY_MIN_PRICE; gold+=h->wood*p;
    if (h->metal>0.f){ p=re->price[RES_METAL]; if (p<NAVY_MIN_PRICE) p=NAVY_MIN_PRICE; gold+=h->metal*p; }
    return gold;
}

bool navy_order_build(NavyState *ns, const World *w, WorldEconomy *econ, int cid, HullType t){
    if (t<0||t>=HULL_COUNT||cid<0||cid>=SCPS_MAX_COUNTRY) return false;
    Navy *n=&ns->n[cid];
    if (n->build_hull>=0) return false;                  /* un chantier à la fois */
    int port=navy_best_port(w,econ,cid);
    if (port<0) return false;                            /* un pays sans port ne bâtit rien */
    RegionEconomy *re=&econ->region[port];
    float gold=navy_build_gold(econ,port,t);
    if (gold>re->treasury) return false;
    const HullCost *h=&HULLS[t];
    re->treasury-=gold;
    re->stock[RES_NAVAL_SUPPLIES]-=h->supplies; if (re->stock[RES_NAVAL_SUPPLIES]<0.f) re->stock[RES_NAVAL_SUPPLIES]=0.f;
    re->stock[RES_WOOD]          -=h->wood;     if (re->stock[RES_WOOD]<0.f)           re->stock[RES_WOOD]=0.f;
    if (h->metal>0.f){ re->stock[RES_METAL]-=h->metal; if (re->stock[RES_METAL]<0.f) re->stock[RES_METAL]=0.f; }
    re->demand[RES_NAVAL_SUPPLIES]+=h->supplies;         /* le marché VOIT le chantier */
    re->demand[RES_WOOD]          +=h->wood;
    n->supplies_eaten+=h->supplies;
    n->build_hull=(int)t; n->build_days=(float)h->days; n->home_port=port;
    return true;
}

bool navy_convert(NavyState *ns, const World *w, WorldEconomy *econ, int cid, bool to_pirate){
    if (cid<0||cid>=SCPS_MAX_COUNTRY) return false;
    Navy *n=&ns->n[cid];
    int port=navy_best_port(w,econ,cid);
    if (port<0) return false;                            /* la conversion se fait AU CHANTIER */
    int from = to_pirate?HULL_MERCHANT:HULL_PIRATE;
    int to   = to_pirate?HULL_PIRATE  :HULL_MERCHANT;
    if (n->hull[from]<=0) return false;
    RegionEconomy *re=&econ->region[port];
    const HullCost *h=&HULLS[HULL_PIRATE];               /* le coût léger de la conversion */
    float gold=navy_build_gold(econ,port,HULL_PIRATE);
    if (gold>re->treasury) return false;
    re->treasury-=gold;
    re->stock[RES_NAVAL_SUPPLIES]-=h->supplies; if (re->stock[RES_NAVAL_SUPPLIES]<0.f) re->stock[RES_NAVAL_SUPPLIES]=0.f;
    n->supplies_eaten+=h->supplies;
    n->hull[from]--; n->hull[to]++;
    if (!to_pirate) n->nest_region=-1;                   /* désarmé : le nid se vide */
    return true;
}

void navy_tick(NavyState *ns, const World *w, WorldEconomy *econ, float dt_days){
    for (int c=0;c<w->n_countries && c<SCPS_MAX_COUNTRY;c++){
        Navy *n=&ns->n[c];
        /* la rade suit la vie du pays (port perdu/conquis → meilleure rade restante) */
        if (n->home_port>=0 && (n->home_port>=econ->n_regions
            || econ->region[n->home_port].owner!=c
            || !navy_region_is_port(w,econ,n->home_port)))
            n->home_port=navy_best_port(w,econ,c);
        if (n->colony_cd>0.f) n->colony_cd-=dt_days;
        /* chantier */
        if (n->build_hull>=0){
            n->build_days-=dt_days;
            if (n->build_days<=0.f){
                n->hull[n->build_hull]++; n->built_total++;
                n->build_hull=-1; n->build_days=0.f;
            }
        }
        /* entretien : fournitures au fil de l'eau, consommées à la rade */
        int hulls=0; float need_y=0.f;
        for (int t=0;t<HULL_COUNT;t++){
            hulls+=n->hull[t];
            need_y += (float)n->hull[t] * ((t==HULL_WAR)?NAVY_UPKEEP_WAR:NAVY_UPKEEP_OTHER);
        }
        if (hulls<=0){ n->starve_days=0.f; continue; }
        if (n->home_port<0){ n->home_port=navy_best_port(w,econ,c); }
        if (n->home_port<0){                              /* sans rade, rien ne s'entretient */
            n->starve_days+=dt_days;
        } else {
            RegionEconomy *re=&econ->region[n->home_port];
            float need=need_y*(dt_days/365.f);
            re->demand[RES_NAVAL_SUPPLIES]+=need;         /* la demande se VOIT au marché */
            if (re->stock[RES_NAVAL_SUPPLIES]>=need){
                re->stock[RES_NAVAL_SUPPLIES]-=need;
                n->supplies_eaten+=need;
                n->starve_days=0.f;
            } else {
                n->supplies_eaten+=re->stock[RES_NAVAL_SUPPLIES];
                re->stock[RES_NAVAL_SUPPLIES]=0.f;
                n->starve_days+=dt_days;
            }
        }
        if (n->starve_days>NAVY_STARVE_YEAR){             /* la flotte pourrit à quai */
            int big=-1, bc=0;
            for (int t=0;t<HULL_COUNT;t++) if (n->hull[t]>bc){ bc=n->hull[t]; big=t; }
            if (big>=0) n->hull[big]--;
            n->starve_days-=365.f;
        }
    }
}

int navy_transport_packets_free(const NavyState *ns, int cid){
    if (cid<0||cid>=SCPS_MAX_COUNTRY) return 0;
    int free_tr=ns->n[cid].hull[HULL_TRANSPORT]-ns->n[cid].at_sea;
    return (free_tr>0)?free_tr*NAVY_TRANSPORT_PKTS:0;
}

float navy_sea_days_regions(const World *w, int reg_a, int reg_b){
    int ax,ay,bx,by;
    if (!world_region_sea_anchor(w,reg_a,&ax,&ay)) return -1.f;
    if (!world_region_sea_anchor(w,reg_b,&bx,&by)) return -1.f;
    return world_sea_days(w,ax,ay,bx,by);
}

/* ── LA COLONISATION OUTRE-MER (mer §8) : on découvre ce que la volta touche ── */
int navy_colonize_tick(NavyState *ns, const World *w, WorldEconomy *econ, float dt_days){
    (void)dt_days;
    int founded=0;
    for (int cid=0;cid<w->n_countries && cid<SCPS_MAX_COUNTRY;cid++){
        const Country *ct=&w->country[cid];
        if (ct->role!=POLITY_PLAYER && ct->role!=POLITY_ANTAGONIST) continue;
        Navy *n=&ns->n[cid];
        if (n->colony_cd>0.f) continue;
        if (n->hull[HULL_TRANSPORT]-n->at_sea<1) continue;   /* pas de flotte = un mur ÉCONOMIQUE */
        int port=navy_best_port(w,econ,cid);
        if (port<0) continue;
        const RegionEconomy *src=&econ->region[port];
        float spop=0.f; for (int k=0;k<CLASS_COUNT;k++) spop+=src->strata[k].pop;
        if (spop<500.f || src->food_sat<0.35f) continue;   /* mêmes seuils que la colonisation TERRESTRE (essaimer) */
        int best=-1; float bscore=-1.f, bdays=0.f;
        for (int rd=0;rd<econ->n_regions;rd++){
            const RegionEconomy *dst=&econ->region[rd];
            if (!dst->active || dst->colonized || econ->adj[port][rd]) continue;
            if (!region_coastal(w,rd)) continue;             /* on atterrit par la côte */
            float days=navy_sea_days_regions(w,port,rd);
            if (days<0.f || days>NAVY_COLONY_MAX_DAYS) continue;
            float score=dst->cap_pop*0.001f/(1.f+days/15.f); /* les courants RAPPROCHENT */
            if (score>bscore){ bscore=score; best=rd; bdays=days; }
        }
        (void)bdays;
        if (best>=0){
            econ_colonize_from(econ,port,best,cid);
            n->colony_cd=NAVY_COLONY_CD;
            founded++;
        }
    }
    return founded;
}
