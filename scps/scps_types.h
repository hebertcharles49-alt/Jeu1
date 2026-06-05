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

#define SCPS_MAX_PROV    160
#define SCPS_MAX_REG      24
#define SCPS_RIVER_MAXLEN 768

/* ---- Seuils de hauteur (0..1) ----------------------------------------- */
#define SEA_LEVEL     0.43f
#define MOUNTAIN_H    0.67f
#define PEAK_H        0.82f

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
    BIO_COUNT
} Biome;

/* ---- Cellule de carte (données géographiques brutes) ------------------- */
typedef struct {
    /* Couches de génération */
    float    height;
    float    moisture;
    float    temperature;
    float    fertility;        /* potentiel de civilisation [0..1] */

    /* Classification */
    Biome    biome;
    int16_t  province;         /* -1 = mer / non assigné */
    int16_t  region;           /* -1 = mer / non assigné */

    /* Hydrologie */
    uint8_t  river;            /* débit accumulé en aval [0..255] */
    int8_t   flow_dir;         /* direction D8 vers l'aval (-1 = exutoire) */
    bool     lake;

    /* Géographie dérivée */
    float    ocean_dist;       /* continentalité [0=côte .. 1=intérieur profond] */
    float    rainfall;         /* précipitation simulée par advection [0..1] */

    /* Flags de rendu (précalculés) */
    bool     coast;            /* adjacent à la mer */
    bool     border_prov;      /* sur frontière de province */
    bool     border_reg;       /* sur frontière de région */
    float    shade;            /* hillshading [0..1] */
} Cell;

/* ---- Ressources / biens commerciaux (style EU4) ----------------------- */
typedef enum {
    RES_NONE = 0,
    /* Agricole & élevage */
    RES_GRAIN, RES_LIVESTOCK, RES_WOOL, RES_WINE, RES_FISH,
    /* Forêt & froid */
    RES_FUR, RES_NAVAL_SUPPLIES,
    /* Minéral (montagnes / collines) */
    RES_SALT, RES_COPPER, RES_IRON, RES_COAL, RES_GEMS, RES_GOLD,
    /* Tropical & colonial */
    RES_IVORY, RES_SLAVES, RES_SPICES, RES_TEA, RES_COCOA, RES_COFFEE,
    RES_COTTON, RES_SUGAR, RES_TOBACCO, RES_DYES, RES_SILK,
    RES_TROPICAL_WOOD, RES_INCENSE, RES_CLOVES,
    /* Manufacturé (centres urbains / carrefours) */
    RES_CLOTH, RES_CHINAWARE, RES_GLASS, RES_PAPER,
    RES_COUNT
} Resource;

/* ---- Province (entité politique de base) ------------------------------- */
typedef struct {
    int16_t  seed_x, seed_y;
    int16_t  region;
    int      area;
    Biome    biome_dominant;
    float    lat;              /* latitude moyenne [0=éq., 1=pôle] */
    float    height_avg;
    bool     coastal;          /* touche la mer */

    /* Économie */
    Resource resource;         /* bien commercial principal */

    /* Fiche SCPS — axes [0..10] (doc §2.2) */
    float    langue;           /* horloge phylogénétique */
    float    parente;
    float    religion;
    float    subsistance;      /* ancrée sur le biome dominant */
    float    valeurs;

    /* Rendu */
    uint32_t color;            /* ARGB, précalculé à la génération */
    char     name[24];         /* stub — sera enrichi plus tard */
} Province;

/* ---- Région (groupement de provinces, futur niveau politique) ---------- */
typedef struct {
    int      seed_x, seed_y;
    int      n_provinces;
    int16_t  province_ids[SCPS_MAX_PROV]; /* indices dans World.provinces */
    uint32_t color;
    char     name[32];
} Region;

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
    Cell     cell[SCPS_N];
    Province province[SCPS_MAX_PROV];
    int      n_provinces;
    Region   region[SCPS_MAX_REG];
    int      n_regions;
    River    river[SCPS_MAX_RIVERS];
    int      n_rivers;
    uint32_t seed;
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
