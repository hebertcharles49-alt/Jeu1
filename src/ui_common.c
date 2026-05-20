/*
 * ui_common.c - font 5x7 + helpers UI 2D + projection monde->ecran
 *
 * Extrait de l'ancien render.c. Cf ui_common.h pour la rationale du split.
 */
#include "ui_common.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* ---------- 5x7 BITMAP FONT ---------- */
typedef struct { char c; uint8_t row[7]; } Glyph;
#define G(c, r0,r1,r2,r3,r4,r5,r6) { c, { r0,r1,r2,r3,r4,r5,r6 } }
static const Glyph FONT[] = {
    G(' ',  0,0,0,0,0,0,0),
    G('!',  0x04,0x04,0x04,0x04,0x04,0x00,0x04),
    G('?',  0x0E,0x11,0x10,0x08,0x04,0x00,0x04),
    G('.',  0x00,0x00,0x00,0x00,0x00,0x00,0x04),
    G(',',  0x00,0x00,0x00,0x00,0x00,0x04,0x02),
    G(':',  0x00,0x04,0x00,0x00,0x00,0x04,0x00),
    G('+',  0x00,0x04,0x04,0x1F,0x04,0x04,0x00),
    G('-',  0x00,0x00,0x00,0x1F,0x00,0x00,0x00),
    G('/',  0x10,0x10,0x08,0x04,0x02,0x01,0x01),
    G('(',  0x08,0x04,0x02,0x02,0x02,0x04,0x08),
    G(')',  0x02,0x04,0x08,0x08,0x08,0x04,0x02),
    G('[',  0x0E,0x02,0x02,0x02,0x02,0x02,0x0E),
    G(']',  0x0E,0x08,0x08,0x08,0x08,0x08,0x0E),
    G('%',  0x11,0x10,0x08,0x04,0x02,0x01,0x11),
    G('\'', 0x04,0x04,0x00,0x00,0x00,0x00,0x00),
    G('*',  0x00,0x15,0x0E,0x1F,0x0E,0x15,0x00),
    G('>',  0x00,0x02,0x04,0x08,0x04,0x02,0x00),
    G('<',  0x00,0x08,0x04,0x02,0x04,0x08,0x00),
    G('=',  0x00,0x00,0x1F,0x00,0x1F,0x00,0x00),
    G('0',  0x0E,0x11,0x13,0x15,0x19,0x11,0x0E),
    G('1',  0x04,0x06,0x04,0x04,0x04,0x04,0x0E),
    G('2',  0x0E,0x11,0x10,0x08,0x04,0x02,0x1F),
    G('3',  0x0E,0x11,0x10,0x0E,0x10,0x11,0x0E),
    G('4',  0x08,0x0C,0x0A,0x09,0x1F,0x08,0x08),
    G('5',  0x1F,0x01,0x0F,0x10,0x10,0x11,0x0E),
    G('6',  0x0E,0x11,0x01,0x0F,0x11,0x11,0x0E),
    G('7',  0x1F,0x10,0x08,0x04,0x02,0x02,0x02),
    G('8',  0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E),
    G('9',  0x0E,0x11,0x11,0x1E,0x10,0x11,0x0E),
    G('A',  0x0E,0x11,0x11,0x1F,0x11,0x11,0x11),
    G('B',  0x0F,0x11,0x11,0x0F,0x11,0x11,0x0F),
    G('C',  0x0E,0x11,0x01,0x01,0x01,0x11,0x0E),
    G('D',  0x07,0x09,0x11,0x11,0x11,0x09,0x07),
    G('E',  0x1F,0x01,0x01,0x0F,0x01,0x01,0x1F),
    G('F',  0x1F,0x01,0x01,0x0F,0x01,0x01,0x01),
    G('G',  0x0E,0x11,0x01,0x1D,0x11,0x11,0x0E),
    G('H',  0x11,0x11,0x11,0x1F,0x11,0x11,0x11),
    G('I',  0x0E,0x04,0x04,0x04,0x04,0x04,0x0E),
    G('J',  0x10,0x10,0x10,0x10,0x10,0x11,0x0E),
    G('K',  0x11,0x09,0x05,0x03,0x05,0x09,0x11),
    G('L',  0x01,0x01,0x01,0x01,0x01,0x01,0x1F),
    G('M',  0x11,0x1B,0x15,0x15,0x11,0x11,0x11),
    G('N',  0x11,0x13,0x15,0x19,0x11,0x11,0x11),
    G('O',  0x0E,0x11,0x11,0x11,0x11,0x11,0x0E),
    G('P',  0x0F,0x11,0x11,0x0F,0x01,0x01,0x01),
    G('Q',  0x0E,0x11,0x11,0x11,0x15,0x09,0x16),
    G('R',  0x0F,0x11,0x11,0x0F,0x05,0x09,0x11),
    G('S',  0x1E,0x01,0x01,0x0E,0x10,0x10,0x0F),
    G('T',  0x1F,0x04,0x04,0x04,0x04,0x04,0x04),
    G('U',  0x11,0x11,0x11,0x11,0x11,0x11,0x0E),
    G('V',  0x11,0x11,0x11,0x11,0x11,0x0A,0x04),
    G('W',  0x11,0x11,0x11,0x15,0x15,0x15,0x0A),
    G('X',  0x11,0x11,0x0A,0x04,0x0A,0x11,0x11),
    G('Y',  0x11,0x11,0x11,0x0A,0x04,0x04,0x04),
    G('Z',  0x1F,0x10,0x08,0x04,0x02,0x01,0x1F),
};
#define FONT_COUNT ((int)(sizeof(FONT) / sizeof(FONT[0])))

static const Glyph *find_glyph(char c) {
    if (c >= 'a' && c <= 'z') c = c - 'a' + 'A';
    for (int i = 0; i < FONT_COUNT; i++)
        if (FONT[i].c == c) return &FONT[i];
    return &FONT[0];
}

void text_draw(GfxCtx *r, int x, int y, const char *s, uint32_t col) {
    gfx_set_color(r, col);
    int cx = x;
    for (const char *p = s; *p; p++) {
        if (*p == '\n') { cx = x; y += 8; continue; }
        const Glyph *gph = find_glyph(*p);
        for (int row = 0; row < 7; row++) {
            uint8_t bits = gph->row[row];
            for (int b = 0; b < 5; b++) {
                if (bits & (1 << b)) {
                    gfx_fill_rect(r, cx + b, y + row, 1, 1);
                }
            }
        }
        cx += 6;
    }
}

void text_drawf(GfxCtx *r, int x, int y, uint32_t col, const char *fmt, ...) {
    char buf[256];
    va_list ap; va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    text_draw(r, x, y, buf, col);
}

int text_width(const char *s) { return (int)strlen(s) * 6; }

/* ---------- HELPERS UI 2D ---------- */
void fill_rect(GfxCtx *r, int x, int y, int w, int h, uint32_t c) {
    gfx_set_color(r, c);
    gfx_fill_rect(r, x, y, w, h);
}

void rect_outline(GfxCtx *r, int x, int y, int w, int h, uint32_t c) {
    fill_rect(r, x, y, w, 1, c);
    fill_rect(r, x, y + h - 1, w, 1, c);
    fill_rect(r, x, y, 1, h, c);
    fill_rect(r, x + w - 1, y, 1, h, c);
}

void draw_disk(GfxCtx *r, int cx, int cy, int radius, uint32_t c) {
    gfx_set_color(r, c);
    for (int dy = -radius; dy <= radius; dy++) {
        int dx = (int)sqrtf((float)(radius * radius - dy * dy));
        gfx_fill_rect(r, cx - dx, cy + dy, dx * 2 + 1, 1);
    }
}

void draw_ring(GfxCtx *r, int cx, int cy, int radius, uint32_t c) {
    gfx_set_color(r, c);
    int n = 48;
    for (int i = 0; i < n; i++) {
        float a = (i / (float)n) * 6.2831f;
        int x = cx + (int)(cosf(a) * radius);
        int y = cy + (int)(sinf(a) * radius);
        gfx_fill_rect(r, x, y, 1, 1);
    }
}

/* projette un point monde vers ecran (FBO interne). */
bool world_to_screen(GfxCtx *gc, v3 world, int *out_sx, int *out_sy) {
    float v[4] = { world.x, world.y, world.z, 1.f };
    float vv[4];
    for (int i = 0; i < 4; i++) {
        vv[i] = gc->view.m[i + 0] * v[0] + gc->view.m[i + 4] * v[1] +
                gc->view.m[i + 8] * v[2] + gc->view.m[i + 12]* v[3];
    }
    float p[4];
    for (int i = 0; i < 4; i++) {
        p[i] = gc->proj.m[i + 0] * vv[0] + gc->proj.m[i + 4] * vv[1] +
               gc->proj.m[i + 8] * vv[2] + gc->proj.m[i + 12]* vv[3];
    }
    if (p[3] <= 0.001f) return false;
    float nx = p[0] / p[3];
    float ny = p[1] / p[3];
    *out_sx = (int)((nx * 0.5f + 0.5f) * INTERNAL_W);
    *out_sy = (int)((1.f - (ny * 0.5f + 0.5f)) * INTERNAL_H);
    return true;
}

/* ============================================================
 *   TOAST NOTIFICATIONS
 * Petits messages flottants (decouvertes meta, achats, etc.) qui
 * apparaissent en bas-droite, glissent vers la gauche en fadant.
 * Slot circulaire de taille 4. Le plus ancien est ecrase si plein.
 * ============================================================ */
void toast_push(Game *g, const char *text, uint32_t color, float life) {
    if (!g || !text) return;
    int slot = -1;
    float oldest_life = 1e9f;
    int oldest = 0;
    int n = (int)(sizeof(g->toasts) / sizeof(g->toasts[0]));
    for (int i = 0; i < n; i++) {
        if (g->toasts[i].life <= 0.f) { slot = i; break; }
        if (g->toasts[i].life < oldest_life) {
            oldest_life = g->toasts[i].life;
            oldest = i;
        }
    }
    if (slot < 0) slot = oldest;
    snprintf(g->toasts[slot].text, sizeof(g->toasts[slot].text), "%s", text);
    g->toasts[slot].color    = color;
    g->toasts[slot].life     = life;
    g->toasts[slot].life_max = life;
}

void toast_tick(Game *g) {
    if (!g) return;
    int n = (int)(sizeof(g->toasts) / sizeof(g->toasts[0]));
    for (int i = 0; i < n; i++) {
        if (g->toasts[i].life > 0.f) g->toasts[i].life -= g->dt;
        if (g->toasts[i].life < 0.f) g->toasts[i].life = 0.f;
    }
}

void toast_render(Game *g) {
    if (!g) return;
    /* on rend du plus ancien (en bas) au plus recent (en haut). On range
     * d abord les slots vivants par life decroissante pour l affichage. */
    int order[4]; int n_order = 0;
    int n = (int)(sizeof(g->toasts) / sizeof(g->toasts[0]));
    for (int i = 0; i < n; i++) {
        if (g->toasts[i].life > 0.f) order[n_order++] = i;
    }
    /* tri insertion par life decroissante (plus jeune toast = grand life) */
    for (int i = 1; i < n_order; i++) {
        int k = order[i]; int j = i - 1;
        while (j >= 0 && g->toasts[order[j]].life < g->toasts[k].life) {
            order[j + 1] = order[j]; j--;
        }
        order[j + 1] = k;
    }
    int by = 50;       /* base Y juste sous le badge biome */
    int bx = INTERNAL_W - 4;
    for (int oi = 0; oi < n_order; oi++) {
        int i = order[oi];
        float t = g->toasts[i].life / g->toasts[i].life_max;
        if (t < 0.f) t = 0.f;
        /* slide : entre 4 px (apparition) et 16 px (sortie) */
        int slide = (t > 0.8f) ? (int)((t - 0.8f) * 60.f)
                  : (t < 0.2f ? (int)((0.2f - t) * 60.f) : 0);
        int tw = text_width(g->toasts[i].text);
        int pad = 4;
        int bw = tw + pad * 2;
        int x = bx - bw + slide;
        int y = by + oi * 11;
        /* fond translucide */
        gfx_set_blend(g->renderer, true);
        fill_rect(g->renderer, x, y, bw, 9, 0x000000C8);
        gfx_set_blend(g->renderer, false);
        /* liseret colore + texte */
        fill_rect(g->renderer, x, y, 2, 9, g->toasts[i].color);
        text_draw(g->renderer, x + pad, y + 1, g->toasts[i].text,
                  g->toasts[i].color);
    }
}

/* ============================================================
 *  COMBAT LOG : ring buffer de 8 entrees, chaque vit ~8s puis fade
 *  sur la derniere seconde. Affichage bas-droite pendant la run.
 * ============================================================ */
#include <stdarg.h>
void log_push(Game *g, uint32_t color, const char *fmt, ...) {
    if (!g || !fmt) return;
    int n = (int)(sizeof(g->logs) / sizeof(g->logs[0]));
    int i = g->log_head;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g->logs[i].text, sizeof(g->logs[i].text), fmt, ap);
    va_end(ap);
    g->logs[i].color = color;
    g->logs[i].life = 8.f;
    g->log_head = (i + 1) % n;
}

void log_tick(Game *g, float dt) {
    if (!g) return;
    int n = (int)(sizeof(g->logs) / sizeof(g->logs[0]));
    for (int i = 0; i < n; i++) {
        if (g->logs[i].life > 0.f) g->logs[i].life -= dt;
        if (g->logs[i].life < 0.f) g->logs[i].life = 0.f;
    }
}

void log_render(Game *g) {
    if (!g) return;
    int N = (int)(sizeof(g->logs) / sizeof(g->logs[0]));
    /* affichage : du plus recent (en haut de la pile) au plus ancien (bas).
     * Position : bas-droite, juste au-dessus des toasts. */
    int by = INTERNAL_H - 20;
    int bx = INTERNAL_W - 4;
    int rendered = 0;
    /* iter du plus recent au plus ancien : on remonte log_head */
    for (int k = 0; k < N && rendered < N; k++) {
        int idx = (g->log_head - 1 - k + N * 2) % N;
        if (g->logs[idx].life <= 0.f) continue;
        float t = g->logs[idx].life / 8.f;
        if (t > 1.f) t = 1.f;
        uint8_t alpha = (uint8_t)(t > 0.125f ? 255 : (uint8_t)(255.f * t * 8.f));
        uint32_t col = (g->logs[idx].color & 0xFFFFFF00u) | alpha;
        int tw = text_width(g->logs[idx].text);
        int pad = 3;
        int px = bx - tw - pad;
        int py = by - rendered * 9;
        if (py < 80) break;     /* deborde, stop */
        gfx_set_blend(g->renderer, true);
        fill_rect(g->renderer, px - pad, py - 1, tw + pad * 2, 9,
                  (uint32_t)((0x00u << 24) | (0x00u << 16) | (0x00u << 8) | (uint32_t)(alpha * 0x80 / 255)));
        gfx_set_blend(g->renderer, false);
        text_draw(g->renderer, px, py, g->logs[idx].text, col);
        rendered++;
    }
}

/* Vignette plein-ecran : assombrit les bords (utilise par render_world). */
void draw_vignette(Game *g) {
    gfx_set_blend(g->renderer, true);
    int n = 24;
    for (int i = 0; i < n; i++) {
        int alpha = 110 - i * 4;
        if (alpha < 0) alpha = 0;
        uint32_t col = (uint32_t)(alpha & 0xFF) | 0x00000600u;
        gfx_set_color(g->renderer, col);
        gfx_fill_rect(g->renderer, i, i, INTERNAL_W - i*2, 1);
        gfx_fill_rect(g->renderer, i, INTERNAL_H - 1 - i, INTERNAL_W - i*2, 1);
        gfx_fill_rect(g->renderer, i, i, 1, INTERNAL_H - i*2);
        gfx_fill_rect(g->renderer, INTERNAL_W - 1 - i, i, 1, INTERNAL_H - i*2);
    }
    gfx_set_blend(g->renderer, false);
}
