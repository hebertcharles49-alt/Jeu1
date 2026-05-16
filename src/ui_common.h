/*
 * ui_common.h - helpers UI 2D partages entre les fichiers render_*.c
 *
 * Les ecrans (HUD, menus, inventaire, codex, ...) sont decoupes en fichiers
 * distincts pour eviter le god-file render.c. Ils partagent ce petit jeu
 * de helpers (rectangles plein/contour, font 5x7 deja exposee via game.h).
 *
 * find_glyph et la table FONT[] restent privees a ui_common.c -- elles ne
 * sont consommees que par text_draw, qui est l'API publique exposee dans
 * game.h.
 */
#ifndef UI_COMMON_H
#define UI_COMMON_H
#include "game.h"
#include "gfx.h"

void fill_rect   (GfxCtx *r, int x, int y, int w, int h, uint32_t c);
void rect_outline(GfxCtx *r, int x, int y, int w, int h, uint32_t c);
void draw_disk   (GfxCtx *r, int cx, int cy, int radius, uint32_t c);
void draw_ring   (GfxCtx *r, int cx, int cy, int radius, uint32_t c);

/* Vignette plein-ecran (assombrit les bords) -- consommee par render_world
 * et le HUD pour la teinte sur faibles PV. */
void draw_vignette(Game *g);

/* Projection monde -> ecran. Utilisee par render_world_overlay_ui (barre
 * de PV des ennemis, dmgnums) et par d autres overlays. */
bool world_to_screen(GfxCtx *gc, v3 world, int *out_sx, int *out_sy);

#endif
