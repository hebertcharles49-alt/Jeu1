/*
 * paradox/worldgen.h
 * Moteur de génération de monde procédural — pipeline 4 couches :
 *   1. Géologie   : plaques tectoniques → relief de base
 *   2. Architecture : détail de relief, crêtes, vallées
 *   3. Érosion    : hydraulique, direction de flux, rivières
 *   4. Civilisation : potentiel de peuplement
 *
 * Puis biomes, lacs, provinces Voronoï, fiche SCPS par province.
 */
#ifndef PARADOX_WORLDGEN_H
#define PARADOX_WORLDGEN_H

#include <stdint.h>
#include <stdbool.h>

/* Dimensions de la carte */
#define PX_W 512
#define PX_H 256
#define PX_N (PX_W * PX_H)

#define PX_MAX_PROVINCES 160
#define PX_MAX_RIVERS     64
#define PX_RIVER_MAXLEN  768

#define SEA_LEVEL   0.43f
#define MOUNTAIN_H  0.67f
#define PEAK_H      0.82f

/* ---- Biomes (doc §3, §6) ---------------------------------------------- */
typedef enum {
    BIOME_DEEP_OCEAN = 0,
    BIOME_OCEAN,
    BIOME_SHALLOW,
    BIOME_COAST,        /* plage, littoral */
    BIOME_PLAINS,
    BIOME_FARMLAND,
    BIOME_GRASSLAND,
    BIOME_STEPPE,
    BIOME_SAVANNA,
    BIOME_DRYLANDS,
    BIOME_DESERT,
    BIOME_COASTAL_DESERT,
    BIOME_FOREST,
    BIOME_WOODS,
    BIOME_JUNGLE,
    BIOME_MARSH,
    BIOME_HIGHLANDS,
    BIOME_HILLS,
    BIOME_MOUNTAINS,
    BIOME_PEAK,
    BIOME_GLACIER,
    BIOME_COUNT
} Biome;

/* ---- Cellule de carte -------------------------------------------------- */
typedef struct {
    float   height;       /* 0..1, niveau mer = SEA_LEVEL */
    float   moisture;     /* 0..1 */
    float   temperature;  /* 0..1 (froid..chaud) */
    float   civilization; /* 0..1, potentiel de peuplement */
    Biome   biome;
    int16_t province;     /* -1 = non assignée */
    uint8_t river;        /* intensité du flux 0..255 */
    bool    lake;
    int8_t  flow_dir;     /* 0..7 voisinage 8-connexe, -1=aucun */
} Cell;

/* ---- Province (= État potentiel, avec fiche SCPS) --------------------- */
typedef struct {
    int   seed_x, seed_y;   /* cellule germe */
    Biome dominant;
    int   area;             /* nombre de cellules */
    float lat;              /* 0=équateur, 1=pôle */
    /* Fiche SCPS — axes 0..10 */
    float langue;
    float parente;
    float religion;
    float subsistance;
    float valeurs;
} Province;

/* ---- Rivière ----------------------------------------------------------- */
typedef struct {
    int16_t x[PX_RIVER_MAXLEN];
    int16_t y[PX_RIVER_MAXLEN];
    int     len;
    float   flow_max;
} River;

/* ---- Monde généré ------------------------------------------------------ */
typedef struct {
    Cell     cell[PX_N];
    Province province[PX_MAX_PROVINCES];
    int      n_provinces;
    River    river[PX_MAX_RIVERS];
    int      n_rivers;
    uint32_t seed;
} World;

/* ---- Helpers inline ---------------------------------------------------- */
static inline int px_idx(int x, int y) { return y * PX_W + x; }

/* ---- API publique ------------------------------------------------------- */
void     world_generate(World *w, uint32_t seed);

/* Couleur RGBA (0xAARRGGBB) selon biome */
uint32_t biome_color(Biome b, bool river, bool lake);
/* Couleur différente par province (palette stable) */
uint32_t province_color(int id);
/* Nom lisible du biome */
const char *biome_name(Biome b);

#endif /* PARADOX_WORLDGEN_H */
