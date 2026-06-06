/*
 * scps_tech.h — ARBRE DE TECHNOLOGIES (prototype)
 *
 * Voir « Arbre de technologies — Document de conception ». L'arbre n'est PAS
 * une échelle de progrès : c'est l'axe de défaite. Un unique curseur
 *
 *      RÉSILIENCE ◄──────────────────────────────► FAUSTIEN
 *        SOCIÉTÉ            FORGE                     MAGIE
 *        bâtit K            puissance, bascule        puissance brute,
 *        charge ≈ 0         faustienne                appelle la catastrophe
 *
 * Verrou SCPS (partout) : tout flux qui afflue au-delà de ce que K peut narrer
 * génère de la DÉRÉALISATION :
 *
 *      dereal = max(0, (P/10)·C + flux_faustien − K)
 *
 * avec C = charge faustienne accumulée, P = puissance, flux_faustien = somme
 * des flux permanents des nœuds Forge/Magie pris. Seule la Société (qui monte
 * K) permet de métaboliser Forge/Magie sans se fissurer.
 *
 * La charge pilote (1) la proximité de la crise de fin et (2) l'ampleur du choc
 * quand elle tombe (plus de magie = plus gros dragon).
 *
 * NOTE — valeurs chiffrées INDICATIVES (à calibrer contre le moteur headless).
 * La structure (branches, prérequis, directions d'écriture SCPS) est, elle,
 * fixe. Choix de design retenus ici :
 *   - Recherche : la charge ne se rembourse PAS (sens unique). Abjurer la magie
 *     stoppe le flux nouveau mais la charge déjà appelée reste (le pacte est
 *     scellé). Cf. §8 « Réversibilité ».
 */
#ifndef SCPS_TECH_H
#define SCPS_TECH_H

#include <stdbool.h>

/* ---- Branches --------------------------------------------------------- */
typedef enum {
    TBR_SOCIETY = 0,   /* Le Socle — résilience */
    TBR_FORGE,         /* L'Atelier — milieu vénéneux */
    TBR_MAGIC,         /* L'Arcane — faustien */
    TBR_COUNT
} TechBranch;

/* ---- Identifiants de nœuds (I.x / II.x / III.x du document) ----------- */
typedef enum {
    /* Société */
    TECH_I1_COUTUME = 0,   /* Coutume codifiée */
    TECH_I2_CHARTE,        /* Charte des terres */
    TECH_I3_GRENIERS,      /* Greniers communs */
    TECH_I4_CONSEIL,       /* Conseil des sphères */
    TECH_I5_CHANCELLERIE,  /* Chancellerie (archive vivante) */
    TECH_I6_INTEGRATION,   /* Droit d'intégration */
    TECH_I7_PACTE,         /* ★ Pacte des peuples (capstone résilience) */
    /* Forge */
    TECH_II1_METALLURGIE,
    TECH_II2_HYDRAULIQUE,
    TECH_II3_FONDERIE,
    TECH_II4_MANUFACTURE,
    TECH_II5_GUERRE,       /* Ingénierie de guerre */
    TECH_II6_FORGE_PROF,   /* Forge profonde */
    TECH_II7_INDUSTRIE,    /* Industrie de masse */
    TECH_II8_OEUVRE,       /* ★ L'Œuvre noire (capstone forge) */
    /* Magie */
    TECH_III1_SAVOIR,      /* Savoir ancien */
    TECH_III2_RUNES,       /* Runes liantes */
    TECH_III3_MIROIRS,     /* Miroirs lointains */
    TECH_III4_ELEMENTS,    /* Maîtrise des éléments */
    TECH_III5_PACTES,      /* Pactes anciens */
    TECH_III6_EVEIL,       /* L'Éveil (déclenche la crise) */
    TECH_III7_COURONNE,    /* ★ La Couronne ardente (capstone magie) */
    TECH_COUNT
} TechId;

/* ---- Définition d'un nœud (table statique) ---------------------------- */
typedef struct {
    const char *name;
    TechBranch  branch;
    int         tier;          /* 1..5 */
    TechId      prereq[2];     /* -1 = aucun (codé TECH_COUNT) */
    bool        needs_ruins;   /* porte d'entrée Magie : accès ruine/relique */
    bool        capstone;      /* définit le pôle de la run */

    /* Écriture SCPS (deltas appliqués à la recherche). */
    float dK, dL, dF;          /* socle : capacité narrative, ordre, fédéralisme */
    float dEco, dMil;          /* puissance économique / militaire */
    float dH;                  /* coercition / dureté */
    float dFracture;           /* tension interne (peuples tenus de force) */
    float dPuissance;          /* puissance brute (surtout magie) */
    float flux;                /* FLUX PERMANENT que K doit narrer (≥0) */
    float charge;              /* contribution à la charge faustienne */
    bool  triggers_crisis;     /* tire soi-même la gâchette de la fin */
} TechNode;

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
    bool  has_ruins_access;   /* porte de la branche Magie */
    bool  crisis_triggered;   /* la crise de fin est-elle convoquée ? */
} TechState;

/* ---- Catégories d'intrants pour la fusion (§7) ------------------------ */
typedef enum {
    ING_COMBURANT = 0,   /* salpêtre */
    ING_COMBUSTIBLE,     /* soufre, charbon */
    ING_MINERAI,         /* fer, cuivre, métal-de-lune */
    ING_LIANT,           /* chaux, résine */
    ING_CATALYSEUR,      /* cristaux, reliques */
    ING_COUNT
} TechIngredient;

/* Recette de fusion : intrants + tech habilitante → tech-produit nommée. */
typedef struct {
    const char    *name;
    TechIngredient in1, in2;
    TechId         enabler;     /* tech requise pour réaliser la fusion */
    /* effet du produit */
    float dMil, dEco;
    float flux;                 /* >0 si le produit appelle de la dereal */
    float charge;               /* contribution faustienne du produit */
} FusionRecipe;

#define FUSION_COUNT 5

/* ---- API -------------------------------------------------------------- */
void        tech_state_init(TechState *s, bool has_ruins_access);
const char *tech_name(TechId id);
const char *tech_branch_name(TechBranch b);
const TechNode *tech_node(TechId id);

/* Prérequis remplis, pas déjà pris, porte d'accès ok ? */
bool  tech_can_research(const TechState *s, TechId id);
/* Applique les deltas SCPS, la charge et le flux ; marque comme acquis. */
bool  tech_research(TechState *s, TechId id);

/* dereal = max(0, (P/10)·C + flux_faustien − K). */
float tech_dereal(const TechState *s);
/* flux faustien permanent (somme des flux des nœuds pris). */
float tech_flux(const TechState *s);
/* Proximité de la crise de fin [0..1], saturante en charge. */
float tech_crisis_proximity(const TechState *s);
/* Ampleur du choc quand la crise tombe (croît avec charge ET magie). */
float tech_shock_amplitude(const TechState *s);
/* Fragilité structurelle : fracture rapportée à l'ordre L (≥5 = on craque). */
float tech_fragility(const TechState *s);

/* Fusion : liste les recettes réalisables (intrants dispo + enabler pris). */
const FusionRecipe *tech_fusion_table(void);
bool  tech_fusion_available(const TechState *s, int recipe_idx,
                            const bool has_ingredient[ING_COUNT]);

#endif /* SCPS_TECH_H */
