/*
 * scps_econ.c — moteur de simulation économique (voir scps_econ.h)
 *
 * Modèle volontairement causal et lisible : chaque chiffre a une raison
 * géographique ou démographique. Aucune valeur n'est posée « au hasard » à
 * la simulation — l'aléa est dans la génération du monde, pas dans l'éco.
 */
#include "scps_econ.h"
#include "scps_world.h"   /* resource_name() */
#include <stdio.h>
#include <string.h>
#include <math.h>

/* ====================================================================== */
/* TABLES DE CONSTANTES                                                    */
/* ====================================================================== */

/* Prix de base par bien (monnaie/unité). Brutes bon marché, manufacturés
 * chers, luxe très cher. Sert d'ancre au prix de marché. */
static const float BASE_PRICE[RES_COUNT] = {
    [RES_NONE]          = 0.f,
    /* brutes agricoles */
    [RES_GRAIN]         = 1.0f,
    [RES_LIVESTOCK]     = 1.6f,
    [RES_WOOL]          = 1.8f,
    [RES_FISH]          = 1.2f,
    [RES_FUR]           = 3.0f,
    [RES_SALT]          = 2.2f,
    [RES_COTTON]        = 1.9f,
    [RES_SUGAR]         = 2.0f,
    [RES_WOOD]          = 1.0f,
    [RES_MED_HERBS]     = 3.5f,
    /* brutes minérales */
    [RES_COPPER]        = 2.6f,
    [RES_IRON]          = 2.4f,
    [RES_COAL]          = 1.8f,
    [RES_SULFUR]        = 3.0f,
    [RES_SALTPETER]     = 3.2f,
    [RES_GOLD]          = 8.0f,
    [RES_PRECIOUS_METAL]= 12.0f,
    /* manufacturés */
    [RES_CLOTH]         = 4.5f,
    [RES_NAVAL_SUPPLIES]= 4.0f,
    [RES_WINE]          = 5.0f,
    [RES_PRECIOUS_WARE] = 22.0f,
    [RES_PRECIOUS_CLOTH]= 18.0f,
    [RES_PAPER]         = 5.5f,
};

/* Recette d'une manufacture : jusqu'à 2 intrants → 1 produit. */
typedef struct {
    Resource in1;  float q1;
    Resource in2;  float q2;   /* in2 = RES_NONE si une seule entrée */
    Resource out;  float qout;
    float    labor;            /* besoin de main-d'œuvre par niveau */
} Recipe;

static const Recipe RECIPE[BLD_TYPE_COUNT] = {
    [BLD_TEXTILE]   = { RES_WOOL,  2.0f, RES_NONE,          0.f, RES_CLOTH,          1.0f, 1.0f },
    [BLD_SAWMILL]   = { RES_WOOD,  2.0f, RES_NONE,          0.f, RES_NAVAL_SUPPLIES, 1.0f, 0.8f },
    [BLD_PAPERMILL] = { RES_WOOD,  1.5f, RES_NONE,          0.f, RES_PAPER,          1.0f, 0.7f },
    [BLD_WINERY]    = { RES_SUGAR, 2.0f, RES_NONE,          0.f, RES_WINE,           1.0f, 0.9f },
    [BLD_JEWELER]   = { RES_GOLD,  1.0f, RES_PRECIOUS_METAL,1.0f, RES_PRECIOUS_WARE, 1.0f, 1.2f },
    [BLD_WEAVER_LUX]= { RES_CLOTH, 2.0f, RES_NONE,          0.f, RES_PRECIOUS_CLOTH, 1.0f, 1.1f },
};

/* Besoins par tête et par strate (unités/100 hab/tick). Le grain (vivres)
 * est universel ; le reste monte en gamme avec la classe. */
static const float NEED[CLASS_COUNT][RES_COUNT] = {
    [CLASS_LABORER] = {
        [RES_GRAIN]=1.00f, [RES_FISH]=0.20f, [RES_WOOD]=0.30f, [RES_CLOTH]=0.20f,
    },
    [CLASS_BOURGEOIS] = {
        [RES_GRAIN]=1.00f, [RES_CLOTH]=0.50f, [RES_PAPER]=0.25f, [RES_WINE]=0.30f,
        [RES_SALT]=0.20f,
    },
    [CLASS_ELITE] = {
        [RES_GRAIN]=1.00f, [RES_WINE]=0.70f, [RES_PAPER]=0.35f,
        [RES_PRECIOUS_WARE]=0.45f, [RES_PRECIOUS_CLOTH]=0.45f, [RES_FUR]=0.30f,
    },
};

/* Part de chaque strate dans la population à l'initialisation. */
static const float CLASS_SHARE[CLASS_COUNT] = { 0.80f, 0.15f, 0.05f };

#define TAX_RATE     0.15f   /* part de la valeur produite captée par les élites */
#define WAGE_SHARE   0.55f   /* part de la valeur → salaires (laborers) */
/* le reste (1 - TAX - WAGE) = profit bourgeois */
#define TECH_RATE    0.010f  /* conversion richesse élite → tech */
#define PRICE_INERTIA 0.65f  /* lissage du prix (0=instantané,1=figé) */
#define EPS          1e-4f

/* Démographie calibrée : doublement en ~30 ans à food_sat=1, society_sat=0.5
 *   net = BIRTH_RATE*food_sat - DEATH_RATE + SOCIETY_BONUS*society_sat
 *   typique : 0.034 - 0.015 + 0.004 = 0.023 → ln(2)/0.023 ≈ 30 ticks */
#define BIRTH_RATE    0.034f
#define DEATH_RATE    0.015f
#define SOCIETY_BONUS 0.008f

/* Colonisation */
#define COLONY_MIN_POP      500.f   /* pop minimale d'une région pour essaimer  */
#define COLONY_COST_POP     250.f   /* colons détachés (quittent la mère)       */
#define COLONY_SEED_POP     100.f   /* pop installée dans la nouvelle région    */
#define COLONY_FOOD_GATE    0.35f   /* seuil de subsistance pour essaimer        */

/* Migration interne */
#define MIGRATE_RATE        0.02f   /* fraction max de bourgeois/élites migrant/tick */
#define MIGRATE_THRESHOLD   1.30f   /* différentiel de prospérité déclencheur        */
#define DIASPORA_TECH_RATE  0.0008f /* tech/tick par unité de diaspora               */
#define DIASPORA_DECAY      0.98f   /* acculturation : diaspora s'absorbe (~50 ticks) */

/* Relocalisation forcée */
#define RELOC_COERCION_BASE 0.25f   /* pic de coercition de base par évènement       */
#define COERCION_DECAY      0.93f   /* demi-vie ≈ 10 ticks                            */

static inline float clampf(float v,float lo,float hi){return v<lo?lo:(v>hi?hi:v);}

const char *social_class_name(SocialClass c) {
    static const char *N[CLASS_COUNT]={"Laborers","Bourgeois","Élites"};
    return (c>=0&&c<CLASS_COUNT)?N[c]:"?";
}
const char *building_name(BuildingType b) {
    static const char *N[BLD_TYPE_COUNT]={
        "Manufacture textile","Scierie navale","Papeterie",
        "Domaine viticole","Joaillerie","Atelier d'étoffe précieuse"
    };
    return (b>=0&&b<BLD_TYPE_COUNT)?N[b]:"?";
}

/* ====================================================================== */
/* INITIALISATION                                                         */
/* ====================================================================== */

/* Ajoute une manufacture de type t si absente, et renvoie son index. */
static int region_ensure_building(RegionEconomy *re, BuildingType t) {
    for (int i=0;i<re->n_bld;i++) if (re->bld[i].type==t) return i;
    if (re->n_bld>=ECON_MAX_BLD) return -1;
    int i=re->n_bld++;
    re->bld[i].type=t; re->bld[i].level=0.f; re->bld[i].workers=0.f;
    return i;
}

/* Injecte une population répartie en strates dans une région (peuplement
 * initial ou arrivée de colons). N'écrase pas les manufactures/prix. */
static void econ_seed_population(RegionEconomy *re, float total_pop) {
    for (int c=0;c<CLASS_COUNT;c++) {
        re->strata[c].pop         = total_pop*CLASS_SHARE[c];
        re->strata[c].wealth      = re->strata[c].pop * (c==CLASS_ELITE?6.f:c==CLASS_BOURGEOIS?2.f:0.5f);
        re->strata[c].satisfaction= 0.5f;
    }
}

void econ_init(WorldEconomy *e, const World *w) {
    memset(e,0,sizeof(*e));
    e->n_regions=w->n_regions;
    e->tick=0;

    /* ---- Passe 1 : capacité de chaque région (fertilité × surface) ------- */
    float reg_cap[SCPS_MAX_REG]={0};
    float cty_cap[SCPS_MAX_COUNTRY]={0};
    for (int rid=0; rid<w->n_regions; rid++) {
        const Region *rg=&w->region[rid];
        float cap=0.f, area=0.f;
        for (int k=0;k<rg->n_provinces;k++) {
            int pid=rg->province_ids[k];
            if (pid<0||pid>=w->n_provinces) continue;
            const Province *pv=&w->province[pid];
            cap  += pv->area * (0.25f + 0.75f*clampf(pv->subsistance/10.f,0.f,1.f));
            area += pv->area;
        }
        if (area<1.f) continue;
        reg_cap[rid]=cap;
        int cid=rg->country;
        if (cid>=0 && cid<SCPS_MAX_COUNTRY) cty_cap[cid]+=cap;
    }

    /* ---- Passe 2 : cible par pays (4000 majeur ≥4 régions, 2000 satellite) */
    float cty_target[SCPS_MAX_COUNTRY]={0};
    int   cty_nreg  [SCPS_MAX_COUNTRY]={0};
    for (int rid=0; rid<w->n_regions; rid++) {
        int cid=w->region[rid].country;
        if (cid>=0 && cid<SCPS_MAX_COUNTRY) cty_nreg[cid]++;
    }
    for (int cid=0; cid<SCPS_MAX_COUNTRY; cid++)
        if (cty_cap[cid]>0.f)
            cty_target[cid] = (cty_nreg[cid]>=4) ? 4000.f : 2000.f;

    /* ---- Passe 3 : peuplement de chaque région --------------------------- */
    for (int rid=0; rid<w->n_regions; rid++) {
        RegionEconomy *re=&e->region[rid];
        const Region *rg=&w->region[rid];

        float area_sum=0.f;
        for (int k=0;k<rg->n_provinces;k++) {
            int pid=rg->province_ids[k];
            if (pid<0||pid>=w->n_provinces) continue;
            area_sum += w->province[pid].area;
        }
        if (area_sum<1.f || reg_cap[rid]<=0.f) { re->active=false; continue; }
        re->active=true;
        re->colonized=false;
        re->owner=-1;

        /* Capacité d'accueil : pop cible à terme (sert au peuplement initial
         * de la capitale et de plafond souple à la croissance). */
        int cid=rg->country;
        float total_pop;
        if (cid>=0 && cid<SCPS_MAX_COUNTRY && cty_cap[cid]>0.f)
            total_pop = cty_target[cid] * reg_cap[rid] / cty_cap[cid];
        else
            total_pop = 40.f + reg_cap[rid]*12.f;
        re->cap_pop = total_pop;

        /* Population : laissée à ZÉRO par défaut. Le monde démarre vide ;
         * seules la capitale du joueur et quelques cités-états seront
         * peuplées (voir plus bas). Tout le reste est colonisable. */
        for (int c=0;c<CLASS_COUNT;c++) {
            re->strata[c].pop         = 0.f;
            re->strata[c].wealth      = 0.f;
            re->strata[c].satisfaction= 0.5f;
        }
        re->food_sat=0.5f; re->society_sat=0.5f;

        /* ---- Capacité d'extraction : héritée des ressources brutes des
         *      provinces. Chaque province « pose » sa ressource dominante. */
        bool coastal=false;
        for (int k=0;k<rg->n_provinces;k++) {
            int pid=rg->province_ids[k];
            if (pid<0||pid>=w->n_provinces) continue;
            const Province *pv=&w->province[pid];
            if (pv->coastal) coastal=true;
            Resource r=pv->resource;
            if (r<=RES_NONE || r>=RES_PROD_FIRST) continue;  /* brutes seules */
            /* débit proportionnel à la surface de la province */
            re->raw_cap[r] += 1.5f + pv->area*0.05f;
        }

        /* Subsistance locale : vivres et bois de feu dimensionnés pour couvrir
         * ~90% de la population, laissant la satisfaction refléter les biens
         * supérieurs et non une famine universelle. */
        float subsist = total_pop / 100.f;
        re->raw_cap[RES_GRAIN] += subsist * 0.95f;
        re->raw_cap[RES_WOOD]  += subsist * 0.40f;
        if (coastal) re->raw_cap[RES_FISH] += subsist * 0.30f;

        /* ---- Manufactures : implantées là où l'intrant est extrait dans
         *      la région (cohérence géographique de la chaîne de prod). */
        if (re->raw_cap[RES_WOOL] > 0.f)  region_ensure_building(re,BLD_TEXTILE);
        if (re->raw_cap[RES_WOOD] > 0.f) {
            region_ensure_building(re,BLD_SAWMILL);
            region_ensure_building(re,BLD_PAPERMILL);
        }
        if (re->raw_cap[RES_SUGAR] > 0.f) region_ensure_building(re,BLD_WINERY);
        if (re->raw_cap[RES_GOLD] > 0.f && re->raw_cap[RES_PRECIOUS_METAL] > 0.f)
            region_ensure_building(re,BLD_JEWELER);
        /* L'atelier de luxe a besoin de tissu : présent si on file la laine. */
        if (re->raw_cap[RES_WOOL] > 0.f) region_ensure_building(re,BLD_WEAVER_LUX);

        /* Niveau initial des manufactures : dimensionné sur la capacité
         * d'accueil (l'infrastructure latente du site). */
        float invest = re->cap_pop*CLASS_SHARE[CLASS_BOURGEOIS];
        for (int i=0;i<re->n_bld;i++)
            re->bld[i].level = 0.5f + invest*0.01f;

        /* ---- Prix & stock de départ. */
        for (int r=0;r<RES_COUNT;r++) {
            re->price[r]=BASE_PRICE[r];
            re->stock[r]=0.f;
        }
    }

    /* ---- Adjacence de régions (terre, 4-connexe) pour la colonisation ---- */
    static const int DX4[4]={1,-1,0,0}, DY4[4]={0,0,1,-1};
    for (int y=0;y<SCPS_H;y++) for (int x=0;x<SCPS_W;x++) {
        int ra=w->cell[scps_idx(x,y)].region;
        if (ra<0) continue;
        for (int d=0;d<4;d++) {
            int nx=x+DX4[d], ny=y+DY4[d];
            if (nx<0||nx>=SCPS_W||ny<0||ny>=SCPS_H) continue;
            int rb=w->cell[scps_idx(nx,ny)].region;
            if (rb<0||rb==ra) continue;
            e->adj[ra][rb]=1; e->adj[rb][ra]=1;
        }
    }

    /* ---- Peuplement initial : monde quasi vide ----------------------------- *
     * JOUEUR / ANTAGONISTE : seule la région-capitale est peuplée à cap_pop.
     *   Le reste de leur territoire est vierge → ils colonisent.
     * CITÉ-ÉTAT : TOUTES leurs régions sont peuplées à pop réduite (2000 / n_regs
     *   par région, plafonnée à cap_pop). Elles colonisent ensuite leurs propres
     *   territoires vacants mais n'en sortent jamais.
     * Tout le reste du monde est vierge et colonisable. */
    for (int cid=0; cid<w->n_countries; cid++) {
        const Country *ct=&w->country[cid];
        PolityRole role=ct->role;

        if (role==POLITY_PLAYER || role==POLITY_ANTAGONIST) {
            int cap_prov=ct->capital_prov;
            if (cap_prov<0||cap_prov>=w->n_provinces) continue;
            int cap_reg=w->province[cap_prov].region;
            if (cap_reg<0||cap_reg>=e->n_regions) continue;
            RegionEconomy *re=&e->region[cap_reg];
            if (!re->active) continue;
            econ_seed_population(re, re->cap_pop);
            re->colonized=true;
            re->owner=(int16_t)cid;

        } else if (role==POLITY_CITY_STATE) {
            /* Compter les régions actives du pays */
            int n_act=0;
            for (int ri=0;ri<ct->n_regions;ri++){
                int rid=ct->region_ids[ri];
                if (rid>=0&&rid<e->n_regions&&e->region[rid].active) n_act++;
            }
            if (n_act==0) continue;
            /* 2000 pop répartis uniformément (plafond = cap_pop de chaque région).
             * À 3 régions → ~667/reg ; à 5 → ~400/reg. Les régions vierges seront
             * colonisées depuis les régions sœurs (econ_colonize_tick). */
            float pop_per_reg = 2000.f / (float)n_act;
            for (int ri=0;ri<ct->n_regions;ri++){
                int rid=ct->region_ids[ri];
                if (rid<0||rid>=e->n_regions) continue;
                RegionEconomy *re=&e->region[rid];
                if (!re->active) continue;
                econ_seed_population(re, fminf(re->cap_pop, pop_per_reg));
                re->colonized=true;
                re->owner=(int16_t)cid;
            }
        }
    }
}

/* ====================================================================== */
/* SIMULATION — un tick                                                   */
/* ====================================================================== */

void econ_tick(WorldEconomy *e) {
    e->tick++;

    for (int rid=0; rid<e->n_regions; rid++) {
        RegionEconomy *re=&e->region[rid];
        if (!re->active || !re->colonized) continue;

        float supply[RES_COUNT]={0}, demand[RES_COUNT]={0};
        float labor_avail = re->strata[CLASS_LABORER].pop;
        float labor_used  = 0.f;
        float gdp         = 0.f;
        float wage_pool   = 0.f;   /* → laborers */
        float profit_pool = 0.f;   /* → bourgeois */
        float tax_pool    = 0.f;   /* → élites */

        /* ---- 1. EXTRACTION des matières premières ----------------------
         * Emploie des laborers ; chaque unité extraite demande 0.5 de
         * main-d'œuvre. Limité par la main-d'œuvre disponible. */
        for (int r=0;r<RES_COUNT;r++) {
            if (re->raw_cap[r]<=0.f) continue;
            float want_labor = re->raw_cap[r]*0.5f;
            float avail = labor_avail-labor_used;
            float ratio = (want_labor>0.f)? clampf(avail/want_labor,0.f,1.f) : 0.f;
            float out = re->raw_cap[r]*ratio;
            labor_used += want_labor*ratio;
            re->stock[r] += out;
            supply[r]    += out;
            float value = out*re->price[r];
            gdp += value;
            wage_pool   += value*WAGE_SHARE;
            profit_pool += value*(1.f-WAGE_SHARE-TAX_RATE);
            tax_pool    += value*TAX_RATE;
        }

        /* ---- 2. MANUFACTURE -------------------------------------------- */
        for (int i=0;i<re->n_bld;i++) {
            Building *b=&re->bld[i];
            const Recipe *rc=&RECIPE[b->type];
            /* Production cible = niveau ; bornée par intrants en stock et
             * par la main-d'œuvre restante. */
            float cap = b->level;
            float lim = cap;
            if (rc->in1!=RES_NONE) lim=fminf(lim, re->stock[rc->in1]/fmaxf(rc->q1,EPS));
            if (rc->in2!=RES_NONE) lim=fminf(lim, re->stock[rc->in2]/fmaxf(rc->q2,EPS));
            float want_labor=rc->labor*cap;
            float avail=labor_avail-labor_used;
            float lratio=(want_labor>0.f)?clampf(avail/want_labor,0.f,1.f):0.f;
            lim=fminf(lim, cap*lratio);
            if (lim<=0.f){ b->workers=0.f; continue; }

            /* Consomme intrants, produit sortie */
            if (rc->in1!=RES_NONE){ re->stock[rc->in1]-=lim*rc->q1; demand[rc->in1]+=lim*rc->q1; }
            if (rc->in2!=RES_NONE){ re->stock[rc->in2]-=lim*rc->q2; demand[rc->in2]+=lim*rc->q2; }
            float out=lim*rc->qout;
            re->stock[rc->out]+=out;
            supply[rc->out]+=out;
            b->workers=rc->labor*lim;
            labor_used+=b->workers;

            /* Valeur ajoutée = valeur sortie − valeur intrants */
            float val_out=out*re->price[rc->out];
            float val_in =0.f;
            if (rc->in1!=RES_NONE) val_in+=lim*rc->q1*re->price[rc->in1];
            if (rc->in2!=RES_NONE) val_in+=lim*rc->q2*re->price[rc->in2];
            float va=fmaxf(0.f, val_out-val_in);
            gdp += va;
            wage_pool   += va*WAGE_SHARE;
            profit_pool += va*(1.f-WAGE_SHARE-TAX_RATE);
            tax_pool    += va*TAX_RATE;
        }
        re->gdp=gdp;

        /* ---- 3. REVENUS répartis sur les strates ----------------------- */
        re->strata[CLASS_LABORER].wealth   += wage_pool;
        re->strata[CLASS_BOURGEOIS].wealth += profit_pool;
        re->strata[CLASS_ELITE].wealth     += tax_pool;
        re->treasury += tax_pool;

        /* ---- 4. DEMANDE de consommation par strate --------------------- */
        for (int c=0;c<CLASS_COUNT;c++) {
            float units=re->strata[c].pop/100.f;   /* besoins exprimés /100 hab */
            for (int r=0;r<RES_COUNT;r++) {
                float need=NEED[c][r];
                if (need>0.f) demand[r]+=need*units;
            }
        }

        /* ---- 5. MARCHÉ : prix puis allocation au budget ---------------- */
        /* Prix : converge vers base × demande/(stock+offre). */
        for (int r=0;r<RES_COUNT;r++) {
            if (BASE_PRICE[r]<=0.f) continue;
            float avail=re->stock[r]+supply[r];
            float target=BASE_PRICE[r]*clampf(demand[r]/(avail+EPS),0.2f,6.f);
            re->price[r]=re->price[r]*PRICE_INERTIA + target*(1.f-PRICE_INERTIA);
            re->price[r]=clampf(re->price[r],BASE_PRICE[r]*0.15f,BASE_PRICE[r]*8.f);
        }

        /* Satisfaction par strate : fraction des besoins effectivement
         * achetée, pondérée par la solvabilité (budget vs coût). On sert
         * d'abord les vivres, puis le reste. Le stock disponible plafonne. */
        for (int c=0;c<CLASS_COUNT;c++) {
            float units=re->strata[c].pop/100.f;
            if (units<=0.f){ re->strata[c].satisfaction=0.f; continue; }
            float budget=re->strata[c].wealth;
            float need_w=0.f, met_w=0.f;   /* pondération par valeur du besoin */
            for (int r=0;r<RES_COUNT;r++) {
                float need=NEED[c][r]*units;
                if (need<=0.f) continue;
                float w=BASE_PRICE[r]*need;          /* importance ~ valeur */
                need_w+=w;
                float can_stock=clampf(re->stock[r]/(need+EPS),0.f,1.f);
                float cost=need*can_stock*re->price[r];
                float can_buy=(cost>0.f)?clampf(budget/cost,0.f,1.f):1.f;
                float got=can_stock*can_buy;
                /* consomme stock & budget */
                re->stock[r]-=need*got;
                budget-=need*got*re->price[r];
                met_w+=w*got;
            }
            re->strata[c].wealth=fmaxf(0.f,budget);
            re->strata[c].satisfaction=(need_w>0.f)?met_w/need_w:0.5f;
        }

        /* ---- 6. MISE À JOUR : démographie, tech, satisfaction générale - */
        /* Satisfaction alimentaire : grain + fish (besoins laborers) */
        {
            float food_need=0.f, food_met=0.f;
            float soc_need=0.f,  soc_met=0.f;
            for (int c=0;c<CLASS_COUNT;c++) {
                float units=re->strata[c].pop/100.f;
                for (int r=0;r<RES_COUNT;r++) {
                    float nd=NEED[c][r]*units;
                    if (nd<=0.f) continue;
                    bool is_food=(r==RES_GRAIN||r==RES_FISH||r==RES_LIVESTOCK);
                    float sat=clampf(re->strata[c].satisfaction,0.f,1.f);
                    if (is_food){ food_need+=nd; food_met+=nd*sat; }
                    else        { soc_need +=nd; soc_met +=nd*sat; }
                }
            }
            re->food_sat   = (food_need>0.f)?clampf(food_met/food_need,0.f,1.f):0.5f;
            re->society_sat= (soc_need >0.f)?clampf(soc_met /soc_need ,0.f,1.f):0.5f;
        }

        /* Croissance calibrée : doublement ~30 ans à food_sat=1, soc=0.5
         *   net = BIRTH_RATE*food_sat - DEATH_RATE + SOCIETY_BONUS*society_sat
         * + pic de famine si food_sat < 0.35
         * Plafond souple : la croissance s'annule à 1.1×cap_pop (apex naturel
         * entre 1000 et 6000 selon la capacité du site). */
        float food_s = re->food_sat;
        float soc_s  = re->society_sat;
        float net_growth = BIRTH_RATE*food_s - DEATH_RATE + SOCIETY_BONUS*soc_s;
        if (food_s < 0.35f)
            net_growth -= (0.35f - food_s) * 0.12f;   /* pic de mortalité famine */
        net_growth = clampf(net_growth, -0.10f, 0.06f);

        float total_pop_now=0.f;
        for (int c=0;c<CLASS_COUNT;c++) total_pop_now+=re->strata[c].pop;
        float cap_factor = fmaxf(0.f, 1.f - total_pop_now/(re->cap_pop*1.1f));
        net_growth *= cap_factor;

        float satsum=0.f, popsum=0.f;
        for (int c=0;c<CLASS_COUNT;c++) {
            PopStratum *st=&re->strata[c];
            st->pop *= 1.f + net_growth;
            if (st->pop<1.f) st->pop=1.f;
            satsum+=st->satisfaction*st->pop; popsum+=st->pop;
        }
        re->satisfaction=(popsum>0.f)?satsum/popsum:0.f;
        re->prosperity = re->gdp/(popsum+1.f);

        /* Tech : les élites convertissent richesse × satisfaction en savoir. */
        PopStratum *el=&re->strata[CLASS_ELITE];
        re->tech += el->wealth*TECH_RATE*el->satisfaction;

        /* Bourgeois réinvestissent une part du profit dans les manufactures
         * (croissance de capacité plafonnée par leur richesse). */
        float reinvest=re->strata[CLASS_BOURGEOIS].wealth*0.02f;
        for (int i=0;i<re->n_bld && reinvest>0.f;i++) {
            float add=fminf(reinvest, 0.5f);
            re->bld[i].level += add*0.1f;
            reinvest-=add;
        }

        for (int r=0;r<RES_COUNT;r++){ re->supply[r]=supply[r]; re->demand[r]=demand[r]; }

        /* Diaspora : bonus tech par innovation culturelle accumulée.
         * S'absorbe progressivement (acculturation, demi-vie ~50 ticks). */
        if (re->diaspora_pop > 0.f) {
            re->tech += re->diaspora_pop * DIASPORA_TECH_RATE * re->diaspora_innovation;
            re->diaspora_pop       *= DIASPORA_DECAY;
            re->diaspora_innovation*= DIASPORA_DECAY;
            if (re->diaspora_pop < 0.5f) {
                re->diaspora_pop=0.f; re->diaspora_innovation=0.f;
            }
        }

        /* Coercition : décroît exponentiellement (demi-vie ≈ 10 ticks).
         * La relocalisation forcée et certains événements la relèvent. */
        re->coercion *= COERCION_DECAY;
        if (re->coercion < 0.005f) re->coercion=0.f;

        /* Décroissance du stock excédentaire (denrées périssables / report
         * limité) : 15% s'évapore, évite l'accumulation infinie. */
        for (int r=0;r<RES_COUNT;r++) re->stock[r]*=0.85f;
    }
}

/* ====================================================================== */
/* COLONISATION                                                            */
/* ====================================================================== */
/* Joueur/Antagoniste : essaiment vers toute région vierge adjacente.
 * Cité-État        : essaime uniquement vers ses propres régions vacantes.
 * Dans les deux cas : au plus une fondation par polité par tick.           */

static void colonize_from(WorldEconomy *e, int src_rid, int dst_rid, int cid) {
    RegionEconomy *src=&e->region[src_rid];
    RegionEconomy *dst=&e->region[dst_rid];
    float spop=0.f; for(int c=0;c<CLASS_COUNT;c++) spop+=src->strata[c].pop;
    float take=fminf(COLONY_COST_POP, spop*0.25f);
    for (int c=0;c<CLASS_COUNT;c++)
        src->strata[c].pop -= take*(src->strata[c].pop/fmaxf(spop,EPS));
    econ_seed_population(dst, COLONY_SEED_POP);
    dst->colonized=true;
    dst->owner=(int16_t)cid;
}

int econ_colonize_tick(WorldEconomy *e, const World *w) {
    int founded=0;

    for (int cid=0; cid<w->n_countries; cid++) {
        const Country *ct=&w->country[cid];
        PolityRole role=ct->role;

        if (role==POLITY_PLAYER || role==POLITY_ANTAGONIST) {
            /* Cherche la meilleure paire (source, cible) du pays. */
            int best_src=-1, best_dst=-1; float best_score=-1.f;
            for (int rs=0; rs<e->n_regions; rs++) {
                RegionEconomy *src=&e->region[rs];
                if (!src->colonized || src->owner!=cid) continue;
                float spop=0.f; for(int c=0;c<CLASS_COUNT;c++) spop+=src->strata[c].pop;
                if (spop<COLONY_MIN_POP || src->food_sat<COLONY_FOOD_GATE) continue;
                for (int rd=0; rd<e->n_regions; rd++) {
                    if (!e->adj[rs][rd]) continue;
                    RegionEconomy *dst=&e->region[rd];
                    if (!dst->active || dst->colonized) continue;
                    float score = dst->cap_pop*0.001f + (spop-COLONY_MIN_POP)*0.0005f
                                + src->food_sat;
                    if (score>best_score){ best_score=score; best_src=rs; best_dst=rd; }
                }
            }
            if (best_src>=0 && best_dst>=0) {
                colonize_from(e, best_src, best_dst, cid);
                founded++;
            }

        } else if (role==POLITY_CITY_STATE) {
            /* Ne peut coloniser que ses propres régions vacantes adjacentes à
             * une de ses régions déjà peuplées. */
            int best_src=-1, best_dst=-1; float best_score=-1.f;
            for (int ri=0; ri<ct->n_regions; ri++) {
                int rs=ct->region_ids[ri];
                if (rs<0||rs>=e->n_regions) continue;
                RegionEconomy *src=&e->region[rs];
                if (!src->colonized || src->owner!=cid) continue;
                float spop=0.f; for(int c=0;c<CLASS_COUNT;c++) spop+=src->strata[c].pop;
                if (spop<COLONY_MIN_POP || src->food_sat<COLONY_FOOD_GATE) continue;
                /* Cibles : uniquement les régions sœurs du même pays */
                for (int rj=0; rj<ct->n_regions; rj++) {
                    int rd=ct->region_ids[rj];
                    if (rd<0||rd>=e->n_regions||!e->adj[rs][rd]) continue;
                    RegionEconomy *dst=&e->region[rd];
                    if (!dst->active || dst->colonized) continue;
                    float score = dst->cap_pop*0.001f + spop*0.0005f;
                    if (score>best_score){ best_score=score; best_src=rs; best_dst=rd; }
                }
            }
            if (best_src>=0 && best_dst>=0) {
                colonize_from(e, best_src, best_dst, cid);
                founded++;
            }
        }
    }
    return founded;
}

/* ====================================================================== */
/* MIGRATION INTERNE                                                       */
/* ====================================================================== */
/* Principe : les bourgeois et élites migrent vers les régions adjacentes
 * plus prospères. La migration crée de la DIASPORA dans la destination,
 * ce qui augmente son innovation (tech). La pression sur les techs orphelines
 * (orphan_tech_weight) est aussi mise à jour pour que scps_tech puisse la lire.
 *
 * Les laborers ne migrent pas spontanément ; ils sont l'objet de relocalisation
 * forcée (econ_relocate_pop).                                                  */

int econ_migrate_tick(WorldEconomy *e, const World *w) {
    (void)w;  /* adj est dans e ; w réservé pour extensions futures */
    int flows=0;

    for (int rs=0; rs<e->n_regions; rs++) {
        RegionEconomy *src=&e->region[rs];
        if (!src->colonized || src->gdp<=0.f) continue;

        for (int rd=0; rd<e->n_regions; rd++) {
            if (!e->adj[rs][rd]) continue;
            RegionEconomy *dst=&e->region[rd];
            if (!dst->colonized) continue;

            /* Différentiel de prospérité : dst doit être significativement
             * plus riche pour attirer des migrants. */
            float pros_src = src->prosperity + 0.01f;  /* éviter div/0 */
            float pros_dst = dst->prosperity;
            if (pros_dst <= pros_src * MIGRATE_THRESHOLD) continue;

            /* Taux de migration proportionnel au différentiel, plafonné. */
            float ratio = fminf(MIGRATE_RATE,
                                (pros_dst/pros_src - 1.f) * 0.04f);
            float migrated=0.f;

            for (int cl=CLASS_BOURGEOIS; cl<CLASS_COUNT; cl++) {
                float mv = src->strata[cl].pop * ratio;
                if (mv < 0.5f) continue;
                /* Transfert pop + richesse proportionnelle */
                float wfrac = mv / fmaxf(src->strata[cl].pop, 1.f);
                float wmv   = src->strata[cl].wealth * wfrac;
                src->strata[cl].pop    -= mv;
                src->strata[cl].wealth -= wmv;
                dst->strata[cl].pop    += mv;
                dst->strata[cl].wealth += wmv;
                migrated += mv;
            }

            if (migrated < 0.5f) continue;

            /* Effet diaspora : novelty = différentiel normalisé.
             * Plus les cultures d'origine et de destination sont distantes
             * (proxy ici : différentiel de prospérité), plus l'apport est riche.
             * Un système culture-intégré pourra remplacer ce proxy plus tard. */
            float novelty = pros_dst/pros_src - 1.f;
            dst->diaspora_pop         += migrated;
            dst->diaspora_innovation  += migrated * novelty;
            dst->orphan_tech_weight   += migrated * novelty * 0.05f;
            flows++;
        }
    }
    return flows;
}

/* ====================================================================== */
/* RELOCALISATION FORCÉE                                                   */
/* ====================================================================== */
/* Déplace `amount` habitants (surtout laborers) de src → dst.
 * Génère un pic de coercition dans la source, proportionnel à la fraction
 * déplacée. La destination subit un léger choc d'accueil (stress social).  */

void econ_relocate_pop(WorldEconomy *e, int src_rid, int dst_rid, float amount) {
    if (src_rid<0||src_rid>=e->n_regions||dst_rid<0||dst_rid>=e->n_regions) return;
    RegionEconomy *src=&e->region[src_rid];
    RegionEconomy *dst=&e->region[dst_rid];
    if (!src->colonized||!dst->colonized||amount<1.f) return;

    float src_pop=0.f;
    for (int c=0;c<CLASS_COUNT;c++) src_pop+=src->strata[c].pop;
    float take=fminf(amount, src_pop*0.5f);  /* limite : 50% de la source */
    if (take<1.f) return;

    /* Prélève d'abord les laborers (80%), puis les bourgeois si insuffisant. */
    float lab_take = fminf(take*0.8f, src->strata[CLASS_LABORER].pop);
    float bou_take = fminf(take - lab_take, src->strata[CLASS_BOURGEOIS].pop);
    src->strata[CLASS_LABORER].pop   -= lab_take;
    src->strata[CLASS_BOURGEOIS].pop -= bou_take;
    dst->strata[CLASS_LABORER].pop   += lab_take;
    dst->strata[CLASS_BOURGEOIS].pop += bou_take;

    /* Coercition source : proportionnelle à la fraction déplacée.
     * La destination a un léger choc de réception (10% du spike source). */
    float frac = take / fmaxf(src_pop, 1.f);
    src->coercion = fminf(1.f, src->coercion + RELOC_COERCION_BASE * frac * 4.f);
    dst->coercion = fminf(1.f, dst->coercion + RELOC_COERCION_BASE * 0.10f);
}

/* ====================================================================== */
/* AFFICHAGE CONSOLE                                                       */
/* ====================================================================== */

void econ_print_region(const WorldEconomy *e, const World *w, int region_id) {
    if (region_id<0||region_id>=e->n_regions) return;
    const RegionEconomy *re=&e->region[region_id];
    const Region *rg=&w->region[region_id];
    printf("\n┌─ Région #%d  « %s »  (tick %d) %s\n",
           region_id, rg->name[0]?rg->name:"—", e->tick,
           re->active?"":"[inactive]");
    if (!re->active) return;

    printf("│ Population & classes\n");
    float tot=0.f; for(int c=0;c<CLASS_COUNT;c++) tot+=re->strata[c].pop;
    for (int c=0;c<CLASS_COUNT;c++) {
        const PopStratum *st=&re->strata[c];
        printf("│   %-10s  pop %7.0f (%4.1f%%)  richesse %8.0f  satisf %3.0f%%\n",
               social_class_name(c), st->pop, tot>0?100.f*st->pop/tot:0.f,
               st->wealth, st->satisfaction*100.f);
    }
    printf("│ Satisfaction générale : %.0f%%   PIB %.0f   Trésor %.0f   Tech %.1f\n",
           re->satisfaction*100.f, re->gdp, re->treasury, re->tech);
    if (re->diaspora_pop > 0.5f || re->coercion > 0.005f)
        printf("│ Diaspora : %.0f hab  innov %.2f  tech-orpheline pression %.2f"
               "  coercition %.0f%%\n",
               re->diaspora_pop, re->diaspora_innovation,
               re->orphan_tech_weight, re->coercion*100.f);

    printf("│ Manufactures\n");
    if (re->n_bld==0) printf("│   (aucune)\n");
    for (int i=0;i<re->n_bld;i++) {
        const Building *b=&re->bld[i];
        printf("│   %-28s niv %4.1f  emploi %6.0f\n",
               building_name(b->type), b->level, b->workers);
    }

    printf("│ Marché (biens actifs)\n");
    printf("│   %-18s %8s %8s %8s %8s\n","bien","prix","offre","demande","stock");
    for (int r=1;r<RES_COUNT;r++) {
        if (re->supply[r]<0.01f && re->demand[r]<0.01f && re->stock[r]<0.01f) continue;
        printf("│   %-18s %8.2f %8.1f %8.1f %8.1f\n",
               resource_name((Resource)r), re->price[r],
               re->supply[r], re->demand[r], re->stock[r]);
    }
    printf("└────────────────────────────────────────────\n");
}

void econ_print_summary(const WorldEconomy *e, const World *w) {
    double pop=0, tech=0, gdp=0, treas=0; float satw=0;
    int active=0, best=-1; float best_gdp=-1.f;
    for (int rid=0; rid<e->n_regions; rid++) {
        const RegionEconomy *re=&e->region[rid];
        if (!re->active) continue;
        active++;
        float rp=0.f; for(int c=0;c<CLASS_COUNT;c++) rp+=re->strata[c].pop;
        pop+=rp; tech+=re->tech; gdp+=re->gdp; treas+=re->treasury;
        satw+=re->satisfaction*rp;
        if (re->gdp>best_gdp){ best_gdp=re->gdp; best=rid; }
    }
    printf("\n══ SOMMAIRE MONDE  (tick %d) ══\n", e->tick);
    printf("  régions actives : %d / %d\n", active, e->n_regions);
    printf("  population tot. : %.0f\n", pop);
    printf("  satisf. moyenne : %.0f%%\n", pop>0?100.f*satw/pop:0.f);
    printf("  PIB cumulé      : %.0f\n", gdp);
    printf("  trésor (taxes)  : %.0f\n", treas);
    printf("  tech cumulée    : %.1f\n", tech);
    if (best>=0)
        printf("  région la plus riche : #%d « %s » (PIB %.0f)\n",
               best, w->region[best].name[0]?w->region[best].name:"—", best_gdp);
}
