/*
 * px_types.h — structures de données du moteur Paradox
 *
 * Hiérarchie de données :
 *   World  →  Region[]  →  Province[]  →  Cell[]
 *
 * Extension future : ajouter des champs dans Province/Region/World
 * sans toucher aux signatures des modules (world, render, diplo, eco…).
 */
#ifndef PX_TYPES_H
#define PX_TYPES_H

#include <stdint.h>
#include <stdbool.h>

/* ---- Dimensions -------------------------------------------------------- */
#define PX_W           512
#define PX_H           256
#define PX_N           (PX_W * PX_H)

#define PX_MAX_PROV    160
#define PX_MAX_REG      24
#define PX_RIVER_MAXLEN 768

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
    uint8_t  river;            /* intensité de flux [0..255] */
    bool     lake;

    /* Flags de rendu (précalculés) */
    bool     coast;            /* adjacent à la mer */
    bool     border_prov;      /* sur frontière de province */
    bool     border_reg;       /* sur frontière de région */
    float    shade;            /* hillshading [0..1] */
} Cell;

/* ---- Province (entité politique de base) ------------------------------- */
typedef struct {
    int16_t  seed_x, seed_y;
    int16_t  region;
    int      area;
    Biome    biome_dominant;
    float    lat;              /* latitude moyenne [0=éq., 1=pôle] */
    float    height_avg;

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
    int16_t  province_ids[PX_MAX_PROV]; /* indices dans World.provinces */
    uint32_t color;
    char     name[32];
} Region;

/* ---- Rivière tracée ---------------------------------------------------- */
typedef struct {
    int16_t x[PX_RIVER_MAXLEN];
    int16_t y[PX_RIVER_MAXLEN];
    int     len;
    float   flow_max;
} River;

/* ---- Monde -------------------------------------------------------------- */
#define PX_MAX_RIVERS 64

typedef struct {
    Cell     cell[PX_N];
    Province province[PX_MAX_PROV];
    int      n_provinces;
    Region   region[PX_MAX_REG];
    int      n_regions;
    River    river[PX_MAX_RIVERS];
    int      n_rivers;
    uint32_t seed;
} World;

/* ---- Accesseur sûr aux cellules --------------------------------------- */
static inline Cell *px_cell(World *w, int x, int y) {
    if (x < 0) x = 0; else if (x >= PX_W) x = PX_W-1;
    if (y < 0) y = 0; else if (y >= PX_H) y = PX_H-1;
    return &w->cell[y * PX_W + x];
}
static inline const Cell *px_cellc(const World *w, int x, int y) {
    if (x < 0) x = 0; else if (x >= PX_W) x = PX_W-1;
    if (y < 0) y = 0; else if (y >= PX_H) y = PX_H-1;
    return &w->cell[y * PX_W + x];
}
static inline int px_idx(int x, int y) { return y * PX_W + x; }

#endif /* PX_TYPES_H */
