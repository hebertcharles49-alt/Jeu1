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

/* Réglages par défaut (monde « standard ») pour une graine donnée. */
WorldParams worldparams_default(uint32_t seed);

/* Génère un monde complet selon les paramètres. Bloquant, ~200ms. */
void world_generate(World *w, const WorldParams *params);

/* Utilitaires biome */
uint32_t biome_base_color(Biome b);
const char *biome_name(Biome b);

/* Utilitaires ressource */
const char *resource_name(Resource r);
uint32_t    resource_color(Resource r);

/* Palette de provinces — couleur ARGB stable par id */
uint32_t province_palette(int id);

#endif /* SCPS_WORLD_H */
