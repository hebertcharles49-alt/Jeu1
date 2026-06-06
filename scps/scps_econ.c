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

        /* Population proportionnelle à la capacité dans le pays.
         * Sans pays connu : fallback ~12 hab/cellule fertile. */
        int cid=rg->country;
        float total_pop;
        if (cid>=0 && cid<SCPS_MAX_COUNTRY && cty_cap[cid]>0.f)
            total_pop = cty_target[cid] * reg_cap[rid] / cty_cap[cid];
        else
            total_pop = 40.f + reg_cap[rid]*12.f;

        for (int c=0;c<CLASS_COUNT;c++) {
            re->strata[c].pop        = total_pop*CLASS_SHARE[c];
            re->strata[c].wealth     = re->strata[c].pop * (c==CLASS_ELITE?6.f:c==CLASS_BOURGEOIS?2.f:0.5f);
            re->strata[c].satisfaction=0.5f;
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

        /* Niveau initial des manufactures : dimensionné sur les bourgeois
         * (ce sont eux qui investissent). */
        float invest = re->strata[CLASS_BOURGEOIS].pop;
        for (int i=0;i<re->n_bld;i++)
            re->bld[i].level = 0.5f + invest*0.01f;

        /* ---- Prix & stock de départ. */
        for (int r=0;r<RES_COUNT;r++) {
            re->price[r]=BASE_PRICE[r];
            re->stock[r]=0.f;
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
        if (!re->active) continue;

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
         * + pic de famine si food_sat < 0.35 */
        float food_s = re->food_sat;
        float soc_s  = re->society_sat;
        float net_growth = BIRTH_RATE*food_s - DEATH_RATE + SOCIETY_BONUS*soc_s;
        if (food_s < 0.35f)
            net_growth -= (0.35f - food_s) * 0.12f;   /* pic de mortalité famine */
        net_growth = clampf(net_growth, -0.10f, 0.06f);

        float satsum=0.f, popsum=0.f;
        for (int c=0;c<CLASS_COUNT;c++) {
            PopStratum *st=&re->strata[c];
            st->pop *= 1.f + net_growth;
            if (st->pop<1.f) st->pop=1.f;
            satsum+=st->satisfaction*st->pop; popsum+=st->pop;
        }
        re->satisfaction=(popsum>0.f)?satsum/popsum:0.f;

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

        /* Décroissance du stock excédentaire (denrées périssables / report
         * limité) : 15% s'évapore, évite l'accumulation infinie. */
        for (int r=0;r<RES_COUNT;r++) re->stock[r]*=0.85f;
    }
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
