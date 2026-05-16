/*
 * world.c - "tick" generique du gameplay : particles, pickups (hover),
 * room logic. Le reste de world a ete decoupe :
 *   dungeon.c  - generation procedurale
 *   entities.c - pool spawners + collision tile
 *   player.c   - update_player + pickup collection + take_damage
 *   enemies.c  - update_enemies + drops + boss + world_enemy_damage
 */
#include "game.h"
#include <stdlib.h>

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
        r->visited = true;     /* la salle apparait sur la minimap */

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
                /* Loot contextuel "petit objet flottant" (Isaac).
                 * 35% : un PU_FOOD pose entre coffre et entree.
                 * 12% supp : un parchemin de lore (rare).
                 * Position : un coin du milieu de la piece (pas pile sur
                 * le coffre, pour qu'on les distingue). */
                if ((rand() % 100) < 35) {
                    int t = rand() % 3;
                    int heal = (t == 0) ? 8 : (t == 1) ? 16 : 12;
                    int ox = r->x + 1 + rand() % (r->w - 2);
                    int oy = r->y + 1 + rand() % (r->h - 2);
                    pickup_spawn(g, PU_FOOD, heal,
                                 ox * TILE + TILE/2, oy * TILE + TILE/2);
                }
                if ((rand() % 100) < 12) {
                    int ox = r->x + 1 + rand() % (r->w - 2);
                    int oy = r->y + 1 + rand() % (r->h - 2);
                    pickup_spawn(g, PU_SCROLL, 0,
                                 ox * TILE + TILE/2, oy * TILE + TILE/2);
                }
            }
        }
        return;
    }
}
