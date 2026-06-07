/*
 * econ_tax_demo.c — l'impôt par ÉTHOS × satisfaction (§6-7 du prompt économie)
 *
 *   make econ_tax_demo && ./econ_tax_demo [graine]
 *
 * Prouve la boucle conso → satisfaction → impôt, chiffrée par la CULTURE :
 *   1. La TABLE d'éthos (§7) : Bureaucrate extrait partout ; Dominateur épargne
 *      l'élite ; Mercantile épargne le bourgeois ; Pacifiste lève peu.
 *   2. L'éthos CHIFFRE le rentré : à wealth + satisfaction ÉGALES, un Bureaucrate
 *      lève PLUS sur l'élite qu'un Dominateur (qui voit son élite s'évader).
 *   3. Le SEUIL × satisfaction : un peuple CONTENT paie plus qu'un mécontent.
 *   4. La surtaxe GRONDE : pousser au-delà du seuil ABAISSE la satisfaction.
 */
#include "scps_world.h"
#include "scps_econ.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_pass=0, g_fail=0;
static void ok(const char *what, bool cond){
    printf("   %s %s\n", cond?"✓":"✗", what);
    if (cond) g_pass++; else g_fail++;
}

/* Fige une région en banc d'essai fiscal : pas de production (raw_cap=0,
 * 0 bâtiment), seule l'élite a de la richesse → le trésor = l'impôt SUR l'élite.
 * Stock plein → le panier se remplit (la satisfaction ne dépend que de l'impôt). */
static void rig(WorldEconomy *e, int r, Ethos ethos, float elite_wealth, float sat){
    RegionEconomy *re=&e->region[r];
    re->active=true; re->colonized=true; re->culture.settled=true;
    re->culture.ethos=ethos;
    re->n_bld=0;
    for (int k=0;k<RES_COUNT;k++){ re->raw_cap[k]=0.f; re->stock[k]=1.0e6f; }
    re->strata[CLASS_LABORER].pop=100.f;   re->strata[CLASS_LABORER].wealth=0.f;
    re->strata[CLASS_BOURGEOIS].pop=100.f; re->strata[CLASS_BOURGEOIS].wealth=0.f;
    re->strata[CLASS_ELITE].pop=100.f;     re->strata[CLASS_ELITE].wealth=elite_wealth;
    for (int c=0;c<CLASS_COUNT;c++) re->strata[c].satisfaction=sat;
    re->treasury=0.f;
}

int main(int argc, char **argv){
    uint32_t seed=(argc>1)?(uint32_t)strtoul(argv[1],NULL,10):42u;
    World *w=malloc(sizeof(World)); WorldEconomy *e=malloc(sizeof(WorldEconomy));
    if(!w||!e){ fprintf(stderr,"OOM\n"); return 1; }
    printf("══════════════════════════════════════════════════════════════\n");
    printf(" IMPÔT PAR ÉTHOS × SATISFACTION (§6-7) — graine %u\n", seed);
    printf("══════════════════════════════════════════════════════════════\n");

    WorldParams p=worldparams_default(seed);
    world_generate(w,&p);
    econ_init(e,w); gen_population(w,e);
    if (e->n_regions<5){ fprintf(stderr,"monde trop petit\n"); return 1; }

    /* ═══ 1. LA TABLE D'ÉTHOS (§7) ═══════════════════════════════════════ */
    printf("\n── 1. La culture chiffre la stratégie fiscale (table §7) ──\n");
    printf("   tolérance élite : Bureaucrate %.2f · Dominateur %.2f · Honneur %.2f\n",
           econ_tax_tolerance(ETHOS_BUREAUCRATE,CLASS_ELITE),
           econ_tax_tolerance(ETHOS_DOMINATEUR,CLASS_ELITE),
           econ_tax_tolerance(ETHOS_HONNEUR,CLASS_ELITE));
    ok("Bureaucrate extrait de l'élite mieux que le Dominateur (qui l'épargne)",
       econ_tax_tolerance(ETHOS_BUREAUCRATE,CLASS_ELITE) > econ_tax_tolerance(ETHOS_DOMINATEUR,CLASS_ELITE));
    ok("Mercantile épargne le BOURGEOIS (ne pas étrangler le commerce)",
       econ_tax_tolerance(ETHOS_MERCANTILE,CLASS_BOURGEOIS) < econ_tax_tolerance(ETHOS_BUREAUCRATE,CLASS_BOURGEOIS));
    ok("Dominateur essore la MASSE (laborers) plus que l'élite",
       econ_tax_tolerance(ETHOS_DOMINATEUR,CLASS_LABORER) > econ_tax_tolerance(ETHOS_DOMINATEUR,CLASS_ELITE));
    ok("Pacifiste lève peu sur TOUTES les classes",
       econ_tax_tolerance(ETHOS_PACIFISTE,CLASS_LABORER) < 0.4f &&
       econ_tax_tolerance(ETHOS_PACIFISTE,CLASS_ELITE)   < 0.4f);

    /* ═══ 2. L'ÉTHOS CHIFFRE LE RENTRÉ (à état égal) ═════════════════════ */
    printf("\n── 2. À richesse + satisfaction ÉGALES, l'éthos décide ce qui rentre ──\n");
    int rD=0, rB=1;
    rig(e, rD, ETHOS_DOMINATEUR,  1000.f, 0.70f);   /* l'élite résiste */
    rig(e, rB, ETHOS_BUREAUCRATE, 1000.f, 0.70f);   /* extraction propre */
    econ_tick(e, 1.f);
    float taxD=e->region[rD].treasury, taxB=e->region[rB].treasury;
    printf("   trésor sur élite (wealth 1000, satisf 0.70) : Dominateur %.0f · Bureaucrate %.0f\n", taxD, taxB);
    ok("le Bureaucrate lève PLUS que le Dominateur sur la même élite (moins d'évasion)", taxB > taxD + 1.f);
    ok("le Dominateur ne lève pas zéro non plus (il essore par ailleurs)", taxD > 0.f);

    /* ═══ 3. LE SEUIL × SATISFACTION : contenter d'abord ════════════════ */
    printf("\n── 3. Un peuple CONTENT paie plus qu'un mécontent (seuil × satisfaction) ──\n");
    int rHi=2, rLo=3;
    rig(e, rHi, ETHOS_BUREAUCRATE, 1000.f, 0.90f);  /* content */
    rig(e, rLo, ETHOS_BUREAUCRATE, 1000.f, 0.20f);  /* mécontent */
    econ_tick(e, 1.f);
    float taxHi=e->region[rHi].treasury, taxLo=e->region[rLo].treasury;
    printf("   même éthos, même richesse : satisfait %.0f · mécontent %.0f\n", taxHi, taxLo);
    ok("surtaxer un peuple mécontent rapporte MOINS (évasion) — contenter d'abord", taxHi > taxLo + 1.f);

    /* ═══ 4. LA SURTAXE GRONDE (agitation) ═════════════════════════════ */
    printf("\n── 4. Pousser au-delà du seuil ABAISSE la satisfaction (la grogne) ──\n");
    int rTol=4, rHard=5<e->n_regions?5:0;
    rig(e, rTol,  ETHOS_BUREAUCRATE, 1000.f, 0.60f);  /* tolérant : pas de surtaxe */
    rig(e, rHard, ETHOS_DOMINATEUR,  1000.f, 0.60f);  /* l'élite essorée au-delà du seuil */
    econ_tick(e, 1.f);
    float satTol=e->region[rTol].strata[CLASS_ELITE].satisfaction;
    float satHard=e->region[rHard].strata[CLASS_ELITE].satisfaction;
    printf("   satisfaction élite après impôt : sous éthos tolérant %.2f · surtaxée %.2f\n", satTol, satHard);
    ok("l'élite SURTAXÉE (au-delà du seuil) est moins satisfaite que la tolérée",
       satHard < satTol - 0.01f);

    printf("\n══════════════════════════════════════════════════════════════\n");
    printf(" BILAN : %d réussis, %d échoués\n", g_pass, g_fail);
    printf("══════════════════════════════════════════════════════════════\n");
    free(w); free(e);
    return g_fail?1:0;
}
