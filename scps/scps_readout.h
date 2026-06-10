#ifndef SCPS_READOUT_H
#define SCPS_READOUT_H
/*
 * scps_readout.h — LA MEMBRANE : du flottant SCPS au MOT diégétique.
 *
 * Garantie structurelle (Partie 0 du cahier UI) : ce fichier — et lui seul —
 * traduit les flottants du moteur en bandes qualitatives + chaînes. Le
 * renderer (viewer.c, scps_render.c) n'inclut QUE cet en-tête : il ne voit
 * jamais scps_core.h ni un flottant SCPS. Il reçoit un Readout (enums +
 * chaînes) et appelle label_X / hover_X. Franchir la cloison est IMPOSSIBLE,
 * pas seulement déconseillé.
 *
 * → scps_core.h est inclus dans scps_readout.c SEULEMENT, jamais ici.
 * → Les seuils opèrent sur des flottants NUS (pas de type scps_core exposé).
 *
 * Distinction tenue : les quantités tangibles (population, année, or) peuvent
 * s'afficher en chiffres — ce ne sont pas du SCPS. Les abstractions SCPS
 * (stabilité, légitimité, fracture, fragilité, prospérité) : JAMAIS un chiffre.
 */
#include <stdbool.h>
/* Types de la SIM (pas scps_core) : le renderer peut tenir les pointeurs, mais
 * la règle « ne lis jamais un flottant SCPS » reste vérifiée par grep (#1).
 * scps_prosperity.h n'inclut PAS scps_core.h → la cloison tient. */
#include "scps_world.h"
#include "scps_prosperity.h"   /* WorldProsperity, WorldLegitimacy, WorldEconomy */

/* ===================================================================== */
/* BANDES QUALITATIVES — jamais un nombre                                 */
/* ===================================================================== */
/* Bandeau du royaume */
typedef enum { ST_SUBMERGE, ST_VACILLANT, ST_TENU, ST_ASSURE, ST_INEBRANLABLE } BandStab;
typedef enum { AS_CONSENTIE, AS_PARTAGEE, AS_CONTRAINTE, AS_TYRANNIQUE }          BandAssise;
typedef enum { LG_USURPEE, LG_CONTESTEE, LG_TOLEREE, LG_RECONNUE, LG_SACREE }     BandLegit;
typedef enum { CO_UNIE, CO_MURMURANTE, CO_FRACTUREE, CO_SECESSION }               BandConcorde;
typedef enum { PR_MISERE, PR_DISETTE, PR_SUFFISANCE, PR_AISANCE, PR_OPULENCE }    BandProsp;
typedef enum { SA_OBSCURITE, SA_LUEUR, SA_FOYER, SA_PHARE }                       BandSavoir;
typedef enum { FORGE_RUDIMENTAIRE, FORGE_ARTISANALE, FORGE_MANUFACTURIERE, FORGE_INDUSTRIELLE } BandForge; /* arbre Forge §7 */
/* SYNCRÉTIQUE (§12) — bandes membrane des cercles de contact. Le suffixe _B distingue
 * ces bandes de l'enum moteur Profondeur (scps_tech) : la cloison reste inviolable. */
typedef enum { PROF_OBSCURE, PROF_SURFACE_B, PROF_METIER_B, PROF_PROFOND_B, PROF_SECRET_B } BandProfondeur;
typedef enum { AC_LOINTAIN, AC_PROCHE, AC_IMMINENT, AC_ACQUIS }                   BandAcces;
/* MARCHÉ (sidebar §2/§4) : l'état d'un bien en mots — du marché MORT (ni offre ni
 * demande : la chaîne ne vit pas) à l'ENGORGÉ. La famine de fer devient lisible. */
typedef enum { MARCHE_MORT, MARCHE_PENURIE, MARCHE_TENDU, MARCHE_SAIN, MARCHE_ENGORGE } BandMarche;
/* FIDÉLITÉ d'un vassal (fronde §7) : le ratio ne s'affiche jamais — la fronde se
 * PRESSENT. Classée sur le grief nu [0..1]. */
typedef enum { FID_FIDELE, FID_TIEDE, FID_FRONDEUR, FID_LIGUEUR } BandFidelite;
/* MORAL d'une armée en bataille (brief bataille §7) : la réserve ne sort JAMAIS en
 * float — en bande. FERME → ROMPU. */
typedef enum { MO_FERME, MO_EPROUVE, MO_VACILLANT, MO_ROMPU } BandMoral;
typedef enum { PG_CALME, PG_FREMISSEMENT, PG_OMBRE, PG_SEUIL }                    BandPresage;
/* Panneau de province */
typedef enum { STA_DESERT, STA_HAMEAU, STA_BOURG, STA_CITE, STA_METROPOLE }       BandStature;
typedef enum { FX_EXODE, FX_SAIGNEE, FX_STABLE, FX_AFFLUX, FX_RUEE }              BandFlux;
typedef enum { AI_MISERE, AI_SUFFISANCE, AI_AISANCE, AI_FASTE }                   BandAisance;
typedef enum { CF_NONE, CF_FLORISSANTE, CF_BOUILLONNANTE, CF_SURCHAUFFE }         BandCarrefour;
typedef enum { HU_REVOLTEE, HU_FRONDEUSE, HU_TIEDE, HU_LOYALE, HU_DEVOUEE }       BandHumeur;
typedef enum { LI_MEME_SANG, LI_COUSINE, LI_SOEUR_LOINTAINE, LI_ETRANGERE,
               LI_HERETIQUE_PROCHE, LI_INASSIMILABLE }                            BandLignee;
typedef enum { AG_CALME, AG_FREMISSANTE, AG_AGITEE, AG_INSURGEE }                 BandAgitation;
/* Humeur de FOI de la province face au culte du trône (passe religion §7).
 * Dévote : même branche sacrée, doctrine proche, ferveur — le troupeau fidèle.
 * Tiède : même branche mais dérive doctrinale, ou indifférence pluraliste.
 * Hérétique : foi étrangère, ou schisme du même tronc devenu inconciliable. */
typedef enum { FOI_DEVOTE, FOI_TIEDE, FOI_HERETIQUE }                             BandFoi;

/* ===================================================================== */
/* MÉTRIQUE 0-100 — le NOMBRE de jeu (projection d'une coordonnée cachée) */
/* ===================================================================== */
/* Le joueur voit « Stabilité 78 — Tenue » : un nombre ET un mot. Jamais le
 * flottant SCPS (SI/PE/K/H/L/D∞) ni son nom — la métrique est une PROJECTION
 * de la coordonnée, le mot sa bande, et les effets (§effets) sont ce que la
 * coordonnée FAIT déjà, rendu lisible en courbe. */
typedef struct {
    int         value;   /* 0-100 (ou −100..100 pour l'opinion) */
    const char *word;    /* le mot de bande, déjà résolu (le renderer ignore l'enum) */
    const char *hover;   /* la définition du concept — jamais sa valeur */
} MetricReadout;

/* Une jauge de faction-éthos : son MOT, sa PART (0-100, tangible comme une part de
 * population), et si elle est ALIGNÉE à la direction (sinon elle s'aigrit/complote). */
typedef struct {
    const char *name;
    int         part;          /* 0-100 : part de POUVOIR (poids dans la politique interne) */
    int         satisfaction;  /* 0-100 : SATISFACTION (contentement vis-à-vis du régime) */
    bool        aligned;       /* true = va dans le sens du régime ; false = aliénée */
} FactionGauge;
/* La sédition d'une politique interne : de la concorde au coup qui couve. */
typedef enum { SED_CALME, SED_MURMURE, SED_TENDUE, SED_SEDITIEUSE } BandSedition;

/* ===================================================================== */
/* READOUTS — ce que le renderer reçoit (bandes + chaînes, AUCUN float)   */
/* ===================================================================== */
typedef struct {
    /* Bandes (le MOT) — conservées : lexique typé + compat des bancs d'essai. */
    BandStab     stabilite;
    BandAssise   assise;        /* la SIGNATURE : sur quoi repose l'obéissance */
    BandLegit    legitimite;
    BandConcorde concorde;
    BandProsp    prosperite;
    BandSavoir   savoir;
    BandPresage  presage;       /* masqué si PG_CALME */
    const char  *augure;        /* ligne d'ambiance de péril, ou NULL */
    /* MÉTRIQUES (le NOMBRE 0-100 + le mot + la déf) — la couche de jeu lisible.
     * Cohésion = l'inverse de la fracture (mot emprunté à Concorde). */
    MetricReadout m_stabilite, m_prosperite, m_legitimite, m_cohesion, m_savoir;
    int           influence;    /* 0-100 — réputation diplomatique (posée par le statecraft) */
    int           corruption;   /* 0-100 — §C3 : capture de l'État par concession (le « rot ») */
} CountryReadout;

/* LA BALANCE DES FACTIONS-ÉTHOS (la politique interne) — six jauges (part 0-100, un
 * nombre tangible comme une part de population) + alignement à la direction. La
 * dominante EST l'éthos effectif ; la sédition jauge la faction forte aliénée. Lecture
 * À PART (elle a besoin de l'économie, que le bandeau du royaume n'a pas). */
typedef struct {
    FactionGauge  faction[6];
    const char   *dominant;     /* mot : la faction qui mène (la direction effective) */
    MetricReadout sedition;     /* 0-100 : tension de coup (faction forte opposée à la direction) */
} FactionsReadout;

typedef struct {
    BandHumeur humeur;
    BandLignee lignee;
} AllegeanceReadout;

/* Panneau de province complet (ce que le renderer dessine). Chaînes + bandes,
 * jamais un flottant SCPS. `ames` est une quantité tangible : un nombre est OK. */
typedef struct {
    const char   *nom;
    const char   *terrain;     /* mot (biome nommé) */
    const char   *climat;      /* mot (climat dérivé : Tempéré/Aride/Tropical/Froid…) */
    const char   *relief;      /* mot (relief dérivé de l'altitude : Plaines/Collines/Montagnes) */
    const char   *race;        /* mot (espèce de la population) */
    BandStature   stature;
    long          ames;        /* population — nombre tangible */
    BandFlux      flux;
    const char   *vocation;    /* mot (spécialisation) */
    const char   *ressource;   /* mot */
    BandAisance   aisance;
    BandCarrefour carrefour;   /* CF_NONE si pas un pôle */
    BandHumeur    humeur;
    BandLignee    lignee;
    BandFoi       foi;         /* humeur religieuse face au culte du trône */
    bool          diaspora;
    MetricReadout agitation;   /* 0-100 : L bas + coercition + tension de diversité */
    MetricReadout m_aisance;   /* 0-100 : la PROSPÉRITÉ locale (la jauge rouge→vert de l'en-tête) */
    MetricReadout m_humeur;    /* 0-100 : l'HUMEUR (les visages) */
    bool          seuil_revolte;/* l'agitation a franchi le seuil de révolte */
    /* BÂTIMENTS — la population consomme 1 logement + 1 service chacun ; on
     * affiche les places ENCORE DISPONIBLES (capacité bâtie − population), pas un
     * score abstrait. Plus deux SLOTS RÉSERVÉS lus de l'état bâti. */
    long  logements_libres, logements_cap;   /* habitat : places libres / capacité totale */
    long  services_libres,  services_cap;    /* services : places libres / capacité totale */
    const char *defense;        /* slot DÉFENSE : structure bâtie (palissade/remparts/citadelle) */
    const char *defense_hover;
    const char *specialisation; /* slot PRODUCTION : ce que la province exploite/raffine */
    const char *specialisation_hover;
} ProvinceReadout;

/* PRODUCTION — ce qu'une province PRODUIT par jour, en QUANTITÉ (unités/jour), une
 * ligne par bien : la COLLECTE des brutes et la SORTIE des ateliers. C'est l'income
 * EN RESSOURCE (ou en OR si le bien est de l'or) — la VENTE (→ or via le commerce)
 * est une autre histoire. Nombres tangibles, jamais un flottant SCPS. */
typedef struct {
    const char *source;        /* le bien produit, mot diégétique */
    float       per_day;       /* +N/j : QUANTITÉ produite par jour (unités, 1 décimale) */
    bool        manufactured;  /* false = collecte (brute) ; true = sortie d'atelier */
} IncomeLine;
typedef struct {
    IncomeLine line[6];        /* les biens principaux, triés par quantité */
    int        n;
} IncomeReadout;
IncomeReadout province_income(const WorldEconomy *econ, int region);

/* ===================================================================== */
/* SEUILLAGE — flottants NUS → bandes (la membrane testable)              */
/* ===================================================================== */
/* Stabilité depuis SI, BORNÉE par la fragilité : un ordre tenu par la force
 * ne se lit jamais « Assurée/Inébranlable » (il « a l'air » tenu, pas plus).
 * C'est ce qui fait émerger la signature « Tenue · Contrainte ». */
BandStab     band_stab(float SI, float fragilite);
BandAssise   band_assise(float fragilite);
BandLegit    band_legit(float L);
BandConcorde band_concorde(float fracture, bool secession_mode);
BandProsp    band_prosp(float prosperity_0_10);
BandSavoir   band_savoir(float lumiere_0_10);
BandForge    band_forge(float forge_level_0_10);          /* profondeur de production matérielle (§7) */
/* §syncrétique (§12) — classés sur des nus : profondeur (niveau 0..4) et progression d'accès
 * (0..1). Aucun type/flottant moteur ne traverse l'en-tête ; les libellés parlent CULTURES. */
BandProfondeur band_profondeur(int depth_level_0_4);
BandAcces      band_acces(float progress_0_1);
const char  *label_forge(BandForge b);
const char  *label_profondeur(BandProfondeur b);
const char  *label_acces(BandAcces b);
/* Marché : classé sur demande vs disponible (flottants NUS — le ratio reste derrière). */
BandMarche   band_marche(float demand, float avail);
const char  *label_marche(BandMarche b);
BandFidelite band_fidelite(float grief_0_1);
const char  *label_fidelite(BandFidelite b);
BandMoral    band_moral(float reserve_frac_0_1);
const char  *label_moral(BandMoral b);

/* ---- LENTILLES de carte (sidebar Filtres §6) — par RÉGION, en TEINTES discrètes --
 * La carte se colore par BANDE (4-5 teintes), jamais par gradient continu : un
 * dégradé sur une coordonnée SCPS serait un float qui fuit par la couleur. Tout est
 * calculé ICI (membrane) ; le viewer ne reçoit que des couleurs. */
typedef enum { LENS_NONE=0, LENS_PROSP, LENS_HUMEUR, LENS_MARCHE, LENS_COUNT } MapLens;
void map_lens_tints(const WorldEconomy *econ, const WorldLegitimacy *wl,
                    MapLens lens, uint32_t out[SCPS_MAX_REG]);
const char *map_lens_name(MapLens lens);

/* §11/§12 — lecture PRÉVISIONNELLE d'un nœud syncrétique (le cercle). Bandes + chemin
 * DIÉGÉTIQUE : où en est la diffusion, et ce qui l'ouvrirait. AC_ACQUIS = loqué (permanent,
 * même si la source s'est fondue). Aucun flottant ne traverse ; les chaînes parlent
 * cultures et savoir-faire, jamais races ni coordonnées. `sync_idx` = indice 0..SYNC_COUNT-1. */
typedef struct {
    BandAcces      acces;        /* lointain → acquis */
    BandProfondeur atteinte;     /* profondeur de contact ATTEINTE pour la source */
    BandProfondeur requise;      /* profondeur REQUISE par le nœud */
    const char    *nom;          /* nom du nœud (mot de jeu) */
    const char    *chemin;       /* l'acquis, ou ce qui manque (canal / profondeur / assimilation) */
} SyncReadout;
SyncReadout sync_node_readout(const TechState *ts, int sync_idx);
BandPresage  band_presage(float charge_0_10);
BandHumeur   band_humeur(float L_local);
/* Lignée : horloge (cousinage) ET contenu (friction), + schisme religieux. */
BandLignee   band_lignee(float clock_dist, float content_dist, bool religious_schism);
BandAgitation band_agitation(int agitation_0_100);
/* Foi : disposition religieuse de la province face au culte du trône. */
BandFoi      band_foi(bool same_branch, float religion_dist, bool schism, bool region_fervent);

/* ===================================================================== */
/* PROJECTIONS — coordonnée NUE [0..10] → métrique de jeu [0..100]        */
/* ===================================================================== */
/* La SEULE arithmétique qui touche les flottants. Composites légitimes (la
 * stabilité encaisse l'usure de guerre) ; jamais l'inverse (pas de stat libre). */
int  metric_from_coord(float x_0_10);                       /* x·10, borné, arrondi */
int  metric_stability (float SI, float war_exhaustion_0_1); /* SI − 2·usure, projeté */
int  metric_prosperity(float prosperity_0_10);
int  metric_legitimacy(float L);
int  metric_cohesion  (float fracture);                     /* (10 − fracture)·10 */
int  metric_savoir    (float lumiere_0_10);
/* Agitation d'une province : L bas + coercition + chocs récents + tension de
 * diversité, ABATTUE par la stabilité du pays et la garnison (H bâti). */
int  metric_agitation (float L_local, float coercion_0_1, float diversity_tension_0_10,
                       float recent_shock_0_1, int country_stability_0_100, float garrison_H);

/* ===================================================================== */
/* EFFETS — une COURBE LUE d'une métrique, jamais un modificateur plat     */
/* ===================================================================== */
/* Ces fonctions NE s'ajoutent PAS au moteur : elles SONT ce que les
 * coordonnées font déjà (le rendement de prospérité, la pression de fracture),
 * surfacé au joueur. Un lecteur, pas un bonus. */
#define STAB_REFORM_MIN   40    /* sous ce seuil, certaines réformes sont gâtées */
#define AGIT_REVOLT_SEUIL 70    /* agitation soutenue au-dessus → révolte         */

float prod_multiplier        (int prosperity);   /* 1 + (P−50)/50·0.15  (±15 %) */
float agitation_modifier     (int stability);    /* −(Stab/100)·2  (−2 à 100)   */
bool  can_enact_reform       (int stability);    /* gate : Stab ≥ seuil          */
float aggression_stability_cost(int stability);  /* surcoût des actes agressifs  */
float integration_speed      (int legitimacy);   /* vitesse de montée de L        */
float research_pace          (int savoir);       /* pacing de la recherche        */
bool  revolt_threshold_reached(int agitation);   /* agitation ≥ seuil de révolte  */

/* ===================================================================== */
/* ASSEMBLAGE — depuis flottants nus (testable sans la sim)               */
/* ===================================================================== */
/* La membrane proprement dite : tous les flottants SCPS entrent ICI, rien
 * n'en ressort que des bandes. `pression` sert à distinguer sécession (la
 * fracture domine) de révolution (la pression domine) pour l'augure. */
CountryReadout country_readout_from_floats(
    float SI, float fragilite, float fracture, float pression,
    float L, float prosperity_0_10, float lumiere_0_10, float charge_0_10);

AllegeanceReadout allegeance_from_floats(
    float L_local, float clock_dist, float content_dist, bool religious_schism);

/* ===================================================================== */
/* ENVELOPPES RENDERER — les SEULES fonctions que viewer.c/render.c appellent */
/* ===================================================================== */
/* Lisent les sorties STOCKÉES (prospérité §2.4 + légitimité) → bandes. Aucun
 * appel à scps_core ici : tout a déjà été calculé par prosperity_tick. */
CountryReadout  country_readout (const WorldProsperity *wp, const TechState *ts,
                                 const World *w, int cid);
ProvinceReadout province_readout(const World *w, const WorldEconomy *econ,
                                 const WorldProsperity *wp, const WorldLegitimacy *wl,
                                 int province_id);

/* La balance des factions-éthos d'un pays (politique interne) — mots + parts 0-100. */
FactionsReadout faction_readout(const World *w, const WorldEconomy *econ, int cid);

/* ===================================================================== */
/* ARBRE DE TECH — la membrane de l'arbre CONCENTRIQUE (mots + nombres)    */
/* ===================================================================== */
/* Le renderer dessine un arbre concentrique : ANGLE = quartier (0..8 = thème×
 * fonction), RAYON = tier. Il ne lit AUCUN flottant de tech : il reçoit, par
 * nœud, sa position (quartier/tier), son ÉTAT (mot), s'il est faustien/orphelin,
 * son nom, ce qu'il déverrouille, et son COÛT (un nombre tangible : des points). */
typedef enum { TREE_LOCKED = 0, TREE_OPEN, TREE_DONE } TreeState; /* verrouillé/disponible/acquis */
typedef struct {
    int         quarter;    /* 0..8 (= thème*3 + fonction) — l'ANGLE */
    int         tier;       /* le RAYON (profondeur) */
    TreeState   state;
    bool        faustian;   /* ⚠ bout interdit */
    bool        orphan;     /* signature d'une AUTRE race, accès manquant (greffe possible) */
    bool        is_base;    /* bâtiment de base (le centre) */
    const char *name;
    const char *unlocks;    /* le bâtiment/capacité déverrouillé */
    const char *effet;      /* l'UTILITÉ concrète du bâtiment (mots de jeu) :
                             * « permet la production de bois », « +logements », … */
    int         cost;       /* points de recherche (0 pour les bases) — nombre tangible */
} TreeNodeReadout;
typedef struct {
    TreeNodeReadout node[TECH_COUNT];
    int         n;          /* = TECH_COUNT */
    int         points;     /* points de recherche DISPONIBLES (tangible) */
    int         n_themes;   /* 3 */
    int         n_functions;/* 3 */
    const char *theme[3];    /* "Savoir" / "Forge" / "Société" — l'ordre des secteurs */
    const char *function[3]; /* "Production" / "Armée" / "Renforcement" */
} TechTreeReadout;
/* Remplit le readout depuis l'état de tech d'un empire : son masque d'accès de
 * race (pour les orphelines) et sa population (pour le coût). */
void tech_tree_readout(const TechState *ts, unsigned race_access, float population,
                       TechTreeReadout *out);
const char *label_tree_state(TreeState s);   /* "verrouillé"/"disponible"/"acquis" */

/* ===================================================================== */
/* LEXIQUE — un mot (label) + une définition (hover) par bande            */
/* ===================================================================== */
/* label_X(band) → le MOT affiché. hover_X() → la définition d'une phrase
 * (le sens du concept, jamais sa valeur). Le renderer n'appelle que ça. */
const char *label_stab(BandStab b);        const char *hover_stab(void);
const char *label_assise(BandAssise b);    const char *hover_assise(void);
const char *label_legit(BandLegit b);      const char *hover_legit(void);
const char *label_sedition(BandSedition b);const char *hover_sedition(void);
BandSedition band_sedition(float coup_tension_0_1);
const char *label_concorde(BandConcorde b);const char *hover_concorde(void);
const char *label_prosp(BandProsp b);      const char *hover_prosp(void);
const char *label_savoir(BandSavoir b);    const char *hover_savoir(void);
const char *label_presage(BandPresage b);  const char *hover_presage(void);
const char *label_stature(BandStature b);  const char *hover_stature(void);
const char *label_flux(BandFlux b);        const char *hover_flux(void);
const char *label_aisance(BandAisance b);  const char *hover_aisance(void);
const char *label_carrefour(BandCarrefour b); const char *hover_carrefour(void);
const char *label_humeur(BandHumeur b);    const char *hover_humeur(void);
const char *label_lignee(BandLignee b);    const char *hover_lignee(void);
const char *label_agitation(BandAgitation b); const char *hover_agitation(void);
const char *label_foi(BandFoi b);          const char *hover_foi(void);

#endif /* SCPS_READOUT_H */
