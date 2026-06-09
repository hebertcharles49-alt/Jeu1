/*
 * econ_scan.c — DIAGNOSTIC headless de l'économie (satisfaction par strate, prix).
 *   ./econ_scan <graine_base> <n_mondes> <ans>
 * Génère n mondes (mêmes paramètres que le chronicle : 2+k empires, 5+k cités),
 * fait tourner l'éco N ans, puis agrège sur les régions colonisées : satisfaction
 * ÉLITE / BOURGEOISE / LABORER (pop-pondérée) et le prix moyen des biens clés.
 * Sert à régler le partage du revenu (TAX_RATE) SANS lancer le chronicle complet.
 */
#include "scps_world.h"
#include "scps_econ.h"
#include <stdio.h>
#include <stdlib.h>

static float avg_price(const WorldEconomy *e, Resource res){
    double s=0.0; int n=0;
    for (int r=0;r<e->n_regions;r++) if (e->region[r].colonized){ s+=e->region[r].price[res]; n++; }
    return n? (float)(s/n):0.f;
}

int main(int argc,char**argv){
    uint32_t base =(argc>1)?(uint32_t)strtoul(argv[1],NULL,10):7u;
    int nsims=(argc>2)?atoi(argv[2]):6;
    int years=(argc>3)?atoi(argv[3]):60;
    World *w=malloc(sizeof(World)); WorldEconomy *e=malloc(sizeof(WorldEconomy));
    if(!w||!e){ fprintf(stderr,"OOM\n"); return 1; }

    double sat_w[CLASS_COUNT]={0}, pop_w[CLASS_COUNT]={0};
    double wealth_w[CLASS_COUNT]={0};
    double pg=0,pc=0,pw=0,pv=0,pt=0; int np=0;
    long n_reg=0;
    long bld_count[BLD_TYPE_COUNT]={0}; double bld_level[BLD_TYPE_COUNT]={0};
    double good_sup[RES_COUNT]={0}, good_dem[RES_COUNT]={0};
    double emp_total=0, labtight=0;   /* emploi Σ + régions où la main-d'œuvre est tendue */

    for (int k=0;k<nsims;k++){
        uint32_t seed=base+(uint32_t)k*101u;
        WorldParams p=worldparams_default(seed);
        p.n_empires=2+k; p.n_city_states=5+k;
        world_generate(w,&p); econ_init(e,w); gen_population(w,e);
        for (int y=0;y<years;y++){
            for (int m=0;m<12;m++) econ_tick(e,1.f/12.f);
            econ_colonize_tick(e,w); world_tick(w,e,1.f);
        }
        for (int r=0;r<e->n_regions;r++){
            RegionEconomy *re=&e->region[r];
            if (!re->active || !re->colonized) continue;
            n_reg++;
            { double emp=0; for (int i=0;i<re->n_bld;i++){ bld_count[re->bld[i].type]++; bld_level[re->bld[i].type]+=re->bld[i].level; emp+=re->bld[i].workers; }
              emp_total+=emp; if (re->strata[CLASS_LABORER].pop>0 && emp > 0.85*re->strata[CLASS_LABORER].pop) labtight++; }
            for (int g=0;g<RES_COUNT;g++){ good_sup[g]+=re->supply[g]; good_dem[g]+=re->demand[g]; }
            for (int c=0;c<CLASS_COUNT;c++){
                double pop=re->strata[c].pop;
                sat_w[c]+=re->strata[c].satisfaction*pop; pop_w[c]+=pop;
                wealth_w[c]+=re->strata[c].wealth;
            }
            pg+=avg_price(e,RES_GRAIN); pc+=avg_price(e,RES_CLOTH);
            pw+=avg_price(e,RES_PRECIOUS_WARE); pv+=avg_price(e,RES_WINE);
            pt+=avg_price(e,RES_TOOLS); np++;
        }
    }
    printf("══════════════════════════════════════════════════════════════════════\n");
    printf(" ECON-SCAN base %u · %d mondes × %d ans · %ld rég colonisées\n", base,nsims,years,n_reg);
    printf("══════════════════════════════════════════════════════════════════════\n");
    static const char *CN[CLASS_COUNT]={"Laborer","Bourgeois","Élite"};
    for (int c=0;c<CLASS_COUNT;c++)
        printf("  satisfaction %-10s %5.1f %%   · richesse Σ %10.0f\n",
               CN[c], pop_w[c]>0?100.0*sat_w[c]/pop_w[c]:-1.0, wealth_w[c]);
    if (np>0)
        printf("  marché : grain %.2f · étoffe %.2f · orfèvrerie %.2f · vin %.2f · outils %.2f\n",
               pg/np, pc/np, pw/np, pv/np, pt/np);
    printf("══════════════════════════════════════════════════════════════════════\n");
    printf(" MAIN-D'ŒUVRE : emploi Σ %.0f vs pop laborer Σ %.0f (%.1f%% employée) · régions main-d'œuvre TENDUE (>85%%) : %.0f/%ld\n",
           emp_total, pop_w[CLASS_LABORER], pop_w[CLASS_LABORER]>0?100.0*emp_total/pop_w[CLASS_LABORER]:0.0, labtight, n_reg);
    printf(" AUDIT BÂTIMENTS — branchés ? inputs consommés ? (sur %ld rég colonisées)\n", n_reg);
    printf("  %-18s %6s %9s | %-14s %9s | %-12s %9s\n","bâtiment","count","levelΣ","sortie","supplyΣ","intrant","demandΣ");
    for (int b=0;b<BLD_TYPE_COUNT;b++){
        Resource in1=RES_NONE,in2=RES_NONE,out=RES_NONE; building_recipe((BuildingType)b,&in1,&in2,&out);
        double sup=(out>=0&&out<RES_COUNT)?good_sup[out]:-1, dem=(in1>=0&&in1<RES_COUNT)?good_dem[in1]:-1;
        printf("  %-18s %6ld %9.1f | %-14s %9.1f | %-12s %9.1f\n",
               building_name((BuildingType)b), bld_count[b], bld_level[b],
               (out!=RES_NONE)?resource_name(out):"—", sup,
               (in1!=RES_NONE)?resource_name(in1):"—", dem);
    }
    printf("══════════════════════════════════════════════════════════════════════\n");
    free(w); free(e);
    return 0;
}
