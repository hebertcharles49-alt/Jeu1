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
    /* Profil culturel des populations régionales (après création des régions). */
    gen_population(w, e);

    printf("=== Construction du réseau commercial ===\n");
    trade_network_build(t, w, e);
    printf("    %d liens créés\n", t->n_links);

    /* Régions à détailler */
    int ra = (argc>3)? atoi(argv[3]) : 0;
    int rb = -1;

    /* Décompte des rôles politiques */
    int n_player=0,n_antag=0,n_city=0,n_virgin=0;
    for (int c=0;c<w->n_countries;c++) switch(w->country[c].role){
        case POLITY_PLAYER: n_player++; break;
        case POLITY_ANTAGONIST: n_antag++; break;
        case POLITY_CITY_STATE: n_city++; break;
        default: n_virgin++; break;
    }
    printf("    pays : %d joueur, %d antagonistes, %d cités-états, %d vierges\n",
           n_player,n_antag,n_city,n_virgin);

    printf("=== Simulation : %d ticks (econ + migration + commerce + colonisation) ===\n", ticks);
    int total_founded=0, total_mig_flows=0;
    for (int tick=0; tick<ticks; tick++) {
        econ_tick(e, 1.f);
        total_founded    += econ_colonize_tick(e, w);
        total_mig_flows  += econ_migrate_tick(e, w);
        world_tick(w, e, 1.0f);   /* dérive lente de l'horloge linguistique */
        trade_tick(e, t);
        /* Recalibrer le réseau tous les 5 ticks (colonisation → nouveaux nœuds). */
        if (tick>0 && tick%5==0) trade_network_build(t, w, e);
    }
    /* Décompte des régions colonisées */
    int n_col=0, n_active=0;
    float total_diaspora=0.f, total_coercion=0.f;
    for (int rid=0;rid<e->n_regions;rid++){
        if (e->region[rid].active) n_active++;
        if (e->region[rid].colonized) {
            n_col++;
            total_diaspora += e->region[rid].diaspora_pop;
            total_coercion += e->region[rid].coercion;
        }
    }
    printf("    colonisation : %d fondations, %d/%d régions peuplées\n",
           total_founded, n_col, n_active);
    printf("    migration    : %d flux actifs / simulation, diaspora tot. %.0f\n",
           total_mig_flows, total_diaspora);

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
