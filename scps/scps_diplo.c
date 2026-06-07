/*
 * scps_diplo.c — diplomatie & guerre (voir scps_diplo.h)
 *
 * Tout est LECTEUR : la menace, la complémentarité, la parenté, le schisme se
 * lisent sur des coordonnées existantes. La conquête déplace l'owner ; la
 * diversité (et la fracture) en découle dans le moteur d'ordre.
 */
#include "scps_diplo.h"
#include "scps_species.h"
#include "scps_culture.h"
#include <string.h>
#include <math.h>

static inline float clampf(float v,float lo,float hi){return v<lo?lo:(v>hi?hi:v);}
static inline float absf(float v){return v<0?-v:v;}

void diplo_init(DiploState *d){ memset(d,0,sizeof(*d)); }

DiploStatus diplo_status(const DiploState *d, int a, int b){
    if (a<0||a>=SCPS_MAX_COUNTRY||b<0||b>=SCPS_MAX_COUNTRY) return DIPLO_NEUTRAL;
    return d->status[a][b];
}
static void set_sym(DiploState *d, int a, int b, DiploStatus s){
    if (a<0||a>=SCPS_MAX_COUNTRY||b<0||b>=SCPS_MAX_COUNTRY||a==b) return;
    d->status[a][b]=d->status[b][a]=s;
}
void diplo_declare_war  (DiploState *d,int a,int b){ set_sym(d,a,b,DIPLO_WAR); }
void diplo_form_alliance(DiploState *d,int a,int b){ set_sym(d,a,b,DIPLO_ALLIED); }
void diplo_make_peace   (DiploState *d,int a,int b){
    set_sym(d,a,b,DIPLO_NEUTRAL);
    if (a>=0&&a<SCPS_MAX_COUNTRY&&b>=0&&b<SCPS_MAX_COUNTRY)
        d->war_years[a][b]=d->war_years[b][a]=0.f;
}

/* ---- accesseurs ------------------------------------------------------- */
static const PopCulture *cap_culture(const World *w, const WorldEconomy *econ, int cid){
    if (cid<0||cid>=w->n_countries) return NULL;
    int cp=w->country[cid].capital_prov;
    if (cp<0||cp>=w->n_provinces) return NULL;
    int cr=w->province[cp].region;
    if (cr<0||cr>=econ->n_regions) return NULL;
    return &econ->region[cr].culture;
}
static unsigned country_res_mask(const World *w, const WorldEconomy *econ, int cid){
    unsigned m=0;
    for (int r=0;r<econ->n_regions;r++) if (econ->region[r].owner==cid)
        for (int res=1;res<RES_PROD_FIRST && res<32;res++)
            if (econ->region[r].raw_cap[res] > 0.f) m |= (1u<<res);
    (void)w; return m;
}
static int popcount(unsigned x){ int n=0; while(x){n+=x&1u;x>>=1;} return n; }
static float geo_dist(const World *w, int a, int b){
    int pa=w->country[a].capital_prov, pb=w->country[b].capital_prov;
    if (pa<0||pb<0||pa>=w->n_provinces||pb>=w->n_provinces) return 1e6f;
    float dx=(float)(w->province[pa].seed_x-w->province[pb].seed_x);
    float dy=(float)(w->province[pa].seed_y-w->province[pb].seed_y);
    return sqrtf(dx*dx+dy*dy);
}
static float race_influence(const World *w, const WorldEconomy *econ, int cid){
    const PopCulture *pc=cap_culture(w,econ,cid);
    if (!pc) return 0.f;
    SpeciesBuild sb=species_default_build(pc->race);
    return build_leviers(&sb).influence;
}

/* ---- lecteurs --------------------------------------------------------- */
float diplo_eco_power(const WorldProsperity *wp, int cid){
    if (cid<0||cid>=wp->n_countries) return 0.f;
    return wp->country[cid].P_realise;
}
float diplo_mil_power(const World *w, const WorldEconomy *econ, int cid){
    float pop=0.f, H=0.f, arms=0.f;
    for (int r=0;r<econ->n_regions;r++) if (econ->region[r].owner==cid){
        const RegionEconomy *re=&econ->region[r];
        pop += re->strata[CLASS_LABORER].pop+re->strata[CLASS_BOURGEOIS].pop+re->strata[CLASS_ELITE].pop;
        H   += re->build.H_coerc;
        arms+= re->stock[RES_ENCHANTED_ARMS];   /* armes enchantées (Forge céleste) */
    }
    const PopCulture *pc=cap_culture(w,econ,cid);
    float race_coerc=0.f, mart=0.f;
    if (pc){
        SpeciesBuild sb=species_default_build(pc->race);
        race_coerc=build_leviers(&sb).coercition;
        if (pc->martial==MART_HORDE_MONTEE||pc->martial==MART_LEVEE_MASSIVE||
            pc->martial==MART_THALASSO_PREDATRICE) mart=0.7f;   /* traditions offensives */
    }
    /* Les armes enchantées sont un MULTIPLICATEUR de qualité (l'arcane nourrit la
     * guerre) — rendements décroissants, plafonnés. */
    float ench = 3.0f*(1.f - 1.f/(1.f + arms*0.05f));
    return sqrtf(pop)*0.04f + H + race_coerc + mart + ench;
}

static float threat_of(const World *w, const WorldEconomy *econ,
                       const WorldProsperity *wp, int a, int b){
    float eco=diplo_eco_power(wp,b), mil=diplo_mil_power(w,econ,b);
    float dist=geo_dist(w,a,b);
    float infl=race_influence(w,econ,b);          /* l'influence étend la portée */
    float eff=dist/(1.f+0.10f*infl);
    return (eco+mil)/(eff*0.02f + 1.f);
}

Relation diplo_relation(const World *w, const WorldEconomy *econ,
                        const WorldProsperity *wp, const DiploState *d, int a, int b){
    Relation r; memset(&r,0,sizeof r);
    if (a<0||a>=w->n_countries||b<0||b>=w->n_countries||a==b) return r;

    r.threat = threat_of(w,econ,wp,a,b);

    unsigned ra=country_res_mask(w,econ,a), rb=country_res_mask(w,econ,b);
    int uni=popcount(ra|rb), inter=popcount(ra&rb);
    r.complement = uni>0 ? (float)(uni-inter)/(float)uni : 0.f;

    const PopCulture *ca=cap_culture(w,econ,a), *cb=cap_culture(w,econ,b);
    r.kinship = (ca&&cb) ? sphere_distance(species_sphere(ca->race),species_sphere(cb->race)) : 0.f;

    if (ca&&cb){
        bool same_branch=(ca->rel_branch==cb->rel_branch);
        bool both_zeal  =(ca->credo!=CREDO_PLURALISTE && cb->credo!=CREDO_PLURALISTE);
        float dr=absf(ca->religion-cb->religion);
        r.schism = (same_branch&&both_zeal) ? clampf(1.f - dr/5.f, 0.f, 1.f) : 0.f;
    }

    /* alliance = menace partagée (transitoire) + λ·complément + μ·f(parenté)
     *          − ν·distance de valeurs − ξ·schisme. */
    float shared=0.f;
    for (int c=0;c<w->n_countries;c++) if (c!=a&&c!=b){
        float t=threat_of(w,econ,wp,a,c), u=threat_of(w,econ,wp,b,c);
        float m=(t<u)?t:u; if (m>shared) shared=m;
    }
    float val_dist = (ca&&cb) ? absf(ca->valeurs-cb->valeurs) : 0.f;
    float fk = r.kinship*(10.f-r.kinship)/25.f;     /* cloche sur la parenté */
    r.alliance = shared + 2.0f*r.complement + 1.0f*fk - 0.3f*val_dist - 2.0f*r.schism;
    (void)d;
    return r;
}

/* ---- guerre : conquête ------------------------------------------------ */
bool diplo_conquer_region(DiploState *d, World *w, WorldEconomy *econ,
                          WorldLegitimacy *wl, int conqueror, int region){
    if (conqueror<0||conqueror>=w->n_countries) return false;
    if (region<0||region>=econ->n_regions) return false;
    RegionEconomy *re=&econ->region[region];
    if (!re->culture.settled) return false;          /* rien à conquérir */
    int defender=re->owner;
    if (defender==conqueror) return false;           /* déjà à nous */
    if (defender>=0 && diplo_status(d,conqueror,defender)!=DIPLO_WAR) return false;
    re->owner = conqueror;            /* transfert : la diversité suit (compute_profile) */
    re->colonized = true;
    legitimacy_on_conquest(wl, region);   /* L au plancher, intégration à zéro */
    return true;
}

void diplo_tick(DiploState *d, float dt){
    for (int a=0;a<SCPS_MAX_COUNTRY;a++) for (int b=a+1;b<SCPS_MAX_COUNTRY;b++)
        if (d->status[a][b]==DIPLO_WAR){
            d->war_years[a][b]+=dt/365.f; d->war_years[b][a]=d->war_years[a][b];
        }
}
