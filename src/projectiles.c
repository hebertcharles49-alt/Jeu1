/*
 * projectiles.c - tick par frame des projectiles, fees et damage numbers.
 *
 * Les projectiles sont spawnes par les fire_* (combat.c) ou par l'IA des
 * ennemis (enemies.c). Ici, on integre leur trajectoire, on resout les
 * collisions et on declenche les effets (AOE / chain / pierce / homing).
 */
#include "combat_internal.h"
#include <math.h>
#include <stdlib.h>

void update_projectiles(Game *g) {
    float dt = g->dt;
    Player *p = &g->player;

    for (int i = 0; i < MAX_PROJECTILES; i++) {
        Projectile *pr = &g->projectiles[i];
        if (!pr->alive) continue;

        pr->life -= dt;
        /* conduction foudre : tant qu un proj EL_LIGHTNING existe, il
         * electrifie les flaques d eau qu il survole (rayon 18 px). */
        if (pr->primary == EL_LIGHTNING && pr->owner == 0) {
            surface_lightning_hit(g, pr->x, pr->y, 18.f);
        }
        /* terre + eau -> boue : projectile EL_EARTH converti l eau qu il
         * survole en boue. */
        if (pr->primary == EL_EARTH && pr->owner == 0) {
            surface_earth_hit(g, pr->x, pr->y, 14.f);
        }
        /* vide/tenebres + huile -> goudron */
        if ((pr->primary == EL_VOID || pr->primary == EL_DARK) &&
            pr->owner == 0) {
            surface_void_hit(g, pr->x, pr->y, 14.f);
        }
        if (pr->life <= 0.f) {
            if (pr->owner == 0 && pr->aoe > 0.f) {
                do_aoe_at(g, pr->x, pr->y, pr->aoe, pr->dmg * 0.7f, pr->primary, element_color(pr->primary));
                sfx_play(g, SFX_EXPLODE);
            }
            /* depot de surface a l expiration selon l element. 30% chance. */
            if (pr->owner == 0 && (rand() % 100) < 30) {
                if (pr->primary == EL_WATER)
                    surface_spawn(g, SURF_WATER, pr->x, pr->y, 14.f, 0.f);
                else if (pr->primary == EL_FIRE)
                    surface_spawn(g, SURF_FIRE, pr->x, pr->y, 12.f, 0.f);
            }
            pr->alive = false;
            continue;
        }

        if (pr->owner == 0 && pr->homing > 0.f) {
            if (pr->target_idx < 0 || !g->enemies[pr->target_idx].alive) {
                pr->target_idx = nearest_enemy(g, pr->x, pr->y, 220.f, NULL);
            }
            if (pr->target_idx >= 0) {
                Enemy *t = &g->enemies[pr->target_idx];
                float dx = t->x - pr->x, dy = t->y - pr->y;
                float d = sqrtf(dx * dx + dy * dy) + 0.001f;
                float speed = sqrtf(pr->vx * pr->vx + pr->vy * pr->vy);
                pr->vx += (dx / d) * pr->homing * 60.f * dt;
                pr->vy += (dy / d) * pr->homing * 60.f * dt;
                float ns = sqrtf(pr->vx * pr->vx + pr->vy * pr->vy) + 0.001f;
                pr->vx = pr->vx / ns * speed;
                pr->vy = pr->vy / ns * speed;
            }
        }

        pr->x += pr->vx * dt;
        pr->y += pr->vy * dt;

        int tx = (int)(pr->x / TILE), ty = (int)(pr->y / TILE);
        if (tx < 0 || ty < 0 || tx >= MAP_W || ty >= MAP_H ||
            tile_solid(g->dungeon.tiles[ty][tx])) {
            if (pr->owner == 0 && pr->aoe > 0.f) {
                do_aoe_at(g, pr->x, pr->y, pr->aoe, pr->dmg * 0.7f, pr->primary, element_color(pr->primary));
                sfx_play(g, SFX_EXPLODE);
            }
            pr->alive = false;
            continue;
        }

        if (pr->owner == 0) {
            if ((rand() % 100) < 40)
                particle_spawn_kind(g, pr->x, pr->y, 0, 0, 0.18f,
                                    element_color(pr->primary), 2.f, 0);
            for (int e = 0; e < MAX_ENEMIES; e++) {
                Enemy *en = &g->enemies[e];
                if (!en->alive) continue;
                float dx = en->x - pr->x, dy = en->y - pr->y;
                float rr = en->r + pr->r;
                if (dx * dx + dy * dy < rr * rr) {
                    world_enemy_damage(g, e, pr->dmg, pr->primary, pr->vx * 0.3f, pr->vy * 0.3f);
                    if (pr->aoe > 0.f) {
                        do_aoe_at(g, en->x, en->y, pr->aoe, pr->dmg * 0.6f,
                                  pr->primary, element_color(pr->primary));
                        sfx_play(g, SFX_EXPLODE);
                    }
                    if (pr->chains > 0) {
                        chain_hit(g, e, pr->dmg * 0.6f, pr->primary, pr->chains,
                                  element_color(pr->primary));
                    }
                    if (pr->pierce > 0) pr->pierce--;
                    else                pr->alive = false;
                    break;
                }
            }
        } else {
            float dx = p->x - pr->x, dy = p->y - pr->y;
            float rr = p->r + pr->r;
            if (dx * dx + dy * dy < rr * rr) {
                /* u_frontal_immune : si le projectile vient de la
                 * direction du regard (aim), on l ignore. dot positif =
                 * proj face au joueur. */
                bool blocked = false;
                if (p->u_frontal_immune) {
                    float ax = p->aim_x - p->x, ay = p->aim_y - p->y;
                    float al = sqrtf(ax*ax + ay*ay) + 0.001f;
                    ax /= al; ay /= al;
                    float ipx = -pr->vx, ipy = -pr->vy;
                    float il = sqrtf(ipx*ipx + ipy*ipy) + 0.001f;
                    ipx /= il; ipy /= il;
                    float dot = ax * ipx + ay * ipy;
                    if (dot > 0.5f) blocked = true;     /* cone ~60deg */
                }
                if (blocked) {
                    /* sparks bleus pour signaler le block */
                    for (int k = 0; k < 8; k++) {
                        float a = (rand() % 360) * 0.01745f;
                        particle_spawn_kind(g, p->x, p->y,
                                            cosf(a) * 70.f, sinf(a) * 70.f,
                                            0.3f, 0xC0E0FFFF, 1.6f, 0);
                    }
                } else if (p->invuln_t <= 0.f && p->dash_t <= 0.f) {
                    player_take_damage_from(g, pr->dmg, pr->x, pr->y);
                }
                pr->alive = false;
            }
        }
    }
}

void update_fairies(Game *g) {
    float dt = g->dt;
    Player *p = &g->player;
    for (int i = 0; i < MAX_FAIRIES; i++) {
        Fairy *f = &g->fairies[i];
        if (!f->alive) continue;
        f->life -= dt;
        f->cd -= dt;
        if (f->life <= 0.f) { f->alive = false; continue; }
        if (f->target < 0 || !g->enemies[f->target].alive) {
            f->target = nearest_enemy(g, f->x, f->y, 180.f, NULL);
        }
        float tx, ty;
        if (f->target >= 0) {
            tx = g->enemies[f->target].x; ty = g->enemies[f->target].y;
        } else {
            tx = p->x + cosf(g->time * 2.f + i) * 30.f;
            ty = p->y + sinf(g->time * 2.f + i) * 30.f;
        }
        float dx = tx - f->x, dy = ty - f->y;
        float d = sqrtf(dx * dx + dy * dy) + 0.01f;
        f->vx += dx / d * 220.f * dt;
        f->vy += dy / d * 220.f * dt;
        f->vx *= 0.92f; f->vy *= 0.92f;
        f->x += f->vx * dt; f->y += f->vy * dt;
        if (f->target >= 0 && d < 14.f && f->cd <= 0.f) {
            f->cd = 0.45f;
            world_enemy_damage(g, f->target, 7.f, f->element, dx * 0.2f, dy * 0.2f);
            particle_spawn_kind(g, f->x, f->y, 0, 0, 0.4f,
                                element_color(f->element), 3.f, 0);
        }
        if ((rand() % 100) < 30)
            particle_spawn_kind(g, f->x, f->y, 0, 0, 0.4f,
                                element_color(f->element), 2.f, 0);
    }
}

void update_dmgnums(Game *g) {
    float dt = g->dt;
    for (int i = 0; i < MAX_DMGNUM; i++) {
        DamageNumber *d = &g->dmgnums[i];
        if (!d->alive) continue;
        d->life -= dt;
        d->y += d->vy * dt;
        d->vy += 30.f * dt;          /* mild gravity */
        if (d->life <= 0.f) d->alive = false;
    }
}
