/*
 * econ_culture_demo.c — la DEMANDE par variante culturelle (catalogue §4, §7.2-3)
 *
 *   make econ_culture_demo && ./econ_culture_demo [graine]
 *
 * Idée maîtresse : les biens d'un peuple ne sont pas d'autres biens — ce sont
 * les VARIANTES d'un même palier. Une minorité d'une autre SPHÈRE réclame SES
 * variantes ; lui servir celles de la dominante la satisfait MAL ; l'ASSIMILATION
 * fait DÉRIVER sa demande → la pénalité s'efface. On vérifie :
 *   1. Province homogène → aucune pénalité.
 *   2. Une minorité d'une AUTRE sphère, non assimilée → pénalité off-culture.
 *   3. L'assimilation (integration↑) EFFACE la pénalité.
 *   4. Sur le moteur : un empire BIGARRÉ a une satisfaction SOCIALE plus basse
 *      qu'homogène (mais la SURVIE, universelle, est épargnée).
 */
#include "scps_world.h"
#include "scps_econ.h"
#include "scps_species.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_pass=0, g_fail=0;
static void ok(const char *what, bool cond){
    printf("   %s %s\n", cond?"✓":"✗", what);
    if (cond) g_pass++; else g_fail++;
}

/* Pose un groupe (race, sphère, effectif, intégration). */
static void set_group(PopGroup *g, SpeciesArchetype race, Sphere sph, long count, float integ){
    memset(g,0,sizeof *g);
    g->race=race; g->origin_sphere=sph; g->count=count; g->integration=integ;
}

int main(int argc, char **argv){
    uint32_t seed=(argc>1)?(uint32_t)strtoul(argv[1],NULL,10):42u;
    printf("══════════════════════════════════════════════════════════════\n");
    printf(" DEMANDE PAR VARIANTE CULTURELLE (§4, §7.2-3) — graine %u\n", seed);
    printf("══════════════════════════════════════════════════════════════\n");

    /* ═══ 1-3. La fonction de pénalité (la mécanique pure) ═══════════════ */
    printf("\n── 1-3. Off-culture & assimilation ──\n");
    ProvincePop pp; memset(&pp,0,sizeof pp);

    set_group(&pp.groups[0], RACE_HUMAIN, SPHERE_HOMMES, 1000, 1.f);
    pp.n_groups=1;
    ok("province HOMOGÈNE (un peuple) → aucune pénalité off-culture",
       econ_off_culture_fraction(&pp) < 0.001f);

    /* On conquiert : une minorité ORQUE (sphère Étrangers), non assimilée. */
    set_group(&pp.groups[1], RACE_ORQUE, SPHERE_ETRANGERS, 500, 0.f);
    pp.n_groups=2;
    float off_raw = econ_off_culture_fraction(&pp);
    printf("   dominante Hommes + 1/3 minorité Étrangers (integration 0) : pénalité=%.2f\n", off_raw);
    ok("une minorité d'une AUTRE sphère, non assimilée → demande mal servie (>0.1)", off_raw>0.10f);

    /* Les générations passent : la minorité s'assimile (sa demande dérive). */
    pp.groups[1].integration = 1.f;
    float off_assim = econ_off_culture_fraction(&pp);
    printf("   même minorité, ASSIMILÉE (integration 1) : pénalité=%.2f\n", off_assim);
    ok("l'ASSIMILATION efface la pénalité (la demande dérive vers la dominante)",
       off_assim < off_raw - 0.10f);

    /* Une minorité de la MÊME sphère (Anciens : elfe sous nain) gêne moins qu'une
     * sphère lointaine (Étrangers). */
    set_group(&pp.groups[0], RACE_NAIN, SPHERE_ANCIENS, 1000, 1.f);
    set_group(&pp.groups[1], RACE_ELFE, SPHERE_ANCIENS, 500, 0.f);
    float off_near = econ_off_culture_fraction(&pp);
    set_group(&pp.groups[1], RACE_ORQUE, SPHERE_ETRANGERS, 500, 0.f);
    float off_far  = econ_off_culture_fraction(&pp);
    printf("   minorité MÊME sphère=%.2f vs sphère LOINTAINE=%.2f\n", off_near, off_far);
    ok("une sphère LOINTAINE gêne plus qu'une sphère proche (variantes plus distantes)",
       off_far > off_near + 0.05f);

    /* ═══ 4. Sur le moteur : bigarré → satisfaction SOCIALE plus basse ═══ */
    printf("\n── 4. Sur le moteur : empire bigarré, satisfaction sociale ↓ (survie épargnée) ──\n");
    World *w=malloc(sizeof(World)); WorldEconomy *e=malloc(sizeof(WorldEconomy));
    if(!w||!e){ fprintf(stderr,"OOM\n"); return 1; }
    WorldParams p=worldparams_default(seed);
    world_generate(w,&p); econ_init(e,w); gen_population(w,e);
    /* on prend une région peuplée */
    int rr=-1; for (int r=0;r<e->n_regions;r++) if (e->region[r].colonized && e->region[r].strata[0].pop>50.f){ rr=r; break; }
    if (rr<0) rr=0;
    long pop = (long)(e->region[rr].strata[0].pop+e->region[rr].strata[1].pop+e->region[rr].strata[2].pop);

    /* cas homogène */
    memset(&e->region[rr].pop,0,sizeof e->region[rr].pop);
    set_group(&e->region[rr].pop.groups[0], RACE_HUMAIN, SPHERE_HOMMES, pop, 1.f);
    e->region[rr].pop.n_groups=1;
    for (int t=0;t<3;t++) econ_tick(e, 1.f);
    float soc_homo = e->region[rr].society_sat, food_homo = e->region[rr].food_sat;

    /* cas bigarré : on injecte une forte minorité étrangère non assimilée */
    econ_init(e,w); gen_population(w,e);
    set_group(&e->region[rr].pop.groups[0], RACE_HUMAIN, SPHERE_HOMMES, pop/2, 1.f);
    set_group(&e->region[rr].pop.groups[1], RACE_ORQUE,  SPHERE_ETRANGERS, pop/2, 0.f);
    e->region[rr].pop.n_groups=2;
    for (int t=0;t<3;t++) econ_tick(e, 1.f);
    float soc_mix = e->region[rr].society_sat, food_mix = e->region[rr].food_sat;

    printf("   satisfaction sociale : homogène %.2f → bigarré %.2f | survie : %.2f → %.2f\n",
           soc_homo, soc_mix, food_homo, food_mix);
    ok("l'empire BIGARRÉ a une satisfaction SOCIALE plus basse (off-culture)", soc_mix < soc_homo - 0.02f);
    ok("la SURVIE (vivres, universelle) n'est PAS punie par la diversité", food_mix > food_homo - 0.05f);

    printf("\n══════════════════════════════════════════════════════════════\n");
    printf(" BILAN : %d réussis, %d échoués\n", g_pass, g_fail);
    printf("══════════════════════════════════════════════════════════════\n");
    free(w); free(e);
    return g_fail?1:0;
}
