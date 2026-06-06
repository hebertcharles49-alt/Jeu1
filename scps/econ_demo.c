/*
 * econ_demo.c — banc d'essai console : économie + commerce inter-régional
 *
 *   make econ_demo && ./econ_demo [graine] [n_ticks] [region_a [region_b]]
 *
 * Boucle : econ_tick → trade_tick → econ_tick → ...
 * Affiche un sommaire monde, les top routes commerciales, et le détail de
 * deux régions (économie + balance commerciale).
 */
#include "scps_world.h"
#include "scps_econ.h"
#include "scps_trade.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

int main(int argc, char **argv) {
    uint32_t seed = (argc>1)? (uint32_t)strtoul(argv[1],NULL,10)
                            : (uint32_t)time(NULL);
    int ticks = (argc>2)? atoi(argv[2]) : 40;
    if (ticks<1) ticks=1;

    World        *w = (World*)       malloc(sizeof(World));
    WorldEconomy *e = (WorldEconomy*)malloc(sizeof(WorldEconomy));
    TradeNetwork *t = (TradeNetwork*)malloc(sizeof(TradeNetwork));
    if (!w||!e||!t){ fprintf(stderr,"OOM\n"); return 1; }

    WorldParams p = worldparams_default(seed);
    printf("=== Génération du monde (graine %u) ===\n", seed);
    world_generate(w, &p);

    printf("=== Initialisation économie ===\n");
    econ_init(e, w);

    printf("=== Construction du réseau commercial ===\n");
    trade_network_build(t, w, e);
    printf("    %d liens créés\n", t->n_links);

    /* Régions à détailler */
    int ra = (argc>3)? atoi(argv[3]) : 0;
    int rb = -1;

    printf("=== Simulation : %d ticks (econ + commerce) ===\n", ticks);
    for (int tick=0; tick<ticks; tick++) {
        econ_tick(e);
        trade_tick(e, t);
        /* Recalibrer les capacités tous les 10 ticks (pop change). */
        if (tick>0 && tick%10==0) trade_network_build(t, w, e);
    }

    econ_print_summary(e, w);
    trade_print_summary(t, e, w, 12);

    /* Région la plus riche si rb pas fourni */
    if (argc>4) rb=atoi(argv[4]);
    if (rb<0) {
        float best=-1.f; rb=0;
        for (int rid=0;rid<e->n_regions;rid++)
            if (e->region[rid].active && e->region[rid].gdp>best) {
                best=e->region[rid].gdp; rb=rid;
            }
    }

    econ_print_region(e, w, ra);
    trade_print_region(t, e, w, ra);
    if (rb!=ra) {
        econ_print_region(e, w, rb);
        trade_print_region(t, e, w, rb);
    }

    free(w); free(e); free(t);
    return 0;
}
