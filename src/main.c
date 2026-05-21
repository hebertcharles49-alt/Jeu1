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
                    case GS_RUN:        g->state = GS_PAUSE; break;
                    case GS_PAUSE:      g->state = GS_RUN; break;
                    case GS_HUB:
                        /* ESC dans un sous-panneau : ferme le panneau,
                         * sinon retour au titre. */
                        if (g->hub_sub_open != 0) g->hub_sub_open = 0;
                        else g->state = GS_TITLE;
                        break;
                    case GS_TITLE:      *quit = true; break;
                    case GS_HELP:       g->state = GS_TITLE; break;
                    case GS_LORE:       g->state = GS_TITLE; break;
                    case GS_OPTIONS:
                        settings_write(&g->settings);
                        g->state = g->opt_return ? g->opt_return : GS_TITLE;
                        break;
                    case GS_CHOOSE_HERO:g->state = GS_HUB; g->hub_sub_open = 0; hub_init(g); break;
                    case GS_CODEX:
                        if (g->codex_return == GS_TITLE) {
                            g->state = GS_TITLE;
                        } else {
                            g->state = GS_HUB; g->hub_sub_open = 0;
                            hub_init(g);
                        }
                        break;
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

/* 4 lignes a 1px sur les bords ecran, au meme inset. Sert au flash
 * vignette et au halo low-HP. */
static void draw_edge_lines(GfxCtx *gc, int inset, uint32_t col) {
    gfx_set_color(gc, col);
    gfx_fill_rect(gc, 0,                       inset,                    INTERNAL_W, 1);
    gfx_fill_rect(gc, 0,                       INTERNAL_H - 1 - inset,   INTERNAL_W, 1);
    gfx_fill_rect(gc, inset,                   0,                        1, INTERNAL_H);
    gfx_fill_rect(gc, INTERNAL_W - 1 - inset,  0,                        1, INTERNAL_H);
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

    /* equipement : stat de base + affixes ; les uniques
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
    p->shop_discount = sb.shop_discount;
    if (p->shop_discount < 0.f) p->shop_discount = 0.f;
    if (p->shop_discount > 1.f) p->shop_discount = 1.f;     /* cap 100% = shop gratuit */
    p->reroll_discount = sb.reroll_discount;
    if (p->reroll_discount < 0.f) p->reroll_discount = 0.f;
    if (p->reroll_discount > 1.f) p->reroll_discount = 1.f;
    p->inv_capacity_bonus = sb.inv_capacity_bonus;
    if (p->inv_capacity_bonus < 0) p->inv_capacity_bonus = 0;
    int cap_max = INVENTORY_MAX_SLOTS - 12;
    if (p->inv_capacity_bonus > cap_max) p->inv_capacity_bonus = cap_max;
    for (int ci = 0; ci < ENEMY_CAT_COUNT; ci++) {
        p->dmg_vs_cat[ci] = sb.dmg_vs_cat[ci];
    }
    p->coin_drop_mul = 1.f + sb.coin_drop_mul;
    if (p->coin_drop_mul < 0.f) p->coin_drop_mul = 0.f;
    p->xp_mul = 1.f + sb.xp_mul;
    if (p->xp_mul < 0.f) p->xp_mul = 0.f;
    p->pixie_on_kill_pct = sb.pixie_on_kill_pct;
    if (p->pixie_on_kill_pct < 0) p->pixie_on_kill_pct = 0;
    if (p->pixie_on_kill_pct > 100) p->pixie_on_kill_pct = 100;
    p->puddle_on_room = sb.puddle_on_room;
    p->no_atk_speed_cap = sb.no_atk_speed_cap;
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
    p->u_dash_pull          = sb.u_dash_pull;
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
    g->hub_sub_open = 0;
    hub_init(g);
}

void game_start_new_run(Game *g) {
    /* Sauvegarde du choix de heros + arme fait dans le HUB. game_start_new_run
     * memset le player a 0, donc on les remet apres. */
    HeroClass chosen_hero = g->player.hero;
    WeaponKind chosen_weapon = g->player.weapons[0].kind;
    Rarity chosen_rarity = g->player.weapons[0].rarity;
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
    /* seed de run : nouvelle a chaque debut. La rand de l OS sert de
     * source d entropie. La seed est affichee a l ecran pour permettre
     * de partager une run. */
    g->run_seed = (unsigned)time(NULL) ^ (unsigned)rand();
    srand(g->run_seed);
    /* Pre-roll des drops : on tire les milestones de kills auxquels
     * les talismans / uniques apparaitront. Bandes de [10..EST_MAX]
     * pour bien repartir les drops sur toute la run.
     *
     * EST_MAX estime le total de kills d une run complete (env. 25 par
     * etage * 10 etages = 250). Si la run est plus courte, les
     * derniers drops ne sortent simplement pas. */
    const int EST_MAX = 250;
    int band_t = EST_MAX / RUN_TALISMAN_MAX;
    for (int i = 0; i < RUN_TALISMAN_MAX; i++) {
        g->talisman_drops[i] = 5 + i * band_t + rand() % (band_t - 5);
    }
    g->talisman_drops_idx = 0;
    /* Pool d'elements de la run : 7 elements distincts parmi les 10
     * (FIRE..HOLY), tires par Fisher-Yates seede. Les 3 non tires
     * sont "en reserve" sur cette run. */
    {
        int all[10];
        int n_all = 0;
        for (int e = (int)EL_FIRE; e <= (int)EL_HOLY; e++) all[n_all++] = e;
        for (int i = n_all - 1; i > 0; i--) {
            int j = rand() % (i + 1);
            int t = all[i]; all[i] = all[j]; all[j] = t;
        }
        for (int i = 0; i < RUN_TALISMAN_MAX; i++) g->run_element_pool[i] = all[i];
    }
    int band_u = EST_MAX / RUN_UNIQUE_MAX;
    for (int i = 0; i < RUN_UNIQUE_MAX; i++) {
        g->unique_drops[i] = 20 + i * band_u + rand() % (band_u - 10);
    }
    g->unique_drops_idx = 0;
    g->killstreak_count = 0; g->killstreak_t = 0.f; g->overdrive_t = 0.f;
    g->dps_smooth = 0.f; g->dps_frame_acc = 0.f;
    /* unique-flag state : reset au demarrage (les charges seront
     * activees au premier point_in_room avec first_visit=true). */
    /* le player vient d etre memset a 0, donc phoenix_charge / last_stand
     * / kill_stack_count / hazard_stacks / last_stand_t sont deja a 0. */
    g->floor_index = 0;     /* commence sur l'arene-pivot a 7 portails */
    for (int i = 0; i < 5; i++) g->biome_cleared[i] = false;
    g->floors_visited = 0;
    g->shake_t = 0.f;
    g->portal_spawned = false;
    g->boss_intro_t = 0.f;
    g->boss_death_t = 0.f;
    g->shop_visits = 0;
    g->shop_reroll_idx = 0;
    g->shop_reroll_cost = shop_reroll_cost_at(0);
    g->combo_callout_t = 0.f;
    g->combo_callout_mask = 0;
    g->scroll_t = 0.f;
    g->current_attack_crit = false;

    Player *p = &g->player;
    /* Garde le heros choisi au HUB ; fallback hero_cursor pour anciens flots. */
    p->hero = (chosen_hero >= 0 && chosen_hero < HERO_COUNT)
              ? chosen_hero : (HeroClass)g->hero_cursor;
    p->r = 6.f;
    p->level = 1; p->xp = 0; p->xp_to_next = 6;
    p->souls = 0; p->coins = 0;
    p->active_weapon = 0;
    p->active_loop_idx  = -1;
    p->active_loop_mask = 0;
    g->enemy_alive_count = 0;

    /* Arme choisie au HUB sur slot 0 ; slot 1 = poings (deuxieme arme a
     * trouver dans la run). */
    WeaponKind wk = (chosen_weapon > W_FISTS && chosen_weapon < W_COUNT)
                    ? chosen_weapon : W_FISTS;
    weapon_init_defaults(&p->weapons[0], wk);
    p->weapons[0].owned = true;
    p->weapons[0].rarity = (chosen_rarity >= 0 && chosen_rarity < R_COUNT)
                            ? chosen_rarity : R_COMMON;
    weapon_init_defaults(&p->weapons[1], W_FISTS);
    p->weapons[1].owned = true;
    /* FORGE bonus : meta.weapon_dmg_bonus[kind] x5 ajoute a base_dmg
     * pour chaque arme detenue au demarrage. (Pour le moment seul
     * W_FISTS est equippe, mais on l applique aussi quand un drop
     * d arme arrive -- cf player.c PU_WEAPON.) */
    for (int s = 0; s < WEAPON_SLOTS; s++) {
        int b = g->meta.weapon_dmg_bonus[p->weapons[s].kind];
        if (b > 0) p->weapons[s].base_dmg += b * 5.f;
    }

    game_recompute_player_stats(g);
    p->hp = p->maxhp;

    /* seed du donjon : derivee du run_seed + floor pour que chaque etage
     * d une meme seed soit reproductible et different. */
    dungeon_generate(&g->dungeon, g->floor_index,
                     g->run_seed * 2654435761u + (unsigned)g->floor_index);
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
    int prev = g->floor_index;
    /* Boss de biome (floors 2,4,6,8,10) : marque le biome cleared et
     * retourne a l'arene-pivot etage 0. Le joueur choisit le prochain
     * biome ou l'archimage si tous cleared. */
    if (prev >= 2 && prev <= 10 && (prev % 2) == 0) {
        int biome = (prev - 1) / 2;        /* 1->0, 2->0, 3->1, 4->1, ... */
        if (biome >= 0 && biome < 5) {
            g->biome_cleared[biome] = true;
            log_push(g, 0x80FF80FF, "Biome %d vaincu", biome + 1);
        }
        game_jump_to_floor(g, 0);
        return;
    }
    /* Sortie de l'archimage (floor 11 -> victory geree par game_jump_to_floor) */
    g->floor_index++;
    if (g->floor_index > MAX_FLOORS) {
        g->meta.victories++;
        save_write(&g->meta);
        g->state = GS_VICTORY;
        return;
    }
    if (g->floor_index >= 1 && g->floor_index <= 10) g->floors_visited++;
    log_push(g, 0xFFE090FF, "Etage %d", g->floor_index);
    /* seed du donjon : derivee du run_seed + floor pour que chaque etage
     * d une meme seed soit reproductible et different. */
    dungeon_generate(&g->dungeon, g->floor_index,
                     g->run_seed * 2654435761u + (unsigned)g->floor_index);
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

/* Jump direct vers un etage cible (depuis floor 0 ou apres boss). */
void game_jump_to_floor(Game *g, int target) {
    if (target > MAX_FLOORS) {
        g->meta.victories++;
        save_write(&g->meta);
        g->state = GS_VICTORY;
        return;
    }
    if (target < 0) target = 0;
    /* Compte les visites des etages de combat (1..10). Le scaling
     * des mobs est base la-dessus, pas floor_index, pour que l'ordre
     * choisi par le joueur ne casse pas la progression. */
    if (target >= 1 && target <= 10) g->floors_visited++;
    g->floor_index = target;
    if (target == 0)             log_push(g, 0xFFE090FF, "Salle des sept portails");
    else if (target == MAX_FLOORS) log_push(g, 0xFFA040FF, "L'Archimage t'attend...");
    else                         log_push(g, 0xFFE090FF, "Etage %d", target);
    dungeon_generate(&g->dungeon, g->floor_index,
                     g->run_seed * 2654435761u + (unsigned)g->floor_index);
    g->player.x = g->dungeon.spawn_x * TILE + TILE / 2;
    g->player.y = g->dungeon.spawn_y * TILE + TILE / 2;
    memset(g->enemies, 0, sizeof(g->enemies));
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
    g->player.hp += 15.f;
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

int forge_level(const MetaSave *m, WeaponKind k) {
    if (k < 0 || k >= W_COUNT) return 0;
    return m->weapon_dmg_bonus[k];
}

int forge_cost(const MetaSave *m, WeaponKind k) {
    int lvl = forge_level(m, k);
    if (lvl >= FORGE_MAX_LEVEL) return 0;     /* max */
    return (lvl + 1) * 40;
}

bool forge_buy(Game *g, WeaponKind k) {
    int cost = forge_cost(&g->meta, k);
    if (cost <= 0 || g->meta.shards < cost) return false;
    /* on n autorise l upgrade que sur les armes deja decouvertes. */
    if (k != W_FISTS && !g->meta.weapon_discovered[k]) return false;
    g->meta.shards -= cost;
    g->meta.weapon_dmg_bonus[k]++;
    save_write(&g->meta);
    sfx_play(g, SFX_COIN);
    return true;
}

/* === HUB walkable ============================
 * GS_HUB rend la meme world-pipeline que GS_RUN mais avec :
 *   - un donjon special : 1 grande salle vide, pas d ennemis
 *   - 5 NPC-batiments fixes
 *   - update_player pour la marche, pas d update_weapons/enemies
 *   - check de proximite + prompt [E] pour interagir
 *
 * sub_id : 0 = DONJON (start run), -1 = TAVERNE (choose_hero),
 *          1 = TEMPLE, 2 = FORGE, 3 = LICHE (stub).
 * ============================================================ */
struct HubBuilding {
    int   sub_id;
    float x, y;          /* world pixel coords */
    float r;             /* rayon d interaction */
    const char *name;
};

#define HUB_ROOM_W 24
#define HUB_ROOM_H 14
#define HUB_BLD_R  32.f

static const HubBuilding HUB_BUILDINGS[5] = {
    /* coordonnees en TILE * TILE -- positions placees apres hub_init carve */
    {  1,  (28 - HUB_ROOM_W/2 +  3) * TILE, (28 - HUB_ROOM_H/2 + 2) * TILE, HUB_BLD_R, "TEMPLE"  },
    {  0,  (28)                     * TILE, (28 - HUB_ROOM_H/2 + 1) * TILE, HUB_BLD_R, "DONJON"  },
    { -1,  (28 + HUB_ROOM_W/2 -  3) * TILE, (28 - HUB_ROOM_H/2 + 2) * TILE, HUB_BLD_R, "TAVERNE" },
    {  2,  (28 - HUB_ROOM_W/2 +  4) * TILE, (28 + HUB_ROOM_H/2 - 2) * TILE, HUB_BLD_R, "FORGE"   },
    {  3,  (28 + HUB_ROOM_W/2 -  4) * TILE, (28 + HUB_ROOM_H/2 - 2) * TILE, HUB_BLD_R, "LICHE"   },
};

int hub_building_count(void) { return 5; }
const HubBuilding *hub_building_get(int i) {
    if (i < 0 || i >= 5) return NULL;
    return &HUB_BUILDINGS[i];
}
float        hub_building_x      (const HubBuilding *b) { return b ? b->x : 0.f; }
float        hub_building_y      (const HubBuilding *b) { return b ? b->y : 0.f; }
float        hub_building_r      (const HubBuilding *b) { return b ? b->r : 0.f; }
const char  *hub_building_name   (const HubBuilding *b) { return b ? b->name : ""; }
int          hub_building_sub_id (const HubBuilding *b) { return b ? b->sub_id : -2; }

void hub_init(Game *g) {
    /* Reset des pools : pas d ennemis / projectiles / surfaces / pickups
     * dans le cimetiere. On garde les charges run et le player. */
    memset(g->enemies, 0, sizeof(g->enemies));
    memset(g->projectiles, 0, sizeof(g->projectiles));
    memset(g->pickups, 0, sizeof(g->pickups));
    memset(g->surfaces, 0, sizeof(g->surfaces));
    memset(g->fairies, 0, sizeof(g->fairies));
    g->enemy_alive_count = 0;
    g->portal_spawned = false;

    /* Donjon : une seule grande salle centree. */
    int prev_gen = g->dungeon.gen_id;
    memset(&g->dungeon, 0, sizeof(g->dungeon));
    g->dungeon.gen_id = prev_gen + 1;
    g->dungeon.level_index = 0;       /* 0 = hub (non joue) */
    for (int y = 0; y < MAP_H; y++)
        for (int x = 0; x < MAP_W; x++)
            g->dungeon.tiles[y][x] = T_WALL;
    int rx = 28 - HUB_ROOM_W / 2;
    int ry = 28 - HUB_ROOM_H / 2;
    for (int y = ry; y < ry + HUB_ROOM_H; y++)
        for (int x = rx; x < rx + HUB_ROOM_W; x++)
            g->dungeon.tiles[y][x] = T_FLOOR;
    /* torches aux coins pour l ambiance */
    g->dungeon.tiles[ry + 1][rx + 1] = T_TORCH;
    g->dungeon.tiles[ry + 1][rx + HUB_ROOM_W - 2] = T_TORCH;
    g->dungeon.tiles[ry + HUB_ROOM_H - 2][rx + 1] = T_TORCH;
    g->dungeon.tiles[ry + HUB_ROOM_H - 2][rx + HUB_ROOM_W - 2] = T_TORCH;
    /* runes sous la porte du donjon (signale l exit) */
    g->dungeon.tiles[ry + 1][rx + HUB_ROOM_W / 2] = T_RUNE;
    g->dungeon.spawn_x = 28;
    g->dungeon.spawn_y = 28;
    g->dungeon.rooms[0].x = rx; g->dungeon.rooms[0].y = ry;
    g->dungeon.rooms[0].w = HUB_ROOM_W; g->dungeon.rooms[0].h = HUB_ROOM_H;
    g->dungeon.rooms[0].cleared = true;
    g->dungeon.rooms[0].visited = true;
    g->dungeon.rooms[0].enemies_to_spawn = 0;
    g->dungeon.rooms[0].is_boss_room = false;
    g->dungeon.room_count = 1;

    /* place le joueur au centre, regarde le sud (vers la porte) */
    g->player.x = g->dungeon.spawn_x * TILE + TILE / 2;
    g->player.y = (28 + HUB_ROOM_H / 2 - 3) * TILE;
    g->player.vx = g->player.vy = 0.f;
    g->player.aim_x = g->player.x;
    g->player.aim_y = g->player.y - 30.f;
    g->player.dash_t = 0.f;
    g->player.invuln_t = 0.f;
    g->player.anim_t = 0.f;
    /* "Paysan" : reset des slots d'arme aux poings, on ne peut pas
     * attaquer ni entrer dans le donjon sans passer par FORGE + TAVERNE. */
    g->hub_weapon_chosen = false;
    g->hub_hero_chosen = false;
    weapon_init_defaults(&g->player.weapons[0], W_FISTS);
    g->player.weapons[0].owned = true;
    g->player.weapons[1].kind = W_FISTS;
    g->player.weapons[1].owned = false;
    g->player.active_weapon = 0;
    /* heros pre-rempli mais marque non-choisi tant que la TAVERNE n'a pas
     * ete visitee : permet a draw_player_3d de rendre un paysan generique. */
    if (g->player.hero < 0 || g->player.hero >= HERO_COUNT)
        g->player.hero = HERO_GUERRIER;
    if (g->player.maxhp <= 0.f) {
        g->player.maxhp = 100.f;
        g->player.hp = 100.f;
        g->player.speed = 110.f;
        g->player.r = 6.f;
    }
    /* boss_dead pour ne pas declencher l etage suivant accidentellement */
    g->dungeon.boss_dead = false;
}

/* FORGE : choisit l'arme de la run. 5 options (Epee, Bouclier, Arc,
 * Baguette, Hache). Une seule selection : assigne le slot 0 et marque
 * hub_weapon_chosen. */
static void hub_forge_pick(Game *g, int idx) {
    static const WeaponKind PICKS[5] = { W_SWORD, W_SHIELD, W_BOW, W_WAND, W_AXE };
    if (idx < 0 || idx >= 5) return;
    WeaponKind k = PICKS[idx];
    weapon_init_defaults(&g->player.weapons[0], k);
    g->player.weapons[0].owned = true;
    /* applique le bonus FORGE meta */
    int b = g->meta.weapon_dmg_bonus[k];
    if (b > 0) g->player.weapons[0].base_dmg += b * 5.f;
    g->hub_weapon_chosen = true;
    g->hub_sub_open = 0;
    sfx_play(g, SFX_LEVELUP);
}

static void update_hub(Game *g) {
    /* sous-panneau ouvert : input dedie + ESC ferme. */
    if (g->hub_sub_open != 0) {
        if (g->hub_sub_open == 1 || g->hub_sub_open == 3) {
            /* TEMPLE / LICHE : work in progress, juste un closer */
            if ((g->keys[SDL_SCANCODE_RETURN] && !g->keys_prev[SDL_SCANCODE_RETURN]) ||
                (g->keys[SDL_SCANCODE_SPACE]  && !g->keys_prev[SDL_SCANCODE_SPACE])  ||
                mouse_clicked(g)) {
                g->hub_sub_open = 0;
            }
        } else if (g->hub_sub_open == 2) {
            /* FORGE : 5 armes (excl. fists). Sub_cursor 0..4. */
            const int N = 5;
            int w = 280;
            int x = INTERNAL_W / 2 - w / 2;
            int y = INTERNAL_H / 2 - 110;
            int rowh = 22;
            for (int k = 0; k < N; k++) {
                int sy = y + 50 + k * rowh;
                if (mouse_in_rect(g, x + 10, sy, w - 20, rowh - 2)) {
                    g->hub_sub_cursor = k;
                    if (mouse_clicked(g)) hub_forge_pick(g, k);
                }
            }
            if (g->keys[SDL_SCANCODE_UP]   && !g->keys_prev[SDL_SCANCODE_UP])
                g->hub_sub_cursor = (g->hub_sub_cursor + N - 1) % N;
            if (g->keys[SDL_SCANCODE_DOWN] && !g->keys_prev[SDL_SCANCODE_DOWN])
                g->hub_sub_cursor = (g->hub_sub_cursor + 1) % N;
            if ((g->keys[SDL_SCANCODE_RETURN] && !g->keys_prev[SDL_SCANCODE_RETURN]) ||
                (g->keys[SDL_SCANCODE_SPACE]  && !g->keys_prev[SDL_SCANCODE_SPACE]))
                hub_forge_pick(g, g->hub_sub_cursor);
        }
        return;
    }

    /* Pas de sous-panneau : on joue le hub walkable. */
    update_player(g);
    /* Cherche un batiment dans le rayon. On stocke le sub_id sur
     * un static local (utilise par render_world pour le prompt).
     * Note : on evite l'identifiant `near` (macro heritee de windows.h
     * dans certains toolchains). */
    int near_idx = -1;
    for (int i = 0; i < 5; i++) {
        const HubBuilding *b = &HUB_BUILDINGS[i];
        float dx = b->x - g->player.x;
        float dy = b->y - g->player.y;
        if (dx * dx + dy * dy < b->r * b->r) { near_idx = i; break; }
    }
    /* expose au rendu via un champ Game pour le prompt overlay. */
    g->hub_cursor = near_idx;
    /* E ou interact : ouvre le panneau ou demarre la run. */
    SDL_Scancode kinter = g->settings.keys[BIND_INTERACT];
    if (kinter == SDL_SCANCODE_UNKNOWN) kinter = SDL_SCANCODE_E;
    bool press_e = (g->keys[kinter] && !g->keys_prev[kinter]);
    if (press_e && near_idx >= 0) {
        int sid = HUB_BUILDINGS[near_idx].sub_id;
        switch (sid) {
            case  0: /* DONJON : verrouille tant que weapon/hero pas choisis */
                if (g->hub_weapon_chosen && g->hub_hero_chosen) {
                    game_start_new_run(g);
                } else {
                    sfx_play_ex(g, SFX_SWING, 0.5f, 0.7f);   /* "non" */
                }
                break;
            case -1: /* TAVERNE -> choisis ta classe */
                g->state = GS_CHOOSE_HERO;
                break;
            case  1: g->hub_sub_open = 1; g->hub_sub_cursor = 0; break;   /* TEMPLE WIP */
            case  2: g->hub_sub_open = 2; g->hub_sub_cursor = 0; break;   /* FORGE */
            case  3: g->hub_sub_open = 3; g->hub_sub_cursor = 0; break;   /* LICHE WIP */
        }
    }
    /* raccourcis */
    if (g->keys[SDL_SCANCODE_K] && !g->keys_prev[SDL_SCANCODE_K]) {
        g->state = GS_CODEX;
        g->codex_tab = 0; g->codex_cursor = 0; g->codex_scroll = 0;
        g->codex_return = GS_HUB;
    }
    if (g->keys[SDL_SCANCODE_O] && !g->keys_prev[SDL_SCANCODE_O]) {
        g->opt_return = GS_HUB; g->opt_section = 0; g->opt_cursor = 0;
        g->opt_waiting_rebind = false; g->state = GS_OPTIONS;
    }
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
            return;
        }
        if (g->meta.hero_unlocked[g->hero_cursor]) {
            /* Selectionne la classe et revient au HUB (le run sera lance
             * via la porte du DONJON apres avoir aussi choisi une arme). */
            g->player.hero = (HeroClass)g->hero_cursor;
            g->hub_hero_chosen = true;
            /* applique les stats de heros immediatement pour que le PJ
             * marche dans le hub avec le bon look / vitesse / PV. */
            game_recompute_player_stats(g);
            g->player.hp = g->player.maxhp;
            sfx_play(g, SFX_LEVELUP);
            g->state = GS_HUB;
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
            /* menu : 5 entrees (JOUER / CODEX / OPTIONS / AIDE / QUITTER).
             * Render maj du title_cursor au survol souris. Ici : nav clavier
             * + activation (ENTER / SPACE / clic). */
            const int N_MENU = 5;
            if ((g->keys[SDL_SCANCODE_UP]   && !g->keys_prev[SDL_SCANCODE_UP])  ||
                (g->keys[SDL_SCANCODE_W]    && !g->keys_prev[SDL_SCANCODE_W]))
                g->title_cursor = (g->title_cursor + N_MENU - 1) % N_MENU;
            if ((g->keys[SDL_SCANCODE_DOWN] && !g->keys_prev[SDL_SCANCODE_DOWN]) ||
                (g->keys[SDL_SCANCODE_S]    && !g->keys_prev[SDL_SCANCODE_S]))
                g->title_cursor = (g->title_cursor + 1) % N_MENU;

            bool activate =
                (g->keys[SDL_SCANCODE_RETURN] && !g->keys_prev[SDL_SCANCODE_RETURN]) ||
                (g->keys[SDL_SCANCODE_SPACE]  && !g->keys_prev[SDL_SCANCODE_SPACE]);
            /* clic sur la ligne focused = activate */
            int yA = INTERNAL_H/2 + 14;
            int rowh = 14;
            int yi = yA + g->title_cursor * rowh;
            if (mouse_in_rect(g, INTERNAL_W/2 - 100, yi, 200, 12) && mouse_clicked(g))
                activate = true;

            if (activate) {
                switch (g->title_cursor) {
                    case 0: /* JOUER */
                        g->state = GS_HUB; g->hub_sub_open = 0; hub_init(g);
                        break;
                    case 1: /* CODEX */
                        g->state = GS_CODEX;
                        g->codex_tab = 0; g->codex_cursor = 0; g->codex_scroll = 0;
                        g->codex_return = GS_TITLE;
                        break;
                    case 2: /* OPTIONS */
                        g->opt_return = GS_TITLE;
                        g->opt_section = 0; g->opt_cursor = 0;
                        g->opt_waiting_rebind = false;
                        g->state = GS_OPTIONS;
                        break;
                    case 3: /* AIDE */
                        g->state = GS_HELP;
                        break;
                    case 4: /* QUITTER */
                        quit = true;
                        break;
                }
            }
            /* raccourcis directs (touches dediees) */
            if (g->keys[SDL_SCANCODE_H] && !g->keys_prev[SDL_SCANCODE_H]) g->state = GS_HELP;
            if (g->keys[SDL_SCANCODE_O] && !g->keys_prev[SDL_SCANCODE_O]) {
                g->opt_return = GS_TITLE;
                g->opt_section = 0; g->opt_cursor = 0;
                g->opt_waiting_rebind = false;
                g->state = GS_OPTIONS;
            }
            if (g->keys[SDL_SCANCODE_K] && !g->keys_prev[SDL_SCANCODE_K]) {
                g->state = GS_CODEX;
                g->codex_tab = 0; g->codex_cursor = 0; g->codex_scroll = 0;
                g->codex_return = GS_TITLE;
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
            /* Transition de portail : fondu au noir + label biome.
             * Pendant la transition, tout est freeze (pas d'update). */
            if (g->portal_transition_t > 0.f) {
                g->portal_transition_t -= dt;
                if (g->portal_transition_t <= 0.f) {
                    g->portal_transition_t = 0.f;
                    int target = g->portal_transition_target;
                    g->portal_transition_target = 0;
                    g->portal_transition_label[0] = '\0';
                    game_jump_to_floor(g, target);
                }
                gfx_frame_begin(g->renderer);
                /* re-render world frozen + overlay fade pour visu fluide */
                bool sw = true; (void)sw;
                if (g->state == GS_RUN) {
                    int sx_, sy_; apply_shake(g, &sx_, &sy_);
                    g->camera_x = (float)sx_; g->camera_y = (float)sy_;
                    render_world(g);
                }
                gfx_ui_begin(g->renderer);
                /* fondu : alpha pic a 1.0 a t=1.5s (mi-chemin), fade en
                 * et hors. Le label apparait en pic. */
                float t = g->portal_transition_t;
                float pulse = 1.f - fabsf((1.5f - t) / 1.5f);
                if (pulse < 0.f) pulse = 0.f;
                if (pulse > 1.f) pulse = 1.f;
                uint8_t alpha = (uint8_t)(pulse * 240);
                gfx_set_blend(g->renderer, true);
                uint32_t bcol = (uint32_t)((0x00u << 24) | (0x00u << 16)
                                          | (0x00u << 8) | (uint32_t)alpha);
                gfx_set_color(g->renderer, bcol);
                gfx_fill_rect(g->renderer, 0, 0, INTERNAL_W, INTERNAL_H);
                gfx_set_blend(g->renderer, false);
                /* label centre, apparait quand pulse > 0.4 */
                if (pulse > 0.35f && g->portal_transition_label[0]) {
                    const char *lbl = g->portal_transition_label;
                    int tw = text_width(lbl);
                    /* shadow + texte ; couleur fade dans pulse */
                    float la_f = ((pulse - 0.35f) / 0.65f) * 255.f;
                    if (la_f > 255.f) la_f = 255.f;
                    uint8_t la = (uint8_t)la_f;
                    uint32_t tc = (uint32_t)((0xFFu << 24) | (0xE0u << 16)
                                            | (0x80u << 8) | (uint32_t)la);
                    text_draw(g->renderer,
                              INTERNAL_W/2 - tw/2 + 1,
                              INTERNAL_H/2 - 4 + 1, lbl, 0x000000FF);
                    text_draw(g->renderer,
                              INTERNAL_W/2 - tw/2,
                              INTERNAL_H/2 - 4, lbl, tc);
                    /* sous-titre "Etage X" */
                    char sub[40];
                    int tgt = g->portal_transition_target;
                    if (tgt == MAX_FLOORS)
                        snprintf(sub, sizeof(sub), "L'arene supreme");
                    else
                        snprintf(sub, sizeof(sub), "Etage %d", tgt);
                    int sw_ = text_width(sub);
                    text_draw(g->renderer,
                              INTERNAL_W/2 - sw_/2,
                              INTERNAL_H/2 + 8, sub,
                              (uint32_t)((0xC0u << 24) | (0xC0u << 16)
                                       | (0xCCu << 8) | (uint32_t)la));
                }
                gfx_ui_end(g->renderer);
                gfx_frame_end(g->renderer);
                SDL_GL_SwapWindow(g->window);
                continue;       /* skip le reste de la frame */
            }
            g->run_time += g->dt;
            if (g->scroll_t > 0.f)         g->scroll_t -= g->dt;
            if (g->combo_callout_t > 0.f)  g->combo_callout_t -= g->dt;
            if (g->boss_intro_t > 0.f) g->boss_intro_t -= dt;
            if (g->boss_death_t > 0.f) g->boss_death_t -= dt;
            if (g->flash_t > 0.f)      g->flash_t -= dt;
            /* decay global du speech de l'archimage : doit s'effacer
             * meme s'il est en stasis / interphase / mort. */
            if (g->arch_speech_t > 0.f) {
                g->arch_speech_t -= dt;
                if (g->arch_speech_t <= 0.f) g->arch_speech[0] = 0;
            }
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
            log_tick(g, dt);
            /* killstreak / overdrive timers */
            /* DPS smooth : exponentiel sur 3s. Si dt=0 (hitstop) on saute. */
            if (g->dt > 0.f) {
                float instant_dps = g->dps_frame_acc / g->dt;
                g->dps_smooth = g->dps_smooth * 0.92f + instant_dps * 0.08f;
                g->dps_frame_acc = 0.f;
            }
            if (g->killstreak_t > 0.f) {
                g->killstreak_t -= dt;
                if (g->killstreak_t <= 0.f) g->killstreak_count = 0;
            }
            if (g->overdrive_t > 0.f) g->overdrive_t -= dt;
            if (g->shake_t > 0.f) g->shake_t -= dt;
            /* heartbeat audio quand PV < 25%. Periode 0.95s, deux thumps
             * rapproches (alignes sur la pulsation visuelle main.c). */
            if (g->player.maxhp > 0.f) {
                float ratio = g->player.hp / g->player.maxhp;
                static float hb_t = 0.f;
                static int   hb_phase = 0;     /* 0 = avant boum1, 1 = avant boum2 */
                if (ratio < 0.25f && ratio > 0.f) {
                    hb_t += dt;
                    float danger = (0.25f - ratio) / 0.25f;
                    float vol = 0.4f + danger * 0.5f;
                    if (hb_phase == 0 && hb_t >= 0.10f) {
                        sfx_play_ex(g, SFX_HEARTBEAT, 1.0f, vol);
                        hb_phase = 1;
                    } else if (hb_phase == 1 && hb_t >= 0.27f) {
                        sfx_play_ex(g, SFX_HEARTBEAT, 1.05f, vol * 0.75f);
                        hb_phase = 2;
                    } else if (hb_t >= 0.95f) {
                        hb_t = 0.f; hb_phase = 0;
                    }
                } else {
                    hb_t = 0.f; hb_phase = 0;
                }
            }
            if (g->player.hp <= 0.f) g->state = GS_DEAD;
            if (g->player.xp >= g->player.xp_to_next) {
                g->player.xp -= g->player.xp_to_next;
                g->player.xp_to_next = (int)(g->player.xp_to_next * 1.4f) + 1;
                g->player.level++;
                log_push(g, 0xFFD040FF, "Niveau %d", g->player.level);
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
                    {
                        int prog = g->floors_visited;
                        if (prog < 1) prog = 1;
                        if (prog > 10) prog = 10;
                        g->levelup_choice_rarity[c]= (int)rarity_for_floor_elite(prog);
                    }
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
        } else if (g->state == GS_PAUSE) {
            /* pause : menu Reprendre / Abandonner.
             * pause_cursor 0 = Reprendre, 1 = Abandonner. */
            const int N = 2;
            int cy0 = INTERNAL_H/2 + 10;
            int rowh = 18;
            for (int i = 0; i < N; i++) {
                if (mouse_in_rect(g, INTERNAL_W/2 - 100, cy0 + i * rowh, 200, 14))
                    g->pause_cursor = i;
            }
            if ((g->keys[SDL_SCANCODE_UP]   && !g->keys_prev[SDL_SCANCODE_UP])  ||
                (g->keys[SDL_SCANCODE_W]    && !g->keys_prev[SDL_SCANCODE_W]))
                g->pause_cursor = (g->pause_cursor + N - 1) % N;
            if ((g->keys[SDL_SCANCODE_DOWN] && !g->keys_prev[SDL_SCANCODE_DOWN]) ||
                (g->keys[SDL_SCANCODE_S]    && !g->keys_prev[SDL_SCANCODE_S]))
                g->pause_cursor = (g->pause_cursor + 1) % N;
            bool act = (g->keys[SDL_SCANCODE_RETURN] && !g->keys_prev[SDL_SCANCODE_RETURN])
                    || (g->keys[SDL_SCANCODE_SPACE]  && !g->keys_prev[SDL_SCANCODE_SPACE])
                    || (mouse_in_rect(g, INTERNAL_W/2 - 100,
                                      cy0 + g->pause_cursor * rowh, 200, 14)
                        && mouse_clicked(g));
            if (act) {
                if (g->pause_cursor == 0) {
                    g->state = GS_RUN;        /* reprendre */
                } else {
                    game_to_hub(g);           /* abandonner */
                }
            }
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
                           g->state == GS_HUB || g->state == GS_PAUSE ||
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
                g->state == GS_DEAD || g->state == GS_VICTORY ||
                g->state == GS_PAUSE) {
                render_world_overlay_ui(g);
            }
            render_hud(g);
            if (g->state == GS_RUN) toast_render(g);
            if (g->state == GS_RUN || g->state == GS_PAUSE) log_render(g);
            if (g->state == GS_PAUSE) render_pause(g);
            if (g->state == GS_LEVELUP) render_levelup(g);
            if (g->state == GS_DEAD)    render_dead(g);
            if (g->state == GS_VICTORY) render_victory(g);
            /* === FLASH ROUGE A L IMPACT ===
             * 1. plein-ecran rouge translucide (peak 220 a flash_t max,
             *    fade quadratique pour relief sur le maximum)
             * 2. vignette pulse : 4 bandes laterales rouges qui assombrissent
             *    les bords et "respirent" — donne le punch sans noyer le jeu
             * 3. fausse aberration chromatique : un voile bleu offset de
             *    quelques pixels sur les bords (cheap RGB split sans shader)
             */
            if (g->flash_t > 0.f) {
                float ref = (g->flash_t > 0.20f) ? g->flash_t : 0.20f;
                float k = g->flash_t / ref; if (k < 0.f) k = 0.f; if (k > 1.f) k = 1.f;
                float kq = k * k;          /* peak rapide, fade soft */
                gfx_set_blend(g->renderer, true);
                /* (1) vignette epaisse rouge (plus de wash plein ecran -- le
                 * voile rouge entier etait trop agressif, il aveuglait). On
                 * garde uniquement les bandes laterales pour le punch sans
                 * cacher l'action centrale. */
                int vmax = (int)(60.f * k);
                if (vmax > 0) {
                    for (int s = 0; s < 6; s++) {
                        int band = vmax * (s + 1) / 6;
                        int a = (int)(35.f + 35.f * s) * (int)(k * 255.f) / 255;
                        if (a < 0) a = 0;
                        if (a > 255) a = 255;
                        uint32_t col = (uint32_t)((0x60u << 24) | (0x08u << 16) | (0x10u << 8) | (uint32_t)a);
                        draw_edge_lines(g->renderer, band - 1, col);
                    }
                }
                /* (3) RGB split : voile cyan a droite + rouge a gauche, cheap */
                int split = (int)(4.f * kq);
                if (split > 0) {
                    int a = (int)(60.f * kq);
                    uint32_t cyan = (uint32_t)((0x00u << 24) | (0xC0u << 16) | (0xFFu << 8) | (uint32_t)a);
                    uint32_t red  = (uint32_t)((0xFFu << 24) | (0x20u << 16) | (0x20u << 8) | (uint32_t)a);
                    gfx_set_color(g->renderer, cyan);
                    gfx_fill_rect(g->renderer, INTERNAL_W - split, 0, split, INTERNAL_H);
                    gfx_set_color(g->renderer, red);
                    gfx_fill_rect(g->renderer, 0, 0, split, INTERNAL_H);
                }
                gfx_set_blend(g->renderer, false);
            }
            /* === HALO LOW-HP : pulse rouge persistant quand PV < 25%, sync
             * sur un "battement de coeur" doux (~1 Hz combine). Le pulse
             * a deux pics rapproches puis pause -- comme un coeur, pas un
             * sinus pur. Signale en continu le danger. */
            if (g->state == GS_RUN && g->player.maxhp > 0.f) {
                float ratio = g->player.hp / g->player.maxhp;
                if (ratio < 0.25f && ratio > 0.f) {
                    float danger = (0.25f - ratio) / 0.25f;
                    float ph = fmodf(g->time, 0.95f) / 0.95f;
                    /* deux gaussiennes : "boum-boum" puis silence */
                    float p1 = expf(-90.f * (ph - 0.10f) * (ph - 0.10f));
                    float p2 = expf(-90.f * (ph - 0.28f) * (ph - 0.28f));
                    float heart = p1 + 0.65f * p2;
                    if (heart > 1.f) heart = 1.f;
                    gfx_set_blend(g->renderer, true);
                    int N = 14;
                    for (int s = 0; s < N; s++) {
                        float t01 = (float)(N - s) / (float)N;
                        int a = (int)(75.f * t01 * t01 * heart * danger);
                        if (a <= 0) continue;
                        if (a > 200) a = 200;
                        uint32_t col = (uint32_t)((0xC0u << 24) | (0x10u << 16) | (0x20u << 8) | (uint32_t)a);
                        draw_edge_lines(g->renderer, s, col);
                    }
                    gfx_set_blend(g->renderer, false);
                }
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
