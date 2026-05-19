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
    /* element_chance desactive : les talismans passent par le milestone
     * system (cf world_enemy_damage). Le champ est garde dans DropProfile
     * pour le debug futur mais on ne tire plus dessus. */
    (void)d.element_chance;
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
    /* u_expose_weakness : si la cible a expose_t > 0 ET l attaque est
     * super-effective (eff > 1), x1.5 bonus. */
    if (g->player.u_expose_weakness && e->expose_t > 0.f && eff > 1.f) {
        dmg *= 1.5f;
    }
    /* u_stun_on_melee : x2 dmg si la cible est actuellement stun
     * (les melee posent le stun via stun_t, cf combat.c fire_fists). */
    if (g->player.u_stun_on_melee && e->stun_t > 0.f) {
        dmg *= 2.f;
    }
    /* u_expose_weakness : un crit POSE le timer 5s sur la cible. */
    if (g->player.u_expose_weakness && g->current_attack_crit) {
        e->expose_t = 5.0f;
    }
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
        /* Sang : "vie + eau". Si une SURF_WATER est proche, elle est
         * convertie en SURF_BLOOD ; sinon petite chance (20%) de
         * spawner du sang neuf a l endroit du cadavre. */
        if (!e->is_boss) surface_blood_drop(g, e->x, e->y);
        /* Signature de mort par kind : certains ennemis laissent une
         * surface au sol (huile, glace, eau) -- ouvre des combos
         * physiques avec les attaques du joueur. */
        if (!e->is_boss) {
            switch (e->kind) {
                case EK_DEMON:
                    /* huile sombre, inflammable. Synergise avec FIRE. */
                    surface_spawn(g, SURF_OIL, e->x, e->y, 18.f, 0.f);
                    break;
                case EK_SLIME:
                    /* eau (resque acide visuel) : ralentit + conducteur. */
                    if ((rand() % 100) < 60)
                        surface_spawn(g, SURF_WATER, e->x, e->y, 14.f, 0.f);
                    break;
                default: break;
            }
        }
        /* BUILD-DEF u_corpse_mines : 30% des morts laissent une mine
         * statique (projectile owner=2, immobile, AOE 26). Touche tout
         * ennemi qui passe dessus, pas le joueur. */
        if (g->player.u_corpse_mines && !e->is_boss && (rand() % 100) < 30) {
            Projectile pr = {0};
            pr.x = e->x; pr.y = e->y;
            pr.vx = 0; pr.vy = 0;
            pr.life = 8.f; pr.r = 6.f;
            pr.dmg = 12.f * powf(1.15f, (float)(g->floor_index - 1));
            pr.owner = 0;        /* mine du joueur : touche les ennemis qui
                                  * marchent dessus (proj statique AOE). */
            pr.aoe = 26.f;
            pr.primary = EL_VOID;
            /* visuel : pose une particule pulsante */
            for (int k = 0; k < 8; k++) {
                float a = (rand() % 360) * 0.01745f;
                particle_spawn_kind(g, e->x, e->y,
                                    cosf(a) * 25.f, sinf(a) * 25.f,
                                    0.35f, 0x80300080, 1.8f, 0);
            }
            projectile_spawn(g, pr);
        }
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
            /* mort cinematique : shake fort + long hitstop + flash blanc +
             * fragments dans la couleur de l element du boss + portail. */
            g->shake_t = 0.9f; g->shake_mag = 10.f;
            g->hitstop_t = 0.45f;
            g->flash_t  = 0.40f;
            g->boss_death_t = 1.8f;    /* declenche l overlay de victoire */
            /* fragment storm dans la couleur element */
            uint32_t col_e = element_color(e->element);
            for (int k = 0; k < 120; k++) {
                float a = (rand() % 360) * 0.01745f;
                float s = 80.f + rand() % 280;
                /* mix entre or et couleur element selon k */
                uint32_t c = (k & 1) ? col_e : 0xFFE060FF;
                particle_spawn_kind(g, e->x, e->y,
                                    cosf(a) * s, sinf(a) * s,
                                    1.2f, c, 3.5f, 2);
            }
            /* anneaux concentriques */
            for (int ring = 0; ring < 3; ring++) {
                float speed = 60.f + ring * 50.f;
                int n = 36;
                for (int k = 0; k < n; k++) {
                    float a = (k / (float)n) * 6.2831f;
                    particle_spawn_kind(g, e->x, e->y,
                                        cosf(a) * speed, sinf(a) * speed,
                                        0.9f + ring * 0.15f, col_e, 2.5f, 0);
                }
            }
            pickup_spawn(g, PU_PORTAL, 0, e->x, e->y);
            sfx_play(g, SFX_BOSS);
            sfx_play(g, SFX_EXPLODE);
            g->portal_spawned = true;
            /* Les drops d elements/uniques sont desormais geres par le
             * milestone system (cf world_enemy_damage). Le boss n a plus
             * besoin de drop garanti -- il declenche naturellement des
             * milestones via les kills accumules. */
            for (int h = 0; h < HERO_COUNT; h++) {
                if (!g->meta.hero_discovered[h]) {
                    g->meta.hero_discovered[h] = true;
                    save_write(&g->meta);
                    char buf[48];
                    snprintf(buf, sizeof(buf), "HEROS REVELE : %s",
                             hero_name((HeroClass)h));
                    toast_push(g, buf, 0xFFE080FF, 5.0f);
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
    if (dmg > 0.f) {
        g->run_damage_dealt += (int)dmg;
        g->dps_frame_acc    += dmg;
    }
    loop_on_hit(g);
    if (e->hp <= 0.f && !already_dead) {
        loop_on_kill(g);
        /* run_kills a deja ete incremente par enemy_take_damage. */
        /* === Drops pre-rolles (talismans + uniques) ===
         * Si run_kills atteint le prochain milestone, on spawn a la
         * position du cadavre. Talismans = element au choix (prefere
         * non-decouvert). Uniques = pick aleatoire dans la table. */
        if (g->talisman_drops_idx < RUN_TALISMAN_MAX &&
            g->run_kills >= g->talisman_drops[g->talisman_drops_idx]) {
            /* Seuls les 7 originels (FIRE..FAE) sont jouables en run.
             * STEEL/DARK/HOLY sont reserves a d'autres contextes (boss /
             * codex / unlock meta). */
            int undisc[EL_COUNT]; int n_und = 0;
            int all[EL_COUNT]; int n_all = 0;
            for (int el = EL_FIRE; el <= EL_FAE; el++) {
                all[n_all++] = el;
                if (!g->meta.element_discovered[el]) undisc[n_und++] = el;
            }
            int el_pick = (n_und > 0)
                            ? undisc[rand() % n_und]
                            : (n_all > 0 ? all[rand() % n_all] : EL_FIRE);
            pickup_spawn(g, PU_ELEMENT, el_pick, e->x, e->y);
            g->talisman_drops_idx++;
        }
        if (g->unique_drops_idx < RUN_UNIQUE_MAX &&
            g->run_kills >= g->unique_drops[g->unique_drops_idx]) {
            int n_uniq = unique_def_count();
            if (n_uniq > 0) {
                int uid = rand() % n_uniq;
                Item u = unique_make(uid);
                if (uid >= 0 && uid < 32) g->meta.unique_seen[uid] = true;
                pickup_spawn_item(g, u, e->x, e->y);
            }
            g->unique_drops_idx++;
        }
        /* === Build-defining effects on kill === */
        /* u_kill_wave : vague de repulsion + dmg autour du joueur */
        if (g->player.u_kill_wave) {
            float diff = powf(1.15f, (float)(g->floor_index - 1));
            float wave_dmg = 6.f * diff;
            for (int j = 0; j < MAX_ENEMIES; j++) {
                Enemy *o = &g->enemies[j];
                if (j == idx || !o->alive || o->dying_t > 0.f) continue;
                float dx = o->x - g->player.x, dy = o->y - g->player.y;
                if (dx*dx + dy*dy < 60.f * 60.f) {
                    world_enemy_damage(g, j, wave_dmg, EL_WATER,
                                       dx * 3.f, dy * 3.f);
                }
            }
            for (int k = 0; k < 18; k++) {
                float a = (k / 18.f) * 6.2831f;
                particle_spawn_kind(g, g->player.x, g->player.y,
                                    cosf(a) * 90.f, sinf(a) * 90.f,
                                    0.40f, 0x80B0FFFF, 2.4f, 0);
            }
        }
        /* u_kill_stack_dmg : +1 stack par kill (cap 20). */
        if (g->player.u_kill_stack_dmg && g->player.kill_stack_count < 20) {
            g->player.kill_stack_count++;
        }
        /* killstreak : reset le timer + increment ; trigger overdrive
         * a 5 kills consecutifs (fenetre 3s par kill). */
        g->killstreak_count++;
        g->killstreak_t     = 3.0f;
        if (g->killstreak_count >= 5 && g->overdrive_t <= 0.f) {
            g->overdrive_t = 5.0f;
            g->shake_t = 0.20f; g->shake_mag = 4.f;
            /* burst d aura : 30 particules rouges autour du joueur */
            for (int k = 0; k < 30; k++) {
                float a = (rand() % 360) * 0.01745f;
                particle_spawn_kind(g, g->player.x, g->player.y,
                                    cosf(a) * 110.f, sinf(a) * 110.f,
                                    0.55f, 0xFF3030FF, 3.0f, 2);
            }
            sfx_play(g, SFX_BOSS);
            toast_push(g, "OVERDRIVE !", 0xFF3030FF, 4.0f);
        }
    }
}

/* forward decls : les boss utilisent les helpers ai_<x> definis plus bas. */
static void ai_move_toward(Game *g, Enemy *e, float dt,
                            float dx, float dy, float dist, float speed,
                            bool phase_walls);
static void ai_contact_damage(Game *g, Enemy *e, float dmg);

/* ============================================================
 *  BOSS -- 1 par biome, multi-phases, mecaniques uniques, adds.
 *  variant 0..4 mappe 1:1 sur biome_for_floor(floor).
 *
 *  Etat utilise dans la struct Enemy :
 *    e->ai_t        : timer pattern principal (reset apres chaque cast)
 *    e->ai_t2       : timer pattern secondaire (summons, sub-patterns)
 *    e->telegraph_t : intro 1.5s au spawn (inchange)
 *    e->split_left  : memo de phase atteinte (0 -> 1 -> 2). On l hijack
 *                     puisqu il sert pour le slime mais le boss n est pas
 *                     un slime, c'est safe.
 *
 *  Phases :
 *    0 : full HP -> 55%
 *    1 :   55%   -> 25%
 *    2 :   25%   -> mort
 *  Le passage de phase declenche un burst de particules + invuln 0.3s.
 * ============================================================ */
static int boss_phase(const Enemy *e) {
    float frac = e->hp / e->maxhp;
    if (frac < 0.25f) return 2;
    if (frac < 0.55f) return 1;
    return 0;
}

static int boss_count_adds(Game *g, const Enemy *self) {
    int n = 0;
    for (int i = 0; i < MAX_ENEMIES; i++) {
        Enemy *o = &g->enemies[i];
        if (o == self || !o->alive || o->is_boss) continue;
        if (o->dying_t > 0.f) continue;
        n++;
    }
    return n;
}

/* burst spectaculaire de particules + invuln pour la transition de phase */
static void boss_phase_transition(Game *g, Enemy *e, int new_phase) {
    uint32_t col = element_color(e->element);
    for (int k = 0; k < 40; k++) {
        float a = (rand() % 360) * 0.01745f;
        float s = 80.f + (rand() % 100);
        particle_spawn_kind(g, e->x, e->y, cosf(a) * s, sinf(a) * s,
                            0.6f, col, 3.5f, 2);
    }
    g->shake_t = 0.35f; g->shake_mag = 6.0f;
    g->hitstop_t = 0.10f;
    e->telegraph_t = 0.30f;      /* mini-invuln pendant le burst */
    sfx_play(g, SFX_BOSS);
    (void)new_phase;
}

/* -------------------- BOSS 0 : NECROPANTE (Crypte, DARK) --------------
 * Phase 0 : 3-shot cone DARK toutes les 1.8s + summon 1 zombie/4s (cap 2)
 * Phase 1 : ajoute teleport aleatoire toutes les 5s + 5-shot cone
 * Phase 2 : summon 4 zombies en burst (une fois) + cone toutes les 1.1s
 */
static void boss_necropante(Game *g, Enemy *e, float dt, int phase,
                             float dx, float dy, float dist)
{
    float diff = powf(1.15f, (float)(g->floor_index - 1));
    /* mouvement : slow chase. Plus rapide en phase 2 (rage). */
    float speed = 32.f + phase * 8.f;
    if (dist > 30.f) ai_move_toward(g, e, dt, dx, dy, dist, speed, false);

    /* teleport en phase 1+ : tous les 5s, choisit une tile floor random */
    if (phase >= 1) {
        e->ai_t2 += dt;
        if (e->ai_t2 > 5.0f) {
            e->ai_t2 = 0.f;
            float ang = (rand() % 360) * 0.01745f;
            float nd  = 80.f + (rand() % 40);
            float nx = e->x + cosf(ang) * nd;
            float ny = e->y + sinf(ang) * nd;
            int tx = (int)(nx / TILE), ty = (int)(ny / TILE);
            if (tx > 0 && ty > 0 && tx < MAP_W - 1 && ty < MAP_H - 1 &&
                !aabb_solid(g, nx, ny, e->r)) {
                for (int k = 0; k < 16; k++) {
                    float a = (rand() % 360) * 0.01745f;
                    particle_spawn_kind(g, e->x, e->y, cosf(a)*90, sinf(a)*90,
                                        0.5f, 0x602080FF, 2.8f, 0);
                }
                e->x = nx; e->y = ny;
            }
        }
    }

    /* attaque cone : 3 a 5 shots elargi avec la phase */
    float cd = (phase == 2) ? 1.1f : 1.8f;
    e->ai_t += dt;
    if (e->ai_t > cd) {
        e->ai_t = 0.f;
        int n = 3 + phase;
        float a0 = atan2f(dy, dx);
        for (int k = -n/2; k <= n/2; k++) {
            float a = a0 + k * 0.16f;
            Projectile pr = {0};
            pr.x = e->x; pr.y = e->y;
            pr.vx = cosf(a) * 120.f; pr.vy = sinf(a) * 120.f;
            pr.life = 3.5f; pr.r = 3.5f; pr.dmg = 10.f * diff; pr.owner = 1;
            pr.reflectable = true; pr.primary = EL_DARK;
            combo_apply_to_enemy_projectile(e->combo_mask, &pr);
            projectile_spawn(g, pr);
        }
        sfx_play(g, SFX_SHOOT);
    }
    /* summon : reuse ai_t comme timer combine via une marche separee. On
     * stocke le timer dans facing (libre apres atan2) -- trop bricole.
     * Plus simple : on summon proportionnellement au time absolu modulo. */
    int max_adds = (phase == 2) ? 4 : 2;
    int adds = boss_count_adds(g, e);
    if (adds < max_adds) {
        static float s_summon_t[5] = {0};
        s_summon_t[0] += dt;
        float cd_sum = (phase == 2) ? 1.2f : 4.0f;
        if (s_summon_t[0] > cd_sum) {
            s_summon_t[0] = 0.f;
            int n_sum = (phase == 2 && adds == 0) ? 3 : 1;
            for (int k = 0; k < n_sum; k++) {
                enemy_spawn(g, EK_ZOMBIE,
                            e->x + (rand() % 50) - 25,
                            e->y + (rand() % 50) - 25);
            }
            sfx_play(g, SFX_BOSS);
        }
    }
}

/* -------------------- BOSS 1 : GEANT DE PIERRE (Cavernes, EARTH) ------
 * Phase 0 : slow chase + ground pound (AOE radial telegraphe a self)
 * Phase 1 : rocks falling -- AOE telegraphe sur le sol pres du joueur
 * Phase 2 : charge en ligne droite sur la position du joueur (telegraphe)
 */
static void boss_geant(Game *g, Enemy *e, float dt, int phase,
                       float dx, float dy, float dist)
{
    float diff = powf(1.15f, (float)(g->floor_index - 1));
    float speed = 26.f;
    /* state : 0 = chasse, 1 = telegraph ground pound, 2 = pound active
     *         3 = rocks falling, 4 = charge telegraph, 5 = charging
     * encode dans ai_t2 (cast en int). */
    int state = (int)e->ai_t2;
    e->ai_t += dt;

    /* dispatch state */
    if (state == 0) {
        if (dist > 30.f) ai_move_toward(g, e, dt, dx, dy, dist, speed, false);
        /* trigger pattern toutes les 2.4s, le pattern depend de la phase */
        if (e->ai_t > 2.4f) {
            e->ai_t = 0.f;
            if (phase == 2)      e->ai_t2 = 4.f;      /* charge telegraph */
            else if (phase >= 1 && (rand() % 100) < 50)
                                 e->ai_t2 = 3.f;      /* rocks falling */
            else                 e->ai_t2 = 1.f;      /* ground pound */
        }
    } else if (state == 1) {
        /* ground pound telegraph 0.8s : sparks bruns autour */
        if ((rand() % 100) < 70) {
            float a = (rand() % 360) * 0.01745f;
            particle_spawn_kind(g, e->x, e->y,
                                cosf(a) * 30.f, sinf(a) * 30.f,
                                0.35f, 0xC09060FF, 2.2f, 0);
        }
        if (e->ai_t > 0.8f) { e->ai_t = 0.f; e->ai_t2 = 2.f; }
    } else if (state == 2) {
        /* pound : radial 12 projectiles + shake + AOE direct sur le joueur */
        for (int k = 0; k < 12; k++) {
            float a = (k / 12.f) * 6.2831f;
            Projectile pr = {0};
            pr.x = e->x; pr.y = e->y;
            pr.vx = cosf(a) * 100.f; pr.vy = sinf(a) * 100.f;
            pr.life = 2.5f; pr.r = 3.5f; pr.dmg = 14.f * diff; pr.owner = 1;
            pr.reflectable = true; pr.primary = EL_EARTH;
            combo_apply_to_enemy_projectile(e->combo_mask, &pr);
            projectile_spawn(g, pr);
        }
        g->shake_t = 0.35f; g->shake_mag = 7.0f;
        sfx_play(g, SFX_EXPLODE);
        e->ai_t = 0.f; e->ai_t2 = 0.f;
    } else if (state == 3) {
        /* rocks falling : on place 4 marqueurs AOE puis declenche apres 1.0s.
         * On encode l etat en sous-state via le timer (compte le temps total) */
        if (e->ai_t < 1.0f) {
            /* telegraph : emet sparks brunes a 4 endroits autour du joueur */
            Player *p = &g->player;
            for (int idx = 0; idx < 4; idx++) {
                float ang = idx * 1.5707f + g->time * 0.5f;
                float rx = p->x + cosf(ang) * 35.f;
                float ry = p->y + sinf(ang) * 35.f;
                if ((rand() % 100) < 50) {
                    particle_spawn_kind(g, rx, ry, 0, -25,
                                        0.30f, 0x80604030, 2.0f, 0);
                }
            }
        } else {
            /* rocks explode at the same 4 spots (snapshot du player a t=1.0) */
            Player *p = &g->player;
            for (int idx = 0; idx < 4; idx++) {
                float ang = idx * 1.5707f + g->time * 0.5f;
                float rx = p->x + cosf(ang) * 35.f;
                float ry = p->y + sinf(ang) * 35.f;
                /* burst */
                for (int k = 0; k < 8; k++) {
                    float a = (rand() % 360) * 0.01745f;
                    particle_spawn_kind(g, rx, ry,
                                        cosf(a)*70, sinf(a)*70,
                                        0.45f, 0xA08060FF, 2.5f, 2);
                }
                /* dmg si player dans le rayon de la rock */
                float pdx = p->x - rx, pdy = p->y - ry;
                if (pdx*pdx + pdy*pdy < 18.f * 18.f &&
                    p->invuln_t <= 0.f && p->dash_t <= 0.f) {
                    player_take_damage_from(g, 12.f * diff, rx, ry);
                }
            }
            sfx_play(g, SFX_EXPLODE);
            e->ai_t = 0.f; e->ai_t2 = 0.f;
        }
    } else if (state == 4) {
        /* charge telegraph : aura rouge clignote */
        if ((rand() % 100) < 60) {
            float a = (rand() % 360) * 0.01745f;
            particle_spawn_kind(g, e->x, e->y,
                                cosf(a) * 25.f, sinf(a) * 25.f,
                                0.30f, 0xFF6040FF, 2.5f, 2);
        }
        if (e->ai_t > 0.7f) {
            e->ai_t = 0.f;
            e->ai_t2 = 5.f;
            /* freeze direction */
            e->knockback_x = dx / dist;
            e->knockback_y = dy / dist;
            sfx_play(g, SFX_HEAVY_HIT);
        }
    } else if (state == 5) {
        /* charging : 320 speed, 0.9s ou wall */
        float vx = e->knockback_x * 320.f;
        float vy = e->knockback_y * 320.f;
        float nx = e->x + vx * dt;
        float ny = e->y + vy * dt;
        bool wall = false;
        if (!aabb_solid(g, nx, e->y, e->r - 1)) e->x = nx; else wall = true;
        if (!aabb_solid(g, e->x, ny, e->r - 1)) e->y = ny; else wall = true;
        ai_contact_damage(g, e, 20.f * diff);
        if (wall || e->ai_t > 0.9f) {
            if (wall) {
                g->shake_t = 0.4f; g->shake_mag = 8.f;
                for (int k = 0; k < 24; k++) {
                    float a = (rand() % 360) * 0.01745f;
                    particle_spawn_kind(g, e->x, e->y,
                                        cosf(a)*100, sinf(a)*100, 0.5f,
                                        0x806040FF, 2.8f, 2);
                }
            }
            e->ai_t = 0.f; e->ai_t2 = 0.f;
            e->knockback_x = 0; e->knockback_y = 0;
        }
    }
    /* SURPRISE GEANT : en phase 2, meteor shower passif tous les 1.5s.
     * Une roche tombe sur la position predite du joueur (snapshot t+0.4s),
     * marqueur de poussiere puis projectile statique avec AOE 30 a t=0.6s. */
    if (phase == 2) {
        /* timer stocke dans split_left>=10 ; on encode "memo phase+timer
         * incrementiel" via un static module local pour pas piocher dans
         * Enemy. Note : ca compte pour tous les Geant simultanes mais on
         * a au plus 1 boss vivant donc OK. */
        static float meteor_t = 0.f;
        meteor_t += dt;
        if (meteor_t > 1.5f) {
            meteor_t = 0.f;
            float px = g->player.x + g->player.vx * 0.4f;
            float py = g->player.y + g->player.vy * 0.4f;
            for (int k = 0; k < 12; k++) {
                float a = (rand() % 360) * 0.01745f;
                particle_spawn_kind(g, px + cosf(a) * 12.f,
                                    py + sinf(a) * 12.f,
                                    0, -8.f,
                                    0.55f, 0x80604040, 2.0f, 0);
            }
            Projectile pr = {0};
            pr.x = px; pr.y = py;
            pr.vx = 0; pr.vy = 0;
            pr.life = 0.60f; pr.r = 5.f;
            pr.dmg = 16.f * diff; pr.owner = 1;
            pr.primary = EL_EARTH; pr.aoe = 30.f;
            projectile_spawn(g, pr);
        }
    }
}

/* -------------------- BOSS 2 : HYDRE (Marais, WATER) ------------------
 * Phase 0 : kite lent + 3 homing water orbs toutes les 2.5s
 * Phase 1 : ajoute des flaques d acide au sol (drop pendant deplacement)
 * Phase 2 : summon 3 slimes en burst + wave radial 8 projectiles
 */
static void boss_hydre(Game *g, Enemy *e, float dt, int phase,
                       float dx, float dy, float dist)
{
    float diff = powf(1.15f, (float)(g->floor_index - 1));
    /* kite : essaie de rester a 150 px */
    if (dist > 180.f)      ai_move_toward(g, e, dt, dx, dy, dist, 28.f, false);
    else if (dist < 120.f) ai_move_toward(g, e, dt, -dx, -dy, dist, 28.f, false);

    /* SURPRISE HYDRE : a chaque entree en phase 2, on summon une vague de
     * 4 slimes splitters dans un cercle autour du boss. C'est un "burst"
     * one-shot grace au memo split_left. */
    if (phase == 2 && e->split_left < 2 && boss_count_adds(g, e) < 5) {
        e->split_left = 2;
        for (int k = 0; k < 4; k++) {
            float a = (k / 4.f) * 6.2831f;
            enemy_spawn(g, EK_SLIME,
                        e->x + cosf(a) * 40.f,
                        e->y + sinf(a) * 40.f);
        }
        sfx_play(g, SFX_BOSS);
    }
    /* phase 1+ : drop d acide au sol */
    if (phase >= 1) {
        e->ai_t2 += dt;
        if (e->ai_t2 > 0.50f) {
            e->ai_t2 = 0.f;
            for (int k = 0; k < 4; k++) {
                float a = (rand() % 360) * 0.01745f;
                particle_spawn_kind(g, e->x, e->y,
                                    cosf(a) * 6.f, sinf(a) * 6.f,
                                    1.50f, 0x80E040C0, 2.4f, 0);
            }
        }
    }

    e->ai_t += dt;
    float cd = 2.5f - phase * 0.4f;        /* 2.5 -> 2.1 -> 1.7 */
    if (e->ai_t > cd) {
        e->ai_t = 0.f;
        /* 3 homing water orbs */
        for (int k = -1; k <= 1; k++) {
            float a0 = atan2f(dy, dx);
            float a = a0 + k * 0.30f;
            Projectile pr = {0};
            pr.x = e->x; pr.y = e->y;
            pr.vx = cosf(a) * 95.f; pr.vy = sinf(a) * 95.f;
            pr.life = 4.0f; pr.r = 4.f; pr.dmg = 11.f * diff; pr.owner = 1;
            pr.primary = EL_WATER; pr.homing = 1.0f;
            pr.target_idx = -1;
            combo_apply_to_enemy_projectile(e->combo_mask, &pr);
            projectile_spawn(g, pr);
        }
        /* phase 2 : burst + summon */
        if (phase == 2) {
            for (int k = 0; k < 8; k++) {
                float a = (k / 8.f) * 6.2831f;
                Projectile pr = {0};
                pr.x = e->x; pr.y = e->y;
                pr.vx = cosf(a) * 110.f; pr.vy = sinf(a) * 110.f;
                pr.life = 3.f; pr.r = 3.f; pr.dmg = 9.f * diff; pr.owner = 1;
                pr.primary = EL_WATER;
                projectile_spawn(g, pr);
            }
            if (boss_count_adds(g, e) < 3) {
                for (int k = 0; k < 2; k++) {
                    enemy_spawn(g, EK_SLIME,
                                e->x + (rand() % 40) - 20,
                                e->y + (rand() % 40) - 20);
                }
            }
        }
        sfx_play(g, SFX_SHOOT);
    }
}

/* -------------------- BOSS 3 : FORGERON DES ENFERS (Forge, FIRE) ------
 * Phase 0 : hammer slam (telegraphe cone in front) toutes les 2.4s
 * Phase 1 : ajoute fire wheels (3 projectiles chain) toutes les 3.5s
 * Phase 2 : rage -- slam cooldown 1.0s + summon 2 demons
 */
static void boss_forgeron(Game *g, Enemy *e, float dt, int phase,
                          float dx, float dy, float dist)
{
    float diff = powf(1.15f, (float)(g->floor_index - 1));
    float speed = 30.f + phase * 5.f;
    if (dist > 50.f) ai_move_toward(g, e, dt, dx, dy, dist, speed, false);

    e->ai_t += dt;
    float slam_cd = (phase == 2) ? 1.0f : (phase == 1 ? 1.8f : 2.4f);
    if (e->ai_t > slam_cd) {
        e->ai_t = 0.f;
        /* slam : 5-shot cone in player direction + AOE direct devant */
        float a0 = atan2f(dy, dx);
        for (int k = -2; k <= 2; k++) {
            float a = a0 + k * 0.16f;
            Projectile pr = {0};
            pr.x = e->x; pr.y = e->y;
            pr.vx = cosf(a) * 140.f; pr.vy = sinf(a) * 140.f;
            pr.life = 2.5f; pr.r = 3.5f; pr.dmg = 13.f * diff; pr.owner = 1;
            pr.primary = EL_FIRE; pr.aoe = 18.f;
            combo_apply_to_enemy_projectile(e->combo_mask, &pr);
            projectile_spawn(g, pr);
        }
        /* feu pose au sol devant */
        float fx = e->x + dx / dist * 35.f;
        float fy = e->y + dy / dist * 35.f;
        for (int k = 0; k < 10; k++) {
            float a = (rand() % 360) * 0.01745f;
            particle_spawn_kind(g, fx, fy,
                                cosf(a) * 40.f, sinf(a) * 40.f - 20.f,
                                0.80f, 0xFF6020FF, 2.5f, 2);
        }
        g->shake_t = 0.20f; g->shake_mag = 5.f;
        sfx_play(g, SFX_HEAVY_HIT);
    }

    /* fire wheels en phase 1+ : timer separe via ai_t2 */
    if (phase >= 1) {
        e->ai_t2 += dt;
        if (e->ai_t2 > 3.5f) {
            e->ai_t2 = 0.f;
            for (int k = 0; k < 3; k++) {
                float a = (k / 3.f) * 6.2831f + g->time;
                Projectile pr = {0};
                pr.x = e->x; pr.y = e->y;
                pr.vx = cosf(a) * 80.f; pr.vy = sinf(a) * 80.f;
                pr.life = 4.f; pr.r = 4.f; pr.dmg = 10.f * diff; pr.owner = 1;
                pr.primary = EL_FIRE; pr.chains = 2;
                projectile_spawn(g, pr);
            }
            sfx_play(g, SFX_ZAP);
        }
    }

    /* phase 2 : summon 2 demons (une seule fois grace au memo split_left) */
    if (phase == 2 && e->split_left < 2 && boss_count_adds(g, e) < 3) {
        e->split_left = 2;
        for (int k = 0; k < 2; k++) {
            enemy_spawn(g, EK_DEMON,
                        e->x + (rand() % 60) - 30,
                        e->y + (rand() % 60) - 30);
        }
        sfx_play(g, SFX_BOSS);
    }
}

/* -------------------- BOSS 4 : AVATAR DIVIN (Sanctuaire, HOLY) --------
 * Phase 0 : teleport + holy beam (line attack telegraphe 1.2s)
 * Phase 1 : summon 2 mages + multi-target beams
 * Phase 2 : beam cooldown reduit + traque le joueur via teleports
 */
static void boss_avatar(Game *g, Enemy *e, float dt, int phase,
                        float dx, float dy, float dist)
{
    float diff = powf(1.15f, (float)(g->floor_index - 1));
    (void)dist;
    /* state encode dans ai_t2 (cast int) : 0=idle, 1=beam telegraph, 2=fire,
     * 3=teleport spark. */
    int state = (int)e->ai_t2;
    e->ai_t += dt;

    if (state == 0) {
        /* idle : on tourne lentement autour du joueur a 150px */
        float ang = g->time * 0.6f;
        float tx = g->player.x + cosf(ang) * 150.f;
        float ty = g->player.y + sinf(ang) * 150.f;
        float tdx = tx - e->x, tdy = ty - e->y;
        float td = sqrtf(tdx * tdx + tdy * tdy) + 0.01f;
        if (td > 8.f) ai_move_toward(g, e, dt, tdx, tdy, td, 35.f, false);
        /* trigger pattern */
        float cd = (phase == 2) ? 1.4f : (phase == 1 ? 2.0f : 2.6f);
        if (e->ai_t > cd) {
            e->ai_t = 0.f;
            /* en phase 1+ tendance a teleporter avant de tirer */
            if (phase >= 1 && (rand() % 100) < 40) e->ai_t2 = 3.f;
            else                                   e->ai_t2 = 1.f;
            /* lock direction pour le beam */
            e->knockback_x = dx / (fabsf(dx) + fabsf(dy) + 0.01f);
            e->knockback_y = dy / (fabsf(dx) + fabsf(dy) + 0.01f);
            /* renormalise */
            float nl = sqrtf(e->knockback_x * e->knockback_x +
                             e->knockback_y * e->knockback_y) + 0.001f;
            e->knockback_x /= nl;
            e->knockback_y /= nl;
        }
    } else if (state == 1) {
        /* beam telegraph 1.2s : ligne de particules de la position vers
         * la direction lockee (knockback_x/y). */
        if ((rand() % 100) < 80) {
            float step = (rand() % 40) * 4.f;
            float px = e->x + e->knockback_x * step;
            float py = e->y + e->knockback_y * step;
            particle_spawn_kind(g, px, py, 0, 0, 0.20f, 0xFFE890FF, 1.5f, 0);
        }
        if (e->ai_t > 1.2f) { e->ai_t = 0.f; e->ai_t2 = 2.f; }
    } else if (state == 2) {
        /* fire beam : 8 projectiles en ligne droite */
        for (int k = 0; k < 8; k++) {
            Projectile pr = {0};
            float jit = ((rand() % 30) - 15);
            pr.x = e->x + e->knockback_x * (k * 6.f);
            pr.y = e->y + e->knockback_y * (k * 6.f) + jit;
            pr.vx = e->knockback_x * 180.f;
            pr.vy = e->knockback_y * 180.f;
            pr.life = 2.5f; pr.r = 4.f; pr.dmg = 12.f * diff; pr.owner = 1;
            pr.primary = EL_HOLY; pr.pierce = 3;
            projectile_spawn(g, pr);
        }
        sfx_play(g, SFX_ZAP);
        g->shake_t = 0.18f; g->shake_mag = 4.0f;
        e->ai_t = 0.f; e->ai_t2 = 0.f;
    } else if (state == 3) {
        /* teleport flash : 0.3s sparks puis bouge a 220px du joueur */
        if (e->ai_t < 0.30f) {
            if ((rand() % 100) < 60) {
                float a = (rand() % 360) * 0.01745f;
                particle_spawn_kind(g, e->x, e->y, cosf(a)*70, sinf(a)*70,
                                    0.30f, 0xFFE0FFFF, 2.f, 0);
            }
        } else {
            float ang = (rand() % 360) * 0.01745f;
            float nx = g->player.x + cosf(ang) * 220.f;
            float ny = g->player.y + sinf(ang) * 220.f;
            int tx = (int)(nx / TILE), ty = (int)(ny / TILE);
            if (tx > 0 && ty > 0 && tx < MAP_W - 1 && ty < MAP_H - 1 &&
                !aabb_solid(g, nx, ny, e->r)) {
                e->x = nx; e->y = ny;
                for (int k = 0; k < 20; k++) {
                    float a = (rand() % 360) * 0.01745f;
                    particle_spawn_kind(g, nx, ny,
                                        cosf(a)*60, sinf(a)*60,
                                        0.40f, 0xFFE0FFFF, 2.f, 0);
                }
            }
            e->ai_t = 0.f; e->ai_t2 = 1.f;       /* enchaine sur un beam */
        }
    }

    /* phase 1+ : summon 2 mages une seule fois (memo split_left) */
    if (phase >= 1 && e->split_left < 1 && boss_count_adds(g, e) < 3) {
        e->split_left = 1;
        for (int k = 0; k < 2; k++) {
            enemy_spawn(g, EK_MAGE,
                        e->x + (rand() % 80) - 40,
                        e->y + (rand() % 80) - 40);
        }
        sfx_play(g, SFX_BOSS);
    }
}

static void boss_update(Game *g, Enemy *e, float dt) {
    Player *p = &g->player;
    if (e->telegraph_t > 0.f) { e->telegraph_t -= dt; return; }
    /* ENRAGE : sous 10% HP, on accelere drastiquement les timers internes
     * (les ai_t avancent 1.6x plus vite) et on emet un signal visuel
     * sanglant. C'est la "surprise" -- le combat termine sur un push
     * agressif au lieu de finir tranquille. */
    bool enraged = (e->hp / e->maxhp) < 0.10f;
    if (enraged) {
        dt *= 1.6f;
        /* fountain de sparks rouges en continu */
        if ((rand() % 100) < 50) {
            float a = (rand() % 360) * 0.01745f;
            particle_spawn_kind(g, e->x, e->y,
                                cosf(a) * 40.f, sinf(a) * 40.f - 20.f,
                                0.35f, 0xFF2030FF, 2.5f, 2);
        }
    }
    /* phase + transition burst */
    int phase = boss_phase(e);
    if (phase > e->split_left && phase > 0) {
        /* hijack : pour memoriser la transition, on prend max() avec
         * split_left mais on garde split_left assez pour les flags
         * "summon-une-fois" de chaque boss. */
        if (phase > e->split_left) {
            boss_phase_transition(g, e, phase);
            /* on bump split_left juste pour l invariant (les boss qui
             * utilisent split_left comme memo de summon le re-bumperont
             * eux-memes, l ordre est compatible). */
            if (e->split_left < phase) {
                /* on tape une valeur "phase-1" pour pas court-circuiter
                 * les summons one-shot (qui testent split_left < N). */
                if (phase == 1) {
                    /* don t bump : laisser split_left=0 pour que le summon
                     * one-shot puisse passer plus tard. */
                } else {
                    /* phase 2 : on bump si pas deja */
                    if (e->split_left < 1) e->split_left = 1;
                }
            }
        }
    }

    float dx = p->x - e->x, dy = p->y - e->y;
    float dist = sqrtf(dx * dx + dy * dy) + 0.01f;
    e->facing = atan2f(dy, dx);

    switch (e->variant) {
        case 0: boss_necropante(g, e, dt, phase, dx, dy, dist); break;
        case 1: boss_geant     (g, e, dt, phase, dx, dy, dist); break;
        case 2: boss_hydre     (g, e, dt, phase, dx, dy, dist); break;
        case 3: boss_forgeron  (g, e, dt, phase, dx, dy, dist); break;
        case 4: boss_avatar    (g, e, dt, phase, dx, dy, dist); break;
        default: break;
    }
    /* contact dmg de base, scale avec la phase pour eviter l etreinte
     * impossible en phase 2. */
    float diff = powf(1.15f, (float)(g->floor_index - 1));
    ai_contact_damage(g, e, (14.f + phase * 4.f) * diff);
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
    /* BUFFER aura : si dmg_flat > 0 (set par ai_buffer pour les voisins),
     * on amplifie. Decay rapide pour qu il faille rester pres du totem. */
    if (e->dmg_flat > 0.f) {
        dmg *= (1.f + e->dmg_flat);
        e->dmg_flat *= 0.92f;     /* decay -- l aura s estompe si on s eloigne */
        if (e->dmg_flat < 0.02f) e->dmg_flat = 0.f;
    }
    float pdx = p->x - e->x;
    float pdy = p->y - e->y;
    float pd = sqrtf(pdx * pdx + pdy * pdy);
    if (pd < e->r + p->r && p->invuln_t <= 0.f && p->dash_t <= 0.f) {
        player_take_damage_from(g, dmg, e->x, e->y);
        float dxn = pdx / (pd + 0.01f);
        float dyn = pdy / (pd + 0.01f);
        p->x += dxn * 6.f;
        p->y += dyn * 6.f;
    }
}

/* ZOMBIE : lent par defaut, mais 2 mecaniques signature :
 *   1. cluster speed : +5 speed par voisin zombie dans 50px (cap 4 -> +20),
 *      donne la sensation de "horde qui converge".
 *   2. lunge : si player < 24px, telegraphe 0.35s puis bondit sur le joueur
 *      avec un knockback impose + 12 dmg. ai_t2 = 0 normal, > 0 lunge timer. */
static int count_zombie_cluster(Game *g, Enemy *self) {
    int n = 0;
    for (int i = 0; i < MAX_ENEMIES; i++) {
        Enemy *o = &g->enemies[i];
        if (o == self || !o->alive || o->kind != EK_ZOMBIE) continue;
        float dx = o->x - self->x, dy = o->y - self->y;
        if (dx*dx + dy*dy < 50.f * 50.f) n++;
    }
    if (n > 4) n = 4;
    return n;
}

static void ai_zombie(Game *g, Enemy *e, float dt, float dx, float dy, float dist) {
    float diff = powf(1.15f, (float)(g->floor_index - 1));
    e->ai_t += dt;
    /* lunge state machine */
    if (e->ai_t2 > 0.f) {
        e->ai_t2 -= dt;
        /* premiere moitie = telegraph (ne bouge pas), seconde moitie = bond */
        if (e->ai_t2 > 0.18f) {
            /* telegraph : sparks rouges */
            if ((rand() % 100) < 50) {
                particle_spawn_kind(g, e->x, e->y, 0, -25,
                                    0.25f, 0xAA3333FF, 1.8f, 2);
            }
            ai_contact_damage(g, e, 7.f * diff);
            return;
        }
        /* bond : direction figee dans knockback_x/y au moment du trigger */
        float vx = e->knockback_x * 240.f;
        float vy = e->knockback_y * 240.f;
        ai_move_toward(g, e, dt, vx, vy, 1.f, 1.f, false);
        ai_contact_damage(g, e, 12.f * diff);
        if (e->ai_t2 <= 0.f) {
            e->knockback_x = 0; e->knockback_y = 0;
        }
        return;
    }
    /* cluster speed boost */
    int allies = count_zombie_cluster(g, e);
    float speed = 50.f + allies * 5.f;
    ai_move_toward(g, e, dt, dx, dy, dist, speed, false);
    /* trigger lunge */
    if (dist < 26.f && e->ai_t > 1.0f) {
        e->ai_t = 0.f;
        e->ai_t2 = 0.55f;
        /* freeze direction */
        e->knockback_x = dx / dist;
        e->knockback_y = dy / dist;
        sfx_play(g, SFX_HIT);
    }
    ai_contact_damage(g, e, 7.f * diff);
}

/* SLIME : hop discret + petite flaque d acide pose en l air a chaque
 * "atterrissage" (quand le hop multiplier est minimum). La flaque damage
 * sur passage. Garde le split a la mort (deja gere dans enemy_take_damage). */
static void ai_slime(Game *g, Enemy *e, int i, float dt, float dx, float dy, float dist) {
    float diff = powf(1.15f, (float)(g->floor_index - 1));
    float hop_sin = sinf(g->time * 6.f + i);
    float hop = 0.6f + 0.4f * hop_sin;
    ai_move_toward(g, e, dt, dx, dy, dist, 95.f * hop, false);
    /* drop d'acide quand le hop est au plus bas (atterrissage). On utilise
     * ai_t2 comme cooldown pour eviter de poser une flaque par frame. */
    e->ai_t2 -= dt;
    if (hop_sin < -0.85f && e->ai_t2 <= 0.f) {
        e->ai_t2 = 0.6f;
        for (int k = 0; k < 5; k++) {
            float a = (rand() % 360) * 0.01745f;
            particle_spawn_kind(g, e->x, e->y,
                                cosf(a) * 8.f, sinf(a) * 8.f - 4.f,
                                1.30f, 0x80B040A0, 2.0f, 0);
        }
        Player *p = &g->player;
        float pdx = p->x - e->x, pdy = p->y - e->y;
        if (pdx*pdx + pdy*pdy < 18.f * 18.f &&
            p->invuln_t <= 0.f && p->dash_t <= 0.f) {
            player_take_damage_from(g, 2.f * diff, e->x, e->y);
        }
    }
    ai_contact_damage(g, e, 5.f * diff);
}

/* BANDIT : kite + 3-shot spread + roll lateral occasionnel pour
 * repositionner et eviter les attaques melee. */
static void ai_bandit(Game *g, Enemy *e, float dt, float dx, float dy, float dist) {
    float diff = powf(1.15f, (float)(g->floor_index - 1));
    e->ai_t += dt;
    /* roll lateral toutes les 4s si le joueur est proche (< 130px) */
    if (e->ai_t2 > 0.f) {
        e->ai_t2 -= dt;
        /* roll : vitesse perpendiculaire forte */
        float perp_x = -dy / dist, perp_y = dx / dist;
        if ((int)(e->facing * 13.f) & 1) { perp_x = -perp_x; perp_y = -perp_y; }
        ai_move_toward(g, e, dt, perp_x, perp_y, 1.f, 180.f, false);
        if ((rand() % 100) < 40) {
            particle_spawn_kind(g, e->x, e->y, (rand()%30)-15, -10,
                                0.30f, 0x60504040, 1.5f, 0);
        }
    } else {
        /* kite : ne s approche pas en deca de 100 px */
        if (dist > 100.f) ai_move_toward(g, e, dt, dx, dy, dist, 35.f, false);
        if (dist < 130.f && (rand() % 600) < 5) {
            e->ai_t2 = 0.35f;     /* declenche un roll */
        }
    }
    /* triple shot : 3 daguettes en eventail */
    if (e->ai_t > 1.6f && dist < 220.f && e->ai_t2 <= 0.f) {
        e->ai_t = 0;
        for (int k = -1; k <= 1; k++) {
            float a0 = atan2f(dy, dx);
            float a = a0 + k * 0.20f;
            Projectile pr = {0};
            pr.x = e->x; pr.y = e->y;
            pr.vx = cosf(a) * 130.f;
            pr.vy = sinf(a) * 130.f;
            pr.life = 2.5f; pr.r = 3.f;
            pr.dmg = 6.f * diff; pr.owner = 1; pr.reflectable = true;
            pr.primary = EL_VOID;
            combo_apply_to_enemy_projectile(e->combo_mask, &pr);
            projectile_spawn(g, pr);
        }
        sfx_play(g, SFX_SHOOT);
    }
    ai_contact_damage(g, e, 7.f * diff);
}

/* DEMON : kite + 3-shot AIME (plus lisible que l ancien radial 8 qui
 * remplissait l ecran). Laisse une trace de feu sous lui qui ralentit le
 * joueur s'il marche dedans (visuel + dmg sur contact via particules). */
static void ai_demon(Game *g, Enemy *e, float dt, float dx, float dy, float dist) {
    float diff = powf(1.15f, (float)(g->floor_index - 1));
    if (dist > 100.f) ai_move_toward(g, e, dt, dx, dy, dist, 30.f, false);
    e->ai_t += dt;
    /* trace de feu : flamme statique posee tous les 0.30s, joue le role
     * d'AOE persistant + signal visuel "ici il a marche". */
    if (e->ai_t2 <= 0.f) {
        e->ai_t2 = 0.30f;
        for (int k = 0; k < 4; k++) {
            float a = (rand() % 360) * 0.01745f;
            particle_spawn_kind(g, e->x, e->y,
                                cosf(a) * 6.f, sinf(a) * 6.f - 5.f,
                                1.20f, 0xFF6020A0, 2.2f, 0);
        }
        /* mini-dmg de zone si le joueur est tres proche du pied */
        Player *p = &g->player;
        float pdx = p->x - e->x, pdy = p->y - e->y;
        if (pdx*pdx + pdy*pdy < 14.f * 14.f &&
            p->invuln_t <= 0.f && p->dash_t <= 0.f) {
            player_take_damage_from(g, 2.f * diff, e->x, e->y);
        }
    } else {
        e->ai_t2 -= dt;
    }
    /* triple aim shot */
    if (e->ai_t > 1.4f && dist < 260.f) {
        e->ai_t = 0;
        float a0 = atan2f(dy, dx);
        for (int k = -1; k <= 1; k++) {
            float a = a0 + k * 0.18f;
            Projectile pr = {0};
            pr.x = e->x; pr.y = e->y;
            pr.vx = cosf(a) * 120.f;
            pr.vy = sinf(a) * 120.f;
            pr.life = 3.f; pr.r = 3.5f;
            pr.dmg = 9.f * diff; pr.owner = 1; pr.reflectable = true;
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

/* HEALER : "Hierophante". Kite a 140 px, et toutes les 1.5s emet une
 * pulse de heal qui restaure 8 PV (* diff) aux ennemis dans 80 px.
 * Pas de degats direct (faible contact). Cible prioritaire pour le
 * joueur car amplifie tous les autres. */
static void ai_healer(Game *g, Enemy *e, float dt, float dx, float dy, float dist) {
    float diff = powf(1.15f, (float)(g->floor_index - 1));
    if (dist > 160.f)      ai_move_toward(g, e, dt, dx, dy, dist, 28.f, false);
    else if (dist < 120.f) ai_move_toward(g, e, dt, -dx, -dy, dist, 30.f, false);
    e->ai_t += dt;
    if (e->ai_t > 1.5f) {
        e->ai_t = 0.f;
        /* particules de halo dore */
        for (int k = 0; k < 18; k++) {
            float a = (k / 18.f) * 6.2831f;
            particle_spawn_kind(g, e->x, e->y,
                                cosf(a) * 60.f, sinf(a) * 60.f,
                                0.45f, 0xFFE890C0, 2.3f, 0);
        }
        /* heal pulse */
        for (int j = 0; j < MAX_ENEMIES; j++) {
            Enemy *o = &g->enemies[j];
            if (o == e || !o->alive || o->dying_t > 0.f) continue;
            float ddx = o->x - e->x, ddy = o->y - e->y;
            if (ddx*ddx + ddy*ddy < 80.f * 80.f) {
                float h = 8.f * diff;
                o->hp += h;
                if (o->hp > o->maxhp) o->hp = o->maxhp;
                /* damage number vert pour signaler le heal */
                dmgnum_spawn(g, o->x, o->y - o->r, (int)h, 0x80FF80FF, false);
            }
        }
        sfx_play(g, SFX_LEVELUP);
    }
    ai_contact_damage(g, e, 4.f * diff);
}

/* BUFFER : totem statique. Ne bouge pas. Emet une aura visible qui
 * boost les degats des ennemis voisins de +30%. L impl est passive
 * cote AI : on stocke le boost dans une variable globale lue par les
 * autres ai_<kind> (cf calcul). Simplifie : on applique un mini "buff"
 * timer aux voisins -> melee enemies tapent plus fort si proches. */
static void ai_buffer(Game *g, Enemy *e, float dt, float dx, float dy, float dist) {
    float diff = powf(1.15f, (float)(g->floor_index - 1));
    (void)dx; (void)dy; (void)dist;
    e->ai_t += dt;
    /* aura visuelle continue : anneau acier */
    if ((rand() % 100) < 35) {
        float a = (rand() % 360) * 0.01745f;
        particle_spawn_kind(g, e->x + cosf(a) * 24.f,
                            e->y + sinf(a) * 24.f,
                            0, -4.f,
                            0.55f, 0xC0C8D0C0, 1.8f, 0);
    }
    /* applique le buff aux voisins via fire_dot a 0 mais hit_flash bref :
     * en pratique on les marque avec hit_flash = 0.05 pour les "highlighter"
     * dans le rendu. Le boost de degats reel se fait dans world_enemy_damage
     * lookup -> non, ce serait invasif. Plus simple : laisse l aura visuelle
     * + leger boost local applique en augmentant la stat dmg_flat des voisins
     * uniquement quand ils tirent (ils lisent e->dmg_flat). */
    if (e->ai_t > 0.40f) {
        e->ai_t = 0.f;
        for (int j = 0; j < MAX_ENEMIES; j++) {
            Enemy *o = &g->enemies[j];
            if (o == e || !o->alive || o->dying_t > 0.f) continue;
            float ddx = o->x - e->x, ddy = o->y - e->y;
            if (ddx*ddx + ddy*ddy < 90.f * 90.f) {
                o->dmg_flat = 0.30f;     /* +30% applique cote calcul de proj */
                /* hit_flash leger pour aura jaune transitoire */
                if (o->hit_flash < 0.10f) o->hit_flash = 0.10f;
            }
        }
    }
    /* contact dmg legerement plus fort pour pas etre passif total */
    ai_contact_damage(g, e, 6.f * diff);
}

/* NECROMANCER : kite tres lent, et toutes les 5s raise un EK_ZOMBIE
 * dans 30 px. Cappe a 2 zombies actifs vivants raise par ce necro
 * (compte global pour simplifier). */
static void ai_necromancer(Game *g, Enemy *e, float dt, float dx, float dy, float dist) {
    float diff = powf(1.15f, (float)(g->floor_index - 1));
    if (dist > 200.f)      ai_move_toward(g, e, dt, dx, dy, dist, 20.f, false);
    else if (dist < 120.f) ai_move_toward(g, e, dt, -dx, -dy, dist, 20.f, false);
    e->ai_t += dt;
    if (e->ai_t > 5.0f) {
        e->ai_t = 0.f;
        /* compte zombies vivants */
        int z = 0;
        for (int j = 0; j < MAX_ENEMIES; j++)
            if (g->enemies[j].alive && g->enemies[j].kind == EK_ZOMBIE) z++;
        if (z < 2) {
            int idx = enemy_spawn(g, EK_ZOMBIE,
                                  e->x + (rand() % 40) - 20,
                                  e->y + (rand() % 40) - 20);
            (void)idx;
            /* particules d invocation : volutes noires */
            for (int k = 0; k < 14; k++) {
                float a = (rand() % 360) * 0.01745f;
                particle_spawn_kind(g, e->x, e->y,
                                    cosf(a) * 50.f, sinf(a) * 50.f - 20.f,
                                    0.55f, 0x402060FF, 2.2f, 0);
            }
            sfx_play(g, SFX_BOSS);
        }
    }
    /* tir occasionnel : void bolt droit */
    e->ai_t2 += dt;
    if (e->ai_t2 > 2.0f && dist < 220.f) {
        e->ai_t2 = 0.f;
        Projectile pr = {0};
        pr.x = e->x; pr.y = e->y;
        pr.vx = dx / dist * 95.f;
        pr.vy = dy / dist * 95.f;
        pr.life = 3.f; pr.r = 3.f;
        pr.dmg = 8.f * diff; pr.owner = 1; pr.reflectable = true;
        pr.primary = EL_DARK;
        combo_apply_to_enemy_projectile(e->combo_mask, &pr);
        projectile_spawn(g, pr);
        sfx_play(g, SFX_SHOOT);
    }
    ai_contact_damage(g, e, 5.f * diff);
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
        case EK_MAGE:        ai_mage       (g, e,    dt, dx, dy, dist); break;
        case EK_HEALER:      ai_healer     (g, e,    dt, dx, dy, dist); break;
        case EK_BUFFER:      ai_buffer     (g, e,    dt, dx, dy, dist); break;
        case EK_NECROMANCER: ai_necromancer(g, e,    dt, dx, dy, dist); break;
        default: break;
    }
}

void update_enemies(Game *g) {
    float dt = g->dt;
    for (int i = 0; i < MAX_ENEMIES; i++) {
        Enemy *e = &g->enemies[i];
        if (!e->alive) continue;
        if (e->hit_flash > 0.f) e->hit_flash -= dt;
        if (e->expose_t > 0.f)  e->expose_t  -= dt;
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
