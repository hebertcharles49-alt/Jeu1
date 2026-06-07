/*
 * scps_routes.c — routes commerciales (voir scps_routes.h)
 *
 * Le rendement EST la cloche f(D̄) du moteur, faite action : on commerce le
 * mieux avec un partenaire à distance intermédiaire, et la porte se ferme aux
 * extrêmes (sauf forte ouverture P/port).
 */
#include "scps_routes.h"
#include <string.h>
#include <math.h>

static inline float clampf(float v,float lo,float hi){return v<lo?lo:(v>hi?hi:v);}
static inline float absf(float v){return v<0?-v:v;}

void routes_init(RouteNetwork *rn){ memset(rn,0,sizeof(*rn)); }

static bool has_port(const WorldEconomy *econ, int r){
    return (r>=0 && r<econ->n_regions && econ->region[r].build.P_open > 0.f);
}

bool routes_order(RouteNetwork *rn, const WorldEconomy *econ,
                  int ra, int rb, bool maritime){
    if (rn->n>=SCPS_MAX_ROUTES) return false;
    if (ra<0||rb<0||ra>=econ->n_regions||rb>=econ->n_regions||ra==rb) return false;
    if (!econ->region[ra].culture.settled || !econ->region[rb].culture.settled) return false;
    if (maritime && !has_port(econ,ra) && !has_port(econ,rb)) return false;  /* port requis */
    TradeRoute *t=&rn->route[rn->n++];
    t->ra=ra; t->rb=rb; t->maritime=maritime;
    t->capacity=1.0f;
    t->days_total = maritime ? 120 : 90;   /* mer plus long (90-180 / 60-120) */
    t->days_done=0; t->open=false; t->yield=0.f;
    return true;
}

/* Distance de CONTENU (L∞) entre les cultures de deux régions. */
static float route_dbar(const WorldEconomy *econ, int ra, int rb){
    const PopCulture *a=&econ->region[ra].culture, *b=&econ->region[rb].culture;
    float dv=absf(a->valeurs-b->valeurs), ds=absf(a->subsistance-b->subsistance);
    float dp=absf(a->parente-b->parente), dr=absf(a->religion-b->religion);
    float m=dv; if(ds>m)m=ds; if(dp>m)m=dp; if(dr>m)m=dr; return m;
}

void routes_advance(RouteNetwork *rn, const World *w, WorldEconomy *econ, int days){
    (void)w;
    for (int r=0;r<econ->n_regions;r++) econ->region[r].route_pe=0.f;
    for (int i=0;i<rn->n;i++){
        TradeRoute *t=&rn->route[i];
        if (!t->open){
            t->days_done+=days;
            if (t->days_done>=t->days_total) t->open=true;
        }
        if (!t->open){ t->yield=0.f; continue; }
        if (t->ra>=econ->n_regions||t->rb>=econ->n_regions){ t->yield=0.f; continue; }

        float dbar=route_dbar(econ,t->ra,t->rb);
        float bell=dbar*(10.f-dbar)/25.f;                 /* CLOCHE : pic à D̄=5 */
        /* Porte : l'ouverture (ports/caravansérails) ouvre aux partenaires
         * lointains. P effectif = base + P_open moyen des deux bouts. */
        float Pavg=5.f + 0.5f*(econ->region[t->ra].build.P_open+econ->region[t->rb].build.P_open);
        float gate=1.f/(1.f+expf(-(0.8f*(Pavg-dbar))));
        t->yield = t->capacity * 10.f * bell * gate;       /* échelle ~ PE */
        econ->region[t->ra].route_pe += t->yield;
        econ->region[t->rb].route_pe += t->yield;
    }
}

float routes_pe_for_region(const RouteNetwork *rn, int region){
    float s=0.f;
    for (int i=0;i<rn->n;i++) if (rn->route[i].open &&
        (rn->route[i].ra==region||rn->route[i].rb==region)) s+=rn->route[i].yield;
    return s;
}
int routes_count_for_region(const RouteNetwork *rn, int region){
    int n=0;
    for (int i=0;i<rn->n;i++) if (rn->route[i].open &&
        (rn->route[i].ra==region||rn->route[i].rb==region)) n++;
    return n;
}
