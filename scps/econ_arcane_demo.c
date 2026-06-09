/*
 * econ_arcane_demo.c — le FIL ARCANE : cristal → essence → Brèche (§ fil arcane, §7.4)
 *
 *   make econ_arcane_demo && ./econ_arcane_demo [graine]
 *
 * La magie est tissée dans la matière. Le cristal arcanique sourd des nœuds
 * telluriques ; l'atelier de mage le BRÛLE pour raffiner l'essence (mana) ; et
 * cette combustion nourrit le flux faustien → déréalisation → la Brèche se
 * rapproche. On vérifie :
 *   1. Le cristal est RARE et géologique (peu de régions en portent).
 *   2. L'atelier de mage CONSOMME le cristal et PRODUIT de l'essence.
 *   3. Brûler le cristal CHARGE l'arcane (arcane_charge > 0).
 *   4. Cette charge MONTE le flux faustien → déréalisation (Brèche plus proche)
 *      qu'un État qui ne brûle rien.
 */
#include "scps_world.h"
#include "scps_econ.h"
#include "scps_trade.h"
#include "scps_tech.h"
#include "scps_legitimacy.h"
#include "scps_prosperity.h"
#include "scps_diplo.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_pass=0, g_fail=0;
static void ok(const char *what, bool cond){
    printf("   %s %s\n", cond?"✓":"✗", what);
    if (cond) g_pass++; else g_fail++;
}

int main(int argc, char **argv){
    uint32_t seed=(argc>1)?(uint32_t)strtoul(argv[1],NULL,10):42u;
    World *w=malloc(sizeof(World)); WorldEconomy *e=malloc(sizeof(WorldEconomy));
    WorldProsperity *wp=malloc(sizeof(WorldProsperity)); WorldLegitimacy *wl=malloc(sizeof(WorldLegitimacy));
    TechState *ts=calloc(SCPS_MAX_COUNTRY,sizeof(TechState));
    if(!w||!e||!wp||!wl||!ts){ fprintf(stderr,"OOM\n"); return 1; }

    printf("══════════════════════════════════════════════════════════════\n");
    printf(" FIL ARCANE — cristal → essence → Brèche (§7.4) — graine %u\n", seed);
    printf("══════════════════════════════════════════════════════════════\n");

    WorldParams p=worldparams_default(seed);
    world_generate(w,&p);
    econ_init(e,w); gen_population(w,e);
    worldgen_seed_peoples(w,e,RACE_HUMAIN);
    prosperity_init(wp,w); legitimacy_init(wl,w,e);
    for (int c=0;c<w->n_countries;c++) tech_state_init(&ts[c],false);

    /* ═══ 1. Rareté géologique du cristal ═══════════════════════════════ */
    printf("\n── 1. Le cristal arcanique est RARE (nœuds telluriques) ──\n");
    int n_nodes=0, n_settled=0;
    for (int r=0;r<e->n_regions;r++){
        if (e->region[r].colonized) n_settled++;
        if (e->region[r].raw_cap[RES_ARCANE_CRYSTAL]>0.f) n_nodes++;
    }
    printf("   nœuds de cristal : %d régions sur %d peuplées\n", n_nodes, n_settled);
    ok("le cristal est rare (pas dans toute région)", n_nodes < n_settled);

    /* On force un nœud + atelier de mage sur une région d'un PAYS, pour isoler
     * le fil arcane même si la graine n'a pas placé de nœud chez lui. */
    int rid=-1, cid=-1;
    for (int r=0;r<e->n_regions;r++) if (e->region[r].owner>=0 && e->region[r].colonized){ rid=r; cid=e->region[r].owner; break; }
    if (rid<0){ rid=0; cid=e->region[0].owner; }
    e->region[rid].raw_cap[RES_ARCANE_CRYSTAL] = 4.0f;   /* un nœud généreux */
    /* atelier de mage (si pas déjà là) — on lui donne un niveau pour qu'il tourne */
    {
        int bi=-1;
        for (int i=0;i<e->region[rid].n_bld;i++) if (e->region[rid].bld[i].type==BLD_MAGE_WORKSHOP) bi=i;
        if (bi<0 && e->region[rid].n_bld<ECON_MAX_BLD){ bi=e->region[rid].n_bld++; e->region[rid].bld[bi].type=BLD_MAGE_WORKSHOP; }
        if (bi>=0) e->region[rid].bld[bi].level=3.f;
    }

    /* ═══ 2-3. L'atelier brûle le cristal → essence + charge ═════════════ */
    printf("\n── 2-3. L'atelier de mage brûle le cristal → essence (charge arcane) ──\n");
    float ess0=e->region[rid].stock[RES_ESSENCE];
    for (int t=0;t<3;t++) econ_tick(e,1.f);
    float ess1=e->region[rid].stock[RES_ESSENCE];
    float charge=e->region[rid].arcane_charge;
    printf("   région %d : essence %.1f→%.1f | charge arcane ce tick = %.2f\n", rid, ess0, ess1, charge);
    ok("l'atelier de mage PRODUIT de l'essence (le cristal raffiné)", ess1 > ess0 + 0.5f);
    ok("brûler le cristal CHARGE l'arcane (arcane_charge > 0)", charge > 0.01f);

    /* ═══ 4. La forge céleste : fer céleste + essence → armes enchantées → puissance ═ */
    printf("\n── 4. Forge céleste : fer céleste + essence → armes enchantées → puissance militaire ──\n");
    float mil0 = diplo_mil_power(w, e, cid);
    e->region[rid].raw_cap[RES_CELESTIAL_IRON] = 3.0f;     /* un filon de fer céleste */
    {
        int bi=-1;
        for (int i=0;i<e->region[rid].n_bld;i++) if (e->region[rid].bld[i].type==BLD_CELESTIAL_FORGE) bi=i;
        if (bi<0 && e->region[rid].n_bld<ECON_MAX_BLD){ bi=e->region[rid].n_bld++; e->region[rid].bld[bi].type=BLD_CELESTIAL_FORGE; }
        if (bi>=0) e->region[rid].bld[bi].level=3.f;
    }
    for (int t=0;t<5;t++) econ_tick(e,1.f);   /* la chaîne tourne : cristal→essence→armes */
    float arms = e->region[rid].stock[RES_ENCHANTED_ARMS];
    float mil1 = diplo_mil_power(w, e, cid);
    printf("   armes enchantées en stock = %.1f | puissance militaire %.2f → %.2f\n", arms, mil0, mil1);
    ok("la forge céleste PRODUIT des armes enchantées (fer céleste + essence)", arms > 0.5f);
    ok("les armes enchantées montent la PUISSANCE militaire (l'arcane nourrit la guerre)", mil1 > mil0 + 0.1f);

    /* ═══ 5. La combustion rapproche la Brèche (flux faustien → déréal) ══ */
    printf("\n── 5. La combustion arcane MONTE la déréalisation (la Brèche approche) ──\n");
    /* On place le pays au MARGE faustienne (capacité K basse) : la déréalisation
     * = max(0, (P/10)·C + flux_faustien − K) ne répond que si K ne l'écrase pas.
     * C'est précisément le sens : sans capacité pour CONTENIR la magie, en brûler
     * fait déréaliser. Un État à K haut encaisse ; un fragile bascule. */
    ts[cid].K = 1.0f;
    /* État brûlant : on tick l'économie (charge>0) puis la prospérité. */
    econ_tick(e,1.f);
    prosperity_tick(wp,w,e,NULL,ts,wl);
    float dereal_burn = wp->country[cid].dereal;
    /* État apaisé : on coupe le nœud ET on VIDE le cristal DÉJÀ extrait — sinon
     * l'atelier brûle les réserves accumulées aux sections 2-4 (arcane_charge se
     * RECHARGE depuis le STOCK, pas seulement l'extraction du tick), la charge ne
     * retombe pas et la déréalisation reste identique. Sans nœud NI stock, la
     * combustion cesse vraiment → charge→0 → le flux faustien reflue. */
    e->region[rid].raw_cap[RES_ARCANE_CRYSTAL]=0.f;
    e->region[rid].stock[RES_ARCANE_CRYSTAL]=0.f;
    for (int t=0;t<4;t++) econ_tick(e,1.f);   /* la charge retombe à 0 */
    prosperity_tick(wp,w,e,NULL,ts,wl);
    float dereal_quiet = wp->country[cid].dereal;
    printf("   déréalisation du pays : EN BRÛLANT %.2f vs APAISÉ %.2f\n", dereal_burn, dereal_quiet);
    ok("brûler le cristal RAPPROCHE la Brèche (déréalisation plus haute)", dereal_burn > dereal_quiet + 0.01f);

    printf("\n══════════════════════════════════════════════════════════════\n");
    printf(" BILAN : %d réussis, %d échoués\n", g_pass, g_fail);
    printf("══════════════════════════════════════════════════════════════\n");
    free(w);free(e);free(wp);free(wl);free(ts);
    return g_fail?1:0;
}
