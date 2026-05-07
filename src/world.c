/*
 * world.c - donjon, joueur, ennemis, pickups, boss, room logic
 */
#include "game.h"
#include "gfx.h"
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
    int prev_gen = d->gen_id;
    memset(d, 0, sizeof(*d));
    d->gen_id = prev_gen + 1;          /* invalide la cache mesh render */
    d->level_index = floor_index;
    for (int y = 0; y < MAP_H; y++)
        for (int x = 0; x < MAP_W; x++)
            d->tiles[y][x] = T_WALL;

    int target_rooms = 6 + floor_index / 2;
    if (target_rooms > 12) target_rooms = 12;
    int placed = 0, attempts = 0;
    while (placed < target_rooms && attempts < 250) {
        attempts++;
        int rw = rand_range(7, 13);
        int rh = rand_range(7, 11);
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
        d->rooms[placed].enemies_to_spawn = 3 + floor_index + rand() % 3;
        d->rooms[placed].spawn_timer_ms = 0;
        d->rooms[placed].is_boss_room = false;
        d->rooms[placed].boss_spawned = false;
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
    if (placed >= 1) {
        d->spawn_x = d->rooms[0].x + d->rooms[0].w / 2;
        d->spawn_y = d->rooms[0].y + d->rooms[0].h / 2;
        d->rooms[0].cleared = true;
        d->rooms[0].enemies_to_spawn = 0;
    }
    /* boss room = last */
    d->boss_room_idx = placed - 1;
    if (placed >= 2) {
        Room *r = &d->rooms[placed - 1];
        r->is_boss_room = true;
        r->enemies_to_spawn = 0;   /* no normal enemies; boss handles it */
        r->cleared = false;
        d->exit_x = r->x + r->w / 2;
        d->exit_y = r->y + r->h / 2;
        /* runes around boss spawn */
        int cx = r->x + r->w / 2, cy = r->y + r->h / 2;
        for (int yy = -2; yy <= 2; yy++) for (int xx = -2; xx <= 2; xx++) {
            if (abs(xx) + abs(yy) == 3 && cx+xx>0 && cy+yy>0 && cx+xx<MAP_W-1 && cy+yy<MAP_H-1) {
                d->tiles[cy + yy][cx + xx] = T_RUNE;
            }
        }
    }
    /* decorate other rooms */
    for (int ri = 0; ri < placed; ri++) {
        Room *r = &d->rooms[ri];
        /* torches at 4 corners */
        int xs[2] = { r->x + 1, r->x + r->w - 2 };
        int ys[2] = { r->y + 1, r->y + r->h - 2 };
        for (int a = 0; a < 2; a++) for (int b = 0; b < 2; b++) {
            int tx = xs[a], ty = ys[b];
            if (d->tiles[ty][tx] == T_FLOOR) d->tiles[ty][tx] = T_TORCH;
        }
        if (!r->is_boss_room && ri > 0) {
            for (int k = 0; k < 2 + rand() % 3; k++) {
                int tx = r->x + 1 + rand() % (r->w - 2);
                int ty = r->y + 1 + rand() % (r->h - 2);
                if (d->tiles[ty][tx] == T_FLOOR) d->tiles[ty][tx] = (rand() % 2) ? T_BLOOD : T_BONES;
            }
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
            e->xp_drop = 1; e->coin_drop = 1;
            float diff = powf(1.15f, (float)(g->floor_index - 1));
            switch (kind) {
                case EK_ZOMBIE:
                    e->hp = e->maxhp = 18.f * diff;
                    e->r = 6.f; e->xp_drop = 1; e->coin_drop = 1;
                    break;
                case EK_BANDIT:
                    e->hp = e->maxhp = 14.f * diff;
                    e->r = 6.f; e->xp_drop = 2; e->coin_drop = 2;
                    break;
                case EK_DEMON:
                    e->hp = e->maxhp = 38.f * diff;
                    e->r = 9.f; e->xp_drop = 4; e->coin_drop = 4;
                    break;
                case EK_SLIME:
                    e->hp = e->maxhp = 12.f * diff;
                    e->r = 5.f; e->xp_drop = 1; e->coin_drop = 1;
                    e->split_left = 1;
                    break;
                case EK_BOSS:
                    e->hp = e->maxhp = 220.f * diff;
                    e->r = 14.f; e->xp_drop = 12; e->coin_drop = 30;
                    e->is_boss = true;
                    e->variant = (g->floor_index - 1) % 5;
                    e->telegraph_t = 1.5f;
                    /* boss = element du palier */
                    e->element = (Element)(EL_FIRE + ((g->floor_index - 1) % 7));
                    break;
                default: break;
            }
            /* elite roll : 5% par etage atteint, plafond 50%, sauf boss */
            if (kind != EK_BOSS) {
                float chance = 0.05f * (float)g->floor_index;
                if (chance > 0.50f) chance = 0.50f;
                if ((rand() / (float)RAND_MAX) < chance) {
                    e->is_elite = true;
                    e->element = (Element)(EL_FIRE + (rand() % (EL_COUNT - 1)));
                    e->maxhp *= 2.0f;
                    e->hp = e->maxhp;
                    e->r += 1.5f;
                    e->coin_drop *= 3;
                    e->xp_drop *= 2;
                }
            }
            /* nom procedural */
            enemy_generate_name(e, g->floor_index);
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

int particle_spawn_kind(Game *g, float x, float y, float vx, float vy, float life, uint32_t color, float size, int kind) {
    /* 1) on cherche un slot libre */
    int free_slot = -1;
    /* 2) fallback : si pool plein, on remplace la particule LA PLUS PROCHE
     *    de mourir (life restante minimum). Comportement defini > drop
     *    silencieux. Cf. critique : "particules sans plafond global". */
    int oldest = 0;
    float oldest_life = g->particles[0].life;
    for (int i = 0; i < MAX_PARTICLES; i++) {
        Particle *q = &g->particles[i];
        if (!q->alive) { free_slot = i; break; }
        if (q->life < oldest_life) { oldest = i; oldest_life = q->life; }
    }
    int idx = (free_slot >= 0) ? free_slot : oldest;
    Particle *p = &g->particles[idx];
    p->alive = true;
    p->x = x; p->y = y; p->vx = vx; p->vy = vy;
    p->life = life; p->life_max = life;
    p->color = color; p->size = size;
    p->kind = kind;
    return idx;
}

int particle_spawn(Game *g, float x, float y, float vx, float vy, float life, uint32_t color, float size) {
    return particle_spawn_kind(g, x, y, vx, vy, life, color, size, 0);
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

int pickup_spawn_item(Game *g, Item it, float x, float y) {
    for (int i = 0; i < MAX_PICKUPS; i++) {
        if (!g->pickups[i].alive) {
            Pickup *p = &g->pickups[i];
            p->alive = true;
            p->kind = PU_ITEM;
            p->value = 0;
            p->item = it;
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
            f->life = 14.f;
            f->element = el;
            f->target = -1;
            return i;
        }
    }
    return -1;
}

int dmgnum_spawn(Game *g, float x, float y, int amount, uint32_t color, bool big) {
    for (int i = 0; i < MAX_DMGNUM; i++) {
        if (!g->dmgnums[i].alive) {
            DamageNumber *d = &g->dmgnums[i];
            d->alive = true;
            d->x = x + (rand() % 8) - 4;
            d->y = y - 6;
            d->vy = -28.f;
            d->life = d->life_max = 0.7f;
            d->color = color;
            d->big = big;
            snprintf(d->text, sizeof(d->text), "%d", amount);
            return i;
        }
    }
    return -1;
}

/* ---------- COLLISION ---------- */
static bool aabb_solid(Game *g, float x, float y, float r) {
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
void player_take_damage(Game *g, float dmg) {
    Player *p = &g->player;
    if (p->invuln_t > 0.f || p->dash_t > 0.f) return;
    /* dodge */
    if (p->dodge > 0.f && (rand() / (float)RAND_MAX) < p->dodge) {
        dmgnum_spawn(g, p->x, p->y, 0, 0xC0FFFFFF, false);
        /* mini effet d'esquive */
        for (int i = 0; i < 8; i++) {
            float a = (rand() % 360) * 0.01745f;
            particle_spawn_kind(g, p->x, p->y, cosf(a) * 80, sinf(a) * 80,
                                0.3f, 0xFFFFFFFF, 1.5f, 0);
        }
        return;
    }
    float real = dmg - p->armor;
    if (real < 1.f) real = 1.f;
    p->hp -= real;
    p->invuln_t = 0.6f;
    g->shake_t = 0.30f; g->shake_mag = 5.f;
    g->hitstop_t = 0.06f;
    g->flash_t = 0.20f;
    sfx_play(g, SFX_PLAYER_HURT);
    dmgnum_spawn(g, p->x, p->y, (int)real, 0xFFFF80FF, true);
    if (p->hp <= 0.f) sfx_play(g, SFX_DEATH);
}

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

    if      (ix > 0.5f)  p->facing_dir = 0;
    else if (iy > 0.5f)  p->facing_dir = 1;
    else if (ix < -0.5f) p->facing_dir = 2;
    else if (iy < -0.5f) p->facing_dir = 3;

    /* aim souris : ray-cast vers plan y=0 via la matrice camera precedente.
       Premier frame : matrice nulle -> aim reste a 0,0 ; OK */
    if (g->renderer) {
        v3 ro, rd;
        gfx_unproject(g->mouse_x, g->mouse_y, INTERNAL_W, INTERNAL_H,
                      g->renderer->view, g->renderer->proj, &ro, &rd);
        if (fabsf(rd.y) > 1e-4f) {
            float t = -ro.y / rd.y;
            if (t > 0.f && t < 200.f) {
                v3 hit = v3_add(ro, v3_scl(rd, t));
                p->aim_x = hit.x * (float)TILE;
                p->aim_y = hit.z * (float)TILE;
            }
        }
    }

    if (p->dash_cd > 0.f) p->dash_cd -= dt;
    if (p->dash_t > 0.f)  p->dash_t  -= dt;
    if (p->anim_t > 0.f)  p->anim_t  -= dt;
    bool dashing = p->dash_t > 0.f;
    {
        SDL_Scancode kdash = g->settings.keys[BIND_DASH];
        if (kdash == SDL_SCANCODE_UNKNOWN) kdash = SDL_SCANCODE_SPACE;
        if (g->keys[kdash] && !g->keys_prev[kdash] &&
            p->dash_cd <= 0.f && len > 0.01f) {
            p->dash_cd = 0.7f;
            p->dash_t = (p->hero == HERO_VOLEUR) ? 0.22f : 0.18f;
        }
    }

    float speed = p->speed * (dashing ? 3.6f : 1.f);
    /* vx/vy en pixels/s, utilise par le rendu (bobbing/swing/orientation) */
    p->vx = ix * speed;
    p->vy = iy * speed;
    float dx = p->vx * dt;
    float dy = p->vy * dt;
    if (!aabb_solid(g, p->x + dx, p->y, p->r - 1)) p->x += dx; else p->vx = 0;
    if (!aabb_solid(g, p->x, p->y + dy, p->r - 1)) p->y += dy; else p->vy = 0;

    /* weapon select : touches (bindings) + molette souris */
    {
        SDL_Scancode kswap = g->settings.keys[BIND_WEAPON_SWAP];
        SDL_Scancode k1    = g->settings.keys[BIND_WEAPON_1];
        SDL_Scancode k2    = g->settings.keys[BIND_WEAPON_2];
        if (kswap && g->keys[kswap] && !g->keys_prev[kswap])
            p->active_weapon = (p->active_weapon + 1) % WEAPON_SLOTS;
        if (k1 && g->keys[k1] && !g->keys_prev[k1]) p->active_weapon = 0;
        if (k2 && g->keys[k2] && !g->keys_prev[k2]) p->active_weapon = 1;
        if (g->mouse_wheel != 0)
            p->active_weapon = (p->active_weapon + WEAPON_SLOTS +
                                (g->mouse_wheel > 0 ? 1 : -1)) % WEAPON_SLOTS;
    }

    /* particules de pas quand on bouge : petit puff de poussiere */
    {
        static float foot_t = 0.f;
        float vsq = p->vx * p->vx + p->vy * p->vy;
        if (vsq > 100.f) {
            foot_t += dt;
            float interval = 0.18f;
            if (foot_t > interval) {
                foot_t -= interval;
                /* puff au pied, leger offset cote oppose */
                float a = (rand() % 360) * 0.01745f;
                particle_spawn_kind(g, p->x + cosf(a) * 4.f, p->y + sinf(a) * 4.f,
                                    cosf(a) * 8.f, sinf(a) * 8.f,
                                    0.30f, 0x40302048, 1.5f, 0);
            }
        } else {
            foot_t = 0.f;
        }
    }

    if (p->invuln_t > 0.f) p->invuln_t -= dt;

    /* regen */
    if (p->regen_per_sec > 0.f && p->hp < p->maxhp) {
        p->regen_acc += p->regen_per_sec * dt;
        while (p->regen_acc >= 1.f) { p->hp += 1.f; p->regen_acc -= 1.f; }
        if (p->hp > p->maxhp) p->hp = p->maxhp;
    }

    /* pickup pull / collect : aimant a loot. Rayon de pull plus genereux
     * pour XP / coins / food (ramassage en passant). Items "interactifs"
     * (parchemins, equipement, coffres) ont un rayon plus court pour eviter
     * de se les coller dessus sans le vouloir. */
    for (int i = 0; i < MAX_PICKUPS; i++) {
        Pickup *pk = &g->pickups[i];
        if (!pk->alive) continue;
        float ddx = p->x - pk->x, ddy = p->y - pk->y;
        float d2 = ddx * ddx + ddy * ddy;
        float pull, vmag;
        switch (pk->kind) {
            case PU_XP:
            case PU_COIN:
            case PU_SOUL:
            case PU_FOOD:
                pull = 130.f; vmag = 220.f; break;     /* magnet generous */
            case PU_PORTAL:
                pull = 0.f;   vmag = 0.f;  break;      /* pas de pull, on doit y aller */
            default:
                pull = 60.f;  vmag = 140.f; break;
        }
        if (pull > 0.f && d2 < pull * pull) {
            float d = sqrtf(d2) + 0.01f;
            pk->x += ddx / d * vmag * dt;
            pk->y += ddy / d * vmag * dt;
        }
        if (d2 < 12.f * 12.f) {
            switch (pk->kind) {
                case PU_XP:    p->xp += 1; sfx_play(g, SFX_PICKUP); break;
                case PU_HEART: p->hp += 18.f; if (p->hp > p->maxhp) p->hp = p->maxhp; sfx_play(g, SFX_PICKUP); break;
                case PU_SOUL:  p->souls += 1; sfx_play(g, SFX_PICKUP); break;
                case PU_COIN:  p->coins += pk->value > 0 ? pk->value : 1; sfx_play(g, SFX_COIN); break;
                case PU_ELEMENT: {
                    Element e = (Element)pk->value;
                    Weapon *w = &p->weapons[p->active_weapon];
                    weapon_attach_element(w, e);
                    sfx_play(g, SFX_LEVELUP);
                    /* DECOUVERTE permanente : element entre dans le pool */
                    if (e > 0 && e < EL_COUNT && !g->meta.element_discovered[e]) {
                        g->meta.element_discovered[e] = true;
                        save_write(&g->meta);
                    }
                    int mask = weapon_combo_id(w);
                    bool found = false;
                    for (int s = 0; s < g->meta.combo_seen_count; s++)
                        if (g->meta.combo_seen[s] == mask) { found = true; break; }
                    if (!found && g->meta.combo_seen_count < 64) {
                        g->meta.combo_seen[g->meta.combo_seen_count++] = mask;
                    }
                    break;
                }
                case PU_WEAPON: {
                    int kind = pk->value;
                    /* place into a slot: prefer a fists slot */
                    int slot = -1;
                    for (int s = 0; s < WEAPON_SLOTS; s++)
                        if (p->weapons[s].kind == W_FISTS) { slot = s; break; }
                    if (slot < 0) slot = p->active_weapon;
                    weapon_init_defaults(&p->weapons[slot], (WeaponKind)kind);
                    p->weapons[slot].owned = true;
                    p->active_weapon = slot;
                    sfx_play(g, SFX_LEVELUP);
                    /* DECOUVERTE permanente */
                    if (kind > 0 && kind < W_COUNT && !g->meta.weapon_discovered[kind]) {
                        g->meta.weapon_discovered[kind] = true;
                        save_write(&g->meta);
                    }
                    break;
                }
                case PU_CHEST:
                    sfx_play(g, SFX_LEVELUP);
                    /* si le joueur a encore des poings, ajoute une arme dans le coffre */
                    {
                        bool has_fists = false;
                        for (int s = 0; s < WEAPON_SLOTS; s++)
                            if (p->weapons[s].kind == W_FISTS) { has_fists = true; break; }
                        if (has_fists) {
                            int weapons[8]; int wn = 0;
                            for (int wk = W_SWORD; wk < W_COUNT; wk++)
                                if (g->meta.weapon_discovered[wk]) weapons[wn++] = wk;
                            if (wn > 0) {
                                int wpick = weapons[rand() % wn];
                                pickup_spawn(g, PU_WEAPON, wpick, pk->x, pk->y - 6);
                            }
                        }
                    }
                    for (int k = 0; k < 5; k++) {
                        float ang = (rand() % 360) * 0.01745f;
                        float fx = pk->x + cosf(ang) * 18.f;
                        float fy = pk->y + sinf(ang) * 18.f;
                        int rr = rand() % 100;
                        if (rr < 35) pickup_spawn(g, PU_COIN, 1 + rand()%3, fx, fy);
                        else if (rr < 55) {
                            int t = rand() % 3;
                            int heal = (t == 0) ? 8 : (t == 1) ? 16 : 12;
                            pickup_spawn(g, PU_FOOD, heal, fx, fy);
                        }
                        else if (rr < 70) pickup_spawn(g, PU_SOUL, 0, fx, fy);
                        else if (rr < 73) pickup_spawn(g, PU_SCROLL, 0, fx, fy);
                        else {
                            int unlocked[8]; int n = 0;
                            for (int e = 1; e < EL_COUNT; e++)
                                if (g->meta.element_discovered[e]) unlocked[n++] = e;
                            if (n > 0) pickup_spawn(g, PU_ELEMENT, unlocked[rand() % n], fx, fy);
                            else pickup_spawn(g, PU_COIN, 2, fx, fy);
                        }
                    }
                    /* burst FX */
                    for (int k = 0; k < 30; k++) {
                        float ang = (rand() % 360) * 0.01745f;
                        particle_spawn_kind(g, pk->x, pk->y, cosf(ang) * 80, sinf(ang) * 80,
                                            0.5f, 0xFFD060FF, 2.f, 2);
                    }
                    break;
                case PU_PORTAL:
                    sfx_play(g, SFX_PORTAL);
                    pk->alive = false;
                    game_open_shop(g);
                    return;
                case PU_ITEM:
                    inventory_pickup(g, pk->item);
                    break;
                case PU_FOOD: {
                    /* nourriture variee : value = quantite de PV (defaut 12) */
                    int heal = pk->value > 0 ? pk->value : 12;
                    p->hp += (float)heal;
                    if (p->hp > p->maxhp) p->hp = p->maxhp;
                    sfx_play(g, SFX_PICKUP);
                    break;
                }
                case PU_SCROLL: {
                    /* parchemin de lore : affiche un extrait pendant 6s.
                     * Lore distillee plutot qu'un menu dedie (Darkest-D-like). */
                    static const char *SNIPPETS[] = {
                        "Sept eclats sont tombes du Cristal. Sept couleurs.",
                        "Le Donjon respire. Il sait que tu es la.",
                        "Au troisieme etage, les murs ont commence a saigner.",
                        "Le Forgeron-Roi fond les ames pour son ACIER.",
                        "La Fee hait le Vide. Le Vide la nourrit.",
                        "Le SACRE et les TENEBRES sont nes du meme silence.",
                        "Aucun heros n'est revenu de l'etage 10.",
                        "L'EAU eteint le FEU. Mais la FOUDRE bout l'EAU.",
                        "Les Gardiens portent les couleurs des elements -- frappe leur faiblesse.",
                        "Trois eclats greffes : la lame change de chant.",
                        "Une lame seule te tuera. Deux te garderont en vie.",
                        "Les ennemis nommes sont nes des reves d'autres heros.",
                    };
                    int n = (int)(sizeof(SNIPPETS) / sizeof(SNIPPETS[0]));
                    const char *s = SNIPPETS[rand() % n];
                    snprintf(g->scroll_text, sizeof(g->scroll_text), "%s", s);
                    g->scroll_t = 6.f;
                    sfx_play(g, SFX_LEVELUP);
                    break;
                }
            }
            pk->alive = false;
        }
    }
}

/* ---------- ENEMIES ---------- */
static void enemy_drop_loot(Game *g, Enemy *e) {
    int xpcount = 1 + e->xp_drop;
    for (int i = 0; i < xpcount; i++) {
        float a = (rand() % 360) * 0.01745f;
        pickup_spawn(g, PU_XP, 0, e->x + cosf(a) * 4, e->y + sinf(a) * 4);
    }
    for (int i = 0; i < e->coin_drop; i++) {
        float a = (rand() % 360) * 0.01745f;
        pickup_spawn(g, PU_COIN, 1, e->x + cosf(a) * 6, e->y + sinf(a) * 6);
    }
    /* Nourriture variee : petit poulet (8 PV), grosse cuisse (16), pain (12).
     * Chaque drop pioche au hasard un type. La chance reste la meme. */
    if ((rand() % 100) < (e->is_boss ? 100 : 7)) {
        int t = rand() % 3;
        int heal = (t == 0) ? 8 : (t == 1) ? 16 : 12;
        pickup_spawn(g, PU_FOOD, heal, e->x, e->y);
    }
    if ((rand() % 100) < (e->is_boss ? 100 : 6)) pickup_spawn(g, PU_SOUL, 0, e->x, e->y);
    /* parchemins de lore : rare. Garanti chez le boss, 1.5% chez les autres. */
    if ((rand() % 1000) < (e->is_boss ? 600 : 15))
        pickup_spawn(g, PU_SCROLL, 0, e->x, e->y);
    if ((rand() % 100) < (e->is_boss ? 60 : 5)) {
        int unlocked[16]; int n = 0;
        for (int el = 1; el < EL_COUNT; el++)
            if (g->meta.element_discovered[el]) unlocked[n++] = el;
        if (n > 0) pickup_spawn(g, PU_ELEMENT, unlocked[rand() % n], e->x, e->y);
    }
    /* equipement : elite garanti, boss garanti, normaux 4% */
    if (e->is_boss) {
        Item it = item_drop_for_floor(g, g->floor_index, false, true);
        pickup_spawn_item(g, it, e->x, e->y);
        /* boss en debloque un second */
        Item it2 = item_drop_for_floor(g, g->floor_index, false, true);
        pickup_spawn_item(g, it2, e->x + 12, e->y + 12);
        /* arme bonus si le joueur a encore des poings */
        bool has_fists = false;
        for (int s = 0; s < WEAPON_SLOTS; s++)
            if (g->player.weapons[s].kind == W_FISTS) { has_fists = true; break; }
        if (has_fists) {
            int weapons[8]; int wn = 0;
            for (int wk = W_SWORD; wk < W_COUNT; wk++)
                if (g->meta.weapon_discovered[wk]) weapons[wn++] = wk;
            if (wn > 0)
                pickup_spawn(g, PU_WEAPON, weapons[rand() % wn], e->x - 12, e->y - 12);
        }
    } else if (e->is_elite) {
        Item it = item_drop_for_floor(g, g->floor_index, true, false);
        pickup_spawn_item(g, it, e->x, e->y);
    } else if ((rand() % 100) < 4) {
        Item it = item_drop_for_floor(g, g->floor_index, false, false);
        pickup_spawn_item(g, it, e->x, e->y);
    }
}

static void enemy_take_damage(Game *g, Enemy *e, float dmg, Element el, float kx, float ky) {
    /* applique la sensibilite/resistance elementaire */
    float eff = elem_effectiveness(el, e->element);
    dmg *= eff;
    e->hp -= dmg;
    e->hit_flash = (eff >= 2.f) ? 0.18f : 0.10f;
    if (el == EL_FIRE)      { e->fire_dot = 2.f; e->fire_dps = 4.f + dmg * 0.2f; }
    if (el == EL_WATER)     { e->slow_t = 1.5f; }
    if (el == EL_LIGHTNING) { e->stun_t = 0.4f; }
    e->knockback_x += kx;
    e->knockback_y += ky;
    /* damage number */
    if (dmg > 0.f) {
        bool big = (dmg > 30.f) || e->is_boss;
        uint32_t col = big ? 0xFFD060FF : 0xFFFFFFFF;
        if (el == EL_FIRE)      col = 0xFF8040FF;
        if (el == EL_LIGHTNING) col = 0xFFEC60FF;
        if (el == EL_WATER)     col = 0x80B0FFFF;
        if (el == EL_VOID)      col = 0xC080FFFF;
        dmgnum_spawn(g, e->x, e->y - e->r, (int)(dmg + 0.5f), col, big);
    }
    /* lifesteal hook */
    if (g->player.lifesteal > 0.f && dmg > 0.f) {
        g->player.hp += dmg * g->player.lifesteal;
        if (g->player.hp > g->player.maxhp) g->player.hp = g->player.maxhp;
    }
    /* particles */
    for (int i = 0; i < 6; i++) {
        float a = (rand() % 360) * 0.01745f;
        float s = 30.f + rand() % 80;
        particle_spawn_kind(g, e->x, e->y, cosf(a) * s, sinf(a) * s, 0.4f,
                            element_color(el), 2.f, 2);
    }
    if (dmg > 25.f) sfx_play(g, SFX_HEAVY_HIT);
    else if (dmg > 0.f) sfx_play(g, SFX_HIT);
    if (e->hp <= 0.f && e->dying_t <= 0.f) {
        /* declenche l'animation de mort : le corps reste rendu sans IA
           pendant dying_max secondes, puis disparait. */
        e->dying_max = e->is_boss ? 1.20f : 0.45f;
        e->dying_t   = e->dying_max;
        e->hp = 0.f;
        g->run_kills++;
        enemy_drop_loot(g, e);
        /* split slime */
        if (e->kind == EK_SLIME && e->split_left > 0) {
            for (int s = 0; s < 2; s++) {
                int idx = enemy_spawn(g, EK_SLIME, e->x + (rand()%16) - 8, e->y + (rand()%16) - 8);
                if (idx >= 0) {
                    g->enemies[idx].split_left = 0;
                    g->enemies[idx].r = 4.f;
                    g->enemies[idx].hp = g->enemies[idx].maxhp = e->maxhp * 0.4f;
                    g->enemies[idx].coin_drop = 0;
                    g->enemies[idx].xp_drop = 0;
                }
            }
        }
        /* boss kill: portal + decouverte du heros suivant */
        if (e->is_boss) {
            g->dungeon.boss_dead = true;
            g->shake_t = 0.6f; g->shake_mag = 8.f;
            g->hitstop_t = 0.20f;
            for (int k = 0; k < 80; k++) {
                float a = (rand() % 360) * 0.01745f;
                float s = 100.f + rand() % 200;
                particle_spawn_kind(g, e->x, e->y, cosf(a) * s, sinf(a) * s, 1.0f,
                                    0xFFE060FF, 3.f, 2);
            }
            pickup_spawn(g, PU_PORTAL, 0, e->x, e->y);
            sfx_play(g, SFX_BOSS);
            g->portal_spawned = true;
            /* revele le prochain heros non-decouvert */
            for (int h = 0; h < HERO_COUNT; h++) {
                if (!g->meta.hero_discovered[h]) {
                    g->meta.hero_discovered[h] = true;
                    save_write(&g->meta);
                    break;
                }
            }
        } else {
            for (int i = 0; i < 14; i++) {
                float a = (rand() % 360) * 0.01745f;
                float s = 40.f + rand() % 80;
                particle_spawn_kind(g, e->x, e->y, cosf(a) * s, sinf(a) * s, 0.6f,
                                    0xAA3333FF, 2.f, 2);
            }
        }
    }
}

void world_enemy_damage(Game *g, int idx, float dmg, Element el, float kx, float ky) {
    if (idx < 0 || idx >= MAX_ENEMIES) return;
    Enemy *e = &g->enemies[idx];
    if (!e->alive) return;
    if (e->dying_t > 0.f) return;   /* ne re-tue pas un cadavre en train de tomber */
    enemy_take_damage(g, e, dmg, el, kx, ky);
}

static void boss_update(Game *g, Enemy *e, float dt) {
    Player *p = &g->player;
    float diff = powf(1.15f, (float)(g->floor_index - 1));
    if (e->telegraph_t > 0.f) {
        e->telegraph_t -= dt;
        return;
    }
    e->ai_t += dt;
    e->ai_t2 += dt;
    /* slow chase */
    float dx = p->x - e->x, dy = p->y - e->y;
    float d = sqrtf(dx * dx + dy * dy) + 0.01f;
    float speed = 35.f + e->variant * 6.f;
    if (e->slow_t > 0.f) speed *= 0.5f;
    if (d > 30.f) {
        float vx = dx / d * speed;
        float vy = dy / d * speed;
        float nx = e->x + vx * dt, ny = e->y + vy * dt;
        if (!aabb_solid(g, nx, e->y, e->r - 1)) e->x = nx;
        if (!aabb_solid(g, e->x, ny, e->r - 1)) e->y = ny;
    }
    /* attacks */
    float pat_cd = 1.6f - e->variant * 0.1f;
    if (e->ai_t > pat_cd) {
        e->ai_t = 0;
        switch (e->variant) {
            case 0: { /* radial 16 */
                for (int k = 0; k < 16; k++) {
                    float a = (k / 16.f) * 6.2831f + g->time;
                    Projectile pr = {0};
                    pr.x = e->x; pr.y = e->y;
                    pr.vx = cosf(a) * 90.f; pr.vy = sinf(a) * 90.f;
                    pr.life = 4.f; pr.r = 3.5f; pr.dmg = 12.f * diff; pr.owner = 1;
                    pr.reflectable = true; pr.primary = EL_FIRE;
                    projectile_spawn(g, pr);
                }
                sfx_play(g, SFX_SHOOT);
                break;
            }
            case 1: { /* spiral */
                for (int k = 0; k < 6; k++) {
                    float a = (k / 6.f) * 6.2831f + e->ai_t2 * 2.f;
                    Projectile pr = {0};
                    pr.x = e->x; pr.y = e->y;
                    pr.vx = cosf(a) * 110.f; pr.vy = sinf(a) * 110.f;
                    pr.life = 3.f; pr.r = 3.f; pr.dmg = 10.f * diff; pr.owner = 1;
                    pr.primary = EL_VOID;
                    projectile_spawn(g, pr);
                }
                sfx_play(g, SFX_SHOOT);
                break;
            }
            case 2: { /* aim cone */
                float a0 = atan2f(dy, dx);
                for (int k = -3; k <= 3; k++) {
                    float a = a0 + k * 0.18f;
                    Projectile pr = {0};
                    pr.x = e->x; pr.y = e->y;
                    pr.vx = cosf(a) * 130.f; pr.vy = sinf(a) * 130.f;
                    pr.life = 3.f; pr.r = 3.f; pr.dmg = 14.f * diff; pr.owner = 1;
                    pr.primary = EL_LIGHTNING;
                    projectile_spawn(g, pr);
                }
                sfx_play(g, SFX_SHOOT);
                break;
            }
            case 3: { /* mines */
                for (int k = 0; k < 5; k++) {
                    Projectile pr = {0};
                    pr.x = e->x + (rand()%80)-40;
                    pr.y = e->y + (rand()%80)-40;
                    pr.vx = 0; pr.vy = 0;
                    pr.life = 2.f; pr.r = 4.f; pr.dmg = 16.f * diff; pr.owner = 1;
                    pr.primary = EL_EARTH;
                    projectile_spawn(g, pr);
                }
                sfx_play(g, SFX_EXPLODE);
                break;
            }
            case 4: { /* summon zombies */
                for (int k = 0; k < 3; k++) {
                    enemy_spawn(g, EK_ZOMBIE, e->x + (rand()%60)-30, e->y + (rand()%60)-30);
                }
                sfx_play(g, SFX_BOSS);
                break;
            }
        }
    }
    /* contact dmg */
    if (d < e->r + p->r) {
        player_take_damage(g, 16.f * diff);
        float ux = dx / d, uy = dy / d;
        p->x += ux * 8.f; p->y += uy * 8.f;
    }
}

void update_enemies(Game *g) {
    Player *p = &g->player;
    float dt = g->dt;
    for (int i = 0; i < MAX_ENEMIES; i++) {
        Enemy *e = &g->enemies[i];
        if (!e->alive) continue;

        if (e->hit_flash > 0.f) e->hit_flash -= dt;

        /* etat de mort : on rend le corps mais on coupe IA + collisions */
        if (e->dying_t > 0.f) {
            e->dying_t -= dt;
            if (e->dying_t <= 0.f) e->alive = false;
            continue;
        }

        if (e->fire_dot > 0.f) {
            e->fire_dot -= dt;
            e->hp -= e->fire_dps * dt;
            if ((rand() % 100) < 12)
                particle_spawn_kind(g, e->x + (rand()%8)-4, e->y - 4, 0, -22, 0.4f, 0xFF8030FF, 2.f, 0);
            if (e->hp <= 0.f) {
                enemy_take_damage(g, e, 0, EL_FIRE, 0, 0);
                continue;
            }
        }
        if (e->slow_t > 0.f) e->slow_t -= dt;
        if (e->stun_t > 0.f) { e->stun_t -= dt; continue; }

        e->knockback_x *= 0.85f;
        e->knockback_y *= 0.85f;
        e->x += e->knockback_x * dt;
        e->y += e->knockback_y * dt;

        if (e->is_boss) { boss_update(g, e, dt); continue; }

        float dx = p->x - e->x;
        float dy = p->y - e->y;
        float dist = sqrtf(dx * dx + dy * dy) + 0.01f;
        e->facing = atan2f(dy, dx);

        float speed = 0.f;
        switch (e->kind) {
            case EK_ZOMBIE: speed = 50.f; break;
            case EK_BANDIT: speed = 35.f; break;
            case EK_DEMON:  speed = 30.f; break;
            case EK_SLIME:  speed = 95.f; break;
        }
        if (e->slow_t > 0.f) speed *= 0.4f;

        bool approach = true;
        if ((e->kind == EK_BANDIT || e->kind == EK_DEMON) && dist < 100.f) approach = false;

        if (approach) {
            float vx = dx / dist * speed;
            float vy = dy / dist * speed;
            /* slime hops */
            if (e->kind == EK_SLIME) {
                float hop = 0.6f + 0.4f * sinf(g->time * 6.f + i);
                vx *= hop; vy *= hop;
            }
            float nx = e->x + vx * dt;
            float ny = e->y + vy * dt;
            if (!aabb_solid(g, nx, e->y, e->r - 1)) e->x = nx;
            if (!aabb_solid(g, e->x, ny, e->r - 1)) e->y = ny;
        }

        e->ai_t += dt;
        float diff = powf(1.15f, (float)(g->floor_index - 1));

        if (e->kind == EK_BANDIT && e->ai_t > 1.4f && dist < 220.f) {
            e->ai_t = 0;
            Projectile pr = {0};
            pr.x = e->x; pr.y = e->y;
            pr.vx = dx / dist * 110.f;
            pr.vy = dy / dist * 110.f;
            pr.life = 4.f; pr.r = 3.f;
            pr.dmg = 8.f * diff; pr.owner = 1; pr.reflectable = true;
            pr.primary = EL_VOID;
            projectile_spawn(g, pr);
            sfx_play(g, SFX_SHOOT);
        }
        if (e->kind == EK_DEMON && e->ai_t > 1.6f && dist < 240.f) {
            e->ai_t = 0;
            for (int k = 0; k < 8; k++) {
                float a = (k / 8.f) * 6.2831f + g->time * 0.3f;
                Projectile pr = {0};
                pr.x = e->x; pr.y = e->y;
                pr.vx = cosf(a) * 90.f;
                pr.vy = sinf(a) * 90.f;
                pr.life = 4.f; pr.r = 3.f;
                pr.dmg = 7.f * diff; pr.owner = 1; pr.reflectable = true;
                pr.primary = EL_FIRE;
                projectile_spawn(g, pr);
            }
            sfx_play(g, SFX_SHOOT);
        }

        /* contact damage */
        float pdx = p->x - e->x;
        float pdy = p->y - e->y;
        float pd = sqrtf(pdx * pdx + pdy * pdy);
        if (pd < e->r + p->r && p->invuln_t <= 0.f && p->dash_t <= 0.f) {
            float dmg = 7.f * diff;
            if (e->kind == EK_DEMON) dmg = 11.f * diff;
            if (e->kind == EK_SLIME) dmg = 5.f * diff;
            player_take_damage(g, dmg);
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
    for (int i = 0; i < g->dungeon.room_count; i++) {
        Room *r = &g->dungeon.rooms[i];
        if (!point_in_room(r, p->x, p->y)) continue;

        if (r->is_boss_room) {
            if (!r->boss_spawned) {
                r->boss_spawned = true;
                int sx = r->x + r->w / 2;
                int sy = r->y + r->h / 2;
                enemy_spawn(g, EK_BOSS, sx * TILE + TILE / 2, sy * TILE + TILE / 2);
                snprintf(g->boss_name, sizeof(g->boss_name), "BOSS %d / 10", g->floor_index);
                g->boss_intro_t = 2.5f;
                sfx_play(g, SFX_BOSS);
            }
            return;
        }

        if (r->cleared) continue;
        if (r->enemies_to_spawn > 0) {
            r->spawn_timer_ms += (int)(g->dt * 1000.f);
            int spawn_interval = 600 - g->floor_index * 25;
            if (spawn_interval < 120) spawn_interval = 120;
            while (r->spawn_timer_ms >= spawn_interval && r->enemies_to_spawn > 0) {
                r->spawn_timer_ms -= spawn_interval;
                int kind;
                int roll = rand() % 100;
                if (roll < 45) kind = EK_ZOMBIE;
                else if (roll < 70) kind = EK_SLIME;
                else if (roll < 88) kind = EK_BANDIT;
                else kind = EK_DEMON;
                int sx = r->x + 1 + rand() % (r->w - 2);
                int sy = r->y + 1 + rand() % (r->h - 2);
                enemy_spawn(g, kind, sx * TILE + TILE / 2, sy * TILE + TILE / 2);
                r->enemies_to_spawn--;
            }
        } else {
            bool any = false;
            for (int e = 0; e < MAX_ENEMIES; e++) {
                if (!g->enemies[e].alive) continue;
                if (point_in_room(r, g->enemies[e].x, g->enemies[e].y)) { any = true; break; }
            }
            if (!any) {
                r->cleared = true;
                if ((rand() % 100) < 80) {
                    pickup_spawn(g, PU_CHEST, 0,
                                 (r->x + r->w / 2) * TILE,
                                 (r->y + r->h / 2) * TILE);
                }
            }
        }
        return;
    }
}
