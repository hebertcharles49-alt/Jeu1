/*
 * scps_labor.c — l'économie des populations (voir scps_labor.h)
 *
 * La prod scale sur les JOBS REMPLIS ; les sorties LISENT la géo. Toutes les
 * constantes sont calées sur les ancres du cahier : 4000 pop, +4 nourriture
 * plancher, 200/200/200 au départ, suite de jobs 100→1000.
 */
#include "scps_labor.h"
#include <string.h>
#include <math.h>
#include <stdio.h>

/* ---- Calibrage (surface d'équilibrage) -------------------------------- */
#define COLLECTOR_BASE   4.0f   /* nourriture/slot, PLANCHER géo-indépendant */
#define COLLECTOR_GEO    4.0f   /* bonus de fertilité (×[0..1]) — ne retire jamais la base */
#define GRANARY_BASE     6.0f   /* grenier : lit la fertilité */
#define FOOD_PER_SLOT    1.0f   /* 100 pop EMPLOYÉE consomment 1 nourriture (§2) */
#define MARKET_BASE      3.0f   /* or = MARKET_BASE × flux × jobs (désert→0.3, floris→15) */
#define EXTRACT_BASE     5.0f   /* brute/slot × présence de ressource */
#define WORKSHOP_BASE    1.0f   /* matériaux/slot (gaté par les intrants) */
#define BASE_PRICE       1.0f
#define PRICE_MIN        0.5f
#define PRICE_MAX        5.0f
#define TAX_LABORER      0.01f
#define TAX_ARTISAN      0.03f
#define TAX_ELITE        0.08f
#define COLONIZE_MAT     100
#define COLONIZE_FOOD    50
#define POP_DEV_BASE     1.0f

static const int LEVEL_JOBS[6] = { 100,100,200,300,500,1000 };  /* capacité AJOUTÉE par niveau */

/* La chaîne de matériaux (§4) : extraire A (job) + extraire B (job) + atelier
 * qui combine (job) → la sortie. 300 pop = les trois étages. */
typedef struct { LRes in_a, in_b, out; int stages_pop; } Recipe;
static const Recipe RECIPES[] = {
    { LR_ARGILE, LR_BOIS,     LR_MATERIALS, 300 },
    { LR_BOIS,   LR_CALCAIRE, LR_MATERIALS, 300 },
    { LR_ARGILE, LR_CALCAIRE, LR_MATERIALS, 300 },
    { LR_BOIS,   LR_METAL,    LR_OUTILS,    300 },
};
#define N_RECIPES ((int)(sizeof(RECIPES)/sizeof(RECIPES[0])))

static inline float clampf(float v,float lo,float hi){ return v<lo?lo:(v>hi?hi:v); }

/* ===================================================================== */
/* JOBS & NIVEAUX (§9)                                                    */
/* ===================================================================== */
int building_job_capacity_pop(int level){
    if (level<0) level=0;
    if (level>5) level=5;
    int s=0; for (int i=0;i<=level;i++) s+=LEVEL_JOBS[i]; return s;   /* niv.5 = 2200 */
}
int building_job_slots(int level){ return building_job_capacity_pop(level)/POP_PER_SLOT; }

/* ===================================================================== */
/* LA CAPITALE & LA MOBILITÉ DE CLASSE (§capitale)                        */
/* ===================================================================== */
#define CAP_ADMIN_PER_TIER  100   /* pop de Nobles à l'administration, par tier (1 paquet/tier) */
#define CAP_PROD_PER_TIER   0.05f /* +5 % de productivité par tier SERVI */

/* Tier que la POPULATION débloque (plafond) — la pop OUVRE, la recette PAIE. */
int capitale_max_tier(long pop){
    if (pop>=10000) return 7;
    if (pop>= 8000) return 6;
    if (pop>= 5000) return 5;
    if (pop>= 4000) return 4;
    if (pop>= 3000) return 3;
    if (pop>= 2000) return 2;
    return 1;                       /* toute province : tier 1 dès la fondation */
}
/* Le STATUT d'urbanisation VIENT DU TIER bâti (pas seulement de la pop). */
const char *capitale_status(int tier){
    static const char *S[8]={ "Hameau","Hameau","Village","Bourg","Ville","Cité","Métropole","Mégapole" };
    if (tier<1) tier=1;
    if (tier>7) tier=7;
    return S[tier];
}
/* DÉFENSE provinciale passive : un niveau par tier (allonge le SIÈGE comme un
 * rempart — mais SANS le bonus défenseur au combat, cf. spec). */
int capitale_defense(int tier){ return tier<0 ? 0 : tier; }
long capitale_admin_pop(int tier){ return (long)tier * CAP_ADMIN_PER_TIER; }
/* Logement/service délivré : min(paquets de Nobles en poste, tier) × 1000 (gaté). */
long capitale_housing(int tier, long admin_pop){
    long packs = admin_pop / POP_PER_SLOT;
    long actifs = packs < tier ? packs : tier;
    if (actifs<0) actifs=0;
    return actifs * 1000;
}
float capitale_prodmult(int tier, long admin_pop){
    long packs = admin_pop / POP_PER_SLOT;
    long actifs = packs < tier ? packs : tier;
    if (actifs<0) actifs=0;
    return 1.f + CAP_PROD_PER_TIER * (float)actifs;
}
/* Recette d'amélioration vers `to_tier` : DE PLUS EN PLUS PRÉCIEUSE (bois → métal
 * → outils ; les paliers exotiques approximés par l'outil, le plus précieux du
 * module labor). Même BuildCost-logique que le reste. */
CapCost capitale_upgrade_cost(int to_tier){
    switch (to_tier){
        case 2:  return (CapCost){ LR_BOIS,  LR_BOIS,   400, 0   };   /* bois */
        case 3:  return (CapCost){ LR_BOIS,  LR_METAL,  400, 200 };   /* bois + métal */
        case 4:  return (CapCost){ LR_METAL, LR_OUTILS, 400, 200 };   /* métal + outils */
        case 5:  return (CapCost){ LR_METAL, LR_OUTILS, 600, 400 };   /* + (joaillerie) */
        case 6:  return (CapCost){ LR_OUTILS,LR_METAL,  800, 600 };   /* + (fer céleste) */
        case 7:  return (CapCost){ LR_OUTILS,LR_OUTILS, 1200,0   };   /* + (essence) : le plus précieux */
        default: return (CapCost){ LR_BOIS,  LR_BOIS,   200, 0   };   /* tier 1 : fondation */
    }
}
bool capitale_upgrade(LProvince *p, LaborEcon *e){
    if (!p || !e) return false;
    int maxt = capitale_max_tier(p->pop);
    if (p->cap_tier >= maxt) return false;             /* la pop ne débloque pas plus haut */
    int to = p->cap_tier + 1;
    CapCost c = capitale_upgrade_cost(to);
    if (e->stock[c.a] < c.qa) return false;            /* recette non payable (produire d'abord) */
    if (c.qb>0 && e->stock[c.b] < c.qb) return false;
    e->stock[c.a] -= c.qa;
    if (c.qb>0) e->stock[c.b] -= c.qb;
    p->cap_tier = to;                                  /* PAYÉ → tier monté */
    return true;
}
/* Les classes ÉMERGENT des emplois (par paquets de 100) ; la capitale délivre
 * logement/services/productivité au prorata des Nobles en poste. N'achète rien. */
void capitale_mobility_tick(LProvince *p){
    if (!p) return;
    if (p->cap_tier < 1) p->cap_tier = 1;              /* obligatoire : toujours ≥ tier 1 */
    long pool = p->pop; if (pool<0) pool=0;
    long noble_jobs   = (capitale_admin_pop(p->cap_tier)/100)*100;   /* admin, par 100 */
    long artisan_jobs = 0;
    for (int b=0;b<p->n_bld;b++)
        if (p->bld[b].type==LB_WORKSHOP) artisan_jobs += (long)p->bld[b].jobs_filled * POP_PER_SLOT;
    artisan_jobs = (artisan_jobs/100)*100;
    long elites   = noble_jobs   < pool ? noble_jobs : pool;          /* promotion → Nobles */
    long rem      = pool - elites; if (rem<0) rem=0;
    long artisans = artisan_jobs < rem ? artisan_jobs : rem;          /* → Bourgeois */
    long laborers = pool - elites - artisans;                         /* le reste : Journaliers */
    p->pop_by_class[LAB_ELITE]   = elites;
    p->pop_by_class[LAB_ARTISAN] = artisans;
    p->pop_by_class[LAB_LABORER] = laborers;
    p->house_cap = capitale_housing(p->cap_tier, elites);             /* gaté par les paquets nobles */
    p->serv_cap  = capitale_housing(p->cap_tier, elites);
    p->prod_mult = capitale_prodmult(p->cap_tier, elites);
}

long capitale_unhoused(const LProvince *p){ long u=p->pop-p->house_cap; return u>0?u:0; }
long capitale_unserved(const LProvince *p){ long u=p->pop-p->serv_cap; return u>0?u:0; }
float capitale_unrest(const LProvince *p){
    if (!p || p->pop<=0) return 0.f;
    long worst = capitale_unhoused(p);
    long us = capitale_unserved(p);
    if (us>worst) worst=us;                       /* le pire des deux manques */
    float f = (float)worst/(float)p->pop;
    return f<0.f?0.f:(f>1.f?1.f:f);
}

/* ===================================================================== */
/* labor_init — RELIT la géographie du worldgen en agrégats par province  */
/* ===================================================================== */
static bool biome_forest(Biome b){ return b==BIO_FOREST||b==BIO_WOODS||b==BIO_JUNGLE||b==BIO_MANGROVE; }
static bool biome_hills (Biome b){ return b==BIO_HILLS||b==BIO_HIGHLANDS; }
static bool biome_mtn   (Biome b){ return b==BIO_MOUNTAINS||b==BIO_PEAK||b==BIO_VOLCANO; }

void labor_init(LaborEcon *e, const World *w){
    memset(e, 0, sizeof(*e));
    e->market.price = BASE_PRICE; e->market.supply = 1.f;

    /* accumulateurs par province */
    static double fsum[SCPS_MAX_PROV]; static int pcells[SCPS_MAX_PROV], rmax[SCPS_MAX_PROV];
    static int nforest[SCPS_MAX_PROV], nhill[SCPS_MAX_PROV], nmtn[SCPS_MAX_PROV];
    memset(fsum,0,sizeof fsum); memset(pcells,0,sizeof pcells); memset(rmax,0,sizeof rmax);
    memset(nforest,0,sizeof nforest); memset(nhill,0,sizeof nhill); memset(nmtn,0,sizeof nmtn);

    for (int i=0;i<SCPS_N;i++){
        const Cell *c=&w->cell[i]; int pr=c->province;
        if (pr<0||pr>=SCPS_MAX_PROV) continue;
        pcells[pr]++; fsum[pr]+=c->fertility;
        if (c->river>rmax[pr]) rmax[pr]=c->river;
        if (biome_forest(c->biome)) nforest[pr]++;
        if (biome_hills(c->biome))  nhill[pr]++;
        if (biome_mtn(c->biome))    nmtn[pr]++;
    }
    for (int pr=0; pr<w->n_provinces && pr<SCPS_MAX_PROV; pr++){
        int n=pcells[pr]; if (n<=0) continue;
        float fert  = (float)(fsum[pr]/n);
        float riv01 = (float)rmax[pr]/255.f;
        bool  coast = w->province[pr].coastal;
        e->g_fert[pr] = fert;
        /* Le FLUX commercial (le carrefour) : la géographie des flux fait le
         * marchand — côte + fleuve + bonne terre. Désert intérieur ~0.1. */
        e->g_flow[pr] = 0.1f + (coast?1.8f:0.f) + riv01*1.5f + fert*1.6f;
        /* Présence de ressource [0..1] — lue des biomes + ressource dominante. */
        e->g_pres[pr][LR_BOIS]     = clampf((float)nforest[pr]/n*2.f, 0.f, 1.f);
        e->g_pres[pr][LR_ARGILE]   = clampf(riv01*1.2f, 0.f, 1.f);          /* argile au fleuve */
        e->g_pres[pr][LR_CALCAIRE] = clampf((float)nhill[pr]/n*2.f, 0.f, 1.f);
        e->g_pres[pr][LR_PIERRE]   = clampf((float)(nhill[pr]+nmtn[pr])/n*1.5f, 0.f, 1.f);
        Resource rr=w->province[pr].resource;
        float metal = (rr==RES_IRON||rr==RES_COPPER||rr==RES_GOLD||rr==RES_COAL||rr==RES_PRECIOUS_METAL)?0.8f:0.f;
        if ((float)nmtn[pr]/n*1.5f > metal) metal=clampf((float)nmtn[pr]/n*1.5f,0.f,1.f);
        e->g_pres[pr][LR_METAL]    = metal;
    }
}

/* ===================================================================== */
/* DÉPART CANONIQUE (§1)                                                  */
/* ===================================================================== */
void labor_seed_start(LaborEcon *e, int prov0){
    e->n_prov=1;
    LProvince *p=&e->prov[0];
    memset(p,0,sizeof(*p));
    p->prov=prov0; p->colonized=true;
    p->pop=4000;
    p->cap_tier=capitale_max_tier(p->pop); p->prod_mult=1.f;   /* capitale développée (la pop débloque) */
    p->pop_by_class[LAB_LABORER]=3200; p->pop_by_class[LAB_ARTISAN]=600; p->pop_by_class[LAB_ELITE]=200;
    /* 2 collecteurs + 2 ateliers, niveau 0 (1 slot chacun), REMPLIS. */
    p->bld[0]=(LBuilding){ LB_COLLECTOR, 0, 1 };
    p->bld[1]=(LBuilding){ LB_COLLECTOR, 0, 1 };
    p->bld[2]=(LBuilding){ LB_WORKSHOP,  0, 1 };
    p->bld[3]=(LBuilding){ LB_WORKSHOP,  0, 1 };
    p->n_bld=4;
    e->stock[LR_FOOD]=200; e->stock[LR_GOLD]=200; e->stock[LR_MATERIALS]=200;
    e->treasury=200;
}

/* ===================================================================== */
/* INTÉGRATION — seeder l'économie depuis un VRAI pays du monde           */
/* ===================================================================== */
void labor_seed_from_world(LaborEcon *e, const World *w, const WorldEconomy *econ, int cid){
    /* on garde la géo précalculée (labor_init) ; on (ré)installe l'économie. */
    e->n_prov=0;
    memset(e->stock,0,sizeof e->stock); memset(e->flow,0,sizeof e->flow);
    e->stock[LR_FOOD]=200; e->stock[LR_GOLD]=200; e->stock[LR_MATERIALS]=200;
    e->market.supply=1.f; e->market.price=BASE_PRICE; e->treasury=200;

    for (int r=0; r<econ->n_regions && e->n_prov<LAB_MAX_PROV; r++){
        if (econ->region[r].owner!=cid || !econ->region[r].culture.settled) continue;
        int pid = (r<w->n_regions) ? w->region[r].province_ids[0] : -1;
        if (pid<0 || pid>=w->n_provinces) continue;
        long pop = (long)(econ->region[r].strata[CLASS_LABORER].pop
                        + econ->region[r].strata[CLASS_BOURGEOIS].pop
                        + econ->region[r].strata[CLASS_ELITE].pop);
        if (pop<100) pop=100;
        LProvince *p=&e->prov[e->n_prov++];
        memset(p,0,sizeof(*p));
        p->prov=pid; p->colonized=true; p->pop=pop;
        p->cap_tier=capitale_max_tier(pop); p->prod_mult=1.f;   /* capitale développée que la pop débloque */
        p->pop_by_class[LAB_LABORER]=pop*8/10;       /* repli ; les classes ÉMERGENT au 1er tick (§5) */
        p->pop_by_class[LAB_ARTISAN]=pop*15/100;
        p->pop_by_class[LAB_ELITE]  =pop - p->pop_by_class[LAB_LABORER] - p->pop_by_class[LAB_ARTISAN];
        /* Bâtiments choisis sur la GÉO réelle : collecteur + marché + extraction. */
        p->bld[0]=(LBuilding){ LB_COLLECTOR, 0, 1 };
        p->bld[1]=(LBuilding){ LB_MARKET,    0, 1 };
        LBuildType ex = LB_QUARRY;
        if      (e->g_pres[pid][LR_METAL] >0.3f) ex=LB_MINE;
        else if (e->g_pres[pid][LR_BOIS]  >0.3f) ex=LB_SAWMILL;
        else if (e->g_pres[pid][LR_ARGILE]>0.3f) ex=LB_CLAYPIT;
        p->bld[2]=(LBuilding){ ex, 0, 1 };
        p->n_bld=3;
    }
}

float labor_prosperity_index(const LaborEcon *e){
    long pop = labor_pop_total(e); if (pop<1) pop=1;
    float per100  = (float)pop/100.f;
    float foodsec = (labor_food_balance(e) >= 0) ? 3.0f : 0.0f;   /* le pain d'abord */
    float gold_pc = (float)e->flow[LR_GOLD]      / per100;        /* revenu par tête */
    float mat_pc  = (float)e->flow[LR_MATERIALS] / per100;        /* matériaux par tête */
    float idx = foodsec + clampf(gold_pc*5.0f, 0.f, 4.f) + clampf(mat_pc*4.0f, 0.f, 3.f);
    return clampf(idx, 0.f, 10.f);
}

/* ===================================================================== */
/* SORTIES LUES DE LA GÉO (§5)                                            */
/* ===================================================================== */
float province_trade_flow(const LaborEcon *e, int prov){
    return (prov>=0&&prov<SCPS_MAX_PROV) ? e->g_flow[prov] : 0.f;
}
float province_fertility(const LaborEcon *e, int prov){
    return (prov>=0&&prov<SCPS_MAX_PROV) ? e->g_fert[prov] : 0.f;
}
float labor_market_output(const LaborEcon *e, int prov, int jobs_filled){
    return MARKET_BASE * province_trade_flow(e,prov) * (float)jobs_filled;
}
static float per_job_output(const LaborEcon *e, int wprov, LBuildType t, LRes *out){
    switch(t){
        case LB_COLLECTOR: *out=LR_FOOD;      return COLLECTOR_BASE + COLLECTOR_GEO*e->g_fert[wprov];
        case LB_GRANARY:   *out=LR_FOOD;      return GRANARY_BASE * e->g_fert[wprov];
        case LB_MARKET:    *out=LR_GOLD;      return MARKET_BASE * e->g_flow[wprov];
        case LB_SAWMILL:   *out=LR_BOIS;      return EXTRACT_BASE * e->g_pres[wprov][LR_BOIS];
        case LB_CLAYPIT:   *out=LR_ARGILE;    return EXTRACT_BASE * e->g_pres[wprov][LR_ARGILE];
        case LB_QUARRY:    *out=LR_CALCAIRE;  return EXTRACT_BASE * e->g_pres[wprov][LR_CALCAIRE];
        case LB_MINE:      *out=LR_METAL;     return EXTRACT_BASE * e->g_pres[wprov][LR_METAL];
        case LB_WORKSHOP:  *out=LR_MATERIALS; return WORKSHOP_BASE;   /* potentiel ; gaté par intrants */
        default:           *out=LR_COUNT;     return 0.f;
    }
}
float labor_building_output(const LaborEcon *e, int prov, int bld_idx, LRes *out_res){
    LRes dummy; if(!out_res) out_res=&dummy;
    if (prov<0||prov>=e->n_prov||bld_idx<0||bld_idx>=e->prov[prov].n_bld){ *out_res=LR_COUNT; return 0.f; }
    const LProvince *p=&e->prov[prov];
    const LBuilding *b=&p->bld[bld_idx];
    /* LA RÈGLE : per-job (lu de la géo) × JOBS REMPLIS. Vide → 0. */
    return per_job_output(e, p->prov, b->type, out_res) * (float)b->jobs_filled;
}

/* ===================================================================== */
/* NOURRITURE & FAMINE (§2)                                               */
/* ===================================================================== */
long labor_food_collected(const LaborEcon *e){
    double f=0.0;
    for (int i=0;i<e->n_prov;i++) for (int b=0;b<e->prov[i].n_bld;b++){
        const LBuilding *bd=&e->prov[i].bld[b];
        if (bd->type==LB_COLLECTOR||bd->type==LB_GRANARY){
            LRes o; f += per_job_output(e, e->prov[i].prov, bd->type, &o)*(float)bd->jobs_filled;
        }
    }
    return (long)(f+0.5);
}
long labor_food_consumed(const LaborEcon *e){
    long slots=0;
    for (int i=0;i<e->n_prov;i++) for (int b=0;b<e->prov[i].n_bld;b++)
        slots += e->prov[i].bld[b].jobs_filled;          /* pop EMPLOYÉE (§2) */
    return (long)(slots*FOOD_PER_SLOT);
}
long labor_food_balance(const LaborEcon *e){ return labor_food_collected(e) - labor_food_consumed(e); }

/* ===================================================================== */
/* MARCHÉ & PRIX DYNAMIQUE (§7)                                           */
/* ===================================================================== */
float labor_material_price(const LaborEcon *e){
    return BASE_PRICE * clampf(e->market.demand / fmaxf(e->market.supply,1.f), PRICE_MIN, PRICE_MAX);
}
long labor_pump_market(LaborEcon *e, long amount){
    if (amount<=0) return 0;
    float price = labor_material_price(e);
    long cost = (long)(amount*price + 0.5f);
    e->stock[LR_MATERIALS] += amount;
    e->stock[LR_GOLD]      -= cost; e->treasury=e->stock[LR_GOLD];
    e->market.demand       += (float)amount;   /* pomper TIRE la demande → le prix monte */
    return cost;
}

/* ===================================================================== */
/* POPULATION — le POOL et sa répartition (topbar)                        */
/* ===================================================================== */
long labor_pop_total(const LaborEcon *e){
    long t=0; for (int i=0;i<e->n_prov;i++) t+=e->prov[i].pop; return t;
}
long labor_pop_employed(const LaborEcon *e){
    long slots=0;
    for (int i=0;i<e->n_prov;i++) for (int b=0;b<e->prov[i].n_bld;b++)
        slots += e->prov[i].bld[b].jobs_filled;
    return slots * POP_PER_SLOT;          /* affectés au travail — TOUJOURS dans le pool */
}
long labor_pop_in_army(const LaborEcon *e){
    long a=0; for (int i=0;i<e->n_prov;i++) a+=e->prov[i].pop_in_army; return a;
}
PopBreakdown labor_pop_breakdown(const LaborEcon *e){
    PopBreakdown k;
    k.total   = labor_pop_total(e);
    k.in_jobs = labor_pop_employed(e);
    k.in_army = labor_pop_in_army(e);
    k.free    = k.total - k.in_jobs - k.in_army;   /* le reste non affecté */
    if (k.free<0) k.free=0;                          /* sur-affectation : bornée */
    return k;
}
void labor_print_topbar(const LaborEcon *e){
    PopBreakdown k=labor_pop_breakdown(e);
    printf("   TOPBAR  Or %ld (%+ld/j) · Nourriture %ld (%+ld/j) · Matériaux %ld (%+ld/j)\n",
           e->stock[LR_GOLD], e->flow[LR_GOLD], e->stock[LR_FOOD], e->flow[LR_FOOD],
           e->stock[LR_MATERIALS], e->flow[LR_MATERIALS]);
    printf("           Pop %ld :  libre %ld · en job %ld · en armée %ld\n",
           k.total, k.free, k.in_jobs, k.in_army);
}

/* ===================================================================== */
/* TAXES (§8)                                                             */
/* ===================================================================== */
float labor_taxes(const LProvince *p){
    return p->pop_by_class[LAB_LABORER]*TAX_LABORER
         + p->pop_by_class[LAB_ARTISAN]*TAX_ARTISAN
         + p->pop_by_class[LAB_ELITE]  *TAX_ELITE;
}

/* ===================================================================== */
/* COLONISATION (§6)                                                      */
/* ===================================================================== */
ColonizeCost labor_colonize_cost(const LaborEcon *e, int prov){
    ColonizeCost c; c.materials=COLONIZE_MAT; c.food=COLONIZE_FOOD;
    /* une bonne terre coûte plus cher à réclamer (la géo, encore). */
    if (prov>=0&&prov<SCPS_MAX_PROV) c.materials += (long)(e->g_flow[prov]*15.f);
    return c;
}
bool labor_can_colonize(const LaborEcon *e, ColonizeCost cost){
    return e->stock[LR_MATERIALS]>=cost.materials && e->stock[LR_FOOD]>=cost.food;
}
bool labor_colonize(LaborEcon *e, int prov){
    if (e->n_prov>=LAB_MAX_PROV) return false;
    ColonizeCost cost=labor_colonize_cost(e,prov);
    if (!labor_can_colonize(e,cost)) return false;
    e->stock[LR_MATERIALS]-=cost.materials; e->stock[LR_FOOD]-=cost.food;
    LProvince *p=&e->prov[e->n_prov++];
    memset(p,0,sizeof(*p));
    p->prov=prov; p->colonized=true; p->pop=500; p->pop_by_class[LAB_LABORER]=500;
    p->cap_tier=1; p->prod_mult=1.f;                /* la capitale obligatoire dès la colonie (§1) */
    return true;
}

/* ===================================================================== */
/* DÉVELOPPEMENT DE LA POP (§10)                                          */
/* ===================================================================== */
bool province_fully_colonized(const LProvince *p){
    return p->colonized && p->n_bld>=LAB_BUILDINGS_PER_PROV;
}
float labor_pop_dev_speed(const LProvince *p, int prosperity_0_100){
    float s=POP_DEV_BASE;
    if (province_fully_colonized(p) && prosperity_0_100>=100) s*=1.15f;   /* plein dev (§10) */
    return s;
}

/* ===================================================================== */
/* PRODUCTION & CHAÎNES (§4) — le cœur de la boucle                       */
/* ===================================================================== */
/* Un atelier combine : pour chaque slot rempli, trouve une recette dont les
 * DEUX intrants sont en stock, les consomme → 1 sortie. Sans intrants, rien. */
static long run_workshop(LaborEcon *e, int jobs){
    long made=0;
    for (int j=0;j<jobs;j++){
        for (int r=0;r<N_RECIPES;r++){
            const Recipe *R=&RECIPES[r];
            if (e->stock[R->in_a]>=1 && e->stock[R->in_b]>=1){
                e->stock[R->in_a]-=1; e->stock[R->in_b]-=1; e->stock[R->out]+=1; made++;
                break;
            }
        }
    }
    return made;
}

/* Croissance de la pop, GATÉE par la nourriture (§2). Elle s'applique au POOL
 * TOTAL : les pop engagées (jobs, armée) se reproduisent comme les autres — elles
 * ne disparaissent pas du pool. La famine STOPPE puis inverse la croissance. */
#define GROWTH_PERMIL    20         /* +2 %/tick quand la nourriture suit (sous le seuil log) */
#define LABOR_LOG_THRESH 10000L     /* au-delà : la croissance devient LOGARITHMIQUE (rendements décroissants) */
#define LABOR_POP_MAX    1000000000L/* garde-fou DUR : sans la dampe log + ce plafond, la croissance
                                     * journalière (+2 %/labor_tick) emballait la pop jusqu'au DÉBORDEMENT
                                     * de `long` (UBSan : pop·20 hors-bornes) — et la somme warhost avec. */
static void pop_growth(LaborEcon *e){
    bool famine = (e->stock[LR_FOOD] <= 0);
    for (int i=0;i<e->n_prov;i++){
        LProvince *p=&e->prov[i];
        if (p->pop<=0) continue;
        /* CROISSANCE : +2 % sous 10k/province ; au-delà, LOGARITHMIQUE — la pop continue de
         * monter mais de moins en moins vite (le delta croît en ln(pop), donc le TAUX s'effondre)
         * → fin de l'emballement exponentiel, pop bornée, plus de débordement de `long`. */
        long delta;
        if (famine)                       delta = -(p->pop/100 + 1);
        else if (p->pop <= LABOR_LOG_THRESH) delta = p->pop / (1000/GROWTH_PERMIL);   /* ≡ +2 %, overflow-safe (pop/50) */
        else {
            float over = logf((float)p->pop / (float)LABOR_LOG_THRESH);  /* 0 au seuil, croît lentement */
            delta = (long)((float)(LABOR_LOG_THRESH/(1000/GROWTH_PERMIL)) * (1.0f + over));  /* continu au seuil (=200) */
        }
        if (delta==0 && !famine) delta=1;
        /* LOGEMENT = capacité CONFORTABLE : au-delà, la pop croît encore (gatée par la
         * nourriture) mais MOINS VITE, et le surpeuplement monte l'agitation (§agitation). */
        if (!famine && p->house_cap>0 && p->pop >= p->house_cap) delta = (delta+1)/2;   /* surpeuplé : ralenti */
        long np=p->pop+delta; if (np<0) np=0; if (np>LABOR_POP_MAX) np=LABOR_POP_MAX;   /* plancher 0, plafond dur */
        long d=np-p->pop; p->pop=np;
        p->pop_by_class[LAB_LABORER]+=d;                 /* la croissance gonfle la masse */
        if (p->pop_by_class[LAB_LABORER]<0) p->pop_by_class[LAB_LABORER]=0;
    }
}

void labor_tick(LaborEcon *e){
    long before[LR_COUNT]; memcpy(before, e->stock, sizeof before);
    float supply_mat=0.f;

    /* 0. CAPITALE : améliorer si la pop le débloque & la recette est payable, puis
     *    faire ÉMERGER les classes des emplois (tier→Nobles, ateliers→Bourgeois) et
     *    délivrer logement/services/productivité (gatés par les Nobles en poste). */
    for (int i=0;i<e->n_prov;i++) capitale_upgrade(&e->prov[i], e);
    for (int i=0;i<e->n_prov;i++) capitale_mobility_tick(&e->prov[i]);

    /* 1. EXTRACTION + collecte + marché : jobs remplis → sorties (lues de la géo),
     *    × la PRODUCTIVITÉ de la capitale (+5 %/tier servi). */
    for (int i=0;i<e->n_prov;i++){
        LProvince *p=&e->prov[i];
        for (int b=0;b<p->n_bld;b++){
            LBuilding *bd=&p->bld[b];
            if (bd->type==LB_WORKSHOP) continue;     /* les ateliers passent en 2 */
            LRes o; float out=per_job_output(e,p->prov,bd->type,&o)*(float)bd->jobs_filled*p->prod_mult;
            if (o<LR_COUNT) e->stock[o]+=(long)(out+0.5f);
        }
    }
    /* 2. ATELIERS : chaînes de matériaux (consomment les bruts extraits). */
    for (int i=0;i<e->n_prov;i++){
        LProvince *p=&e->prov[i];
        for (int b=0;b<p->n_bld;b++) if (p->bld[b].type==LB_WORKSHOP)
            supply_mat += (float)run_workshop(e, p->bld[b].jobs_filled);
    }
    /* 3. NOURRITURE : la collecte a été ajoutée en passe 1 ; on retire la conso.
     * Net du tick = collecte − conso = balance (famine si négatif → gate la croissance). */
    e->stock[LR_FOOD] -= labor_food_consumed(e);

    /* 4. MARCHÉ : l'offre de matériaux du tick met à jour le prix. */
    e->market.supply = fmaxf(1.f, supply_mat);
    e->market.price  = labor_material_price(e);
    e->treasury      = e->stock[LR_GOLD];

    /* 5. CROISSANCE de la pop, gatée par la nourriture ET par le LOGEMENT de la
     * capitale (plafond). 6. on RE-ÉMERGE les classes (la pop a bougé). 7. flux. */
    pop_growth(e);
    for (int i=0;i<e->n_prov;i++) capitale_mobility_tick(&e->prov[i]);
    for (int r=0;r<LR_COUNT;r++) e->flow[r]=e->stock[r]-before[r];
}

/* ===================================================================== */
/* LIBELLÉS                                                              */
/* ===================================================================== */
const char *lres_name(LRes r){
    static const char *N[LR_COUNT]={ "Nourriture","Or","Matériaux","Bois","Argile",
                                     "Calcaire","Pierre","Métal","Outils" };
    return (r>=0&&r<LR_COUNT)?N[r]:"?";
}
const char *lbuild_name(LBuildType b){
    static const char *N[LB_TYPE_COUNT]={ "—","Collecteur","Grenier","Marché","Scierie",
                                          "Argilière","Carrière","Mine","Atelier" };
    return (b>=0&&b<LB_TYPE_COUNT)?N[b]:"?";
}
