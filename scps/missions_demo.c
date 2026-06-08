/*
 * missions_demo.c — LES MISSIONS DÉCENNALES (factions §8)
 *
 *   make missions_demo && ./missions_demo [graine]
 *
 * Prouve : une mission CONTEXTUELLE est émise à la décennie ; l'accomplir VERSE
 * or + matières ; le kind tourne (bâtir / chaîne / tech) ; texte diégétique.
 */
#include "scps_missions.h"
#include "scps_culture.h"
#include "scps_species.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_pass=0, g_fail=0;
static void ok(const char *what, int cond){
    printf("   %s %s\n", cond?"✓":"✗", what);
    if (cond) g_pass++; else g_fail++;
}

int main(int argc, char **argv){
    uint32_t seed=(argc>1)?(uint32_t)strtoul(argv[1],NULL,10):42u;
    World *w=malloc(sizeof(World)); WorldEconomy *econ=malloc(sizeof(WorldEconomy));
    TechState *ts=calloc(SCPS_MAX_COUNTRY,sizeof(TechState));
    MissionsState *ms=malloc(sizeof(MissionsState));
    if(!w||!econ||!ts||!ms){ fprintf(stderr,"OOM\n"); return 1; }

    printf("══════════════════════════════════════════════════════════════\n");
    printf(" MISSIONS DÉCENNALES (factions §8) — contexte, récompense, rythme\n");
    printf("══════════════════════════════════════════════════════════════\n");

    WorldParams p=worldparams_default(seed);
    world_generate(w,&p);
    econ_init(econ,w); gen_population(w,econ); worldgen_seed_peoples(w,econ,RACE_HUMAIN);
    for (int c=0;c<SCPS_MAX_COUNTRY;c++) tech_state_init(&ts[c],false);
    missions_init(ms);

    /* Un pays réel avec une capitale. */
    int cid=-1;
    for (int c=0;c<w->n_countries;c++){
        if (w->country[c].role==POLITY_UNCLAIMED) continue;
        int cp=w->country[c].capital_prov; if (cp<0) continue;
        int cr=w->province[cp].region;
        for (int r=0;r<econ->n_regions;r++) if (econ->region[r].owner==c && cr>=0){ cid=c; break; }
        if (cid>=0) break;
    }
    if (cid<0){ fprintf(stderr,"monde trop vide — autre graine\n"); return 1; }
    int cr=w->province[w->country[cid].capital_prov].region;

    /* ═══ 1. ÉMISSION DÉCENNALE ════════════════════════════════════════ */
    printf("\n── 1. À la décennie, une mission contextuelle est émise ──\n");
    missions_tick(ms, w, econ, ts, 0);             /* an 0 : décennie → émission */
    const Mission *m = mission_of(ms, cid);
    ok("une mission est émise au pays (an 0, décennie)", m!=NULL && m->active);
    ok("la mission porte un texte diégétique (membrane)", m && m->text[0]!=0);
    if (m) printf("     « %s »  (récompense : %.0f or + %.0f matières)\n",
                  m->text, m->reward_gold, m->reward_qty);
    ok("an 0 (décennie 0) → mission de BÂTI (le kind tourne)", m && m->kind==MIS_BUILD);

    /* ═══ 2. ACCOMPLISSEMENT → RÉCOMPENSE OR + MATIÈRES ════════════════ */
    printf("\n── 2. Accomplir la mission verse or + matières ──\n");
    float tre0=econ->region[cr].treasury;
    Resource rmat = m?m->reward_mat:RES_NONE;
    float mat0 = (rmat>RES_NONE)?econ->region[cr].stock[rmat]:0.f;
    /* on SATISFAIT la mission de bâti : toutes les coordonnées bâties au-delà du seuil. */
    ProvBuild *b=&econ->region[cr].build;
    b->K_inst=b->PE_infra=b->faith=b->savoir=b->H_coerc=b->food_cap=50.f;
    missions_tick(ms, w, econ, ts, 1);             /* an 1 : vérifie → accomplie */
    const Mission *m2=mission_of(ms,cid);
    ok("la mission est marquée ACCOMPLIE une fois la cible atteinte", m2 && m2->done);
    ok("la récompense en OR a abondé le trésor de la capitale", econ->region[cr].treasury > tre0 + 1.f);
    ok("la récompense en MATIÈRES a abondé le marché",
       rmat<=RES_NONE || econ->region[cr].stock[rmat] > mat0 + 1.f);

    /* ═══ 3. LE KIND TOURNE PAR DÉCENNIE (chaîne, puis tech) ═══════════ */
    printf("\n── 3. Le kind tourne : chaîne (décennie 1), tech (décennie 2) ──\n");
    missions_tick(ms, w, econ, ts, 10);            /* décennie 1 → CHAÎNE */
    const Mission *mc=mission_of(ms,cid);
    ok("décennie 1 (an 10) → mission de CHAÎNE de production", mc && mc->kind==MIS_CHAIN);
    if (mc) printf("     chaîne : « %s »\n", mc->text);
    missions_tick(ms, w, econ, ts, 20);            /* décennie 2 → TECH */
    const Mission *mt=mission_of(ms,cid);
    ok("décennie 2 (an 20) → mission de TECHNOLOGIE", mt && mt->kind==MIS_TECH);
    if (mt) printf("     tech   : « %s »\n", mt->text);

    printf("\n══════════════════════════════════════════════════════════════\n");
    printf(" BILAN : %d réussis, %d échoués\n", g_pass, g_fail);
    printf("══════════════════════════════════════════════════════════════\n");
    free(w);free(econ);free(ts);free(ms);
    return g_fail?1:0;
}
