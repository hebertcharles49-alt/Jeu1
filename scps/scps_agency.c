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
    /* Institutionnel → K (ce qui métabolise la distance, tient la diversité). */
    [EDI_TRIBUNAL]     = { "Tribunal",      180,  { .K_inst=1.0f } },
    [EDI_CHANCELLERIE] = { "Chancellerie",  365,  { .K_inst=1.5f } },
    [EDI_ACADEMIE]     = { "Académie",      1800, { .K_inst=1.5f, .P_open=0.5f } },
    /* Coercitif → H (tient l'ordre par la force — ronge L, voie fragile). */
    [EDI_GARNISON]     = { "Garnison",      180,  { .H_coerc=1.0f } },
    [EDI_FORTERESSE]   = { "Forteresse",    1100, { .H_coerc=2.0f } },
    [EDI_CITADELLE]    = { "Citadelle",     2200, { .H_coerc=3.0f } },
    /* Ouverture → P (porte d'assimilation, contact, routes maritimes). */
    [EDI_PORT]         = { "Port",          540,  { .P_open=1.0f } },
    [EDI_CARAVANSERAIL]= { "Caravansérail", 365,  { .P_open=0.7f } },
    /* Prospérité → PE local (capte le carrefour). */
    [EDI_MARCHE]       = { "Marché",        180,  { .PE_infra=1.0f } },
    [EDI_ENTREPOT]     = { "Entrepôt",      270,  { .PE_infra=0.7f } },
    /* Croissance → food (nourrit la pop). */
    [EDI_GRENIER]      = { "Grenier",       90,   { .food_cap=1.0f } },
    [EDI_IRRIGATION]   = { "Irrigation",    270,  { .food_cap=1.5f } },
    /* Foi → SOUTIENT L (sacraliser le trône apaise sans réprimer — §4 du catalogue). */
    [EDI_SANCTUAIRE]   = { "Sanctuaire",    150,  { .faith=1.0f } },
    [EDI_TEMPLE]       = { "Temple",        600,  { .faith=2.0f } },
};

const EdificeDef *edifice_def(Edifice e){ return (e>=0&&e<EDIFICE_COUNT)?&EDIFICES[e]:NULL; }
const char       *edifice_name(Edifice e){ return (e>=0&&e<EDIFICE_COUNT)?EDIFICES[e].name:"?"; }

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
