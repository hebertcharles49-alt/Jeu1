/*
 * main.c - point d'entrée et boucle principale
 */
#include "game.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static Game g_game;

Game *game_get(void) { return &g_game; }

static void poll_input(Game *g, bool *quit) {
    SDL_Event ev;
    g->mouse_btn_prev = g->mouse_btn;
    memcpy(g->keys_prev, g->keys, SDL_NUM_SCANCODES);
    while (SDL_PollEvent(&ev)) {
        if (ev.type == SDL_QUIT) { *quit = true; }
        if (ev.type == SDL_KEYDOWN && ev.key.keysym.scancode == SDL_SCANCODE_ESCAPE) {
            if (g->state == GS_RUN) g->state = GS_HUB; /* abandon run */
            else if (g->state == GS_HUB || g->state == GS_TITLE) *quit = true;
            else if (g->state == GS_HELP) g->state = GS_TITLE;
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
    float m = g->shake_mag * (g->shake_t);
    *ox = (int)((rand() / (float)RAND_MAX - 0.5f) * 2.f * m);
    *oy = (int)((rand() / (float)RAND_MAX - 0.5f) * 2.f * m);
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
    if (!g->meta.weapon_unlocked[W_SWORD]) {
        /* first launch: starter kit */
        g->meta.weapon_unlocked[W_SWORD] = true;
    }
    g->state = GS_TITLE;
    srand((unsigned)time(NULL));
}

void game_shutdown(Game *g) {
    save_write(&g->meta);
    if (g->target)   SDL_DestroyTexture(g->target);
    if (g->renderer) SDL_DestroyRenderer(g->renderer);
    if (g->window)   SDL_DestroyWindow(g->window);
    SDL_Quit();
}

void game_to_hub(Game *g) {
    /* convert run rewards to shards */
    int gained = g->player.souls + g->run_kills / 4 + g->floor_index * 5;
    g->meta.shards += gained;
    g->meta.total_runs++;
    if (g->floor_index > g->meta.best_level) g->meta.best_level = g->floor_index;
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
    g->run_kills = 0;
    g->run_time = 0.f;
    g->floor_index = 1;
    g->shake_t = 0.f;

    Player *p = &g->player;
    p->maxhp = 100; p->hp = 100;
    p->speed = 110.f;
    p->r = 6.f;
    p->level = 1; p->xp = 0; p->xp_to_next = 6;
    p->souls = 0;
    p->active_weapon = 0;

    /* equip every unlocked weapon */
    int slot = 0;
    for (int k = 0; k < W_COUNT && slot < WEAPON_SLOTS; k++) {
        if (g->meta.weapon_unlocked[k]) {
            weapon_init_defaults(&p->weapons[slot], (WeaponKind)k);
            p->weapons[slot].owned = true;
            slot++;
        }
    }
    /* if nothing unlocked (shouldn't happen), give sword */
    if (slot == 0) {
        weapon_init_defaults(&p->weapons[0], W_SWORD);
        p->weapons[0].owned = true;
    }

    dungeon_generate(&g->dungeon, g->floor_index, (unsigned)rand());
    p->x = g->dungeon.spawn_x * TILE + TILE / 2;
    p->y = g->dungeon.spawn_y * TILE + TILE / 2;
    g->state = GS_RUN;
}

static void next_floor(Game *g) {
    g->floor_index++;
    dungeon_generate(&g->dungeon, g->floor_index, (unsigned)rand());
    g->player.x = g->dungeon.spawn_x * TILE + TILE / 2;
    g->player.y = g->dungeon.spawn_y * TILE + TILE / 2;
    /* clear in-flight stuff */
    memset(g->enemies, 0, sizeof(g->enemies));
    memset(g->projectiles, 0, sizeof(g->projectiles));
    memset(g->pickups, 0, sizeof(g->pickups));
    memset(g->fairies, 0, sizeof(g->fairies));
    /* small heal between floors */
    g->player.hp += 20.f;
    if (g->player.hp > g->player.maxhp) g->player.hp = g->player.maxhp;
}

/* check exit reached */
static void check_exit(Game *g) {
    int tx = (int)(g->player.x / TILE);
    int ty = (int)(g->player.y / TILE);
    if (tx < 0 || ty < 0 || tx >= MAP_W || ty >= MAP_H) return;
    if (g->dungeon.tiles[ty][tx] == T_EXIT) {
        next_floor(g);
    }
}

/* hub navigation */
static void update_hub(Game *g) {
    /* W/S or arrows to navigate, ENTER to confirm, R to start run */
    if (g->keys[SDL_SCANCODE_W] && !g->keys_prev[SDL_SCANCODE_W])
        g->hub_cursor = (g->hub_cursor + (W_COUNT + EL_COUNT - 1)) % (W_COUNT + EL_COUNT);
    if (g->keys[SDL_SCANCODE_S] && !g->keys_prev[SDL_SCANCODE_S])
        g->hub_cursor = (g->hub_cursor + 1) % (W_COUNT + EL_COUNT);
    if (g->keys[SDL_SCANCODE_UP] && !g->keys_prev[SDL_SCANCODE_UP])
        g->hub_cursor = (g->hub_cursor + (W_COUNT + EL_COUNT - 1)) % (W_COUNT + EL_COUNT);
    if (g->keys[SDL_SCANCODE_DOWN] && !g->keys_prev[SDL_SCANCODE_DOWN])
        g->hub_cursor = (g->hub_cursor + 1) % (W_COUNT + EL_COUNT);

    if ((g->keys[SDL_SCANCODE_RETURN] && !g->keys_prev[SDL_SCANCODE_RETURN]) ||
        (g->keys[SDL_SCANCODE_E] && !g->keys_prev[SDL_SCANCODE_E])) {
        int idx = g->hub_cursor;
        if (idx < W_COUNT) {
            int cost = 30 + idx * 20;
            if (!g->meta.weapon_unlocked[idx] && g->meta.shards >= cost) {
                g->meta.shards -= cost;
                g->meta.weapon_unlocked[idx] = true;
                save_write(&g->meta);
            }
        } else {
            int e = idx - W_COUNT;
            if (e >= 1 && e < EL_COUNT) {
                int cost = 25 + e * 15;
                if (!g->meta.element_unlocked[e] && g->meta.shards >= cost) {
                    g->meta.shards -= cost;
                    g->meta.element_unlocked[e] = true;
                    save_write(&g->meta);
                }
            }
        }
    }
    if (g->keys[SDL_SCANCODE_R] && !g->keys_prev[SDL_SCANCODE_R]) {
        game_start_new_run(g);
    }
    if (g->keys[SDL_SCANCODE_H] && !g->keys_prev[SDL_SCANCODE_H]) {
        g->state = GS_HELP;
    }
}

static void update_levelup(Game *g) {
    if (g->keys[SDL_SCANCODE_1] && !g->keys_prev[SDL_SCANCODE_1]) {
        /* apply choice 0 */
        int kind = g->levelup_choice_kind[0];
        int val  = g->levelup_choices[0];
        if (kind == 1) { /* element */
            Weapon *w = &g->player.weapons[g->player.active_weapon];
            weapon_attach_element(w, (Element)val);
        } else if (kind == 2) { /* stat */
            if (val == 0) g->player.maxhp += 20.f, g->player.hp += 20.f;
            else if (val == 1) g->player.speed += 10.f;
            else if (val == 2) {
                for (int i = 0; i < WEAPON_SLOTS; i++)
                    if (g->player.weapons[i].owned)
                        g->player.weapons[i].base_dmg *= 1.15f;
            }
        }
        g->state = GS_RUN;
    }
    if (g->keys[SDL_SCANCODE_2] && !g->keys_prev[SDL_SCANCODE_2]) {
        int kind = g->levelup_choice_kind[1];
        int val  = g->levelup_choices[1];
        if (kind == 1) {
            Weapon *w = &g->player.weapons[g->player.active_weapon];
            weapon_attach_element(w, (Element)val);
        } else if (kind == 2) {
            if (val == 0) g->player.maxhp += 20.f, g->player.hp += 20.f;
            else if (val == 1) g->player.speed += 10.f;
            else if (val == 2) {
                for (int i = 0; i < WEAPON_SLOTS; i++)
                    if (g->player.weapons[i].owned)
                        g->player.weapons[i].base_dmg *= 1.15f;
            }
        }
        g->state = GS_RUN;
    }
    if (g->keys[SDL_SCANCODE_3] && !g->keys_prev[SDL_SCANCODE_3]) {
        int kind = g->levelup_choice_kind[2];
        int val  = g->levelup_choices[2];
        if (kind == 1) {
            Weapon *w = &g->player.weapons[g->player.active_weapon];
            weapon_attach_element(w, (Element)val);
        } else if (kind == 2) {
            if (val == 0) g->player.maxhp += 20.f, g->player.hp += 20.f;
            else if (val == 1) g->player.speed += 10.f;
            else if (val == 2) {
                for (int i = 0; i < WEAPON_SLOTS; i++)
                    if (g->player.weapons[i].owned)
                        g->player.weapons[i].base_dmg *= 1.15f;
            }
        }
        g->state = GS_RUN;
    }
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
        g->dt = dt;
        g->time += dt;

        poll_input(g, &quit);

        if (g->state == GS_TITLE) {
            if ((g->keys[SDL_SCANCODE_RETURN] && !g->keys_prev[SDL_SCANCODE_RETURN]) ||
                (g->keys[SDL_SCANCODE_SPACE] && !g->keys_prev[SDL_SCANCODE_SPACE])) {
                g->state = GS_HUB;
            }
            if (g->keys[SDL_SCANCODE_H] && !g->keys_prev[SDL_SCANCODE_H]) {
                g->state = GS_HELP;
            }
        } else if (g->state == GS_HELP) {
            /* nothing, ESC handled in poll */
        } else if (g->state == GS_HUB) {
            update_hub(g);
        } else if (g->state == GS_RUN) {
            g->run_time += dt;
            update_player(g);
            update_weapons(g);
            update_enemies(g);
            update_projectiles(g);
            update_pickups(g);
            update_fairies(g);
            update_particles(g);
            update_room_logic(g);
            check_exit(g);

            if (g->shake_t > 0.f) g->shake_t -= dt;
            if (g->player.hp <= 0.f) {
                g->state = GS_DEAD;
            }
            /* level up trigger */
            if (g->player.xp >= g->player.xp_to_next) {
                g->player.xp -= g->player.xp_to_next;
                g->player.xp_to_next = (int)(g->player.xp_to_next * 1.4f) + 1;
                g->player.level++;
                /* generate 3 random choices */
                int idx = 0;
                /* always offer +HP and +speed and +damage as fallback stats */
                int statpool[3] = {0, 1, 2};
                /* shuffle */
                for (int i = 2; i > 0; i--) {
                    int j = rand() % (i + 1);
                    int t = statpool[i]; statpool[i] = statpool[j]; statpool[j] = t;
                }
                /* element offers */
                int elpool[8]; int elcount = 0;
                for (int e = 1; e < EL_COUNT; e++) {
                    if (g->meta.element_unlocked[e]) elpool[elcount++] = e;
                }
                for (int i = elcount - 1; i > 0; i--) {
                    int j = rand() % (i + 1);
                    int t = elpool[i]; elpool[i] = elpool[j]; elpool[j] = t;
                }
                int el_used = 0;
                for (int c = 0; c < 3; c++) {
                    int r = rand() % 100;
                    if (r < 60 && el_used < elcount) {
                        g->levelup_choice_kind[c] = 1;
                        g->levelup_choices[c] = elpool[el_used++];
                    } else {
                        g->levelup_choice_kind[c] = 2;
                        g->levelup_choices[c] = statpool[idx++ % 3];
                    }
                }
                g->state = GS_LEVELUP;
            }
        } else if (g->state == GS_LEVELUP) {
            update_levelup(g);
        } else if (g->state == GS_DEAD) {
            if (g->keys[SDL_SCANCODE_RETURN] && !g->keys_prev[SDL_SCANCODE_RETURN]) {
                game_to_hub(g);
            }
        }

        /* render to internal target */
        SDL_SetRenderTarget(g->renderer, g->target);
        SDL_SetRenderDrawColor(g->renderer, 12, 10, 18, 255);
        SDL_RenderClear(g->renderer);

        if (g->state == GS_TITLE) {
            render_title(g);
        } else if (g->state == GS_HELP) {
            render_help(g);
        } else if (g->state == GS_HUB) {
            render_hub(g);
        } else {
            int sx, sy; apply_shake(g, &sx, &sy);
            g->camera_x = g->player.x - INTERNAL_W / 2 + sx;
            g->camera_y = g->player.y - INTERNAL_H / 2 + sy;
            render_world(g);
            render_hud(g);
            if (g->state == GS_LEVELUP) render_levelup(g);
            if (g->state == GS_DEAD)    render_dead(g);
        }

        /* present */
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
