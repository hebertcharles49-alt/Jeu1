/*
 * render.c - UI 2D + rendu 3D voxel
 *
 * UI : reutilise les helpers (fill_rect, text_draw, ...) qui dispatchent
 * vers le batcher GL (gfx.c). Tous les ecrans (titre, hub, options, shop,
 * inventaire) sont en passe ortho.
 *
 * Monde : render_world genere un mesh voxel a partir du donjon (heightmap
 * murs / sol) et dessine joueur, ennemis, projectiles, particules en
 * cubes 3D ou billboards via gfx_box_draw / gfx_billboard_draw.
 */
#include "game.h"
#include "gfx.h"
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

static void draw_vignette(Game *g);   /* defini plus bas */

/* ---------- HELPERS UI 2D ----------
 * Dispatch vers le batcher GL. La signature reste GfxCtx* pour que
 * tous les sites d'appel (g->renderer) continuent a compiler. */
#if defined(__GNUC__) || defined(__clang__)
#define MAYBE_UNUSED __attribute__((unused))
#else
#define MAYBE_UNUSED
#endif

MAYBE_UNUSED static void set_color_u32(GfxCtx *r, uint32_t c) {
    gfx_set_color(r, c);
}

static void fill_rect(GfxCtx *r, int x, int y, int w, int h, uint32_t c) {
    gfx_set_color(r, c);
    gfx_fill_rect(r, x, y, w, h);
}

MAYBE_UNUSED static void draw_disk(GfxCtx *r, int cx, int cy, int radius, uint32_t c) {
    gfx_set_color(r, c);
    for (int dy = -radius; dy <= radius; dy++) {
        int dx = (int)sqrtf((float)(radius*radius - dy*dy));
        gfx_fill_rect(r, cx - dx, cy + dy, dx*2 + 1, 1);
    }
}

MAYBE_UNUSED static void draw_ring(GfxCtx *r, int cx, int cy, int radius, uint32_t c) {
    gfx_set_color(r, c);
    int n = 48;
    for (int i = 0; i < n; i++) {
        float a = (i / (float)n) * 6.2831f;
        int x = cx + (int)(cosf(a) * radius);
        int y = cy + (int)(sinf(a) * radius);
        gfx_fill_rect(r, x, y, 1, 1);
    }
}

static void rect_outline(GfxCtx *r, int x, int y, int w, int h, uint32_t c) {
    fill_rect(r, x, y, w, 1, c);
    fill_rect(r, x, y+h-1, w, 1, c);
    fill_rect(r, x, y, 1, h, c);
    fill_rect(r, x+w-1, y, 1, h, c);
}

/* ============================================================
 *  RENDU 3D VOXEL
 * ============================================================ */

/* projette un point monde vers ecran (FBO interne) */
static bool world_to_screen(GfxCtx *gc, v3 world, int *out_sx, int *out_sy) {
    /* multiplie par view puis proj */
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

/* ----------------------------------------------------------------
 *  generation du mesh voxel a partir du donjon (heightmap)
 * ---------------------------------------------------------------- */

#define WALL_H 2.0f          /* hauteur des murs en blocks */
#define MAX_VERTS_PER_FRAME (1024 * 1024)   /* 1M verts max */

static float *s_mesh_buf = NULL;
static int    s_mesh_cap = 0;
static int    s_mesh_count = 0;

static void mesh_reserve(int n_verts_min) {
    if (n_verts_min <= s_mesh_cap) return;
    int new_cap = s_mesh_cap ? s_mesh_cap * 2 : 16384;
    while (new_cap < n_verts_min) new_cap *= 2;
    s_mesh_buf = (float *)realloc(s_mesh_buf, (size_t)new_cap * 9 * sizeof(float));
    s_mesh_cap = new_cap;
}

static void mesh_push_vert(float x, float y, float z,
                           float nx, float ny, float nz,
                           float r, float g, float b) {
    mesh_reserve(s_mesh_count + 1);
    float *p = &s_mesh_buf[s_mesh_count * 9];
    p[0]=x; p[1]=y; p[2]=z;
    p[3]=nx; p[4]=ny; p[5]=nz;
    p[6]=r; p[7]=g; p[8]=b;
    s_mesh_count++;
}

/* emet un quad face avec 2 triangles, normale et couleur uniforme */
static void mesh_push_quad(v3 a, v3 b, v3 c, v3 d, v3 n, float r, float g, float bl) {
    mesh_push_vert(a.x,a.y,a.z, n.x,n.y,n.z, r,g,bl);
    mesh_push_vert(b.x,b.y,b.z, n.x,n.y,n.z, r,g,bl);
    mesh_push_vert(c.x,c.y,c.z, n.x,n.y,n.z, r,g,bl);
    mesh_push_vert(a.x,a.y,a.z, n.x,n.y,n.z, r,g,bl);
    mesh_push_vert(c.x,c.y,c.z, n.x,n.y,n.z, r,g,bl);
    mesh_push_vert(d.x,d.y,d.z, n.x,n.y,n.z, r,g,bl);
}

/* couleurs par tile pour la face top */
static void tile_color_top(TileKind t, float *r, float *g, float *b) {
    switch (t) {
        case T_FLOOR:       *r=0.10f; *g=0.07f; *b=0.13f; break;
        case T_BLOOD:       *r=0.30f; *g=0.07f; *b=0.10f; break;
        case T_BONES:       *r=0.65f; *g=0.62f; *b=0.55f; break;
        case T_RUNE:        *r=0.55f; *g=0.30f; *b=0.85f; break;
        case T_TORCH:       *r=0.20f; *g=0.13f; *b=0.16f; break;
        case T_EXIT:        *r=0.30f; *g=0.50f; *b=0.95f; break;
        case T_HAZARD_LAVA: *r=0.95f; *g=0.40f; *b=0.10f; break;
        case T_HAZARD_WATER:*r=0.20f; *g=0.40f; *b=0.85f; break;
        case T_DOOR:        *r=0.40f; *g=0.20f; *b=0.10f; break;
        default:            *r=0.10f; *g=0.07f; *b=0.13f; break;
    }
}
/* couleurs murs (faces verticales) */
static void wall_color_side(float *r, float *g, float *b) {
    *r = 0.13f; *g = 0.10f; *b = 0.18f;
}
static void wall_color_top(float *r, float *g, float *b) {
    *r = 0.18f; *g = 0.14f; *b = 0.24f;
}

MAYBE_UNUSED static bool tile_is_floor(TileKind t) {
    return t != T_VOID && t != T_WALL;
}

static void emit_wall_column(float x, float z) {
    float x0 = x, x1 = x + 1.f;
    float z0 = z, z1 = z + 1.f;
    float y0 = 0.f, y1 = WALL_H;
    float r, g, b; wall_color_side(&r, &g, &b);
    /* +X face */
    mesh_push_quad(v3_make(x1,y0,z0), v3_make(x1,y1,z0), v3_make(x1,y1,z1), v3_make(x1,y0,z1),
                   v3_make(1,0,0), r,g,b);
    /* -X face */
    mesh_push_quad(v3_make(x0,y0,z1), v3_make(x0,y1,z1), v3_make(x0,y1,z0), v3_make(x0,y0,z0),
                   v3_make(-1,0,0), r,g,b);
    /* +Z face */
    mesh_push_quad(v3_make(x0,y0,z1), v3_make(x1,y0,z1), v3_make(x1,y1,z1), v3_make(x0,y1,z1),
                   v3_make(0,0,1), r*0.9f,g*0.9f,b*0.9f);
    /* -Z face */
    mesh_push_quad(v3_make(x1,y0,z0), v3_make(x0,y0,z0), v3_make(x0,y1,z0), v3_make(x1,y1,z0),
                   v3_make(0,0,-1), r*1.1f,g*1.1f,b*1.1f);
    /* top (lit) */
    float tr, tg, tb; wall_color_top(&tr, &tg, &tb);
    mesh_push_quad(v3_make(x0,y1,z0), v3_make(x0,y1,z1), v3_make(x1,y1,z1), v3_make(x1,y1,z0),
                   v3_make(0,1,0), tr,tg,tb);
}

static void emit_floor_top(float x, float z, TileKind t) {
    float x0 = x, x1 = x + 1.f;
    float z0 = z, z1 = z + 1.f;
    float y = 0.f;
    float r, g, b; tile_color_top(t, &r, &g, &b);
    mesh_push_quad(v3_make(x0,y,z0), v3_make(x0,y,z1), v3_make(x1,y,z1), v3_make(x1,y,z0),
                   v3_make(0,1,0), r,g,b);
}

static void build_dungeon_mesh(Game *g) {
    s_mesh_count = 0;
    Dungeon *d = &g->dungeon;
    for (int y = 0; y < MAP_H; y++) {
        for (int x = 0; x < MAP_W; x++) {
            TileKind t = d->tiles[y][x];
            if (t == T_VOID) continue;
            if (t == T_WALL) {
                emit_wall_column((float)x, (float)y);
            } else {
                emit_floor_top((float)x, (float)y, t);
            }
        }
    }
    gfx_terrain_upload(g->renderer, s_mesh_buf, s_mesh_count);
}

/* ----------------------------------------------------------------
 *  rendu d'entites (cubes-stack a la Minecraft)
 * ---------------------------------------------------------------- */

static void hero_color(HeroClass h, float *r, float *g, float *b,
                       float *r2, float *g2, float *b2) {
    /* corps + casque/cape */
    *r=0.42f; *g=0.29f; *b=0.16f; *r2=0.19f; *g2=0.19f; *b2=0.25f;
    switch (h) {
        case HERO_GUERRIER:  *r=0.44f;*g=0.44f;*b=0.50f; *r2=0.50f;*g2=0.13f;*b2=0.13f; break;
        case HERO_VOLEUR:    *r=0.13f;*g=0.13f;*b=0.16f; *r2=0.19f;*g2=0.31f;*b2=0.19f; break;
        case HERO_MAGE:      *r=0.38f;*g=0.25f;*b=0.63f; *r2=0.25f;*g2=0.13f;*b2=0.44f; break;
        case HERO_BERSERKER: *r=0.50f;*g=0.19f;*b=0.13f; *r2=0.13f;*g2=0.13f;*b2=0.13f; break;
        case HERO_PALADIN:   *r=0.75f;*g=0.75f;*b=0.81f; *r2=1.00f;*g2=0.81f;*b2=0.25f; break;
        case HERO_DRUIDE:    *r=0.31f;*g=0.50f;*b=0.13f; *r2=0.50f;*g2=0.32f;*b2=0.13f; break;
        case HERO_ASSASSIN:  *r=0.13f;*g=0.07f;*b=0.16f; *r2=0.07f;*g2=0.07f;*b2=0.13f; break;
        case HERO_RANGER:    *r=0.50f;*g=0.38f;*b=0.19f; *r2=0.31f;*g2=0.38f;*b2=0.13f; break;
        case HERO_TEMPLIER:  *r=0.50f;*g=0.50f;*b=0.55f; *r2=0.75f;*g2=0.75f;*b2=0.81f; break;
        case HERO_NECROMANT: *r=0.19f;*g=0.19f;*b=0.38f; *r2=0.13f;*g2=0.13f;*b2=0.25f; break;
        default: break;
    }
}

static void enemy_color(Enemy *e, float *r, float *g, float *b) {
    switch (e->kind) {
        case EK_ZOMBIE: *r=0.31f;*g=0.50f;*b=0.25f; break;
        case EK_BANDIT: *r=0.50f;*g=0.38f;*b=0.25f; break;
        case EK_DEMON:  *r=0.63f;*g=0.13f;*b=0.25f; break;
        case EK_SLIME:  *r=0.25f;*g=0.63f;*b=0.63f; break;
        case EK_BOSS:   *r=1.00f;*g=0.13f;*b=0.50f; break;
        default:        *r=0.5f;*g=0.5f;*b=0.5f; break;
    }
    if (e->hit_flash > 0.f) { *r = 1.f; *g = 1.f; *b = 1.f; }
}

static v3 player_world_pos(Player *p) {
    return v3_make(p->x / TILE, 0.f, p->y / TILE);
}

static void draw_player_3d(Game *g) {
    Player *p = &g->player;
    bool blink = p->invuln_t > 0.f && (((int)(g->time * 24.f)) % 2 == 0);
    if (blink) return;
    v3 pos = player_world_pos(p);             /* monde continu, pas tile-aligne */

    /* bobbing visible quand on bouge ; respiration legere a l'arret */
    float speed_sq = p->vx*p->vx + p->vy*p->vy;
    bool moving = speed_sq > 5.f;
    float bob   = moving ? sinf(g->time * 14.f) * 0.05f
                         : sinf(g->time * 2.5f) * 0.015f;
    float swing = moving ? sinf(g->time * 14.f) * 0.10f : 0.f;

    /* orientation : on tourne legerement le perso vers le mouvement */
    float face_x = 0.f, face_z = 0.f;
    if (speed_sq > 1.f) {
        float l = sqrtf(speed_sq);
        face_x = (p->vx / l);
        face_z = (p->vy / l);
    }
    /* perpendiculaire pour positionner les bras/jambes "sur le cote" */
    float side_x = -face_z, side_z = face_x;
    if (face_x == 0.f && face_z == 0.f) { side_x = 1.f; side_z = 0.f; }

    float br, bg, bb, hr, hg, hb;
    hero_color(p->hero, &br, &bg, &bb, &hr, &hg, &hb);

    /* ombre projetee */
    gfx_box_draw(g->renderer,
                 v3_make(pos.x, 0.005f, pos.z),
                 v3_make(0.55f, 0.01f, 0.55f),
                 0.02f, 0.01f, 0.04f);

    /* jambes : 2 cubes lateraux qui swingent */
    float leg_off = 0.13f;
    float leg_lift_l = (swing > 0 ? swing * 0.5f : 0.f);
    float leg_lift_r = (swing < 0 ? -swing * 0.5f : 0.f);
    gfx_box_draw(g->renderer,
                 v3_make(pos.x + side_x * leg_off, 0.20f + leg_lift_l, pos.z + side_z * leg_off),
                 v3_make(0.18f, 0.40f, 0.20f),
                 hr*0.55f, hg*0.55f, hb*0.55f);
    gfx_box_draw(g->renderer,
                 v3_make(pos.x - side_x * leg_off, 0.20f + leg_lift_r, pos.z - side_z * leg_off),
                 v3_make(0.18f, 0.40f, 0.20f),
                 hr*0.55f, hg*0.55f, hb*0.55f);

    /* corps (tunique) */
    gfx_box_draw(g->renderer,
                 v3_make(pos.x, 0.62f + bob, pos.z),
                 v3_make(0.50f, 0.46f, 0.40f),
                 br, bg, bb);

    /* bras : 2 cubes lateraux */
    gfx_box_draw(g->renderer,
                 v3_make(pos.x + side_x * 0.32f, 0.62f + bob - swing * 0.5f,
                         pos.z + side_z * 0.32f),
                 v3_make(0.14f, 0.34f, 0.14f),
                 br * 1.05f, bg * 1.05f, bb * 1.05f);
    gfx_box_draw(g->renderer,
                 v3_make(pos.x - side_x * 0.32f, 0.62f + bob + swing * 0.5f,
                         pos.z - side_z * 0.32f),
                 v3_make(0.14f, 0.34f, 0.14f),
                 br * 1.05f, bg * 1.05f, bb * 1.05f);

    /* tete (peau) */
    gfx_box_draw(g->renderer,
                 v3_make(pos.x + face_x * 0.02f, 1.05f + bob, pos.z + face_z * 0.02f),
                 v3_make(0.36f, 0.36f, 0.36f),
                 0.91f, 0.75f, 0.54f);
    /* yeux : 2 minuscules cubes noirs devant la tete */
    {
        float ex = pos.x + face_x * 0.18f + side_x * 0.07f;
        float ez = pos.z + face_z * 0.18f + side_z * 0.07f;
        gfx_box_draw(g->renderer, v3_make(ex, 1.10f + bob, ez),
                     v3_make(0.04f, 0.04f, 0.04f), 0.02f, 0.02f, 0.02f);
        ex = pos.x + face_x * 0.18f - side_x * 0.07f;
        ez = pos.z + face_z * 0.18f - side_z * 0.07f;
        gfx_box_draw(g->renderer, v3_make(ex, 1.10f + bob, ez),
                     v3_make(0.04f, 0.04f, 0.04f), 0.02f, 0.02f, 0.02f);
    }

    /* casque / capuche : couvre la tete sauf si rien d'equipe */
    if (p->equipped[SLOT_HELM].occupied) {
        uint32_t c = rarity_color(p->equipped[SLOT_HELM].rarity);
        gfx_box_draw(g->renderer,
            v3_make(pos.x, 1.20f + bob, pos.z),
            v3_make(0.42f, 0.20f, 0.42f),
            ((c>>24)&0xFF)/255.f, ((c>>16)&0xFF)/255.f, ((c>>8)&0xFF)/255.f);
    } else {
        /* cheveux / bandeau couleur classe */
        gfx_box_draw(g->renderer,
            v3_make(pos.x, 1.22f + bob, pos.z),
            v3_make(0.40f, 0.10f, 0.40f),
            hr * 0.4f, hg * 0.4f, hb * 0.4f);
    }

    /* armure de torse */
    if (p->equipped[SLOT_CHEST].occupied) {
        uint32_t c = rarity_color(p->equipped[SLOT_CHEST].rarity);
        gfx_box_draw(g->renderer,
            v3_make(pos.x, 0.70f + bob, pos.z),
            v3_make(0.55f, 0.30f, 0.45f),
            ((c>>24)&0xFF)/255.f, ((c>>16)&0xFF)/255.f, ((c>>8)&0xFF)/255.f);
    }

    /* arme tenue dans la main droite (cote +side).
     * Anim de combat : pendant anim_t (0..0.18s), on pousse l'arme vers
     * l'avant en arc (sin bell). anim_kind dispatch le visuel par arme. */
    {
        Weapon *w = &p->weapons[p->active_weapon];
        /* progress = 0 (debut anim) -> 1 (fin) */
        float ap = 0.f;
        if (p->anim_t > 0.f) ap = 1.f - (p->anim_t / 0.18f);
        if (ap < 0.f) ap = 0.f;
        if (ap > 1.f) ap = 1.f;
        /* courbe bell : pic a ap=0.5, retour a 0 a ap=0/1 */
        float arc = sinf(ap * 3.1416f);
        /* dans quelle direction frapper : anim_dir si dispo, sinon facing */
        float ax = p->anim_dir_x, az = p->anim_dir_y;
        if (ax * ax + az * az < 0.001f) { ax = face_x; az = face_z; }
        float al = sqrtf(ax * ax + az * az);
        if (al > 0.001f) { ax /= al; az /= al; }

        if (w->kind != W_FISTS) {
            float wr = 0.9f, wg = 0.9f, wb = 0.95f;
            float wlen = 0.50f, wsize = 0.10f;
            switch (w->kind) {
                case W_SWORD:  wr=0.92f; wg=0.92f; wb=0.96f; wlen=0.55f; break;
                case W_AXE:    wr=0.65f; wg=0.65f; wb=0.70f; wlen=0.50f; wsize=0.14f; break;
                case W_BOW:    wr=0.55f; wg=0.35f; wb=0.18f; wlen=0.55f; break;
                case W_WAND:   wr=0.62f; wg=0.32f; wb=0.95f; wlen=0.45f; break;
                case W_SHIELD: wr=0.72f; wg=0.62f; wb=0.32f; wlen=0.45f; wsize=0.20f; break;
                default: break;
            }
            /* position de base : main droite */
            float hx = pos.x + side_x * 0.42f + face_x * 0.10f;
            float hz = pos.z + side_z * 0.42f + face_z * 0.10f;
            float hy = 0.62f + bob - swing * 0.5f;
            /* Decalage selon anim_kind */
            switch (p->anim_kind) {
                case 0: { /* sword swing : pousse l'arme dans l'arc */
                    hx += ax * 0.35f * arc;
                    hz += az * 0.35f * arc;
                    /* leger angle en hauteur (haut->bas) */
                    hy += 0.10f * sinf(ap * 3.1416f * 2.f);
                    break;
                }
                case 1: { /* axe : trajectoire circulaire devant le perso */
                    float ang = ap * 3.1416f * 1.4f;
                    float ofx = cosf(ang) * 0.35f;
                    float ofz = sinf(ang) * 0.35f;
                    hx = pos.x + face_x * 0.10f + side_x * (0.42f + ofx);
                    hz = pos.z + face_z * 0.10f + side_z * (0.42f + ofz);
                    break;
                }
                case 2: { /* shield bash : avance brefe */
                    hx += ax * 0.30f * arc;
                    hz += az * 0.30f * arc;
                    break;
                }
                case 3: { /* wand : reste mais pulse */
                    /* poignet recule d'un cheveu, eclair geant en bout */
                    hy += 0.05f * arc;
                    break;
                }
                case 4: { /* bow : recule puis revient (pull-back/release) */
                    hx -= ax * 0.10f * (1.f - arc);
                    hz -= az * 0.10f * (1.f - arc);
                    break;
                }
                default: break;
            }
            gfx_box_draw(g->renderer,
                v3_make(hx, hy, hz),
                v3_make(wsize, wlen, wsize),
                wr, wg, wb);
            /* eclat magique au bout du baton (boost pendant l'anim wand) */
            if (w->kind == W_WAND) {
                float gw = 0.16f + 0.10f * arc;
                gfx_box_draw(g->renderer,
                    v3_make(hx, 0.92f + bob, hz),
                    v3_make(gw, gw, gw),
                    0.95f, 0.65f, 1.0f);
            }
            /* trail visuel pour epee / hache pendant l'anim */
            if ((w->kind == W_SWORD || w->kind == W_AXE) && arc > 0.2f) {
                float tx = pos.x + ax * (0.30f + arc * 0.40f);
                float tz = pos.z + az * (0.30f + arc * 0.40f);
                gfx_box_draw(g->renderer,
                    v3_make(tx, 0.55f + bob, tz),
                    v3_make(0.06f, 0.04f, 0.06f),
                    1.f, 0.95f, 0.6f);
            }
        } else if (p->anim_kind == 5 && ap > 0.f) {
            /* poings : un poing extra-mis vers l'avant */
            float fx = pos.x + ax * (0.20f + 0.40f * arc) + side_x * 0.18f;
            float fz = pos.z + az * (0.20f + 0.40f * arc) + side_z * 0.18f;
            gfx_box_draw(g->renderer,
                v3_make(fx, 0.62f + bob, fz),
                v3_make(0.18f, 0.18f, 0.18f),
                0.91f, 0.75f, 0.54f);
        }
    }

    /* dash glow */
    if (p->dash_t > 0.f) {
        gfx_box_draw(g->renderer,
            v3_make(pos.x, 0.5f, pos.z),
            v3_make(0.85f, 0.05f, 0.85f),
            0.6f, 1.0f, 1.0f);
    }
}

static void draw_enemy_3d(Game *g, Enemy *e) {
    float r, gg, b; enemy_color(e, &r, &gg, &b);
    v3 pos = v3_make(e->x / TILE, 0.f, e->y / TILE);
    float h = 1.0f, w = 0.65f;
    if (e->is_boss) { h = 1.7f; w = 1.15f; }
    if (e->kind == EK_SLIME) { h = 0.45f; w = 0.7f; }

    /* anim de mort : le corps fond dans le sol, retreci, et tinte rouge.
     * dying_ratio = 1 -> mort fraiche ; -> 0 = sur le point de disparaitre */
    if (e->dying_t > 0.f) {
        float ratio = e->dying_max > 0.f ? (e->dying_t / e->dying_max) : 0.f;
        if (ratio < 0.f) ratio = 0.f;
        float scale = 0.20f + 0.80f * ratio;             /* shrinks to 20% */
        float sink  = (1.f - ratio) * (h * 0.5f);        /* coule */
        gfx_box_draw(g->renderer,
                     v3_make(pos.x, h * 0.5f * scale - sink, pos.z),
                     v3_make(w * scale, h * scale, w * scale),
                     r * (0.7f + 0.3f * ratio),
                     gg * 0.4f * ratio,
                     b  * 0.4f * ratio);
        /* particules ascendantes */
        if ((rand() % 100) < 25) {
            particle_spawn_kind(g, e->x, e->y, (rand()%40)-20, -50,
                                0.4f, (uint32_t)(0xC02828FF), 2.f, 0);
        }
        return;
    }

    /* swing/bobbing en mouvement */
    float swing = sinf(g->time * 8.f + e->x * 0.13f + e->y * 0.07f) * 0.06f;
    float wobble = (e->kind == EK_SLIME) ? sinf(g->time * 6.f + e->x) * 0.08f : 0.f;

    /* ombre */
    gfx_box_draw(g->renderer,
                 v3_make(pos.x, 0.005f, pos.z),
                 v3_make(w + 0.05f, 0.01f, w + 0.05f),
                 0.02f, 0.01f, 0.04f);

    /* SLIME : grosse bulle + reflet */
    if (e->kind == EK_SLIME) {
        gfx_box_draw(g->renderer,
                     v3_make(pos.x, h * 0.5f + wobble, pos.z),
                     v3_make(w + wobble*0.5f, h + wobble, w + wobble*0.5f),
                     r, gg, b);
        gfx_box_draw(g->renderer,
                     v3_make(pos.x - 0.08f, h * 0.85f + wobble, pos.z - 0.08f),
                     v3_make(0.10f, 0.10f, 0.10f),
                     1.f, 1.f, 1.f);
        return;
    }

    /* BOSS : grand corps + couronne + chaque element marque */
    if (e->is_boss) {
        gfx_box_draw(g->renderer, v3_make(pos.x, h * 0.5f, pos.z),
                     v3_make(w, h, w), r, gg, b);
        /* tete carre */
        gfx_box_draw(g->renderer, v3_make(pos.x, h + 0.25f, pos.z),
                     v3_make(w * 0.7f, 0.45f, w * 0.7f),
                     r * 1.2f, gg * 1.2f, b * 1.2f);
        /* yeux rouges */
        gfx_box_draw(g->renderer, v3_make(pos.x - 0.18f, h + 0.30f, pos.z + 0.32f),
                     v3_make(0.08f, 0.08f, 0.06f), 1.f, 0.1f, 0.1f);
        gfx_box_draw(g->renderer, v3_make(pos.x + 0.18f, h + 0.30f, pos.z + 0.32f),
                     v3_make(0.08f, 0.08f, 0.06f), 1.f, 0.1f, 0.1f);
        /* couronne doree */
        gfx_box_draw(g->renderer, v3_make(pos.x, h + 0.55f, pos.z),
                     v3_make(w * 1.0f, 0.10f, w * 1.0f), 1.0f, 0.85f, 0.25f);
        /* 3 pointes */
        for (int i = -1; i <= 1; i++) {
            gfx_box_draw(g->renderer,
                v3_make(pos.x + i * 0.30f, h + 0.70f, pos.z),
                v3_make(0.10f, 0.18f, 0.10f),
                1.0f, 0.85f, 0.25f);
        }
        /* halo elementaire */
        if (e->element != EL_NONE) {
            uint32_t c = element_color(e->element);
            float er=((c>>24)&0xFF)/255.f,eg=((c>>16)&0xFF)/255.f,eb=((c>>8)&0xFF)/255.f;
            float pulse = 0.10f + 0.06f * sinf(g->time * 3.f);
            gfx_box_draw(g->renderer,
                         v3_make(pos.x, 0.05f, pos.z),
                         v3_make(w + pulse * 2.f, 0.02f, w + pulse * 2.f), er, eg, eb);
        }
        return;
    }

    /* ENNEMIS NORMAUX : corps + tete + 2 jambes + 2 bras
     * Bump de taille bref pendant hit_flash pour la sensation d'impact. */
    if (e->hit_flash > 0.f) {
        float k = 1.f + (e->hit_flash / 0.10f) * 0.10f;
        if (k > 1.10f) k = 1.10f;
        h *= k; w *= k;
    }
    float leg_h = 0.32f;
    float body_y = leg_h + (h - leg_h) * 0.5f;
    /* jambes */
    gfx_box_draw(g->renderer,
                 v3_make(pos.x - 0.12f, leg_h * 0.5f + swing * 0.5f, pos.z),
                 v3_make(0.16f, leg_h, 0.18f),
                 r * 0.6f, gg * 0.6f, b * 0.6f);
    gfx_box_draw(g->renderer,
                 v3_make(pos.x + 0.12f, leg_h * 0.5f - swing * 0.5f, pos.z),
                 v3_make(0.16f, leg_h, 0.18f),
                 r * 0.6f, gg * 0.6f, b * 0.6f);
    /* corps */
    gfx_box_draw(g->renderer, v3_make(pos.x, body_y, pos.z),
                 v3_make(w, h - leg_h, w * 0.85f), r, gg, b);
    /* bras */
    float arm_swing = swing * 1.5f;
    gfx_box_draw(g->renderer,
                 v3_make(pos.x - (w/2 + 0.08f), body_y - arm_swing, pos.z),
                 v3_make(0.13f, (h - leg_h) * 0.85f, 0.13f),
                 r * 0.9f, gg * 0.9f, b * 0.9f);
    gfx_box_draw(g->renderer,
                 v3_make(pos.x + (w/2 + 0.08f), body_y + arm_swing, pos.z),
                 v3_make(0.13f, (h - leg_h) * 0.85f, 0.13f),
                 r * 0.9f, gg * 0.9f, b * 0.9f);
    /* tete */
    float head_y = h + 0.18f;
    gfx_box_draw(g->renderer,
                 v3_make(pos.x, head_y, pos.z),
                 v3_make(w * 0.7f, 0.36f, w * 0.7f),
                 r * 1.15f, gg * 1.15f, b * 1.15f);
    /* yeux */
    {
        float ey = head_y + 0.05f;
        gfx_box_draw(g->renderer,
                     v3_make(pos.x - 0.10f, ey, pos.z + w * 0.36f),
                     v3_make(0.06f, 0.06f, 0.04f),
                     1.f, 0.95f, 0.25f);
        gfx_box_draw(g->renderer,
                     v3_make(pos.x + 0.10f, ey, pos.z + w * 0.36f),
                     v3_make(0.06f, 0.06f, 0.04f),
                     1.f, 0.95f, 0.25f);
    }
    /* details par kind */
    switch (e->kind) {
        case EK_DEMON: {
            /* cornes */
            gfx_box_draw(g->renderer,
                         v3_make(pos.x - 0.18f, head_y + 0.30f, pos.z),
                         v3_make(0.10f, 0.20f, 0.10f), 0.4f, 0.15f, 0.2f);
            gfx_box_draw(g->renderer,
                         v3_make(pos.x + 0.18f, head_y + 0.30f, pos.z),
                         v3_make(0.10f, 0.20f, 0.10f), 0.4f, 0.15f, 0.2f);
            /* dents */
            gfx_box_draw(g->renderer,
                         v3_make(pos.x, head_y - 0.10f, pos.z + w * 0.37f),
                         v3_make(0.18f, 0.06f, 0.04f),
                         1.f, 1.f, 1.f);
            break;
        }
        case EK_BANDIT: {
            /* masque noir au visage */
            gfx_box_draw(g->renderer,
                         v3_make(pos.x, head_y + 0.04f, pos.z + w * 0.36f),
                         v3_make(w * 0.65f, 0.13f, 0.04f),
                         0.05f, 0.05f, 0.07f);
            break;
        }
        case EK_ZOMBIE: {
            /* "blessure" verdatre sur le torse */
            gfx_box_draw(g->renderer,
                         v3_make(pos.x, body_y + 0.05f, pos.z + w * 0.43f),
                         v3_make(0.20f, 0.10f, 0.04f),
                         0.45f, 0.65f, 0.2f);
            break;
        }
        default: break;
    }

    /* aura elite (disque pulsant au sol couleur element) */
    if (e->is_elite && e->element != EL_NONE) {
        uint32_t c = element_color(e->element);
        float er=((c>>24)&0xFF)/255.f,eg=((c>>16)&0xFF)/255.f,eb=((c>>8)&0xFF)/255.f;
        float pulse = 0.10f + 0.06f * sinf(g->time * 5.f);
        gfx_box_draw(g->renderer,
                     v3_make(pos.x, 0.04f, pos.z),
                     v3_make(w + pulse * 2.f, 0.02f, w + pulse * 2.f), er, eg, eb);
    }
}

static void draw_pickup_3d(Game *g, Pickup *pk) {
    /* Hover plus marque + pillar de lumiere couleur sous le pickup pour
     * qu'il soit visible meme dans un coin sombre / cache derriere un mur. */
    float hover = sinf(pk->hover_t) * 0.10f + 0.10f;
    v3 pos = v3_make(pk->x / TILE, 0.55f + hover, pk->y / TILE);
    float r=0.7f, gg=0.7f, b=0.7f, sz=0.40f;
    bool draw_pillar = true;
    switch (pk->kind) {
        case PU_XP:      r=0.30f; gg=0.80f; b=1.0f;   sz=0.32f; break;
        case PU_HEART:   r=1.0f;  gg=0.25f; b=0.38f;  sz=0.40f; break;
        case PU_SOUL:    r=0.55f; gg=0.95f; b=0.55f;  sz=0.40f; break;
        case PU_COIN:    r=1.0f;  gg=0.85f; b=0.25f;  sz=0.32f; break;
        case PU_ELEMENT: {
            uint32_t c = element_color((Element)pk->value);
            r=((c>>24)&0xFF)/255.f; gg=((c>>16)&0xFF)/255.f; b=((c>>8)&0xFF)/255.f;
            sz=0.50f; break;
        }
        case PU_WEAPON:  r=0.92f; gg=0.92f; b=1.0f;   sz=0.55f; break;
        case PU_CHEST:   r=0.55f; gg=0.34f; b=0.20f;  sz=0.70f; draw_pillar = false; break;
        case PU_PORTAL: {
            /* gros disque + halo */
            float a = g->time * 3.f;
            for (int i = 0; i < 10; i++) {
                float ang = a + i * 0.628f;
                gfx_box_draw(g->renderer,
                    v3_make(pos.x + cosf(ang)*0.55f,
                            0.40f + sinf(ang*0.5f + g->time)*0.30f,
                            pos.z + sinf(ang)*0.55f),
                    v3_make(0.14f, 0.14f, 0.14f),
                    0.55f, 0.90f, 1.0f);
            }
            /* socle bleu lumineux */
            gfx_box_draw(g->renderer, v3_make(pos.x, 0.05f, pos.z),
                         v3_make(0.85f, 0.06f, 0.85f), 0.30f, 0.60f, 1.0f);
            /* pillar haut de lumiere */
            for (int i = 0; i < 4; i++) {
                gfx_box_draw(g->renderer,
                    v3_make(pos.x, 0.5f + i * 0.6f, pos.z),
                    v3_make(0.10f, 0.5f, 0.10f),
                    0.30f + i * 0.10f, 0.60f, 1.0f);
            }
            return;
        }
        case PU_ITEM: {
            uint32_t c = rarity_color(pk->item.rarity);
            r=((c>>24)&0xFF)/255.f; gg=((c>>16)&0xFF)/255.f; b=((c>>8)&0xFF)/255.f;
            sz=0.50f; break;
        }
        case PU_FOOD: {
            /* poulet : corps brun grossi + os blanc visible */
            gfx_box_draw(g->renderer, pos, v3_make(0.45f, 0.28f, 0.28f),
                         0.78f, 0.55f, 0.30f);
            gfx_box_draw(g->renderer,
                         v3_make(pos.x, pos.y + 0.20f, pos.z),
                         v3_make(0.16f, 0.18f, 0.16f),
                         0.95f, 0.88f, 0.75f);
            /* pillar */
            gfx_box_draw(g->renderer, v3_make(pos.x, 0.05f, pos.z),
                         v3_make(0.50f, 0.04f, 0.50f),
                         0.85f, 0.45f, 0.20f);
            return;
        }
        case PU_SCROLL: {
            /* parchemin : roule plus gros + sceau rouge + halo dore */
            gfx_box_draw(g->renderer, pos, v3_make(0.50f, 0.16f, 0.32f),
                         0.92f, 0.86f, 0.65f);
            gfx_box_draw(g->renderer,
                         v3_make(pos.x, pos.y + 0.10f, pos.z),
                         v3_make(0.16f, 0.06f, 0.34f),
                         0.70f, 0.18f, 0.18f);
            /* halo dore au sol */
            gfx_box_draw(g->renderer, v3_make(pos.x, 0.05f, pos.z),
                         v3_make(0.55f, 0.04f, 0.55f),
                         0.90f, 0.75f, 0.30f);
            return;
        }
        case PU_SHRINE: {
            /* autel de pierre : 3 cubes empiles + flamme bleue */
            gfx_box_draw(g->renderer, v3_make(pos.x, 0.10f, pos.z),
                         v3_make(0.55f, 0.20f, 0.55f),
                         0.40f, 0.40f, 0.45f);
            gfx_box_draw(g->renderer, v3_make(pos.x, 0.32f, pos.z),
                         v3_make(0.40f, 0.22f, 0.40f),
                         0.50f, 0.50f, 0.55f);
            gfx_box_draw(g->renderer, v3_make(pos.x, 0.55f, pos.z),
                         v3_make(0.16f, 0.20f, 0.16f),
                         0.40f, 0.70f, 1.0f);  /* flamme bleue */
            return;
        }
    }
    /* corps principal */
    gfx_box_draw(g->renderer, pos, v3_make(sz, sz, sz), r, gg, b);
    /* halo plat brillant a la base = "spot light" sous le pickup,
     * tres lisible meme dans les couloirs sombres */
    if (draw_pillar) {
        float hr = r * 0.7f + 0.2f, hg = gg * 0.7f + 0.2f, hb = b * 0.7f + 0.2f;
        gfx_box_draw(g->renderer, v3_make(pos.x, 0.04f, pos.z),
                     v3_make(sz + 0.20f, 0.03f, sz + 0.20f),
                     hr, hg, hb);
        /* mini-pillar fin pour attirer l'oeil */
        float pulse = 0.5f + 0.5f * sinf(g->time * 4.f + pk->hover_t * 2.f);
        gfx_box_draw(g->renderer, v3_make(pos.x, 0.30f, pos.z),
                     v3_make(0.05f, 0.55f * pulse, 0.05f),
                     hr, hg, hb);
    }
}

static void draw_projectile_3d(Game *g, Projectile *pr) {
    v3 pos = v3_make(pr->x / TILE, 0.5f, pr->y / TILE);
    uint32_t c = element_color(pr->primary);
    if (pr->owner == 1 && pr->primary == EL_NONE) c = 0xFF80C0FF;
    float r = ((c>>24)&0xFF)/255.f, gg = ((c>>16)&0xFF)/255.f, b = ((c>>8)&0xFF)/255.f;
    float sz = (pr->aoe > 0.f) ? 0.30f : 0.18f;
    gfx_box_draw(g->renderer, pos, v3_make(sz, sz, sz), r, gg, b);
}

static void draw_fairy_3d(Game *g, Fairy *f) {
    v3 pos = v3_make(f->x / TILE, 0.7f, f->y / TILE);
    uint32_t c = element_color(f->element);
    float r = ((c>>24)&0xFF)/255.f, gg = ((c>>16)&0xFF)/255.f, b = ((c>>8)&0xFF)/255.f;
    gfx_box_draw(g->renderer, pos, v3_make(0.16f, 0.16f, 0.16f), r, gg, b);
}

static void draw_particles_3d(Game *g) {
    for (int i = 0; i < MAX_PARTICLES; i++) {
        Particle *p = &g->particles[i];
        if (!p->alive) continue;
        uint32_t c = p->color;
        float r = ((c>>24)&0xFF)/255.f, gg = ((c>>16)&0xFF)/255.f, b = ((c>>8)&0xFF)/255.f;
        v3 pos = v3_make(p->x / TILE, 0.5f, p->y / TILE);
        float s = (p->size + 1.f) / TILE * 1.5f;
        if (s < 0.05f) s = 0.05f;
        gfx_box_draw(g->renderer, pos, v3_make(s, s, s), r, gg, b);
    }
}


#if 0
/* ---------- SPRITES (ANCIEN 2D - desactive en 3D) ---------- */
static void draw_player(Game *g, int sx, int sy) {
    Player *p = &g->player;
    bool blink = p->invuln_t > 0.f && (((int)(g->time * 24.f)) % 2 == 0);
    if (blink) return;

    /* ombre douce ovale */
    gfx_set_blend(g->renderer, true);
    fill_rect(g->renderer, sx - 7, sy + 6, 14, 2, 0x00000080);
    fill_rect(g->renderer, sx - 5, sy + 8, 10, 1, 0x00000060);
    gfx_set_blend(g->renderer, false);

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
    gfx_set_blend(g->renderer, true);
    fill_rect(g->renderer, sx - w/2, sy + w/2 - 1, w, 3, 0x00000080);
    gfx_set_blend(g->renderer, false);

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

#endif
/* ---------- WORLD ---------- */
void render_world(Game *g) {
    Player *p = &g->player;
    GfxCtx *gc = g->renderer;

    /* (re)build mesh quand le donjon change. On utilise gen_id, qui est
       incremente a chaque dungeon_generate, pour detecter une regeneration
       meme au meme niveau (relance d'une course par exemple). */
    static int built_gen = -1;
    bool dungeon_changed = (built_gen != g->dungeon.gen_id);
    if (dungeon_changed || s_mesh_count == 0) {
        build_dungeon_mesh(g);
        built_gen = g->dungeon.gen_id;
    }

    /* camera : 3eme personne, isometrique-ish, lerp doux vers le joueur */
    v3 player_w = player_world_pos(p);
    static float scam_x = 0.f, scam_z = 0.f;
    static int   scam_init = 0;
    if (!scam_init || dungeon_changed) {
        scam_x = player_w.x; scam_z = player_w.z; scam_init = 1;
    }
    /* approche : 12% du delta par frame -> environ 0.4 sec pour rattraper */
    float lerp_k = 0.12f;
    scam_x += (player_w.x - scam_x) * lerp_k;
    scam_z += (player_w.z - scam_z) * lerp_k;

    /* shake : on utilise camera_x/y (offsets shake depuis main) en world */
    float shake_x = g->camera_x * 0.015f;
    float shake_z = g->camera_y * 0.015f;
    v3 cam_target = v3_make(scam_x + shake_x, 0.6f, scam_z + shake_z);
    v3 cam_eye = v3_add(cam_target, v3_make(0.0f, 9.0f, 8.0f));
    m4 view = m4_lookat(cam_eye, cam_target, v3_make(0, 1, 0));
    float aspect = (float)INTERNAL_W / (float)INTERNAL_H;
    m4 proj = m4_perspective(0.85f, aspect, 0.1f, 90.0f);
    gfx_set_camera(gc, view, proj);

    /* mouse aim : ray-cast vers plan y=0 et stocke en pixel-coords */
    {
        v3 ro, rd;
        gfx_unproject(g->mouse_x, g->mouse_y, INTERNAL_W, INTERNAL_H, view, proj, &ro, &rd);
        if (fabsf(rd.y) > 1e-4f) {
            float t = -ro.y / rd.y;
            if (t > 0.f && t < 200.f) {
                v3 hit = v3_add(ro, v3_scl(rd, t));
                p->aim_x = hit.x * (float)TILE;
                p->aim_y = hit.z * (float)TILE;
            }
        }
    }

    /* ---- 3D world ---- */
    gfx_terrain_draw(gc, player_w);

    for (int i = 0; i < MAX_PICKUPS; i++) {
        Pickup *pk = &g->pickups[i]; if (!pk->alive) continue;
        draw_pickup_3d(g, pk);
    }
    for (int i = 0; i < MAX_FAIRIES; i++) {
        Fairy *f = &g->fairies[i]; if (!f->alive) continue;
        draw_fairy_3d(g, f);
    }
    for (int i = 0; i < MAX_ENEMIES; i++) {
        Enemy *e = &g->enemies[i]; if (!e->alive) continue;
        draw_enemy_3d(g, e);
    }
    draw_player_3d(g);
    for (int i = 0; i < MAX_PROJECTILES; i++) {
        Projectile *pr = &g->projectiles[i]; if (!pr->alive) continue;
        draw_projectile_3d(g, pr);
    }
    draw_particles_3d(g);
}

/* HP bars + names + dmg numbers : passe UI (apres gfx_ui_begin).
   Expose en non-static car appelee par main.c. */
void render_world_overlay_ui(Game *g) {
    GfxCtx *gc = g->renderer;
    /* Aura pulsante autour du joueur quand le triple combo est en overload.
     * Pose des particules en couronne -- elles seront rendues a la frame
     * suivante par draw_particles_3d. */
    {
        int lidx = g->player.active_loop_idx;
        if (lidx >= 0 && g->player.loop_states[lidx].overloaded) {
            float alpha = sinf(g->time * 8.0f) * 0.4f + 0.5f;
            uint32_t aura = triple_loop_aura_color(lidx);
            uint32_t col = (aura & 0xFFFFFF00u) | (uint32_t)(alpha * 255.f);
            int n = 24;
            float r = 18.f;
            for (int i = 0; i < n; i++) {
                float a = (i / (float)n) * 6.2831f + g->time * 2.f;
                particle_spawn_kind(g,
                    g->player.x + cosf(a) * r,
                    g->player.y + sinf(a) * r,
                    0, 0, 0.06f, col, 2.5f, 0);
            }
        }
    }
    /* HP bars + noms au-dessus des ennemis */
    for (int i = 0; i < MAX_ENEMIES; i++) {
        Enemy *e = &g->enemies[i];
        if (!e->alive) continue;
        v3 head = v3_make(e->x / TILE, (e->is_boss ? 2.0f : 1.4f),
                          e->y / TILE);
        int sx, sy;
        if (!world_to_screen(gc, head, &sx, &sy)) continue;
        if (e->hp < e->maxhp) {
            int bw = e->is_boss ? 80 : 24;
            int bx = sx - bw / 2, by = sy;
            fill_rect(gc, bx, by, bw, e->is_boss ? 4 : 2, 0x402020FF);
            int hf = (int)(bw * (e->hp / e->maxhp));
            fill_rect(gc, bx, by, hf, e->is_boss ? 4 : 2, 0xFF4040FF);
        }
        if ((e->is_elite || e->is_boss) && e->name[0]) {
            int nw = text_width(e->name);
            uint32_t col = e->is_boss ? 0xFFD040FF : element_color(e->element);
            text_draw(gc, sx - nw/2 + 1, sy - 9, e->name, 0x000000FF);
            text_draw(gc, sx - nw/2,     sy - 10, e->name, col);
            /* signature de combo : pour bien lire le pattern de l'ennemi */
            if (e->combo_mask) {
                const char *sig = combo_name(e->combo_mask);
                if (sig && sig[0]) {
                    char buf[40];
                    snprintf(buf, sizeof(buf), "[%s]", sig);
                    int sw = text_width(buf);
                    uint32_t scol = combo_color(e->combo_mask);
                    text_draw(gc, sx - sw/2 + 1, sy - 19, buf, 0x000000FF);
                    text_draw(gc, sx - sw/2,     sy - 20, buf, scol);
                }
            }
        }
    }
    /* damage numbers */
    for (int i = 0; i < MAX_DMGNUM; i++) {
        DamageNumber *d = &g->dmgnums[i];
        if (!d->alive) continue;
        v3 wp = v3_make(d->x / TILE, 1.0f, d->y / TILE);
        int sx, sy;
        if (!world_to_screen(gc, wp, &sx, &sy)) continue;
        uint32_t col = d->color;
        if (d->life < 0.25f) {
            uint8_t a = (uint8_t)(255 * (d->life / 0.25f));
            col = (col & 0xFFFFFF00) | a;
        }
        if (d->big) {
            text_draw(gc, sx - 5, sy - 7, d->text, 0x000000FF);
            text_draw(gc, sx - 4, sy - 6, d->text, col);
        } else {
            text_draw(gc, sx - 4, sy - 6, d->text, col);
        }
    }
    /* curseur de visee */
    int mx = g->mouse_x, my = g->mouse_y;
    fill_rect(gc, mx - 5, my, 4, 1, 0xFFFFFFFF);
    fill_rect(gc, mx + 2, my, 4, 1, 0xFFFFFFFF);
    fill_rect(gc, mx, my - 5, 1, 4, 0xFFFFFFFF);
    fill_rect(gc, mx, my + 2, 1, 4, 0xFFFFFFFF);
    fill_rect(gc, mx, my, 1, 1, 0xFFFFFFFF);
    /* vignette */
    draw_vignette(g);
    /* boss intro */
    if (g->boss_intro_t > 0.f) {
        int alpha = (int)(180 * (g->boss_intro_t / 2.5f));
        if (alpha > 180) alpha = 180;
        gfx_set_blend(gc, true);
        uint32_t col = (uint32_t)(0x20608000u | (alpha & 0xFF));
        gfx_set_color(gc, col);
        gfx_fill_rect(gc, 0, INTERNAL_H/2 - 18, INTERNAL_W, 36);
        gfx_set_blend(gc, false);
        text_draw(gc, INTERNAL_W/2 - text_width(g->boss_name)/2,
                  INTERNAL_H/2 - 6, g->boss_name, 0xFFFFFFFF);
        text_draw(gc, INTERNAL_W/2 - text_width("CONFRONTATION")/2,
                  INTERNAL_H/2 + 4, "CONFRONTATION", 0xFFD040FF);
    }

    /* Signature Combo callout : nom du combo triple en grand au-dessus du
     * joueur. Anim : pop/scale rapide les 0.3s puis stable, fade-out 0.5s. */
    if (g->combo_callout_t > 0.f && g->combo_callout[0]) {
        float t = g->combo_callout_t;
        float life = 2.5f;
        float age = life - t;
        float alpha = 1.f;
        float yoff = 0.f;
        if (age < 0.30f) {
            /* pop : remonte */
            yoff = -8.f - (1.f - age / 0.30f) * 12.f;
            alpha = age / 0.30f;
        } else if (t < 0.5f) {
            yoff = -8.f - (0.5f - t) * 14.f;   /* monte en disparaissant */
            alpha = t / 0.5f;
        } else {
            yoff = -8.f;
        }
        v3 head = v3_make(g->player.x / TILE, 1.6f, g->player.y / TILE);
        int sx, sy;
        if (world_to_screen(gc, head, &sx, &sy)) {
            int ialpha = (int)(255.f * alpha);
            if (ialpha < 0)   ialpha = 0;
            if (ialpha > 255) ialpha = 255;
            uint32_t col = (g->combo_callout_color & 0xFFFFFF00u) | (uint32_t)ialpha;
            int tw = text_width(g->combo_callout);
            /* texte avec ombre noire pour lisibilite sur n'importe quel fond */
            text_draw(gc, sx - tw/2 + 1, (int)(sy + yoff) + 1,
                      g->combo_callout, (uint32_t)(ialpha & 0xFF));
            text_draw(gc, sx - tw/2,     (int)(sy + yoff),
                      g->combo_callout, col);
        }
    }

    /* parchemin de lore : overlay bas d'ecran. Fade in/out doux. */
    if (g->scroll_t > 0.f && g->scroll_text[0]) {
        float t = g->scroll_t;
        float a = 1.f;
        if (t > 5.f)      a = (6.f - t);          /* fade-in 1s */
        else if (t < 1.f) a = t;                  /* fade-out 1s */
        if (a < 0.f) a = 0.f;
        if (a > 1.f) a = 1.f;
        int alpha = (int)(220.f * a);
        gfx_set_blend(gc, true);
        uint32_t bg = (uint32_t)(0x18120800u | (alpha & 0xFF));
        gfx_set_color(gc, bg);
        gfx_fill_rect(gc, 30, INTERNAL_H - 70, INTERNAL_W - 60, 50);
        /* bordure parchemin */
        uint32_t bord = (uint32_t)(0xC0A86000u | (alpha & 0xFF));
        gfx_set_color(gc, bord);
        gfx_fill_rect(gc, 30, INTERNAL_H - 70, INTERNAL_W - 60, 1);
        gfx_fill_rect(gc, 30, INTERNAL_H - 21, INTERNAL_W - 60, 1);
        gfx_set_blend(gc, false);
        uint32_t txt = (uint32_t)(0xE8D0A000u | (alpha & 0xFF));
        text_draw(gc, INTERNAL_W/2 - text_width("PARCHEMIN")/2,
                  INTERNAL_H - 64, "PARCHEMIN", txt);
        int tw = text_width(g->scroll_text);
        text_draw(gc, INTERNAL_W/2 - tw/2,
                  INTERNAL_H - 48, g->scroll_text, txt);
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
        gfx_set_blend(g->renderer, true);
        fill_rect(g->renderer, 0, INTERNAL_H/2 - 16, INTERNAL_W, 32, 0x000000C0);
        gfx_set_blend(g->renderer, false);
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
    gfx_set_blend(g->renderer, true);
    fill_rect(g->renderer, 0, 0, INTERNAL_W, INTERNAL_H, 0x000000C0);
    gfx_set_blend(g->renderer, false);
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
    gfx_set_blend(g->renderer, true);
    fill_rect(g->renderer, 0, 0, INTERNAL_W, INTERNAL_H, 0x300010C0);
    gfx_set_blend(g->renderer, false);
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
    gfx_set_blend(g->renderer, true);
    fill_rect(g->renderer, 0, 0, INTERNAL_W, INTERNAL_H, 0x102030E0);
    gfx_set_blend(g->renderer, false);
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

    /* menu vertical (lore retire : distille en jeu via parchemins) */
    const char *items[4] = { "JOUER", "OPTIONS", "AIDE", "QUITTER" };
    int yA = INTERNAL_H/2 + 20;
    int rowh = 16;
    for (int i = 0; i < 4; i++) {
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
    text_draw(g->renderer, 8, y, "FUSION: si 3 items identiques presents -> F les fusionne automatiquement", 0xCCCCCCFF); y += 9;
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
    gfx_set_blend(g->renderer, true);
    fill_rect(g->renderer, 0, 0, INTERNAL_W, INTERNAL_H, 0x000000D0);
    gfx_set_blend(g->renderer, false);
    text_draw(g->renderer, INTERNAL_W/2 - text_width("INVENTAIRE")/2, 6,
              "INVENTAIRE", 0xFFE080FF);

    /* 12 slots: 4x3 grid */
    int gridx = 30, gridy = 24;
    int cell = 28;
    text_draw(g->renderer, gridx, gridy - 9, "SAC (12)", 0xCCCCFFFF);
    /* auto-detection d'un groupe fusion : on l'affiche en surbrillance
     * verte pour que le joueur sache qu'il peut presser F sans rien marquer. */
    int fa = -1, fb = -1, fc = -1;
    bool has_auto_fuse = inventory_find_fusion_group(g, &fa, &fb, &fc);
    for (int i = 0; i < INVENTORY_SLOTS; i++) {
        int row = i / 4, col = i % 4;
        int sx = gridx + col * cell;
        int sy = gridy + row * cell;
        bool sel = (g->inv_cursor == i);
        bool mk = false;
        for (int m = 0; m < g->inv_marked_count; m++)
            if (g->inv_marked[m] == i) mk = true;
        /* highlight auto-fuse group si rien n'est manuellement marque */
        if (g->inv_marked_count == 0 && has_auto_fuse &&
            (i == fa || i == fb || i == fc)) {
            mk = true;
        }
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

    /* fusion preview : montre l'etat manuel s'il y en a un, sinon l'auto-detect */
    if (g->inv_marked_count > 0) {
        text_drawf(g->renderer, gridx + 90, spy, 0x80FF80FF,
                   "FUSION (%d/3 marques)", g->inv_marked_count);
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
    } else if (has_auto_fuse) {
        Item *base = &g->player.inventory[fa];
        text_draw(g->renderer, gridx + 90, spy, "FUSION DETECTEE", 0x80FF80FF);
        text_drawf(g->renderer, gridx + 90, spy + 10, 0x80FF80FF,
                   "F = %s %s",
                   rarity_name(base->rarity + 1), slot_name(base->slot));
        text_draw(g->renderer, gridx + 90, spy + 20,
                  "(verts = items utilises)", 0x80C080FF);
    }

    /* message */
    if (g->inv_msg_t > 0.f) {
        text_draw(g->renderer, INTERNAL_W/2 - text_width(g->inv_msg)/2,
                  INTERNAL_H - 28, g->inv_msg, 0xFFFF40FF);
    }
    text_draw(g->renderer, INTERNAL_W/2 - text_width("FLECHES NAVIGUER  E EQUIPER  F FUSIONNER (auto)  M MARQUER  X RAZ")/2,
              INTERNAL_H - 18, "FLECHES NAVIGUER  E EQUIPER  F FUSIONNER (auto)  M MARQUER  X RAZ", 0xCCCCCCFF);
    text_draw(g->renderer, INTERNAL_W/2 - text_width("ECHAP POUR FERMER")/2,
              INTERNAL_H - 8, "ECHAP POUR FERMER", 0xFFFF80FF);
}
