/*
 * scps_tech.h — ARBRE DE TECHNOLOGIES — concentrique & fractal
 *
 * L'UI lit un arbre CONCENTRIQUE. Centre = 0 (les 6 bâtiments de base). Il se
 * divise en 3 THÈMES (Savoir · Forge · Société) ; chaque thème rejoue 3 FONCTIONS
 * (Production · Armée · Renforcement) — auto-similaire → 9 QUARTIERS. Le RAYON est
 * la profondeur (tier) : plus loin = plus cher, plus puissant, plus risqué. Le
 * FAUSTIEN est au bord ; les techs ORPHELINES de race s'y greffent.
 *
 *      angle = quartier (3 thèmes × 3 fonctions)   rayon = profondeur/tier
 *
 * MAGIE ⊂ SAVOIR : l'arcane EST du savoir ; ses bouts faustiens (Invocation,
 * L'Éveil, Savoir interdit) vivent dans Savoir·Armée / Savoir·Renforcement.
 *
 * Une tech = UN déverrouillage (un bâtiment, une capacité). Au départ, SEULS les
 * 6 bâtiments de base existent ; tout le reste se déverrouille vers l'extérieur.
 *
 * Verrou SCPS (la Brèche) : tout flux qui afflue au-delà de ce que K peut narrer
 * déréalise — dereal = max(0, (P/10)·C + flux − K). Le faustien (de CHAQUE thème)
 * monte flux/charge/fracture ; seule la SOCIÉTÉ monte K pour métaboliser.
 *
 * COÛT : une tech coûte d'autant plus que l'empire est ÉTENDU (∝ population) →
 * frein au snowball, jeu « tall » viable. Cf. tech_cost().
 */
#ifndef SCPS_TECH_H
#define SCPS_TECH_H

#include <stdbool.h>
#include "scps_species.h"   /* SpeciesArchetype : la race native d'une tech signature */

/* ---- Thèmes (3) — la Magie est fondue dans le Savoir ------------------- */
typedef enum { THM_SAVOIR = 0, THM_FORGE, THM_SOCIETE, THM_COUNT } TechTheme;

/* ---- Fonctions (3) — chaque thème les rejoue (fractal) ----------------- */
typedef enum { FN_PRODUCTION = 0, FN_ARMEE, FN_RENFORCEMENT, FN_COUNT } TechFunction;

/* 9 quartiers = THM_COUNT × FN_COUNT (l'angle de l'arbre). */
#define TECH_QUARTERS (THM_COUNT*FN_COUNT)

/* ---- Identifiants de nœuds (chaque nœud = un déverrouillage) ----------- */
typedef enum {
    /* SAVOIR · Production (spine sûre — vitesse de recherche) */
    TECH_BIBLIOTHEQUE = 0,   /* ◎ base */
    TECH_SCRIPTORIUM, TECH_ACADEMIE, TECH_UNIVERSITE,
    /* SAVOIR · Armée (arcane offensif — faustien) */
    TECH_SAVOIR_GUERRE, TECH_MAGIE_BATAILLE, TECH_INVOCATION, TECH_EVEIL,
    /* SAVOIR · Renforcement (arcane durable — faustien) */
    TECH_WARDS, TECH_SCRYING, TECH_COMMUNION, TECH_SAVOIR_INTERDIT,
    /* FORGE · Production (sortie — le multiplicateur de rendement) */
    TECH_COLLECTE_BOIS,      /* ◎ base */
    TECH_COLLECTE_ARGILE,    /* ◎ base */
    TECH_FONDERIE, TECH_OUTILLAGE, TECH_MANUFACTURE, TECH_INDUSTRIE,
    TECH_FOREUSE,            /* §B2 FAUSTIEN : foreuse arcanique (essence → fer en masse) */
    /* FORGE · Armée (armes — faustien) */
    TECH_ARMURERIE, TECH_POUDRIERE, TECH_FORGE_RUNES, TECH_OEUVRE_NOIRE,
    /* FORGE · Renforcement (durabilité / fortification) */
    TECH_ATELIER,            /* ◎ base */
    TECH_QUALITE_MATERIAUX, TECH_FORTIFICATIONS, TECH_AUTOMATES,
    /* SOCIÉTÉ · Production (croissance / commerce / impôt) */
    TECH_COLLECTE_NOURRITURE,/* ◎ base */
    TECH_IRRIGATION, TECH_COMMERCE, TECH_CADASTRE, TECH_ABONDANCE,
    /* SOCIÉTÉ · Armée (levée — faustien : l'esclavage) */
    TECH_CASERNE,            /* ◎ base */
    TECH_CONSCRIPTION, TECH_ORGANISATION, TECH_ESCLAVAGE, TECH_CASTE_MARTIALE,
    /* SOCIÉTÉ · Renforcement (K / L / intégration — la spine métabolisante) */
    TECH_CHANCELLERIE, TECH_FOI, TECH_INTEGRATION, TECH_CULTE_IMPERIAL,
    TECH_COUNT
} TechId;

/* ---- Définition d'un nœud (table statique) ---------------------------- */
typedef struct {
    const char     *name;
    const char     *unlocks;     /* le bâtiment/capacité déverrouillé (mot de JEU) */
    TechTheme       theme;
    TechFunction    func;
    int             tier;        /* le RAYON : 0 = base (centre), 1.. = profondeur */
    TechId          prereq;      /* nœud précédent (TECH_COUNT = aucun) */
    bool            faustian;    /* ⚠ bout interdit (monte charge/flux → Brèche) */
    bool            needs_ruins; /* porte arcane : accès ruine/relique */
    SpeciesArchetype native;     /* race signature ; RACE_COUNT = universelle */

    /* Écriture SCPS (deltas appliqués au TechState). */
    float dK, dL, dF;            /* socle : capacité narrative, ordre, fédéralisme */
    float dEco, dMil;            /* puissance économique / militaire */
    float dH;                    /* coercition / dureté */
    float dFracture;             /* tension interne (peuples tenus de force) */
    float dPuissance;            /* puissance brute (surtout arcane) */
    float flux;                  /* FLUX PERMANENT que K doit narrer (≥0) */
    float charge;                /* contribution à la charge faustienne */
    bool  triggers_crisis;       /* tire soi-même la gâchette de la fin */
} TechNode;

/* ---- SYNCRÉTIQUE — profondeur de contact & nœuds de diffusion ---------- *
 * (briefs §4-8) Une tradition étrangère DIFFUSE par CONTACT, à une PROFONDEUR qui
 * dépend du canal : le comptoir transmet la SURFACE (la brasserie), la frontière/foi
 * le MÉTIER, seule la GOUVERNANCE (digérée) atteint le SECRET. Un nœud syncrétique
 * pend d'un nœud de base et se LOQUETTE (permanent) dès qu'on atteint l'archétype
 * requis à la profondeur requise — AUTOMATIQUE (diffusion, pas recherche). */
typedef enum { PROF_NONE=0, PROF_SURFACE, PROF_METIER, PROF_PROFOND, PROF_SECRET } Profondeur;

/* ARCHÉTYPES (briefs §7) : un PROFIL culturel, pas une race. Les indices 0..RACE_COUNT-1
 * sont les 6 signatures de race (centroïdes culturels — arcane=elfe, forge runique=nain,
 * artificier=gnome, assimilationniste=humain, pastoral=halfelin, martial-servile=orque,
 * MÊME ORDRE que SpeciesArchetype) ; au-delà, des profils d'ÉTHOS. depth[] est indexé
 * sur ARCH_COUNT. Un archétype d'éthos est « porté » par toute culture de cet éthos. */
#define ARCH_BUREAUCRATIQUE (RACE_COUNT)       /* éthos bureaucrate : scriptorium, cadastre */
#define ARCH_MERCANTILE     (RACE_COUNT+1)     /* éthos mercantile : comptoir, cothon */
#define ARCH_COUNT          (RACE_COUNT+2)

typedef struct {
    const char      *name;
    const char      *unlocks;          /* la capacité diffusée (mot de jeu) */
    int              arch;             /* archétype-source requis (indice 0..ARCH_COUNT-1) */
    Profondeur       prof_requise;     /* profondeur de contact minimale (surface…secret) */
    TechId           parent;           /* nœud de base dont le cercle s'ouvre (doit être acquis) */
    float dK, dL, dF, dEco, dMil;      /* écriture SCPS — diffusion BÉNÉFIQUE (jamais faustien) */
} SyncNode;

#define SYNC_COUNT 8

/* ---- État techno d'un empire (axes SCPS écrits par l'arbre) ----------- */
typedef struct {
    /* Socle résilient */
    float K;          /* capacité de métabolisation narrative */
    float L;          /* légitimité / ordre consenti */
    float F;          /* plafond de fédéralisme (diversité praticable) */
    /* Puissance */
    float eco, mil;
    float puissance;  /* puissance brute (P dans la formule de dereal) */
    /* Coûts */
    float H;          /* coercition */
    float fracture;   /* fractures internes cumulées */
    float charge;     /* C — charge faustienne accumulée (sens unique) */

    bool  unlocked[TECH_COUNT];
    int   n_unlocked;
    bool  sync_unlocked[SYNC_COUNT];   /* §syncrétique : nœuds de diffusion loqués (permanents) */
    int   n_sync;
    unsigned char arch_depth[ARCH_COUNT];  /* §13 cache : profondeur de contact ATTEINTE par archétype (lu par la membrane) */
    bool  has_ruins_access;   /* porte de l'arcane (Savoir faustien profond) */
    bool  crisis_triggered;   /* la crise de fin est-elle convoquée ? */
    float research_points;    /* points de recherche accumulés (économie de tech) */
} TechState;

/* ---- Catégories d'intrants pour la fusion ----------------------------- */
typedef enum {
    ING_COMBURANT = 0,   /* salpêtre */
    ING_COMBUSTIBLE,     /* soufre, charbon */
    ING_MINERAI,         /* fer, cuivre, métal-de-lune */
    ING_LIANT,           /* chaux, résine */
    ING_CATALYSEUR,      /* cristaux, reliques */
    ING_COUNT
} TechIngredient;

typedef struct {
    const char    *name;
    TechIngredient in1, in2;
    TechId         enabler;     /* tech requise pour réaliser la fusion */
    float dMil, dEco;
    float flux;
    float charge;
} FusionRecipe;

#define FUSION_COUNT 5

/* ---- API : état & recherche ------------------------------------------- */
void        tech_state_init(TechState *s, bool has_ruins_access);

/* Accesseurs de nœud (pour l'IA, l'UI, la membrane). */
const TechNode *tech_node(TechId id);
const char *tech_name(TechId id);
const char *tech_unlocks(TechId id);          /* le bâtiment/capacité déverrouillé */
const char *tech_theme_name(TechTheme t);     /* "Savoir"/"Forge"/"Société" */
const char *tech_function_name(TechFunction f);/* "Production"/"Armée"/"Renforcement" */
int         tech_quarter(TechTheme t, TechFunction f);  /* 0..8 — l'angle */
bool        tech_is_base(TechId id);          /* tier 0 = bâtiment de base (centre) */

/* Masque de RACES accessibles à un empire (sa propre race + races conquises/
 * migrées). Une tech native d'une race n'est recherchable qu'avec l'accès. */
unsigned    tech_race_bit(SpeciesArchetype r);

/* Prérequis remplis, pas déjà pris, porte arcane ok, ACCÈS de race ok ? */
bool  tech_can_research(const TechState *s, TechId id, unsigned race_access);
/* Applique les deltas SCPS, la charge et le flux ; marque comme acquis.
 * (Le PAIEMENT en points de recherche est géré par l'appelant via tech_cost.) */
bool  tech_research(TechState *s, TechId id, unsigned race_access);

/* §syncrétique — LATCH AUTOMATIQUE des nœuds de diffusion : pour chaque nœud dont le
 * PARENT est acquis et dont l'archétype-source est atteint à la PROFONDEUR requise
 * (depth[] indexé par race-signature : PROF_NONE..PROF_SECRET), loquette de façon
 * PERMANENTE et écrit ses deltas SCPS. Renvoie le nb de nœuds nouvellement loqués.
 * À appeler chaque pas — idempotent (un nœud loqué n'est jamais recalculé). */
int  tech_sync_tick(TechState *s, const unsigned char depth[ARCH_COUNT]);
const SyncNode *tech_sync_node(int i);   /* lecture (UI/membrane/télémétrie) ; NULL hors borne */

/* COÛT en points de recherche : BASE_COST[tier] × (1 + EXTENT_W·population/BASE).
 * Plus l'empire est ÉTENDU (∝ population), plus CHAQUE tech coûte → frein au
 * snowball, « tall » viable. Les bâtiments de base (tier 0) coûtent 0. */
float tech_cost(TechId id, float population);

/* Rendement de recherche : multiplicateur issu du SAVOIR·Production (Bibliothèque
 * → Scriptorium → Académie → Université). La POPULATION fournit l'assiette (côté
 * appelant : income = yield × f(pop)). La pop produit la recherche ET en renchérit
 * le coût → équilibre en un seul levier. */
float tech_research_yield(const TechState *s);

/* §B1 — bonus de PRODUCTION cumulés des nœuds déverrouillés (somme des prod_pct / eff_pct).
 * L'éco les lit pour abonder prod_mult : +production et +efficacité d'emploi, modestes,
 * dispatchés thématiquement (Forge/Société·Prod → prod ; Savoir·Prod → eff). */
float tech_prod_bonus(const TechState *s);   /* Σ prod_pct (fraction, ex. 0.30 = +30 %) */
float tech_eff_bonus(const TechState *s);    /* Σ eff_pct */
/* Le PENCHANT d'une race : le thème vers lequel sa signature la porte (biais IA,
 * jamais un « si race==X » : c'est une lecture de la table). */
TechTheme tech_race_affinity(SpeciesArchetype r);

/* ---- API : la Brèche (verrou SCPS, inchangé) -------------------------- */
float tech_dereal(const TechState *s);          /* max(0,(P/10)·C + flux − K) */
float tech_flux(const TechState *s);            /* flux faustien permanent */
float tech_crisis_proximity(const TechState *s);/* proximité de la crise [0..1] */
float tech_shock_amplitude(const TechState *s); /* ampleur du choc (croît avec magie) */
float tech_fragility(const TechState *s);       /* fracture / ordre net */

/* ---- API : fusion (intrants géologiques + enabler) -------------------- */
const FusionRecipe *tech_fusion_table(void);
bool  tech_fusion_available(const TechState *s, int recipe_idx,
                            const bool has_ingredient[ING_COUNT]);

#endif /* SCPS_TECH_H */
