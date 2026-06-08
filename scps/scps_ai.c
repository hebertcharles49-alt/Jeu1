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
#include <string.h>
#include <math.h>

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
    a->next_econ_day  = (int)(frand(&a->rng) * AI_ECON_CADENCE);
    a->next_strat_day = (int)(frand(&a->rng) * AI_STRAT_CADENCE);
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
    float p = fragile; if (surext>p) p=surext; if (tendu>p) p=tendu;
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
        float score = rel.threat
                    + a->w_expand * 3.0f * (opportunism>0.f ? opportunism : 0.f)
                    + a->w_faith  * 5.0f * rel.schism
                    + AI_RANCOR_W * diplo_rancor(diplo, a->cid, b)
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
    for (int r=0; r<econ->n_regions; r++) if (econ->region[r].owner==cid)
        for (int s=0; s<econ->n_regions; s++){
            const RegionEconomy *re = &econ->region[s];
            if (!econ->adj[r][s]) continue;
            if (re->owner<0 || re->owner==cid) continue;
            if (!re->culture.settled) continue;
            if (diplo_status(diplo, cid, re->owner)!=DIPLO_WAR) continue;
            return s;
        }
    return -1;
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
/* Économie : commercer OU bâtir (le frein réoriente l'énergie vers le K). */
static void ai_econ_turn(AiActor *a, WorldEconomy *econ, const AiView *v,
                         AgencyState *ag, RouteNetwork *rn, float brake){
    /* Famine d'abord : un peuple affamé ne bâtit ni cours ni comptoir. */
    if (v->food < AI_FOOD_FLOOR && a->home_region>=0){
        if (agency_order_build(ag, a->home_region, EDI_GRENIER)) a->stats.builds_other++;
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
            if (a->home_region>=0 && agency_order_build(ag, a->home_region, e)) a->stats.builds_h++;
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
            if (a->home_region>=0 && agency_order_build(ag, a->home_region, e)){
                /* développement institutionnel PROACTIF (la marque du Bâtisseur)
                 * vs DIGESTION imposée par le frein — on ne les confond pas. */
                if (brake > AI_BRAKE_HARD) a->stats.builds_other++;
                else                       a->stats.builds_k++;
            }
        }
    } else if (a->credit_trade>=1.f){
        a->credit_trade -= 1.f;
        int p = ai_pick_trade_partner(econ, a->home_region, a->cid);
        if (p>=0 && routes_order(rn, econ, a->home_region, p, false)){
            a->stats.routes++;
        } else if (a->home_region>=0 && agency_order_build(ag, a->home_region, EDI_MARCHE)){
            a->stats.builds_other++;                       /* pas de partenaire : on bâtit le carrefour */
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

/* Stratégie : conquérir, déclarer la guerre, ou CONSOLIDER (le frein). */
static void ai_strat_turn(AiActor *a, World *w, WorldEconomy *econ, WorldProsperity *wp,
                          WorldLegitimacy *wl, DiploState *diplo, const AiView *v,
                          float brake, int day){
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
            if (diplo_conquer_region(diplo, w, econ, wl, a->cid, er)){
                a->credit_war -= 1.f; a->stats.conquests++;
                /* §5 PAIX PROPORTIONNELLE : un casus belli non-territorial est SATISFAIT
                 * par une prise (la source / l'humiliation) ; le territorial ENCAISSE sa
                 * REVENDICATION (∝ domination militaire) puis SIGNE — l'IA banque le gain
                 * légitime plutôt que de sur-étendre (prendre au-delà ligue le monde). */
                bool done = (goal!=CB_TERRITORIAL) ||
                            (enemy>=0 && diplo->conquered[a->cid][enemy]
                                          >= diplo_war_claim(diplo,w,econ,a->cid,enemy));
                if (done && enemy>=0){
                    diplo_reparations(diplo, w, econ, a->cid, enemy);   /* le vaincu indemnise */
                    diplo_make_peace(diplo, a->cid, enemy);
                }
            }
        } else {
            /* plus de territoire ennemi adjacent : la guerre est GAGNÉE → on signe la
             * paix (indemnité au passage) et l'on pourra viser une autre cible plus tard. */
            for (int b=0; b<w->n_countries; b++)
                if (b!=a->cid && diplo_status(diplo, a->cid, b)==DIPLO_WAR){
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
        ai_strat_turn(a, w, econ, wp, wl, diplo, &v, brake, day);
        a->next_strat_day = day + AI_STRAT_CADENCE/2 + (int)(frand(&a->rng)*AI_STRAT_CADENCE);
    }
}
