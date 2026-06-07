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

/* ---- Diplomatie d'équilibre — surface d'équilibrage ------------------- */
#define TRUCE_BASE       (3.f*365.f)   /* trêve de base après une paix (3 ans) */
#define TRUCE_PER_YEAR   (365.f)       /* + 1 an de trêve par an de guerre menée */
#define TRUCE_MAX        (12.f*365.f)  /* plafond (une vie de génération) */
#define MOMENTUM_PER_CONQ 1.0f         /* +1 fulgurance par région prise */
#define MOMENTUM_DECAY   (1.2f/365.f)  /* la fulgurance s'oublie (~ -1.2/an) */
#define MOMENTUM_W       0.7f          /* poids de la fulgurance sur la menace */
/* Hégémon = menace qui ÉCRASE le champ (domine NETTEMENT la 2e) — critère RELATIF,
 * indépendant de l'échelle (les menaces vont de ~1 au début à ~500 en fin de partie). */
#define HEGEMON_RATIO    1.8f
#define HEGEMON_FLOOR    0.5f

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
    if (a>=0&&a<SCPS_MAX_COUNTRY&&b>=0&&b<SCPS_MAX_COUNTRY){
        /* TRÊVE : une longue guerre → une longue trêve. On ne peut redéclarer
         * avant qu'elle fonde — l'enchaînement conquête→reconquête est cassé. */
        float dur = clampf(TRUCE_BASE + TRUCE_PER_YEAR*d->war_years[a][b], 0.f, TRUCE_MAX);
        d->truce[a][b]=d->truce[b][a]=dur;
        d->war_years[a][b]=d->war_years[b][a]=0.f;
    }
}
bool diplo_can_declare(const DiploState *d,int a,int b){
    if (a<0||a>=SCPS_MAX_COUNTRY||b<0||b>=SCPS_MAX_COUNTRY) return false;
    return d->truce[a][b] <= 0.f;
}
float diplo_truce_days(const DiploState *d,int a,int b){
    if (a<0||a>=SCPS_MAX_COUNTRY||b<0||b>=SCPS_MAX_COUNTRY) return 0.f;
    return d->truce[a][b];
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
    float pop=0.f, H=0.f, arms=0.f, kit=0.f;
    for (int r=0;r<econ->n_regions;r++) if (econ->region[r].owner==cid){
        const RegionEconomy *re=&econ->region[r];
        pop += re->strata[CLASS_LABORER].pop+re->strata[CLASS_BOURGEOIS].pop+re->strata[CLASS_ELITE].pop;
        H   += re->build.H_coerc;
        arms+= re->stock[RES_ENCHANTED_ARMS];                      /* armes enchantées (Forge céleste) */
        kit += re->stock[RES_ARMS] + re->stock[RES_GUNPOWDER];     /* armes & poudre (Armurerie/Poudrière) */
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
    /* Armes & poudre de BASE : équipent la levée — rendements décroissants,
     * plafonnés plus bas que l'arcane (le fer arme, l'arcane décide). */
    float gear = 1.8f*(1.f - 1.f/(1.f + kit*0.03f));
    return sqrtf(pop)*0.04f + H + race_coerc + mart + ench + gear;
}

static float threat_of(const World *w, const WorldEconomy *econ,
                       const WorldProsperity *wp, const DiploState *d, int a, int b){
    float eco=diplo_eco_power(wp,b), mil=diplo_mil_power(w,econ,b);
    float dist=geo_dist(w,a,b);
    float infl=race_influence(w,econ,b);          /* l'influence étend la portée */
    float eff=dist/(1.f+0.10f*infl);
    float base=(eco+mil)/(eff*0.02f + 1.f);
    /* MOMENTUM : la FULGURANCE effraie plus que la masse — un empire qui snowballe
     * alarme bien plus qu'un grand empire immobile à puissance égale. */
    float momentum = (d && b<SCPS_MAX_COUNTRY) ? d->momentum[b] : 0.f;
    return base * (1.f + MOMENTUM_W*momentum);
}

Relation diplo_relation(const World *w, const WorldEconomy *econ,
                        const WorldProsperity *wp, const DiploState *d, int a, int b){
    Relation r; memset(&r,0,sizeof r);
    if (a<0||a>=w->n_countries||b<0||b>=w->n_countries||a==b) return r;

    r.threat = threat_of(w,econ,wp,d,a,b);

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
        float t=threat_of(w,econ,wp,d,a,c), u=threat_of(w,econ,wp,d,b,c);
        float m=(t<u)?t:u; if (m>shared) shared=m;
    }
    float val_dist = (ca&&cb) ? absf(ca->valeurs-cb->valeurs) : 0.f;
    float fk = r.kinship*(10.f-r.kinship)/25.f;     /* cloche sur la parenté */
    r.alliance = shared + 2.0f*r.complement + 1.0f*fk - 0.3f*val_dist - 2.0f*r.schism;
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
    re->revolt_scar = 1.0f;           /* la conquête CONVULSE : −50 % dévelop. quelques années */
    if (conqueror<SCPS_MAX_COUNTRY)
        d->momentum[conqueror] += MOMENTUM_PER_CONQ;   /* la fulgurance EFFRAIE (→ coalition) */
    legitimacy_on_conquest(wl, region);   /* L au plancher, intégration à zéro */
    return true;
}

/* ---- Diplomatie d'ÉQUILIBRE — friction & coalition -------------------- */
float diplo_war_widening_cost(const World *w, const WorldEconomy *econ,
                              const DiploState *d, int attacker, int target){
    float c=0.f;
    for (int k=0;k<w->n_countries;k++){
        if (k==attacker || k==target) continue;
        if (diplo_status(d,target,k)==DIPLO_ALLIED)     /* allié susceptible d'entrer en guerre */
            c += diplo_mil_power(w,econ,k);
    }
    return c;   /* renchérit la cible : frapper un protégé d'une puissance ÉLARGIT la guerre */
}
int diplo_perceived_hegemon(const World *w, const WorldEconomy *econ,
                            const WorldProsperity *wp, const DiploState *d, int self){
    int best=-1; float t1=0.f, t2=0.f;            /* les deux plus fortes menaces perçues */
    for (int b=0;b<w->n_countries;b++){
        if (b==self || w->country[b].role==POLITY_UNCLAIMED) continue;
        float t=threat_of(w,econ,wp,d,self,b);
        if (t>t1){ t2=t1; t1=t; best=b; } else if (t>t2) t2=t;
    }
    /* hégémon = une menace qui DOMINE nettement la suivante (aucun script : c'est la
     * lecture de menace de CHACUN ; quand un même pays domine pour plusieurs, ils se
     * comportent de facto en coalition). */
    return (best>=0 && t1 > HEGEMON_RATIO*fmaxf(t2, HEGEMON_FLOOR)) ? best : -1;
}

void diplo_tick(DiploState *d, float dt){
    for (int a=0;a<SCPS_MAX_COUNTRY;a++){
        /* la fulgurance s'oublie : un conquérant arrêté cesse d'effrayer. */
        d->momentum[a] = fmaxf(0.f, d->momentum[a] - MOMENTUM_DECAY*dt);
        for (int b=a+1;b<SCPS_MAX_COUNTRY;b++){
            if (d->status[a][b]==DIPLO_WAR){
                d->war_years[a][b]+=dt/365.f; d->war_years[b][a]=d->war_years[a][b];
            }
            if (d->truce[a][b]>0.f){            /* la trêve fond comme le revanchisme */
                d->truce[a][b]=fmaxf(0.f, d->truce[a][b]-dt);
                d->truce[b][a]=d->truce[a][b];
            }
        }
    }
}
