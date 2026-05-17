/*
 * entities.c - pool spawners (enemy / projectile / particle / pickup /
 * fairy / dmgnum) + collision tile aabb_solid.
 *
 * Extrait de world.c. Concentre les helpers techniques utilises par
 * player.c et enemies.c via world_internal.h.
 */
#include "game.h"
#include "world_internal.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- COLLISION ---------- */
bool aabb_solid(Game *g, float x, float y, float r) {
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
                case EK_RAT:
                    e->hp = e->maxhp = 8.f * diff;
                    e->r = 4.f; e->xp_drop = 1; e->coin_drop = 0;
                    break;
                case EK_GHOST:
                    e->hp = e->maxhp = 25.f * diff;
                    e->r = 6.f; e->xp_drop = 3; e->coin_drop = 2;
                    break;
                case EK_CHARGER:
                    e->hp = e->maxhp = 50.f * diff;
                    e->r = 9.f; e->xp_drop = 4; e->coin_drop = 3;
                    /* ai_t2 sert au state-machine charge : 0=cooldown,
                     * 1=telegraph, 2=charging. Voir enemies.c. */
                    e->ai_t2 = 0.f;
                    break;
                case EK_MAGE:
                    e->hp = e->maxhp = 22.f * diff;
                    e->r = 6.f; e->xp_drop = 3; e->coin_drop = 3;
                    break;
                case EK_BOSS:
                    e->hp = e->maxhp = 220.f * diff;
                    e->r = 14.f; e->xp_drop = 12; e->coin_drop = 30;
                    e->is_boss = true;
                    e->variant = (g->floor_index - 1) % 5;
                    e->telegraph_t = 1.5f;
                    e->element = biome_element(biome_for_floor(g->floor_index));
                    break;
                default: break;
            }
            /* affinite elementaire de base */
            switch (kind) {
                case EK_ZOMBIE:  e->element = EL_DARK;     break;
                case EK_BANDIT:  e->element = EL_NONE;     break;
                case EK_DEMON:   e->element = EL_FIRE;     break;
                case EK_SLIME:   e->element = EL_WATER;    break;
                case EK_RAT:     e->element = EL_NONE;     break;
                case EK_GHOST:   e->element = EL_DARK;     break;
                case EK_CHARGER: e->element = EL_EARTH;    break;
                case EK_MAGE:    e->element = EL_FAE;      break;
                default: break;
            }
            /* elite roll : 5% par etage atteint, plafond 50%, sauf boss.
             * Bias : 25% de chance de prendre l'element du BIOME courant
             * au lieu d un element random -- ca renforce le theme. */
            if (kind != EK_BOSS) {
                float chance = 0.05f * (float)g->floor_index;
                if (chance > 0.50f) chance = 0.50f;
                if ((rand() / (float)RAND_MAX) < chance) {
                    e->is_elite = true;
                    if ((rand() % 100) < 25) {
                        Element bel = biome_element(biome_for_floor(g->floor_index));
                        e->element = (bel != EL_NONE) ? bel
                            : (Element)(EL_FIRE + (rand() % (EL_COUNT - 1)));
                    } else {
                        e->element = (Element)(EL_FIRE + (rand() % (EL_COUNT - 1)));
                    }
                    /* HP multiplicateur croit doucement avec l etage :
                     * x2.0 au floor 1, x2.5 au floor 10. Donne un peu plus
                     * de mordant aux elites en fin de run. */
                    float elite_mul = 2.0f + (float)g->floor_index * 0.05f;
                    e->maxhp *= elite_mul;
                    e->hp = e->maxhp;
                    e->r += 1.5f;
                    e->coin_drop *= 3;
                    e->xp_drop *= 2;
                }
            }
            /* combo signature de l'ennemi (cf combat.c TRIPLE_LOOPS) */
            e->combo_mask = 0;
            if (e->is_boss) {
                static const int BOSS_MASKS[5] = {
                    (1<<EL_FIRE)|(1<<EL_WATER)|(1<<EL_LIGHTNING), /* Tempete */
                    (1<<EL_FIRE)|(1<<EL_EARTH)|(1<<EL_AIR),       /* Volcan  */
                    (1<<EL_VOID)|(1<<EL_FAE)|(1<<EL_LIGHTNING),   /* Dechirure */
                    (1<<EL_WATER)|(1<<EL_EARTH)|(1<<EL_LIGHTNING),/* Tsunami */
                    (1<<EL_FIRE)|(1<<EL_AIR)|(1<<EL_FAE),         /* Phenix  */
                };
                e->combo_mask = BOSS_MASKS[e->variant % 5];
                if (e->element == EL_NONE)
                    e->element = biome_element(biome_for_floor(g->floor_index));
            } else if (e->is_elite) {
                int second = 1 + rand() % (EL_COUNT - 1);
                while (second == (int)e->element)
                    second = 1 + rand() % (EL_COUNT - 1);
                e->combo_mask = (1 << e->element) | (1 << second);
            } else {
                if (e->element != EL_NONE) e->combo_mask = (1 << e->element);
            }
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

int particle_spawn_kind(Game *g, float x, float y, float vx, float vy,
                        float life, uint32_t color, float size, int kind) {
    /* 1) on cherche un slot libre. 2) fallback : si pool plein, on remplace
     * la particule la plus proche de mourir (life minimum). */
    int free_slot = -1;
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

int particle_spawn(Game *g, float x, float y, float vx, float vy,
                   float life, uint32_t color, float size) {
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
    int free_slot = -1, oldest = 0;
    float oldest_life = g->dmgnums[0].life;
    for (int i = 0; i < MAX_DMGNUM; i++) {
        if (!g->dmgnums[i].alive) { free_slot = i; break; }
        if (g->dmgnums[i].life < oldest_life) {
            oldest = i; oldest_life = g->dmgnums[i].life;
        }
    }
    int idx = (free_slot >= 0) ? free_slot : oldest;
    DamageNumber *d = &g->dmgnums[idx];
    d->alive = true;
    d->x = x + (rand() % 10) - 5;
    d->y = y - 8;
    d->vy = big ? -68.f : -52.f;
    d->life = d->life_max = big ? 1.10f : 0.85f;
    d->color = color;
    d->big = big;
    snprintf(d->text, sizeof(d->text), "%d", amount);
    return idx;
}
