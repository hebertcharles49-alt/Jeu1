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

/* Le PORT RÉEL (mer §5) : l'édifice Port sur une côte — pas le Caravansérail. */
static bool has_true_port(const WorldEconomy *econ, int r){
    return (r>=0 && r<econ->n_regions
            && econ->region[r].build.port > 0.f && econ->region[r].coastal);
}
#define SEA_ROUTE_MAX_DAYS 60.f   /* au-delà, le partenaire est « trop loin » (surface d'équilibrage) */

bool routes_order(RouteNetwork *rn, const World *w, const WorldEconomy *econ,
                  int ra, int rb, bool maritime){
    if (rn->n>=SCPS_MAX_ROUTES) return false;
    if (ra<0||rb<0||ra>=econ->n_regions||rb>=econ->n_regions||ra==rb) return false;
    for (int i=0;i<rn->n;i++){   /* une seule route par PAIRE (sinon l'IA sature le réseau) */
        const TradeRoute *t=&rn->route[i];
        if ((t->ra==ra&&t->rb==rb)||(t->ra==rb&&t->rb==ra)) return false;
    }
    if (!econ->region[ra].culture.settled || !econ->region[rb].culture.settled) return false;
    float sea_days=0.f;
    if (maritime){
        if (!has_true_port(econ,ra) || !has_true_port(econ,rb)) return false;  /* port→port */
        if (!w) return false;
        int ax,ay,bx,by;
        if (!world_region_sea_anchor(w,ra,&ax,&ay)) return false;
        if (!world_region_sea_anchor(w,rb,&bx,&by)) return false;
        float aller=world_sea_days(w,ax,ay,bx,by);
        float retour=world_sea_days(w,bx,by,ax,ay);
        if (aller<0.f || retour<0.f) return false;            /* bassins séparés */
        sea_days=0.5f*(aller+retour);                          /* la route vit dans les DEUX sens */
        if (sea_days>SEA_ROUTE_MAX_DAYS) return false;         /* des eaux mortes le rendent « loin » */
    }
    TradeRoute *t=&rn->route[rn->n++];
    t->ra=ra; t->rb=rb; t->maritime=maritime;
    t->capacity=1.0f;
    t->days_total = maritime ? 120 : 90;   /* mer plus long (90-180 / 60-120) */
    t->days_done=0; t->open=false; t->yield=0.f;
    t->sea_days=sea_days; t->days_ab=0.f; t->days_ba=0.f;
    t->fluvial=0; t->flow=0.f; t->pirate_press=0.f;
    if (maritime && w){
        int ax,ay,bx,by;
        if (world_region_sea_anchor(w,ra,&ax,&ay) && world_region_sea_anchor(w,rb,&bx,&by)){
            t->days_ab=world_sea_days(w,ax,ay,bx,by);
            t->days_ba=world_sea_days(w,bx,by,ax,ay);
        }
    }
    /* LA VOIE D'EAU INTÉRIEURE (commerce asym. §4) : si les deux régions vivent
     * le long du MÊME fleuve, la route terrestre l'emprunte — le tracé ordonné
     * (source → mer) donne le sens ; le débit, la capacité. */
    if (!maritime && w){
        for (int rv=0; rv<w->n_rivers && !t->fluvial; rv++){
            const River *R=&w->river[rv];
            int ia=-1, ib=-1;
            for (int k=0;k<R->len;k++){
                const Cell *c=scps_cellc(w, R->x[k], R->y[k]);
                if (c->region==ra && ia<0) ia=k;
                if (c->region==rb && ib<0) ib=k;
                if (ia>=0 && ib>=0) break;
            }
            if (ia>=0 && ib>=0 && ia!=ib){
                t->fluvial = (ia<ib) ? 1 : 2;              /* le plus proche de la SOURCE est l'amont */
                t->flow    = (R->flow_max>1.f)?1.f:(R->flow_max<0.f?0.f:R->flow_max);
            }
        }
    }
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
        if (t->maritime)                                    /* mer §7 : le COURANT fait la distance */
            t->yield *= 1.f/(1.f+t->sea_days/40.f);
        if (t->pirate_press>0.f){                           /* coques §4 : la SAIGNÉE pèse sur le flux */
            if (t->pirate_press>=90.f) t->yield=0.f;        /* blocus : le lien est COUPÉ */
            else t->yield *= 1.f/(1.f+0.08f*t->pirate_press);
        }
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
