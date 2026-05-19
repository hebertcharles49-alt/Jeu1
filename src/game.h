/*
 * game.h - types et constantes principaux du jeu
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
#define MAX_SURFACES 32

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

/* Caps par run (anti-deadlock + force des decisions). */
#define RUN_TALISMAN_MAX  7
#define RUN_UNIQUE_MAX    3

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

/* Affixes : un item peut porter de 0 a 4 affixes additionnels,
 * selon sa rarete (commun=0, magique=1, rare=2, epique=3, legendaire=4).
 * Chaque affixe roule sa valeur dans une plage min/max au moment du drop. */
typedef enum {
    AFFIX_NONE = 0,
    AFFIX_HP,           /* +X PV max */
    AFFIX_ARMOR,        /* +X armure */
    AFFIX_SPEED,        /* +X vitesse */
    AFFIX_DMG_PCT,      /* +X% degats (fraction : 0.05 = +5%) */
    AFFIX_CRIT_CHANCE,  /* +X% chance crit (fraction) */
    AFFIX_LIFESTEAL,    /* +X% vol de vie (fraction) */
    AFFIX_REGEN,        /* +X regen / s */
    AFFIX_ATK_SPEED,    /* +X% vitesse d'attaque (fraction, additive) */
    AFFIX_DODGE,        /* +X% esquive (fraction) */
    AFFIX_RANGE_MUL,    /* +X% portee (fraction) -- archetypes Frenetique */
    AFFIX_FLAT_DMG,     /* +X dmg flat (entier-ish) -- archetypes Frenetique */
    AFFIX_COUNT
} Affix;

typedef struct { Affix kind; float value; } ItemAffix;
#define MAX_AFFIXES 4

typedef struct {
    bool      occupied;
    EquipSlot slot;
    Rarity    rarity;
    int       base_kind;     /* sub-kind, used as identity for fusion */
    float     stat_value;    /* effective scaled stat */
    ItemAffix affixes[MAX_AFFIXES];
    int       affix_count;
    /* Nom procedural. Genere via item_generate_name() au
     * drop. Pour les uniques, c'est le nom fixe de l'entree UNIQUE_DEFS. */
    char      name[40];
    /* Items uniques : si is_unique = true, unique_id pointe vers
     * UNIQUE_DEFS[unique_id]. Cf uniques.c. */
    bool      is_unique;
    int       unique_id;
} Item;

/* ---------- Entities ---------- */
typedef enum {
    EK_ZOMBIE = 0,    /* lent, contact, shambler */
    EK_BANDIT,        /* kite + voids arrows */
    EK_DEMON,         /* radial fire spreader */
    EK_SLIME,         /* hopper, split a la mort */
    EK_RAT,           /* swarm, tres rapide, zigzag, faible */
    EK_GHOST,         /* float + phase murs + teleport sur hit */
    EK_CHARGER,       /* telegraph 0.8s puis charge en ligne droite */
    EK_MAGE,          /* kite + homing fae + blink si trop proche */
    /* "support" enemies : modifient les alliees voisines */
    EK_HEALER,        /* heal pulse 8 PV / 1.5s sur les ennemis < 80 px */
    EK_BUFFER,        /* totem statique : aura damage +30% pour voisins */
    EK_NECROMANCER,   /* raise EK_ZOMBIE toutes les 5s, capped a 2 */
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
    /* u_expose_weakness : timer 5s pose par un crit du joueur. Pendant
     * ce temps, une attaque elementaire SUPER-effective vs e->element
     * inflige x1.5 degats. */
    float expose_t;
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
    PU_SCROLL,      /* parchemin de lore */
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

/* Surfaces : flaques au sol dynamiques. Spawnees par les attaques /
 * morts d ennemis, reagissent entre elles (eau+feu = vapeur, eau+foudre
 * = electrocution, huile+feu = explosion), et affectent les entites
 * qui marchent dessus (slow, dmg, ignition). Cf surfaces.c. */
typedef enum {
    SURF_NONE = 0,
    /* surfaces "base" */
    SURF_WATER,        /* ralentit ; conducteur */
    SURF_OIL,          /* ralentit ; inflammable */
    SURF_FIRE,         /* dmg over time */
    SURF_ICE,          /* ralentit ; slip */
    SURF_ELECTRIFIED,  /* eau electrifiee : dmg + stun */
    /* surfaces "derivees" : naissent des interactions et des morts.
     * Suivent une physique simple :
     *   eau + chaud   -> vapeur, qui condense en eau en refroidissant
     *   eau + terre   -> boue (slow fort, pas conductrice)
     *   eau + vie     -> sang (regen sur passage, lifesteal local)
     *   huile + acide -> goudron (slow extreme, inflammable) -- skip cette
     *                    passe pour rester scoped */
    SURF_STEAM,        /* nuage transitoire, bref, condense -> water */
    SURF_MUD,          /* boue : slow fort, pas conductrice */
    SURF_BLOOD,        /* "vie + eau" : regen lent au contact */
    /* nouvelles surfaces tier 2 (elements rares) */
    SURF_HOLY,         /* eau benie : regen rapide + cleanse status */
    SURF_SHADOW,       /* ombre : DOT dark + slow leger */
    SURF_TAR,          /* goudron : huile corrompue, slow extreme */
    SURF_COUNT
} SurfaceKind;

typedef struct {
    bool        alive;
    SurfaceKind kind;
    float       x, y;
    float       r;
    float       life;       /* secondes restantes */
    float       life_max;
} Surface;

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
    /* ---- stats avancees ---- */
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
    /* hit_t : compte a rebours du dernier impact (decroit dans
     * update_player). Sert au rendu : vignette pulse, sparks, low-pass
     * "ringing" visuel. Plus long que flash_t pour un afterglow. */
    float hit_t;
    /* direction du dernier impact (unit vector) pour le knockback visuel et
     * la dust kick. */
    float hit_dir_x, hit_dir_y;
    int   souls;
    int   coins;
    int   facing_dir;
    float aim_x, aim_y;
    float dash_cd;
    float dash_t;
    float regen_acc;
    float anim_t;
    float anim_dur;              /* duree de l anim courante. Permet aux
                                    armes (axe lent, sword rapide) d avoir
                                    des timings differents tout en restant
                                    normalises pour le render. */
    float anim_dir_x, anim_dir_y;
    int   anim_kind;
    /* combo alternance : sword/fists alternent gauche-droite a chaque
     * swing. anim_flip = +1 ou -1, multiplie l'amplitude du side dans
     * l'arc pour donner un effet de "1-2" en chaine de coups. */
    int   anim_flip;
    /* trail buffer : positions historiques de la pointe d'arme pour le
     * trail visuel (slash de l epee / hache, swirl de la baguette).
     * Anneau circulaire de N positions, ecrit dans render_world.c. */
    float trail_x[8], trail_y[8], trail_z[8];
    int   trail_head;             /* indice prochaine ecriture */
    int   trail_count;            /* nb d'entrees valides (sature a 8) */

    /* inventaire */
    Item  inventory[INVENTORY_SLOTS];
    Item  equipped[EQUIP_SLOTS];

    /* shop items achetes durant la course (effets cumulatifs) */
    int   shop_purchased[64];   /* tableau d'index d'item achete */
    int   shop_purchased_count;

    /* ---- Flags de gameplay venant des uniques build-defining ----
     * Mis a jour par game_recompute_player_stats a partir des uniques
     * equipes. Cf uniques.c pour qui les active. */
    bool  u_explosions_attract;  /* les AOE tirent les ennemis vers le centre */
    bool  u_crit_shrink;         /* chaque crit reduit player.r */
    bool  u_corpse_mines;        /* 30% des morts laissent une mine */
    bool  u_free_dash;           /* dash sans cooldown */
    int   u_drone_count;         /* 0..3 fees-drones qui orbitent */
    /* nouveaux flags rework v2 */
    bool  u_berserk_cd;          /* cooldowns *0.5 sous 50% HP */
    bool  u_element_absorb;      /* hit elementaire -> +30% affinite 8s */
    bool  u_hazard_immune;       /* immunite hazards sol */
    bool  u_hazard_stacks;       /* +5% dmg par tile hazard (max +50%) */
    bool  u_phoenix_revive;      /* revie a 30% HP, 1 charge / salle */
    bool  u_kill_wave;           /* vague de repulsion + dmg sur chaque kill */
    bool  u_frontal_immune;      /* immunite proj de face (-40% vitesse) */
    bool  u_dodge_attack;        /* esquive -> attaque arme gratuite */
    bool  u_crowd_regen;         /* regen += alive_count * 0.1 */
    bool  u_stun_on_melee;       /* 20% stun 1s sur melee, x2 dmg pendant */
    bool  u_void_trail;          /* dash laisse champ vide 3s */
    bool  u_kill_stack_dmg;      /* +2 flat_dmg / kill, max +40 */
    bool  u_heavy_armor;         /* armure *2 mais -3 vitesse / point */
    bool  u_expose_weakness;     /* crit revele faiblesse 5s, exploit *1.5 */
    bool  u_last_stand;          /* mort -> 1 HP + stats *2 pendant 10s */
    /* state runtime des flags : timers / charges qui changent par salle. */
    float r_base;                /* hitbox de base (u_crit_shrink) */
    bool  phoenix_charge;        /* charge disponible (u_phoenix_revive) */
    bool  last_stand_charge;     /* charge disponible (u_last_stand) */
    float last_stand_t;          /* timer du buff x2 actif */
    int   kill_stack_count;      /* stacks de u_kill_stack_dmg (0..20) */
    int   hazard_stacks;         /* 0..10 (par tile traversee) */

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
    T_WALL_CRACKED,    /* mur destructible : casse par AOE > seuil */
} TileKind;

typedef struct {
    int x, y, w, h;
    bool cleared;
    int  enemies_to_spawn;
    int  spawn_timer_ms;
    bool is_boss_room;
    bool boss_spawned;
    bool is_debug_room;       /* salle bac-a-sable spawnee via options.debug_room */
    bool visited;             /* devient true quand le joueur entre dedans (minimap) */
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
    /* uniques decouverts (par id). Cf uniques.c. */
    bool unique_seen[32];
    /* FORGE : bonus de dmg permanent par arme (0..FORGE_MAX_LEVEL). Cout
     * croissant. Lu par weapon_init_defaults pour ajuster base_dmg. */
    int  weapon_dmg_bonus[W_COUNT];
} MetaSave;
#define FORGE_MAX_LEVEL 5

/* ---------- SHOP ---------- */
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
    int          mob_healthbars;    /* 1 = barres de vies flottantes au-dessus
                                       des ennemis (toujours visibles, pas
                                       juste quand HP < max). */
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
    Surface       surfaces[MAX_SURFACES];
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
    /* sous-panneau du hub : 0 = aucun, 1 = TEMPLE (stat upgrades),
     * 2 = FORGE (weapon upgrades), 3 = LICHE (stub). TAVERNE bascule
     * vers GS_CHOOSE_HERO ; DOOR bascule vers game_start_new_run. */
    int           hub_sub_open;
    int           hub_sub_cursor;

    int           run_kills;
    float         run_time;
    /* stats de run affichees sur l ecran de mort */
    int           run_damage_dealt;       /* cumul total */
    int           run_best_combo_size;    /* nb d elements distincts du
                                            meilleur combo joue */
    int           run_legendary_drops;    /* nb d items legendaires / uniques drop */

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
    float         boss_death_t;   /* >0 = animation de mort en cours */
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

    /* seed de la run : capture au demarrage, utilise pour la generation
     * du donjon. Affiche pour permettre de rejouer la meme seed. */
    unsigned      run_seed;
    /* Pre-roll des drops : a chaque run, on tire un nombre fixe de
     * talismans (7) et d uniques (3), DISPATCHES sur les kills via
     * milestones tries. Quand run_kills atteint un milestone, on
     * spawne le drop. Invisible cote joueur -- seule la seed dicte
     * l ordre des spawns. */
    int           talisman_drops[RUN_TALISMAN_MAX];
    int           talisman_drops_idx;
    int           unique_drops  [RUN_UNIQUE_MAX];
    int           unique_drops_idx;

    /* DPS smoothed (TTK debug + UI). Recharge a chaque world_enemy_damage
     * et decay exponentiel chaque frame. */
    float         dps_smooth;
    float         dps_frame_acc;     /* dmg accumule cette frame */

    /* OVERDRIVE killstreak : "moment de rupture".
     *   - chaque kill incremente killstreak_count + reset killstreak_t = 3s.
     *   - quand le compteur atteint OVERDRIVE_TRIGGER (5), overdrive_t
     *     passe a 5s : +50% atk speed, +30% dmg, +20% crit, aura visible.
     *   - quand killstreak_t expire, le compteur retombe a 0. */
    int           killstreak_count;
    float         killstreak_t;
    float         overdrive_t;

    /* codex */
    int           codex_tab;       /* 0=combos 1=talismans 2=equip 3=armes */
    int           codex_cursor;    /* row in current tab */
    int           codex_scroll;    /* premier item visible (defilement) */

    /* TOASTS : 4 slots circulaires. Une notification affichee bas-droite
     * sous le HUD pour signaler les decouvertes meta (combo, element,
     * heros, unique, etc.) sans interrompre le gameplay. Cf game.c
     * toast_push(). */
    struct {
        char     text[48];
        uint32_t color;
        float    life;        /* secondes restantes -- 0 = slot libre */
        float    life_max;
    }             toasts[4];
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
/* Construit la scene HUB (Cimetiere walkable) : une grande salle
 * unique avec 5 batiments en NPC. Cf hub_buildings[] dans main.c.
 * Reset les pools (enemies/pickups/projectiles) car le hub n'en a
 * pas. Place le joueur au centre. */
void  hub_init(Game *g);
/* Accesseurs sur la table HUB_BUILDINGS de main.c (utilisee par
 * render_world.c pour dessiner les batiments en 3D). */
typedef struct HubBuilding HubBuilding;
int                   hub_building_count(void);
const HubBuilding    *hub_building_get(int i);
float                 hub_building_x(const HubBuilding *b);
float                 hub_building_y(const HubBuilding *b);
float                 hub_building_r(const HubBuilding *b);
const char           *hub_building_name(const HubBuilding *b);
int                   hub_building_sub_id(const HubBuilding *b);
/* Salle debug (options.debug_room) : carve une chambre supplementaire reliee
 * a la salle de spawn et la remplit d'un exemplaire de chaque arme,
 * element et equipement legendaire. Idempotent : ne fait rien si la salle
 * existe deja sur le dungeon (room avec is_debug_room). */
void  dungeon_add_debug_room(Game *g);

/* assets : habillage du monde (props 3D decoratifs + particules d ambiance).
 * Cycle : populate apres dungeon_generate, tick par frame en GS_RUN,
 * render dans render_world apres le terrain. Idempotent sur gen_id. */
void  world_assets_reset   (Game *g);
void  world_assets_populate(Game *g);
void  world_assets_tick    (Game *g);
void  world_assets_render  (Game *g);
/* secoue / fait sauter un prop si un objet passe a (x_world, y_world)
 * dans le rayon donne. Renvoie true si au moins un prop a ete touche.
 * Utilise par les knockback rapides pour donner du feedback visuel
 * sans collision dure. */
bool  world_assets_bump_at (Game *g, float x, float y, float radius);

/* ---- BIOMES ----
 * Theme d'etage : 5 biomes qui se partagent les 10 etages, chacun avec
 * un element signature, un tint colore sur le terrain, une couleur
 * d ambient particles et un ennemi aligne. Cf biomes.c. */
int         biome_for_floor       (int floor_index);
const char *biome_name            (int biome_id);
Element     biome_element         (int biome_id);
void        biome_tint            (int biome_id, float *r, float *g, float *b);
uint32_t    biome_ambient_color   (int biome_id);
int         biome_ambient_chance_p1000(int biome_id);
int         biome_aligned_kind    (int biome_id);   /* EnemyKind */

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

/* surfaces (flaques eau/huile/feu/glace/electrifiee). Pool prive
 * dans Game.surfaces. */
int   surface_spawn  (Game *g, SurfaceKind kind, float x, float y,
                       float r, float life);
void  update_surfaces(Game *g);
void  render_surfaces(Game *g);     /* dessine les disques au sol */
/* convertit toutes les SURF_WATER dans le rayon en SURF_ELECTRIFIED.
 * Appele par projectiles.c quand un proj EL_LIGHTNING touche une
 * surface ou un mur a proximite d eau. */
void  surface_lightning_hit(Game *g, float x, float y, float radius);
/* convertit les SURF_WATER en SURF_MUD si touchees par un proj EL_EARTH. */
void  surface_earth_hit    (Game *g, float x, float y, float radius);
/* appele a la mort d un ennemi : si une eau est dans le rayon, elle
 * devient sang. Sinon a 20% spawne une petite flaque de sang. */
void  surface_blood_drop   (Game *g, float x, float y);
/* convertit les SURF_OIL en SURF_TAR (huile corrompue) si touchees par
 * un proj EL_DARK ou EL_VOID. */
void  surface_void_hit     (Game *g, float x, float y, float radius);
/* peuple chaque salle (sauf spawn) de quelques surfaces ambient typees
 * par le biome courant. Appele apres world_assets_populate. */
void  surfaces_seed_biome  (Game *g);

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
/* nom thematique d un boss par variant (= biome). Cf heroes.c. */
const char *boss_title_for_variant(int variant);

/* ---- META : achats permanents au sanctuaire ----
 * 4 stats (0=HP, 1=ARMOR, 2=SPEED, 3=DMG_PCT). Le cout suit un ramp
 * lineaire base + base * level pour eviter le farm trivial des
 * premiers etages. Cap a 10 niveaux par stat.
 */
#define PERM_MAX_LEVEL 10
int         perm_stat_level (const MetaSave *m, int kind);
int         perm_stat_cost  (const MetaSave *m, int kind);   /* cout du prochain achat */
const char *perm_stat_label (int kind);
int         perm_stat_step  (int kind);                       /* delta par niveau */

/* FORGE : ameliore le base_dmg d une arme +5 par niveau, cap 5.
 * kind dans [W_FISTS..W_AXE]. cost = (level + 1) * 40 ; 0 = max. */
int         forge_level (const MetaSave *m, WeaponKind k);
int         forge_cost  (const MetaSave *m, WeaponKind k);
bool        forge_buy   (Game *g, WeaponKind k);    /* renvoie true si paye */
/* couleurs (cape + tunique) du heros, partagees entre render_choose_hero
 * et la paper-doll inventaire. Cf heroes.c. */
void        hero_palette(HeroClass h, uint32_t *cape, uint32_t *tunic);
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
/* Inflige des dmg au joueur en provenance de (srcx, srcy) en pixels-monde.
 * Sert au knockback et a la direction des sparks. Wrapper player_take_damage
 * sans source = pas de knockback (utiliser pour DoT sols, etc). */
void        player_take_damage_from(Game *g, float dmg, float srcx, float srcy);
void        player_take_damage(Game *g, float dmg);

/* audio */
typedef enum {
    SFX_PUNCH = 0, SFX_HIT, SFX_HEAVY_HIT, SFX_SWING, SFX_EXPLODE,
    SFX_PICKUP, SFX_COIN, SFX_LEVELUP, SFX_PLAYER_HURT, SFX_DEATH,
    SFX_BOSS, SFX_PORTAL, SFX_SHOOT, SFX_ZAP, SFX_FUSE,
    SFX_HEARTBEAT,
    /* signatures par arme : chaque weapon kind a son swing dedie.
     * Les SFX_*_HIT/PUNCH/HEAVY_HIT restent pour l impact. */
    SFX_SWORD_SLASH,
    SFX_AXE_SWING,
    SFX_BOW_FIRE,
    SFX_WAND_CAST,
    SFX_COUNT
} SfxId;
void  audio_init(Game *g);
void  audio_shutdown(Game *g);
void  sfx_play(Game *g, SfxId id);
/* version etendue : pitch (1.0 = normal, 0.5 = octave en bas, 2.0 = en haut)
 * et multiplicateur de volume (0..1+). Utilise par les variations
 * contextuelles : low-HP grunt, heavy thump etc. */
void  sfx_play_ex(Game *g, SfxId id, float pitch, float vol_mul);

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

/* toast notifications : reserves un slot, decremente toutes les
 * frames. La life est dans le slot, pas globale. */
void  toast_push (Game *g, const char *text, uint32_t color, float life);
void  toast_tick (Game *g);
void  toast_render(Game *g);

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
    /* flags build-defining propages des uniques. Identiques aux champs
     * Player.u_* ; recopies a la fin du recompute. */
    bool  u_explosions_attract;
    bool  u_crit_shrink;
    bool  u_corpse_mines;
    bool  u_free_dash;
    int   u_drone_count;
    /* rework v2 -- 14 flags supplementaires (cf Player) */
    bool  u_berserk_cd;
    bool  u_element_absorb;
    bool  u_hazard_immune;
    bool  u_hazard_stacks;
    bool  u_phoenix_revive;
    bool  u_kill_wave;
    bool  u_frontal_immune;
    bool  u_dodge_attack;
    bool  u_crowd_regen;
    bool  u_stun_on_melee;
    bool  u_void_trail;
    bool  u_kill_stack_dmg;
    bool  u_heavy_armor;
    bool  u_expose_weakness;
    bool  u_last_stand;
} StatBlock;

/* shop */
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
/* vente d'un item du sac : donne coins selon item_sell_value puis libere
 * le slot. Renvoie le gain en coins ou 0 si echec. */
int   shop_sell_item(Game *g, int inv_index);

/* procedural names */
void  enemy_generate_name(Enemy *e, int floor_index);

/* mod support */
void  mods_load(Game *g);

/* drop tables */
Rarity rarity_for_floor_elite(int floor_index);
Rarity rarity_for_floor_boss(int floor_index);

/* inventory */
Item  item_make(EquipSlot slot, Rarity rarity, int sub_kind);
const char *affix_name (Affix a);
/* formate un libelle court pour un affixe ("+12 PV", "+5% Vol vie", ...). */
void        affix_label(const ItemAffix *af, char *buf, int bufsz);
/* genere un nom procedural pour un item normal (non-unique). Deterministe
 * sur (slot, rarity, base_kind, affixes) pour rester stable. */
void        item_generate_name(Item *it);
/* nom de l archetype d un item (Offensif / Defensif / ...). base_kind 0..4. */
const char *archetype_name    (int base_kind);
/* detruit un item du sac (libere le slot, no-op si vide). */
bool        inventory_destroy(Game *g, int inv_index);
/* valeur de revente en coins (rarity-based). */
int         item_sell_value(const Item *it);

/* ---- UNIQUES ----
 * Items pre-definis avec un effet de gameplay specifique. Drop tres rare
 * sur elite/boss. Cf uniques.c pour la table. */
int         unique_def_count(void);          /* nb d'entrees dans UNIQUE_DEFS */
const char *unique_def_name(int id);
const char *unique_def_desc(int id);
EquipSlot   unique_def_slot(int id);
/* construit un Item complet (occupied, slot, rarity=R_LEGENDARY, name,
 * is_unique=true, unique_id=id, affixes vides). Pour la table de loot. */
Item        unique_make(int id);
/* applique les bonus du unique sur le StatBlock du joueur. */
void        unique_apply_to_block(int id, StatBlock *sb);
/* roule un unique sur l'etage : -1 = pas de drop. Probabilite croissante
 * avec elite/boss/etage. */
int         unique_roll_drop(int floor_index, bool elite, bool boss);
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
