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
/* tint additionnel pour les murs fissures (plus sombre + plus bas) */
static float s_wall_tint = 1.f;
static float s_wall_height = 0.f;       /* override de hauteur ; 0 = WALL_H */

static void emit_wall_column(float x, float z) {
    float x0 = x, x1 = x + 1.f;
    float z0 = z, z1 = z + 1.f;
    float y0 = 0.f, y1 = (s_wall_height > 0.f) ? s_wall_height : WALL_H;
    float r, g, b; wall_color_side(&r, &g, &b);
    r *= s_biome_r * s_wall_tint;
    g *= s_biome_g * s_wall_tint;
    b *= s_biome_b * s_wall_tint;
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
    tr *= s_biome_r * s_wall_tint;
    tg *= s_biome_g * s_wall_tint;
    tb *= s_biome_b * s_wall_tint;
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
                s_wall_tint = 1.f; s_wall_height = 0.f;
                emit_wall_column((float)x, (float)y);
            } else if (t == T_WALL_CRACKED) {
                /* murs fissures : plus bas (0.7) + tint plus sombre. */
                s_wall_tint = 0.55f; s_wall_height = 0.70f;
                emit_wall_column((float)x, (float)y);
                s_wall_tint = 1.f;   s_wall_height = 0.f;
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
        case EK_ZOMBIE:      *r=0.31f;*g=0.50f;*b=0.25f; break;
        case EK_BANDIT:      *r=0.50f;*g=0.38f;*b=0.25f; break;
        case EK_DEMON:       *r=0.63f;*g=0.13f;*b=0.25f; break;
        case EK_SLIME:       *r=0.25f;*g=0.63f;*b=0.63f; break;
        case EK_RAT:         *r=0.30f;*g=0.18f;*b=0.14f; break;
        case EK_GHOST:       *r=0.65f;*g=0.65f;*b=0.85f; break;
        case EK_CHARGER:     *r=0.55f;*g=0.30f;*b=0.18f; break;
        case EK_MAGE:        *r=0.35f;*g=0.20f;*b=0.55f; break;
        case EK_HEALER:      *r=0.95f;*g=0.85f;*b=0.55f; break;
        case EK_BUFFER:      *r=0.65f;*g=0.65f;*b=0.75f; break;
        case EK_NECROMANCER: *r=0.20f;*g=0.13f;*b=0.30f; break;
        case EK_BOSS:   *r=1.00f;*g=0.13f;*b=0.50f; break;
        default:        *r=0.5f;*g=0.5f;*b=0.5f; break;
    }
    if (e->hit_flash > 0.f) { *r = 1.f; *g = 1.f; *b = 1.f; }
}

v3 player_world_pos(Player *p);
v3 player_world_pos(Player *p) {
    return v3_make(p->x / TILE, 0.f, p->y / TILE);
}

void draw_player_3d(Game *g);
void draw_player_3d(Game *g) {
    Player *p = &g->player;
    bool blink = p->invuln_t > 0.f && (((int)(g->time * 24.f)) % 2 == 0);
    /* I-frame visible : pendant les invuln, anneau jaune-rouge au pied
     * du joueur. Telegraphie clairement l etat "j en cours d echapper",
     * tres lisible pour predire les frames. */
    if (p->invuln_t > 0.f) {
        v3 base = player_world_pos(p);
        float k = p->invuln_t / 0.60f;       /* 1 a 0 */
        if (k > 1.f) k = 1.f;
        /* pulse rapide (10Hz) qui s ralentit en fin d invuln */
        float pulse = 0.5f + 0.5f * sinf(g->time * 22.f);
        float r = 0.55f + 0.10f * pulse;
        float cr = 1.0f, cg = 0.55f + 0.3f * (1.f - k), cb = 0.20f;
        int n_ring = 16;
        for (int i = 0; i < n_ring; i++) {
            float a = (i / (float)n_ring) * 6.2831f;
            v3 spot = v3_make(base.x + cosf(a) * r,
                              0.03f + 0.02f * pulse,
                              base.z + sinf(a) * r);
            gfx_box_draw(g->renderer, spot,
                         v3_make(0.10f, 0.04f, 0.10f),
                         cr, cg, cb);
        }
    }
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

    /* Influence esthetique de l'equipement : chaque slot occupe tinte
     * sa partie du corps avec la couleur de rarete (ou orange unique).
     * Mix : 60% couleur base + 40% couleur item, sauf legendaire/unique
     * qui pousse a 60% (plus dramatique). */
    Item *eq_helm   = &p->equipped[SLOT_HELM];
    Item *eq_chest  = &p->equipped[SLOT_CHEST];
    Item *eq_legs   = &p->equipped[SLOT_LEGS];
    Item *eq_boots  = &p->equipped[SLOT_BOOTS];
    Item *eq_belt   = &p->equipped[SLOT_BELT];
    Item *eq_gloves = &p->equipped[SLOT_GLOVES];

    #define EQUIP_TINT(it, rOut, gOut, bOut) do { \
        if ((it)->occupied) { \
            uint32_t cc = (it)->is_unique ? 0xFF8030FF : rarity_color((it)->rarity); \
            /* si l'item a un element dominant, on prend sa couleur a la \
             * place du rarity-color : l'esthetique elementaire prime. */ \
            Element el_ = item_element(it); \
            if (el_ > EL_NONE && el_ < EL_COUNT) cc = element_color(el_); \
            float tr = ((cc>>24)&0xFF)/255.f; \
            float tg = ((cc>>16)&0xFF)/255.f; \
            float tb = ((cc>>8)&0xFF)/255.f; \
            float mx = ((it)->rarity >= R_LEGENDARY || (it)->is_unique) ? 0.60f : 0.40f; \
            if (el_ > EL_NONE) mx = 0.70f;       /* element : tint plus marque */ \
            rOut = rOut * (1.f - mx) + tr * mx; \
            gOut = gOut * (1.f - mx) + tg * mx; \
            bOut = bOut * (1.f - mx) + tb * mx; \
        } \
    } while (0)

    /* Couleurs effectives des parties */
    float chest_r = br, chest_g = bg, chest_b = bb;
    EQUIP_TINT(eq_chest, chest_r, chest_g, chest_b);
    float leg_r = hr * 0.55f, leg_g = hg * 0.55f, leg_b = hb * 0.55f;
    EQUIP_TINT(eq_legs, leg_r, leg_g, leg_b);
    float boots_r = leg_r * 0.7f, boots_g = leg_g * 0.7f, boots_b = leg_b * 0.7f;
    EQUIP_TINT(eq_boots, boots_r, boots_g, boots_b);
    float arm_r = chest_r * 1.05f, arm_g = chest_g * 1.05f, arm_b = chest_b * 1.05f;
    EQUIP_TINT(eq_gloves, arm_r, arm_g, arm_b);

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
                 leg_r, leg_g, leg_b);
    gfx_box_draw(g->renderer,
                 v3_make(pos.x - side_x * leg_off, 0.20f + leg_lift_r, pos.z - side_z * leg_off),
                 v3_make(0.18f, 0.40f, 0.20f),
                 leg_r, leg_g, leg_b);
    /* bottes : petits cubes plus sombres aux pieds (visibles si equipees) */
    if (eq_boots->occupied) {
        gfx_box_draw(g->renderer,
                     v3_make(pos.x + side_x * leg_off, 0.04f + leg_lift_l,
                             pos.z + side_z * leg_off),
                     v3_make(0.22f, 0.08f, 0.24f),
                     boots_r, boots_g, boots_b);
        gfx_box_draw(g->renderer,
                     v3_make(pos.x - side_x * leg_off, 0.04f + leg_lift_r,
                             pos.z - side_z * leg_off),
                     v3_make(0.22f, 0.08f, 0.24f),
                     boots_r, boots_g, boots_b);
    }

    /* corps (tunique) */
    gfx_box_draw(g->renderer,
                 v3_make(pos.x, 0.62f + bob, pos.z),
                 v3_make(0.50f, 0.46f, 0.40f),
                 chest_r, chest_g, chest_b);
    /* ceinture : fine bande sous le torse si equipee */
    if (eq_belt->occupied) {
        uint32_t cc = eq_belt->is_unique ? 0xFF8030FF : rarity_color(eq_belt->rarity);
        float trr = ((cc>>24)&0xFF)/255.f;
        float tgg = ((cc>>16)&0xFF)/255.f;
        float tbb = ((cc>>8)&0xFF)/255.f;
        gfx_box_draw(g->renderer,
                     v3_make(pos.x, 0.40f + bob, pos.z),
                     v3_make(0.52f, 0.08f, 0.42f),
                     trr, tgg, tbb);
    }

    /* bras : 2 cubes lateraux */
    gfx_box_draw(g->renderer,
                 v3_make(pos.x + side_x * 0.32f, 0.62f + bob - swing * 0.5f,
                         pos.z + side_z * 0.32f),
                 v3_make(0.14f, 0.34f, 0.14f),
                 arm_r, arm_g, arm_b);
    gfx_box_draw(g->renderer,
                 v3_make(pos.x - side_x * 0.32f, 0.62f + bob + swing * 0.5f,
                         pos.z - side_z * 0.32f),
                 v3_make(0.14f, 0.34f, 0.14f),
                 arm_r, arm_g, arm_b);

    /* tete (peau) */
    gfx_box_draw(g->renderer,
                 v3_make(pos.x + face_x * 0.02f, 1.05f + bob, pos.z + face_z * 0.02f),
                 v3_make(0.36f, 0.36f, 0.36f),
                 0.91f, 0.75f, 0.54f);
    /* casque : surimpose la tete avec la couleur d'item rarete */
    if (eq_helm->occupied) {
        uint32_t cc = eq_helm->is_unique ? 0xFF8030FF : rarity_color(eq_helm->rarity);
        float trr = ((cc>>24)&0xFF)/255.f;
        float tgg = ((cc>>16)&0xFF)/255.f;
        float tbb = ((cc>>8)&0xFF)/255.f;
        /* dome */
        gfx_box_draw(g->renderer,
                     v3_make(pos.x + face_x * 0.02f, 1.20f + bob, pos.z + face_z * 0.02f),
                     v3_make(0.40f, 0.20f, 0.40f),
                     trr, tgg, tbb);
        /* visiere sombre devant */
        gfx_box_draw(g->renderer,
                     v3_make(pos.x + face_x * 0.18f, 1.08f + bob,
                             pos.z + face_z * 0.18f),
                     v3_make(0.30f, 0.06f, 0.06f),
                     0.10f, 0.08f, 0.12f);
    }
    /* Aura particles :
     *   - elements equipes : sparks de la couleur d'element qui montent
     *     (priorite haute, plus dense)
     *   - legendaire / unique : aura gold/orange (fallback)
     */
    {
        Item *all_eq[6] = { eq_helm, eq_chest, eq_legs, eq_boots, eq_belt, eq_gloves };
        int legcount = 0;
        Element elem_seen[6]; int n_el = 0;
        for (int i = 0; i < 6; i++) {
            if (!all_eq[i]->occupied) continue;
            if (all_eq[i]->rarity >= R_LEGENDARY || all_eq[i]->is_unique) legcount++;
            Element el = item_element(all_eq[i]);
            if (el > EL_NONE && el < EL_COUNT) {
                /* eviter doublons */
                bool dup = false;
                for (int j = 0; j < n_el; j++) if (elem_seen[j] == el) { dup = true; break; }
                if (!dup) elem_seen[n_el++] = el;
            }
        }
        /* sparks elementaires (priorite) */
        if (n_el > 0 && (rand() % 100) < 18) {
            Element pick = elem_seen[rand() % n_el];
            uint32_t col = element_color(pick);
            particle_spawn_kind(g, p->x + (rand()%18)-9, p->y - 14,
                                (rand()%8)-4, -14.f,
                                0.65f, col, 1.5f, 0);
        }
        /* fallback gold/orange si legendaire sans element */
        if (n_el == 0 && legcount > 0 && (rand() % 100) < 8) {
            uint32_t col = legcount >= 3 ? 0xFF8030FF : 0xFFD040FF;
            particle_spawn_kind(g, p->x + (rand()%14)-7, p->y - 18,
                                (rand()%10)-5, -12.f,
                                0.6f, col, 1.4f, 0);
        }
    }
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

    /* === ARME DANS LA MAIN DROITE ===
     * Chaque kind a son timing (anim_dur) et son geste propre :
     *   sword : arc rapide + motion-blur (ghost copies)
     *   axe   : chop overhead (haut -> bas), trail epais
     *   bow   : bow visible avec corde tendue + fleche puis snap release
     *   wand  : pointe trace une ellipse, sparkles
     *   shield: bash avance courte + ring de lumiere
     *   fists : jab (anim_kind 5)
     */
    {
        Weapon *w = &p->weapons[p->active_weapon];
        float dur = (p->anim_dur > 0.001f) ? p->anim_dur : 0.18f;
        float ap = (p->anim_t > 0.f) ? (1.f - p->anim_t / dur) : 0.f;
        if (ap < 0.f) ap = 0.f;
        if (ap > 1.f) ap = 1.f;
        float arc = sinf(ap * 3.1416f);    /* bell : pic a 0.5 */

        float ax = p->anim_dir_x, az = p->anim_dir_y;
        if (ax * ax + az * az < 0.001f) { ax = face_x; az = face_z; }
        float al = sqrtf(ax * ax + az * az);
        if (al > 0.001f) { ax /= al; az /= al; }
        /* perpendiculaire a l'aim (utile pour wobble bow + sparkles wand) */
        float perp_x = -az;
        float perp_z =  ax;

        if (w->kind != W_FISTS) {
            float wr = 0.9f, wg = 0.9f, wb = 0.95f;
            float wlen = 0.50f, wsize = 0.10f;
            switch (w->kind) {
                case W_SWORD:  wr=0.92f; wg=0.92f; wb=0.96f; wlen=0.55f; break;
                case W_AXE:    wr=0.65f; wg=0.65f; wb=0.70f; wlen=0.50f; wsize=0.14f; break;
                case W_BOW:    wr=0.55f; wg=0.35f; wb=0.18f; wlen=0.10f; wsize=0.06f; break;
                case W_WAND:   wr=0.62f; wg=0.32f; wb=0.95f; wlen=0.45f; break;
                case W_SHIELD: wr=0.72f; wg=0.62f; wb=0.32f; wlen=0.45f; wsize=0.20f; break;
                default: break;
            }
            /* position de base : main droite */
            float hx = pos.x + side_x * 0.42f + face_x * 0.10f;
            float hz = pos.z + side_z * 0.42f + face_z * 0.10f;
            float hy = 0.62f + bob - swing * 0.5f;
            bool drew_main = true;

            switch (p->anim_kind) {
                case 0: { /* SWORD : slash transversal (cote -> autre cote).
                           * anim_flip alterne le sens : 1-2-1-2 visible. */
                    float flip = (p->anim_flip != 0) ? (float)p->anim_flip : 1.f;
                    /* perpendiculaire a l aim (cote vers cote) */
                    float perp_x_lcl = -az * flip;
                    float perp_z_lcl =  ax * flip;
                    /* side amount : +0.45 -> -0.45 sur l anim */
                    float side_t = (1.f - 2.f * ap) * 0.45f;
                    /* forward : bell de 0.30 max au milieu */
                    float fwd_t = arc * 0.30f;
                    hx = pos.x + face_x * 0.10f + perp_x_lcl * side_t + ax * fwd_t;
                    hz = pos.z + face_z * 0.10f + perp_z_lcl * side_t + az * fwd_t;
                    hy += 0.08f * sinf(ap * 3.1416f * 2.f);
                    /* crescent trail : 6 positions le long de l'arc derriere
                     * la pointe, formant la trainee de la lame. */
                    if (ap > 0.05f) {
                        int nseg = 6;
                        for (int gi = 1; gi <= nseg; gi++) {
                            float gap = ap - 0.04f * gi;
                            if (gap < 0.f) break;
                            float gside = (1.f - 2.f * gap) * 0.45f;
                            float gfwd  = sinf(gap * 3.1416f) * 0.30f;
                            float gx = pos.x + face_x * 0.10f + perp_x_lcl * gside + ax * gfwd;
                            float gz = pos.z + face_z * 0.10f + perp_z_lcl * gside + az * gfwd;
                            float fade = 1.f - (float)gi / (float)(nseg + 1);
                            gfx_box_draw(g->renderer,
                                v3_make(gx, hy, gz),
                                v3_make(0.06f * fade, 0.04f, 0.06f * fade),
                                wr * fade, wg * fade, wb * fade);
                        }
                    }
                    break;
                }
                case 1: { /* AXE : chop overhead. Phase 1 (0..0.5) lever
                           * en haut/arriere, phase 2 (0.5..1) chute vers
                           * l'avant. anim_flip alterne le cote du lift. */
                    float flip = (p->anim_flip != 0) ? (float)p->anim_flip : 1.f;
                    float p1 = ap < 0.5f ? ap * 2.f : 1.f;
                    float p2 = ap > 0.5f ? (ap - 0.5f) * 2.f : 0.f;
                    /* lever du cote alternant */
                    hx = pos.x + face_x * (-0.05f * p1) + side_x * 0.28f * flip;
                    hz = pos.z + face_z * (-0.05f * p1) + side_z * 0.28f * flip;
                    hy = 0.62f + bob + 0.85f * p1;
                    /* chop : pousse en avant + descend */
                    hx += ax * (0.55f * p2);
                    hz += az * (0.55f * p2);
                    hy -= 0.85f * p2;
                    /* trail epais : 4 positions le long de la trajectoire */
                    if (p2 > 0.1f) {
                        for (int gi = 1; gi <= 4; gi++) {
                            float t = p2 - 0.10f * gi;
                            if (t < 0.f) continue;
                            float ghx = pos.x + side_x * 0.28f * flip + ax * (0.55f * t);
                            float ghz = pos.z + side_z * 0.28f * flip + az * (0.55f * t);
                            float ghy = 0.62f + bob + 0.85f * (1.f - t);
                            float fade = 1.f - gi * 0.18f;
                            gfx_box_draw(g->renderer,
                                v3_make(ghx, ghy, ghz),
                                v3_make(0.10f, 0.05f * fade, 0.10f),
                                0.95f * fade, 0.85f * fade, 0.50f * fade);
                        }
                    }
                    /* IMPACT AU SOL : a la fin de la chute (p2 ~ 0.6-1.0),
                     * spawn de dust + shake supplementaire. Une seule fois par
                     * anim, gate sur la frame ou on franchit le seuil. */
                    if (p2 > 0.55f && p2 < 0.75f) {
                        /* tip de la hache au sol */
                        float tipx = pos.x + ax * 0.55f + side_x * 0.28f * flip;
                        float tipz = pos.z + az * 0.55f + side_z * 0.28f * flip;
                        /* limite a 1 burst par anim via le trail_count comme
                         * marqueur "deja fait" : on note la phase atteinte. */
                        if (p->trail_count == 0) {
                            p->trail_count = 1;
                            int nb = 12;
                            for (int di = 0; di < nb; di++) {
                                float a = (di / (float)nb) * 6.2831f;
                                particle_spawn_kind(g,
                                    tipx * TILE, tipz * TILE,
                                    cosf(a) * 90.f, sinf(a) * 90.f,
                                    0.50f, 0x806040C0, 2.4f, 0);
                            }
                            /* shake-burst supplementaire au moment de l'impact */
                            if (g->shake_t < 0.25f) g->shake_t = 0.25f;
                            if (g->shake_mag < 7.f) g->shake_mag = 7.f;
                        }
                    }
                    break;
                }
                case 2: { /* SHIELD : bash avance + ring de lumiere bref */
                    hx += ax * 0.30f * arc;
                    hz += az * 0.30f * arc;
                    if (arc > 0.4f) {
                        int n_ring = 12;
                        for (int ri = 0; ri < n_ring; ri++) {
                            float ang = (ri / (float)n_ring) * 6.2831f;
                            float rad = 0.50f + arc * 0.20f;
                            gfx_box_draw(g->renderer,
                                v3_make(pos.x + cosf(ang) * rad + ax * 0.35f,
                                        0.55f + bob,
                                        pos.z + sinf(ang) * rad + az * 0.35f),
                                v3_make(0.06f, 0.03f, 0.06f),
                                1.f, 0.85f, 0.40f);
                        }
                    }
                    break;
                }
                case 3: { /* WAND : la pointe trace une ellipse + trail
                           * persistant des 8 dernieres positions. */
                    float angle = ap * 6.2831f;
                    float orb_x = cosf(angle) * 0.22f;
                    float orb_y = sinf(angle) * 0.12f;
                    hx = pos.x + face_x * 0.18f + side_x * 0.28f + ax * orb_x;
                    hz = pos.z + face_z * 0.18f + side_z * 0.28f + az * orb_x
                         + perp_x * orb_x * 0.5f;
                    hy = 0.62f + bob + orb_y;
                    /* tip = bout du baton, leg au-dessus du pommeau */
                    float tipx = hx;
                    float tipy = 0.92f + bob + orb_y;
                    float tipz = hz;
                    /* enregistre la position dans le ring buffer */
                    int N = (int)(sizeof(p->trail_x)/sizeof(p->trail_x[0]));
                    p->trail_x[p->trail_head] = tipx;
                    p->trail_y[p->trail_head] = tipy;
                    p->trail_z[p->trail_head] = tipz;
                    p->trail_head = (p->trail_head + 1) % N;
                    if (p->trail_count < N) p->trail_count++;
                    /* Trail allege : 4 dernieres positions seulement, plus
                     * fines et plus pales. L'ancienne trainee de 8 cubes
                     * couvrait le champ de vision et masquait les
                     * ennemis a courte portee. */
                    int trail_max = 4;
                    if (trail_max > p->trail_count) trail_max = p->trail_count;
                    for (int ti = 0; ti < trail_max; ti++) {
                        int idx = (p->trail_head - 1 - ti + N) % N;
                        float age = (float)ti / (float)trail_max;
                        float fade = 1.f - age;
                        float sz = 0.06f * fade;
                        if (sz < 0.02f) continue;
                        float cr = 0.95f * fade + 0.05f;
                        float cg = 0.65f * fade + 0.20f;
                        float cb = 1.00f * fade + 0.30f;
                        gfx_box_draw(g->renderer,
                            v3_make(p->trail_x[idx], p->trail_y[idx], p->trail_z[idx]),
                            v3_make(sz, sz, sz), cr, cg, cb);
                    }
                    /* sparkles a la pointe : un par frame */
                    particle_spawn_kind(g,
                        tipx * TILE, tipz * TILE,
                        (rand()/(float)RAND_MAX - 0.5f) * 30.f,
                        (rand()/(float)RAND_MAX - 0.5f) * 30.f,
                        0.35f,
                        (rand()%2) ? 0xC0A0FFFF : 0xFFC0F0FF,
                        1.8f, 0);
                    break;
                }
                case 4: { /* BOW : visuel complet (limbes + corde + fleche)
                           * Phase 1 (0..0.6) : draw (corde tirees vers
                           * l'arriere, fleche tendue) ; phase 2 (>=0.6) :
                           * release (snap forward, fleche partie). */
                    drew_main = false;     /* on dessine custom plus bas */
                    /* main + bras tendu en avant */
                    float bow_x = pos.x + face_x * 0.35f + side_x * 0.10f;
                    float bow_z = pos.z + face_z * 0.35f + side_z * 0.10f;
                    float bow_y = 0.65f + bob;
                    /* recul du bras d'arc pendant le draw */
                    if (ap < 0.6f) {
                        float draw = ap / 0.6f;
                        bow_x -= ax * 0.05f * draw;
                        bow_z -= az * 0.05f * draw;
                    }
                    /* 2 limbes : haut et bas du bois, perpendiculaire a l'aim */
                    float limb_y_top = bow_y + 0.30f;
                    float limb_y_bot = bow_y - 0.30f;
                    gfx_box_draw(g->renderer,
                        v3_make(bow_x, limb_y_top, bow_z),
                        v3_make(0.08f, 0.22f, 0.08f),
                        wr, wg, wb);
                    gfx_box_draw(g->renderer,
                        v3_make(bow_x, limb_y_bot, bow_z),
                        v3_make(0.08f, 0.22f, 0.08f),
                        wr, wg, wb);
                    /* poignee centrale */
                    gfx_box_draw(g->renderer,
                        v3_make(bow_x, bow_y, bow_z),
                        v3_make(0.06f, 0.20f, 0.06f),
                        wr * 0.7f, wg * 0.7f, wb * 0.7f);
                    /* corde : 2 segments diagonaux vers le point d ancrage
                     * (la main qui tire). En draw, ce point recule vers le
                     * joueur. En release, il revient au plat avec un wobble
                     * de quelques frames (corde qui vibre). */
                    float pull;
                    if (ap < 0.6f) pull = 0.05f + (ap / 0.6f) * 0.30f;
                    else           pull = 0.35f * (1.f - (ap - 0.6f) / 0.4f);
                    if (pull < 0.f) pull = 0.f;
                    /* wobble post-release : sinusoide amortie 25Hz pendant la
                     * phase de retour. Visible mais discret. */
                    float wobble = 0.f;
                    if (ap > 0.60f && ap < 0.95f) {
                        float rel_t = (ap - 0.60f) / 0.35f;       /* 0..1 */
                        float decay = 1.f - rel_t;
                        wobble = sinf(rel_t * 25.f) * 0.05f * decay;
                    }
                    float anchor_x = bow_x - ax * pull + perp_x * wobble;
                    float anchor_z = bow_z - az * pull + perp_z * wobble;
                    /* corde haut */
                    for (int si = 0; si < 4; si++) {
                        float t = si / 4.f;
                        float sx = bow_x * (1.f - t) + anchor_x * t;
                        float sz = bow_z * (1.f - t) + anchor_z * t;
                        float sy = limb_y_top * (1.f - t) + bow_y * t;
                        gfx_box_draw(g->renderer,
                            v3_make(sx, sy, sz),
                            v3_make(0.02f, 0.02f, 0.02f),
                            0.95f, 0.95f, 0.85f);
                        sy = limb_y_bot * (1.f - t) + bow_y * t;
                        gfx_box_draw(g->renderer,
                            v3_make(sx, sy, sz),
                            v3_make(0.02f, 0.02f, 0.02f),
                            0.95f, 0.95f, 0.85f);
                    }
                    /* fleche : visible pendant le draw, disparait au release */
                    if (ap < 0.65f) {
                        float arrow_back = ap < 0.6f ? (ap / 0.6f) * 0.30f : 0.30f;
                        /* tige */
                        for (int ai = 0; ai < 5; ai++) {
                            float t = ai / 4.f;          /* 0 = pointe, 1 = arriere */
                            float seg_x = bow_x + ax * (0.10f - t * 0.40f) - ax * arrow_back;
                            float seg_z = bow_z + az * (0.10f - t * 0.40f) - az * arrow_back;
                            gfx_box_draw(g->renderer,
                                v3_make(seg_x, bow_y, seg_z),
                                v3_make(0.03f, 0.03f, 0.03f),
                                0.85f, 0.65f, 0.40f);
                        }
                    } else {
                        /* flash de release au point d'ancrage */
                        float flash_k = 1.f - (ap - 0.65f) / 0.35f;
                        if (flash_k > 0.f) {
                            gfx_box_draw(g->renderer,
                                v3_make(bow_x, bow_y, bow_z),
                                v3_make(0.12f * flash_k, 0.12f * flash_k, 0.12f * flash_k),
                                1.f, 0.95f, 0.6f);
                        }
                    }
                    break;
                }
                default: break;
            }

            if (drew_main) {
                gfx_box_draw(g->renderer,
                    v3_make(hx, hy, hz),
                    v3_make(wsize, wlen, wsize),
                    wr, wg, wb);
            }
            /* eclat magique au bout du baton */
            if (w->kind == W_WAND) {
                float gw = 0.16f + 0.10f * arc;
                gfx_box_draw(g->renderer,
                    v3_make(hx, 0.92f + bob, hz),
                    v3_make(gw, gw, gw),
                    0.95f, 0.65f, 1.0f);
            }
            /* trail sword classique (l'axe a deja son trail epais) */
            if (w->kind == W_SWORD && arc > 0.2f) {
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
    if (e->is_archmage) {
        float g_ = e->arch_grow;
        if (g_ < 0.3f) g_ = 0.3f;
        h = 1.8f + g_ * 1.6f;
        w = 1.2f + g_ * 1.0f;
    }
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

    /* wobble pour le slime (utilise plus bas) */
    float wobble = (e->kind == EK_SLIME) ? sinf(g->time * 6.f + e->x) * 0.08f : 0.f;

    /* ombre */
    gfx_box_draw(g->renderer,
                 v3_make(pos.x, 0.005f, pos.z),
                 v3_make(w + 0.05f, 0.01f, w + 0.05f),
                 0.02f, 0.01f, 0.04f);

    /* SLIME : grosse bulle + reflet + noyau interne + drip occasionnel.
     * Pendant windup, squash-stretch (s'aplatit puis s'etire avant le bond). */
    if (e->kind == EK_SLIME) {
        float squash = 0.f, stretch = 0.f;
        if (e->telegraph_t > 0.f) {
            float t = 1.f - e->telegraph_t / 1.0f;
            if (t < 0.f) t = 0.f;
            if (t > 1.f) t = 1.f;
            /* premiere moitie : s'aplatit, deuxieme : s'etire */
            if (t < 0.5f) { squash = t * 2.f * 0.20f; }
            else          { stretch = (t - 0.5f) * 2.f * 0.30f; }
        }
        float sw = w + wobble * 0.5f + squash * 0.5f - stretch * 0.3f;
        float sh = h + wobble - squash * 0.4f + stretch * 0.5f;
        /* corps externe */
        gfx_box_draw(g->renderer,
                     v3_make(pos.x, sh * 0.5f + wobble, pos.z),
                     v3_make(sw, sh, sw),
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
     * shake violent + aura rouge clignotante. */
    if (e->kind == EK_CHARGER) {
        int state = (int)e->ai_t2;
        float shake_x = 0.f, shake_z = 0.f;
        if (state == 1) {
            shake_x = (sinf(g->time * 60.f) * 0.04f);
            shake_z = (cosf(g->time * 52.f) * 0.04f);
        }
        /* gros corps bas */
        gfx_box_draw(g->renderer,
                     v3_make(pos.x + shake_x, 0.40f, pos.z + shake_z),
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

    /* HEALER (Hierophante) : silhouette en robe doree + halo cardinal
     * + sceptre lumineux. Pulse jaune en continu pour signaler son role. */
    if (e->kind == EK_HEALER) {
        float pulse = 0.7f + 0.3f * sinf(g->time * 4.f);
        /* halo dore au sol (toujours visible, plus large pendant pulse) */
        gfx_box_draw(g->renderer,
                     v3_make(pos.x, 0.04f, pos.z),
                     v3_make(w * 2.0f + 0.20f * pulse, 0.02f,
                             w * 2.0f + 0.20f * pulse),
                     1.0f * pulse, 0.85f * pulse, 0.30f * pulse);
        /* robe blanche */
        gfx_box_draw(g->renderer, v3_make(pos.x, h * 0.40f, pos.z),
                     v3_make(w * 1.05f, h * 0.85f, w * 1.05f),
                     r, gg, b);
        /* mitre haute pointu */
        gfx_box_draw(g->renderer, v3_make(pos.x, h + 0.30f, pos.z),
                     v3_make(0.30f, 0.40f, 0.30f),
                     1.0f, 0.92f, 0.55f);
        gfx_box_draw(g->renderer, v3_make(pos.x, h + 0.55f, pos.z),
                     v3_make(0.10f, 0.12f, 0.10f),
                     1.0f, 0.95f, 0.65f);
        /* sceptre lumineux dans la main droite */
        gfx_box_draw(g->renderer,
                     v3_make(pos.x + (w / 2 + 0.18f), h * 0.50f, pos.z),
                     v3_make(0.06f, 0.55f, 0.06f),
                     0.80f, 0.60f, 0.30f);
        float gp = 0.6f + 0.4f * sinf(g->time * 6.f);
        gfx_box_draw(g->renderer,
                     v3_make(pos.x + (w / 2 + 0.18f), h * 0.85f, pos.z),
                     v3_make(0.10f, 0.10f, 0.10f),
                     1.0f * gp, 0.90f * gp, 0.45f * gp);
        return;
    }

    /* BUFFER (Totem) : statique, totem en pierre avec un crystal qui
     * pulse + 3 anneaux qui flottent autour. Pas de jambes. */
    if (e->kind == EK_BUFFER) {
        float pulse = 0.6f + 0.4f * sinf(g->time * 3.f);
        /* base trapue */
        gfx_box_draw(g->renderer, v3_make(pos.x, 0.20f, pos.z),
                     v3_make(0.55f, 0.40f, 0.55f),
                     r * 0.7f, gg * 0.7f, b * 0.7f);
        /* colonne moyenne */
        gfx_box_draw(g->renderer, v3_make(pos.x, 0.62f, pos.z),
                     v3_make(0.42f, 0.45f, 0.42f),
                     r * 0.9f, gg * 0.9f, b * 0.9f);
        /* tete avec gemme acier */
        gfx_box_draw(g->renderer, v3_make(pos.x, 1.00f, pos.z),
                     v3_make(0.32f, 0.22f, 0.32f),
                     r, gg, b);
        gfx_box_draw(g->renderer, v3_make(pos.x, 1.20f, pos.z),
                     v3_make(0.16f, 0.18f, 0.16f),
                     0.85f * pulse, 0.92f * pulse, 1.0f * pulse);
        /* 3 anneaux qui orbitent en hauteur differente */
        for (int ri = 0; ri < 3; ri++) {
            float ang = g->time * (1.5f + ri * 0.5f) + ri * 2.f;
            float radius = 0.45f + ri * 0.05f;
            gfx_box_draw(g->renderer,
                v3_make(pos.x + cosf(ang) * radius,
                        0.65f + ri * 0.12f,
                        pos.z + sinf(ang) * radius),
                v3_make(0.08f, 0.04f, 0.08f),
                0.80f, 0.82f, 0.92f);
        }
        return;
    }

    /* NECROMANCER : silhouette en robe sombre, hood profond, baton avec
     * crane au sommet. Particules d ame autour. */
    if (e->kind == EK_NECROMANCER) {
        /* robe sombre */
        gfx_box_draw(g->renderer, v3_make(pos.x, h * 0.35f, pos.z),
                     v3_make(w * 1.10f, h * 0.65f, w * 1.10f),
                     r, gg, b);
        gfx_box_draw(g->renderer, v3_make(pos.x, h * 0.75f, pos.z),
                     v3_make(w * 0.80f, h * 0.40f, w * 0.80f),
                     r * 0.7f, gg * 0.7f, b * 0.7f);
        /* hood */
        gfx_box_draw(g->renderer, v3_make(pos.x, h + 0.28f, pos.z),
                     v3_make(0.45f, 0.30f, 0.45f),
                     r * 0.45f, gg * 0.45f, b * 0.50f);
        /* yeux violets */
        float ep = 0.7f + 0.3f * sinf(g->time * 5.f);
        gfx_box_draw(g->renderer,
                     v3_make(pos.x - 0.13f, h + 0.30f, pos.z + 0.32f),
                     v3_make(0.05f, 0.05f, 0.04f),
                     0.70f * ep, 0.30f * ep, 1.00f * ep);
        gfx_box_draw(g->renderer,
                     v3_make(pos.x + 0.13f, h + 0.30f, pos.z + 0.32f),
                     v3_make(0.05f, 0.05f, 0.04f),
                     0.70f * ep, 0.30f * ep, 1.00f * ep);
        /* baton avec crane */
        gfx_box_draw(g->renderer,
                     v3_make(pos.x + (w/2 + 0.18f), h * 0.45f, pos.z),
                     v3_make(0.06f, 0.75f, 0.06f),
                     0.35f, 0.25f, 0.15f);
        gfx_box_draw(g->renderer,
                     v3_make(pos.x + (w/2 + 0.18f), h * 0.95f, pos.z),
                     v3_make(0.16f, 0.18f, 0.16f),
                     0.90f, 0.88f, 0.78f);
        /* ame qui flotte */
        if ((rand() % 100) < 30) {
            particle_spawn_kind(g, e->x, e->y - 4,
                                (rand() % 20) - 10, -25.f,
                                0.6f, 0x6020A0A0, 1.6f, 0);
        }
        return;
    }

    /* ARCHIMAGE : silhouette robe haute + hood + baton + aura multicolore.
     * Cycle de couleur via arch_element_order pour donner une teinte qui
     * change (Ganondorf-like puissance contenue). En stase, halo bleute
     * + 3 marqueurs pour les bosses summons. */
    if (e->is_archmage) {
        float gscale = e->arch_grow < 0.3f ? 0.3f : e->arch_grow;
        /* couleur principale : cycle des elements pour l'aura */
        int el_idx = ((int)(g->time * 0.5f)) % 10;
        uint32_t col = element_color((Element)g->arch_element_order[el_idx]);
        float ar = ((col>>24)&0xFF)/255.f;
        float ag = ((col>>16)&0xFF)/255.f;
        float ab = ((col>>8)&0xFF)/255.f;
        /* robe noir-violet de base */
        gfx_box_draw(g->renderer, v3_make(pos.x, h * 0.35f, pos.z),
                     v3_make(w * 1.10f, h * 0.65f, w * 1.10f),
                     0.15f, 0.10f, 0.25f);
        gfx_box_draw(g->renderer, v3_make(pos.x, h * 0.75f, pos.z),
                     v3_make(w * 0.78f, h * 0.40f, w * 0.78f),
                     0.20f, 0.14f, 0.32f);
        /* hood pointu massif */
        gfx_box_draw(g->renderer, v3_make(pos.x, h + 0.40f, pos.z),
                     v3_make(0.65f * gscale, 0.50f * gscale, 0.65f * gscale),
                     0.10f, 0.06f, 0.18f);
        gfx_box_draw(g->renderer, v3_make(pos.x, h + 0.85f, pos.z),
                     v3_make(0.35f * gscale, 0.25f * gscale, 0.35f * gscale),
                     0.08f, 0.05f, 0.14f);
        /* yeux ardents 4 (l'archimage est multi-element) */
        float ep = 0.7f + 0.3f * sinf(g->time * 6.f);
        gfx_box_draw(g->renderer, v3_make(pos.x - 0.18f, h + 0.35f, pos.z + 0.40f),
                     v3_make(0.06f, 0.06f, 0.04f), ar * ep, ag * ep, ab * ep);
        gfx_box_draw(g->renderer, v3_make(pos.x + 0.18f, h + 0.35f, pos.z + 0.40f),
                     v3_make(0.06f, 0.06f, 0.04f), ar * ep, ag * ep, ab * ep);
        /* baton sur le cote droit, hauteur = grow */
        float staff_h = 1.5f * gscale;
        gfx_box_draw(g->renderer,
                     v3_make(pos.x + (w / 2 + 0.30f), h * 0.55f, pos.z),
                     v3_make(0.10f, staff_h, 0.10f),
                     0.35f, 0.22f, 0.10f);
        /* cristal en tete du baton (cycle de couleur element) */
        gfx_box_draw(g->renderer,
                     v3_make(pos.x + (w / 2 + 0.30f), h * 0.55f + staff_h * 0.5f + 0.10f, pos.z),
                     v3_make(0.22f, 0.22f, 0.22f),
                     ar, ag, ab);
        /* halo au sol : grand cercle qui cycle 10 couleurs */
        for (int k = 0; k < 10; k++) {
            float a = (k / 10.f) * 6.2831f + g->time * 0.4f;
            uint32_t hc = element_color((Element)g->arch_element_order[k]);
            float hr_ = ((hc>>24)&0xFF)/255.f;
            float hg_ = ((hc>>16)&0xFF)/255.f;
            float hb_ = ((hc>>8)&0xFF)/255.f;
            gfx_box_draw(g->renderer,
                v3_make(pos.x + cosf(a) * (1.5f * gscale),
                        0.05f,
                        pos.z + sinf(a) * (1.5f * gscale)),
                v3_make(0.16f, 0.04f, 0.16f),
                hr_, hg_, hb_);
        }
        /* aura stase : disque bleute pulsant */
        if (g->arch_stasis_t > 0.f) {
            float pulse = 0.5f + 0.5f * sinf(g->time * 8.f);
            gfx_box_draw(g->renderer,
                v3_make(pos.x, 0.10f, pos.z),
                v3_make(w * 2.5f, 0.05f, w * 2.5f),
                0.40f + 0.40f * pulse, 0.60f * pulse, 1.0f * pulse);
            /* couronne d ame autour de la tete */
            for (int k = 0; k < 12; k++) {
                float a = (k / 12.f) * 6.2831f + g->time * 1.5f;
                gfx_box_draw(g->renderer,
                    v3_make(pos.x + cosf(a) * 0.7f,
                            h + 0.60f + sinf(a * 2.f) * 0.10f,
                            pos.z + sinf(a) * 0.7f),
                    v3_make(0.08f, 0.08f, 0.08f),
                    0.7f, 0.85f, 1.0f);
            }
        }
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

    /* ENNEMIS NORMAUX : corps + tete + 2 jambes + 2 bras avec
     * animations etat-pilotees :
     *   - walk : leg/arm swing amplifie par |v|
     *   - idle : petite respiration (sin lent sur y du corps)
     *   - windup : lean vers le joueur pendant telegraph_t
     *   - hurt : recul + secousse pendant hit_flash
     *   - stun : wobble lateral pendant stun_t
     */
    if (e->hit_flash > 0.f) {
        float k = 1.f + (e->hit_flash / 0.10f) * 0.10f;
        if (k > 1.10f) k = 1.10f;
        h *= k; w *= k;
    }
    /* vitesse normalisee (0..1 a ~60 px/s) pour amplifier le swing */
    float spd = sqrtf(e->vx * e->vx + e->vy * e->vy);
    float walk_amt = spd / 60.f;
    if (walk_amt > 1.5f) walk_amt = 1.5f;
    /* swing tempo : 0.4Hz idle, ~8Hz running */
    float walk_phase = g->time * (4.f + walk_amt * 8.f) + e->x * 0.13f + e->y * 0.07f;
    float swing_anim = sinf(walk_phase) * (0.04f + 0.10f * walk_amt);
    /* idle breathing (subtil, additionne au corps y) */
    float breathe = sinf(g->time * 1.8f + e->x * 0.1f) * 0.020f * (1.f - walk_amt * 0.7f);
    /* windup lean : pendant telegraph, l'ennemi se penche vers
     * le joueur (offset face direction). */
    float windup = 0.f;
    if (e->telegraph_t > 0.f) windup = 1.f - e->telegraph_t / 1.0f;
    if (windup < 0.f) windup = 0.f;
    if (windup > 1.f) windup = 1.f;
    float wind_pulse = sinf(windup * 6.2831f * 2.f) * 0.5f + 0.5f;
    /* lean direction : vers le joueur */
    float pdx = g->player.x - e->x, pdy = g->player.y - e->y;
    float plen = sqrtf(pdx*pdx + pdy*pdy) + 0.001f;
    float lean_x = (pdx/plen) / TILE * 0.10f * windup;
    float lean_z = (pdy/plen) / TILE * 0.10f * windup;
    /* hurt lean-back : recul oppose au knockback pendant hit_flash */
    float hurt_kx = 0.f, hurt_kz = 0.f;
    if (e->hit_flash > 0.f) {
        float klen = sqrtf(e->knockback_x*e->knockback_x +
                           e->knockback_y*e->knockback_y) + 0.001f;
        float hp_k = e->hit_flash / 0.18f; if (hp_k > 1.f) hp_k = 1.f;
        hurt_kx = (e->knockback_x / klen) / TILE * 0.08f * hp_k;
        hurt_kz = (e->knockback_y / klen) / TILE * 0.08f * hp_k;
    }
    /* stun wobble : oscillation horizontale rapide */
    float stun_wobble_x = 0.f, stun_wobble_z = 0.f;
    if (e->stun_t > 0.f) {
        float sk = sinf(g->time * 18.f) * 0.05f;
        float sk2 = cosf(g->time * 16.f) * 0.03f;
        stun_wobble_x = sk; stun_wobble_z = sk2;
    }
    /* offset global du corps */
    float body_off_x = lean_x + hurt_kx + stun_wobble_x;
    float body_off_z = lean_z + hurt_kz + stun_wobble_z;

    float leg_h = 0.32f;
    float body_y = leg_h + (h - leg_h) * 0.5f + breathe;
    /* jambes : swing dans l'axe perpendiculaire au mouvement quand
     * on marche. Quand a l'arret, jambes immobiles. */
    float leg_off_x = 0.f, leg_off_z = 0.f;
    if (spd > 5.f) {
        float vx_n = e->vx / spd, vy_n = e->vy / spd;
        /* swing_anim oscille +-, jambes alternent (signe oppose) */
        leg_off_x = vx_n * swing_anim * 0.6f;
        leg_off_z = vy_n * swing_anim * 0.6f;
    }
    gfx_box_draw(g->renderer,
                 v3_make(pos.x + body_off_x - 0.12f + leg_off_x,
                         leg_h * 0.5f + swing_anim * 0.5f,
                         pos.z + body_off_z + leg_off_z),
                 v3_make(0.16f, leg_h, 0.18f),
                 r * 0.6f, gg * 0.6f, b * 0.6f);
    gfx_box_draw(g->renderer,
                 v3_make(pos.x + body_off_x + 0.12f - leg_off_x,
                         leg_h * 0.5f - swing_anim * 0.5f,
                         pos.z + body_off_z - leg_off_z),
                 v3_make(0.16f, leg_h, 0.18f),
                 r * 0.6f, gg * 0.6f, b * 0.6f);
    /* corps */
    gfx_box_draw(g->renderer,
                 v3_make(pos.x + body_off_x, body_y, pos.z + body_off_z),
                 v3_make(w, h - leg_h, w * 0.85f), r, gg, b);
    /* bras : swing en opposition aux jambes ; pendant windup, levent
     * tous les deux en avant pour l'attaque */
    float arm_swing = swing_anim * 1.5f;
    float arm_lift = windup * 0.15f * wind_pulse;
    gfx_box_draw(g->renderer,
                 v3_make(pos.x + body_off_x - (w/2 + 0.08f),
                         body_y - arm_swing + arm_lift,
                         pos.z + body_off_z),
                 v3_make(0.13f, (h - leg_h) * 0.85f, 0.13f),
                 r * 0.9f, gg * 0.9f, b * 0.9f);
    gfx_box_draw(g->renderer,
                 v3_make(pos.x + body_off_x + (w/2 + 0.08f),
                         body_y + arm_swing + arm_lift,
                         pos.z + body_off_z),
                 v3_make(0.13f, (h - leg_h) * 0.85f, 0.13f),
                 r * 0.9f, gg * 0.9f, b * 0.9f);
    /* tete (suit le corps) */
    float head_y = h + 0.18f + breathe;
    gfx_box_draw(g->renderer,
                 v3_make(pos.x + body_off_x, head_y, pos.z + body_off_z),
                 v3_make(w * 0.7f, 0.36f, w * 0.7f),
                 r * 1.15f, gg * 1.15f, b * 1.15f);
    /* yeux */
    {
        float ey = head_y + 0.05f;
        /* yeux rouges pendant windup, jaunes normalement */
        float er_ = 1.f, eg_ = 0.95f, eb_ = 0.25f;
        if (windup > 0.2f) { er_ = 1.f; eg_ = 0.20f + 0.40f * (1.f - windup); eb_ = 0.10f; }
        gfx_box_draw(g->renderer,
                     v3_make(pos.x + body_off_x - 0.10f, ey,
                             pos.z + body_off_z + w * 0.36f),
                     v3_make(0.06f, 0.06f, 0.04f),
                     er_, eg_, eb_);
        gfx_box_draw(g->renderer,
                     v3_make(pos.x + body_off_x + 0.10f, ey,
                             pos.z + body_off_z + w * 0.36f),
                     v3_make(0.06f, 0.06f, 0.04f),
                     er_, eg_, eb_);
    }
    /* particules de stun (3 etoiles tournantes au-dessus de la tete) */
    if (e->stun_t > 0.f && (rand() % 100) < 35) {
        float a = (rand() / (float)RAND_MAX) * 6.2831f;
        particle_spawn_kind(g,
            e->x + cosf(a) * 8.f,
            e->y - 18.f,
            cosf(a) * 18.f, -8.f,
            0.40f, 0xFFE040C0, 1.6f, 0);
    }
    /* sparks de windup avant l'attaque */
    if (windup > 0.3f && (rand() % 100) < 15) {
        particle_spawn_kind(g,
            e->x + (rand() % 20) - 10,
            e->y + (rand() % 12) - 6,
            (rand() % 30) - 15, -15.f,
            0.30f, 0xFF6040C0, 1.4f, 0);
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
    /* === Beam Diablo-style : descendant depuis le ciel pour les loots
     * de qualite. Calcule ici pour PU_ITEM / PU_WEAPON / PU_ELEMENT. */
    if (pk->kind == PU_ITEM || pk->kind == PU_WEAPON || pk->kind == PU_ELEMENT) {
        Rarity rar = R_COMMON;
        uint32_t bcol = 0xCCCCCCFF;
        if (pk->kind == PU_ITEM) {
            rar = pk->item.rarity;
            /* priorite a l'element s'il en a un, sinon orange unique
             * sinon rarity color */
            Element pel = item_element(&pk->item);
            if (pel > EL_NONE && pel < EL_COUNT) bcol = element_color(pel);
            else if (pk->item.is_unique) bcol = 0xFF8030FF;
            else bcol = rarity_color(rar);
        } else if (pk->kind == PU_WEAPON) {
            int rb = (pk->value >> 8) & 0xFF;
            if (rb >= 0 && rb < R_COUNT) rar = (Rarity)rb;
            bcol = rarity_color(rar);
        } else {
            bcol = element_color((Element)pk->value);
            rar = R_RARE;     /* niveau de visibilite : trait moyen */
        }
        float br = ((bcol>>24)&0xFF)/255.f;
        float bgg= ((bcol>>16)&0xFF)/255.f;
        float bb_= ((bcol>>8)&0xFF)/255.f;
        /* hauteur du beam scale par rarete + bonus unique */
        float beam_h = 1.5f + 0.6f * (int)rar;
        if (pk->kind == PU_ITEM && pk->item.is_unique) beam_h += 1.5f;
        int n_seg = 6 + (int)rar;
        if (n_seg > 12) n_seg = 12;
        float pulse = 0.5f + 0.5f * sinf(g->time * 4.f + pk->hover_t * 2.f);
        for (int s = 0; s < n_seg; s++) {
            float t = (float)s / (float)n_seg;
            float py = pos.y + 0.4f + t * beam_h;
            float w = 0.22f * (1.f - t * 0.6f);
            float fade = (1.f - t * 0.7f) * (0.5f + 0.5f * pulse);
            gfx_box_draw(g->renderer, v3_make(pos.x, py, pos.z),
                         v3_make(w, 0.08f, w),
                         br * fade, bgg * fade, bb_ * fade);
        }
        /* ambient particles : etincelles montantes selon la rarete.
         * Plus la rarete est haute, plus c'est dense. */
        int chance = 5 + (int)rar * 12;     /* 5..65% / frame */
        if (pk->kind == PU_ITEM && pk->item.is_unique) chance = 80;
        if ((rand() % 100) < chance) {
            float ox = pk->x + (rand() % 24) - 12;
            float oy = pk->y + (rand() % 12) - 6;
            float vy = -20.f - (rand() % 20);
            particle_spawn_kind(g, ox, oy, 0, vy,
                                0.8f + (rand() % 40) * 0.01f,
                                bcol, 1.6f + (int)rar * 0.3f, 0);
        }
    }
    switch (pk->kind) {
        case PU_XP:      r=0.30f; gg=0.80f; b=1.0f;   sz=0.32f; break;
        case PU_HEART:   r=1.0f;  gg=0.25f; b=0.38f;  sz=0.40f; break;
        case PU_SOUL:    r=0.55f; gg=0.95f; b=0.55f;  sz=0.40f; break;
        case PU_COIN:    r=1.0f;  gg=0.85f; b=0.25f;  sz=0.32f; break;
        case PU_ELEMENT: {
            /* Orbe magique : noyau brillant + halo plus large + 3
             * sparks qui orbitent. Beaucoup plus lisible qu'un simple
             * cube en couleur d element. */
            uint32_t c = element_color((Element)pk->value);
            float er = ((c>>24)&0xFF)/255.f;
            float eg = ((c>>16)&0xFF)/255.f;
            float eb = ((c>>8)&0xFF)/255.f;
            float pulse = 0.7f + 0.3f * sinf(g->time * 6.f + pk->hover_t);
            /* socle assorti */
            gfx_box_draw(g->renderer, v3_make(pos.x, 0.05f, pos.z),
                         v3_make(0.45f, 0.05f, 0.45f),
                         er * 0.6f, eg * 0.6f, eb * 0.6f);
            /* halo large translucide */
            gfx_box_draw(g->renderer, pos,
                         v3_make(0.55f, 0.55f, 0.55f),
                         er * 0.45f, eg * 0.45f, eb * 0.45f);
            /* noyau brillant pulse */
            gfx_box_draw(g->renderer, pos,
                         v3_make(0.32f * pulse, 0.32f * pulse, 0.32f * pulse),
                         er, eg, eb);
            /* highlight blanc au centre */
            gfx_box_draw(g->renderer,
                         v3_make(pos.x - 0.04f, pos.y + 0.06f, pos.z - 0.04f),
                         v3_make(0.08f, 0.08f, 0.08f),
                         1.f, 1.f, 1.f);
            /* 3 sparks orbitants */
            for (int s = 0; s < 3; s++) {
                float a = g->time * 2.f + s * 2.094f + pk->hover_t;
                gfx_box_draw(g->renderer,
                    v3_make(pos.x + cosf(a) * 0.40f,
                            pos.y + sinf(a * 0.7f) * 0.10f,
                            pos.z + sinf(a) * 0.40f),
                    v3_make(0.06f, 0.06f, 0.06f),
                    er, eg, eb);
            }
            /* pillar pour reperer dans le noir */
            float pulse2 = 0.5f + 0.5f * sinf(g->time * 4.f);
            gfx_box_draw(g->renderer, v3_make(pos.x, 0.30f, pos.z),
                         v3_make(0.04f, 0.55f * pulse2, 0.04f),
                         er, eg, eb);
            return;
        }
        case PU_WEAPON: {
            /* Forme mini-arme reconnaissable, couleur de rarete. */
            int wk = pk->value & 0xFF;
            int rar = (pk->value >> 8) & 0xFF;
            uint32_t c = (rar >= 0 && rar < R_COUNT)
                            ? rarity_color((Rarity)rar) : 0xC0C0C0FF;
            float rr = ((c>>24)&0xFF)/255.f;
            float gg2 = ((c>>16)&0xFF)/255.f;
            float bb = ((c>>8)&0xFF)/255.f;
            /* socle metallique sombre */
            gfx_box_draw(g->renderer, v3_make(pos.x, 0.05f, pos.z),
                         v3_make(0.55f, 0.08f, 0.55f),
                         0.25f, 0.25f, 0.30f);
            /* anneau rarete sous la lame */
            gfx_box_draw(g->renderer, v3_make(pos.x, 0.10f, pos.z),
                         v3_make(0.42f, 0.04f, 0.42f),
                         rr * 0.7f, gg2 * 0.7f, bb * 0.7f);
            /* arme par kind */
            float ty = 0.55f + hover * 0.3f;
            switch (wk) {
                case W_SWORD: {
                    /* lame verticale + garde + grip */
                    gfx_box_draw(g->renderer, v3_make(pos.x, ty + 0.18f, pos.z),
                                 v3_make(0.08f, 0.45f, 0.08f), 0.9f, 0.9f, 0.95f);
                    gfx_box_draw(g->renderer, v3_make(pos.x, ty - 0.10f, pos.z),
                                 v3_make(0.35f, 0.07f, 0.10f), rr, gg2, bb);
                    gfx_box_draw(g->renderer, v3_make(pos.x, ty - 0.22f, pos.z),
                                 v3_make(0.09f, 0.14f, 0.09f), 0.55f, 0.32f, 0.18f);
                    /* pommeau */
                    gfx_box_draw(g->renderer, v3_make(pos.x, ty - 0.32f, pos.z),
                                 v3_make(0.12f, 0.08f, 0.12f), rr, gg2, bb);
                    break;
                }
                case W_AXE: {
                    /* manche vertical + tete double */
                    gfx_box_draw(g->renderer, v3_make(pos.x, ty, pos.z),
                                 v3_make(0.07f, 0.60f, 0.07f), 0.55f, 0.30f, 0.18f);
                    /* tete : deux trapezoides aux cotes */
                    gfx_box_draw(g->renderer,
                                 v3_make(pos.x - 0.16f, ty + 0.18f, pos.z),
                                 v3_make(0.18f, 0.20f, 0.10f), rr, gg2, bb);
                    gfx_box_draw(g->renderer,
                                 v3_make(pos.x + 0.16f, ty + 0.18f, pos.z),
                                 v3_make(0.18f, 0.20f, 0.10f), rr, gg2, bb);
                    /* axe spike sommet */
                    gfx_box_draw(g->renderer,
                                 v3_make(pos.x, ty + 0.32f, pos.z),
                                 v3_make(0.06f, 0.10f, 0.06f), 0.9f, 0.9f, 0.95f);
                    break;
                }
                case W_BOW: {
                    /* 2 limbes verticaux courbe + corde diagonale + grip */
                    gfx_box_draw(g->renderer,
                                 v3_make(pos.x, ty + 0.22f, pos.z),
                                 v3_make(0.10f, 0.28f, 0.08f), 0.55f, 0.32f, 0.18f);
                    gfx_box_draw(g->renderer,
                                 v3_make(pos.x, ty - 0.22f, pos.z),
                                 v3_make(0.10f, 0.28f, 0.08f), 0.55f, 0.32f, 0.18f);
                    /* poignee centrale */
                    gfx_box_draw(g->renderer, v3_make(pos.x, ty, pos.z),
                                 v3_make(0.08f, 0.10f, 0.10f), rr, gg2, bb);
                    /* corde : 2 petits segments diagonaux derriere */
                    gfx_box_draw(g->renderer,
                                 v3_make(pos.x - 0.06f, ty + 0.18f, pos.z),
                                 v3_make(0.02f, 0.22f, 0.02f), 0.95f, 0.95f, 0.85f);
                    gfx_box_draw(g->renderer,
                                 v3_make(pos.x - 0.06f, ty - 0.18f, pos.z),
                                 v3_make(0.02f, 0.22f, 0.02f), 0.95f, 0.95f, 0.85f);
                    break;
                }
                case W_WAND: {
                    /* baton long fin + tete brillante */
                    gfx_box_draw(g->renderer, v3_make(pos.x, ty, pos.z),
                                 v3_make(0.05f, 0.55f, 0.05f), 0.40f, 0.22f, 0.10f);
                    /* pommeau cristal */
                    float pulse2 = 0.6f + 0.4f * sinf(g->time * 5.f);
                    gfx_box_draw(g->renderer,
                                 v3_make(pos.x, ty + 0.32f, pos.z),
                                 v3_make(0.16f * pulse2, 0.16f * pulse2, 0.16f * pulse2),
                                 rr, gg2, bb);
                    gfx_box_draw(g->renderer,
                                 v3_make(pos.x, ty + 0.32f, pos.z),
                                 v3_make(0.06f, 0.06f, 0.06f),
                                 1.f, 1.f, 1.f);
                    break;
                }
                case W_SHIELD: {
                    /* disque epais + clous + emblem rarete */
                    gfx_box_draw(g->renderer, v3_make(pos.x, ty, pos.z),
                                 v3_make(0.45f, 0.55f, 0.10f), 0.55f, 0.42f, 0.25f);
                    /* bordure metallique plus claire */
                    gfx_box_draw(g->renderer, v3_make(pos.x, ty, pos.z),
                                 v3_make(0.50f, 0.10f, 0.12f), 0.78f, 0.72f, 0.55f);
                    gfx_box_draw(g->renderer, v3_make(pos.x, ty - 0.20f, pos.z),
                                 v3_make(0.10f, 0.20f, 0.10f), 0.78f, 0.72f, 0.55f);
                    /* boss centre rarete */
                    gfx_box_draw(g->renderer, v3_make(pos.x, ty, pos.z + 0.05f),
                                 v3_make(0.16f, 0.16f, 0.06f), rr, gg2, bb);
                    break;
                }
                default: {
                    /* fallback : cube + barre */
                    gfx_box_draw(g->renderer, v3_make(pos.x, ty, pos.z),
                                 v3_make(0.35f, 0.35f, 0.35f), rr, gg2, bb);
                    break;
                }
            }
            /* pillar de lumiere */
            float pulse3 = 0.5f + 0.5f * sinf(g->time * 4.f + pk->hover_t * 2.f);
            gfx_box_draw(g->renderer, v3_make(pos.x, 0.30f, pos.z),
                         v3_make(0.04f, 0.45f * pulse3, 0.04f),
                         rr, gg2, bb);
            return;
        }
        case PU_CHEST: {
            /* Coffre : tronc bois + bandes dorees + pillar de lumiere
             * pulsant doree pour le repere a distance. */
            r=0.55f; gg=0.34f; b=0.20f; sz=0.85f; draw_pillar = false;
            /* corps du coffre (plus gros que les autres pickups) */
            gfx_box_draw(g->renderer, v3_make(pos.x, 0.18f, pos.z),
                         v3_make(sz * 0.7f, 0.30f, sz * 0.5f),
                         r, gg, b);
            /* bandes en metal dore */
            gfx_box_draw(g->renderer, v3_make(pos.x, 0.32f, pos.z),
                         v3_make(sz * 0.72f, 0.04f, sz * 0.52f),
                         0.95f, 0.78f, 0.20f);
            gfx_box_draw(g->renderer, v3_make(pos.x, 0.08f, pos.z),
                         v3_make(sz * 0.72f, 0.04f, sz * 0.52f),
                         0.95f, 0.78f, 0.20f);
            /* cadenas / serrure devant */
            gfx_box_draw(g->renderer, v3_make(pos.x, 0.18f, pos.z + sz * 0.27f),
                         v3_make(0.08f, 0.10f, 0.04f),
                         1.0f, 0.85f, 0.30f);
            /* pillar de lumiere doree pulsant -- visible a travers la salle */
            float cpulse = 0.6f + 0.4f * sinf(g->time * 3.f);
            gfx_box_draw(g->renderer, v3_make(pos.x, 0.50f + cpulse * 0.25f, pos.z),
                         v3_make(0.08f, 0.60f + cpulse * 0.30f, 0.08f),
                         1.0f, 0.85f, 0.35f);
            /* halo plus large au pied du coffre */
            gfx_box_draw(g->renderer, v3_make(pos.x, 0.02f, pos.z),
                         v3_make(sz * 1.1f, 0.02f, sz * 0.9f),
                         1.0f, 0.80f, 0.20f);
            return;
        }
        case PU_PORTAL: {
            /* Couleur du portail : sur floor 0, encode la destination
             * via value. -1 = HUB (dore), 0..4 = biome (couleur biome),
             * 5 = Archimage (orange/violet). Verrouille = gris. */
            float pr_ = 0.55f, pg_ = 0.90f, pb_ = 1.0f;
            bool locked = false;
            int pv = pk->value;
            if (g->floor_index == 0) {
                if (pv == -1) { pr_ = 1.0f; pg_ = 0.85f; pb_ = 0.30f; }
                else if (pv >= 0 && pv <= 4) {
                    float br_, bg_, bb_;
                    biome_tint(pv, &br_, &bg_, &bb_);
                    pr_ = br_; pg_ = bg_; pb_ = bb_;
                    if (g->biome_cleared[pv]) locked = true;
                } else if (pv == 5) {
                    pr_ = 1.0f; pg_ = 0.50f; pb_ = 0.80f;
                    int n = 0;
                    for (int b = 0; b < 5; b++) if (g->biome_cleared[b]) n++;
                    if (n < 5) locked = true;
                }
                if (locked) { pr_ = 0.30f; pg_ = 0.30f; pb_ = 0.35f; }

                /* === Backdrop thematique derriere chaque portail ===
                 * Place a 1.5 units "behind" (direction outward from
                 * arena center 28,28). Le visuel varie selon la
                 * destination. Estompe si locked (50% alpha equivalent
                 * via couleurs attenuees). */
                float cx_a = 28.f, cz_a = 28.f;
                float odx = pos.x - cx_a, odz = pos.z - cz_a;
                float odl = sqrtf(odx * odx + odz * odz) + 0.01f;
                odx /= odl; odz /= odl;
                float bx = pos.x + odx * 1.5f;
                float bz = pos.z + odz * 1.5f;
                float dim = locked ? 0.45f : 1.0f;
                if (pv == -1) {
                    /* HUB : 2 pierres tombales en arc derriere */
                    for (int k = -1; k <= 1; k += 2) {
                        float lx = bx + (-odz) * 0.40f * k;
                        float lz = bz + ( odx) * 0.40f * k;
                        /* corps */
                        gfx_box_draw(g->renderer,
                            v3_make(lx, 0.30f, lz),
                            v3_make(0.25f, 0.50f, 0.10f),
                            0.45f * dim, 0.42f * dim, 0.45f * dim);
                        /* arrondi top */
                        gfx_box_draw(g->renderer,
                            v3_make(lx, 0.55f, lz),
                            v3_make(0.15f, 0.10f, 0.12f),
                            0.42f * dim, 0.40f * dim, 0.42f * dim);
                    }
                } else if (pv == 0) {
                    /* Crypte (DARK) : 3 pierres tombales sombres + croix */
                    for (int k = -1; k <= 1; k++) {
                        float lx = bx + (-odz) * 0.50f * k;
                        float lz = bz + ( odx) * 0.50f * k;
                        gfx_box_draw(g->renderer,
                            v3_make(lx, 0.35f, lz),
                            v3_make(0.20f, 0.60f, 0.10f),
                            0.30f * dim, 0.22f * dim, 0.32f * dim);
                    }
                } else if (pv == 1) {
                    /* Cavernes (EARTH) : 3 stalagmites brunes pointues */
                    for (int k = -1; k <= 1; k++) {
                        float lx = bx + (-odz) * 0.45f * k;
                        float lz = bz + ( odx) * 0.45f * k;
                        float hh = 0.55f + (k == 0 ? 0.20f : 0.f);
                        gfx_box_draw(g->renderer,
                            v3_make(lx, hh * 0.5f, lz),
                            v3_make(0.30f, hh, 0.30f),
                            0.55f * dim, 0.38f * dim, 0.22f * dim);
                        gfx_box_draw(g->renderer,
                            v3_make(lx, hh + 0.10f, lz),
                            v3_make(0.18f, 0.15f, 0.18f),
                            0.50f * dim, 0.32f * dim, 0.18f * dim);
                    }
                } else if (pv == 2) {
                    /* Marais (WATER) : flaque sombre + spores verts qui flottent */
                    gfx_box_draw(g->renderer,
                        v3_make(bx, 0.04f, bz),
                        v3_make(1.2f, 0.05f, 1.0f),
                        0.20f * dim, 0.42f * dim, 0.32f * dim);
                    for (int k = 0; k < 3; k++) {
                        float a = g->time * 0.5f + k * 2.094f;
                        float lx = bx + cosf(a) * 0.40f;
                        float lz = bz + sinf(a) * 0.30f;
                        gfx_box_draw(g->renderer,
                            v3_make(lx, 0.55f + sinf(g->time + k) * 0.10f, lz),
                            v3_make(0.10f, 0.10f, 0.10f),
                            0.40f * dim, 0.85f * dim, 0.40f * dim);
                    }
                } else if (pv == 3) {
                    /* Forge (FIRE) : enclume + 2 piliers flames */
                    gfx_box_draw(g->renderer,
                        v3_make(bx, 0.30f, bz),
                        v3_make(0.60f, 0.30f, 0.45f),
                        0.35f * dim, 0.30f * dim, 0.32f * dim);
                    gfx_box_draw(g->renderer,
                        v3_make(bx, 0.55f, bz),
                        v3_make(0.30f, 0.20f, 0.30f),
                        0.40f * dim, 0.32f * dim, 0.30f * dim);
                    /* flammes : 2 cubes orangés qui pulsent */
                    float fp = 0.5f + 0.5f * sinf(g->time * 6.f);
                    for (int k = -1; k <= 1; k += 2) {
                        float lx = bx + (-odz) * 0.50f * k;
                        float lz = bz + ( odx) * 0.50f * k;
                        gfx_box_draw(g->renderer,
                            v3_make(lx, 0.50f, lz),
                            v3_make(0.12f, 0.40f, 0.12f),
                            1.0f * dim, (0.45f + 0.35f * fp) * dim, 0.20f * dim);
                    }
                } else if (pv == 4) {
                    /* Sanctuaire (HOLY) : grand pilier blanc + halo */
                    gfx_box_draw(g->renderer,
                        v3_make(bx, 0.80f, bz),
                        v3_make(0.40f, 1.60f, 0.40f),
                        0.95f * dim, 0.90f * dim, 0.70f * dim);
                    gfx_box_draw(g->renderer,
                        v3_make(bx, 1.65f, bz),
                        v3_make(0.55f, 0.10f, 0.55f),
                        1.0f * dim, 0.92f * dim, 0.55f * dim);
                    /* halo dore au sol */
                    gfx_box_draw(g->renderer,
                        v3_make(bx, 0.04f, bz),
                        v3_make(1.1f, 0.04f, 1.1f),
                        1.0f * dim, 0.85f * dim, 0.40f * dim);
                } else if (pv == 5) {
                    /* Archimage : pentacle + 5 piliers void + orbe central */
                    for (int k = 0; k < 5; k++) {
                        float a = (k / 5.f) * 6.2831f - 1.57f;
                        float lx = bx + cosf(a) * 0.70f;
                        float lz = bz + sinf(a) * 0.70f;
                        gfx_box_draw(g->renderer,
                            v3_make(lx, 0.04f, lz),
                            v3_make(0.18f, 0.04f, 0.18f),
                            0.70f * dim, 0.30f * dim, 0.85f * dim);
                        /* piliers void verticaux */
                        gfx_box_draw(g->renderer,
                            v3_make(lx, 0.65f, lz),
                            v3_make(0.10f, 1.10f, 0.10f),
                            0.40f * dim, 0.15f * dim, 0.55f * dim);
                    }
                    /* orbe central qui pulse */
                    float op = 0.6f + 0.4f * sinf(g->time * 4.f);
                    gfx_box_draw(g->renderer,
                        v3_make(bx, 1.2f, bz),
                        v3_make(0.30f * op, 0.30f * op, 0.30f * op),
                        1.0f * dim, 0.50f * dim, 0.95f * dim);
                }

                /* === Gemme brillante au-dessus du portail (clean only) ===
                 * Petit cube qui flotte + pulse en couleur du portail.
                 * Spin via decalage du highlight. Pas affichee si locked. */
                if (!locked) {
                    float gp = 0.85f + 0.15f * sinf(g->time * 5.f);
                    float gy = 1.85f + sinf(g->time * 2.f + pk->hover_t) * 0.08f;
                    /* halo derriere */
                    gfx_box_draw(g->renderer,
                        v3_make(pos.x, gy, pos.z),
                        v3_make(0.28f, 0.28f, 0.28f),
                        pr_ * 0.45f, pg_ * 0.45f, pb_ * 0.45f);
                    /* coeur de gemme */
                    gfx_box_draw(g->renderer,
                        v3_make(pos.x, gy, pos.z),
                        v3_make(0.18f * gp, 0.18f * gp, 0.18f * gp),
                        pr_, pg_, pb_);
                    /* highlight blanc qui orbite (spin visuel) */
                    float sa = g->time * 3.f + pk->hover_t * 4.f;
                    gfx_box_draw(g->renderer,
                        v3_make(pos.x + cosf(sa) * 0.06f,
                                gy + 0.04f,
                                pos.z + sinf(sa) * 0.06f),
                        v3_make(0.05f, 0.05f, 0.05f),
                        1.f, 1.f, 1.f);
                }
            }
            /* gros disque + halo */
            float a = g->time * 3.f;
            for (int i = 0; i < 10; i++) {
                float ang = a + i * 0.628f;
                gfx_box_draw(g->renderer,
                    v3_make(pos.x + cosf(ang)*0.55f,
                            0.40f + sinf(ang*0.5f + g->time)*0.30f,
                            pos.z + sinf(ang)*0.55f),
                    v3_make(0.14f, 0.14f, 0.14f),
                    pr_, pg_, pb_);
            }
            /* socle lumineux */
            gfx_box_draw(g->renderer, v3_make(pos.x, 0.05f, pos.z),
                         v3_make(0.85f, 0.06f, 0.85f), pr_, pg_, pb_);
            /* pillar haut de lumiere (plus haut pour archimage) */
            int n_pillar = (g->floor_index == 0 && pk->value == 5 && !locked) ? 8 : 4;
            for (int i = 0; i < n_pillar; i++) {
                gfx_box_draw(g->renderer,
                    v3_make(pos.x, 0.5f + i * 0.6f, pos.z),
                    v3_make(0.10f, 0.5f, 0.10f),
                    pr_, pg_, pb_);
            }
            /* croix sombre pour les verrouilles */
            if (locked) {
                gfx_box_draw(g->renderer,
                    v3_make(pos.x, 1.0f, pos.z),
                    v3_make(0.20f, 0.20f, 0.40f),
                    0.10f, 0.05f, 0.08f);
            }
            return;
        }
        case PU_ITEM: {
            /* socle / pedestal sous l'item. Couleur priorite : element
             * dominant > unique orange > rarity color. */
            Element pel = item_element(&pk->item);
            uint32_t c;
            if (pel > EL_NONE && pel < EL_COUNT) c = element_color(pel);
            else if (pk->item.is_unique) c = 0xFF8030FF;
            else c = rarity_color(pk->item.rarity);
            float rr = ((c>>24)&0xFF)/255.f;
            float gg2 = ((c>>16)&0xFF)/255.f;
            float bb = ((c>>8)&0xFF)/255.f;
            /* base sombre (socle) */
            gfx_box_draw(g->renderer, v3_make(pos.x, 0.05f, pos.z),
                         v3_make(0.55f, 0.08f, 0.55f),
                         0.20f, 0.18f, 0.22f);
            /* anneau colore dessus pour signaler la rarete */
            float ph = 0.10f + 0.05f * (int)pk->item.rarity;
            gfx_box_draw(g->renderer, v3_make(pos.x, 0.10f, pos.z),
                         v3_make(0.42f, ph, 0.42f),
                         rr * 0.5f, gg2 * 0.5f, bb * 0.5f);
            /* forme de slot reconnaissable au-dessus */
            float ty = 0.55f + hover * 0.3f;
            switch (pk->item.slot) {
                case SLOT_HELM:
                    /* casque : dome arrondi avec visiere */
                    gfx_box_draw(g->renderer, v3_make(pos.x, ty, pos.z),
                                 v3_make(0.40f, 0.20f, 0.40f), rr, gg2, bb);
                    gfx_box_draw(g->renderer, v3_make(pos.x, ty + 0.15f, pos.z),
                                 v3_make(0.30f, 0.10f, 0.30f), rr, gg2, bb);
                    /* visiere sombre */
                    gfx_box_draw(g->renderer, v3_make(pos.x, ty - 0.04f, pos.z + 0.18f),
                                 v3_make(0.28f, 0.06f, 0.04f), 0.10f, 0.08f, 0.12f);
                    break;
                case SLOT_CHEST:
                    /* plastron : torse rectangulaire epais avec col */
                    gfx_box_draw(g->renderer, v3_make(pos.x, ty, pos.z),
                                 v3_make(0.45f, 0.45f, 0.20f), rr, gg2, bb);
                    /* col / epaulettes */
                    gfx_box_draw(g->renderer, v3_make(pos.x - 0.20f, ty + 0.16f, pos.z),
                                 v3_make(0.14f, 0.10f, 0.18f), rr * 1.1f, gg2 * 1.1f, bb * 1.1f);
                    gfx_box_draw(g->renderer, v3_make(pos.x + 0.20f, ty + 0.16f, pos.z),
                                 v3_make(0.14f, 0.10f, 0.18f), rr * 1.1f, gg2 * 1.1f, bb * 1.1f);
                    break;
                case SLOT_LEGS:
                    /* jambieres : 2 pieces verticales rapprochees */
                    gfx_box_draw(g->renderer, v3_make(pos.x - 0.10f, ty, pos.z),
                                 v3_make(0.15f, 0.45f, 0.18f), rr, gg2, bb);
                    gfx_box_draw(g->renderer, v3_make(pos.x + 0.10f, ty, pos.z),
                                 v3_make(0.15f, 0.45f, 0.18f), rr, gg2, bb);
                    /* ceinture en haut */
                    gfx_box_draw(g->renderer, v3_make(pos.x, ty + 0.20f, pos.z),
                                 v3_make(0.36f, 0.06f, 0.20f), rr * 0.8f, gg2 * 0.8f, bb * 0.8f);
                    break;
                case SLOT_BOOTS:
                    /* bottes : 2 pieces basses + semelle elargie */
                    gfx_box_draw(g->renderer, v3_make(pos.x - 0.12f, ty, pos.z),
                                 v3_make(0.18f, 0.25f, 0.18f), rr, gg2, bb);
                    gfx_box_draw(g->renderer, v3_make(pos.x + 0.12f, ty, pos.z),
                                 v3_make(0.18f, 0.25f, 0.18f), rr, gg2, bb);
                    /* semelles plus larges */
                    gfx_box_draw(g->renderer, v3_make(pos.x - 0.12f, ty - 0.10f, pos.z + 0.04f),
                                 v3_make(0.22f, 0.05f, 0.26f), 0.20f, 0.16f, 0.12f);
                    gfx_box_draw(g->renderer, v3_make(pos.x + 0.12f, ty - 0.10f, pos.z + 0.04f),
                                 v3_make(0.22f, 0.05f, 0.26f), 0.20f, 0.16f, 0.12f);
                    break;
                case SLOT_BELT:
                    /* ceinture : bande horizontale large avec boucle */
                    gfx_box_draw(g->renderer, v3_make(pos.x, ty, pos.z),
                                 v3_make(0.50f, 0.16f, 0.20f), rr, gg2, bb);
                    /* boucle dorée centre */
                    gfx_box_draw(g->renderer, v3_make(pos.x, ty, pos.z + 0.08f),
                                 v3_make(0.16f, 0.18f, 0.06f), 1.0f, 0.78f, 0.30f);
                    break;
                case SLOT_GLOVES:
                    /* gants : 2 petits cubes a hauteur de poing */
                    gfx_box_draw(g->renderer, v3_make(pos.x - 0.16f, ty, pos.z),
                                 v3_make(0.20f, 0.25f, 0.18f), rr, gg2, bb);
                    gfx_box_draw(g->renderer, v3_make(pos.x + 0.16f, ty, pos.z),
                                 v3_make(0.20f, 0.25f, 0.18f), rr, gg2, bb);
                    /* poignets */
                    gfx_box_draw(g->renderer, v3_make(pos.x - 0.16f, ty - 0.12f, pos.z),
                                 v3_make(0.16f, 0.06f, 0.16f), rr * 0.7f, gg2 * 0.7f, bb * 0.7f);
                    gfx_box_draw(g->renderer, v3_make(pos.x + 0.16f, ty - 0.12f, pos.z),
                                 v3_make(0.16f, 0.06f, 0.16f), rr * 0.7f, gg2 * 0.7f, bb * 0.7f);
                    break;
                default:
                    /* fallback cube */
                    gfx_box_draw(g->renderer, v3_make(pos.x, ty, pos.z),
                                 v3_make(0.40f, 0.40f, 0.40f), rr, gg2, bb);
                    break;
            }
            /* uniques : aura clignotante au-dessus */
            if (pk->item.is_unique) {
                float pulse = 0.5f + 0.5f * sinf(g->time * 4.f);
                gfx_box_draw(g->renderer,
                             v3_make(pos.x, ty + 0.45f, pos.z),
                             v3_make(0.20f, 0.20f, 0.20f),
                             rr * pulse, gg2 * pulse, bb * pulse);
            }
            /* pillar de lumiere */
            float pulse3 = 0.5f + 0.5f * sinf(g->time * 4.f + pk->hover_t * 2.f);
            gfx_box_draw(g->renderer, v3_make(pos.x, 0.30f, pos.z),
                         v3_make(0.04f, 0.55f * pulse3, 0.04f),
                         rr, gg2, bb);
            return;
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
        case PU_MERCHANT: {
            /* Marchand : etal en bois + parasol + or pulsant */
            float pulse = 0.6f + 0.4f * sinf(g->time * 3.f);
            /* socle / etal */
            gfx_box_draw(g->renderer, v3_make(pos.x, 0.18f, pos.z),
                         v3_make(0.90f, 0.36f, 0.70f),
                         0.45f, 0.30f, 0.18f);
            /* dessus de l'etal (or) */
            gfx_box_draw(g->renderer, v3_make(pos.x, 0.42f, pos.z),
                         v3_make(0.95f, 0.06f, 0.75f),
                         0.85f * pulse, 0.65f * pulse, 0.20f);
            /* poteau de parasol */
            gfx_box_draw(g->renderer, v3_make(pos.x + 0.30f, 0.85f, pos.z),
                         v3_make(0.06f, 0.55f, 0.06f),
                         0.30f, 0.20f, 0.10f);
            /* parasol rouge */
            gfx_box_draw(g->renderer, v3_make(pos.x + 0.30f, 1.18f, pos.z),
                         v3_make(0.85f, 0.10f, 0.70f),
                         0.85f, 0.20f, 0.20f);
            /* signe doree */
            gfx_box_draw(g->renderer, v3_make(pos.x, 0.62f, pos.z + 0.40f),
                         v3_make(0.40f, 0.12f, 0.04f),
                         1.0f, 0.85f, 0.25f);
            return;
        }
        case PU_ALTAR: {
            /* Autel : socle blanc + flamme doree pulsante (depots ames) */
            float pulse = 0.6f + 0.4f * sinf(g->time * 2.5f);
            gfx_box_draw(g->renderer, v3_make(pos.x, 0.12f, pos.z),
                         v3_make(0.80f, 0.24f, 0.80f),
                         0.85f, 0.82f, 0.75f);
            gfx_box_draw(g->renderer, v3_make(pos.x, 0.38f, pos.z),
                         v3_make(0.55f, 0.20f, 0.55f),
                         0.92f, 0.90f, 0.82f);
            /* coupole doree au sommet */
            gfx_box_draw(g->renderer, v3_make(pos.x, 0.58f, pos.z),
                         v3_make(0.30f, 0.12f, 0.30f),
                         1.0f, 0.80f, 0.25f);
            /* flamme doree pulsante */
            gfx_box_draw(g->renderer, v3_make(pos.x, 0.78f + pulse * 0.08f, pos.z),
                         v3_make(0.16f, 0.28f, 0.16f),
                         1.0f * pulse, 0.85f * pulse, 0.30f);
            /* halo doree au sol */
            gfx_box_draw(g->renderer, v3_make(pos.x, 0.02f, pos.z),
                         v3_make(1.10f, 0.02f, 1.10f),
                         0.95f * pulse, 0.75f * pulse, 0.25f);
            return;
        }
        case PU_SLOTMACHINE: {
            /* Machine a sous : box noir + rouleaux colores + clignotant */
            int phase = (int)(g->time * 4.f);
            float r1 = (phase % 3 == 0) ? 1.0f : 0.30f;
            float g1 = ((phase + 1) % 3 == 0) ? 1.0f : 0.30f;
            float b1 = ((phase + 2) % 3 == 0) ? 1.0f : 0.30f;
            /* corps de la machine */
            gfx_box_draw(g->renderer, v3_make(pos.x, 0.40f, pos.z),
                         v3_make(0.70f, 0.80f, 0.55f),
                         0.20f, 0.18f, 0.25f);
            /* fenetre des rouleaux (3 carres animes) */
            gfx_box_draw(g->renderer, v3_make(pos.x - 0.22f, 0.60f, pos.z + 0.30f),
                         v3_make(0.16f, 0.20f, 0.04f),
                         r1, 0.20f, 0.20f);
            gfx_box_draw(g->renderer, v3_make(pos.x,         0.60f, pos.z + 0.30f),
                         v3_make(0.16f, 0.20f, 0.04f),
                         0.20f, g1, 0.20f);
            gfx_box_draw(g->renderer, v3_make(pos.x + 0.22f, 0.60f, pos.z + 0.30f),
                         v3_make(0.16f, 0.20f, 0.04f),
                         0.20f, 0.20f, b1);
            /* levier (rouge) sur le cote */
            gfx_box_draw(g->renderer, v3_make(pos.x + 0.42f, 0.65f, pos.z),
                         v3_make(0.10f, 0.30f, 0.10f),
                         0.90f, 0.20f, 0.20f);
            /* boule doree au sommet du levier */
            gfx_box_draw(g->renderer, v3_make(pos.x + 0.42f, 0.85f, pos.z),
                         v3_make(0.12f, 0.10f, 0.12f),
                         1.0f, 0.85f, 0.25f);
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

    /* sprite : 1 = fleche (arc), 2 = orbe (baguette), 0 = generique */
    if (pr->sprite == 1) {
        /* fleche : tige longue alignee sur le velocity vector, pointe
         * legerement plus claire, fletching arriere. */
        float vlen = sqrtf(pr->vx * pr->vx + pr->vy * pr->vy) + 0.001f;
        float dirx = pr->vx / vlen;
        float diry = pr->vy / vlen;
        /* corps : 3 segments le long de l'axe de vol */
        for (int si = 0; si < 3; si++) {
            float t = (si - 1.f) * 0.10f;     /* -0.10, 0, +0.10 */
            v3 seg = v3_make(pos.x + dirx * t, pos.y, pos.z + diry * t);
            gfx_box_draw(g->renderer, seg, v3_make(0.06f, 0.06f, 0.06f),
                         r * 0.9f, gg * 0.9f, b * 0.9f);
        }
        /* pointe (devant) brillante */
        gfx_box_draw(g->renderer,
            v3_make(pos.x + dirx * 0.18f, pos.y, pos.z + diry * 0.18f),
            v3_make(0.05f, 0.05f, 0.05f),
            1.f, 0.95f, 0.7f);
        /* empennage : 2 ailerons perpendiculaires a l'arriere */
        float px = -diry, pz = dirx;
        gfx_box_draw(g->renderer,
            v3_make(pos.x - dirx * 0.18f + px * 0.06f, pos.y, pos.z - diry * 0.18f + pz * 0.06f),
            v3_make(0.04f, 0.06f, 0.04f),
            0.85f, 0.65f, 0.40f);
        gfx_box_draw(g->renderer,
            v3_make(pos.x - dirx * 0.18f - px * 0.06f, pos.y, pos.z - diry * 0.18f - pz * 0.06f),
            v3_make(0.04f, 0.06f, 0.04f),
            0.85f, 0.65f, 0.40f);
    } else if (pr->sprite == 2) {
        /* orbe magique : noyau pulsant + halo plus large + sparkles. */
        float pulse = 0.5f + 0.5f * sinf(g->time * 12.f);
        float core_sz = 0.16f + 0.05f * pulse;
        float halo_sz = 0.28f + 0.04f * pulse;
        /* halo translucide (couleur attenuee, plus gros) */
        gfx_box_draw(g->renderer, pos,
                     v3_make(halo_sz, halo_sz, halo_sz),
                     r * 0.45f, gg * 0.45f, b * 0.45f);
        gfx_box_draw(g->renderer, pos,
                     v3_make(core_sz, core_sz, core_sz),
                     r, gg, b);
        /* sparkle trail derriere la trajectoire : 1 par frame */
        if ((rand() % 100) < 60) {
            float vlen = sqrtf(pr->vx * pr->vx + pr->vy * pr->vy) + 0.001f;
            particle_spawn_kind(g,
                pr->x - pr->vx / vlen * 6.f,
                pr->y - pr->vy / vlen * 6.f,
                (rand()/(float)RAND_MAX - 0.5f) * 40.f,
                (rand()/(float)RAND_MAX - 0.5f) * 40.f,
                0.40f, c, 1.8f, 0);
        }
    } else {
        /* generique : cube simple, scale par aoe */
        float sz = (pr->aoe > 0.f) ? 0.30f : 0.18f;
        gfx_box_draw(g->renderer, pos, v3_make(sz, sz, sz), r, gg, b);
    }
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
    /* surfaces au sol (flaques) : juste au-dessus du terrain, sous les
     * entites. */
    render_surfaces(g);

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
    /* Porte verrouillee : mur rouge translucide pulsant. Bloque le
     * passage visuellement (en plus du blocage solide cote collision).
     * Doit etre tres visible : 2 grosses colonnes + barre transversale
     * + halo au sol. */
    {
        float pulse = 0.6f + 0.4f * sinf(g->time * 4.f);
        float pr_ = 1.0f * pulse;
        float pg_ = 0.15f;
        float pb_ = 0.15f;
        for (int y = 0; y < MAP_H; y++) {
            for (int x = 0; x < MAP_W; x++) {
                if (g->dungeon.tiles[y][x] != T_DOOR) continue;
                if (!door_locked_at(g, x, y)) continue;
                float wx = (float)x + 0.5f;
                float wz = (float)y + 0.5f;
                /* halo lumineux au sol, large et brillant */
                gfx_box_draw(g->renderer,
                             v3_make(wx, 0.03f, wz),
                             v3_make(0.95f, 0.04f, 0.95f),
                             pr_, pg_, pb_);
                /* 2 colonnes verticales epaisses, hauteur pleine */
                gfx_box_draw(g->renderer,
                             v3_make(wx - 0.32f, 0.55f, wz),
                             v3_make(0.18f, 1.10f, 0.18f),
                             pr_, pg_, pb_);
                gfx_box_draw(g->renderer,
                             v3_make(wx + 0.32f, 0.55f, wz),
                             v3_make(0.18f, 1.10f, 0.18f),
                             pr_, pg_, pb_);
                /* barre transversale au milieu (forme un X visuel) */
                gfx_box_draw(g->renderer,
                             v3_make(wx, 0.65f, wz),
                             v3_make(0.85f, 0.14f, 0.18f),
                             pr_, pg_, pb_);
                /* cadenas central pulsant plus brillant */
                gfx_box_draw(g->renderer,
                             v3_make(wx, 0.65f, wz),
                             v3_make(0.18f, 0.22f, 0.22f),
                             1.0f, 0.40f * pulse, 0.10f);
            }
        }
    }
    draw_particles_3d(g);

    /* HUB walkable : dessine les 5 batiments en 3D + decoration
     * (tombstones, arbres morts, lanternes, runes au sol). */
    if (g->state == GS_HUB) {
        /* === DECORATION DU CIMETIERE === */
        float t = g->time;
        /* tombstones le long du mur du fond (cote nord). Positions
         * scriptees pour rester coherentes a chaque frame. */
        static const float TOMB_POS[][2] = {
            {22.f, 22.5f}, {25.f, 22.0f}, {27.5f, 22.5f},
            {30.5f, 22.0f}, {33.f, 22.5f},
            /* avant des batiments en bas */
            {23.f, 30.5f}, {26.f, 30.0f}, {30.f, 30.0f}, {33.f, 30.5f},
        };
        int n_tomb = (int)(sizeof(TOMB_POS)/sizeof(TOMB_POS[0]));
        for (int i = 0; i < n_tomb; i++) {
            float tx = TOMB_POS[i][0];
            float tz = TOMB_POS[i][1];
            /* pierre tombale : socle + corps arrondi */
            gfx_box_draw(gc, v3_make(tx, 0.10f, tz),
                         v3_make(0.40f, 0.06f, 0.40f),
                         0.30f, 0.28f, 0.32f);
            gfx_box_draw(gc, v3_make(tx, 0.35f, tz),
                         v3_make(0.30f, 0.40f, 0.12f),
                         0.45f, 0.42f, 0.45f);
            /* arrondi du dessus */
            gfx_box_draw(gc, v3_make(tx, 0.58f, tz),
                         v3_make(0.22f, 0.08f, 0.12f),
                         0.40f, 0.38f, 0.42f);
        }
        /* arbres morts dans les coins. 4 troncs noirs + branches. */
        static const float TREE_POS[4][2] = {
            {17.5f, 22.5f}, {38.5f, 22.5f},
            {17.5f, 33.5f}, {38.5f, 33.5f},
        };
        for (int i = 0; i < 4; i++) {
            float tx = TREE_POS[i][0];
            float tz = TREE_POS[i][1];
            /* tronc principal */
            gfx_box_draw(gc, v3_make(tx, 0.70f, tz),
                         v3_make(0.20f, 1.40f, 0.20f),
                         0.22f, 0.16f, 0.10f);
            /* branche gauche */
            gfx_box_draw(gc, v3_make(tx - 0.30f, 1.30f, tz),
                         v3_make(0.30f, 0.10f, 0.10f),
                         0.20f, 0.14f, 0.08f);
            /* branche droite plus haute */
            gfx_box_draw(gc, v3_make(tx + 0.25f, 1.55f, tz),
                         v3_make(0.30f, 0.10f, 0.10f),
                         0.20f, 0.14f, 0.08f);
            /* fourche haute */
            gfx_box_draw(gc, v3_make(tx, 1.85f, tz),
                         v3_make(0.10f, 0.30f, 0.10f),
                         0.18f, 0.12f, 0.08f);
        }
        /* lanternes au sol (pulse orange) dispersees */
        static const float LAMP_POS[6][2] = {
            {24.f, 26.f}, {32.f, 26.f},
            {24.f, 31.f}, {32.f, 31.f},
            {21.f, 28.f}, {35.f, 28.f},
        };
        for (int i = 0; i < 6; i++) {
            float lx = LAMP_POS[i][0];
            float lz = LAMP_POS[i][1];
            /* pied */
            gfx_box_draw(gc, v3_make(lx, 0.20f, lz),
                         v3_make(0.08f, 0.40f, 0.08f),
                         0.18f, 0.14f, 0.10f);
            /* lampe qui flicker (pulse subtile sur time + offset) */
            float flick = 0.85f + sinf(t * 4.f + i * 1.7f) * 0.15f;
            gfx_box_draw(gc, v3_make(lx, 0.55f, lz),
                         v3_make(0.15f, 0.15f, 0.15f),
                         1.00f * flick, 0.75f * flick, 0.30f * flick);
        }
        /* chemin pave devant la porte du donjon : 3 dalles claires
         * entre le spawn et la porte centrale. */
        for (int i = 0; i < 4; i++) {
            float pz = 30.f - i * 1.5f;
            gfx_box_draw(gc, v3_make(28.f, 0.02f, pz),
                         v3_make(0.8f, 0.04f, 0.8f),
                         0.35f, 0.32f, 0.28f);
        }
        /* runes pulsantes au sol devant les batiments (un cercle de 4
         * petits cubes violets qui battent). */
        {
            float pulse = 0.5f + 0.5f * sinf(t * 2.5f);
            float ry = 0.04f;
            float rad = 0.45f;
            float runes_cx[5] = { 19.f, 28.f, 37.f, 20.f, 36.f };
            float runes_cz[5] = { 23.5f, 23.f, 23.5f, 32.5f, 32.5f };
            for (int b = 0; b < 5; b++) {
                for (int k = 0; k < 4; k++) {
                    float a = (k / 4.f) * 6.2831f;
                    float rx = runes_cx[b] + cosf(a) * rad;
                    float rz = runes_cz[b] + sinf(a) * rad;
                    gfx_box_draw(gc, v3_make(rx, ry, rz),
                                 v3_make(0.06f, 0.03f, 0.06f),
                                 0.40f + 0.30f * pulse,
                                 0.20f + 0.15f * pulse,
                                 0.80f);
                }
            }
        }
        /* particules ambient : poussiere lente qui tombe (un par frame
         * a position aleatoire dans la salle). */
        if ((rand() % 100) < 25) {
            float dx = 17.f + (rand() % 100) / 100.f * 22.f;
            float dz = 22.f + (rand() % 100) / 100.f * 12.f;
            particle_spawn_kind(g,
                dx * TILE, dz * TILE,
                0, -8.f,
                1.5f, 0x504050A0, 1.4f, 0);
        }
        int n = hub_building_count();
        for (int i = 0; i < n; i++) {
            const HubBuilding *b = hub_building_get(i);
            if (!b) continue;
            float bx = hub_building_x(b) / (float)TILE;
            float bz = hub_building_y(b) / (float)TILE;
            int sid = hub_building_sub_id(b);
            /* couleur par batiment */
            float cr = 0.45f, cg = 0.30f, cb = 0.22f;
            float rr = 0.30f, rg = 0.15f, rb = 0.18f;
            switch (sid) {
                case  0: /* DONJON : porte sombre / arche */
                    cr = 0.20f; cg = 0.18f; cb = 0.25f;
                    rr = 0.10f; rg = 0.08f; rb = 0.14f;
                    break;
                case -1: /* TAVERNE : bois chaud */
                    cr = 0.55f; cg = 0.32f; cb = 0.18f;
                    rr = 0.40f; rg = 0.20f; rb = 0.12f;
                    break;
                case  1: /* TEMPLE : pierre claire */
                    cr = 0.62f; cg = 0.58f; cb = 0.50f;
                    rr = 0.30f; rg = 0.28f; rb = 0.40f;
                    break;
                case  2: /* FORGE : fer/brique */
                    cr = 0.42f; cg = 0.24f; cb = 0.20f;
                    rr = 0.20f; rg = 0.12f; rb = 0.10f;
                    break;
                case  3: /* LICHE : violet froid */
                    cr = 0.28f; cg = 0.22f; cb = 0.42f;
                    rr = 0.18f; rg = 0.14f; rb = 0.28f;
                    break;
            }
            if (sid == 0) {
                /* DONJON : grande arche, deux piliers + linteau */
                gfx_box_draw(gc, v3_make(bx - 0.7f, 0.9f, bz),
                             v3_make(0.4f, 1.8f, 0.6f), cr, cg, cb);
                gfx_box_draw(gc, v3_make(bx + 0.7f, 0.9f, bz),
                             v3_make(0.4f, 1.8f, 0.6f), cr, cg, cb);
                gfx_box_draw(gc, v3_make(bx, 1.9f, bz),
                             v3_make(1.8f, 0.3f, 0.6f), rr, rg, rb);
                /* haut symbolique : pierre tombale en V */
                gfx_box_draw(gc, v3_make(bx, 2.25f, bz),
                             v3_make(0.5f, 0.4f, 0.45f), rr, rg, rb);
            } else {
                /* maisonnette : socle + corps + toit (pyramide bas/haut)  */
                gfx_box_draw(gc, v3_make(bx, 0.10f, bz),
                             v3_make(1.7f, 0.20f, 1.7f), rr, rg, rb);
                gfx_box_draw(gc, v3_make(bx, 0.75f, bz),
                             v3_make(1.5f, 1.20f, 1.5f), cr, cg, cb);
                gfx_box_draw(gc, v3_make(bx, 1.55f, bz),
                             v3_make(1.7f, 0.20f, 1.7f), rr, rg, rb);
                gfx_box_draw(gc, v3_make(bx, 1.80f, bz),
                             v3_make(1.2f, 0.25f, 1.2f), rr, rg, rb);
                gfx_box_draw(gc, v3_make(bx, 2.05f, bz),
                             v3_make(0.7f, 0.20f, 0.7f), rr, rg, rb);
                /* porte sombre */
                gfx_box_draw(gc, v3_make(bx, 0.45f, bz - 0.74f),
                             v3_make(0.40f, 0.60f, 0.04f), 0.08f, 0.06f, 0.05f);
                /* lanternes : 2 petits cubes lumineux aux coins avant */
                gfx_box_draw(gc, v3_make(bx - 0.65f, 1.05f, bz - 0.7f),
                             v3_make(0.10f, 0.10f, 0.10f), 1.0f, 0.75f, 0.30f);
                gfx_box_draw(gc, v3_make(bx + 0.65f, 1.05f, bz - 0.7f),
                             v3_make(0.10f, 0.10f, 0.10f), 1.0f, 0.75f, 0.30f);
            }
        }
    }
}

/* HP bars + names + dmg numbers : passe UI (apres gfx_ui_begin).
   Expose en non-static car appelee par main.c. */
void render_world_overlay_ui(Game *g) {
    /* Labels au-dessus des portails de l'arene-pivot (floor 0).
     * Affiche destination + status (verrouille / X biomes manquants). */
    if (g->floor_index == 0) {
        GfxCtx *gc_ = g->renderer;
        for (int i = 0; i < MAX_PICKUPS; i++) {
            Pickup *pk = &g->pickups[i];
            if (!pk->alive || pk->kind != PU_PORTAL) continue;
            v3 head = v3_make(pk->x / TILE, 2.6f, pk->y / TILE);
            int sx, sy;
            if (!world_to_screen(gc_, head, &sx, &sy)) continue;
            int v = pk->value;
            char label[48];
            uint32_t col = 0xFFE080FF;
            bool locked = false;
            if (v == -1) {
                snprintf(label, sizeof(label), "[ HUB ]");
                col = 0xFFD040FF;
            } else if (v >= 0 && v <= 4) {
                snprintf(label, sizeof(label), "%s", biome_name(v));
                if (g->biome_cleared[v]) {
                    locked = true;
                    col = 0x808080FF;
                } else {
                    float br_, bg_, bb_;
                    biome_tint(v, &br_, &bg_, &bb_);
                    col = ((uint32_t)(br_ * 255) << 24)
                        | ((uint32_t)(bg_ * 255) << 16)
                        | ((uint32_t)(bb_ * 255) << 8)
                        | 0xFF;
                }
            } else if (v == 5) {
                int n = 0;
                for (int b = 0; b < 5; b++) if (g->biome_cleared[b]) n++;
                if (n < 5) {
                    snprintf(label, sizeof(label), "ARCHIMAGE (%d/5)", n);
                    locked = true;
                    col = 0x808080FF;
                } else {
                    snprintf(label, sizeof(label), "ARCHIMAGE");
                    col = 0xFF80E0FF;
                }
            } else {
                continue;
            }
            int tw = text_width(label);
            gfx_set_blend(gc_, true);
            fill_rect(gc_, sx - tw/2 - 3, sy - 2, tw + 6, 10, 0x000000C0);
            gfx_set_blend(gc_, false);
            text_draw(gc_, sx - tw/2, sy, label, col);
            if (locked) {
                int lw = text_width("verrouille");
                text_draw(gc_, sx - lw/2, sy + 10, "verrouille", 0xFF6060FF);
            }
        }
        /* banniere centre-haut */
        const char *banner = "Salle des sept portails -- Choisis ton chemin";
        int btw = text_width(banner);
        int bx = INTERNAL_W/2 - btw/2;
        gfx_set_blend(g->renderer, true);
        fill_rect(g->renderer, bx - 6, 22, btw + 12, 12, 0x000000A0);
        gfx_set_blend(g->renderer, false);
        text_draw(g->renderer, bx, 24, banner, 0xFFE080FF);
    }

    GfxCtx *gc = g->renderer;
    /* Pickups durs (items / armes / elements) : nom au-dessus quand on
     * approche. Ajoute [E] quand on est dans la zone de ramassage. */
    for (int i = 0; i < MAX_PICKUPS; i++) {
        Pickup *pk = &g->pickups[i];
        if (!pk->alive) continue;
        bool is_hard = (pk->kind == PU_ITEM || pk->kind == PU_WEAPON ||
                        pk->kind == PU_ELEMENT);
        if (!is_hard) continue;
        float dx = pk->x - g->player.x, dy = pk->y - g->player.y;
        float d2 = dx * dx + dy * dy;
        if (d2 > 90.f * 90.f) continue;
        v3 head = v3_make(pk->x / TILE, 1.4f, pk->y / TILE);
        int sx, sy;
        if (!world_to_screen(gc, head, &sx, &sy)) continue;
        char label[64];
        uint32_t col = 0xCCCCCCFF;
        if (pk->kind == PU_ITEM) {
            const char *nm = pk->item.name[0] ? pk->item.name
                                              : slot_name(pk->item.slot);
            snprintf(label, sizeof(label), "%s", nm);
            col = pk->item.is_unique ? 0xFF8030FF
                                     : rarity_color(pk->item.rarity);
        } else if (pk->kind == PU_WEAPON) {
            int kind = pk->value & 0xFF;
            int rar  = (pk->value >> 8) & 0xFF;
            snprintf(label, sizeof(label), "%s", weapon_name((WeaponKind)kind));
            if (rar >= 0 && rar < R_COUNT) col = rarity_color((Rarity)rar);
        } else { /* PU_ELEMENT */
            snprintf(label, sizeof(label), "%s", element_name((Element)pk->value));
            col = element_color((Element)pk->value);
        }
        int tw = text_width(label);
        gfx_set_blend(gc, true);
        fill_rect(gc, sx - tw/2 - 2, sy - 3, tw + 4, 9, 0x00000090);
        gfx_set_blend(gc, false);
        text_draw(gc, sx - tw/2, sy - 2, label, col);
        /* [E] prompt si dans la zone de ramassage (rayon 18) */
        if (d2 < 18.f * 18.f) {
            const char *prompt = "[E] ramasser";
            int pw = text_width(prompt);
            gfx_set_blend(gc, true);
            fill_rect(gc, sx - pw/2 - 2, sy + 7, pw + 4, 9, 0x000000C0);
            gfx_set_blend(gc, false);
            text_draw(gc, sx - pw/2, sy + 8, prompt, 0xFFFF80FF);
        }
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
    /* OVERDRIVE aura : couronne rouge serree autour du joueur pendant
     * toute la duree. Plus serre que l aura overload pour distinguer. */
    if (g->overdrive_t > 0.f) {
        int n = 18;
        float radius = 14.f;
        for (int i = 0; i < n; i++) {
            float a = (i / (float)n) * 6.2831f + g->time * 4.f;
            particle_spawn_kind(g,
                g->player.x + cosf(a) * radius,
                g->player.y + sinf(a) * radius,
                cosf(a) * -10.f, sinf(a) * -10.f,
                0.10f, 0xFF3030FF, 2.2f, 0);
        }
        /* tag "OVERDRIVE" au-dessus du joueur */
        v3 head = v3_make(g->player.x / TILE, 1.5f, g->player.y / TILE);
        int sx, sy;
        if (world_to_screen(gc, head, &sx, &sy)) {
            const char *m = "OVERDRIVE";
            int tw = text_width(m);
            text_draw(gc, sx - tw/2 + 1, sy - 18 + 1, m, 0x000000FF);
            text_draw(gc, sx - tw/2,     sy - 18,     m, 0xFF3030FF);
        }
    }
    /* HP bars + noms au-dessus des ennemis. Setting mob_healthbars =
     * affiche meme a pleine vie (bool). Les boss sont toujours visibles
     * (gameplay critical). */
    bool show_full = g->settings.mob_healthbars != 0;
    for (int i = 0; i < MAX_ENEMIES; i++) {
        Enemy *e = &g->enemies[i];
        if (!e->alive) continue;
        if (e->dying_t > 0.f) continue;
        v3 head = v3_make(e->x / TILE, (e->is_boss ? 2.0f : 1.4f),
                          e->y / TILE);
        int sx, sy;
        if (!world_to_screen(gc, head, &sx, &sy)) continue;
        bool show_bar = e->is_boss || show_full || (e->hp < e->maxhp);
        if (show_bar && e->maxhp > 0.f) {
            int bw = e->is_boss ? 80 : 24;
            int bh = e->is_boss ? 4 : 2;
            int bx = sx - bw / 2, by = sy;
            /* fond noir cadre pour la lisibilite */
            fill_rect(gc, bx - 1, by - 1, bw + 2, bh + 2, 0x000000C0);
            fill_rect(gc, bx, by, bw, bh, 0x402020FF);
            float frac = e->hp / e->maxhp;
            if (frac < 0.f) frac = 0.f;
            if (frac > 1.f) frac = 1.f;
            int hf = (int)(bw * frac);
            /* couleur : vert > 60%, jaune 30-60%, rouge < 30% */
            uint32_t hpcol;
            if      (frac > 0.6f) hpcol = 0x60D040FF;
            else if (frac > 0.3f) hpcol = 0xFFC040FF;
            else                  hpcol = 0xFF4040FF;
            fill_rect(gc, bx, by, hf, bh, hpcol);
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
            /* tag "STASE" pour l'archimage */
            if (bb->is_archmage && g->arch_stasis_t > 0.f) {
                text_draw(gc, bx + bw - text_width("STASE"),
                          by + bh + 2, "STASE", 0x80C0FFFF);
            }
        }
    }

    /* === SPEECH ARCHIMAGE === bandeau central avec son nom + sa phrase */
    if (g->arch_speech_t > 0.f && g->arch_speech[0]) {
        int tw = text_width(g->arch_speech);
        int box_w = tw + 28;
        if (box_w > INTERNAL_W - 20) box_w = INTERNAL_W - 20;
        int box_h = 26;
        int bx = INTERNAL_W/2 - box_w/2;
        int by = INTERNAL_H/2 - 60;
        float a01 = g->arch_speech_t / 5.f;
        if (a01 > 1.f) a01 = 1.f;
        uint8_t alpha = (uint8_t)(220 * (a01 > 0.2f ? 1.f : a01 / 0.2f));
        gfx_set_blend(gc, true);
        fill_rect(gc, bx, by, box_w, box_h,
                  (uint32_t)((0x10u << 24) | (0x05u << 16) | (0x20u << 8) | (uint32_t)alpha));
        gfx_set_blend(gc, false);
        rect_outline(gc, bx, by, box_w, box_h, 0xFFA040FF);
        text_draw(gc, bx + 14, by + 4, "ARCHIMAGE", 0xFFA040FF);
        text_draw(gc, bx + 14, by + 14, g->arch_speech, 0xFFFFFFFF);
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

