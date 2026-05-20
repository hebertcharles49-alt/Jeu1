/*
 * player.c - update_player (mouvement + dash + visee + pickup
 * collection) + player_take_damage. Cf world_internal.h pour aabb_solid.
 *
 * La logique "pick up un PU_*" est ici parce qu'elle est declenchee par
 * la collision joueur-pickup, pas par le pickup lui-meme. La duree de vie
 * des pickups (hover_t) est dans world.c.
 */
#include "game.h"
#include "gfx.h"
#include "world_internal.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

void player_take_damage_from(Game *g, float dmg, float srcx, float srcy) {
    Player *p = &g->player;
    if (p->invuln_t > 0.f || p->dash_t > 0.f) return;

    /* direction d impact : depuis la source vers le joueur. Si la source
     * est sur le joueur (DoT), on garde la direction precedente. */
    float dxp = p->x - srcx, dyp = p->y - srcy;
    float dl = sqrtf(dxp * dxp + dyp * dyp);
    float dirx, diry;
    if (dl > 0.5f) { dirx = dxp / dl; diry = dyp / dl; }
    else           { dirx = p->hit_dir_x; diry = p->hit_dir_y;
                     if (dirx == 0.f && diry == 0.f) { dirx = 0.f; diry = 1.f; } }

    if (p->dodge > 0.f && (rand() / (float)RAND_MAX) < p->dodge) {
        dmgnum_spawn(g, p->x, p->y, 0, 0xC0FFFFFF, false);
        for (int i = 0; i < 8; i++) {
            float a = (rand() % 360) * 0.01745f;
            particle_spawn_kind(g, p->x, p->y, cosf(a) * 80, sinf(a) * 80,
                                0.3f, 0xFFFFFFFF, 1.5f, 0);
        }
        /* feedback audio leger : un swish aigu et discret */
        sfx_play_ex(g, SFX_SWING, 1.6f, 0.7f);
        if (p->u_dodge_attack) {
            p->weapons[p->active_weapon].cooldown = 0.f;
        }
        return;
    }
    float real = dmg - p->armor;
    if (real < 1.f) real = 1.f;
    /* u_last_stand : si le coup serait letal, on s arrete a 1 HP et on
     * declenche le buff x2 stats pendant 10s. 1 charge / salle. */
    if (p->u_last_stand && p->last_stand_charge && p->hp - real <= 0.f) {
        p->last_stand_charge = false;
        p->last_stand_t = 10.0f;
        p->hp = 1.f;
        /* burst dore */
        for (int k = 0; k < 26; k++) {
            float a = (rand() % 360) * 0.01745f;
            particle_spawn_kind(g, p->x, p->y, cosf(a) * 120, sinf(a) * 120,
                                0.65f, 0xFFE890FF, 3.0f, 2);
        }
        g->shake_t = 0.30f; g->shake_mag = 6.f; g->hitstop_t = 0.20f;
        sfx_play(g, SFX_BOSS);
        toast_push(g, "DERNIER REPLI", 0xFFE890FF, 4.0f);
        return;
    }
    /* u_phoenix_revive : si letal, revis a 30% HP. 1 charge / salle. */
    if (p->u_phoenix_revive && p->phoenix_charge && p->hp - real <= 0.f) {
        p->phoenix_charge = false;
        p->hp = p->maxhp * 0.30f;
        p->invuln_t = 1.5f;
        for (int k = 0; k < 30; k++) {
            float a = (rand() % 360) * 0.01745f;
            particle_spawn_kind(g, p->x, p->y, cosf(a) * 140, sinf(a) * 140,
                                0.70f, 0xFF8040FF, 3.0f, 2);
        }
        g->shake_t = 0.45f; g->shake_mag = 7.f; g->hitstop_t = 0.30f;
        sfx_play(g, SFX_BOSS);
        toast_push(g, "RENAISSANCE", 0xFF8040FF, 4.0f);
        return;
    }
    p->hp -= real;
    p->invuln_t = 0.6f;
    /* BUILD-DEF crit_shrink */
    if (p->u_crit_shrink && p->r_base > 0.f && p->r < p->r_base) {
        p->r += 1.5f;
        if (p->r > p->r_base) p->r = p->r_base;
    }

    /* Polish (shake/hitstop/flash/knockback) scale par % maxhp, puis
     * boost a faible PV pour amplifier le sens du danger. */
    float severity = real / (p->maxhp > 1.f ? p->maxhp : 1.f);
    if (severity < 0.f) severity = 0.f;
    if (severity > 1.f) severity = 1.f;
    float danger = 1.f - (p->hp / (p->maxhp > 1.f ? p->maxhp : 1.f));
    if (danger < 0.f) danger = 0.f;
    if (danger > 1.f) danger = 1.f;
    float intensity = severity * (1.f + danger * 0.6f);
    if (intensity > 1.6f) intensity = 1.6f;

    g->shake_t   = 0.22f + intensity * 0.18f;
    g->shake_mag = 4.f   + intensity * 9.f;
    g->hitstop_t = 0.05f + intensity * 0.13f;
    g->flash_t   = 0.20f + intensity * 0.18f;

    /* knockback : substep avec collision check pour ne pas teleporter
     * a travers les murs. Decoupe en N steps de longueur <= 0.5 * r,
     * abandonne le sens collisionne. */
    float kb = 4.f + intensity * 14.f;
    float kb_dx = dirx * kb;
    float kb_dy = diry * kb;
    float max_step = p->r * 0.5f;
    int kb_steps = 1;
    float tot = sqrtf(kb_dx * kb_dx + kb_dy * kb_dy);
    if (tot > max_step) kb_steps = (int)ceilf(tot / max_step);
    if (kb_steps > 8) kb_steps = 8;
    float sx_step = kb_dx / kb_steps;
    float sy_step = kb_dy / kb_steps;
    for (int s = 0; s < kb_steps; s++) {
        if (!aabb_solid(g, p->x + sx_step, p->y, p->r - 1)) p->x += sx_step;
        else { sx_step = 0.f; }
        if (!aabb_solid(g, p->x, p->y + sy_step, p->r - 1)) p->y += sy_step;
        else { sy_step = 0.f; }
        if (sx_step == 0.f && sy_step == 0.f) break;
    }
    p->hit_t = 0.45f + intensity * 0.25f;
    p->hit_dir_x = dirx;
    p->hit_dir_y = diry;

    /* sparks rouges + dust dans la direction d impact, count scale severite. */
    int nspark = 10 + (int)(intensity * 12.f);
    for (int i = 0; i < nspark; i++) {
        float spread = (rand() / (float)RAND_MAX - 0.5f) * 2.0f;     /* +/-1 rad */
        float sa = atan2f(diry, dirx) + spread;
        float sp = 90.f + (rand() / (float)RAND_MAX) * 130.f;
        uint32_t col = (rand() % 3 == 0) ? 0xFFE040FF : 0xFF4030FF;
        particle_spawn_kind(g,
            p->x - dirx * 4.f, p->y - diry * 4.f,
            cosf(sa) * sp, sinf(sa) * sp,
            0.45f + (rand() / (float)RAND_MAX) * 0.25f,
            col,
            1.6f + (rand() / (float)RAND_MAX) * 1.4f, 0);
    }
    /* "dust kick" arriere (poussiere brun-noir) */
    for (int i = 0; i < 6; i++) {
        float spread = (rand() / (float)RAND_MAX - 0.5f) * 0.8f;
        float sa = atan2f(diry, dirx) + spread;
        particle_spawn_kind(g,
            p->x, p->y,
            cosf(sa) * 60.f, sinf(sa) * 60.f,
            0.35f, 0x603020A0, 1.8f, 0);
    }

    /* grognement pitche bas a PV faibles (~0.78x sub-30%) pour sentir le
     * danger sans changer de SFX. */
    float pitch = 1.0f - danger * 0.22f;
    float vol_h = 1.0f + intensity * 0.20f;
    sfx_play_ex(g, SFX_PLAYER_HURT, pitch, vol_h);
    /* gros coup : ajoute un thump bas pour donner du poids. Ne joue pas
     * sur les petits tics (DoT). */
    if (severity > 0.10f) {
        sfx_play_ex(g, SFX_HEAVY_HIT, 0.75f + (1.f - intensity) * 0.10f,
                    0.55f + intensity * 0.35f);
    }

    dmgnum_spawn(g, p->x, p->y, (int)real, 0xFFFF80FF, true);
    if (p->hp <= 0.f) sfx_play(g, SFX_DEATH);
}

void player_take_damage(Game *g, float dmg) {
    /* wrapper sans source : pas de knockback, sparks omnidirectionnels */
    player_take_damage_from(g, dmg, g->player.x, g->player.y);
}

/* ---- Pickup handlers ---- */
static void on_pickup_collect(Game *g, Pickup *pk) {
    Player *p = &g->player;
    switch (pk->kind) {
        case PU_XP:    p->xp += 1; sfx_play(g, SFX_PICKUP); break;
        case PU_HEART: p->hp += 18.f; if (p->hp > p->maxhp) p->hp = p->maxhp;
                       sfx_play(g, SFX_PICKUP);
                       log_push(g, 0xFF8080FF, "+18 PV"); break;
        case PU_SOUL:  p->souls += 1; sfx_play(g, SFX_PICKUP); break;
        case PU_COIN:  p->coins += pk->value > 0 ? pk->value : 1;
                       sfx_play(g, SFX_COIN); break;
        case PU_ELEMENT: {
            Element e = (Element)pk->value;
            /* Plus d'auto-greffe : depose en inventaire (ITEM_KIND_ELEMENT).
             * Le joueur greffe manuellement via E dans l'inventaire.
             * Discovery codex et combo signal sont aussi reportes a
             * l'equip pour eviter les spoilers de combo sans avoir
             * effectivement essaye le greffon. */
            Item it = (Item){0};
            it.occupied = true;
            it.kind = ITEM_KIND_ELEMENT;
            it.base_kind = (int)e;
            it.rarity = R_COMMON;
            snprintf(it.name, sizeof(it.name), "%s", element_name(e));
            inventory_pickup(g, it);
            sfx_play(g, SFX_PICKUP);
            log_push(g, element_color(e), "+ Element : %s",
                     element_name(e));
            break;
        }
        case PU_WEAPON: {
            /* value packe : kind (8 bits bas) + rarity (8 bits suivants).
             * Depose en inventaire (ITEM_KIND_WEAPON). Le joueur equipe
             * manuellement via E sur le slot inventaire. */
            int kind   = pk->value & 0xFF;
            int rarity = (pk->value >> 8) & 0xFF;
            if (rarity < 0 || rarity >= R_COUNT) rarity = R_COMMON;
            Item it = (Item){0};
            it.occupied = true;
            it.kind = ITEM_KIND_WEAPON;
            it.base_kind = kind;
            it.rarity = (Rarity)rarity;
            snprintf(it.name, sizeof(it.name), "%s",
                     weapon_name((WeaponKind)kind));
            inventory_pickup(g, it);
            sfx_play(g, SFX_PICKUP);
            log_push(g, rarity_color((Rarity)rarity),
                     "+ Arme : %s (%s)",
                     weapon_name((WeaponKind)kind),
                     rarity_name((Rarity)rarity));
            break;
        }
        case PU_CHEST:
            sfx_play(g, SFX_LEVELUP);
            /* arme bonus si le joueur a encore des poings */
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
                        Rarity wr = rarity_for_floor_elite(g->floor_index);
                        pickup_spawn(g, PU_WEAPON,
                                     wpick | (((int)wr) << 8),
                                     pk->x, pk->y - 6);
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
                else if (rr < 75) pickup_spawn(g, PU_SOUL, 0, fx, fy);
                else if (rr < 80) pickup_spawn(g, PU_SCROLL, 0, fx, fy);
                else pickup_spawn(g, PU_COIN, 2, fx, fy);
                /* Elements ne sortent plus des coffres -- ils sont
                 * scheduled via les milestones de kill (cf enemies.c). */
            }
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
            int heal = pk->value > 0 ? pk->value : 12;
            p->hp += (float)heal;
            if (p->hp > p->maxhp) p->hp = p->maxhp;
            sfx_play(g, SFX_PICKUP);
            break;
        }
        case PU_SCROLL: {
            /* parchemin de lore : affiche un extrait 6s. */
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
        case PU_SHRINE:
            /* TODO : Pacte via shop_apply_recipe(shrine_only). */
            sfx_play(g, SFX_LEVELUP);
            break;
    }
    pk->alive = false;
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

    /* aim souris : ray-cast vers plan y=0 via la matrice camera precedente. */
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
            p->dash_cd = p->u_free_dash ? 0.f : 0.7f;
            p->dash_t = (p->hero == HERO_VOLEUR) ? 0.22f : 0.18f;
            /* u_void_trail : depose un champ de Vide a la position de
             * depart du dash. 3s de slow + dmg DARK. */
            if (p->u_void_trail) {
                surface_spawn(g, SURF_SHADOW, p->x, p->y, 20.f, 3.0f);
            }
        }
    }

    float speed = p->speed * (dashing ? 3.6f : 1.f);
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

    /* particules de pas quand on bouge */
    {
        static float foot_t = 0.f;
        float vsq = p->vx * p->vx + p->vy * p->vy;
        if (vsq > 100.f) {
            foot_t += dt;
            float interval = 0.18f;
            if (foot_t > interval) {
                foot_t -= interval;
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
    if (p->hit_t    > 0.f) p->hit_t    -= dt;

    /* regen */
    /* u_last_stand : tick le buff x2 (visible via shake + particules) */
    if (p->last_stand_t > 0.f) {
        p->last_stand_t -= dt;
        if ((rand() % 100) < 30) {
            float a = (rand() % 360) * 0.01745f;
            particle_spawn_kind(g, p->x, p->y,
                                cosf(a) * 50.f, sinf(a) * 50.f,
                                0.30f, 0xFFE890FF, 1.8f, 0);
        }
    }
    /* u_crowd_regen : regen bonus = 0.1 / ennemi vivant en salle. */
    float regen_rate = p->regen_per_sec;
    if (p->u_crowd_regen) regen_rate += g->enemy_alive_count * 0.1f;
    if (regen_rate > 0.f && p->hp < p->maxhp) {
        p->regen_acc += regen_rate * dt;
        while (p->regen_acc >= 1.f) { p->hp += 1.f; p->regen_acc -= 1.f; }
        if (p->hp > p->maxhp) p->hp = p->maxhp;
    }

    /* pickup pull / collect.
     * AIMANT : seulement sur consommables (XP/COIN/SOUL/FOOD/HEART) ;
     *          les objets durs (items/armes/elements) restent au sol et
     *          demandent un ramassage manuel (touche E ou collision si
     *          autoriseE par le joueur). */
    SDL_Scancode k_interact = g->settings.keys[BIND_INTERACT];
    if (k_interact == SDL_SCANCODE_UNKNOWN) k_interact = SDL_SCANCODE_E;
    bool press_e = (g->keys[k_interact] && !g->keys_prev[k_interact]);
    for (int i = 0; i < MAX_PICKUPS; i++) {
        Pickup *pk = &g->pickups[i];
        if (!pk->alive) continue;
        float ddx = p->x - pk->x, ddy = p->y - pk->y;
        float d2 = ddx * ddx + ddy * ddy;
        bool is_soft = (pk->kind == PU_XP || pk->kind == PU_COIN ||
                        pk->kind == PU_SOUL || pk->kind == PU_FOOD ||
                        pk->kind == PU_HEART);
        bool is_hard = (pk->kind == PU_ITEM || pk->kind == PU_WEAPON ||
                        pk->kind == PU_ELEMENT);
        if (is_soft) {
            float pull = 130.f, vmag = 220.f;
            if (d2 < pull * pull) {
                float d = sqrtf(d2) + 0.01f;
                pk->x += ddx / d * vmag * dt;
                pk->y += ddy / d * vmag * dt;
            }
            if (d2 < 12.f * 12.f) on_pickup_collect(g, pk);
        } else if (is_hard) {
            /* Pas d'aimant. Ramassage manuel : E pres de l'objet. */
            if (press_e && d2 < 18.f * 18.f) on_pickup_collect(g, pk);
        } else {
            /* PU_PORTAL / PU_CHEST / PU_SCROLL / PU_SHRINE : collision
             * directe (le portail change d'etat tout seul). */
            if (d2 < 12.f * 12.f) on_pickup_collect(g, pk);
        }
        if (g->state == GS_SHOP) return;
    }
    /* feedback loop : decroit en dehors du combat */
    loop_decay(g, g->dt);

    /* BUILD-DEF u_drone_count : maintient le nombre de drones (fees
     * EL_FAE) au-dessus de u_drone_count. Refresh leur life pour
     * qu elles persistent toute la course. */
    if (p->u_drone_count > 0) {
        int alive = 0;
        for (int i = 0; i < MAX_FAIRIES; i++) {
            Fairy *f = &g->fairies[i];
            if (f->alive && f->element == EL_FAE) {
                alive++;
                f->life = 60.f;
            }
        }
        for (int k = alive; k < p->u_drone_count; k++) {
            fairy_spawn(g, p->x, p->y, EL_FAE);
        }
    }
}
