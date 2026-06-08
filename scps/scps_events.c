/*
 * scps_events.c — évènements, chocs géo & âges (voir scps_events.h)
 *
 * Rien d'aléatoire hors-sol : les chocs RELISENT la géo (relief tectonique,
 * rivières, pluie, routes), les évènements lisent la FICHE, les âges lisent
 * l'ÉTAT du monde. Les effets déplacent des coordonnées/métriques ; les textes
 * sont diégétiques (aucun nom SCPS).
 */
#include "scps_events.h"
#include <string.h>
#include <math.h>

/* ===================================================================== */
/* Le bundle de pointeurs systèmes passé aux triggers/effets             */
/* ===================================================================== */
struct EventCtx {
    EventsState     *ev;
    World           *w;
    WorldEconomy    *econ;
    WorldLegitimacy *wl;
    WorldProsperity *wp;
    Statecraft      *sc;
    RouteNetwork    *rn;
    const TechState *ts;
};

/* ---- Utilitaires ------------------------------------------------------ */
static inline float clampf(float v,float lo,float hi){ return v<lo?lo:(v>hi?hi:v); }
static inline float absf(float v){ return v<0?-v:v; }
static uint32_t xs32(uint32_t *s){ uint32_t x=*s; x^=x<<13; x^=x>>17; x^=x<<5; return *s=x?x:1u; }
static float frand(uint32_t *s){ return (float)(xs32(s)&0xffffffu)/(float)0x1000000u; }

static float pc_dist(const PopCulture *a, const PopCulture *b){
    float dv=absf(a->valeurs-b->valeurs), ds=absf(a->subsistance-b->subsistance);
    float dp=absf(a->parente-b->parente), dr=absf(a->religion-b->religion);
    float m=dv; if(ds>m)m=ds; if(dp>m)m=dp; if(dr>m)m=dr; return m;
}
static int cap_region(const World *w, int cid){
    if (cid<0||cid>=w->n_countries) return -1;
    int cp=w->country[cid].capital_prov;
    return (cp>=0&&cp<w->n_provinces)?w->province[cp].region:-1;
}
static const PopCulture *ruling_culture(const World *w, const WorldEconomy *econ, int cid){
    int cr=cap_region(w,cid);
    return (cr>=0&&cr<econ->n_regions)?&econ->region[cr].culture:NULL;
}

/* ===================================================================== */
/* events_init — RELIRE la géo du worldgen en agrégats par région        */
/* ===================================================================== */
static float relief_weight(Biome b){
    switch(b){
        case BIO_VOLCANO:   return 1.4f;   /* faille active : le plus tremblant */
        case BIO_PEAK:      return 1.2f;
        case BIO_MOUNTAINS: return 1.0f;
        case BIO_HIGHLANDS: return 0.6f;
        case BIO_HILLS:     return 0.35f;
        default:            return 0.f;
    }
}
static bool is_forest(Biome b){ return b==BIO_FOREST||b==BIO_WOODS||b==BIO_JUNGLE||b==BIO_MANGROVE; }
static bool is_arid (Biome b){ return b==BIO_DESERT||b==BIO_DRYLANDS||b==BIO_SAVANNA||b==BIO_STEPPE||b==BIO_COASTAL_DESERT; }
static bool is_lowland(Biome b){ return b==BIO_PLAINS||b==BIO_FARMLAND||b==BIO_GRASSLAND||b==BIO_MARSH||b==BIO_MANGROVE||b==BIO_BOG; }

#define AGE_DAWN_YEARS 20   /* l'âge de l'Aube (base) dure les 20 premières années */
#define AGE_MIN_YEARS  30   /* puis un âge par génération (30 ans) entre avènements */

void events_init(EventsState *ev, const World *w, uint32_t seed){
    memset(ev,0,sizeof(*ev));
    ev->rng = seed ? seed : 0xA17F23C5u;
    ev->ages.research_mult = 1.f;
    ev->ages.integration_mult = 1.f;
    ev->ages.last_dawned = -1;
    /* L'Aube dure 20 ans : le 1er âge ne peut s'éveiller avant l'an 20
     * (gate : an ≥ last_dawn_year + 30, donc last init = 20 − 30 = −10). */
    ev->ages.last_dawn_year = AGE_DAWN_YEARS - AGE_MIN_YEARS;
    ev->last_id = -1; ev->last_name = NULL;

    /* accumulateurs par région */
    double sr_rain[SCPS_MAX_REG]={0}, sr_temp[SCPS_MAX_REG]={0}, sr_relief[SCPS_MAX_REG]={0};
    int    rmax_river[SCPS_MAX_REG]={0}, nforest[SCPS_MAX_REG]={0}, narid[SCPS_MAX_REG]={0}, nlow[SCPS_MAX_REG]={0};

    for (int i=0;i<SCPS_N;i++){
        const Cell *c=&w->cell[i];
        int r=c->region;
        if (r<0||r>=SCPS_MAX_REG) continue;
        RegionGeo *g=&ev->geo[r];
        g->cells++;
        sr_rain[r]   += c->rainfall;
        sr_temp[r]   += c->temperature;
        sr_relief[r] += relief_weight(c->biome);
        if (c->river > rmax_river[r]) rmax_river[r]=c->river;
        if (is_forest(c->biome)) nforest[r]++;
        if (is_arid(c->biome))   narid[r]++;
        if (is_lowland(c->biome))nlow[r]++;
    }
    for (int r=0;r<SCPS_MAX_REG;r++){
        RegionGeo *g=&ev->geo[r];
        if (g->cells<=0) continue;
        float n=(float)g->cells;
        g->rainfall = (float)(sr_rain[r]/n);
        g->temp     = (float)(sr_temp[r]/n);
        g->relief   = clampf((float)(sr_relief[r]/n), 0.f, 1.f);
        g->river01  = (float)rmax_river[r]/255.f;
        g->forest   = (float)nforest[r]/n;
        g->arid     = (float)narid[r]/n;
        g->lowland  = (float)nlow[r]/n;
    }
}

/* ===================================================================== */
/* LECTEURS DE RISQUE — relisent la géo ; 0 = la géographie l'interdit    */
/* ===================================================================== */
float events_quake_risk(const EventsState *ev, int region){
    if (region<0||region>=SCPS_MAX_REG) return 0.f;
    return clampf(ev->geo[region].relief, 0.f, 1.f);          /* la tectonique = le relief */
}
float events_flood_risk(const EventsState *ev, int region){
    if (region<0||region>=SCPS_MAX_REG) return 0.f;
    const RegionGeo *g=&ev->geo[region];
    return clampf(g->river01 * (0.35f + 0.65f*g->rainfall) * (0.3f + 0.7f*g->lowland), 0.f, 1.f);
}
float events_drought_risk(const EventsState *ev, int region){
    if (region<0||region>=SCPS_MAX_REG) return 0.f;
    const RegionGeo *g=&ev->geo[region];
    float dry = (1.f - g->rainfall) * (0.5f + 0.5f*g->temp);
    return clampf(dry * (0.35f + 0.65f*g->arid), 0.f, 1.f);
}
float events_fire_risk(const EventsState *ev, int region){
    if (region<0||region>=SCPS_MAX_REG) return 0.f;
    const RegionGeo *g=&ev->geo[region];
    float dry = (1.f - g->rainfall) * (0.5f + 0.5f*g->temp);
    return clampf(g->forest * dry, 0.f, 1.f);
}
static float shock_risk(const EventsState *ev, int region, int shock){
    switch(shock){
        case EVID_QUAKE:   return events_quake_risk(ev,region);
        case EVID_FLOOD:   return events_flood_risk(ev,region);
        case EVID_DROUGHT: return events_drought_risk(ev,region);
        case EVID_FIRE:    return events_fire_risk(ev,region);
        default:           return 0.f;
    }
}

/* ===================================================================== */
/* TRIGGERS — chocs (géo) & politiques (fiche)                           */
/* ===================================================================== */
static bool trig_quake  (const EventCtx *cx,int r){ return events_quake_risk  (cx->ev,r)>0.08f; }
static bool trig_flood  (const EventCtx *cx,int r){ return events_flood_risk  (cx->ev,r)>0.08f; }
static bool trig_drought(const EventCtx *cx,int r){ return events_drought_risk(cx->ev,r)>0.10f; }
static bool trig_fire   (const EventCtx *cx,int r){ return events_fire_risk   (cx->ev,r)>0.10f; }

/* Base commune de l'évènement d'intégration : une région avalée, lointaine,
 * fraîchement tenue, dont l'agitation monte. */
static bool integ_base(const EventCtx *cx, int r){
    if (r<0||r>=cx->econ->n_regions) return false;
    const RegionEconomy *re=&cx->econ->region[r];
    if (!re->culture.settled || re->owner<0) return false;
    const PopCulture *rul=ruling_culture(cx->w,cx->econ,re->owner);
    if (!rul) return false;
    float dinf = pc_dist(&re->culture, rul);
    float yh   = (r<SCPS_MAX_REG)?cx->wl->years_held[r]:50.f;
    float agit = (cx->sc && r<SCPS_MAX_REG)?cx->sc->agitation[r]:0.f;
    return dinf > 6.f && yh < 30.f && agit > 40.f;
}
static Ethos  owner_ethos (const EventCtx *cx,int r){ const PopCulture*p=ruling_culture(cx->w,cx->econ,cx->econ->region[r].owner); return p?p->ethos:ETHOS_ORDRE; }
static Sphere owner_sphere(const EventCtx *cx,int r){ const PopCulture*p=ruling_culture(cx->w,cx->econ,cx->econ->region[r].owner); return p?species_sphere(p->race):SPHERE_HOMMES; }

static bool trig_integ_dom(const EventCtx *cx,int r){
    if (!integ_base(cx,r)) return false;
    Ethos e=owner_ethos(cx,r);
    return e==ETHOS_DOMINATEUR||e==ETHOS_HONNEUR;
}
static bool trig_integ_merc(const EventCtx *cx,int r){
    return integ_base(cx,r) && owner_ethos(cx,r)==ETHOS_MERCANTILE;
}
static bool trig_integ_bur(const EventCtx *cx,int r){
    if (!integ_base(cx,r)) return false;
    Ethos e=owner_ethos(cx,r);
    return e==ETHOS_BUREAUCRATE||e==ETHOS_ORDRE;
}
static bool trig_integ_anc(const EventCtx *cx,int r){
    return integ_base(cx,r) && owner_sphere(cx,r)==SPHERE_ANCIENS;
}

/* Succession (pays) : une couronne mal assise. */
static bool trig_succession(const EventCtx *cx,int c){
    if(c<0||c>=cx->w->n_countries) return false;
    if(cx->wp && c<cx->wp->n_countries) return cx->wp->country[c].L < 5.0f;
    return false;
}
/* Schisme (pays) : un credo zélé sur une foi qui diverge. */
static bool trig_schism(const EventCtx *cx,int c){
    const PopCulture *rul=ruling_culture(cx->w,cx->econ,c);
    if(!rul) return false;
    if(rul->credo==CREDO_PLURALISTE) return false;
    /* divergence religieuse interne : une région du pays s'écarte de la foi du trône */
    for(int r=0;r<cx->econ->n_regions;r++) if(cx->econ->region[r].owner==c && cx->econ->region[r].culture.settled){
        if(absf(cx->econ->region[r].culture.religion - rul->religion) > 4.f) return true;
    }
    return false;
}

/* ===================================================================== */
/* LA TABLE D'ÉVÈNEMENTS (effets = coordonnées ; textes = mots)           */
/* ===================================================================== */
static const EventDef EVENTS[EVID_COUNT] = {
    [EVID_QUAKE] = { EVID_QUAKE, EV_PROVINCE, "La terre tremble", trig_quake, 8000.f, NULL,
        {{ "—", "Le sol se déchire : édifices effondrés, vies fauchées, ordre ébranlé.",
           { .d_K_inst=-1.5f, .d_agitation=12.f, .pop_mult=0.90f, .d_treasury=-20.f, .unlock_branch=-1 }, 1.f }}, 1 },
    [EVID_FLOOD] = { EVID_FLOOD, EV_PROVINCE, "Les eaux débordent", trig_flood, 5000.f, NULL,
        {{ "—", "La crue emporte des récoltes — mais le limon laisse une terre plus grasse.",
           { .d_food_cap=1.5f, .d_agitation=6.f, .pop_mult=0.97f, .d_treasury=-10.f, .unlock_branch=-1 }, 1.f }}, 1 },
    [EVID_DROUGHT] = { EVID_DROUGHT, EV_PROVINCE, "La sécheresse s'installe", trig_drought, 6000.f, NULL,
        {{ "—", "La pluie se refuse ; les greniers se vident et la colère monte.",
           { .d_food_cap=-1.5f, .d_agitation=15.f, .pop_mult=0.98f, .d_treasury=-10.f, .unlock_branch=-1 }, 1.f }}, 1 },
    [EVID_FIRE] = { EVID_FIRE, EV_PROVINCE, "Le feu court sur la forêt", trig_fire, 7000.f, NULL,
        {{ "—", "Les flammes ravagent les bois — et ouvrent, de force, des terres à la charrue.",
           { .d_food_cap=0.8f, .d_agitation=5.f, .pop_mult=0.97f, .unlock_branch=-1 }, 1.f }}, 1 },
    [EVID_PLAGUE] = { EVID_PLAGUE, EV_PROVINCE, "La peste remonte les routes", NULL, 12000.f, NULL,
        {{ "—", "Le mal voyage avec les marchands : il frappe d'autant plus fort les grands carrefours ouverts.",
           { .d_agitation=18.f, .pop_mult=0.78f, .d_treasury=-15.f, .unlock_branch=-1 }, 1.f }}, 1 },

    /* ---- Intégration des marches : MÊME état, quatre récits par la fiche ---- */
    [EVID_INTEG_DOMINATEUR] = { EVID_INTEG_DOMINATEUR, EV_PROVINCE, "Les marges osent gronder",
        trig_integ_dom, 900.f, NULL, {
        { "Mater dans le sang", "On brise la révolte par la terreur : l'ordre revient, mais la haine reste.",
          { .d_H_coerc=2.0f, .d_L=-1.0f, .d_coercion=0.5f, .d_agitation=-40.f, .unlock_branch=-1 }, 0.7f },
        { "Lever une horde punitive", "Une chevauchée de représailles ravage les insolents.",
          { .d_H_coerc=1.0f, .d_agitation=-22.f, .pop_mult=0.98f, .unlock_branch=-1 }, 0.3f } }, 2 },
    [EVID_INTEG_MERCANTILE] = { EVID_INTEG_MERCANTILE, EV_PROVINCE, "Le comptoir réclame son autonomie",
        trig_integ_merc, 900.f, NULL, {
        { "Affranchir le commerce", "On lâche du contrôle contre la paix : la cité respire et s'enrichit.",
          { .d_food_cap=0.6f, .d_L=0.5f, .d_coercion=-0.3f, .d_agitation=-30.f, .unlock_branch=-1 }, 0.6f },
        { "Acheter les meneurs", "Quelques bourses bien placées éteignent la fronde.",
          { .d_treasury=-50.f, .d_agitation=-25.f, .unlock_branch=-1 }, 0.4f } }, 2 },
    [EVID_INTEG_BUREAUCRATE] = { EVID_INTEG_BUREAUCRATE, EV_PROVINCE, "Une province mal arrimée",
        trig_integ_bur, 900.f, NULL, {
        { "Réforme d'intégration", "On dépêche des fonctionnaires : lent, mais le droit finit par lier.",
          { .d_L=1.5f, .d_agitation=-20.f, .unlock_branch=-1 }, 0.65f },
        { "Garnison", "Des baïonnettes tiennent ce que le droit n'a pas encore lié.",
          { .d_H_coerc=1.5f, .d_agitation=-15.f, .unlock_branch=-1 }, 0.35f } }, 2 },
    [EVID_INTEG_ANCIEN] = { EVID_INTEG_ANCIEN, EV_PROVINCE, "La longue mémoire des conquis",
        trig_integ_anc, 900.f, NULL, {
        { "Patience des siècles", "On ne fait rien d'éclatant : le temps, lentement, ploie les mémoires.",
          { .d_L=0.3f, .d_agitation=-10.f, .unlock_branch=-1 }, 0.6f },
        { "Concession rituelle", "Un geste sacré apaise — au prix d'un peu de prestige.",
          { .d_L=0.8f, .d_influence=-5.f, .d_agitation=-12.f, .unlock_branch=-1 }, 0.4f } }, 2 },

    /* ---- Génériques (légitimité / credo) ---- */
    [EVID_SUCCESSION] = { EVID_SUCCESSION, EV_COUNTRY, "La couronne vacante",
        trig_succession, 6000.f, NULL, {
        { "Transition ordonnée", "Les grands s'accordent : la passation se fait sans verser le sang.",
          { .d_L=0.6f, .d_influence=3.f, .unlock_branch=-1 }, 0.5f },
        { "Régence contestée", "Deux partis se disputent le trône ; le royaume retient son souffle.",
          { .d_L=-1.0f, .d_agitation=15.f, .unlock_branch=-1 }, 0.5f } }, 2 },
    [EVID_SCHISM] = { EVID_SCHISM, EV_COUNTRY, "Une foi se déchire",
        trig_schism, 7000.f, NULL, {
        { "Tolérer les deux rites", "On laisse coexister les chapelles : la paix, au prix de l'unité.",
          { .d_agitation=-10.f, .d_L=-0.3f, .unlock_branch=-1 }, 0.5f },
        { "Imposer l'orthodoxie", "Un seul rite, par la force s'il le faut — et l'hérésie gronde.",
          { .d_H_coerc=1.0f, .d_coercion=0.3f, .d_agitation=10.f, .unlock_branch=-1 }, 0.5f } }, 2 },
};

const EventDef *event_def(int evid){ return (evid>=0&&evid<EVID_COUNT)?&EVENTS[evid]:NULL; }

/* ===================================================================== */
/* APPLICATION DES EFFETS (déplace des coordonnées/métriques)            */
/* ===================================================================== */
static void apply_region_eff(EventCtx *cx, int r, const EvEffect *e){
    if (r<0||r>=cx->econ->n_regions) return;
    RegionEconomy *re=&cx->econ->region[r];
    re->build.K_inst  = fmaxf(0.f, re->build.K_inst  + e->d_K_inst);
    re->build.H_coerc = fmaxf(0.f, re->build.H_coerc + e->d_H_coerc);
    re->build.food_cap= fmaxf(0.f, re->build.food_cap+ e->d_food_cap);
    re->coercion      = clampf(re->coercion + e->d_coercion, 0.f, 1.f);
    re->treasury      = fmaxf(0.f, re->treasury + e->d_treasury);
    if (e->pop_mult>0.f && e->pop_mult!=1.f)
        for (int k=0;k<CLASS_COUNT;k++) re->strata[k].pop *= e->pop_mult;
    if (cx->wl && r<SCPS_MAX_REG) cx->wl->L[r]=clampf(cx->wl->L[r]+e->d_L,0.f,10.f);
    if (cx->sc && r<SCPS_MAX_REG) cx->sc->agitation[r]=clampf(cx->sc->agitation[r]+e->d_agitation,0.f,100.f);
}
static void apply_effect(EventCtx *cx, EvScope scope, int subject, const EvEffect *e){
    if (scope==EV_WORLD){
        if (cx->wp){
            cx->wp->age_C_bonus     = clampf(cx->wp->age_C_bonus + e->d_C_global, 0.f, 5.f);
            cx->wp->age_breach_flux = clampf(cx->wp->age_breach_flux + e->d_breach, 0.f, 10.f);
        }
        cx->ev->ages.breach_pressure = clampf(cx->ev->ages.breach_pressure + e->d_breach, 0.f, 10.f);
        if (e->unlock_branch>=0 && e->unlock_branch<THM_COUNT && e->unlock_tier>=0 && e->unlock_tier<8)
            cx->ev->ages.tier_open[e->unlock_branch][e->unlock_tier]=true;
        return;
    }
    if (scope==EV_PROVINCE){
        apply_region_eff(cx, subject, e);
        int cid = (subject>=0&&subject<cx->econ->n_regions)?cx->econ->region[subject].owner:-1;
        if (cid>=0 && cx->sc && cid<cx->sc->n_countries)
            cx->sc->influence[cid]=clampf(cx->sc->influence[cid]+e->d_influence,0.f,100.f);
    } else { /* EV_COUNTRY : cœur à la capitale, humeur (L/agitation) au pays entier */
        int cid=subject, capr=cap_region(cx->w,cid);
        if (capr>=0) apply_region_eff(cx, capr, e);
        for (int r=0;r<cx->econ->n_regions;r++) if (r!=capr && cx->econ->region[r].owner==cid){
            if (cx->wl && r<SCPS_MAX_REG) cx->wl->L[r]=clampf(cx->wl->L[r]+e->d_L,0.f,10.f);
            if (cx->sc && r<SCPS_MAX_REG) cx->sc->agitation[r]=clampf(cx->sc->agitation[r]+e->d_agitation,0.f,100.f);
        }
        if (cid>=0 && cx->sc && cid<cx->sc->n_countries)
            cx->sc->influence[cid]=clampf(cx->sc->influence[cid]+e->d_influence,0.f,100.f);
    }
}

/* Choix de l'option par l'IA (poids ai_chance) puis application + journal. */
static void fire_event(EventCtx *cx, int evid, int subject){
    const EventDef *d=&EVENTS[evid];
    int best=0; float bw=-1.f;
    for (int i=0;i<d->n_options;i++) if (d->options[i].ai_chance>bw){ bw=d->options[i].ai_chance; best=i; }
    apply_effect(cx, d->scope, subject, &d->options[best].eff);
    cx->ev->last_id=evid; cx->ev->last_name=d->name; cx->ev->n_fired++;
}

/* ===================================================================== */
/* events_strike — applique un choc géo précis (déterministe)            */
/* ===================================================================== */
void events_strike(EventsState *ev, World *w, WorldEconomy *econ,
                   WorldLegitimacy *wl, WorldProsperity *wp, Statecraft *sc,
                   int region, EvId shock){
    EventCtx cx={ev,w,econ,wl,wp,sc,NULL,NULL};
    if (shock<0||shock>=EVID_COUNT) return;
    apply_effect(&cx, EVENTS[shock].scope, region, &EVENTS[shock].options[0].eff);
    ev->last_id=shock; ev->last_name=EVENTS[shock].name; ev->n_fired++;
}

/* ===================================================================== */
/* PESTE — diffusion BFS le long des ROUTES ouvertes                     */
/* ===================================================================== */
int events_plague_spread(EventsState *ev, World *w, WorldEconomy *econ,
                         WorldLegitimacy *wl, Statecraft *sc, RouteNetwork *rn,
                         int seed_region){
    if (!rn || seed_region<0 || seed_region>=econ->n_regions) return 0;
    int hop[SCPS_MAX_REG]; for (int i=0;i<SCPS_MAX_REG;i++) hop[i]=-1;
    int queue[SCPS_MAX_REG], qh=0, qt=0;
    hop[seed_region]=0; queue[qt++]=seed_region;
    int infected=0;
    while (qh<qt){
        int r=queue[qh++];
        int h=hop[r];
        /* sévérité décroît avec la distance le long des routes */
        float mult = (h==0)?0.78f:(h==1)?0.85f:(h==2)?0.90f:0.95f;
        EvEffect e = EVENTS[EVID_PLAGUE].options[0].eff;
        e.pop_mult = mult;
        e.d_agitation = 18.f - 3.f*h;
        EventCtx cx={ev,w,econ,wl,NULL,sc,rn,NULL};
        apply_effect(&cx, EV_PROVINCE, r, &e);
        infected++;
        if (h>=4) continue;                         /* portée bornée */
        for (int i=0;i<rn->n;i++){
            const TradeRoute *t=&rn->route[i];
            if (!t->open) continue;
            int other=-1;
            if (t->ra==r) other=t->rb; else if (t->rb==r) other=t->ra;
            if (other<0||other>=SCPS_MAX_REG) continue;
            if (hop[other]<0){ hop[other]=h+1; queue[qt++]=other; }
        }
    }
    ev->last_id=EVID_PLAGUE; ev->last_name=EVENTS[EVID_PLAGUE].name;
    return infected;
}

/* ===================================================================== */
/* events_match_political — la VARIANTE par la fiche (priorité éthos)     */
/* ===================================================================== */
int events_match_political(const EventsState *ev, World *w, WorldEconomy *econ,
                           WorldLegitimacy *wl, Statecraft *sc, int region){
    EventCtx cx={ (EventsState*)ev, w, econ, wl, NULL, sc, NULL, NULL };
    static const int order[4]={ EVID_INTEG_DOMINATEUR, EVID_INTEG_MERCANTILE,
                                EVID_INTEG_BUREAUCRATE, EVID_INTEG_ANCIEN };
    for (int i=0;i<4;i++){
        const EventDef *d=&EVENTS[order[i]];
        if (d->trigger && d->trigger(&cx, region)) return order[i];
    }
    return -1;
}

/* ===================================================================== */
/* ÂGES — une LECTURE du monde fait advenir l'ère                        */
/* ===================================================================== */
#define NODE_VALUE_Y   1.0f    /* une région « riche » : route_pe au-dessus de ça */
#define X_NODES        4       /* … et il en faut X pour l'Âge du Commerce         */
#define LUM_TOTAL_Y   25.f     /* Lumière mondiale cumulée → Âge de la Raison      */
#define EMPIRES_INTEG  10      /* régions bien intégrées → Âge des Empires         */
#define BREACH_CHARGE  5.f     /* charge faustienne quelque part → Âge de la Brèche*/
/* ---- Âges structurels : seuils & poussées (la surface d'équilibrage) ---- */
#define LUM_SAVOIR_Y  40.f     /* société de MASSE : savoir mondial cumulé          */
#define LUM_C_Y        4.f     /* … ET connectée (les idées circulent)              */
#define MASSE_CRITIQUE 2       /* pays en révolution → contagion des Soulèvements   */
#define OF_FRAC_Y      3.0f    /* monde qui se fracture …                            */
#define OF_DEREAL_Y    1.0f    /* … sous charge faustienne …                         */
#define OF_SI_CRISE    5.0f    /* … en crise ouverte → l'Ordre de Fer répond         */
#define AGE_DELTA_I    2.0f    /* Lumières : surgissement des idées (+ I)            */
#define AGE_DELTA_SOLV 2.0f    /* Lumières : dissolution de la légitimité coercitive */
#define AGE_DELTA_L    2.0f    /* Soulèvements : la légitimité ne porte plus (− L)   */
#define AGE_DELTA_H    2.5f    /* Ordre de Fer : la poigne (+ H)                     */
#define AGE_DELTA_MYTH 3.0f    /* Ordre de Fer : le mythe nie la diversité (− D̄)     */

/* Verdict du moteur, mode « révolution » = SCPS_SUBMERGE_REVOLUTION (miroir, on
 * n'inclut PAS scps_core : on lit l'int déjà stocké dans CountryProsperity). */
enum { EV_MODE_CONSENTI=0, EV_MODE_COERC_FRAGILE, EV_MODE_REVOLUTION, EV_MODE_SECESSION };

static const char *AGE_NAMES[AGE_COUNT]={
    "Âge du Commerce Mondial","Âge de la Raison","Âge des Empires","Âge de la Brèche",
    "Âge des Lumières","Âge des Soulèvements","Âge de l'Ordre de Fer"
};
const char *age_name(AgeId a){ return (a>=0&&a<AGE_COUNT)?AGE_NAMES[a]:"?"; }

/* ---- Lecteurs de l'état AGRÉGÉ du monde (sur les pays non-vierges) ------ */
static int nc_loop(const World *w, const WorldProsperity *wp){
    return (w->n_countries < wp->n_countries) ? w->n_countries : wp->n_countries;
}
static float w_total_savoir(const World *w, const WorldProsperity *wp){
    float s=0.f;
    for (int c=0;c<nc_loop(w,wp);c++)
        if (w->country[c].role!=POLITY_UNCLAIMED) s+=wp->country[c].Lumiere;
    return s;
}
static float w_mean_C(const World *w, const WorldProsperity *wp){
    float s=0.f; int n=0;
    for (int c=0;c<nc_loop(w,wp);c++)
        if (w->country[c].role!=POLITY_UNCLAIMED){ s+=wp->country[c].C; n++; }
    return n? s/(float)n : 0.f;
}
static float w_mean_fracture(const World *w, const WorldProsperity *wp){
    float s=0.f; int n=0;
    for (int c=0;c<nc_loop(w,wp);c++)
        if (w->country[c].role!=POLITY_UNCLAIMED){ s+=wp->country[c].fracture; n++; }
    return n? s/(float)n : 0.f;
}
static float w_mean_dereal(const World *w, const WorldProsperity *wp){
    float s=0.f; int n=0;
    for (int c=0;c<nc_loop(w,wp);c++)
        if (w->country[c].role!=POLITY_UNCLAIMED){ s+=wp->country[c].dereal; n++; }
    return n? s/(float)n : 0.f;
}
static float w_mean_SI(const World *w, const WorldProsperity *wp){
    float s=0.f; int n=0;
    for (int c=0;c<nc_loop(w,wp);c++)
        if (w->country[c].role!=POLITY_UNCLAIMED){ s+=wp->country[c].SI; n++; }
    return n? s/(float)n : 10.f;
}
int events_count_revolutionary(const World *w, const WorldProsperity *wp){
    int n=0;
    for (int c=0;c<nc_loop(w,wp);c++)
        if (w->country[c].role!=POLITY_UNCLAIMED && wp->country[c].mode==EV_MODE_REVOLUTION) n++;
    return n;
}
/* Le mythe homogénéisant se diffuse : la foi du trône devient exclusive. */
static void spread_credo_purificateur(World *w, WorldEconomy *econ){
    for (int c=0;c<w->n_countries;c++){
        int cr=cap_region(w,c);
        if (cr>=0 && cr<econ->n_regions && econ->region[cr].culture.settled
            && econ->region[cr].culture.credo==CREDO_PLURALISTE)
            econ->region[cr].culture.credo=CREDO_PURIFICATEUR;
    }
}

static bool age_trig_commerce(World *w, WorldEconomy *econ, WorldProsperity *wp, const TechState ts[]){
    (void)w;(void)wp;(void)ts; int rich=0;
    for (int r=0;r<econ->n_regions;r++) if (econ->region[r].route_pe > NODE_VALUE_Y) rich++;
    return rich >= X_NODES;
}
static bool age_trig_reason(World *w, WorldEconomy *econ, WorldProsperity *wp, const TechState ts[]){
    (void)w;(void)econ;(void)ts; if(!wp) return false; float lum=0.f;
    for (int c=0;c<wp->n_countries;c++) lum += wp->country[c].Lumiere;
    return lum > LUM_TOTAL_Y;
}
static bool age_trig_empires(World *w, WorldEconomy *econ, WorldProsperity *wp, const TechState ts[],
                             WorldLegitimacy *wl){
    (void)w;(void)wp;(void)ts; if(!wl) return false; int integ=0;
    for (int r=0;r<econ->n_regions;r++)
        if (econ->region[r].owner>=0 && econ->region[r].culture.settled
            && r<SCPS_MAX_REG && wl->years_held[r] > 40.f) integ++;
    return integ >= EMPIRES_INTEG;
}
static bool age_trig_breach(World *w, WorldEconomy *econ, WorldProsperity *wp, const TechState ts[]){
    (void)w;(void)econ;(void)wp; if(!ts) return false;
    for (int c=0;c<SCPS_MAX_COUNTRY;c++) if (ts[c].charge > BREACH_CHARGE) return true;
    return false;
}
/* ---- Âges STRUCTURELS (chaîne causale : Lumières d'abord) --------------- */
static bool age_trig_lumieres(World *w, WorldProsperity *wp){
    if (!wp) return false;
    return w_total_savoir(w,wp) > LUM_SAVOIR_Y && w_mean_C(w,wp) > LUM_C_Y;
}
static bool age_trig_soulevements(const EventsState *ev, World *w, WorldProsperity *wp){
    if (!ev->ages.dawned[AGE_LUMIERES] || !wp) return false;       /* précondition causale */
    return events_count_revolutionary(w,wp) >= MASSE_CRITIQUE;     /* verdict du moteur */
}
static bool age_trig_ordrefer(const EventsState *ev, World *w, WorldProsperity *wp){
    if (!ev->ages.dawned[AGE_LUMIERES] || !wp) return false;       /* société de masse */
    return w_mean_fracture(w,wp) > OF_FRAC_Y
        && w_mean_dereal(w,wp)   > OF_DEREAL_Y
        && w_mean_SI(w,wp)       < OF_SI_CRISE;                    /* crise ouverte */
}

static void age_dawn(EventsState *ev, AgeId a, World *w, WorldEconomy *econ, WorldProsperity *wp){
    EventCtx cx={ev,w,econ,NULL,wp,NULL,NULL,NULL};
    EvEffect e; memset(&e,0,sizeof e); e.pop_mult=1.f; e.unlock_branch=-1;
    switch(a){
        case AGE_COMMERCE: e.d_C_global=1.0f; e.unlock_branch=THM_SOCIETE; e.unlock_tier=3; break;
        case AGE_REASON:   ev->ages.research_mult += 0.5f; e.unlock_branch=THM_SOCIETE; e.unlock_tier=4; break;
        case AGE_EMPIRES:  ev->ages.integration_mult += 0.5f; e.unlock_branch=THM_SOCIETE; e.unlock_tier=5; break;
        case AGE_BREACH:   e.d_breach=2.0f; e.unlock_branch=THM_SAVOIR; e.unlock_tier=5; break;
        /* Structurels : on déplace une ENTRÉE GLOBALE du moteur (le verdict suit). */
        case AGE_LUMIERES:
            if (wp){ wp->age_I_bonus += AGE_DELTA_I;            /* les idées surgissent */
                     wp->age_lumiere_solvent += AGE_DELTA_SOLV; }/* la légitimité coercitive se dissout */
            e.unlock_branch=THM_SOCIETE; e.unlock_tier=4;       /* le boon : le savoir */
            break;
        case AGE_SOULEVEMENTS: if(wp) wp->age_L_penalty += AGE_DELTA_L; break;   /* L ↓ partout (contagion) */
        case AGE_ORDRE_FER:
            if (wp){ wp->age_H_bonus      += AGE_DELTA_H;          /* la poigne */
                     wp->age_myth_homogen += AGE_DELTA_MYTH; }     /* le mythe nie la diversité */
            if (w && econ) spread_credo_purificateur(w,econ);
            break;
        default: break;
    }
    apply_effect(&cx, EV_WORLD, 0, &e);
    ev->ages.dawned[a]=true; ev->ages.last_dawned=(int)a;
    ev->ages.last_dawn_year = ev->ages.days_elapsed/365;   /* horodate l'avènement */
}

bool events_check_ages(EventsState *ev, World *w, WorldEconomy *econ,
                       WorldProsperity *wp, WorldLegitimacy *wl, const TechState ts[]){
    bool any=false;
    /* Rythme : UN SEUL âge par fenêtre de 30 ans, rien avant l'an 30. La barrière
     * est RÉÉVALUÉE après chaque avènement (last_dawn_year bouge) → les âges
     * s'égrènent une génération à la fois, jamais en grappe. */
    #define AGE_GATE (ev->ages.days_elapsed/365 >= ev->ages.last_dawn_year + AGE_MIN_YEARS)
    if (AGE_GATE && !ev->ages.dawned[AGE_COMMERCE] && age_trig_commerce(w,econ,wp,ts)){ age_dawn(ev,AGE_COMMERCE,w,econ,wp); any=true; }
    if (AGE_GATE && !ev->ages.dawned[AGE_REASON]   && age_trig_reason  (w,econ,wp,ts)){ age_dawn(ev,AGE_REASON,w,econ,wp);   any=true; }
    /* Lumières AVANT les âges politiques (la société de masse d'abord). */
    if (AGE_GATE && !ev->ages.dawned[AGE_LUMIERES] && age_trig_lumieres(w,wp))        { age_dawn(ev,AGE_LUMIERES,w,econ,wp); any=true; }
    if (AGE_GATE && !ev->ages.dawned[AGE_EMPIRES]  && age_trig_empires (w,econ,wp,ts,wl)){ age_dawn(ev,AGE_EMPIRES,w,econ,wp);any=true; }
    if (AGE_GATE && !ev->ages.dawned[AGE_BREACH]   && age_trig_breach  (w,econ,wp,ts)){ age_dawn(ev,AGE_BREACH,w,econ,wp);   any=true; }
    /* Politiques : exigent que les Lumières aient eu lieu (causalité). */
    if (AGE_GATE && !ev->ages.dawned[AGE_SOULEVEMENTS] && age_trig_soulevements(ev,w,wp)){ age_dawn(ev,AGE_SOULEVEMENTS,w,econ,wp); any=true; }
    if (AGE_GATE && !ev->ages.dawned[AGE_ORDRE_FER]    && age_trig_ordrefer   (ev,w,wp)){ age_dawn(ev,AGE_ORDRE_FER,w,econ,wp);    any=true; }
    #undef AGE_GATE
    return any;
}
bool  ages_dawned(const EventsState *ev, AgeId a){ return (a>=0&&a<AGE_COUNT)?ev->ages.dawned[a]:false; }
bool  ages_tier_open(const EventsState *ev, TechTheme br, int tier){
    return (br>=0&&br<THM_COUNT&&tier>=0&&tier<8)?ev->ages.tier_open[br][tier]:false;
}
float ages_breach_pressure(const EventsState *ev){ return ev->ages.breach_pressure; }

/* ===================================================================== */
/* LA BOUCLE (§5)                                                        */
/* ===================================================================== */
static float mtth_p(float mtth_days, int days){
    if (mtth_days<1.f) mtth_days=1.f;
    return 1.f - expf(-(float)days/mtth_days);
}
void world_events_tick(EventsState *ev, World *w, WorldEconomy *econ,
                       WorldLegitimacy *wl, WorldProsperity *wp, Statecraft *sc,
                       RouteNetwork *rn, const TechState ts[], int days){
    EventCtx cx={ev,w,econ,wl,wp,sc,rn,ts};
    ev->ages.days_elapsed += days;          /* horloge de jeu (rythme des âges) */

    /* 1. CHOCS GÉO — à risque, par région, sur leur cadence (1/risk accélère). */
    static const int SHOCKS[4]={EVID_QUAKE,EVID_FLOOD,EVID_DROUGHT,EVID_FIRE};
    for (int r=0;r<econ->n_regions;r++){
        if (!econ->region[r].culture.settled) continue;
        for (int si=0;si<4;si++){
            int s=SHOCKS[si];
            float risk=shock_risk(ev,r,s);
            if (risk<0.06f) continue;                       /* la géo l'interdit ici */
            float mtth=EVENTS[s].mtth_days / risk;          /* fréquent là où le risque est haut */
            if (frand(&ev->rng) < mtth_p(mtth,days)) events_strike(ev,w,econ,wl,wp,sc,r,s);
        }
    }
    /* Peste : foyer rare sur le plus grand carrefour ouvert, puis diffusion. */
    {
        int hub=-1; float best=NODE_VALUE_Y;
        for (int r=0;r<econ->n_regions;r++) if (econ->region[r].route_pe>best){ best=econ->region[r].route_pe; hub=r; }
        if (hub>=0 && frand(&ev->rng) < mtth_p(EVENTS[EVID_PLAGUE].mtth_days, days))
            events_plague_spread(ev,w,econ,wl,sc,rn,hub);
    }

    /* 2. ÉVÈNEMENTS POLITIQUES/CULTURELS — la fiche sélectionne la variante. */
    for (int r=0;r<econ->n_regions;r++){
        if (!econ->region[r].culture.settled) continue;
        int evid=events_match_political(ev,w,econ,wl,sc,r);
        if (evid<0) continue;
        if (frand(&ev->rng) < mtth_p(EVENTS[evid].mtth_days, days)) fire_event(&cx, evid, r);
    }
    for (int c=0;c<w->n_countries;c++){
        if (EVENTS[EVID_SUCCESSION].trigger(&cx,c) &&
            frand(&ev->rng) < mtth_p(EVENTS[EVID_SUCCESSION].mtth_days,days)) fire_event(&cx,EVID_SUCCESSION,c);
        if (EVENTS[EVID_SCHISM].trigger(&cx,c) &&
            frand(&ev->rng) < mtth_p(EVENTS[EVID_SCHISM].mtth_days,days)) fire_event(&cx,EVID_SCHISM,c);
    }

    /* 3. ÂGES — scan d'interprétation du monde. */
    events_check_ages(ev,w,econ,wp,wl,ts);

    /* Résorption TRANSITOIRE de l'amplification structurelle : la crise aiguë
     * retombe à mesure que les pays tranchent (réforme / poigne / effondrement)
     * — résolution émergente, pas un minuteur. Les acquis (palier, credo) restent. */
    if (wp){
        float k = clampf(0.0004f*(float)days, 0.f, 1.f);
        wp->age_I_bonus         -= wp->age_I_bonus         * k;
        wp->age_lumiere_solvent -= wp->age_lumiere_solvent * k;
        wp->age_L_penalty       -= wp->age_L_penalty       * k;
        wp->age_H_bonus         -= wp->age_H_bonus         * k;
        wp->age_myth_homogen    -= wp->age_myth_homogen    * k;
    }
}

/* ===================================================================== */
/* GARDE-FOU MEMBRANE — aucun nom SCPS dans les textes joueur            */
/* ===================================================================== */
bool events_text_clean(void){
    static const char *BANNED[]={ "SCPS","D∞","∞","K_inst","H_coerc","P_realise",
        "fragilit","fractur","flux_faustien","age_C","breach_flux","D_bar", NULL };
    const char *texts[256]; int n=0;
    for (int i=0;i<EVID_COUNT;i++){
        texts[n++]=EVENTS[i].name;
        for (int o=0;o<EVENTS[i].n_options;o++){ texts[n++]=EVENTS[i].options[o].label; texts[n++]=EVENTS[i].options[o].blurb; }
    }
    for (int a=0;a<AGE_COUNT;a++) texts[n++]=AGE_NAMES[a];
    for (int i=0;i<n;i++){
        const char *s=texts[i]; if(!s) continue;
        for (int b=0;BANNED[b];b++) if (strstr(s,BANNED[b])) return false;
    }
    return true;
}
