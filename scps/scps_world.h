/*
 * scps_world.h — API de génération de monde
 *
 * Point d'extension : pour ajouter une couche de génération, implémenter
 * une fonction `step_XXX(World*)` dans scps_world.c et l'appeler dans
 * world_generate() entre les étapes existantes.
 */
#ifndef SCPS_WORLD_H
#define SCPS_WORLD_H

#include "scps_types.h"

/* Génère un monde complet depuis une graine. Bloquant, ~200ms. */
void world_generate(World *w, uint32_t seed);

/* Utilitaires biome */
uint32_t biome_base_color(Biome b);
const char *biome_name(Biome b);

/* Utilitaires ressource */
const char *resource_name(Resource r);
uint32_t    resource_color(Resource r);

/* Palette de provinces — couleur ARGB stable par id */
uint32_t province_palette(int id);

#endif /* SCPS_WORLD_H */
