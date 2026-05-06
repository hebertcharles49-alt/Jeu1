/*
 * game.h - types et constantes du doom-hybride
 */
#ifndef GAME_H
#define GAME_H

#include <SDL2/SDL.h>
#include <stdbool.h>
#include <stdint.h>

#define INTERNAL_W 480
#define INTERNAL_H 270
#define WINDOW_SCALE 3
#define WINDOW_W (INTERNAL_W * WINDOW_SCALE)
#define WINDOW_H (INTERNAL_H * WINDOW_SCALE)

#define TILE 16
#define MAP_W 48
#define MAP_H 48

#define MAX_ENEMIES 256
#define MAX_PROJECTILES 1024
#define MAX_PARTICLES 512
#define MAX_PICKUPS 64
#define MAX_FAIRIES 32

#define WEAPON_SLOTS 5
#define MAX_ELEMENTS_PER_WEAPON 3

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
    W_SWORD = 0,    /* mêlée single target — fencer */
    W_SHIELD,       /* défense / reflect */
    W_BOW,          /* distance single target */
    W_WAND,         /* magie AOE */
    W_AXE,          /* mêlée AOE */
    W_COUNT
} WeaponKind;

typedef struct {
    WeaponKind kind;
    bool       owned;
    float      cooldown;     /* current cd timer */
    float      base_cd;      /* base cooldown sec */
    float      base_dmg;
    float      base_range;
    Element    elements[MAX_ELEMENTS_PER_WEAPON];
    int        element_count;
} Weapon;

/* ---------- Entities ---------- */
typedef struct {
    bool  alive;
    float x, y;
    float vx, vy;
    float r;            /* collision radius */
    float hp, maxhp;
    int   kind;         /* enemy archetype */
    float fire_dot;     /* burning timer */
    float fire_dps;
    float slow_t;       /* water/mud */
    float stun_t;       /* lightning shock */
    float knockback_x, knockback_y;
    float ai_t;         /* shoot timer for ranged */
    float facing;       /* radians */
    int   xp_drop;
} Enemy;

typedef struct {
    bool  alive;
    float x, y;
    float vx, vy;
    float life;
    float r;
    float dmg;
    int   owner;        /* 0 = player, 1 = enemy */
    int   pierce;
    int   bounces;
    int   chains;
    Element primary;
    Element secondary;
    Element tertiary;
    int   sprite;       /* visual variant */
    int   target_idx;   /* for homing */
    float homing;       /* homing strength */
    float aoe;          /* aoe radius on hit */
    bool  reflectable;
} Projectile;

typedef struct {
    bool  alive;
    float x, y;
    float vx, vy;
    float life;
    uint32_t color;
    float size;
} Particle;

typedef enum {
    PU_XP = 0,
    PU_HEART,
    PU_SOUL,
    PU_ELEMENT,
    PU_WEAPON,
    PU_CHEST,
} PickupKind;

typedef struct {
    bool  alive;
    float x, y;
    float vy;             /* hover */
    float hover_t;
    PickupKind kind;
    int   value;          /* element id or weapon id */
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

/* ---------- Player ---------- */
typedef struct {
    float x, y;
    float vx, vy;
    float r;
    float hp, maxhp;
    float speed;
    int   level;
    int   xp;
    int   xp_to_next;
    Weapon weapons[WEAPON_SLOTS];
    int    active_weapon;   /* used for "assign element here" */
    float invuln_t;
    int   souls;            /* in-run currency that converts on death */
    int   facing_dir;       /* 0 right,1 down,2 left,3 up */
    float aim_x, aim_y;
    float dash_cd;
    float dash_t;            /* iframes during dash */
} Player;

/* ---------- Map / Dungeon ---------- */
typedef enum {
    T_VOID = 0,
    T_FLOOR,
    T_WALL,
    T_DOOR,
    T_EXIT,
    T_BLOOD,    /* decoration */
    T_HAZARD_LAVA,
    T_HAZARD_WATER,
} TileKind;

typedef struct {
    int x, y, w, h;
    bool cleared;
    int  enemies_to_spawn;
    int  spawn_timer_ms;
} Room;

typedef struct {
    TileKind tiles[MAP_H][MAP_W];
    Room rooms[32];
    int  room_count;
    int  spawn_x, spawn_y;
    int  exit_x, exit_y;
    int  level_index;
} Dungeon;

/* ---------- Meta progression ---------- */
typedef struct {
    int  shards;                       /* permanent currency */
    bool weapon_unlocked[W_COUNT];
    bool element_unlocked[EL_COUNT];
    int  best_level;
    int  total_runs;
} MetaSave;

/* ---------- Game state ---------- */
typedef enum {
    GS_HUB = 0,
    GS_RUN,
    GS_LEVELUP,
    GS_DEAD,
    GS_QUIT,
    GS_TITLE,
    GS_HELP,
} GameStateKind;

typedef struct {
    GameStateKind state;
    Player        player;
    Dungeon       dungeon;
    Enemy         enemies[MAX_ENEMIES];
    Projectile    projectiles[MAX_PROJECTILES];
    Particle      particles[MAX_PARTICLES];
    Pickup        pickups[MAX_PICKUPS];
    Fairy         fairies[MAX_FAIRIES];
    MetaSave      meta;
    SDL_Window   *window;
    SDL_Renderer *renderer;
    SDL_Texture  *target;        /* internal-resolution target */
    const Uint8  *keys;
    int           mouse_x, mouse_y;
    int           mouse_btn;
    int           mouse_btn_prev;
    Uint8         keys_prev[SDL_NUM_SCANCODES];
    float         time;
    float         dt;
    float         camera_x, camera_y;

    /* level-up choice */
    int           levelup_choices[3];
    int           levelup_choice_kind[3]; /* 0=weapon,1=element,2=stat */
    bool          frozen;

    /* HUB */
    int           hub_cursor;

    /* run stats */
    int           run_kills;
    float         run_time;

    /* shake */
    float         shake_t;
    float         shake_mag;

    /* title screen */
    int           title_cursor;

    /* misc */
    int           floor_index;     /* current dungeon depth */
} Game;

/* ---------- API ---------- */
Game *game_get(void);
void  game_init(Game *g);
void  game_shutdown(Game *g);
void  game_run(Game *g);

void  game_start_new_run(Game *g);
void  game_to_hub(Game *g);

/* world */
void  dungeon_generate(Dungeon *d, int floor_index, unsigned seed);
bool  tile_solid(TileKind t);

/* spawn/find */
int   enemy_spawn(Game *g, int kind, float x, float y);
int   projectile_spawn(Game *g, Projectile p);
int   particle_spawn(Game *g, float x, float y, float vx, float vy, float life, uint32_t color, float size);
int   pickup_spawn(Game *g, PickupKind k, int v, float x, float y);
int   fairy_spawn(Game *g, float x, float y, Element el);

/* update */
void  update_player(Game *g);
void  update_weapons(Game *g);
void  update_enemies(Game *g);
void  update_projectiles(Game *g);
void  update_particles(Game *g);
void  update_pickups(Game *g);
void  update_fairies(Game *g);
void  update_room_logic(Game *g);

/* render */
void  render_world(Game *g);
void  render_hud(Game *g);
void  render_hub(Game *g);
void  render_levelup(Game *g);
void  render_dead(Game *g);
void  render_title(Game *g);
void  render_help(Game *g);

/* world helpers used by combat */
void world_enemy_damage(Game *g, int idx, float dmg, Element el, float kx, float ky);

/* misc */
const char *element_name(Element e);
uint32_t    element_color(Element e);
const char *weapon_name(WeaponKind w);
void        weapon_init_defaults(Weapon *w, WeaponKind kind);
void        weapon_attach_element(Weapon *w, Element e);
void        weapon_describe(const Weapon *w, char *buf, int bufsz);
int         weapon_combo_id(const Weapon *w); /* canonical signature for combo lookup */

void        save_load(MetaSave *m);
void        save_write(const MetaSave *m);

void        text_draw(SDL_Renderer *r, int x, int y, const char *s, uint32_t col);
void        text_drawf(SDL_Renderer *r, int x, int y, uint32_t col, const char *fmt, ...);
int         text_width(const char *s);

#endif
