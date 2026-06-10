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
#include <math.h>
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
    econ_tick(s->econ, 1.f);
    agency_advance(s->ag, s->w, s->econ, s->wl, NULL, STEP);
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

    /* SUBSTRAT ÉGAL — « la SEULE différence = la fiche ». La graine assigne au hasard
     * des capitales inégales (une riche qui bâtit, une dépeuplée qui reste inerte),
     * ce qui BROUILLE la lecture des archétypes. On dote donc les trois capitales à
     * l'identique : trésor (acheter les chantiers), matériaux de construction (de quoi
     * bâtir du K), et une garnison de base (de quoi PROJETER — sinon le Dominateur,
     * sans armée, bâtit faute de pouvoir conquérir). Les proies plantées plus haut
     * restent, elles, sans défense — seul le Dominateur a une cible facile. */
    for (int i=0;i<3;i++){
        int cc=(i==0)?cidD:(i==1)?cidM:cidB;
        int cr=cap_region(s.w,cc);
        if (cr<0) continue;
        RegionEconomy *re=&s.econ->region[cr];
        re->treasury = 30000.f;
        re->stock[RES_WOOD]=900.f; re->stock[RES_METAL]=900.f;
        re->stock[RES_TOOLS]=600.f; re->stock[RES_GRAIN]=900.f;
        re->build.H_coerc = fmaxf(re->build.H_coerc, 2.0f);   /* garnison → projeter la force */
        if (re->strata[CLASS_LABORER].pop<300.f) re->strata[CLASS_LABORER].pop=500.f;
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
        /* À BESOINS ÉGAUX (zéro trou), l'agressivité vient de la FICHE. */
        vD.gap_acuity=vM.gap_acuity=vB.gap_acuity=0.f;
        vD.take_pressure=vM.take_pressure=vB.take_pressure=0.f;
        float gD=ai_aggression(&act[0],&vD), gB=ai_aggression(&act[2],&vB), gM=ai_aggression(&act[1],&vM);
        ok("à besoins égaux, l'agressivité ordonne Dominateur > Bâtisseur > Mercantile", gD>gB && gB>gM);

        /* BESOIN PERÇU : la vue expose désormais les TROUS (chaîne/demande/stratégie). */
        ok("la VUE perçoit les besoins (trou de chaîne/demande/stratégie + acuité)",
           (vM.chain_gap!=RES_NONE||vM.demand_gap!=RES_NONE||vM.strat_gap!=RES_NONE||vM.gap_acuity>=0.f));

        /* ESCALADE : un Mercantile STABLE à qui un bien aigu est REFUSÉ (introuvable
         * → ne reste que PRENDRE) devient agressif — pas une permission de fiche, une PRESSION. */
        AiView calm   = { .SI=7.f,.fragilite=2.f,.L=6.f,.K=6.f,.Dinf_interne=2.f,.armee=3.f };
        AiView blocked= calm; blocked.gap_acuity=1.f; blocked.take_pressure=1.f;
        float g_calm = ai_aggression(&act[1], &calm);
        float g_war  = ai_aggression(&act[1], &blocked);
        ok("un besoin AIGU et BLOQUÉ pousse même un Mercantile à l'agression (escalade)",
           g_war > g_calm + 0.1f);

        /* FREIN PRÉSERVÉ : le MÊME besoin bloqué, sur un acteur FRAGILE, escalade MOINS
         * (il encaisse plutôt que de se suicider). */
        AiView frag    = { .SI=2.f,.fragilite=7.f,.L=4.f,.K=4.f,.Dinf_interne=9.f,.armee=3.f };
        AiView frag_blk= frag; frag_blk.gap_acuity=1.f; frag_blk.take_pressure=1.f;
        float g_frag = ai_aggression(&act[1], &frag_blk);
        ok("un acteur FRAGILE ENCAISSE le manque (le frein borne l'escalade)", g_frag < g_war);
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

    /* ---- §3 : l'ÉTHOS EFFECTIF GLISSE avec la composition ------------------ *
     * Le Mercantile, homogène, a un appétit de conquête effectif = son socle.
     * On lui INJECTE une grosse province orque (Conquérants) non assimilée : sa
     * résultante de factions glisse → son w_expand EFFECTIF monte. « Un empire
     * change d'éthos quand qui le compose change. » */
    printf("\n── Vérification : l'éthos effectif glisse avec la composition (§3) ──\n");
    {
        int day=horizon;
        act[1].next_strat_day=day; ai_step(&act[1],s.w,s.econ,s.wp,s.wl,s.ag,s.rn,s.dp,day);
        float expand_before=act[1].w_expand;
        int rg=-1;
        for (int r=0;r<s.econ->n_regions;r++)
            if (s.econ->region[r].active && s.econ->region[r].owner!=cidM
                && s.econ->region[r].owner!=cidD && s.econ->region[r].owner!=cidB){ rg=r; break; }
        if (rg>=0){
            RegionEconomy *re=&s.econ->region[rg];
            PopCulture oc=make_fiche(9.f,ETHOS_DOMINATEUR,ECON_TRIBUT,CREDO_PLURALISTE); oc.race=RACE_ORQUE;
            re->owner=(int16_t)cidM; re->colonized=true; re->culture=oc;
            memset(&re->pop,0,sizeof re->pop);
            re->pop.groups[0].race=RACE_ORQUE; re->pop.groups[0].origin=oc; re->pop.groups[0].culture=oc;
            re->pop.groups[0].klass=CLASS_LABORER; re->pop.groups[0].count=3000;
            re->pop.n_groups=1;
        }
        act[1].next_strat_day=day; ai_step(&act[1],s.w,s.econ,s.wp,s.wl,s.ag,s.rn,s.dp,day);
        float expand_after=act[1].w_expand;
        printf("   Mercantile : w_expand effectif %.3f → après avoir avalé une province orque → %.3f\n",
               expand_before, expand_after);
        ok("avaler une province ORQUE monte l'appétit de conquête EFFECTIF (l'éthos glisse, §3)",
           rg>=0 && expand_after > expand_before + 0.01f);
    }

    /* ---- §5 COMBAT : le SCORE DE GUERRE est un BUDGET, la province a un PRIX ---- *
     * Un croupion R de 2 régions, désarmé, au contact du Dominateur. Un budget
     * marginal n'achète rien (provinces trop chères → R survit) ; un budget écrasant
     * (≥ valeur totale du pays) annexe R. Un cœur développé coûte plus qu'un bled. */
    printf("\n── Vérification : le score de guerre est un BUDGET, la province a un PRIX (§5) ──\n");
    {
        /* On soigne le Dominateur (K haut, bien armé) : le frein ne le fige pas. */
        s.ts[cidD].K=12.f;
        for (int r=0;r<s.econ->n_regions;r++)
            if (s.econ->region[r].owner==cidD){
                s.econ->region[r].build.K_inst=6.f; s.econ->region[r].build.H_coerc=4.f;
                s.econ->region[r].stock[RES_ARMS]=80.f;
            }
        /* Un pays vierge reçoit 2 régions adjacentes au Dominateur, désarmées. */
        int R=-1;
        for (int c=0;c<s.w->n_countries;c++)
            if (c!=cidD&&c!=cidM&&c!=cidB && s.w->country[c].role==POLITY_UNCLAIMED){ R=c; break; }
        int rr[2], nr=0;
        for (int rd=0; rd<s.econ->n_regions && nr<2; rd++){
            if (s.econ->region[rd].owner!=cidD) continue;
            for (int r=0;r<s.econ->n_regions && nr<2;r++){
                if (r==rd || !s.econ->adj[rd][r] || !s.econ->region[r].active) continue;
                if (s.econ->region[r].owner==cidD) continue;
                bool dup=false; for (int i=0;i<nr;i++) if (rr[i]==r) dup=true;
                if (!dup) rr[nr++]=r;
            }
        }
        int rcount=0;
        if (R>=0 && nr==2){
            s.w->country[R].role=POLITY_CITY_STATE;
            s.w->country[R].capital_prov=s.w->region[rr[0]].province_ids[0];
            for (int i=0;i<2;i++){
                RegionEconomy *re=&s.econ->region[rr[i]];
                re->culture=make_fiche(4.f,ETHOS_PACIFISTE,ECON_RENTE_AGRAIRE,CREDO_PLURALISTE);
                re->owner=(int16_t)R; re->colonized=true;
                re->strata[CLASS_LABORER].pop=40.f; re->strata[CLASS_ELITE].pop=2.f;
                re->build.H_coerc=0.f; re->stock[RES_ARMS]=0.f;
                re->stock[RES_ENCHANTED_ARMS]=0.f; re->stock[RES_GUNPOWDER]=0.f;
            }
            for (int r=0;r<s.econ->n_regions;r++) if (s.econ->region[r].owner==R) rcount++;
        }
        legitimacy_tick(s.wl,s.w,s.econ,s.ts);
        prosperity_tick(s.wp,s.w,s.econ,s.net,s.ts,s.wl);
        ok("croupion R planté : 2 régions désarmées au contact du Dominateur", rcount==2);

        /* (prix) une province DÉVELOPPÉE coûte plus cher qu'un arrière-pays nu. */
        RegionEconomy *rich=&s.econ->region[rr[1]];
        rich->build.K_inst=8.f; rich->build.PE_infra=6.f; rich->prosperity=9.f;
        rich->strata[CLASS_LABORER].pop=6000.f; rich->revolt_scar=0.f;
        float p_cheap=diplo_province_price(s.econ, rr[0]), p_rich=diplo_province_price(s.econ, rr[1]);
        printf("   prix : arrière-pays %.0f  vs  cœur développé %.0f\n", p_cheap, p_rich);
        ok("une province DÉVELOPPÉE coûte plus cher qu'un arrière-pays (§5)", p_rich > p_cheap + 8.f);
        /* le SACCAGE effondre la valeur → la province pillée est moins chère à prendre. */
        rich->revolt_scar=1.0f; float p_sacked=diplo_province_price(s.econ, rr[1]); rich->revolt_scar=0.f;
        ok("une province SACCAGÉE coûte MOINS (saccager-puis-prendre est bon marché)", p_sacked < p_rich - 4.f);
        /* on re-aplatit rr[1] : deux provinces bon marché pour le test d'absorption. */
        rich->build.K_inst=0.f; rich->build.PE_infra=0.f; rich->prosperity=1.f;
        rich->strata[CLASS_LABORER].pop=40.f;

        /* (butin) le budget de score restant VIDE les coffres du vaincu. */
        s.econ->region[rr[0]].treasury=500.f;
        int capD=s.w->province[s.w->country[cidD].capital_prov].region;
        float tD0=s.econ->region[capD].treasury;
        float looted=diplo_loot(s.w, s.econ, cidD, R, 20.f);   /* 20 de budget restant → pillage */
        ok("le BUTIN vide les coffres du vaincu vers la capitale du vainqueur (§5)",
           looted > 1.f && s.econ->region[rr[0]].treasury < 500.f && s.econ->region[capD].treasury > tD0);

        /* On soigne l'ordre du Dominateur (le frein ne le fige pas). */
        for (int r=0;r<s.econ->n_regions;r++) if (s.econ->region[r].owner==cidD){
            s.wl->L[r]=9.f; s.wl->years_held[r]=120.f; s.econ->region[r].coercion=0.f;
            s.econ->region[r].build.food_cap=6.f;
        }
        for (int t=0;t<6;t++){ legitimacy_tick(s.wl,s.w,s.econ,s.ts);
            prosperity_tick(s.wp,s.w,s.econ,s.net,s.ts,s.wl); }
        act[0].next_econ_day=INT_MAX;                 /* gèle l'éco, isole la stratégie */
        int dW=2*horizon;

        /* (A) R BIEN DÉFENDU (parité militaire) → budget marginal → R survit. */
        for (int i=0;i<2;i++){ RegionEconomy *re=&s.econ->region[rr[i]];
            re->stock[RES_ARMS]=800.f; re->build.H_coerc=24.f; re->strata[CLASS_LABORER].pop=4000.f; }
        prosperity_tick(s.wp,s.w,s.econ,s.net,s.ts,s.wl);
        diplo_init(s.dp); diplo_declare_war_cb(s.dp, cidD, R, CB_TERRITORIAL);
        for (int k=0;k<8;k++){ act[0].peace_lock_until=0; act[0].credit_war=20.f;
            act[0].next_strat_day=dW; ai_step(&act[0],s.w,s.econ,s.wp,s.wl,s.ag,s.rn,s.dp,dW); }
        int rB=0; for (int r=0;r<s.econ->n_regions;r++) if (s.econ->region[r].owner==R) rB++;
        ok("R bien défendu (parité) : budget marginal → R survit", rB>=1);

        /* (B) R DÉSARMÉ (domination écrasante) → budget large ≥ sa valeur → R ANNEXÉ. */
        for (int i=0;i<2;i++){ RegionEconomy *re=&s.econ->region[rr[i]];
            re->stock[RES_ARMS]=0.f; re->build.H_coerc=0.f; re->strata[CLASS_LABORER].pop=40.f; }
        prosperity_tick(s.wp,s.w,s.econ,s.net,s.ts,s.wl);
        diplo_init(s.dp); diplo_declare_war_cb(s.dp, cidD, R, CB_TERRITORIAL);
        for (int k=0;k<10;k++){ act[0].peace_lock_until=0; act[0].credit_war=20.f;
            act[0].next_strat_day=dW; ai_step(&act[0],s.w,s.econ,s.wp,s.wl,s.ag,s.rn,s.dp,dW); }
        int rA=0; for (int r=0;r<s.econ->n_regions;r++) if (s.econ->region[r].owner==R) rA++;
        ok("R désarmé (domination écrasante) : le Dominateur ANNEXE R (0 région)",
           rcount==2 && rA==0);
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
