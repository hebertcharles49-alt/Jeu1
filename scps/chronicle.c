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
#include "scps_intertrade.h"
#include "scps_warhost.h"
#include "scps_diplo.h"
#include "scps_events.h"
#include "scps_modifier.h"
#include "scps_demography.h"
#include "scps_revolt.h"
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
    RevoltState *rs;
    WarHost     *host;   /* armées levées par pays (mobilisation) */
    int16_t prev_owner_mo[SCPS_MAX_REG];   /* propriétaires au mois précédent (détection de conquête) */
    int day, year, player;
} Sim;

static int regions_of(const WorldEconomy *e, int c);   /* défini plus bas */

static void sim_day(Sim *s, World *w) {
    agency_advance(s->ag, w, s->econ, s->wl, 1);
    routes_advance(s->rn, w, s->econ, 1);
    for (int c=0;c<w->n_countries;c++) if (s->ai_on[c]){
        ai_step(&s->ai[c], w, s->econ, s->wp, s->wl, s->ag, s->rn, s->dp, s->day);
        ai_research_step(&s->ai[c], &s->ts[c], w, s->econ, s->wp, s->day);  /* l'arbre vivant */
    }
    world_events_tick(s->ev, w, s->econ, s->wl, s->wp, s->sc, s->rn, s->ts, 1);
    labor_tick(s->labor);
    /* — mensuel : économie + réputation diplomatique (O(n²)) + démographie — */
    if (s->day % 30 == 29) {
        econ_tick(s->econ, 1.f/12.f);
        statecraft_tick(s->sc, w, s->econ, s->wp, s->wl, s->dp, s->rn, 30);
        demography_tick(w, s->econ, s->wl, s->drift, 5.f, 5.f, 1.f/12.f);
        /* — conquête du mois : un peuple passé sous une couronne ÉTRANGÈRE devient
         *   restif (intégration à zéro, L au plancher) → terreau de sécession. */
        for (int r=0;r<s->econ->n_regions && r<SCPS_MAX_REG;r++){
            int16_t no=s->econ->region[r].owner, po=s->prev_owner_mo[r];
            if (po>=0 && no>=0 && no!=po){
                demography_on_conquest(w, s->econ, s->drift, r, no);
                revolt_on_conquest(s->rs, r);    /* subir la conquête arme le séparatisme (≈10 ans) */
            }
            s->prev_owner_mo[r]=no;
        }
        /* — la révolte INCARNÉE : la misère SOUTENUE d'une région (le pire déficit
         *   de groupe : faim, sur-taxe, aliénation, non-intégration) allume un
         *   soulèvement, puis on tranche (sécession, coup, jacquerie, écrasement).
         *   Un pays NÉ d'une sécession prend vie. */
        revolt_scan(s->rs, w, s->econ, s->drift, 30);
        revolt_tick(s->rs, w, s->econ, s->drift, s->wl, s->wp, 30);
        if (s->rs->last_spawned>=0){
            /* un pays vient de naître : on donne vie (IA) à tout sécessionniste
             * vivant pas encore piloté (plusieurs peuvent éclore le même mois). */
            for (int c=0;c<w->n_countries && c<SCPS_MAX_COUNTRY;c++){
                if (c==s->player || s->ai_on[c]) continue;
                if (w->country[c].role==POLITY_ANTAGONIST && w->country[c].capital_prov>=0
                    && regions_of(s->econ,c)>0){
                    s->ai_on[c]=true;
                    ai_actor_init(&s->ai[c], w, s->econ, c, w->seed ^ (uint32_t)(c*2654435761u));
                }
            }
            /* une SÉCESSION a changé des propriétaires CE mois : resynchroniser, sinon
             * la détection de conquête du mois prochain prendrait l'indépendance pour
             * une invasion (le peuple libéré deviendrait restif envers SON propre État). */
            for (int r=0;r<s->econ->n_regions && r<SCPS_MAX_REG;r++)
                s->prev_owner_mo[r]=s->econ->region[r].owner;
        }
    }
    if (s->day % 365 == 364) {
        econ_colonize_tick(s->econ, w); econ_migrate_tick(s->econ, w);
        world_tick(w, s->econ, 1.0f);
        legitimacy_tick(s->wl, w, s->econ, s->ts);
        trade_network_build(s->net, w, s->econ); trade_tick(s->econ, s->net);
        intertrade_tick(s->econ, s->rn, s->dp);   /* grandes routes marchandes (goods inter-pays + embargo) */
        prosperity_tick(s->wp, w, s->econ, s->net, s->ts, s->wl);
        /* DIPLOMATIE annuelle : usure de guerre, FONTE des trêves & du momentum
         * (la guerre peut reprendre après le répit), et le SCORE DE GUERRE (bras-de-fer
         * + attrition qui saigne les armes). */
        warhost_tick(s->host, w, s->econ, s->dp, 1.0f);   /* la mobilisation : les armées vivent */
        for (int c=0;c<w->n_countries && c<SCPS_MAX_COUNTRY;c++)
            diplo_set_faustian(s->dp, c, s->ts[c].charge);  /* souillure faustienne → croisades */
        diplo_tick(s->dp, 365.f);
        diplo_war_tick(s->dp, w, s->econ, s->wp, 1.0f);
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
    revolt_init(s->rs); warhost_init(s->host);
    for (int r=0;r<SCPS_MAX_REG;r++)
        s->prev_owner_mo[r] = (r<s->econ->n_regions)? s->econ->region[r].owner : -1;
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
/* Population totale (somme des strates économiques de toutes les régions). */
static double total_pop(const WorldEconomy *e){
    double p=0.0;
    for (int r=0;r<e->n_regions;r++) for (int c=0;c<CLASS_COUNT;c++) p+=e->region[r].strata[c].pop;
    return p;
}
/* Population PAR CONTINENT (remplit pc[0..ncont-1]). */
static void continent_pop(const World *w, const WorldEconomy *e, double *pc, int ncont){
    for (int i=0;i<ncont;i++) pc[i]=0.0;
    for (int r=0;r<e->n_regions && r<w->n_regions;r++){
        int ci=w->region[r].continent;
        if (ci<0||ci>=ncont) continue;
        for (int c=0;c<CLASS_COUNT;c++) pc[ci]+=e->region[r].strata[c].pop;
    }
}
/* POURQUOI les révoltes : moyennes L/K/SI des polities EN révolution (mode 2)
 * vs stables → on lit la cause (légitimité ? capacité ?). */
static void revolt_cause(const World *w, const WorldEconomy *e, const WorldProsperity *wp,
                         int *nr, float *Lr, float *Kr, float *SIr,
                         int *ns, float *Ls, float *Ks, float *SIs){
    *nr=*ns=0; *Lr=*Kr=*SIr=*Ls=*Ks=*SIs=0.f;
    for (int c=0;c<w->n_countries;c++){
        if (w->country[c].role==POLITY_UNCLAIMED || regions_of(e,c)==0) continue;
        const CountryProsperity *cp=&wp->country[c];
        if (cp->mode==2){ (*nr)++; *Lr+=cp->L; *Kr+=cp->K; *SIr+=cp->SI; }
        else            { (*ns)++; *Ls+=cp->L; *Ks+=cp->K; *SIs+=cp->SI; }
    }
    if (*nr){ *Lr/=*nr; *Kr/=*nr; *SIr/=*nr; }
    if (*ns){ *Ls/=*ns; *Ks/=*ns; *SIs/=*ns; }
}

/* Armée TOTALE du monde (somme des puissances militaires). */
static float total_army(const World *w, const WorldEconomy *e){
    float a=0.f; for (int c=0;c<w->n_countries;c++) a+=diplo_mil_power(w,e,c); return a;
}
/* Provinces COLONISÉES (régions peuplées × leurs provinces). */
static int colonized_provinces(const World *w, const WorldEconomy *e){
    int n=0;
    for (int r=0;r<e->n_regions && r<w->n_regions;r++)
        if (e->region[r].colonized) n+=w->region[r].n_provinces;
    return n;
}

int main(int argc, char **argv){
    uint32_t base = (argc>1)?(uint32_t)strtoul(argv[1],NULL,10):20240607u;
    int nsims     = (argc>2)?atoi(argv[2]):10;   /* sim i : 2+i empires, 5+i cités (2→11 / 5→14) */
    int years     = (argc>3)?atoi(argv[3]):200;
    if (nsims<1) nsims=1;
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
    s.rs=malloc(sizeof(RevoltState)); s.host=malloc(sizeof(WarHost));
    if (!w||!s.econ||!s.wp||!s.wl||!s.net||!s.ts||!s.sc||!s.ag||!s.ev||!s.drift
        ||!s.labor||!s.dp||!s.rn||!s.ai||!s.ai_on||!s.rs){ fprintf(stderr,"OOM\n"); return 1; }

    printf("══════════════════════════════════════════════════════════════════════\n");
    printf(" CHRONIQUE — balayage : %d sims, %d ans (empires 2→%d, cités 5→%d ; sans joueur)\n",
           nsims, years, 1+nsims, 4+nsims);
    printf("══════════════════════════════════════════════════════════════════════\n");

    /* Agrégats sur toutes les sims */
    long tot_wars=0, tot_absorbed=0, tot_peakrev=0, tot_ages=0, tot_conq=0;
    long tot_ignited=0, tot_seceded=0, tot_coup=0, tot_concession=0, tot_crushed=0, tot_revdead=0;
    long tot_techs=0, tot_faustian=0;
    int  worlds_with_ironorder=0, worlds_with_uprising=0;

    for (int k=0;k<nsims;k++){
        uint32_t seed = base + (uint32_t)k*101u;
        WorldParams p = worldparams_default(seed);
        p.n_empires     = 2 + k;      /* sim 1 : 2 empires … sim 11 : 12 */
        p.n_city_states = 5 + k;      /* sim 1 : 5 cités  … sim 11 : 15  */
        world_generate(w, &p);
        /* silence le bruit de génération : on a déjà tout imprimé par sim plus bas */
        sim_init(&s, w);

        int cont = w->n_continents;
        int n_emp, n_city; role_counts(w, &n_emp, &n_city);
        int c0 = living_countries(w, s.econ);

        int age_year[AGE_COUNT]; for (int a=0;a<AGE_COUNT;a++) age_year[a]=-1;
        int war_onsets=0, prev_wars=0, peak_wars=0;
        int peak_rev=0, peak_rev_year=0;
        int min_living=c0;
        /* suivi des transferts de propriété : conquête vs colonisation */
        int conq_prov=0;                              /* provinces PRISES de force (cumul) */
        int16_t prev_owner[SCPS_MAX_REG];
        for (int r=0;r<s.econ->n_regions && r<SCPS_MAX_REG;r++) prev_owner[r]=s.econ->region[r].owner;

        printf("\n── Sim %d (graine %u) — %d empires · %d cités-états · %d continents · %d régions ──\n",
               k+1, seed, n_emp, n_city, cont, s.econ->n_regions);

        int snap[4]={years/5, years*2/5, years*3/5, years*4/5}, si=0;  /* instantanés mis à l'échelle */
        for (int yr=0; yr<years; yr++){
            for (int d=0; d<365; d++) sim_day(&s, w);
            /* conquêtes de l'année : régions passées d'un PAYS à un autre (de force) */
            for (int r=0;r<s.econ->n_regions && r<SCPS_MAX_REG;r++){
                int16_t no=s.econ->region[r].owner, po=prev_owner[r];
                if (po>=0 && no>=0 && no!=po) conq_prov += w->region[r].n_provinces;
                prev_owner[r]=no;
            }
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
            /* instantané tous les 50 ans — les courbes DANS LE TEMPS :
             * population · armée totale · provinces colonisées · prises de force */
            if (si<4 && s.year>=snap[si]){
                int treg=0; top_power(w,s.econ,&treg);
                int ap,as_; empire_avg(w,s.econ,s.wp,s.ts,&ap,&as_);
                printf("   an %3d : %2d pays | pop %5.0fk | armée %5.0f | colonisées %3d prov | prises %3d prov | 1er empire %2d rég | prosp %2d stab %2d | %2d révolté(s)\n",
                       snap[si], lv, total_pop(s.econ)/1000.0, total_army(w,s.econ),
                       colonized_provinces(w,s.econ), conq_prov, treg, ap, as_, rv);
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

        /* MÉTRIQUES PAR EMPIRE (chaque empire vivant, trié par taille) — métriques 0-100. */
        {
            int idx[SCPS_MAX_COUNTRY], ne=0;
            for (int c=0;c<w->n_countries;c++){ PolityRole rl=w->country[c].role;
                if ((rl==POLITY_PLAYER||rl==POLITY_ANTAGONIST) && regions_of(s.econ,c)>0) idx[ne++]=c; }
            for (int a=0;a<ne;a++) for (int b=a+1;b<ne;b++)
                if (regions_of(s.econ,idx[b])>regions_of(s.econ,idx[a])){ int t=idx[a];idx[a]=idx[b];idx[b]=t; }
            printf("              empires vivants (%d) — métriques 0-100 :\n", ne);
            for (int a=0;a<ne;a++){ int c=idx[a];
                CountryReadout cr=country_readout(s.wp,s.ts,w,c);
                int ctech = s.ai_on[c]? s.ai[c].stats.techs : 0;
                printf("                · %-16s %3d rég · pop %5.0fk · Stab %3d Prosp %3d Légit %3d Cohés %3d Infl %3d · %2d tech%s\n",
                       w->country[c].name, regions_of(s.econ,c), ai_country_population(w,s.econ,c)/1000.0,
                       cr.m_stabilite.value, cr.m_prosperite.value, cr.m_legitimite.value, cr.m_cohesion.value,
                       cr.influence, ctech, (c==tp)?" ★":"");
            }
        }
        /* VIVIER DE CITÉS-ÉTATS : combien du pool initial reste DISPONIBLE (vivant). */
        { int cs=0; for (int c=0;c<w->n_countries;c++)
              if (w->country[c].role==POLITY_CITY_STATE && regions_of(s.econ,c)>0) cs++;
          printf("              vivier cités-états : %d disponibles / %d au départ (%d absorbées)\n",
                 cs, n_city, n_city-cs); }

        /* POPULATION : totale + par continent (les 4 plus peuplés). */
        {
            double pc[SCPS_MAX_CONTINENT]; continent_pop(w, s.econ, pc, cont);
            int ord[SCPS_MAX_CONTINENT]; for (int i=0;i<cont;i++) ord[i]=i;
            for (int i=0;i<cont;i++) for (int j=i+1;j<cont;j++) if (pc[ord[j]]>pc[ord[i]]){int t=ord[i];ord[i]=ord[j];ord[j]=t;}
            printf("              population : %.0fk au total ; par continent :", total_pop(s.econ)/1000.0);
            for (int i=0;i<cont && i<4;i++) printf(" C%d %.0fk", ord[i], pc[ord[i]]/1000.0);
            printf("\n");
        }
        /* EXPANSION : provinces colonisées (vierges peuplées) vs PRISES de force. */
        printf("              expansion : %d prov colonisées · %d prov PRISES de force · armée finale %.0f\n",
               colonized_provinces(w,s.econ), conq_prov, total_army(w,s.econ));
        /* POURQUOI les révoltes : la cause LUE (légitimité ? capacité ?). */
        {
            int nr,ns; float Lr,Kr,SIr,Ls,Ks,SIs;
            revolt_cause(w, s.econ, s.wp, &nr,&Lr,&Kr,&SIr, &ns,&Ls,&Ks,&SIs);
            printf("              révoltes : %d en révolution (Légit moy %.1f · capacité K %.1f · stab.int SI %.1f) "
                   "vs %d stables (Légit %.1f · K %.1f · SI %.1f)\n",
                   nr,Lr,Kr,SIr, ns,Ls,Ks,SIs);
        }
        /* SOULÈVEMENTS INCARNÉS : qui s'est levé et ce qu'il est advenu (acteurs réels). */
        printf("              soulèvements : %d allumés → %d sécession(s) · %d coup(s) · %d concession(s) · %d écrasé(s) (%ld morts au combat)\n",
               s.rs->n_ignited, s.rs->n_seceded, s.rs->n_coup, s.rs->n_concession, s.rs->n_crushed, s.rs->pop_lost);

        /* RECHERCHE : l'arbre VIT — nœuds déverrouillés (dont des bouts faustiens). */
        { int sim_techs=0, sim_faust=0;
          for (int c=0;c<w->n_countries;c++) if (s.ai_on[c]){ sim_techs+=s.ai[c].stats.techs; sim_faust+=s.ai[c].stats.techs_faustian; }
          printf("              recherche : %d nœuds déverrouillés (dont %d faustiens)\n", sim_techs, sim_faust);
          tot_techs += sim_techs; tot_faustian += sim_faust; }

        tot_wars += war_onsets; tot_absorbed += absorbed; tot_peakrev += peak_rev; tot_ages += nages;
        tot_conq += conq_prov;
        tot_ignited += s.rs->n_ignited; tot_seceded += s.rs->n_seceded; tot_coup += s.rs->n_coup;
        tot_concession += s.rs->n_concession; tot_crushed += s.rs->n_crushed; tot_revdead += s.rs->pop_lost;
        if (age_year[AGE_ORDRE_FER]>=0)   worlds_with_ironorder++;
        if (age_year[AGE_SOULEVEMENTS]>=0) worlds_with_uprising++;
    }

    printf("\n══════════════════════════════════════════════════════════════════════\n");
    printf(" SYNTHÈSE (%d sims × %d ans)\n", nsims, years);
    printf("   âges éveillés (total) ....... %ld   (moy. %.1f/sim)\n", tot_ages, (double)tot_ages/nsims);
    printf("   guerres déclenchées (total) . %ld   (moy. %.1f/sim)\n", tot_wars, (double)tot_wars/nsims);
    printf("   provinces prises de force ... %ld   (moy. %.1f/sim)\n", tot_conq, (double)tot_conq/nsims);
    printf("   pays absorbés (total) ....... %ld   (moy. %.1f/sim)\n", tot_absorbed, (double)tot_absorbed/nsims);
    printf("   nœuds de tech débloqués ..... %ld   (moy. %.1f/sim ; %ld faustiens)\n", tot_techs, (double)tot_techs/nsims, tot_faustian);
    printf("   pic de révolte moyen ........ %.1f pays\n", (double)tot_peakrev/nsims);
    printf("   soulèvements incarnés ....... %ld allumés → %ld sécession(s) · %ld coup(s) · %ld concession(s) · %ld écrasé(s)\n",
           tot_ignited, tot_seceded, tot_coup, tot_concession, tot_crushed);
    printf("   morts au combat (révoltes) .. %ld   (moy. %.0f/sim)\n", tot_revdead, (double)tot_revdead/nsims);
    printf("   sims atteignant les Soulèvements : %d/%d   l'Ordre de Fer : %d/%d\n",
           worlds_with_uprising, nsims, worlds_with_ironorder, nsims);
    printf("══════════════════════════════════════════════════════════════════════\n");

    free(w); free(s.econ); free(s.wp); free(s.wl); free(s.net); free(s.ts); free(s.sc);
    free(s.ag); free(s.ev); free(s.drift); free(s.labor); free(s.dp); free(s.rn);
    warhost_free(s.host); free(s.ai); free(s.ai_on); free(s.rs); free(s.host);
    return 0;
}
