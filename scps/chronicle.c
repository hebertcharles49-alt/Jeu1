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
#include "scps_campaign.h"
#include "scps_navy.h"
#include "scps_diplo.h"
#include "scps_events.h"
#include "scps_modifier.h"
#include "scps_demography.h"
#include "scps_revolt.h"
#include "scps_missions.h"
#include "scps_factions.h"
#include "scps_labor.h"
#include "scps_ai.h"
#include "scps_species.h"
#include "miniz.h"          /* HARNAIS DE DÉTERMINISME : mz_crc32 (vendoré, third_party) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#define CORR_CAPTURED 30   /* §C3 : seuil « polity tenue par une faction » (corr 0-100) */

/* ── État de simulation (copié de viewer.c, sans SDL) ───────────────────── */
typedef struct {
    WorldEconomy *econ; WorldProsperity *wp; WorldLegitimacy *wl; TradeNetwork *net;
    TechState *ts; Statecraft *sc; AgencyState *ag; EventsState *ev; ModifierStack *drift;
    LaborEcon *labor; DiploState *dp; RouteNetwork *rn; AiActor *ai; bool *ai_on;
    RevoltState *rs;
    WarHost     *host;   /* armées levées par pays (mobilisation) */
    Campaign    *camp;   /* armées de campagne : marche/siège/bataille sur la carte (non-invasif) */
    uint32_t     camp_rng;
    MissionsState *missions; /* missions décennales (rythme + injection de ressources) */
    NavyState   *navy;   /* la flotte (mer §5) : coques, chantier, entretien */
    int16_t prev_owner_mo[SCPS_MAX_REG];   /* propriétaires au mois précédent (détection de conquête) */
    int prev_dawned;         /* dernier âge avéné traité (engagement d'âge §7) */
    int day, year, player;
} Sim;

static int regions_of(const WorldEconomy *e, int c);   /* défini plus bas */

/* Les armées de CAMPAGNE : chaque pays mobilisé ET en guerre projette sa force vers
 * le front ennemi adjacent (marche §1 → siège → bataille §2/§3). NON-INVASIF : la
 * campagne LIT econ, ne change JAMAIS la propriété des régions — la conquête
 * abstraite (prix/volume) reste seule maîtresse du qui-tient-quoi ; ici, les armées
 * VIVENT seulement sur la carte (la fondation que l'UI §4 dessinera). */
static void sim_campaign_year(Sim *s, World *w) {
    for (int c=0; c<w->n_countries && c<SCPS_MAX_COUNTRY; c++) {
        if (campaign_active(s->camp,c) && campaign_phase(s->camp,c)!=FA_IDLE) continue; /* déjà en route */
        if (warhost_units(s->host, c) <= 0) continue;                                   /* rien à projeter */
        int frontier=-1, target=-1;
        for (int r=0; r<s->econ->n_regions && frontier<0; r++) {
            if (s->econ->region[r].owner!=c) continue;
            for (int sn=0; sn<s->econ->n_regions; sn++) {
                if (!s->econ->adj[r][sn]) continue;
                int ob=s->econ->region[sn].owner;
                if (ob<0 || ob==c || diplo_status(s->dp,c,ob)!=DIPLO_WAR) continue;
                frontier=r; target=sn; break;                       /* une frontière chaude */
            }
        }
        if (frontier>=0){
            campaign_order(s->camp, s->econ, c, frontier, target, &s->host->army[c]);
        } else {
            /* pas de frontière TERRESTRE : la guerre passe la mer si un port, des
             * transports et un chemin existent (mer §6/§8 — contraint par le champ). */
            int port=navy_best_port(w,s->econ,c);
            if (port>=0 && navy_transport_packets_free(s->navy,c)>0){
                int tgt=-1;
                for (int r2=0;r2<s->econ->n_regions && tgt<0;r2++){
                    int ob=s->econ->region[r2].owner;
                    if (ob<0||ob==c||diplo_status(s->dp,c,ob)!=DIPLO_WAR) continue;
                    if (!s->econ->region[r2].coastal) continue;
                    tgt=r2;
                }
                if (tgt>=0)
                    campaign_order_sea(s->camp, w, s->econ, s->navy, c, port, tgt, &s->host->army[c]);
            }
        }
    }
    campaign_tick(s->camp, w, s->econ, s->dp, &s->camp_rng, 365.f);  /* une année de campagne */
    campaign_release_transports(s->camp, s->navy);                    /* les transports rentrent à la rade */
}

static void sim_day(Sim *s, World *w) {
    agency_advance(s->ag, w, s->econ, s->wl, s->drift, 1);
    /* leviers intérieurs : draine les coûts SCPS différés (purge/mater) vers TechState */
    for (int c=0;c<w->n_countries && c<SCPS_MAX_COUNTRY;c++){
        float ch,fr,hh;
        if (agency_drain_levier_costs(c,&ch,&fr,&hh)){
            s->ts[c].charge+=ch; s->ts[c].fracture+=fr; s->ts[c].H+=hh;
        }
    }
    routes_advance(s->rn, w, s->econ, 1);
    for (int c=0;c<w->n_countries;c++) if (s->ai_on[c]){
        ai_step(&s->ai[c], w, s->econ, s->wp, s->wl, s->ag, s->rn, s->dp, s->day);
        ai_research_step(&s->ai[c], &s->ts[c], w, s->econ, s->wp, s->day);  /* l'arbre vivant */
    }
    world_events_tick(s->ev, w, s->econ, s->wl, s->wp, s->sc, s->rn, s->ts, 1);
    labor_tick(s->labor);
    navy_tick(s->navy, w, s->econ, 1.f);   /* chantier + entretien (la chaîne navale TIRE) */
    /* — mensuel : économie + réputation diplomatique (O(n²)) + démographie — */
    if (s->day % 30 == 29) {
        econ_apply_country_tech(s->econ, s->ts, SCPS_MAX_COUNTRY);  /* §B1 : techs de prod du pays → prod_mult région */
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
        navy_colonize_tick(s->navy, w, s->econ, 30.f);   /* mer §8 : on découvre ce que la volta touche */
        navy_course_tick(s->navy, w, s->econ, s->dp, s->rn, &s->camp_rng,
                         -1, 30.f);   /* coques : la course (raids - saignee - blocus - verdicts) */
        navy_interception_tick(s->navy, s->camp, w, s->econ, s->dp, &s->camp_rng);   /* les convois se chassent */
        /* IA navale FRUGALE (mer §5/§8) : un pays côtier prospère bâtit son port,
         * puis un transport, puis tente la route MARITIME — décision par éthos
         * (les poids fins viendront avec la passe course). */
        for (int c=0;c<w->n_countries && c<SCPS_MAX_COUNTRY;c++){
            if (!s->ai_on[c]) continue;
            int hr=s->ai[c].home_region;
            if (hr<0||hr>=s->econ->n_regions) continue;
            RegionEconomy *re=&s->econ->region[hr];
            if (re->owner!=c) continue;
            if (re->coastal && re->build.port<=0.f && re->treasury>400.f){
                agency_build(s->ag, s->econ, hr, EDI_PORT);
            } else if (navy_best_port(w,s->econ,c)>=0 && s->navy->n[c].build_hull<0){
                if (s->navy->n[c].hull[HULL_TRANSPORT]<2 && re->treasury>500.f)
                    navy_order_build(s->navy, w, s->econ, c, HULL_TRANSPORT);
                else if (s->navy->n[c].hull[HULL_MERCHANT]<1 && re->treasury>700.f)
                    navy_order_build(s->navy, w, s->econ, c, HULL_MERCHANT);
            }
            /* la route maritime : un partenaire porté, joignable, pas le même pays —
             * et la SOBRIÉTÉ : trois liens maritimes au plus par pays. */
            if (s->day%180==29 && navy_region_is_port(w,s->econ,hr)){
                int mine=0;
                for (int i=0;i<s->rn->n;i++){
                    const TradeRoute *t=&s->rn->route[i];
                    if (t->maritime && (t->ra==hr||t->rb==hr)) mine++;
                }
                for (int r2=0;r2<s->econ->n_regions && mine<3;r2++){
                    if (s->econ->region[r2].owner==c||s->econ->region[r2].owner<0) continue;
                    if (!navy_region_is_port(w,s->econ,r2)) continue;
                    if (routes_order(s->rn, w, s->econ, hr, r2, true)){ mine++; break; }
                }
            }
        }
        revolt_scan(s->rs, w, s->econ, s->drift, 30);
        revolt_tick(s->rs, w, s->econ, s->drift, s->wl, s->wp, 30);
        if (s->rs->last_spawned>=0){
            /* un pays vient de naître : on donne vie (IA) à tout sécessionniste
             * vivant pas encore piloté (plusieurs peuvent éclore le même mois). */
            for (int c=0;c<w->n_countries && c<SCPS_MAX_COUNTRY;c++){
                if (s->ai_on[c]) continue;
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
        sim_campaign_year(s, w);                           /* … et MARCHENT : campagne sur la carte */
        for (int c=0;c<w->n_countries && c<SCPS_MAX_COUNTRY;c++)
            diplo_set_faustian(s->dp, c, s->ts[c].charge);  /* souillure faustienne → croisades */
        diplo_tick(s->dp, 365.f);
        diplo_suzerainty_tick(s->dp, w, s->econ, s->wp);   /* suzeraineté + FRONDE : tributs, ligues, défections */
        diplo_war_tick(s->dp, w, s->econ, s->wp, 1.0f);
        missions_tick(s->missions, w, s->econ, s->ts, s->year);  /* missions décennales : rythme + récompense */
        faction_levers_decay(0.07f);   /* §4 : une stance non entretenue s'efface (~15 ans) */
        if (s->ev->ages.last_dawned != s->prev_dawned){          /* §7 : un âge se lève → engagement */
            int age=s->ev->ages.last_dawned;
            if (age>=0) for (int c=0;c<w->n_countries && c<SCPS_MAX_COUNTRY;c++)
                if (w->country[c].role!=POLITY_UNCLAIMED && regions_of(s->econ,c)>0)
                    faction_age_engage(w, s->econ, c, age);       /* la faction-patronne s'avance (l'IA accepte) */
            s->prev_dawned = s->ev->ages.last_dawned;
        }
    }
    if (++s->day % 365 == 0) s->year++;
}

static void sim_init(Sim *s, World *w) {
    econ_init(s->econ, w); gen_population(w, s->econ);
    worldgen_seed_peoples(w, s->econ, RACE_HUMAIN);
    legitimacy_init(s->wl, w, s->econ); prosperity_init(s->wp, w);
    trade_network_build(s->net, w, s->econ);
    statecraft_init(s->sc, w); agency_init(s->ag); diplo_init(s->dp); routes_init(s->rn);
    diplo_seed_rng(s->dp, w->seed);   /* la fronde tire sa graine du monde (séquence par sim) */
    intertrade_reset();   /* embargos décrétés + flux inter-pays : RAZ par sim */
    /* RAZ PLEINE PLAGE (SCPS_MAX_COUNTRY, pas n_countries) : n_countries GRANDIT par
     * sécession en cours de sim — la sim suivante repart plus bas. Sans ça, les slots
     * hauts gardent ai_on=true + un acteur/TechState PÉRIMÉS d'un autre monde : un pays
     * sécessionniste né à cet index sautait son init (« if (ai_on) continue ») → piloté
     * par un fantôme (home_region d'un ancien monde, cadences mortes, arbre de tech
     * hérité) et sa télémétrie polluait les totaux par sim. */
    for (int c=0;c<SCPS_MAX_COUNTRY;c++){ s->ai_on[c]=false; tech_state_init(&s->ts[c], false); }
    s->player = 0;
    for (int c=0;c<w->n_countries;c++) if (w->country[c].role==POLITY_PLAYER){ s->player=c; break; }
    /* PAS DE JOUEUR HUMAIN dans la chronique : TOUT pays habitable est piloté par
     * l'IA — y compris l'ex-emplacement « joueur ». Sinon ce pays restait inerte
     * (il ne bâtissait rien, ne se défendait pas) et FAUSSAIT le balayage (un trou
     * mort sur la carte). Le LaborEcon reste calé sur s->player (modèle isolé : il
     * ne nourrit pas l'éco partagée, les capitales agissent via capitale_* en direct). */
    for (int c=0;c<w->n_countries;c++){
        s->ai_on[c] = (w->country[c].role!=POLITY_UNCLAIMED
                       && w->country[c].capital_prov>=0);
        if (s->ai_on[c]) ai_actor_init(&s->ai[c], w, s->econ, c, w->seed ^ (uint32_t)(c*2654435761u));
    }
    demography_attach(w, s->econ, s->drift);
    demography_dyn_id_rebase(s->econ);   /* compteur de drift_id : repart au socle par sim */
    revolt_init(s->rs); warhost_init(s->host); missions_init(s->missions);
    navy_init(s->navy);
    campaign_init(s->camp, w, s->econ);                  /* armées de campagne : table de terrain + RAZ */
    s->camp_rng = w->seed ^ 0xCA117A11u;                 /* graine de campagne, propre à la sim */
    faction_levers_reset();   /* §4 : stances de factions remises à zéro pour cette sim */
    s->prev_dawned=-1;        /* §7 : aucun âge encore traité */
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
/* Pactes d'ALLIANCE actifs : paires de pays vivants liés (DIPLO_ALLIED). §D1/D2 :
 * les blocs se nouent (menace partagée RELATIVE) et se dénouent (désuétude/trahison) —
 * ce compteur dit si la diplomatie RESPIRE ou se fige. */
static int active_alliances(const World *w, const WorldEconomy *e, const DiploState *dp){
    int n=0;
    for (int a=0;a<w->n_countries;a++){
        if (w->country[a].role==POLITY_UNCLAIMED || regions_of(e,a)==0) continue;
        for (int b=a+1;b<w->n_countries;b++){
            if (w->country[b].role==POLITY_UNCLAIMED || regions_of(e,b)==0) continue;
            if (diplo_status(dp,a,b)==DIPLO_ALLIED) n++;
        }
    }
    return n;
}
/* Recensement des manufactures PAR TYPE (combien de joailleries, de scieries…) — lit
 * si la construction par rétroaction négative peuple bien la carte (et où ça manque). */
static void print_building_census(const WorldEconomy *e){
    long count[BLD_TYPE_COUNT]; for (int b=0;b<BLD_TYPE_COUNT;b++) count[b]=0;
    for (int r=0;r<e->n_regions;r++){
        const RegionEconomy *re=&e->region[r];
        if (!re->active || !re->colonized) continue;
        for (int i=0;i<re->n_bld;i++)
            if (re->bld[i].type>=0 && re->bld[i].type<BLD_TYPE_COUNT) count[re->bld[i].type]++;
    }
    printf("              manufactures :");
    for (int b=0;b<BLD_TYPE_COUNT;b++) if (count[b]>0) printf(" %ld×%s", count[b], building_name((BuildingType)b));
    printf("\n");
}
/* Satisfaction par CLASSE (pop-pondérée, sur les régions vivantes) — la vraie mesure du
 * bien-être, AVEC la distribution régionale active (≠ econ_scan, sans commerce). */
static void world_class_sat(const WorldEconomy *e, double out[CLASS_COUNT]){
    double sw[CLASS_COUNT]={0}, pw[CLASS_COUNT]={0};
    for (int r=0;r<e->n_regions;r++){
        const RegionEconomy *re=&e->region[r];
        if (!re->active || !re->colonized) continue;
        for (int c=0;c<CLASS_COUNT;c++){ double p=re->strata[c].pop; sw[c]+=re->strata[c].satisfaction*p; pw[c]+=p; }
    }
    for (int c=0;c<CLASS_COUNT;c++) out[c] = pw[c]>0 ? 100.0*sw[c]/pw[c] : 0.0;
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

/* ── TÉLÉMÉTRIE PAR ÂGE : marché · or par empire · tech, figés à l'avènement ── */
static double country_gold(const WorldEconomy *e, int c){
    double g=0.0; for (int r=0;r<e->n_regions;r++) if (e->region[r].owner==c) g+=e->region[r].treasury;
    return g;
}
static float avg_price(const WorldEconomy *e, Resource res){
    double s=0.0; int n=0;
    for (int r=0;r<e->n_regions;r++) if (e->region[r].colonized){ s+=e->region[r].price[res]; n++; }
    return n? (float)(s/n):0.f;
}
typedef struct {
    int    year;                                 /* -1 = âge pas (encore) avéné */
    double gold_total, gdp_total, pop_total;
    int    tech_total, living;
    float  p_grain, p_cloth, p_ware, p_tools;    /* le « marché » figé par âge */
    int    n_top; char top_name[3][32]; double top_gold[3]; int top_reg[3];
} AgeSnap;

static void capture_age_snap(AgeSnap *snap, int year, const World *w, const Sim *s){
    const WorldEconomy *e=s->econ;
    snap->year=year;
    double gold=0.0, gdp=0.0;
    for (int r=0;r<e->n_regions;r++){ gold+=e->region[r].treasury; gdp+=e->region[r].gdp; }
    snap->gold_total=gold; snap->gdp_total=gdp; snap->pop_total=total_pop(e);
    int tech=0; for (int c=0;c<w->n_countries && c<SCPS_MAX_COUNTRY;c++) if (s->ai_on[c]) tech+=s->ai[c].stats.techs;
    snap->tech_total=tech; snap->living=living_countries(w,e);
    snap->p_grain=avg_price(e,RES_GRAIN);         snap->p_cloth=avg_price(e,RES_CLOTH);
    snap->p_ware =avg_price(e,RES_PRECIOUS_WARE); snap->p_tools=avg_price(e,RES_TOOLS);
    /* or des 3 plus gros empires (par régions) */
    int idx[SCPS_MAX_COUNTRY], ne=0;
    for (int c=0;c<w->n_countries;c++){ PolityRole rl=w->country[c].role;
        if ((rl==POLITY_PLAYER||rl==POLITY_ANTAGONIST) && regions_of(e,c)>0 && ne<SCPS_MAX_COUNTRY) idx[ne++]=c; }
    for (int a=0;a<ne;a++) for (int b=a+1;b<ne;b++)
        if (regions_of(e,idx[b])>regions_of(e,idx[a])){ int t=idx[a];idx[a]=idx[b];idx[b]=t; }
    snap->n_top = ne<3?ne:3;
    for (int a=0;a<snap->n_top;a++){ int c=idx[a];
        snprintf(snap->top_name[a],sizeof snap->top_name[a],"%s",w->country[c].name);
        snap->top_gold[a]=country_gold(e,c); snap->top_reg[a]=regions_of(e,c); }
}

/* ════════════════════════════════════════════════════════════════════════
 * LE HARNAIS DE DÉTERMINISME (brief build §2) — le JUGE DE PAIX.
 * En mode --hash, chaque sim émet « HASH <graine> <crc> » : un CRC32 (miniz,
 * vendoré) de sa propre SORTIE OBSERVABLE, rendue ici sous forme TEXTE
 * canonique (le flux du monde : qui tient quoi, l'or, la tech, révoltes,
 * diplomatie, la flotte, la course, les batailles). Le texte porte les
 * flottants en %.9g — round-trippables : toute réduction dont l'ORDRE
 * d'accumulation change (une parallélisation §4 mal placée) décale un bit,
 * décale le hash, et `make determinism` vire au rouge. Même graine → même
 * hash, toujours : c'est lui qui arbitre toute modif du moteur. */
typedef struct { mz_ulong crc; } HashAcc;
static void hx(HashAcc *h, const char *fmt, ...){
    char b[256]; va_list ap; va_start(ap,fmt);
    int n=vsnprintf(b,sizeof b,fmt,ap); va_end(ap);
    if (n<0) return;
    if (n>(int)sizeof b-1) n=(int)sizeof b-1;
    h->crc = mz_crc32(h->crc, (const unsigned char*)b, (size_t)n);
}
static uint32_t chronicle_sim_hash(uint32_t seed, const Sim *s, const World *w){
    HashAcc h; h.crc = MZ_CRC32_INIT;
    hx(&h,"seed=%u nc=%d nr=%d np=%d nk=%d\n",seed,w->n_countries,w->n_regions,w->n_provinces,w->n_continents);
    for (int c=0;c<w->n_countries && c<SCPS_MAX_COUNTRY;c++)
        hx(&h,"C%d reg=%d gold=%.9g tech=%d role=%d\n",
           c,regions_of(s->econ,c),country_gold(s->econ,c),s->ts[c].n_unlocked,(int)w->country[c].role);
    for (int r=0;r<s->econ->n_regions && r<SCPS_MAX_REG;r++){ const RegionEconomy *re=&s->econ->region[r];
        double pop=0; for(int k=0;k<CLASS_COUNT;k++) pop+=re->strata[k].pop;
        hx(&h,"R%d o=%d t=%.9g p=%.9g c=%d e=%d b=%.9g\n",
           r,re->owner,(double)re->treasury,pop,re->colonized,re->estuary,(double)re->balafre_days);
    }
    hx(&h,"rev ig=%d se=%d co=%d cc=%d cr=%d pl=%ld\n",
       s->rs->n_ignited,s->rs->n_seceded,s->rs->n_coup,s->rs->n_concession,s->rs->n_crushed,(long)s->rs->pop_lost);
    hx(&h,"dip sv=%d pr=%d cc=%d ci=%d df=%d ap=%d\n",
       s->dp->n_servage,s->dp->n_protectorat,s->dp->n_concordat,s->dp->n_cite,s->dp->n_defections,s->dp->n_war_antipirate);
    for (int c=0;c<SCPS_MAX_COUNTRY;c++){ const Navy *nv=&s->navy->n[c];
        if (!nv->built_total && !nv->raids_done && !nv->crew && !nv->intercepts) continue;
        hx(&h,"N%d b=%d r=%d pz=%d nv=%d ic=%d dr=%ld cw=%d su=%.9g\n",
           c,nv->built_total,nv->raids_done,nv->prises,nv->navals,nv->intercepts,(long)nv->drowned,nv->crew,(double)nv->supplies_eaten);
    }
    hx(&h,"camp bt=%d dc=%ld dp=%ld sl=%d\n",
       s->camp->n_battles,(long)s->camp->dead_choc,(long)s->camp->dead_pursuit,s->camp->n_sails);
    return (uint32_t)h.crc;
}

int main(int argc, char **argv){
    /* positionnels FILTRÉS de l'option --hash (le harnais de déterminisme). */
    const char *pos[8]; int np=0, hash_mode=0;
    for (int i=1;i<argc;i++){
        if (!strcmp(argv[i],"--hash")) hash_mode=1;
        else if (np<8) pos[np++]=argv[i];
    }
    uint32_t base = (np>0)?(uint32_t)strtoul(pos[0],NULL,10):20240607u;
    int nsims     = (np>1)?atoi(pos[1]):10;      /* sim i : 2+i empires, 5+i cités (2→11 / 5→14) */
    int years     = (np>2)?atoi(pos[2]):200;
    int fix_emp   = (np>3)?atoi(pos[3]):0;       /* >0 : empires FIXES (sinon cycle 2+k) */
    int fix_cs    = (np>4)?atoi(pos[4]):0;       /* >0 : cités-états FIXES (sinon cycle 5+k) */
    int fix_cont  = (np>5)?atoi(pos[5]):0;       /* >0 : continents FIXES (mer §8 : bancs 2..4) */
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
    s.missions=malloc(sizeof(MissionsState)); s.camp=malloc(sizeof(Campaign));
    s.navy=malloc(sizeof(NavyState));
    if (!w||!s.econ||!s.wp||!s.wl||!s.net||!s.ts||!s.sc||!s.ag||!s.ev||!s.drift
        ||!s.labor||!s.dp||!s.rn||!s.ai||!s.ai_on||!s.rs||!s.host||!s.missions||!s.camp||!s.navy){
        fprintf(stderr,"OOM\n"); return 1; }

    printf("══════════════════════════════════════════════════════════════════════\n");
    if (fix_emp>0 || fix_cs>0)
        printf(" CHRONIQUE — balayage : %d sims, %d ans (empires %d, cités %d FIXES ; sans joueur)\n",
               nsims, years, fix_emp>0?fix_emp:2, fix_cs>0?fix_cs:5);
    else
        printf(" CHRONIQUE — balayage : %d sims, %d ans (empires 2→%d, cités 5→%d ; sans joueur)\n",
               nsims, years, 1+nsims, 4+nsims);
    printf("══════════════════════════════════════════════════════════════════════\n");

    /* Agrégats sur toutes les sims */
    long tot_wars=0, tot_absorbed=0, tot_emerged=0, tot_peakrev=0, tot_ages=0, tot_conq=0;
    long tot_ignited=0, tot_seceded=0, tot_coup=0, tot_concession=0, tot_crushed=0, tot_revdead=0;
    long tot_techs=0, tot_faustian=0, tot_campaign=0, tot_alliances=0;   /* §D : pactes actifs */
    long tot_sync=0, tot_sync_distinct=0;   /* §syncrétique : nœuds à porte culturelle + dispersion */
    long tot_tree_pct=0; int tot_tree_sims=0;   /* §A : fraction d'arbre déverrouillée (le coût force les choix) */
    long tot_reloc=0;   /* §reloc : ensemencements de pop pour combler une pénurie */
    long tot_repress=0, tot_assim=0, tot_purge=0, tot_purge_dead=0;       /* leviers intérieurs */
    long tot_serv=0, tot_prot=0, tot_conc=0, tot_cite=0, tot_defect=0;    /* suzeraineté */
    long tot_ligues=0, tot_frondes=0, tot_indep=0, tot_renvers=0, tot_ecrase=0;   /* fronde */
    long tot_bt=0, tot_btj=0, tot_routs=0, tot_mchoc=0, tot_mpour=0, tot_deseng=0, tot_renf=0, tot_nul=0;   /* batailles */
    double tot_sat[CLASS_COUNT]={0}; double tot_trade=0;   /* §distrib : satisfaction par classe + commerce */
    long tot_captured=0, tot_worstcorr=0; int worlds_with_capture=0;   /* §C3 : le rot, agrégé */
    int  worlds_with_ironorder=0, worlds_with_uprising=0;
    long tot_hulls=0, tot_sails=0, tot_searoutes=0, tot_colonies_om=0;   /* mer §10 */
    double tot_supplies=0, tot_saildays=0;
    long tot_raids=0, tot_prises=0, tot_navals=0, tot_disarm=0, tot_warpir=0, tot_balafres=0;
    double tot_loot=0;   /* coques §8 : la course, agrégée */
    long tot_intercepts=0, tot_drowned=0;

    for (int k=0;k<nsims;k++){
        uint32_t seed = base + (uint32_t)k*101u;
        WorldParams p = worldparams_default(seed);
        /* La taille du monde CYCLE (2→11 empires, 5→14 cités) — identique aux 10
         * premières sims, puis on REBOUCLE : un balayage de 100 sims reste faisable
         * (sinon 2+k saturerait SCPS_MAX_COUNTRY=56 et chaque sim tardive ramperait).
         * Graines toutes distinctes (base+k·101) → 100 mondes différents. */
        p.n_empires     = (fix_emp>0)? fix_emp : 2 + (k % 10);
        p.n_city_states = (fix_cs >0)? fix_cs  : 5 + (k % 10);
        if (fix_cont>0) p.n_continents = fix_cont;
        world_generate(w, &p);
        /* silence le bruit de génération : on a déjà tout imprimé par sim plus bas */
        sim_init(&s, w);

        /* LA VOLTA, mesurée (mer §10) : entre deux côtes éloignées, l'aller ne vaut
         * pas le retour — l'asymétrie du champ de courants, en jours. */
        { int rA=-1, rB=-1;
          for (int r=0;r<s.econ->n_regions;r++) if (s.econ->region[r].coastal){ rA=r; break; }
          for (int r=s.econ->n_regions-1;r>=0;r--) if (s.econ->region[r].coastal){ rB=r; break; }
          if (rA>=0 && rB>=0 && rA!=rB){
              float aller=navy_sea_days_regions(w,rA,rB), retour=navy_sea_days_regions(w,rB,rA);
              if (aller>=0.f && retour>=0.f)
                  printf("   mer : volta mesurée région %d ⇄ %d — aller %.0f j · retour %.0f j (asymétrie ×%.2f)\n",
                         rA, rB, aller, retour, (aller>0.f)?retour/aller:0.f);
          } }

        int cont = w->n_continents;
        int n_emp, n_city; role_counts(w, &n_emp, &n_city);
        int c0 = living_countries(w, s.econ);
        /* qui est VIVANT au départ : on tracera les morts (absorbés) ET les
         * naissances (sécessions) — la carte politique BOUGE, plus d'invariant figé. */
        bool was_alive[SCPS_MAX_COUNTRY];
        for (int c=0;c<SCPS_MAX_COUNTRY;c++)
            was_alive[c] = (c<w->n_countries && w->country[c].role!=POLITY_UNCLAIMED && regions_of(s.econ,c)>0);

        int age_year[AGE_COUNT]; AgeSnap age_snap[AGE_COUNT];
        for (int a=0;a<AGE_COUNT;a++){ age_year[a]=-1; age_snap[a].year=-1; }
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
                    capture_age_snap(&age_snap[a], s.year, w, &s);   /* marché·or·tech figés à l'avènement */
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
        /* MORTS (absorbés) vs NAISSANCES (sécessions) : la carte politique respire. */
        int absorbed=0, emerged=0;
        for (int c=0;c<w->n_countries && c<SCPS_MAX_COUNTRY;c++){
            bool alive_now = (w->country[c].role!=POLITY_UNCLAIMED && regions_of(s.econ,c)>0);
            if (was_alive[c] && !alive_now) absorbed++;
            if (!was_alive[c] && alive_now) emerged++;
        }
        int share = (s.econ->n_regions>0)? treg*100/s.econ->n_regions : 0;
        int nages=0; for (int a=0;a<AGE_COUNT;a++) if (age_year[a]>=0) nages++;

        printf("   BILAN an %d : %d pays subsistent (%d absorbés · %d émergés ; plancher %d) | %d âge(s) ; %d guerre(s) au total, pic %d ; pic de révolte %d (an %d)\n",
               years, c1, absorbed, emerged, min_living, nages, war_onsets, peak_wars, peak_rev, peak_rev_year);
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
                printf("                · %-16s %3d rég · pop %5.0fk · Stab %3d Prosp %3d Légit %3d Cohés %3d Corr %3d · %2d tech%s\n",
                       w->country[c].name, regions_of(s.econ,c), ai_country_population(w,s.econ,c)/1000.0,
                       cr.m_stabilite.value, cr.m_prosperite.value, cr.m_legitimite.value, cr.m_cohesion.value,
                       cr.corruption, ctech, (c==tp)?" ★":"");
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
        int n_alliances = active_alliances(w, s.econ, s.dp);
        printf("              diplomatie : %d pacte(s) d'alliance actif(s)\n", n_alliances);
        { double csat[CLASS_COUNT]; world_class_sat(s.econ, csat);
          double tradev = intertrade_imports_value(s.econ);
          printf("              satisfaction (pop-pondérée, AVEC distribution) : Laborer %.0f%% · Bourgeois %.0f%% · Élite %.0f%% | commerce inter-pays/an %.0f\n",
                 csat[CLASS_LABORER], csat[CLASS_BOURGEOIS], csat[CLASS_ELITE], tradev);
          for (int c=0;c<CLASS_COUNT;c++) tot_sat[c]+=csat[c];
          tot_trade += tradev; }
        print_building_census(s.econ);
        printf("              expansion : %d prov colonisées · %d prov PRISES de force · armée finale %.0f\n",
               colonized_provinces(w,s.econ), conq_prov, total_army(w,s.econ));
        /* TÉLÉMÉTRIE PAR ÂGE : le marché, l'or par empire et la tech à chaque avènement. */
        { bool any=false; for (int a=0;a<AGE_COUNT;a++) if (age_snap[a].year>=0) any=true;
          if (any){
            printf("              ── par âge (instantané à l'avènement) ──\n");
            int ord[AGE_COUNT]; for (int a=0;a<AGE_COUNT;a++) ord[a]=a;   /* tri chronologique */
            for (int a=0;a<AGE_COUNT;a++) for (int b=a+1;b<AGE_COUNT;b++){
                int ya=age_snap[ord[a]].year, yb=age_snap[ord[b]].year;
                if (ya<0) ya=99999;
                if (yb<0) yb=99999;
                if (yb<ya){ int t=ord[a];ord[a]=ord[b];ord[b]=t; }
            }
            for (int oi=0;oi<AGE_COUNT;oi++){ int a=ord[oi];
                const AgeSnap *sn=&age_snap[a]; if (sn->year<0) continue;
                printf("                an %3d %-24s | pays %2d · pop %5.0fk · or Σ %7.0f · PIB %6.0f · tech %3d\n",
                       sn->year, age_name((AgeId)a), sn->living, sn->pop_total/1000.0,
                       sn->gold_total, sn->gdp_total, sn->tech_total);
                printf("                       marché : grain %.2f · étoffe %.2f · orfèvr. %.2f · outils %.2f | or/empire :",
                       sn->p_grain, sn->p_cloth, sn->p_ware, sn->p_tools);
                for (int t=0;t<sn->n_top;t++) printf(" %s %.0f(%dr)", sn->top_name[t], sn->top_gold[t], sn->top_reg[t]);
                printf("\n");
            }
          }
        }
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
        /* §C3 — LA CONCESSION A UN PRIX, EN CLAIR : le rot s'accumule sur la polity qui
         * CÈDE (souvent une marche faible ou une cité-état, PAS le 1er empire), donc on
         * balaie TOUTES les polities vivantes — pas seulement les empires affichés plus
         * haut — sinon la capture reste invisible là où elle mord vraiment. */
        {
            int worst_c=-1, worst_corr=0, n_captured=0;
            for (int c=0;c<w->n_countries && c<SCPS_MAX_COUNTRY;c++){
                if (w->country[c].role==POLITY_UNCLAIMED || regions_of(s.econ,c)==0) continue;
                int cor = faction_corruption_0_100(c);
                if (cor>=CORR_CAPTURED) n_captured++;
                if (cor>worst_corr){ worst_corr=cor; worst_c=c; }
            }
            if (worst_c>=0 && worst_corr>0)
                printf("              corruption : %d polity(s) capturée(s) (corr≥%d) · la plus pourrie « %s » corr %d — tenue par les %s\n",
                       n_captured, CORR_CAPTURED, w->country[worst_c].name, worst_corr,
                       faction_name(faction_captor(worst_c)));
            else
                printf("              corruption : aucune capture notable (l'élite n'a pas eu à céder)\n");
            tot_captured += n_captured; tot_worstcorr += worst_corr;
            if (n_captured>0) worlds_with_capture++;
        }

        /* RECHERCHE : l'arbre VIT — nœuds déverrouillés (dont des bouts faustiens). */
        { int sim_techs=0, sim_faust=0, sim_reloc=0;
          for (int c=0;c<w->n_countries;c++) if (s.ai_on[c]){ sim_techs+=s.ai[c].stats.techs; sim_faust+=s.ai[c].stats.techs_faustian; sim_reloc+=s.ai[c].stats.relocations; }
          printf("              recherche : %d nœuds déverrouillés (dont %d faustiens) · %d relocalisation(s) pour combler une pénurie (peupler sa province-ressource)\n", sim_techs, sim_faust, sim_reloc);
          tot_techs += sim_techs; tot_faustian += sim_faust; tot_reloc += sim_reloc; }

        /* LEVIERS & SUZERAINETÉ (brief leviers) : l'usage par sim — sans ces lignes,
         * on ne sait ni si l'IA s'en sert, ni si elle s'en sert TROP. */
        { int rep,ass,pur; long dead; agency_levier_stats(&rep,&ass,&pur,&dead);
          printf("              leviers : %d matage(s) · %d formation(s) · %d purge(s) (%ld morts) | suzeraineté : %d servage · %d protectorat · %d concordat · %d cité · %d défection(s)\n",
                 rep, ass, pur, dead,
                 s.dp->n_servage, s.dp->n_protectorat, s.dp->n_concordat, s.dp->n_cite, s.dp->n_defections);
          printf("              fronde : %d ligue(s) · %d fronde(s) → %d indépendance(s) · %d RENVERSEMENT(s) · %d écrasée(s) | défections %d paisibles · %d par reprise | contre-leviers : %d don · %d allègement · %d division · %d intimidation\n",
                 s.dp->n_ligues, s.dp->n_frondes, s.dp->n_indep, s.dp->n_renvers, s.dp->n_ecrase,
                 s.dp->n_defect_paix, s.dp->n_defect_guerre,
                 s.dp->n_lev_don, s.dp->n_lev_allege, s.dp->n_lev_divise, s.dp->n_lev_intim);
          tot_ligues+=s.dp->n_ligues; tot_frondes+=s.dp->n_frondes; tot_indep+=s.dp->n_indep;
          tot_renvers+=s.dp->n_renvers; tot_ecrase+=s.dp->n_ecrase;

        /* BATAILLES DANS LE TEMPS (§8) : durées, déroutes, et LA vérif — la poursuite
         * doit dominer le choc, sinon on a juste ralenti l'ancien modèle. */
        printf("              batailles : %d livrée(s) · %.0f j en moy. · %d déroute(s) · %d décrochage(s) · %d renfort(s) · %d nul(s) | morts : %ld au CHOC vs %ld en POURSUITE\n",
               s.camp->n_battles, s.camp->n_battles? (double)s.camp->battle_days/s.camp->n_battles:0.0,
               s.camp->n_routs, s.camp->n_disengage, s.camp->n_reinforce, s.camp->n_stalemate,
               s.camp->dead_choc, s.camp->dead_pursuit);
        tot_bt+=s.camp->n_battles; tot_btj+=s.camp->battle_days; tot_routs+=s.camp->n_routs;
        tot_mchoc+=s.camp->dead_choc; tot_mpour+=s.camp->dead_pursuit;
        tot_deseng+=s.camp->n_disengage; tot_renf+=s.camp->n_reinforce; tot_nul+=s.camp->n_stalemate;
          tot_repress+=rep; tot_assim+=ass; tot_purge+=pur; tot_purge_dead+=dead;
          tot_serv+=s.dp->n_servage; tot_prot+=s.dp->n_protectorat; tot_conc+=s.dp->n_concordat;
          tot_cite+=s.dp->n_cite; tot_defect+=s.dp->n_defections; }

        /* ARBRE (§A) : fraction de l'arbre déverrouillée PAR EMPIRE (cible < 100 % → l'empire
         * doit CHOISIR) + thème DOMINANT (deux empires aux choix différents → divergence). */
        { int nemp=0, fmin=999, fmax=0; long fsum=0; int dom[THM_COUNT]={0};
          for (int c=0;c<w->n_countries;c++){
              if (!s.ai_on[c] || regions_of(s.econ,c)==0) continue;
              int pct=(TECH_COUNT>0)?100*s.ts[c].n_unlocked/TECH_COUNT:0;
              fsum+=pct; if(pct<fmin)fmin=pct; if(pct>fmax)fmax=pct; nemp++;
              int th_cnt[THM_COUNT]={0};
              for (int id=0;id<TECH_COUNT;id++) if (s.ts[c].unlocked[id] && !tech_is_base((TechId)id)){
                  TechTheme th=tech_node((TechId)id)->theme; if(th>=0&&th<THM_COUNT) th_cnt[th]++; }
              int best=0; for (int t=1;t<THM_COUNT;t++) if (th_cnt[t]>th_cnt[best]) best=t;
              dom[best]++;
          }
          if (nemp>0){
              printf("              arbre : %ld%% déverrouillé/empire (min %d%% · max %d%%) · spécialisation — %d Savoir · %d Forge · %d Société\n",
                     fsum/nemp, fmin, fmax, dom[THM_SAVOIR], dom[THM_FORGE], dom[THM_SOCIETE]);
              tot_tree_pct += fsum/nemp; tot_tree_sims++;
          }
        }

        /* SYNCRÉTISME (§tech culturelle) : les nœuds à PORTE D'ARCHÉTYPE (ex-signatures de
         * race, désormais ouvertes par la CULTURE gouvernée — soi ou contact) — combien
         * acquis, et la DISPERSION entre empires : deux contacts différents → arbres différents. */
        { int sync_total=0, nmax=0, nmin=999, nemp=0, combo=0, diff_total=0; int arch_reached[RACE_COUNT]={0};
          for (int c=0;c<w->n_countries;c++){
              if (!s.ai_on[c] || regions_of(s.econ,c)==0) continue;     /* vivant seulement */
              int n=0;
              for (int id=0; id<TECH_COUNT; id++)
                  if (s.ts[c].unlocked[id] && tech_node((TechId)id)->native!=RACE_COUNT){
                      n++; arch_reached[tech_node((TechId)id)->native]=1; }
              if (s.ts[c].unlocked[TECH_FORGE_RUNES]) combo++;          /* §18.3 : armes enchantées = forge runique × arcane */
              diff_total += s.ts[c].n_sync;                             /* §8 : nœuds de diffusion (contact peu profond) loqués */
              sync_total+=n; if(n>nmax)nmax=n; if(n<nmin)nmin=n; nemp++;
          }
          int distinct=0; for (int ar=0;ar<RACE_COUNT;ar++) distinct+=arch_reached[ar];   /* ar : ne pas masquer le r extérieur (-Wshadow) */
          if (nemp>0){
              printf("              syncrétisme : %d nœud(s) profond(s) (gouvernance) · %d diffusion(s) (commerce/frontière/foi) · %d/%d archétype(s) · dispersion %d–%d/empire · %d ont la COMBINAISON forge runique × arcane\n",
                     sync_total, diff_total, distinct, (int)RACE_COUNT, nmin, nmax, combo);
              tot_sync += sync_total; tot_sync_distinct += distinct;
          }
        }

        /* CAMPAGNE : les armées ont-elles VÉCU sur la carte (marche/siège/bataille) ?
         * Réductions = sièges menés à terme (enregistrés, PAS appliqués à econ). */
        { int reduced=0, moving=0;
          for (int c=0;c<w->n_countries && c<SCPS_MAX_COUNTRY;c++){
              reduced += campaign_taken(s.camp,c);
              if (campaign_active(s.camp,c) && campaign_phase(s.camp,c)!=FA_IDLE) moving++;
          }
          printf("              campagne : %d région(s) réduite(s) par les armées de terrain · %d en mouvement (non-invasif)\n",
                 reduced, moving);
          tot_campaign += reduced; }
        { long hulls=0; double sup=0;   /* mer §10 : la chaîne navale TIRE */
          for (int c=0;c<SCPS_MAX_COUNTRY;c++){ hulls+=s.navy->n[c].built_total; sup+=s.navy->n[c].supplies_eaten; }
          int searoutes=0;
          for (int i=0;i<s.rn->n;i++) if (s.rn->route[i].maritime && s.rn->route[i].open) searoutes++;
          long col_om=0;   /* colonies OUTRE-MER : la région vit sur un autre continent que sa couronne */
          for (int r=0;r<s.econ->n_regions && r<w->n_regions;r++){
              const RegionEconomy *re=&s.econ->region[r];
              if (!re->colonized || re->owner<0 || re->owner>=w->n_countries) continue;
              int cp=w->country[re->owner].capital_prov;
              if (cp<0||cp>=w->n_provinces) continue;
              if (w->region[r].continent != w->province[cp].continent) col_om++;
          }
          printf("              mer : %ld coque(s) bâtie(s) · %.0f fournitures navales consommées · %d traversée(s) (%.0f j/trav.) · %d route(s) maritime(s) · %ld colonie(s) outre-mer\n",
                 hulls, sup, s.camp->n_sails,
                 (s.camp->n_sails>0)?(double)s.camp->sail_days_sum/s.camp->n_sails:0.0,
                 searoutes, col_om);
          tot_hulls+=hulls; tot_supplies+=sup; tot_sails+=s.camp->n_sails;
          tot_saildays+=s.camp->sail_days_sum; tot_searoutes+=searoutes; tot_colonies_om+=col_om; }
        { long raids=0, prises=0, navals=0, disarmed=0; double loot=0, blocj=0; int pirates=0, nids=0;
          long intercepts=0, drowned=0, crews=0;
          for (int c=0;c<SCPS_MAX_COUNTRY;c++){ const Navy *nv=&s.navy->n[c];
              raids+=nv->raids_done; prises+=nv->prises; navals+=nv->navals; disarmed+=nv->disarmed;
              loot+=nv->loot_gold; blocj+=nv->blocus_days;
              intercepts+=nv->intercepts; drowned+=nv->drowned; crews+=nv->crew;
              pirates+=nv->hull[HULL_PIRATE]; if (nv->nest_region>=0) nids++; }
          int balafres=0; for (int r=0;r<s.econ->n_regions;r++) if (s.econ->region[r].balafre_days>0.f) balafres++;
          printf("              course : %ld raid(s) (%.0f or pillés) - %d pirate(s) (%d nid(s) en eaux mortes) - %ld bataille(s) navale(s) - %ld prise(s) - %.0f j de blocus - %ld désarmement(s) - %d guerre(s) anti-piraterie - %d balafre(s) actives\n",
                 raids, loot, pirates, nids, navals, prises, blocj, disarmed, s.dp->n_war_antipirate, balafres);
          printf("              marine : %ld marin(s) embarqués - %ld interception(s) - %ld paquet(s) noyés\n",
                 crews, intercepts, drowned);
        { float vd,vu,bd,bu,pd,pu;   /* commerce asym. §6 : le tri par SENS, mesuré */
          intertrade_asym_stats(&vd,&vu,&bd,&bu,&pd,&pu);
          int nfluv=0, nmar=0;
          for (int i=0;i<s.rn->n;i++){ if (s.rn->route[i].fluvial) nfluv++; if (s.rn->route[i].maritime&&s.rn->route[i].open) nmar++; }
          printf("              commerce asym. : aval %.2f vs amont %.2f (ratio %.1fx) - vrac %.2f/%.2f - précieux %.2f/%.2f (aval/amont, %d remontée(s) de luxe) - %d route(s) fluviale(s)\n",
                 vd, vu, (vu>0.01f)?vd/vu:0.f, bd, bu, pd, pu, intertrade_precious_upstream_events(), nfluv);
          /* le Carrefour des EMBOUCHURES : les estuaires doivent monter d'eux-mêmes */
          { int top_est=0;
            for (int r=0;r<s.econ->n_regions;r++){
                if (!s.econ->region[r].estuary) continue;
                if (routes_count_for_region(s.rn,r)>=2) top_est++;
            }
            printf("              entrepôts : %d estuaire(s) en carrefour (≥2 routes)\n", top_est); }
          /* LA VÉRIF des briefs : un continent PLUS RICHE qu'un autre (géographie, pas script) */
          { double cw[SCPS_MAX_CONTINENT]={0}; double cpop[SCPS_MAX_CONTINENT]={0};
            for (int r=0;r<s.econ->n_regions && r<w->n_regions;r++){
                int ct2=w->region[r].continent;
                if (ct2<0||ct2>=SCPS_MAX_CONTINENT) continue;
                cw[ct2]+=s.econ->region[r].treasury;
                for (int k=0;k<CLASS_COUNT;k++) cpop[ct2]+=s.econ->region[r].strata[k].pop;
            }
            printf("              continents :");
            for (int ct2=0;ct2<w->n_continents && ct2<SCPS_MAX_CONTINENT;ct2++)
                if (cpop[ct2]>0) printf(" C%d %.0f or (%.0fk hab)", ct2, cw[ct2], cpop[ct2]/1000.0);
            printf("\n"); } }
          tot_raids+=raids; tot_loot+=loot; tot_prises+=prises; tot_navals+=navals;
          tot_disarm+=disarmed; tot_warpir+=s.dp->n_war_antipirate; tot_balafres+=balafres;
          tot_intercepts+=intercepts; tot_drowned+=drowned; }

        tot_alliances += n_alliances;
        tot_wars += war_onsets; tot_absorbed += absorbed; tot_emerged += emerged; tot_peakrev += peak_rev; tot_ages += nages;
        tot_conq += conq_prov;
        tot_ignited += s.rs->n_ignited; tot_seceded += s.rs->n_seceded; tot_coup += s.rs->n_coup;
        tot_concession += s.rs->n_concession; tot_crushed += s.rs->n_crushed; tot_revdead += s.rs->pop_lost;
        if (age_year[AGE_ORDRE_FER]>=0)   worlds_with_ironorder++;
        if (age_year[AGE_SOULEVEMENTS]>=0) worlds_with_uprising++;
        if (hash_mode) printf("HASH %u %08x\n", seed, chronicle_sim_hash(seed, &s, w));
    }

    printf("\n══════════════════════════════════════════════════════════════════════\n");
    printf(" SYNTHÈSE (%d sims × %d ans)\n", nsims, years);
    printf("   âges éveillés (total) ....... %ld   (moy. %.1f/sim)\n", tot_ages, (double)tot_ages/nsims);
    printf("   guerres déclenchées (total) . %ld   (moy. %.1f/sim)\n", tot_wars, (double)tot_wars/nsims);
    printf("   alliances actives (fin de sim) %ld   (moy. %.1f/sim ; la diplomatie respire)\n", tot_alliances, (double)tot_alliances/nsims);
    printf("   satisfaction moy (AVEC distribution) : Laborer %.0f%% · Bourgeois %.0f%% · Élite %.0f%% | commerce/an moy %.0f\n",
           tot_sat[CLASS_LABORER]/nsims, tot_sat[CLASS_BOURGEOIS]/nsims, tot_sat[CLASS_ELITE]/nsims, tot_trade/nsims);
    printf("   provinces prises de force ... %ld   (moy. %.1f/sim)\n", tot_conq, (double)tot_conq/nsims);
    printf("   pays absorbés (morts) ....... %ld   (moy. %.1f/sim)\n", tot_absorbed, (double)tot_absorbed/nsims);
    printf("   pays émergés (sécession) .... %ld   (moy. %.1f/sim ; la carte politique respire)\n", tot_emerged, (double)tot_emerged/nsims);
    printf("   nœuds de tech débloqués ..... %ld   (moy. %.1f/sim ; %ld faustiens)\n", tot_techs, (double)tot_techs/nsims, tot_faustian);
    printf("   arbre déverrouillé / empire . %ld%%   (le coût force les choix : cible < 100 %% → spécialisation)\n",
           tot_tree_sims>0? tot_tree_pct/tot_tree_sims : 0);
    printf("   relocalisations (pénurie) ... %ld   (moy. %.1f/sim ; l'IA peuple ses provinces-ressource sous-exploitées)\n",
           tot_reloc, (double)tot_reloc/nsims);
    printf("   leviers intérieurs .......... %ld matage(s) · %ld formation(s) · %ld purge(s) (%ld morts — RARE attendu)\n",
           tot_repress, tot_assim, tot_purge, tot_purge_dead);
    printf("   suzeraineté ................. %ld servage · %ld protectorat · %ld concordat · %ld cité · %ld défection(s)\n",
           tot_serv, tot_prot, tot_conc, tot_cite, tot_defect);
    printf("   fronde vassale .............. %ld ligue(s) · %ld fronde(s) → %ld indép. · %ld renversement(s) · %ld écrasée(s)  (les TROIS fins doivent exister)\n",
           tot_ligues, tot_frondes, tot_indep, tot_renvers, tot_ecrase);
    printf("   batailles dans le temps ..... %ld livrées · %.0f j/bataille · %ld déroutes · %ld décrochages · %ld renforts · %ld nuls | morts choc %ld vs POURSUITE %ld (ratio %.1fx — la poursuite doit dominer)\n",
           tot_bt, tot_bt? (double)tot_btj/tot_bt:0.0, tot_routs, tot_deseng, tot_renf, tot_nul,
           tot_mchoc, tot_mpour, tot_mchoc? (double)tot_mpour/tot_mchoc:0.0);
    printf("   syncrétisme culturel ........ %.1f nœud(s)/sim · %.1f archétype(s) distincts/sim (porte = CULTURE, plus race ; la diffusion par contact DIVERGE)\n",
           (double)tot_sync/(nsims>0?nsims:1), (double)tot_sync_distinct/(nsims>0?nsims:1));
    printf("   régions réduites (campagne) . %ld   (moy. %.1f/sim ; armées de terrain, hors conquête abstraite)\n", tot_campaign, (double)tot_campaign/nsims);
    printf("   la mer ...................... %ld coque(s) · %.0f fournitures consommées (NE doit plus être zéro) · %ld traversée(s) (%.0f j moy.) · %ld route(s) maritime(s) · %ld colonie(s) outre-mer\n",
           tot_hulls, tot_supplies, tot_sails, (tot_sails>0)?tot_saildays/(double)tot_sails:0.0, tot_searoutes, tot_colonies_om);
    printf("   la course ................... %ld raid(s) (%.0f or) · %ld bataille(s) navale(s) · %ld prise(s) · %ld désarmement(s) · %ld guerre(s) anti-piraterie · %ld balafre(s) en fin de sim\n",
           tot_raids, tot_loot, tot_navals, tot_prises, tot_disarm, tot_warpir, tot_balafres);
    printf("   l'interception .............. %ld convoi(s) coulé(s) · %ld paquet(s) noyés (le transport sans escorte est une PROIE)\n",
           tot_intercepts, tot_drowned);
    printf("   pic de révolte moyen ........ %.1f pays\n", (double)tot_peakrev/nsims);
    printf("   soulèvements incarnés ....... %ld allumés → %ld sécession(s) · %ld coup(s) · %ld concession(s) · %ld écrasé(s)\n",
           tot_ignited, tot_seceded, tot_coup, tot_concession, tot_crushed);
    printf("   corruption (le prix des concessions) : %d/%d sims avec ≥1 polity capturée · %.1f capturée(s)/sim · pire-corr moy %.0f/100\n",
           worlds_with_capture, nsims, (double)tot_captured/nsims, (double)tot_worstcorr/nsims);
    printf("   morts au combat (révoltes) .. %ld   (moy. %.0f/sim)\n", tot_revdead, (double)tot_revdead/nsims);
    printf("   sims atteignant les Soulèvements : %d/%d   l'Ordre de Fer : %d/%d\n",
           worlds_with_uprising, nsims, worlds_with_ironorder, nsims);
    printf("══════════════════════════════════════════════════════════════════════\n");

    free(w); free(s.econ); free(s.wp); free(s.wl); free(s.net); free(s.ts); free(s.sc);
    free(s.ag); free(s.ev); free(s.drift); free(s.labor); free(s.dp); free(s.rn);
    warhost_free(s.host); free(s.camp); free(s.ai); free(s.ai_on); free(s.rs); free(s.host);
    free(s.missions);   /* fuyait (6 496 o, vu par LeakSanitizer) */
    free(s.navy);
    return 0;
}
