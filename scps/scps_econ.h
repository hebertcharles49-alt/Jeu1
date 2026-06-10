/*
 * scps_econ.h — couche ÉCONOMIE & CLASSES SOCIALES (moteur, sans UI)
 *
 * Tout est intriqué autour de la POPULATION, répartie en trois strates :
 *
 *   LABORERS   — la masse. Fournit le travail (RGO + manufactures), demande
 *                des biens de base (vivres, étoffe, bois de feu).
 *   BOURGEOIS  — marchands/artisans. Possèdent et gèrent les manufactures,
 *                captent le profit, demandent des biens manufacturés.
 *   ELITES     — aristocratie. Vivent de la taxe et de la rente, produisent
 *                la TECH (recherche) et exigent des biens de luxe (joaillerie,
 *                étoffe précieuse, vin).
 *
 * Unité de simulation : la RÉGION (= le « marché régional » du cahier des
 * charges). Chaque région a :
 *   - une capacité d'extraction de matières premières (héritée de la
 *     géographie des provinces qui la composent) ;
 *   - des manufactures (laine → tissu, bois → papier, or → joaillerie…) ;
 *   - un entrepôt (stock par bien) et un PRIX par bien déterminé par la
 *     demande face au stock+offre.
 *
 * Boucle (econ_tick) :
 *   1. Extraction des matières premières (emploie des laborers).
 *   2. Manufacture : consomme intrants → produit biens finis (laborers +
 *      encadrement bourgeois).
 *   3. Revenus : salaires → laborers ; profit → bourgeois ; taxe → elites.
 *   4. Demande : besoin par tête × population, par strate.
 *   5. Marché : prix = base × demande/(stock+offre) ; allocation au budget ;
 *      satisfaction par strate.
 *   6. Mise à jour : stock reporté, satisfaction → croissance de pop, elites
 *      convertissent richesse+satisfaction en tech.
 */
#ifndef SCPS_ECON_H
#define SCPS_ECON_H

#include "scps_types.h"
#include "scps_culture.h"   /* PopCulture embarque les traits dérivés (Ethos, …) */
#include "scps_species.h"   /* couche biologique : race + traits (leviers) */
#include "scps_tech.h"      /* TechState : §B1 abonde prod_mult par les techs de production */

/* ---- Strates sociales ------------------------------------------------- */
typedef enum {
    CLASS_LABORER = 0,
    CLASS_BOURGEOIS,
    CLASS_ELITE,
    CLASS_COUNT
} SocialClass;

typedef struct {
    float pop;            /* effectif */
    float wealth;         /* trésor accumulé (monnaie) */
    float satisfaction;   /* [0..1] : fraction des besoins couverts au dernier tick */
} PopStratum;

/* ---- Manufactures (transforment matières premières → biens finis) ------ */
typedef enum {
    BLD_TEXTILE = 0,   /* laine   → tissu               */
    BLD_SAWMILL,       /* bois    → fournitures navales */
    BLD_PAPERMILL,     /* bois    → papier              */
    BLD_WINERY,        /* sucre   → vin                 */
    BLD_BREWERY,       /* grain   → bière (palier moral des Clans/Souterrain) */
    BLD_JEWELER,       /* or+métal précieux → joaillerie (bien d'élite) */
    BLD_WEAVER_LUX,    /* tissu   → étoffe précieuse    */
    BLD_MAGE_WORKSHOP, /* ARCANE : cristal → essence (mana) — sa combustion MONTE la Brèche */
    BLD_CELESTIAL_FORGE,/* ARCANE militaire : fer céleste + essence → armes enchantées */
    BLD_FOUNDRY,       /* fer + charbon → métal (haut-fourneau) */
    BLD_TOOLWORKS,     /* métal + bois → outils → MULTIPLICATEUR de productivité */
    BLD_ARMORY,        /* fer → armes (armurerie) → puissance militaire de base */
    BLD_POWDERMILL,    /* salpêtre + charbon → poudre (poudrière) → puissance militaire */
    BLD_APOTHECARY,    /* simples → remèdes (apothicaire) → santé/confort */
    BLD_TUNIC,         /* étoffe → TUNIQUE (1:1) — vêtement fini des journaliers (chaîne séparée du luxe) */
    BLD_CHARCOAL,      /* 2 bois → 1 charbon (charbonnière, tech de base) — libère la fonderie de la rareté du charbon minier */
    BLD_FOREUSE,       /* §B2 FAUSTIEN : essence → GROS rendement de FER (foreuse arcanique) — l'issue à la famine de matière, payée en charge (tech) */
    BLD_TYPE_COUNT
} BuildingType;

typedef struct {
    BuildingType type;
    float        level;     /* capacité (échelle de production) */
    float        workers;   /* emploi effectif au dernier tick  */
} Building;

#define ECON_MAX_BLD BLD_TYPE_COUNT  /* une manufacture de CHAQUE type par région — calé sur
                                      * l'enum (était figé à 6 alors que les types ont grimpé à
                                      * 14 : les bâtiments communs saturaient les slots et
                                      * évinçaient les rares — joaillerie, fonderie, outillage —
                                      * d'où leur pénurie. Désormais auto-calé : plus de désync). */

/* ---- Profil culturel de la population d'une région --------------------- *
 * Distinct de la géographie : ce sont les gens qui ont une culture, pas la
 * terre. Initialisé depuis le biome dominant à la génération (gen_population),
 * puis mutable via syncrétisme, dérive (world_tick) et migration. */
typedef struct {
    /* Les cinq axes [0..10] — seuls axes lus pour la distance culturelle. */
    float langue;      /* horloge phylogénétique */
    float valeurs;
    float subsistance; /* ancrée sur le biome dominant à l'init */
    float parente;
    float religion;
    /* Traits dérivés (résolus par culture_make, remutés par syncrétisme). */
    Ethos        ethos;
    Lifeway      lifeway;
    Structure    structure;
    Credo        credo;
    ReligionBranch rel_branch;
    MartialTrait martial;
    EconTrait    econ;
    int  age;       /* ticks d'existence (dérive) */
    bool settled;   /* false = région vierge, pas encore peuplée */
    /* Couche BIOLOGIQUE (superposée à la culture) : la race et ses leviers
     * (démographie, K, P, H, dérive, fracture…). Posée par worldgen_seed_peoples. */
    SpeciesArchetype race;
} PopCulture;

/* ---- GROUPES de population (clé de voûte démographique) ---------------- *
 * Une province ne contient plus une fiche homogène mais des GROUPES
 * (race, culture, classe, effectif). D interne = distance ENTRE groupes ;
 * H agit SUR eux ; l'assimilation fait DÉRIVER la culture d'une minorité.
 * `culture` est la fiche EFFECTIVE (cache recalculé = origine + dérive) — ce que
 * lisent prosperity/legitimacy ; `origin` est le substrat FIXE. Rétro-compat :
 * une province à UN groupe reproduit exactement les nombres d'hier. */
#define SCPS_MAX_GROUPS 8
typedef struct {
    SpeciesArchetype race;
    Sphere       origin_sphere;  /* FIXE : pour le gouffre */
    PopCulture   origin;         /* substrat FIXE */
    PopCulture   culture;        /* fiche EFFECTIVE (cache = origine + dérive) */
    SocialClass  klass;          /* (hérité) la classe « principale » du groupe */
    long         count;
    /* CLASSE ÉMERGENTE (§pop précise) : combien de ce groupe (race×culture×foi) sont
     * Journaliers / Bourgeois / Nobles — sort des EMPLOIS (capitale + ateliers), par
     * paquets de 100. Σ pop_by_class = count. Jamais posé : recalculé au tick. */
    long         pop_by_class[CLASS_COUNT];
    float        L;              /* légitimité du groupe envers la couronne */
    float        agit_base;      /* agitation VRAIE (la suppression la masque) */
    float        integration;    /* 0..1 → pilote l'assimilation */
    bool         diaspora;       /* installé loin de sa terre (migration) */
    int          drift_id;       /* clé dans la pile de dérive du monde */
} PopGroup;
typedef struct {
    PopGroup groups[SCPS_MAX_GROUPS];
    int      n_groups;           /* 0 = non attaché → repli sur RegionEconomy.culture */
    int      prov;               /* province du monde (géo) — optionnel */
    float    prosperity;         /* prospérité locale [0..10] (gradient de migration) */
} ProvincePop;

/* ---- Densité institutionnelle bâtie (couche d'agency) ----------------- *
 * Un bâtiment n'est pas un bonus : c'est de la densité institutionnelle
 * RÉALISÉE qui déplace une coordonnée que le moteur d'ordre LIT. Ces
 * accumulateurs sont remplis par scps_agency (à l'achèvement d'un édifice) et
 * relus par prosperity_tick (K/P/H) et legitimacy_tick (H ronge L). */
typedef struct {
    float K_inst;    /* institutionnel → monte K du pays */
    float H_coerc;   /* coercitif      → monte H, RONGE L local */
    float P_open;    /* ouverture      → monte P (perméabilité) */
    float PE_infra;  /* prospérité     → capte plus de PE local */
    float food_cap;  /* rendement/stockage alimentaire → croissance */
    float faith;     /* foi (temple/sanctuaire) → SOUTIENT L local (apaise l'agitation) */
    float savoir;    /* savoir bâti (bibliothèque/monastère) → ACCÉLÈRE la recherche locale */
    float port;      /* le PORT réel (chantier·rade·débouché — mer §5) ; ≠ P_open (le
                      * Caravansérail ouvre P sans donner de rade). Posé par EDI_PORT. */
} ProvBuild;

/* ---- Économie d'une région -------------------------------------------- */
typedef struct {
    PopStratum strata[CLASS_COUNT];
    PopCulture culture;   /* profil culturel DOMINANT (synchronisé sur le plus gros groupe) */
    ProvincePop pop;      /* les GROUPES de la province (clé de voûte) — n_groups=0 si non attaché */
    ProvBuild  build;     /* densité institutionnelle bâtie par le joueur */
    float      route_pe;  /* PE apporté par les routes commerciales (transitoire) */

    float      raw_cap[RES_COUNT];   /* extraction max/tick par matière première */
    Building   bld[ECON_MAX_BLD];
    int        n_bld;

    float      stock [RES_COUNT];    /* entrepôt régional */
    float      price [RES_COUNT];    /* prix de marché courant */
    float      demand[RES_COUNT];    /* demande agrégée (dernier tick) */
    float      supply[RES_COUNT];    /* offre agrégée (dernier tick) */

    float      treasury;             /* taxe captée par les élites (cumul) */
    float      tech;                 /* recherche cumulée */
    float      tech_prod;            /* §B1 : multiplicateur de prod issu des techs de PRODUCTION du pays (1 = aucun) */
    bool       tech_foreuse;         /* §B2 : le pays a-t-il débloqué la foreuse arcanique ? (gate de BLD_FOREUSE) */
    float      gdp;                  /* valeur produite au dernier tick */
    float      satisfaction;         /* satisfaction générale [0..1] */
    float      food_sat;             /* satisfaction alimentaire [0..1] (grain+fish) */
    float      society_sat;          /* satisfaction sociale [0..1] (cloth+wine+…) */
    float      over_tax;             /* surtaxe ressentie par les laboureurs [0..1] (grief → révolte) */
    float      cap_pop;              /* capacité d'accueil (pop cible à terme) */
    float      prosperity;           /* PIB/tête normalisé — cache pour migration */

    /* Diaspora & innovation culturelle */
    float      diaspora_pop;         /* immigrants non-primaires installés (bourgeois+élites) */
    float      diaspora_innovation;  /* score d'innovation cumulé (diminue par acculturation) */

    /* Coercition temporaire (relocalisation forcée) — décroît chaque tick */
    float      coercion;             /* [0..1] : 0=libre, 1=état d'urgence */
    /* Cicatrice de révolte [0..1] : une province récemment soulevée se développe
     * MAL (−50 % de croissance ET de production) ; décroît sur quelques années. */
    float      revolt_scar;
    /* Anti-saccage (§4 guerre) : une province DÉPOUILLÉE ne peut l'être à nouveau
     * avant ~5 ans (plus rien à prendre) — compteur en années, décroît chaque tick. */
    float      pillage_cd;

    /* ARCANE (§ fil arcane) : essence brûlée ce tick par les ateliers de mage.
     * prosperity_tick l'agrège dans le flux faustien → déréalisation/Brèche. */
    float      arcane_charge;

    float      habitability;          /* habitabilité moyenne [0..1] — héritée des provinces */
    bool       active;               /* terre habitable (colonisable) */
    bool       impassable;           /* zone morte : infranchissable pour colonisation et commerce */
    bool       colonized;            /* effectivement peuplée/settlée */
    int16_t    owner;                /* pays qui contrôle la région (-1 = vierge) */
    bool       coastal;              /* une province au moins touche la mer (posé à econ_init) */
    bool       estuary;              /* une EMBOUCHURE vit ici (mer ∩ gros fleuve) — l'entrepôt naturel */
    /* LA COURSE (coques §4) : balafre côtière et immunité au raid. */
    float      balafre_days;         /* > 0 : côte balafrée (production entaillée ~1 an) */
    float      raid_cd_days;         /* > 0 : immunisée (~5 ans — on ne trait pas la même vache) */
} RegionEconomy;

/* Conteneur — possédé par l'appelant, séparé du World pour ne pas alourdir
 * son ABI. */
typedef struct {
    RegionEconomy region[SCPS_MAX_REG];
    int           n_regions;
    int           tick;

    /* Adjacence de régions (terre, 4-connexe) — calculée à l'init, sert à
     * la colonisation (expansion vers une région vierge voisine). */
    uint8_t       adj[SCPS_MAX_REG][SCPS_MAX_REG];
} WorldEconomy;

/* ---- API -------------------------------------------------------------- */

/* Initialise pops, capacités d'extraction et manufactures à partir de la
 * géographie/ressources du monde déjà généré. */
void econ_init(WorldEconomy *e, const World *w);

/* Avance la simulation d'un pas. dt = années/tick (1 en annuel, 1/12 en mensuel) :
 * les processus cumulatifs (croissance, tech, impôt→trésor) suivent dt, les flux
 * production/consommation s'équilibrent par tick (satisfaction préservée). */
void econ_tick(WorldEconomy *e, float dt);

/* §B1 — pousse le bonus de PRODUCTION (techs Forge/Société/Savoir·Production) du PAYS
 * propriétaire vers chaque région (re->tech_prod), lu par econ_tick pour abonder prod_mult.
 * À appeler par le harnais avant econ_tick (la recherche pays → la prod région). */
void econ_apply_country_tech(WorldEconomy *e, const TechState *ts, int n_ts);

/* Tolérance fiscale [0..1] par ÉTHOS × classe (§7) : le seuil (×satisfaction)
 * au-delà duquel on fuit l'impôt et l'on gronde. Exposé pour les bancs d'essai. */
float econ_tax_tolerance(Ethos e, SocialClass c);

/* §4 (catalogue des biens) — fraction de pop « mal servie » d'une province :
 * une minorité d'une autre SPHÈRE réclame ses variantes ; l'assimilation efface
 * la pénalité. 0 si la province est homogène. Frappe la satisfaction SOCIALE. */
float econ_off_culture_fraction(const ProvincePop *pp);

/* Pas de colonisation : joueur et antagonistes essaiment vers les régions
 * vierges voisines ; les cités-états colonisent leurs propres territoires
 * non encore peuplés. À appeler après econ_tick(). Renvoie le nb de régions
 * nouvellement colonisées ce tick. */
int econ_colonize_tick(WorldEconomy *e, const World *w);
/* Fonde une colonie de `cid` sur `dst` depuis `src` (pop essaimée, owner posé).
 * Exposé pour la colonisation OUTRE-MER (scps_navy §8) — même acte fondateur. */
void econ_colonize_from(WorldEconomy *e, int src_rid, int dst_rid, int cid);

/* Migration interne basée sur la prospérité : les bourgeois et élites
 * migrent vers les régions plus riches adjacentes. Crée de la diaspora et
 * de l'innovation dans la destination. Renvoie le nb de flux migrateurs
 * actifs ce tick. */
int econ_migrate_tick(WorldEconomy *e, const World *w);

/* Relocalisation forcée : déplace `amount` habitants (surtout laborers)
 * de src_rid vers dst_rid. Provoque un pic de coercition dans la source.
 * Peut être appelée par le joueur ou par un événement scénarisé. */
void econ_relocate_pop(WorldEconomy *e, int src_rid, int dst_rid, float amount);

/* Affiche un tableau récapitulatif d'une région sur stdout. */
void econ_print_region(const WorldEconomy *e, const World *w, int region_id);

/* Affiche un sommaire monde (totaux pop/tech/PIB + top régions). */
void econ_print_summary(const WorldEconomy *e, const World *w);

/* Libellés */
const char *social_class_name(SocialClass c);
const char *building_name(BuildingType b);
/* Recette d'un bâtiment (intrants → extrant) — pour la perception IA. */
void        building_recipe(BuildingType b, Resource *in1, Resource *in2, Resource *out);

#endif /* SCPS_ECON_H */
