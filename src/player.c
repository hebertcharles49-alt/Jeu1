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

void player_take_damage(Game *g, float dmg) {
    Player *p = &g->player;
    if (p->invuln_t > 0.f || p->dash_t > 0.f) return;
    if (p->dodge > 0.f && (rand() / (float)RAND_MAX) < p->dodge) {
        dmgnum_spawn(g, p->x, p->y, 0, 0xC0FFFFFF, false);
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

/* ---- Pickup handlers ---- */
static void on_pickup_collect(Game *g, Pickup *pk) {
    Player *p = &g->player;
    switch (pk->kind) {
        case PU_XP:    p->xp += 1; sfx_play(g, SFX_PICKUP); break;
        case PU_HEART: p->hp += 18.f; if (p->hp > p->maxhp) p->hp = p->maxhp;
                       sfx_play(g, SFX_PICKUP); break;
        case PU_SOUL:  p->souls += 1; sfx_play(g, SFX_PICKUP); break;
        case PU_COIN:  p->coins += pk->value > 0 ? pk->value : 1;
                       sfx_play(g, SFX_COIN); break;
        case PU_ELEMENT: {
            Element e = (Element)pk->value;
            Weapon *w = &p->weapons[p->active_weapon];
            weapon_attach_element(w, e);
            sfx_play(g, SFX_LEVELUP);
            if (e > 0 && e < EL_COUNT && !g->meta.element_discovered[e]) {
                g->meta.element_discovered[e] = true;
                save_write(&g->meta);
                char buf[48];
                snprintf(buf, sizeof(buf), "ELEMENT DECOUVERT : %s",
                         element_name(e));
                toast_push(g, buf, element_color(e), 4.0f);
            }
            int mask = weapon_combo_id(w);
            if (mask != 0 && !meta_combo_is_seen(&g->meta, mask)) {
                meta_combo_mark(&g->meta, mask);
                save_write(&g->meta);
                char buf[48];
                snprintf(buf, sizeof(buf), "COMBO : %s", combo_name(mask));
                toast_push(g, buf, combo_color(mask), 4.0f);
            }
            break;
        }
        case PU_WEAPON: {
            /* value packe : kind (8 bits bas) + rarity (8 bits suivants) */
            int kind   = pk->value & 0xFF;
            int rarity = (pk->value >> 8) & 0xFF;
            if (rarity < 0 || rarity >= R_COUNT) rarity = R_COMMON;
            int slot = -1;
            for (int s = 0; s < WEAPON_SLOTS; s++)
                if (p->weapons[s].kind == W_FISTS) { slot = s; break; }
            if (slot < 0) slot = p->active_weapon;
            weapon_init_defaults(&p->weapons[slot], (WeaponKind)kind);
            p->weapons[slot].rarity = (Rarity)rarity;
            p->weapons[slot].owned = true;
            p->active_weapon = slot;
            sfx_play(g, SFX_LEVELUP);
            if (kind > 0 && kind < W_COUNT && !g->meta.weapon_discovered[kind]) {
                g->meta.weapon_discovered[kind] = true;
                save_write(&g->meta);
                char buf[48];
                snprintf(buf, sizeof(buf), "ARME DECOUVERTE : %s",
                         weapon_name((WeaponKind)kind));
                toast_push(g, buf, 0xC0E0FFFF, 4.0f);
            }
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
            /* parchemin de lore : affiche un extrait 6s (Darkest-D-like). */
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
            p->dash_cd = 0.7f;
            p->dash_t = (p->hero == HERO_VOLEUR) ? 0.22f : 0.18f;
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

    /* regen */
    if (p->regen_per_sec > 0.f && p->hp < p->maxhp) {
        p->regen_acc += p->regen_per_sec * dt;
        while (p->regen_acc >= 1.f) { p->hp += 1.f; p->regen_acc -= 1.f; }
        if (p->hp > p->maxhp) p->hp = p->maxhp;
    }

    /* pickup pull / collect : aimant a loot, rayons differents selon le
     * kind (xp/coins genereux, items prudents). */
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
                pull = 130.f; vmag = 220.f; break;
            case PU_PORTAL:
                pull = 0.f;   vmag = 0.f;  break;
            default:
                pull = 60.f;  vmag = 140.f; break;
        }
        if (pull > 0.f && d2 < pull * pull) {
            float d = sqrtf(d2) + 0.01f;
            pk->x += ddx / d * vmag * dt;
            pk->y += ddy / d * vmag * dt;
        }
        if (d2 < 12.f * 12.f) on_pickup_collect(g, pk);
        if (g->state == GS_SHOP) return;   /* portail change l'etat */
    }
    /* feedback loop : decroit en dehors du combat */
    loop_decay(g, g->dt);
}
