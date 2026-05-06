/*
 * render.c - rendu pixel art procedural + UI + bitmap font
 */
#include "game.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ---------- 5x7 BITMAP FONT (uppercase + digits + basic punct) ---------- */
/* each glyph: 7 rows, low 5 bits each (bit0 = leftmost column) */
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

void text_draw(SDL_Renderer *r, int x, int y, const char *s, uint32_t col) {
    SDL_SetRenderDrawColor(r,
        (col >> 24) & 0xFF, (col >> 16) & 0xFF, (col >> 8) & 0xFF, col & 0xFF);
    int cx = x;
    for (const char *p = s; *p; p++) {
        if (*p == '\n') { cx = x; y += 8; continue; }
        const Glyph *gph = find_glyph(*p);
        for (int row = 0; row < 7; row++) {
            uint8_t bits = gph->row[row];
            for (int b = 0; b < 5; b++) {
                if (bits & (1 << b)) {
                    SDL_Rect rr = { cx + b, y + row, 1, 1 };
                    SDL_RenderFillRect(r, &rr);
                }
            }
        }
        cx += 6;
    }
}

void text_drawf(SDL_Renderer *r, int x, int y, uint32_t col, const char *fmt, ...) {
    char buf[256];
    va_list ap; va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    text_draw(r, x, y, buf, col);
}

int text_width(const char *s) { return (int)strlen(s) * 6; }

/* ---------- HELPERS ---------- */
static void set_color_u32(SDL_Renderer *r, uint32_t c) {
    SDL_SetRenderDrawColor(r,
        (c >> 24) & 0xFF, (c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF);
}

static void fill_rect(SDL_Renderer *r, int x, int y, int w, int h, uint32_t c) {
    set_color_u32(r, c);
    SDL_Rect rr = { x, y, w, h };
    SDL_RenderFillRect(r, &rr);
}

static void draw_disk(SDL_Renderer *r, int cx, int cy, int radius, uint32_t c) {
    set_color_u32(r, c);
    for (int dy = -radius; dy <= radius; dy++) {
        int dx = (int)sqrtf((float)(radius * radius - dy * dy));
        SDL_Rect rr = { cx - dx, cy + dy, dx * 2 + 1, 1 };
        SDL_RenderFillRect(r, &rr);
    }
}

static void draw_ring(SDL_Renderer *r, int cx, int cy, int radius, uint32_t c) {
    set_color_u32(r, c);
    int n = 32;
    for (int i = 0; i < n; i++) {
        float a = (i / (float)n) * 6.2831f;
        int x = cx + (int)(cosf(a) * radius);
        int y = cy + (int)(sinf(a) * radius);
        SDL_Rect rr = { x, y, 1, 1 };
        SDL_RenderFillRect(r, &rr);
    }
}

/* ---------- SPRITES ---------- */
static void draw_player(Game *g, int sx, int sy) {
    Player *p = &g->player;
    bool blink = p->invuln_t > 0.f && (((int)(g->time * 24.f)) % 2 == 0);
    if (blink) return;
    /* body (cape + tunic) */
    fill_rect(g->renderer, sx - 5, sy - 8, 10, 12, 0x303040FF); /* cape */
    fill_rect(g->renderer, sx - 4, sy - 4, 8, 8, 0x6A4A2AFF);   /* tunic */
    /* head */
    fill_rect(g->renderer, sx - 3, sy - 10, 6, 5, 0xE8C089FF);
    /* hair */
    fill_rect(g->renderer, sx - 3, sy - 11, 6, 2, 0x402010FF);
    /* eyes */
    fill_rect(g->renderer, sx - 2, sy - 8, 1, 1, 0x000000FF);
    fill_rect(g->renderer, sx + 1, sy - 8, 1, 1, 0x000000FF);
    /* belt */
    fill_rect(g->renderer, sx - 4, sy + 1, 8, 1, 0x202020FF);
    /* boots */
    fill_rect(g->renderer, sx - 4, sy + 4, 3, 3, 0x402010FF);
    fill_rect(g->renderer, sx + 1, sy + 4, 3, 3, 0x402010FF);
    /* dash glow */
    if (p->dash_t > 0.f) draw_ring(g->renderer, sx, sy - 2, 10, 0x80FFFFFF);
}

static void draw_enemy(Game *g, Enemy *e, int sx, int sy) {
    uint32_t base = 0xC03030FF;
    int w = (int)e->r * 2;
    switch (e->kind) {
        case 0: base = 0xA02020FF; break;
        case 1: base = 0x208030FF; break;
        case 2: base = 0xA040C0FF; break;
        case 3: base = 0xC09020FF; break;
        case 4: base = 0xFF2080FF; break;
    }
    /* shadow */
    fill_rect(g->renderer, sx - w / 2, sy + w / 2 - 1, w, 2, 0x00000060);
    /* body */
    fill_rect(g->renderer, sx - w / 2, sy - w / 2, w, w, base);
    /* eyes */
    fill_rect(g->renderer, sx - w / 4, sy - 1, 2, 2, 0xFFFF40FF);
    fill_rect(g->renderer, sx + w / 4 - 2, sy - 1, 2, 2, 0xFFFF40FF);
    /* status overlays */
    if (e->fire_dot > 0.f) {
        fill_rect(g->renderer, sx - w / 2, sy - w / 2 - 2, w, 2, 0xFF8030FF);
    }
    if (e->slow_t > 0.f) {
        fill_rect(g->renderer, sx - w / 2 - 1, sy - w / 2, 1, w, 0x60A0FFFF);
        fill_rect(g->renderer, sx + w / 2, sy - w / 2, 1, w, 0x60A0FFFF);
    }
    if (e->stun_t > 0.f) {
        for (int i = 0; i < 4; i++) {
            float a = g->time * 6.f + i * 1.5f;
            int x = sx + (int)(cosf(a) * (w / 2 + 3));
            int y = sy + (int)(sinf(a) * (w / 2 + 3));
            fill_rect(g->renderer, x, y, 1, 1, 0xFFFF80FF);
        }
    }
    /* HP bar */
    if (e->hp < e->maxhp) {
        int bw = w + 2;
        fill_rect(g->renderer, sx - bw / 2, sy - w / 2 - 4, bw, 1, 0x402020FF);
        int hf = (int)(bw * (e->hp / e->maxhp));
        fill_rect(g->renderer, sx - bw / 2, sy - w / 2 - 4, hf, 1, 0xFF4040FF);
    }
}

static void draw_projectile(Game *g, Projectile *pr, int sx, int sy) {
    uint32_t col = element_color(pr->primary);
    if (pr->owner == 1) col = 0xFF80C0FF;
    if (pr->aoe > 0.f) {
        draw_disk(g->renderer, sx, sy, (int)pr->r + 1, col);
        draw_ring(g->renderer, sx, sy, (int)pr->r + 3, col);
    } else {
        draw_disk(g->renderer, sx, sy, (int)pr->r, col);
    }
}

static void draw_pickup(Game *g, Pickup *pk, int sx, int sy) {
    int yoff = (int)(sinf(pk->hover_t) * 1.5f);
    switch (pk->kind) {
        case PU_XP:
            fill_rect(g->renderer, sx - 1, sy - 1 + yoff, 3, 3, 0x40C0FFFF); break;
        case PU_HEART:
            fill_rect(g->renderer, sx - 2, sy - 1 + yoff, 5, 3, 0xFF4060FF);
            fill_rect(g->renderer, sx - 1, sy - 2 + yoff, 1, 1, 0xFF4060FF);
            fill_rect(g->renderer, sx + 1, sy - 2 + yoff, 1, 1, 0xFF4060FF);
            break;
        case PU_SOUL:
            fill_rect(g->renderer, sx - 2, sy - 2 + yoff, 4, 4, 0x80E080FF);
            fill_rect(g->renderer, sx - 1, sy + 2 + yoff, 1, 1, 0x80E080FF);
            break;
        case PU_ELEMENT:
            draw_disk(g->renderer, sx, sy + yoff, 3, element_color((Element)pk->value));
            draw_ring(g->renderer, sx, sy + yoff, 5, element_color((Element)pk->value));
            break;
        case PU_WEAPON:
            fill_rect(g->renderer, sx - 3, sy - 3 + yoff, 6, 6, 0xE0E0FFFF);
            fill_rect(g->renderer, sx - 1, sy - 1 + yoff, 2, 2, 0x404060FF);
            break;
        case PU_CHEST:
            fill_rect(g->renderer, sx - 5, sy - 4 + yoff, 10, 8, 0x805030FF);
            fill_rect(g->renderer, sx - 5, sy - 4 + yoff, 10, 1, 0xFFC040FF);
            fill_rect(g->renderer, sx - 1, sy - 1 + yoff, 2, 2, 0xFFE060FF);
            break;
    }
}

static void draw_fairy(Game *g, Fairy *f, int sx, int sy) {
    uint32_t col = element_color(f->element);
    draw_disk(g->renderer, sx, sy, 2, col);
    draw_ring(g->renderer, sx, sy, 4, (col & 0xFFFFFF00) | 0x40);
}

/* ---------- WORLD ---------- */
static void draw_tile(SDL_Renderer *r, int sx, int sy, TileKind t, int tx, int ty) {
    switch (t) {
        case T_VOID:
            fill_rect(r, sx, sy, TILE, TILE, 0x080610FF);
            break;
        case T_FLOOR: {
            uint32_t c = ((tx + ty) & 1) ? 0x2A1F30FF : 0x231828FF;
            fill_rect(r, sx, sy, TILE, TILE, c);
            /* pixel detail */
            int hash = (tx * 7 + ty * 13) & 7;
            if (hash == 0) fill_rect(r, sx + 3, sy + 4, 1, 1, 0x3A2A40FF);
            if (hash == 3) fill_rect(r, sx + 11, sy + 9, 1, 1, 0x3A2A40FF);
            break;
        }
        case T_WALL:
            fill_rect(r, sx, sy, TILE, TILE, 0x40384DFF);
            fill_rect(r, sx, sy, TILE, 2, 0x564867FF);
            fill_rect(r, sx, sy + TILE - 2, TILE, 2, 0x2A2333FF);
            fill_rect(r, sx + 4, sy + 5, 2, 2, 0x322840FF);
            fill_rect(r, sx + 10, sy + 9, 2, 2, 0x322840FF);
            break;
        case T_DOOR:
            fill_rect(r, sx, sy, TILE, TILE, 0x603020FF);
            break;
        case T_EXIT:
            fill_rect(r, sx, sy, TILE, TILE, 0x101830FF);
            for (int i = 0; i < 4; i++) {
                int p = (i * 4) + (tx + ty);
                fill_rect(r, sx + 2 + p % 12, sy + 2 + (p * 3) % 12, 1, 1, 0x80E0FFFF);
            }
            fill_rect(r, sx + 4, sy + 4, 8, 8, 0x4070C0FF);
            fill_rect(r, sx + 6, sy + 6, 4, 4, 0xC0E0FFFF);
            break;
        case T_BLOOD:
            fill_rect(r, sx, sy, TILE, TILE, 0x231828FF);
            fill_rect(r, sx + 4, sy + 5, 6, 4, 0x602020FF);
            fill_rect(r, sx + 3, sy + 7, 8, 2, 0x602020FF);
            break;
        case T_HAZARD_LAVA:
            fill_rect(r, sx, sy, TILE, TILE, 0xC04020FF);
            fill_rect(r, sx + 4, sy + 4, 4, 4, 0xFF8030FF);
            break;
        case T_HAZARD_WATER:
            fill_rect(r, sx, sy, TILE, TILE, 0x4080C0FF);
            break;
    }
}

void render_world(Game *g) {
    /* tiles: only draw visible */
    int cx = (int)g->camera_x;
    int cy = (int)g->camera_y;
    int x0 = cx / TILE - 1; if (x0 < 0) x0 = 0;
    int y0 = cy / TILE - 1; if (y0 < 0) y0 = 0;
    int x1 = (cx + INTERNAL_W) / TILE + 1; if (x1 > MAP_W) x1 = MAP_W;
    int y1 = (cy + INTERNAL_H) / TILE + 1; if (y1 > MAP_H) y1 = MAP_H;
    for (int y = y0; y < y1; y++) {
        for (int x = x0; x < x1; x++) {
            int sx = x * TILE - cx;
            int sy = y * TILE - cy;
            draw_tile(g->renderer, sx, sy, g->dungeon.tiles[y][x], x, y);
        }
    }

    /* pickups */
    for (int i = 0; i < MAX_PICKUPS; i++) {
        Pickup *pk = &g->pickups[i];
        if (!pk->alive) continue;
        draw_pickup(g, pk, (int)pk->x - cx, (int)pk->y - cy);
    }

    /* fairies */
    for (int i = 0; i < MAX_FAIRIES; i++) {
        Fairy *f = &g->fairies[i];
        if (!f->alive) continue;
        draw_fairy(g, f, (int)f->x - cx, (int)f->y - cy);
    }

    /* enemies */
    for (int i = 0; i < MAX_ENEMIES; i++) {
        Enemy *e = &g->enemies[i];
        if (!e->alive) continue;
        draw_enemy(g, e, (int)e->x - cx, (int)e->y - cy);
    }

    /* player */
    draw_player(g, (int)g->player.x - cx, (int)g->player.y - cy);

    /* projectiles */
    for (int i = 0; i < MAX_PROJECTILES; i++) {
        Projectile *pr = &g->projectiles[i];
        if (!pr->alive) continue;
        draw_projectile(g, pr, (int)pr->x - cx, (int)pr->y - cy);
    }

    /* particles */
    for (int i = 0; i < MAX_PARTICLES; i++) {
        Particle *p = &g->particles[i];
        if (!p->alive) continue;
        uint32_t c = p->color;
        if (p->life < 0.2f) {
            /* fade alpha */
            uint8_t a = (uint8_t)(255 * (p->life / 0.2f));
            c = (c & 0xFFFFFF00) | a;
        }
        int sz = (int)p->size;
        SDL_SetRenderDrawBlendMode(g->renderer, SDL_BLENDMODE_BLEND);
        fill_rect(g->renderer, (int)p->x - cx - sz / 2, (int)p->y - cy - sz / 2, sz, sz, c);
        SDL_SetRenderDrawBlendMode(g->renderer, SDL_BLENDMODE_NONE);
    }

    /* aim cursor */
    int mx = g->mouse_x;
    int my = g->mouse_y;
    fill_rect(g->renderer, mx - 4, my, 3, 1, 0xFFFFFFFF);
    fill_rect(g->renderer, mx + 2, my, 3, 1, 0xFFFFFFFF);
    fill_rect(g->renderer, mx, my - 4, 1, 3, 0xFFFFFFFF);
    fill_rect(g->renderer, mx, my + 2, 1, 3, 0xFFFFFFFF);
}

/* ---------- HUD ---------- */
void render_hud(Game *g) {
    Player *p = &g->player;
    /* HP bar top-left */
    fill_rect(g->renderer, 4, 4, 100, 8, 0x202020FF);
    int hf = (int)(100 * (p->hp / p->maxhp));
    if (hf < 0) hf = 0;
    fill_rect(g->renderer, 4, 4, hf, 8, 0xC03030FF);
    text_drawf(g->renderer, 6, 5, 0xFFFFFFFF, "HP %d/%d", (int)p->hp, (int)p->maxhp);

    /* XP bar */
    fill_rect(g->renderer, 4, 14, 100, 4, 0x102040FF);
    int xf = p->xp_to_next > 0 ? (100 * p->xp / p->xp_to_next) : 0;
    fill_rect(g->renderer, 4, 14, xf, 4, 0x40A0FFFF);
    text_drawf(g->renderer, 110, 14, 0xCCCCFFFF, "LV %d", p->level);
    text_drawf(g->renderer, 145, 14, 0xC0FFC0FF, "AME %d", p->souls);
    text_drawf(g->renderer, 4, 22, 0xFFE0A0FF, "ETAGE %d  KILLS %d  T %.0f",
               g->floor_index, g->run_kills, g->run_time);

    /* weapon slots bottom-center */
    int total = 0;
    for (int i = 0; i < WEAPON_SLOTS; i++) if (p->weapons[i].owned) total++;
    int sw = 92, sh = 24, gap = 2;
    int total_w = total * sw + (total - 1) * gap;
    int sx = (INTERNAL_W - total_w) / 2;
    int sy = INTERNAL_H - sh - 4;
    int idx = 0;
    char buf[64];
    for (int i = 0; i < WEAPON_SLOTS; i++) {
        Weapon *w = &p->weapons[i];
        if (!w->owned) continue;
        bool active = (i == p->active_weapon);
        fill_rect(g->renderer, sx, sy, sw, sh, active ? 0x303060FF : 0x18181EFF);
        fill_rect(g->renderer, sx, sy, sw, 1, active ? 0xFFFF80FF : 0x404048FF);
        fill_rect(g->renderer, sx, sy + sh - 1, sw, 1, active ? 0xFFFF80FF : 0x404048FF);
        weapon_describe(w, buf, sizeof(buf));
        text_drawf(g->renderer, sx + 4, sy + 3, active ? 0xFFFF80FF : 0xCCCCCCFF,
                   "[%d] %s", i + 1, weapon_name(w->kind));
        text_draw(g->renderer, sx + 4, sy + 11, buf + (int)(strchr(buf, '[') - buf), 0xFFC0FFFF);
        /* CD */
        float frac = (w->base_cd > 0.f) ? (1.f - w->cooldown / w->base_cd) : 1.f;
        if (frac < 0) frac = 0;
        if (frac > 1) frac = 1;
        fill_rect(g->renderer, sx + 4, sy + sh - 4, sw - 8, 2, 0x202028FF);
        fill_rect(g->renderer, sx + 4, sy + sh - 4, (int)((sw - 8) * frac), 2,
                  active ? 0xFFFF80FF : 0x80C0FFFF);
        sx += sw + gap;
        idx++;
    }

    /* tip */
    text_draw(g->renderer, INTERNAL_W - 96, INTERNAL_H - 12, "TAB / 1-5 ARMES", 0x808080FF);
    text_draw(g->renderer, INTERNAL_W - 96, INTERNAL_H - 4,  "ESPACE DASH", 0x808080FF);
}

/* ---------- HUB ---------- */
void render_hub(Game *g) {
    fill_rect(g->renderer, 0, 0, INTERNAL_W, INTERNAL_H, 0x080612FF);
    /* faux brazier */
    for (int i = 0; i < 80; i++) {
        int x = INTERNAL_W / 2 + (rand() % 30) - 15;
        int y = 60 - (rand() % 30);
        fill_rect(g->renderer, x, y, 1, 1, 0xFF8040FF);
    }
    text_draw(g->renderer, INTERNAL_W / 2 - text_width("LE SANCTUAIRE") / 2, 12,
              "LE SANCTUAIRE", 0xFFE080FF);
    text_drawf(g->renderer, 8, 28, 0xC0E0FFFF, "ECLATS %d   COURSES %d   MEILLEUR ETAGE %d",
               g->meta.shards, g->meta.total_runs, g->meta.best_level);

    text_draw(g->renderer, 8, 44, "ARMES", 0xFFFFFFFF);
    int y = 56;
    for (int i = 0; i < W_COUNT; i++) {
        bool sel = (g->hub_cursor == i);
        bool unl = g->meta.weapon_unlocked[i];
        int cost = 30 + i * 20;
        uint32_t col = unl ? 0x80FF80FF : 0xC0C0C0FF;
        if (sel) col = 0xFFFF40FF;
        text_drawf(g->renderer, sel ? 4 : 12, y, col,
                   "%s %s    %s",
                   sel ? ">" : " ",
                   weapon_name((WeaponKind)i),
                   unl ? "POSSEDE" : "");
        if (!unl) text_drawf(g->renderer, 180, y, sel ? 0xFFFF40FF : 0xFFC080FF, "%d ECLATS", cost);
        y += 10;
    }

    text_draw(g->renderer, INTERNAL_W / 2, 44, "ELEMENTS", 0xFFFFFFFF);
    y = 56;
    for (int e = 1; e < EL_COUNT; e++) {
        int cursor = W_COUNT + e;
        bool sel = (g->hub_cursor == cursor);
        bool unl = g->meta.element_unlocked[e];
        int cost = 25 + e * 15;
        uint32_t col = unl ? 0x80FF80FF : 0xC0C0C0FF;
        if (sel) col = 0xFFFF40FF;
        text_drawf(g->renderer, INTERNAL_W / 2 + (sel ? -4 : 4), y, col,
                   "%s %s   %s",
                   sel ? ">" : " ",
                   element_name((Element)e),
                   unl ? "DEBLOQUE" : "");
        if (!unl) text_drawf(g->renderer, INTERNAL_W - 76, y, sel ? 0xFFFF40FF : 0xFFC080FF, "%d ECLATS", cost);
        y += 10;
    }

    text_draw(g->renderer, INTERNAL_W / 2 - 110, INTERNAL_H - 32,
              "W/S NAVIGUER   ENTREE ACHETER", 0xCCCCCCFF);
    text_draw(g->renderer, INTERNAL_W / 2 - 110, INTERNAL_H - 22,
              "R DEBUTER COURSE   H AIDE   ECHAP QUITTER", 0xFFFF80FF);
}

/* ---------- LEVELUP ---------- */
void render_levelup(Game *g) {
    SDL_SetRenderDrawBlendMode(g->renderer, SDL_BLENDMODE_BLEND);
    fill_rect(g->renderer, 0, 0, INTERNAL_W, INTERNAL_H, 0x000000C0);
    SDL_SetRenderDrawBlendMode(g->renderer, SDL_BLENDMODE_NONE);

    text_draw(g->renderer, INTERNAL_W / 2 - text_width("MONTEE DE NIVEAU") / 2, 30,
              "MONTEE DE NIVEAU", 0xFFFF80FF);
    Weapon *w = &g->player.weapons[g->player.active_weapon];
    text_drawf(g->renderer, INTERNAL_W / 2 - 100, 46, 0xC0C0FFFF,
               "ARME ACTIVE %s   (CHANGE AVEC TAB AVANT MAJ)", weapon_name(w->kind));

    int boxw = 130, boxh = 50;
    int total_w = boxw * 3 + 12;
    int sx = (INTERNAL_W - total_w) / 2;
    int sy = 70;
    for (int c = 0; c < 3; c++) {
        int kind = g->levelup_choice_kind[c];
        int v = g->levelup_choices[c];
        fill_rect(g->renderer, sx, sy, boxw, boxh, 0x18101DFF);
        fill_rect(g->renderer, sx, sy, boxw, 1, 0xFFFF80FF);
        fill_rect(g->renderer, sx, sy + boxh - 1, boxw, 1, 0xFFFF80FF);
        text_drawf(g->renderer, sx + 4, sy + 4, 0xFFFF80FF, "[%d]", c + 1);
        if (kind == 1) {
            Element e = (Element)v;
            text_drawf(g->renderer, sx + 24, sy + 4, element_color(e), "ELEMENT %s", element_name(e));
            text_drawf(g->renderer, sx + 4, sy + 16, 0xCCCCCCFF, "Greffe sur %s",
                       weapon_name(w->kind));
            /* preview combo by simulating attach */
            Weapon test = *w;
            weapon_attach_element(&test, e);
            char buf[80]; weapon_describe(&test, buf, sizeof(buf));
            text_draw(g->renderer, sx + 4, sy + 28, buf, 0xFFC0FFFF);
        } else {
            const char *lbl = "?";
            if (v == 0) lbl = "+20 PV MAX";
            else if (v == 1) lbl = "+10 VITESSE";
            else if (v == 2) lbl = "+15% DEGATS";
            text_drawf(g->renderer, sx + 24, sy + 4, 0xFFE080FF, "STAT");
            text_draw(g->renderer, sx + 4, sy + 20, lbl, 0xFFFFFFFF);
        }
        sx += boxw + 6;
    }
    text_draw(g->renderer, INTERNAL_W / 2 - 80, INTERNAL_H - 20,
              "APPUYE 1 / 2 / 3 POUR CHOISIR", 0xFFFFFFFF);
}

/* ---------- DEAD ---------- */
void render_dead(Game *g) {
    SDL_SetRenderDrawBlendMode(g->renderer, SDL_BLENDMODE_BLEND);
    fill_rect(g->renderer, 0, 0, INTERNAL_W, INTERNAL_H, 0x300010C0);
    SDL_SetRenderDrawBlendMode(g->renderer, SDL_BLENDMODE_NONE);
    text_draw(g->renderer, INTERNAL_W / 2 - text_width("VAINCU") / 2, 80,
              "VAINCU", 0xFF4060FF);
    text_drawf(g->renderer, INTERNAL_W / 2 - 60, 100, 0xFFFFFFFF,
               "ETAGE %d   KILLS %d   AMES %d",
               g->floor_index, g->run_kills, g->player.souls);
    int gain = g->player.souls + g->run_kills / 4 + g->floor_index * 5;
    text_drawf(g->renderer, INTERNAL_W / 2 - 60, 112, 0xFFE080FF,
               "GAIN PERMANENT %d ECLATS", gain);
    text_draw(g->renderer, INTERNAL_W / 2 - text_width("ENTREE POUR RETOUR AU SANCTUAIRE") / 2,
              140, "ENTREE POUR RETOUR AU SANCTUAIRE", 0xFFFFFFFF);
}

/* ---------- TITLE ---------- */
void render_title(Game *g) {
    /* starfield */
    for (int i = 0; i < 60; i++) {
        int x = (i * 73 + (int)(g->time * 8)) % INTERNAL_W;
        int y = (i * 37) % INTERNAL_H;
        fill_rect(g->renderer, x, y, 1, 1, 0x404060FF);
    }
    text_draw(g->renderer, INTERNAL_W / 2 - text_width("CRUCIBLE") * 2 / 2, 60,
              "CRUCIBLE", 0xFFE080FF);
    text_draw(g->renderer, INTERNAL_W / 2 - text_width("DOOM x VAMPIRE x ISAAC") / 2, 80,
              "DOOM x VAMPIRE x ISAAC", 0xFFFFFFFF);
    text_draw(g->renderer, INTERNAL_W / 2 - text_width("ENTREE POUR COMMENCER") / 2, 130,
              "ENTREE POUR COMMENCER", 0x80FF80FF);
    text_draw(g->renderer, INTERNAL_W / 2 - text_width("H POUR LAIDE") / 2, 150,
              "H POUR LAIDE", 0xCCCCCCFF);
}

/* ---------- HELP ---------- */
void render_help(Game *g) {
    fill_rect(g->renderer, 0, 0, INTERNAL_W, INTERNAL_H, 0x080612FF);
    text_draw(g->renderer, 8, 8, "AIDE", 0xFFE080FF);
    int y = 24;
    text_draw(g->renderer, 8, y, "WASD / FLECHES   DEPLACEMENT", 0xFFFFFFFF); y += 10;
    text_draw(g->renderer, 8, y, "SOURIS           VISER WAND / ARC", 0xFFFFFFFF); y += 10;
    text_draw(g->renderer, 8, y, "ESPACE           DASH (IFRAMES)", 0xFFFFFFFF); y += 10;
    text_draw(g->renderer, 8, y, "1-5 / TAB / Q    ARME ACTIVE (POUR GREFFE ELEMENT)", 0xFFFFFFFF); y += 10;
    text_draw(g->renderer, 8, y, "ECHAP            ABANDONNER COURSE / QUITTER", 0xFFFFFFFF); y += 14;
    text_draw(g->renderer, 8, y, "REGLES", 0xFFE080FF); y += 10;
    text_draw(g->renderer, 8, y, "LES ARMES TIRENT AUTO QUAND PRETES", 0xCCCCCCFF); y += 10;
    text_draw(g->renderer, 8, y, "RAMASSE DES ELEMENTS, ILS SE GREFFENT SUR LARME ACTIVE", 0xCCCCCCFF); y += 10;
    text_draw(g->renderer, 8, y, "MAX 3 ELEMENTS PAR ARME, COMBOS UNIQUES", 0xCCCCCCFF); y += 10;
    text_draw(g->renderer, 8, y, "TUE TOUS LES ENNEMIS DUNE PIECE POUR LA NETTOYER", 0xCCCCCCFF); y += 10;
    text_draw(g->renderer, 8, y, "PORTAIL BLEU = ETAGE SUIVANT", 0xCCCCCCFF); y += 14;
    text_draw(g->renderer, 8, y, "ELEMENTS: FEU EAU TERRE FOUDRE AIR VIDE FEE", 0x80FFC0FF); y += 10;
    text_draw(g->renderer, 8, y, "EXEMPLES DE COMBOS:", 0xFFFFFFFF); y += 10;
    text_draw(g->renderer, 8, y, " FEU+EAU = VAPEUR  AOE", 0xFFC0C0FF); y += 8;
    text_draw(g->renderer, 8, y, " FEU+FOUDRE = PLASMA  CHAINE", 0xFFC0C0FF); y += 8;
    text_draw(g->renderer, 8, y, " EAU+FOUDRE = CHOC  PARALYSIE", 0xFFC0C0FF); y += 8;
    text_draw(g->renderer, 8, y, " VIDE+FEE+FOUDRE = DECHIRURE  PERCE+CHAINE+VOL", 0xFFC0C0FF); y += 8;
    text_draw(g->renderer, 8, INTERNAL_H - 16, "ECHAP POUR REVENIR", 0xFFFF80FF);
}
