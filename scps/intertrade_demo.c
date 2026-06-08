/*
 * intertrade_demo.c — banc d'essai du COMMERCE INTER-PAYS (scps_intertrade)
 *
 *   make intertrade_demo && ./intertrade_demo [graine]
 *
 * Prouve : une grande route marchande porte des GOODS entre pays (le bien remonte
 * la pente de prix, l'exportateur encaisse l'or) ; la GUERRE l'EMBARGO (guerre
 * commerciale) ; l'intra-pays n'est pas du commerce inter-pays.
 */
#include "scps_world.h"
#include "scps_econ.h"
#include "scps_culture.h"
#include "scps_species.h"
#include "scps_routes.h"
#include "scps_diplo.h"
#include "scps_intertrade.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_pass=0,g_fail=0;
static void ok(const char*w,bool c){ printf("   %s %s\n",c?"✓":"✗",w); if(c)g_pass++; else g_fail++; }

int main(int argc,char**argv){
    uint32_t seed=(argc>1)?(uint32_t)strtoul(argv[1],NULL,10):42u;
    World*w=malloc(sizeof(World)); WorldEconomy*econ=malloc(sizeof(WorldEconomy));
    if(!w||!econ){fprintf(stderr,"OOM\n");return 1;}

    printf("══════════════════════════════════════════════════════════════\n");
    printf(" COMMERCE INTER-PAYS — les grandes routes marchandes portent des goods (graine %u)\n",seed);
    printf("══════════════════════════════════════════════════════════════\n");

    WorldParams p=worldparams_default(seed);
    world_generate(w,&p);
    econ_init(econ,w); gen_population(w,econ); worldgen_seed_peoples(w,econ,RACE_HUMAIN);

    /* deux régions peuplées que l'on attribue à DEUX pays distincts. */
    int ra=-1, rb=-1;
    for(int r=0;r<econ->n_regions;r++) if(econ->region[r].culture.settled){ if(ra<0)ra=r; else {rb=r;break;} }
    if(ra<0||rb<0){ printf(" (monde trop vide)\n"); return 0; }
    int g=8;   /* un bien quelconque : on pose nous-mêmes stock & prix */

    DiploState dp; diplo_init(&dp);
    RouteNetwork rn; routes_init(&rn);
    rn.route[0].ra=ra; rn.route[0].rb=rb; rn.route[0].maritime=true; rn.route[0].open=true;
    rn.route[0].capacity=120.f; rn.n=1;

    /* A bon marché + surplus ; B cher + pénurie → le bien doit remonter A→B. */
    #define SETUP() do{ econ->region[ra].owner=0; econ->region[rb].owner=1; \
        econ->region[ra].stock[g]=500.f; econ->region[ra].price[g]=1.0f; econ->region[ra].treasury=0.f; \
        econ->region[rb].stock[g]=0.f;   econ->region[rb].price[g]=6.0f; }while(0)

    /* ---- 1. Une route inter-pays PORTE des goods ---- */
    printf("\n── 1. La grande route porte des goods (arbitrage + or à l'exportateur) ──\n");
    SETUP();
    intertrade_tick(econ,&rn,&dp);
    printf("   après le tick : B reçoit %.0f unités · A encaisse %.0f or · valeur échangée %.0f\n",
           econ->region[rb].stock[g], econ->region[ra].treasury, intertrade_imports_value(econ));
    ok("le bien REMONTE la pente de prix (l'importateur B reçoit)", econ->region[rb].stock[g] > 0.f);
    ok("l'EXPORTATEUR A encaisse de l'or", econ->region[ra].treasury > 0.f);
    ok("un échange a eu lieu (valeur > 0)", intertrade_imports_value(econ) > 0.f);
    ok("la route marchande est ACTIVE pour les deux pays",
       intertrade_active_routes(econ,&rn,&dp,0)==1 && intertrade_active_routes(econ,&rn,&dp,1)==1);

    /* ---- 2. EMBARGO : la guerre coupe le commerce ---- */
    printf("\n── 2. Embargo : la guerre suspend le commerce inter-pays ──\n");
    SETUP();
    diplo_declare_war(&dp,0,1);
    intertrade_tick(econ,&rn,&dp);
    ok("EMBARGO : aucun bien ne passe entre pays EN GUERRE", econ->region[rb].stock[g]==0.f);
    ok("aucune valeur échangée sous l'embargo", intertrade_imports_value(econ)==0.f);
    ok("aucune route marchande active en guerre", intertrade_active_routes(econ,&rn,&dp,0)==0);

    /* ---- 3. La paix rouvre la route ---- */
    printf("\n── 3. La paix rouvre la route ──\n");
    SETUP();
    diplo_make_peace(&dp,0,1);
    intertrade_tick(econ,&rn,&dp);
    ok("après la paix, le commerce REPREND (B reçoit de nouveau)", econ->region[rb].stock[g] > 0.f);

    /* ---- 4. Intra-pays ≠ commerce inter-pays ---- */
    printf("\n── 4. Une route intra-pays n'est pas du commerce inter-pays ──\n");
    SETUP();
    econ->region[rb].owner=0;   /* même couronne aux deux bouts */
    intertrade_tick(econ,&rn,&dp);
    ok("même couronne aux deux bouts → pas de route INTER-pays (rien ne passe ici)",
       econ->region[rb].stock[g]==0.f);

    printf("\n══════════════════════════════════════════════════════════════\n");
    printf(" BILAN : %d réussis, %d échoués\n",g_pass,g_fail);
    printf("══════════════════════════════════════════════════════════════\n");
    free(w); free(econ);
    return g_fail?1:0;
}
