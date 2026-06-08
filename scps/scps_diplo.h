#ifndef SCPS_DIPLO_H
#define SCPS_DIPLO_H
/*
 * scps_diplo.h — DIPLOMATIE (§6) & GUERRE (§5)
 *
 * Les relations se LISENT (lecteurs), jamais ne se posent à la main :
 *   - threat     = (eco + mil) / (distance + projection)   [ta formule]
 *   - complement = complémentarité de ressources (durable)
 *   - kinship    = distance de sphère (du même sang … étranger d'une autre sphère)
 *   - schism     = branche religieuse proche + prosélytisme = ennemi naturel
 *   - alliance   = menace partagée (transitoire) + complément (durable) − valeurs − schisme
 *
 * Guerre = acquisition de territoire ET montée de la diversité interne :
 * conquérir une culture lointaine monte le D̄ du conquérant → fracture tant que
 * K ne métabolise pas. La récompense est gatée par le K.
 */
#include "scps_world.h"
#include "scps_econ.h"
#include "scps_prosperity.h"
#include "scps_legitimacy.h"

typedef struct {
    float threat;       /* menace de b sur a */
    float complement;   /* complémentarité de ressources [0..1] */
    float kinship;      /* distance de sphère [0..7] (haut = étranger) */
    float schism;       /* ennemi naturel religieux [0..1] */
    float alliance;     /* score d'alliance (haut = allié naturel) */
} Relation;

typedef enum { DIPLO_NEUTRAL = 0, DIPLO_ALLIED, DIPLO_WAR } DiploStatus;

/* CASUS BELLI — la guerre a une RAISON (lue de la relation) ; son type fixe le
 * BUT de guerre et ce qui est exigible à la paix (un frein de plus). */
typedef enum {
    CB_NONE = 0,
    CB_TERRITORIAL,   /* adjacence / revendication / province perdue → prend des provinces */
    CB_RELIGIOUS,     /* schisme + prosélytisme → humiliation (peu/pas de terre) */
    CB_ECONOMIC,      /* un bien aigu MONOPOLISÉ par la cible → la province-source */
    CB_SUBJUGATION    /* menace + projection → vassalité (pas d'annexion massive) */
} CasusBelli;

typedef struct {
    DiploStatus status[SCPS_MAX_COUNTRY][SCPS_MAX_COUNTRY];
    float       war_years[SCPS_MAX_COUNTRY][SCPS_MAX_COUNTRY];
    float       truce[SCPS_MAX_COUNTRY][SCPS_MAX_COUNTRY];  /* jours d'interdiction de guerre (fond) */
    float       momentum[SCPS_MAX_COUNTRY];                 /* conquêtes RÉCENTES (décroît) → fulgurance perçue */
    int8_t      cb[SCPS_MAX_COUNTRY][SCPS_MAX_COUNTRY];     /* casus belli ACTIF de a contre b (but de guerre) */
    /* SCORE DE GUERRE — le bras-de-fer (a = ATTAQUANT, celui qui a le CB) :
     * batailles (∝ avantage militaire, PLAFONNÉ +50) + occupation (provinces prises,
     * l'autre +50→+100) ; le défenseur pousse vers −100 par l'attrition. */
    float       battle_score[SCPS_MAX_COUNTRY][SCPS_MAX_COUNTRY];  /* [-100 .. +50] */
    int16_t     conquered  [SCPS_MAX_COUNTRY][SCPS_MAX_COUNTRY];   /* régions prises ce conflit (occupation) */
} DiploState;

void diplo_init(DiploState *d);

/* ---- Lecteurs ---------------------------------------------------------- */
float    diplo_eco_power(const WorldProsperity *wp, int cid);
float    diplo_mil_power(const World *w, const WorldEconomy *econ, int cid);
Relation diplo_relation (const World *w, const WorldEconomy *econ,
                         const WorldProsperity *wp, const DiploState *d, int a, int b);

/* ---- Actions ----------------------------------------------------------- */
void        diplo_declare_war (DiploState *d, int a, int b);
/* Le CASUS BELLI inhérent le plus pertinent de a contre b (lu de la relation +
 * du besoin `want` : un bien aigu monopolisé par b → CB économique). CB_NONE si
 * aucune raison ne tient → l'IA ne peut pas déclarer (elle renonce ou attend). */
CasusBelli  diplo_casus_belli (const World *w, const WorldEconomy *econ,
                               const WorldProsperity *wp, const DiploState *d,
                               int a, int b, Resource want);
/* Déclare la guerre AVEC un but (le CB est mémorisé → il gate la paix). */
void        diplo_declare_war_cb(DiploState *d, int a, int b, CasusBelli cb);
CasusBelli  diplo_war_goal     (const DiploState *d, int a, int b);
const char *diplo_cb_name      (CasusBelli cb);
void        diplo_form_alliance(DiploState *d, int a, int b);
void        diplo_make_peace  (DiploState *d, int a, int b);
DiploStatus diplo_status      (const DiploState *d, int a, int b);
/* Peut-on déclarer la guerre ? false pendant la TRÊVE (espace l'enchaînement). */
bool        diplo_can_declare (const DiploState *d, int a, int b);
float       diplo_truce_days  (const DiploState *d, int a, int b);   /* lecture (UI/IA) */

/* ---- Diplomatie d'ÉQUILIBRE (rétroaction négative, pas d'interdiction) ----- *
 * Coût d'élargissement : frapper un protégé d'alliés puissants risque d'étendre
 * la guerre → renchérit la cible (somme des forces alliées susceptibles d'entrer). */
float diplo_war_widening_cost(const World *w, const WorldEconomy *econ,
                              const DiploState *d, int attacker, int target);
/* La menace dominante perçue par `self` ; renvoie -1 si aucune ne franchit le
 * seuil de coalition. Une coalition ÉMERGE quand un même hégémon dépasse ce seuil
 * pour plusieurs royaumes (aucun script, juste des lectures de menace sommées). */
int   diplo_perceived_hegemon(const World *w, const WorldEconomy *econ,
                              const WorldProsperity *wp, const DiploState *d, int self);

/* Conquête (§5) : transfère une région ennemie au conquérant (suppose la guerre
 * et l'issue militaire favorable). L'owner change → la diversité du conquérant
 * monte ; L de la région s'effondre (puis réintègre sur des générations).
 * Renvoie true si la conquête a lieu. */
bool diplo_conquer_region(DiploState *d, World *w, WorldEconomy *econ,
                          WorldLegitimacy *wl, int conqueror, int region);

/* SACCAGE (§4) — une province PRISE est DÉPOUILLÉE une fois : l'or de ses coffres
 * et ~6 mois de production (entrepôt valorisé) sont fondus dans le trésor de
 * l'occupant (région `dst_region`, sa capitale) ; 1×/5 ans/province (plus rien à
 * prendre avant). Le sac convulse la province (cicatrice au plancher → gel du
 * développement). Renvoie la valeur pillée (or-équivalent) ; 0 si encore à vif.
 * Appelé automatiquement par diplo_conquer_region ; exposé pour le banc d'essai. */
float diplo_pillage_region(WorldEconomy *econ, int region, int dst_region);

void diplo_tick(DiploState *d, float dt);   /* usure de guerre (war_years++) + trêve/momentum */

/* ---- SCORE DE GUERRE (§2) — le bras-de-fer, à ticker chaque an ---------- *
 * Met à jour le battle_score (∝ avantage militaire, plafonné +50) et applique
 * l'ATTRITION (la guerre SAIGNE les armes des deux camps, le perdant plus →
 * mil_power baisse → la guerre s'épuise). L'occupation se lit à part (conquered). */
void  diplo_war_tick (DiploState *d, World *w, WorldEconomy *econ,
                      const WorldProsperity *wp, float dt);
/* Le score courant du point de vue de l'ATTAQUANT a contre b [-100..+100] :
 * batailles (≤+50) + occupation (+50→+100) − attrition (vers −100). */
float diplo_war_score(const DiploState *d, int a, int b);

/* ---- PAIX PROPORTIONNELLE (§5) — la victoire ACHÈTE des termes -------- *
 * REVENDICATION légitime de a contre b : combien de provinces la domination
 * MILITAIRE justifie d'annexer. Territorial → 1 + ∝ dominance ; les autres CB →
 * 1 prise (la source / l'humiliation) ; sans CB → 1 province tampon si l'on
 * domine, 0 sinon. PRENDRE AU-DELÀ est de la SUREXPANSION : diplo_conquer_region
 * la punit en fulgurance (→ coalition) — biaisé, jamais interdit. */
int   diplo_war_claim (const DiploState *d, const World *w,
                       const WorldEconomy *econ, int a, int b);
/* RÉPARATIONS : à la paix, le VAINCU (score adverse net) indemnise le vainqueur
 * ∝ |score de guerre| — ponction des trésors provinciaux du perdant → capitale du
 * vainqueur. Renvoie l'or transféré ; 0 si match nul (pas de vainqueur net). */
float diplo_reparations(DiploState *d, World *w, WorldEconomy *econ, int a, int b);

#endif /* SCPS_DIPLO_H */
