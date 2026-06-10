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
#include <stdio.h>

typedef struct {
    float threat;       /* menace de b sur a */
    float complement;   /* complémentarité de ressources [0..1] */
    float kinship;      /* distance de sphère [0..7] (haut = étranger) */
    float schism;       /* ennemi naturel religieux [0..1] */
    float alliance;     /* score d'alliance (haut = allié naturel) */
    float shared_rel;   /* §D1 : menace COMMUNE relative au monde — la RAISON de l'alliance.
                         * La dissolution la relit pour LÂCHER quand la menace fond (sans
                         * quoi complément + parenté maintiennent le lien à jamais). */
} Relation;

typedef enum { DIPLO_NEUTRAL = 0, DIPLO_ALLIED, DIPLO_WAR } DiploStatus;

/* CASUS BELLI — la guerre a une RAISON (lue de la relation) ; son type fixe le
 * BUT de guerre et ce qui est exigible à la paix (un frein de plus). */
typedef enum {
    CB_NONE = 0,
    CB_TERRITORIAL,   /* adjacence / revendication / province perdue → prend des provinces */
    CB_RELIGIOUS,     /* schisme + prosélytisme → humiliation (peu/pas de terre) */
    CB_ECONOMIC,      /* un bien aigu MONOPOLISÉ par la cible → la province-source */
    CB_SUBJUGATION,   /* menace + projection → vassalité (pas d'annexion massive) */
    CB_ANTIPIRATERIE  /* la course subie trop longtemps (coques §5) → but : DÉSARMER le commanditaire */
} CasusBelli;

typedef struct DiploState {
    DiploStatus status[SCPS_MAX_COUNTRY][SCPS_MAX_COUNTRY];
    float       war_years[SCPS_MAX_COUNTRY][SCPS_MAX_COUNTRY];
    float       truce[SCPS_MAX_COUNTRY][SCPS_MAX_COUNTRY];  /* jours d'interdiction de guerre (fond) */
    float       momentum[SCPS_MAX_COUNTRY];                 /* conquêtes RÉCENTES (décroît) → fulgurance perçue */
    float       ambient_threat;                             /* §D2 : menace MOYENNE du monde (réf. relative des alliances) */
    int8_t      cb[SCPS_MAX_COUNTRY][SCPS_MAX_COUNTRY];     /* casus belli ACTIF de a contre b (but de guerre) */
    /* SCORE DE GUERRE — le bras-de-fer (a = ATTAQUANT, celui qui a le CB) :
     * batailles (∝ avantage militaire, PLAFONNÉ +50) + occupation (provinces prises,
     * l'autre +50→+100) ; le défenseur pousse vers −100 par l'attrition. */
    float       battle_score[SCPS_MAX_COUNTRY][SCPS_MAX_COUNTRY];  /* [-100 .. +50] */
    int16_t     conquered  [SCPS_MAX_COUNTRY][SCPS_MAX_COUNTRY];   /* régions prises ce conflit (occupation) */
    float       conq_value [SCPS_MAX_COUNTRY][SCPS_MAX_COUNTRY];   /* §5 combat : PRIX cumulé des provinces prises (budget de score dépensé) */
    /* RANCUNE NATIONALE (§6) — rancor[a][b] = grief de a contre b qui lui a PRIS des
     * terres. ASYMÉTRIQUE, SURVIT à la paix (le grief reste), décroît sur une
     * génération. Donne à a un casus belli territorial (irrédentisme, sans adjacence)
     * et galvanise sa guerre de reconquête (ralliement). */
    float       rancor     [SCPS_MAX_COUNTRY][SCPS_MAX_COUNTRY];
    /* LA COURSE (coques §5) : rancune de la VICTIME envers le COMMANDITAIRE
     * identifié — même patron d'accumulation/déclin que la fronde. pirate_disarm :
     * verdict d'une guerre anti-piraterie perdue — la flotte pirate se désarme
     * (lu/exécuté par scps_navy, qui efface le drapeau). */
    float       pirate_rancor[SCPS_MAX_COUNTRY][SCPS_MAX_COUNTRY];
    int8_t      pirate_disarm[SCPS_MAX_COUNTRY];
    int         n_war_antipirate;     /* télémétrie : guerres anti-piraterie déclarées */
    /* SOUILLURE FAUSTIENNE — faustian[c] = à quel point c développe l'interdit
     * (synchronisé sur sa charge de tech). Une foi ORTHODOXE a une CHANCE de
     * croiser contre un empire qui développe le faustien (Gardiens vs Transgresseurs). */
    float       faustian   [SCPS_MAX_COUNTRY];
    /* ── SUZERAINETÉ (brief leviers §3) — un statut ASYMÉTRIQUE, distinct d'ALLIED :
     * le vassal garde son trône, sa culture, son IA (qui complote sa défection). Le
     * lien vit par la FORCE ou l'INTÉRÊT — le ratio s'effondre, le vassal dénonce. */
    int16_t     suzerain   [SCPS_MAX_COUNTRY];   /* -1 = libre */
    int8_t      contrat    [SCPS_MAX_COUNTRY];   /* SuzContrat du lien (porté par le vassal) */
    int n_servage, n_protectorat, n_concordat, n_cite, n_defections;   /* chronique (cumul sim) */
    /* ── LA FRONDE VASSALE (brief fronde) — grief (combustible) × ratio (oxygène) ── */
    float    v_grief [SCPS_MAX_COUNTRY];  /* grief du vassal envers son maître [0..1] */
    float    v_loyal [SCPS_MAX_COUNTRY];  /* jours de LOYAUTÉ ACHETÉE (bloque l'entrée en ligue) */
    uint8_t  v_ligue [SCPS_MAX_COUNTRY];  /* membre de la ligue contre son suzerain ? */
    uint8_t  v_dons  [SCPS_MAX_COUNTRY];  /* usure du don (chaque don pèse moins) */
    int16_t  fronde_suz, fronde_lead;     /* fronde ACTIVE contre ce suzerain (-1 = aucune) */
    float    fronde_score;                /* bras-de-fer de la fronde, capturé AVANT la paix */
    uint32_t fronde_rng;                  /* probabilité par tick (jamais un couperet) */
    /* chronique fronde */
    int n_ligues, n_frondes, n_indep, n_renvers, n_ecrase, n_defect_paix, n_defect_guerre;
    int n_lev_don, n_lev_allege, n_lev_divise, n_lev_intim;
} DiploState;

/* Les QUATRE contrats de suzeraineté — quatre logiques (cf. brief §3). */
typedef enum { CONTRAT_NONE=0, CONTRAT_SERVAGE, CONTRAT_PROTECTORAT,
               CONTRAT_CONCORDAT, CONTRAT_CITE } SuzContrat;
/* Pose un lien suzerain→vassal (met fin à leur guerre, ouvre une trêve) ; remplace l'ancien. */
void        diplo_set_vassal  (DiploState *d, int suzerain, int vassal, SuzContrat c);
/* Le vassal dénonce : libre — le SERF part en guerre d'indépendance (to_war). */
void        diplo_break_vassal(DiploState *d, int vassal, bool to_war);
int         diplo_suzerain    (const DiploState *d, int cid);   /* -1 = libre */
SuzContrat  diplo_contrat     (const DiploState *d, int cid);
int         diplo_vassal_count(const DiploState *d, int cid);
const char *diplo_contrat_name(SuzContrat c);
/* Route GARANTIE (cité-état commerciale) : ni guerre ni embargo ne coupent ce lien. */
bool        diplo_trade_pact  (const DiploState *d, int a, int b);
/* Tick ANNUEL : TRIBUTS (servage lourd 8 %/an + coercition chez le serf ; protectorat
 * léger 2 %), APPEL du protecteur (les guerres du protégé l'appellent), DÉFECTION
 * (ratio de force < ~1.15 → dénonciation ; le serf part en guerre), ACCEPTATION par
 * la MENACE (un petit SANS allié sous un voisin écrasant ≥ 1.8 accepte le protectorat
 * — la voie « menace » ; la voie « mission » D3 viendra par statecraft). */
void diplo_suzerainty_tick(DiploState *d, const World *w, WorldEconomy *econ,
                           const WorldProsperity *wp);
/* Fidélité d'un vassal — pour la membrane (le ratio 1,2 ne s'affiche JAMAIS ;
 * la fronde se PRESSENT en bandes et en signes, elle ne se calcule pas à l'écran). */
float diplo_vassal_grief(const DiploState *d, int vassal);   /* [0..1] nu — à BANDER côté readout */

void diplo_init(DiploState *d);
/* graine de fronde ∝ monde (à appeler à la CRÉATION d'une partie, après init —
 * la sauvegarde préserve fronde_rng, on ne re-sème pas au chargement). */
void diplo_seed_rng(DiploState *d, uint32_t seed);
/* sauvegarde : statiques du module (cooldown d'intimidation de la fronde). */
void diplo_save_statics(FILE *f);
bool diplo_load_statics(FILE *f);

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
/* §D-sat : plafond d'alliances par polité (l'alliance est une ressource RARE) +
 * compteur — partagés par l'IA (alliances naturelles) ET le statecraft (missions),
 * pour que « 2 slots max » soit un invariant GLOBAL, sans fuite par une autre voie. */
#define     DIPLO_ALLY_SLOTS  2
int         diplo_ally_count  (const DiploState *d, int a);
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
                          WorldLegitimacy *wl, int conqueror, int region, bool conqueror_enslaves);

/* SACCAGE (§4) — une province PRISE est DÉPOUILLÉE une fois : l'or de ses coffres
 * et ~6 mois de production (entrepôt valorisé) sont fondus dans le trésor de
 * l'occupant (région `dst_region`, sa capitale) ; 1×/5 ans/province (plus rien à
 * prendre avant). Le sac convulse la province (cicatrice au plancher → gel du
 * développement). Renvoie la valeur pillée (or-équivalent) ; 0 si encore à vif.
 * Appelé automatiquement par diplo_conquer_region ; exposé pour le banc d'essai. */
float diplo_pillage_region(WorldEconomy *econ, int region, int dst_region);

/* ESCLAVAGE (§4c) — une société ASSERVISSANTE déporte une part de la population
 * prise vers son CŒUR (capitale) : un groupe DIASPORA non-intégré (restif) de
 * culture étrangère → le D̄ du maître monte, la fracture s'installe au centre.
 * Renvoie le nombre de captifs ; 0 si `enslaves` est faux. GATE = la TECH
 * d'asservissement (TECH_ESCLAVAGE, signature Orque) ; l'appelant passe le booléen.
 * Appelé par diplo_conquer_region ; exposé pour le banc d'essai. */
long diplo_enslave_capture(World *w, WorldEconomy *econ, int conqueror, int region, bool enslaves);

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

/* §5 COMBAT — LE PRIX D'UNE PROVINCE en score de guerre, ∝ sa VALEUR DÉVELOPPÉE
 * (bâti + prospérité + population). Le score de guerre [0..100] est un BUDGET dépensé
 * à la paix : on ne prend que les provinces dont le prix cumulé ≤ score. Un cœur
 * développé coûte cher (victoire DÉCISIVE requise) ; un arrière-pays est bon marché ;
 * une province SACCAGÉE (valeur effondrée) devient moins chère à annexer. */
float diplo_province_price(const WorldEconomy *econ, int region);
float diplo_war_budget(const DiploState *d, const World *w, const WorldEconomy *econ,
                       int attacker, int defender);  /* domination militaire + prime de score */
float diplo_country_value(const WorldEconomy *econ, int cid);             /* Σ prix des provinces */
/* §5 BUTIN : le budget de score NON dépensé en terres VIDE les coffres du vaincu. */
float diplo_loot(World *w, WorldEconomy *econ, int attacker, int defender, float leftover_value);
/* RÉPARATIONS : à la paix, le VAINCU (score adverse net) indemnise le vainqueur
 * ∝ |score de guerre| — ponction des trésors provinciaux du perdant → capitale du
 * vainqueur. Renvoie l'or transféré ; 0 si match nul (pas de vainqueur net). */
float diplo_reparations(DiploState *d, World *w, WorldEconomy *econ, int a, int b);

/* RANCUNE (§6) — le grief de a contre b (terres perdues). Lecture pour l'IA (biais
 * de reconquête) et l'UI. Posée par diplo_conquer_region sur le DÉPOSSÉDÉ, plus
 * profonde si la prise fut ILLÉGITIME ; survit à la paix, décroît dans diplo_tick. */
float diplo_rancor(const DiploState *d, int a, int b);
/* COURSE : grief de piraterie (victime → commanditaire IDENTIFIÉ) ; clampé [0..10]. */
void  diplo_pirate_grief (DiploState *d, int victim, int sponsor, float amount);
float diplo_pirate_rancor(const DiploState *d, int victim, int sponsor);

/* SOUILLURE FAUSTIENNE — synchronisée chaque an depuis la charge de tech d'un pays.
 * diplo_faustian_cb : un attaquant ORTHODOXE (foi régnante austère) contre un
 * empire qui développe nettement le faustien a une RAISON religieuse (croisade). */
void  diplo_set_faustian(DiploState *d, int cid, float level);
float diplo_faustian    (const DiploState *d, int cid);
bool  diplo_faustian_cb (const World *w, const WorldEconomy *econ, const DiploState *d,
                         int attacker, int target);

#endif /* SCPS_DIPLO_H */
