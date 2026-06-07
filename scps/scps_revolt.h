#ifndef SCPS_REVOLT_H
#define SCPS_REVOLT_H
/*
 * scps_revolt.h — LA RÉVOLTE INCARNÉE : un soulèvement est un ACTEUR, pas un drapeau
 *
 * Hier une révolte n'était qu'un fanion de région (statecraft : agitation
 * soutenue → L↓, loi martiale). Personne ne se soulevait : le grief n'était
 * ancré nulle part. Ici la révolte naît d'un GROUPE démographique concret :
 *
 *   QUI se soulève   — le groupe au plus fort DÉFICIT (panier + sur-taxe +
 *                      aliénation à la couronne + répression + non-intégration) ;
 *   COMBIEN          — une fraction MOBILISÉE ∝ déficit qui QUITTE la main-
 *                      d'œuvre (choc économique réel : l'atelier perd ses bras) ;
 *   CE QU'ILS VEULENT— selon qui se lève : une CLASSE réclame (jacquerie), une
 *                      nation conquise fait SÉCESSION (nouveau pays), l'ÉLITE
 *                      renverse le trône (coup d'État) ;
 *   CE QU'IL ADVIENT — les rebelles (force réelle) affrontent la garnison :
 *                      écrasés → perte de pop + fragilité ; victorieux →
 *                      sécession (un pays NAÎT) / concession / coup.
 *
 * Discipline membrane : ce module est SIM (il lit des flottants SCPS comme
 * diplo/legitimacy). Son API de lecture ne renvoie que des nombres/mots de JEU.
 */
#include "scps_world.h"
#include "scps_econ.h"
#include "scps_demography.h"
#include "scps_legitimacy.h"
#include "scps_prosperity.h"
#include "scps_diplo.h"
#include "scps_readout.h"

/* ---- Nature du soulèvement (qui se lève → ce qu'il veut) --------------- */
typedef enum {
    REBEL_NONE = 0,
    REBEL_CLASS,        /* une classe réclame des concessions (jacquerie)      */
    REBEL_SECESSION,    /* une nation conquise/étrangère se détache → nouveau pays */
    REBEL_COUP          /* l'élite renverse la couronne (affaire de palais)    */
} RebelKind;

/* ---- Verdict d'un soulèvement (rempli à la résolution) ----------------- */
typedef enum {
    OUT_ONGOING = 0,
    OUT_CRUSHED,        /* écrasés : morts + le pays se fragilise (coercition)  */
    OUT_SECEDED,        /* sécession réussie : un pays est né                   */
    OUT_CONCESSION,     /* la couronne cède : satisfaction ↑, agitation ↓       */
    OUT_COUP            /* l'élite prend le trône : la couronne change de mains */
} RevoltOutcome;

#define REVOLT_MAX 64

/* ---- Un soulèvement incarné ------------------------------------------- */
typedef struct {
    bool             active;
    int              region;     /* foyer du soulèvement */
    int              owner;      /* la couronne visée */
    RebelKind        kind;
    /* identité du groupe soulevé — pour la résolution ET le chroniqueur */
    SpeciesArchetype race;
    SocialClass      klass;
    PopCulture       culture;    /* fiche effective au moment du soulèvement */
    int              drift_id;   /* clé du groupe (le retrouver dans la province) */
    long             mobilized;  /* combattants partis du travail = force rebelle */
    float            deficit;    /* ce qui les a poussés [0..1] (mémoire) */
    int              days;       /* durée du soulèvement */
    int              outcome;    /* RevoltOutcome (0 = en cours) */
    int              spawned;    /* pays né de la sécession (-1 sinon) */
} Rebellion;

typedef struct {
    Rebellion list[REVOLT_MAX];
    int       count;             /* slots actifs (liste compactée) */
    float     desperation_days[SCPS_MAX_REG];  /* misère SOUTENUE par région → allumage */
    float     revanchism_days[SCPS_MAX_REG];   /* SÉPARATISME post-conquête (≈10 ans) :
                                                * une province fraîchement soumise peut se
                                                * soulever pour l'indépendance « quoi qu'il arrive » */
    /* compteurs de chronique (cumul sur la partie) */
    int       n_ignited, n_crushed, n_seceded, n_concession, n_coup;
    long      pop_lost;          /* morts au total */
    int       last_spawned;      /* dernier pays né (-1) — pour brancher l'IA */
} RevoltState;

void revolt_init(RevoltState *rs);

/* ---- Le DÉFICIT d'un groupe [0..1] : la pression qui pousse au soulèvement
 * panier vivrier+social (1-satisfaction) + sur-taxe + aliénation à la couronne
 * (distance culturelle) + répression (coercition/H) + (1-intégration). */
float revolt_group_deficit(const PopGroup *g, const ModifierStack *drift,
                           const PopCulture *crown, float food_sat, float society_sat,
                           float tax_pressure, float coercion);

/* ---- La fraction MOBILISÉE : combien quittent le travail pour se battre - */
long  revolt_mobilized(const PopGroup *g, float deficit);

/* ---- La nature : qui se lève détermine ce qu'il veut ------------------- */
RebelKind revolt_classify(const PopGroup *g, const ModifierStack *drift, const PopCulture *crown);

/* ---- SCAN : la misère SOUTENUE d'une région allume un soulèvement ------
 * Chaque région possédée : on lit le pire déficit de groupe ; au-delà du seuil,
 * la désespérance s'accumule ; soutenue assez longtemps → revolt_ignite. C'est
 * le déclencheur ANCRÉ sur les groupes (un conquis non-intégré finit par se lever),
 * complémentaire de l'agitation abstraite du statecraft. `days` = pas écoulé. */
void  revolt_scan(RevoltState *rs, World *w, WorldEconomy *econ,
                  const ModifierStack *drift, int days);

/* ---- REVANCHISME : subir la conquête arme le séparatisme (≈10 ans) ------
 * À appeler quand une province passe sous une couronne ÉTRANGÈRE. Pendant la
 * fenêtre : le soulèvement s'allume « quoi qu'il arrive », les rangs rebelles
 * grossissent et la garnison d'occupation tient moins bien — une vraie chance
 * d'indépendance, qui s'éteint à mesure que la blessure se referme. */
void  revolt_on_conquest(RevoltState *rs, int region);

/* ---- ALLUMAGE : une région a basculé → on incarne le soulèvement -------
 * Choisit le groupe au plus fort déficit ; s'il dépasse le seuil, MOBILISE
 * (les combattants QUITTENT la main-d'œuvre → choc économique) et enregistre un
 * Rebellion. Renvoie l'index du soulèvement, ou -1 si aucun groupe ne se lève. */
int   revolt_ignite(RevoltState *rs, World *w, WorldEconomy *econ,
                    const ModifierStack *drift, int region, float tax_pressure);

/* ---- RÉSOLUTION : un pas (jours). Les rebelles affrontent la garnison ---
 * garnison = pop loyale levée + H local + renforts de la couronne (mil_power) ;
 * force rebelle = mobilisés × zèle (le coup frappe fort, peu nombreux). Verdict :
 *   écrasés  → perte de pop + coercition (le pays se raidit) ;
 *   victoire → sécession (un PAYS NAÎT) / concession / coup (couronne changée). */
void  revolt_tick(RevoltState *rs, World *w, WorldEconomy *econ, ModifierStack *drift,
                  WorldLegitimacy *wl, const WorldProsperity *wp, int days);

/* ---- Membrane : des mots/nombres de JEU, jamais un flottant SCPS ------- */
int          revolt_active_count(const RevoltState *rs);
const char  *revolt_kind_word   (RebelKind k);   /* "jacquerie"/"sécession"/"coup d'État" */
const char  *revolt_outcome_word(int outcome);   /* "écrasée"/"indépendance"/… */
const char  *revolt_class_word  (SocialClass k); /* "Laboureurs"/"Artisans"/"Noblesse" */

#endif /* SCPS_REVOLT_H */
