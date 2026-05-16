/*
 * game.h - types et constantes du doom-hybride "Crucible"
 */
#ifndef GAME_H
#define GAME_H

#include <SDL.h>
#include <stdbool.h>
#include <stdint.h>

/* type opaque du contexte graphique GL3.3 (defini dans gfx.h) */
typedef struct GfxCtx GfxCtx;

#define INTERNAL_W 640
#define INTERNAL_H 360
#define WINDOW_SCALE 2
#define WINDOW_W (INTERNAL_W * WINDOW_SCALE)
#define WINDOW_H (INTERNAL_H * WINDOW_SCALE)

#define TILE 16
#define MAP_W 56
#define MAP_H 56

#define MAX_ENEMIES 256
#define MAX_PROJECTILES 1024
#define MAX_PARTICLES 2048
#define MAX_PICKUPS 96
#define MAX_FAIRIES 32
#define MAX_DMGNUM 128

#define WEAPON_SLOTS 2
#define MAX_ELEMENTS_PER_WEAPON 3

#define INVENTORY_SLOTS 12
#define EQUIP_SLOTS 6     /* helm, chest, legs, boots, belt, gloves */

/* mapping inv_cursor : 0-11 sac, 12-17 equipement, 18-19 armes,
 * 20-22 talismans arme 0, 23-25 talismans arme 1. */
#define INV_CURSOR_BAG_BASE       0
#define INV_CURSOR_EQUIP_BASE     12
#define INV_CURSOR_WEAPON_BASE    18
#define INV_CURSOR_TALISMAN_BASE  20
#define INV_CURSOR_MAX            26

#define MAX_FLOORS 10

/* ---------- Elements ---------- */
typedef enum {
    EL_NONE = 0,
    EL_FIRE,
    EL_WATER,
    EL_EARTH,
    EL_LIGHTNING,
    EL_AIR,
    EL_VOID,
    EL_FAE,
    EL_STEEL,        /* Acier */
    EL_DARK,         /* Tenebres */
    EL_HOLY,         /* Sacre */
    EL_COUNT
} Element;

/* ---------- Rarity (avant Weapon car utilisee dedans) ---------- */
typedef enum {
    R_COMMON = 0,
    R_MAGIC,
    R_RARE,
    R_EPIC,
    R_LEGENDARY,
    R_COUNT
} Rarity;

/* ---------- Weapons ---------- */
typedef enum {
    W_FISTS = 0,
    W_SWORD,
    W_SHIELD,
    W_BOW,
    W_WAND,
    W_AXE,
    W_COUNT
} WeaponKind;

typedef struct {
    WeaponKind kind;
    bool       owned;
    float      cooldown;
    float      base_cd;
    float      base_dmg;
    float      base_range;
    Rarity     rarity;       /* qualite : determine le nombre de slots talisman
                                R_COMMON/MAGIC -> 1, RARE/EPIC -> 2, LEGENDARY -> 3 */
    Element    elements[MAX_ELEMENTS_PER_WEAPON];
    int        element_count;
} Weapon;

/* ---------- Triple Feedback Loop ----------
 * Etat persistant d'un triple combo : monte sur hit/kill, decroit en
 * dehors du combat, declenche un mode "overload" au-dela d'un seuil. */
typedef struct {
    float intensity;    /* 0.0 -> 2.0 ; monte sur hit/kill */
    float decay_rate;   /* copie depuis TripleLoopDef a l'activation */
    int   proc_count;   /* hits+kills depuis debut de salle */
    bool  overloaded;   /* intensity > overload_threshold */
} LoopState;
#define MAX_TRIPLE_LOOPS 8

/* ---------- Heroes ---------- */
typedef enum {
    HERO_GUERRIER = 0,
    HERO_VOLEUR,
    HERO_MAGE,
    HERO_BERSERKER,
    HERO_PALADIN,
    /* archetypes balance + / - */
    HERO_DRUIDE,        /* +elem dmg, -melee dmg */
    HERO_ASSASSIN,      /* +crit, -hp */
    HERO_RANGER,        /* +range dmg, -melee dmg */
    HERO_TEMPLIER,      /* +armor, -atk speed */
    HERO_NECROMANT,     /* +lifesteal, -regen */
    HERO_COUNT
} HeroClass;

/* ---------- Items / Equipment ---------- */
typedef enum {
    SLOT_HELM = 0,
    SLOT_CHEST,
    SLOT_LEGS,
    SLOT_BOOTS,
    SLOT_BELT,
    SLOT_GLOVES,
    SLOT_NONE
} EquipSlot;

/* (Rarity declaree plus haut) */

typedef struct {
    bool      occupied;
    EquipSlot slot;
    Rarity    rarity;
    int       base_kind;     /* sub-kind, used as identity for fusion */
    float     stat_value;    /* effective scaled stat */
} Item;

/* ---------- Entities ---------- */
typedef enum {
    EK_ZOMBIE = 0,
    EK_BANDIT,
    EK_DEMON,
    EK_SLIME,
    EK_BOSS,
    EK_COUNT
} EnemyKind;

typedef struct {
    bool  alive;
    float x, y;
    float vx, vy;
    float r;
    float hp, maxhp;
    int   kind;
    int   variant;
    Element element;          /* affinity for resistances */
    int    combo_mask;        /* bitmask d'elements (1<<EL_*) -> compute_combo() */
    bool   is_elite;
    float fire_dot;
    float fire_dps;
    float slow_t;
    float stun_t;
    float knockback_x, knockback_y;
    float ai_t;
    float ai_t2;
    float facing;
    int   xp_drop;
    int   coin_drop;
    float hit_flash;
    int   split_left;
    bool  is_boss;
    float telegraph_t;
    /* anim de mort : tant que >0, le corps est rendu et fond, sans IA */
    float dying_t;
    float dying_max;
    /* ---- procedural ---- */
    char  name[40];           /* "Vorgar le Brulant" */
    /* stats (peuvent etre modifiees par mods/affixes) */
    float dmg_flat;
    float speed;
    float atk_cd;
} Enemy;

typedef struct {
    bool  alive;
    float x, y;
    float vx, vy;
    float life;
    float r;
    float dmg;
    int   owner;
    int   pierce;
    int   bounces;
    int   chains;
    Element primary;
    Element secondary;
    Element tertiary;
    int   sprite;
    int   target_idx;
    float homing;
    float aoe;
    bool  reflectable;
} Projectile;

typedef struct {
    bool  alive;
    float x, y;
    float vx, vy;
    float life;
    float life_max;
    uint32_t color;
    float size;
    int   kind;
} Particle;

typedef enum {
    PU_XP = 0,
    PU_HEART,
    PU_SOUL,
    PU_ELEMENT,
    PU_WEAPON,
    PU_CHEST,
    PU_COIN,
    PU_PORTAL,
    PU_ITEM,        /* equipement */
    PU_SCROLL,      /* parchemin de lore (Darkest-Dungeon-like) */
    PU_FOOD,        /* nourriture (poulet/legume) - regen PV */
    PU_SHRINE,      /* pacte : bonus + malus permanent pour la run */
} PickupKind;

typedef struct {
    bool  alive;
    float x, y;
    float vy;
    float hover_t;
    PickupKind kind;
    int   value;
    Item  item;          /* used when kind == PU_ITEM */
} Pickup;

typedef struct {
    bool  alive;
    float x, y;
    float vx, vy;
    float life;
    float cd;
    int   target;
    Element element;
} Fairy;

typedef struct {
    bool  alive;
    float x, y, vy;
    float life;
    float life_max;
    char  text[16];
    uint32_t color;
    bool  big;
} DamageNumber;

/* ---------- Player ---------- */
typedef struct {
    HeroClass hero;
    float x, y;
    float vx, vy;
    float r;
    float hp, maxhp;
    float speed;
    float armor;
    float dmg_mul;            /* multiplicateur global */
    float lifesteal;
    float regen_per_sec;
    /* ---- stats avancees (style Brotato) ---- */
    float flat_dmg;           /* +flat ajoute aux dmg arme */
    float melee_dmg_mul;      /* multiplicateur arme melee */
    float range_dmg_mul;      /* multiplicateur arme distance */
    float elem_dmg_mul;       /* multiplicateur si arme a elements */
    float atk_speed_mul;      /* < 1.0 = plus rapide, divise le cd */
    float crit_chance;        /* 0..1 */
    float crit_dmg;           /* multiplicateur sur coup critique (default 1.5) */
    float range_mul;          /* portee armes */
    float dodge;              /* 0..1 chance d'esquiver */
    /* affinites elementaires : multiplicateur de dmg par element (+/-) */
    float elem_affinity[EL_COUNT];

    int   level;
    int   xp;
    int   xp_to_next;
    Weapon weapons[WEAPON_SLOTS];
    int    active_weapon;
    float invuln_t;
    int   souls;
    int   coins;
    int   facing_dir;
    float aim_x, aim_y;
    float dash_cd;
    float dash_t;
    float regen_acc;
    float anim_t;
    float anim_dir_x, anim_dir_y;
    int   anim_kind;

    /* inventaire */
    Item  inventory[INVENTORY_SLOTS];
    Item  equipped[EQUIP_SLOTS];

    /* shop items achetes durant la course (effets cumulatifs) */
    int   shop_purchased[64];   /* tableau d'index d'item achete */
    int   shop_purchased_count;

    /* ---- Triple feedback loop ----
     * Etat persistant pour chaque triple combo definissant un loop. */
    LoopState loop_states[MAX_TRIPLE_LOOPS];
    int       active_loop_idx;    /* -1 = aucun triple actif */
    int       active_loop_mask;   /* mask du triple actif */
} Player;

/* ---------- Map / Dungeon ---------- */
typedef enum {
    T_VOID = 0,
    T_FLOOR,
    T_WALL,
    T_DOOR,
    T_EXIT,
    T_BLOOD,
    T_HAZARD_LAVA,
    T_HAZARD_WATER,
    T_TORCH,
    T_BONES,
    T_RUNE,
} TileKind;

typedef struct {
    int x, y, w, h;
    bool cleared;
    int  enemies_to_spawn;
    int  spawn_timer_ms;
    bool is_boss_room;
    bool boss_spawned;
    bool is_debug_room;       /* salle bac-a-sable spawnee via options.debug_room */
} Room;

typedef struct {
    TileKind tiles[MAP_H][MAP_W];
    Room rooms[32];
    int  room_count;
    int  spawn_x, spawn_y;
    int  exit_x, exit_y;
    int  level_index;
    int  gen_id;          /* incremente a chaque generation : pour la cache mesh */
    bool boss_dead;
    int  boss_room_idx;
} Dungeon;

/* ---------- Meta ---------- */
typedef struct {
    int  shards;
    /* progression de decouverte (revelee via gameplay) */
    bool weapon_discovered[W_COUNT];
    bool element_discovered[EL_COUNT];
    bool hero_discovered[HERO_COUNT];
    /* deblocage paye avec eclats (pour heros uniquement) */
    bool hero_unlocked[HERO_COUNT];
    int  best_floor;
    int  total_runs;
    int  perm_dmg_pct;
    int  perm_hp;
    int  perm_armor;
    int  perm_speed;
    int  victories;
    int  combo_seen[64];
    int  combo_seen_count;
    /* meilleure rarete vue par (slot, sub_kind) -- -1 = jamais decouvert.
     * sub_kind est borne a 5 dans item_drop_for_floor (rand()%5). */
    int  item_seen_rarity[EQUIP_SLOTS][5];
} MetaSave;

/* ---------- SHOP (Brotato-like) ---------- */
typedef struct {
    int   recipe_id;        /* index dans la table de recettes shop */
    int   cost;
    bool  bought;
} ShopItem;
#define SHOP_SLOTS 4

/* ---------- Settings (rebind / son / DLSS) ---------- */
typedef enum {
    BIND_DASH = 0,
    BIND_INVENTORY,
    BIND_WEAPON_SWAP,
    BIND_WEAPON_1,
    BIND_WEAPON_2,
    BIND_INTERACT,
    BIND_COUNT
} BindAction;

typedef struct {
    int          version;
    SDL_Scancode keys[BIND_COUNT];
    int          sfx_volume;        /* 0..4 */
    int          sfx_mute;
    int          dlss_on;           /* 0 = nearest, 1 = linear upscale */
    int          debug_room;        /* 1 = salle bac-a-sable a cote de l'entree
                                       avec un exemplaire de chaque arme,
                                       element et equipement legendaire. */
} Settings;

/* ---------- Game state ---------- */
typedef enum {
    GS_TITLE = 0,
    GS_LORE,
    GS_HUB,
    GS_HELP,
    GS_OPTIONS,
    GS_CHOOSE_HERO,
    GS_RUN,
    GS_LEVELUP,
    GS_SHOP,
    GS_INVENTORY,
    GS_CODEX,
    GS_DEAD,
    GS_VICTORY,
    GS_QUIT,
} GameStateKind;

typedef struct {
    GameStateKind state;
    GameStateKind state_prev;     /* used by inventory pause */
    Player        player;
    Dungeon       dungeon;
    Enemy         enemies[MAX_ENEMIES];
    Projectile    projectiles[MAX_PROJECTILES];
    Particle      particles[MAX_PARTICLES];
    Pickup        pickups[MAX_PICKUPS];
    Fairy         fairies[MAX_FAIRIES];
    DamageNumber  dmgnums[MAX_DMGNUM];
    MetaSave      meta;

    SDL_Window   *window;
    GfxCtx       *renderer;     /* contexte OpenGL (gardons le nom pour compat) */
    const Uint8  *keys;
    int           mouse_x, mouse_y;
    int           mouse_btn;
    int           mouse_btn_prev;
    int           mouse_wheel;     /* delta vertical de molette ce frame */
    Uint8         keys_prev[SDL_NUM_SCANCODES];

    float         time;
    float         dt;
    float         hitstop_t;
    float         camera_x, camera_y;
    float         flash_t;

    int           levelup_choices[3];
    int           levelup_choice_kind[3];
    int           levelup_choice_rarity[3];   /* R_COMMON..R_LEGENDARY par choix */
    int           hero_cursor;
    int           hub_cursor;
    int           title_cursor;

    int           run_kills;
    float         run_time;

    float         shake_t;
    float         shake_mag;

    int           floor_index;
    bool          portal_spawned;

    /* parchemin de lore actuel (overlay UI temporaire) */
    char          scroll_text[224];
    float         scroll_t;

    /* Signature Combo : nom du combo triple actif, affiche en gros au-
     * dessus du joueur. Recalcul lorsqu'on change de loadout. */
    char          combo_callout[40];
    float         combo_callout_t;
    int           combo_callout_mask;     /* dernier mask annonce */
    uint32_t      combo_callout_color;

    /* flag de crit pour l'attaque en cours : positionne par update_weapons,
     * lu par enemy_take_damage pour colorer/scale le dmgnum. */
    bool          current_attack_crit;

    /* shop */
    ShopItem      shop_items[SHOP_SLOTS];
    int           shop_cursor;
    int           shop_reroll_cost;
    int           shop_visits;

    /* inventory cursor: 0..11 inv, 12..17 equip */
    int           inv_cursor;
    int           inv_marked[3];   /* items marked for fusion (inventory indices) */
    int           inv_marked_count;
    char          inv_msg[64];
    float         inv_msg_t;

    /* audio */
    SDL_AudioDeviceID audio_dev;
    int           audio_sample_rate;

    float         boss_intro_t;
    char          boss_name[32];

    /* options / settings */
    Settings      settings;
    int           opt_cursor;            /* row in options menu */
    int           opt_section;           /* 0 = controls, 1 = audio, 2 = video */
    bool          opt_waiting_rebind;
    SDL_Scancode  opt_last_keydown;      /* captured by poll_input */
    GameStateKind opt_return;            /* state to return to (title/hub) */
    char          opt_msg[64];
    float         opt_msg_t;

    /* nombre d'ennemis vivants (non en train de mourir) maintenu par
     * update_enemies. Utilise par loop_decay : la jauge ne decroit que
     * quand la salle est vide. */
    int           enemy_alive_count;

    /* codex */
    int           codex_tab;       /* 0=combos 1=talismans 2=equip 3=armes */
    int           codex_cursor;    /* row in current tab */
    int           codex_scroll;    /* premier item visible (defilement) */
} Game;

/* ---------- API ---------- */
Game *game_get(void);
void  game_init(Game *g);
void  game_shutdown(Game *g);
void  game_run(Game *g);

void  game_start_new_run(Game *g);
void  game_to_hub(Game *g);
void  game_open_shop(Game *g);
void  game_next_floor(Game *g);
void  game_recompute_player_stats(Game *g);

/* triple feedback loop : voir TripleLoopDef dans combat.c */
void     loop_on_hit(Game *g);
void     loop_on_kill(Game *g);
void     loop_decay(Game *g, float dt);
uint32_t triple_loop_aura_color(int loop_idx);

/* combos cote ennemis : applique les proprietes d'un combo (chain/AOE/
 * homing/pierce/element) sur un projectile que l'ennemi vient de spawner.
 * Utilise par world.c pour faire heriter aux projectiles ennemis le
 * comportement de leur combo signature. */
void        combo_apply_to_enemy_projectile(int mask, Projectile *pr);
const char *combo_name (int mask);
uint32_t    combo_color(int mask);
/* iterateur sur la table COMBO_NAMES (sans la sentinelle).
 * Renvoie le nombre d entrees. combo_table_get remplit out_* pour i in [0..N). */
int         combo_table_count(void);
bool        combo_table_get(int i, int *out_mask, const char **out_name, uint32_t *out_color);
/* nombre d'elements distincts dans le mask (1, 2, 3, ...) */
int         combo_mask_element_count(int mask);
/* render */
void        render_codex(Game *g);
/* navigation codex (clavier + souris) */
void        update_codex(Game *g);
/* helper meta : marque un combo decouvert (no-op si deja seen). */
void        meta_combo_mark(MetaSave *m, int mask);
bool        meta_combo_is_seen(const MetaSave *m, int mask);
/* helper meta : enregistre la decouverte d un item (slot, sub_kind) avec la
 * rarete a laquelle on l a vu (garde la max). sub_kind est borne a 0..4. */
void        meta_item_mark(MetaSave *m, EquipSlot slot, int sub_kind, Rarity r);

/* world */
void  dungeon_generate(Dungeon *d, int floor_index, unsigned seed);
bool  tile_solid(TileKind t);
/* Salle debug (options.debug_room) : carve une chambre supplementaire reliee
 * a la salle de spawn et la remplit d'un exemplaire de chaque arme,
 * element et equipement legendaire. Idempotent : ne fait rien si la salle
 * existe deja sur le dungeon (room avec is_debug_room). */
void  dungeon_add_debug_room(Game *g);

int   enemy_spawn(Game *g, int kind, float x, float y);
int   projectile_spawn(Game *g, Projectile p);
int   particle_spawn(Game *g, float x, float y, float vx, float vy, float life, uint32_t color, float size);
int   particle_spawn_kind(Game *g, float x, float y, float vx, float vy, float life, uint32_t color, float size, int kind);
int   pickup_spawn(Game *g, PickupKind k, int v, float x, float y);
int   pickup_spawn_item(Game *g, Item it, float x, float y);
int   fairy_spawn(Game *g, float x, float y, Element el);
int   dmgnum_spawn(Game *g, float x, float y, int amount, uint32_t color, bool big);

void  update_player(Game *g);
void  update_weapons(Game *g);
void  update_enemies(Game *g);
void  update_projectiles(Game *g);
void  update_particles(Game *g);
void  update_pickups(Game *g);
void  update_fairies(Game *g);
void  update_dmgnums(Game *g);
void  update_room_logic(Game *g);

void  render_world(Game *g);
void  render_world_overlay_ui(Game *g);   /* HP bars/noms/dmgnums en UI 2D */
void  render_hud(Game *g);
void  render_hub(Game *g);
void  render_levelup(Game *g);
void  render_dead(Game *g);
void  render_title(Game *g);
void  render_help(Game *g);
void  render_choose_hero(Game *g);
void  render_shop(Game *g);
void  render_victory(Game *g);
void  render_inventory(Game *g);

const char *element_name(Element e);
uint32_t    element_color(Element e);
const char *weapon_name(WeaponKind w);
const char *hero_name(HeroClass h);
const char *hero_desc(HeroClass h);
const char *enemy_name(EnemyKind k);
const char *subclass_name(WeaponKind a, WeaponKind b);
const char *slot_name(EquipSlot s);
const char *rarity_name(Rarity r);
uint32_t    rarity_color(Rarity r);
float       rarity_mul(Rarity r);
float       elem_effectiveness(Element atk, Element def);

void        weapon_init_defaults(Weapon *w, WeaponKind kind);
void        weapon_attach_element(Weapon *w, Element e);
void        weapon_describe(const Weapon *w, char *buf, int bufsz);
int         weapon_combo_id(const Weapon *w);
int         weapon_slot_count(Rarity r);   /* talisman slots autorises */

/* tables level-up (definies dans main.c, exposees pour render.c) */
const char *level_stat_name (int axis);
float       level_stat_value_for(int axis, Rarity r);

void        save_load(MetaSave *m);
void        save_write(const MetaSave *m);

void        text_draw(GfxCtx *r, int x, int y, const char *s, uint32_t col);
void        text_drawf(GfxCtx *r, int x, int y, uint32_t col, const char *fmt, ...);
int         text_width(const char *s);

void        world_enemy_damage(Game *g, int idx, float dmg, Element el, float kx, float ky);
void        player_take_damage(Game *g, float dmg);

/* audio */
typedef enum {
    SFX_PUNCH = 0, SFX_HIT, SFX_HEAVY_HIT, SFX_SWING, SFX_EXPLODE,
    SFX_PICKUP, SFX_COIN, SFX_LEVELUP, SFX_PLAYER_HURT, SFX_DEATH,
    SFX_BOSS, SFX_PORTAL, SFX_SHOOT, SFX_ZAP, SFX_FUSE,
    SFX_COUNT
} SfxId;
void  audio_init(Game *g);
void  audio_shutdown(Game *g);
void  sfx_play(Game *g, SfxId id);

/* settings */
void  settings_defaults(Settings *s);
void  settings_load(Settings *s);
void  settings_write(const Settings *s);
const char *bind_action_name(BindAction a);
const char *scancode_label(SDL_Scancode sc);
void  render_options(Game *g);
void  update_options(Game *g);
void  apply_render_filter(Game *g);    /* recree g->target avec le filtre courant */

void  render_lore(Game *g);

/* helpers souris */
bool  mouse_in_rect(Game *g, int x, int y, int w, int h);
bool  mouse_clicked(Game *g);

/* StatBlock : centralise toutes les stats joueur en une seule struct,
 * pour eviter les fonctions a 15+ pointeurs. game_recompute_player_stats
 * accumule dans un StatBlock puis copie vers Player avec clamps. */
typedef struct {
    float maxhp, speed, armor;
    float dmg_mul, lifesteal, regen;
    float flat_dmg, melee, range, elem;
    float atk_speed;
    float crit_chance, crit_dmg;
    float range_mul, dodge;
    float aff[EL_COUNT];
} StatBlock;

/* shop (Brotato-like) */
void  shop_generate(Game *g);
void  shop_buy(Game *g, int idx);
void  shop_reroll(Game *g);
const char *shop_recipe_name(int recipe_id);
const char *shop_recipe_desc(int recipe_id);
int   shop_recipe_count(void);
int   shop_recipe_cost(int recipe_id);
uint32_t shop_recipe_color(int recipe_id);   /* couleur d'aperçu */
void  shop_apply_recipe(Game *g, int recipe_id);
/* applique l effet d une recette sur un StatBlock (additif). */
void  shop_recipe_apply_to_block(int recipe_id, StatBlock *sb);

/* procedural names */
void  enemy_generate_name(Enemy *e, int floor_index);

/* mod support */
void  mods_load(Game *g);

/* drop tables */
Rarity rarity_for_floor_elite(int floor_index);
Rarity rarity_for_floor_boss(int floor_index);

/* inventory */
Item  item_make(EquipSlot slot, Rarity rarity, int sub_kind);
Item  item_drop_for_floor(Game *g, int floor_index, bool elite, bool boss);
void  inventory_pickup(Game *g, Item it);            /* tries to add to first empty inv slot */
bool  inventory_equip(Game *g, int inv_index);       /* swap inv slot with matching equip slot */
bool  inventory_unequip(Game *g, int equip_index);   /* move equip back to first free inv slot */
bool  inventory_fuse(Game *g);                       /* fuse 3 marked items */
bool  inventory_find_fusion_group(Game *g, int *a, int *b, int *c);
const char *item_kind_name(EquipSlot s);
const char *item_label(const Item *it, char *buf, int bufsz);
void        update_inventory_input(Game *g);
/* renvoie le rect ecran d'un slot de l'inventaire (sac, equipement, arme,
 * talisman). cursor_idx suit le mapping INV_CURSOR_* (voir render.c).
 * Retourne false si l'index est hors-bornes. */
bool        inv_layout_rect(int cursor_idx, int *x, int *y, int *w, int *h);

#endif
