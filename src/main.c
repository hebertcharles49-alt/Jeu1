/*
 * main.c - boucle principale et machine d'etats
 */
#include "game.h"
#include "gfx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

static Game g_game;
Game *game_get(void) { return &g_game; }

bool mouse_in_rect(Game *g, int x, int y, int w, int h) {
    return g->mouse_x >= x && g->mouse_x < x + w &&
           g->mouse_y >= y && g->mouse_y < y + h;
}
bool mouse_clicked(Game *g) {
    return g->mouse_btn && !g->mouse_btn_prev;
}

static void poll_input(Game *g, bool *quit) {
    SDL_Event ev;
    g->mouse_btn_prev = g->mouse_btn;
    if (g->keys) memcpy(g->keys_prev, g->keys, SDL_NUM_SCANCODES);
    while (SDL_PollEvent(&ev)) {
        if (ev.type == SDL_QUIT) *quit = true;
        if (ev.type == SDL_KEYDOWN) {
            SDL_Scancode sc = ev.key.keysym.scancode;
            /* capture pour rebind */
            if (g->state == GS_OPTIONS && g->opt_waiting_rebind) {
                g->opt_last_keydown = sc;
                continue;     /* avale la touche */
            }
            if (sc == SDL_SCANCODE_ESCAPE) {
                switch (g->state) {
                    case GS_RUN:        g->state = GS_HUB; break;
                    case GS_HUB:        *quit = true; break;
                    case GS_TITLE:      *quit = true; break;
                    case GS_HELP:       g->state = GS_TITLE; break;
                    case GS_LORE:       g->state = GS_TITLE; break;
                    case GS_OPTIONS:
                        settings_write(&g->settings);
                        g->state = g->opt_return ? g->opt_return : GS_TITLE;
                        break;
                    case GS_CHOOSE_HERO:g->state = GS_HUB; break;
                    case GS_INVENTORY:  g->state = g->state_prev; break;
                    case GS_LEVELUP:    /* pas d'echap */ break;
                    case GS_SHOP:       game_next_floor(g); break;
                    case GS_DEAD:       game_to_hub(g); break;
                    case GS_VICTORY:    game_to_hub(g); break;
                    default: break;
                }
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
    /* defaults */
    float base_maxhp = 100.f, base_speed = 110.f, base_armor = 0.f;
    float base_dmg_mul = 1.0f, base_lifesteal = 0.f, base_regen = 0.f;
    float base_flat_dmg = 0.f;
    float base_melee = 1.f, base_range = 1.f, base_elem = 1.f;
    float base_atkspeed = 1.f;
    float base_crit_chance = 0.05f, base_crit_dmg = 1.5f;
    float base_rangem = 1.f;
    float base_dodge = 0.f;
    float base_aff[EL_COUNT];
    for (int i = 0; i < EL_COUNT; i++) base_aff[i] = 0.f;

    /* hero archetype */
    switch (p->hero) {
        case HERO_GUERRIER:  base_maxhp += 25.f; base_melee *= 1.15f; break;
        case HERO_VOLEUR:    base_speed += 20.f; break;
        case HERO_MAGE:      base_maxhp -= 20.f; base_elem  *= 1.30f; break;
        case HERO_BERSERKER: base_maxhp -= 10.f; base_dmg_mul *= 1.20f; base_lifesteal = 0.08f; break;
        case HERO_PALADIN:   base_armor += 2.f;  base_regen += 1.f; base_maxhp += 15.f; break;
        case HERO_DRUIDE:    base_elem  *= 1.50f; base_melee *= 0.70f; break;
        case HERO_ASSASSIN:  base_crit_chance += 0.25f; base_crit_dmg += 0.5f; base_maxhp -= 25.f; break;
        case HERO_RANGER:    base_range *= 1.40f; base_melee *= 0.75f; break;
        case HERO_TEMPLIER:  base_armor += 3.f; base_maxhp += 15.f; base_atkspeed *= 1.15f; break;
        case HERO_NECROMANT: base_lifesteal = 0.15f; base_regen -= 1.f;
                             base_aff[EL_VOID] += 0.20f; base_aff[EL_DARK] += 0.20f; break;
        default: break;
    }

    /* meta perm bonuses (sanctuaire) */
    base_maxhp += g->meta.perm_hp;
    base_armor += g->meta.perm_armor;
    base_speed += g->meta.perm_speed;
    base_dmg_mul *= 1.f + g->meta.perm_dmg_pct / 100.f;

    /* equipement */
    for (int s = 0; s < EQUIP_SLOTS; s++) {
        if (!p->equipped[s].occupied) continue;
        float v = p->equipped[s].stat_value;
        switch ((EquipSlot)s) {
            case SLOT_HELM:   base_maxhp   += v;          break;
            case SLOT_CHEST:  base_armor   += v;          break;
            case SLOT_LEGS:   base_speed   += v;          break;
            case SLOT_BOOTS:  base_dodge   += v * 0.5f;   break;
            case SLOT_BELT:   base_regen   += v;          break;
            case SLOT_GLOVES: base_dmg_mul *= (1.f + v);  break;
            default: break;
        }
    }

    /* shop items (effets cumulatifs) */
    for (int i = 0; i < p->shop_purchased_count; i++) {
        int rid = p->shop_purchased[i];
        /* applique l'effet du recipe -- callback exterieur */
        extern void shop_recipe_apply_effect(Game *g, int rid,
                                             float *maxhp, float *speed, float *armor,
                                             float *dmg_mul, float *lifesteal, float *regen,
                                             float *flat_dmg, float *melee, float *range,
                                             float *elem, float *atkspeed, float *crit_c,
                                             float *crit_d, float *rangem, float *dodge,
                                             float *aff);
        shop_recipe_apply_effect(g, rid,
            &base_maxhp, &base_speed, &base_armor,
            &base_dmg_mul, &base_lifesteal, &base_regen,
            &base_flat_dmg, &base_melee, &base_range,
            &base_elem, &base_atkspeed, &base_crit_chance,
            &base_crit_dmg, &base_rangem, &base_dodge,
            base_aff);
    }

    /* clamp et applique */
    if (base_maxhp < 1.f) base_maxhp = 1.f;
    if (base_speed < 30.f) base_speed = 30.f;
    if (base_atkspeed < 0.3f) base_atkspeed = 0.3f;
    if (base_crit_chance < 0.f) base_crit_chance = 0.f;
    if (base_crit_chance > 1.f) base_crit_chance = 1.f;
    if (base_dodge < 0.f) base_dodge = 0.f;
    if (base_dodge > 0.75f) base_dodge = 0.75f;

    float ratio = (p->maxhp > 0.f) ? (p->hp / p->maxhp) : 1.f;
    p->maxhp = base_maxhp;
    p->speed = base_speed;
    p->armor = base_armor;
    p->dmg_mul = base_dmg_mul;
    p->lifesteal = base_lifesteal;
    p->regen_per_sec = base_regen;
    p->flat_dmg = base_flat_dmg;
    p->melee_dmg_mul = base_melee;
    p->range_dmg_mul = base_range;
    p->elem_dmg_mul = base_elem;
    p->atk_speed_mul = base_atkspeed;
    p->crit_chance = base_crit_chance;
    p->crit_dmg = base_crit_dmg;
    p->range_mul = base_rangem;
    p->dodge = base_dodge;
    for (int i = 0; i < EL_COUNT; i++) p->elem_affinity[i] = base_aff[i];
    if (p->hp <= 0.f || ratio > 1.f) p->hp = p->maxhp;
    else                              p->hp = ratio * p->maxhp;
}

void game_init(Game *g) {
    memset(g, 0, sizeof(*g));
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        exit(1);
    }

    /* charger les reglages avant la creation du contexte */
    settings_load(&g->settings);

    /* fenetre OpenGL */
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    g->window = SDL_CreateWindow("Element Dungeon",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        WINDOW_W, WINDOW_H, SDL_WINDOW_SHOWN | SDL_WINDOW_OPENGL);
    if (!g->window) { fprintf(stderr, "Win: %s\n", SDL_GetError()); exit(1); }

    /* contexte GL3.3 + FBO offscreen pour pixel-art chunky */
    g->renderer = (GfxCtx *)calloc(1, sizeof(GfxCtx));
    if (!g->renderer) { fprintf(stderr, "alloc gfx\n"); exit(1); }
    if (!gfx_init(g->renderer, g->window, INTERNAL_W, INTERNAL_H, WINDOW_W, WINDOW_H)) {
        fprintf(stderr, "echec init OpenGL\n"); exit(1);
    }
    apply_render_filter(g);

    save_load(&g->meta);
    /* decouvertes de depart : un heros, deux armes, un element */
    g->meta.hero_unlocked[HERO_GUERRIER]   = true;
    g->meta.hero_discovered[HERO_GUERRIER] = true;
    g->meta.weapon_discovered[W_FISTS]   = true;
    g->meta.weapon_discovered[W_SWORD]   = true;
    g->meta.element_discovered[EL_FIRE]  = true;

    audio_init(g);
    mods_load(g);

    /* initialise la table des touches : evite memcpy depuis NULL au 1er frame */
    g->keys = SDL_GetKeyboardState(NULL);
    memset(g->keys_prev, 0, sizeof(g->keys_prev));

    g->state = GS_TITLE;
    srand((unsigned)time(NULL));
}

void game_shutdown(Game *g) {
    save_write(&g->meta);
    audio_shutdown(g);
    if (g->renderer) { gfx_shutdown(g->renderer); free(g->renderer); g->renderer = NULL; }
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
    g->shop_visits = 0;
    g->shop_reroll_cost = 5;

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

/* shop_generate / shop_buy / shop_reroll : voir src/shop.c */

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
/* ---------- HUB : codex + boutique de stats permanentes ---------- */
/* 4 boutons de stats + zones bas pour debuter/options. */
static int hub_perm_cost(int kind) {
    /* cout fixe par achat ; tu peux acheter plusieurs fois */
    switch (kind) {
        case 0: return 40;   /* +10 PV */
        case 1: return 60;   /* +1 ARMURE */
        case 2: return 50;   /* +5 VITESSE */
        case 3: return 70;   /* +5% DEGATS */
        default: return 999;
    }
}
static void hub_perm_apply(Game *g, int kind) {
    int cost = hub_perm_cost(kind);
    if (g->meta.shards < cost) return;
    g->meta.shards -= cost;
    switch (kind) {
        case 0: g->meta.perm_hp     += 10; break;
        case 1: g->meta.perm_armor  += 1;  break;
        case 2: g->meta.perm_speed  += 5;  break;
        case 3: g->meta.perm_dmg_pct+= 5;  break;
    }
    save_write(&g->meta);
    sfx_play(g, SFX_COIN);
}

static void update_hub(Game *g) {
    /* navigation curseur 0..3 sur les 4 boutons stats */
    if (g->keys[SDL_SCANCODE_LEFT]  && !g->keys_prev[SDL_SCANCODE_LEFT])
        g->hub_cursor = (g->hub_cursor + 3) % 4;
    if (g->keys[SDL_SCANCODE_RIGHT] && !g->keys_prev[SDL_SCANCODE_RIGHT])
        g->hub_cursor = (g->hub_cursor + 1) % 4;
    if (g->keys[SDL_SCANCODE_A] && !g->keys_prev[SDL_SCANCODE_A])
        g->hub_cursor = (g->hub_cursor + 3) % 4;
    if (g->keys[SDL_SCANCODE_D] && !g->keys_prev[SDL_SCANCODE_D])
        g->hub_cursor = (g->hub_cursor + 1) % 4;

    /* mouse hover sur les boutons (memes coords que render_hub) */
    int boxw = 118, boxh = 50, gap = 6;
    int total_w = 4 * boxw + 3 * gap;
    int sx0 = (INTERNAL_W - total_w) / 2;
    int sy  = 60;
    for (int i = 0; i < 4; i++) {
        int sx = sx0 + i * (boxw + gap);
        if (mouse_in_rect(g, sx, sy, boxw, boxh)) {
            g->hub_cursor = i;
            if (mouse_clicked(g)) hub_perm_apply(g, i);
        }
    }
    if ((g->keys[SDL_SCANCODE_RETURN] && !g->keys_prev[SDL_SCANCODE_RETURN]) ||
        (g->keys[SDL_SCANCODE_E]      && !g->keys_prev[SDL_SCANCODE_E])) {
        hub_perm_apply(g, g->hub_cursor);
    }

    /* zones bas d'ecran : DEBUTER / OPTIONS / AIDE */
    int by = INTERNAL_H - 30;
    int bw = 120, bh = 16;
    /* trois boutons centres */
    int gx = INTERNAL_W/2 - (bw * 3 + 12) / 2;
    if (mouse_in_rect(g, gx, by, bw, bh) && mouse_clicked(g))
        g->state = GS_CHOOSE_HERO;
    if (mouse_in_rect(g, gx + bw + 6, by, bw, bh) && mouse_clicked(g)) {
        g->opt_return = GS_HUB; g->opt_section = 0; g->opt_cursor = 0;
        g->opt_waiting_rebind = false; g->state = GS_OPTIONS;
    }
    if (mouse_in_rect(g, gx + (bw + 6) * 2, by, bw, bh) && mouse_clicked(g))
        g->state = GS_HELP;

    if (g->keys[SDL_SCANCODE_R] && !g->keys_prev[SDL_SCANCODE_R])
        g->state = GS_CHOOSE_HERO;
    if (g->keys[SDL_SCANCODE_H] && !g->keys_prev[SDL_SCANCODE_H])
        g->state = GS_HELP;
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

    /* mouse hover sur les portraits (2 rangees, aligne sur render_choose_hero) */
    {
        int per_row = 5;
        int gap_x = INTERNAL_W / (per_row + 1);
        int row_y[2] = { (INTERNAL_H * 4) / 12, (INTERNAL_H * 8) / 12 };
        for (int i = 0; i < HERO_COUNT; i++) {
            int row = i / per_row;
            int col = i % per_row;
            int sx = gap_x * (col + 1);
            int sy = row_y[row];
            if (mouse_in_rect(g, sx - 26, sy - 34, 52, 80)) {
                g->hero_cursor = i;
            }
        }
    }

    bool activate = (g->keys[SDL_SCANCODE_RETURN] && !g->keys_prev[SDL_SCANCODE_RETURN]) ||
                    (g->keys[SDL_SCANCODE_SPACE]  && !g->keys_prev[SDL_SCANCODE_SPACE])  ||
                    mouse_clicked(g);
    if (activate) {
        if (!g->meta.hero_discovered[g->hero_cursor]) {
            /* heros encore inconnu : pas selectionnable */
            return;
        }
        if (g->meta.hero_unlocked[g->hero_cursor]) {
            game_start_new_run(g);
        } else {
            int cost = 60 + g->hero_cursor * 25;
            if (g->meta.shards >= cost) {
                g->meta.shards -= cost;
                g->meta.hero_unlocked[g->hero_cursor] = true;
                save_write(&g->meta);
                sfx_play(g, SFX_LEVELUP);
            }
        }
    }
}

static void apply_levelup_choice(Game *g, int c) {
    int val = g->levelup_choices[c];
    Player *p = &g->player;
    /* val = stat id, sans option element (les elements se decouvrent via gameplay) */
    switch (val) {
        case 0: p->maxhp += 20.f; p->hp += 20.f; break;
        case 1: p->speed += 12.f; break;
        case 2: p->dmg_mul *= 1.15f; break;
        case 3: p->armor += 1.f; break;
        case 4: p->regen_per_sec += 0.6f; break;
        case 5: p->lifesteal += 0.04f; break;
        case 6:
            /* +10% range : reduit cooldown de toutes armes */
            for (int i = 0; i < WEAPON_SLOTS; i++)
                if (p->weapons[i].owned) p->weapons[i].base_range *= 1.10f;
            break;
        case 7:
            for (int i = 0; i < WEAPON_SLOTS; i++)
                if (p->weapons[i].owned) p->weapons[i].base_cd *= 0.90f;
            break;
        case 8:
            /* heal complet */
            p->hp = p->maxhp;
            break;
        default: break;
    }
    sfx_play(g, SFX_LEVELUP);
    g->state = GS_RUN;
}

static void update_levelup(Game *g) {
    for (int c = 0; c < 3; c++) {
        if (g->keys[SDL_SCANCODE_1 + c] && !g->keys_prev[SDL_SCANCODE_1 + c]) {
            apply_levelup_choice(g, c); return;
        }
    }
    /* mouse : align with render_levelup boxes */
    int boxw = 130, boxh = 56;
    int total_w = boxw * 3 + 12;
    int sx0 = (INTERNAL_W - total_w) / 2;
    int sy = 70;
    for (int c = 0; c < 3; c++) {
        int sx = sx0 + c * (boxw + 6);
        if (mouse_in_rect(g, sx, sy, boxw, boxh) && mouse_clicked(g)) {
            apply_levelup_choice(g, c); return;
        }
    }
}

static void update_shop(Game *g) {
    if (g->keys[SDL_SCANCODE_LEFT]  && !g->keys_prev[SDL_SCANCODE_LEFT])
        g->shop_cursor = (g->shop_cursor + SHOP_SLOTS - 1) % SHOP_SLOTS;
    if (g->keys[SDL_SCANCODE_RIGHT] && !g->keys_prev[SDL_SCANCODE_RIGHT])
        g->shop_cursor = (g->shop_cursor + 1) % SHOP_SLOTS;
    if (g->keys[SDL_SCANCODE_A] && !g->keys_prev[SDL_SCANCODE_A])
        g->shop_cursor = (g->shop_cursor + SHOP_SLOTS - 1) % SHOP_SLOTS;
    if (g->keys[SDL_SCANCODE_D] && !g->keys_prev[SDL_SCANCODE_D])
        g->shop_cursor = (g->shop_cursor + 1) % SHOP_SLOTS;

    /* layout (aligne sur render_shop) */
    int boxw = 130, boxh = 150, gap = 8;
    int total_w = SHOP_SLOTS * boxw + (SHOP_SLOTS - 1) * gap;
    int sx0 = (INTERNAL_W - total_w) / 2;
    int sy  = 56;
    for (int i = 0; i < SHOP_SLOTS; i++) {
        int sx = sx0 + i * (boxw + gap);
        if (mouse_in_rect(g, sx, sy, boxw, boxh)) {
            g->shop_cursor = i;
            if (mouse_clicked(g)) shop_buy(g, i);
        }
    }

    if ((g->keys[SDL_SCANCODE_RETURN] && !g->keys_prev[SDL_SCANCODE_RETURN]) ||
        (g->keys[SDL_SCANCODE_E] && !g->keys_prev[SDL_SCANCODE_E])) {
        shop_buy(g, g->shop_cursor);
    }
    /* reroll : touche R ou bouton */
    if (g->keys[SDL_SCANCODE_R] && !g->keys_prev[SDL_SCANCODE_R]) {
        shop_reroll(g);
    }
    int rrw = 110, rrh = 18;
    int rrx = (INTERNAL_W - rrw) / 2;
    int rry = sy + boxh + 8;
    if (mouse_in_rect(g, rrx, rry, rrw, rrh) && mouse_clicked(g)) {
        shop_reroll(g);
    }
    {
        SDL_Scancode kinv = g->settings.keys[BIND_INVENTORY];
        if (kinv && g->keys[kinv] && !g->keys_prev[kinv]) {
            g->state_prev = GS_SHOP; g->state = GS_INVENTORY;
        }
    }
    /* "ETAGE SUIVANT" zone bas */
    if (mouse_in_rect(g, INTERNAL_W/2 - 100, INTERNAL_H - 22, 200, 14) && mouse_clicked(g)) {
        game_next_floor(g);
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
            bool start = (g->keys[SDL_SCANCODE_RETURN] && !g->keys_prev[SDL_SCANCODE_RETURN]) ||
                         (g->keys[SDL_SCANCODE_SPACE]  && !g->keys_prev[SDL_SCANCODE_SPACE]);
            /* mouse zones (alignees sur render_title) */
            int yA = INTERNAL_H/2 + 20;
            int yB = INTERNAL_H/2 + 36;
            int yC = INTERNAL_H/2 + 52;
            int yD = INTERNAL_H/2 + 68;
            int yE = INTERNAL_H/2 + 84;
            if (mouse_in_rect(g, INTERNAL_W/2 - 100, yA, 200, 12) && mouse_clicked(g)) start = true;
            if (mouse_in_rect(g, INTERNAL_W/2 - 100, yB, 200, 12) && mouse_clicked(g))
                g->state = GS_LORE;
            if (mouse_in_rect(g, INTERNAL_W/2 - 100, yC, 200, 12) && mouse_clicked(g)) {
                g->opt_return = GS_TITLE; g->opt_section = 0; g->opt_cursor = 0;
                g->opt_waiting_rebind = false; g->state = GS_OPTIONS;
            }
            if (mouse_in_rect(g, INTERNAL_W/2 - 100, yD, 200, 12) && mouse_clicked(g))
                g->state = GS_HELP;
            if (mouse_in_rect(g, INTERNAL_W/2 - 100, yE, 200, 12) && mouse_clicked(g))
                quit = true;
            if (start) g->state = GS_HUB;
            if (g->keys[SDL_SCANCODE_H] && !g->keys_prev[SDL_SCANCODE_H]) g->state = GS_HELP;
            if (g->keys[SDL_SCANCODE_L] && !g->keys_prev[SDL_SCANCODE_L]) g->state = GS_LORE;
            if (g->keys[SDL_SCANCODE_O] && !g->keys_prev[SDL_SCANCODE_O]) {
                g->opt_return = GS_TITLE;
                g->opt_section = 0;
                g->opt_cursor = 0;
                g->opt_waiting_rebind = false;
                g->state = GS_OPTIONS;
            }
        } else if (g->state == GS_LORE) {
            if ((g->keys[SDL_SCANCODE_RETURN] && !g->keys_prev[SDL_SCANCODE_RETURN]) ||
                (g->keys[SDL_SCANCODE_SPACE]  && !g->keys_prev[SDL_SCANCODE_SPACE])  ||
                mouse_clicked(g)) {
                g->state = GS_TITLE;
            }
        } else if (g->state == GS_HELP) {
            /* esc handled */
        } else if (g->state == GS_OPTIONS) {
            update_options(g);
        } else if (g->state == GS_HUB) {
            update_hub(g);
            if (g->keys[SDL_SCANCODE_O] && !g->keys_prev[SDL_SCANCODE_O]) {
                g->opt_return = GS_HUB;
                g->opt_section = 0;
                g->opt_cursor = 0;
                g->opt_waiting_rebind = false;
                g->state = GS_OPTIONS;
            }
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
                /* uniquement des stats : les elements se decouvrent via
                   pickups dans le donjon, pas au level-up. */
                int statpool[9] = { 0, 1, 2, 3, 4, 5, 6, 7, 8 };
                int N = 9;
                for (int i = N - 1; i > 0; i--) {
                    int j = rand() % (i + 1);
                    int t = statpool[i]; statpool[i] = statpool[j]; statpool[j] = t;
                }
                for (int c = 0; c < 3; c++) {
                    g->levelup_choice_kind[c] = 2; /* tous stats */
                    g->levelup_choices[c] = statpool[c];
                }
                g->state = GS_LEVELUP;
                sfx_play(g, SFX_LEVELUP);
            }
            /* inventaire (binding) */
            {
                SDL_Scancode kinv = g->settings.keys[BIND_INVENTORY];
                if (kinv != SDL_SCANCODE_UNKNOWN &&
                    g->keys[kinv] && !g->keys_prev[kinv]) {
                    g->state_prev = GS_RUN;
                    g->state = GS_INVENTORY;
                }
            }
        } else if (g->state == GS_LEVELUP) {
            update_levelup(g);
            {
                SDL_Scancode kinv = g->settings.keys[BIND_INVENTORY];
                if (kinv != SDL_SCANCODE_UNKNOWN &&
                    g->keys[kinv] && !g->keys_prev[kinv]) {
                    g->state_prev = GS_LEVELUP;
                    g->state = GS_INVENTORY;
                }
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

        gfx_frame_begin(g->renderer);

        /* le monde 3D est dessine d'abord (avec depth test), puis l'UI 2D
           est dessinee en passe ortho par-dessus. */
        bool show_world = (g->state == GS_RUN || g->state == GS_LEVELUP ||
                           g->state == GS_DEAD || g->state == GS_VICTORY ||
                           (g->state == GS_INVENTORY &&
                              (g->state_prev == GS_RUN || g->state_prev == GS_LEVELUP)));
        if (show_world) {
            int sx, sy; apply_shake(g, &sx, &sy);
            g->camera_x = (float)sx;
            g->camera_y = (float)sy;
            render_world(g);     /* 3D */
        }

        /* UI 2D : tout passe par le batcher GL. */
        gfx_ui_begin(g->renderer);
        if (g->state == GS_TITLE)            render_title(g);
        else if (g->state == GS_LORE)        render_lore(g);
        else if (g->state == GS_HELP)        render_help(g);
        else if (g->state == GS_OPTIONS)     render_options(g);
        else if (g->state == GS_HUB)         render_hub(g);
        else if (g->state == GS_CHOOSE_HERO) render_choose_hero(g);
        else if (g->state == GS_SHOP) {
            render_shop(g);
        } else if (g->state == GS_INVENTORY) {
            if (g->state_prev == GS_RUN || g->state_prev == GS_LEVELUP) {
                render_world_overlay_ui(g);
                render_hud(g);
            } else if (g->state_prev == GS_SHOP) {
                render_shop(g);
            }
            render_inventory(g);
        } else {
            if (g->state == GS_RUN || g->state == GS_LEVELUP ||
                g->state == GS_DEAD || g->state == GS_VICTORY) {
                render_world_overlay_ui(g);
            }
            render_hud(g);
            if (g->state == GS_LEVELUP) render_levelup(g);
            if (g->state == GS_DEAD)    render_dead(g);
            if (g->state == GS_VICTORY) render_victory(g);
            if (g->flash_t > 0.f) {
                int alpha = (int)(180.f * (g->flash_t / 0.20f));
                if (alpha < 0) alpha = 0;
                if (alpha > 255) alpha = 255;
                gfx_set_blend(g->renderer, true);
                uint32_t col = ((uint32_t)0xFF3C3CU << 8) | (uint32_t)(alpha & 0xFF);
                gfx_set_color(g->renderer, col);
                gfx_fill_rect(g->renderer, 0, 0, INTERNAL_W, INTERNAL_H);
                gfx_set_blend(g->renderer, false);
            }
        }
        gfx_ui_end(g->renderer);

        gfx_frame_end(g->renderer);
        SDL_GL_SwapWindow(g->window);
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
