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

/* ── LA MER (brief mer §4) : le mouvement directionnel sur le champ de courants ──
 * coût(tuile, direction) = base / (1 + k·max(0, v̂·d̂)) × (1 + m·max(0, −v̂·d̂)) ;
 * eaux mortes ×P ; cabotage = constante (sûr, lent, indifférent aux courants).
 * Conséquence à NE PAS rater : l'aller ≠ le retour (la volta émerge du champ). */

/* Jours de mer entre deux cellules MARINES (Dijkstra directionnel, 8 voisins).
 * < 0 si injoignable (bassins séparés / cellule terrestre). */
float world_sea_days(const World *w, int ax, int ay, int bx, int by);

/* Ancre marine d'une RÉGION (l'avant-port) : la cellule de mer adjacente à la
 * côte de la région, la plus proche du germe de sa meilleure province côtière.
 * DÉRIVÉE du monde (cache interne par seed — rien à sérialiser).
 * false si la région n'a aucune côte. */
bool world_region_sea_anchor(const World *w, int region, int *sx, int *sy);

#endif /* SCPS_WORLD_H */
