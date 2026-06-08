/*
 * scps_agency.c — actions, temps, bâtiments (voir scps_agency.h)
 *
 * Data-driven. Les durées (jours) sont calibrées pour que 250 ans soit l'arc
 * jouable : un Tribunal en ~6 mois, une Citadelle en ~6 ans. Les deltas sont
 * des coordonnées (K/H/P…), jamais des bonus plats.
 */
#include "scps_agency.h"
#include <string.h>

static const EdificeDef EDIFICES[EDIFICE_COUNT] = {
    /* {name, jours, delta, recette} — la recette monte avec le TIER : bois (palier 0)
     * → bois+métal (palier 1) → métal+précieux (palier 2). Achetée AU MARCHÉ en or. */
    /* Institutionnel → K (ce qui métabolise la distance, tient la diversité). */
    [EDI_TRIBUNAL]     = { "Tribunal",      180,  { .K_inst=1.0f }, {{RES_WOOD},{40}} },
    [EDI_CHANCELLERIE] = { "Chancellerie",  365,  { .K_inst=1.5f }, {{RES_WOOD,RES_METAL},{50,25}} },
    [EDI_ACADEMIE]     = { "Académie",      1800, { .K_inst=1.5f, .P_open=0.5f }, {{RES_METAL,RES_PRECIOUS_METAL},{60,40}} },  /* §7 : tier 3 → métal-préc 15→40 */
    /* Coercitif → H (tient l'ordre par la force — ronge L, voie fragile). */
    [EDI_GARNISON]     = { "Garnison",      180,  { .H_coerc=1.0f }, {{RES_WOOD,RES_METAL},{40,20}} },
    [EDI_FORTERESSE]   = { "Forteresse",    1100, { .H_coerc=2.0f }, {{RES_WOOD,RES_METAL},{60,50}} },
    [EDI_CITADELLE]    = { "Citadelle",     2200, { .H_coerc=3.0f }, {{RES_METAL,RES_TOOLS},{100,30}} },
    /* Ouverture → P (porte d'assimilation, contact, routes maritimes). */
    [EDI_PORT]         = { "Port",          540,  { .P_open=1.0f }, {{RES_WOOD,RES_METAL},{80,20}} },
    [EDI_CARAVANSERAIL]= { "Caravansérail", 365,  { .P_open=0.7f }, {{RES_WOOD},{45}} },
    /* Prospérité → PE local (capte le carrefour). */
    [EDI_MARCHE]       = { "Marché",        180,  { .PE_infra=1.0f }, {{RES_WOOD},{35}} },
    [EDI_ENTREPOT]     = { "Entrepôt",      270,  { .PE_infra=0.7f }, {{RES_WOOD},{45}} },
    /* Croissance → food (nourrit la pop ; l'aqueduc : santé urbaine → croissance). */
    [EDI_GRENIER]      = { "Grenier",       90,   { .food_cap=1.0f }, {{RES_WOOD},{25}} },
    [EDI_IRRIGATION]   = { "Irrigation",    270,  { .food_cap=1.5f }, {{RES_WOOD,RES_METAL},{30,15}} },
    [EDI_AQUEDUC]      = { "Aqueduc",       540,  { .food_cap=1.2f }, {{RES_WOOD,RES_METAL},{30,40}} },
    /* Foi → SOUTIENT L (sacraliser le trône apaise sans réprimer — §4 du catalogue). */
    [EDI_SANCTUAIRE]   = { "Sanctuaire",    150,  { .faith=1.0f }, {{RES_WOOD},{30}} },
    [EDI_TEMPLE]       = { "Temple",        600,  { .faith=2.0f }, {{RES_WOOD,RES_METAL},{50,30}} },
    [EDI_CATHEDRALE]   = { "Cathédrale",    2000, { .faith=3.5f }, {{RES_METAL,RES_PRECIOUS_METAL},{70,40}} },  /* §7 : tier 3 → métal-préc 25→40 */
    /* Savoir → recherche (le monastère sacralise ET étudie — §5 du catalogue). */
    [EDI_BIBLIOTHEQUE] = { "Bibliothèque",  500,  { .savoir=1.5f }, {{RES_WOOD,RES_METAL},{40,20}} },
    [EDI_MONASTERE]    = { "Monastère",     900,  { .savoir=1.0f, .faith=1.0f }, {{RES_WOOD,RES_METAL},{50,15}} },
    /* Commerce → PE local (capte le flux ; la banque finance l'État). */
    [EDI_COMPTOIR]     = { "Comptoir",      200,  { .PE_infra=0.8f }, {{RES_WOOD},{30}} },
    [EDI_BANQUE]       = { "Banque",        700,  { .PE_infra=1.4f }, {{RES_METAL,RES_PRECIOUS_METAL},{40,40}} },  /* §7 : tier 3 → métal-préc 20→40 */
};

const EdificeDef *edifice_def(Edifice e){ return (e>=0&&e<EDIFICE_COUNT)?&EDIFICES[e]:NULL; }
const char       *edifice_name(Edifice e){ return (e>=0&&e<EDIFICE_COUNT)?EDIFICES[e].name:"?"; }

/* ---- Coût des bâtiments (§1) : matériaux ACHETÉS au marché en or ------- */
#define BUILD_MIN_PRICE 0.20f   /* plancher de prix : même un bien abondant n'est jamais gratuit */

/* §7 — l'ÉTENDUE du pays RENCHÉRIT ses institutions (le frein tall/wide qui manquait) :
 * facteur ×(1 + 0.15·n_régions du pays) sur le coût matériaux. Un grand empire paie
 * ses édifices bien plus cher — sa croissance institutionnelle se paie. */
static float agency_extent_mult(const WorldEconomy *econ, int region){
    int owner = econ->region[region].owner;
    if (owner < 0) return 1.f;
    int n=0;
    for (int r=0;r<econ->n_regions;r++) if (econ->region[r].owner==owner) n++;
    return 1.f + 0.15f*(float)n;
}

float agency_build_gold(const WorldEconomy *econ, int region, Edifice e){
    if (e<0||e>=EDIFICE_COUNT || !econ || region<0 || region>=econ->n_regions) return 0.f;
    const RegionEconomy *re=&econ->region[region];
    const BuildCost *c=&EDIFICES[e].cost;
    float gold=0.f;
    for (int k=0;k<BUILD_RES_MAX;k++){
        Resource r=c->res[k];
        if (r<=RES_NONE || r>=RES_COUNT || c->qty[k]<=0.f) continue;
        float price = re->price[r]; if (price < BUILD_MIN_PRICE) price = BUILD_MIN_PRICE;
        gold += c->qty[k] * price;       /* le manque renchérit : la rareté monte le prix */
    }
    return gold * agency_extent_mult(econ, region);   /* §7 : indexé sur l'étendue du pays */
}

bool agency_build(AgencyState *a, WorldEconomy *econ, int region, Edifice e){
    if (e<0||e>=EDIFICE_COUNT || !econ || region<0 || region>=econ->n_regions) return false;
    RegionEconomy *re=&econ->region[region];
    float gold = agency_build_gold(econ, region, e);
    if (gold > re->treasury) return false;        /* pas l'or → pas de chantier (garde, comme colonize) */
    re->treasury -= gold;                          /* on PAIE le marché en or */
    const BuildCost *c=&EDIFICES[e].cost;          /* … et l'on CONSOMME les matériaux du marché */
    float mult = agency_extent_mult(econ, region); /* §7 : un grand pays consomme plus */
    for (int k=0;k<BUILD_RES_MAX;k++){
        Resource r=c->res[k];
        if (r<=RES_NONE || r>=RES_COUNT || c->qty[k]<=0.f) continue;
        re->stock[r] -= c->qty[k]*mult; if (re->stock[r] < 0.f) re->stock[r]=0.f;
    }
    return agency_order_build(a, region, e);       /* enfile le chantier (durée existante) */
}

/* Constantes des actions non-bâtiment (calibrables). */
#define CLEAR_DAYS        200
#define EXPLOIT_DAYS      180
#define CLEAR_FOOD_GAIN   1.5f
#define CLEAR_SUBS_TARGET 6.0f    /* mode de vie agricole (FARMER) */
#define CLEAR_SUBS_SHIFT  0.40f   /* fraction du chemin à l'achèvement (le reste dérive) */
#define CLEAR_L_HIT       2.0f
#define EXPLOIT_CAP_GAIN  3.0f

void agency_init(AgencyState *a){ memset(a,0,sizeof(*a)); }

static bool enqueue(AgencyState *a, ActionKind k, int region, int param, int days){
    if (a->n>=SCPS_MAX_BUILDS) return false;
    BuildOrder *o=&a->order[a->n++];
    o->kind=k; o->region=region; o->param=param;
    o->days_total=days; o->days_done=0; o->active=true;
    return true;
}
bool agency_order_build(AgencyState *a, int region, Edifice e){
    if (e<0||e>=EDIFICE_COUNT) return false;
    return enqueue(a, AGY_BUILD, region, (int)e, EDIFICES[e].days);
}
bool agency_order_clear(AgencyState *a, int region){
    return enqueue(a, AGY_CLEAR, region, 0, CLEAR_DAYS);
}
bool agency_order_exploit(AgencyState *a, int region, Resource res){
    if (res<=RES_NONE||res>=RES_COUNT) return false;
    return enqueue(a, AGY_EXPLOIT, region, (int)res, EXPLOIT_DAYS);
}

static void apply_delta(ProvBuild *b, const ProvBuild *d){
    b->K_inst  += d->K_inst;  b->H_coerc += d->H_coerc;  b->P_open += d->P_open;
    b->PE_infra+= d->PE_infra; b->food_cap += d->food_cap;
}

static void apply_action(WorldEconomy *econ, WorldLegitimacy *wl, const BuildOrder *o){
    int reg=o->region;
    if (reg<0 || reg>=econ->n_regions) return;
    RegionEconomy *re=&econ->region[reg];
    switch (o->kind){
        case AGY_BUILD:
            apply_delta(&re->build, &EDIFICES[(Edifice)o->param].delta);
            break;
        case AGY_CLEAR:
            re->build.food_cap += CLEAR_FOOD_GAIN;
            /* dérive du substrat vers l'agriculture (impérialisme sur la terre) */
            re->culture.subsistance += (CLEAR_SUBS_TARGET - re->culture.subsistance)*CLEAR_SUBS_SHIFT;
            /* niche forestière (chasseurs/horticulteurs) : leur monde rasé → L↓ */
            if ((re->culture.lifeway==LIFE_HUNTER || re->culture.lifeway==LIFE_HORTICULTURE)
                && wl && reg<SCPS_MAX_REG)
                wl->L[reg] = (wl->L[reg]>CLEAR_L_HIT) ? wl->L[reg]-CLEAR_L_HIT : 0.f;
            break;
        case AGY_EXPLOIT:
            if (o->param>RES_NONE && o->param<RES_COUNT)
                re->raw_cap[o->param] += EXPLOIT_CAP_GAIN;
            break;
    }
}

void agency_advance(AgencyState *a, World *w, WorldEconomy *econ,
                    WorldLegitimacy *wl, int days){
    (void)w;
    a->day += days;
    for (int i=a->n-1; i>=0; i--){
        BuildOrder *o=&a->order[i];
        if (!o->active) continue;
        o->days_done += days;
        if (o->days_done >= o->days_total){
            apply_action(econ, wl, o);
            a->order[i]=a->order[--a->n];   /* achevé : swap-remove */
        }
    }
}

int agency_active_in_region(const AgencyState *a, int region){
    int n=0;
    for (int i=0;i<a->n;i++) if (a->order[i].active && a->order[i].region==region) n++;
    return n;
}
int agency_year(const AgencyState *a){ return a->day / SCPS_DAYS_PER_YEAR; }
