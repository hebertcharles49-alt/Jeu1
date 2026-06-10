/*
 * econ_production_demo.c — l'épine dorsale : fer+charbon → métal → outils → productivité
 *
 *   make econ_production_demo && ./econ_production_demo [graine]
 *
 * Les biens de PRODUCTION sont des intrants, pas des paniers. La chaîne centrale :
 *   Fer + Charbon → (Fonderie) Métal → (Atelier) Outils.
 * Et les OUTILS sont le MULTIPLICATEUR de productivité : leur stock booste
 * l'extraction et la manufacture. On vérifie :
 *   1. La fonderie produit du MÉTAL (fer + charbon).
 *   2. L'atelier d'outillage produit des OUTILS (métal + bois).
 *   3. Les outils MONTENT la production (même région, plus d'outils → plus de PIB).
 *   4. Les outils s'USENT (sans entretien, le stock décroît).
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

/* Fige une région en banc d'essai de production : fer+charbon+bois+grain, plus
 * une fonderie et un atelier d'outillage, sur une pop de travail donnée. */
static void rig(WorldEconomy *e, int r, float tools){
    RegionEconomy *re=&e->region[r];
    re->active=true; re->colonized=true; re->culture.settled=true;
    re->owner=-1;   /* ISOLATION : hors domaine §NF (sinon la construction demande-menée
                     * bâtirait un atelier qui CONSOMME le métal que le test veut accumuler) */
    for (int k=0;k<RES_COUNT;k++){ re->raw_cap[k]=0.f; re->stock[k]=0.f; }
    re->raw_cap[RES_IRON]=4.f; re->raw_cap[RES_COAL]=4.f;
    re->raw_cap[RES_WOOD]=4.f; re->raw_cap[RES_GRAIN]=8.f;
    re->n_bld=0;
    re->bld[re->n_bld].type=BLD_FOUNDRY;   re->bld[re->n_bld].level=3.f; re->n_bld++;
    re->bld[re->n_bld].type=BLD_TOOLWORKS; re->bld[re->n_bld].level=3.f; re->n_bld++;
    re->strata[CLASS_LABORER].pop=600.f;  re->strata[CLASS_LABORER].wealth=400.f;
    re->strata[CLASS_BOURGEOIS].pop=100.f;re->strata[CLASS_BOURGEOIS].wealth=200.f;
    re->strata[CLASS_ELITE].pop=50.f;     re->strata[CLASS_ELITE].wealth=300.f;
    re->stock[RES_TOOLS]=tools;
}

int main(int argc, char **argv){
    uint32_t seed=(argc>1)?(uint32_t)strtoul(argv[1],NULL,10):42u;
    World *w=malloc(sizeof(World)); WorldEconomy *e=malloc(sizeof(WorldEconomy));
    if(!w||!e){ fprintf(stderr,"OOM\n"); return 1; }
    printf("══════════════════════════════════════════════════════════════\n");
    printf(" ÉPINE DORSALE — fer+charbon→métal→outils→productivité — graine %u\n", seed);
    printf("══════════════════════════════════════════════════════════════\n");

    WorldParams p=worldparams_default(seed);
    world_generate(w,&p); econ_init(e,w); gen_population(w,e);
    if (e->n_regions<3){ fprintf(stderr,"monde trop petit\n"); return 1; }

    /* ═══ 1-2. La chaîne réelle ═════════════════════════════════════════ */
    printf("\n── 1-2. Fonderie (fer+charbon→métal) puis Atelier (métal+bois→outils) ──\n");
    /* Région 0 : Fonderie SEULE → le métal s'accumule (rien ne le consomme). */
    rig(e, 0, 0.f);
    e->region[0].n_bld=1;   /* garder la fonderie, retirer l'atelier */
    for (int t=0;t<6;t++) econ_tick(e,1.f);
    float metal=e->region[0].stock[RES_METAL];
    /* Région 1 : Fonderie + Atelier → la chaîne complète sort des OUTILS (donc
     * le métal a bien été produit PUIS consommé pour les outils). */
    rig(e, 1, 0.f);
    for (int t=0;t<6;t++) econ_tick(e,1.f);
    float tools=e->region[1].stock[RES_TOOLS];
    printf("   fonderie seule : métal=%.1f | chaîne complète : outils=%.1f\n", metal, tools);
    ok("la Fonderie produit du MÉTAL (fer + charbon)", metal > 0.5f);
    ok("la chaîne complète (métal→bois) produit des OUTILS", tools > 0.5f);

    /* ═══ 3. Les outils MULTIPLIENT la productivité ═════════════════════ */
    printf("\n── 3. Les outils montent la production (le multiplicateur) ──\n");
    rig(e, 1, 0.f);    /* site sans outils */
    econ_tick(e,1.f);
    float gdp_none=e->region[1].gdp;
    rig(e, 1, 600.f);  /* région IDENTIQUE mais bien outillée */
    econ_tick(e,1.f);
    float gdp_tools=e->region[1].gdp;
    printf("   PIB du même site : sans outils=%.0f vs bien outillé=%.0f\n", gdp_none, gdp_tools);
    ok("un site BIEN OUTILLÉ produit plus qu'un site sans outils (productivité)",
       gdp_tools > gdp_none + 1.f);

    /* ═══ 4. Les outils s'usent ═════════════════════════════════════════ */
    printf("\n── 4. Les outils s'usent (il faut les entretenir) ──\n");
    RegionEconomy *re2=&e->region[2];
    re2->active=true; re2->colonized=true; re2->culture.settled=true;
    for (int k=0;k<RES_COUNT;k++){ re2->raw_cap[k]=0.f; re2->stock[k]=0.f; }
    re2->n_bld=0;   /* AUCUN atelier → pas d'entretien */
    re2->strata[CLASS_LABORER].pop=600.f;
    re2->stock[RES_TOOLS]=1000.f;
    float tw0=re2->stock[RES_TOOLS];
    for (int t=0;t<10;t++) econ_tick(e,1.f);
    float tw1=re2->stock[RES_TOOLS];
    printf("   stock d'outils sans entretien : %.0f → %.0f (usure)\n", tw0, tw1);
    ok("sans atelier pour les entretenir, le stock d'outils DÉCROÎT", tw1 < tw0 - 1.f);

    printf("\n══════════════════════════════════════════════════════════════\n");
    printf(" BILAN : %d réussis, %d échoués\n", g_pass, g_fail);
    printf("══════════════════════════════════════════════════════════════\n");
    free(w); free(e);
    return g_fail?1:0;
}
