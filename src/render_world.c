/*
 * render_world.c - rendu 3D voxel : mesh du donjon + cubes/billboards
 * pour joueur, ennemis, pickups, projectiles, fees et particules. + le
 * overlay 2D des barres de PV/dmgnums/noms via world_to_screen.
 *
 * Extrait de render.c. Cf ui_common.h pour fill_rect/text_draw/etc.
 */
#include "ui_common.h"
#include "gfx.h"
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#if defined(__GNUC__) || defined(__clang__)
#define MAYBE_UNUSED __attribute__((unused))
#else
#define MAYBE_UNUSED
#endif

/* ============================================================
 *  RENDU 3D VOXEL
 * ============================================================ */

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

/* Tint biome applique lors du build du mesh. Set par build_dungeon_mesh,
 * lu par emit_wall_column / emit_floor_top via tile/wall_color_*. */
static float s_biome_r = 1.f, s_biome_g = 1.f, s_biome_b = 1.f;

static void emit_wall_column(float x, float z) {
    float x0 = x, x1 = x + 1.f;
    float z0 = z, z1 = z + 1.f;
    float y0 = 0.f, y1 = WALL_H;
    float r, g, b; wall_color_side(&r, &g, &b);
    r *= s_biome_r; g *= s_biome_g; b *= s_biome_b;
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
    tr *= s_biome_r; tg *= s_biome_g; tb *= s_biome_b;
    mesh_push_quad(v3_make(x0,y1,z0), v3_make(x0,y1,z1), v3_make(x1,y1,z1), v3_make(x1,y1,z0),
                   v3_make(0,1,0), tr,tg,tb);
}

static void emit_floor_top(float x, float z, TileKind t) {
    float x0 = x, x1 = x + 1.f;
    float z0 = z, z1 = z + 1.f;
    float y = 0.f;
    float r, g, b; tile_color_top(t, &r, &g, &b);
    /* tint biome : on attenue moins sur les tiles "speciales" (torche,
     * rune, blood) pour qu elles restent reconnaissables. */
    float k = (t == T_FLOOR) ? 1.f : 0.5f;
    r *= (1.f - k) + k * s_biome_r;
    g *= (1.f - k) + k * s_biome_g;
    b *= (1.f - k) + k * s_biome_b;
    mesh_push_quad(v3_make(x0,y,z0), v3_make(x0,y,z1), v3_make(x1,y,z1), v3_make(x1,y,z0),
                   v3_make(0,1,0), r,g,b);
}

static void build_dungeon_mesh(Game *g) {
    s_mesh_count = 0;
    Dungeon *d = &g->dungeon;
    /* sync tint biome avant de pousser les quads */
    biome_tint(biome_for_floor(g->floor_index),
               &s_biome_r, &s_biome_g, &s_biome_b);
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
        case EK_ZOMBIE:  *r=0.31f;*g=0.50f;*b=0.25f; break;
        case EK_BANDIT:  *r=0.50f;*g=0.38f;*b=0.25f; break;
        case EK_DEMON:   *r=0.63f;*g=0.13f;*b=0.25f; break;
        case EK_SLIME:   *r=0.25f;*g=0.63f;*b=0.63f; break;
        case EK_RAT:     *r=0.30f;*g=0.18f;*b=0.14f; break;
        case EK_GHOST:   *r=0.65f;*g=0.65f;*b=0.85f; break;
        case EK_CHARGER: *r=0.55f;*g=0.30f;*b=0.18f; break;
        case EK_MAGE:    *r=0.35f;*g=0.20f;*b=0.55f; break;
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

    /* SLIME : grosse bulle + reflet + noyau interne + drip occasionnel */
    if (e->kind == EK_SLIME) {
        /* corps externe */
        gfx_box_draw(g->renderer,
                     v3_make(pos.x, h * 0.5f + wobble, pos.z),
                     v3_make(w + wobble*0.5f, h + wobble, w + wobble*0.5f),
                     r, gg, b);
        /* noyau interne plus sombre / sature, visible a travers */
        gfx_box_draw(g->renderer,
                     v3_make(pos.x + 0.04f, h * 0.4f + wobble, pos.z + 0.04f),
                     v3_make(0.22f, 0.20f, 0.22f),
                     r * 0.45f, gg * 0.55f, b * 0.45f);
        /* reflet blanc en haut */
        gfx_box_draw(g->renderer,
                     v3_make(pos.x - 0.08f, h * 0.85f + wobble, pos.z - 0.08f),
                     v3_make(0.10f, 0.10f, 0.10f),
                     1.f, 1.f, 1.f);
        /* drip vert qui tombe occasionnellement */
        if ((rand() % 200) < 4) {
            particle_spawn_kind(g, e->x + (rand()%6)-3,
                                e->y + (rand()%6)-3,
                                0, 18.f, 0.40f, 0xB0E060FF, 1.6f, 0);
        }
        return;
    }

    /* RAT : silhouette tres basse, corps allonge + queue qui frette + 2
     * yeux rouges minuscules. Pas de jambes visibles, on glisse. */
    if (e->kind == EK_RAT) {
        float wig = sinf(g->time * 14.f + e->x) * 0.08f;
        /* corps allonge dans l axe du mouvement (facing) */
        float fa = e->facing;
        float cx = cosf(fa), cy = sinf(fa);
        /* corps : box principal (etendu en X local) */
        gfx_box_draw(g->renderer,
                     v3_make(pos.x, 0.18f, pos.z),
                     v3_make(0.40f, 0.18f, 0.22f),
                     r, gg, b);
        /* tete : box plus petite devant */
        gfx_box_draw(g->renderer,
                     v3_make(pos.x + cx * 0.22f, 0.20f, pos.z + cy * 0.22f),
                     v3_make(0.18f, 0.16f, 0.18f),
                     r * 1.1f, gg * 1.1f, b * 1.1f);
        /* queue : derriere, fretille */
        gfx_box_draw(g->renderer,
                     v3_make(pos.x - cx * 0.30f + wig * cy, 0.14f,
                             pos.z - cy * 0.30f - wig * cx),
                     v3_make(0.06f, 0.06f, 0.18f),
                     r * 0.7f, gg * 0.7f, b * 0.7f);
        /* 2 yeux rouges */
        gfx_box_draw(g->renderer,
                     v3_make(pos.x + cx * 0.28f - cy * 0.08f, 0.24f,
                             pos.z + cy * 0.28f + cx * 0.08f),
                     v3_make(0.04f, 0.04f, 0.04f), 1.f, 0.1f, 0.1f);
        gfx_box_draw(g->renderer,
                     v3_make(pos.x + cx * 0.28f + cy * 0.08f, 0.24f,
                             pos.z + cy * 0.28f - cx * 0.08f),
                     v3_make(0.04f, 0.04f, 0.04f), 1.f, 0.1f, 0.1f);
        return;
    }

    /* GHOST : flotte au-dessus du sol, pas de jambes, bobbing fort,
     * desature + un eclat fantome qui pulse, yeux blancs. */
    if (e->kind == EK_GHOST) {
        float bob = sinf(g->time * 3.f + e->x) * 0.10f;
        float pulse = 0.7f + 0.3f * sinf(g->time * 4.f);
        /* corps : ovoide flottant */
        gfx_box_draw(g->renderer,
                     v3_make(pos.x, 0.85f + bob, pos.z),
                     v3_make(0.50f, 0.60f, 0.50f),
                     r * pulse, gg * pulse, b * pulse);
        /* "queue" effilee qui descend */
        gfx_box_draw(g->renderer,
                     v3_make(pos.x, 0.40f + bob * 0.5f, pos.z),
                     v3_make(0.30f, 0.40f, 0.30f),
                     r * 0.7f, gg * 0.7f, b * 0.7f);
        gfx_box_draw(g->renderer,
                     v3_make(pos.x, 0.18f + bob * 0.3f, pos.z),
                     v3_make(0.16f, 0.20f, 0.16f),
                     r * 0.5f, gg * 0.5f, b * 0.5f);
        /* yeux blancs creuses */
        gfx_box_draw(g->renderer,
                     v3_make(pos.x - 0.13f, 0.95f + bob, pos.z + 0.22f),
                     v3_make(0.07f, 0.07f, 0.04f), 1.0f, 1.0f, 1.0f);
        gfx_box_draw(g->renderer,
                     v3_make(pos.x + 0.13f, 0.95f + bob, pos.z + 0.22f),
                     v3_make(0.07f, 0.07f, 0.04f), 1.0f, 1.0f, 1.0f);
        /* sparks spectrales */
        if ((rand() % 100) < 25) {
            particle_spawn_kind(g, e->x, e->y - 4,
                                (rand()%20)-10, -20.f,
                                0.6f, 0xC080FFA0, 1.4f, 0);
        }
        return;
    }

    /* CHARGER : silhouette de taureau / minotaure, gros torse bas et
     * 2 grosses cornes en avant. Pendant le telegraph (ai_t2 == 1),
     * une aura rouge clignotante. */
    if (e->kind == EK_CHARGER) {
        int state = (int)e->ai_t2;
        /* gros corps bas */
        gfx_box_draw(g->renderer,
                     v3_make(pos.x, 0.40f, pos.z),
                     v3_make(0.65f, 0.55f, 0.50f),
                     r, gg, b);
        /* tete proeminente plus basse */
        gfx_box_draw(g->renderer,
                     v3_make(pos.x, 0.50f, pos.z + 0.30f),
                     v3_make(0.42f, 0.32f, 0.32f),
                     r * 1.1f, gg * 1.1f, b * 1.1f);
        /* 2 grosses cornes blanches en avant */
        gfx_box_draw(g->renderer,
                     v3_make(pos.x - 0.20f, 0.60f, pos.z + 0.42f),
                     v3_make(0.10f, 0.10f, 0.30f),
                     0.92f, 0.88f, 0.78f);
        gfx_box_draw(g->renderer,
                     v3_make(pos.x + 0.20f, 0.60f, pos.z + 0.42f),
                     v3_make(0.10f, 0.10f, 0.30f),
                     0.92f, 0.88f, 0.78f);
        /* yeux rouges */
        gfx_box_draw(g->renderer,
                     v3_make(pos.x - 0.10f, 0.54f, pos.z + 0.45f),
                     v3_make(0.05f, 0.05f, 0.04f), 1.f, 0.1f, 0.1f);
        gfx_box_draw(g->renderer,
                     v3_make(pos.x + 0.10f, 0.54f, pos.z + 0.45f),
                     v3_make(0.05f, 0.05f, 0.04f), 1.f, 0.1f, 0.1f);
        /* 4 pattes courtes */
        for (int lx = -1; lx <= 1; lx += 2)
        for (int lz = -1; lz <= 1; lz += 2) {
            gfx_box_draw(g->renderer,
                v3_make(pos.x + lx * 0.22f, 0.08f, pos.z + lz * 0.18f),
                v3_make(0.10f, 0.14f, 0.10f),
                r * 0.6f, gg * 0.6f, b * 0.6f);
        }
        /* aura rouge pendant telegraph (state 1) */
        if (state == 1) {
            float pulse = 0.5f + 0.5f * sinf(g->time * 20.f);
            gfx_box_draw(g->renderer,
                v3_make(pos.x, 0.05f, pos.z),
                v3_make(0.85f, 0.02f, 0.85f),
                1.0f * pulse, 0.2f * pulse, 0.1f * pulse);
        }
        return;
    }

    /* MAGE : tall, robed, hood. Floating tome au-dessus. */
    if (e->kind == EK_MAGE) {
        float bob = sinf(g->time * 2.f + e->x) * 0.04f;
        /* robe : trapeze elargi en bas */
        gfx_box_draw(g->renderer,
                     v3_make(pos.x, 0.18f, pos.z),
                     v3_make(0.50f, 0.36f, 0.50f),
                     r * 0.7f, gg * 0.7f, b * 0.7f);
        gfx_box_draw(g->renderer,
                     v3_make(pos.x, 0.55f, pos.z),
                     v3_make(0.38f, 0.40f, 0.38f),
                     r, gg, b);
        /* hood pointu */
        gfx_box_draw(g->renderer,
                     v3_make(pos.x, 0.95f, pos.z),
                     v3_make(0.30f, 0.30f, 0.30f),
                     r * 0.55f, gg * 0.55f, b * 0.55f);
        gfx_box_draw(g->renderer,
                     v3_make(pos.x, 1.15f, pos.z),
                     v3_make(0.16f, 0.18f, 0.16f),
                     r * 0.45f, gg * 0.45f, b * 0.45f);
        /* yeux bleus brillants sous le hood */
        gfx_box_draw(g->renderer,
                     v3_make(pos.x - 0.08f, 0.95f, pos.z + 0.16f),
                     v3_make(0.05f, 0.05f, 0.03f), 0.4f, 0.7f, 1.0f);
        gfx_box_draw(g->renderer,
                     v3_make(pos.x + 0.08f, 0.95f, pos.z + 0.16f),
                     v3_make(0.05f, 0.05f, 0.03f), 0.4f, 0.7f, 1.0f);
        /* tome flottant a cote */
        gfx_box_draw(g->renderer,
                     v3_make(pos.x + 0.45f, 0.70f + bob, pos.z),
                     v3_make(0.20f, 0.10f, 0.16f),
                     0.55f, 0.25f, 0.20f);
        /* glow sur le tome */
        float gp = 0.6f + 0.4f * sinf(g->time * 6.f);
        gfx_box_draw(g->renderer,
                     v3_make(pos.x + 0.45f, 0.80f + bob, pos.z),
                     v3_make(0.06f, 0.06f, 0.06f),
                     0.9f * gp, 0.5f * gp, 1.0f * gp);
        return;
    }

    /* BOSS : 5 silhouettes distinctes (1 par biome).
     * Couleur de base toujours r/gg/b (enemy_color) + tint biome ou variant.
     * Toutes les variantes affichent un halo d element au sol + une aura
     * de phase (rouge en phase 2, jaune en phase 1) pour signaler l etat. */
    if (e->is_boss) {
        /* couleur element pour les accessoires */
        uint32_t cel = element_color(e->element);
        float er=((cel>>24)&0xFF)/255.f, eg=((cel>>16)&0xFF)/255.f,
              eb=((cel>>8)&0xFF)/255.f;
        /* phase estimee : utile pour l aura visuelle */
        float frac = e->hp / e->maxhp;
        int phase = (frac < 0.25f) ? 2 : (frac < 0.55f ? 1 : 0);

        switch (e->variant) {
            case 0: {
                /* NECROPANTE : robe + hood ample + orbe flottant violet */
                gfx_box_draw(g->renderer, v3_make(pos.x, h * 0.35f, pos.z),
                             v3_make(w * 1.15f, h * 0.65f, w * 1.15f),
                             r * 0.6f, gg * 0.6f, b * 0.7f);
                gfx_box_draw(g->renderer, v3_make(pos.x, h * 0.75f, pos.z),
                             v3_make(w * 0.85f, h * 0.40f, w * 0.85f),
                             r, gg, b);
                /* hood pointu */
                gfx_box_draw(g->renderer, v3_make(pos.x, h + 0.30f, pos.z),
                             v3_make(0.55f, 0.40f, 0.55f),
                             r * 0.4f, gg * 0.4f, b * 0.5f);
                /* yeux blancs creux profond */
                gfx_box_draw(g->renderer,
                             v3_make(pos.x - 0.16f, h + 0.32f, pos.z + 0.36f),
                             v3_make(0.07f, 0.07f, 0.06f), 1.f, 1.f, 1.f);
                gfx_box_draw(g->renderer,
                             v3_make(pos.x + 0.16f, h + 0.32f, pos.z + 0.36f),
                             v3_make(0.07f, 0.07f, 0.06f), 1.f, 1.f, 1.f);
                /* orbe DARK flottant au-dessus */
                float ob = sinf(g->time * 2.f) * 0.12f;
                float pulse = 0.7f + 0.3f * sinf(g->time * 4.f);
                gfx_box_draw(g->renderer,
                             v3_make(pos.x, h + 1.10f + ob, pos.z),
                             v3_make(0.20f, 0.20f, 0.20f),
                             er * pulse, eg * pulse, eb * pulse);
                break;
            }
            case 1: {
                /* GEANT DE PIERRE : corps massif bas + petite tete + 4 bras
                 * trapus + un coeur de cristal qui brille. */
                gfx_box_draw(g->renderer, v3_make(pos.x, h * 0.55f, pos.z),
                             v3_make(w * 1.50f, h * 1.10f, w * 1.50f),
                             r, gg, b);
                /* tete proportionnellement petite */
                gfx_box_draw(g->renderer, v3_make(pos.x, h * 1.20f, pos.z),
                             v3_make(w * 0.5f, 0.32f, w * 0.5f),
                             r * 1.1f, gg * 1.1f, b * 1.1f);
                /* coeur cristal (centre torse) */
                float cp = 0.6f + 0.4f * sinf(g->time * 3.f);
                gfx_box_draw(g->renderer,
                             v3_make(pos.x, h * 0.7f, pos.z + w * 0.55f),
                             v3_make(0.16f, 0.20f, 0.04f),
                             er * cp, eg * cp, eb * cp);
                /* 4 bras / poings */
                for (int sx = -1; sx <= 1; sx += 2)
                for (int sy = -1; sy <= 1; sy += 2) {
                    gfx_box_draw(g->renderer,
                        v3_make(pos.x + sx * (w * 0.75f),
                                h * (sy > 0 ? 0.85f : 0.45f),
                                pos.z),
                        v3_make(0.22f, 0.22f, 0.22f),
                        r * 0.8f, gg * 0.8f, b * 0.8f);
                }
                break;
            }
            case 2: {
                /* HYDRE : corps allonge + 3 tetes en eventail + queue
                 * crochue. Les tetes oscillent independamment. */
                gfx_box_draw(g->renderer, v3_make(pos.x, h * 0.50f, pos.z),
                             v3_make(w * 1.40f, h * 0.55f, w * 0.95f),
                             r * 0.8f, gg * 0.8f, b * 0.9f);
                for (int hi = -1; hi <= 1; hi++) {
                    float swing = sinf(g->time * 3.f + hi * 1.3f) * 0.10f;
                    float neck_y = h * 0.75f + sinf(g->time * 4.f + hi) * 0.08f;
                    /* cou */
                    gfx_box_draw(g->renderer,
                        v3_make(pos.x + hi * 0.25f + swing, neck_y,
                                pos.z + 0.35f),
                        v3_make(0.10f, 0.30f, 0.10f),
                        r * 0.85f, gg * 0.85f, b * 0.85f);
                    /* tete */
                    gfx_box_draw(g->renderer,
                        v3_make(pos.x + hi * 0.25f + swing, neck_y + 0.30f,
                                pos.z + 0.45f),
                        v3_make(0.20f, 0.20f, 0.20f),
                        r * 1.1f, gg * 1.1f, b * 1.1f);
                    /* yeux rouges sur chaque tete */
                    gfx_box_draw(g->renderer,
                        v3_make(pos.x + hi * 0.25f + swing - 0.05f,
                                neck_y + 0.32f, pos.z + 0.56f),
                        v3_make(0.03f, 0.03f, 0.02f), 1.f, 0.1f, 0.1f);
                    gfx_box_draw(g->renderer,
                        v3_make(pos.x + hi * 0.25f + swing + 0.05f,
                                neck_y + 0.32f, pos.z + 0.56f),
                        v3_make(0.03f, 0.03f, 0.02f), 1.f, 0.1f, 0.1f);
                }
                /* queue derriere */
                gfx_box_draw(g->renderer,
                             v3_make(pos.x, h * 0.30f, pos.z - 0.60f),
                             v3_make(0.20f, 0.20f, 0.40f),
                             r * 0.7f, gg * 0.7f, b * 0.7f);
                break;
            }
            case 3: {
                /* FORGERON DES ENFERS : torse massif + enclume devant +
                 * marteau a cote + braise au sol. */
                gfx_box_draw(g->renderer, v3_make(pos.x, h * 0.50f, pos.z),
                             v3_make(w * 1.20f, h * 0.95f, w * 1.05f),
                             r, gg, b);
                /* tete carree + cornes */
                gfx_box_draw(g->renderer, v3_make(pos.x, h + 0.20f, pos.z),
                             v3_make(w * 0.70f, 0.40f, w * 0.70f),
                             r * 1.05f, gg * 1.05f, b * 1.05f);
                gfx_box_draw(g->renderer,
                             v3_make(pos.x - 0.22f, h + 0.42f, pos.z),
                             v3_make(0.08f, 0.20f, 0.08f), 0.5f, 0.2f, 0.15f);
                gfx_box_draw(g->renderer,
                             v3_make(pos.x + 0.22f, h + 0.42f, pos.z),
                             v3_make(0.08f, 0.20f, 0.08f), 0.5f, 0.2f, 0.15f);
                /* enclume devant */
                gfx_box_draw(g->renderer,
                             v3_make(pos.x, 0.20f, pos.z + 0.80f),
                             v3_make(0.55f, 0.20f, 0.30f),
                             0.35f, 0.35f, 0.40f);
                /* marteau */
                float swing = sinf(g->time * 4.f) * 0.10f;
                gfx_box_draw(g->renderer,
                             v3_make(pos.x + 0.65f + swing, h * 0.40f, pos.z),
                             v3_make(0.10f, 0.10f, 0.45f),
                             0.40f, 0.30f, 0.20f);
                gfx_box_draw(g->renderer,
                             v3_make(pos.x + 0.65f + swing, h * 0.85f, pos.z),
                             v3_make(0.30f, 0.20f, 0.30f),
                             0.55f, 0.55f, 0.60f);
                /* braise au sol qui pulse */
                float fp = 0.6f + 0.4f * sinf(g->time * 5.f);
                gfx_box_draw(g->renderer,
                             v3_make(pos.x, 0.04f, pos.z),
                             v3_make(w * 1.8f, 0.02f, w * 1.8f),
                             1.0f * fp, 0.45f * fp, 0.10f * fp);
                /* yeux jaunes */
                gfx_box_draw(g->renderer,
                             v3_make(pos.x - 0.16f, h + 0.25f, pos.z + 0.36f),
                             v3_make(0.07f, 0.07f, 0.05f),
                             1.f, 0.85f, 0.20f);
                gfx_box_draw(g->renderer,
                             v3_make(pos.x + 0.16f, h + 0.25f, pos.z + 0.36f),
                             v3_make(0.07f, 0.07f, 0.05f),
                             1.f, 0.85f, 0.20f);
                break;
            }
            case 4: {
                /* AVATAR DIVIN : silhouette flottante elance + halo dore +
                 * 2 ailes de chaque cote + couronne lumineuse. */
                float lift = sinf(g->time * 2.5f) * 0.10f;
                /* aucun pied : flotte */
                gfx_box_draw(g->renderer,
                             v3_make(pos.x, h * 0.65f + lift, pos.z),
                             v3_make(w * 0.85f, h * 0.85f, w * 0.85f),
                             r, gg, b);
                /* tete */
                gfx_box_draw(g->renderer,
                             v3_make(pos.x, h + 0.30f + lift, pos.z),
                             v3_make(w * 0.65f, 0.42f, w * 0.65f),
                             r * 1.15f, gg * 1.15f, b * 1.15f);
                /* halo flottant */
                float hp = 0.7f + 0.3f * sinf(g->time * 3.f);
                gfx_box_draw(g->renderer,
                             v3_make(pos.x, h + 0.75f + lift, pos.z),
                             v3_make(w * 1.20f, 0.06f, w * 1.20f),
                             1.0f * hp, 0.90f * hp, 0.55f * hp);
                /* ailes : 2 grandes plates a chaque cote, animees */
                float flap = sinf(g->time * 3.5f) * 0.15f;
                gfx_box_draw(g->renderer,
                             v3_make(pos.x - (w + 0.30f), h * 0.80f + lift,
                                     pos.z),
                             v3_make(0.04f, 0.70f, 0.40f + flap),
                             0.95f, 0.92f, 0.80f);
                gfx_box_draw(g->renderer,
                             v3_make(pos.x + (w + 0.30f), h * 0.80f + lift,
                                     pos.z),
                             v3_make(0.04f, 0.70f, 0.40f + flap),
                             0.95f, 0.92f, 0.80f);
                /* yeux dores brillants */
                gfx_box_draw(g->renderer,
                             v3_make(pos.x - 0.15f, h + 0.35f + lift,
                                     pos.z + 0.35f),
                             v3_make(0.07f, 0.07f, 0.04f),
                             1.0f, 0.92f, 0.40f);
                gfx_box_draw(g->renderer,
                             v3_make(pos.x + 0.15f, h + 0.35f + lift,
                                     pos.z + 0.35f),
                             v3_make(0.07f, 0.07f, 0.04f),
                             1.0f, 0.92f, 0.40f);
                break;
            }
        }
        /* halo element au sol commun a tous */
        {
            float pulse = 0.18f + 0.10f * sinf(g->time * 3.f);
            gfx_box_draw(g->renderer,
                         v3_make(pos.x, 0.05f, pos.z),
                         v3_make(w * 1.5f + pulse * 2.f, 0.02f,
                                 w * 1.5f + pulse * 2.f),
                         er, eg, eb);
        }
        /* aura de phase : disque pulsant rouge en phase 2, jaune en
         * phase 1. Donne au joueur un signal visible que le boss
         * change de comportement. */
        if (phase >= 1) {
            float pulse = 0.5f + 0.5f * sinf(g->time * (phase == 2 ? 12.f : 6.f));
            float pr = (phase == 2) ? 1.0f * pulse : 1.0f * pulse;
            float pg = (phase == 2) ? 0.15f * pulse : 0.85f * pulse;
            float pb = (phase == 2) ? 0.10f * pulse : 0.25f * pulse;
            gfx_box_draw(g->renderer,
                         v3_make(pos.x, 0.10f, pos.z),
                         v3_make(w * 2.0f, 0.04f, w * 2.0f),
                         pr, pg, pb);
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
            /* cornes courbees */
            gfx_box_draw(g->renderer,
                         v3_make(pos.x - 0.18f, head_y + 0.30f, pos.z),
                         v3_make(0.10f, 0.20f, 0.10f), 0.4f, 0.15f, 0.2f);
            gfx_box_draw(g->renderer,
                         v3_make(pos.x + 0.18f, head_y + 0.30f, pos.z),
                         v3_make(0.10f, 0.20f, 0.10f), 0.4f, 0.15f, 0.2f);
            /* dents pointues */
            gfx_box_draw(g->renderer,
                         v3_make(pos.x, head_y - 0.10f, pos.z + w * 0.37f),
                         v3_make(0.18f, 0.06f, 0.04f),
                         1.f, 1.f, 1.f);
            /* ailes : 2 boxes plates sur les cotes, animees en sin */
            float wing_flap = sinf(g->time * 6.f + e->x) * 0.15f;
            gfx_box_draw(g->renderer,
                         v3_make(pos.x - (w/2 + 0.20f), body_y + 0.10f,
                                 pos.z - 0.05f),
                         v3_make(0.04f, 0.34f, 0.32f + wing_flap),
                         0.30f, 0.08f, 0.12f);
            gfx_box_draw(g->renderer,
                         v3_make(pos.x + (w/2 + 0.20f), body_y + 0.10f,
                                 pos.z - 0.05f),
                         v3_make(0.04f, 0.34f, 0.32f + wing_flap),
                         0.30f, 0.08f, 0.12f);
            /* halo de braise au pied */
            float ember = 0.5f + 0.5f * sinf(g->time * 4.f);
            gfx_box_draw(g->renderer,
                         v3_make(pos.x, 0.04f, pos.z),
                         v3_make(w * 1.4f, 0.02f, w * 1.4f),
                         1.0f * ember, 0.40f * ember, 0.10f * ember);
            break;
        }
        case EK_BANDIT: {
            /* masque noir au visage */
            gfx_box_draw(g->renderer,
                         v3_make(pos.x, head_y + 0.04f, pos.z + w * 0.36f),
                         v3_make(w * 0.65f, 0.13f, 0.04f),
                         0.05f, 0.05f, 0.07f);
            /* cape qui flotte derriere : ondulation sin sur Z arriere */
            float flap = sinf(g->time * 5.f + e->y) * 0.06f;
            gfx_box_draw(g->renderer,
                         v3_make(pos.x, body_y + 0.05f,
                                 pos.z - (w * 0.50f + flap)),
                         v3_make(w * 0.85f, (h - leg_h) * 0.95f, 0.04f),
                         0.55f, 0.10f, 0.10f);
            /* dague brillante dans la main droite */
            gfx_box_draw(g->renderer,
                         v3_make(pos.x + (w/2 + 0.16f), body_y, pos.z + 0.10f),
                         v3_make(0.04f, 0.04f, 0.18f),
                         0.90f, 0.92f, 0.95f);
            break;
        }
        case EK_ZOMBIE: {
            /* "blessure" verdatre sur le torse */
            gfx_box_draw(g->renderer,
                         v3_make(pos.x, body_y + 0.05f, pos.z + w * 0.43f),
                         v3_make(0.20f, 0.10f, 0.04f),
                         0.45f, 0.65f, 0.2f);
            /* bras tendus en avant pendant le telegraph de lunge
             * (ai_t2 > 0.18 = phase telegraphique, voir ai_zombie). */
            if (e->ai_t2 > 0.18f) {
                gfx_box_draw(g->renderer,
                             v3_make(pos.x - 0.12f, body_y + 0.05f,
                                     pos.z + w * 0.60f),
                             v3_make(0.10f, 0.10f, 0.30f),
                             r * 0.9f, gg * 0.9f, b * 0.9f);
                gfx_box_draw(g->renderer,
                             v3_make(pos.x + 0.12f, body_y + 0.05f,
                                     pos.z + w * 0.60f),
                             v3_make(0.10f, 0.10f, 0.30f),
                             r * 0.9f, gg * 0.9f, b * 0.9f);
                /* glow rouge au sol pour signaler le bond imminent */
                float lp = 0.5f + 0.5f * sinf(g->time * 18.f);
                gfx_box_draw(g->renderer,
                             v3_make(pos.x, 0.03f, pos.z),
                             v3_make(w * 1.2f, 0.02f, w * 1.2f),
                             1.0f * lp, 0.15f * lp, 0.10f * lp);
            }
            /* mache qui pendouille */
            gfx_box_draw(g->renderer,
                         v3_make(pos.x, head_y - 0.10f, pos.z + w * 0.34f),
                         v3_make(0.16f, 0.06f, 0.04f),
                         0.45f, 0.32f, 0.22f);
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
            /* socle / pedestal sous l'item, hauteur graduee par rarete. Donne
             * un repere visuel "loot drop" facile a repérer. Les uniques ont
             * un socle plus haut + une teinte chaude (orange). */
            uint32_t c = pk->item.is_unique ? 0xFF8030FF
                                            : rarity_color(pk->item.rarity);
            float rr = ((c>>24)&0xFF)/255.f;
            float gg2 = ((c>>16)&0xFF)/255.f;
            float bb = ((c>>8)&0xFF)/255.f;
            /* base sombre (socle) */
            gfx_box_draw(g->renderer, v3_make(pos.x, 0.05f, pos.z),
                         v3_make(0.55f, 0.08f, 0.55f),
                         0.20f, 0.18f, 0.22f);
            /* anneau coloré dessus pour signaler la rarete */
            float ph = 0.10f + 0.05f * (int)pk->item.rarity;
            gfx_box_draw(g->renderer, v3_make(pos.x, 0.10f, pos.z),
                         v3_make(0.42f, ph, 0.42f),
                         rr * 0.5f, gg2 * 0.5f, bb * 0.5f);
            /* item lui-meme (cube flottant) */
            r = rr; gg = gg2; b = bb; sz = 0.50f;
            /* uniques : aura clignotante au-dessus */
            if (pk->item.is_unique) {
                float pulse = 0.5f + 0.5f * sinf(g->time * 4.f);
                gfx_box_draw(g->renderer,
                             v3_make(pos.x, pos.y + 0.6f, pos.z),
                             v3_make(0.20f, 0.20f, 0.20f),
                             rr * pulse, gg2 * pulse, bb * pulse);
            }
            break;
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
    /* props decoratifs (assets.c) -- entre terrain et entites mobiles
     * pour le z-ordering correct. */
    world_assets_render(g);

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
    /* Nom des items poses au sol, visible quand le joueur s'approche.
     * On utilise une distance generous (90 px monde = ~6 tiles) pour
     * que les drops ne soient pas perdus dans le decor. */
    for (int i = 0; i < MAX_PICKUPS; i++) {
        Pickup *pk = &g->pickups[i];
        if (!pk->alive || pk->kind != PU_ITEM) continue;
        float dx = pk->x - g->player.x, dy = pk->y - g->player.y;
        if (dx * dx + dy * dy > 90.f * 90.f) continue;
        v3 head = v3_make(pk->x / TILE, 1.4f, pk->y / TILE);
        int sx, sy;
        if (!world_to_screen(gc, head, &sx, &sy)) continue;
        const char *nm = pk->item.name[0] ? pk->item.name
                                          : slot_name(pk->item.slot);
        uint32_t col = pk->item.is_unique ? 0xFF8030FF
                                          : rarity_color(pk->item.rarity);
        int tw = text_width(nm);
        /* fond sombre pour la lisibilite */
        gfx_set_blend(gc, true);
        fill_rect(gc, sx - tw/2 - 2, sy - 3, tw + 4, 9, 0x00000090);
        gfx_set_blend(gc, false);
        text_draw(gc, sx - tw/2, sy - 2, nm, col);
    }
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
    /* === BOSS INTRO CINEMATIQUE === */
    if (g->boss_intro_t > 0.f) {
        float t = g->boss_intro_t / 2.5f;        /* normalise 0..1 */
        if (t > 1.f) t = 1.f;
        /* letterbox noir top/bottom qui glisse depuis les bords */
        gfx_set_blend(gc, true);
        int bar_h = (int)(28.f * (1.f - t * 0.6f));
        if (bar_h > 28) bar_h = 28;
        fill_rect(gc, 0, 0, INTERNAL_W, bar_h, 0x000000F0);
        fill_rect(gc, 0, INTERNAL_H - bar_h, INTERNAL_W, bar_h, 0x000000F0);
        /* couleur element du biome pour la bande centrale */
        int bi = biome_for_floor(g->floor_index);
        uint32_t bcol = element_color(biome_element(bi));
        uint32_t bandc = (bcol & 0xFFFFFF00u) | 0x60;
        fill_rect(gc, 0, INTERNAL_H/2 - 22, INTERNAL_W, 44, 0x000000C0);
        fill_rect(gc, 0, INTERNAL_H/2 - 22, INTERNAL_W,  2, bandc);
        fill_rect(gc, 0, INTERNAL_H/2 + 20, INTERNAL_W,  2, bandc);
        gfx_set_blend(gc, false);
        /* "MENACE :" en petit, biome */
        text_drawf(gc, INTERNAL_W/2 - text_width("MENACE")/2,
                   INTERNAL_H/2 - 16, bcol, "MENACE");
        /* nom du boss en grand : double passe (1 px offset) pour bold-fake */
        int nw = text_width(g->boss_name);
        int nx = INTERNAL_W/2 - nw/2;
        int ny = INTERNAL_H/2 - 5;
        text_draw(gc, nx + 1, ny, g->boss_name, 0x000000FF);
        text_draw(gc, nx,     ny, g->boss_name, 0xFFFFFFFF);
        /* biome sous-titre */
        char sub[48]; snprintf(sub, sizeof(sub), "- %s -", biome_name(bi));
        text_draw(gc, INTERNAL_W/2 - text_width(sub)/2,
                  INTERNAL_H/2 + 8, sub, 0xCCCCCCFF);
    }

    /* === BOSS DEATH CINEMATIQUE === */
    if (g->boss_death_t > 0.f) {
        float t = g->boss_death_t / 1.8f;
        if (t > 1.f) t = 1.f;
        /* fade blanc plein-ecran qui s estompe (visible 0.5s) */
        if (g->boss_death_t > 1.3f) {
            int alpha = (int)(220 * (g->boss_death_t - 1.3f) / 0.5f);
            if (alpha > 0) {
                gfx_set_blend(gc, true);
                fill_rect(gc, 0, 0, INTERNAL_W, INTERNAL_H,
                          (uint32_t)0xFFFFFF00u | (uint32_t)(alpha & 0xFF));
                gfx_set_blend(gc, false);
            }
        }
        /* vignette dore pulsante */
        float pulse = 0.6f + 0.4f * sinf(g->time * 8.f);
        gfx_set_blend(gc, true);
        fill_rect(gc, 0, INTERNAL_H/2 - 14, INTERNAL_W, 28,
                  (uint32_t)0xFFD04000u | (uint32_t)(int)(140 * t * pulse));
        gfx_set_blend(gc, false);
        /* "VAINCU" en grand avec ombre noire */
        const char *msg = "VAINCU";
        int mw = text_width(msg);
        int mx = INTERNAL_W/2 - mw/2;
        int my = INTERNAL_H/2 - 3;
        text_draw(gc, mx + 1, my + 1, msg, 0x000000FF);
        text_draw(gc, mx,     my,     msg, 0xFFE060FF);
        /* nom du boss en dessous, plus discret */
        text_draw(gc, INTERNAL_W/2 - text_width(g->boss_name)/2,
                  INTERNAL_H/2 + 10, g->boss_name, 0xCCCCCCFF);
    }

    /* === HP BAR PLEIN ECRAN POUR LE BOSS ACTIF === */
    {
        /* trouve le boss vivant le plus proche, s il y en a un */
        Enemy *bb = NULL;
        for (int i = 0; i < MAX_ENEMIES; i++) {
            Enemy *e = &g->enemies[i];
            if (!e->alive || !e->is_boss) continue;
            if (e->dying_t > 0.f) continue;
            bb = e; break;
        }
        if (bb && g->boss_intro_t <= 0.f && g->boss_death_t <= 0.f) {
            int bx = 30, by = 8, bw = INTERNAL_W - 60, bh = 8;
            /* fond noir + bord */
            gfx_set_blend(gc, true);
            fill_rect(gc, bx - 1, by - 1, bw + 2, bh + 2, 0x000000E0);
            gfx_set_blend(gc, false);
            /* fond rouge sombre */
            fill_rect(gc, bx, by, bw, bh, 0x401010FF);
            /* fill couleur element */
            float frac = bb->hp / bb->maxhp;
            if (frac < 0.f) frac = 0.f;
            uint32_t ecol = element_color(bb->element);
            int fillw = (int)(bw * frac);
            fill_rect(gc, bx, by, fillw, bh, ecol);
            /* highlight top 1px */
            fill_rect(gc, bx, by, fillw, 1, 0xFFFFFFC0);
            /* phase ticks : trait noir vertical a 55% et 25% */
            int t55 = bx + (int)(bw * 0.55f);
            int t25 = bx + (int)(bw * 0.25f);
            fill_rect(gc, t55, by - 2, 1, bh + 4, 0xFFFFFF80);
            fill_rect(gc, t25, by - 2, 1, bh + 4, 0xFFFFFF80);
            /* enrage : flash rouge si <10% */
            if (frac < 0.10f) {
                float pulse = 0.5f + 0.5f * sinf(g->time * 14.f);
                gfx_set_blend(gc, true);
                fill_rect(gc, bx, by, bw, bh,
                          (uint32_t)0xFF000000u | (uint32_t)(int)(100 * pulse));
                gfx_set_blend(gc, false);
            }
            /* contour */
            rect_outline(gc, bx, by, bw, bh, 0xFFD060FF);
            /* nom au-dessus */
            int tw = text_width(bb->name[0] ? bb->name : g->boss_name);
            text_draw(gc, INTERNAL_W/2 - tw/2, by + bh + 2,
                      bb->name[0] ? bb->name : g->boss_name, 0xFFFFFFFF);
            /* tag "ENRAGE" en rouge a droite si frac < 10% */
            if (frac < 0.10f) {
                text_draw(gc, bx + bw - text_width("ENRAGE"),
                          by + bh + 2, "ENRAGE", 0xFF4040FF);
            }
        }
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

