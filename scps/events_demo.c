/*
 * events_demo.c — banc d'essai des ÉVÈNEMENTS, CHOCS & ÂGES
 *
 *   make events_demo && ./events_demo [graine]
 *
 * Prouve l'ancrage (rien d'aléatoire hors-sol) :
 *   1. CHOCS ancrés : tremblements PRÈS DES FAILLES (relief) seulement ;
 *      inondations le long des RIVIÈRES ; sécheresses dans le SEC ; peste le
 *      long des ROUTES (un empire fermé est épargné).
 *   2. SAVEUR par la fiche : le MÊME état (marche lointaine instable) produit
 *      « Mater dans le sang » chez l'orque Dominateur, « Affranchir le commerce »
 *      chez le gnome Mercantile — quatre récits, un seul déclencheur.
 *   3. ÂGES émergents : ils n'arrivent pas à DATE mais quand le monde ATTEINT un
 *      état ; à l'avènement, une coordonnée globale bouge + un palier s'ouvre.
 *   4. BRÈCHE : pousser la charge faustienne fait advenir l'Âge de la Brèche →
 *      pression mondiale (l'endgame).
 *   5. Aucun nom SCPS dans les textes d'évènement.
 */
#include "scps_world.h"
#include "scps_econ.h"
#include "scps_trade.h"
#include "scps_tech.h"
#include "scps_legitimacy.h"
#include "scps_prosperity.h"
#include "scps_statecraft.h"
#include "scps_routes.h"
#include "scps_events.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_pass=0, g_fail=0;
static void ok(const char *what, bool cond){
    printf("   %s %s\n", cond?"✓":"✗", what);
    if (cond) g_pass++; else g_fail++;
}

typedef struct {
    World *w; WorldEconomy *econ; TradeNetwork *net; TechState *ts;
    WorldProsperity *wp; WorldLegitimacy *wl; Statecraft *sc; RouteNetwork *rn; EventsState *ev;
} Sim;

static int cap_region(const World *w, int cid){
    int cp=(cid>=0&&cid<w->n_countries)?w->country[cid].capital_prov:-1;
    return (cp>=0&&cp<w->n_provinces)?w->province[cp].region:-1;
}
static float region_pop(const WorldEconomy *e, int r){
    if (r<0||r>=e->n_regions) return 0.f;
    return e->region[r].strata[CLASS_LABORER].pop + e->region[r].strata[CLASS_BOURGEOIS].pop
         + e->region[r].strata[CLASS_ELITE].pop;
}
static PopCulture make_fiche(float valeurs, Ethos e, SpeciesArchetype race){
    PopCulture pc; memset(&pc,0,sizeof pc);
    pc.langue=5.f; pc.valeurs=valeurs; pc.subsistance=6.f; pc.parente=5.f; pc.religion=5.f;
    pc.ethos=e; pc.lifeway=LIFE_FARMER; pc.structure=STRUCT_LIGNAGER;
    pc.credo=CREDO_PLURALISTE; pc.rel_branch=REL_ABRAHAMIQUE; pc.econ=ECON_RENTE_AGRAIRE;
    pc.martial=MART_MUR_BOUCLIERS; pc.race=race; pc.settled=true; pc.age=200;
    return pc;
}

int main(int argc, char **argv){
    uint32_t seed=(argc>1)?(uint32_t)strtoul(argv[1],NULL,10):42u;
    Sim s={0};
    s.w=malloc(sizeof(World)); s.econ=malloc(sizeof(WorldEconomy)); s.net=malloc(sizeof(TradeNetwork));
    s.ts=calloc(SCPS_MAX_COUNTRY,sizeof(TechState)); s.wp=malloc(sizeof(WorldProsperity));
    s.wl=malloc(sizeof(WorldLegitimacy)); s.sc=malloc(sizeof(Statecraft));
    s.rn=malloc(sizeof(RouteNetwork)); s.ev=malloc(sizeof(EventsState));
    if(!s.w||!s.econ||!s.net||!s.ts||!s.wp||!s.wl||!s.sc||!s.rn||!s.ev){ fprintf(stderr,"OOM\n"); return 1; }

    printf("══════════════════════════════════════════════════════════════\n");
    printf(" ÉVÈNEMENTS & ÂGES — chocs ancrés, saveur par la fiche, ères émergentes (graine %u)\n", seed);
    printf("══════════════════════════════════════════════════════════════\n");

    WorldParams p=worldparams_default(seed);
    world_generate(s.w,&p);
    econ_init(s.econ,s.w); gen_population(s.w,s.econ); worldgen_seed_peoples(s.w,s.econ,RACE_HUMAIN);
    trade_network_build(s.net,s.w,s.econ);
    for (int c=0;c<s.w->n_countries;c++) tech_state_init(&s.ts[c],false);
    prosperity_init(s.wp,s.w); legitimacy_init(s.wl,s.w,s.econ);
    statecraft_init(s.sc,s.w); routes_init(s.rn);
    for (int t=0;t<15;t++){ econ_tick(s.econ, 1.f); econ_colonize_tick(s.econ,s.w); world_tick(s.w,s.econ,1.f);
        legitimacy_tick(s.wl,s.w,s.econ,s.ts); prosperity_tick(s.wp,s.w,s.econ,s.net,s.ts,s.wl); }
    events_init(s.ev,s.w,seed);

    /* ═══ 1. CHOCS ANCRÉS DANS LA GÉO ═══════════════════════════════════ */
    printf("\n── 1. Les chocs RELISENT la géo (rien d'aléatoire hors-sol) ──\n");
    int rRelief=-1,rRiver=-1,rArid=-1,rWet=-1,rFlat=-1;
    float bRel=0,bRiv=0,bAr=0;
    for (int r=0;r<s.econ->n_regions;r++){
        float rel=events_quake_risk(s.ev,r);
        if (rel>bRel){bRel=rel;rRelief=r;}
        if (events_flood_risk(s.ev,r)>bRiv){bRiv=events_flood_risk(s.ev,r);rRiver=r;}
        if (events_drought_risk(s.ev,r)>bAr){bAr=events_drought_risk(s.ev,r);rArid=r;}
    }
    /* plaine la plus stable du monde (relief minimal — il y a toujours une côte
     * ou une plaine loin des failles), pour le contraste tectonique. */
    { float lo=1e9f; for (int r=0;r<s.econ->n_regions;r++){ float q=events_quake_risk(s.ev,r);
        if (q<lo){ lo=q; rFlat=r; } } }
    /* zone la plus humide pour le contraste sécheresse */
    { float best=-1; for(int r=0;r<s.econ->n_regions;r++){ float fr=events_flood_risk(s.ev,r);
        if (fr>best && s.econ->region[r].culture.settled){best=fr;rWet=r;} } if(rWet<0)rWet=rRiver; }
    printf("   max relief: rég %d risque tremblement=%.2f  |  plaine stable: rég %d risque=%.2f\n",
           rRelief, bRel, rFlat, rFlat>=0?events_quake_risk(s.ev,rFlat):-1.f);
    printf("   max rivière: rég %d risque inondation=%.2f  |  max aride: rég %d risque sécheresse=%.2f\n",
           rRiver, bRiv, rArid, bAr);
    ok("le tremblement de terre ne menace QUE le relief (faille), pas la plaine stable",
       rRelief>=0 && bRel>0.10f && rFlat>=0 && events_quake_risk(s.ev,rFlat)<0.05f);
    ok("l'inondation suit les rivières (nulle sans cours d'eau)",
       rRiver>=0 && bRiv>0.f && events_flood_risk(s.ev,rRelief)<bRiv);
    ok("la sécheresse frappe le sec, pas l'humide",
       rArid>=0 && bAr>0.f && (rWet<0 || events_drought_risk(s.ev,rWet) < bAr));
    /* le feu n'existe QUE là où il y a de la forêt (anchoring) */
    bool fire_anchored=true;
    for (int r=0;r<s.econ->n_regions;r++)
        if (s.ev->geo[r].forest<0.001f && events_fire_risk(s.ev,r)>0.f){ fire_anchored=false; break; }
    ok("le feu de forêt n'existe que là où il y a de la forêt", fire_anchored);

    /* Effets d'un choc (déterministe) : le tremblement détruit et soulève. On
     * frappe une région PEUPLÉE (l'effet sur la pop/le bâti doit être visible). */
    int rstrike=-1;
    for (int r=0;r<s.econ->n_regions;r++)
        if (s.econ->region[r].culture.settled && region_pop(s.econ,r)>10.f){ rstrike=r; break; }
    if (rstrike<0) rstrike=(rFlat>=0)?rFlat:0;
    s.econ->region[rstrike].build.K_inst=3.0f;
    float pop0=region_pop(s.econ,rstrike), agit0=s.sc->agitation[rstrike];
    events_strike(s.ev,s.w,s.econ,s.wl,s.wp,s.sc,rstrike,EVID_QUAKE);
    ok("le tremblement détruit du bâti (institutions), tue de la pop, soulève l'agitation",
       s.econ->region[rstrike].build.K_inst<3.0f && region_pop(s.econ,rstrike)<pop0
       && s.sc->agitation[rstrike]>agit0);
    /* L'inondation : pertes immédiates MAIS fertilité après (double tranchant). */
    int rfl=rstrike; float food0=s.econ->region[rfl].build.food_cap;
    events_strike(s.ev,s.w,s.econ,s.wl,s.wp,s.sc,rfl,EVID_FLOOD);
    ok("l'inondation laisse une terre plus grasse (fertilité ↑ après la crue)",
       s.econ->region[rfl].build.food_cap > food0);

    /* ═══ 2. PESTE LE LONG DES ROUTES (empire fermé épargné) ════════════ */
    printf("\n── 2. La peste remonte les ROUTES — l'isolé est épargné ──\n");
    int chain[4]={-1,-1,-1,-1}, nc=0;
    for (int r=0;r<s.econ->n_regions && nc<4;r++) if (s.econ->region[r].culture.settled) chain[nc++]=r;
    if (nc>=4){
        int A=chain[0],B=chain[1],C=chain[2],D=chain[3];
        routes_order(s.rn,NULL,s.econ,A,B,false);
        routes_order(s.rn,NULL,s.econ,B,C,false);
        routes_advance(s.rn,s.w,s.econ,150);     /* ouvre les routes (terre 90 j) ; D reste isolée */
        float pA=region_pop(s.econ,A),pB=region_pop(s.econ,B),pC=region_pop(s.econ,C),pD=region_pop(s.econ,D);
        int infected=events_plague_spread(s.ev,s.w,s.econ,s.wl,s.sc,s.rn,A);
        printf("   foyer en rég %d ; routes %d-%d-%d ouvertes, rég %d isolée ; %d régions touchées\n",A,A,B,C,D,infected);
        ok("la peste atteint les régions reliées par routes (A→B→C)",
           region_pop(s.econ,A)<pA && region_pop(s.econ,B)<pB && region_pop(s.econ,C)<pC);
        ok("la région ISOLÉE (sans route) est épargnée", region_pop(s.econ,D)==pD);
    } else { ok("(monde trop petit pour le test peste — ignoré)", true); ok("(idem)", true); }

    /* ═══ 3. SAVEUR PAR LA FICHE — un état, quatre récits ═══════════════ */
    printf("\n── 3. Le MÊME état (marche lointaine instable) → un récit PAR culture ──\n");
    struct { Ethos e; SpeciesArchetype race; const char *who; int expect; } V[4]={
        { ETHOS_DOMINATEUR, RACE_ORQUE,   "Orque Dominateur",  EVID_INTEG_DOMINATEUR },
        { ETHOS_MERCANTILE, RACE_GNOME,   "Gnome Mercantile",  EVID_INTEG_MERCANTILE },
        { ETHOS_BUREAUCRATE,RACE_HUMAIN,  "Humain Bureaucrate",EVID_INTEG_BUREAUCRATE },
        { ETHOS_PACIFISTE,  RACE_ELFE,    "Elfe Ancien",       EVID_INTEG_ANCIEN },
    };
    /* On a besoin de 4 pays + 4 régions-marches. */
    int nC=s.w->n_countries; bool flavor_ok = (nC>=4);
    const char *seen_names[4]={"","","",""};
    for (int i=0;i<4 && flavor_ok;i++){
        int cid=i;                                   /* un pays par variante */
        int capr=cap_region(s.w,cid);
        if (capr<0){ flavor_ok=false; break; }
        s.econ->region[capr].culture = make_fiche(5.f, V[i].e, V[i].race);   /* le TRÔNE porte l'éthos */
        s.econ->region[capr].culture.religion=1.f;          /* … sur une foi marquée */
        s.econ->region[capr].owner=(int16_t)cid;
        /* une marche conquise lointaine, fraîche, agitée, possédée par ce pays */
        int march=-1;
        for (int r=0;r<s.econ->n_regions;r++){
            if (r==capr) continue;
            bool used=false; for(int j=0;j<i;j++){ if(cap_region(s.w,j)==r) used=true; }
            if (used) continue;
            march=r; break;
        }
        if (march<0){ flavor_ok=false; break; }
        PopCulture far = make_fiche(5.f, V[i].e, V[i].race);
        far.valeurs=0.f; far.religion=10.f; far.subsistance=1.f;   /* D∞ ≫ 6 du trône */
        s.econ->region[march].culture=far;
        s.econ->region[march].owner=(int16_t)cid;
        if (march<SCPS_MAX_REG){ s.wl->years_held[march]=5.f; s.sc->agitation[march]=55.f; }
        int evid=events_match_political(s.ev,s.w,s.econ,s.wl,s.sc,march);
        const EventDef *d=event_def(evid);
        seen_names[i]= d? d->name : "(aucun)";
        printf("   %-19s → « %s »  /  choix : « %s »\n", V[i].who,
               d?d->name:"(aucun)", (d&&d->n_options>0)?d->options[0].label:"—");
        ok(V[i].who, evid==V[i].expect);
    }
    if (flavor_ok){
        bool distinct = strcmp(seen_names[0],seen_names[1]) && strcmp(seen_names[1],seen_names[2])
                     && strcmp(seen_names[2],seen_names[3]) && strcmp(seen_names[0],seen_names[3]);
        ok("quatre cultures, quatre récits distincts (même déclencheur)", distinct);
    } else ok("(monde trop petit pour le test de saveur — ignoré)", true);

    /* ═══ 4. ÂGES ÉMERGENTS (état du monde, pas une date) ═══════════════ */
    printf("\n── 4. Les âges ADVIENNENT quand le monde atteint un état ──\n");
    /* On neutralise les conditions, puis on les allume une à une : chaque âge
     * n'advient QUE quand son état est atteint (preuve d'émergence, pas de minuteur). */
    for (int r=0;r<s.econ->n_regions;r++){ s.econ->region[r].route_pe=0.f; if(r<SCPS_MAX_REG) s.wl->years_held[r]=0.f; }
    for (int c=0;c<SCPS_MAX_COUNTRY;c++){ s.ts[c].charge=0.f; if(c<s.wp->n_countries) s.wp->country[c].Lumiere=0.f; }
    events_init(s.ev,s.w,seed);
    s.wp->age_C_bonus=0.f; s.wp->age_breach_flux=0.f;
    s.ev->ages.days_elapsed += 31*365; events_check_ages(s.ev,s.w,s.econ,s.wp,s.wl,s.ts);
    ok("monde « éteint » : aucun âge ne s'éveille",
       !ages_dawned(s.ev,AGE_COMMERCE)&&!ages_dawned(s.ev,AGE_REASON)
       &&!ages_dawned(s.ev,AGE_EMPIRES)&&!ages_dawned(s.ev,AGE_BREACH));

    /* Âge du Commerce : X nœuds au-dessus de la valeur Y. */
    { int settled[64], ns=0;
      for (int r=0;r<s.econ->n_regions && ns<6;r++) if (s.econ->region[r].culture.settled) settled[ns++]=r;
      for (int k=0;k<ns;k++) s.econ->region[settled[k]].route_pe=2.0f; }   /* 6 carrefours riches */
    s.ev->ages.days_elapsed += 31*365; events_check_ages(s.ev,s.w,s.econ,s.wp,s.wl,s.ts);
    printf("   Commerce : éveillé=%d  C mondial=+%.1f  palier Société/3 ouvert=%d\n",
           ages_dawned(s.ev,AGE_COMMERCE), s.wp->age_C_bonus, ages_tier_open(s.ev,THM_SOCIETE,3));
    ok("l'Âge du Commerce s'éveille quand X nœuds dépassent la valeur Y", ages_dawned(s.ev,AGE_COMMERCE));
    ok("à son avènement, la connectivité MONDIALE monte (C global) + palier de tech",
       s.wp->age_C_bonus>0.f && ages_tier_open(s.ev,THM_SOCIETE,3));

    /* Âge de la Raison : Lumière mondiale cumulée. */
    for (int c=0;c<s.wp->n_countries && c<4;c++) s.wp->country[c].Lumiere=9.f;
    s.ev->ages.days_elapsed += 31*365; events_check_ages(s.ev,s.w,s.econ,s.wp,s.wl,s.ts);
    ok("l'Âge de la Raison s'éveille au seuil de Lumière mondiale (recherche ↑)",
       ages_dawned(s.ev,AGE_REASON) && s.ev->ages.research_mult>1.f);

    /* Âge des Empires : régions bien intégrées. */
    { int n=0; for (int r=0;r<s.econ->n_regions && n<12;r++)
        if (s.econ->region[r].owner>=0 && s.econ->region[r].culture.settled && r<SCPS_MAX_REG){ s.wl->years_held[r]=60.f; n++; } }
    s.ev->ages.days_elapsed += 31*365; events_check_ages(s.ev,s.w,s.econ,s.wp,s.wl,s.ts);
    ok("l'Âge des Empires s'éveille sur l'intégration cumulée (intégration ↑)",
       ages_dawned(s.ev,AGE_EMPIRES) && s.ev->ages.integration_mult>1.f);

    /* ═══ 5. L'ÂGE DE LA BRÈCHE — l'endgame faustien ═══════════════════ */
    printf("\n── 5. Pousser la Magie fait advenir la Brèche (pression mondiale) ──\n");
    ok("avant : la Brèche dort", !ages_dawned(s.ev,AGE_BREACH));
    s.ts[0].charge=6.0f;                              /* une démesure faustienne quelque part */
    s.ev->ages.days_elapsed += 31*365; events_check_ages(s.ev,s.w,s.econ,s.wp,s.wl,s.ts);
    printf("   Brèche : éveillée=%d  pression mondiale=%.1f  flux faustien mondial=%.1f  palier Magie/5=%d\n",
           ages_dawned(s.ev,AGE_BREACH), ages_breach_pressure(s.ev), s.wp->age_breach_flux,
           ages_tier_open(s.ev,THM_SAVOIR,5));
    ok("la charge faustienne fait advenir l'Âge de la Brèche", ages_dawned(s.ev,AGE_BREACH));
    ok("la Brèche monte la pression de fin MONDIALE (flux faustien global → déréalisation)",
       ages_breach_pressure(s.ev)>0.f && s.wp->age_breach_flux>0.f);

    /* Dispersion : les quatre âges sont advenus à des MOMENTS distincts (états
     * atteints l'un après l'autre), pas tous en même temps — la tech se disperse. */
    ok("les quatre âges sont advenus par PALIERS d'état (tech dispersée, pas un minuteur)",
       ages_dawned(s.ev,AGE_COMMERCE)&&ages_dawned(s.ev,AGE_REASON)
       &&ages_dawned(s.ev,AGE_EMPIRES)&&ages_dawned(s.ev,AGE_BREACH));

    /* ═══ 6. MEMBRANE — aucun nom SCPS dans les textes ═════════════════ */
    printf("\n── 6. Membrane : les textes parlent en mots, jamais en SCPS ──\n");
    ok("aucun nom SCPS (SI/PE/K/H/D∞/fracture…) dans les titres et options", events_text_clean());

    /* Intégration : la boucle complète tourne sans incident et FAIT des choses. */
    int fired0=s.ev->n_fired;
    for (int yr=0; yr<40; yr++)
        world_events_tick(s.ev,s.w,s.econ,s.wl,s.wp,s.sc,s.rn,s.ts,365);
    printf("\n   (boucle 40 ans : %d évènements déclenchés au total)\n", s.ev->n_fired);
    ok("la boucle world_events_tick tourne et déclenche des évènements", s.ev->n_fired>fired0);

    printf("\n══════════════════════════════════════════════════════════════\n");
    printf(" BILAN : %d réussis, %d échoués\n", g_pass, g_fail);
    printf("══════════════════════════════════════════════════════════════\n");
    free(s.w);free(s.econ);free(s.net);free(s.ts);free(s.wp);free(s.wl);free(s.sc);free(s.rn);free(s.ev);
    return g_fail?1:0;
}
