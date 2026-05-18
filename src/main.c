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
    g->mouse_wheel = 0;
    if (g->keys) memcpy(g->keys_prev, g->keys, SDL_NUM_SCANCODES);
    while (SDL_PollEvent(&ev)) {
        if (ev.type == SDL_QUIT) *quit = true;
        if (ev.type == SDL_MOUSEWHEEL) g->mouse_wheel += ev.wheel.y;
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
                    case GS_CODEX:      g->state = GS_HUB; break;
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

/* shake polish :
 *   1. ease-out (t*t) au lieu de lineaire : impact maximal a t=tmax,
 *      decroissance rapide ensuite.
 *   2. low-pass 1-pole sur le jitter random pour adoucir le "bruit blanc"
 *      qui rendait l ancien shake "buzzy" / pixelise.
 *   3. cap de magnitude pour que les gros impacts ne sortent pas du FBO. */
static void apply_shake(Game *g, int *ox, int *oy) {
    static float prev_x = 0.f, prev_y = 0.f;
    if (g->shake_t <= 0.f) {
        prev_x *= 0.5f; prev_y *= 0.5f;       /* decay residuel */
        *ox = (int)prev_x; *oy = (int)prev_y;
        return;
    }
    /* normalisation sur 0.30s (typique des hits melee). Les shakes plus
     * longs (boss / dash perdu) auront un fade plus doux. */
    float fade = g->shake_t / 0.30f;
    if (fade > 1.f) fade = 1.f;
    fade = fade * fade;                        /* ease-out quadratique */
    float m = g->shake_mag * fade;
    if (m > 10.f) m = 10.f;                    /* cap pour pas casser le FBO */
    float jx = (rand() / (float)RAND_MAX - 0.5f) * 2.f * m;
    float jy = (rand() / (float)RAND_MAX - 0.5f) * 2.f * m;
    /* low-pass 0.55 : un peu plus de smoothing que 0.5 sans devenir mou */
    prev_x = prev_x * 0.55f + jx * 0.45f;
    prev_y = prev_y * 0.55f + jy * 0.45f;
    *ox = (int)prev_x; *oy = (int)prev_y;
}

/* clamp helper : cap simple low/high */
static inline float clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

void game_recompute_player_stats(Game *g) {
    Player *p = &g->player;
    /* StatBlock unique : tous les modificateurs s'accumulent ici, puis
     * un seul passage de clamp avant d'ecrire dans Player. */
    StatBlock sb = {
        .maxhp = 100.f, .speed = 110.f, .armor = 0.f,
        .dmg_mul = 1.f, .lifesteal = 0.f, .regen = 0.f,
        .flat_dmg = 0.f,
        .melee = 1.f, .range = 1.f, .elem = 1.f,
        .atk_speed = 1.f,
        .crit_chance = 0.05f, .crit_dmg = 1.5f,
        .range_mul = 1.f, .dodge = 0.f,
    };
    for (int i = 0; i < EL_COUNT; i++) sb.aff[i] = 0.f;

    /* hero archetype */
    switch (p->hero) {
        case HERO_GUERRIER:  sb.maxhp += 25.f; sb.melee *= 1.15f; break;
        case HERO_VOLEUR:    sb.speed += 20.f; break;
        case HERO_MAGE:      sb.maxhp -= 20.f; sb.elem  *= 1.30f; break;
        case HERO_BERSERKER: sb.maxhp -= 10.f; sb.dmg_mul *= 1.20f; sb.lifesteal = 0.08f; break;
        case HERO_PALADIN:   sb.armor += 2.f;  sb.regen += 1.f; sb.maxhp += 15.f; break;
        case HERO_DRUIDE:    sb.elem  *= 1.50f; sb.melee *= 0.70f; break;
        case HERO_ASSASSIN:  sb.crit_chance += 0.25f; sb.crit_dmg += 0.5f; sb.maxhp -= 25.f; break;
        case HERO_RANGER:    sb.range *= 1.40f; sb.melee *= 0.75f; break;
        case HERO_TEMPLIER:  sb.armor += 3.f; sb.maxhp += 15.f; sb.atk_speed *= 1.15f; break;
        case HERO_NECROMANT: sb.lifesteal = 0.15f; sb.regen -= 1.f;
                             sb.aff[EL_VOID] += 0.20f; sb.aff[EL_DARK] += 0.20f; break;
        default: break;
    }

    /* meta perm bonuses (sanctuaire) */
    sb.maxhp += g->meta.perm_hp;
    sb.armor += g->meta.perm_armor;
    sb.speed += g->meta.perm_speed;
    sb.dmg_mul *= 1.f + g->meta.perm_dmg_pct / 100.f;

    /* equipement : stat de base + affixes (Diablo-like) ; les uniques
     * shortent par unique_apply_to_block (effets pre-definis). */
    for (int s = 0; s < EQUIP_SLOTS; s++) {
        if (!p->equipped[s].occupied) continue;
        Item *it = &p->equipped[s];
        if (it->is_unique) {
            unique_apply_to_block(it->unique_id, &sb);
            continue;
        }
        float v = it->stat_value;
        switch ((EquipSlot)s) {
            case SLOT_HELM:   sb.maxhp   += v;          break;
            case SLOT_CHEST:  sb.armor   += v;          break;
            case SLOT_LEGS:   sb.speed   += v;          break;
            case SLOT_BOOTS:  sb.dodge   += v * 0.5f;   break;
            case SLOT_BELT:   sb.regen   += v;          break;
            case SLOT_GLOVES: sb.dmg_mul *= (1.f + v);  break;
            default: break;
        }
        /* Affixes : tous additifs sur la stat correspondante. dmg_mul est
         * additif sur la fraction (donc +5% = sb.dmg_mul *= 1.05). */
        for (int a = 0; a < it->affix_count; a++) {
            const ItemAffix *af = &it->affixes[a];
            switch (af->kind) {
                case AFFIX_HP:          sb.maxhp       += af->value; break;
                case AFFIX_ARMOR:       sb.armor       += af->value; break;
                case AFFIX_SPEED:       sb.speed       += af->value; break;
                case AFFIX_DMG_PCT:     sb.dmg_mul     *= (1.f + af->value); break;
                case AFFIX_CRIT_CHANCE: sb.crit_chance += af->value; break;
                case AFFIX_LIFESTEAL:   sb.lifesteal   += af->value; break;
                case AFFIX_REGEN:       sb.regen       += af->value; break;
                /* atk_speed multiplicateur : <1 = plus rapide. On reduit
                 * sb.atk_speed (donc cooldown plus court). */
                case AFFIX_ATK_SPEED:   sb.atk_speed   *= (1.f - af->value); break;
                case AFFIX_DODGE:       sb.dodge       += af->value; break;
                case AFFIX_RANGE_MUL:   sb.range_mul   *= (1.f + af->value); break;
                case AFFIX_FLAT_DMG:    sb.flat_dmg    += af->value; break;
                default: break;
            }
        }
    }

    /* shop : on accumule via la nouvelle API qui prend un StatBlock */
    for (int i = 0; i < p->shop_purchased_count; i++) {
        shop_recipe_apply_to_block(p->shop_purchased[i], &sb);
    }

    /* ===== CLAMPS EXPLICITES ====================================
     * Garantit que toute valeur out-of-range (achats / heros / equip)
     * est ramenee dans une plage saine avant d'ecrire sur Player. */
    sb.maxhp     = clampf(sb.maxhp,    1.f, 9999.f);
    sb.speed     = clampf(sb.speed,   30.f,  400.f);
    sb.armor     = clampf(sb.armor,    0.f,   50.f);
    sb.dmg_mul   = clampf(sb.dmg_mul,  0.1f, 10.f);
    sb.lifesteal = clampf(sb.lifesteal,0.f,   0.50f);  /* cap 50% */
    sb.regen     = clampf(sb.regen,   -5.f,  10.f);
    sb.flat_dmg  = clampf(sb.flat_dmg, 0.f, 200.f);
    sb.melee     = clampf(sb.melee,    0.1f,  5.f);
    sb.range     = clampf(sb.range,    0.1f,  5.f);
    sb.elem      = clampf(sb.elem,     0.1f,  5.f);
    /* atk_speed : multiplicateur du cooldown ; minimum 0.30 evite cadence absurde */
    sb.atk_speed = clampf(sb.atk_speed, 0.30f, 3.f);
    /* crit chance : diminishing returns au-dela de 75%, cap dur 95% */
    if (sb.crit_chance > 0.75f) {
        float over = sb.crit_chance - 0.75f;
        sb.crit_chance = 0.75f + over * 0.25f;
    }
    sb.crit_chance = clampf(sb.crit_chance, 0.f, 0.95f);
    sb.crit_dmg    = clampf(sb.crit_dmg,    1.f, 6.f);
    sb.range_mul   = clampf(sb.range_mul,   0.5f, 3.f);
    sb.dodge       = clampf(sb.dodge,       0.f, 0.75f);
    /* affinites elementaires : -50%..+200% par element */
    for (int i = 0; i < EL_COUNT; i++) sb.aff[i] = clampf(sb.aff[i], -0.50f, 2.00f);

    /* applique sur Player */
    float ratio = (p->maxhp > 0.f) ? (p->hp / p->maxhp) : 1.f;
    p->maxhp        = sb.maxhp;
    p->speed        = sb.speed;
    p->armor        = sb.armor;
    p->dmg_mul      = sb.dmg_mul;
    p->lifesteal    = sb.lifesteal;
    p->regen_per_sec = sb.regen;
    p->flat_dmg     = sb.flat_dmg;
    p->melee_dmg_mul= sb.melee;
    p->range_dmg_mul= sb.range;
    p->elem_dmg_mul = sb.elem;
    p->atk_speed_mul= sb.atk_speed;
    p->crit_chance  = sb.crit_chance;
    p->crit_dmg     = sb.crit_dmg;
    p->range_mul    = sb.range_mul;
    p->dodge        = sb.dodge;
    for (int i = 0; i < EL_COUNT; i++) p->elem_affinity[i] = sb.aff[i];
    /* flags build-defining */
    p->u_explosions_attract = sb.u_explosions_attract;
    p->u_crit_shrink        = sb.u_crit_shrink;
    p->u_corpse_mines       = sb.u_corpse_mines;
    p->u_free_dash          = sb.u_free_dash;
    p->u_drone_count        = sb.u_drone_count;
    p->u_berserk_cd         = sb.u_berserk_cd;
    p->u_element_absorb     = sb.u_element_absorb;
    p->u_hazard_immune      = sb.u_hazard_immune;
    p->u_hazard_stacks      = sb.u_hazard_stacks;
    p->u_phoenix_revive     = sb.u_phoenix_revive;
    p->u_kill_wave          = sb.u_kill_wave;
    p->u_frontal_immune     = sb.u_frontal_immune;
    p->u_dodge_attack       = sb.u_dodge_attack;
    p->u_crowd_regen        = sb.u_crowd_regen;
    p->u_stun_on_melee      = sb.u_stun_on_melee;
    p->u_void_trail         = sb.u_void_trail;
    p->u_kill_stack_dmg     = sb.u_kill_stack_dmg;
    p->u_heavy_armor        = sb.u_heavy_armor;
    p->u_expose_weakness    = sb.u_expose_weakness;
    p->u_last_stand         = sb.u_last_stand;
    /* HEAVY ARMOR : armure x2, mais -3 vitesse / point d armure. */
    if (sb.u_heavy_armor) {
        float speed_malus = sb.armor * 3.f;
        p->armor *= 2.f;
        p->speed -= speed_malus;
        if (p->speed < 30.f) p->speed = 30.f;
    }
    /* FRONTAL_IMMUNE : -40% vitesse en echange de l immunite frontale. */
    if (sb.u_frontal_immune) p->speed *= 0.60f;
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
    /* cumul lifetime pour les stats du sanctuaire */
    g->meta.lifetime_kills       += g->run_kills;
    g->meta.lifetime_damage      += g->run_damage_dealt;
    g->meta.lifetime_legendaries += g->run_legendary_drops;
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
    memset(g->surfaces, 0, sizeof(g->surfaces));
    g->run_kills = 0;
    g->run_time = 0.f;
    g->run_damage_dealt = 0;
    g->run_best_combo_size = 0;
    g->run_legendary_drops = 0;
    g->killstreak_count = 0; g->killstreak_t = 0.f; g->overdrive_t = 0.f;
    /* unique-flag state : reset au demarrage (les charges seront
     * activees au premier point_in_room avec first_visit=true). */
    /* le player vient d etre memset a 0, donc phoenix_charge / last_stand
     * / kill_stack_count / hazard_stacks / last_stand_t sont deja a 0. */
    g->floor_index = 1;
    g->shake_t = 0.f;
    g->portal_spawned = false;
    g->boss_intro_t = 0.f;
    g->boss_death_t = 0.f;
    g->shop_visits = 0;
    g->shop_reroll_cost = 5;
    g->combo_callout_t = 0.f;
    g->combo_callout_mask = 0;
    g->scroll_t = 0.f;
    g->current_attack_crit = false;

    Player *p = &g->player;
    p->hero = (HeroClass)g->hero_cursor;
    p->r = 6.f;
    p->level = 1; p->xp = 0; p->xp_to_next = 6;
    p->souls = 0; p->coins = 0;
    p->active_weapon = 0;
    /* triple feedback loop : aucune jauge active au depart */
    p->active_loop_idx  = -1;
    p->active_loop_mask = 0;
    g->enemy_alive_count = 0;

    /* poings sur les 2 slots */
    weapon_init_defaults(&p->weapons[0], W_FISTS); p->weapons[0].owned = true;
    weapon_init_defaults(&p->weapons[1], W_FISTS); p->weapons[1].owned = true;

    game_recompute_player_stats(g);
    p->hp = p->maxhp;

    dungeon_generate(&g->dungeon, g->floor_index, (unsigned)rand());
    p->x = g->dungeon.spawn_x * TILE + TILE / 2;
    p->y = g->dungeon.spawn_y * TILE + TILE / 2;
    world_assets_reset(g);
    world_assets_populate(g);
    surfaces_seed_biome(g);
    if (g->settings.debug_room) {
        /* en mode debug, on revele tout pour pouvoir reellement utiliser
         * le contenu de la salle (sinon les armes/elements non decouverts
         * ne s'affichent meme pas a l'UI). */
        for (int e = 1; e < EL_COUNT;  e++) g->meta.element_discovered[e] = true;
        for (int w = 1; w < W_COUNT;   w++) g->meta.weapon_discovered[w]  = true;
        dungeon_add_debug_room(g);
    }
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
    /* triple feedback loop : reset entre les etages (intensity, proc_count, etc) */
    memset(g->player.loop_states, 0, sizeof(g->player.loop_states));
    g->player.active_loop_idx  = -1;
    g->player.active_loop_mask = 0;
    g->enemy_alive_count = 0;
    memset(g->projectiles, 0, sizeof(g->projectiles));
    memset(g->pickups, 0, sizeof(g->pickups));
    memset(g->fairies, 0, sizeof(g->fairies));
    memset(g->surfaces, 0, sizeof(g->surfaces));
    g->portal_spawned = false;
    g->boss_intro_t = 0.f;
    g->boss_death_t = 0.f;
    g->player.hp += 25.f;
    if (g->player.hp > g->player.maxhp) g->player.hp = g->player.maxhp;
    world_assets_reset(g);
    world_assets_populate(g);
    surfaces_seed_biome(g);
    if (g->settings.debug_room) dungeon_add_debug_room(g);
    g->state = GS_RUN;
}

/* hub navigation */
/* ---------- HUB : codex + boutique de stats permanentes ---------- */
/* 4 boutons de stats + zones bas pour debuter/options. */
/* === META achats permanents : cout ramp + cap 10 niveaux ===
 * Niveau = total achete divise par step. Cout = base * (1 + level).
 * Refuse l achat au niveau max. */
int perm_stat_step(int kind) {
    switch (kind) {
        case 0: return 10;   /* +10 PV / niveau */
        case 1: return 1;    /* +1 ARMURE       */
        case 2: return 5;    /* +5 VITESSE      */
        case 3: return 5;    /* +5% DEGATS      */
        default: return 1;
    }
}

const char *perm_stat_label(int kind) {
    switch (kind) {
        case 0: return "PV MAX";
        case 1: return "ARMURE";
        case 2: return "VITESSE";
        case 3: return "DEGATS";
        default: return "?";
    }
}

int perm_stat_level(const MetaSave *m, int kind) {
    int cur = 0;
    switch (kind) {
        case 0: cur = m->perm_hp;       break;
        case 1: cur = m->perm_armor;    break;
        case 2: cur = m->perm_speed;    break;
        case 3: cur = m->perm_dmg_pct;  break;
    }
    int step = perm_stat_step(kind);
    return (step > 0) ? cur / step : 0;
}

int perm_stat_cost(const MetaSave *m, int kind) {
    int level = perm_stat_level(m, kind);
    if (level >= PERM_MAX_LEVEL) return 0;       /* 0 = locked / max */
    int base;
    switch (kind) {
        case 0: base = 40; break;
        case 1: base = 60; break;
        case 2: base = 50; break;
        case 3: base = 70; break;
        default: return 999;
    }
    return base * (1 + level);
}

static void hub_perm_apply(Game *g, int kind) {
    int cost = perm_stat_cost(&g->meta, kind);
    if (cost <= 0 || g->meta.shards < cost) return;
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

    /* zones bas d'ecran : DEBUTER / OPTIONS / CODEX / AIDE */
    int by = INTERNAL_H - 30;
    int bw = 96, bh = 16;
    int gx = INTERNAL_W/2 - (bw * 4 + 18) / 2;
    if (mouse_in_rect(g, gx, by, bw, bh) && mouse_clicked(g))
        g->state = GS_CHOOSE_HERO;
    if (mouse_in_rect(g, gx + bw + 6, by, bw, bh) && mouse_clicked(g)) {
        g->opt_return = GS_HUB; g->opt_section = 0; g->opt_cursor = 0;
        g->opt_waiting_rebind = false; g->state = GS_OPTIONS;
    }
    if (mouse_in_rect(g, gx + (bw + 6) * 2, by, bw, bh) && mouse_clicked(g)) {
        g->state = GS_CODEX;
        g->codex_tab = 0; g->codex_cursor = 0; g->codex_scroll = 0;
    }
    if (mouse_in_rect(g, gx + (bw + 6) * 3, by, bw, bh) && mouse_clicked(g))
        g->state = GS_HELP;

    if (g->keys[SDL_SCANCODE_R] && !g->keys_prev[SDL_SCANCODE_R])
        g->state = GS_CHOOSE_HERO;
    if (g->keys[SDL_SCANCODE_H] && !g->keys_prev[SDL_SCANCODE_H])
        g->state = GS_HELP;
    if (g->keys[SDL_SCANCODE_K] && !g->keys_prev[SDL_SCANCODE_K]) {
        g->state = GS_CODEX;
        g->codex_tab = 0; g->codex_cursor = 0; g->codex_scroll = 0;
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

/* table des stats de level-up (utilisee par render_levelup et apply).
 * base_value = palier R_COMMON. Les rarites superieures multiplient. */
typedef struct {
    const char *name;
    float       base_value;
} LevelStat;
static const LevelStat LEVEL_STATS[8] = {
    { "PV max",        20.f  },   /* axis 0 */
    { "Vitesse",       12.f  },   /* axis 1 */
    { "Degats",         0.15f },  /* axis 2 - +X% global */
    { "Armure",         1.0f },   /* axis 3 */
    { "Regen / s",      0.6f },   /* axis 4 */
    { "Vol de vie",     0.04f },  /* axis 5 - +X% lifesteal */
    { "Portee",         0.10f },  /* axis 6 - +X% portee */
    { "Atk speed",      0.10f },  /* axis 7 - +X% atk speed */
};

/* multiplicateurs par rarete : meme philosophie que Item (Common 1, Magic 1.4,
 * Rare 2, Epic 3, Legendaire 5). Une rarete elevee transforme l'effet. */
static const float LEVELUP_RARITY_MUL[R_COUNT] = {
    [R_COMMON]    = 1.00f,
    [R_MAGIC]     = 1.40f,
    [R_RARE]      = 2.00f,
    [R_EPIC]      = 3.00f,
    [R_LEGENDARY] = 5.00f,
};

/* renvoie la valeur effective scaled par la rarete pour l'axe donne */
static float level_stat_value(int axis, Rarity r) {
    if (axis < 0 || axis >= (int)(sizeof(LEVEL_STATS)/sizeof(LEVEL_STATS[0]))) return 0.f;
    if (r < 0 || r >= R_COUNT) r = R_COMMON;
    return LEVEL_STATS[axis].base_value * LEVELUP_RARITY_MUL[r];
}

static void apply_levelup_choice(Game *g, int c) {
    int axis = g->levelup_choices[c];
    Rarity rar = (Rarity)g->levelup_choice_rarity[c];
    if (rar < 0 || rar >= R_COUNT) rar = R_COMMON;
    float v = level_stat_value(axis, rar);
    Player *p = &g->player;
    switch (axis) {
        case 0: p->maxhp += v; p->hp += v; break;
        case 1: p->speed += v; break;
        case 2: p->dmg_mul *= (1.f + v); break;
        case 3: p->armor += v; break;
        case 4: p->regen_per_sec += v; break;
        case 5: p->lifesteal += v; break;
        case 6:
            for (int i = 0; i < WEAPON_SLOTS; i++)
                if (p->weapons[i].owned) p->weapons[i].base_range *= (1.f + v);
            break;
        case 7:
            for (int i = 0; i < WEAPON_SLOTS; i++)
                if (p->weapons[i].owned) p->weapons[i].base_cd *= (1.f - v);
            break;
        default: break;
    }
    sfx_play(g, SFX_LEVELUP);
    g->state = GS_RUN;
}

/* signature publique appelee par render_levelup (definie dans render.c) */
const char *level_stat_name (int axis) {
    if (axis < 0 || axis >= (int)(sizeof(LEVEL_STATS)/sizeof(LEVEL_STATS[0]))) return "?";
    return LEVEL_STATS[axis].name;
}
float        level_stat_value_for(int axis, Rarity r) { return level_stat_value(axis, r); }

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
        /* slowmo pendant la cinematique de mort du boss : on ralentit le
         * gameplay a 30% pour laisser les fragments tomber au ralenti. */
        if (g->boss_death_t > 0.f) g->dt *= 0.30f;

        g->time += dt;

        poll_input(g, &quit);

        if (g->state == GS_TITLE) {
            bool start = (g->keys[SDL_SCANCODE_RETURN] && !g->keys_prev[SDL_SCANCODE_RETURN]) ||
                         (g->keys[SDL_SCANCODE_SPACE]  && !g->keys_prev[SDL_SCANCODE_SPACE]);
            /* mouse zones (alignees sur render_title : 4 lignes desormais) */
            int yA = INTERNAL_H/2 + 20;   /* JOUER */
            int yB = INTERNAL_H/2 + 36;   /* OPTIONS */
            int yC = INTERNAL_H/2 + 52;   /* AIDE */
            int yD = INTERNAL_H/2 + 68;   /* QUITTER */
            if (mouse_in_rect(g, INTERNAL_W/2 - 100, yA, 200, 12) && mouse_clicked(g)) start = true;
            if (mouse_in_rect(g, INTERNAL_W/2 - 100, yB, 200, 12) && mouse_clicked(g)) {
                g->opt_return = GS_TITLE; g->opt_section = 0; g->opt_cursor = 0;
                g->opt_waiting_rebind = false; g->state = GS_OPTIONS;
            }
            if (mouse_in_rect(g, INTERNAL_W/2 - 100, yC, 200, 12) && mouse_clicked(g))
                g->state = GS_HELP;
            if (mouse_in_rect(g, INTERNAL_W/2 - 100, yD, 200, 12) && mouse_clicked(g))
                quit = true;
            if (start) g->state = GS_HUB;
            if (g->keys[SDL_SCANCODE_H] && !g->keys_prev[SDL_SCANCODE_H]) g->state = GS_HELP;
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
        } else if (g->state == GS_CODEX) {
            update_codex(g);
        } else if (g->state == GS_CHOOSE_HERO) {
            update_choose_hero(g);
        } else if (g->state == GS_RUN) {
            g->run_time += g->dt;
            if (g->scroll_t > 0.f)         g->scroll_t -= g->dt;
            if (g->combo_callout_t > 0.f)  g->combo_callout_t -= g->dt;
            if (g->boss_intro_t > 0.f) g->boss_intro_t -= dt;
            if (g->boss_death_t > 0.f) g->boss_death_t -= dt;
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
            world_assets_tick(g);
            update_surfaces(g);
            toast_tick(g);
            /* killstreak / overdrive timers */
            if (g->killstreak_t > 0.f) {
                g->killstreak_t -= dt;
                if (g->killstreak_t <= 0.f) g->killstreak_count = 0;
            }
            if (g->overdrive_t > 0.f) g->overdrive_t -= dt;
            if (g->shake_t > 0.f) g->shake_t -= dt;
            if (g->player.hp <= 0.f) g->state = GS_DEAD;
            if (g->player.xp >= g->player.xp_to_next) {
                g->player.xp -= g->player.xp_to_next;
                g->player.xp_to_next = (int)(g->player.xp_to_next * 1.4f) + 1;
                g->player.level++;
                /* 8 axes de stats nommees (cf LEVEL_STATS dans apply_levelup_choice).
                 * Chaque choix tire un axe distinct + une rarete via la table
                 * elite par etage : c'est la meme logique probabiliste que
                 * pour le loot, donc legendaire reste rare et sent fort. */
                int statpool[8] = { 0, 1, 2, 3, 4, 5, 6, 7 };
                for (int i = 7; i > 0; i--) {
                    int j = rand() % (i + 1);
                    int t = statpool[i]; statpool[i] = statpool[j]; statpool[j] = t;
                }
                for (int c = 0; c < 3; c++) {
                    g->levelup_choice_kind[c] = 2;
                    g->levelup_choices[c]      = statpool[c];
                    g->levelup_choice_rarity[c]= (int)rarity_for_floor_elite(g->floor_index);
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
        else if (g->state == GS_CODEX)       render_codex(g);
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
            if (g->state == GS_RUN) toast_render(g);
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
