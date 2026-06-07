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
};

const EdificeDef *edifice_def(Edifice e){ return (e>=0&&e<EDIFICE_COUNT)?&EDIFICES[e]:NULL; }
const char       *edifice_name(Edifice e){ return (e>=0&&e<EDIFICE_COUNT)?EDIFICES[e].name:"?"; }

void agency_init(AgencyState *a){ memset(a,0,sizeof(*a)); }

bool agency_order_build(AgencyState *a, int region, Edifice e){
    if (a->n>=SCPS_MAX_BUILDS || e<0 || e>=EDIFICE_COUNT) return false;
    BuildOrder *o=&a->order[a->n++];
    o->region=region; o->type=e;
    o->days_total=EDIFICES[e].days; o->days_done=0; o->active=true;
    return true;
}

static void apply_delta(ProvBuild *b, const ProvBuild *d){
    b->K_inst  += d->K_inst;  b->H_coerc += d->H_coerc;  b->P_open += d->P_open;
    b->PE_infra+= d->PE_infra; b->food_cap += d->food_cap;
}

void agency_advance(AgencyState *a, WorldEconomy *econ, int days){
    a->day += days;
    for (int i=a->n-1; i>=0; i--){
        BuildOrder *o=&a->order[i];
        if (!o->active) continue;
        o->days_done += days;
        if (o->days_done >= o->days_total){
            if (o->region>=0 && o->region<econ->n_regions)
                apply_delta(&econ->region[o->region].build, &EDIFICES[o->type].delta);
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
