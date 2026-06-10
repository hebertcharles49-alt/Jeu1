#ifndef SCPS_DEMOGRAPHY_H
#define SCPS_DEMOGRAPHY_H
/*
 * scps_demography.h — LA CLÉ DE VOÛTE : une province contient des GROUPES
 *
 * Hier une province était une fiche HOMOGÈNE → D interne nul, H injouable,
 * assimilation orpheline. Ici une province contient des groupes
 * (race, culture, classe, effectif). Un seul changement rend réels d'un coup :
 *   - D interne PAR province (distance ENTRE groupes) ;
 *   - H jouable (on réprime UNE province et ses minorités restives) ;
 *   - l'assimilation incarnée (la culture d'une minorité DÉRIVE vers la
 *     dominante — la pile scps_modifier trouve sa pâture) ;
 *   - légitimité & fracture VÉCUES (le conquis a une L basse, les natifs loyaux).
 *
 * Discipline : H SUPPRIME (réversible) ; seuls P+K+L+temps MÉTABOLISENT (durable).
 * Rétro-compat : une province MONO-GROUPE reproduit les nombres d'aujourd'hui.
 * Le verdict reste au PAYS (scps_order inchangé) ; les métriques par province
 * (D, L, agitation) suffisent au local et remontent au pays.
 */
#include "scps_world.h"      /* World, WorldEconomy (l'intégration au moteur) */
#include "scps_econ.h"       /* PopGroup, ProvincePop, PopCulture, SocialClass */
#include "scps_species.h"    /* SpeciesArchetype, Sphere */
#include "scps_modifier.h"   /* la pile de dérive (assimilation/suppression) */
#include "scps_readout.h"    /* BandHumeur — la loyauté en MOT (membrane) */

/* PopGroup / ProvincePop sont définis dans scps_econ.h (bas niveau) : ainsi
 * RegionEconomy les porte et prosperity/legitimacy les LISENT sans cycle. */
#define DEMO_MAX_GROUPS SCPS_MAX_GROUPS

/* ---- Fiche EFFECTIVE = origine + pile (recalcul, pas mutation) -------- */
PopCulture group_culture_effective  (const PopGroup *g, const ModifierStack *drift);
float      group_agitation_effective(const PopGroup *g, const ModifierStack *drift);

/* ---- Lectures de province (§2) --------------------------------------- */
const PopGroup *province_dominant (const ProvincePop *pp);
long            province_total_pop(const ProvincePop *pp);
float province_Dbar     (const ProvincePop *pp, const ModifierStack *drift);  /* moyenne pondérée inter-groupes */
float province_Dinf     (const ProvincePop *pp, const ModifierStack *drift);  /* MAILLON FAIBLE : max */
float province_L        (const ProvincePop *pp);
float province_agitation(const ProvincePop *pp, const ModifierStack *drift);

/* ---- Légitimité PAR GROUPE (formule existante, clé sur culture vs couronne) */
float group_L_target(const PopGroup *g, const ModifierStack *drift, const PopCulture *crown,
                     float satisfaction, float integ, float country_H, float coercion, float build_H);
void  group_L_tick  (PopGroup *g, const ModifierStack *drift, const PopCulture *crown,
                     float satisfaction, float country_H, float coercion, float build_H);

/* ---- H jouable — SUPPRIME (réversible), n'assimile pas (§3) ----------- */
typedef struct { float agitation_drop, L_drop, fragility_rise; } CoercionEffect;
CoercionEffect province_apply_coercion(ProvincePop *pp, ModifierStack *drift, float H);
void           province_lift_coercion (ProvincePop *pp, ModifierStack *drift);  /* la botte se lève (Kuran) */

/* ---- Assimilation — DÉRIVE durable, timer ∝ D∞ (gouffre, §5) ---------- */
float assimilation_years(float Dinf, float P, float K);   /* Halfelin ~20 ans, Orque 80-150 */
/* Fait dériver chaque minorité vers le dominant d'un pas (years_per_tick). Fusion
 * quand la distance < EPS. Renvoie le nb de groupes fusionnés ce tick. */
int   assimilation_tick(ProvincePop *pp, ModifierStack *drift, float P, float K, float years_per_tick);

/* ---- Conversion religieuse (§2) — la FOI converge vers le TRÔNE ------- *
 * Distincte de l'assimilation (qui tire vers la dominante LOCALE) : l'axe
 * doctrinal de chaque groupe dérive vers la couronne, et la BRANCHE sacrée
 * bascule une fois la foi enracinée (`years_held`) et l'axe convergé — mais
 * seulement sous un trône PROSÉLYTE (credo ≠ pluraliste). Pluraliste : nulle
 * conversion, l'empire reste multi-confessionnel. Appelé par demography_tick. */
void  faith_convert_tick(ProvincePop *pp, const PopCulture *crown,
                         float years_held, float years_per_tick);

/* ---- Migration passive — emporte race + culture (§4) ----------------- */
/* Déplace `amount` du groupe `gi` de `from` vers `to` (adjacence/prospérité
 * jugées par l'appelant). Crée une minorité/diaspora à l'arrivée → du D interne.
 * `new_drift_id` : clé fraîche si une diaspora doit être créée. */
bool migration_move(ProvincePop *from, ProvincePop *to, int gi, long amount, int new_drift_id);

/* ---- Agrégation PAYS (§2, §6) — alimente scps_order (inchangé) -------- */
float country_Dbar(const ProvincePop *provs, int n, const ModifierStack *drift);
float country_Dinf(const ProvincePop *provs, int n, const ModifierStack *drift);  /* maillon faible pays */
float country_L   (const ProvincePop *provs, int n);

/* ---- Composition (§6) — la membrane : mots, jamais de SCPS brut ------- */
typedef struct {
    const char *race;      /* "Humain", "Orque"… (diégétique) */
    const char *culture;   /* nom de culture (diégétique) */
    const char *religion;  /* branche de foi (diégétique) — pour le camembert Religion */
    const char *klass;     /* "Noblesse" / "Artisans" / "Laboureurs" */
    int         percent;   /* part de la province */
    BandHumeur  loyaute;   /* L du groupe → MOT (membrane) */
    const char *etat;      /* "natif" / "en assimilation (N ans)" / "diaspora" */
} GroupReadout;
int province_composition(const ProvincePop *pp, const ModifierStack *drift,
                         const PopCulture *crown, float P, float K,
                         GroupReadout out[], int max);
const char *labor_class_word(SocialClass k);   /* Noblesse / Artisans / Laboureurs */

/* ===================================================================== */
/* INTÉGRATION AU MOTEUR VIVANT (§7) — la province RÉELLE porte des groupes */
/* ===================================================================== */
/* Attache à chaque région peuplée UN groupe substrat (sa culture, sa pop) →
 * rétro-compatible : mono-groupe = les nombres d'hier. À appeler après
 * gen_population/worldgen_seed_peoples. drift = la pile du monde (réinitialisée). */
void demography_attach(World *w, WorldEconomy *econ, ModifierStack *drift);

/* Un pas (un an) sur la démographie VIVANTE : rafraîchit la fiche effective de
 * chaque groupe (cache), fait la L par groupe, l'assimilation (dérive durable),
 * la migration (groupes vers la prospérité), puis SYNCHRONISE RegionEconomy.culture
 * (= groupe dominant). Le verdict reste au pays (scps_order inchangé). */
void demography_tick(World *w, WorldEconomy *econ, WorldLegitimacy *wl,
                     ModifierStack *drift, float P, float K, float dt);

/* Dépose `amount` du groupe dominant du pays `cid` (sa culture régnante) dans la
 * région conquise `region` → minorité de colons sous une couronne étrangère, OU
 * laisse les locaux conquis en minorité restive. Crée du D INTERNE vécu. */
void demography_on_conquest(World *w, WorldEconomy *econ, ModifierStack *drift, int region, int conqueror);

/* drift_id DYNAMIQUES (migration/conquête) : compteur unique et monotone — deux
 * groupes vivants ne partagent jamais une identité de dérive. rebase : à appeler
 * après un chargement (le compteur n'est pas sérialisé) — repart au-dessus du
 * plus grand drift_id vivant. */
int  demography_dyn_id_next(void);
void demography_dyn_id_rebase(const WorldEconomy *econ);

#endif /* SCPS_DEMOGRAPHY_H */
