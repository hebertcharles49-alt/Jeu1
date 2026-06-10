/*
 * agency_demo.c — banc d'essai de la couche d'agency (§1-§2)
 *
 *   make agency_demo && ./agency_demo [graine]
 *
 * Prouve le principe « une action est un levier qui déplace une coordonnée » :
 *   - bâtir des institutions (Tribunal/Chancellerie/Académie) monte K → l'ordre
 *     se LIT « plus stable » (consenti) ;
 *   - bâtir des citadelles (Garnison→Citadelle) monte H ET ronge L → l'ordre
 *     bascule « coercitif-fragile » (Stabilité Tenue · Assise Contrainte).
 *
 * Le temps passe en JOURS (l'arc de 250 ans) ; aucune construction n'est
 * instantanée. La lecture se fait par la MEMBRANE (mots), jamais en chiffres
 * (les flottants affichés ici sont pour le développeur, pas le joueur).
 */
#include "scps_world.h"
#include "scps_econ.h"
#include "scps_trade.h"
#include "scps_tech.h"
#include "scps_legitimacy.h"
#include "scps_prosperity.h"
#include "scps_readout.h"
#include "scps_agency.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static int g_pass=0, g_fail=0;
static void ok(const char *what, bool cond){
    printf("   %s %s\n", cond?"✓":"✗", what);
    if (cond) g_pass++; else g_fail++;
}

/* Contexte de simulation. */
typedef struct {
    World *w; WorldEconomy *econ; TradeNetwork *net; TechState *ts;
    WorldProsperity *wp; WorldLegitimacy *wl; AgencyState *ag;
    int player, cap_reg;
} Sim;

/* Un jour : éco → actions → légitimité → prospérité (ordre anti-circularité). */
static void run_days(Sim *s, int days){
    for (int d=0; d<days; d++){
        econ_tick(s->econ, 1.f);
        agency_advance(s->ag, s->w, s->econ, s->wl, NULL, 1);
        legitimacy_tick(s->wl, s->w, s->econ, s->ts);
        prosperity_tick(s->wp, s->w, s->econ, s->net, s->ts, s->wl);
    }
}

static void snapshot(Sim *s, const char *phase, float *out_SI, float *out_frag, float *out_L){
    CountryProsperity *cp=&s->wp->country[s->player];
    CountryReadout r=country_readout(s->wp, s->ts, s->w, s->player);
    printf("  An %-3d %-22s Stabilité=%-12s Assise=%-11s Légitimité=%-10s",
           agency_year(s->ag), phase,
           label_stab(r.stabilite), label_assise(r.assise), label_legit(r.legitimite));
    printf("   [dev SI=%.2f frag=%.2f L=%.2f]\n", cp->SI, cp->fragilite, cp->L);
    if (out_SI)   *out_SI=cp->SI;
    if (out_frag) *out_frag=cp->fragilite;
    if (out_L)    *out_L=cp->L;
}

int main(int argc, char **argv){
    uint32_t seed=(argc>1)?(uint32_t)strtoul(argv[1],NULL,10):42u;

    Sim s={0};
    s.w   =(World*)          malloc(sizeof(World));
    s.econ=(WorldEconomy*)   malloc(sizeof(WorldEconomy));
    s.net =(TradeNetwork*)   malloc(sizeof(TradeNetwork));
    s.ts  =(TechState*)      calloc(SCPS_MAX_COUNTRY,sizeof(TechState));
    s.wp  =(WorldProsperity*)malloc(sizeof(WorldProsperity));
    s.wl  =(WorldLegitimacy*)malloc(sizeof(WorldLegitimacy));
    s.ag  =(AgencyState*)    malloc(sizeof(AgencyState));
    if(!s.w||!s.econ||!s.net||!s.ts||!s.wp||!s.wl||!s.ag){ fprintf(stderr,"OOM\n"); return 1; }

    printf("══════════════════════════════════════════════════════════════\n");
    printf(" AGENCY — bâtir des coordonnées dans le temps (graine %u)\n", seed);
    printf("══════════════════════════════════════════════════════════════\n");

    WorldParams p=worldparams_default(seed);
    world_generate(s.w,&p);
    econ_init(s.econ,s.w);
    gen_population(s.w,s.econ);
    worldgen_seed_peoples(s.w,s.econ,RACE_HUMAIN);
    trade_network_build(s.net,s.w,s.econ);
    for (int c=0;c<s.w->n_countries;c++) tech_state_init(&s.ts[c],false);
    prosperity_init(s.wp,s.w);
    legitimacy_init(s.wl,s.w,s.econ);
    agency_init(s.ag);

    /* Joueur + sa région-capitale (où l'on bâtit). */
    s.player=0;
    for (int c=0;c<s.w->n_countries;c++) if (s.w->country[c].role==POLITY_PLAYER){ s.player=c; break; }
    int cap_prov=s.w->country[s.player].capital_prov;
    s.cap_reg=(cap_prov>=0)?s.w->province[cap_prov].region:0;
    printf("\n  Joueur = pays %d, capitale région %d (« %s »)\n",
           s.player, s.cap_reg, s.w->region[s.cap_reg].name);

    printf("\n── L'arc d'une partie : on bâtit, le temps passe, l'ordre se lit ──\n");
    float SI0,SI1,SI2, F0,F1,F2, L0,L1,L2;

    /* Phase 0 — fondation (rien de bâti). */
    run_days(&s, 2*SCPS_DAYS_PER_YEAR);
    snapshot(&s, "fondation", &SI0,&F0,&L0);

    /* Phase 1 — INSTITUTIONS (→ K) + infrastructure (→ PE, food). */
    agency_order_build(s.ag, s.cap_reg, EDI_TRIBUNAL);
    agency_order_build(s.ag, s.cap_reg, EDI_CHANCELLERIE);
    agency_order_build(s.ag, s.cap_reg, EDI_ACADEMIE);
    agency_order_build(s.ag, s.cap_reg, EDI_MARCHE);    /* → PE_infra (carrefour) */
    agency_order_build(s.ag, s.cap_reg, EDI_GRENIER);   /* → food_cap (apex) */
    run_days(&s, 6*SCPS_DAYS_PER_YEAR);   /* l'Académie met ~5 ans */
    snapshot(&s, "après institutions (K↑)", &SI1,&F1,&L1);

    /* Phase 2 — CITADELLES (→ H, ronge L) : tenir par la force. */
    agency_order_build(s.ag, s.cap_reg, EDI_GARNISON);
    agency_order_build(s.ag, s.cap_reg, EDI_FORTERESSE);
    agency_order_build(s.ag, s.cap_reg, EDI_CITADELLE);
    run_days(&s, 8*SCPS_DAYS_PER_YEAR);   /* la Citadelle met ~6 ans */
    snapshot(&s, "après citadelles (H↑, L↓)", &SI2,&F2,&L2);

    /* Phase 3 — DÉFRICHEMENT (§4) sur une niche forestière + EXPLOITATION (§3).
     * On choisit une niche forestière existante ; s'il n'y en a pas dans le monde
     * généré, on en FABRIQUE une (déterministe) sur une région ≠ capitale → le
     * test mesure l'EFFET du défrichement, pas la flore de la graine. */
    int forest=-1;
    for (int r=0;r<s.econ->n_regions;r++){
        const PopCulture *c=&s.econ->region[r].culture;
        if (c->settled && (c->lifeway==LIFE_HUNTER||c->lifeway==LIFE_HORTICULTURE)){ forest=r; break; }
    }
    if (forest<0)
        for (int r=0;r<s.econ->n_regions;r++)
            if (s.econ->region[r].culture.settled && r!=s.cap_reg){ forest=r; break; }
    if (forest<0) forest=s.cap_reg;
    s.econ->region[forest].culture.lifeway=LIFE_HORTICULTURE;  /* niche forestière franche */
    s.econ->region[forest].culture.subsistance=2.5f;            /* marge nette pour la dérive agricole */
    bool is_forest=true;
    float subs0=s.econ->region[forest].culture.subsistance;
    float food0=s.econ->region[forest].build.food_cap;
    float Lf0=s.wl->L[forest];
    float iron0=s.econ->region[s.cap_reg].raw_cap[RES_IRON];
    agency_order_clear(s.ag, forest);                 /* §4 défrichement */
    agency_order_exploit(s.ag, s.cap_reg, RES_IRON);  /* §3 exploitation */
    run_days(&s, 210);                                /* défrichement 200j, exploit 180j */
    float subs1=s.econ->region[forest].culture.subsistance;
    float food1=s.econ->region[forest].build.food_cap;
    float Lf1=s.wl->L[forest];
    float iron1=s.econ->region[s.cap_reg].raw_cap[RES_IRON];
    printf("  An %-3d défrichement (région %d%s) : subsistance %.1f→%.1f  food_cap +%.1f  L %.1f→%.1f\n",
           agency_year(s.ag), forest, is_forest?" forestière":"", subs0,subs1, food1-food0, Lf0,Lf1);

    /* ---- Contrôles ---------------------------------------------------- */
    printf("\n── Vérification : l'action est un levier ──\n");
    ok("le temps passe en années (≥ 16 ans écoulés)", agency_year(s.ag) >= 16);
    ok("bâtir des institutions monte la stabilité (SI augmente)", SI1 > SI0 + 0.2f);
    ok("bâtir des citadelles ronge la légitimité (L baisse)",     L2  < L1 - 0.1f);
    ok("les citadelles aggravent la fragilité (par la force)",    F2  > F1);
    /* La densité bâtie dans la capitale est bien accumulée. */
    const ProvBuild *b=&s.econ->region[s.cap_reg].build;
    ok("K institutionnel bâti dans la capitale (Tribunal+Chancellerie+Académie)",
       b->K_inst >= 3.5f);
    ok("coercition bâtie dans la capitale (Garnison+Forteresse+Citadelle)",
       b->H_coerc >= 5.5f);
    ok("infrastructure marchande bâtie (PE_infra, Marché)", b->PE_infra >= 1.0f);
    ok("stockage alimentaire bâti (food_cap, Grenier)",     b->food_cap >= 1.0f);
    printf("     capitale : K_inst=%.1f  H_coerc=%.1f  PE_infra=%.1f  food_cap=%.1f\n",
           b->K_inst, b->H_coerc, b->PE_infra, b->food_cap);
    /* §4 défrichement + §3 exploitation */
    ok("défricher monte la nourriture (food_cap)",            food1 > food0);
    ok("défricher dérive la subsistance vers l'agriculture",  subs1 > subs0 + 0.3f);
    if (is_forest)
        ok("défricher en niche forestière ronge L local",     Lf1 < Lf0);
    ok("exploiter monte l'extraction (raw_cap fer)",          iron1 > iron0 + 0.5f);

    /* ═══ §1 — LE COÛT DES BÂTIMENTS : matériaux achetés AU MARCHÉ en or ════ */
    printf("\n── §1. Coût des bâtiments : acheté au marché en or, ∝ tier ──\n");
    {
        RegionEconomy *re=&s.econ->region[s.cap_reg];
        /* Marché de RÉFÉRENCE uniforme (prix=1 partout) : on teste ici que le coût
         * suit le TIER (la recette : Grenier 25+18 unités vs Citadelle 100+30), PAS
         * les oscillations du marché — sinon une conjoncture où le bois flambe et le
         * métal s'effondre inverserait l'ordre des paliers (cf. §1 « coût = recette »). */
        for (int r=0;r<RES_COUNT;r++) re->price[r]=1.0f;
        re->stock[RES_WOOD]=1000.f; re->stock[RES_METAL]=1000.f; re->treasury=100000.f;
        float gold_grenier   = agency_build_gold(s.econ, s.cap_reg, EDI_GRENIER);
        float gold_citadelle = agency_build_gold(s.econ, s.cap_reg, EDI_CITADELLE);
        printf("   Grenier coûte %.0f or (bois) | Citadelle %.0f or (métal+outils, palier supérieur)\n",
               gold_grenier, gold_citadelle);
        ok("un bâtiment a un PRIX en or = Σ recette × prix marché", gold_grenier > 0.f);
        ok("un palier SUPÉRIEUR coûte plus, en matériaux plus avancés (∝ tier)",
           gold_citadelle > gold_grenier);
        float tre0=re->treasury, wood0=re->stock[RES_WOOD];
        bool built = agency_build(s.ag, s.econ, s.cap_reg, EDI_GRENIER);
        ok("bâtir DÉDUIT l'or du trésor (acheté au marché)", built && re->treasury < tre0 - 1.f);
        ok("… et CONSOMME les matériaux du marché (le stock baisse)", re->stock[RES_WOOD] < wood0);
        re->treasury = 0.f;
        int nbefore=s.ag->n;
        bool blocked = agency_build(s.ag, s.econ, s.cap_reg, EDI_CITADELLE);
        ok("sans or pour acheter les matériaux : REFUSÉ, pas de chantier",
           !blocked && s.ag->n==nbefore);
        re->treasury=100000.f;
        ok("le coût vient du TIER (recette), pas de l'étendue (prix inchangé)",
           agency_build_gold(s.econ, s.cap_reg, EDI_GRENIER) == gold_grenier);
    }

    printf("\n══════════════════════════════════════════════════════════════\n");
    printf(" BILAN : %d réussis, %d échoués\n", g_pass, g_fail);
    printf("══════════════════════════════════════════════════════════════\n");
    free(s.w);free(s.econ);free(s.net);free(s.ts);free(s.wp);free(s.wl);free(s.ag);
    return g_fail?1:0;
}
