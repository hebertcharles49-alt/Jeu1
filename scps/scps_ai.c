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
#define AI_FOOD_FLOOR     1.5f   /* sous ce seuil de marge : grenier d'abord */
#define AI_BRAKE_HARD     0.6f   /* frein dur : consolidation impérative     */

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

float ai_aggression(const AiActor *a, const AiView *v){
    float brake = ai_consolidation_pressure(v);
    return (a->w_expand + 0.5f*a->w_faith) * (1.f - brake);
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
                         const WorldProsperity *wp, const DiploState *diplo, float my_army){
    int best=-1; float bestscore=0.f;
    for (int b=0; b<w->n_countries; b++){
        if (b==a->cid) continue;
        if (w->country[b].role==POLITY_UNCLAIMED) continue;
        if (!countries_adjacent(econ, a->cid, b)) continue;
        float their_army = diplo_mil_power(w, econ, b);
        if (my_army < AI_ARMY_MARGIN*their_army) continue;     /* on n'attaque pas plus fort */
        Relation rel = diplo_relation(w, econ, wp, diplo, a->cid, b);
        float opportunism = my_army - their_army;              /* une proie faible = une occasion */
        /* On frappe ce qui MENACE — et, à proportion de l'appétit de conquête, ce
         * qui est FAIBLE. Un peuple pacifique ne préda­te pas un voisin sans défense ;
         * un Dominateur, si. La « parenté/alliance » retient (un peu) la main. */
        float score = rel.threat
                    + a->w_expand * 3.0f * (opportunism>0.f ? opportunism : 0.f)
                    + a->w_faith  * 5.0f * rel.schism
                    - rel.alliance;
        if (score > bestscore){ bestscore=score; best=b; }
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
/* Progression de foi : Sanctuaire → Temple (sacraliser le trône → SOUTIENT L). */
static Edifice ai_next_faith_edifice(const WorldEconomy *econ, int region){
    if (region<0 || region>=econ->n_regions) return EDI_SANCTUAIRE;
    return (econ->region[region].build.faith < 1.0f) ? EDI_SANCTUAIRE : EDI_TEMPLE;
}
#define AI_FAITH_L 4.5f   /* sous ce consentement, le trône se SACRALISE (bâtit la foi) */

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
             * SACRALISE d'abord (la foi soutient L sans réprimer — §4 catalogue). */
            Edifice e;
            if (v->L < AI_FAITH_L && a->home_region>=0
                && econ->region[a->home_region].build.faith < 3.0f)
                e = ai_next_faith_edifice(econ, a->home_region);
            else
                e = ai_next_k_edifice(econ, a->home_region);
            if (a->home_region>=0 && agency_order_build(ag, a->home_region, e)){
                /* K de TEMPÉRAMENT (proactif, la marque du Bâtisseur) vs K de DIGESTION
                 * (réactif, imposé par le frein quand on a trop avalé) — on ne les
                 * confond pas : la personnalité se lit dans le premier. */
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
        int er = ai_pick_enemy_region(econ, diplo, a->cid);
        if (er>=0){
            if (diplo_conquer_region(diplo, w, econ, wl, a->cid, er)){
                a->credit_war -= 1.f; a->stats.conquests++;
            }
        } else {
            /* plus de territoire ennemi adjacent : la guerre est GAGNÉE → on signe
             * la paix (et l'on pourra viser une autre cible au prochain réveil). */
            for (int b=0; b<w->n_countries; b++)
                if (b!=a->cid && diplo_status(diplo, a->cid, b)==DIPLO_WAR)
                    diplo_make_peace(diplo, a->cid, b);
        }
        return;
    }

    /* En paix : on OUVRE une guerre contre la meilleure cible (lue, pas scriptée). */
    int rival = ai_pick_rival(a, w, econ, wp, diplo, v->armee);
    if (rival>=0){
        diplo_declare_war(diplo, a->cid, rival);
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
