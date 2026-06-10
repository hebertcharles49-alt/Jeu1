#ifndef SCPS_DEV_OVERLAY_H
#define SCPS_DEV_OVERLAY_H
/*
 * dev_overlay.h — L'OVERLAY DE DEV (brief build §6) — JAMAIS dans le release.
 *
 * Compilé UNIQUEMENT sous -DSCPS_DEV (cible `make dev`). Togglé F3 en jeu :
 * inspecteur des COORDONNÉES BRUTES (les flottants que la membrane cache au
 * joueur — charge, fracture, coercition, K/H/P bâtis, prospérité…). L'ingénieur
 * voit les nombres ; le joueur, jamais (ce fichier n'existe pas dans le binaire
 * release : `strings scps_viewer | grep -i nuklear` = vide).
 *
 * Ne tire PAS scps_core.h (la cloison du viewer tient) : il lit les flottants
 * DÉJÀ stockés dans econ/tech/diplo, pas le cœur §2.
 */
#ifdef SCPS_DEV
#include <SDL2/SDL.h>
#include "scps_world.h"
#include "scps_econ.h"
#include "scps_tech.h"
#include "scps_diplo.h"

void dev_overlay_init(SDL_Window *win, SDL_Renderer *ren);
void dev_overlay_shutdown(void);
/* Entoure le sondage d'évènements (Nuklear collecte ses entrées par frame). */
void dev_overlay_input_begin(void);
void dev_overlay_input_end(void);
/* Renvoie 1 si Nuklear a CONSOMMÉ l'évènement (le viewer l'ignore alors). */
int  dev_overlay_handle_event(SDL_Event *e);
/* Construit + rend l'overlay (à appeler APRÈS le rendu du jeu, AVANT present).
 * sel_country / sel_prov = -1 si rien de sélectionné. */
void dev_overlay_draw(const World *w, const WorldEconomy *econ,
                      const TechState *ts, const DiploState *dp,
                      int sel_country, int sel_prov);
#endif /* SCPS_DEV */

#endif /* SCPS_DEV_OVERLAY_H */
