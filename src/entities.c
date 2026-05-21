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

/* Helper : une porte T_DOOR est-elle verrouillee (solide) ?
 * Une porte est solide ssi elle est sur le perimetre d'une salle
 * qui a locked=true. Permet d'enfermer le joueur jusqu'au clear.
 * Expose pour render_world.c (visuel barriere rouge). */
bool door_locked_at(Game *g, int tx, int ty) {
    if (g->dungeon.tiles[ty][tx] != T_DOOR) return false;
    for (int i = 0; i < g->dungeon.room_count; i++) {
        const Room *r = &g->dungeon.rooms[i];
        if (!r->locked) continue;
        /* perimetre = bordure exterieure de la salle (1 tile autour) */
        if (tx >= r->x - 1 && tx <= r->x + r->w &&
            ty >= r->y - 1 && ty <= r->y + r->h) {
            return true;
        }
    }
    return false;
}

/* ---------- COLLISION ---------- */
bool aabb_solid(Game *g, float x, float y, float r) {
    int xs[2] = { (int)((x - r) / TILE), (int)((x + r) / TILE) };
    int ys[2] = { (int)((y - r) / TILE), (int)((y + r) / TILE) };
    for (int i = 0; i < 2; i++) {
        for (int j = 0; j < 2; j++) {
            int tx = xs[i], ty = ys[j];
            if (tx < 0 || ty < 0 || tx >= MAP_W || ty >= MAP_H) return true;
            if (tile_solid(g->dungeon.tiles[ty][tx])) return true;
            if (door_locked_at(g, tx, ty)) return true;
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
            /* Buff HP global : *3.0 sur la baseline mob, *3.5 sur boss.
             * Le joueur ramene des coups a 1000+ via combos / uniques ;
             * sans ce buff, le run est trivial. */
            float diff = powf(1.15f, (float)(g->floors_visited > 0 ? g->floors_visited - 1 : 0));
            switch (kind) {
                case EK_ZOMBIE:
                    e->hp = e->maxhp = 54.f * diff;
                    e->r = 6.f; e->xp_drop = 1; e->coin_drop = 1;
                    break;
                case EK_BANDIT:
                    e->hp = e->maxhp = 42.f * diff;
                    e->r = 6.f; e->xp_drop = 2; e->coin_drop = 2;
                    break;
                case EK_DEMON:
                    e->hp = e->maxhp = 114.f * diff;
                    e->r = 9.f; e->xp_drop = 4; e->coin_drop = 4;
                    break;
                case EK_SLIME:
                    e->hp = e->maxhp = 36.f * diff;
                    e->r = 5.f; e->xp_drop = 1; e->coin_drop = 1;
                    e->split_left = 1;
                    break;
                case EK_RAT:
                    e->hp = e->maxhp = 24.f * diff;
                    e->r = 4.f; e->xp_drop = 1; e->coin_drop = 0;
                    break;
                case EK_GHOST:
                    e->hp = e->maxhp = 75.f * diff;
                    e->r = 6.f; e->xp_drop = 3; e->coin_drop = 2;
                    break;
                case EK_CHARGER:
                    e->hp = e->maxhp = 150.f * diff;
                    e->r = 9.f; e->xp_drop = 4; e->coin_drop = 3;
                    e->ai_t2 = 0.f;
                    break;
                case EK_MAGE:
                    e->hp = e->maxhp = 66.f * diff;
                    e->r = 6.f; e->xp_drop = 3; e->coin_drop = 3;
                    break;
                case EK_HEALER:
                    e->hp = e->maxhp = 54.f * diff;
                    e->r = 6.f; e->xp_drop = 3; e->coin_drop = 2;
                    break;
                case EK_BUFFER:
                    e->hp = e->maxhp = 96.f * diff;
                    e->r = 7.f; e->xp_drop = 3; e->coin_drop = 2;
                    break;
                case EK_NECROMANCER:
                    e->hp = e->maxhp = 78.f * diff;
                    e->r = 6.f; e->xp_drop = 4; e->coin_drop = 3;
                    break;
                case EK_BOSS:
                    e->hp = e->maxhp = 770.f * diff;
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
                case EK_MAGE:        e->element = EL_FAE;    break;
                case EK_HEALER:      e->element = EL_HOLY;   break;
                case EK_BUFFER:      e->element = EL_STEEL;  break;
                case EK_NECROMANCER: e->element = EL_DARK;   break;
                default: break;
            }
            /* elite roll : 5% par etage atteint, plafond 50%, sauf boss.
             * Bias : 25% de chance de prendre l'element du BIOME courant
             * au lieu d un element random -- ca renforce le theme. */
            if (kind != EK_BOSS) {
                float chance = 0.05f * (float)g->floors_visited;
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
                    float elite_mul = 2.0f + (float)g->floors_visited * 0.05f;
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
    /* Buff projectiles ennemis : +60% dmg. Cible uniquement les
     * projectiles d'owner == 1 (ennemis) pour ne pas amplifier le
     * joueur. Permet aux mobs distants d etre menaçants sans avoir
     * a editer 30 callsites. */
    if (p.owner == 1) p.dmg *= 1.6f;
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
            /* feedback epique sur drop legendaire ou unique :
             *   - hitstop 0.30s (pause de respiration)
             *   - shake court mais marque
             *   - 60 particules dorees + 24 dans la couleur rarete
             *   - SFX_BOSS pour la solennite
             *   - increment du compteur de run pour l ecran de mort
             * Le rendu pulsant + aura est gere par draw_pickup_3d. */
            if (it.rarity == R_LEGENDARY || it.is_unique) {
                g->hitstop_t = 0.30f;
                g->shake_t = 0.35f; g->shake_mag = 5.5f;
                g->run_legendary_drops++;
                uint32_t col_e = it.is_unique ? 0xFF8030FF
                                              : rarity_color(R_LEGENDARY);
                for (int k = 0; k < 60; k++) {
                    float a = (rand() % 360) * 0.01745f;
                    float s = 60.f + (rand() % 140);
                    particle_spawn_kind(g, x, y,
                                        cosf(a) * s, sinf(a) * s,
                                        0.85f, 0xFFE060FF, 3.0f, 2);
                }
                for (int k = 0; k < 24; k++) {
                    float a = (rand() % 360) * 0.01745f;
                    particle_spawn_kind(g, x, y,
                                        cosf(a) * 50.f, sinf(a) * 50.f,
                                        1.20f, col_e, 2.5f, 0);
                }
                /* pillar vertical de lumiere */
                for (int k = 0; k < 18; k++) {
                    particle_spawn_kind(g, x + (rand()%6)-3,
                                        y + (rand()%6)-3,
                                        0, -60.f,
                                        0.70f, col_e, 1.8f, 0);
                }
                sfx_play(g, SFX_BOSS);
            }
            return i;
        }
    }
    return -1;
}

/* hardcap : 30 pixies alive max (MAX_FAIRIES=32 reste comme buffer).
 * Au-dela on refuse les spawns -- evite les essaims abusifs et garantit
 * que le scaling de dmg (en update_fairies) reste borne. */
#define FAIRY_HARDCAP 30

int fairy_spawn(Game *g, float x, float y, Element el) {
    int alive_n = 0;
    int free_slot = -1;
    for (int i = 0; i < MAX_FAIRIES; i++) {
        if (g->fairies[i].alive) alive_n++;
        else if (free_slot < 0) free_slot = i;
    }
    if (alive_n >= FAIRY_HARDCAP) return -1;
    if (free_slot < 0) return -1;
    Fairy *f = &g->fairies[free_slot];
    memset(f, 0, sizeof(*f));
    f->alive = true;
    f->x = x; f->y = y;
    f->life = 14.f;
    f->element = el;
    f->target = -1;
    return free_slot;
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
