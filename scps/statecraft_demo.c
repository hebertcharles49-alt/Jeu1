/*
 * statecraft_demo.c — banc d'essai des MÉTRIQUES & du STATECRAFT
 *
 *   make statecraft_demo && ./statecraft_demo [graine]
 *
 * Prouve l'évolution de la membrane (mots → mots + NOMBRES 0-100 à effets) et
 * les trois systèmes ancrés :
 *   1. Métriques 0-100 + effets LUS (prod ×1.15 à 100, agitation −2 à 100) —
 *      jamais un flottant SCPS ni son nom.
 *   2. Influence : trahir la fait chuter, tenir ses accords la monte, elle
 *      plafonne le vivier de diplomates.
 *   3. Diplomates : « Intégrer une province » occupe un agent ∝ D∞ et accélère
 *      la Légitimité ; le vivier est limité (plafond d'Influence).
 *   4. Opinion (−100..100) : une mission la monte, la guerre la crève.
 *   5. Agitation/révolte : une province à agitation SOUTENUE bascule ; la
 *      garnison (H) et la stabilité l'apaisent.
 */
#include "scps_world.h"
#include "scps_econ.h"
#include "scps_trade.h"
#include "scps_tech.h"
#include "scps_legitimacy.h"
#include "scps_prosperity.h"
#include "scps_readout.h"
#include "scps_diplo.h"
#include "scps_routes.h"
#include "scps_statecraft.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_pass=0, g_fail=0;
static void ok(const char *what, bool cond){
    printf("   %s %s\n", cond?"✓":"✗", what);
    if (cond) g_pass++; else g_fail++;
}
static bool near_f(float a, float b, float eps){ float d=a-b; if(d<0)d=-d; return d<=eps; }

typedef struct {
    World *w; WorldEconomy *econ; TradeNetwork *net; TechState *ts;
    WorldProsperity *wp; WorldLegitimacy *wl; DiploState *dp; RouteNetwork *rn;
    Statecraft *sc;
} Sim;

static int cap_region(const World *w, int cid){
    int cp=(cid>=0&&cid<w->n_countries)?w->country[cid].capital_prov:-1;
    return (cp>=0&&cp<w->n_provinces)?w->province[cp].region:-1;
}

int main(int argc, char **argv){
    uint32_t seed=(argc>1)?(uint32_t)strtoul(argv[1],NULL,10):42u;
    Sim s={0};
    s.w=malloc(sizeof(World)); s.econ=malloc(sizeof(WorldEconomy));
    s.net=malloc(sizeof(TradeNetwork)); s.ts=calloc(SCPS_MAX_COUNTRY,sizeof(TechState));
    s.wp=malloc(sizeof(WorldProsperity)); s.wl=malloc(sizeof(WorldLegitimacy));
    s.dp=malloc(sizeof(DiploState)); s.rn=malloc(sizeof(RouteNetwork)); s.sc=malloc(sizeof(Statecraft));
    if(!s.w||!s.econ||!s.net||!s.ts||!s.wp||!s.wl||!s.dp||!s.rn||!s.sc){ fprintf(stderr,"OOM\n"); return 1; }

    printf("══════════════════════════════════════════════════════════════\n");
    printf(" STATECRAFT — métriques 0-100, Influence, Diplomates, Révolte (graine %u)\n", seed);
    printf("══════════════════════════════════════════════════════════════\n");

    WorldParams p=worldparams_default(seed);
    world_generate(s.w,&p);
    econ_init(s.econ,s.w); gen_population(s.w,s.econ); worldgen_seed_peoples(s.w,s.econ,RACE_HUMAIN);
    trade_network_build(s.net,s.w,s.econ);
    for (int c=0;c<s.w->n_countries;c++) tech_state_init(&s.ts[c],false);
    prosperity_init(s.wp,s.w); legitimacy_init(s.wl,s.w,s.econ);
    diplo_init(s.dp); routes_init(s.rn); statecraft_init(s.sc,s.w);
    for (int t=0;t<25;t++){                 /* échauffe la sim (25 ans) */
        econ_tick(s.econ, 1.f); econ_colonize_tick(s.econ,s.w); world_tick(s.w,s.econ,1.f);
        legitimacy_tick(s.wl,s.w,s.econ,s.ts);
        prosperity_tick(s.wp,s.w,s.econ,s.net,s.ts,s.wl);
    }
    int player=0; for (int c=0;c<s.w->n_countries;c++) if (s.w->country[c].role==POLITY_PLAYER){player=c;break;}

    /* ═══ 1. MÉTRIQUES 0-100 + EFFETS LUS ═══════════════════════════════ */
    printf("\n── 1. La membrane sort un NOMBRE et un MOT (jamais un flottant SCPS) ──\n");
    CountryReadout r=country_readout(s.wp,s.ts,s.w,player);
    r.influence=statecraft_influence(s.sc,player);
    printf("   Stabilité %3d — %-12s   Prospérité %3d — %-10s   Légitimité %3d — %s\n",
           r.m_stabilite.value, r.m_stabilite.word, r.m_prosperite.value, r.m_prosperite.word,
           r.m_legitimite.value, r.m_legitimite.word);
    printf("   Cohésion  %3d — %-12s   Savoir     %3d — %-10s   Influence  %3d\n",
           r.m_cohesion.value, r.m_cohesion.word, r.m_savoir.value, r.m_savoir.word, r.influence);
    ok("toutes les métriques sont bornées 0-100",
       r.m_stabilite.value>=0 && r.m_stabilite.value<=100 && r.m_prosperite.value>=0 &&
       r.m_prosperite.value<=100 && r.m_cohesion.value>=0 && r.m_cohesion.value<=100);
    ok("chaque métrique porte un mot (la bande coexiste avec le nombre)",
       r.m_stabilite.word[0] && r.m_prosperite.word[0] && r.m_legitimite.word[0]);

    printf("\n── Les effets sont des COURBES LUES (l'effet existant des coordonnées) ──\n");
    ok("Prospérité 100 → production ×1.15 (l'effet de prospérité, surfacé)",
       near_f(prod_multiplier(100), 1.15f, 0.001f));
    ok("Prospérité 0 → production ×0.85",  near_f(prod_multiplier(0), 0.85f, 0.001f));
    ok("Prospérité 50 → production ×1.00 (neutre)", near_f(prod_multiplier(50), 1.00f, 0.001f));
    ok("Stabilité 100 → agitation −2",     near_f(agitation_modifier(100), -2.0f, 0.001f));
    ok("Stabilité sous le seuil → réforme gâtée", !can_enact_reform(STAB_REFORM_MIN-1) && can_enact_reform(STAB_REFORM_MIN));
    ok("la métrique croît avec la coordonnée (monotone)",
       metric_prosperity(8.f) > metric_prosperity(3.f) && metric_legitimacy(9.f) > metric_legitimacy(2.f));

    /* ═══ 2. INFLUENCE ══════════════════════════════════════════════════ */
    printf("\n── 2. Influence : la réputation suit l'acte ──\n");
    statecraft_init(s.sc,s.w);
    int inf0=statecraft_influence(s.sc,player);
    int cap0=statecraft_missions_cap(s.sc,player);
    for (int k=0;k<4;k++) statecraft_on_accord_kept(s.sc,player);
    int inf_acc=statecraft_influence(s.sc,player);
    int cap_acc=statecraft_missions_cap(s.sc,player);
    statecraft_on_betrayal(s.sc,player);
    int inf_bet=statecraft_influence(s.sc,player);
    printf("   Influence : départ %d → accords tenus %d → trahison %d   (plafond missions %d→%d)\n",
           inf0, inf_acc, inf_bet, cap0, cap_acc);
    ok("tenir ses accords monte l'Influence",  inf_acc > inf0);
    ok("trahir un allié fait chuter l'Influence", inf_bet < inf_acc);
    ok("une Influence plus haute élargit le vivier (plafond de missions)", cap_acc >= cap0);

    /* ═══ 3. DIPLOMATES ═════════════════════════════════════════════════ */
    printf("\n── 3. Diplomates : un vivier limité, des missions en JOURS ──\n");
    statecraft_init(s.sc,s.w);
    const PopCulture *rul=&s.econ->region[cap_region(s.w,player)].culture;
    /* Deux régions test : l'une à culture PROCHE du trône, l'autre LOINTAINE. */
    int rClose=-1, rFar=-1, home=cap_region(s.w,player);
    for (int rr=0;rr<s.econ->n_regions;rr++){
        if (rr==home || !s.econ->region[rr].culture.settled) continue;
        if (rClose<0) rClose=rr; else if (rFar<0){ rFar=rr; break; }
    }
    if (rClose>=0){ s.econ->region[rClose].culture=*rul; }               /* D∞ ≈ 0 */
    if (rFar>=0){ s.econ->region[rFar].culture=*rul;
        s.econ->region[rFar].culture.valeurs = (rul->valeurs<5.f)?rul->valeurs+9.f:rul->valeurs-9.f; }
    bool okClose=statecraft_send(s.sc,s.w,s.econ,player,DIP_INTEGRATE,rClose);
    bool okFar  =statecraft_send(s.sc,s.w,s.econ,player,DIP_INTEGRATE,rFar);
    int dClose=s.sc->staff[player].agents[0].days_left;
    int dFar  =s.sc->staff[player].agents[1].days_left;
    printf("   « Intégrer » : province proche %d j, province lointaine %d j (∝ D∞)\n", dClose, dFar);
    ok("les deux missions occupent un agent chacune", okClose && okFar && statecraft_missions_active(s.sc,player)==2);
    ok("intégrer du LOINTAIN prend bien plus de jours (∝ D∞)", dFar > dClose + 300);

    /* Vivier limité : on remplit jusqu'au plafond, le suivant échoue. */
    int cap=statecraft_missions_cap(s.sc,player);
    int sent=2, fails=0;
    for (int k=0;k<cap+3;k++){
        int tgt=(player+1)%s.w->n_countries;
        if (statecraft_send(s.sc,s.w,s.econ,player,DIP_RELATIONS,tgt)) sent++; else fails++;
    }
    printf("   Vivier : plafond %d, missions actives %d, refus %d\n",
           cap, statecraft_missions_active(s.sc,player), fails);
    ok("le vivier sature au plafond (les missions en trop sont refusées)",
       statecraft_missions_active(s.sc,player)==cap && fails>0);

    /* L'intégration proche aboutit et fait BONDIR la Légitimité de la région. */
    statecraft_init(s.sc,s.w);
    float yh_before=s.wl->years_held[rClose], L_before=s.wl->L[rClose];
    statecraft_send(s.sc,s.w,s.econ,player,DIP_INTEGRATE,rClose);
    for (int d=0; d<800 && s.sc->staff[player].agents[0].mission!=DIP_IDLE; d+=10)
        statecraft_tick(s.sc,s.w,s.econ,s.wp,s.wl,s.dp,s.rn,10);
    printf("   Intégration achevée : région %d, ancienneté %.0f→%.0f, L %.1f→%.1f\n",
           rClose, yh_before, s.wl->years_held[rClose], L_before, s.wl->L[rClose]);
    ok("la mission d'intégration accélère la Légitimité (ancienneté + L montent)",
       s.wl->years_held[rClose] > yh_before && s.wl->L[rClose] >= L_before);

    /* Une route s'ouvre après ~90 jours d'occupation d'un agent. */
    statecraft_init(s.sc,s.w);
    int partner=-1;
    for (int rr=0;rr<s.econ->n_regions;rr++)
        if (rr!=home && s.econ->region[rr].culture.settled && s.econ->region[rr].owner!=player){ partner=rr; break; }
    if (partner<0) partner=rFar;
    int routes_before=routes_count_for_region(s.rn,home);
    statecraft_send(s.sc,s.w,s.econ,player,DIP_ROUTE,partner);
    /* L'agent ouvre la route (90 j) ; le module routes la fait MÛRIR (90 j de
     * plus) — les deux systèmes avancent ensemble, comme dans la boucle réelle. */
    for (int d=0; d<220; d+=10){
        statecraft_tick(s.sc,s.w,s.econ,s.wp,s.wl,s.dp,s.rn,10);
        routes_advance(s.rn,s.w,s.econ,10);
    }
    ok("« Établir une route » occupe un agent puis ouvre une route",
       routes_count_for_region(s.rn,home) > routes_before);

    /* ═══ 4. OPINION (−100..100) ════════════════════════════════════════ */
    printf("\n── 4. Opinion : une mission la monte, la guerre la crève ──\n");
    statecraft_init(s.sc,s.w);
    int other=-1;
    for (int c=0;c<s.w->n_countries;c++) if (c!=player && s.w->country[c].role!=POLITY_UNCLAIMED){ other=c; break; }
    if (other<0) other=(player+1)%s.w->n_countries;
    int op0=statecraft_opinion(s.sc,player,other);
    statecraft_send(s.sc,s.w,s.econ,player,DIP_RELATIONS,other);
    for (int d=0; d<200; d+=10) statecraft_tick(s.sc,s.w,s.econ,s.wp,s.wl,s.dp,s.rn,10);
    int op_rel=statecraft_opinion(s.sc,player,other);
    diplo_declare_war(s.dp,player,other);
    for (int d=0; d<400; d+=10) statecraft_tick(s.sc,s.w,s.econ,s.wp,s.wl,s.dp,s.rn,10);
    int op_war=statecraft_opinion(s.sc,player,other);
    printf("   Opinion envers le voisin : départ %d → après mission %d → après guerre %d\n",
           op0, op_rel, op_war);
    ok("améliorer les relations monte l'opinion", op_rel > op0);
    ok("la guerre crève l'opinion", op_war < op_rel);

    /* ═══ 5. AGITATION / RÉVOLTE ════════════════════════════════════════ */
    printf("\n── 5. Agitation soutenue → révolte ; H + Stabilité l'apaisent ──\n");
    /* Unité : la garnison et la stabilité abattent l'agitation. */
    int agit_nu  = metric_agitation(/*L*/1.f,/*coerc*/0.6f,/*div*/8.f,/*shock*/1.f,/*stab*/40,/*H*/0.f);
    int agit_gar = metric_agitation(1.f, 0.6f, 8.f, 1.f, /*stab*/90, /*H*/8.f);
    printf("   Province L basse, lignée étrangère : nue=%d  garnisonnée+stable=%d\n", agit_nu, agit_gar);
    ok("sans garnison ni stabilité, l'agitation franchit le seuil de révolte", agit_nu >= AGIT_REVOLT_SEUIL);
    ok("garnison (H) + stabilité ramènent l'agitation sous le seuil", agit_gar < AGIT_REVOLT_SEUIL);

    /* Intégration : une province à L basse, étrangère, sans garnison BASCULE. */
    statecraft_init(s.sc,s.w);
    int rRevolt=rFar>=0?rFar:rClose;
    s.econ->region[rRevolt].owner=(int16_t)player; s.econ->region[rRevolt].culture.settled=true;
    s.econ->region[rRevolt].culture.valeurs=(rul->valeurs<5.f)?rul->valeurs+9.f:rul->valeurs-9.f;
    s.econ->region[rRevolt].build.H_coerc=0.f; s.econ->region[rRevolt].coercion=1.0f;
    /* Conquête FRAÎCHE, zéro légitimité (déterministe) : la frondeuse franchit
     * le seuil quelle que soit la géographie — on teste la RÉVOLTE, pas le monde. */
    if (rRevolt<SCPS_MAX_REG){ s.wl->L[rRevolt]=0.0f; s.wl->years_held[rRevolt]=0.f; }
    bool fired=false; int day_fired=-1;
    for (int d=0; d<800 && !fired; d+=10){
        statecraft_tick(s.sc,s.w,s.econ,s.wp,s.wl,s.dp,s.rn,10);
        if (statecraft_revolt_fired(s.sc,rRevolt)){ fired=true; day_fired=d; }
    }
    printf("   Province frondeuse (L≈1, étrangère, sans garnison) : agitation %d → révolte au jour %d\n",
           statecraft_agitation(s.sc,rRevolt), day_fired);
    ok("une agitation soutenue au-dessus du seuil finit en RÉVOLTE", fired);

    /* Une province garnisonnée et légitime ne se révolte jamais. */
    statecraft_init(s.sc,s.w);
    int rCalm=rClose;
    s.econ->region[rCalm].owner=(int16_t)player; s.econ->region[rCalm].culture=*rul;
    s.econ->region[rCalm].culture.settled=true;
    s.econ->region[rCalm].build.H_coerc=6.f; s.econ->region[rCalm].coercion=0.f;
    if (rCalm<SCPS_MAX_REG){ s.wl->L[rCalm]=8.f; s.wl->years_held[rCalm]=60.f; }
    bool calm_fired=false;
    for (int d=0; d<600; d+=10){
        statecraft_tick(s.sc,s.w,s.econ,s.wp,s.wl,s.dp,s.rn,10);
        if (statecraft_revolt_fired(s.sc,rCalm)) calm_fired=true;
    }
    printf("   Province loyale (L=8, du même sang, garnisonnée) : agitation %d → révolte=%s\n",
           statecraft_agitation(s.sc,rCalm), calm_fired?"OUI":"non");
    ok("une province légitime et garnisonnée ne se révolte pas", !calm_fired);

    printf("\n══════════════════════════════════════════════════════════════\n");
    printf(" BILAN : %d réussis, %d échoués\n", g_pass, g_fail);
    printf("══════════════════════════════════════════════════════════════\n");
    free(s.w);free(s.econ);free(s.net);free(s.ts);free(s.wp);free(s.wl);free(s.dp);free(s.rn);free(s.sc);
    return g_fail?1:0;
}
