#ifndef SCPS_ARMY_H
#define SCPS_ARMY_H
/*
 * scps_army.h — LES ARMÉES : recrutement, armes, pierre-feuille-ciseaux, combat au dé
 *
 * « Créer une armée n'est pas juste appuyer sur un bouton. » Une unité = 100 pop
 * d'une CLASSE (élite→cavalerie, commun→piétaille) + des ARMES FABRIQUÉES
 * (matériaux → armes, même logique que les bâtiments) + du temps. Pas d'armes en
 * stock → pas de levée. La pop enrôlée RESTE dans le pool (scps_labor) : un
 * enrôlement est une affectation, pas un retrait.
 *
 * Le combat est un PIERRE-FEUILLE-CISEAUX à grande échelle, en doublon (réseau de
 * contres redondant), résolu par des JETS DE DÉ pondérés par les contres ET les
 * stats. Le dé apporte l'incertitude ; le matchup et les stats penchent la balance.
 * Le contre PRIME sur la qualité brute — un mur de piquiers bon marché brise une
 * cavalerie d'élite.
 */
#include "scps_labor.h"   /* LaborEcon : pop par classe, matériaux, armes fabriquées */

#define POP_PER_UNIT     100   /* enrôlement par paquets de 100 */
#define ARMY_MAX_UNITS   32

/* ---- Types d'unité ---------------------------------------------------- */
typedef enum {
    U_PIQUIER=0, U_LANCIER, U_EPEISTE, U_ARCHER, U_ARBALETE,
    U_CAV_LEGERE, U_CAV_LOURDE, U_MAGE, U_COUNT
} UnitType;

/* ---- Armes fabriquées (matériaux → arme) ------------------------------ */
typedef enum {
    W_PIQUE=0, W_LANCE, W_EPEE, W_ARC, W_ARBALETE, W_MONTURE_L, W_MONTURE_H, W_BATON, W_COUNT
} ArmWeapon;

/* Recette d'arme : deux intrants matériaux + un temps (jours). */
typedef struct { LRes cost_a, cost_b; int days; } WeaponRecipe;

/* Définition statique d'un type d'unité. */
typedef struct {
    const char *name;
    LaborClass  from;      /* classe sociale prélevée (§2) */
    ArmWeapon   weapon;    /* arme requise (§1) */
    /* stats (§5) */
    float discipline;      /* +% dégâts infligés ET réduction des dégâts reçus */
    float moral;           /* capacité d'encaisse PAR paquet de 100 */
    float mouvement;       /* débordement (flanc) + vitesse de carte */
    float commandement;    /* abaisse le seuil du jet de dé */
} UnitDef;

/* ---- Une unité levée -------------------------------------------------- */
typedef struct {
    UnitType type;
    long     count;          /* en paquets de 100 */
    float    moral_courant;  /* la réserve de moral qui s'épuise au combat */
} Unit;

typedef struct {
    Unit units[ARMY_MAX_UNITS];
    int  n_units;
    long weapons[W_COUNT];               /* stock d'armes fabriquées */
    long pop_by_class_in_army[LAB_CLASS_COUNT];   /* affectées (toujours dans le pool labor) */
} ArmyState;

/* ---- Résultat de bataille --------------------------------------------- */
typedef struct {
    int winner;   /* -1 = A gagne, +1 = B gagne, 0 = nul */
    int routA, routB;
    int rounds;
} BattleResult;

/* ===================================================================== */
/* API                                                                   */
/* ===================================================================== */
const UnitDef     *unit_def(UnitType t);
const WeaponRecipe*weapon_recipe(ArmWeapon wp);
const char        *unit_name(UnitType t);
const char        *weapon_name(ArmWeapon wp);

void army_init(ArmyState *a);

/* Fabrique des armes : consomme les intrants matériaux dans l'économie (chaîne).
 * Pompe le marché des matériaux si le manque est en LR_MATERIALS. Renvoie le
 * nombre d'armes effectivement fabriquées. */
long army_fabricate_weapon(ArmyState *a, LaborEcon *e, ArmWeapon wp, long qty);

/* Peut-on lever `count` unités de ce type ? (pop libre de la bonne CLASSE +
 * armes en stock). Renvoie false sinon — ce n'est pas un bouton. */
bool army_can_recruit(const ArmyState *a, const LaborEcon *e, UnitType t, long count);
/* Lève l'unité : prélève la pop (affectée, PAS retirée du pool), consomme les
 * armes + un coût matériaux (pompe si manque). Renvoie le nb réellement levé. */
long army_recruit(ArmyState *a, LaborEcon *e, UnitType t, long count);

/* ---- Le pierre-feuille-ciseaux (§3) ----------------------------------- */
/* >1 si `a` contre `b`, <1 si `a` est contré, 1 neutre. */
float matchup(UnitType a, UnitType b);

/* ---- Le combat au dé (§4) --------------------------------------------- */
/* Le jet réussit-il ? (dé d20 + commandement ≥ seuil). */
bool  arm_hit(float commandement, int roll_d20);
/* Dégâts d'un contact réussi : contre × discipline_a × terrain, réduits par la
 * discipline de b. (Exposé pour vérifier la pondération.) */
float arm_damage(UnitType a, UnitType b, long count_a, float disc_b, float terrain);
/* Résout une bataille : appariement, débordement par le mouvement, contacts au
 * dé, le moral s'épuise, rupture quand il tombe ; l'armée la plus rompue recule.
 * terrainA module l'efficacité de A (forêt favorise l'embuscade, plaine la
 * cavalerie — lien couche d'action). rng = graine (xorshift) avancée en place. */
BattleResult resolve_battle(ArmyState *A, ArmyState *B, float terrainA, uint32_t *rng);

#endif /* SCPS_ARMY_H */
