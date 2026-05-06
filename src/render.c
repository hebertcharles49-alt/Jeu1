/*
 * render.c - rendu pixel art procedural + UI + bitmap font
 */
#include "game.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

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

void text_draw(SDL_Renderer *r, int x, int y, const char *s, uint32_t col) {
    SDL_SetRenderDrawColor(r, (col>>24)&0xFF, (col>>16)&0xFF, (col>>8)&0xFF, col&0xFF);
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
    SDL_SetRenderDrawColor(r, (c>>24)&0xFF, (c>>16)&0xFF, (c>>8)&0xFF, c&0xFF);
}

static void fill_rect(SDL_Renderer *r, int x, int y, int w, int h, uint32_t c) {
    set_color_u32(r, c);
    SDL_Rect rr = { x, y, w, h };
    SDL_RenderFillRect(r, &rr);
}

static void draw_disk(SDL_Renderer *r, int cx, int cy, int radius, uint32_t c) {
    set_color_u32(r, c);
    for (int dy = -radius; dy <= radius; dy++) {
        int dx = (int)sqrtf((float)(radius*radius - dy*dy));
        SDL_Rect rr = { cx - dx, cy + dy, dx*2 + 1, 1 };
        SDL_RenderFillRect(r, &rr);
    }
}

static void draw_ring(SDL_Renderer *r, int cx, int cy, int radius, uint32_t c) {
    set_color_u32(r, c);
    int n = 48;
    for (int i = 0; i < n; i++) {
        float a = (i / (float)n) * 6.2831f;
        int x = cx + (int)(cosf(a) * radius);
        int y = cy + (int)(sinf(a) * radius);
        SDL_Rect rr = { x, y, 1, 1 };
        SDL_RenderFillRect(r, &rr);
    }
}

static void rect_outline(SDL_Renderer *r, int x, int y, int w, int h, uint32_t c) {
    fill_rect(r, x, y, w, 1, c);
    fill_rect(r, x, y+h-1, w, 1, c);
    fill_rect(r, x, y, 1, h, c);
    fill_rect(r, x+w-1, y, 1, h, c);
}

/* ---------- SPRITES ---------- */
static void draw_player(Game *g, int sx, int sy) {
    Player *p = &g->player;
    bool blink = p->invuln_t > 0.f && (((int)(g->time * 24.f)) % 2 == 0);
    if (blink) return;

    /* shadow */
    SDL_SetRenderDrawBlendMode(g->renderer, SDL_BLENDMODE_BLEND);
    fill_rect(g->renderer, sx - 6, sy + 5, 12, 3, 0x00000080);
    SDL_SetRenderDrawBlendMode(g->renderer, SDL_BLENDMODE_NONE);

    uint32_t cape = 0x303040FF, tunic = 0x6A4A2AFF, hair = 0x402010FF;
    switch (p->hero) {
        case HERO_GUERRIER:  cape = 0x802020FF; tunic = 0x707080FF; break;
        case HERO_VOLEUR:    cape = 0x305030FF; tunic = 0x202028FF; hair = 0x603020FF; break;
        case HERO_MAGE:      cape = 0x402070FF; tunic = 0x6040A0FF; hair = 0x806020FF; break;
        case HERO_BERSERKER: cape = 0x202020FF; tunic = 0x803020FF; hair = 0x202020FF; break;
        case HERO_PALADIN:   cape = 0xFFD040FF; tunic = 0xC0C0D0FF; hair = 0xC0A040FF; break;
        default: break;
    }

    fill_rect(g->renderer, sx - 5, sy - 8, 10, 12, cape);
    fill_rect(g->renderer, sx - 4, sy - 4, 8, 8,   tunic);
    fill_rect(g->renderer, sx - 3, sy - 10, 6, 5,  0xE8C089FF);
    fill_rect(g->renderer, sx - 3, sy - 11, 6, 2,  hair);
    fill_rect(g->renderer, sx - 2, sy - 8, 1, 1,   0x000000FF);
    fill_rect(g->renderer, sx + 1, sy - 8, 1, 1,   0x000000FF);
    fill_rect(g->renderer, sx - 4, sy + 1, 8, 1,   0x202020FF);
    fill_rect(g->renderer, sx - 4, sy + 4, 3, 3,   0x402010FF);
    fill_rect(g->renderer, sx + 1, sy + 4, 3, 3,   0x402010FF);

    /* attack swing */
    if (p->anim_t > 0.f) {
        float t = 1.f - (p->anim_t / 0.18f);
        float ang = atan2f(p->anim_dir_y, p->anim_dir_x);
        switch (p->anim_kind) {
            case 0: { /* sword arc */
                for (int i = 0; i < 10; i++) {
                    float fr = i / 9.f;
                    float a = ang - 0.7f + (fr + t) * 0.9f;
                    int x = sx + (int)(cosf(a) * 18);
                    int y = sy + (int)(sinf(a) * 18);
                    fill_rect(g->renderer, x - 1, y - 1, 3, 3, 0xE0E0E0FF);
                }
                break;
            }
            case 1: { /* axe radial */
                int rr = 12 + (int)(t * 14);
                draw_ring(g->renderer, sx, sy, rr, 0xFFE0A0FF);
                break;
            }
            case 2: { /* shield pulse */
                int rr = 8 + (int)(t * 24);
                draw_ring(g->renderer, sx, sy, rr, 0xC0C0FFFF);
                break;
            }
            case 3: { /* wand spark */
                int x = sx + (int)(cosf(ang) * 12);
                int y = sy + (int)(sinf(ang) * 12);
                draw_disk(g->renderer, x, y, 3, 0xC080FFFF);
                break;
            }
            case 4: { /* bow string */
                int x = sx + (int)(cosf(ang) * 14);
                int y = sy + (int)(sinf(ang) * 14);
                fill_rect(g->renderer, x - 1, y - 1, 3, 3, 0xE0E080FF);
                break;
            }
            case 5: { /* fists */
                float push = t * 6.f;
                int x = sx + (int)(cosf(ang) * (6 + push));
                int y = sy + (int)(sinf(ang) * (6 + push));
                fill_rect(g->renderer, x - 2, y - 2, 4, 4, 0xE8C089FF);
                break;
            }
        }
    }

    if (p->dash_t > 0.f) draw_ring(g->renderer, sx, sy - 2, 10, 0x80FFFFFF);
}

static void draw_enemy(Game *g, Enemy *e, int sx, int sy) {
    int w = (int)e->r * 2;
    bool flash = e->hit_flash > 0.f;
    uint32_t base = 0x808080FF;
    switch (e->kind) {
        case EK_ZOMBIE: base = 0x508040FF; break;
        case EK_BANDIT: base = 0x806040FF; break;
        case EK_DEMON:  base = 0xA02040FF; break;
        case EK_SLIME:  base = 0x40A0A0FF; break;
        case EK_BOSS:   base = 0xFF2080FF; break;
    }
    if (flash) base = 0xFFFFFFFF;

    /* shadow */
    SDL_SetRenderDrawBlendMode(g->renderer, SDL_BLENDMODE_BLEND);
    fill_rect(g->renderer, sx - w/2, sy + w/2 - 1, w, 3, 0x00000080);
    SDL_SetRenderDrawBlendMode(g->renderer, SDL_BLENDMODE_NONE);

    /* elite aura */
    if (e->is_elite && e->element != EL_NONE) {
        uint32_t col = element_color(e->element);
        int pulse = 2 + (int)(sinf(g->time * 4.f) * 1.5f);
        draw_ring(g->renderer, sx, sy, w/2 + 3 + pulse, col);
    }

    if (e->kind == EK_ZOMBIE) {
        fill_rect(g->renderer, sx - w/2, sy - w/2, w, w, base);
        if (!flash) fill_rect(g->renderer, sx - w/2, sy - w/2, w, 2, 0x405030FF);
        fill_rect(g->renderer, sx - 3, sy - 1, 2, 2, 0xFF4040FF);
        fill_rect(g->renderer, sx + 1, sy - 1, 2, 2, 0xFF4040FF);
        fill_rect(g->renderer, sx - 2, sy + 3, 4, 1, 0x202010FF);
    } else if (e->kind == EK_BANDIT) {
        fill_rect(g->renderer, sx - w/2, sy - w/2, w, w, base);
        if (!flash) fill_rect(g->renderer, sx - w/2, sy - w/2 + 2, w, 2, 0x000000FF);  /* mask */
        fill_rect(g->renderer, sx - 3, sy + 1, 2, 1, 0xFFE040FF);
        fill_rect(g->renderer, sx + 1, sy + 1, 2, 1, 0xFFE040FF);
    } else if (e->kind == EK_DEMON) {
        fill_rect(g->renderer, sx - w/2, sy - w/2, w, w, base);
        if (!flash) {
            /* horns */
            fill_rect(g->renderer, sx - w/2, sy - w/2 - 3, 2, 3, 0x603040FF);
            fill_rect(g->renderer, sx + w/2 - 2, sy - w/2 - 3, 2, 3, 0x603040FF);
            /* mouth */
            fill_rect(g->renderer, sx - 3, sy + 1, 6, 2, 0x000000FF);
            fill_rect(g->renderer, sx - 2, sy + 1, 1, 2, 0xFFFFFFFF);
            fill_rect(g->renderer, sx + 0, sy + 1, 1, 2, 0xFFFFFFFF);
            fill_rect(g->renderer, sx + 2, sy + 1, 1, 2, 0xFFFFFFFF);
        }
        fill_rect(g->renderer, sx - 3, sy - 2, 2, 2, 0xFFFF00FF);
        fill_rect(g->renderer, sx + 1, sy - 2, 2, 2, 0xFFFF00FF);
    } else if (e->kind == EK_SLIME) {
        int wob = (int)(sinf(g->time * 8.f + e->x) * 1.5f);
        for (int dy = -w/2; dy <= w/2; dy++) {
            int dx = (int)sqrtf((float)((w/2)*(w/2) - dy*dy));
            fill_rect(g->renderer, sx - dx, sy + dy + wob, dx*2 + 1, 1, base);
        }
        if (!flash) {
            fill_rect(g->renderer, sx - 2, sy - 1 + wob, 1, 1, 0xFFFFFFFF);
            fill_rect(g->renderer, sx + 1, sy - 1 + wob, 1, 1, 0xFFFFFFFF);
        }
    } else if (e->kind == EK_BOSS) {
        /* bigger detailed sprite */
        fill_rect(g->renderer, sx - w/2, sy - w/2, w, w, base);
        if (!flash) {
            /* spikes */
            fill_rect(g->renderer, sx - w/2 - 2, sy - 2, 2, 4, 0x803040FF);
            fill_rect(g->renderer, sx + w/2,     sy - 2, 2, 4, 0x803040FF);
            /* crown / horns */
            fill_rect(g->renderer, sx - w/2,     sy - w/2 - 4, 3, 4, 0xFFD040FF);
            fill_rect(g->renderer, sx + w/2 - 3, sy - w/2 - 4, 3, 4, 0xFFD040FF);
            fill_rect(g->renderer, sx - 1,       sy - w/2 - 5, 2, 5, 0xFFD040FF);
            /* eyes glow */
            fill_rect(g->renderer, sx - 5, sy - 2, 3, 3, 0xFFFFFFFF);
            fill_rect(g->renderer, sx + 2, sy - 2, 3, 3, 0xFFFFFFFF);
            fill_rect(g->renderer, sx - 4, sy - 1, 1, 1, 0xFF0000FF);
            fill_rect(g->renderer, sx + 3, sy - 1, 1, 1, 0xFF0000FF);
            /* fang */
            fill_rect(g->renderer, sx - 3, sy + 3, 6, 2, 0x000000FF);
            fill_rect(g->renderer, sx - 2, sy + 4, 1, 1, 0xFFFFFFFF);
            fill_rect(g->renderer, sx + 1, sy + 4, 1, 1, 0xFFFFFFFF);
        }
        if (e->element != EL_NONE) {
            draw_ring(g->renderer, sx, sy, w/2 + 4, element_color(e->element));
        }
    }

    if (e->fire_dot > 0.f) fill_rect(g->renderer, sx - w/2, sy - w/2 - 2, w, 2, 0xFF8030FF);
    if (e->slow_t > 0.f) {
        fill_rect(g->renderer, sx - w/2 - 1, sy - w/2, 1, w, 0x60A0FFFF);
        fill_rect(g->renderer, sx + w/2,     sy - w/2, 1, w, 0x60A0FFFF);
    }
    if (e->stun_t > 0.f) {
        for (int i = 0; i < 4; i++) {
            float a = g->time * 6.f + i * 1.5f;
            int x = sx + (int)(cosf(a) * (w/2 + 3));
            int y = sy + (int)(sinf(a) * (w/2 + 3));
            fill_rect(g->renderer, x, y, 1, 1, 0xFFFF80FF);
        }
    }

    /* HP bar */
    if (e->hp < e->maxhp) {
        int bw = (e->is_boss) ? 80 : (w + 2);
        int bx = sx - bw / 2;
        int by = (e->is_boss) ? (sy - w/2 - 8) : (sy - w/2 - 4);
        fill_rect(g->renderer, bx, by, bw, e->is_boss ? 3 : 1, 0x402020FF);
        int hf = (int)(bw * (e->hp / e->maxhp));
        fill_rect(g->renderer, bx, by, hf, e->is_boss ? 3 : 1, 0xFF4040FF);
        if (e->is_boss) rect_outline(g->renderer, bx-1, by-1, bw+2, 5, 0xFFD040FF);
    }
}

static void draw_projectile(Game *g, Projectile *pr, int sx, int sy) {
    uint32_t col = element_color(pr->primary);
    if (pr->owner == 1) {
        col = (pr->primary == EL_NONE) ? 0xFF80C0FF : col;
        /* outer red glow on enemy proj */
        draw_ring(g->renderer, sx, sy, (int)pr->r + 2, 0xFF206080);
    }
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
        case PU_COIN:
            draw_disk(g->renderer, sx, sy + yoff, 3, 0xFFD040FF);
            fill_rect(g->renderer, sx - 1, sy + yoff, 2, 1, 0x806020FF);
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
        case PU_PORTAL:
            for (int i = 0; i < 12; i++) {
                float a = (i / 12.f) * 6.2831f + g->time * 2.f;
                int x = sx + (int)(cosf(a) * 8);
                int y = sy + (int)(sinf(a) * 8) + yoff;
                fill_rect(g->renderer, x - 1, y - 1, 2, 2, 0x80E0FFFF);
            }
            draw_disk(g->renderer, sx, sy + yoff, 4, 0x4070C0FF);
            draw_disk(g->renderer, sx, sy + yoff, 2, 0xC0E0FFFF);
            break;
        case PU_ITEM: {
            uint32_t col = rarity_color(pk->item.rarity);
            fill_rect(g->renderer, sx - 4, sy - 4 + yoff, 8, 8, col);
            rect_outline(g->renderer, sx - 4, sy - 4 + yoff, 8, 8, 0x000000FF);
            fill_rect(g->renderer, sx - 1, sy - 1 + yoff, 2, 2, 0xFFFFFFFF);
            break;
        }
    }
}

static void draw_fairy(Game *g, Fairy *f, int sx, int sy) {
    uint32_t col = element_color(f->element);
    draw_disk(g->renderer, sx, sy, 2, col);
    draw_ring(g->renderer, sx, sy, 4, (col & 0xFFFFFF00) | 0x40);
}

/* ---------- TILES ---------- */
static void draw_tile(Game *g, int sx, int sy, TileKind t, int tx, int ty) {
    SDL_Renderer *r = g->renderer;
    switch (t) {
        case T_VOID:
            fill_rect(r, sx, sy, TILE, TILE, 0x040308FF);
            break;
        case T_FLOOR: {
            uint32_t c = ((tx + ty) & 1) ? 0x2A1F30FF : 0x231828FF;
            fill_rect(r, sx, sy, TILE, TILE, c);
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
        case T_TORCH: {
            fill_rect(r, sx, sy, TILE, TILE, 0x231828FF);
            fill_rect(r, sx + 7, sy + 4, 2, 8, 0x402010FF);
            int flick = ((tx + ty) * 7 + (int)(g->time * 18.f)) & 7;
            int yy = 1 + flick / 4;
            int hh = 4 - flick % 2;
            fill_rect(r, sx + 6, sy + yy, 4, hh, 0xFF8030FF);
            fill_rect(r, sx + 7, sy + yy, 2, hh - 1, 0xFFE060FF);
            /* ambient pixels */
            fill_rect(r, sx + 4 + (flick & 1), sy + 12, 1, 1, 0x402010FF);
            fill_rect(r, sx + 11 - (flick & 1), sy + 12, 1, 1, 0x402010FF);
            break;
        }
        case T_BONES:
            fill_rect(r, sx, sy, TILE, TILE, 0x231828FF);
            fill_rect(r, sx + 3, sy + 8, 9, 1, 0xC0C0A0FF);
            fill_rect(r, sx + 4, sy + 6, 1, 1, 0xC0C0A0FF);
            fill_rect(r, sx + 11, sy + 10, 1, 1, 0xC0C0A0FF);
            break;
        case T_RUNE: {
            fill_rect(r, sx, sy, TILE, TILE, 0x180C20FF);
            int pulse = 1 + (int)(sinf(g->time * 3.f + tx + ty) * 0.5f + 0.5f);
            fill_rect(r, sx + 4, sy + 4, 8, 8, 0x4020A0FF);
            fill_rect(r, sx + 6, sy + 6, 4, 4, 0xA060FFFF);
            fill_rect(r, sx + 7, sy + 7, 2, 2, 0xE0C0FFFF);
            (void)pulse;
            break;
        }
    }
}

/* ---------- WORLD ---------- */
void render_world(Game *g) {
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
            draw_tile(g, sx, sy, g->dungeon.tiles[y][x], x, y);
        }
    }

    for (int i = 0; i < MAX_PICKUPS; i++) {
        Pickup *pk = &g->pickups[i];
        if (!pk->alive) continue;
        draw_pickup(g, pk, (int)pk->x - cx, (int)pk->y - cy);
    }
    for (int i = 0; i < MAX_FAIRIES; i++) {
        Fairy *f = &g->fairies[i];
        if (!f->alive) continue;
        draw_fairy(g, f, (int)f->x - cx, (int)f->y - cy);
    }
    for (int i = 0; i < MAX_ENEMIES; i++) {
        Enemy *e = &g->enemies[i];
        if (!e->alive) continue;
        draw_enemy(g, e, (int)e->x - cx, (int)e->y - cy);
    }
    draw_player(g, (int)g->player.x - cx, (int)g->player.y - cy);

    for (int i = 0; i < MAX_PROJECTILES; i++) {
        Projectile *pr = &g->projectiles[i];
        if (!pr->alive) continue;
        draw_projectile(g, pr, (int)pr->x - cx, (int)pr->y - cy);
    }

    for (int i = 0; i < MAX_PARTICLES; i++) {
        Particle *p = &g->particles[i];
        if (!p->alive) continue;
        uint32_t c = p->color;
        if (p->life < 0.2f) {
            uint8_t a = (uint8_t)(255 * (p->life / 0.2f));
            c = (c & 0xFFFFFF00) | a;
        }
        int sz = (int)p->size;
        SDL_SetRenderDrawBlendMode(g->renderer, SDL_BLENDMODE_BLEND);
        fill_rect(g->renderer, (int)p->x - cx - sz/2, (int)p->y - cy - sz/2, sz, sz, c);
        SDL_SetRenderDrawBlendMode(g->renderer, SDL_BLENDMODE_NONE);
    }

    /* damage numbers */
    for (int i = 0; i < MAX_DMGNUM; i++) {
        DamageNumber *d = &g->dmgnums[i];
        if (!d->alive) continue;
        int x = (int)d->x - cx;
        int y = (int)d->y - cy;
        uint32_t col = d->color;
        if (d->life < 0.25f) {
            uint8_t a = (uint8_t)(255 * (d->life / 0.25f));
            col = (col & 0xFFFFFF00) | a;
        }
        if (d->big) {
            text_draw(g->renderer, x - 5, y - 7, d->text, 0x000000FF);
            text_draw(g->renderer, x - 4, y - 6, d->text, col);
            text_draw(g->renderer, x - 4, y - 7, d->text, col);
        } else {
            text_draw(g->renderer, x - 4, y - 6, d->text, col);
        }
    }

    /* aim cursor */
    int mx = g->mouse_x;
    int my = g->mouse_y;
    fill_rect(g->renderer, mx - 4, my, 3, 1, 0xFFFFFFFF);
    fill_rect(g->renderer, mx + 2, my, 3, 1, 0xFFFFFFFF);
    fill_rect(g->renderer, mx, my - 4, 1, 3, 0xFFFFFFFF);
    fill_rect(g->renderer, mx, my + 2, 1, 3, 0xFFFFFFFF);

    /* boss intro overlay */
    if (g->boss_intro_t > 0.f) {
        int alpha = (int)(180 * (g->boss_intro_t / 2.5f));
        if (alpha > 180) alpha = 180;
        SDL_SetRenderDrawBlendMode(g->renderer, SDL_BLENDMODE_BLEND);
        fill_rect(g->renderer, 0, INTERNAL_H/2 - 18, INTERNAL_W, 36,
                  (uint32_t)((0xFF206080 & 0xFFFFFF00) | (alpha & 0xFF)));
        SDL_SetRenderDrawBlendMode(g->renderer, SDL_BLENDMODE_NONE);
        text_draw(g->renderer, INTERNAL_W/2 - text_width(g->boss_name)/2,
                  INTERNAL_H/2 - 6, g->boss_name, 0xFFFFFFFF);
        text_draw(g->renderer, INTERNAL_W/2 - text_width("CONFRONTATION") / 2,
                  INTERNAL_H/2 + 4, "CONFRONTATION", 0xFFD040FF);
    }
}

/* ---------- HUD ---------- */
void render_hud(Game *g) {
    Player *p = &g->player;
    /* HP bar */
    fill_rect(g->renderer, 4, 4, 110, 9, 0x000000FF);
    fill_rect(g->renderer, 5, 5, 108, 7, 0x202020FF);
    int hf = (int)(108 * (p->hp / p->maxhp));
    if (hf < 0) hf = 0;
    fill_rect(g->renderer, 5, 5, hf, 7, 0xC03030FF);
    fill_rect(g->renderer, 5, 5, hf, 2, 0xE05050FF);
    text_drawf(g->renderer, 7, 5, 0xFFFFFFFF, "PV %d/%d", (int)p->hp, (int)p->maxhp);

    /* XP bar */
    fill_rect(g->renderer, 4, 15, 110, 4, 0x102040FF);
    int xf = p->xp_to_next > 0 ? (110 * p->xp / p->xp_to_next) : 0;
    fill_rect(g->renderer, 4, 15, xf, 4, 0x40A0FFFF);
    text_drawf(g->renderer, 120, 5,  0xCCCCFFFF, "LV %d", p->level);
    text_drawf(g->renderer, 120, 14, 0xFFD040FF, "%d", p->coins);
    fill_rect(g->renderer, 142, 14, 5, 5, 0xFFD040FF);
    text_drawf(g->renderer, 158, 14, 0xC0FFC0FF, "AME %d", p->souls);

    text_drawf(g->renderer, 4, 22, 0xFFE0A0FF, "ETAGE %d/%d  KILLS %d  T %.0f  ARMURE %.0f",
               g->floor_index, MAX_FLOORS, g->run_kills, g->run_time, p->armor);

    /* subclass */
    const char *sc = subclass_name(p->weapons[0].kind, p->weapons[1].kind);
    text_drawf(g->renderer, 4, 30, 0xFF80FFFF, "[%s] %s", hero_name(p->hero), sc);

    /* weapon slots */
    int sw = 130, sh = 28, gap = 4;
    int total_w = WEAPON_SLOTS * sw + (WEAPON_SLOTS - 1) * gap;
    int sx = (INTERNAL_W - total_w) / 2;
    int sy = INTERNAL_H - sh - 4;
    char buf[64];
    for (int i = 0; i < WEAPON_SLOTS; i++) {
        Weapon *w = &p->weapons[i];
        bool active = (i == p->active_weapon);
        fill_rect(g->renderer, sx, sy, sw, sh, active ? 0x303060FF : 0x18181EFF);
        rect_outline(g->renderer, sx, sy, sw, sh, active ? 0xFFFF80FF : 0x404048FF);
        weapon_describe(w, buf, sizeof(buf));
        text_drawf(g->renderer, sx + 4, sy + 3, active ? 0xFFFF80FF : 0xCCCCCCFF,
                   "[%d] %s", i + 1, weapon_name(w->kind));
        text_draw(g->renderer, sx + 4, sy + 12, buf, 0xFFC0FFFF);
        float frac = (w->base_cd > 0.f) ? (1.f - w->cooldown / w->base_cd) : 1.f;
        if (frac < 0) frac = 0;
        if (frac > 1) frac = 1;
        fill_rect(g->renderer, sx + 4, sy + sh - 5, sw - 8, 3, 0x202028FF);
        fill_rect(g->renderer, sx + 4, sy + sh - 5, (int)((sw - 8) * frac), 3,
                  active ? 0xFFFF80FF : 0x80C0FFFF);
        sx += sw + gap;
    }

    /* tip */
    text_draw(g->renderer, INTERNAL_W - 122, INTERNAL_H - 12, "TAB ARMES   I INVENTAIRE", 0x808080FF);
    text_draw(g->renderer, INTERNAL_W - 122, INTERNAL_H - 4,  "ESPACE DASH", 0x808080FF);
}

/* ---------- HUB ---------- */
void render_hub(Game *g) {
    fill_rect(g->renderer, 0, 0, INTERNAL_W, INTERNAL_H, 0x080612FF);
    /* brazier */
    for (int i = 0; i < 80; i++) {
        int x = INTERNAL_W / 2 + (rand() % 30) - 15;
        int y = 60 - (rand() % 30);
        fill_rect(g->renderer, x, y, 1, 1, 0xFF8040FF);
    }
    text_draw(g->renderer, INTERNAL_W/2 - text_width("LE SANCTUAIRE")/2, 12,
              "LE SANCTUAIRE", 0xFFE080FF);
    text_drawf(g->renderer, 8, 28, 0xC0E0FFFF,
               "ECLATS %d  COURSES %d  MEILLEUR %d/%d  VICTOIRES %d",
               g->meta.shards, g->meta.total_runs, g->meta.best_floor, MAX_FLOORS, g->meta.victories);

    text_draw(g->renderer, 8, 44, "ARMES", 0xFFFFFFFF);
    int y = 56;
    for (int i = 1; i < W_COUNT; i++) {     /* skip W_FISTS */
        bool sel = (g->hub_cursor == (i - 1));
        bool unl = g->meta.weapon_unlocked[i];
        int cost = 30 + i * 18;
        uint32_t col = unl ? 0x80FF80FF : 0xC0C0C0FF;
        if (sel) col = 0xFFFF40FF;
        text_drawf(g->renderer, sel ? 4 : 12, y, col, "%s %s   %s",
                   sel ? ">" : " ", weapon_name((WeaponKind)i),
                   unl ? "POSSEDE" : "");
        if (!unl) text_drawf(g->renderer, 180, y, sel ? 0xFFFF40FF : 0xFFC080FF, "%d ECLATS", cost);
        y += 10;
    }

    text_draw(g->renderer, INTERNAL_W/2, 44, "ELEMENTS", 0xFFFFFFFF);
    y = 56;
    for (int e = 1; e < EL_COUNT; e++) {
        int cursor = (W_COUNT - 1) + (e - 1);
        bool sel = (g->hub_cursor == cursor);
        bool unl = g->meta.element_unlocked[e];
        int cost = 25 + e * 12;
        uint32_t col = unl ? 0x80FF80FF : 0xC0C0C0FF;
        if (sel) col = 0xFFFF40FF;
        text_drawf(g->renderer, INTERNAL_W/2 + (sel ? -4 : 4), y, col, "%s %s   %s",
                   sel ? ">" : " ", element_name((Element)e),
                   unl ? "DEBLOQUE" : "");
        if (!unl) text_drawf(g->renderer, INTERNAL_W - 76, y,
                             sel ? 0xFFFF40FF : 0xFFC080FF, "%d ECLATS", cost);
        y += 10;
    }

    text_draw(g->renderer, INTERNAL_W/2 - 110, INTERNAL_H - 32,
              "W/S NAVIGUER   ENTREE ACHETER", 0xCCCCCCFF);
    text_draw(g->renderer, INTERNAL_W/2 - 110, INTERNAL_H - 22,
              "R DEBUTER COURSE   H AIDE", 0xFFFF80FF);
    text_draw(g->renderer, INTERNAL_W/2 - 110, INTERNAL_H - 12,
              "ECHAP QUITTER", 0xCCCCCCFF);
}

/* ---------- HERO SELECT ---------- */
static void draw_hero_portrait(Game *g, int sx, int sy, HeroClass h, bool selected) {
    uint32_t cape = 0x303040FF, tunic = 0x6A4A2AFF;
    switch (h) {
        case HERO_GUERRIER:  cape = 0x802020FF; tunic = 0x707080FF; break;
        case HERO_VOLEUR:    cape = 0x305030FF; tunic = 0x202028FF; break;
        case HERO_MAGE:      cape = 0x402070FF; tunic = 0x6040A0FF; break;
        case HERO_BERSERKER: cape = 0x202020FF; tunic = 0x803020FF; break;
        case HERO_PALADIN:   cape = 0xFFD040FF; tunic = 0xC0C0D0FF; break;
        default: break;
    }
    if (selected) rect_outline(g->renderer, sx - 22, sy - 30, 44, 60, 0xFFFF40FF);
    fill_rect(g->renderer, sx - 14, sy - 16, 28, 32, cape);
    fill_rect(g->renderer, sx - 12, sy - 8, 24, 22, tunic);
    fill_rect(g->renderer, sx - 8, sy - 22, 16, 12, 0xE8C089FF);
    fill_rect(g->renderer, sx - 8, sy - 24, 16, 4, 0x402010FF);
    fill_rect(g->renderer, sx - 5, sy - 18, 3, 3, 0x000000FF);
    fill_rect(g->renderer, sx + 2, sy - 18, 3, 3, 0x000000FF);
}

void render_choose_hero(Game *g) {
    fill_rect(g->renderer, 0, 0, INTERNAL_W, INTERNAL_H, 0x080612FF);
    text_draw(g->renderer, INTERNAL_W/2 - text_width("CHOISIS TON HEROS")/2, 12,
              "CHOISIS TON HEROS", 0xFFE080FF);
    text_drawf(g->renderer, 8, 24, 0xC0E0FFFF, "ECLATS %d", g->meta.shards);

    int gap = INTERNAL_W / (HERO_COUNT + 1);
    for (int i = 0; i < HERO_COUNT; i++) {
        int sx = gap * (i + 1);
        int sy = 90;
        bool sel = (g->hero_cursor == i);
        draw_hero_portrait(g, sx, sy, (HeroClass)i, sel);
        text_draw(g->renderer, sx - text_width(hero_name((HeroClass)i)) / 2, sy + 26,
                  hero_name((HeroClass)i), sel ? 0xFFFF40FF : 0xFFFFFFFF);
        if (!g->meta.hero_unlocked[i]) {
            int cost = 60 + i * 25;
            char b[24]; snprintf(b, sizeof(b), "%d ECLATS", cost);
            text_draw(g->renderer, sx - text_width(b) / 2, sy + 36, b, 0xFFC080FF);
        }
    }
    /* description */
    text_draw(g->renderer, INTERNAL_W/2 - text_width(hero_desc((HeroClass)g->hero_cursor))/2,
              INTERNAL_H - 60, hero_desc((HeroClass)g->hero_cursor), 0xCCCCFFFF);
    if (!g->meta.hero_unlocked[g->hero_cursor]) {
        text_draw(g->renderer, INTERNAL_W/2 - text_width("VERROUILLE - ENTREE POUR ACHETER") / 2,
                  INTERNAL_H - 44, "VERROUILLE - ENTREE POUR ACHETER", 0xFFC080FF);
    } else {
        text_draw(g->renderer, INTERNAL_W/2 - text_width("ENTREE POUR PARTIR EN COURSE")/2,
                  INTERNAL_H - 44, "ENTREE POUR PARTIR EN COURSE", 0x80FF80FF);
    }
    text_draw(g->renderer, INTERNAL_W/2 - text_width("A/D NAVIGUER   ECHAP RETOUR")/2,
              INTERNAL_H - 24, "A/D NAVIGUER   ECHAP RETOUR", 0xCCCCCCFF);
}

/* ---------- LEVELUP ---------- */
void render_levelup(Game *g) {
    SDL_SetRenderDrawBlendMode(g->renderer, SDL_BLENDMODE_BLEND);
    fill_rect(g->renderer, 0, 0, INTERNAL_W, INTERNAL_H, 0x000000C0);
    SDL_SetRenderDrawBlendMode(g->renderer, SDL_BLENDMODE_NONE);
    text_draw(g->renderer, INTERNAL_W/2 - text_width("MONTEE DE NIVEAU")/2, 30,
              "MONTEE DE NIVEAU", 0xFFFF80FF);
    Weapon *w = &g->player.weapons[g->player.active_weapon];
    text_drawf(g->renderer, INTERNAL_W/2 - 100, 46, 0xC0C0FFFF,
               "ARME ACTIVE %s   (TAB POUR CHANGER AVANT MAJ)", weapon_name(w->kind));

    int boxw = 130, boxh = 56;
    int total_w = boxw * 3 + 12;
    int sx = (INTERNAL_W - total_w) / 2;
    int sy = 70;
    for (int c = 0; c < 3; c++) {
        int kind = g->levelup_choice_kind[c];
        int v = g->levelup_choices[c];
        fill_rect(g->renderer, sx, sy, boxw, boxh, 0x18101DFF);
        rect_outline(g->renderer, sx, sy, boxw, boxh, 0xFFFF80FF);
        text_drawf(g->renderer, sx + 4, sy + 4, 0xFFFF80FF, "[%d]", c + 1);
        if (kind == 1) {
            Element e = (Element)v;
            text_drawf(g->renderer, sx + 24, sy + 4, element_color(e),
                       "ELEMENT %s", element_name(e));
            text_drawf(g->renderer, sx + 4, sy + 18, 0xCCCCCCFF, "Greffe sur %s",
                       weapon_name(w->kind));
            Weapon test = *w;
            weapon_attach_element(&test, e);
            char buf[80]; weapon_describe(&test, buf, sizeof(buf));
            text_draw(g->renderer, sx + 4, sy + 30, buf, 0xFFC0FFFF);
        } else {
            const char *lbl = "?";
            if      (v == 0) lbl = "+20 PV MAX";
            else if (v == 1) lbl = "+10 VITESSE";
            else if (v == 2) lbl = "+15% DEGATS";
            text_drawf(g->renderer, sx + 24, sy + 4, 0xFFE080FF, "STAT");
            text_draw(g->renderer, sx + 4, sy + 24, lbl, 0xFFFFFFFF);
        }
        sx += boxw + 6;
    }
    text_draw(g->renderer, INTERNAL_W/2 - 80, INTERNAL_H - 20,
              "1 / 2 / 3 POUR CHOISIR   I INVENTAIRE", 0xFFFFFFFF);
}

/* ---------- DEAD ---------- */
void render_dead(Game *g) {
    SDL_SetRenderDrawBlendMode(g->renderer, SDL_BLENDMODE_BLEND);
    fill_rect(g->renderer, 0, 0, INTERNAL_W, INTERNAL_H, 0x300010C0);
    SDL_SetRenderDrawBlendMode(g->renderer, SDL_BLENDMODE_NONE);
    text_draw(g->renderer, INTERNAL_W/2 - text_width("VAINCU")/2, 80,
              "VAINCU", 0xFF4060FF);
    text_drawf(g->renderer, INTERNAL_W/2 - 70, 100, 0xFFFFFFFF,
               "ETAGE %d/%d   KILLS %d   AMES %d",
               g->floor_index, MAX_FLOORS, g->run_kills, g->player.souls);
    int gain = g->player.souls + g->run_kills / 4 + g->floor_index * 5;
    text_drawf(g->renderer, INTERNAL_W/2 - 70, 112, 0xFFE080FF,
               "GAIN PERMANENT %d ECLATS", gain);
    text_draw(g->renderer, INTERNAL_W/2 - text_width("ENTREE POUR RETOUR AU SANCTUAIRE")/2,
              140, "ENTREE POUR RETOUR AU SANCTUAIRE", 0xFFFFFFFF);
}

/* ---------- VICTORY ---------- */
void render_victory(Game *g) {
    SDL_SetRenderDrawBlendMode(g->renderer, SDL_BLENDMODE_BLEND);
    fill_rect(g->renderer, 0, 0, INTERNAL_W, INTERNAL_H, 0x102030E0);
    SDL_SetRenderDrawBlendMode(g->renderer, SDL_BLENDMODE_NONE);
    /* fireworks */
    for (int i = 0; i < 80; i++) {
        int x = (int)((sinf(g->time + i) * 0.5f + 0.5f) * INTERNAL_W);
        int y = (int)((cosf(g->time * 1.3f + i) * 0.5f + 0.5f) * INTERNAL_H);
        uint32_t cols[5] = { 0xFFD040FF, 0xFF80C0FF, 0x80E0FFFF, 0xC080FFFF, 0xFFE080FF };
        fill_rect(g->renderer, x, y, 2, 2, cols[i % 5]);
    }
    text_draw(g->renderer, INTERNAL_W/2 - text_width("VICTOIRE")*2/2, 50,
              "VICTOIRE", 0xFFE080FF);
    text_draw(g->renderer, INTERNAL_W/2 - text_width("LES 10 ETAGES SONT TOMBES")/2, 80,
              "LES 10 ETAGES SONT TOMBES", 0xFFFFFFFF);
    text_drawf(g->renderer, INTERNAL_W/2 - 70, 100, 0xCCCCFFFF,
               "KILLS %d   AMES %d   PIECES %d",
               g->run_kills, g->player.souls, g->player.coins);
    text_draw(g->renderer, INTERNAL_W/2 - text_width("ENTREE POUR LE SANCTUAIRE")/2,
              160, "ENTREE POUR LE SANCTUAIRE", 0x80FF80FF);
}

/* ---------- TITLE ---------- */
void render_title(Game *g) {
    for (int i = 0; i < 60; i++) {
        int x = (i * 73 + (int)(g->time * 8)) % INTERNAL_W;
        int y = (i * 37) % INTERNAL_H;
        fill_rect(g->renderer, x, y, 1, 1, 0x404060FF);
    }
    text_draw(g->renderer, INTERNAL_W/2 - text_width("CRUCIBLE")*2/2, 60,
              "CRUCIBLE", 0xFFE080FF);
    text_draw(g->renderer, INTERNAL_W/2 - text_width("DOOM x VAMPIRE x ISAAC x DIABLO")/2, 80,
              "DOOM x VAMPIRE x ISAAC x DIABLO", 0xFFFFFFFF);
    text_draw(g->renderer, INTERNAL_W/2 - text_width("ENTREE POUR COMMENCER")/2, 130,
              "ENTREE POUR COMMENCER", 0x80FF80FF);
    text_draw(g->renderer, INTERNAL_W/2 - text_width("H POUR LAIDE")/2, 150,
              "H POUR LAIDE", 0xCCCCCCFF);
}

/* ---------- HELP ---------- */
void render_help(Game *g) {
    fill_rect(g->renderer, 0, 0, INTERNAL_W, INTERNAL_H, 0x080612FF);
    text_draw(g->renderer, 8, 6, "AIDE", 0xFFE080FF);
    int y = 18;
    text_draw(g->renderer, 8, y, "WASD / FLECHES   DEPLACEMENT", 0xFFFFFFFF); y += 9;
    text_draw(g->renderer, 8, y, "SOURIS           VISER", 0xFFFFFFFF); y += 9;
    text_draw(g->renderer, 8, y, "ESPACE           DASH", 0xFFFFFFFF); y += 9;
    text_draw(g->renderer, 8, y, "1 / 2 / TAB      ARME ACTIVE", 0xFFFFFFFF); y += 9;
    text_draw(g->renderer, 8, y, "I                INVENTAIRE", 0xFFFFFFFF); y += 9;
    text_draw(g->renderer, 8, y, "ECHAP            ABANDONNER / QUITTER", 0xFFFFFFFF); y += 12;
    text_draw(g->renderer, 8, y, "REGLES", 0xFFE080FF); y += 9;
    text_draw(g->renderer, 8, y, "Tu commences avec tes POINGS sur 2 slots", 0xCCCCCCFF); y += 9;
    text_draw(g->renderer, 8, y, "Trouve / achete des armes pour debloquer ta sous-classe", 0xCCCCCCFF); y += 9;
    text_draw(g->renderer, 8, y, "Greffe jusqu'a 3 elements par arme (ramassage = arme active)", 0xCCCCCCFF); y += 9;
    text_draw(g->renderer, 8, y, "Nettoie les pieces, abats le BOSS, prends le portail", 0xCCCCCCFF); y += 9;
    text_draw(g->renderer, 8, y, "Shop entre etages: pieces dorees", 0xCCCCCCFF); y += 9;
    text_draw(g->renderer, 8, y, "Inventaire: 12 slots + 6 equipements (casque/torse/etc)", 0xCCCCCCFF); y += 9;
    text_draw(g->renderer, 8, y, "FUSION: marque 3 items identiques (M) puis F", 0xCCCCCCFF); y += 9;
    text_draw(g->renderer, 8, y, "Raretes: Commun < Magique < Rare < Epique < Legendaire (+100%)", 0xCCCCCCFF); y += 12;
    text_draw(g->renderer, 8, y, "ELEMENTS (sensibilites Pokemon-like):", 0xFFE080FF); y += 9;
    text_draw(g->renderer, 8, y, " EAU > FEU   FOUDRE > EAU   AIR > FOUDRE   TERRE > AIR", 0x80FFC0FF); y += 9;
    text_draw(g->renderer, 8, y, " VIDE <-> FEE (mutuels)   FEU > FEE   EAU > TERRE", 0x80FFC0FF); y += 12;
    text_draw(g->renderer, 8, y, "ELITES: brillent dans leur couleur. Frappe avec leur faiblesse", 0xFFC0C0FF); y += 9;
    text_draw(g->renderer, 8, y, "10 etages, 1 boss par etage, +15% stats ennemis a chaque etage", 0xFFC0C0FF); y += 9;
    text_draw(g->renderer, 8, INTERNAL_H - 12, "ECHAP POUR REVENIR", 0xFFFF80FF);
}

/* ---------- SHOP ---------- */
static const char *shop_kind_name(int k) {
    switch (k) {
        case 0: return "Coeur (+30 PV)";
        case 1: return "Armure +1 (perm)";
        case 2: return "+8% Degats (perm)";
        case 4: return "+15 PV Max (perm)";
        case 5: return "Equipement";
        default: return "?";
    }
}

void render_shop(Game *g) {
    fill_rect(g->renderer, 0, 0, INTERNAL_W, INTERNAL_H, 0x100818FF);
    /* candle decor */
    for (int i = 0; i < 30; i++) {
        int x = 60 + i * 12;
        int yy = 24 + (((int)(g->time * 6) + i) & 3);
        fill_rect(g->renderer, x, yy, 1, 2, 0xFF8030FF);
    }
    text_draw(g->renderer, INTERNAL_W/2 - text_width("BOUTIQUE DE PALIER")/2, 12,
              "BOUTIQUE DE PALIER", 0xFFE080FF);
    text_drawf(g->renderer, 8, 26, 0xFFD040FF, "PIECES %d", g->player.coins);
    text_drawf(g->renderer, 8, 36, 0xC0E0FFFF, "ETAGE %d -> %d", g->floor_index, g->floor_index + 1);

    int boxw = 88, boxh = 110, gap = 4;
    int total_w = 5 * boxw + 4 * gap;
    int sx = (INTERNAL_W - total_w) / 2;
    int sy = 50;
    for (int i = 0; i < 5; i++) {
        ShopItem *si = &g->shop_items[i];
        bool sel = (g->shop_cursor == i);
        fill_rect(g->renderer, sx, sy, boxw, boxh, sel ? 0x281828FF : 0x14101AFF);
        rect_outline(g->renderer, sx, sy, boxw, boxh, sel ? 0xFFFF40FF : 0x404048FF);
        text_drawf(g->renderer, sx + 4, sy + 4, sel ? 0xFFFF40FF : 0xFFFFFFFF, "[%d]", i + 1);
        if (si->kind == 5) {
            text_draw(g->renderer, sx + 4, sy + 16, rarity_name(si->item.rarity),
                      rarity_color(si->item.rarity));
            text_draw(g->renderer, sx + 4, sy + 26, slot_name(si->item.slot), 0xFFFFFFFF);
            text_drawf(g->renderer, sx + 4, sy + 36, 0xCCCCCCFF, "+%.1f", si->item.stat_value);
        } else {
            text_draw(g->renderer, sx + 4, sy + 18, shop_kind_name(si->kind), 0xFFFFFFFF);
        }
        if (si->bought) {
            text_draw(g->renderer, sx + 4, sy + boxh - 24, "ACHETE", 0x80FF80FF);
        } else {
            text_drawf(g->renderer, sx + 4, sy + boxh - 24, 0xFFD040FF, "%d", si->cost);
            fill_rect(g->renderer, sx + 4 + 12, sy + boxh - 23, 5, 5, 0xFFD040FF);
        }
        sx += boxw + gap;
    }

    if (g->inv_msg_t > 0.f)
        text_draw(g->renderer, INTERNAL_W/2 - text_width(g->inv_msg)/2,
                  INTERNAL_H - 40, g->inv_msg, 0xFFFF40FF);

    text_draw(g->renderer, INTERNAL_W/2 - text_width("A/D CHOISIR   ENTREE ACHETER   I INVENTAIRE")/2,
              INTERNAL_H - 28, "A/D CHOISIR   ENTREE ACHETER   I INVENTAIRE", 0xCCCCCCFF);
    text_draw(g->renderer, INTERNAL_W/2 - text_width("C OU ECHAP : ETAGE SUIVANT")/2,
              INTERNAL_H - 16, "C OU ECHAP : ETAGE SUIVANT", 0x80FF80FF);
}

/* ---------- INVENTORY ---------- */
static void render_item_slot(Game *g, int sx, int sy, Item *it, bool sel, bool marked) {
    uint32_t border = 0x404048FF;
    if (sel)    border = 0xFFFF40FF;
    if (marked) border = 0x80FF80FF;
    fill_rect(g->renderer, sx, sy, 26, 26, 0x14101AFF);
    rect_outline(g->renderer, sx, sy, 26, 26, border);
    if (it->occupied) {
        uint32_t col = rarity_color(it->rarity);
        fill_rect(g->renderer, sx + 4, sy + 4, 18, 18, col);
        rect_outline(g->renderer, sx + 4, sy + 4, 18, 18, 0x000000FF);
        const char *abbr = "?";
        switch (it->slot) {
            case SLOT_HELM:   abbr = "HE"; break;
            case SLOT_CHEST:  abbr = "TO"; break;
            case SLOT_LEGS:   abbr = "JA"; break;
            case SLOT_BOOTS:  abbr = "BO"; break;
            case SLOT_BELT:   abbr = "CE"; break;
            case SLOT_GLOVES: abbr = "GA"; break;
            default: break;
        }
        text_draw(g->renderer, sx + 8, sy + 9, abbr, 0x000000FF);
    }
}

void render_inventory(Game *g) {
    SDL_SetRenderDrawBlendMode(g->renderer, SDL_BLENDMODE_BLEND);
    fill_rect(g->renderer, 0, 0, INTERNAL_W, INTERNAL_H, 0x000000D0);
    SDL_SetRenderDrawBlendMode(g->renderer, SDL_BLENDMODE_NONE);
    text_draw(g->renderer, INTERNAL_W/2 - text_width("INVENTAIRE")/2, 6,
              "INVENTAIRE", 0xFFE080FF);

    /* 12 slots: 4x3 grid */
    int gridx = 30, gridy = 24;
    int cell = 28;
    text_draw(g->renderer, gridx, gridy - 9, "SAC (12)", 0xCCCCFFFF);
    for (int i = 0; i < INVENTORY_SLOTS; i++) {
        int row = i / 4, col = i % 4;
        int sx = gridx + col * cell;
        int sy = gridy + row * cell;
        bool sel = (g->inv_cursor == i);
        bool mk = false;
        for (int m = 0; m < g->inv_marked_count; m++)
            if (g->inv_marked[m] == i) mk = true;
        render_item_slot(g, sx, sy, &g->player.inventory[i], sel, mk);
    }

    /* equipment slots: vertical column on right */
    int eqx = INTERNAL_W - 60;
    int eqy = 24;
    text_draw(g->renderer, eqx, eqy - 9, "EQUIPEMENT", 0xCCCCFFFF);
    for (int i = 0; i < EQUIP_SLOTS; i++) {
        int sx = eqx;
        int sy = eqy + i * cell;
        bool sel = (g->inv_cursor == 12 + i);
        render_item_slot(g, sx, sy, &g->player.equipped[i], sel, false);
        text_draw(g->renderer, sx + 30, sy + 10, slot_name((EquipSlot)i), 0xCCCCCCFF);
    }

    /* details panel */
    int px = gridx + 4 * cell + 10;
    int py = gridy;
    fill_rect(g->renderer, px, py, INTERNAL_W - px - 80, 100, 0x14101AFF);
    rect_outline(g->renderer, px, py, INTERNAL_W - px - 80, 100, 0x404048FF);
    Item *it = NULL;
    if (g->inv_cursor < 12)        it = &g->player.inventory[g->inv_cursor];
    else if (g->inv_cursor < 18)   it = &g->player.equipped[g->inv_cursor - 12];
    if (it && it->occupied) {
        text_drawf(g->renderer, px + 4, py + 4, rarity_color(it->rarity),
                   "%s", rarity_name(it->rarity));
        text_drawf(g->renderer, px + 4, py + 14, 0xFFFFFFFF,
                   "%s", slot_name(it->slot));
        const char *unit = "";
        switch (it->slot) {
            case SLOT_HELM:   unit = "PV MAX"; break;
            case SLOT_CHEST:  unit = "ARMURE"; break;
            case SLOT_LEGS:   unit = "VITESSE"; break;
            case SLOT_BOOTS:  unit = "DASH-CD"; break;
            case SLOT_BELT:   unit = "REGEN/S"; break;
            case SLOT_GLOVES: unit = "DEGATS"; break;
            default: break;
        }
        if (it->slot == SLOT_GLOVES || it->slot == SLOT_BOOTS) {
            text_drawf(g->renderer, px + 4, py + 28, 0x80FFC0FF,
                       "+%.0f%% %s", it->stat_value * 100.f, unit);
        } else {
            text_drawf(g->renderer, px + 4, py + 28, 0x80FFC0FF,
                       "+%.1f %s", it->stat_value, unit);
        }
        text_drawf(g->renderer, px + 4, py + 40, 0xCCCCCCFF, "Variant %d", it->base_kind);
    } else {
        text_draw(g->renderer, px + 4, py + 4, "(slot vide)", 0x808080FF);
    }

    /* stats panel */
    int spy = gridy + 90;
    text_draw(g->renderer, gridx, spy, "STATS", 0xFFE080FF);
    Player *p = &g->player;
    text_drawf(g->renderer, gridx, spy + 10, 0xFFFFFFFF, "PV %d/%d", (int)p->hp, (int)p->maxhp);
    text_drawf(g->renderer, gridx, spy + 18, 0xFFFFFFFF, "Armure %.0f", p->armor);
    text_drawf(g->renderer, gridx, spy + 26, 0xFFFFFFFF, "Vitesse %.0f", p->speed);
    text_drawf(g->renderer, gridx, spy + 34, 0xFFFFFFFF, "Degats x%.2f", p->dmg_mul);
    text_drawf(g->renderer, gridx, spy + 42, 0xFFFFFFFF, "Regen %.1f /s", p->regen_per_sec);
    text_drawf(g->renderer, gridx, spy + 50, 0xFFFFFFFF, "Vol vie %.0f%%", p->lifesteal * 100.f);

    /* fusion preview */
    if (g->inv_marked_count > 0) {
        text_drawf(g->renderer, gridx + 90, spy, 0x80FF80FF,
                   "FUSION (%d/3)", g->inv_marked_count);
        if (g->inv_marked_count == 3) {
            int a = g->inv_marked[0];
            Item *base = &g->player.inventory[a];
            if (base->occupied && base->rarity < R_LEGENDARY) {
                text_drawf(g->renderer, gridx + 90, spy + 10, 0x80FF80FF,
                           "F = %s %s + 20%% stats",
                           rarity_name(base->rarity + 1), slot_name(base->slot));
            } else if (base->occupied) {
                text_draw(g->renderer, gridx + 90, spy + 10, "Deja max", 0xFFC080FF);
            }
        }
    }

    /* message */
    if (g->inv_msg_t > 0.f) {
        text_draw(g->renderer, INTERNAL_W/2 - text_width(g->inv_msg)/2,
                  INTERNAL_H - 28, g->inv_msg, 0xFFFF40FF);
    }
    text_draw(g->renderer, INTERNAL_W/2 - text_width("FLECHES NAVIGUER  E EQUIPER  M MARQUER  F FUSIONNER  X RAZ")/2,
              INTERNAL_H - 18, "FLECHES NAVIGUER  E EQUIPER  M MARQUER  F FUSIONNER  X RAZ", 0xCCCCCCFF);
    text_draw(g->renderer, INTERNAL_W/2 - text_width("ECHAP POUR FERMER")/2,
              INTERNAL_H - 8, "ECHAP POUR FERMER", 0xFFFF80FF);
}
