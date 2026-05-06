/*
 * game.h - types et constantes du doom-hybride "Crucible"
 */
#ifndef GAME_H
#define GAME_H

#include <SDL.h>
#include <stdbool.h>
#include <stdint.h>

#define INTERNAL_W 480
#define INTERNAL_H 270
#define WINDOW_SCALE 3
#define WINDOW_W (INTERNAL_W * WINDOW_SCALE)
#define WINDOW_H (INTERNAL_H * WINDOW_SCALE)

#define TILE 16
#define MAP_W 56
#define MAP_H 56

#define MAX_ENEMIES 256
#define MAX_PROJECTILES 1024
#define MAX_PARTICLES 1024
#define MAX_PICKUPS 96
#define MAX_FAIRIES 32
#define MAX_DMGNUM 64

#define WEAPON_SLOTS 2
#define MAX_ELEMENTS_PER_WEAPON 3

#define INVENTORY_SLOTS 12
#define EQUIP_SLOTS 6     /* helm, chest, legs, boots, belt, gloves */

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
    EL_COUNT
} Element;

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
    Element    elements[MAX_ELEMENTS_PER_WEAPON];
    int        element_count;
} Weapon;

/* ---------- Heroes ---------- */
typedef enum {
    HERO_GUERRIER = 0,
    HERO_VOLEUR,
    HERO_MAGE,
    HERO_BERSERKER,
    HERO_PALADIN,
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

typedef enum {
    R_COMMON = 0,
    R_MAGIC,
    R_RARE,
    R_EPIC,
    R_LEGENDARY,
    R_COUNT
} Rarity;

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
    float dmg_mul;
    float lifesteal;
    float regen_per_sec;
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
} Room;

typedef struct {
    TileKind tiles[MAP_H][MAP_W];
    Room rooms[32];
    int  room_count;
    int  spawn_x, spawn_y;
    int  exit_x, exit_y;
    int  level_index;
    bool boss_dead;
    int  boss_room_idx;
} Dungeon;

/* ---------- Meta ---------- */
typedef struct {
    int  shards;
    bool hero_unlocked[HERO_COUNT];
    bool weapon_unlocked[W_COUNT];
    bool element_unlocked[EL_COUNT];
    int  best_floor;
    int  total_runs;
    int  perm_dmg_pct;
    int  perm_hp;
    int  perm_armor;
    int  perm_speed;
    int  victories;
    int  combo_seen[64];
    int  combo_seen_count;
} MetaSave;

/* ---------- SHOP ---------- */
typedef struct {
    int  kind;
    int  value;
    int  cost;
    bool bought;
    Item item;
} ShopItem;

/* ---------- Game state ---------- */
typedef enum {
    GS_TITLE = 0,
    GS_HUB,
    GS_HELP,
    GS_CHOOSE_HERO,
    GS_RUN,
    GS_LEVELUP,
    GS_SHOP,
    GS_INVENTORY,
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
    SDL_Renderer *renderer;
    SDL_Texture  *target;
    const Uint8  *keys;
    int           mouse_x, mouse_y;
    int           mouse_btn;
    int           mouse_btn_prev;
    Uint8         keys_prev[SDL_NUM_SCANCODES];

    float         time;
    float         dt;
    float         hitstop_t;
    float         camera_x, camera_y;
    float         flash_t;

    int           levelup_choices[3];
    int           levelup_choice_kind[3];
    int           hero_cursor;
    int           hub_cursor;
    int           title_cursor;

    int           run_kills;
    float         run_time;

    float         shake_t;
    float         shake_mag;

    int           floor_index;
    bool          portal_spawned;

    /* shop */
    ShopItem      shop_items[5];
    int           shop_cursor;

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

/* world */
void  dungeon_generate(Dungeon *d, int floor_index, unsigned seed);
bool  tile_solid(TileKind t);

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

void        save_load(MetaSave *m);
void        save_write(const MetaSave *m);

void        text_draw(SDL_Renderer *r, int x, int y, const char *s, uint32_t col);
void        text_drawf(SDL_Renderer *r, int x, int y, uint32_t col, const char *fmt, ...);
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

/* shop */
void  shop_generate(Game *g);
void  shop_buy(Game *g, int idx);

/* inventory */
Item  item_make(EquipSlot slot, Rarity rarity, int sub_kind);
Item  item_drop_for_floor(Game *g, int floor_index, bool elite, bool boss);
void  inventory_pickup(Game *g, Item it);            /* tries to add to first empty inv slot */
bool  inventory_equip(Game *g, int inv_index);       /* swap inv slot with matching equip slot */
bool  inventory_unequip(Game *g, int equip_index);   /* move equip back to first free inv slot */
bool  inventory_fuse(Game *g);                       /* fuse 3 marked items */
const char *item_kind_name(EquipSlot s);
const char *item_label(const Item *it, char *buf, int bufsz);
void        update_inventory_input(Game *g);

#endif
