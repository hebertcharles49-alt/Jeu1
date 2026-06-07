/*
 * agency_demo.c — banc d'essai de la couche d'agency (§1-§2)
 *
 *   make agency_demo && ./agency_demo [graine]
 *
 * Prouve le principe « une action est un levier qui déplace une coordonnée » :
 *   - bâtir des institutions (Tribunal/Chancellerie/Académie) monte K → l'ordre
 *     se LIT « plus stable » (consenti) ;
 *   - bâtir des citadelles (Garnison→Citadelle) monte H ET ronge L → l'ordre
 *     bascule « coercitif-fragile » (Stabilité Tenue · Assise Contrainte).
 *
 * Le temps passe en JOURS (l'arc de 250 ans) ; aucune construction n'est
 * instantanée. La lecture se fait par la MEMBRANE (mots), jamais en chiffres
 * (les flottants affichés ici sont pour le développeur, pas le joueur).
 */
#include "scps_world.h"
#include "scps_econ.h"
#include "scps_trade.h"
#include "scps_tech.h"
#include "scps_legitimacy.h"
#include "scps_prosperity.h"
#include "scps_readout.h"
#include "scps_agency.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static int g_pass=0, g_fail=0;
static void ok(const char *what, bool cond){
    printf("   %s %s\n", cond?"✓":"✗", what);
    if (cond) g_pass++; else g_fail++;
}

/* Contexte de simulation. */
typedef struct {
    World *w; WorldEconomy *econ; TradeNetwork *net; TechState *ts;
    WorldProsperity *wp; WorldLegitimacy *wl; AgencyState *ag;
    int player, cap_reg;
} Sim;

/* Un jour : éco → actions → légitimité → prospérité (ordre anti-circularité). */
static void run_days(Sim *s, int days){
    for (int d=0; d<days; d++){
        econ_tick(s->econ);
        agency_advance(s->ag, s->econ, 1);
        legitimacy_tick(s->wl, s->w, s->econ, s->ts);
        prosperity_tick(s->wp, s->w, s->econ, s->net, s->ts, s->wl);
    }
}

static void snapshot(Sim *s, const char *phase, float *out_SI, float *out_frag, float *out_L){
    CountryProsperity *cp=&s->wp->country[s->player];
    CountryReadout r=country_readout(s->wp, s->ts, s->w, s->player);
    printf("  An %-3d %-22s Stabilité=%-12s Assise=%-11s Légitimité=%-10s",
           agency_year(s->ag), phase,
           label_stab(r.stabilite), label_assise(r.assise), label_legit(r.legitimite));
    printf("   [dev SI=%.2f frag=%.2f L=%.2f]\n", cp->SI, cp->fragilite, cp->L);
    if (out_SI)   *out_SI=cp->SI;
    if (out_frag) *out_frag=cp->fragilite;
    if (out_L)    *out_L=cp->L;
}

int main(int argc, char **argv){
    uint32_t seed=(argc>1)?(uint32_t)strtoul(argv[1],NULL,10):42u;

    Sim s={0};
    s.w   =(World*)          malloc(sizeof(World));
    s.econ=(WorldEconomy*)   malloc(sizeof(WorldEconomy));
    s.net =(TradeNetwork*)   malloc(sizeof(TradeNetwork));
    s.ts  =(TechState*)      calloc(SCPS_MAX_COUNTRY,sizeof(TechState));
    s.wp  =(WorldProsperity*)malloc(sizeof(WorldProsperity));
    s.wl  =(WorldLegitimacy*)malloc(sizeof(WorldLegitimacy));
    s.ag  =(AgencyState*)    malloc(sizeof(AgencyState));
    if(!s.w||!s.econ||!s.net||!s.ts||!s.wp||!s.wl||!s.ag){ fprintf(stderr,"OOM\n"); return 1; }

    printf("══════════════════════════════════════════════════════════════\n");
    printf(" AGENCY — bâtir des coordonnées dans le temps (graine %u)\n", seed);
    printf("══════════════════════════════════════════════════════════════\n");

    WorldParams p=worldparams_default(seed);
    world_generate(s.w,&p);
    econ_init(s.econ,s.w);
    gen_population(s.w,s.econ);
    worldgen_seed_peoples(s.w,s.econ,RACE_HUMAIN);
    trade_network_build(s.net,s.w,s.econ);
    for (int c=0;c<s.w->n_countries;c++) tech_state_init(&s.ts[c],false);
    prosperity_init(s.wp,s.w);
    legitimacy_init(s.wl,s.w,s.econ);
    agency_init(s.ag);

    /* Joueur + sa région-capitale (où l'on bâtit). */
    s.player=0;
    for (int c=0;c<s.w->n_countries;c++) if (s.w->country[c].role==POLITY_PLAYER){ s.player=c; break; }
    int cap_prov=s.w->country[s.player].capital_prov;
    s.cap_reg=(cap_prov>=0)?s.w->province[cap_prov].region:0;
    printf("\n  Joueur = pays %d, capitale région %d (« %s »)\n",
           s.player, s.cap_reg, s.w->region[s.cap_reg].name);

    printf("\n── L'arc d'une partie : on bâtit, le temps passe, l'ordre se lit ──\n");
    float SI0,SI1,SI2, F0,F1,F2, L0,L1,L2;

    /* Phase 0 — fondation (rien de bâti). */
    run_days(&s, 2*SCPS_DAYS_PER_YEAR);
    snapshot(&s, "fondation", &SI0,&F0,&L0);

    /* Phase 1 — INSTITUTIONS (→ K) : Tribunal, Chancellerie, Académie. */
    agency_order_build(s.ag, s.cap_reg, EDI_TRIBUNAL);
    agency_order_build(s.ag, s.cap_reg, EDI_CHANCELLERIE);
    agency_order_build(s.ag, s.cap_reg, EDI_ACADEMIE);
    run_days(&s, 6*SCPS_DAYS_PER_YEAR);   /* l'Académie met ~5 ans */
    snapshot(&s, "après institutions (K↑)", &SI1,&F1,&L1);

    /* Phase 2 — CITADELLES (→ H, ronge L) : tenir par la force. */
    agency_order_build(s.ag, s.cap_reg, EDI_GARNISON);
    agency_order_build(s.ag, s.cap_reg, EDI_FORTERESSE);
    agency_order_build(s.ag, s.cap_reg, EDI_CITADELLE);
    run_days(&s, 8*SCPS_DAYS_PER_YEAR);   /* la Citadelle met ~6 ans */
    snapshot(&s, "après citadelles (H↑, L↓)", &SI2,&F2,&L2);

    /* ---- Contrôles ---------------------------------------------------- */
    printf("\n── Vérification : l'action est un levier ──\n");
    ok("le temps passe en années (≥ 16 ans écoulés)", agency_year(s.ag) >= 16);
    ok("bâtir des institutions monte la stabilité (SI augmente)", SI1 > SI0 + 0.2f);
    ok("bâtir des citadelles ronge la légitimité (L baisse)",     L2  < L1 - 0.1f);
    ok("les citadelles aggravent la fragilité (par la force)",    F2  > F1);
    /* La densité bâtie dans la capitale est bien accumulée. */
    const ProvBuild *b=&s.econ->region[s.cap_reg].build;
    ok("K institutionnel bâti dans la capitale (Tribunal+Chancellerie+Académie)",
       b->K_inst >= 3.5f);
    ok("coercition bâtie dans la capitale (Garnison+Forteresse+Citadelle)",
       b->H_coerc >= 5.5f);
    printf("     capitale : K_inst=%.1f  H_coerc=%.1f  P_open=%.1f\n",
           b->K_inst, b->H_coerc, b->P_open);

    printf("\n══════════════════════════════════════════════════════════════\n");
    printf(" BILAN : %d réussis, %d échoués\n", g_pass, g_fail);
    printf("══════════════════════════════════════════════════════════════\n");
    free(s.w);free(s.econ);free(s.net);free(s.ts);free(s.wp);free(s.wl);free(s.ag);
    return g_fail?1:0;
}
