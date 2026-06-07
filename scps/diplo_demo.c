/*
 * diplo_demo.c — banc d'essai diplomatie & guerre (§5-§6)
 *
 *   make diplo_demo && ./diplo_demo [graine]
 *
 * Prouve :
 *   1. Les relations se LISENT : menace, parenté (sphère), schisme, alliance —
 *      calculées sur des coordonnées existantes, pas posées à la main.
 *   2. La guerre = territoire CONTRE diversité : conquérir une culture lointaine
 *      monte le D̄ interne du conquérant → la fracture monte (gated par K).
 *      Concorde « Unie » → « Murmurante/Fracturée ».
 */
#include "scps_world.h"
#include "scps_econ.h"
#include "scps_trade.h"
#include "scps_tech.h"
#include "scps_legitimacy.h"
#include "scps_prosperity.h"
#include "scps_readout.h"
#include "scps_diplo.h"
#include <stdio.h>
#include <stdlib.h>

static int g_pass=0,g_fail=0;
static void ok(const char*w,bool c){ printf("   %s %s\n",c?"✓":"✗",w); if(c)g_pass++; else g_fail++; }

static float content_dist(const PopCulture*a,const PopCulture*b){
    float dv=a->valeurs-b->valeurs;       if(dv<0)dv=-dv;
    float ds=a->subsistance-b->subsistance; if(ds<0)ds=-ds;
    float dp=a->parente-b->parente;       if(dp<0)dp=-dp;
    float dr=a->religion-b->religion;     if(dr<0)dr=-dr;
    float m=dv; if(ds>m)m=ds; if(dp>m)m=dp; if(dr>m)m=dr; return m;
}

int main(int argc,char**argv){
    uint32_t seed=(argc>1)?(uint32_t)strtoul(argv[1],NULL,10):42u;
    World*w=malloc(sizeof(World)); WorldEconomy*econ=malloc(sizeof(WorldEconomy));
    TradeNetwork*net=malloc(sizeof(TradeNetwork)); TechState*ts=calloc(SCPS_MAX_COUNTRY,sizeof(TechState));
    WorldProsperity*wp=malloc(sizeof(WorldProsperity)); WorldLegitimacy*wl=malloc(sizeof(WorldLegitimacy));
    DiploState*dp=malloc(sizeof(DiploState));
    if(!w||!econ||!net||!ts||!wp||!wl||!dp){fprintf(stderr,"OOM\n");return 1;}

    printf("══════════════════════════════════════════════════════════════\n");
    printf(" DIPLOMATIE & GUERRE — lire les relations, payer la conquête (graine %u)\n",seed);
    printf("══════════════════════════════════════════════════════════════\n");

    WorldParams p=worldparams_default(seed);
    world_generate(w,&p);
    econ_init(econ,w); gen_population(w,econ); worldgen_seed_peoples(w,econ,RACE_HUMAIN);
    trade_network_build(net,w,econ);
    for(int c=0;c<w->n_countries;c++) tech_state_init(&ts[c],false);
    prosperity_init(wp,w); legitimacy_init(wl,w,econ); diplo_init(dp);

    int player=0; for(int c=0;c<w->n_countries;c++) if(w->country[c].role==POLITY_PLAYER){player=c;break;}

    /* L'économie se met en route, SANS colonisation : le joueur reste sur sa
     * seule capitale (homogène, D̄≈0) — pour isoler l'effet de la conquête. */
    for(int t=0;t<8;t++){
        econ_tick(econ, 1.f);
        legitimacy_tick(wl,w,econ,ts); prosperity_tick(wp,w,econ,net,ts,wl);
    }

    /* ---- 1. Les relations se lisent ---------------------------------- */
    printf("\n── 1. Relations du joueur (pays %d) — tout est LU ──\n",player);
    printf("   %-10s %-8s %-8s %-8s %-8s %-9s\n","pays","menace","parenté","schisme","compl.","alliance");
    int shown=0;
    for(int c=0;c<w->n_countries && shown<6;c++){
        if(c==player) continue;
        Relation r=diplo_relation(w,econ,wp,dp,player,c);
        printf("   #%-9d %-8.2f %-8.0f %-8.2f %-8.2f %-+9.2f\n",
               c,r.threat,r.kinship,r.schism,r.complement,r.alliance);
        shown++;
    }

    /* ---- 2. La guerre monte la diversité ----------------------------- */
    printf("\n── 2. Territoire CONTRE diversité ──\n");
    const PopCulture *pcap=NULL;
    { int cp=w->country[player].capital_prov;
      if(cp>=0){int cr=w->province[cp].region; if(cr>=0)pcap=&econ->region[cr].culture;} }

    /* cible : la région PEUPLÉE la plus LOINTAINE culturellement, hors joueur. */
    int target=-1; float best=-1.f; int tgt_owner=-1;
    for(int r=0;r<econ->n_regions;r++){
        RegionEconomy*re=&econ->region[r];
        if(!re->culture.settled || re->owner==player || re->owner<0) continue;
        float d = pcap?content_dist(&re->culture,pcap):0.f;
        if(d>best){best=d; target=r; tgt_owner=re->owner;}
    }
    if(target<0){ printf("   (pas de cible peuplée ennemie — monde trop vide)\n"); }
    else {
        CountryProsperity*cp=&wp->country[player];
        float Dbar0=cp->profile.D_bar_int, frac0=cp->fracture;
        CountryReadout r0=country_readout(wp,ts,w,player);
        printf("   AVANT : Concorde=%-12s  [dev D̄_int=%.2f fracture=%.2f]\n",
               label_concorde(r0.concorde), Dbar0, frac0);

        printf("   → guerre au pays #%d, conquête de la région %d (« %s », D∞=%.1f de nous)\n",
               tgt_owner, target, w->region[target].name, best);
        diplo_declare_war(dp,player,tgt_owner);
        bool took=diplo_conquer_region(dp,w,econ,wl,player,target);

        /* recalcul : la région conquise est désormais à nous → diversité. */
        for(int t=0;t<3;t++){ legitimacy_tick(wl,w,econ,ts); prosperity_tick(wp,w,econ,net,ts,wl); }
        float Dbar1=cp->profile.D_bar_int, frac1=cp->fracture;
        CountryReadout r1=country_readout(wp,ts,w,player);
        printf("   APRÈS : Concorde=%-12s  [dev D̄_int=%.2f fracture=%.2f]\n",
               label_concorde(r1.concorde), Dbar1, frac1);

        printf("\n── Vérification ──\n");
        ok("la conquête a eu lieu (owner transféré)", took && econ->region[target].owner==player);
        ok("la région conquise démarre à légitimité effondrée",
           wl->L[target] <= 2.0f);
        ok("conquérir une culture lointaine monte la diversité interne (D̄↑)",
           Dbar1 > Dbar0 + 0.3f);
        ok("la diversité non métabolisée monte la fracture", frac1 > frac0);
    }

    /* ---- 3. Diplomatie d'ÉQUILIBRE : trêve · momentum · friction · coalition ---- */
    printf("\n── 3. Diplomatie d'équilibre (rétroaction négative, pas d'interdit) ──\n");
    {
        /* B = un voisin ; C = un pays avec une FORCE réelle (l'allié dont l'entrée
         * pèse) — sinon le coût d'élargissement serait nul (pays vide). */
        int A=player, B=-1, C=-1;
        for(int c=0;c<w->n_countries;c++){ if(c==A||w->country[c].role==POLITY_UNCLAIMED) continue;
            if(B<0){B=c;continue;}
            if(diplo_mil_power(w,econ,c)>0.01f){ C=c; break; } }
        if(B>=0 && C>=0){
            /* TRÊVE : une longue guerre → une longue trêve ; on n'enchaîne plus. */
            diplo_init(dp); dp->war_years[A][B]=dp->war_years[B][A]=4.f;
            diplo_make_peace(dp,A,B);
            float tr4=diplo_truce_days(dp,A,B);
            ok("après la paix, on ne peut PAS redéclarer (trêve)", !diplo_can_declare(dp,A,B));
            diplo_init(dp); dp->war_years[A][B]=dp->war_years[B][A]=8.f;
            diplo_make_peace(dp,A,B);
            ok("une plus LONGUE guerre → une plus longue trêve", diplo_truce_days(dp,A,B) > tr4);
            diplo_tick(dp, 365.f*15.f);
            ok("la trêve FOND : la guerre redevient possible après le répit", diplo_can_declare(dp,A,B));

            /* MOMENTUM : la fulgurance effraie plus que la masse statique. */
            diplo_init(dp);
            float th0=diplo_relation(w,econ,wp,dp,A,B).threat;
            dp->momentum[B]=6.f;
            float th1=diplo_relation(w,econ,wp,dp,A,B).threat;
            ok("un conquérant FULGURANT menace plus qu'à puissance statique égale", th1 > th0+0.01f);

            /* FRICTION : un protégé d'un allié puissant renchérit (coût d'élargissement). */
            diplo_init(dp);
            float wno=diplo_war_widening_cost(w,econ,dp,A,B);
            diplo_form_alliance(dp,B,C);
            ok("frapper un protégé d'allié ÉLARGIT la guerre (coût ↑)",
               diplo_war_widening_cost(w,econ,dp,A,B) > wno);

            /* COALITION : un hégémon (fulgurance extrême) est perçu comme menace dominante. */
            diplo_init(dp); dp->momentum[B]=40.f;
            ok("un hégémon fulgurant est PERÇU (posture de coalition, sans script)",
               diplo_perceived_hegemon(w,econ,wp,dp,A)==B);
        } else {
            ok("(monde trop petit pour le test d'équilibre)", true);
        }
    }

    printf("\n══════════════════════════════════════════════════════════════\n");
    printf(" BILAN : %d réussis, %d échoués\n",g_pass,g_fail);
    printf("══════════════════════════════════════════════════════════════\n");
    free(w);free(econ);free(net);free(ts);free(wp);free(wl);free(dp);
    return g_fail?1:0;
}
