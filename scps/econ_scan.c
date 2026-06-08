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
    free(w); free(e);
    return 0;
}
