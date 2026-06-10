/*
 * routes_demo.c — banc d'essai des routes commerciales (§7)
 *
 *   make routes_demo && ./routes_demo [graine]
 *
 * Prouve la CLOCHE f(D̄) faite action : une route vers un partenaire à distance
 * INTERMÉDIAIRE (D̄≈5) rapporte PLUS qu'un quasi-identique (rien à échanger) ou
 * un très lointain (porte fermée). Et une région à plusieurs routes devient un
 * PÔLE (carrefour), lu par la membrane.
 */
#include "scps_world.h"
#include "scps_econ.h"
#include "scps_trade.h"
#include "scps_tech.h"
#include "scps_legitimacy.h"
#include "scps_prosperity.h"
#include "scps_readout.h"
#include "scps_routes.h"
#include <stdio.h>
#include <stdlib.h>

static int g_pass=0,g_fail=0;
static void ok(const char*w,bool c){ printf("   %s %s\n",c?"✓":"✗",w); if(c)g_pass++; else g_fail++; }

static float cdist(const PopCulture*a,const PopCulture*b){
    float dv=a->valeurs-b->valeurs;if(dv<0)dv=-dv; float ds=a->subsistance-b->subsistance;if(ds<0)ds=-ds;
    float dp=a->parente-b->parente;if(dp<0)dp=-dp; float dr=a->religion-b->religion;if(dr<0)dr=-dr;
    float m=dv;if(ds>m)m=ds;if(dp>m)m=dp;if(dr>m)m=dr;return m;
}

int main(int argc,char**argv){
    uint32_t seed=(argc>1)?(uint32_t)strtoul(argv[1],NULL,10):42u;
    World*w=malloc(sizeof(World)); WorldEconomy*econ=malloc(sizeof(WorldEconomy));
    TradeNetwork*net=malloc(sizeof(TradeNetwork)); TechState*ts=calloc(SCPS_MAX_COUNTRY,sizeof(TechState));
    WorldProsperity*wp=malloc(sizeof(WorldProsperity)); WorldLegitimacy*wl=malloc(sizeof(WorldLegitimacy));
    RouteNetwork*rn=malloc(sizeof(RouteNetwork));
    if(!w||!econ||!net||!ts||!wp||!wl||!rn){fprintf(stderr,"OOM\n");return 1;}

    printf("══════════════════════════════════════════════════════════════\n");
    printf(" ROUTES — la cloche f(D̄) faite action (graine %u)\n",seed);
    printf("══════════════════════════════════════════════════════════════\n");

    WorldParams p=worldparams_default(seed);
    world_generate(w,&p);
    econ_init(econ,w); gen_population(w,econ); worldgen_seed_peoples(w,econ,RACE_HUMAIN);
    trade_network_build(net,w,econ);
    for(int c=0;c<w->n_countries;c++) tech_state_init(&ts[c],false);
    prosperity_init(wp,w); legitimacy_init(wl,w,econ); routes_init(rn);
    for(int t=0;t<5;t++){ econ_tick(econ, 1.f); }

    int player=0; for(int c=0;c<w->n_countries;c++) if(w->country[c].role==POLITY_PLAYER){player=c;break;}
    int cap=w->country[player].capital_prov; int home=(cap>=0)?w->province[cap].region:0;
    const PopCulture*hc=&econ->region[home].culture;

    /* Trois partenaires SYNTHÉTIQUES à distances contrôlées (1 / 5 / 8) — le
     * monde naturel ne s'étale pas jusqu'au gouffre ; on isole la cloche. */
    int spare[3]={-1,-1,-1}, ns=0;
    for(int r=0;r<econ->n_regions && ns<3;r++)
        if(r!=home && econ->region[r].culture.settled) spare[ns++]=r;
    float dist[3]={1.f,5.f,8.f};
    for(int k=0;k<3 && spare[k]>=0;k++){
        PopCulture*pc=&econ->region[spare[k]].culture; *pc=*hc;   /* aligne tout sur le foyer… */
        pc->valeurs = (hc->valeurs<=5.f)? hc->valeurs+dist[k] : hc->valeurs-dist[k];  /* …sauf 1 axe */
        pc->valeurs = (pc->valeurs<0.f)?0.f:(pc->valeurs>10.f?10.f:pc->valeurs);
    }
    int near=spare[0], mid=spare[1], far=spare[2];
    printf("\n── Trois partenaires depuis « %s » (distances contrôlées) ──\n", w->region[home].name);
    printf("   quasi-identique : région %d  D̄=%.1f\n", near, near>=0?cdist(&econ->region[near].culture,hc):0);
    printf("   intermédiaire   : région %d  D̄=%.1f\n", mid,  mid >=0?cdist(&econ->region[mid ].culture,hc):0);
    printf("   gouffre         : région %d  D̄=%.1f\n", far,  far >=0?cdist(&econ->region[far ].culture,hc):0);

    routes_order(rn,NULL,econ,home,near,false);
    routes_order(rn,NULL,econ,home,mid, false);
    routes_order(rn,NULL,econ,home,far, false);
    /* ouvre les routes (terre 90 j) puis calcule les rendements. */
    routes_advance(rn,w,econ,150);

    float yn=routes_pe_for_region(rn,near), ym=routes_pe_for_region(rn,mid), yf=routes_pe_for_region(rn,far);
    printf("\n── Rendement par route (la cloche) ──\n");
    printf("   proche  PE=%.2f\n   moyen   PE=%.2f\n   lointain PE=%.2f\n", yn, ym, yf);

    printf("\n── Vérification ──\n");
    ok("les 3 routes s'ouvrent après leurs jours (terre 90j)",
       routes_count_for_region(rn,home)==3);
    ok("le partenaire INTERMÉDIAIRE rapporte le plus (pic de la cloche)",
       ym > yn && ym > yf);
    ok("le quasi-identique rapporte peu (rien à échanger)", yn < ym);

    /* Le foyer, sur 3 routes, devient un carrefour (membrane). */
    prosperity_tick(wp,w,econ,net,ts,wl);
    ProvinceReadout pr=province_readout(w,econ,wp,wl,cap);
    printf("\n   Foyer sur %d routes → Carrefour : %s (route_pe=%.1f)\n",
           routes_count_for_region(rn,home), label_carrefour(pr.carrefour), econ->region[home].route_pe);
    ok("la région à plusieurs routes est un carrefour (≥ Florissante)",
       pr.carrefour != CF_NONE);

    printf("\n══════════════════════════════════════════════════════════════\n");
    printf(" BILAN : %d réussis, %d échoués\n",g_pass,g_fail);
    printf("══════════════════════════════════════════════════════════════\n");
    free(w);free(econ);free(net);free(ts);free(wp);free(wl);free(rn);
    return g_fail?1:0;
}
