/*
 * ai_demo.c — banc d'essai de la boucle de décision IA (§13.1)
 *
 *   make ai_demo && ./ai_demo [graine]
 *
 * Le point dur, prouvé : un LECTEUR de coordonnées qui choisit des LEVIERS, sans
 * triche ni script. On lie trois acteurs au MÊME ai_step ; seules leurs fiches
 * diffèrent (Dominateur / Mercantile / Bureaucrate). On regarde émerger trois
 * conduites distinctes :
 *   - le Dominateur déclare plus de guerres ;
 *   - le Mercantile ouvre plus de routes ;
 *   - le Bureaucrate bâtit plus de K —
 * sans une seule ligne de code « si pays==X ».
 *
 * Puis on prouve LE FREIN : un acteur dont la SI tombe ou dont la diversité
 * interne D∞ dépasse sa capacité K cesse d'attaquer et consolide (il digère).
 */
#include "scps_world.h"
#include "scps_econ.h"
#include "scps_trade.h"
#include "scps_tech.h"
#include "scps_legitimacy.h"
#include "scps_prosperity.h"
#include "scps_agency.h"
#include "scps_routes.h"
#include "scps_diplo.h"
#include "scps_ai.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

static int g_pass=0, g_fail=0;
static void ok(const char *what, bool cond){
    printf("   %s %s\n", cond?"✓":"✗", what);
    if (cond) g_pass++; else g_fail++;
}

/* ---- Contexte ---------------------------------------------------------- */
typedef struct {
    World *w; WorldEconomy *econ; TradeNetwork *net; TechState *ts;
    WorldProsperity *wp; WorldLegitimacy *wl; AgencyState *ag;
    RouteNetwork *rn; DiploState *dp;
} Sim;

#define STEP 10   /* on avance le monde par pas de 10 jours (le jour reste l'atome IA) */

static void world_step(Sim *s, AiActor *act, int n_act, int day){
    econ_tick(s->econ);
    agency_advance(s->ag, s->w, s->econ, s->wl, STEP);
    routes_advance(s->rn, s->w, s->econ, STEP);
    for (int i=0;i<n_act;i++) ai_step(&act[i], s->w, s->econ, s->wp, s->wl,
                                      s->ag, s->rn, s->dp, day);
    legitimacy_tick(s->wl, s->w, s->econ, s->ts);
    prosperity_tick(s->wp, s->w, s->econ, s->net, s->ts, s->wl);
    diplo_tick(s->dp, (float)STEP);
}

static int cap_region(const World *w, int cid){
    int cp=(cid>=0&&cid<w->n_countries)?w->country[cid].capital_prov:-1;
    return (cp>=0&&cp<w->n_provinces)?w->province[cp].region:-1;
}

/* Pose une fiche culturelle sur la région-capitale d'un pays (l'entrée que l'IA
 * LIT pour dériver sa personnalité). C'est tout le « scénario » : on ne touche
 * pas à la décision, juste à la donnée d'entrée. */
static PopCulture make_fiche(float valeurs, Ethos e, EconTrait ec, Credo cr){
    PopCulture pc; memset(&pc,0,sizeof pc);
    pc.langue=5.f; pc.valeurs=valeurs; pc.subsistance=6.f; pc.parente=5.f; pc.religion=5.f;
    pc.ethos=e; pc.lifeway=LIFE_FARMER; pc.structure=STRUCT_LIGNAGER;
    pc.credo=cr; pc.rel_branch=REL_ABRAHAMIQUE; pc.econ=ec; pc.martial=MART_MUR_BOUCLIERS;
    pc.race=RACE_HUMAIN; pc.settled=true; pc.age=200;
    return pc;
}
static void set_capital_fiche(Sim *s, int cid, PopCulture fiche, float healthK){
    int r=cap_region(s->w,cid);
    if (r<0) return;
    RegionEconomy *re=&s->econ->region[r];
    re->culture=fiche; re->owner=(int16_t)cid; re->colonized=true;
    if (re->strata[CLASS_LABORER].pop < 200.f) re->strata[CLASS_LABORER].pop=400.f;
    if (re->strata[CLASS_ELITE].pop   < 20.f)  re->strata[CLASS_ELITE].pop=40.f;
    /* Empire développé : on part avec un K/L sain pour que le frein ne morde pas
     * AVANT qu'on l'ait mérité (on veut voir l'expansion, puis la digestion). */
    s->ts[cid].K=healthK; s->ts[cid].L=6.f;
}

static const char *posture_word(const AiActor *a){
    float m=a->w_expand; const char *s="Conquérant";
    if (a->w_trade>m){ m=a->w_trade; s="Marchand"; }
    if (a->w_build>m){ m=a->w_build; s="Bâtisseur"; }
    if (a->w_faith>m){ m=a->w_faith; s="Zélote"; }
    return s;
}
static bool strict_max(float x,float y,float z){ return x>y && x>z; }

int main(int argc, char **argv){
    uint32_t seed=(argc>1)?(uint32_t)strtoul(argv[1],NULL,10):42u;

    Sim s={0};
    s.w =(World*)malloc(sizeof(World));            s.econ=(WorldEconomy*)malloc(sizeof(WorldEconomy));
    s.net=(TradeNetwork*)malloc(sizeof(TradeNetwork)); s.ts=(TechState*)calloc(SCPS_MAX_COUNTRY,sizeof(TechState));
    s.wp=(WorldProsperity*)malloc(sizeof(WorldProsperity)); s.wl=(WorldLegitimacy*)malloc(sizeof(WorldLegitimacy));
    s.ag=(AgencyState*)malloc(sizeof(AgencyState)); s.rn=(RouteNetwork*)malloc(sizeof(RouteNetwork));
    s.dp=(DiploState*)malloc(sizeof(DiploState));
    if(!s.w||!s.econ||!s.net||!s.ts||!s.wp||!s.wl||!s.ag||!s.rn||!s.dp){ fprintf(stderr,"OOM\n"); return 1; }

    printf("══════════════════════════════════════════════════════════════\n");
    printf(" IA — un lecteur de coordonnées qui choisit des leviers (graine %u)\n", seed);
    printf("══════════════════════════════════════════════════════════════\n");

    WorldParams p=worldparams_default(seed);
    world_generate(s.w,&p);
    econ_init(s.econ,s.w); gen_population(s.w,s.econ); worldgen_seed_peoples(s.w,s.econ,RACE_HUMAIN);
    trade_network_build(s.net,s.w,s.econ);
    for (int c=0;c<s.w->n_countries;c++) tech_state_init(&s.ts[c],false);
    prosperity_init(s.wp,s.w); legitimacy_init(s.wl,s.w,s.econ); agency_init(s.ag);
    routes_init(s.rn); diplo_init(s.dp);

    /* Les pays « réels » (non-vierges) — on en prend trois comme acteurs. */
    int polity[SCPS_MAX_COUNTRY], npol=0;
    for (int c=0;c<s.w->n_countries;c++)
        if (s.w->country[c].role!=POLITY_UNCLAIMED && cap_region(s.w,c)>=0) polity[npol++]=c;
    if (npol<3){ fprintf(stderr,"monde trop vide (%d pays) — autre graine\n",npol); return 1; }

    int cidD=polity[0], cidM=polity[1], cidB=polity[2];

    /* Trois fiches → trois personnalités. La SEULE différence entre les acteurs. */
    set_capital_fiche(&s, cidD, make_fiche(9.0f, ETHOS_DOMINATEUR,  ECON_RENTE_AGRAIRE, CREDO_PLURALISTE), 6.0f);
    set_capital_fiche(&s, cidM, make_fiche(3.0f, ETHOS_MERCANTILE,   ECON_GUILDE,        CREDO_PLURALISTE), 6.0f);
    set_capital_fiche(&s, cidB, make_fiche(4.5f, ETHOS_BUREAUCRATE,  ECON_RENTE_AGRAIRE, CREDO_PLURALISTE), 6.0f);

    /* On plante des proies FAIBLES au contact du Dominateur : des principautés
     * voisines sans défense, de culture PROCHE (les avaler ne le surétend pas —
     * on veut voir son appétit s'exprimer, pas le frein le figer tout de suite).
     * Le Marchand et le Bâtisseur, eux, n'ont pas de cible facile. */
    int rD=cap_region(s.w,cidD), nbarb=0;
    {
        /* Des pays VIERGES (sans territoire) comme coquilles : la principauté
         * ne possèdera que la région plantée → réellement faible (un vrai
         * cité-état traînerait son armée d'origine et ne serait pas une proie). */
        int spare[SCPS_MAX_COUNTRY], nsp=0;
        for (int c=0;c<s.w->n_countries;c++)
            if (c!=cidD&&c!=cidM&&c!=cidB && s.w->country[c].role==POLITY_UNCLAIMED) spare[nsp++]=c;
        if (nsp==0) for (int c=0;c<s.w->n_countries;c++)
            if (c!=cidD&&c!=cidM&&c!=cidB) spare[nsp++]=c;
        for (int r=0; r<s.econ->n_regions && rD>=0 && nbarb<3 && nbarb<nsp; r++){
            if (r==rD || !s.econ->adj[rD][r] || !s.econ->region[r].active) continue;
            int barb=spare[nbarb];
            s.w->country[barb].role=POLITY_CITY_STATE;
            s.w->country[barb].capital_prov=s.w->region[r].province_ids[0];
            RegionEconomy *re=&s.econ->region[r];
            /* Culture intermédiaire (D̄≈5 du Dominateur) : une vraie rivale, mais
             * PETITE — l'avaler ne suffit pas à le surétendre (ça, c'est le test
             * du frein, plus bas, où on lui fait avaler de l'inassimilable). */
            re->culture=make_fiche(4.f, ETHOS_PACIFISTE, ECON_RENTE_AGRAIRE, CREDO_PLURALISTE);
            re->owner=(int16_t)barb; re->colonized=true;
            re->strata[CLASS_LABORER].pop=30.f; re->strata[CLASS_ELITE].pop=2.f;
            re->build.H_coerc=0.f;                               /* sans défense */
            nbarb++;
        }
    }

    /* On lie les acteurs APRÈS avoir posé les fiches (l'IA lit l'entrée). */
    AiActor act[3];
    ai_actor_init(&act[0], s.w, s.econ, cidD, seed^0xA1u);
    ai_actor_init(&act[1], s.w, s.econ, cidM, seed^0xB2u);
    ai_actor_init(&act[2], s.w, s.econ, cidB, seed^0xC3u);
    const char *NAME[3]={"Dominateur","Mercantile","Bâtisseur"};

    /* Amorce la prospérité (l'IA doit lire un état au premier réveil). */
    legitimacy_tick(s.wl,s.w,s.econ,s.ts);
    prosperity_tick(s.wp,s.w,s.econ,s.net,s.ts,s.wl);

    printf("\n── Trois fiches, un seul ai_step. Personnalités dérivées ──\n");
    for (int i=0;i<3;i++){
        AiView v=ai_observe(s.wp,s.w,s.econ,act[i].cid);
        printf("  %-11s [%-10s]  poids: expand=%.2f trade=%.2f build=%.2f faith=%.2f"
               "   [dev SI=%.1f K=%.1f]\n",
               NAME[i], posture_word(&act[i]),
               act[i].w_expand, act[i].w_trade, act[i].w_build, act[i].w_faith, v.SI, v.K);
    }

    /* ---- Les poids EUX-MÊMES suivent la fiche (preuve déterministe) ------- */
    printf("\n── Vérification : la personnalité ÉMERGE de la fiche ──\n");
    ok("le Dominateur a le plus fort appétit de conquête (w_expand)",
       strict_max(act[0].w_expand, act[1].w_expand, act[2].w_expand));
    ok("le Mercantile a le plus fort appétit de commerce (w_trade)",
       strict_max(act[1].w_trade, act[0].w_trade, act[2].w_trade));
    ok("le Bâtisseur a le plus fort appétit de K (w_build)",
       strict_max(act[2].w_build, act[0].w_build, act[1].w_build));
    {
        AiView vD=ai_observe(s.wp,s.w,s.econ,cidD);
        AiView vM=ai_observe(s.wp,s.w,s.econ,cidM);
        AiView vB=ai_observe(s.wp,s.w,s.econ,cidB);
        float gD=ai_aggression(&act[0],&vD), gB=ai_aggression(&act[2],&vB), gM=ai_aggression(&act[1],&vM);
        ok("l'agressivité nette ordonne Dominateur > Bâtisseur > Mercantile", gD>gB && gB>gM);
    }

    /* ---- L'arc : on laisse tourner, on tally les ACTES (mêmes verbes) ----- */
    printf("\n── 60 ans : on regarde émerger trois conduites (même code) ──\n");
    int horizon = 60*SCPS_DAYS_PER_YEAR;
    for (int day=0; day<horizon; day+=STEP) world_step(&s, act, 3, day);

    printf("  %-11s   guerres=%d  conquêtes=%d  routes=%d  K bâti=%d  greniers/marchés=%d  consolidations=%d\n",
           NAME[0], act[0].stats.wars, act[0].stats.conquests, act[0].stats.routes,
           act[0].stats.builds_k, act[0].stats.builds_other, act[0].stats.consolidations);
    printf("  %-11s   guerres=%d  conquêtes=%d  routes=%d  K bâti=%d  greniers/marchés=%d  consolidations=%d\n",
           NAME[1], act[1].stats.wars, act[1].stats.conquests, act[1].stats.routes,
           act[1].stats.builds_k, act[1].stats.builds_other, act[1].stats.consolidations);
    printf("  %-11s   guerres=%d  conquêtes=%d  routes=%d  K bâti=%d  greniers/marchés=%d  consolidations=%d\n",
           NAME[2], act[2].stats.wars, act[2].stats.conquests, act[2].stats.routes,
           act[2].stats.builds_k, act[2].stats.builds_other, act[2].stats.consolidations);

    printf("\n── Vérification : trois conduites distinctes, sans IA dédiée ──\n");
    ok("le Mercantile ouvre le PLUS de routes",
       strict_max(act[1].stats.routes, act[0].stats.routes, act[2].stats.routes));
    ok("le Bâtisseur bâtit le PLUS de K",
       strict_max(act[2].stats.builds_k, act[0].stats.builds_k, act[1].stats.builds_k));
    {
        int aD=act[0].stats.wars+act[0].stats.conquests;
        int aM=act[1].stats.wars+act[1].stats.conquests;
        int aB=act[2].stats.wars+act[2].stats.conquests;
        ok("le Dominateur est le plus agressif (guerres+conquêtes)", aD>=aM && aD>=aB && aD>0);
    }

    /* ---- LE FREIN — au niveau de la fonction (déterministe) --------------- */
    printf("\n── Vérification : le frein de survie (consolidation) ──\n");
    {
        AiView sain   ={ .SI=7.f, .fragilite=2.f, .L=6.f, .K=6.f, .Dinf_interne=2.f, .armee=3.f };
        AiView siBas  ={ .SI=3.f, .fragilite=4.f, .L=4.f, .K=6.f, .Dinf_interne=2.f, .armee=3.f };
        AiView surext ={ .SI=6.f, .fragilite=4.f, .L=5.f, .K=4.f, .Dinf_interne=10.f, .armee=3.f };
        ok("un empire sain ne ressent presque aucune pression de consolidation",
           ai_consolidation_pressure(&sain) < 0.2f);
        ok("une SI qui s'effondre déclenche le frein",
           ai_consolidation_pressure(&siBas) > 0.8f);
        ok("la surextension (D∞ > K) déclenche le frein",
           ai_consolidation_pressure(&surext) > 0.5f);
    }

    /* ---- LE FREIN — dans la boucle : on injecte de la diversité au Dominateur,
     * il CESSE d'attaquer et consolide. On gèle l'éco pour isoler la stratégie. */
    {
        /* Une jeune puissance qui a mordu trop gros : on rabat sa capacité K
         * institutionnelle (tech + bâti) au plancher, PUIS on lui fait avaler des
         * cultures aux antipodes (D∞→10). D∞ ≫ K = surextension : le moteur le
         * dira fragile, l'IA doit le lire et cesser de mordre. */
        s.ts[cidD].K=0.f;
        for (int r=0;r<s.econ->n_regions;r++)
            if (s.econ->region[r].owner==cidD) s.econ->region[r].build.K_inst=0.f;
        int injected=0;
        for (int r=0;r<s.econ->n_regions && injected<3;r++){
            RegionEconomy *re=&s.econ->region[r];
            if (re->owner==cidD || !re->active) continue;
            re->culture=make_fiche((injected%2)?0.f:10.f, ETHOS_HONNEUR, ECON_TRIBUT, CREDO_EVANGELISTE);
            re->culture.valeurs=(injected%2)?0.f:10.f;
            re->culture.subsistance=(injected%2)?1.f:9.f;
            re->culture.religion=(injected%2)?10.f:0.f;
            re->owner=(int16_t)cidD; re->colonized=true;
            if (re->strata[CLASS_LABORER].pop<50.f) re->strata[CLASS_LABORER].pop=100.f;
            injected++;
        }
        legitimacy_tick(s.wl,s.w,s.econ,s.ts);
        prosperity_tick(s.wp,s.w,s.econ,s.net,s.ts,s.wl);

        AiView vinj=ai_observe(s.wp,s.w,s.econ,cidD);
        float brake=ai_consolidation_pressure(&vinj);
        printf("  Dominateur après avoir avalé %d cultures lointaines :"
               " D∞_interne=%.1f  K=%.1f  SI=%.1f  → pression=%.2f\n",
               injected, vinj.Dinf_interne, vinj.K, vinj.SI, brake);
        ok("avaler du lointain met le Dominateur en surextension (pression haute)", brake>0.6f);

        int wars0=act[0].stats.wars, cons0=act[0].stats.consolidations;
        act[0].next_econ_day=INT_MAX;             /* gèle l'éco (pas de K bâti → frein figé) */
        act[0].peace_lock_until=0;
        int d0=horizon;
        for (int k=0;k<6;k++){ act[0].next_strat_day=d0; ai_step(&act[0],s.w,s.econ,s.wp,s.wl,s.ag,s.rn,s.dp,d0); }
        printf("  Sous le frein : guerres +%d, consolidations +%d\n",
               act[0].stats.wars-wars0, act[0].stats.consolidations-cons0);
        ok("le Dominateur surétendu CESSE de déclarer la guerre", act[0].stats.wars==wars0);
        ok("le Dominateur surétendu consolide (il digère)", act[0].stats.consolidations>cons0);
    }

    printf("\n══════════════════════════════════════════════════════════════\n");
    printf(" BILAN : %d réussis, %d échoués\n", g_pass, g_fail);
    printf("══════════════════════════════════════════════════════════════\n");
    free(s.w);free(s.econ);free(s.net);free(s.ts);free(s.wp);free(s.wl);free(s.ag);free(s.rn);free(s.dp);
    return g_fail?1:0;
}
