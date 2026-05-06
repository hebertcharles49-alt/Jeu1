/*
 * world.c - dungeon, player movement, enemies, pickups, room logic
 */
#include "game.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

bool tile_solid(TileKind t) {
    return t == T_VOID || t == T_WALL;
}

/* ---------- DUNGEON GENERATION ---------- */
static void carve_room(Dungeon *d, int x, int y, int w, int h) {
    for (int yy = y; yy < y + h; yy++) {
        for (int xx = x; xx < x + w; xx++) {
            if (xx <= 0 || yy <= 0 || xx >= MAP_W - 1 || yy >= MAP_H - 1) continue;
            d->tiles[yy][xx] = T_FLOOR;
        }
    }
}

static void carve_corridor(Dungeon *d, int x1, int y1, int x2, int y2) {
    int x = x1, y = y1;
    while (x != x2) {
        if (x > 0 && y > 0 && x < MAP_W - 1 && y < MAP_H - 1) d->tiles[y][x] = T_FLOOR;
        x += (x2 > x) ? 1 : -1;
    }
    while (y != y2) {
        if (x > 0 && y > 0 && x < MAP_W - 1 && y < MAP_H - 1) d->tiles[y][x] = T_FLOOR;
        y += (y2 > y) ? 1 : -1;
    }
    if (x > 0 && y > 0 && x < MAP_W - 1 && y < MAP_H - 1) d->tiles[y][x] = T_FLOOR;
}

static int rand_range(int lo, int hi) {
    if (hi <= lo) return lo;
    return lo + rand() % (hi - lo);
}

void dungeon_generate(Dungeon *d, int floor_index, unsigned seed) {
    srand(seed);
    memset(d, 0, sizeof(*d));
    d->level_index = floor_index;
    /* fill with walls */
    for (int y = 0; y < MAP_H; y++)
        for (int x = 0; x < MAP_W; x++)
            d->tiles[y][x] = T_WALL;

    int target_rooms = 6 + floor_index;
    if (target_rooms > 14) target_rooms = 14;
    int placed = 0, attempts = 0;
    while (placed < target_rooms && attempts < 200) {
        attempts++;
        int rw = rand_range(6, 12);
        int rh = rand_range(6, 10);
        int rx = rand_range(2, MAP_W - rw - 2);
        int ry = rand_range(2, MAP_H - rh - 2);
        bool overlap = false;
        for (int i = 0; i < placed; i++) {
            Room *o = &d->rooms[i];
            if (rx < o->x + o->w + 1 && rx + rw + 1 > o->x &&
                ry < o->y + o->h + 1 && ry + rh + 1 > o->y) {
                overlap = true; break;
            }
        }
        if (overlap) continue;
        d->rooms[placed].x = rx;
        d->rooms[placed].y = ry;
        d->rooms[placed].w = rw;
        d->rooms[placed].h = rh;
        d->rooms[placed].cleared = false;
        d->rooms[placed].enemies_to_spawn = 3 + floor_index + rand() % 4;
        d->rooms[placed].spawn_timer_ms = 0;
        carve_room(d, rx, ry, rw, rh);
        placed++;
    }
    d->room_count = placed;
    /* connect via corridors */
    for (int i = 1; i < placed; i++) {
        Room *a = &d->rooms[i - 1];
        Room *b = &d->rooms[i];
        int ax = a->x + a->w / 2;
        int ay = a->y + a->h / 2;
        int bx = b->x + b->w / 2;
        int by = b->y + b->h / 2;
        carve_corridor(d, ax, ay, bx, by);
    }
    /* spawn point in first room, exit in last */
    if (placed >= 1) {
        d->spawn_x = d->rooms[0].x + d->rooms[0].w / 2;
        d->spawn_y = d->rooms[0].y + d->rooms[0].h / 2;
        d->rooms[0].cleared = true; /* no enemies in spawn */
        d->rooms[0].enemies_to_spawn = 0;
    }
    if (placed >= 1) {
        Room *r = &d->rooms[placed - 1];
        d->exit_x = r->x + r->w / 2;
        d->exit_y = r->y + r->h / 2;
        d->tiles[d->exit_y][d->exit_x] = T_EXIT;
    }
    /* sprinkle hazard tiles based on floor */
    for (int i = 0; i < floor_index * 4; i++) {
        int x = rand_range(2, MAP_W - 2);
        int y = rand_range(2, MAP_H - 2);
        if (d->tiles[y][x] == T_FLOOR) {
            int r = rand() % 3;
            if (r == 0) d->tiles[y][x] = T_BLOOD;
        }
    }
}

/* ---------- ENTITY POOLS ---------- */
int enemy_spawn(Game *g, int kind, float x, float y) {
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (!g->enemies[i].alive) {
            Enemy *e = &g->enemies[i];
            memset(e, 0, sizeof(*e));
            e->alive = true;
            e->x = x; e->y = y;
            e->kind = kind;
            e->r = 6.f;
            e->xp_drop = 1;
            switch (kind) {
                case 0: /* grunt - melee chaser */
                    e->hp = e->maxhp = 14.f + g->floor_index * 3.f;
                    e->r = 6.f;
                    e->xp_drop = 1;
                    break;
                case 1: /* shooter */
                    e->hp = e->maxhp = 10.f + g->floor_index * 2.f;
                    e->r = 6.f;
                    e->xp_drop = 2;
                    break;
                case 2: /* bullet hell brute */
                    e->hp = e->maxhp = 30.f + g->floor_index * 5.f;
                    e->r = 9.f;
                    e->xp_drop = 3;
                    break;
                case 3: /* fast skitter */
                    e->hp = e->maxhp = 8.f + g->floor_index * 2.f;
                    e->r = 5.f;
                    e->xp_drop = 1;
                    break;
                case 4: /* mini-boss */
                    e->hp = e->maxhp = 120.f + g->floor_index * 20.f;
                    e->r = 12.f;
                    e->xp_drop = 8;
                    break;
                default: break;
            }
            return i;
        }
    }
    return -1;
}

int projectile_spawn(Game *g, Projectile p) {
    for (int i = 0; i < MAX_PROJECTILES; i++) {
        if (!g->projectiles[i].alive) {
            g->projectiles[i] = p;
            g->projectiles[i].alive = true;
            return i;
        }
    }
    return -1;
}

int particle_spawn(Game *g, float x, float y, float vx, float vy, float life, uint32_t color, float size) {
    for (int i = 0; i < MAX_PARTICLES; i++) {
        if (!g->particles[i].alive) {
            Particle *p = &g->particles[i];
            p->alive = true;
            p->x = x; p->y = y; p->vx = vx; p->vy = vy;
            p->life = life; p->color = color; p->size = size;
            return i;
        }
    }
    return -1;
}

int pickup_spawn(Game *g, PickupKind k, int v, float x, float y) {
    for (int i = 0; i < MAX_PICKUPS; i++) {
        if (!g->pickups[i].alive) {
            Pickup *p = &g->pickups[i];
            p->alive = true;
            p->kind = k; p->value = v;
            p->x = x; p->y = y;
            p->hover_t = (float)(rand() % 100) / 50.f;
            return i;
        }
    }
    return -1;
}

int fairy_spawn(Game *g, float x, float y, Element el) {
    for (int i = 0; i < MAX_FAIRIES; i++) {
        if (!g->fairies[i].alive) {
            Fairy *f = &g->fairies[i];
            memset(f, 0, sizeof(*f));
            f->alive = true;
            f->x = x; f->y = y;
            f->life = 12.f;
            f->element = el;
            return i;
        }
    }
    return -1;
}

/* ---------- COLLISION ---------- */
static bool aabb_solid(Game *g, float x, float y, float r) {
    /* sample 4 corners */
    int xs[2] = { (int)((x - r) / TILE), (int)((x + r) / TILE) };
    int ys[2] = { (int)((y - r) / TILE), (int)((y + r) / TILE) };
    for (int i = 0; i < 2; i++) {
        for (int j = 0; j < 2; j++) {
            int tx = xs[i], ty = ys[j];
            if (tx < 0 || ty < 0 || tx >= MAP_W || ty >= MAP_H) return true;
            if (tile_solid(g->dungeon.tiles[ty][tx])) return true;
        }
    }
    return false;
}

/* ---------- PLAYER ---------- */
void update_player(Game *g) {
    Player *p = &g->player;
    float dt = g->dt;

    float ix = 0, iy = 0;
    if (g->keys[SDL_SCANCODE_W] || g->keys[SDL_SCANCODE_UP])    iy -= 1.f;
    if (g->keys[SDL_SCANCODE_S] || g->keys[SDL_SCANCODE_DOWN])  iy += 1.f;
    if (g->keys[SDL_SCANCODE_A] || g->keys[SDL_SCANCODE_LEFT])  ix -= 1.f;
    if (g->keys[SDL_SCANCODE_D] || g->keys[SDL_SCANCODE_RIGHT]) ix += 1.f;
    float len = sqrtf(ix * ix + iy * iy);
    if (len > 0.001f) { ix /= len; iy /= len; }

    /* facing direction */
    if      (ix > 0.5f)  p->facing_dir = 0;
    else if (iy > 0.5f)  p->facing_dir = 1;
    else if (ix < -0.5f) p->facing_dir = 2;
    else if (iy < -0.5f) p->facing_dir = 3;

    /* aim toward mouse (in world coords) */
    p->aim_x = g->mouse_x + g->camera_x;
    p->aim_y = g->mouse_y + g->camera_y;

    /* dash */
    if (p->dash_cd > 0.f) p->dash_cd -= dt;
    if (p->dash_t > 0.f)  p->dash_t  -= dt;
    bool dashing = p->dash_t > 0.f;
    if (g->keys[SDL_SCANCODE_SPACE] && !g->keys_prev[SDL_SCANCODE_SPACE] &&
        p->dash_cd <= 0.f && len > 0.01f) {
        p->dash_cd = 0.8f;
        p->dash_t = 0.18f;
    }

    float speed = p->speed * (dashing ? 3.5f : 1.f);
    float dx = ix * speed * dt;
    float dy = iy * speed * dt;

    /* move with collision */
    if (!aabb_solid(g, p->x + dx, p->y, p->r - 1)) p->x += dx;
    if (!aabb_solid(g, p->x, p->y + dy, p->r - 1)) p->y += dy;

    /* weapon switch (Q/E for prev/next active) */
    if (g->keys[SDL_SCANCODE_Q] && !g->keys_prev[SDL_SCANCODE_Q]) {
        for (int k = 0; k < WEAPON_SLOTS; k++) {
            int idx = (p->active_weapon - 1 - k + WEAPON_SLOTS) % WEAPON_SLOTS;
            if (p->weapons[idx].owned) { p->active_weapon = idx; break; }
        }
    }
    if (g->keys[SDL_SCANCODE_TAB] && !g->keys_prev[SDL_SCANCODE_TAB]) {
        for (int k = 0; k < WEAPON_SLOTS; k++) {
            int idx = (p->active_weapon + 1 + k) % WEAPON_SLOTS;
            if (p->weapons[idx].owned) { p->active_weapon = idx; break; }
        }
    }
    /* number keys 1..5 to set active */
    for (int i = 0; i < 5; i++) {
        if (g->keys[SDL_SCANCODE_1 + i] && !g->keys_prev[SDL_SCANCODE_1 + i]) {
            if (p->weapons[i].owned) p->active_weapon = i;
        }
    }

    if (p->invuln_t > 0.f) p->invuln_t -= dt;

    /* pickup pull */
    for (int i = 0; i < MAX_PICKUPS; i++) {
        Pickup *pk = &g->pickups[i];
        if (!pk->alive) continue;
        float ddx = p->x - pk->x, ddy = p->y - pk->y;
        float d2 = ddx * ddx + ddy * ddy;
        if (d2 < 60.f * 60.f) {
            float d = sqrtf(d2) + 0.01f;
            pk->x += ddx / d * 120.f * dt;
            pk->y += ddy / d * 120.f * dt;
        }
        if (d2 < 12.f * 12.f) {
            /* collect */
            switch (pk->kind) {
                case PU_XP:    p->xp += 1; break;
                case PU_HEART: p->hp += 15.f; if (p->hp > p->maxhp) p->hp = p->maxhp; break;
                case PU_SOUL:  p->souls += 1; break;
                case PU_ELEMENT: {
                    Element e = (Element)pk->value;
                    Weapon *w = &p->weapons[p->active_weapon];
                    weapon_attach_element(w, e);
                    break;
                }
                case PU_WEAPON: {
                    int kind = pk->value;
                    if (!p->weapons[kind].owned) {
                        weapon_init_defaults(&p->weapons[kind], (WeaponKind)kind);
                        p->weapons[kind].owned = true;
                    } else {
                        p->weapons[kind].base_dmg *= 1.10f;
                    }
                    break;
                }
                case PU_CHEST:
                    /* chest spawns multiple goodies */
                    for (int k = 0; k < 4; k++) {
                        float ang = (rand() % 360) * 0.01745f;
                        float fx = pk->x + cosf(ang) * 16.f;
                        float fy = pk->y + sinf(ang) * 16.f;
                        int r = rand() % 100;
                        if (r < 30) pickup_spawn(g, PU_HEART, 0, fx, fy);
                        else if (r < 60) pickup_spawn(g, PU_SOUL, 0, fx, fy);
                        else {
                            int unlocked[8]; int n = 0;
                            for (int e = 1; e < EL_COUNT; e++)
                                if (g->meta.element_unlocked[e]) unlocked[n++] = e;
                            if (n > 0) pickup_spawn(g, PU_ELEMENT, unlocked[rand() % n], fx, fy);
                            else pickup_spawn(g, PU_XP, 0, fx, fy);
                        }
                    }
                    break;
            }
            pk->alive = false;
        }
    }
}

/* ---------- ENEMIES ---------- */
static void enemy_take_damage(Game *g, Enemy *e, float dmg, Element el, float kx, float ky) {
    e->hp -= dmg;
    /* status */
    if (el == EL_FIRE) {
        e->fire_dot = 2.f;
        e->fire_dps = 4.f + dmg * 0.2f;
    }
    if (el == EL_WATER) {
        e->slow_t = 1.5f;
    }
    if (el == EL_LIGHTNING) {
        e->stun_t = 0.4f;
    }
    e->knockback_x += kx;
    e->knockback_y += ky;
    /* hit particles */
    for (int i = 0; i < 6; i++) {
        float a = (rand() % 360) * 0.01745f;
        float s = 30.f + rand() % 60;
        particle_spawn(g, e->x, e->y, cosf(a) * s, sinf(a) * s, 0.4f, element_color(el), 2.f);
    }
    if (e->hp <= 0.f) {
        e->alive = false;
        g->run_kills++;
        /* drops */
        int xpcount = 1 + e->xp_drop;
        for (int i = 0; i < xpcount; i++) {
            float a = (rand() % 360) * 0.01745f;
            pickup_spawn(g, PU_XP, 0, e->x + cosf(a) * 4, e->y + sinf(a) * 4);
        }
        if ((rand() % 100) < 8) pickup_spawn(g, PU_HEART, 0, e->x, e->y);
        if ((rand() % 100) < 6) pickup_spawn(g, PU_SOUL, 0, e->x, e->y);
        if ((rand() % 100) < 5) {
            int unlocked[8]; int n = 0;
            for (int el = 1; el < EL_COUNT; el++)
                if (g->meta.element_unlocked[el]) unlocked[n++] = el;
            if (n > 0) pickup_spawn(g, PU_ELEMENT, unlocked[rand() % n], e->x, e->y);
        }
        /* death poof */
        for (int i = 0; i < 14; i++) {
            float a = (rand() % 360) * 0.01745f;
            float s = 40.f + rand() % 80;
            particle_spawn(g, e->x, e->y, cosf(a) * s, sinf(a) * s, 0.6f, 0xAA3333FF, 2.f);
        }
    }
}

/* exposed for combat.c via header? we'll keep it static and let combat.c call a helper */
void world_enemy_damage(Game *g, int idx, float dmg, Element el, float kx, float ky) {
    if (idx < 0 || idx >= MAX_ENEMIES) return;
    Enemy *e = &g->enemies[idx];
    if (!e->alive) return;
    enemy_take_damage(g, e, dmg, el, kx, ky);
}

void update_enemies(Game *g) {
    Player *p = &g->player;
    float dt = g->dt;
    for (int i = 0; i < MAX_ENEMIES; i++) {
        Enemy *e = &g->enemies[i];
        if (!e->alive) continue;

        /* status timers */
        if (e->fire_dot > 0.f) {
            e->fire_dot -= dt;
            e->hp -= e->fire_dps * dt;
            if ((rand() % 100) < 10)
                particle_spawn(g, e->x + (rand()%8)-4, e->y - 4, 0, -20, 0.4f, 0xFF8030FF, 2.f);
            if (e->hp <= 0.f) {
                enemy_take_damage(g, e, 0, EL_FIRE, 0, 0);
                continue;
            }
        }
        if (e->slow_t > 0.f) e->slow_t -= dt;
        if (e->stun_t > 0.f) { e->stun_t -= dt; continue; }

        /* knockback decay */
        e->knockback_x *= 0.85f;
        e->knockback_y *= 0.85f;
        e->x += e->knockback_x * dt;
        e->y += e->knockback_y * dt;

        float dx = p->x - e->x;
        float dy = p->y - e->y;
        float dist = sqrtf(dx * dx + dy * dy) + 0.01f;
        e->facing = atan2f(dy, dx);

        float speed = 0.f;
        switch (e->kind) {
            case 0: speed = 50.f; break;
            case 1: speed = 35.f; break;
            case 2: speed = 30.f; break;
            case 3: speed = 80.f; break;
            case 4: speed = 40.f; break;
        }
        if (e->slow_t > 0.f) speed *= 0.4f;

        /* approach but stop at firing range for shooters */
        bool approach = true;
        if ((e->kind == 1 || e->kind == 2) && dist < 90.f) approach = false;

        if (approach) {
            float vx = dx / dist * speed;
            float vy = dy / dist * speed;
            float nx = e->x + vx * dt;
            float ny = e->y + vy * dt;
            if (!aabb_solid(g, nx, e->y, e->r - 1)) e->x = nx;
            if (!aabb_solid(g, e->x, ny, e->r - 1)) e->y = ny;
        }

        /* shoot */
        e->ai_t += dt;
        if (e->kind == 1 && e->ai_t > 1.4f && dist < 200.f) {
            e->ai_t = 0;
            Projectile pr = {0};
            pr.x = e->x; pr.y = e->y;
            pr.vx = dx / dist * 90.f;
            pr.vy = dy / dist * 90.f;
            pr.life = 4.f;
            pr.r = 3.f;
            pr.dmg = 8.f + g->floor_index;
            pr.owner = 1;
            pr.reflectable = true;
            pr.primary = EL_VOID;
            projectile_spawn(g, pr);
        }
        if (e->kind == 2 && e->ai_t > 1.8f && dist < 220.f) {
            e->ai_t = 0;
            for (int k = 0; k < 8; k++) {
                float a = (k / 8.f) * 6.2831f + g->time * 0.3f;
                Projectile pr = {0};
                pr.x = e->x; pr.y = e->y;
                pr.vx = cosf(a) * 80.f;
                pr.vy = sinf(a) * 80.f;
                pr.life = 4.f;
                pr.r = 3.f;
                pr.dmg = 6.f + g->floor_index;
                pr.owner = 1;
                pr.reflectable = true;
                pr.primary = EL_FIRE;
                projectile_spawn(g, pr);
            }
        }
        if (e->kind == 4 && e->ai_t > 1.0f) {
            e->ai_t = 0;
            for (int k = 0; k < 12; k++) {
                float a = (k / 12.f) * 6.2831f + g->time;
                Projectile pr = {0};
                pr.x = e->x; pr.y = e->y;
                pr.vx = cosf(a) * 70.f;
                pr.vy = sinf(a) * 70.f;
                pr.life = 5.f;
                pr.r = 4.f;
                pr.dmg = 10.f + g->floor_index;
                pr.owner = 1;
                pr.reflectable = true;
                pr.primary = (k % 2) ? EL_FIRE : EL_VOID;
                projectile_spawn(g, pr);
            }
        }

        /* contact damage */
        float pdx = p->x - e->x;
        float pdy = p->y - e->y;
        float pd = sqrtf(pdx * pdx + pdy * pdy);
        if (pd < e->r + p->r && p->invuln_t <= 0.f && p->dash_t <= 0.f) {
            float dmg = 6.f + g->floor_index * 1.5f;
            if (e->kind == 4) dmg = 14.f + g->floor_index * 2.f;
            p->hp -= dmg;
            p->invuln_t = 0.6f;
            g->shake_t = 0.25f; g->shake_mag = 4.f;
            /* knockback player */
            float dxn = pdx / (pd + 0.01f);
            float dyn = pdy / (pd + 0.01f);
            p->x += dxn * 6.f;
            p->y += dyn * 6.f;
        }
    }
}

/* ---------- PARTICLES ---------- */
void update_particles(Game *g) {
    float dt = g->dt;
    for (int i = 0; i < MAX_PARTICLES; i++) {
        Particle *p = &g->particles[i];
        if (!p->alive) continue;
        p->x += p->vx * dt;
        p->y += p->vy * dt;
        p->vx *= 0.92f;
        p->vy *= 0.92f;
        p->life -= dt;
        if (p->life <= 0.f) p->alive = false;
    }
}

/* ---------- PICKUPS ---------- */
void update_pickups(Game *g) {
    float dt = g->dt;
    for (int i = 0; i < MAX_PICKUPS; i++) {
        Pickup *p = &g->pickups[i];
        if (!p->alive) continue;
        p->hover_t += dt * 4.f;
    }
}

/* ---------- ROOM LOGIC ---------- */
static bool point_in_room(Room *r, float x, float y) {
    int tx = (int)(x / TILE);
    int ty = (int)(y / TILE);
    return tx >= r->x && tx < r->x + r->w && ty >= r->y && ty < r->y + r->h;
}

void update_room_logic(Game *g) {
    Player *p = &g->player;
    /* find current room */
    for (int i = 0; i < g->dungeon.room_count; i++) {
        Room *r = &g->dungeon.rooms[i];
        if (point_in_room(r, p->x, p->y)) {
            if (r->cleared) continue;
            /* spawn enemies progressively until budget exhausted */
            if (r->enemies_to_spawn > 0) {
                r->spawn_timer_ms += (int)(g->dt * 1000.f);
                int spawn_interval = 600 - g->floor_index * 30;
                if (spawn_interval < 120) spawn_interval = 120;
                while (r->spawn_timer_ms >= spawn_interval && r->enemies_to_spawn > 0) {
                    r->spawn_timer_ms -= spawn_interval;
                    int kind;
                    int roll = rand() % 100;
                    if (g->floor_index >= 3 && r->enemies_to_spawn == 1 && (rand() % 100) < 18) kind = 4;
                    else if (roll < 50) kind = 0;
                    else if (roll < 75) kind = 3;
                    else if (roll < 92) kind = 1;
                    else kind = 2;
                    /* spawn near room edge */
                    int sx = r->x + 1 + rand() % (r->w - 2);
                    int sy = r->y + 1 + rand() % (r->h - 2);
                    enemy_spawn(g, kind, sx * TILE + TILE / 2, sy * TILE + TILE / 2);
                    r->enemies_to_spawn--;
                }
            } else {
                /* check if all enemies in room are dead */
                bool any = false;
                for (int e = 0; e < MAX_ENEMIES; e++) {
                    if (!g->enemies[e].alive) continue;
                    if (point_in_room(r, g->enemies[e].x, g->enemies[e].y)) { any = true; break; }
                }
                if (!any) {
                    r->cleared = true;
                    /* reward: chest in room */
                    if ((rand() % 100) < 70) {
                        pickup_spawn(g, PU_CHEST, 0,
                                     (r->x + r->w / 2) * TILE,
                                     (r->y + r->h / 2) * TILE);
                    }
                    /* if last room, ensure exit visible already (it is) */
                }
            }
            return;
        }
    }
}
