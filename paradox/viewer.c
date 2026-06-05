/*
 * paradox/viewer.c
 * Visualiseur SDL2 du monde généré.
 *
 * Contrôles :
 *   TAB        — cycle des modes de vue
 *   R          — regénère avec une nouvelle graine
 *   Flèches    — déplace la carte
 *   +/-        — zoom
 *   P          — affiche infos province sous la souris
 *   ESC/Q      — quitte
 *
 * Modes de vue :
 *   0 Terrain (biomes colorés)
 *   1 Hauteur (niveaux de gris)
 *   2 Humidité
 *   3 Température
 *   4 Civilisation (heatmap)
 *   5 Provinces (Voronoï coloré + frontières)
 */
#include <SDL.h>
#include "worldgen.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <math.h>

/* ---- Fenêtre ---------------------------------------------------------- */
#define WIN_W 1024
#define WIN_H 512

typedef enum {
    VIEW_TERRAIN = 0,
    VIEW_HEIGHT,
    VIEW_MOISTURE,
    VIEW_TEMPERATURE,
    VIEW_CIVILIZATION,
    VIEW_PROVINCES,
    VIEW_COUNT
} ViewMode;

static const char *VIEW_NAMES[VIEW_COUNT] = {
    "Terrain", "Hauteur", "Humidite", "Temperature", "Civilisation", "Provinces"
};

/* ---- État de la caméra ------------------------------------------------ */
typedef struct {
    float ox, oy;   /* offset (cellules) */
    float scale;    /* pixels par cellule */
} Camera;

static Camera g_cam = { 0.f, 0.f, 2.f };

static void cam_world_to_screen(Camera *c, float wx, float wy, float *sx, float *sy) {
    *sx = (wx - c->ox) * c->scale;
    *sy = (wy - c->oy) * c->scale;
}
static void cam_screen_to_world(Camera *c, float sx, float sy, float *wx, float *wy) {
    *wx = sx / c->scale + c->ox;
    *wy = sy / c->scale + c->oy;
}

/* ---- Palette heatmap 0..1 → ARGB -------------------------------------- */
static uint32_t heatmap(float v) {
    v = v < 0.f ? 0.f : v > 1.f ? 1.f : v;
    /* bleu → cyan → vert → jaune → rouge */
    float r, g, b;
    if      (v < 0.25f) { float t=v/0.25f; r=0.f;      g=t;        b=1.f;      }
    else if (v < 0.50f) { float t=(v-0.25f)/0.25f; r=0.f; g=1.f; b=1.f-t; }
    else if (v < 0.75f) { float t=(v-0.50f)/0.25f; r=t;   g=1.f; b=0.f;   }
    else                { float t=(v-0.75f)/0.25f; r=1.f; g=1.f-t; b=0.f; }
    return 0xFF000000u
         | ((uint32_t)(r*255.f) << 16)
         | ((uint32_t)(g*255.f) <<  8)
         | ((uint32_t)(b*255.f));
}

static uint32_t grayscale(float v) {
    uint8_t c = (uint8_t)(v * 255.f);
    return 0xFF000000u | ((uint32_t)c<<16) | ((uint32_t)c<<8) | c;
}

/* ---- Construction du pixel buffer ------------------------------------- */
static void build_pixels(World *w, ViewMode mode, uint32_t *pix,
                          int tex_w, int tex_h, Camera *cam) {
    for (int sy = 0; sy < tex_h; sy++) {
        for (int sx = 0; sx < tex_w; sx++) {
            float wx, wy;
            cam_screen_to_world(cam, (float)sx, (float)sy, &wx, &wy);
            int cx = (int)wx, cy = (int)wy;
            uint32_t col = 0xFF101010u;

            if (cx >= 0 && cx < PX_W && cy >= 0 && cy < PX_H) {
                Cell *cell = &w->cell[cx + cy * PX_W];

                switch (mode) {
                case VIEW_TERRAIN:
                    col = biome_color(cell->biome,
                                      cell->river > 80 && cell->height > SEA_LEVEL,
                                      cell->lake);
                    /* Rivières : teinte bleue proportionnelle au flux */
                    if (!cell->lake && cell->river > 80 && cell->height > SEA_LEVEL) {
                        float rs = cell->river / 255.f;
                        uint32_t bc = biome_color(cell->biome, false, false);
                        uint8_t r2 = (uint8_t)(((bc>>16)&0xFF)*(1-rs*0.6f) + 0x50*rs*0.6f);
                        uint8_t g2 = (uint8_t)(((bc>> 8)&0xFF)*(1-rs*0.6f) + 0x90*rs*0.6f);
                        uint8_t b2 = (uint8_t)(((bc    )&0xFF)*(1-rs*0.6f) + 0xD8*rs*0.6f);
                        col = 0xFF000000u|(r2<<16)|(g2<<8)|b2;
                    }
                    break;
                case VIEW_HEIGHT:
                    col = grayscale(cell->height);
                    break;
                case VIEW_MOISTURE:
                    col = heatmap(cell->moisture);
                    break;
                case VIEW_TEMPERATURE:
                    col = heatmap(cell->temperature);
                    break;
                case VIEW_CIVILIZATION:
                    col = (cell->height < SEA_LEVEL) ? 0xFF0F1E2Du
                         : heatmap(cell->civilization);
                    break;
                case VIEW_PROVINCES:
                    if (cell->province >= 0) {
                        uint32_t pc = province_color(cell->province);
                        /* Teinter légèrement avec le biome de fond */
                        uint32_t bc = biome_color(cell->biome, false, false);
                        uint8_t r2 = (uint8_t)((((pc>>16)&0xFF)*2 + ((bc>>16)&0xFF)) / 3);
                        uint8_t g2 = (uint8_t)((((pc>> 8)&0xFF)*2 + ((bc>> 8)&0xFF)) / 3);
                        uint8_t b2 = (uint8_t)((((pc    )&0xFF)*2 + ((bc    )&0xFF)) / 3);
                        col = 0xFF000000u|(r2<<16)|(g2<<8)|b2;
                    } else {
                        col = biome_color(cell->biome, false, false);
                    }
                    break;
                default: break;
                }
            }
            pix[sy * tex_w + sx] = col;
        }
    }
}

/* ---- Rendu des frontières de province (mode PROVINCES) ---------------- */
static void draw_province_borders(SDL_Renderer *ren, World *w, Camera *cam) {
    SDL_SetRenderDrawColor(ren, 20, 15, 10, 220);
    for (int y = 0; y < PX_H - 1; y++) {
        for (int x = 0; x < PX_W - 1; x++) {
            int p  = w->cell[px_idx(x,y)].province;
            int pr = w->cell[px_idx(x+1,y)].province;
            int pb = w->cell[px_idx(x,y+1)].province;
            if ((p != pr && (p >= 0 || pr >= 0)) ||
                (p != pb && (p >= 0 || pb >= 0))) {
                float sx, sy;
                cam_world_to_screen(cam, (float)x+0.5f, (float)y+0.5f, &sx, &sy);
                SDL_RenderDrawPoint(ren, (int)sx, (int)sy);
            }
        }
    }
}

/* ---- Rendu des rivières tracées --------------------------------------- */
static void draw_rivers(SDL_Renderer *ren, World *w, Camera *cam) {
    for (int r = 0; r < w->n_rivers; r++) {
        River *rv = &w->river[r];
        if (rv->len < 2) continue;
        /* Épaisseur proportionnelle au débit */
        int thickness = (int)(rv->flow_max * 3.f) + 1;
        uint8_t alpha = (uint8_t)(180 + rv->flow_max * 75.f);
        SDL_SetRenderDrawColor(ren, 40, 110, 200, alpha);

        for (int s = 1; s < rv->len; s++) {
            float x1, y1, x2, y2;
            cam_world_to_screen(cam, rv->x[s-1]+0.5f, rv->y[s-1]+0.5f, &x1, &y1);
            cam_world_to_screen(cam, rv->x[s  ]+0.5f, rv->y[s  ]+0.5f, &x2, &y2);

            for (int t = -thickness/2; t <= thickness/2; t++) {
                SDL_RenderDrawLine(ren,
                    (int)x1+t, (int)y1,
                    (int)x2+t, (int)y2);
                SDL_RenderDrawLine(ren,
                    (int)x1, (int)y1+t,
                    (int)x2, (int)y2+t);
            }
        }
    }
}

/* ---- Overlay texte minimal (sans SDL_ttf) — barre de status ASCII ----- */
static void draw_status(SDL_Renderer *ren, ViewMode mode, uint32_t seed,
                        int mouse_cx, int mouse_cy, World *w) {
    (void)ren;
    /* On ne peut pas dessiner du texte sans SDL_ttf, on log en console */
    printf("\r[%s] graine=%u  ", VIEW_NAMES[mode], seed);
    if (mouse_cx >= 0 && mouse_cx < PX_W && mouse_cy >= 0 && mouse_cy < PX_H) {
        Cell *c = &w->cell[mouse_cx + mouse_cy * PX_W];
        printf("(%3d,%3d) %s  h=%.2f  m=%.2f  t=%.2f  civ=%.2f  prov=%d",
               mouse_cx, mouse_cy,
               biome_name(c->biome),
               c->height, c->moisture, c->temperature,
               c->civilization, c->province);
        if (c->province >= 0) {
            Province *pr = &w->province[c->province];
            printf("  L=%.1f P=%.1f R=%.1f S=%.1f V=%.1f",
                   pr->langue, pr->parente, pr->religion,
                   pr->subsistance, pr->valeurs);
        }
    }
    fflush(stdout);
}

/* ======================================================================== */

int main(int argc, char **argv) {
    (void)argc; (void)argv;

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window   *win = SDL_CreateWindow(
        "Paradox — Carte procédurale",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        WIN_W, WIN_H, SDL_WINDOW_RESIZABLE);
    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
    if (!win || !ren) {
        fprintf(stderr, "SDL: %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);

    int tex_w = WIN_W, tex_h = WIN_H;
    SDL_Texture *tex = SDL_CreateTexture(ren,
        SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, tex_w, tex_h);

    uint32_t *pixels = (uint32_t*)malloc(tex_w * tex_h * 4);
    World    *world  = (World*)malloc(sizeof(World));
    if (!pixels || !world) { fprintf(stderr,"malloc\n"); return 1; }

    uint32_t  seed    = (uint32_t)time(NULL);
    ViewMode  mode    = VIEW_TERRAIN;
    bool      dirty   = true;
    bool      running = true;
    int       win_w   = WIN_W, win_h = WIN_H;

    /* Centre la carte dans la fenêtre au démarrage */
    g_cam.scale = (float)WIN_W / PX_W;
    g_cam.ox = 0.f;
    g_cam.oy = 0.f;

    printf("[paradox] Génération du monde (graine %u)…\n", seed);
    world_generate(world, seed);
    printf("[paradox] Prêt.\n");
    printf("Touches : TAB=vue  R=regénère  Flèches=déplace  +/-=zoom  ESC=quitte\n");

    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            switch (ev.type) {
            case SDL_QUIT:
                running = false;
                break;
            case SDL_WINDOWEVENT:
                if (ev.window.event == SDL_WINDOWEVENT_RESIZED) {
                    win_w = ev.window.data1;
                    win_h = ev.window.data2;
                    SDL_DestroyTexture(tex);
                    free(pixels);
                    tex_w = win_w; tex_h = win_h;
                    tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888,
                                            SDL_TEXTUREACCESS_STREAMING, tex_w, tex_h);
                    pixels = (uint32_t*)malloc(tex_w * tex_h * 4);
                    dirty = true;
                }
                break;
            case SDL_KEYDOWN:
                switch (ev.key.keysym.sym) {
                case SDLK_ESCAPE:
                case SDLK_q:
                    running = false;
                    break;
                case SDLK_TAB:
                    mode = (ViewMode)((mode + 1) % VIEW_COUNT);
                    dirty = true;
                    printf("\n");
                    break;
                case SDLK_r:
                    seed = (uint32_t)time(NULL) ^ (seed * 6364136223846793005ull + 1442695040888963407ull);
                    printf("\n[paradox] Nouvelle graine %u…\n", seed);
                    world_generate(world, seed);
                    dirty = true;
                    break;
                case SDLK_LEFT:
                    g_cam.ox -= 16.f / g_cam.scale; dirty = true; break;
                case SDLK_RIGHT:
                    g_cam.ox += 16.f / g_cam.scale; dirty = true; break;
                case SDLK_UP:
                    g_cam.oy -= 16.f / g_cam.scale; dirty = true; break;
                case SDLK_DOWN:
                    g_cam.oy += 16.f / g_cam.scale; dirty = true; break;
                case SDLK_PLUS:
                case SDLK_EQUALS:
                    g_cam.scale *= 1.25f; dirty = true; break;
                case SDLK_MINUS:
                    g_cam.scale /= 1.25f;
                    if (g_cam.scale < 0.25f) g_cam.scale = 0.25f;
                    dirty = true;
                    break;
                default: break;
                }
                break;
            default: break;
            }
        }

        /* Mise à jour du pixel buffer si nécessaire */
        if (dirty) {
            build_pixels(world, mode, pixels, tex_w, tex_h, &g_cam);
            SDL_UpdateTexture(tex, NULL, pixels, tex_w * 4);
            dirty = false;
        }

        SDL_RenderClear(ren);
        SDL_RenderCopy(ren, tex, NULL, NULL);

        /* Rivières par-dessus (tracé vectoriel) */
        draw_rivers(ren, world, &g_cam);

        /* Frontières de province en mode PROVINCES */
        if (mode == VIEW_PROVINCES)
            draw_province_borders(ren, world, &g_cam);

        SDL_RenderPresent(ren);

        /* Infos sous la souris */
        int mx, my;
        SDL_GetMouseState(&mx, &my);
        float wx, wy;
        cam_screen_to_world(&g_cam, (float)mx, (float)my, &wx, &wy);
        draw_status(ren, mode, seed, (int)wx, (int)wy, world);

        SDL_Delay(16);
    }

    printf("\n");
    free(pixels);
    free(world);
    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
