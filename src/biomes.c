/*
 * biomes.c - thematique d'etage : 5 biomes qui couvrent les 10 etages,
 * chacun avec un element signature, un tint colore applique au terrain,
 * une couleur d ambient particles et une preference de spawn d ennemi.
 *
 * Floor mapping :
 *   1-2  : Crypte     (DARK)
 *   3-4  : Cavernes   (EARTH)
 *   5-6  : Marais     (WATER)
 *   7-8  : Forge      (FIRE)
 *   9-10 : Sanctuaire (HOLY)
 *
 * Cf game.h pour les declarations publiques.
 */
#include "game.h"

typedef struct {
    const char *name;
    Element     element;
    /* multiplicateurs sur la couleur des tiles (1.0 = neutre) */
    float       tint_r, tint_g, tint_b;
    /* couleur RGBA des particules d ambient flottantes */
    uint32_t    ambient_color;
    /* probabilite par frame (sur 1000) qu une particule ambient soit
     * spawnee dans la salle courante */
    int         ambient_chance_p1000;
    /* enemy kind aligne avec le biome (cf EnemyKind) */
    int         aligned_kind;
} BiomeDef;

#define BIOME_COUNT 5

static const BiomeDef BIOMES[BIOME_COUNT] = {
    /* Crypte : grise / violette froide, cendre fine qui flotte */
    { "Crypte",     EL_DARK,
      0.75f, 0.70f, 0.85f,
      0x80708080, 4,
      EK_GHOST },
    /* Cavernes : brun chaud, poussiere ocre */
    { "Cavernes",   EL_EARTH,
      1.10f, 0.85f, 0.55f,
      0xC09050A0, 6,
      EK_CHARGER },
    /* Marais : vert-bleu sombre, spores vertes */
    { "Marais",     EL_WATER,
      0.65f, 0.95f, 0.75f,
      0x80C080A0, 6,
      EK_SLIME },
    /* Forge : rouge / orange, embers tres visibles */
    { "Forge",      EL_FIRE,
      1.20f, 0.65f, 0.40f,
      0xFF8030B0, 9,
      EK_DEMON },
    /* Sanctuaire : dore / blanc, motes de lumiere */
    { "Sanctuaire", EL_HOLY,
      1.20f, 1.10f, 0.80f,
      0xFFE890A0, 5,
      EK_MAGE },
};

int biome_for_floor(int floor_index) {
    if (floor_index <= 0) return 0;
    int b = (floor_index - 1) / 2;
    if (b >= BIOME_COUNT) b = BIOME_COUNT - 1;
    return b;
}

const char *biome_name(int biome_id) {
    if (biome_id < 0 || biome_id >= BIOME_COUNT) return "?";
    return BIOMES[biome_id].name;
}

Element biome_element(int biome_id) {
    if (biome_id < 0 || biome_id >= BIOME_COUNT) return EL_NONE;
    return BIOMES[biome_id].element;
}

void biome_tint(int biome_id, float *r, float *g, float *b) {
    if (biome_id < 0 || biome_id >= BIOME_COUNT) {
        if (r) *r = 1.f;
        if (g) *g = 1.f;
        if (b) *b = 1.f;
        return;
    }
    if (r) *r = BIOMES[biome_id].tint_r;
    if (g) *g = BIOMES[biome_id].tint_g;
    if (b) *b = BIOMES[biome_id].tint_b;
}

uint32_t biome_ambient_color(int biome_id) {
    if (biome_id < 0 || biome_id >= BIOME_COUNT) return 0x80808060;
    return BIOMES[biome_id].ambient_color;
}

int biome_ambient_chance_p1000(int biome_id) {
    if (biome_id < 0 || biome_id >= BIOME_COUNT) return 3;
    return BIOMES[biome_id].ambient_chance_p1000;
}

int biome_aligned_kind(int biome_id) {
    if (biome_id < 0 || biome_id >= BIOME_COUNT) return -1;
    return BIOMES[biome_id].aligned_kind;
}
