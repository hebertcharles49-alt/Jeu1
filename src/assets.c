/*
 * assets.c - "habillage" du monde : props decoratifs 3D (barriques, vases,
 * champignons, statues, candelabres, bannieres, debris) + particules
 * d'ambiance (poussiere, embers, gouttes, flammes).
 *
 * Sous-fichier de world.c : meme cycle de vie (reset / populate apres
 * dungeon_generate, tick par frame en GS_RUN, render dans render_world
 * apres le terrain mais avant les entites mobiles).
 *
 * Stockage en pool module-prive (S_PROPS[MAX_PROPS]) pour eviter d'avoir
 * a toucher Game / game.h. Cf game.h pour les seules signatures publiques.
 */
#include "game.h"
#include "gfx.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================
 *  PROPS - elements decoratifs statiques
 * ============================================================ */
typedef enum {
    PROP_NONE = 0,
    PROP_BARREL,       /* baril en bois sombre */
    PROP_CRATE,        /* caisse claire */
    PROP_VASE,         /* vase fin */
    PROP_MUSHROOM,     /* champignon (couleur variant) */
    PROP_BOOKPILE,     /* pile de livres / parchemins */
    PROP_BONES,        /* tas d ossements */
    PROP_CANDLE,       /* chandelier (emet flamme) */
    PROP_DEBRIS,       /* debris bas */
    PROP_PILLAR,       /* colonne (salle boss) */
    PROP_STATUE,       /* statue (salle boss) */
    PROP_BANNER,       /* banniere accrochee (sway) */
    PROP_COUNT
} PropKind;

typedef struct {
    bool      alive;
    float     x, y;        /* coords tile (avec sub-tile possible) */
    PropKind  kind;
    uint8_t   variant;     /* tint / orientation */
    uint8_t   floor_gen;   /* gen_id du donjon pour invalidation lazy */
} WorldProp;

#define MAX_PROPS 256
static WorldProp S_PROPS[MAX_PROPS];
static int       S_PROPS_GEN = -1;     /* gen_id du donjon pour lequel S_PROPS est valide */

/* compteur d'embers : on dose pour ne pas exploser le pool de particules. */
static float S_AMBIENT_T = 0.f;

/* ============================================================
 *  POPULATION (placement procedural des props)
 * ============================================================ */

void world_assets_reset(Game *g) {
    (void)g;
    memset(S_PROPS, 0, sizeof(S_PROPS));
    S_PROPS_GEN = -1;
    S_AMBIENT_T = 0.f;
}

static WorldProp *prop_alloc(void) {
    for (int i = 0; i < MAX_PROPS; i++)
        if (!S_PROPS[i].alive) return &S_PROPS[i];
    return NULL;
}

/* place un prop a (tx, ty) si tile libre. Renvoie true si pose. */
static bool try_place_prop(Game *g, PropKind kind, int tx, int ty, uint8_t variant) {
    if (tx <= 0 || ty <= 0 || tx >= MAP_W - 1 || ty >= MAP_H - 1) return false;
    TileKind t = g->dungeon.tiles[ty][tx];
    if (t != T_FLOOR) return false;     /* on respecte torches/blood/bones existants */
    /* aussi : ne pas placer sur la tile de spawn ou exit pour ne pas bloquer
     * visuellement le joueur. */
    if (tx == g->dungeon.spawn_x && ty == g->dungeon.spawn_y) return false;
    if (tx == g->dungeon.exit_x  && ty == g->dungeon.exit_y)  return false;
    WorldProp *p = prop_alloc();
    if (!p) return false;
    p->alive = true;
    p->kind = kind;
    p->variant = variant;
    /* leger jitter sub-tile pour casser la grille */
    float jx = ((rand() % 100) - 50) / 200.f;     /* +/- 0.25 */
    float jy = ((rand() % 100) - 50) / 200.f;
    p->x = tx + 0.5f + jx;
    p->y = ty + 0.5f + jy;
    p->floor_gen = (uint8_t)(g->dungeon.gen_id & 0xFF);
    return true;
}

/* table de probabilites par genre de salle. Boss : statues / pilliers.
 * Salle de spawn : peu de choses, surtout vases / livres. Salles normales :
 * barriques + caisses + champignons. */
static PropKind pick_prop_for_room(Game *g, const Room *r, int slot_idx) {
    if (r->is_boss_room) {
        /* alternance statue / pilier sur les coins ; debris ailleurs. */
        if (slot_idx < 4) return PROP_PILLAR;
        if (slot_idx == 4) return PROP_STATUE;
        return PROP_DEBRIS;
    }
    if (r->is_debug_room) {
        /* salle bac-a-sable : tres peu d'asset pour ne pas noyer le loot. */
        return PROP_CANDLE;
    }
    /* spawn room (cleared, room 0) : ambiance "hub" */
    if (r == &g->dungeon.rooms[0]) {
        static const PropKind T[] = { PROP_VASE, PROP_BOOKPILE, PROP_CANDLE, PROP_CRATE };
        return T[rand() % (int)(sizeof(T)/sizeof(T[0]))];
    }
    /* salle normale : pool varie, biaisé sur barriques/caisses/champignons */
    static const PropKind T[] = {
        PROP_BARREL, PROP_BARREL, PROP_CRATE, PROP_CRATE,
        PROP_MUSHROOM, PROP_MUSHROOM, PROP_VASE,
        PROP_BONES, PROP_DEBRIS, PROP_CANDLE, PROP_BANNER,
    };
    return T[rand() % (int)(sizeof(T)/sizeof(T[0]))];
}

/* place les props d une salle. Budget = aire / 14, capped a 6 par salle
 * pour ne pas etouffer le combat. */
static void populate_room(Game *g, const Room *r) {
    int area = r->w * r->h;
    int budget = area / 14;
    if (budget > 6) budget = 6;
    if (r->is_boss_room) budget = 6;        /* boss room toujours decoree */
    if (r->is_debug_room) budget = 2;

    /* slots privilegies : coins + bords (les ennemis aiment le centre).
     * Pour les boss, on tente de poser des pilliers symetriques d abord. */
    int placed = 0;
    if (r->is_boss_room) {
        /* 4 coins en pilier, puis 1 statue au fond, le reste hors-coins. */
        int cx[4] = { r->x + 1, r->x + r->w - 2, r->x + 1, r->x + r->w - 2 };
        int cy[4] = { r->y + 1, r->y + 1, r->y + r->h - 2, r->y + r->h - 2 };
        for (int i = 0; i < 4 && placed < budget; i++) {
            if (try_place_prop(g, PROP_PILLAR, cx[i], cy[i], 0)) placed++;
        }
        /* statue au fond */
        if (placed < budget) {
            int sx = r->x + r->w / 2;
            int sy = r->y + 1;
            if (try_place_prop(g, PROP_STATUE, sx, sy, 0)) placed++;
        }
    }
    /* remplissage random : tirages bornes pour eviter une boucle infinie */
    int attempts = 0;
    while (placed < budget && attempts < 30) {
        attempts++;
        int tx = r->x + 1 + rand() % (r->w - 2);
        int ty = r->y + 1 + rand() % (r->h - 2);
        PropKind k = pick_prop_for_room(g, r, placed);
        uint8_t variant = (uint8_t)(rand() & 0x3);
        if (try_place_prop(g, k, tx, ty, variant)) placed++;
    }
}

void world_assets_populate(Game *g) {
    /* idempotent : ne re-populate que si le donjon a change. */
    if (S_PROPS_GEN == g->dungeon.gen_id) return;
    memset(S_PROPS, 0, sizeof(S_PROPS));
    for (int i = 0; i < g->dungeon.room_count; i++) {
        populate_room(g, &g->dungeon.rooms[i]);
    }
    S_PROPS_GEN = g->dungeon.gen_id;
}

/* ============================================================
 *  TICK - particules d'ambiance + animations time-based
 * ============================================================ */

/* renvoie l index de la salle qui contient le joueur, -1 sinon. */
static int player_room_index(Game *g) {
    int ptx = (int)(g->player.x / TILE);
    int pty = (int)(g->player.y / TILE);
    for (int i = 0; i < g->dungeon.room_count; i++) {
        const Room *r = &g->dungeon.rooms[i];
        if (ptx >= r->x && ptx < r->x + r->w &&
            pty >= r->y && pty < r->y + r->h) return i;
    }
    return -1;
}

void world_assets_tick(Game *g) {
    /* embers de chandelier : chaque CANDLE crache 1 particule toutes les
     * 0.15s en moyenne. On batch via timer global pour le cout CPU. */
    S_AMBIENT_T += g->dt;
    if (S_AMBIENT_T > 0.15f) {
        S_AMBIENT_T = 0.f;
        for (int i = 0; i < MAX_PROPS; i++) {
            WorldProp *p = &S_PROPS[i];
            if (!p->alive || p->kind != PROP_CANDLE) continue;
            /* coupe les chandeliers hors-ecran : on n emet que dans la salle
             * du joueur (ou ses voisines proches). */
            float ddx = p->x * TILE - g->player.x;
            float ddy = p->y * TILE - g->player.y;
            if (ddx*ddx + ddy*ddy > 200.f * 200.f) continue;
            /* petite flamme + spark vertical */
            particle_spawn_kind(g, p->x * TILE, p->y * TILE,
                                ((rand()%20) - 10) * 0.5f,
                                -20.f - (rand()%20),
                                0.40f, 0xFFC060FF, 1.6f, 0);
            if ((rand() % 4) == 0) {
                particle_spawn_kind(g, p->x * TILE, p->y * TILE,
                                    0, -30.f,
                                    0.30f, 0xFFE890FF, 1.0f, 0);
            }
        }
    }
    /* poussiere d ambiance dans la salle courante : 1 particule par seconde
     * en moyenne, pour le feeling "rayons de lumiere" sans saturer. */
    int rid = player_room_index(g);
    if (rid >= 0 && (rand() % 100) < 3) {
        const Room *r = &g->dungeon.rooms[rid];
        float px = (r->x + 1 + rand() % (r->w - 2)) * (float)TILE + (rand()%16);
        float py = (r->y + 1 + rand() % (r->h - 2)) * (float)TILE + (rand()%16);
        particle_spawn_kind(g, px, py, 0, -6.f, 2.0f, 0x80808060, 1.0f, 0);
    }
}

/* ============================================================
 *  RENDER - les props sont dessines en cubes 3D
 * ============================================================ */

/* helper : convertit (tile_x, tile_y) en coords monde (x: tx, z: ty). */
static v3 prop_world_pos(const WorldProp *p, float y) {
    return v3_make(p->x, y, p->y);
}

static void render_one_prop(Game *g, WorldProp *p) {
    GfxCtx *gc = g->renderer;
    float t = g->time;
    switch (p->kind) {
        case PROP_BARREL: {
            /* corps brun + cerceau plus clair */
            gfx_box_draw(gc, prop_world_pos(p, 0.35f),
                         v3_make(0.32f, 0.35f, 0.32f),
                         0.45f, 0.28f, 0.16f);
            gfx_box_draw(gc, prop_world_pos(p, 0.45f),
                         v3_make(0.34f, 0.04f, 0.34f),
                         0.30f, 0.18f, 0.12f);
            gfx_box_draw(gc, prop_world_pos(p, 0.20f),
                         v3_make(0.34f, 0.04f, 0.34f),
                         0.30f, 0.18f, 0.12f);
            break;
        }
        case PROP_CRATE: {
            /* cube clair plus haut, avec traverses cruciformes */
            gfx_box_draw(gc, prop_world_pos(p, 0.32f),
                         v3_make(0.36f, 0.32f, 0.36f),
                         0.65f, 0.45f, 0.22f);
            gfx_box_draw(gc, prop_world_pos(p, 0.32f),
                         v3_make(0.40f, 0.04f, 0.10f),
                         0.40f, 0.25f, 0.12f);
            gfx_box_draw(gc, prop_world_pos(p, 0.32f),
                         v3_make(0.10f, 0.04f, 0.40f),
                         0.40f, 0.25f, 0.12f);
            break;
        }
        case PROP_VASE: {
            /* base + col + bouchon, couleur selon variant */
            float vr = 0.40f + (p->variant & 1) * 0.20f;
            float vg = 0.20f + ((p->variant >> 1) & 1) * 0.30f;
            float vb = 0.35f;
            gfx_box_draw(gc, prop_world_pos(p, 0.20f),
                         v3_make(0.26f, 0.18f, 0.26f),
                         vr, vg, vb);
            gfx_box_draw(gc, prop_world_pos(p, 0.40f),
                         v3_make(0.16f, 0.22f, 0.16f),
                         vr * 1.1f, vg * 1.1f, vb * 1.1f);
            gfx_box_draw(gc, prop_world_pos(p, 0.55f),
                         v3_make(0.20f, 0.06f, 0.20f),
                         vr * 0.7f, vg * 0.7f, vb * 0.7f);
            break;
        }
        case PROP_MUSHROOM: {
            /* pied blanc + chapeau colore (rouge / bleu / violet selon variant) */
            float cr, cg, cb;
            switch (p->variant & 3) {
                case 0:  cr=0.80f; cg=0.15f; cb=0.15f; break;   /* rouge */
                case 1:  cr=0.30f; cg=0.50f; cb=0.95f; break;   /* bleu */
                case 2:  cr=0.70f; cg=0.35f; cb=0.85f; break;   /* violet */
                default: cr=0.95f; cg=0.85f; cb=0.30f; break;   /* doree */
            }
            gfx_box_draw(gc, prop_world_pos(p, 0.10f),
                         v3_make(0.10f, 0.15f, 0.10f),
                         0.85f, 0.85f, 0.75f);
            gfx_box_draw(gc, prop_world_pos(p, 0.22f),
                         v3_make(0.26f, 0.10f, 0.26f),
                         cr, cg, cb);
            gfx_box_draw(gc, prop_world_pos(p, 0.30f),
                         v3_make(0.14f, 0.06f, 0.14f),
                         cr * 0.7f, cg * 0.7f, cb * 0.7f);
            break;
        }
        case PROP_BOOKPILE: {
            /* 3 livres empiles, couleurs different */
            gfx_box_draw(gc, prop_world_pos(p, 0.08f),
                         v3_make(0.28f, 0.08f, 0.20f),
                         0.55f, 0.25f, 0.20f);
            gfx_box_draw(gc, prop_world_pos(p, 0.18f),
                         v3_make(0.26f, 0.08f, 0.22f),
                         0.30f, 0.45f, 0.55f);
            gfx_box_draw(gc, prop_world_pos(p, 0.28f),
                         v3_make(0.22f, 0.06f, 0.18f),
                         0.45f, 0.30f, 0.15f);
            break;
        }
        case PROP_BONES: {
            /* tas bas blanchatre + crane (cube clair surplombe) */
            gfx_box_draw(gc, prop_world_pos(p, 0.05f),
                         v3_make(0.40f, 0.06f, 0.40f),
                         0.78f, 0.75f, 0.65f);
            gfx_box_draw(gc, prop_world_pos(p, 0.15f),
                         v3_make(0.15f, 0.15f, 0.15f),
                         0.88f, 0.85f, 0.75f);
            break;
        }
        case PROP_CANDLE: {
            /* socle + pilier en cire + meche, flamme animee via particles */
            gfx_box_draw(gc, prop_world_pos(p, 0.04f),
                         v3_make(0.28f, 0.04f, 0.28f),
                         0.25f, 0.20f, 0.16f);
            gfx_box_draw(gc, prop_world_pos(p, 0.30f),
                         v3_make(0.10f, 0.50f, 0.10f),
                         0.92f, 0.85f, 0.70f);
            /* glow cube (mini-flamme) animee */
            float pulse = 0.7f + 0.3f * sinf(t * 5.f + p->x);
            gfx_box_draw(gc, prop_world_pos(p, 0.62f),
                         v3_make(0.06f, 0.10f, 0.06f),
                         1.0f * pulse, 0.55f * pulse, 0.20f * pulse);
            break;
        }
        case PROP_DEBRIS: {
            /* 2-3 morceaux sombres bas */
            gfx_box_draw(gc, prop_world_pos(p, 0.04f),
                         v3_make(0.18f, 0.06f, 0.30f),
                         0.30f, 0.25f, 0.22f);
            gfx_box_draw(gc, v3_make(p->x + 0.18f, 0.05f, p->y + 0.10f),
                         v3_make(0.12f, 0.08f, 0.16f),
                         0.35f, 0.28f, 0.20f);
            break;
        }
        case PROP_PILLAR: {
            /* haute colonne en pierre */
            gfx_box_draw(gc, prop_world_pos(p, 0.05f),
                         v3_make(0.50f, 0.06f, 0.50f),
                         0.55f, 0.55f, 0.60f);
            gfx_box_draw(gc, prop_world_pos(p, 1.0f),
                         v3_make(0.34f, 1.85f, 0.34f),
                         0.60f, 0.60f, 0.65f);
            gfx_box_draw(gc, prop_world_pos(p, 1.95f),
                         v3_make(0.50f, 0.08f, 0.50f),
                         0.55f, 0.55f, 0.60f);
            break;
        }
        case PROP_STATUE: {
            /* socle + corps + tete : silhouette de gardien */
            gfx_box_draw(gc, prop_world_pos(p, 0.10f),
                         v3_make(0.50f, 0.12f, 0.50f),
                         0.45f, 0.45f, 0.50f);
            gfx_box_draw(gc, prop_world_pos(p, 0.55f),
                         v3_make(0.32f, 0.55f, 0.32f),
                         0.50f, 0.50f, 0.55f);
            gfx_box_draw(gc, prop_world_pos(p, 1.00f),
                         v3_make(0.22f, 0.22f, 0.22f),
                         0.55f, 0.55f, 0.60f);
            break;
        }
        case PROP_BANNER: {
            /* tissu rouge / vert / bleu / violet qui ondule selon variant */
            float br, bg, bb;
            switch (p->variant & 3) {
                case 0:  br=0.75f; bg=0.15f; bb=0.15f; break;   /* rouge */
                case 1:  br=0.20f; bg=0.55f; bb=0.30f; break;   /* vert  */
                case 2:  br=0.20f; bg=0.30f; bb=0.65f; break;   /* bleu */
                default: br=0.50f; bg=0.20f; bb=0.55f; break;   /* violet */
            }
            /* mat sombre */
            gfx_box_draw(gc, prop_world_pos(p, 1.10f),
                         v3_make(0.06f, 1.10f, 0.06f),
                         0.22f, 0.18f, 0.15f);
            /* tissu : oscille en x via offset sin(time) */
            float sway = sinf(t * 1.5f + p->x * 0.5f) * 0.03f;
            gfx_box_draw(gc,
                v3_make(p->x + sway, 0.85f, p->y),
                v3_make(0.18f, 0.55f, 0.04f),
                br, bg, bb);
            break;
        }
        default: break;
    }
}

void world_assets_render(Game *g) {
    for (int i = 0; i < MAX_PROPS; i++) {
        WorldProp *p = &S_PROPS[i];
        if (!p->alive) continue;
        render_one_prop(g, p);
    }
}

bool world_assets_bump_at(Game *g, float x_world, float y_world, float radius) {
    /* x_world / y_world sont en pixels-monde (cf Player.x/y, Enemy.x/y) ;
     * nos props sont stockes en tile-units. On convertit. */
    float ptx = x_world / (float)TILE;
    float pty = y_world / (float)TILE;
    float r2  = (radius / (float)TILE);
    r2 = r2 * r2;
    bool hit = false;
    for (int i = 0; i < MAX_PROPS; i++) {
        WorldProp *p = &S_PROPS[i];
        if (!p->alive) continue;
        /* pilliers / statues / bannieres sont "lourds" -- ils ne s envolent
         * pas. Pour eux on emet juste des etincelles, pas de destruction. */
        float dx = p->x - ptx;
        float dy = p->y - pty;
        if (dx * dx + dy * dy > r2) continue;
        hit = true;
        /* burst de particules sur la position du prop */
        float wx = p->x * TILE;
        float wy = p->y * TILE;
        uint32_t col = 0xA08070FF;
        switch (p->kind) {
            case PROP_BARREL: case PROP_CRATE: case PROP_DEBRIS:
                col = 0x806040FF; break;       /* eclats de bois */
            case PROP_VASE:
                col = 0x80A0C0FF; break;       /* poterie */
            case PROP_BOOKPILE:
                col = 0xC0A060FF; break;       /* parchemins */
            case PROP_BONES:
                col = 0xE0E0C0FF; break;       /* os pulverises */
            case PROP_MUSHROOM:
                col = 0xA0E0A0FF; break;       /* spores */
            default: break;
        }
        for (int k = 0; k < 10; k++) {
            float a = (rand() % 360) * 0.01745f;
            float s = 50.f + (rand() % 60);
            particle_spawn_kind(g, wx, wy,
                                cosf(a) * s, sinf(a) * s,
                                0.40f, col, 2.0f, 2);
        }
        /* les props "legers" sont detruits par l impact (1-shot). On garde
         * les pilliers / statues / bannieres : ils resistent. */
        if (p->kind == PROP_BARREL || p->kind == PROP_CRATE ||
            p->kind == PROP_VASE   || p->kind == PROP_BONES ||
            p->kind == PROP_BOOKPILE || p->kind == PROP_DEBRIS ||
            p->kind == PROP_MUSHROOM) {
            p->alive = false;
        }
    }
    return hit;
}
