/*
 * econ_demo.c — banc d'essai console du moteur économique (sans SDL/UI)
 *
 *   make econ_demo && ./econ_demo [graine] [n_ticks] [region_a region_b]
 *
 * Génère un monde, initialise l'économie depuis sa géographie, fait tourner
 * la simulation n_ticks tours, puis affiche un sommaire monde et le détail
 * de deux régions (par défaut : la plus riche et une autre). Permet de
 * valider la chaîne pop → production → marché → satisfaction sans interface.
 */
#include "scps_world.h"
#include "scps_econ.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

int main(int argc, char **argv) {
    uint32_t seed = (argc>1)? (uint32_t)strtoul(argv[1],NULL,10)
                            : (uint32_t)time(NULL);
    int ticks = (argc>2)? atoi(argv[2]) : 40;
    if (ticks<1) ticks=1;

    World *w = (World*)malloc(sizeof(World));
    WorldEconomy *e = (WorldEconomy*)malloc(sizeof(WorldEconomy));
    if (!w||!e){ fprintf(stderr,"OOM\n"); return 1; }

    WorldParams p = worldparams_default(seed);
    printf("=== Génération du monde (graine %u) ===\n", seed);
    world_generate(w, &p);

    printf("\n=== Initialisation économie ===\n");
    econ_init(e, w);

    /* Choix des deux régions à détailler */
    int ra = (argc>3)? atoi(argv[3]) : 0;
    int rb = (argc>4)? atoi(argv[4]) : -1;  /* -1 → on prendra la + riche */

    printf("=== Simulation : %d ticks ===\n", ticks);
    for (int t=0; t<ticks; t++) econ_tick(e);

    econ_print_summary(e, w);

    /* Si rb non fourni, détaille la région la plus riche en plus de ra. */
    if (rb<0) {
        float best=-1.f; rb=0;
        for (int rid=0; rid<e->n_regions; rid++)
            if (e->region[rid].active && e->region[rid].gdp>best) {
                best=e->region[rid].gdp; rb=rid;
            }
    }
    econ_print_region(e, w, ra);
    if (rb!=ra) econ_print_region(e, w, rb);

    free(w); free(e);
    return 0;
}
