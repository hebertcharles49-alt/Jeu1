/*
 * scps_render.h — API de rendu de la carte
 *
 * Point d'extension : ajouter des ViewMode ici, implémenter dans scps_render.c.
 * Le renderer ne modifie jamais le World — lecture seule.
 */
#ifndef SCPS_RENDER_H
#define SCPS_RENDER_H

#include "scps_types.h"

/* ---- Modes de vue ---------------------------------------------------- */
typedef enum {
    VIEW_TERRAIN = 0,   /* Terrain physique (biomes + hillshading) */
    VIEW_POLITICAL,     /* Territoires (provinces) colorés + terrain */
    VIEW_REGIONS,       /* Régions colorées */
    VIEW_COUNTRIES,     /* Pays colorés */
    VIEW_CONTINENTS,    /* Continents (masses géographiques) */
    VIEW_HEIGHT,        /* Altimétrie (niveaux de gris) */
    VIEW_FERTILITY,     /* Potentiel de civilisation (heatmap) */
    VIEW_MOISTURE,      /* Précipitations / humidité (bleu=humide) */
    VIEW_TEMPERATURE,   /* Température (rouge=chaud) */
    VIEW_RESOURCES,     /* Bien commercial par province */
    VIEW_HABITABILITY,  /* Habitabilité : rouge=mort, jaune=marginal, vert=fertile */
    VIEW_CULTURE,       /* Culture dominante par région (teinte fournie par l'appelant) */
    VIEW_FAITH,         /* Foi dominante par région (teinte fournie par l'appelant) */
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
    /* Teinte PAR RÉGION (ARGB), fournie par l'appelant pour VIEW_CULTURE/VIEW_FAITH
     * (la membrane : le viewer calcule les couleurs diégétiques depuis l'éco et les
     * passe ; le renderer ne lit aucun flottant SCPS). NULL = pas de teinte. */
    const uint32_t *region_tint;
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

#endif /* SCPS_RENDER_H */
