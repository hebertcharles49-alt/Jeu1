#ifndef SCPS_CORE_H
#define SCPS_CORE_H
/*
 * scps_core.h — MOTEUR SCPS HEADLESS (§2 + annexe du document de conception)
 *
 * La colonne vertébrale VÉRIFIÉE du jeu : couche distance (§2.2), prospérité
 * externe (§2.3), ordre interne et mode d'effondrement (§2.4–2.5). Aucune
 * dépendance hors <math.h> : ce module se teste seul contre des cas de
 * référence (cf. core_demo.c), exactement comme le veut l'étape 0 de §14.
 *
 * Discipline §1 — on LIT des coordonnées, on n'ASSIGNE jamais de modificateur :
 *   - une fiche décrit une entité dans l'absolu (coordonnées) ;
 *   - les distances se LISENT entre deux fiches, jamais à la main ;
 *   - là où il faut un agrégat de friction, c'est le MAILLON FAIBLE (le max)
 *     qui sert, immunisé contre le double comptage d'axes corrélés.
 *
 * Toutes les variables sont sur l'échelle 0..10.
 */
#include <stdbool.h>

/* ===================================================================== */
/* §2.2 — COUCHE DISTANCE                                                 */
/* ===================================================================== */
/* Fiche = vecteur de coordonnées sur cinq axes, décrivant une sphère
 * culturelle dans l'absolu. Ordre du document : (langue, parenté, religion,
 * subsistance, valeurs).
 *
 * langue = HORLOGE : elle descend du parent et dérive avec la profondeur de
 *   branche → mesure le TEMPS depuis l'ancêtre commun. La friction l'IGNORE ;
 *   le cousinage ressenti la LIT (canal scps_clock).
 * Les 4 autres = CONTENU : la nature du système, ce que lit la friction. */
enum {
    SCPS_LANGUE = 0,   /* horloge phylogénétique */
    SCPS_PARENTE,
    SCPS_RELIGION,
    SCPS_SUBSISTANCE,
    SCPS_VALEURS,
    SCPS_NAXES
};
typedef struct { float axis[SCPS_NAXES]; } ScpsFiche;

float scps_sigmoid(float x);                                  /* σ(x)=1/(1+e^-x) */

/* D̄ = √((1/5)·Σ δ_k²) — Minkowski p=2 non pondéré sur les 5 axes (annexe).
 * Divergence d'ensemble : nourrit la cloche f(D̄). */
float scps_dist_bar(const ScpsFiche *a, const ScpsFiche *b);

/* D∞ = max δ_k sur les 4 axes de CONTENU (langue exclue). Le mur de friction,
 * la distance opérante. L'exclusion de la langue est imposée par §4.5 : deux
 * jumeaux transcontinentaux (langue ~8, contenu jumeau) ont un D∞ minuscule
 * et fusionnent — ce qui serait faux si la langue gonflait le max. */
float scps_dist_inf(const ScpsFiche *a, const ScpsFiche *b);

/* Horloge = |Δlangue| : cousinage ressenti (diplomatie, casus belli, §11). */
float scps_clock(const ScpsFiche *a, const ScpsFiche *b);

/* ===================================================================== */
/* §2.3 — PROSPÉRITÉ EXTERNE (contact entre deux entités)                */
/* ===================================================================== */
/* Cloche : pic = 1.0 à D̄ = 5. Nul à distance nulle (rien à échanger) comme
 * aux extrêmes (rien de métabolisable) → interdit le repli sur les siens. */
float scps_bell(float D_bar);                                   /* f(D̄)=D̄(10−D̄)/25 */
float scps_metabolisation(float P, float D_inf, float K);       /* σ(0.8(P−D∞)+0.35(K−5)) */
float scps_PE(float C, float P, float K, float D_bar, float D_inf);

/* ===================================================================== */
/* §2.4 — ORDRE INTERNE : état → diagnostic                              */
/* ===================================================================== */
/* D_bar ici = DIVERSITÉ INTERNE de l'État : moyenne des D̄ entre les fiches
 * des provinces qu'il contient (scps_diversity). C'est la pièce qui ne mord
 * que parce qu'on joue un État à plusieurs fiches (§9, §12). */
typedef struct {
    float D_bar;          /* diversité interne [0..10] */
    float P, C, K, F, I, H, L;
    float flux_faustien;  /* forge·a + magie·b (extension §8) */
} ScpsState;

typedef struct {
    float K_prime;        /* capacité renforcée par le fédéralisme */
    float coercition;     /* n'agit qu'à mesure que L manque */
    float R;              /* maintien (consentement OU force) */
    float I_prime;        /* pression après décompression */
    float amplificateur;  /* privation relative */
    float pression;
    float dereal;         /* flux que le filtre ne narre pas */
    float fracture;       /* sécession latente : diverse ET non consentie */
    float S;              /* charge totale */
    float SI;             /* stabilité de l'ordre [0..10] */
    float fragilite;      /* part de l'ordre tenue par la contrainte [0..10] */
} ScpsOrder;

/* §2.5 — lecture de la sortie. C'est `fragilite` qui distingue « stable parce
 * que consenti » (Suisse) de « stable parce que réprimé » (URSS tardive). */
typedef enum {
    SCPS_CONSENTI = 0,        /* SI≥5, fragilité basse : tient par adhésion, encaisse */
    SCPS_COERCITIF_FRAGILE,   /* SI≥5, fragilité≥5  : ne franchit pas le seuil, il CRAQUE */
    SCPS_SUBMERGE_REVOLUTION, /* SI<5, pression≥fracture : renversement du centre */
    SCPS_SUBMERGE_SECESSION   /* SI<5, fracture>pression : déchirure le long des coutures */
} ScpsMode;

ScpsOrder   scps_order(const ScpsState *s);   /* applique §2.4 / annexe mot pour mot */
ScpsMode    scps_mode (const ScpsOrder *o);   /* lecture §2.5 */
const char *scps_mode_name(ScpsMode m);

/* Diversité interne (§2.4) : moyenne des D̄ sur toutes les paires de fiches. */
float scps_diversity(const ScpsFiche *fiches, int n);

#endif /* SCPS_CORE_H */
