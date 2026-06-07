/*
 * prosperity_demo.c — banc d'essai console : moteur de prospérité PE/SI
 *
 *   make prosperity_demo && ./prosperity_demo [graine] [n_ticks]
 *
 * Boucle : econ_tick + colonize + migrate + trade_tick + prosperity_tick
 * Affiche le résumé monde + détails pays non-vierges + §9 vérification.
 */
#include "scps_world.h"
#include "scps_econ.h"
#include "scps_trade.h"
#include "scps_tech.h"
#include "scps_prosperity.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>

/* ---- Fonctions locales pour la section §9 Vérification ----------------- */
static float bell_f_local(float d) {
    if (d < 0.f) d = 0.f;
    if (d > 10.f) d = 10.f;
    return d * (10.f - d) / 25.f;
}
static float sigmoid_local(float x) {
    return 1.f / (1.f + expf(-x));
}

int main(int argc, char **argv) {
    uint32_t seed  = (argc > 1) ? (uint32_t)strtoul(argv[1], NULL, 10)
                                : (uint32_t)time(NULL);
    int ticks      = (argc > 2) ? atoi(argv[2]) : 20;
    if (ticks < 1) ticks = 1;

    /* ---- Allocation sur le tas ----------------------------------------- */
    World            *w   = (World*)           malloc(sizeof(World));
    WorldEconomy     *econ= (WorldEconomy*)    malloc(sizeof(WorldEconomy));
    TradeNetwork     *net = (TradeNetwork*)    malloc(sizeof(TradeNetwork));
    TechState        *ts  = (TechState*)       calloc(SCPS_MAX_COUNTRY, sizeof(TechState));
    WorldProsperity  *wp  = (WorldProsperity*) malloc(sizeof(WorldProsperity));
    WorldLegitimacy  *wl  = (WorldLegitimacy*) malloc(sizeof(WorldLegitimacy));

    if (!w || !econ || !net || !ts || !wp || !wl) {
        fprintf(stderr, "OOM\n");
        return 1;
    }

    /* ---- Génération du monde -------------------------------------------- */
    printf("=== Génération du monde (graine %u) ===\n", seed);
    WorldParams p = worldparams_default(seed);
    world_generate(w, &p);
    printf("    %d pays, %d régions, %d provinces\n",
           w->n_countries, w->n_regions, w->n_provinces);

    /* ---- Init économie -------------------------------------------------- */
    printf("=== Initialisation économie ===\n");
    econ_init(econ, w);
    /* Profil culturel des populations régionales (après création des régions). */
    gen_population(w, econ);
    /* Races en gradient autour du joueur (couche biologique). */
    worldgen_seed_peoples(w, econ, RACE_HUMAIN);

    /* ---- Init réseau commercial ----------------------------------------- */
    printf("=== Construction du réseau commercial ===\n");
    trade_network_build(net, w, econ);
    printf("    %d liens créés\n", net->n_links);

    /* ---- Init tech pour tous les pays ------------------------------------ */
    printf("=== Init tech (%d pays) ===\n", w->n_countries);
    for (int c = 0; c < w->n_countries; c++) {
        tech_state_init(&ts[c], false);
    }

    /* ---- Init prospérité + légitimité ------------------------------------ */
    prosperity_init(wp, w);
    legitimacy_init(wl, w, econ);

    /* ---- Boucle de simulation ------------------------------------------- */
    printf("=== Simulation : %d ticks ===\n", ticks);
    for (int tick = 0; tick < ticks; tick++) {
        econ_tick(econ, 1.f);
        econ_colonize_tick(econ, w);
        econ_migrate_tick(econ, w);
        world_tick(w, econ, 1.0f);   /* dérive lente de l'horloge linguistique */
        legitimacy_tick(wl, w, econ, ts);   /* L émerge (lit l'éco du tick) AVANT la prospérité */
        if (tick > 0 && tick % 5 == 0)
            trade_network_build(net, w, econ);
        trade_tick(econ, net);
        prosperity_tick(wp, w, econ, net, ts, wl);   /* assemble ScpsState avec le L frais */
    }

    /* ---- Résumé monde ---------------------------------------------------- */
    prosperity_print_summary(wp, w);

    /* ---- Détails des pays non-vierges ------------------------------------- */
    printf("\n=== Détail des pays actifs ===\n");
    for (int c = 0; c < w->n_countries; c++) {
        if (w->country[c].role != POLITY_UNCLAIMED)
            prosperity_print_country(wp, w, c);
    }

    /* ======================================================================
     * §9 VÉRIFICATION — exemple numérique du document de conception
     * C=6, P=6, K=5, D̄_int=3.0, D∞_int=5.0
     * ====================================================================== */
    printf("\n╔══════════════════════════════════════════════════════════════╗\n");
    printf("║              §9 VÉRIFICATION (exemple doc)                   ║\n");
    printf("╠══════════════════════════════════════════════════════════════╣\n");

    float C=6.f, P=6.f, K=5.f;
    float D_bar_int=3.0f, D_inf_int=5.0f;

    float bell_int = bell_f_local(D_bar_int);
    float metro_arg_int = 0.8f*(P - D_inf_int) + 0.35f*(K - 5.f);
    float metro_int = sigmoid_local(metro_arg_int);
    float PE_int = 10.f * (C/10.f) * bell_int * metro_int;

    printf("║  C=%.1f  P=%.1f  K=%.1f\n", C, P, K);
    printf("║  D̄_int=%.1f → bell_f=%.3f (attendu 0.84)\n", D_bar_int, bell_int);
    printf("║  D∞_int=%.1f → σ(0.8*(%.1f-%.1f)+0.35*(%.1f-5))=σ(%.3f)=%.3f (attendu 0.690)\n",
           D_inf_int, P, D_inf_int, K, metro_arg_int, metro_int);
    printf("║  PE_interne = 10*(%.1f/10)*%.3f*%.3f = %.3f (attendu 3.48)\n",
           C, bell_int, metro_int, PE_int);

    /* Contact A : D̄=2.5, D∞=4 */
    float dA_bar=2.5f, dA_inf=4.f;
    float bellA = bell_f_local(dA_bar);
    float argA  = 0.8f*(P - dA_inf) + 0.35f*(K - 5.f);
    float metroA= sigmoid_local(argA);
    float PEA   = 10.f*(C/10.f)*bellA*metroA;
    printf("║  Contact A : D̄=%.1f → f=%.3f (att 0.75), σ(%.3f)=%.3f (att 0.832), PE=%.3f (att 3.74)\n",
           dA_bar, bellA, argA, metroA, PEA);

    /* Contact B (tribal désertique) : D̄=6.5, D∞=8.5 */
    float dB_bar=6.5f, dB_inf=8.5f;
    float bellB = bell_f_local(dB_bar);
    float argB  = 0.8f*(P - dB_inf) + 0.35f*(K - 5.f);
    float metroB= sigmoid_local(argB);
    float PEB   = 10.f*(C/10.f)*bellB*metroB;
    printf("║  Contact B : D̄=%.1f → f=%.3f (att 0.91), σ(%.3f)=%.3f (att 0.119), PE=%.3f (att 0.65)\n",
           dB_bar, bellB, argB, metroB, PEB);

    /* Contact C : D̄=1.8, D∞=3 */
    float dC_bar=1.8f, dC_inf=3.f;
    float bellC = bell_f_local(dC_bar);
    float argC  = 0.8f*(P - dC_inf) + 0.35f*(K - 5.f);
    float metroC= sigmoid_local(argC);
    float PEC   = 10.f*(C/10.f)*bellC*metroC;
    printf("║  Contact C : D̄=%.1f → f=%.3f (att 0.59), σ(%.3f)=%.3f (att 0.917), PE=%.3f (att 3.25)\n",
           dC_bar, bellC, argC, metroC, PEC);

    float P_pot = PE_int + PEA + PEB + PEC;
    printf("║  P_pot = %.3f (attendu 11.12)\n", P_pot);

    /* Rendement : SI=7, fragilité=2.5, λ=0.45 */
    float SI=7.f, fragil=2.5f, lambda=0.45f;
    float rendement = (SI/10.f)*(1.f - lambda*fragil/10.f);
    float P_real = P_pot * rendement;
    printf("║  SI=%.1f, fragilité=%.1f → rendement=%.3f, P_réalisé=%.3f (attendus 0.621, 6.91)\n",
           SI, fragil, rendement, P_real);

    printf("╚══════════════════════════════════════════════════════════════╝\n");

    free(w); free(econ); free(net); free(ts); free(wp); free(wl);
    return 0;
}
