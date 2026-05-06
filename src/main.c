/*
 * main.c - boucle principale et machine d'etats
 */
#include "game.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

static Game g_game;
Game *game_get(void) { return &g_game; }

static void poll_input(Game *g, bool *quit) {
    SDL_Event ev;
    g->mouse_btn_prev = g->mouse_btn;
    memcpy(g->keys_prev, g->keys, SDL_NUM_SCANCODES);
    while (SDL_PollEvent(&ev)) {
        if (ev.type == SDL_QUIT) *quit = true;
        if (ev.type == SDL_KEYDOWN && ev.key.keysym.scancode == SDL_SCANCODE_ESCAPE) {
            switch (g->state) {
                case GS_RUN:        g->state = GS_HUB; break;
                case GS_HUB:        *quit = true; break;
                case GS_TITLE:      *quit = true; break;
                case GS_HELP:       g->state = GS_TITLE; break;
                case GS_CHOOSE_HERO:g->state = GS_HUB; break;
                case GS_INVENTORY:  g->state = g->state_prev; break;
                case GS_LEVELUP:    /* pas d'echap */ break;
                case GS_SHOP:       /* sortir = continuer */ game_next_floor(g); break;
                case GS_DEAD:       game_to_hub(g); break;
                case GS_VICTORY:    game_to_hub(g); break;
                default: break;
            }
        }
    }
    g->keys = SDL_GetKeyboardState(NULL);
    int mx, my;
    Uint32 mb = SDL_GetMouseState(&mx, &my);
    g->mouse_x = mx / WINDOW_SCALE;
    g->mouse_y = my / WINDOW_SCALE;
    g->mouse_btn = (mb & SDL_BUTTON(SDL_BUTTON_LEFT)) ? 1 : 0;
}

static void apply_shake(Game *g, int *ox, int *oy) {
    if (g->shake_t <= 0.f) { *ox = 0; *oy = 0; return; }
    float m = g->shake_mag * g->shake_t;
    *ox = (int)((rand() / (float)RAND_MAX - 0.5f) * 2.f * m);
    *oy = (int)((rand() / (float)RAND_MAX - 0.5f) * 2.f * m);
}

void game_recompute_player_stats(Game *g) {
    Player *p = &g->player;
    /* base stats from hero + meta */
    float base_maxhp = 100.f, base_speed = 110.f, base_armor = 0.f;
    float base_dmg_mul = 1.0f, base_lifesteal = 0.f, base_regen = 0.f;
    switch (p->hero) {
        case HERO_GUERRIER:  base_maxhp = 125.f; base_dmg_mul = 1.15f; break;
        case HERO_VOLEUR:    base_speed = 130.f; break;
        case HERO_MAGE:      base_maxhp = 80.f;  base_dmg_mul = 1.30f; break;
        case HERO_BERSERKER: base_maxhp = 90.f;  base_dmg_mul = 1.20f; base_lifesteal = 0.08f; break;
        case HERO_PALADIN:   base_armor = 2.f;   base_regen = 1.f; break;
        default: break;
    }
    /* meta perm bonuses */
    base_maxhp += g->meta.perm_hp;
    base_armor += g->meta.perm_armor;
    base_speed += g->meta.perm_speed;
    base_dmg_mul *= 1.f + g->meta.perm_dmg_pct / 100.f;
    /* equipped items */
    for (int s = 0; s < EQUIP_SLOTS; s++) {
        if (!p->equipped[s].occupied) continue;
        float v = p->equipped[s].stat_value;
        switch ((EquipSlot)s) {
            case SLOT_HELM:   base_maxhp   += v;          break;
            case SLOT_CHEST:  base_armor   += v;          break;
            case SLOT_LEGS:   base_speed   += v;          break;
            case SLOT_BOOTS:  /* dash cd reduc, applied in update_player */ break;
            case SLOT_BELT:   base_regen   += v;          break;
            case SLOT_GLOVES: base_dmg_mul *= (1.f + v);  break;
            default: break;
        }
    }
    /* save current hp ratio to scale */
    float ratio = (p->maxhp > 0.f) ? (p->hp / p->maxhp) : 1.f;
    p->maxhp = base_maxhp;
    p->speed = base_speed;
    p->armor = base_armor;
    p->dmg_mul = base_dmg_mul;
    p->lifesteal = base_lifesteal;
    p->regen_per_sec = base_regen;
    if (p->hp <= 0.f || ratio > 1.f) p->hp = p->maxhp;
    else                              p->hp = ratio * p->maxhp;
}

void game_init(Game *g) {
    memset(g, 0, sizeof(*g));
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        exit(1);
    }
    g->window = SDL_CreateWindow("Crucible — Doomlike Hybride",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        WINDOW_W, WINDOW_H, SDL_WINDOW_SHOWN);
    if (!g->window) { fprintf(stderr, "Win: %s\n", SDL_GetError()); exit(1); }
    g->renderer = SDL_CreateRenderer(g->window, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!g->renderer) { fprintf(stderr, "Ren: %s\n", SDL_GetError()); exit(1); }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    g->target = SDL_CreateTexture(g->renderer, SDL_PIXELFORMAT_RGBA8888,
        SDL_TEXTUREACCESS_TARGET, INTERNAL_W, INTERNAL_H);

    save_load(&g->meta);
    /* premiers debloques par defaut */
    g->meta.hero_unlocked[HERO_GUERRIER] = true;
    g->meta.weapon_unlocked[W_FISTS]  = true;
    g->meta.weapon_unlocked[W_SWORD]  = true;
    g->meta.element_unlocked[EL_FIRE] = true;

    audio_init(g);

    g->state = GS_TITLE;
    srand((unsigned)time(NULL));
}

void game_shutdown(Game *g) {
    save_write(&g->meta);
    audio_shutdown(g);
    if (g->target)   SDL_DestroyTexture(g->target);
    if (g->renderer) SDL_DestroyRenderer(g->renderer);
    if (g->window)   SDL_DestroyWindow(g->window);
    SDL_Quit();
}

void game_to_hub(Game *g) {
    int gained = g->player.souls + g->run_kills / 4 + g->floor_index * 5;
    g->meta.shards += gained;
    g->meta.total_runs++;
    if (g->floor_index > g->meta.best_floor) g->meta.best_floor = g->floor_index;
    save_write(&g->meta);
    g->state = GS_HUB;
}

void game_start_new_run(Game *g) {
    memset(&g->player, 0, sizeof(g->player));
    memset(g->enemies, 0, sizeof(g->enemies));
    memset(g->projectiles, 0, sizeof(g->projectiles));
    memset(g->particles, 0, sizeof(g->particles));
    memset(g->pickups, 0, sizeof(g->pickups));
    memset(g->fairies, 0, sizeof(g->fairies));
    memset(g->dmgnums, 0, sizeof(g->dmgnums));
    g->run_kills = 0;
    g->run_time = 0.f;
    g->floor_index = 1;
    g->shake_t = 0.f;
    g->portal_spawned = false;
    g->boss_intro_t = 0.f;

    Player *p = &g->player;
    p->hero = (HeroClass)g->hero_cursor;
    p->r = 6.f;
    p->level = 1; p->xp = 0; p->xp_to_next = 6;
    p->souls = 0; p->coins = 0;
    p->active_weapon = 0;

    /* poings sur les 2 slots */
    weapon_init_defaults(&p->weapons[0], W_FISTS); p->weapons[0].owned = true;
    weapon_init_defaults(&p->weapons[1], W_FISTS); p->weapons[1].owned = true;

    game_recompute_player_stats(g);
    p->hp = p->maxhp;

    dungeon_generate(&g->dungeon, g->floor_index, (unsigned)rand());
    p->x = g->dungeon.spawn_x * TILE + TILE / 2;
    p->y = g->dungeon.spawn_y * TILE + TILE / 2;
    g->state = GS_RUN;
}

/* shop generation */
void shop_generate(Game *g) {
    int cost[5];
    for (int i = 0; i < 5; i++) {
        ShopItem *si = &g->shop_items[i];
        memset(si, 0, sizeof(*si));
    }
    /* slot 0: heart pack */
    g->shop_items[0].kind = 0; g->shop_items[0].value = 30; g->shop_items[0].cost = 8 + g->floor_index;
    /* slot 1: armor */
    g->shop_items[1].kind = 1; g->shop_items[1].value = 1;  g->shop_items[1].cost = 10 + g->floor_index;
    /* slot 2: dmg % */
    g->shop_items[2].kind = 2; g->shop_items[2].value = 8;  g->shop_items[2].cost = 12 + g->floor_index;
    /* slot 3: random equipment for sale */
    g->shop_items[3].kind = 5;
    g->shop_items[3].item = item_drop_for_floor(g, g->floor_index, true, false);
    {
        int base_cost = 10 + g->floor_index * 4;
        g->shop_items[3].cost = base_cost * (int)(rarity_mul(g->shop_items[3].item.rarity) * 2);
    }
    /* slot 4: maxhp */
    g->shop_items[4].kind = 4; g->shop_items[4].value = 15; g->shop_items[4].cost = 14 + g->floor_index * 2;
    g->shop_cursor = 0;
    (void)cost;
}

void shop_buy(Game *g, int idx) {
    ShopItem *si = &g->shop_items[idx];
    if (si->bought) return;
    if (g->player.coins < si->cost) {
        snprintf(g->inv_msg, sizeof(g->inv_msg), "Pas assez de pieces");
        g->inv_msg_t = 1.5f;
        return;
    }
    g->player.coins -= si->cost;
    si->bought = true;
    sfx_play(g, SFX_COIN);
    switch (si->kind) {
        case 0: g->player.hp += si->value; if (g->player.hp > g->player.maxhp) g->player.hp = g->player.maxhp; break;
        case 1: g->meta.perm_armor += si->value; break;
        case 2: g->meta.perm_dmg_pct += si->value; break;
        case 4: g->meta.perm_hp += si->value; break;
        case 5: inventory_pickup(g, si->item); break;
    }
    game_recompute_player_stats(g);
    save_write(&g->meta);
}

void game_open_shop(Game *g) {
    shop_generate(g);
    g->state = GS_SHOP;
}

void game_next_floor(Game *g) {
    g->floor_index++;
    if (g->floor_index > MAX_FLOORS) {
        g->meta.victories++;
        save_write(&g->meta);
        g->state = GS_VICTORY;
        return;
    }
    dungeon_generate(&g->dungeon, g->floor_index, (unsigned)rand());
    g->player.x = g->dungeon.spawn_x * TILE + TILE / 2;
    g->player.y = g->dungeon.spawn_y * TILE + TILE / 2;
    memset(g->enemies, 0, sizeof(g->enemies));
    memset(g->projectiles, 0, sizeof(g->projectiles));
    memset(g->pickups, 0, sizeof(g->pickups));
    memset(g->fairies, 0, sizeof(g->fairies));
    g->portal_spawned = false;
    g->boss_intro_t = 0.f;
    g->player.hp += 25.f;
    if (g->player.hp > g->player.maxhp) g->player.hp = g->player.maxhp;
    g->state = GS_RUN;
}

/* hub navigation */
static void update_hub(Game *g) {
    int total = W_COUNT - 1 + (EL_COUNT - 1);   /* exclude W_FISTS and EL_NONE */
    if (g->keys[SDL_SCANCODE_W] && !g->keys_prev[SDL_SCANCODE_W])
        g->hub_cursor = (g->hub_cursor + total - 1) % total;
    if (g->keys[SDL_SCANCODE_S] && !g->keys_prev[SDL_SCANCODE_S])
        g->hub_cursor = (g->hub_cursor + 1) % total;
    if (g->keys[SDL_SCANCODE_UP] && !g->keys_prev[SDL_SCANCODE_UP])
        g->hub_cursor = (g->hub_cursor + total - 1) % total;
    if (g->keys[SDL_SCANCODE_DOWN] && !g->keys_prev[SDL_SCANCODE_DOWN])
        g->hub_cursor = (g->hub_cursor + 1) % total;
    if ((g->keys[SDL_SCANCODE_RETURN] && !g->keys_prev[SDL_SCANCODE_RETURN]) ||
        (g->keys[SDL_SCANCODE_E]      && !g->keys_prev[SDL_SCANCODE_E])) {
        if (g->hub_cursor < W_COUNT - 1) {
            int wi = g->hub_cursor + 1; /* skip fists */
            int cost = 30 + wi * 18;
            if (!g->meta.weapon_unlocked[wi] && g->meta.shards >= cost) {
                g->meta.shards -= cost;
                g->meta.weapon_unlocked[wi] = true;
                save_write(&g->meta);
            }
        } else {
            int e = g->hub_cursor - (W_COUNT - 1) + 1; /* skip EL_NONE */
            int cost = 25 + e * 12;
            if (!g->meta.element_unlocked[e] && g->meta.shards >= cost) {
                g->meta.shards -= cost;
                g->meta.element_unlocked[e] = true;
                save_write(&g->meta);
            }
        }
    }
    if (g->keys[SDL_SCANCODE_R] && !g->keys_prev[SDL_SCANCODE_R]) {
        g->state = GS_CHOOSE_HERO;
    }
    if (g->keys[SDL_SCANCODE_H] && !g->keys_prev[SDL_SCANCODE_H]) {
        g->state = GS_HELP;
    }
}

static void update_choose_hero(Game *g) {
    if (g->keys[SDL_SCANCODE_LEFT]  && !g->keys_prev[SDL_SCANCODE_LEFT])
        g->hero_cursor = (g->hero_cursor + HERO_COUNT - 1) % HERO_COUNT;
    if (g->keys[SDL_SCANCODE_RIGHT] && !g->keys_prev[SDL_SCANCODE_RIGHT])
        g->hero_cursor = (g->hero_cursor + 1) % HERO_COUNT;
    if (g->keys[SDL_SCANCODE_A] && !g->keys_prev[SDL_SCANCODE_A])
        g->hero_cursor = (g->hero_cursor + HERO_COUNT - 1) % HERO_COUNT;
    if (g->keys[SDL_SCANCODE_D] && !g->keys_prev[SDL_SCANCODE_D])
        g->hero_cursor = (g->hero_cursor + 1) % HERO_COUNT;
    if ((g->keys[SDL_SCANCODE_RETURN] && !g->keys_prev[SDL_SCANCODE_RETURN]) ||
        (g->keys[SDL_SCANCODE_SPACE]  && !g->keys_prev[SDL_SCANCODE_SPACE])) {
        if (g->meta.hero_unlocked[g->hero_cursor]) {
            game_start_new_run(g);
        } else {
            int cost = 60 + g->hero_cursor * 25;
            if (g->meta.shards >= cost) {
                g->meta.shards -= cost;
                g->meta.hero_unlocked[g->hero_cursor] = true;
                save_write(&g->meta);
            }
        }
    }
}

static void update_levelup(Game *g) {
    for (int c = 0; c < 3; c++) {
        if (g->keys[SDL_SCANCODE_1 + c] && !g->keys_prev[SDL_SCANCODE_1 + c]) {
            int kind = g->levelup_choice_kind[c];
            int val  = g->levelup_choices[c];
            if (kind == 1) { /* element */
                Weapon *w = &g->player.weapons[g->player.active_weapon];
                weapon_attach_element(w, (Element)val);
                int mask = weapon_combo_id(w);
                bool found = false;
                for (int s = 0; s < g->meta.combo_seen_count; s++)
                    if (g->meta.combo_seen[s] == mask) { found = true; break; }
                if (!found && g->meta.combo_seen_count < 64) {
                    g->meta.combo_seen[g->meta.combo_seen_count++] = mask;
                }
            } else if (kind == 2) { /* stat */
                if      (val == 0) { g->player.maxhp += 20.f; g->player.hp += 20.f; }
                else if (val == 1) { g->player.speed += 10.f; }
                else if (val == 2) { g->player.dmg_mul *= 1.15f; }
            }
            sfx_play(g, SFX_LEVELUP);
            g->state = GS_RUN;
            return;
        }
    }
}

static void update_shop(Game *g) {
    if (g->keys[SDL_SCANCODE_LEFT]  && !g->keys_prev[SDL_SCANCODE_LEFT])
        g->shop_cursor = (g->shop_cursor + 4) % 5;
    if (g->keys[SDL_SCANCODE_RIGHT] && !g->keys_prev[SDL_SCANCODE_RIGHT])
        g->shop_cursor = (g->shop_cursor + 1) % 5;
    if (g->keys[SDL_SCANCODE_A] && !g->keys_prev[SDL_SCANCODE_A])
        g->shop_cursor = (g->shop_cursor + 4) % 5;
    if (g->keys[SDL_SCANCODE_D] && !g->keys_prev[SDL_SCANCODE_D])
        g->shop_cursor = (g->shop_cursor + 1) % 5;
    if ((g->keys[SDL_SCANCODE_RETURN] && !g->keys_prev[SDL_SCANCODE_RETURN]) ||
        (g->keys[SDL_SCANCODE_E] && !g->keys_prev[SDL_SCANCODE_E])) {
        shop_buy(g, g->shop_cursor);
    }
    if (g->keys[SDL_SCANCODE_I] && !g->keys_prev[SDL_SCANCODE_I]) {
        g->state_prev = GS_SHOP;
        g->state = GS_INVENTORY;
    }
    if ((g->keys[SDL_SCANCODE_C] && !g->keys_prev[SDL_SCANCODE_C]) ||
        (g->keys[SDL_SCANCODE_SPACE] && !g->keys_prev[SDL_SCANCODE_SPACE])) {
        game_next_floor(g);
    }
    if (g->inv_msg_t > 0.f) g->inv_msg_t -= g->dt;
}

void game_run(Game *g) {
    bool quit = false;
    Uint64 prev = SDL_GetPerformanceCounter();
    Uint64 freq = SDL_GetPerformanceFrequency();

    while (!quit) {
        Uint64 now = SDL_GetPerformanceCounter();
        float dt = (float)((now - prev) / (double)freq);
        if (dt > 0.05f) dt = 0.05f;
        prev = now;

        if (g->hitstop_t > 0.f) { g->hitstop_t -= dt; g->dt = 0.f; }
        else                    { g->dt = dt; }

        g->time += dt;

        poll_input(g, &quit);

        if (g->state == GS_TITLE) {
            if ((g->keys[SDL_SCANCODE_RETURN] && !g->keys_prev[SDL_SCANCODE_RETURN]) ||
                (g->keys[SDL_SCANCODE_SPACE] && !g->keys_prev[SDL_SCANCODE_SPACE])) {
                g->state = GS_HUB;
            }
            if (g->keys[SDL_SCANCODE_H] && !g->keys_prev[SDL_SCANCODE_H]) g->state = GS_HELP;
        } else if (g->state == GS_HELP) {
            /* esc handled */
        } else if (g->state == GS_HUB) {
            update_hub(g);
        } else if (g->state == GS_CHOOSE_HERO) {
            update_choose_hero(g);
        } else if (g->state == GS_RUN) {
            g->run_time += g->dt;
            if (g->boss_intro_t > 0.f) g->boss_intro_t -= dt;
            if (g->flash_t > 0.f)      g->flash_t -= dt;
            update_player(g);
            update_weapons(g);
            update_enemies(g);
            update_projectiles(g);
            update_pickups(g);
            update_fairies(g);
            update_particles(g);
            update_dmgnums(g);
            update_room_logic(g);
            if (g->shake_t > 0.f) g->shake_t -= dt;
            if (g->player.hp <= 0.f) g->state = GS_DEAD;
            if (g->player.xp >= g->player.xp_to_next) {
                g->player.xp -= g->player.xp_to_next;
                g->player.xp_to_next = (int)(g->player.xp_to_next * 1.4f) + 1;
                g->player.level++;
                int statpool[3] = {0, 1, 2};
                for (int i = 2; i > 0; i--) {
                    int j = rand() % (i + 1);
                    int t = statpool[i]; statpool[i] = statpool[j]; statpool[j] = t;
                }
                int elpool[8]; int elcount = 0;
                for (int e = 1; e < EL_COUNT; e++)
                    if (g->meta.element_unlocked[e]) elpool[elcount++] = e;
                for (int i = elcount - 1; i > 0; i--) {
                    int j = rand() % (i + 1);
                    int t = elpool[i]; elpool[i] = elpool[j]; elpool[j] = t;
                }
                int el_used = 0;
                for (int c = 0; c < 3; c++) {
                    int rr = rand() % 100;
                    if (rr < 60 && el_used < elcount) {
                        g->levelup_choice_kind[c] = 1;
                        g->levelup_choices[c] = elpool[el_used++];
                    } else {
                        g->levelup_choice_kind[c] = 2;
                        g->levelup_choices[c] = statpool[c % 3];
                    }
                }
                g->state = GS_LEVELUP;
                sfx_play(g, SFX_LEVELUP);
            }
            /* I = inventaire */
            if (g->keys[SDL_SCANCODE_I] && !g->keys_prev[SDL_SCANCODE_I]) {
                g->state_prev = GS_RUN;
                g->state = GS_INVENTORY;
            }
        } else if (g->state == GS_LEVELUP) {
            update_levelup(g);
            if (g->keys[SDL_SCANCODE_I] && !g->keys_prev[SDL_SCANCODE_I]) {
                g->state_prev = GS_LEVELUP;
                g->state = GS_INVENTORY;
            }
        } else if (g->state == GS_SHOP) {
            update_shop(g);
        } else if (g->state == GS_INVENTORY) {
            update_inventory_input(g);
        } else if (g->state == GS_DEAD) {
            if (g->keys[SDL_SCANCODE_RETURN] && !g->keys_prev[SDL_SCANCODE_RETURN]) {
                game_to_hub(g);
            }
        } else if (g->state == GS_VICTORY) {
            if (g->keys[SDL_SCANCODE_RETURN] && !g->keys_prev[SDL_SCANCODE_RETURN]) {
                game_to_hub(g);
            }
        }

        SDL_SetRenderTarget(g->renderer, g->target);
        SDL_SetRenderDrawColor(g->renderer, 12, 10, 18, 255);
        SDL_RenderClear(g->renderer);

        if (g->state == GS_TITLE)            render_title(g);
        else if (g->state == GS_HELP)        render_help(g);
        else if (g->state == GS_HUB)         render_hub(g);
        else if (g->state == GS_CHOOSE_HERO) render_choose_hero(g);
        else if (g->state == GS_SHOP) {
            render_shop(g);
        } else if (g->state == GS_INVENTORY) {
            /* render previous state behind */
            if (g->state_prev == GS_RUN || g->state_prev == GS_LEVELUP) {
                int sx, sy; apply_shake(g, &sx, &sy);
                g->camera_x = g->player.x - INTERNAL_W / 2 + sx;
                g->camera_y = g->player.y - INTERNAL_H / 2 + sy;
                render_world(g);
                render_hud(g);
            } else if (g->state_prev == GS_SHOP) {
                render_shop(g);
            }
            render_inventory(g);
        } else {
            int sx, sy; apply_shake(g, &sx, &sy);
            g->camera_x = g->player.x - INTERNAL_W / 2 + sx;
            g->camera_y = g->player.y - INTERNAL_H / 2 + sy;
            render_world(g);
            render_hud(g);
            if (g->state == GS_LEVELUP) render_levelup(g);
            if (g->state == GS_DEAD)    render_dead(g);
            if (g->state == GS_VICTORY) render_victory(g);
            /* white flash on hurt */
            if (g->flash_t > 0.f) {
                Uint8 alpha = (Uint8)(180.f * (g->flash_t / 0.20f));
                SDL_SetRenderDrawBlendMode(g->renderer, SDL_BLENDMODE_BLEND);
                SDL_SetRenderDrawColor(g->renderer, 255, 60, 60, alpha);
                SDL_Rect full = {0, 0, INTERNAL_W, INTERNAL_H};
                SDL_RenderFillRect(g->renderer, &full);
                SDL_SetRenderDrawBlendMode(g->renderer, SDL_BLENDMODE_NONE);
            }
        }

        SDL_SetRenderTarget(g->renderer, NULL);
        SDL_SetRenderDrawColor(g->renderer, 0, 0, 0, 255);
        SDL_RenderClear(g->renderer);
        SDL_RenderCopy(g->renderer, g->target, NULL, NULL);
        SDL_RenderPresent(g->renderer);
    }
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    Game *g = game_get();
    game_init(g);
    game_run(g);
    game_shutdown(g);
    return 0;
}
