/*
 * scps_types.h — structures de données du moteur SCPS
 *
 * Hiérarchie de données :
 *   World  →  Region[]  →  Province[]  →  Cell[]
 *
 * Extension future : ajouter des champs dans Province/Region/World
 * sans toucher aux signatures des modules (world, render, diplo, eco…).
 */
#ifndef SCPS_TYPES_H
#define SCPS_TYPES_H

#include <stdint.h>
#include <stdbool.h>

/* ---- Dimensions -------------------------------------------------------- */
#define SCPS_W           512
#define SCPS_H           256
#define SCPS_N           (SCPS_W * SCPS_H)

/* Hiérarchie territoriale (doc §3) :
 *   territoire (province) → 3-5 = région → 3-5 = pays
 *   continent = masse continentale géographique (séparée par l'océan),
 *   hébergeant ~4-7 pays. */
#define SCPS_MAX_PROV      320
#define SCPS_MAX_REG       130
#define SCPS_MAX_COUNTRY    56
#define SCPS_MAX_CONTINENT  16
#define SCPS_REG_TARGET_MIN 2   /* territoires par région (unités plus fines → plus de pays) */
#define SCPS_REG_TARGET_MAX 3
#define SCPS_CTY_TARGET_MIN 1   /* régions par pays : viser ~40 pays (15 empires + 20 cités) */
#define SCPS_CTY_TARGET_MAX 3
#define SCPS_RIVER_MAXLEN 768

/* ---- Seuils de hauteur (0..1) ----------------------------------------- */
#define SEA_LEVEL     0.43f
#define MOUNTAIN_H    0.78f   /* relief relevé : moins de montagnes, plus de vallées */
#define PEAK_H        0.88f

/* ---- Paramètres de génération (futurs « sliders » façon Civ) -----------
 * Toutes les valeurs continues sont normalisées : 0.5 = neutre/défaut,
 * sauf indication. Le générateur lit ces réglages ; l'UI viendra les
 * piloter. (La taille de carte reste fixe pour l'instant — un passage en
 * allocation dynamique sera nécessaire pour la rendre réglable.) */
typedef struct {
    uint32_t seed;
    int      n_continents;   /* 1..8  — masses continentales visées        */
    float    land_amount;    /* 0..1  — 0.5 neutre ; haut = plus de terres */
    float    world_age;      /* 0..1  — vieux = relief usé (érosion therm.) */
    float    erosion;        /* 0..1  — intensité du creusement hydraulique */
    float    mountains;      /* 0..1  — amplitude du relief                 */
    float    temperature;    /* 0..1  — 0.5 neutre ; haut = monde chaud     */
    float    humidity;       /* 0..1  — 0.5 neutre ; haut = monde humide    */
    int      n_empires;      /* empires (joueur + antagonistes) visés ; 0 = défaut (15) */
    int      n_city_states;  /* cités-états visées ; 0 = défaut (20)                    */
} WorldParams;

/* ---- Biomes ------------------------------------------------------------ */
typedef enum {
    BIO_DEEP_OCEAN = 0,
    BIO_OCEAN,
    BIO_SHALLOW,
    BIO_COAST,
    BIO_PLAINS,
    BIO_FARMLAND,
    BIO_GRASSLAND,
    BIO_STEPPE,
    BIO_SAVANNA,
    BIO_DRYLANDS,
    BIO_DESERT,
    BIO_COASTAL_DESERT,
    BIO_FOREST,
    BIO_WOODS,
    BIO_JUNGLE,
    BIO_MARSH,
    BIO_HIGHLANDS,
    BIO_HILLS,
    BIO_MOUNTAINS,
    BIO_PEAK,
    BIO_GLACIER,
    BIO_MANGROVE,      /* côte tropicale ennoyée (ajouté par l'altération) */
    BIO_BOG,           /* tourbière froide / lande humide */
    BIO_VOLCANO,       /* cône/caldeira — roche nue et cendres */
    BIO_COUNT
} Biome;

/* ---- Cellule de carte (données géographiques brutes) ------------------- */
typedef struct {
    /* Couches de génération */
    float    height;
    float    moisture;
    float    temperature;
    float    fertility;        /* potentiel de civilisation [0..1] */

    /* Classification — hiérarchie territoriale */
    Biome    biome;
    int16_t  province;         /* territoire ; -1 = mer */
    int16_t  region;           /* -1 = mer */
    int16_t  country;          /* pays ; -1 = mer */
    int16_t  continent;        /* masse continentale ; -1 = mer */

    /* Hydrologie */
    uint8_t  river;            /* débit accumulé en aval [0..255] */
    int8_t   flow_dir;         /* direction D8 vers l'aval (-1 = exutoire) */
    bool     lake;

    /* Géographie dérivée */
    float    ocean_dist;       /* continentalité [0=côte .. 1=intérieur profond] */
    float    rainfall;         /* précipitation simulée par advection [0..1] */

    /* Flags de rendu (précalculés) */
    bool     coast;            /* adjacent à la mer */
    bool     border_prov;      /* frontière de territoire */
    bool     border_reg;       /* frontière de région */
    bool     border_country;   /* frontière de pays */
    bool     border_continent; /* trait de côte du continent */
    float    shade;            /* hillshading [0..1] */

    /* ── LA MER (brief mer) : courants de surface DÉRIVÉS du vent — un CHAMP de
     * worldgen (pas une simulation). Vecteur quantifié [-100..100] ; classe : la
     * géographie de l'océan (couloirs · eaux vives · eaux mortes · cabotage). */
    int8_t   cur_vx, cur_vy;   /* (0,0) à terre */
    uint8_t  sea;              /* SeaClass — 0 = terre */
} Cell;

/* Les trois espaces marins (+ la côte) — le design de l'océan. */
typedef enum { SEA_NONE=0, SEA_CABOTAGE, SEA_MORTE, SEA_VIVE, SEA_COURANT } SeaClass;

/* ---- Ressources / biens commerciaux ----------------------------------
 * Deux familles :
 *   BRUTES      — posées par la géographie (ce générateur les attribue).
 *   PRODUCTION  — fabriquées à partir des brutes (chaînes à venir : le
 *                 générateur ne les pose PAS encore, cf. doc §9).
 * Tout ce qui est < RES_PROD_FIRST est une ressource brute. */
typedef enum {
    RES_NONE = 0,
    /* --- Brutes : agricole & élevage --- */
    RES_GRAIN,          /* flatlands humides            */
    RES_LIVESTOCK,      /* flatlands pastoraux          */
    RES_WOOL,           /* flatlands/collines pastoraux */
    RES_FISH,           /* côte ou fleuve à fort débit  */
    RES_FUR,            /* régions froides, sauvages    */
    RES_SALT,           /* déserts et côtes             */
    RES_COTTON,         /* flatlands arides             */
    RES_SUGAR,          /* côtes arides                 */
    RES_WOOD,           /* régions boisées              */
    RES_MED_HERBS,      /* herbes médicinales — wetland d'altitude */
    /* --- Brutes : minéral & stratégique --- */
    RES_COPPER,         /* montagnes, collines, mesas   */
    RES_IRON,           /* montagnes, collines, mesas   */
    RES_COAL,           /* gisements de relief          */
    RES_SULFUR,         /* volcanique / montagne        */
    RES_SALTPETER,      /* arides / grottes (→ poudre)  */
    RES_GOLD,           /* montagnes (parfois artefact) */
    RES_PRECIOUS_METAL, /* mithril, adamantium — profond/rare */
    RES_PEARL,          /* perle — littoral rare ; luxe ouvré (orfèvrerie premium ×2 or) */
    RES_ARCANE_CRYSTAL, /* cristal arcanique — RÉSIDU de la Conjonction, nœuds telluriques (TRÈS rare) */
    RES_CELESTIAL_IRON, /* fer céleste — météorique : cratères/sommets (TRÈS rare) → armes enchantées */
    RES_MUREX,          /* teinture POURPRE — côtière (arbitrage pêche/sel) → étoffe précieuse */
    RES_INDIGO,         /* teinture BLEUE — bas-pays chaud, plante (arbitrage grain) → étoffe précieuse */

    /* === Frontière : tout ce qui suit est un bien de PRODUCTION === */
    RES_PROD_FIRST,
    RES_CLOTH = RES_PROD_FIRST, /* production               */
    RES_NAVAL_SUPPLIES,         /* production (bois+goudron) */
    RES_WINE,                   /* boisson des Cités/Sylvain (vin) — palier MORAL */
    RES_BEER,                   /* boisson des Clans/Souterrain/Sauvage (bière) — palier MORAL */
    RES_PRECIOUS_WARE,          /* bien précieux des 4 races (porcelaine, bière…) */
    RES_PRECIOUS_CLOTH,         /* étoffe précieuse des 4 races */
    RES_PAPER,                  /* transformation           */
    RES_METAL,                  /* fer + charbon → métal (Fonderie) — intrant outils/armes */
    RES_TOOLS,                  /* métal + bois → outils (Atelier) → MULTIPLICATEUR de productivité */
    RES_ESSENCE,                /* ARCANE : mana raffiné (cristal brûlé) → sa combustion MONTE la Brèche */
    RES_ENCHANTED_ARMS,         /* armes/armures enchantées (fer céleste + essence) → puissance militaire */
    RES_ARMS,                   /* armes & armures (fer → Armurerie) → puissance militaire de BASE */
    RES_GUNPOWDER,              /* poudre (salpêtre + charbon → Poudrière) → puissance militaire */
    RES_REMEDE,                 /* remèdes (simples → Apothicaire) → santé (besoin de confort) */
    RES_TUNIQUE,                /* TUNIQUE — vêtement fini des JOURNALIERS (étoffe → tunique 1:1) */
    RES_COUNT
} Resource;

/* ---- Province (entité politique de base) ------------------------------- */
typedef struct {
    int16_t  seed_x, seed_y;
    int16_t  region;
    int16_t  country;
    int16_t  continent;
    int      area;
    Biome    biome_dominant;
    float    lat;              /* latitude moyenne [0=éq., 1=pôle] */
    float    height_avg;
    bool     coastal;          /* touche la mer */

    /* Économie */
    Resource resource;         /* bien commercial principal (brute dominante) */
    Resource resource2;        /* §6b : brute SECONDAIRE mineure (raw_cap ×0.4) — casse
                                * le « une seule brute par province », sauve les rares */

    /* Habitabilité [0..1] — calculée une fois à la génération (biome + temp + altitude).
     * 0 = région morte (glacier, pic, désert hyperaride) ;
     * 1 = terres cultivées idéales.
     * Sert de filtre dans econ_init (active/impassable) et de couche visuelle. */
    float    habitability;

    /* NOTE : aucun champ culturel ici. La culture (langue, valeurs, subsistance,
     * parenté, religion, traits dérivés) est une propriété de la POPULATION,
     * portée par RegionEconomy.culture (PopCulture, cf. scps_econ.h), pas du
     * terrain. La Province reste strictement géographique. */

    /* Rendu */
    uint32_t color;            /* ARGB, précalculé à la génération */
    char     name[24];         /* stub — sera enrichi plus tard */
} Province;

/* ---- Région : 3-5 territoires contigus -------------------------------- */
typedef struct {
    int      seed_x, seed_y;
    int      n_provinces;
    int16_t  province_ids[12];  /* cible 3-5, marge à 12 */
    int16_t  country;
    int16_t  continent;
    uint32_t color;
    char     name[32];          /* nom courant (= variante humaine) */
    /* Toponymie des 4 peuples (préfixe/suffixe liés à l'environnement) */
    char     name_hum[32];      /* langue commune (descriptif) */
    char     name_elf[32];      /* elfique — mélodique */
    char     name_dwarf[32];    /* nain — gutturalo-minéral */
    char     name_orc[32];      /* orque — rauque */
} Region;

/* ---- Rôle politique d'un pays ----------------------------------------- *
 * Au démarrage, le monde est essentiellement VIDE : seul le joueur et une
 * poignée de cités-états sont peuplés. Tout le reste est colonisable.
 * JOUEUR / ANTAGONISTE : colonisent le monde entier (expansion libre).
 * CITÉ-ÉTAT : toute leur région démarrée à pop réduite ; colonisent uniquement
 *   leurs territoires propres vacants — commerce, pas conquête extérieure. */
typedef enum {
    POLITY_PLAYER = 0,   /* le joueur — capitale peuplée au départ */
    POLITY_ANTAGONIST,   /* IA majeure — peuplée, colonise */
    POLITY_CITY_STATE,   /* cité-état — peuplée sur toute sa région, colonise ses propres territoires */
    POLITY_UNCLAIMED     /* terres vierges colonisables (pays sans départ) */
} PolityRole;

/* ---- Pays : 3-5 régions contiguës ------------------------------------- */
typedef struct {
    int        n_regions;
    int16_t    region_ids[12];
    int16_t    continent;
    int        capital_prov;      /* province-capitale (plus fertile) */
    PolityRole role;              /* joueur / antagoniste / cité-état / vierge */
    uint32_t   color;
    char       name[32];
} Country;

/* ---- Continent : masse continentale géographique ---------------------- */
typedef struct {
    int      area;              /* cellules terrestres */
    int      n_countries;
    int16_t  country_ids[SCPS_MAX_COUNTRY];
    uint32_t color;
    char     name[32];
} Continent;

/* ---- Rivière tracée ---------------------------------------------------- */
typedef struct {
    int16_t x[SCPS_RIVER_MAXLEN];
    int16_t y[SCPS_RIVER_MAXLEN];
    int     len;
    float   flow_max;
} River;

/* ---- Monde -------------------------------------------------------------- */
#define SCPS_MAX_RIVERS 64

typedef struct {
    Cell      cell[SCPS_N];
    Province  province[SCPS_MAX_PROV];
    int       n_provinces;
    Region    region[SCPS_MAX_REG];
    int       n_regions;
    Country   country[SCPS_MAX_COUNTRY];
    int       n_countries;
    Continent continent[SCPS_MAX_CONTINENT];
    int       n_continents;
    River     river[SCPS_MAX_RIVERS];
    int       n_rivers;
    uint32_t  seed;
} World;

/* ---- Accesseur sûr aux cellules --------------------------------------- */
static inline Cell *scps_cell(World *w, int x, int y) {
    if (x < 0) x = 0; else if (x >= SCPS_W) x = SCPS_W-1;
    if (y < 0) y = 0; else if (y >= SCPS_H) y = SCPS_H-1;
    return &w->cell[y * SCPS_W + x];
}
static inline const Cell *scps_cellc(const World *w, int x, int y) {
    if (x < 0) x = 0; else if (x >= SCPS_W) x = SCPS_W-1;
    if (y < 0) y = 0; else if (y >= SCPS_H) y = SCPS_H-1;
    return &w->cell[y * SCPS_W + x];
}
static inline int scps_idx(int x, int y) { return y * SCPS_W + x; }

#endif /* SCPS_TYPES_H */
