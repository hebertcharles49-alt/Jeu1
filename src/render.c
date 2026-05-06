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

static void draw_vignette(Game *g);   /* defini plus bas */

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

    /* ombre douce ovale */
    SDL_SetRenderDrawBlendMode(g->renderer, SDL_BLENDMODE_BLEND);
    fill_rect(g->renderer, sx - 7, sy + 6, 14, 2, 0x00000080);
    fill_rect(g->renderer, sx - 5, sy + 8, 10, 1, 0x00000060);
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

    /* leger bobbing en mouvement (Hades-like) */
    int bob = 0;
    if (p->vx * p->vx + p->vy * p->vy > 0.001f) {
        bob = ((int)(g->time * 12.f) & 1);
    } else {
        bob = (((int)(g->time * 4.f)) & 1) ? 0 : -1;
    }

    /* CAPE qui flotte avec direction de mouvement */
    int cape_off = (p->facing_dir == 0) ? -1 : (p->facing_dir == 2) ? 1 : 0;
    fill_rect(g->renderer, sx - 5 + cape_off, sy - 7 + bob, 10, 11, cape);
    fill_rect(g->renderer, sx - 5 + cape_off, sy - 7 + bob, 10, 2, 0x000000FF);

    /* TORSE / TUNIQUE */
    fill_rect(g->renderer, sx - 4, sy - 4 + bob, 8, 8, tunic);

    /* TETE */
    fill_rect(g->renderer, sx - 3, sy - 10 + bob, 6, 5, 0xE8C089FF);
    fill_rect(g->renderer, sx - 3, sy - 10 + bob, 6, 1, 0xC09060FF);  /* shadow rim */

    /* CHEVEUX (sera couvert par casque equipe) */
    if (!p->equipped[SLOT_HELM].occupied) {
        fill_rect(g->renderer, sx - 3, sy - 11 + bob, 6, 2, hair);
        fill_rect(g->renderer, sx - 3, sy - 12 + bob, 6, 1, hair);
    }

    /* YEUX */
    fill_rect(g->renderer, sx - 2, sy - 8 + bob, 1, 1, 0x000000FF);
    fill_rect(g->renderer, sx + 1, sy - 8 + bob, 1, 1, 0x000000FF);

    /* CEINTURE */
    if (p->equipped[SLOT_BELT].occupied)
        fill_rect(g->renderer, sx - 4, sy + 1 + bob, 8, 1, rarity_color(p->equipped[SLOT_BELT].rarity));
    else
        fill_rect(g->renderer, sx - 4, sy + 1 + bob, 8, 1, 0x202020FF);

    /* JAMBES */
    {
        uint32_t legcol = 0x402010FF;
        if (p->equipped[SLOT_LEGS].occupied) legcol = rarity_color(p->equipped[SLOT_LEGS].rarity);
        fill_rect(g->renderer, sx - 3, sy + 2 + bob, 2, 3, legcol);
        fill_rect(g->renderer, sx + 1, sy + 2 + bob, 2, 3, legcol);
    }

    /* BOTTES */
    {
        uint32_t bootcol = 0x180A04FF;
        if (p->equipped[SLOT_BOOTS].occupied) bootcol = rarity_color(p->equipped[SLOT_BOOTS].rarity);
        fill_rect(g->renderer, sx - 4, sy + 4 + bob, 3, 3, bootcol);
        fill_rect(g->renderer, sx + 1, sy + 4 + bob, 3, 3, bootcol);
        fill_rect(g->renderer, sx - 4, sy + 6 + bob, 3, 1, 0x000000FF);
        fill_rect(g->renderer, sx + 1, sy + 6 + bob, 3, 1, 0x000000FF);
    }

    /* CASQUE par dessus tete si equipe */
    if (p->equipped[SLOT_HELM].occupied) {
        uint32_t hc = rarity_color(p->equipped[SLOT_HELM].rarity);
        fill_rect(g->renderer, sx - 3, sy - 12 + bob, 6, 4, hc);
        fill_rect(g->renderer, sx - 3, sy - 12 + bob, 6, 1, 0x000000FF);
        /* visiere */
        fill_rect(g->renderer, sx - 3, sy - 9 + bob, 6, 1, 0x000000FF);
    }

    /* TORSE armor par dessus tunique */
    if (p->equipped[SLOT_CHEST].occupied) {
        uint32_t cc = rarity_color(p->equipped[SLOT_CHEST].rarity);
        fill_rect(g->renderer, sx - 4, sy - 4 + bob, 8, 6, cc);
        fill_rect(g->renderer, sx - 4, sy - 4 + bob, 8, 1, 0x000000FF);
        /* harnais V */
        fill_rect(g->renderer, sx - 1, sy - 3 + bob, 2, 4, 0x000000FF);
    }

    /* GANTS */
    {
        uint32_t gc = (p->equipped[SLOT_GLOVES].occupied) ?
                      rarity_color(p->equipped[SLOT_GLOVES].rarity) : 0xE8C089FF;
        fill_rect(g->renderer, sx - 6, sy + 0 + bob, 2, 2, gc);
        fill_rect(g->renderer, sx + 4, sy + 0 + bob, 2, 2, gc);
    }

    /* arme tenue : petit pictogramme dans la main droite */
    {
        Weapon *w = &p->weapons[p->active_weapon];
        int hx = sx + 5, hy = sy + 0 + bob;
        switch (w->kind) {
            case W_SWORD:  fill_rect(g->renderer, hx, hy - 5, 1, 5, 0xE8E8FFFF);
                           fill_rect(g->renderer, hx - 1, hy - 6, 3, 1, 0xC0C0FFFF); break;
            case W_AXE:    fill_rect(g->renderer, hx, hy - 4, 1, 4, 0x806040FF);
                           fill_rect(g->renderer, hx - 1, hy - 5, 3, 2, 0xC0C0C0FF); break;
            case W_BOW:    fill_rect(g->renderer, hx, hy - 5, 1, 5, 0x804020FF);
                           fill_rect(g->renderer, hx - 1, hy - 5, 3, 1, 0x804020FF);
                           fill_rect(g->renderer, hx - 1, hy - 1, 3, 1, 0x804020FF); break;
            case W_WAND:   fill_rect(g->renderer, hx, hy - 4, 1, 4, 0x402080FF);
                           fill_rect(g->renderer, hx - 1, hy - 5, 3, 1, 0xC080FFFF); break;
            case W_SHIELD: fill_rect(g->renderer, hx, hy - 3, 2, 5, 0xC0A060FF);
                           fill_rect(g->renderer, hx, hy - 3, 2, 1, 0x806030FF); break;
            default: break;
        }
    }

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
        int bw = (e->is_boss) ? 100 : (w + 2);
        int bx = sx - bw / 2;
        int by = (e->is_boss) ? (sy - w/2 - 12) : (sy - w/2 - 4);
        fill_rect(g->renderer, bx, by, bw, e->is_boss ? 3 : 1, 0x402020FF);
        int hf = (int)(bw * (e->hp / e->maxhp));
        fill_rect(g->renderer, bx, by, hf, e->is_boss ? 3 : 1, 0xFF4040FF);
        if (e->is_boss) rect_outline(g->renderer, bx-1, by-1, bw+2, 5, 0xFFD040FF);
    }
    /* nom (elites + boss) */
    if ((e->is_elite || e->is_boss) && e->name[0]) {
        int nw = text_width(e->name);
        int nx = sx - nw / 2;
        int ny = (e->is_boss) ? (sy - w/2 - 22) : (sy - w/2 - 12);
        /* halo */
        text_draw(g->renderer, nx + 1, ny + 1, e->name, 0x000000FF);
        uint32_t col = e->is_boss ? 0xFFD040FF : element_color(e->element);
        text_draw(g->renderer, nx, ny, e->name, col);
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
            fill_rect(r, sx, sy, TILE, TILE, 0x020106FF);
            break;
        case T_FLOOR: {
            /* dalle tres sombre style donjon Diablo 2 */
            uint32_t c = ((tx + ty) & 1) ? 0x140B1AFF : 0x0E0712FF;
            fill_rect(r, sx, sy, TILE, TILE, c);
            /* joint de dalle */
            fill_rect(r, sx, sy, TILE, 1, 0x07050AFF);
            fill_rect(r, sx, sy, 1, TILE, 0x07050AFF);
            /* grain */
            int hash = (tx * 7 + ty * 13) & 15;
            if (hash == 0) fill_rect(r, sx + 3, sy + 4, 1, 1, 0x281A30FF);
            if (hash == 3) fill_rect(r, sx + 11, sy + 9, 1, 1, 0x281A30FF);
            if (hash == 7) fill_rect(r, sx + 7, sy + 12, 2, 1, 0x1A1024FF);
            break;
        }
        case T_WALL:
            /* mur en pierre sombre, luminance haut > bas */
            fill_rect(r, sx, sy, TILE, TILE, 0x1F1828FF);
            fill_rect(r, sx, sy, TILE, 3, 0x342A40FF);
            fill_rect(r, sx, sy + TILE - 3, TILE, 3, 0x100918FF);
            /* cracks */
            fill_rect(r, sx + 4, sy + 5, 2, 2, 0x180F22FF);
            fill_rect(r, sx + 10, sy + 9, 2, 2, 0x180F22FF);
            fill_rect(r, sx + 7, sy + 11, 1, 2, 0x261A35FF);
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

    /* vignette ambiance dungeon */
    draw_vignette(g);

    /* aim cursor */
    int mx = g->mouse_x;
    int my = g->mouse_y;
    fill_rect(g->renderer, mx - 5, my, 4, 1, 0xFFFFFFFF);
    fill_rect(g->renderer, mx + 2, my, 4, 1, 0xFFFFFFFF);
    fill_rect(g->renderer, mx, my - 5, 1, 4, 0xFFFFFFFF);
    fill_rect(g->renderer, mx, my + 2, 1, 4, 0xFFFFFFFF);
    fill_rect(g->renderer, mx, my, 1, 1, 0xFFFFFFFF);

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
/* helpers extern (fournis par main.c -> shop_recipe etc) */
extern int  hub_perm_cost(int kind);
/* main.c uses static; redefine local labels for buttons */

static const char *perm_btn_label(int k) {
    switch (k) {
        case 0: return "+10 PV MAX";
        case 1: return "+1 ARMURE";
        case 2: return "+5 VITESSE";
        case 3: return "+5% DEGATS";
        default: return "?";
    }
}
static int perm_btn_cost(int k) {
    switch (k) {
        case 0: return 40;
        case 1: return 60;
        case 2: return 50;
        case 3: return 70;
        default: return 999;
    }
}

void render_hub(Game *g) {
    /* fond degrade */
    for (int yy = 0; yy < INTERNAL_H; yy++) {
        int v = 6 + (INTERNAL_H - yy) / 36;
        fill_rect(g->renderer, 0, yy, INTERNAL_W, 1,
                  (uint32_t)((v << 24) | ((v / 2) << 16) | ((v) << 8) | 0xFF));
    }
    /* embers */
    for (int i = 0; i < 70; i++) {
        int x = (i * 73 + (int)(g->time * 12)) % INTERNAL_W;
        int y = ((i * 37) + (int)(g->time * (i % 5 + 2) * 5)) % INTERNAL_H;
        fill_rect(g->renderer, x, y, 1, 1, (i & 3) ? 0x301820FF : 0xFFA060FF);
    }

    text_draw(g->renderer, INTERNAL_W/2 - text_width("LE SANCTUAIRE")/2, 10,
              "LE SANCTUAIRE", 0xFFE080FF);
    text_drawf(g->renderer, 8, 26, 0xC0E0FFFF,
               "ECLATS %d   COURSES %d   MEILLEUR %d/%d   VICTOIRES %d",
               g->meta.shards, g->meta.total_runs, g->meta.best_floor,
               MAX_FLOORS, g->meta.victories);
    text_draw(g->renderer, 8, 38,
              "ACHATS PERMANENTS - Augmente tes stats pour les futures courses",
              0x80FFC0FF);

    /* 4 boutons stats permanents */
    int boxw = 118, boxh = 50, gap = 6;
    int total_w = 4 * boxw + 3 * gap;
    int sx0 = (INTERNAL_W - total_w) / 2;
    int sy = 60;
    for (int i = 0; i < 4; i++) {
        int sx = sx0 + i * (boxw + gap);
        bool sel = (g->hub_cursor == i);
        bool ok = (g->meta.shards >= perm_btn_cost(i));
        fill_rect(g->renderer, sx, sy, boxw, boxh, sel ? 0x281828FF : 0x14101AFF);
        rect_outline(g->renderer, sx, sy, boxw, boxh,
                     sel ? 0xFFFF40FF : (ok ? 0x404048FF : 0x60303AFF));
        text_draw(g->renderer, sx + 8, sy + 8, perm_btn_label(i),
                  sel ? 0xFFFF40FF : 0xFFFFFFFF);
        text_drawf(g->renderer, sx + 8, sy + 24, 0xFFD040FF, "%d", perm_btn_cost(i));
        fill_rect(g->renderer, sx + 8 + 16, sy + 25, 5, 5, 0xFFD040FF);
        /* etat actuel */
        int cur = 0;
        switch (i) {
            case 0: cur = g->meta.perm_hp; break;
            case 1: cur = g->meta.perm_armor; break;
            case 2: cur = g->meta.perm_speed; break;
            case 3: cur = g->meta.perm_dmg_pct; break;
        }
        text_drawf(g->renderer, sx + 8, sy + 36, 0x80FFC0FF, "actuel +%d", cur);
    }

    /* CODEX */
    int cx = 24, cy = 124;
    text_draw(g->renderer, cx, cy, "CODEX  -  ARMES", 0xFFFF80FF);
    int discovered_w = 0;
    for (int i = 1; i < W_COUNT; i++) if (g->meta.weapon_discovered[i]) discovered_w++;
    text_drawf(g->renderer, cx + 130, cy, 0xCCCCCCFF, "%d / %d", discovered_w, W_COUNT - 1);
    cy += 12;
    for (int i = 1; i < W_COUNT; i++) {
        bool d = g->meta.weapon_discovered[i];
        const char *n = d ? weapon_name((WeaponKind)i) : "??????";
        uint32_t c = d ? 0xFFFFFFFF : 0x404040FF;
        text_drawf(g->renderer, cx, cy, c, "- %s", n);
        cy += 10;
    }

    int cx2 = INTERNAL_W / 2 + 20, cy2 = 124;
    text_draw(g->renderer, cx2, cy2, "CODEX  -  ELEMENTS", 0xFFFF80FF);
    int discovered_e = 0;
    for (int i = 1; i < EL_COUNT; i++) if (g->meta.element_discovered[i]) discovered_e++;
    text_drawf(g->renderer, cx2 + 160, cy2, 0xCCCCCCFF, "%d / %d", discovered_e, EL_COUNT - 1);
    cy2 += 12;
    for (int e = 1; e < EL_COUNT; e++) {
        bool d = g->meta.element_discovered[e];
        const char *n = d ? element_name((Element)e) : "??????";
        uint32_t c = d ? element_color((Element)e) : 0x404040FF;
        text_drawf(g->renderer, cx2, cy2, c, "- %s", n);
        cy2 += 10;
    }

    /* 3 boutons bas */
    int by = INTERNAL_H - 30;
    int bw = 120, bh = 16;
    int gx = INTERNAL_W/2 - (bw * 3 + 12) / 2;
    const char *labels[3] = { "[R] DEBUTER", "[O] OPTIONS", "[H] AIDE" };
    uint32_t col_active[3] = { 0x80FF80FF, 0xCCCCCCFF, 0xCCCCCCFF };
    for (int i = 0; i < 3; i++) {
        int x = gx + i * (bw + 6);
        bool hov = mouse_in_rect(g, x, by, bw, bh);
        fill_rect(g->renderer, x, by, bw, bh, hov ? 0x303060FF : 0x18181EFF);
        rect_outline(g->renderer, x, by, bw, bh, hov ? 0xFFFF80FF : 0x404048FF);
        const char *l = labels[i];
        text_draw(g->renderer, x + (bw - text_width(l)) / 2, by + 5, l,
                  hov ? 0xFFFF80FF : col_active[i]);
    }
    text_draw(g->renderer, INTERNAL_W/2 - text_width("ECHAP : QUITTER")/2,
              INTERNAL_H - 10, "ECHAP : QUITTER", 0x808080FF);
}

/* ---------- OPTIONS ---------- */
void render_options(Game *g) {
    fill_rect(g->renderer, 0, 0, INTERNAL_W, INTERNAL_H, 0x080612FF);
    text_draw(g->renderer, INTERNAL_W/2 - text_width("OPTIONS")/2, 8,
              "OPTIONS", 0xFFE080FF);

    /* tabs */
    const char *tabs[3] = { "CONTROLES", "AUDIO", "VIDEO" };
    int tabw = 100, tabh = 14;
    int tx = (INTERNAL_W - tabw * 3 - 8) / 2;
    for (int i = 0; i < 3; i++) {
        bool sel = (g->opt_section == i);
        fill_rect(g->renderer, tx, 22, tabw, tabh, sel ? 0x303060FF : 0x18181EFF);
        rect_outline(g->renderer, tx, 22, tabw, tabh, sel ? 0xFFFF80FF : 0x404048FF);
        text_draw(g->renderer, tx + (tabw - text_width(tabs[i])) / 2, 26,
                  tabs[i], sel ? 0xFFFF80FF : 0xCCCCCCFF);
        tx += tabw + 4;
    }

    int y = 50;
    if (g->opt_section == 0) {
        text_draw(g->renderer, 30, y, "ACTION", 0xCCCCCCFF);
        text_draw(g->renderer, 280, y, "TOUCHE", 0xCCCCCCFF);
        y += 12;
        for (int i = 0; i < BIND_COUNT; i++) {
            bool sel = (g->opt_cursor == i);
            uint32_t col = sel ? 0xFFFF40FF : 0xFFFFFFFF;
            text_drawf(g->renderer, sel ? 22 : 30, y, col, "%s%s",
                       sel ? "> " : "  ", bind_action_name((BindAction)i));
            const char *kn = scancode_label(g->settings.keys[i]);
            text_draw(g->renderer, 280, y, kn, col);
            y += 11;
        }
        y += 6;
        text_draw(g->renderer, 30, y, "WASD / FLECHES sont reserves au deplacement.", 0x808080FF); y += 9;
        text_draw(g->renderer, 30, y, "ENTREE = remapper   GAUCHE/DROITE = restaurer defaut", 0x808080FF);
    } else if (g->opt_section == 1) {
        const char *labels[2] = { "Couper le son (mute)", "Volume" };
        for (int i = 0; i < 2; i++) {
            bool sel = (g->opt_cursor == i);
            uint32_t col = sel ? 0xFFFF40FF : 0xFFFFFFFF;
            text_drawf(g->renderer, sel ? 22 : 30, y, col, "%s%s",
                       sel ? "> " : "  ", labels[i]);
            if (i == 0) {
                text_draw(g->renderer, 280, y,
                          g->settings.sfx_mute ? "ON" : "OFF", col);
            } else {
                /* volume bar */
                int bx = 280, bw = 80;
                fill_rect(g->renderer, bx, y, bw, 6, 0x202028FF);
                int filled = (g->settings.sfx_volume * bw) / 4;
                fill_rect(g->renderer, bx, y, filled, 6, sel ? 0xFFFF80FF : 0x80C0FFFF);
                rect_outline(g->renderer, bx, y, bw, 6, 0x404048FF);
                text_drawf(g->renderer, bx + bw + 6, y, col, "%d/4", g->settings.sfx_volume);
            }
            y += 11;
        }
        y += 6;
        text_draw(g->renderer, 30, y, "ENTREE bascule mute.   GAUCHE/DROITE ajuste le volume.", 0x808080FF);
    } else if (g->opt_section == 2) {
        bool sel = (g->opt_cursor == 0);
        uint32_t col = sel ? 0xFFFF40FF : 0xFFFFFFFF;
        text_drawf(g->renderer, sel ? 22 : 30, y, col, "%sDLSS Generatif",
                   sel ? "> " : "  ");
        text_draw(g->renderer, 280, y,
                  g->settings.dlss_on ? "ON  (lisse)" : "OFF (pixel art net)", col);
        y += 14;
        text_draw(g->renderer, 30, y, "Filtrage lineaire AI-like sur la sortie finale.", 0x808080FF); y += 9;
        text_draw(g->renderer, 30, y, "Recommande OFF pour garder le pixel art bien net.", 0x808080FF); y += 9;
        text_draw(g->renderer, 30, y, "ENTREE bascule.", 0x808080FF);
    }

    if (g->opt_waiting_rebind) {
        SDL_SetRenderDrawBlendMode(g->renderer, SDL_BLENDMODE_BLEND);
        fill_rect(g->renderer, 0, INTERNAL_H/2 - 16, INTERNAL_W, 32, 0x000000C0);
        SDL_SetRenderDrawBlendMode(g->renderer, SDL_BLENDMODE_NONE);
        const char *msg = "APPUIE SUR UNE TOUCHE...   (ECHAP POUR ANNULER)";
        text_draw(g->renderer, INTERNAL_W/2 - text_width(msg)/2,
                  INTERNAL_H/2 - 4, msg, 0xFFFF40FF);
    }

    if (g->opt_msg_t > 0.f) {
        text_draw(g->renderer, INTERNAL_W/2 - text_width(g->opt_msg)/2,
                  INTERNAL_H - 32, g->opt_msg, 0xFFFF40FF);
    }
    text_draw(g->renderer, INTERNAL_W/2 - text_width("Q/TAB SECTION   FLECHES NAVIGUER   ECHAP RETOUR")/2,
              INTERNAL_H - 18, "Q/TAB SECTION   FLECHES NAVIGUER   ECHAP RETOUR", 0xCCCCCCFF);
}

/* ---------- HERO SELECT ---------- */
static void hero_palette(HeroClass h, uint32_t *cape, uint32_t *tunic) {
    *cape = 0x303040FF; *tunic = 0x6A4A2AFF;
    switch (h) {
        case HERO_GUERRIER:  *cape = 0x802020FF; *tunic = 0x707080FF; break;
        case HERO_VOLEUR:    *cape = 0x305030FF; *tunic = 0x202028FF; break;
        case HERO_MAGE:      *cape = 0x402070FF; *tunic = 0x6040A0FF; break;
        case HERO_BERSERKER: *cape = 0x202020FF; *tunic = 0x803020FF; break;
        case HERO_PALADIN:   *cape = 0xFFD040FF; *tunic = 0xC0C0D0FF; break;
        case HERO_DRUIDE:    *cape = 0x305020FF; *tunic = 0x60804030; break;
        case HERO_ASSASSIN:  *cape = 0x101018FF; *tunic = 0x301030FF; break;
        case HERO_RANGER:    *cape = 0x405028FF; *tunic = 0x806030FF; break;
        case HERO_TEMPLIER:  *cape = 0xC0C0C8FF; *tunic = 0x808088FF; break;
        case HERO_NECROMANT: *cape = 0x202840FF; *tunic = 0x303060FF; break;
        default: break;
    }
}

static void draw_hero_portrait(Game *g, int sx, int sy, HeroClass h,
                               bool selected, bool discovered) {
    uint32_t cape, tunic;
    hero_palette(h, &cape, &tunic);
    if (!discovered) { cape = 0x101010FF; tunic = 0x202020FF; }
    if (selected) rect_outline(g->renderer, sx - 22, sy - 30, 44, 60, 0xFFFF40FF);
    fill_rect(g->renderer, sx - 14, sy - 16, 28, 32, cape);
    fill_rect(g->renderer, sx - 12, sy - 8, 24, 22, tunic);
    fill_rect(g->renderer, sx - 8, sy - 22, 16, 12,
              discovered ? 0xE8C089FF : 0x303030FF);
    fill_rect(g->renderer, sx - 8, sy - 24, 16, 4,
              discovered ? 0x402010FF : 0x101010FF);
    if (discovered) {
        fill_rect(g->renderer, sx - 5, sy - 18, 3, 3, 0x000000FF);
        fill_rect(g->renderer, sx + 2, sy - 18, 3, 3, 0x000000FF);
    } else {
        /* point d'interrogation gigantesque */
        text_draw(g->renderer, sx - 3, sy - 22, "?", 0x808080FF);
    }
}

void render_choose_hero(Game *g) {
    /* fond degrade */
    for (int yy = 0; yy < INTERNAL_H; yy++) {
        int v = 6 + (INTERNAL_H - yy) / 32;
        fill_rect(g->renderer, 0, yy, INTERNAL_W, 1,
                  (uint32_t)((v << 24) | (v << 16) | ((v + 4) << 8) | 0xFF));
    }
    text_draw(g->renderer, INTERNAL_W/2 - text_width("CHOISIS TON HEROS")/2, 10,
              "CHOISIS TON HEROS", 0xFFE080FF);
    text_drawf(g->renderer, 8, 24, 0xC0E0FFFF, "ECLATS %d", g->meta.shards);

    int discovered_n = 0;
    for (int i = 0; i < HERO_COUNT; i++) if (g->meta.hero_discovered[i]) discovered_n++;
    text_drawf(g->renderer, INTERNAL_W - 110, 24, 0xCCCCCCFF,
               "DECOUVERTS %d/%d", discovered_n, HERO_COUNT);

    /* 2 rangees de 5 portraits */
    int per_row = 5;
    int gap_x = INTERNAL_W / (per_row + 1);
    int row_y[2] = { (INTERNAL_H * 4) / 12, (INTERNAL_H * 8) / 12 };
    for (int i = 0; i < HERO_COUNT; i++) {
        int row = i / per_row;
        int col = i % per_row;
        int sx = gap_x * (col + 1);
        int sy = row_y[row];
        bool sel = (g->hero_cursor == i);
        bool disc = g->meta.hero_discovered[i];
        draw_hero_portrait(g, sx, sy, (HeroClass)i, sel, disc);
        const char *n = disc ? hero_name((HeroClass)i) : "??????";
        uint32_t c = disc ? (sel ? 0xFFFF40FF : 0xFFFFFFFF) : 0x606060FF;
        text_draw(g->renderer, sx - text_width(n) / 2, sy + 22, n, c);
        if (disc && !g->meta.hero_unlocked[i]) {
            int cost = 60 + i * 25;
            char b[24]; snprintf(b, sizeof(b), "%d ECLATS", cost);
            text_draw(g->renderer, sx - text_width(b) / 2, sy + 32, b, 0xFFC080FF);
        }
    }

    /* description */
    HeroClass cur = (HeroClass)g->hero_cursor;
    if (g->meta.hero_discovered[cur]) {
        text_draw(g->renderer, INTERNAL_W/2 - text_width(hero_desc(cur))/2,
                  INTERNAL_H - 56, hero_desc(cur), 0xCCCCFFFF);
        if (!g->meta.hero_unlocked[cur]) {
            text_draw(g->renderer,
                      INTERNAL_W/2 - text_width("VERROUILLE - CLIC POUR ACHETER")/2,
                      INTERNAL_H - 42, "VERROUILLE - CLIC POUR ACHETER", 0xFFC080FF);
        } else {
            text_draw(g->renderer,
                      INTERNAL_W/2 - text_width("CLIC OU ENTREE POUR PARTIR EN COURSE")/2,
                      INTERNAL_H - 42, "CLIC OU ENTREE POUR PARTIR EN COURSE", 0x80FF80FF);
        }
    } else {
        text_draw(g->renderer,
                  INTERNAL_W/2 - text_width("HEROS INCONNU - terrasse un boss pour le reveler")/2,
                  INTERNAL_H - 42,
                  "HEROS INCONNU - terrasse un boss pour le reveler",
                  0x808080FF);
    }
    text_draw(g->renderer, INTERNAL_W/2 - text_width("A/D NAVIGUER   SOURIS HOVER/CLIC   ECHAP RETOUR")/2,
              INTERNAL_H - 18, "A/D NAVIGUER   SOURIS HOVER/CLIC   ECHAP RETOUR",
              0xCCCCCCFF);
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
    /* fond degrade vertical sombre */
    for (int y = 0; y < INTERNAL_H; y++) {
        int v = 8 + (INTERNAL_H - y) / 24;
        fill_rect(g->renderer, 0, y, INTERNAL_W, 1, (uint32_t)((v << 24) | (v / 2 << 16) | (v << 8) | 0xFF));
    }
    /* particules embers / cendres animees */
    for (int i = 0; i < 100; i++) {
        int x = (i * 73 + (int)(g->time * 14)) % INTERNAL_W;
        int y = ((i * 37) + (int)(g->time * (i % 7 + 2) * 6)) % INTERNAL_H;
        uint32_t col = (i & 3) ? 0x402030FF : 0xFFB060FF;
        fill_rect(g->renderer, x, y, 1, 1, col);
    }

    /* titre chevele : ombre + coeur + glow */
    const char *t = "ELEMENT DUNGEON";
    int tw = text_width(t);
    int tx = INTERNAL_W/2 - tw/2;
    int ty = INTERNAL_H/2 - 60;
    /* glow */
    for (int dx = -2; dx <= 2; dx++) for (int dy = -2; dy <= 2; dy++) {
        if (!dx && !dy) continue;
        text_draw(g->renderer, tx + dx, ty + dy, t, 0x402010FF);
    }
    text_draw(g->renderer, tx, ty + 1, t, 0x000000FF);
    text_draw(g->renderer, tx, ty, t, 0xFFD060FF);

    text_draw(g->renderer, INTERNAL_W/2 - text_width("DOOM x HADES x ISAAC x DIABLO")/2,
              INTERNAL_H/2 - 38, "DOOM x HADES x ISAAC x DIABLO", 0xC0A080FF);

    /* menu vertical */
    const char *items[5] = { "JOUER", "LORE", "OPTIONS", "AIDE", "QUITTER" };
    int yA = INTERNAL_H/2 + 20;
    int rowh = 16;
    for (int i = 0; i < 5; i++) {
        int yi = yA + i * rowh;
        bool hov = mouse_in_rect(g, INTERNAL_W/2 - 100, yi, 200, 12);
        uint32_t col = hov ? 0xFFFF80FF : (i == 0 ? 0x80FFA0FF : 0xCCCCCCFF);
        int w = text_width(items[i]);
        text_draw(g->renderer, INTERNAL_W/2 - w/2, yi + 2, items[i], col);
        if (hov) {
            /* fleches */
            text_draw(g->renderer, INTERNAL_W/2 - w/2 - 16, yi + 2, ">", col);
            text_draw(g->renderer, INTERNAL_W/2 + w/2 + 10, yi + 2, "<", col);
        }
    }

    text_draw(g->renderer, 8, INTERNAL_H - 14, "v1 -- C + SDL2", 0x606080FF);
}

/* ---------- LORE ---------- */
void render_lore(Game *g) {
    /* fond noir avec etoiles bleutees */
    fill_rect(g->renderer, 0, 0, INTERNAL_W, INTERNAL_H, 0x040208FF);
    for (int i = 0; i < 80; i++) {
        int x = (i * 53 + (int)(g->time * 6)) % INTERNAL_W;
        int y = ((i * 91) ) % INTERNAL_H;
        fill_rect(g->renderer, x, y, 1, 1, ((i % 3) ? 0x404068FF : 0x80A0E0FF));
    }
    text_draw(g->renderer, INTERNAL_W/2 - text_width("LE PROLOGUE")/2, 18,
              "LE PROLOGUE", 0xFFD060FF);

    const char *paragraphs[] = {
        "Il y a sept mille ans, le Cristal Originel se brisa dans le ciel.",
        "Sept eclats tomberent dans l'abime. Chacun devint un element :",
        "FEU, EAU, TERRE, FOUDRE, AIR, VIDE et FEE.",
        "",
        "Les rois batirent un Donjon pour les contenir. Sous terre, dix",
        "etages, dix cles, dix Gardiens. Le temps fit son oeuvre :",
        "les murs furent oublies, les eclats endormis sous la mousse.",
        "",
        "La porte vient de s'ouvrir. Quelque chose remonte. Les Gardiens",
        "se reveillent. Les ennemis qui te traquent portent les couleurs",
        "des elements -- frappe-les avec leur faiblesse.",
        "",
        "Toi, Heros sans nom : descend. Combine les eclats. Forge ton",
        "arme, ta classe, ta legende. Ou meurs comme tous les autres.",
        "",
        "                            *      *      *",
    };
    int n = (int)(sizeof(paragraphs) / sizeof(paragraphs[0]));
    int y = 50;
    for (int i = 0; i < n; i++) {
        int w = text_width(paragraphs[i]);
        uint32_t col = 0xCCCCDDFF;
        if (i == 2 || i == 9 || i == 10) col = 0xFFD0A0FF;
        text_draw(g->renderer, INTERNAL_W/2 - w/2, y, paragraphs[i], col);
        y += 11;
    }
    text_draw(g->renderer, INTERNAL_W/2 - text_width("ENTREE / CLIC / ECHAP POUR REVENIR")/2,
              INTERNAL_H - 18, "ENTREE / CLIC / ECHAP POUR REVENIR", 0xFFFF80FF);
}

/* ---------- VIGNETTE ---------- */
static void draw_vignette(Game *g) {
    SDL_SetRenderDrawBlendMode(g->renderer, SDL_BLENDMODE_BLEND);
    int n = 24;
    for (int i = 0; i < n; i++) {
        int alpha = 110 - i * 4;
        if (alpha < 0) alpha = 0;
        SDL_SetRenderDrawColor(g->renderer, 0, 0, 6, (Uint8)alpha);
        SDL_Rect t = { i, i, INTERNAL_W - i*2, 1 };
        SDL_Rect b = { i, INTERNAL_H - 1 - i, INTERNAL_W - i*2, 1 };
        SDL_Rect l = { i, i, 1, INTERNAL_H - i*2 };
        SDL_Rect r = { INTERNAL_W - 1 - i, i, 1, INTERNAL_H - i*2 };
        SDL_RenderFillRect(g->renderer, &t);
        SDL_RenderFillRect(g->renderer, &b);
        SDL_RenderFillRect(g->renderer, &l);
        SDL_RenderFillRect(g->renderer, &r);
    }
    SDL_SetRenderDrawBlendMode(g->renderer, SDL_BLENDMODE_NONE);
}

/* ---------- HELP ---------- */
void render_help(Game *g) {
    fill_rect(g->renderer, 0, 0, INTERNAL_W, INTERNAL_H, 0x080612FF);
    text_draw(g->renderer, 8, 6, "AIDE", 0xFFE080FF);
    int y = 18;
    text_draw(g->renderer, 8, y, "WASD / FLECHES   DEPLACEMENT", 0xFFFFFFFF); y += 9;
    text_draw(g->renderer, 8, y, "SOURIS           VISER + naviguer/cliquer dans les menus", 0xFFFFFFFF); y += 9;
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

/* ---------- SHOP (Brotato-like) ---------- */
void render_shop(Game *g) {
    /* fond degrade ambiance taverne */
    for (int y = 0; y < INTERNAL_H; y++) {
        int v = 12 + (INTERNAL_H - y) / 28;
        fill_rect(g->renderer, 0, y, INTERNAL_W, 1,
                  (uint32_t)((v << 24) | ((v / 2) << 16) | ((v / 3) << 8) | 0xFF));
    }
    /* particules braise */
    for (int i = 0; i < 60; i++) {
        int x = (i * 73 + (int)(g->time * 16)) % INTERNAL_W;
        int y = ((i * 91) + (int)(g->time * (i % 5 + 2) * 4)) % INTERNAL_H;
        uint32_t col = (i & 3) ? 0x402030FF : 0xFFB060FF;
        fill_rect(g->renderer, x, y, 1, 1, col);
    }

    text_draw(g->renderer, INTERNAL_W/2 - text_width("MARCHE DE PALIER")/2, 14,
              "MARCHE DE PALIER", 0xFFE080FF);
    text_drawf(g->renderer, 8, 30, 0xFFD040FF, "PIECES %d", g->player.coins);
    text_drawf(g->renderer, 8, 40, 0xC0E0FFFF, "ETAGE %d -> %d",
               g->floor_index, g->floor_index + 1);
    text_drawf(g->renderer, INTERNAL_W - 110, 30, 0x80FFC0FF,
               "ACHATS  %d", g->player.shop_purchased_count);

    int boxw = 130, boxh = 150, gap = 8;
    int total_w = SHOP_SLOTS * boxw + (SHOP_SLOTS - 1) * gap;
    int sx0 = (INTERNAL_W - total_w) / 2;
    int sy = 56;
    for (int i = 0; i < SHOP_SLOTS; i++) {
        ShopItem *si = &g->shop_items[i];
        int sx = sx0 + i * (boxw + gap);
        bool sel = (g->shop_cursor == i);
        bool affordable = (g->player.coins >= si->cost);
        uint32_t border = sel ? 0xFFFF40FF : (affordable ? 0x404048FF : 0x60303AFF);
        fill_rect(g->renderer, sx, sy, boxw, boxh, sel ? 0x1A1422FF : 0x10080FFF);
        rect_outline(g->renderer, sx, sy, boxw, boxh, border);

        /* badge rarete couleur */
        uint32_t rcol = shop_recipe_color(si->recipe_id);
        fill_rect(g->renderer, sx + 4, sy + 4, boxw - 8, 2, rcol);
        fill_rect(g->renderer, sx, sy, 6, boxh, rcol);

        /* nom */
        text_draw(g->renderer, sx + 10, sy + 12, shop_recipe_name(si->recipe_id), rcol);
        /* desc en multi-ligne grossier (decoupe a l'espace si trop long) */
        const char *desc = shop_recipe_desc(si->recipe_id);
        int max_chars = (boxw - 16) / 6;
        char line[64];
        int desc_y = sy + 26;
        const char *p = desc;
        while (*p && desc_y < sy + boxh - 28) {
            int n = 0;
            while (p[n] && n < max_chars && p[n] != '\n') n++;
            /* coupe a un espace si possible */
            if (p[n] && n == max_chars) {
                int back = n;
                while (back > 0 && p[back] != ' ') back--;
                if (back > 0) n = back;
            }
            int len = n; if (len > 60) len = 60;
            memcpy(line, p, len); line[len] = 0;
            text_draw(g->renderer, sx + 10, desc_y, line, 0xCCCCDDFF);
            desc_y += 9;
            p += n;
            while (*p == ' ') p++;
        }

        /* prix / status */
        int by = sy + boxh - 18;
        if (si->bought) {
            text_draw(g->renderer, sx + 10, by, "ACHETE", 0x80FF80FF);
        } else {
            uint32_t pcol = affordable ? 0xFFD040FF : 0x806020FF;
            text_drawf(g->renderer, sx + 10, by, pcol, "%d", si->cost);
            fill_rect(g->renderer, sx + 10 + 22, by + 1, 5, 5, pcol);
            if (sel) text_draw(g->renderer, sx + boxw - 56, by, "[ACHETER]",
                              affordable ? 0x80FF80FF : 0x808080FF);
        }
    }

    /* bouton REROLL */
    int rrw = 110, rrh = 18;
    int rrx = (INTERNAL_W - rrw) / 2;
    int rry = sy + boxh + 8;
    bool rrhov = mouse_in_rect(g, rrx, rry, rrw, rrh);
    bool rrok  = (g->player.coins >= g->shop_reroll_cost);
    fill_rect(g->renderer, rrx, rry, rrw, rrh, rrhov ? 0x303060FF : 0x18181EFF);
    rect_outline(g->renderer, rrx, rry, rrw, rrh, rrhov ? 0xFFFF80FF : 0x404048FF);
    text_drawf(g->renderer, rrx + 8, rry + 6,
               rrok ? (rrhov ? 0xFFFF80FF : 0xCCCCCCFF) : 0x808080FF,
               "REROLL  %d", g->shop_reroll_cost);
    fill_rect(g->renderer, rrx + 8 + text_width("REROLL  ") + 6, rry + 7, 5, 5,
              rrok ? 0xFFD040FF : 0x806020FF);

    if (g->inv_msg_t > 0.f)
        text_draw(g->renderer, INTERNAL_W/2 - text_width(g->inv_msg)/2,
                  INTERNAL_H - 38, g->inv_msg, 0xFFFF40FF);

    text_draw(g->renderer, INTERNAL_W/2 - text_width("CLIC ACHETER  R REROLL  I INVENTAIRE")/2,
              INTERNAL_H - 26, "CLIC ACHETER  R REROLL  I INVENTAIRE", 0xCCCCCCFF);
    int fbx = INTERNAL_W/2 - 100, fby = INTERNAL_H - 14;
    bool fbhov = mouse_in_rect(g, fbx, fby, 200, 12);
    text_draw(g->renderer, INTERNAL_W/2 - text_width("[ESPACE / CLIC] ETAGE SUIVANT")/2,
              fby + 1, "[ESPACE / CLIC] ETAGE SUIVANT",
              fbhov ? 0xFFFF80FF : 0x80FF80FF);
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
