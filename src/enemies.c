/*
 * enemies.c - intelligence des ennemis : tables de drops, prise de degats,
 * pattern boss, mise a jour par frame.
 *
 * Extrait de world.c. Le spawn brut des Enemy reste cote entities.c
 * (pool_spawner), ici on a la simulation comportementale.
 */
#include "game.h"
#include "world_internal.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

/* ---------- DROP TABLES ----------
 * Uniquement les ennemis "interessants" laissent du loot ; les zombies/slimes
 * lambdas donnent juste un soupcon d'XP. Evite la diarrhea de loot. */
typedef struct {
    int xp_count;
    int coin_chance, coin_max;
    int food_chance, soul_chance;
    int scroll_per1000;
    int element_chance, item_chance;
} DropProfile;

static DropProfile drop_profile_for(Enemy *e) {
    DropProfile d = {0, 0, 0, 0, 0, 0, 0, 0};
    if (e->is_boss) {
        d.xp_count = 12; d.coin_chance = 100; d.coin_max = 8;
        d.food_chance = 100; d.soul_chance = 100;
        d.scroll_per1000 = 600; d.element_chance = 80; d.item_chance = 100;
        return d;
    }
    if (e->is_elite) {
        d.xp_count = 4; d.coin_chance = 100; d.coin_max = 4;
        d.food_chance = 60; d.soul_chance = 35;
        d.scroll_per1000 = 60; d.element_chance = 25; d.item_chance = 100;
        return d;
    }
    switch (e->kind) {
        case EK_ZOMBIE:
            d.xp_count = 1; d.coin_chance = 18; d.coin_max = 1;
            d.food_chance = 4; d.soul_chance = 2;
            d.scroll_per1000 = 8; d.element_chance = 2; d.item_chance = 1;
            break;
        case EK_SLIME:
            d.xp_count = 1; d.coin_chance = 12; d.coin_max = 1;
            d.food_chance = 2; d.soul_chance = 2;
            d.scroll_per1000 = 5; d.element_chance = 1; d.item_chance = 1;
            break;
        case EK_BANDIT:
            d.xp_count = 2; d.coin_chance = 55; d.coin_max = 2;
            d.food_chance = 8; d.soul_chance = 6;
            d.scroll_per1000 = 18; d.element_chance = 4; d.item_chance = 3;
            break;
        case EK_DEMON:
            d.xp_count = 4; d.coin_chance = 90; d.coin_max = 4;
            d.food_chance = 18; d.soul_chance = 14;
            d.scroll_per1000 = 35; d.element_chance = 9; d.item_chance = 6;
            break;
        default: break;
    }
    return d;
}

static void enemy_drop_loot(Game *g, Enemy *e) {
    DropProfile d = drop_profile_for(e);
    for (int i = 0; i < d.xp_count; i++) {
        float a = (rand() % 360) * 0.01745f;
        pickup_spawn(g, PU_XP, 0, e->x + cosf(a) * 4, e->y + sinf(a) * 4);
    }
    if (d.coin_chance > 0 && (rand() % 100) < d.coin_chance) {
        int n = 1 + (rand() % (d.coin_max > 0 ? d.coin_max : 1));
        for (int i = 0; i < n; i++) {
            float a = (rand() % 360) * 0.01745f;
            pickup_spawn(g, PU_COIN, 1, e->x + cosf(a) * 6, e->y + sinf(a) * 6);
        }
    }
    if (d.food_chance > 0 && (rand() % 100) < d.food_chance) {
        int t = rand() % 3;
        int heal = (t == 0) ? 8 : (t == 1) ? 16 : 12;
        pickup_spawn(g, PU_FOOD, heal, e->x, e->y);
    }
    if (d.soul_chance > 0 && (rand() % 100) < d.soul_chance)
        pickup_spawn(g, PU_SOUL, 0, e->x, e->y);
    if (d.scroll_per1000 > 0 && (rand() % 1000) < d.scroll_per1000)
        pickup_spawn(g, PU_SCROLL, 0, e->x, e->y);
    if (d.element_chance > 0 && (rand() % 100) < d.element_chance) {
        int unlocked[16]; int n = 0;
        for (int el = 1; el < EL_COUNT; el++)
            if (g->meta.element_discovered[el]) unlocked[n++] = el;
        if (n > 0) pickup_spawn(g, PU_ELEMENT, unlocked[rand() % n], e->x, e->y);
    }
    if (e->is_boss) {
        Item it1 = item_drop_for_floor(g, g->floor_index, false, true);
        pickup_spawn_item(g, it1, e->x, e->y);
        Item it2 = item_drop_for_floor(g, g->floor_index, false, true);
        pickup_spawn_item(g, it2, e->x + 12, e->y + 12);
        bool has_fists = false;
        for (int s = 0; s < WEAPON_SLOTS; s++)
            if (g->player.weapons[s].kind == W_FISTS) { has_fists = true; break; }
        if (has_fists) {
            int weapons[8]; int wn = 0;
            for (int wk = W_SWORD; wk < W_COUNT; wk++)
                if (g->meta.weapon_discovered[wk]) weapons[wn++] = wk;
            if (wn > 0) {
                Rarity wr = rarity_for_floor_boss(g->floor_index);
                pickup_spawn(g, PU_WEAPON,
                             weapons[rand() % wn] | (((int)wr) << 8),
                             e->x - 12, e->y - 12);
            }
        }
    } else if (d.item_chance > 0 && (rand() % 100) < d.item_chance) {
        Item it = item_drop_for_floor(g, g->floor_index, e->is_elite, false);
        pickup_spawn_item(g, it, e->x, e->y);
    }
}

static void enemy_take_damage(Game *g, Enemy *e, float dmg, Element el,
                               float kx, float ky)
{
    float eff = elem_effectiveness(el, e->element);
    dmg *= eff;
    e->hp -= dmg;
    e->hit_flash = (eff >= 2.f) ? 0.18f : 0.10f;
    if (el == EL_FIRE)      { e->fire_dot = 2.f; e->fire_dps = 4.f + dmg * 0.2f; }
    if (el == EL_WATER)     { e->slow_t = 1.5f; }
    if (el == EL_LIGHTNING) { e->stun_t = 0.4f; }
    e->knockback_x += kx;
    e->knockback_y += ky;
    if (dmg > 0.f) {
        bool crit = g->current_attack_crit;
        bool big = crit || (dmg > 30.f) || e->is_boss;
        uint32_t col = big ? 0xFFD060FF : 0xFFFFFFFF;
        if (el == EL_FIRE)      col = 0xFF8040FF;
        if (el == EL_LIGHTNING) col = 0xFFEC60FF;
        if (el == EL_WATER)     col = 0x80B0FFFF;
        if (el == EL_VOID)      col = 0xC080FFFF;
        if (el == EL_HOLY)      col = 0xFFE890FF;
        if (el == EL_DARK)      col = 0xC080A0FF;
        if (el == EL_FAE)       col = 0xF080F0FF;
        if (crit) col = 0xFFFF40FF;
        int idx = dmgnum_spawn(g, e->x, e->y - e->r, (int)(dmg + 0.5f), col, big);
        if (idx >= 0 && crit) {
            DamageNumber *d = &g->dmgnums[idx];
            char buf[16];
            snprintf(buf, sizeof(buf), "!%s", d->text);
            snprintf(d->text, sizeof(d->text), "%s", buf);
        }
    }
    {
        float new_stop = 0.020f + dmg * 0.0009f;
        if (g->current_attack_crit) new_stop += 0.04f;
        if (new_stop > 0.180f) new_stop = 0.180f;
        if (new_stop > g->hitstop_t) g->hitstop_t = new_stop;
        float new_shake = 1.5f + dmg * 0.045f;
        if (new_shake > 8.f) new_shake = 8.f;
        if (new_shake > g->shake_mag || g->shake_t < 0.05f) {
            g->shake_mag = new_shake;
            g->shake_t = 0.18f;
        }
    }
    if (g->player.lifesteal > 0.f && dmg > 0.f) {
        g->player.hp += dmg * g->player.lifesteal;
        if (g->player.hp > g->player.maxhp) g->player.hp = g->player.maxhp;
    }
    for (int i = 0; i < 6; i++) {
        float a = (rand() % 360) * 0.01745f;
        float s = 30.f + rand() % 80;
        particle_spawn_kind(g, e->x, e->y, cosf(a) * s, sinf(a) * s, 0.4f,
                            element_color(el), 2.f, 2);
    }
    if (dmg > 25.f) sfx_play(g, SFX_HEAVY_HIT);
    else if (dmg > 0.f) sfx_play(g, SFX_HIT);
    if (e->hp <= 0.f && e->dying_t <= 0.f) {
        e->dying_max = e->is_boss ? 1.20f : 0.45f;
        e->dying_t   = e->dying_max;
        e->hp = 0.f;
        g->run_kills++;
        enemy_drop_loot(g, e);
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
    if (e->dying_t > 0.f) return;
    bool already_dead = (e->hp <= 0.f);
    enemy_take_damage(g, e, dmg, el, kx, ky);
    loop_on_hit(g);
    if (e->hp <= 0.f && !already_dead) loop_on_kill(g);
}

static void boss_update(Game *g, Enemy *e, float dt) {
    Player *p = &g->player;
    float diff = powf(1.15f, (float)(g->floor_index - 1));
    if (e->telegraph_t > 0.f) { e->telegraph_t -= dt; return; }
    e->ai_t += dt;
    e->ai_t2 += dt;
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
    float pat_cd = 1.6f - e->variant * 0.1f;
    if (e->ai_t > pat_cd) {
        e->ai_t = 0;
        switch (e->variant) {
            case 0: {
                for (int k = 0; k < 16; k++) {
                    float a = (k / 16.f) * 6.2831f + g->time;
                    Projectile pr = {0};
                    pr.x = e->x; pr.y = e->y;
                    pr.vx = cosf(a) * 90.f; pr.vy = sinf(a) * 90.f;
                    pr.life = 4.f; pr.r = 3.5f; pr.dmg = 12.f * diff; pr.owner = 1;
                    pr.reflectable = true; pr.primary = EL_FIRE;
                    combo_apply_to_enemy_projectile(e->combo_mask, &pr);
                    projectile_spawn(g, pr);
                }
                sfx_play(g, SFX_SHOOT); break;
            }
            case 1: {
                for (int k = 0; k < 6; k++) {
                    float a = (k / 6.f) * 6.2831f + e->ai_t2 * 2.f;
                    Projectile pr = {0};
                    pr.x = e->x; pr.y = e->y;
                    pr.vx = cosf(a) * 110.f; pr.vy = sinf(a) * 110.f;
                    pr.life = 3.f; pr.r = 3.f; pr.dmg = 10.f * diff; pr.owner = 1;
                    pr.primary = EL_VOID;
                    combo_apply_to_enemy_projectile(e->combo_mask, &pr);
                    projectile_spawn(g, pr);
                }
                sfx_play(g, SFX_SHOOT); break;
            }
            case 2: {
                float a0 = atan2f(dy, dx);
                for (int k = -3; k <= 3; k++) {
                    float a = a0 + k * 0.18f;
                    Projectile pr = {0};
                    pr.x = e->x; pr.y = e->y;
                    pr.vx = cosf(a) * 130.f; pr.vy = sinf(a) * 130.f;
                    pr.life = 3.f; pr.r = 3.f; pr.dmg = 14.f * diff; pr.owner = 1;
                    pr.primary = EL_LIGHTNING;
                    combo_apply_to_enemy_projectile(e->combo_mask, &pr);
                    projectile_spawn(g, pr);
                }
                sfx_play(g, SFX_SHOOT); break;
            }
            case 3: {
                for (int k = 0; k < 5; k++) {
                    Projectile pr = {0};
                    pr.x = e->x + (rand()%80)-40;
                    pr.y = e->y + (rand()%80)-40;
                    pr.vx = 0; pr.vy = 0;
                    pr.life = 2.f; pr.r = 4.f; pr.dmg = 16.f * diff; pr.owner = 1;
                    pr.primary = EL_EARTH;
                    combo_apply_to_enemy_projectile(e->combo_mask, &pr);
                    projectile_spawn(g, pr);
                }
                sfx_play(g, SFX_EXPLODE); break;
            }
            case 4: {
                for (int k = 0; k < 3; k++)
                    enemy_spawn(g, EK_ZOMBIE, e->x + (rand()%60)-30, e->y + (rand()%60)-30);
                sfx_play(g, SFX_BOSS); break;
            }
        }
    }
    if (d < e->r + p->r) {
        player_take_damage(g, 16.f * diff);
        float ux = dx / d, uy = dy / d;
        p->x += ux * 8.f; p->y += uy * 8.f;
    }
}

/* ============================================================
 *  AI PAR KIND -- chaque ennemi a sa signature comportementale
 * ============================================================ */

/* helpers communs */
static void ai_move_toward(Game *g, Enemy *e, float dt,
                            float dx, float dy, float dist, float speed,
                            bool phase_walls)
{
    if (e->slow_t > 0.f) speed *= 0.4f;
    float vx = dx / dist * speed;
    float vy = dy / dist * speed;
    float nx = e->x + vx * dt;
    float ny = e->y + vy * dt;
    if (phase_walls) { e->x = nx; e->y = ny; return; }
    if (!aabb_solid(g, nx, e->y, e->r - 1)) e->x = nx;
    if (!aabb_solid(g, e->x, ny, e->r - 1)) e->y = ny;
}

static void ai_contact_damage(Game *g, Enemy *e, float dmg) {
    Player *p = &g->player;
    float pdx = p->x - e->x;
    float pdy = p->y - e->y;
    float pd = sqrtf(pdx * pdx + pdy * pdy);
    if (pd < e->r + p->r && p->invuln_t <= 0.f && p->dash_t <= 0.f) {
        player_take_damage(g, dmg);
        float dxn = pdx / (pd + 0.01f);
        float dyn = pdy / (pd + 0.01f);
        p->x += dxn * 6.f;
        p->y += dyn * 6.f;
    }
}

static void ai_zombie(Game *g, Enemy *e, float dt, float dx, float dy, float dist) {
    float diff = powf(1.15f, (float)(g->floor_index - 1));
    ai_move_toward(g, e, dt, dx, dy, dist, 50.f, false);
    ai_contact_damage(g, e, 7.f * diff);
}

static void ai_slime(Game *g, Enemy *e, int i, float dt, float dx, float dy, float dist) {
    float diff = powf(1.15f, (float)(g->floor_index - 1));
    float hop = 0.6f + 0.4f * sinf(g->time * 6.f + i);
    ai_move_toward(g, e, dt, dx, dy, dist, 95.f * hop, false);
    ai_contact_damage(g, e, 5.f * diff);
}

static void ai_bandit(Game *g, Enemy *e, float dt, float dx, float dy, float dist) {
    float diff = powf(1.15f, (float)(g->floor_index - 1));
    /* kite : ne s approche pas en deca de 100 px */
    if (dist > 100.f) ai_move_toward(g, e, dt, dx, dy, dist, 35.f, false);
    e->ai_t += dt;
    if (e->ai_t > 1.4f && dist < 220.f) {
        e->ai_t = 0;
        Projectile pr = {0};
        pr.x = e->x; pr.y = e->y;
        pr.vx = dx / dist * 110.f;
        pr.vy = dy / dist * 110.f;
        pr.life = 4.f; pr.r = 3.f;
        pr.dmg = 8.f * diff; pr.owner = 1; pr.reflectable = true;
        pr.primary = EL_VOID;
        combo_apply_to_enemy_projectile(e->combo_mask, &pr);
        projectile_spawn(g, pr);
        sfx_play(g, SFX_SHOOT);
    }
    ai_contact_damage(g, e, 7.f * diff);
}

static void ai_demon(Game *g, Enemy *e, float dt, float dx, float dy, float dist) {
    float diff = powf(1.15f, (float)(g->floor_index - 1));
    if (dist > 100.f) ai_move_toward(g, e, dt, dx, dy, dist, 30.f, false);
    e->ai_t += dt;
    if (e->ai_t > 1.6f && dist < 240.f) {
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
            combo_apply_to_enemy_projectile(e->combo_mask, &pr);
            projectile_spawn(g, pr);
        }
        sfx_play(g, SFX_SHOOT);
    }
    ai_contact_damage(g, e, 11.f * diff);
}

/* RAT : tres rapide, zigzag (composante perpendiculaire sinusoidale).
 * Faible, mais en groupe c'est dangereux. */
static void ai_rat(Game *g, Enemy *e, int i, float dt, float dx, float dy, float dist) {
    float diff = powf(1.15f, (float)(g->floor_index - 1));
    /* perpendiculaire au vecteur joueur, oscillation rapide */
    float perp_x = -dy / dist;
    float perp_y =  dx / dist;
    float wobble = sinf(g->time * 10.f + i * 1.7f) * 0.7f;
    float dirx = dx / dist + perp_x * wobble;
    float diry = dy / dist + perp_y * wobble;
    float dlen = sqrtf(dirx * dirx + diry * diry) + 0.001f;
    ai_move_toward(g, e, dt, dirx / dlen, diry / dlen, 1.f, 120.f, false);
    ai_contact_damage(g, e, 4.f * diff);
}

/* GHOST : float (visuel via bobbing render), phase a travers les murs,
 * 35% de chance de teleporter de +/-60 px quand il prend un coup. Le
 * teleport est gere ici en lisant hit_flash > 0 + un cooldown. */
static void ai_ghost(Game *g, Enemy *e, float dt, float dx, float dy, float dist) {
    float diff = powf(1.15f, (float)(g->floor_index - 1));
    /* teleport "reactif" : on tente sur entree de hit_flash (rising edge). */
    e->ai_t += dt;
    if (e->hit_flash > 0.05f && e->ai_t > 0.6f) {
        if ((rand() % 100) < 35) {
            float ang = (rand() % 360) * 0.01745f;
            float dist_tp = 50.f + (rand() % 30);
            float nx = e->x + cosf(ang) * dist_tp;
            float ny = e->y + sinf(ang) * dist_tp;
            /* respecte la map quand meme : si tile out of bounds, on annule */
            int tx = (int)(nx / TILE), ty = (int)(ny / TILE);
            if (tx > 0 && ty > 0 && tx < MAP_W - 1 && ty < MAP_H - 1) {
                /* burst spectral au depart + arrivee */
                for (int k = 0; k < 12; k++) {
                    float a = (rand() % 360) * 0.01745f;
                    particle_spawn_kind(g, e->x, e->y, cosf(a)*60, sinf(a)*60,
                                        0.35f, 0xC080FFC0, 2.f, 0);
                    particle_spawn_kind(g, nx, ny, cosf(a)*60, sinf(a)*60,
                                        0.35f, 0xC080FFC0, 2.f, 0);
                }
                e->x = nx; e->y = ny;
                e->ai_t = 0.f;
            }
        }
    }
    /* mouvement : phase a travers les murs, lent */
    ai_move_toward(g, e, dt, dx, dy, dist, 45.f, true);
    ai_contact_damage(g, e, 6.f * diff);
}

/* CHARGER : state machine sur ai_t2.
 *   0 = cooldown : mouvement lent, recale ai_t2->1 si aligne+250px
 *   1 = telegraph : immobile, 0.8s, brille rouge
 *   2 = charging : straight line a 280 speed, 0.9s, gros dmg
 * Quand charging finit -> retour 0 et cooldown forc. */
static void ai_charger(Game *g, Enemy *e, float dt, float dx, float dy, float dist) {
    float diff = powf(1.15f, (float)(g->floor_index - 1));
    e->ai_t += dt;
    int state = (int)e->ai_t2;          /* 0/1/2 -- on stocke en float */
    if (state == 0) {
        /* cooldown : approche lente */
        ai_move_toward(g, e, dt, dx, dy, dist, 30.f, false);
        /* trigger : aligne (dist < 250) ET cooldown ecoule (1.4s) */
        if (e->ai_t > 1.4f && dist < 250.f) {
            e->ai_t2 = 1.f;             /* -> telegraph */
            e->ai_t = 0.f;
            /* stocke la direction au moment de l aim (locked) */
            e->knockback_x = dx / dist; /* hijack pour stocker dir.x */
            e->knockback_y = dy / dist; /* dir.y */
            sfx_play(g, SFX_HEAVY_HIT);
        }
    } else if (state == 1) {
        /* telegraph : on reste sur place, emet sparks rouges */
        if ((rand() % 100) < 60) {
            float a = (rand() % 360) * 0.01745f;
            particle_spawn_kind(g, e->x, e->y,
                                cosf(a) * 30.f, sinf(a) * 30.f,
                                0.25f, 0xFF4020FF, 2.5f, 2);
        }
        if (e->ai_t > 0.80f) {
            e->ai_t2 = 2.f;             /* -> charging */
            e->ai_t = 0.f;
            sfx_play(g, SFX_SHOOT);
        }
    } else {
        /* charging : straight line. La direction est dans knockback_x/y. */
        float vx = e->knockback_x * 280.f;
        float vy = e->knockback_y * 280.f;
        float nx = e->x + vx * dt;
        float ny = e->y + vy * dt;
        bool wall = false;
        if (!aabb_solid(g, nx, e->y, e->r - 1)) e->x = nx; else wall = true;
        if (!aabb_solid(g, e->x, ny, e->r - 1)) e->y = ny; else wall = true;
        /* coupure du charge sur mur ou sur fin du timer */
        if (wall || e->ai_t > 0.90f) {
            e->ai_t2 = 0.f;
            e->ai_t = 0.f;
            e->knockback_x = 0; e->knockback_y = 0;
            if (wall) {
                /* impact spectaculaire : shake + particules */
                g->shake_t = 0.30f; g->shake_mag = 6.f;
                for (int k = 0; k < 20; k++) {
                    float a = (rand() % 360) * 0.01745f;
                    float s = 60.f + (rand() % 80);
                    particle_spawn_kind(g, e->x, e->y,
                                        cosf(a)*s, sinf(a)*s,
                                        0.45f, 0x806050FF, 2.5f, 2);
                }
                sfx_play(g, SFX_HEAVY_HIT);
                e->stun_t = 0.6f;       /* stun apres collision murale */
            }
        }
        /* dmg renforces pendant la charge */
        ai_contact_damage(g, e, 18.f * diff);
        return;
    }
    /* dmg de contact en cooldown / telegraph */
    ai_contact_damage(g, e, 8.f * diff);
}

/* MAGE : kite a 180 px, homing fae bolt toutes les 2s, blink si trop proche */
static void ai_mage(Game *g, Enemy *e, float dt, float dx, float dy, float dist) {
    float diff = powf(1.15f, (float)(g->floor_index - 1));
    e->ai_t += dt;
    /* blink defensif */
    if (dist < 80.f && e->ai_t2 <= 0.f) {
        /* teleport a 220 px dans la direction opposee */
        float bx = e->x - dx / dist * 220.f;
        float by = e->y - dy / dist * 220.f;
        int tx = (int)(bx / TILE), ty = (int)(by / TILE);
        if (tx > 0 && ty > 0 && tx < MAP_W - 1 && ty < MAP_H - 1 &&
            !aabb_solid(g, bx, by, e->r)) {
            for (int k = 0; k < 14; k++) {
                float a = (rand() % 360) * 0.01745f;
                particle_spawn_kind(g, e->x, e->y, cosf(a)*70, sinf(a)*70,
                                    0.35f, 0xF080F0FF, 2.f, 0);
                particle_spawn_kind(g, bx, by, cosf(a)*70, sinf(a)*70,
                                    0.35f, 0xF080F0FF, 2.f, 0);
            }
            e->x = bx; e->y = by;
            e->ai_t2 = 3.0f;            /* cooldown blink */
            sfx_play(g, SFX_PORTAL);
        }
    } else {
        /* drift lent pour maintenir distance 180 px */
        if (dist > 200.f)      ai_move_toward(g, e, dt, dx, dy, dist, 25.f, false);
        else if (dist < 160.f) ai_move_toward(g, e, dt, -dx, -dy, dist, 25.f, false);
    }
    if (e->ai_t2 > 0.f) e->ai_t2 -= dt;
    /* tir homing fae */
    if (e->ai_t > 2.0f && dist < 260.f) {
        e->ai_t = 0.f;
        Projectile pr = {0};
        pr.x = e->x; pr.y = e->y;
        pr.vx = dx / dist * 90.f;
        pr.vy = dy / dist * 90.f;
        pr.life = 4.f; pr.r = 4.f;
        pr.dmg = 9.f * diff; pr.owner = 1; pr.reflectable = true;
        pr.primary = EL_FAE;
        pr.homing = 1.5f;               /* recherche le joueur */
        pr.target_idx = -1;
        combo_apply_to_enemy_projectile(e->combo_mask, &pr);
        projectile_spawn(g, pr);
        sfx_play(g, SFX_ZAP);
    }
    ai_contact_damage(g, e, 3.f * diff);
}

static void ai_dispatch(Game *g, Enemy *e, int i, float dt) {
    Player *p = &g->player;
    float dx = p->x - e->x;
    float dy = p->y - e->y;
    float dist = sqrtf(dx * dx + dy * dy) + 0.01f;
    e->facing = atan2f(dy, dx);
    switch (e->kind) {
        case EK_ZOMBIE:  ai_zombie (g, e,    dt, dx, dy, dist); break;
        case EK_SLIME:   ai_slime  (g, e, i, dt, dx, dy, dist); break;
        case EK_BANDIT:  ai_bandit (g, e,    dt, dx, dy, dist); break;
        case EK_DEMON:   ai_demon  (g, e,    dt, dx, dy, dist); break;
        case EK_RAT:     ai_rat    (g, e, i, dt, dx, dy, dist); break;
        case EK_GHOST:   ai_ghost  (g, e,    dt, dx, dy, dist); break;
        case EK_CHARGER: ai_charger(g, e,    dt, dx, dy, dist); break;
        case EK_MAGE:    ai_mage   (g, e,    dt, dx, dy, dist); break;
        default: break;
    }
}

void update_enemies(Game *g) {
    float dt = g->dt;
    for (int i = 0; i < MAX_ENEMIES; i++) {
        Enemy *e = &g->enemies[i];
        if (!e->alive) continue;
        if (e->hit_flash > 0.f) e->hit_flash -= dt;
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
        /* knockback : on capture la vitesse AVANT decay pour pouvoir
         * calculer les degats d impact si on heurte quelque chose. */
        float kspeed = sqrtf(e->knockback_x * e->knockback_x +
                             e->knockback_y * e->knockback_y);
        e->knockback_x *= 0.85f;
        e->knockback_y *= 0.85f;
        bool hit_wall = false;
        {
            float kdx = e->knockback_x * dt;
            float kdy = e->knockback_y * dt;
            if (!aabb_solid(g, e->x + kdx, e->y, e->r - 1)) e->x += kdx;
            else { e->knockback_x = 0; hit_wall = true; }
            if (!aabb_solid(g, e->x, e->y + kdy, e->r - 1)) e->y += kdy;
            else { e->knockback_y = 0; hit_wall = true; }
        }
        /* impact prop : pas de collision dure, mais on declenche la
         * destruction visuelle des props legers a la portee. */
        if (kspeed > 90.f) {
            if (world_assets_bump_at(g, e->x, e->y, e->r + 4.f)) {
                /* coup d arret partiel : on conserve 60% de l elan */
                e->knockback_x *= 0.60f;
                e->knockback_y *= 0.60f;
            }
        }
        /* impact mur : degats proportionnels a la vitesse ecrasee. Seuil
         * 80 px/s pour que les petits knockback ne fassent pas de bruit. */
        if (hit_wall && kspeed > 80.f) {
            float impact_dmg = (kspeed - 80.f) * 0.06f;
            if (impact_dmg > 30.f) impact_dmg = 30.f;
            if (impact_dmg >= 1.f) {
                /* dust burst sur le point d impact */
                for (int k = 0; k < 10; k++) {
                    float a = (rand() % 360) * 0.01745f;
                    float s = 40.f + (rand() % 60);
                    particle_spawn_kind(g, e->x, e->y,
                                        cosf(a) * s, sinf(a) * s,
                                        0.30f, 0x806050C0, 2.0f, 0);
                }
                sfx_play(g, SFX_HEAVY_HIT);
                /* shake court mais marque pour signaler l impact */
                if (g->shake_t < 0.12f) {
                    g->shake_t = 0.12f;
                    g->shake_mag = 3.0f + impact_dmg * 0.10f;
                }
                /* degats : on passe par world_enemy_damage pour le dmgnum
                 * + le hook loop_on_hit (joueur credite). */
                world_enemy_damage(g, i, impact_dmg, EL_NONE, 0, 0);
            }
        }
        /* chain enemy-enemy : si on bouge vite, on tente une collision
         * avec un voisin. Les deux prennent des degats et l elan est
         * partage (effet domino visible). */
        if (kspeed > 100.f) {
            for (int j = 0; j < MAX_ENEMIES; j++) {
                if (j == i) continue;
                Enemy *o = &g->enemies[j];
                if (!o->alive || o->dying_t > 0.f) continue;
                float ddx = o->x - e->x;
                float ddy = o->y - e->y;
                float rr  = e->r + o->r;
                if (ddx*ddx + ddy*ddy < rr * rr) {
                    float chain_dmg = (kspeed - 100.f) * 0.05f;
                    if (chain_dmg > 20.f) chain_dmg = 20.f;
                    if (chain_dmg < 1.f) break;
                    world_enemy_damage(g, j, chain_dmg, EL_NONE,
                                       e->knockback_x * 0.5f,
                                       e->knockback_y * 0.5f);
                    world_enemy_damage(g, i, chain_dmg * 0.5f, EL_NONE, 0, 0);
                    /* transfert d elan (cradle de Newton) */
                    o->knockback_x += e->knockback_x * 0.5f;
                    o->knockback_y += e->knockback_y * 0.5f;
                    e->knockback_x *= 0.35f;
                    e->knockback_y *= 0.35f;
                    /* particules d impact entre les deux */
                    float mx = (e->x + o->x) * 0.5f;
                    float my = (e->y + o->y) * 0.5f;
                    for (int k = 0; k < 6; k++) {
                        float a = (rand() % 360) * 0.01745f;
                        particle_spawn_kind(g, mx, my,
                                            cosf(a) * 50.f, sinf(a) * 50.f,
                                            0.25f, 0xC0A080FF, 1.8f, 2);
                    }
                    break;
                }
            }
        }
        if (e->is_boss) { boss_update(g, e, dt); continue; }
        ai_dispatch(g, e, i, dt);
    }
    g->enemy_alive_count = 0;
    for (int i = 0; i < MAX_ENEMIES; i++)
        if (g->enemies[i].alive && g->enemies[i].dying_t <= 0.f)
            g->enemy_alive_count++;
}
