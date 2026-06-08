/*
 * structural_demo.c — Lumières, Soulèvements, l'Ordre de Fer
 *
 *   make structural_demo && ./structural_demo [graine]
 *
 * Thèse : rien n'arrive pour rien. Ces âges N'AJOUTENT aucun mécanisme — ils
 * poussent les ENTRÉES GLOBALES du moteur d'ordre vérifié (I/L/H) ; le verdict
 * §2.4 (révolution/sécession/coercitif-fragile, fragilité) fait les conséquences,
 * pays par pays. On vérifie :
 *   1. Causalité : Soulèvements & Ordre de Fer ne peuvent précéder les Lumières.
 *   2. Lumières, double tranchant : le savoir s'ouvre (palier) ; une société
 *      OUVERTE subit la pression de réforme (SI baisse) ; un régime COERCITIF
 *      voit sa légitimité se dissoudre → sa fragilité monte (amorcé pour la rupture).
 *   3. Contagion : les Soulèvements baissent L partout → plus de pays franchissent
 *      le seuil de RÉVOLUTION du moteur — sans code de révolution dédié.
 *   4. Le fork : sous la crise, l'IA Bureaucrate RÉFORME (bâtit K), l'IA
 *      Dominatrice SERRE (bâtit H — le chemin de l'Ordre de Fer).
 *   5. Le géant cassant : un État de l'Ordre de Fer lit Stabilité HAUTE mais
 *      Assise TYRANNIQUE (fragilité maxée) — et craque au premier choc majeur.
 *   6. Aucun terme réel ni variable SCPS à l'écran.
 */
#include "scps_world.h"
#include "scps_econ.h"
#include "scps_trade.h"
#include "scps_tech.h"
#include "scps_legitimacy.h"
#include "scps_prosperity.h"
#include "scps_readout.h"
#include "scps_statecraft.h"
#include "scps_routes.h"
#include "scps_diplo.h"
#include "scps_agency.h"
#include "scps_ai.h"
#include "scps_events.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

static int g_pass=0, g_fail=0;
static void ok(const char *what, bool cond){
    printf("   %s %s\n", cond?"✓":"✗", what);
    if (cond) g_pass++; else g_fail++;
}

typedef struct {
    World *w; WorldEconomy *econ; TradeNetwork *net; TechState *ts;
    WorldProsperity *wp; WorldLegitimacy *wl; Statecraft *sc; RouteNetwork *rn;
    DiploState *dp; EventsState *ev;
} Sim;

static int cap_region(const World *w, int cid){
    int cp=(cid>=0&&cid<w->n_countries)?w->country[cid].capital_prov:-1;
    return (cp>=0&&cp<w->n_provinces)?w->province[cp].region:-1;
}
static void tickP(Sim *s){ prosperity_tick(s->wp,s->w,s->econ,s->net,s->ts,s->wl); }

/* Laisse couler les générations (palier d'âge 30 ans) jusqu'à ce qu'un âge cible
 * advienne — la chaîne s'égrène un âge à la fois, on ne la teste pas en grappe. */
static void chain_to(Sim *s, AgeId target){
    for (int g=0; g<14 && !ages_dawned(s->ev,target); g++){
        s->ev->ages.days_elapsed += 31*365;
        events_check_ages(s->ev,s->w,s->econ,s->wp,s->wl,s->ts);
    }
}

/* Façonne les ENTRÉES d'ordre d'un pays (H/K/L/C/race) pour isoler un cas. */
static void shape(Sim *s, int cid, float techH, float techK, float Lval, float Cval){
    if (cid<0||cid>=s->w->n_countries) return;
    s->ts[cid].H=techH; s->ts[cid].K=techK; s->ts[cid].L=Lval;
    for (int r=0;r<s->econ->n_regions;r++) if (s->econ->region[r].owner==cid){
        if (r<SCPS_MAX_REG){ s->wl->L[r]=Lval; s->wl->years_held[r]=60.f; }
        s->econ->region[r].build.H_coerc=0.f; s->econ->region[r].build.K_inst=0.f;
        s->econ->region[r].coercion=0.f; s->econ->region[r].build.food_cap=3.f;
        s->econ->region[r].culture.race=RACE_HUMAIN;   /* race neutre : H = tech seul */
    }
    if (cid<s->wp->n_countries) s->wp->country[cid].C=Cval;
}
/* Met de force la Lumière mondiale (savoir) + C au-dessus des seuils des Lumières. */
static void light_the_world(Sim *s){
    for (int c=0;c<s->w->n_countries && c<s->wp->n_countries;c++)
        if (s->w->country[c].role!=POLITY_UNCLAIMED){ s->wp->country[c].Lumiere=12.f; s->wp->country[c].C=6.f; }
}
/* Donne à une région un propriétaire + une culture marquée (pour fabriquer de
 * la diversité interne réelle que le mythe suppkillera, puis libèrera au choc). */
static void assign_region(Sim *s, int r, int cid, float axis){
    if (r<0||r>=s->econ->n_regions) return;
    RegionEconomy *re=&s->econ->region[r];
    re->owner=(int16_t)cid; re->colonized=true; re->culture.settled=true;
    re->culture.valeurs=axis; re->culture.religion=axis; re->culture.subsistance=axis;
}

int main(int argc, char **argv){
    uint32_t seed=(argc>1)?(uint32_t)strtoul(argv[1],NULL,10):42u;
    Sim s={0};
    s.w=malloc(sizeof(World)); s.econ=malloc(sizeof(WorldEconomy)); s.net=malloc(sizeof(TradeNetwork));
    s.ts=calloc(SCPS_MAX_COUNTRY,sizeof(TechState)); s.wp=malloc(sizeof(WorldProsperity));
    s.wl=malloc(sizeof(WorldLegitimacy)); s.sc=malloc(sizeof(Statecraft)); s.rn=malloc(sizeof(RouteNetwork));
    s.dp=malloc(sizeof(DiploState)); s.ev=malloc(sizeof(EventsState));
    if(!s.w||!s.econ||!s.net||!s.ts||!s.wp||!s.wl||!s.sc||!s.rn||!s.dp||!s.ev){ fprintf(stderr,"OOM\n"); return 1; }

    printf("══════════════════════════════════════════════════════════════\n");
    printf(" ÂGES STRUCTURELS — Lumières, Soulèvements, l'Ordre de Fer (graine %u)\n", seed);
    printf("══════════════════════════════════════════════════════════════\n");

    WorldParams p=worldparams_default(seed);
    world_generate(s.w,&p);
    econ_init(s.econ,s.w); gen_population(s.w,s.econ); worldgen_seed_peoples(s.w,s.econ,RACE_HUMAIN);
    trade_network_build(s.net,s.w,s.econ);
    for (int c=0;c<s.w->n_countries;c++) tech_state_init(&s.ts[c],false);
    prosperity_init(s.wp,s.w); legitimacy_init(s.wl,s.w,s.econ);
    statecraft_init(s.sc,s.w); routes_init(s.rn); diplo_init(s.dp);
    for (int t=0;t<15;t++){ econ_tick(s.econ, 1.f); econ_colonize_tick(s.econ,s.w); world_tick(s.w,s.econ,1.f);
        legitimacy_tick(s.wl,s.w,s.econ,s.ts); tickP(&s); }
    events_init(s.ev,s.w,seed);

    int polities[SCPS_MAX_COUNTRY], npol=0;
    for (int c=0;c<s.w->n_countries;c++) if (s.w->country[c].role!=POLITY_UNCLAIMED && cap_region(s.w,c)>=0) polities[npol++]=c;
    if (npol<4){ fprintf(stderr,"monde trop vide (%d pays)\n",npol); return 1; }

    /* ═══ 1. CAUSALITÉ — pas de Soulèvement/Ordre de Fer avant les Lumières ═ */
    printf("\n── 1. Causalité : la société de masse (Lumières) d'abord ──\n");
    events_init(s.ev,s.w,seed);
    /* Monde en pleine crise (révolutions + fracture) MAIS sans savoir : les âges
     * politiques NE DOIVENT PAS s'éveiller (les Lumières n'ont pas eu lieu). */
    for (int c=0;c<s.w->n_countries && c<s.wp->n_countries;c++) if (s.w->country[c].role!=POLITY_UNCLAIMED){
        s.wp->country[c].mode=2; /* révolution */ s.wp->country[c].fracture=5.f;
        s.wp->country[c].dereal=2.f; s.wp->country[c].SI=3.f; s.wp->country[c].Lumiere=0.f; s.wp->country[c].C=0.f;
    }
    s.ev->ages.days_elapsed += 31*365; events_check_ages(s.ev,s.w,s.econ,s.wp,s.wl,s.ts);
    ok("sans les Lumières, l'Âge des Soulèvements ne peut PAS s'éveiller", !ages_dawned(s.ev,AGE_SOULEVEMENTS));
    ok("sans les Lumières, l'Âge de l'Ordre de Fer ne peut PAS s'éveiller", !ages_dawned(s.ev,AGE_ORDRE_FER));
    /* Maintenant la société accumule savoir + connexion → les Lumières adviennent,
     * et alors seulement la chaîne peut se dérouler. */
    light_the_world(&s);
    chain_to(&s, AGE_SOULEVEMENTS);   /* on laisse le temps : la chaîne s'égrène */
    ok("les Lumières s'éveillent (savoir mondial + connexion atteints)", ages_dawned(s.ev,AGE_LUMIERES));
    ok("APRÈS les Lumières, la chaîne causale peut se dérouler (Soulèvements éveillé)",
       ages_dawned(s.ev,AGE_SOULEVEMENTS));

    /* ═══ 2. LUMIÈRES — le solvant double (via le moteur) ═══════════════ */
    printf("\n── 2. Lumières : la société ouverte réforme, le régime coercitif s'amorce ──\n");
    /* Repart d'un monde propre, deux cas façonnés. */
    for (int c=0;c<s.w->n_countries && c<s.wp->n_countries;c++){ s.wp->country[c].mode=0; s.wp->country[c].fracture=0; s.wp->country[c].dereal=0; }
    s.wp->age_I_bonus=0; s.wp->age_lumiere_solvent=0; s.wp->age_L_penalty=0; s.wp->age_H_bonus=0; s.wp->age_myth_homogen=0;
    events_init(s.ev,s.w,seed);
    int cOpen=polities[0], cCoer=polities[1];
    shape(&s, cOpen, /*H*/1.f, /*K*/7.f, /*L*/8.f, /*C*/6.f);   /* société ouverte, consentie */
    shape(&s, cCoer, /*H*/9.f, /*K*/4.f, /*L*/2.f, /*C*/6.f);   /* régime coercitif, fragile  */
    tickP(&s);
    float SIo0=s.wp->country[cOpen].SI, FRc0=s.wp->country[cCoer].fragilite;
    /* Avènement des Lumières (savoir+C atteints) → +I et dissolution coercitive. */
    light_the_world(&s);
    shape(&s, cOpen, 1.f,7.f,8.f,6.f); shape(&s, cCoer, 9.f,4.f,2.f,6.f);  /* re-fige les cas */
    chain_to(&s, AGE_LUMIERES);        /* Commerce→Raison→Lumières, génération par génération */
    shape(&s, cOpen, 1.f,7.f,8.f,6.f); shape(&s, cCoer, 9.f,4.f,2.f,6.f);  /* re-fige après la chaîne */
    tickP(&s);
    float SIo1=s.wp->country[cOpen].SI, FRc1=s.wp->country[cCoer].fragilite;
    printf("   société OUVERTE : SI %.1f→%.1f (pression de réforme) ; régime COERCITIF : fragilité %.1f→%.1f (amorcé)\n",
           SIo0,SIo1, FRc0,FRc1);
    ok("le boon : le palier du savoir s'ouvre (Société/4)", ages_tier_open(s.ev,THM_SOCIETE,4));
    ok("la société OUVERTE subit la pression de réforme (SI baisse)", SIo1 < SIo0 - 0.1f);
    ok("le régime COERCITIF voit sa légitimité se dissoudre → fragilité monte", FRc1 > FRc0 + 0.1f);

    /* ═══ 3. SOULÈVEMENTS — la contagion (baisser L partout) ════════════ */
    printf("\n── 3. Soulèvements : une chute, toutes les couronnes questionnées ──\n");
    for (int c=0;c<s.w->n_countries && c<s.wp->n_countries;c++){ s.wp->country[c].mode=0; }
    s.wp->age_I_bonus=0; s.wp->age_lumiere_solvent=0; s.wp->age_L_penalty=0; s.wp->age_H_bonus=0; s.wp->age_myth_homogen=0;
    events_init(s.ev,s.w,seed); s.ev->ages.dawned[AGE_LUMIERES]=true;   /* la société de masse existe */
    /* Deux régimes DÉJÀ tombés (la masse critique) + plusieurs CONSENTIS mais
     * juste au-dessus du seuil, que la chute de L mondiale fera basculer. On
     * rassasie les peuples (satisfaction haute → charge fiscale basse) pour
     * ISOLER l'effet de la légitimité : seul L bouge. */
    for (int i=0;i<npol;i++){
        int c=polities[i];
        bool deep=(i<2);
        shape(&s,c, /*H*/1.f, /*K*/ deep?3.f:5.f, /*L*/ deep?1.0f:6.0f, /*C*/6.f);
        for (int r=0;r<s.econ->n_regions;r++) if (s.econ->region[r].owner==c)
            s.econ->region[r].satisfaction=0.85f;
    }
    s.wp->age_I_bonus=1.5f;   /* l'effervescence des idées (Lumières en cours) */
    tickP(&s);
    int rev0=events_count_revolutionary(s.w,s.wp);
    chain_to(&s, AGE_SOULEVEMENTS);   /* Lumières déjà là → la masse critique éveille les Soulèvements */
    bool soulev = ages_dawned(s.ev,AGE_SOULEVEMENTS);
    tickP(&s);
    int rev1=events_count_revolutionary(s.w,s.wp);
    printf("   pays en révolution : %d → %d  (Soulèvements éveillé=%d, L mondial −%.1f)\n",
           rev0, rev1, soulev, s.wp->age_L_penalty);
    ok("la masse critique de révolutions éveille les Soulèvements", soulev);
    ok("la contagion (L ↓ partout) fait franchir le seuil à PLUS de pays — via le seul moteur",
       rev1 > rev0);

    /* ═══ 4. LE FORK — réformer (K) ou serrer (H), selon la fiche ════════ */
    printf("\n── 4. Le fork sous la crise : le Bureaucrate réforme, le Dominateur serre ──\n");
    {
        int cidB=polities[0], cidD=polities[1];
        int rB=cap_region(s.w,cidB), rD=cap_region(s.w,cidD);
        /* Deux capitales mises en CRISE (SI bas → frein dur) ; fiches opposées. */
        PopCulture fb; memset(&fb,0,sizeof fb);
        fb.langue=5;fb.valeurs=4.5f;fb.subsistance=6;fb.parente=5;fb.religion=5;fb.ethos=ETHOS_BUREAUCRATE;
        fb.lifeway=LIFE_FARMER;fb.structure=STRUCT_LIGNAGER;fb.credo=CREDO_PLURALISTE;fb.rel_branch=REL_ABRAHAMIQUE;
        fb.econ=ECON_RENTE_AGRAIRE;fb.martial=MART_MUR_BOUCLIERS;fb.race=RACE_HUMAIN;fb.settled=true;fb.age=200;
        PopCulture fd=fb; fd.valeurs=9.f; fd.ethos=ETHOS_DOMINATEUR;
        if (rB>=0){ s.econ->region[rB].culture=fb; s.econ->region[rB].owner=(int16_t)cidB; }
        if (rD>=0){ s.econ->region[rD].culture=fd; s.econ->region[rD].owner=(int16_t)cidD; }
        shape(&s,cidB, 5.f,3.f,1.0f,3.f); shape(&s,cidD, 5.f,3.f,1.0f,3.f);  /* crise : L très basse */
        if (rB>=0) s.econ->region[rB].culture=fb;   /* shape a écrasé la race → on repose la fiche */
        if (rD>=0) s.econ->region[rD].culture=fd;
        s.wp->age_I_bonus=2.0f;                       /* la pression des idées (crise ouverte) */
        tickP(&s);
        /* La crise est la PRÉCONDITION du fork : on la force directement (SI bas,
         * fragilité haute) sur les deux États, indépendamment de la géographie —
         * le test mesure alors la RÉPONSE (serrer vs réformer), pas le hasard du
         * monde. L'IA lit ces valeurs telles quelles (ai_observe, sans re-tick). */
        if (cidB<s.wp->n_countries){ s.wp->country[cidB].SI=2.0f; s.wp->country[cidB].fragilite=7.0f; }
        if (cidD<s.wp->n_countries){ s.wp->country[cidD].SI=2.0f; s.wp->country[cidD].fragilite=7.0f; }
        AgencyState ag; agency_init(&ag);
        AiActor aB, aD;
        ai_actor_init(&aB, s.w, s.econ, cidB, seed^0xBu);
        ai_actor_init(&aD, s.w, s.econ, cidD, seed^0xDu);
        AiView vB=ai_observe(s.wp,s.w,s.econ,cidB), vD=ai_observe(s.wp,s.w,s.econ,cidD);
        printf("   Bureaucrate : w_expand=%.2f SI=%.1f (crise) | Dominateur : w_expand=%.2f SI=%.1f (crise)\n",
               aB.w_expand, vB.SI, aD.w_expand, vD.SI);
        for (int k=0;k<8;k++){
            int day=k*600;
            aB.next_econ_day=day; aB.next_strat_day=INT_MAX; ai_step(&aB,s.w,s.econ,s.wp,s.wl,&ag,s.rn,s.dp,day);
            aD.next_econ_day=day; aD.next_strat_day=INT_MAX; ai_step(&aD,s.w,s.econ,s.wp,s.wl,&ag,s.rn,s.dp,day);
        }
        printf("   bâtiments sous la crise — Bureaucrate : K=%d H=%d | Dominateur : K=%d H=%d\n",
               aB.stats.builds_k+aB.stats.builds_other, aB.stats.builds_h,
               aD.stats.builds_k+aD.stats.builds_other, aD.stats.builds_h);
        ok("le Dominateur SERRE : il bâtit du H (le chemin de l'Ordre de Fer)", aD.stats.builds_h>0);
        ok("le Bureaucrate RÉFORME : il bâtit du K, jamais la citadelle",
           (aB.stats.builds_k+aB.stats.builds_other)>0 && aB.stats.builds_h==0);
    }

    /* ═══ 5. LE GÉANT CASSANT — Stabilité haute, Assise tyrannique ═══════ */
    printf("\n── 5. L'Ordre de Fer : un géant qui PARAÎT stable et craque au premier choc ──\n");
    {
        for (int c=0;c<s.w->n_countries && c<s.wp->n_countries;c++){ s.wp->country[c].mode=0; }
        s.wp->age_I_bonus=0; s.wp->age_lumiere_solvent=0; s.wp->age_L_penalty=0; s.wp->age_breach_flux=0;
        int cIron=polities[0], cResil=polities[1];
        /* On donne aux DEUX une diversité interne RÉELLE (régions aux antipodes) :
         * c'est elle que le mythe de l'Ordre de Fer suppkille — et que le choc
         * libèrera. Le géant la nie (D̄ effectif ↓) ; le métaboliseur la digère (K). */
        int regs[4], ng=0;
        for (int r=0;r<s.econ->n_regions && ng<4;r++) if (s.econ->region[r].culture.settled) regs[ng++]=r;
        if (ng>=4){
            assign_region(&s,regs[0],cIron, 2.f);  assign_region(&s,regs[1],cIron, 8.f);
            assign_region(&s,regs[2],cResil,2.f);  assign_region(&s,regs[3],cResil,8.f);
        }
        shape(&s,cIron, /*H*/9.f, /*K*/2.f, /*L*/0.0f, /*C*/4.f);   /* l'État fort : la poigne, zéro consentement vrai */
        shape(&s,cResil,/*H*/1.f, /*K*/9.f, /*L*/8.0f, /*C*/4.f);   /* le métaboliseur : K haut, légitimité vraie */
        /* L'Ordre de Fer advenu : la poigne + le mythe qui nie la diversité. */
        s.wp->age_H_bonus=2.5f; s.wp->age_myth_homogen=4.0f;
        tickP(&s);
        CountryReadout r=country_readout(s.wp,s.ts,s.w,cIron);
        printf("   géant de Fer : Stabilité %d — %s | Assise %s   [dev SI=%.1f frag=%.1f]\n",
               r.m_stabilite.value, r.m_stabilite.word, label_assise(r.assise),
               s.wp->country[cIron].SI, s.wp->country[cIron].fragilite);
        ok("le géant LIT une Stabilité HAUTE (l'ordre semble rétabli)", r.m_stabilite.value >= 70);
        ok("… mais l'Assise le TRAHIT : Tyrannique (fragilité maxée)", r.assise==AS_TYRANNIQUE);
        float SIiron_before=s.wp->country[cIron].SI, SIresil_before=s.wp->country[cResil].SI;
        /* LE CHOC MAJEUR : guerre totale faustienne — le mythe se brise (D̄ revient). */
        s.wp->age_breach_flux=4.0f;     /* flux faustien mondial (guerre totale → la Brèche) */
        s.wp->age_myth_homogen=0.0f;    /* le mythe unificateur se déchire */
        tickP(&s);
        float SIiron_after=s.wp->country[cIron].SI, SIresil_after=s.wp->country[cResil].SI;
        printf("   au choc (guerre totale) — géant de Fer : SI %.1f→%.1f (craque) | métaboliseur : SI %.1f→%.1f (tient)\n",
               SIiron_before,SIiron_after, SIresil_before,SIresil_after);
        ok("le géant cassant CRAQUE au premier choc majeur (effondrement de l'ordre)",
           SIiron_after < 5.0f && SIiron_after < SIiron_before - 1.0f);
        ok("le métaboliseur (K haut, L vraie) ENCAISSE le même choc", SIresil_after >= 5.0f);
    }

    /* ═══ 6. MEMBRANE — aucun terme réel ni variable SCPS ═══════════════ */
    printf("\n── 6. Membrane : des noms diégétiques, jamais un terme réel ni SCPS ──\n");
    printf("   âges : « %s », « %s », « %s »\n",
           age_name(AGE_LUMIERES), age_name(AGE_SOULEVEMENTS), age_name(AGE_ORDRE_FER));
    ok("noms d'âges diégétiques, aucun nom SCPS dans les textes", events_text_clean());

    printf("\n══════════════════════════════════════════════════════════════\n");
    printf(" BILAN : %d réussis, %d échoués\n", g_pass, g_fail);
    printf("══════════════════════════════════════════════════════════════\n");
    free(s.w);free(s.econ);free(s.net);free(s.ts);free(s.wp);free(s.wl);free(s.sc);free(s.rn);free(s.dp);free(s.ev);
    return g_fail?1:0;
}
