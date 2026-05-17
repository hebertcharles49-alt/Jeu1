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

void update_enemies(Game *g) {
    Player *p = &g->player;
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
            combo_apply_to_enemy_projectile(e->combo_mask, &pr);
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
                combo_apply_to_enemy_projectile(e->combo_mask, &pr);
                projectile_spawn(g, pr);
            }
            sfx_play(g, SFX_SHOOT);
        }
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
    g->enemy_alive_count = 0;
    for (int i = 0; i < MAX_ENEMIES; i++)
        if (g->enemies[i].alive && g->enemies[i].dying_t <= 0.f)
            g->enemy_alive_count++;
}
