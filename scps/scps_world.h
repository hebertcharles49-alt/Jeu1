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
#include "scps_econ.h"   /* gen_population/world_tick écrivent RegionEconomy.culture */

/* Réglages par défaut (monde « standard ») pour une graine donnée. */
WorldParams worldparams_default(uint32_t seed);

/* Génère un monde complet (géographie seule) selon les paramètres. ~200ms. */
void world_generate(World *w, const WorldParams *params);

/* Intensité agricole [0..10] d'un biome — source unique de vérité partagée
 * avec l'axe de subsistance culturel (cf. lifeway_subs). Sert de proxy de
 * capacité d'accueil dans econ_init. */
float subsistance_for_biome(Biome b);

/* Peuple la culture de chaque région (PopCulture) à partir du biome dominant,
 * de l'éthos tiré, de la latitude (branche religieuse) et des proto-familles
 * linguistiques ancrées sur les continents. À appeler APRÈS econ_init (les
 * régions doivent exister). */
void gen_population(World *w, WorldEconomy *econ);

/* Avance les processus lents liés au monde+population d'un pas dt : pour
 * l'instant la dérive de l'horloge linguistique des régions peuplées. */
void world_tick(World *w, WorldEconomy *econ, float dt);

/* Assigne une race à chaque pays en GRADIENT autour du joueur (distance de
 * sphère ~ distance géographique) ; cités-états = isolats exotiques. Pose la
 * race sur RegionEconomy.culture.race. À appeler après gen_population. */
void worldgen_seed_peoples(World *w, WorldEconomy *econ, SpeciesArchetype player_race);

/* Utilitaires biome */
uint32_t biome_base_color(Biome b);
const char *biome_name(Biome b);

/* Utilitaires ressource */
const char *resource_name(Resource r);
uint32_t    resource_color(Resource r);

/* Palette de provinces — couleur ARGB stable par id */
uint32_t province_palette(int id);

#endif /* SCPS_WORLD_H */
