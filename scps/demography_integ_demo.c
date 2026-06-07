/*
 * demography_integ_demo.c — la clé de voûte CÂBLÉE au moteur vivant
 *
 *   make demography_integ_demo && ./demography_integ_demo [graine]
 *
 * Prouve l'intégration (§7) sur le VRAI moteur (RegionEconomy porte des groupes,
 * prosperity les LIT, le verdict scps_order est INCHANGÉ) :
 *   A. Non-régression : attacher des groupes MONO ne change AUCUN nombre.
 *   B. Province mixte VIVANTE : injecter une minorité lointaine dans une région du
 *      joueur monte le D∞ du pays LU PAR prosperity → plus de fracture (le verdict).
 *   C. demography_tick vivant : la minorité conquise a une L plus basse que les
 *      natifs ; l'assimilation (P+K) fait DÉRIVER sa culture → le D du pays retombe.
 *   D. La dominante mène : RegionEconomy.culture suit le groupe majoritaire.
 */
#include "scps_world.h"
#include "scps_econ.h"
#include "scps_trade.h"
#include "scps_tech.h"
#include "scps_legitimacy.h"
#include "scps_prosperity.h"
#include "scps_modifier.h"
#include "scps_demography.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_pass=0, g_fail=0;
static void ok(const char *what, bool cond){
    printf("   %s %s\n", cond?"✓":"✗", what);
    if (cond) g_pass++; else g_fail++;
}
static float absf(float v){ return v<0?-v:v; }
static float cdist(const PopCulture*a,const PopCulture*b){
    float dv=absf(a->valeurs-b->valeurs),ds=absf(a->subsistance-b->subsistance);
    float dp=absf(a->parente-b->parente),dr=absf(a->religion-b->religion);
    float m=dv;if(ds>m)m=ds;if(dp>m)m=dp;if(dr>m)m=dr;return m;
}

int main(int argc, char **argv){
    uint32_t seed=(argc>1)?(uint32_t)strtoul(argv[1],NULL,10):42u;
    World *w=malloc(sizeof(World)); WorldEconomy *econ=malloc(sizeof(WorldEconomy));
    TechState *ts=calloc(SCPS_MAX_COUNTRY,sizeof(TechState));
    WorldProsperity *wp=malloc(sizeof(WorldProsperity)); WorldLegitimacy *wl=malloc(sizeof(WorldLegitimacy));
    ModifierStack *drift=malloc(sizeof(ModifierStack));
    if(!w||!econ||!ts||!wp||!wl||!drift){ fprintf(stderr,"OOM\n"); return 1; }

    printf("══════════════════════════════════════════════════════════════\n");
    printf(" DÉMOGRAPHIE INTÉGRÉE — la province RÉELLE porte des groupes (graine %u)\n", seed);
    printf("══════════════════════════════════════════════════════════════\n");

    WorldParams p=worldparams_default(seed);
    world_generate(w,&p);
    econ_init(econ,w); gen_population(w,econ); worldgen_seed_peoples(w,econ,RACE_HUMAIN);
    for (int c=0;c<w->n_countries;c++) tech_state_init(&ts[c],false);
    prosperity_init(wp,w); legitimacy_init(wl,w,econ);
    for (int t=0;t<20;t++){ econ_tick(econ, 1.f); econ_colonize_tick(econ,w); world_tick(w,econ,1.f);
        legitimacy_tick(wl,w,econ,ts); prosperity_tick(wp,w,econ,NULL,ts,wl); }
    int player=0; for (int c=0;c<w->n_countries;c++) if (w->country[c].role==POLITY_PLAYER){player=c;break;}

    /* ═══ A. NON-RÉGRESSION : attacher des groupes mono ne change rien ══ */
    printf("\n── A. Non-régression : attacher des groupes MONO = les nombres d'hier ──\n");
    prosperity_tick(wp,w,econ,NULL,ts,wl);
    float Dinf_before=wp->country[player].profile.D_inf_int;
    float frac_before=wp->country[player].fracture, SI_before=wp->country[player].SI;
    demography_attach(w,econ,drift);                 /* 1 groupe substrat par région */
    prosperity_tick(wp,w,econ,NULL,ts,wl);
    float Dinf_attach=wp->country[player].profile.D_inf_int;
    float frac_attach=wp->country[player].fracture, SI_attach=wp->country[player].SI;
    printf("   pays joueur : D∞ %.3f→%.3f | fracture %.3f→%.3f | SI %.2f→%.2f\n",
           Dinf_before,Dinf_attach, frac_before,frac_attach, SI_before,SI_attach);
    ok("attacher des groupes mono ne change NI le D, NI la fracture, NI la SI",
       absf(Dinf_attach-Dinf_before)<0.01f && absf(frac_attach-frac_before)<0.01f
       && absf(SI_attach-SI_before)<0.01f);

    /* ═══ B. PROVINCE MIXTE VIVANTE → le moteur la LIT ═════════════════ */
    printf("\n── B. Un empire homogène conquiert une marche lointaine ──\n");
    /* On part d'un empire HOMOGÈNE (natifs = couronne) — le cas-clé propre. */
    int cap_prov=w->country[player].capital_prov, crown_reg=w->province[cap_prov].region;
    PopCulture crown=econ->region[crown_reg].culture;
    int rP=-1;
    for (int r=0;r<econ->n_regions;r++) if (econ->region[r].owner==player && econ->region[r].pop.n_groups>0){
        PopGroup *g=&econ->region[r].pop.groups[0];
        g->origin=crown; g->culture=crown; g->race=crown.race; g->origin_sphere=SPHERE_HOMMES;
        g->L=7.f; g->integration=1.f; econ->region[r].pop.n_groups=1; econ->region[r].culture=crown;
        if (rP<0) rP=r;
    }
    prosperity_tick(wp,w,econ,NULL,ts,wl);
    float D0=wp->country[player].profile.D_inf_int, frac0=wp->country[player].fracture;
    /* La marche conquise : on dépose une minorité aux ANTIPODES de la couronne. */
    PopCulture mc=crown;
    mc.valeurs=(crown.valeurs<5?10.f:0.f); mc.religion=(crown.religion<5?10.f:0.f);
    mc.subsistance=(crown.subsistance<5?9.f:1.f); mc.parente=(crown.parente<5?9.f:1.f);
    ProvincePop *pp=&econ->region[rP].pop;
    PopGroup m; memset(&m,0,sizeof m);
    m.race=RACE_ORQUE; m.origin_sphere=SPHERE_ETRANGERS; m.origin=mc; m.culture=mc; m.klass=CLASS_LABORER;
    m.count=pp->groups[0].count/3+50; m.L=5.f; m.integration=0.f; m.diaspora=true; m.drift_id=50000;
    pp->groups[pp->n_groups++]=m;
    float pdinf=province_Dinf(pp,drift);
    prosperity_tick(wp,w,econ,NULL,ts,wl);
    float D1=wp->country[player].profile.D_inf_int, frac1=wp->country[player].fracture;
    printf("   région %d : %d groupes, D∞ interne=%.1f | pays : D∞ %.2f→%.2f, fracture %.2f→%.2f\n",
           rP, pp->n_groups, pdinf, D0,D1, frac0,frac1);
    ok("la province devient mixte (≥2 groupes) → D∞ interne RÉEL", pp->n_groups>=2 && pdinf>6.f);
    ok("le moteur (prosperity/scps_order, INCHANGÉ) lit ce D → plus de fracture",
       D1>D0+2.f && frac1>frac0+0.1f);

    /* ═══ C. demography_tick VIVANT : L par groupe, puis assimilation ══ */
    printf("\n── C. demography_tick : la minorité a une L basse, puis s'assimile ──\n");
    for (int yr=0; yr<8; yr++) demography_tick(w,econ,wl,drift,5.f,5.f,1.f);
    const PopGroup *dom=province_dominant(pp);
    float Lnat=dom?dom->L:0, Lmin=10.f; int mi=-1;
    for (int i=0;i<pp->n_groups;i++) if (&pp->groups[i]!=dom && cdist(&pp->groups[i].origin,&crown)>3.f)
        if (pp->groups[i].L<Lmin){ Lmin=pp->groups[i].L; mi=i; }
    printf("   région %d : L natifs (couronne)=%.1f vs L minorité conquise=%.1f\n", rP, Lnat, Lmin);
    ok("la légitimité est VÉCUE par groupe : la minorité conquise a une L plus basse",
       mi>=0 && Lmin < Lnat - 0.5f);
    if (mi>=0){
        PopCulture morig=pp->groups[mi].origin;
        float dist0=cdist(&pp->groups[mi].culture,&crown);
        for (int yr=0; yr<70; yr++) demography_tick(w,econ,wl,drift,7.f,7.f,1.f);
        bool gone=true; float dist1=0;
        for (int i=0;i<pp->n_groups;i++) if (cdist(&pp->groups[i].origin,&morig)<0.1f){
            gone=false; dist1=cdist(&pp->groups[i].culture,&crown);
        }
        printf("   après ~80 ans d'assimilation (P+K=7) : minorité %s (distance à la couronne %.1f→%.1f)\n",
               gone?"FUSIONNÉE":"en dérive", dist0, dist1);
        ok("l'assimilation (P+K, durable) fait dériver la minorité vers la dominante",
           gone || dist1 < dist0 - 1.5f);
    }

    /* ═══ D. La dominante mène — RegionEconomy.culture suit le majoritaire ═ */
    printf("\n── D. RegionEconomy.culture suit le groupe DOMINANT ──\n");
    const PopGroup *dom2=province_dominant(&econ->region[rP].pop);
    ok("la culture de la région = celle du groupe dominant (la dominante mène)",
       dom2 && cdist(&econ->region[rP].culture,&dom2->culture)<0.01f);

    printf("\n══════════════════════════════════════════════════════════════\n");
    printf(" BILAN : %d réussis, %d échoués\n", g_pass, g_fail);
    printf("══════════════════════════════════════════════════════════════\n");
    free(w);free(econ);free(ts);free(wp);free(wl);free(drift);
    return g_fail?1:0;
}
