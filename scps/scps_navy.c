/*
 * scps_navy.c — LA FLOTTE : trois coques + conversion, chantier, entretien,
 * colonisation outre-mer (briefs mer §5/§8 & coques §2).
 *
 * La flotte est LE consommateur de RES_NAVAL_SUPPLIES (chaîne bois → Scierie
 * navale → fournitures → coques) : l'achat suit le patron d'agency_build —
 * l'or paie le marché, le stock se consomme, la demande se REGISTRE (le marché
 * voit la flotte et la production tire).
 */
#include "scps_navy.h"
#include <string.h>
#include <math.h>

static inline float clampf(float v,float lo,float hi){ return v<lo?lo:(v>hi?hi:v); }

/* ── Surface d'équilibrage ──────────────────────────────────────────────── */
#define NAVY_MIN_PRICE       0.5f     /* plancher de prix (même rôle que BUILD_MIN_PRICE) */
#define NAVY_UPKEEP_WAR      1.5f     /* fournitures / an / coque                  */
#define NAVY_UPKEEP_OTHER    0.8f
#define NAVY_STARVE_YEAR     365.f    /* > 1 an sans entretien : une coque pourrit/an */
#define NAVY_COLONY_MAX_DAYS 90.f     /* le seuil de traversée colonisatrice (jours) */
#define NAVY_COLONY_CD       (2.f*365.f)  /* une colonie outre-mer / 2 ans / pays   */
#define NAVY_TRANSPORT_PKTS  10       /* 1 transport = 10 paquets = 1 000 hommes   */

typedef struct { float supplies, wood, metal; int days; } HullCost;
static const HullCost HULLS[HULL_COUNT]={
    [HULL_WAR]      ={ 30.f, 40.f, 25.f, 420 },
    [HULL_TRANSPORT]={ 20.f, 30.f,  0.f, 270 },
    [HULL_MERCHANT] ={ 15.f, 25.f,  0.f, 200 },
    [HULL_PIRATE]   ={  6.f,  8.f,  0.f,  60 },   /* la CONVERSION coûte peu : c'est sa nature */
};
int navy_hull_crew(HullType t){ return (t==HULL_WAR)?NAVY_CREW_WAR:NAVY_CREW_LIGHT; }
const char *navy_hull_name(HullType t){
    static const char *N[HULL_COUNT]={"navire de combat","transport","marchand","pirate"};
    return (t>=0&&t<HULL_COUNT)?N[t]:"?";
}

void navy_init(NavyState *ns){
    memset(ns,0,sizeof *ns);
    for (int c=0;c<SCPS_MAX_COUNTRY;c++){
        ns->n[c].build_hull=-1; ns->n[c].home_port=-1;
        ns->n[c].mission_target=-1; ns->n[c].nest_region=-1;
    }
}

/* Une région est côtière si l'une de ses provinces touche la mer. */
static bool region_coastal(const World *w, int region){
    if (region<0 || region>=w->n_regions) return false;
    const Region *rg=&w->region[region];
    for (int k=0;k<rg->n_provinces;k++){
        int p=rg->province_ids[k];
        if (p>=0 && p<w->n_provinces && w->province[p].coastal) return true;
    }
    return false;
}
bool navy_region_is_port(const World *w, const WorldEconomy *econ, int region){
    if (region<0 || region>=econ->n_regions) return false;
    return econ->region[region].build.port>0.f && region_coastal(w,region);
}
int navy_best_port(const World *w, const WorldEconomy *econ, int cid){
    if (cid<0 || cid>=w->n_countries) return -1;
    int cap_reg=-1;
    { int cp=w->country[cid].capital_prov;
      if (cp>=0 && cp<w->n_provinces) cap_reg=w->province[cp].region; }
    if (cap_reg>=0 && navy_region_is_port(w,econ,cap_reg)
        && econ->region[cap_reg].owner==cid) return cap_reg;
    int best=-1; float bpop=-1.f;
    for (int r=0;r<econ->n_regions;r++){
        const RegionEconomy *re=&econ->region[r];
        if (re->owner!=cid || !navy_region_is_port(w,econ,r)) continue;
        float pop=0.f; for (int c=0;c<CLASS_COUNT;c++) pop+=re->strata[c].pop;
        if (pop>bpop){ bpop=pop; best=r; }
    }
    return best;
}

float navy_build_gold(const WorldEconomy *econ, int region, HullType t){
    if (t<0||t>=HULL_COUNT||region<0||region>=econ->n_regions) return 0.f;
    const RegionEconomy *re=&econ->region[region];
    const HullCost *h=&HULLS[t];
    float gold=0.f, p;
    p=re->price[RES_NAVAL_SUPPLIES]; if (p<NAVY_MIN_PRICE) p=NAVY_MIN_PRICE; gold+=h->supplies*p;
    p=re->price[RES_WOOD];           if (p<NAVY_MIN_PRICE) p=NAVY_MIN_PRICE; gold+=h->wood*p;
    if (h->metal>0.f){ p=re->price[RES_METAL]; if (p<NAVY_MIN_PRICE) p=NAVY_MIN_PRICE; gold+=h->metal*p; }
    return gold;
}

bool navy_order_build(NavyState *ns, const World *w, WorldEconomy *econ, int cid, HullType t){
    if (t<0||t>=HULL_COUNT||cid<0||cid>=SCPS_MAX_COUNTRY) return false;
    Navy *n=&ns->n[cid];
    if (n->build_hull>=0) return false;                  /* un chantier à la fois */
    int port=navy_best_port(w,econ,cid);
    if (port<0) return false;                            /* un pays sans port ne bâtit rien */
    RegionEconomy *re=&econ->region[port];
    float gold=navy_build_gold(econ,port,t);
    if (gold>re->treasury) return false;
    if (re->strata[CLASS_LABORER].pop < (float)navy_hull_crew(t)+200.f) return false;  /* pas les bras */
    const HullCost *h=&HULLS[t];
    re->treasury-=gold;
    re->stock[RES_NAVAL_SUPPLIES]-=h->supplies; if (re->stock[RES_NAVAL_SUPPLIES]<0.f) re->stock[RES_NAVAL_SUPPLIES]=0.f;
    re->stock[RES_WOOD]          -=h->wood;     if (re->stock[RES_WOOD]<0.f)           re->stock[RES_WOOD]=0.f;
    if (h->metal>0.f){ re->stock[RES_METAL]-=h->metal; if (re->stock[RES_METAL]<0.f) re->stock[RES_METAL]=0.f; }
    re->demand[RES_NAVAL_SUPPLIES]+=h->supplies;         /* le marché VOIT le chantier */
    re->demand[RES_WOOD]          +=h->wood;
    n->supplies_eaten+=h->supplies;
    /* L'ÉQUIPAGE se lève sur la pop du port (le warhost de la mer) : 50 bras
     * par coque légère, 100 par bordée — vérifié AVANT le paiement. */
    { int crew=navy_hull_crew(t);
      re->strata[CLASS_LABORER].pop -= (float)crew;
      n->crew += crew; }
    n->build_hull=(int)t; n->build_days=(float)h->days; n->home_port=port;
    return true;
}

bool navy_convert(NavyState *ns, const World *w, WorldEconomy *econ, int cid, bool to_pirate){
    if (cid<0||cid>=SCPS_MAX_COUNTRY) return false;
    Navy *n=&ns->n[cid];
    int port=navy_best_port(w,econ,cid);
    if (port<0) return false;                            /* la conversion se fait AU CHANTIER */
    int from = to_pirate?HULL_MERCHANT:HULL_PIRATE;
    int to   = to_pirate?HULL_PIRATE  :HULL_MERCHANT;
    if (n->hull[from]<=0) return false;
    RegionEconomy *re=&econ->region[port];
    const HullCost *h=&HULLS[HULL_PIRATE];               /* le coût léger de la conversion */
    float gold=navy_build_gold(econ,port,HULL_PIRATE);
    if (gold>re->treasury) return false;
    re->treasury-=gold;
    re->stock[RES_NAVAL_SUPPLIES]-=h->supplies; if (re->stock[RES_NAVAL_SUPPLIES]<0.f) re->stock[RES_NAVAL_SUPPLIES]=0.f;
    n->supplies_eaten+=h->supplies;
    n->hull[from]--; n->hull[to]++;
    if (!to_pirate) n->nest_region=-1;                   /* désarmé : le nid se vide */
    return true;
}

void navy_tick(NavyState *ns, const World *w, WorldEconomy *econ, float dt_days){
    for (int c=0;c<w->n_countries && c<SCPS_MAX_COUNTRY;c++){
        Navy *n=&ns->n[c];
        /* la rade suit la vie du pays (port perdu/conquis → meilleure rade restante) */
        if (n->home_port>=0 && (n->home_port>=econ->n_regions
            || econ->region[n->home_port].owner!=c
            || !navy_region_is_port(w,econ,n->home_port)))
            n->home_port=navy_best_port(w,econ,c);
        if (n->colony_cd>0.f) n->colony_cd-=dt_days;
        /* chantier */
        if (n->build_hull>=0){
            n->build_days-=dt_days;
            if (n->build_days<=0.f){
                n->hull[n->build_hull]++; n->built_total++;
                n->build_hull=-1; n->build_days=0.f;
            }
        }
        /* entretien : fournitures au fil de l'eau, consommées à la rade */
        int hulls=0; float need_y=0.f;
        for (int t=0;t<HULL_COUNT;t++){
            hulls+=n->hull[t];
            need_y += (float)n->hull[t] * ((t==HULL_WAR)?NAVY_UPKEEP_WAR:NAVY_UPKEEP_OTHER);
        }
        if (hulls<=0){ n->starve_days=0.f; continue; }
        if (n->home_port<0){ n->home_port=navy_best_port(w,econ,c); }
        if (n->home_port<0){                              /* sans rade, rien ne s'entretient */
            n->starve_days+=dt_days;
        } else {
            RegionEconomy *re=&econ->region[n->home_port];
            float need=need_y*(dt_days/365.f);
            re->demand[RES_NAVAL_SUPPLIES]+=need;         /* la demande se VOIT au marché */
            if (re->stock[RES_NAVAL_SUPPLIES]>=need){
                re->stock[RES_NAVAL_SUPPLIES]-=need;
                n->supplies_eaten+=need;
                n->starve_days=0.f;
            } else {
                n->supplies_eaten+=re->stock[RES_NAVAL_SUPPLIES];
                re->stock[RES_NAVAL_SUPPLIES]=0.f;
                n->starve_days+=dt_days;
            }
        }
        if (n->starve_days>NAVY_STARVE_YEAR){             /* la flotte pourrit à quai */
            int big=-1, bc=0;
            for (int t=0;t<HULL_COUNT;t++) if (n->hull[t]>bc){ bc=n->hull[t]; big=t; }
            if (big>=0){
                n->hull[big]--;
                int crew=navy_hull_crew((HullType)big);   /* les marins DÉBARQUENT et rentrent */
                n->crew-=crew; if (n->crew<0) n->crew=0;
                if (n->home_port>=0 && n->home_port<econ->n_regions)
                    econ->region[n->home_port].strata[CLASS_LABORER].pop += (float)crew;
            }
            n->starve_days-=365.f;
        }
    }
}

int navy_transport_packets_free(const NavyState *ns, int cid){
    if (cid<0||cid>=SCPS_MAX_COUNTRY) return 0;
    int free_tr=ns->n[cid].hull[HULL_TRANSPORT]-ns->n[cid].at_sea;
    return (free_tr>0)?free_tr*NAVY_TRANSPORT_PKTS:0;
}

float navy_sea_days_regions(const World *w, int reg_a, int reg_b){
    int ax,ay,bx,by;
    if (!world_region_sea_anchor(w,reg_a,&ax,&ay)) return -1.f;
    if (!world_region_sea_anchor(w,reg_b,&bx,&by)) return -1.f;
    return world_sea_days(w,ax,ay,bx,by);
}

/* ── LA COLONISATION OUTRE-MER (mer §8) : on découvre ce que la volta touche ── */
int navy_colonize_tick(NavyState *ns, const World *w, WorldEconomy *econ, float dt_days){
    (void)dt_days;
    int founded=0;
    for (int cid=0;cid<w->n_countries && cid<SCPS_MAX_COUNTRY;cid++){
        const Country *ct=&w->country[cid];
        if (ct->role!=POLITY_PLAYER && ct->role!=POLITY_ANTAGONIST) continue;
        Navy *n=&ns->n[cid];
        if (n->colony_cd>0.f) continue;
        if (n->hull[HULL_TRANSPORT]-n->at_sea<1) continue;   /* pas de flotte = un mur ÉCONOMIQUE */
        int port=navy_best_port(w,econ,cid);
        if (port<0) continue;
        const RegionEconomy *src=&econ->region[port];
        float spop=0.f; for (int k=0;k<CLASS_COUNT;k++) spop+=src->strata[k].pop;
        if (spop<500.f || src->food_sat<0.35f) continue;   /* mêmes seuils que la colonisation TERRESTRE (essaimer) */
        int best=-1; float bscore=-1.f, bdays=0.f;
        for (int rd=0;rd<econ->n_regions;rd++){
            const RegionEconomy *dst=&econ->region[rd];
            if (!dst->active || dst->colonized || econ->adj[port][rd]) continue;
            if (!region_coastal(w,rd)) continue;             /* on atterrit par la côte */
            float days=navy_sea_days_regions(w,port,rd);
            if (days<0.f || days>NAVY_COLONY_MAX_DAYS) continue;
            float score=dst->cap_pop*0.001f/(1.f+days/15.f); /* les courants RAPPROCHENT */
            if (score>bscore){ bscore=score; best=rd; bdays=days; }
        }
        (void)bdays;
        if (best>=0){
            econ_colonize_from(econ,port,best,cid);
            n->colony_cd=NAVY_COLONY_CD;
            founded++;
        }
    }
    return founded;
}

/* ════════════════════════════════════════════════════════════════════════
 * LA COURSE (brief coques) — le marchand recyclé en pirate chez les éthos
 * de razzia ; les eaux mortes deviennent des NIDS ; le raid prélève 1/10,
 * balafre 1 an, immunise 5 ans ; la saignée pèse sur les routes (marchands
 * +5 %/coque plafond 50 %, l'escorte chasse SANS plafond) ; la rancune
 * s'arme en CB. Le combat naval réutilise les OS des batailles-phases :
 * choc (2 j) · accalmie (1 j) · déroute · poursuite (prises).
 * ════════════════════════════════════════════════════════════════════════ */
#include "scps_diplo.h"
#include "scps_routes.h"
#include "scps_culture.h"

#define COURSE_RAID_TITHE   0.10f        /* 1/10 des stocks                   */
#define COURSE_BALAFRE_J    365.f        /* la balafre : 1 an                 */
#define COURSE_IMMUNITE_J   (5.f*365.f)  /* l'immunité : 5 ans (par PROVINCE) */
#define COURSE_RAID_CD_J    240.f        /* cadence d'un commanditaire        */
#define COURSE_PORTEE_J     60.f         /* portée de chasse (jours de mer)   */
#define COURSE_PROT_PAR_M   0.05f        /* +5 % de protection par marchand   */
#define COURSE_PROT_CAP     0.50f        /* plafond 50 % — l'escorte fait le reste */
#define COURSE_GRIEF_RAID   1.0f         /* rancune par raid IDENTIFIÉ        */
#define COURSE_GRIEF_SAIGNEE 0.10f       /* rancune/mois de saignée identifiée */

static uint32_t crs_rng(uint32_t *r){ uint32_t x=*r; x^=x<<13; x^=x>>17; x^=x<<5; return *r=x; }
static float crs_f(uint32_t *r){ return (crs_rng(r)&0xFFFFFF)*(1.f/16777216.f); }

/* Le COMBAT NAVAL — mêmes os que la bataille terrestre, chair marine : des
 * CHOCS de 2 jours pondérés par les bordées et LE COURANT (qui a le courant
 * dans le dos gagne un multiplicateur), une ACCALMIE de 1 jour, jusqu'à la
 * rupture du moral d'équipage ; la DÉROUTE livre les coques à la POURSUITE
 * (coulées ou PRISES). Renvoie +1 si A l'emporte, -1 si B, 0 si nul. */
static int navy_battle(const World *w, int cell_x, int cell_y,
                       int shipsA, int shipsB, float curA_dot,
                       uint32_t *rng, int *lossA, int *lossB, int *prises){
    (void)w; (void)cell_x; (void)cell_y;
    float resA=(float)shipsA, resB=(float)shipsB;            /* moral d'équipage ≈ bordées */
    float res0A=resA+0.01f, res0B=resB+0.01f;
    float multA=1.f+0.35f*clampf(curA_dot,-1.f,1.f);         /* le courant dans le dos ARME */
    float multB=2.f-multA;
    *lossA=*lossB=*prises=0;
    for (int day=0; day<30 && resA>0.25f*res0A && resB>0.25f*res0B; day++){
        if (day%3==2) continue;                              /* accalmie (1 j sur 3) */
        float jA=resA*multA*(0.8f+0.4f*crs_f(rng));
        float jB=resB*multB*(0.8f+0.4f*crs_f(rng));
        if (jA>jB) resB-=0.5f+0.25f*(jA-jB); else resA-=0.5f+0.25f*(jB-jA);
    }
    int verdict = (resA<=0.25f*res0A && resB<=0.25f*res0B) ? 0 : (resB<=0.25f*res0B ? +1 : (resA<=0.25f*res0A ? -1 : 0));
    *lossA=(int)clampf((float)shipsA-resA+0.5f, 0.f, (float)shipsA);
    *lossB=(int)clampf((float)shipsB-resB+0.5f, 0.f, (float)shipsB);
    if (verdict!=0) *prises = (crs_f(rng)<0.5f)?1:0;         /* la poursuite COULE ou CAPTURE */
    return verdict;
}

/* L'éthos du trône (capitale) — la doctrine navale en découle. */
static Ethos navy_ethos(const World *w, const WorldEconomy *econ, int cid){
    int cp=(cid>=0&&cid<w->n_countries)?w->country[cid].capital_prov:-1;
    int cr=(cp>=0&&cp<w->n_provinces)?w->province[cp].region:-1;
    return (cr>=0&&cr<econ->n_regions)?econ->region[cr].culture.ethos:(Ethos)0;
}

void navy_course_tick(NavyState *ns, const World *w, WorldEconomy *econ,
                      DiploState *dp, RouteNetwork *rn, uint32_t *rng,
                      int player, float dt_days){
    /* 0. la pression des routes se REPOSE chaque passe (re-mesurée ci-dessous) */
    for (int i=0;i<rn->n;i++) rn->route[i].pirate_press=0.f;

    for (int c=0;c<w->n_countries && c<SCPS_MAX_COUNTRY;c++){
        Navy *n=&ns->n[c];

        /* 1. VERDICT anti-piraterie : la paix l'exige — on désarme (réversible). */
        if (dp->pirate_disarm[c]){
            n->hull[HULL_MERCHANT]+=n->hull[HULL_PIRATE];
            n->hull[HULL_PIRATE]=0; n->nest_region=-1; n->mission=NAVY_RADE;
            dp->pirate_disarm[c]=0; n->disarmed++;
        }

        /* 2. DOCTRINE PAR ÉTHOS (IA seule — le joueur peut tout, et le paiera). */
        if (c!=player){
            Ethos e=navy_ethos(w,econ,c);
            int port=navy_best_port(w,econ,c);
            if (port>=0){
                if (e==ETHOS_HONNEUR && n->build_hull<0 && n->hull[HULL_WAR]<1
                    && n->hull[HULL_PIRATE]>0)
                    navy_order_build(ns,w,econ,c,HULL_WAR);     /* la course s'escorte */
                if (e==ETHOS_HONNEUR && n->hull[HULL_WAR]>=1){   /* l'Honneur CHASSE en guerre */
                    bool guerre=false;
                    for (int b=0;b<w->n_countries && b<SCPS_MAX_COUNTRY && !guerre;b++)
                        if (b!=c && diplo_status(dp,c,b)==DIPLO_WAR) guerre=true;
                    if (guerre && n->mission==NAVY_RADE) n->mission=NAVY_INTERCEPTION;
                }
                if ((e==ETHOS_HONNEUR || e==ETHOS_DOMINATEUR)){
                    /* la razzia : convertir tôt (Honneur) / armer contre le rival (Dominateur) */
                    int veut=(e==ETHOS_HONNEUR)?3:2; bool rival=(e==ETHOS_HONNEUR);
                    if (!rival) for (int b=0;b<w->n_countries && b<SCPS_MAX_COUNTRY && !rival;b++)
                        if (b!=c && (diplo_status(dp,c,b)==DIPLO_WAR || dp->rancor[c][b]>0.5f)) rival=true;
                    if (rival && n->hull[HULL_PIRATE]<veut && n->hull[HULL_MERCHANT]>0)
                        navy_convert(ns,w,econ,c,true);
                } else if (n->hull[HULL_PIRATE]>0){
                    navy_convert(ns,w,econ,c,false);   /* les autres éthos ne s'encanaillent pas d'eux-mêmes */
                }
                /* le Mercantile harcelé MONTE : marchands au plafond, puis l'escorte sort */
                if (e==ETHOS_MERCANTILE || e==ETHOS_PACIFISTE){
                    float worst=0.f;
                    for (int i=0;i<rn->n;i++){ const TradeRoute *t=&rn->route[i];
                        if (!t->open||!t->maritime) continue;
                        if (econ->region[t->ra].owner!=c && econ->region[t->rb].owner!=c) continue;
                        if (t->pirate_press>worst) worst=t->pirate_press; }
                    if (n->build_hull<0 && worst>0.5f){
                        if (n->hull[HULL_MERCHANT]<10) navy_order_build(ns,w,econ,c,HULL_MERCHANT);
                        else if (e==ETHOS_MERCANTILE && n->hull[HULL_WAR]<3) navy_order_build(ns,w,econ,c,HULL_WAR);
                    }
                    if (n->hull[HULL_WAR]>0) n->mission=NAVY_ESCORTE;
                }
                /* l'Ordre patrouille ses eaux ; en guerre, le BLOCUS du port ennemi.
                 * Dominateur ET Bureaucrate ARMENT des bordées (sans elles, blocus
                 * et interception restaient lettre morte — 0/308 au balayage). */
                if (e==ETHOS_DOMINATEUR || e==ETHOS_BUREAUCRATE){
                    if (n->build_hull<0 && n->hull[HULL_WAR]<2)
                        navy_order_build(ns,w,econ,c,HULL_WAR);
                    int foe=-1, foe_any=-1;
                    for (int b=0;b<w->n_countries && b<SCPS_MAX_COUNTRY;b++){
                        if (b==c || diplo_status(dp,c,b)!=DIPLO_WAR) continue;
                        if (foe_any<0) foe_any=b;
                        if (foe<0 && navy_best_port(w,econ,b)>=0) foe=b;
                    }
                    if (foe>=0 && n->hull[HULL_WAR]>=2){ n->mission=NAVY_BLOCUS; n->mission_target=foe; }
                    else if (foe_any>=0 && n->hull[HULL_WAR]>=1){ n->mission=NAVY_INTERCEPTION; n->mission_target=foe_any; }
                    else if (n->mission==NAVY_BLOCUS||n->mission==NAVY_INTERCEPTION){ n->mission=NAVY_RADE; n->mission_target=-1; }
                }
            }
        }
        if (n->mission==NAVY_BLOCUS){
            int t=n->mission_target;
            if (t<0||t>=SCPS_MAX_COUNTRY||diplo_status(dp,c,t)!=DIPLO_WAR||n->hull[HULL_WAR]<1){
                n->mission=NAVY_RADE; n->mission_target=-1;   /* le blocus ne survit pas à la paix */
            }
        }

        /* 3. LE NID : les pirates mouillent dans les EAUX MORTES proches de la rade. */
        if (n->hull[HULL_PIRATE]>0 && n->nest_region<0){
            int port=navy_best_port(w,econ,c);
            int ax,ay;
            if (port>=0 && world_region_sea_anchor(w,port,&ax,&ay)){
                int best=-1; int32_t bd=0x7FFFFFFF;
                for (int y=0;y<SCPS_H;y+=4) for (int x=0;x<SCPS_W;x+=4){
                    const Cell *cc=&w->cell[scps_idx(x,y)];
                    if (cc->sea!=SEA_MORTE) continue;
                    int32_t d2=(x-ax)*(x-ax)+(y-ay)*(y-ay);
                    if (d2<bd){ bd=d2; best=scps_idx(x,y); }
                }
                n->nest_region=best;                          /* une CELLULE de mer morte */
            }
        }
        if (n->hull[HULL_PIRATE]<=0) n->nest_region=-1;

        /* 4. LE RAID CÔTIER : 1/10 des stocks · balafre 1 an · immunité 5 ans. */
        if (n->raid_cd>0.f) n->raid_cd-=dt_days;
        if (n->hull[HULL_PIRATE]>0 && n->raid_cd<=0.f){
            int port=navy_best_port(w,econ,c);
            int best=-1; float bv=-1.f;
            for (int r=0;r<econ->n_regions;r++){
                RegionEconomy *re=&econ->region[r];
                if (re->owner==c || re->owner<0 || !re->coastal || !re->culture.settled) continue;
                if (re->raid_cd_days>0.f) continue;                    /* la vache est traite */
                if (diplo_status(dp,c,re->owner)==DIPLO_ALLIED) continue;
                if (port>=0){ float dj=navy_sea_days_regions(w,port,r);
                              if (dj<0.f || dj>COURSE_PORTEE_J) continue; }
                float v=0.f; for (int g=1;g<RES_COUNT;g++) v+=re->stock[g]*re->price[g];
                if (re->build.port>0.f) v*=1.3f;                       /* le port : la cible juteuse */
                if (v>bv){ bv=v; best=r; }
            }
            if (best>=0){
                RegionEconomy *re=&econ->region[best];
                int victim=re->owner;
                int defense=(victim>=0&&victim<SCPS_MAX_COUNTRY)?ns->n[victim].hull[HULL_WAR]:0;
                bool success=true, identified=false;
                if (defense>0){
                    /* l'escorte/patrouille FORCE la bataille — le courant local arme */
                    int ax2,ay2; float dot=0.f;
                    if (world_region_sea_anchor(w,best,&ax2,&ay2)){
                        const Cell *cc=&w->cell[scps_idx(ax2,ay2)];
                        dot=(cc->cur_vx*0.7f+cc->cur_vy*0.3f)/100.f;   /* proxy : l'attaquant vient du large */
                    }
                    int lA,lB,pr;
                    float effA=(float)n->hull[HULL_PIRATE]*(n->starve_days>0.f?0.7f:1.f);
                    float effB=(float)defense*(ns->n[victim].starve_days>0.f?0.7f:1.f);
                    int v2=navy_battle(w,0,0,(int)(effA+0.5f),(int)(effB+0.5f),dot,rng,&lA,&lB,&pr);
                    if (lA>n->hull[HULL_PIRATE]) lA=n->hull[HULL_PIRATE];
                    if (lB>ns->n[victim].hull[HULL_WAR]) lB=ns->n[victim].hull[HULL_WAR];
                    n->hull[HULL_PIRATE]-=lA;
                    ns->n[victim].hull[HULL_WAR]-=lB;
                    n->crew-=lA*NAVY_CREW_LIGHT; if (n->crew<0) n->crew=0;             /* sombrés avec la coque */
                    ns->n[victim].crew-=lB*NAVY_CREW_WAR; if (ns->n[victim].crew<0) ns->n[victim].crew=0;
                    n->navals++; ns->n[victim].navals++;
                    if (v2>0){ if (pr && ns->n[victim].hull[HULL_MERCHANT]>0){
                        ns->n[victim].hull[HULL_MERCHANT]--; n->hull[HULL_PIRATE]++; n->prises++;
                        ns->n[victim].crew-=NAVY_CREW_LIGHT; if (ns->n[victim].crew<0) ns->n[victim].crew=0;
                        n->crew+=NAVY_CREW_LIGHT;            /* l'équipage pris est ENRÔLÉ de force */ } }
                    else { success=false; identified=(crs_f(rng)<0.85f); }   /* capturé, il DÉSIGNE */
                } else identified=(crs_f(rng)<0.35f);
                if (success && n->hull[HULL_PIRATE]>0){
                    float loot=0.f;
                    for (int g=1;g<RES_COUNT;g++){
                        loot += re->stock[g]*COURSE_RAID_TITHE*re->price[g];
                        re->stock[g]*=(1.f-COURSE_RAID_TITHE);
                    }
                    re->balafre_days=COURSE_BALAFRE_J;                  /* « côte balafrée » */
                    re->raid_cd_days=COURSE_IMMUNITE_J;
                    int hp=navy_best_port(w,econ,c);
                    if (hp>=0) econ->region[hp].treasury+=loot;         /* la course est un revenu d'ÉTAT */
                    n->raids_done++; n->loot_gold+=loot;
                    if (identified && victim>=0) diplo_pirate_grief(dp,victim,c,COURSE_GRIEF_RAID);
                } else if (identified && victim>=0)
                    diplo_pirate_grief(dp,victim,c,COURSE_GRIEF_RAID*0.6f);
                n->raid_cd=COURSE_RAID_CD_J*(0.8f+0.4f*crs_f(rng));
            } else n->raid_cd=120.f;                                    /* rien à portée : on patiente */
        }

        /* 5. LA SAIGNÉE : les pirates pèsent sur la route la plus riche à portée. */
        if (n->hull[HULL_PIRATE]>0){
            int port=navy_best_port(w,econ,c), tgt=-1; float bv=-1.f;
            for (int i=0;i<rn->n;i++){
                TradeRoute *t=&rn->route[i];
                if (!t->open || !t->maritime) continue;
                int oa=econ->region[t->ra].owner, ob=econ->region[t->rb].owner;
                if (oa==c||ob==c) continue;
                if (port>=0){ float dj=navy_sea_days_regions(w,port,t->ra);
                              if (dj<0.f||dj>COURSE_PORTEE_J) continue; }
                if (t->yield>bv){ bv=t->yield; tgt=i; }
            }
            if (tgt>=0){
                TradeRoute *t=&rn->route[tgt];
                float press=(float)n->hull[HULL_PIRATE];
                int oa=econ->region[t->ra].owner, ob=econ->region[t->rb].owner;
                int merch=0, escort=0;
                if (oa>=0&&oa<SCPS_MAX_COUNTRY){ merch+=ns->n[oa].hull[HULL_MERCHANT];
                    if (ns->n[oa].mission==NAVY_ESCORTE) escort+=ns->n[oa].hull[HULL_WAR]; }
                if (ob>=0&&ob<SCPS_MAX_COUNTRY){ merch+=ns->n[ob].hull[HULL_MERCHANT];
                    if (ns->n[ob].mission==NAVY_ESCORTE) escort+=ns->n[ob].hull[HULL_WAR]; }
                float prot=fminf(COURSE_PROT_CAP, COURSE_PROT_PAR_M*(float)merch);
                float net=press*(1.f-prot)-(float)escort;               /* l'escorte CHASSE, sans plafond */
                if (net>0.f){
                    t->pirate_press+=net;
                    float idp=0.15f*(dt_days/30.f);
                    if (oa>=0 && crs_f(rng)<idp) diplo_pirate_grief(dp,oa,c,COURSE_GRIEF_SAIGNEE);
                    if (ob>=0 && crs_f(rng)<idp) diplo_pirate_grief(dp,ob,c,COURSE_GRIEF_SAIGNEE);
                }
            }
        }

        /* 6. LE BLOCUS : mouillé devant le port ennemi, il COUPE ses liens. */
        if (n->mission==NAVY_BLOCUS && n->mission_target>=0){
            int t=n->mission_target;
            for (int i=0;i<rn->n;i++){
                TradeRoute *rt=&rn->route[i];
                if (!rt->open||!rt->maritime) continue;
                if (econ->region[rt->ra].owner==t || econ->region[rt->rb].owner==t)
                    rt->pirate_press=99.f;                              /* le lien est tenu fermé */
            }
            n->blocus_days+=dt_days;
        }
    }
}

/* ════════════════════════════════════════════════════════════════════════
 * L'INTERCEPTION (coques §3 — le job FINI) : les transports pleins sont des
 * cibles stratégiques. Un convoi hostile en mer (FA_SAIL) croise les
 * patrouilles d'INTERCEPTION : l'escorte du convoi (les bordées de son
 * pays), c'est tout ce qui le sépare du fond — un transport sans escorte
 * MEURT SEUL. La bataille réutilise les phases ; le convoi coulé NOIE ses
 * paquets (le warhost embarqué sombre avec les coques).
 * ════════════════════════════════════════════════════════════════════════ */
#include "scps_campaign.h"

void navy_interception_tick(NavyState *ns, struct Campaign *camp, const World *w,
                            WorldEconomy *econ, struct DiploState *dp, uint32_t *rng){
    (void)econ;
    for (int i=0;i<SCPS_MAX_COUNTRY;i++){
        FieldArmy *a=&camp->army[i];
        if (!a->active || a->phase!=FA_SAIL || a->intercept_done) continue;
        int owner=a->owner;
        if (owner<0||owner>=SCPS_MAX_COUNTRY) continue;
        for (int e=0;e<w->n_countries && e<SCPS_MAX_COUNTRY;e++){
            if (e==owner) continue;
            Navy *pat=&ns->n[e];
            if (pat->mission!=NAVY_INTERCEPTION || pat->hull[HULL_WAR]<1) continue;
            if (diplo_status(dp,e,owner)!=DIPLO_WAR) continue;
            if (crs_f(rng)>0.45f) continue;                   /* la mer est grande : on ne trouve pas toujours */
            a->intercept_done=true;                            /* une chasse par traversée */
            Navy *esc=&ns->n[owner];
            int escort=esc->hull[HULL_WAR];
            int lA,lB,pr;
            float effA=(float)pat->hull[HULL_WAR]*(pat->starve_days>0.f?0.7f:1.f);
            float effB=(float)escort*(esc->starve_days>0.f?0.7f:1.f);
            int v=(escort>0)
                ? navy_battle(w,0,0,(int)(effA+0.5f),(int)(effB+0.5f),0.f,rng,&lA,&lB,&pr)
                : (+1);                                        /* sans escorte : PROIE */
            if (escort>0){
                if (lA>pat->hull[HULL_WAR]) lA=pat->hull[HULL_WAR];
                if (lB>esc->hull[HULL_WAR]) lB=esc->hull[HULL_WAR];
                pat->hull[HULL_WAR]-=lA; pat->crew-=lA*NAVY_CREW_WAR; if (pat->crew<0) pat->crew=0;
                esc->hull[HULL_WAR]-=lB; esc->crew-=lB*NAVY_CREW_WAR; if (esc->crew<0) esc->crew=0;
                pat->navals++; esc->navals++;
            }
            if (v>0){                                          /* le convoi COULE */
                long pk=0;
                { ArmyComposition ac=campaign_composition(camp,owner); pk=ac.total; }
                int tr=a->sail_transports;
                esc->hull[HULL_TRANSPORT]-=tr; if (esc->hull[HULL_TRANSPORT]<0) esc->hull[HULL_TRANSPORT]=0;
                esc->at_sea-=tr; if (esc->at_sea<0) esc->at_sea=0;
                esc->crew-=tr*NAVY_CREW_LIGHT; if (esc->crew<0) esc->crew=0;
                a->sail_transports=0;
                a->active=false; a->phase=FA_IDLE; a->dest=-1; a->next=-1;
                pat->intercepts++; pat->drowned+=pk;           /* les paquets SOMBRENT */
                if (pr && esc->hull[HULL_TRANSPORT]>0){        /* la poursuite fait une PRISE */
                    esc->hull[HULL_TRANSPORT]--; pat->hull[HULL_TRANSPORT]++;
                    pat->crew+=NAVY_CREW_LIGHT; pat->prises++;
                }
            }
            break;                                             /* une bataille par convoi et par mois */
        }
    }
}
