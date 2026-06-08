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
/* ---- Score de guerre (§2) -------------------------------------------- */
#define WAR_BATTLE_W     14.f   /* vitesse du battle_score vers ±50 (par an, à avantage net) */
#define WAR_BATTLE_CAP   50.f   /* les batailles SEULES ne gagnent pas la guerre (la moitié) */
#define WAR_OCCUPY_PER   12.f   /* points d'occupation par région prise (l'autre moitié) */
#define WAR_ATTRITION    0.18f  /* part d'armes perdue/an (saigne les deux ; le perdant ×plus) */
#define WAR_ATTR_LOSER   1.6f
#define WAR_ATTR_WINNER  0.6f
/* ---- Paix proportionnelle (§5) --------------------------------------- */
#define CLAIM_DOM         18.f  /* provinces légitimes de plus par cran de domination militaire */
#define CLAIM_ILLEGIT_MOM 2.0f  /* surcroît de fulgurance par prise ILLÉGITIME (→ coalition) */
#define REP_MIN_SCORE     20.f  /* en-deçà de ce score : match nul → aucune indemnité */
#define REP_RATE          0.5f  /* part max du trésor du perdant exigée (à 100 de score) */

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
void diplo_declare_war_cb(DiploState *d,int a,int b,CasusBelli cb){
    set_sym(d,a,b,DIPLO_WAR);
    if (a>=0&&a<SCPS_MAX_COUNTRY&&b>=0&&b<SCPS_MAX_COUNTRY) d->cb[a][b]=(int8_t)cb;  /* le but de l'AGRESSEUR */
}
CasusBelli diplo_war_goal(const DiploState *d,int a,int b){
    if (a<0||a>=SCPS_MAX_COUNTRY||b<0||b>=SCPS_MAX_COUNTRY) return CB_NONE;
    return (CasusBelli)d->cb[a][b];
}
const char *diplo_cb_name(CasusBelli cb){
    switch(cb){ case CB_TERRITORIAL: return "territorial"; case CB_RELIGIOUS: return "religieux";
                case CB_ECONOMIC: return "économique"; case CB_SUBJUGATION: return "assujettissement";
                default: return "aucun"; }
}
void diplo_form_alliance(DiploState *d,int a,int b){ set_sym(d,a,b,DIPLO_ALLIED); }
void diplo_make_peace   (DiploState *d,int a,int b){
    set_sym(d,a,b,DIPLO_NEUTRAL);
    if (a>=0&&a<SCPS_MAX_COUNTRY&&b>=0&&b<SCPS_MAX_COUNTRY){
        /* TRÊVE : une longue guerre → une longue trêve. On ne peut redéclarer
         * avant qu'elle fonde — l'enchaînement conquête→reconquête est cassé. */
        float dur = clampf(TRUCE_BASE + TRUCE_PER_YEAR*d->war_years[a][b], 0.f, TRUCE_MAX);
        d->truce[a][b]=d->truce[b][a]=dur;
        d->war_years[a][b]=d->war_years[b][a]=0.f;
        d->cb[a][b]=d->cb[b][a]=CB_NONE;   /* le but de guerre s'éteint avec la guerre */
        d->battle_score[a][b]=d->battle_score[b][a]=0.f;   /* le bras-de-fer se solde */
        d->conquered[a][b]=d->conquered[b][a]=0;
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

/* ---- CASUS BELLI — la raison de la guerre (lue, jamais posée) ---------- */
static bool country_extracts(const WorldEconomy *econ, int cid, Resource g){
    if (g<=RES_NONE||g>=RES_COUNT) return false;
    for (int r=0;r<econ->n_regions;r++)
        if (econ->region[r].owner==cid && econ->region[r].raw_cap[g]>0.1f) return true;
    return false;
}
static bool diplo_adjacent(const WorldEconomy *econ, int a, int b){
    for (int r=0;r<econ->n_regions;r++) if (econ->region[r].owner==a)
        for (int s=0;s<econ->n_regions;s++)
            if (econ->region[s].owner==b && econ->adj[r][s]) return true;
    return false;
}
CasusBelli diplo_casus_belli(const World *w, const WorldEconomy *econ, const WorldProsperity *wp,
                             const DiploState *d, int a, int b, Resource want){
    if (a<0||a>=w->n_countries||b<0||b>=w->n_countries||a==b) return CB_NONE;
    /* ÉCONOMIQUE — le bien AIGU que la cible extrait et que nous n'avons pas (monopole) :
     * le casus belli du Mercantile bloqué (il vise la province-source). */
    if (want>RES_NONE && want<RES_COUNT && country_extracts(econ,b,want) && !country_extracts(econ,a,want))
        return CB_ECONOMIC;
    /* RELIGIEUX — schisme (branche proche + prosélytisme = ennemi naturel). */
    Relation rel = diplo_relation(w,econ,wp,d,a,b);
    if (rel.schism > 0.45f) return CB_RELIGIOUS;
    /* TERRITORIAL — adjacence / revendication de frontière (la raison la plus commune). */
    if (diplo_adjacent(econ,a,b)) return CB_TERRITORIAL;
    /* ASSUJETTISSEMENT — on PROJETTE nettement plus de puissance que la cible. */
    if (diplo_mil_power(w,econ,a) > 1.6f*diplo_mil_power(w,econ,b)+1.f) return CB_SUBJUGATION;
    return CB_NONE;   /* aucune raison ne tient → pas de guerre */
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
    if (conqueror<SCPS_MAX_COUNTRY){
        d->momentum[conqueror] += MOMENTUM_PER_CONQ;   /* la fulgurance EFFRAIE (→ coalition) */
        if (defender>=0 && defender<SCPS_MAX_COUNTRY){
            d->conquered[conqueror][defender]++;        /* OCCUPATION : pousse le score de guerre */
            /* §5 LÉGITIMITÉ : au-delà de ce que la domination militaire justifie (la
             * revendication), la prise est de la SUREXPANSION — surcroît de fulgurance
             * (le monde se ligue) et plaie plus profonde (intégration déjà à zéro). */
            if (d->conquered[conqueror][defender] > diplo_war_claim(d,w,econ,conqueror,defender)){
                d->momentum[conqueror] += CLAIM_ILLEGIT_MOM;
                re->revolt_scar = 1.0f;
            }
        }
    }
    legitimacy_on_conquest(wl, region);   /* L au plancher, intégration à zéro */
    /* SACCAGE : la prise DÉPOUILLE la province (or + production → trésor de
     * l'occupant), 1×/5 ans. Le butin afflue vers la capitale du conquérant. */
    int dst=-1, cp=w->country[conqueror].capital_prov;
    if (cp>=0 && cp<w->n_provinces) dst=w->province[cp].region;
    diplo_pillage_region(econ, region, dst);
    return true;
}

/* ---- guerre : SACCAGE (§4) — dépouiller la province prise -------------- */
#define PILLAGE_COOLDOWN_Y 5.0f    /* 1 saccage / 5 ans / province (note utilisateur) */
#define PILLAGE_GOLD_FRAC  0.6f    /* part du trésor provincial raflée d'un coup */
#define PILLAGE_STOCK_FRAC 0.5f    /* ~6 mois de production en entrepôt, fondus en or */
float diplo_pillage_region(WorldEconomy *econ, int region, int dst_region){
    if (!econ || region<0 || region>=econ->n_regions) return 0.f;
    RegionEconomy *re=&econ->region[region];
    if (re->pillage_cd > 0.f) return 0.f;          /* déjà dépouillée → plus rien à prendre */
    float loot = PILLAGE_GOLD_FRAC * re->treasury; /* l'or des coffres */
    re->treasury *= (1.f - PILLAGE_GOLD_FRAC);
    for (int g=1; g<RES_COUNT; g++){               /* l'entrepôt, valorisé au prix courant */
        float take = PILLAGE_STOCK_FRAC * re->stock[g];
        loot += take * re->price[g];
        re->stock[g] -= take;
    }
    re->revolt_scar = 1.0f;                         /* le sac CONVULSE : gel du développement */
    re->pillage_cd  = PILLAGE_COOLDOWN_Y;           /* ne pourra être re-saccagée avant ~5 ans */
    if (dst_region>=0 && dst_region<econ->n_regions && dst_region!=region)
        econ->region[dst_region].treasury += loot;  /* fondu dans le trésor de l'occupant */
    return loot;
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

/* ---- Score de guerre : le bras-de-fer + l'attrition ------------------- */
static void deplete_arms(WorldEconomy *econ, int cid, float frac){
    frac = clampf(frac, 0.f, 0.95f);
    for (int r=0;r<econ->n_regions;r++) if (econ->region[r].owner==cid){
        econ->region[r].stock[RES_ARMS]          *= (1.f-frac);
        econ->region[r].stock[RES_GUNPOWDER]      *= (1.f-frac);
        econ->region[r].stock[RES_ENCHANTED_ARMS] *= (1.f-frac);
    }
}
void diplo_war_tick(DiploState *d, World *w, WorldEconomy *econ,
                    const WorldProsperity *wp, float dt){
    (void)wp;
    for (int a=0;a<w->n_countries;a++) for (int b=0;b<w->n_countries;b++){
        if (a==b || d->status[a][b]!=DIPLO_WAR) continue;
        if (d->cb[a][b]==CB_NONE) continue;             /* a est l'ATTAQUANT (il porte le CB) */
        float pA=diplo_mil_power(w,econ,a), pB=diplo_mil_power(w,econ,b);
        float ratio = pA/(pA+pB+0.01f);                  /* avantage militaire de l'attaquant */
        /* BATAILLES : l'avantage pousse le battle_score vers +50 ; un attaquant plus
         * FAIBLE le voit chuter (la voie défensive de l'adversaire vers −100). */
        d->battle_score[a][b] = clampf(d->battle_score[a][b] + WAR_BATTLE_W*(ratio-0.5f)*2.f*dt,
                                       -100.f, WAR_BATTLE_CAP);
        d->battle_score[b][a] = d->battle_score[a][b];   /* miroir lisible */
        /* ATTRITION : la guerre SAIGNE les armes des deux ; le perdant de l'échange
         * en perd plus → mil_power baisse → la guerre s'épuise (pression à la paix). */
        float lossA = WAR_ATTRITION*dt*(ratio<0.5f?WAR_ATTR_LOSER:WAR_ATTR_WINNER);
        float lossB = WAR_ATTRITION*dt*(ratio>0.5f?WAR_ATTR_LOSER:WAR_ATTR_WINNER);
        deplete_arms(econ,a,lossA); deplete_arms(econ,b,lossB);
    }
}
float diplo_war_score(const DiploState *d, int a, int b){
    if (a<0||a>=SCPS_MAX_COUNTRY||b<0||b>=SCPS_MAX_COUNTRY) return 0.f;
    float occ = fminf(50.f, WAR_OCCUPY_PER*(float)d->conquered[a][b]);  /* +50→+100 par l'occupation */
    return clampf(d->battle_score[a][b] + occ, -100.f, 100.f);
}

/* ---- Paix proportionnelle (§5) : la victoire achète des termes -------- */
int diplo_war_claim(const DiploState *d, const World *w, const WorldEconomy *econ, int a, int b){
    if (a<0||a>=SCPS_MAX_COUNTRY||b<0||b>=SCPS_MAX_COUNTRY) return 0;
    CasusBelli cb=(CasusBelli)d->cb[a][b];
    float pA=diplo_mil_power(w,econ,a), pB=diplo_mil_power(w,econ,b);
    float ratio=pA/(pA+pB+0.01f);                        /* domination militaire de l'attaquant */
    if (cb==CB_NONE)        return ratio>0.5f ? 1 : 0;   /* conquête nue : 1 tampon si l'on domine */
    if (cb!=CB_TERRITORIAL) return 1;                    /* humiliation/source/vassalité : une prise */
    return 1 + (int)(CLAIM_DOM*fmaxf(0.f, ratio-0.5f));  /* territorial : ∝ domination */
}
float diplo_reparations(DiploState *d, World *w, WorldEconomy *econ, int a, int b){
    if (a<0||a>=w->n_countries||b<0||b>=w->n_countries||a==b) return 0.f;
    float s=diplo_war_score(d,a,b);                      /* point de vue de a */
    if (absf(s) < REP_MIN_SCORE) return 0.f;             /* match nul → pas de vainqueur net */
    int winner=(s>0.f)?a:b, loser=(s>0.f)?b:a;
    float frac=REP_RATE*fminf(1.f, absf(s)/100.f);       /* plus la défaite est nette, plus on saigne */
    int cap=w->country[winner].capital_prov;
    int dst=(cap>=0&&cap<w->n_provinces)?w->province[cap].region:-1;
    float total=0.f;
    for (int r=0;r<econ->n_regions;r++) if (econ->region[r].owner==loser){
        float pay=frac*econ->region[r].treasury;
        econ->region[r].treasury-=pay; total+=pay;       /* indemnité prélevée sur tout le royaume */
    }
    if (dst>=0&&dst<econ->n_regions) econ->region[dst].treasury+=total;
    return total;
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
