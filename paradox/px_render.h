/*
 * px_render.h — API de rendu de la carte
 *
 * Point d'extension : ajouter des ViewMode ici, implémenter dans px_render.c.
 * Le renderer ne modifie jamais le World — lecture seule.
 */
#ifndef PX_RENDER_H
#define PX_RENDER_H

#include "px_types.h"

/* ---- Modes de vue ---------------------------------------------------- */
typedef enum {
    VIEW_TERRAIN = 0,   /* Terrain physique (biomes + hillshading) */
    VIEW_POLITICAL,     /* Provinces colorées + terrain en fond */
    VIEW_REGIONS,       /* Régions colorées */
    VIEW_HEIGHT,        /* Altimétrie (niveaux de gris) */
    VIEW_FERTILITY,     /* Potentiel de civilisation (heatmap) */
    VIEW_COUNT
} ViewMode;

extern const char *VIEW_NAMES[VIEW_COUNT];

/* ---- Paramètres de rendu --------------------------------------------- */
typedef struct {
    float    cam_ox, cam_oy;   /* offset de caméra en cellules-monde */
    float    cam_scale;        /* pixels par cellule */
    int      selected_prov;    /* province surlignée (-1=aucune) */
    bool     show_rivers;
    bool     show_borders;
    bool     show_grid;        /* debug : grille des cellules */
} RenderParams;

/* ---- Rendu principal --------------------------------------------------
 * Remplit pixels[pw×ph] (format ARGB8888) avec la carte rendue selon
 * le mode demandé.  Thread-safe (lecture seule sur World).
 */
void render_map(const World      *w,
                uint32_t         *pixels,
                int               pw, int ph,
                const RenderParams *p,
                ViewMode          mode);

#endif /* PX_RENDER_H */
