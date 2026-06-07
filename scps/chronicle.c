/*
 * chronicle.c — le moteur VIVANT, sans écran : N mondes × M années
 *
 *   make chronicle && ./chronicle [graine_base] [n_mondes=10] [années=200]
 *
 * Fait tourner exactement la boucle du jeu (sim_day du viewer, mais headless) et
 * tient la CHRONIQUE de ce qui émerge : âges qui s'éveillent, guerres, révoltes,
 * empires qui croissent, pays absorbés. Aucune entrée joueur — on regarde le
 * monde vivre par les seuls acteurs IA et le moteur d'ordre.
 */
#include "scps_world.h"
#include "scps_econ.h"
#include "scps_trade.h"
#include "scps_tech.h"
#include "scps_legitimacy.h"
#include "scps_prosperity.h"
#include "scps_readout.h"
#include "scps_statecraft.h"
#include "scps_agency.h"
#include "scps_routes.h"
#include "scps_diplo.h"
#include "scps_events.h"
#include "scps_modifier.h"
#include "scps_demography.h"
#include "scps_labor.h"
#include "scps_ai.h"
#include "scps_species.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── État de simulation (copié de viewer.c, sans SDL) ───────────────────── */
typedef struct {
    WorldEconomy *econ; WorldProsperity *wp; WorldLegitimacy *wl; TradeNetwork *net;
    TechState *ts; Statecraft *sc; AgencyState *ag; EventsState *ev; ModifierStack *drift;
    LaborEcon *labor; DiploState *dp; RouteNetwork *rn; AiActor *ai; bool *ai_on;
    int day, year, player;
} Sim;

static void sim_day(Sim *s, World *w) {
    agency_advance(s->ag, w, s->econ, s->wl, 1);
    routes_advance(s->rn, w, s->econ, 1);
    for (int c=0;c<w->n_countries;c++) if (s->ai_on[c])
        ai_step(&s->ai[c], w, s->econ, s->wp, s->wl, s->ag, s->rn, s->dp, s->day);
    world_events_tick(s->ev, w, s->econ, s->wl, s->wp, s->sc, s->rn, s->ts, 1);
    labor_tick(s->labor);
    /* — mensuel : économie + réputation diplomatique (O(n²)) + démographie — */
    if (s->day % 30 == 29) {
        econ_tick(s->econ, 1.f/12.f);
        statecraft_tick(s->sc, w, s->econ, s->wp, s->wl, s->dp, s->rn, 30);
        demography_tick(w, s->econ, s->wl, s->drift, 5.f, 5.f, 1.f/12.f);
    }
    if (s->day % 365 == 364) {
        econ_colonize_tick(s->econ, w); econ_migrate_tick(s->econ, w);
        world_tick(w, s->econ, 1.0f);
        legitimacy_tick(s->wl, w, s->econ, s->ts);
        trade_network_build(s->net, w, s->econ); trade_tick(s->econ, s->net);
        prosperity_tick(s->wp, w, s->econ, s->net, s->ts, s->wl);
    }
    if (++s->day % 365 == 0) s->year++;
}

static void sim_init(Sim *s, World *w) {
    econ_init(s->econ, w); gen_population(w, s->econ);
    worldgen_seed_peoples(w, s->econ, RACE_HUMAIN);
    legitimacy_init(s->wl, w, s->econ); prosperity_init(s->wp, w);
    trade_network_build(s->net, w, s->econ);
    statecraft_init(s->sc, w); agency_init(s->ag); diplo_init(s->dp); routes_init(s->rn);
    for (int c=0;c<w->n_countries;c++) tech_state_init(&s->ts[c], false);
    s->player = 0;
    for (int c=0;c<w->n_countries;c++) if (w->country[c].role==POLITY_PLAYER){ s->player=c; break; }
    for (int c=0;c<w->n_countries;c++){
        s->ai_on[c] = (c!=s->player && w->country[c].role!=POLITY_UNCLAIMED
                       && w->country[c].capital_prov>=0);
        if (s->ai_on[c]) ai_actor_init(&s->ai[c], w, s->econ, c, w->seed ^ (uint32_t)(c*2654435761u));
    }
    demography_attach(w, s->econ, s->drift);
    events_init(s->ev, w, w->seed);
    labor_init(s->labor, w); labor_seed_from_world(s->labor, w, s->econ, s->player);
    s->day=0; s->year=0;
}

/* ── Lectures de la chronique ────────────────────────────────────────────── */
/* Combien de régions un pays tient-il (les conquêtes/effondrements bougent ça). */
static int regions_of(const WorldEconomy *e, int c){
    int n=0; for (int r=0;r<e->n_regions;r++) if (e->region[r].owner==c) n++; return n;
}
/* Pays VIVANTS : ceux qui tiennent ≥1 région. */
static int living_countries(const World *w, const WorldEconomy *e){
    int n=0;
    for (int c=0;c<w->n_countries;c++)
        if (w->country[c].role!=POLITY_UNCLAIMED && regions_of(e,c)>0) n++;
    return n;
}
/* Pays le plus étendu (par régions). */
static int top_power(const World *w, const WorldEconomy *e, int *out_regions){
    int best=-1, bn=0;
    for (int c=0;c<w->n_countries;c++){
        if (w->country[c].role==POLITY_UNCLAIMED) continue;
        int n=regions_of(e,c); if (n>bn){bn=n;best=c;}
    }
    if (out_regions) *out_regions=bn;
    return best;
}
/* Nombre de guerres en cours (paires DIPLO_WAR). */
static int wars_active(const World *w, const DiploState *dp){
    int n=0;
    for (int a=0;a<w->n_countries;a++) for (int b=a+1;b<w->n_countries;b++)
        if (diplo_status(dp,a,b)==DIPLO_WAR) n++;
    return n;
}
/* Compte les rôles : empires (joueur + antagonistes) et cités-états. */
static void role_counts(const World *w, int *emp, int *city){
    *emp=*city=0;
    for (int c=0;c<w->n_countries;c++){
        PolityRole r=w->country[c].role;
        if (r==POLITY_PLAYER||r==POLITY_ANTAGONIST) (*emp)++;
        else if (r==POLITY_CITY_STATE) (*city)++;
    }
}
/* Prospérité & stabilité MOYENNES des empires vivants (lues par la membrane). */
static void empire_avg(const World *w, const WorldEconomy *e, const WorldProsperity *wp,
                       const TechState *ts, int *prosp, int *stab){
    long sp=0,ss=0; int n=0;
    for (int c=0;c<w->n_countries;c++){
        PolityRole rl=w->country[c].role;
        if ((rl==POLITY_PLAYER||rl==POLITY_ANTAGONIST) && regions_of(e,c)>0){
            CountryReadout r=country_readout(wp,ts,w,c);
            sp+=r.m_prosperite.value; ss+=r.m_stabilite.value; n++;
        }
    }
    *prosp = n? (int)(sp/n):0;
    *stab  = n? (int)(ss/n):0;
}

int main(int argc, char **argv){
    uint32_t base = (argc>1)?(uint32_t)strtoul(argv[1],NULL,10):20240607u;
    int nworlds   = (argc>2)?atoi(argv[2]):10;
    int years     = (argc>3)?atoi(argv[3]):200;
    if (nworlds<1) nworlds=1;
    if (years<1) years=1;

    World *w = malloc(sizeof(World));
    Sim s;
    s.econ=malloc(sizeof(WorldEconomy)); s.wp=malloc(sizeof(WorldProsperity));
    s.wl=malloc(sizeof(WorldLegitimacy)); s.net=malloc(sizeof(TradeNetwork));
    s.ts=calloc(SCPS_MAX_COUNTRY,sizeof(TechState)); s.sc=malloc(sizeof(Statecraft));
    s.ag=malloc(sizeof(AgencyState)); s.ev=malloc(sizeof(EventsState));
    s.drift=malloc(sizeof(ModifierStack)); s.labor=malloc(sizeof(LaborEcon));
    s.dp=malloc(sizeof(DiploState)); s.rn=malloc(sizeof(RouteNetwork));
    s.ai=calloc(SCPS_MAX_COUNTRY,sizeof(AiActor)); s.ai_on=calloc(SCPS_MAX_COUNTRY,sizeof(bool));
    if (!w||!s.econ||!s.wp||!s.wl||!s.net||!s.ts||!s.sc||!s.ag||!s.ev||!s.drift
        ||!s.labor||!s.dp||!s.rn||!s.ai||!s.ai_on){ fprintf(stderr,"OOM\n"); return 1; }

    printf("══════════════════════════════════════════════════════════════════════\n");
    printf(" CHRONIQUE — %d mondes, %d ans chacun (le moteur vivant, sans joueur)\n", nworlds, years);
    printf("══════════════════════════════════════════════════════════════════════\n");

    /* Agrégats sur tous les mondes */
    long tot_wars=0, tot_absorbed=0, tot_peakrev=0, tot_ages=0;
    int  worlds_with_ironorder=0, worlds_with_uprising=0;

    for (int k=0;k<nworlds;k++){
        uint32_t seed = base + (uint32_t)k*101u;
        WorldParams p = worldparams_default(seed);
        world_generate(w, &p);
        /* silence le bruit de génération : on a déjà tout imprimé par monde plus bas */
        sim_init(&s, w);

        int cont = w->n_continents;
        int n_emp, n_city; role_counts(w, &n_emp, &n_city);
        int c0 = living_countries(w, s.econ);

        int age_year[AGE_COUNT]; for (int a=0;a<AGE_COUNT;a++) age_year[a]=-1;
        int war_onsets=0, prev_wars=0, peak_wars=0;
        int peak_rev=0, peak_rev_year=0;
        int min_living=c0;

        printf("\n── Monde %d (graine %u) — %d empires, %d cités-états, %d continents, %d régions ──\n",
               k+1, seed, n_emp, n_city, cont, s.econ->n_regions);

        int snap[4]={50,100,150,200}, si=0;
        for (int yr=0; yr<years; yr++){
            for (int d=0; d<365; d++) sim_day(&s, w);
            /* âges : on imprime À L'AVÈNEMENT → la ligne du temps est chronologique */
            for (int a=0;a<AGE_COUNT;a++)
                if (age_year[a]<0 && ages_dawned(s.ev,(AgeId)a)){
                    age_year[a]=s.year;
                    printf("   an %3d  ÂGE : %s\n", s.year, age_name((AgeId)a));
                }
            int wa = wars_active(w, s.dp);
            if (wa>prev_wars) war_onsets += (wa-prev_wars);
            if (wa>peak_wars) peak_wars=wa;
            prev_wars = wa;
            int rv = events_count_revolutionary(w, s.wp);
            if (rv>peak_rev){ peak_rev=rv; peak_rev_year=s.year; }
            int lv = living_countries(w, s.econ);
            if (lv<min_living) min_living=lv;
            /* instantané tous les 50 ans : où en est le monde ? */
            if (si<4 && s.year>=snap[si]){
                int treg=0; top_power(w,s.econ,&treg);
                int ap,as_; empire_avg(w,s.econ,s.wp,s.ts,&ap,&as_);
                printf("   an %3d : %2d pays vivants | 1er empire %2d rég | empires : prospérité %2d stab %2d | %d guerre(s), %d révolté(s)\n",
                       snap[si], lv, treg, ap, as_, wa, rv);
                si++;
            }
        }

        int c1 = living_countries(w, s.econ);
        int treg=0, tp = top_power(w, s.econ, &treg);
        CountryReadout r = (tp>=0)? country_readout(s.wp,s.ts,w,tp)
                                  : country_readout(s.wp,s.ts,w,0);
        int absorbed = c0 - c1; if (absorbed<0) absorbed=0;
        int share = (s.econ->n_regions>0)? treg*100/s.econ->n_regions : 0;
        int nages=0; for (int a=0;a<AGE_COUNT;a++) if (age_year[a]>=0) nages++;

        printf("   BILAN an %d : %d pays subsistent (%d absorbés ; plancher %d) | %d âge(s) ; %d guerre(s) au total, pic %d ; pic de révolte %d (an %d)\n",
               years, c1, absorbed, min_living, nages, war_onsets, peak_wars, peak_rev, peak_rev_year);
        if (tp>=0)
            printf("              1er empire « %s » : %d régions (%d%% des terres) | Stabilité %d  Prospérité %d  Légitimité %d  Cohésion %d — Assise %s\n",
                   w->country[tp].name, treg, share, r.m_stabilite.value, r.m_prosperite.value,
                   r.m_legitimite.value, r.m_cohesion.value, label_assise(r.assise));

        tot_wars += war_onsets; tot_absorbed += absorbed; tot_peakrev += peak_rev; tot_ages += nages;
        if (age_year[AGE_ORDRE_FER]>=0)   worlds_with_ironorder++;
        if (age_year[AGE_SOULEVEMENTS]>=0) worlds_with_uprising++;
    }

    printf("\n══════════════════════════════════════════════════════════════════════\n");
    printf(" SYNTHÈSE (%d mondes × %d ans)\n", nworlds, years);
    printf("   âges éveillés (total) ....... %ld   (moy. %.1f/monde)\n", tot_ages, (double)tot_ages/nworlds);
    printf("   guerres déclenchées (total) . %ld   (moy. %.1f/monde)\n", tot_wars, (double)tot_wars/nworlds);
    printf("   pays absorbés (total) ....... %ld   (moy. %.1f/monde)\n", tot_absorbed, (double)tot_absorbed/nworlds);
    printf("   pic de révolte moyen ........ %.1f pays\n", (double)tot_peakrev/nworlds);
    printf("   mondes atteignant les Soulèvements : %d/%d   l'Ordre de Fer : %d/%d\n",
           worlds_with_uprising, nworlds, worlds_with_ironorder, nworlds);
    printf("══════════════════════════════════════════════════════════════════════\n");

    free(w); free(s.econ); free(s.wp); free(s.wl); free(s.net); free(s.ts); free(s.sc);
    free(s.ag); free(s.ev); free(s.drift); free(s.labor); free(s.dp); free(s.rn);
    free(s.ai); free(s.ai_on);
    return 0;
}
