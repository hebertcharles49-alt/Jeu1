/*
 * px_world.h — API de génération de monde
 *
 * Point d'extension : pour ajouter une couche de génération, implémenter
 * une fonction `step_XXX(World*)` dans px_world.c et l'appeler dans
 * world_generate() entre les étapes existantes.
 */
#ifndef PX_WORLD_H
#define PX_WORLD_H

#include "px_types.h"

/* Génère un monde complet depuis une graine. Bloquant, ~200ms. */
void world_generate(World *w, uint32_t seed);

/* Utilitaires biome */
uint32_t biome_base_color(Biome b);
const char *biome_name(Biome b);

/* Palette de provinces — couleur ARGB stable par id */
uint32_t province_palette(int id);

#endif /* PX_WORLD_H */
