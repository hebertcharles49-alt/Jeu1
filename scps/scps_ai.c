/*
 * scps_ai.c — la boucle de décision IA (voir scps_ai.h)
 *
 * Tout ce que fait l'IA passe par les verbes du JOUEUR :
 *   agency_order_build   (bâtir K/H/food/PE)
 *   routes_order         (ouvrir une route — la cloche f(D̄))
 *   diplo_declare_war / diplo_conquer_region / diplo_make_peace
 * Elle ne touche jamais l'état directement : elle pousse des leviers, le moteur
 * d'ordre fait le reste. La personnalité sort de la fiche ; le frein sort de la
 * coordonnée de consolidation. Aucune branche « si pays==X ».
 */
#include "scps_ai.h"
#include "scps_tech.h"
#include "scps_species.h"
#include "scps_factions.h"   /* l'éthos effectif + la fracture de valeurs (frein interne §6) */
#include <string.h>
#include <math.h>

#define AI_ETHOS_FRACTURE_W     0.7f  /* poids du frein INTERNE : une politique déchirée se consolide (§6) */
#define AI_ETHOS_FRACTURE_FLOOR 0.38f /* socle : un mono-éthos a une fracture résiduelle — on ne freine qu'AU-DELÀ */

/* ---- Cadences & calibrage --------------------------------------------- */
#define AI_ECON_CADENCE   550    /* ~1.5 an entre décisions éco/bâti        */
#define AI_STRAT_CADENCE  1100   /* ~3 ans entre décisions stratégiques     */
#define AI_PEACE_LOCK     1825   /* 5 ans de consolidation forcée (hystérésis) */
#define AI_ARMY_MARGIN    0.75f  /* n'attaque que si armée ≥ 0.75× la cible  */
#define AI_WIDEN_W        0.5f    /* friction : poids du coût d'élargissement (alliés de la cible) */
#define AI_SURRENDER      55.f    /* score de guerre adverse au-delà duquel un défenseur sans espoir capitule */
#define AI_ALLY_SEUIL     6.0f    /* score d'alliance au-delà duquel on propose l'alliance */
#define AI_FOOD_FLOOR     1.5f   /* sous ce seuil de marge : grenier d'abord */
#define AI_BRAKE_HARD     0.6f   /* frein dur : consolidation impérative     */
#define AI_RANCOR_W       3.0f   /* §6 biais de RECONQUÊTE : on vise qui nous a pris nos terres */
#define AI_CRUSADE_W      4.0f   /* croisade : l'orthodoxe vise qui développe le faustien (chance ∝ ferveur) */
#define AI_ANNEX_FRAC     0.6f   /* §5 : un budget ≥ 60 % de la valeur du pays = victoire décisive → annexion */
/* §4 — leviers : chaque ACTE est un vote. Une politique tenue accumule vers le cap. */
#define AI_LEVER_TECH     0.05f  /* franchir l'interdit (tech faustienne) → Transgresseurs */
#define AI_LEVER_WAR      0.05f  /* conquérir → Conquérants */
#define AI_LEVER_BUILD    0.035f /* bâtir une famille d'édifices → la faction afférente */
/* ---- Recherche (l'arbre de tech vivant) ------------------------------- */
#define AI_RESEARCH_CADENCE 365  /* ~1 an entre déverrouillages potentiels */
#define AI_RESEARCH_RATE    14.f /* points/an de base, × rendement Savoir × f(pop) */
#define AI_RESEARCH_POPREF  8000.f /* population qui DOUBLE l'assiette de recherche */
#define AI_TECH_PENCHANT    2.0f  /* biais vers le thème de SA race (penchant, pas « si ») */
#define AI_TECH_SIGNATURE   1.5f  /* prime à une signature accessible (la sienne / greffée) */
#define AI_TECH_FAUSTIAN    2.5f  /* tolérance faustienne = w_faustian − frein (sinon on évite) */
#define AI_FAITH_FAUSTIAN   3.0f  /* §4 : l'orthodoxie INTERDIT le faustien, le culte le SACRALISE */

/* ---- Utilitaires ------------------------------------------------------ */
static inline float clampf(float v, float lo, float hi){ return v<lo?lo:(v>hi?hi:v); }
static uint32_t xs32(uint32_t *s){ uint32_t x=*s; x^=x<<13; x^=x>>17; x^=x<<5; return *s=x?x:1u; }
static float frand(uint32_t *s){ return (float)(xs32(s)&0xffffffu) / (float)0x1000000u; }

/* ===================================================================== */
/* PERSONNALITÉ — dérivée de la fiche (aucun code par-faction)            */
/* ===================================================================== */
static float norm01(float x){ return clampf(x/10.f, 0.f, 1.f); }

void ai_derive_weights(AiActor *a, const PopCulture *self){
    if (!self){ a->w_expand=a->w_trade=a->w_build=a->w_faith=0.f; a->w_faustian=0.2f; return; }
    float v = self->valeurs;                       /* 0..10 ; haut = Dominateur/martial */

    /* Conquête : l'appétit suit les VALEURS (Dominateur ~9 … Pacifiste ~1.5). */
    a->w_expand = norm01(v);

    /* Commerce : l'inverse des valeurs, amplifié par un trait éco mercantile,
     * étouffé par un trait prédateur (le tribut n'ouvre pas de routes). */
    float econ_f = (self->econ==ECON_CARAVANE || self->econ==ECON_GUILDE) ? 1.5f
                 : (self->econ==ECON_TRIBUT    || self->econ==ECON_PILLAGE_RUINES) ? 0.4f
                 : 1.0f;
    a->w_trade = norm01(10.f - v) * econ_f;

    /* Bâtir du K : l'éthos qui TIENT la diversité (Bureaucrate > Ordre > reste ;
     * le Dominateur/Honneur bâtit peu — il tient par la force, pas par le droit). */
    float build_f = (self->ethos==ETHOS_BUREAUCRATE) ? 1.6f
                  : (self->ethos==ETHOS_ORDRE)        ? 1.2f
                  : (self->ethos==ETHOS_DOMINATEUR || self->ethos==ETHOS_HONNEUR) ? 0.6f
                  : 1.0f;
    a->w_build = 0.5f * build_f;

    /* Foi : le prosélytisme pousse la guerre sainte / l'homogénéisation forcée. */
    a->w_faith = (self->credo==CREDO_PURIFICATEUR) ? 1.0f
               : (self->credo==CREDO_EVANGELISTE)  ? 0.6f
               : 0.1f;

    /* Pente faustienne : appétit d'arcane (de base ; la race Arcanique l'amplifie
     * via ses leviers, lus ailleurs). */
    a->w_faustian = 0.2f;

    /* Jitter : ±12 % par poids — deux Dominateurs ne conquièrent pas en phase. */
    a->w_expand   *= 0.88f + 0.24f*frand(&a->rng);
    a->w_trade    *= 0.88f + 0.24f*frand(&a->rng);
    a->w_build    *= 0.88f + 0.24f*frand(&a->rng);
    a->w_faith    *= 0.88f + 0.24f*frand(&a->rng);
    a->w_faustian *= 0.88f + 0.24f*frand(&a->rng);
    /* Socle figé : la résultante des factions le MODULERA (ai_refresh_ethos, §3). */
    a->w_base[0]=a->w_expand; a->w_base[1]=a->w_trade; a->w_base[2]=a->w_build;
    a->w_base[3]=a->w_faith;  a->w_base[4]=a->w_faustian;
}

/* §3 — L'ÉTHOS EFFECTIF GLISSE : la personnalité du pays n'est plus figée à sa
 * culture de trône, elle suit la RÉSULTANTE de ses factions. On module le socle
 * par l'ÉCART entre le penchant du PEUPLE (distribution enracinée) et celui du
 * TRÔNE (culture régnante) : un empire homogène ne bouge pas (écart nul, équilibre
 * préservé) ; un empire qui a avalé des orques voit sa conquête (et son faustien)
 * MONTER, son commerce baisser — « un empire change d'éthos quand qui le compose
 * change ». Borné : la résultante infléchit, elle ne renverse pas le socle. */
#define AI_ETHOS_GLIDE 1.0f
static float glide_axis(float base, float pop_share, float crown_share){
    float f = 1.f + AI_ETHOS_GLIDE*(pop_share - crown_share);
    return base * clampf(f, 0.3f, 2.0f);
}
static void ai_refresh_ethos(AiActor *a, const World *w, const WorldEconomy *econ){
    int cp = (a->cid>=0 && a->cid<w->n_countries) ? w->country[a->cid].capital_prov : -1;
    int cr = (cp>=0 && cp<w->n_provinces) ? w->province[cp].region : -1;
    if (cr<0 || cr>=econ->n_regions) return;
    /* Le penchant du TRÔNE = celui de sa capitale (le siège du pouvoir) ; celui du
     * PEUPLE = la distribution de tout l'empire. Même source (les groupes) → un
     * empire homogène ne glisse PAS (capitale == empire), seule la diversité conquise
     * écarte les deux. */
    float crownlean[FAC_COUNT]; faction_weights_of(&econ->region[cr].pop, 1, crownlean);
    float pop[FAC_COUNT];       faction_effective_distribution(w, econ, a->cid, pop);  /* base + leviers (§4) */
    a->w_expand   = glide_axis(a->w_base[0], pop[FAC_CONQUERANT],    crownlean[FAC_CONQUERANT]);
    a->w_trade    = glide_axis(a->w_base[1], pop[FAC_MARCHAND],      crownlean[FAC_MARCHAND]);
    a->w_build    = glide_axis(a->w_base[2], pop[FAC_LEGISTE],       crownlean[FAC_LEGISTE]);
    a->w_faith    = glide_axis(a->w_base[3], pop[FAC_GARDIEN],       crownlean[FAC_GARDIEN]);
    a->w_faustian = glide_axis(a->w_base[4], pop[FAC_TRANSGRESSEUR], crownlean[FAC_TRANSGRESSEUR]);
}

void ai_actor_init(AiActor *a, const World *w, const WorldEconomy *econ,
                   int cid, uint32_t seed){
    memset(a, 0, sizeof(*a));
    a->cid = cid;
    a->rng = (seed ? seed : 0x9e3779b9u) ^ (uint32_t)(cid*2654435761u);
    if (a->rng==0) a->rng=1u;

    int cp = (cid>=0 && cid<w->n_countries) ? w->country[cid].capital_prov : -1;
    a->home_region = (cp>=0 && cp<w->n_provinces) ? w->province[cp].region : -1;

    const PopCulture *self = (a->home_region>=0 && a->home_region<econ->n_regions)
                           ? &econ->region[a->home_region].culture : NULL;
    ai_derive_weights(a, self);

    /* Cadences DÉCALÉES : chacun se réveille à un moment propre (pas de lockstep). */
    a->next_econ_day     = (int)(frand(&a->rng) * AI_ECON_CADENCE);
    a->next_strat_day    = (int)(frand(&a->rng) * AI_STRAT_CADENCE);
    a->next_research_day = (int)(frand(&a->rng) * AI_RESEARCH_CADENCE);
}

/* ===================================================================== */
/* OBSERVATION — l'IA lit les mêmes coordonnées que la membrane           */
/* ===================================================================== */
AiView ai_observe(const WorldProsperity *wp, const World *w,
                  const WorldEconomy *econ, int cid){
    AiView v; memset(&v, 0, sizeof v);
    if (cid<0 || cid>=wp->n_countries) return v;
    const CountryProsperity *cp = &wp->country[cid];
    v.SI=cp->SI; v.fragilite=cp->fragilite; v.fracture=cp->fracture;
    v.L=cp->L; v.K=cp->K; v.Dinf_interne=cp->profile.D_inf_int; v.PE=cp->P_realise;
    for (int r=0; r<econ->n_regions; r++) if (econ->region[r].owner==cid){
        v.tresor += econ->region[r].treasury;
        v.food   += econ->region[r].build.food_cap;
    }
    v.armee = diplo_mil_power(w, econ, cid);

    /* Fracture de VALEURS (§6) : si deux factions-éthos opposées se disputent la
     * direction du pays, la politique se déchire (paralysie interne). Lu de la
     * distribution de factions enracinée dans les peuples du pays. */
    { float fw[FAC_COUNT]; country_faction_weights(w, econ, cid, fw);
      v.ethos_fracture = faction_fracture(fw); }

    /* ── PERCEPTION DES BESOINS — ce qui MANQUE (l'IA était aveugle à tout ça) ──
     * Lu des MÊMES données que la membrane montre au joueur : capacités d'extraction,
     * stocks, demande/offre agrégées du pays. Aucune omniscience sur l'ennemi. */
    {
        static const Resource STRAT[3] = { RES_SALTPETER, RES_CELESTIAL_IRON, RES_ARCANE_CRYSTAL };
        float rawcap[RES_COUNT], stock[RES_COUNT], demand[RES_COUNT], supply[RES_COUNT];
        for (int g=0; g<RES_COUNT; g++){ rawcap[g]=stock[g]=demand[g]=supply[g]=0.f; }
        for (int r=0; r<econ->n_regions; r++) if (econ->region[r].owner==cid){
            const RegionEconomy *re=&econ->region[r];
            for (int g=1; g<RES_COUNT; g++){ rawcap[g]+=re->raw_cap[g]; stock[g]+=re->stock[g];
                demand[g]+=re->demand[g]; supply[g]+=re->supply[g]; }
        }
        /* TROU DE CHAÎNE : un raffineur présent dont un intrant manque (ni extrait, ni en stock). */
        Resource chain=RES_NONE; float chain_short=0.f;
        for (int r=0; r<econ->n_regions && chain==RES_NONE; r++) if (econ->region[r].owner==cid){
            const RegionEconomy *re=&econ->region[r];
            for (int i=0; i<re->n_bld && chain==RES_NONE; i++){
                Resource in1,in2,out; building_recipe(re->bld[i].type,&in1,&in2,&out);
                Resource ins[2]={in1,in2};
                for (int k=0;k<2;k++){ Resource g=ins[k]; if(g==RES_NONE) continue;
                    if (rawcap[g]<0.1f && supply[g]<0.5f && stock[g]<1.f){ chain=g; chain_short=1.f; break; } }
            }
        }
        v.chain_gap=chain;
        /* TROU STRATÉGIQUE : une matière qui débloque tech/militaire, extraite NULLE PART. */
        Resource strat=RES_NONE; float strat_short=0.f;
        for (int k=0;k<3;k++){ Resource g=STRAT[k]; if (rawcap[g]<0.1f && stock[g]<0.5f){ strat=g; strat_short=1.f; break; } }
        v.strat_gap=strat;
        /* TROU DE DEMANDE : un bien dont la demande dépasse nettement l'offre (panier non comblé,
         * variante culturelle comprise — la demande des minorités est déjà dans re->demand). */
        Resource dgap=RES_NONE; float dworst=0.f;
        for (int g=RES_PROD_FIRST; g<RES_COUNT; g++){
            float d=demand[g], s=supply[g]+stock[g];
            if (d>1.f && s < d*0.6f){ float sh=(d-s)/d; if (sh>dworst){ dworst=sh; dgap=g; } }
        }
        v.demand_gap=dgap;
        v.gap_acuity = clampf(0.5f*chain_short + 0.5f*strat_short + 0.6f*dworst, 0.f, 1.f);
        /* PRESSION DE PRISE : un trou stratégique, ou un brut de chaîne, INTROUVABLE chez soi
         * (rawcap nul) → on ne peut ni le produire : ne restent que PRENDRE ou COMMERCER. */
        float take=0.f;
        if (strat!=RES_NONE) take += 0.6f;
        if (chain!=RES_NONE && chain<RES_PROD_FIRST && rawcap[chain]<0.1f) take += 0.4f;
        v.take_pressure = clampf(take, 0.f, 1.f);
    }
    return v;
}

float ai_consolidation_pressure(const AiView *v){
    /* fragile : la stabilité s'effondre (ordre qui ne tient plus). */
    float fragile = (v->SI < 5.f) ? 1.f : (v->fragilite > 5.f ? 0.6f : 0.f);
    /* surextension : on a avalé plus de diversité que le K ne métabolise. */
    float surext  = clampf(v->Dinf_interne / fmaxf(v->K, 1.f) - 1.f, 0.f, 1.f);
    /* tendu : l'ordre tient surtout par la contrainte (fragilité haute). */
    float tendu   = clampf((v->fragilite - 5.f)/5.f, 0.f, 1.f);
    /* déchiré : deux factions-éthos opposées paralysent la direction (§6) — un frein
     * INTERNE, sœur de la surextension culturelle : la surexpansion ligue le monde
     * dehors, l'incohérence d'éthos te ligue dedans. PLANCHER : un mono-éthos garde
     * une fracture résiduelle (les penchants s'étalent) — seule la fracture AU-DELÀ
     * de ce socle (avaler des éthos divergents) freine, pas la cohésion ordinaire. */
    float dechire = clampf((v->ethos_fracture - AI_ETHOS_FRACTURE_FLOOR)
                           / (1.f - AI_ETHOS_FRACTURE_FLOOR), 0.f, 1.f) * AI_ETHOS_FRACTURE_W;
    float p = fragile; if (surext>p) p=surext; if (tendu>p) p=tendu; if (dechire>p) p=dechire;
    return clampf(p, 0.f, 1.f);
}

#define NEED_W 0.7f   /* poids de la pression de besoin sur l'agression (surface d'équilibrage) */
float ai_aggression(const AiActor *a, const AiView *v){
    float brake = ai_consolidation_pressure(v);
    float base  = a->w_expand + 0.5f*a->w_faith;
    /* L'agression ne lit plus QUE la fiche : un besoin AIGU dont le seul moyen
     * restant est PRENDRE (bien introuvable chez soi, donc à arracher) POUSSE à la
     * guerre — même un Mercantile bloqué escalade. Le frein la borne toujours
     * (un acteur fragile encaisse le manque plutôt que de se suicider). */
    float need_push = NEED_W * v->gap_acuity * v->take_pressure;
    return (base + need_push) * (1.f - brake);
}

/* ===================================================================== */
/* LECTEURS DE CIBLE (toujours sur des coordonnées existantes)            */
/* ===================================================================== */
static bool countries_adjacent(const WorldEconomy *econ, int a, int b){
    for (int r=0; r<econ->n_regions; r++) if (econ->region[r].owner==a)
        for (int s=0; s<econ->n_regions; s++)
            if (econ->region[s].owner==b && econ->adj[r][s]) return true;
    return false;
}

/* Meilleure cible de guerre : voisine, qu'on peut battre (pas de suicide),
 * pondérée par la menace qu'elle fait peser + un schisme si l'on est zélote. */
static int ai_pick_rival(const AiActor *a, const World *w, const WorldEconomy *econ,
                         const WorldProsperity *wp, const DiploState *diplo, float my_army,
                         Resource want){
    int best=-1; float bestscore=0.f;
    for (int b=0; b<w->n_countries; b++){
        if (b==a->cid) continue;
        if (w->country[b].role==POLITY_UNCLAIMED) continue;
        if (!countries_adjacent(econ, a->cid, b)) continue;
        if (diplo_status(diplo, a->cid, b)==DIPLO_ALLIED) continue;  /* on ne frappe pas un allié */
        if (!diplo_can_declare(diplo, a->cid, b)) continue;          /* TRÊVE : on n'enchaîne pas */
        if (diplo_casus_belli(w,econ,wp,diplo,a->cid,b,want)==CB_NONE) continue;  /* PAS DE CB → pas de guerre */
        float their_army = diplo_mil_power(w, econ, b);
        if (my_army < AI_ARMY_MARGIN*their_army) continue;     /* on n'attaque pas plus fort */
        Relation rel = diplo_relation(w, econ, wp, diplo, a->cid, b);
        float opportunism = my_army - their_army;              /* une proie faible = une occasion */
        /* FRICTION : frapper un protégé d'alliés puissants risque d'ÉLARGIR la
         * guerre → la cible se renchérit de la force alliée susceptible d'entrer.
         * Moins de guerres marginales ; le besoin aigu en vaut encore le risque. */
        float widen = diplo_war_widening_cost(w, econ, diplo, a->cid, b);
        /* On frappe ce qui MENACE — et, à proportion de l'appétit de conquête, ce
         * qui est FAIBLE. La RANCUNE pèse (on veut reprendre nos terres) ; la
         * parenté/alliance et le risque d'élargissement retiennent. */
        /* CROISADE : une foi orthodoxe a une CHANCE de frapper qui développe le
         * faustien — pesée par sa ferveur (w_faith). Gardiens vs Transgresseurs. */
        float crusade = diplo_faustian_cb(w,econ,diplo,a->cid,b) ? AI_CRUSADE_W*(0.4f+a->w_faith) : 0.f;
        float score = rel.threat
                    + a->w_expand * 3.0f * (opportunism>0.f ? opportunism : 0.f)
                    + a->w_faith  * 5.0f * rel.schism
                    + AI_RANCOR_W * diplo_rancor(diplo, a->cid, b)
                    + crusade
                    - rel.alliance
                    - AI_WIDEN_W * widen;
        if (score > bestscore){ bestscore=score; best=b; }
    }
    return best;
}

/* ---- Diplomatie d'équilibre : alliés, coalition (lectures, pas de script) -- */
static float allied_power(const World *w, const WorldEconomy *econ, const DiploState *d, int self){
    float p=0.f;
    for (int k=0;k<w->n_countries;k++) if (k!=self && diplo_status(d,self,k)==DIPLO_ALLIED)
        p += diplo_mil_power(w,econ,k);
    return p;
}
static bool country_at_war(const World *w, const DiploState *d, int c){
    for (int k=0;k<w->n_countries;k++) if (k!=c && diplo_status(d,c,k)==DIPLO_WAR) return true;
    return false;
}
/* L'allié naturel le plus fort (score d'alliance au-delà du seuil) — la friction
 * préventive : on se lie aux complémentaires/parents/menacés-communs. */
static int ai_pick_ally(const AiActor *a, const World *w, const WorldEconomy *econ,
                        const WorldProsperity *wp, const DiploState *d){
    int best=-1; float bestsc=AI_ALLY_SEUIL;
    for (int b=0;b<w->n_countries;b++){
        if (b==a->cid || w->country[b].role==POLITY_UNCLAIMED) continue;
        if (diplo_status(d,a->cid,b)!=DIPLO_NEUTRAL) continue;     /* déjà allié ou en guerre */
        if (!countries_adjacent(econ,a->cid,b)) continue;
        Relation rel=diplo_relation(w,econ,wp,d,a->cid,b);
        if (rel.alliance>bestsc){ bestsc=rel.alliance; best=b; }
    }
    return best;
}

/* Une région ennemie adjacente à conquérir (suppose la guerre déjà déclarée). */
static int ai_pick_enemy_region(const WorldEconomy *econ, const DiploState *diplo, int cid){
    /* §5 : la province ennemie adjacente la MOINS CHÈRE d'abord (on dépense le budget
     * de score efficacement : l'arrière-pays avant le cœur développé). */
    int best=-1; float bestp=0.f;
    for (int r=0; r<econ->n_regions; r++) if (econ->region[r].owner==cid)
        for (int s=0; s<econ->n_regions; s++){
            const RegionEconomy *re = &econ->region[s];
            if (!econ->adj[r][s]) continue;
            if (re->owner<0 || re->owner==cid) continue;
            if (!re->culture.settled) continue;
            if (diplo_status(diplo, cid, re->owner)!=DIPLO_WAR) continue;
            float price = diplo_province_price(econ, s);
            if (best<0 || price<bestp){ best=s; bestp=price; }
        }
    return best;
}

static float content_dist(const PopCulture *a, const PopCulture *b){
    float dv=fabsf(a->valeurs-b->valeurs),   ds=fabsf(a->subsistance-b->subsistance);
    float dp=fabsf(a->parente-b->parente),   dr=fabsf(a->religion-b->religion);
    float m=dv; if(ds>m)m=ds; if(dp>m)m=dp; if(dr>m)m=dr; return m;
}
/* Partenaire commercial : région étrangère peuplée dont la distance de contenu
 * approche le PIC de la cloche (D̄≈5 : le plus à échanger). On ne déduplique pas
 * — rouvrir la même artère, c'est l'INTENSIFIER (un négociant y revient). */
static int ai_pick_trade_partner(const WorldEconomy *econ, int home_region, int cid){
    if (home_region<0 || home_region>=econ->n_regions) return -1;
    const PopCulture *hc = &econ->region[home_region].culture;
    int best=-1; float bestgap=1e9f;
    for (int r=0; r<econ->n_regions; r++){
        const RegionEconomy *re = &econ->region[r];
        if (r==home_region || re->owner==cid) continue;
        if (!re->culture.settled || re->impassable) continue;
        float gap = fabsf(content_dist(hc, &re->culture) - 5.f);
        if (gap < bestgap){ bestgap=gap; best=r; }
    }
    return best;
}

/* Progression institutionnelle K : Tribunal → Chancellerie → Académie. */
static Edifice ai_next_k_edifice(const WorldEconomy *econ, int region){
    if (region<0 || region>=econ->n_regions) return EDI_TRIBUNAL;
    float k = econ->region[region].build.K_inst;
    if (k < 1.0f) return EDI_TRIBUNAL;
    if (k < 2.5f) return EDI_CHANCELLERIE;
    return EDI_ACADEMIE;
}
/* Progression coercitive H : Garnison → Forteresse → Citadelle (le chemin de
 * l'Ordre de Fer : tenir par la force au lieu de métaboliser). */
static Edifice ai_next_h_edifice(const WorldEconomy *econ, int region){
    if (region<0 || region>=econ->n_regions) return EDI_GARNISON;
    float h = econ->region[region].build.H_coerc;
    if (h < 1.0f) return EDI_GARNISON;
    if (h < 3.0f) return EDI_FORTERESSE;
    return EDI_CITADELLE;
}
/* Progression de foi : Sanctuaire → Temple → Cathédrale (sacraliser → SOUTIENT L). */
static Edifice ai_next_faith_edifice(const WorldEconomy *econ, int region){
    if (region<0 || region>=econ->n_regions) return EDI_SANCTUAIRE;
    float f = econ->region[region].build.faith;
    if (f < 1.0f) return EDI_SANCTUAIRE;
    if (f < 3.0f) return EDI_TEMPLE;
    return EDI_CATHEDRALE;
}
/* Progression du savoir : Bibliothèque → Monastère (recherche ; le monastère aussi foi). */
static Edifice ai_next_savoir_edifice(const WorldEconomy *econ, int region){
    if (region<0 || region>=econ->n_regions) return EDI_BIBLIOTHEQUE;
    return (econ->region[region].build.savoir < 1.5f) ? EDI_BIBLIOTHEQUE : EDI_MONASTERE;
}
#define AI_FAITH_L 3.0f   /* consentement DÉFAILLANT (Légit<30) → le trône se SACRALISE */
#define AI_SAVOIR_K 5.0f  /* institutions MÛRES (K élevé) → on investit le SAVOIR (recherche) */

/* ===================================================================== */
/* TOURS DE DÉCISION                                                       */
/* ===================================================================== */
/* §4 — la famille d'un édifice désigne la faction qu'il AVANCE (bâtir = voter). */
static EthosFaction ai_lever_for_edifice(Edifice e){
    switch (e){
        case EDI_GARNISON: case EDI_FORTERESSE: case EDI_CITADELLE:        return FAC_CONQUERANT;
        case EDI_SANCTUAIRE: case EDI_TEMPLE: case EDI_CATHEDRALE: case EDI_MONASTERE: return FAC_GARDIEN;
        case EDI_GRENIER: case EDI_IRRIGATION: case EDI_AQUEDUC:           return FAC_COMMUNAUTAIRE;
        case EDI_MARCHE: case EDI_ENTREPOT: case EDI_PORT: case EDI_CARAVANSERAIL:
        case EDI_COMPTOIR: case EDI_BANQUE:                               return FAC_MARCHAND;
        default:                                                          return FAC_LEGISTE;  /* Tribunal/Académie/Bibliothèque… */
    }
}

/* Économie : commercer OU bâtir (le frein réoriente l'énergie vers le K). */
static void ai_econ_turn(AiActor *a, WorldEconomy *econ, const AiView *v,
                         AgencyState *ag, RouteNetwork *rn, float brake){
    /* Famine d'abord : un peuple affamé ne bâtit ni cours ni comptoir. */
    if (v->food < AI_FOOD_FLOOR && a->home_region>=0){
        if (agency_build(ag, econ, a->home_region, EDI_GRENIER)) a->stats.builds_other++;
        return;
    }

    a->credit_trade += a->w_trade * (1.f - 0.5f*brake);
    a->credit_build += a->w_build + 0.8f*brake;           /* le frein POUSSE à consolider */

    /* On décharge le seau le plus plein (≥ 1). */
    if (a->credit_build>=1.f && a->credit_build>=a->credit_trade){
        a->credit_build -= 1.f;
        /* LE FORK (§ Soulèvements/Ordre de Fer) : sous une crise OUVERTE, un
         * tempérament COERCITIF (appétit de conquête haut) SERRE — il bâtit du H
         * (Garnison→Citadelle : tenir par la force, le chemin de l'Ordre de Fer)
         * au lieu de métaboliser. Les autres RÉFORMENT — ils bâtissent du K.
         * Aucun « si révolution alors » : c'est le même levier (bâtir), choisi
         * par la fiche ; le moteur d'ordre fait le verdict. */
        if (brake > AI_BRAKE_HARD && a->w_expand >= 0.60f){
            Edifice e = ai_next_h_edifice(econ, a->home_region);
            if (a->home_region>=0 && agency_build(ag, econ, a->home_region, e)){
                a->stats.builds_h++;
                faction_lever_apply(a->cid, ai_lever_for_edifice(e), AI_LEVER_BUILD);  /* §4 : bâtir = voter */
            }
        } else {
            /* RÉFORME : on métabolise (K). Mais un trône au consentement bas se
             * SACRALISE d'abord (la foi soutient L) ; institutions mûres, on
             * investit le SAVOIR (la recherche). §4-§5 du catalogue. */
            Edifice e;
            int hr = a->home_region;
            const ProvBuild *bd = (hr>=0&&hr<econ->n_regions)?&econ->region[hr].build:NULL;
            if (bd && v->L < AI_FAITH_L && bd->faith < 5.0f)
                e = ai_next_faith_edifice(econ, hr);       /* consentement défaillant → foi */
            else if (bd && bd->K_inst >= AI_SAVOIR_K && bd->savoir < 2.5f)
                e = ai_next_savoir_edifice(econ, hr);      /* institutions mûres → savoir */
            else
                e = ai_next_k_edifice(econ, hr);           /* le métabolisme par défaut : K */
            if (a->home_region>=0 && agency_build(ag, econ, a->home_region, e)){
                /* développement institutionnel PROACTIF (la marque du Bâtisseur)
                 * vs DIGESTION imposée par le frein — on ne les confond pas. */
                if (brake > AI_BRAKE_HARD) a->stats.builds_other++;
                else                       a->stats.builds_k++;
                faction_lever_apply(a->cid, ai_lever_for_edifice(e), AI_LEVER_BUILD);  /* §4 : la famille d'édifice vote */
            }
        }
    } else if (a->credit_trade>=1.f){
        a->credit_trade -= 1.f;
        int p = ai_pick_trade_partner(econ, a->home_region, a->cid);
        if (p>=0 && routes_order(rn, econ, a->home_region, p, false)){
            a->stats.routes++;
            faction_lever_apply(a->cid, FAC_MARCHAND, AI_LEVER_BUILD);   /* §4 : le négoce AVANCE les Marchands */
        } else if (a->home_region>=0 && agency_build(ag, econ, a->home_region, EDI_MARCHE)){
            a->stats.builds_other++;                       /* pas de partenaire : on bâtit le carrefour */
            faction_lever_apply(a->cid, FAC_MARCHAND, AI_LEVER_BUILD);
        }
    }
}

/* Le BIEN que l'IA veut arracher (pour le casus belli économique) : son trou le
 * plus aigu (stratégique → demande → chaîne). */
static Resource ai_war_want(const AiView *v){
    if (v->strat_gap !=RES_NONE) return v->strat_gap;
    if (v->demand_gap!=RES_NONE) return v->demand_gap;
    return v->chain_gap;
}

static int ai_owned_regions(const WorldEconomy *econ, int cid){
    int n=0; for (int r=0;r<econ->n_regions;r++) if (econ->region[r].owner==cid) n++; return n;
}
/* (Le « coup de grâce » par surcoût de capitale est désormais SUBSUMÉ par le §5 combat :
 * la capitale, cœur développé, coûte cher → seule une victoire décisive — budget de score
 * couvrant tout le territoire — l'arrache. Voir diplo_province_price / diplo_war_budget.) */

/* Stratégie : conquérir, déclarer la guerre, ou CONSOLIDER (le frein). */
static void ai_strat_turn(AiActor *a, World *w, WorldEconomy *econ, WorldProsperity *wp,
                          WorldLegitimacy *wl, DiploState *diplo, const AiView *v,
                          float brake, int day){
    if (ai_owned_regions(econ, a->cid)==0) return;      /* polité ABSORBÉE : inerte (plus de stratégie) */
    /* FREIN DUR : on a trop avalé / l'ordre craque → paix générale + verrou. */
    if (brake > AI_BRAKE_HARD){
        a->credit_consolidate += brake;
        if (a->credit_consolidate >= 1.f){
            a->credit_consolidate -= 1.f;
            for (int b=0; b<w->n_countries; b++)
                if (b!=a->cid && diplo_status(diplo, a->cid, b)==DIPLO_WAR)
                    diplo_make_peace(diplo, a->cid, b);
            a->peace_lock_until = day + AI_PEACE_LOCK;       /* hystérésis : on digère */
            a->stats.consolidations++;
        }
        return;
    }

    /* REDDITION (§3) : si l'on est DÉFENSEUR dans une guerre nettement PERDUE (le
     * bras-de-fer penche fort vers l'attaquant) et militairement sans espoir → on
     * CAPITULE plutôt que de se faire anéantir. (L'IA lit le score + l'armée + le frein.) */
    for (int b=0; b<w->n_countries; b++){
        if (b==a->cid || diplo_status(diplo,a->cid,b)!=DIPLO_WAR) continue;
        if (diplo_war_goal(diplo,b,a->cid)==CB_NONE) continue;          /* b est l'attaquant */
        float their_score = diplo_war_score(diplo, b, a->cid);
        if (their_score >= AI_SURRENDER && v->armee < AI_ARMY_MARGIN*diplo_mil_power(w,econ,b)){
            /* §5 : si la victoire de b est DÉCISIVE (son budget de score couvre une bonne
             * part de notre territoire), nul tribut ne l'arrête — il veut TOUT, et son
             * budget enfle d'occupation à mesure qu'il prend → annexion. Sinon on capitule. */
            if (diplo_war_budget(diplo,w,econ,b,a->cid) >= AI_ANNEX_FRAC*diplo_country_value(econ,a->cid)) continue;
            diplo_reparations(diplo, w, econ, a->cid, b);               /* le vaincu indemnise le vainqueur */
            diplo_make_peace(diplo, a->cid, b);                         /* capitulation */
        }
    }

    if (day < a->peace_lock_until) return;                  /* on tient la paix (digestion) */

    /* Appétit agressif, gaté par le frein (mou) et nourri par la foi. */
    a->credit_war += ai_aggression(a, v);
    if (a->credit_war < 1.f) return;

    /* Déjà en guerre ? Priorité : ENCAISSER (conquérir une région ennemie). On
     * ne multiplie pas les fronts — un seul à la fois. */
    int at_war = 0;
    for (int b=0; b<w->n_countries; b++)
        if (b!=a->cid && diplo_status(diplo, a->cid, b)==DIPLO_WAR) at_war++;
    if (at_war>0){
        int enemy=-1;
        for (int b=0; b<w->n_countries; b++)
            if (b!=a->cid && diplo_status(diplo, a->cid, b)==DIPLO_WAR){ enemy=b; break; }
        CasusBelli goal = (enemy>=0)? diplo_war_goal(diplo, a->cid, enemy) : CB_TERRITORIAL;
        int er = ai_pick_enemy_region(econ, diplo, a->cid);
        if (er>=0){
            int victim = econ->region[er].owner;
            /* §5 COMBAT — LE SCORE DE GUERRE EST UN BUDGET dépensé sur des provinces
             * TARIFÉES par leur valeur développée. On n'achète une province que si le
             * budget couvre le prix cumulé : un cœur développé (cher) exige une victoire
             * DÉCISIVE ; sous le budget, il est PROTÉGÉ (on signe). Petits pays bon marché
             * → absorbables ; grands cœurs riches → tempérés par la conséquence, sans plafond. */
            float price  = diplo_province_price(econ, er);
            float budget = (victim>=0)? diplo_war_budget(diplo, w, econ, a->cid, victim) : 0.f;
            float spent  = (victim>=0 && victim<SCPS_MAX_COUNTRY)? diplo->conq_value[a->cid][victim] : 0.f;
            bool territorial = (goal==CB_TERRITORIAL || goal==CB_NONE);
            if (territorial && victim>=0 && spent + price > budget){
                /* trop cher pour cette victoire → on banque le gain ET, du budget restant
                 * (« les 95 % »), on VIDE les coffres du vaincu, puis on signe. */
                diplo_loot(w, econ, a->cid, victim, budget - spent);
                diplo_reparations(diplo, w, econ, a->cid, victim);
                diplo_make_peace(diplo, a->cid, victim);
            } else if (diplo_conquer_region(diplo, w, econ, wl, a->cid, er, a->can_enslave)){
                a->credit_war -= 1.f; a->stats.conquests++;
                faction_lever_apply(a->cid, FAC_CONQUERANT, AI_LEVER_WAR);  /* §4 : la guerre AVANCE les Conquérants */
                /* non-territorial (humiliation/source/foi) : SATISFAIT par UNE prise.
                 * territorial : on poursuit tant que le budget de score n'est pas épuisé
                 * (la prochaine province, si trop chère, déclenchera la paix ci-dessus). */
                if (!territorial && enemy>=0){
                    diplo_reparations(diplo, w, econ, a->cid, enemy);
                    diplo_make_peace(diplo, a->cid, enemy);
                }
            }
        } else {
            /* plus de territoire ennemi adjacent : la guerre est GAGNÉE → le budget restant
             * VIDE les coffres, indemnité au passage, et l'on signe (autre cible plus tard). */
            for (int b=0; b<w->n_countries; b++)
                if (b!=a->cid && diplo_status(diplo, a->cid, b)==DIPLO_WAR){
                    float lo = diplo_war_budget(diplo,w,econ,a->cid,b)
                             - ((b<SCPS_MAX_COUNTRY)? diplo->conq_value[a->cid][b] : 0.f);
                    diplo_loot(w, econ, a->cid, b, lo);
                    diplo_reparations(diplo, w, econ, a->cid, b);
                    diplo_make_peace(diplo, a->cid, b);
                }
        }
        return;
    }

    /* En paix : ÉQUILIBRE avant prédation (rétroaction négative, jamais d'interdit). */

    /* (1) COALITION — se liguer contre l'HÉGÉMON perçu (anti-runaway, émergent des
     * menaces sommées). On se joint si l'hégémon GUERROIE déjà (pile-on) et que
     * notre camp (soi + alliés) pèse assez. Le frein gouverne : un fragile n'ose pas. */
    int heg = diplo_perceived_hegemon(w, econ, wp, diplo, a->cid);
    if (heg>=0 && heg!=a->cid && diplo_status(diplo,a->cid,heg)==DIPLO_NEUTRAL
        && diplo_can_declare(diplo,a->cid,heg) && country_at_war(w,diplo,heg)){
        float my_side = v->armee + allied_power(w,econ,diplo,a->cid);
        CasusBelli cb = diplo_casus_belli(w,econ,wp,diplo,a->cid,heg, ai_war_want(v));
        if (cb!=CB_NONE && my_side >= AI_ARMY_MARGIN*diplo_mil_power(w,econ,heg)){
            diplo_declare_war_cb(diplo, a->cid, heg, cb);   /* la ligue a une raison (souvent territoriale) */
            a->credit_war -= 1.f; a->stats.wars++;
            return;
        }
    }

    /* (2) ALLIANCE EN ACTE — se lier à l'allié naturel le plus fort (friction
     * préventive : la menace partagée et le complément écrasent la prédation mutuelle). */
    int ally = ai_pick_ally(a, w, econ, wp, diplo);
    if (ally>=0){
        diplo_form_alliance(diplo, a->cid, ally);
        a->credit_war = fmaxf(0.f, a->credit_war - 0.5f);    /* l'énergie passe à se lier */
        return;
    }

    /* (3) PRÉDATION — la meilleure cible (lue) : hors trêve, hors allié, AVEC un
     * casus belli qui colle au but, friction d'élargissement comprise. */
    Resource want = ai_war_want(v);
    int rival = ai_pick_rival(a, w, econ, wp, diplo, v->armee, want);
    if (rival>=0){
        CasusBelli cb = diplo_casus_belli(w,econ,wp,diplo,a->cid,rival, want);
        diplo_declare_war_cb(diplo, a->cid, rival, cb);   /* la guerre a une RAISON (gate la paix) */
        a->credit_war -= 1.f; a->stats.wars++;
    }
}

/* ===================================================================== */
/* RECHERCHE — l'arbre de tech vivant (buts + penchant de race + frein)     */
/* ===================================================================== */
static SpeciesArchetype ai_capital_race(const World *w, const WorldEconomy *econ, int cid){
    if (cid<0||cid>=w->n_countries) return RACE_HUMAIN;
    int cp=w->country[cid].capital_prov;
    if (cp<0||cp>=w->n_provinces) return RACE_HUMAIN;
    int cr=w->province[cp].region;
    if (cr<0||cr>=econ->n_regions) return RACE_HUMAIN;
    return econ->region[cr].culture.race;
}
/* §4 RELIGION — la posture de la foi régnante sur l'interdit [0..1] (orthodoxe bas
 * ↔ culte haut), lue de l'éthos de la culture-capitale (même barème que scps_faith).
 * L'orthodoxe INTERDIT le faustien (sacrilège) ; le culte le SACRALISE. */
static float ai_faith_stance(const World *w, const WorldEconomy *econ, int cid){
    if (cid<0||cid>=w->n_countries) return 0.25f;
    int cp=w->country[cid].capital_prov; if (cp<0||cp>=w->n_provinces) return 0.25f;
    int cr=w->province[cp].region;       if (cr<0||cr>=econ->n_regions) return 0.25f;
    switch (econ->region[cr].culture.ethos){
        case ETHOS_DOMINATEUR: return 0.36f; case ETHOS_HONNEUR:  return 0.30f;
        case ETHOS_MERCANTILE: return 0.26f; case ETHOS_PACIFISTE:return 0.20f;
        case ETHOS_BUREAUCRATE:return 0.14f; case ETHOS_ORDRE:    return 0.10f;
        default:               return 0.20f;
    }
}
float ai_country_population(const World *w, const WorldEconomy *econ, int cid){
    float pop=0.f; (void)w;
    for (int r=0;r<econ->n_regions;r++) if (econ->region[r].owner==cid){
        const RegionEconomy *re=&econ->region[r];
        for (int k=0;k<CLASS_COUNT;k++) pop += re->strata[k].pop;
    }
    return pop;
}
unsigned ai_race_access(const World *w, const WorldEconomy *econ, int cid){
    unsigned m = tech_race_bit(ai_capital_race(w,econ,cid));        /* sa propre race, toujours */
    for (int r=0;r<econ->n_regions;r++) if (econ->region[r].owner==cid){
        const RegionEconomy *re=&econ->region[r];
        m |= tech_race_bit(re->culture.race);                      /* la culture dominante */
        for (int g=0;g<re->pop.n_groups;g++)
            m |= tech_race_bit(re->pop.groups[g].race);            /* groupes conquis/migrés → diffusion */
    }
    return m;
}

/* Le nœud à déverrouiller : score = BUTS (la fonction répond au besoin lu) +
 * PENCHANT de race (biais vers son thème + ses signatures) − FREIN (le faustien
 * n'est pris que si la pente dépasse le frein). Aucun « si race==X ». */
static TechId ai_pick_tech(const AiActor *a, const TechState *ts, const World *w,
                           const WorldEconomy *econ, const WorldProsperity *wp,
                           unsigned access, float pop){
    AiView v = ai_observe(wp, w, econ, a->cid);
    float brake = ai_consolidation_pressure(&v);
    TechTheme affinity = tech_race_affinity(ai_capital_race(w,econ,a->cid));
    float faith_stance = ai_faith_stance(w,econ,a->cid);   /* §4 : orthodoxe interdit, culte sacralise */
    TechId best=TECH_COUNT; float bestscore=-1e30f;
    for (int i=0;i<TECH_COUNT;i++){
        TechId id=(TechId)i;
        if (!tech_can_research(ts,id,access)) continue;
        float cost=tech_cost(id,pop);
        if (cost > ts->research_points + 0.01f) continue;          /* pas encore les moyens */
        const TechNode *n=tech_node(id);
        float score=0.f;
        /* BUTS — la fonction du nœud répond à un besoin lu de la VUE (pas de script). */
        if (n->func==FN_ARMEE)        score += 1.2f*a->w_expand + 2.0f*v.take_pressure + 0.25f*n->dMil;
        if (n->func==FN_PRODUCTION)   score += 1.0f*a->w_trade  + 1.5f*v.gap_acuity   + 0.25f*n->dEco;
        if (n->func==FN_RENFORCEMENT){ score += 1.0f*a->w_build + 0.4f*n->dK + 0.3f*n->dL;
            if (n->dFracture<0.f) score += 0.05f*v.fracture; }                 /* anti-fracture si fracturé */
        if (n->theme==THM_SOCIETE && n->func==FN_RENFORCEMENT) score += 0.6f*a->w_faith;
        /* PENCHANT de race — biais, jamais un gate. */
        if (n->theme==affinity)    score += AI_TECH_PENCHANT;
        if (n->native!=RACE_COUNT) score += AI_TECH_SIGNATURE;     /* une signature accessible se prend */
        /* FREIN — le faustien rapproche la Brèche : pris seulement si la pente l'emporte. */
        if (n->faustian){          /* la pente faustienne, FREINÉE ou BÉNIE par la foi (§4) */
            float religious = (faith_stance - 0.5f)*2.f;   /* −1 orthodoxe (sacrilège) … +1 culte */
            score += AI_TECH_FAUSTIAN*(a->w_faustian - brake) - 0.3f*n->charge + AI_FAITH_FAUSTIAN*religious;
        }
        score -= 0.002f*cost;      /* à score égal : le plus proche (le moins cher) d'abord */
        if (score>bestscore){ bestscore=score; best=id; }
    }
    return best;
}

float ai_research_income(const TechState *ts, float pop){
    if (!ts) return 0.f;
    return (AI_RESEARCH_RATE/365.f) * tech_research_yield(ts) * (1.f + pop/AI_RESEARCH_POPREF);
}

void ai_research_step(AiActor *a, TechState *ts, const World *w,
                      const WorldEconomy *econ, const WorldProsperity *wp, int day){
    if (!ts || day < a->next_research_day) return;
    a->next_research_day = day + AI_RESEARCH_CADENCE;
    float pop = ai_country_population(w, econ, a->cid);
    /* ASSIETTE : la pop PRODUIT la recherche — et la renchérit (tech_cost) → équilibre. */
    float income = (AI_RESEARCH_RATE/365.f)*AI_RESEARCH_CADENCE
                 * tech_research_yield(ts) * (1.f + pop/AI_RESEARCH_POPREF);
    ts->research_points += income;
    unsigned access = ai_race_access(w, econ, a->cid);
    TechId pick = ai_pick_tech(a, ts, w, econ, wp, access, pop);
    if (pick!=TECH_COUNT){
        float cost = tech_cost(pick, pop);
        if (ts->research_points >= cost && tech_research(ts, pick, access)){
            ts->research_points -= cost;
            a->stats.techs++;
            if (tech_node(pick)->faustian){ a->stats.techs_faustian++;
                faction_lever_apply(a->cid, FAC_TRANSGRESSEUR, AI_LEVER_TECH);  /* §4 : franchir l'interdit AVANCE les Transgresseurs */
            }
        }
    }
    a->can_enslave = ts->unlocked[TECH_ESCLAVAGE];   /* §4c : le gate de l'esclavage suit la tech */
}

/* ===================================================================== */
/* LE TICK : dormir → lire → choisir un levier → agir (verbes du joueur)   */
/* ===================================================================== */
void ai_step(AiActor *a, World *w, WorldEconomy *econ, WorldProsperity *wp,
             WorldLegitimacy *wl, AgencyState *ag, RouteNetwork *rn,
             DiploState *diplo, int day){
    if (a->cid<0 || a->cid>=w->n_countries) return;
    bool econ_due  = (day >= a->next_econ_day);
    bool strat_due = (day >= a->next_strat_day);
    if (!econ_due && !strat_due) return;

    AiView v = ai_observe(wp, w, econ, a->cid);
    float brake = ai_consolidation_pressure(&v);

    if (econ_due){
        ai_econ_turn(a, econ, &v, ag, rn, brake);
        a->next_econ_day = day + AI_ECON_CADENCE/2 + (int)(frand(&a->rng)*AI_ECON_CADENCE);
    }
    if (strat_due){
        ai_refresh_ethos(a, w, econ);   /* §3 : l'éthos effectif GLISSE avec la composition avant d'agir */
        ai_strat_turn(a, w, econ, wp, wl, diplo, &v, brake, day);
        a->next_strat_day = day + AI_STRAT_CADENCE/2 + (int)(frand(&a->rng)*AI_STRAT_CADENCE);
    }
}
