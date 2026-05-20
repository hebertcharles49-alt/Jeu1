/*
 * uniques.c - items uniques (20 entrees, 2 par triple combo).
 *
 * Chaque unique a un nom fixe, un slot, une description courte et un
 * ensemble de stat-deltas appliques au StatBlock du joueur. Drop rare
 * sur elite/boss, gere par unique_roll_drop().
 *
 * Le choix "2 par triple combo" donne 10 themes (Tempete, Volcan, ...) x
 * 2 variantes : un offensif + un defensif. Cf TRIPLE_LOOPS dans elements.c.
 */
#include "game.h"
#include <stdio.h>
#include <stdlib.h>

typedef struct {
    const char *name;
    const char *desc;
    EquipSlot   slot;
    /* deltas appliques au StatBlock (additifs sauf dmg_mul = multiplicatif) */
    float d_maxhp;
    float d_armor;
    float d_speed;
    float d_dmg_mul;       /* multiplicatif : 0.20f = +20% (sb *= 1+x) */
    float d_lifesteal;
    float d_regen;
    float d_atk_speed_red; /* fraction qui REDUIT atk_speed (0.20 = -20% cd) */
    float d_crit_chance;
    float d_crit_dmg;
    float d_range_mul;
    float d_dodge;
    /* affinite elementaire (id, valeur). EL_NONE = pas d'effet. */
    Element d_aff_el;
    float   d_aff_val;
    /* ---- Flags build-defining (rule-changing) ---- */
    bool  u_explosions_attract;
    bool  u_crit_shrink;
    bool  u_corpse_mines;
    bool  u_free_dash;
    int   u_drone_count;
    /* rework v2 */
    bool  u_berserk_cd, u_element_absorb;
    bool  u_hazard_immune, u_hazard_stacks;
    bool  u_phoenix_revive, u_kill_wave, u_frontal_immune;
    bool  u_dodge_attack, u_crowd_regen, u_stun_on_melee;
    bool  u_void_trail, u_kill_stack_dmg, u_heavy_armor;
    bool  u_expose_weakness, u_last_stand;
} UniqueDef;

/* 20 uniques = 2 par triple combo. Repartition slots equilibree :
 * Helm x4 / Chest x5 / Belt x3 / Boots x2 / Gloves x3 / Legs x3.
 * 6 items "gardes" + 14 reworkes avec flags rule-changing. */
static const UniqueDef UNIQUE_DEFS[] = {
    /* === Tempete (Feu+Eau+Foudre) === */
    { "Oeil du Cyclone",         "Sous 50% HP : cooldowns x0.5",
      SLOT_HELM,    .d_dmg_mul=0.10f,
      .d_aff_el=EL_LIGHTNING, .d_aff_val=0.20f, .u_berserk_cd=true },
    { "Cuirasse Orageuse",       "Hit elementaire : +30% affinite 8s",
      SLOT_CHEST,   .d_armor=2.f, .u_element_absorb=true },
    /* === Volcan (Feu+Terre+Air) === */
    { "Pendentif Volcanique",    "Tes AOE attirent les ennemis",
      SLOT_BELT,    .d_dmg_mul=0.10f, .u_explosions_attract=true },
    { "Semelles de Lave",        "Immune aux hazards. Stacks +5% dmg",
      SLOT_BOOTS,   .d_aff_el=EL_FIRE, .d_aff_val=0.15f,
      .u_hazard_immune=true, .u_hazard_stacks=true },
    /* === Dechirure (Vide+Fee+Foudre) === */
    { "Voile du Vide",           "2 drones spectraux orbitent et tirent",
      SLOT_CHEST,   .u_drone_count=2 },
    { "Couronne Spectrale",      "Chaque crit te reduit. Crit +15%",
      SLOT_HELM,    .d_crit_chance=0.15f, .u_crit_shrink=true },
    /* === Phenix (Feu+Air+Fee) === */
    { "Plume du Phenix",         "Dash sans cooldown",
      SLOT_BELT,    .u_free_dash=true },
    { "Cape Solaire",            "A 0 HP : revis a 30% (1 / salle)",
      SLOT_LEGS,    .d_aff_el=EL_FIRE, .d_aff_val=0.25f,
      .u_phoenix_revive=true },
    /* === Tsunami (Eau+Terre+Foudre) === */
    { "Ceinture des Marees",     "Chaque kill : vague de repulsion + dmg",
      SLOT_BELT,    .d_aff_el=EL_WATER, .d_aff_val=0.20f,
      .u_kill_wave=true },
    { "Jambieres Abyssales",     "-40% vitesse, immune proj de face",
      SLOT_LEGS,    .d_armor=4.f, .u_frontal_immune=true },
    /* === Marais (Eau+Air+Terre) === */
    { "Dague du Serpent",        "Esquive declenche une attaque gratuite",
      SLOT_GLOVES,  .d_dodge=0.10f, .u_dodge_attack=true },
    { "Veste de Roseaux",        "Regen += 0.1 / ennemi vivant en salle",
      SLOT_CHEST,   .d_maxhp=15.f, .u_crowd_regen=true },
    /* === Brume Mortelle (Vide+Eau+Air) === */
    { "Voile de Brume",          "+15% esquive, 1 drone supplementaire",
      SLOT_CHEST,   .d_dodge=0.15f, .u_drone_count=1 },
    { "Couronne Empoisonneuse",  "Les morts laissent des mines",
      SLOT_HELM,    .d_dmg_mul=0.10f, .u_corpse_mines=true },
    /* === Effondrement (Terre+Air+Vide) === */
    { "Gants du Seisme",         "Melee : 20% stun 1s (dmg recus x2)",
      SLOT_GLOVES,  .d_dmg_mul=0.20f, .u_stun_on_melee=true },
    { "Bottes du Vide",          "Le dash laisse un champ de Vide 3s",
      SLOT_BOOTS,   .d_aff_el=EL_VOID, .d_aff_val=0.20f,
      .u_void_trail=true },
    /* === Forge Solaire (Acier+Feu+Foudre) === */
    { "Marteau de Forge",        "Chaque kill : +2 dmg flat (max +40)",
      SLOT_GLOVES,  .d_aff_el=EL_STEEL, .d_aff_val=0.20f,
      .u_kill_stack_dmg=true },
    { "Jambieres de Mithril",    "Armure x2 mais -3 vitesse / point",
      SLOT_LEGS,    .d_armor=5.f, .u_heavy_armor=true },
    /* === Jugement (Tenebres+Sacre+Foudre) === */
    { "Oeil du Juge",            "Crit revele la faiblesse 5s. Exploit x1.5",
      SLOT_HELM,    .d_crit_chance=0.15f, .u_expose_weakness=true },
    { "Suaire de Penitence",     "Mort -> 1 HP + stats x2 pendant 10s",
      SLOT_CHEST,   .d_aff_el=EL_HOLY, .d_aff_val=0.20f,
      .u_last_stand=true },
};
#define N_UNIQUES ((int)(sizeof(UNIQUE_DEFS)/sizeof(UNIQUE_DEFS[0])))
_Static_assert(N_UNIQUES <= 32,
    "unique_seen array taille 32 -- augmente MetaSave.unique_seen si tu ajoutes plus");

int unique_def_count(void) { return N_UNIQUES; }

const char *unique_def_name(int id) {
    if (id < 0 || id >= N_UNIQUES) return "?";
    return UNIQUE_DEFS[id].name;
}

const char *unique_def_desc(int id) {
    if (id < 0 || id >= N_UNIQUES) return "";
    return UNIQUE_DEFS[id].desc;
}

EquipSlot unique_def_slot(int id) {
    if (id < 0 || id >= N_UNIQUES) return SLOT_NONE;
    return UNIQUE_DEFS[id].slot;
}

/* Element dominant d'un unique : utilise par le rendu pour teinter
 * les pieces equipees avec une affinite elementaire. EL_NONE si
 * l'unique n'a pas de d_aff_el. */
Element unique_def_element(int id) {
    if (id < 0 || id >= N_UNIQUES) return EL_NONE;
    return UNIQUE_DEFS[id].d_aff_el;
}

Item unique_make(int id) {
    Item it = {0};
    if (id < 0 || id >= N_UNIQUES) return it;
    const UniqueDef *u = &UNIQUE_DEFS[id];
    it.occupied  = true;
    it.slot      = u->slot;
    it.rarity    = R_LEGENDARY;        /* uniques affiches en couleur legendaire */
    it.base_kind = 4;
    it.stat_value = 0.f;               /* les uniques utilisent unique_apply_to_block */
    it.affix_count = 0;
    it.is_unique = true;
    it.unique_id = id;
    snprintf(it.name, sizeof(it.name), "%s", u->name);
    return it;
}

void unique_apply_to_block(int id, StatBlock *sb) {
    if (id < 0 || id >= N_UNIQUES || !sb) return;
    const UniqueDef *u = &UNIQUE_DEFS[id];
    sb->maxhp       += u->d_maxhp;
    sb->armor       += u->d_armor;
    sb->speed       += u->d_speed;
    if (u->d_dmg_mul != 0.f) sb->dmg_mul *= (1.f + u->d_dmg_mul);
    sb->lifesteal   += u->d_lifesteal;
    sb->regen       += u->d_regen;
    if (u->d_atk_speed_red != 0.f) sb->atk_speed *= (1.f - u->d_atk_speed_red);
    sb->crit_chance += u->d_crit_chance;
    sb->crit_dmg    += u->d_crit_dmg;
    if (u->d_range_mul != 0.f) sb->range_mul *= (1.f + u->d_range_mul);
    sb->dodge       += u->d_dodge;
    if (u->d_aff_el > EL_NONE && u->d_aff_el < EL_COUNT) {
        sb->aff[u->d_aff_el] += u->d_aff_val;
    }
    /* propagation des flags rule-changing en OR (sauf u_drone_count
     * additif avec cap 4 = MAX_FAIRIES/8). */
    if (u->u_explosions_attract) sb->u_explosions_attract = true;
    if (u->u_crit_shrink)        sb->u_crit_shrink        = true;
    if (u->u_corpse_mines)       sb->u_corpse_mines       = true;
    if (u->u_free_dash)          sb->u_free_dash          = true;
    sb->u_drone_count += u->u_drone_count;
    if (sb->u_drone_count > 4) sb->u_drone_count = 4;
    /* rework v2 */
    if (u->u_berserk_cd)      sb->u_berserk_cd      = true;
    if (u->u_element_absorb)  sb->u_element_absorb  = true;
    if (u->u_hazard_immune)   sb->u_hazard_immune   = true;
    if (u->u_hazard_stacks)   sb->u_hazard_stacks   = true;
    if (u->u_phoenix_revive)  sb->u_phoenix_revive  = true;
    if (u->u_kill_wave)       sb->u_kill_wave       = true;
    if (u->u_frontal_immune)  sb->u_frontal_immune  = true;
    if (u->u_dodge_attack)    sb->u_dodge_attack    = true;
    if (u->u_crowd_regen)     sb->u_crowd_regen     = true;
    if (u->u_stun_on_melee)   sb->u_stun_on_melee   = true;
    if (u->u_void_trail)      sb->u_void_trail      = true;
    if (u->u_kill_stack_dmg)  sb->u_kill_stack_dmg  = true;
    if (u->u_heavy_armor)     sb->u_heavy_armor     = true;
    if (u->u_expose_weakness) sb->u_expose_weakness = true;
    if (u->u_last_stand)      sb->u_last_stand      = true;
}

/* Tire un id d'unique au hasard avec une probabilite croissante. */
int unique_roll_drop(int floor_index, bool elite, bool boss) {
    /* probabilite : boss 8% + 1% / etage ; elite 1% + 0.3% / etage ; sinon 0 */
    int chance_p1000 = 0;   /* sur 1000 */
    if (boss)       chance_p1000 = 80 + floor_index * 10;
    else if (elite) chance_p1000 = 10 + floor_index * 3;
    if (chance_p1000 <= 0) return -1;
    if ((rand() % 1000) >= chance_p1000) return -1;
    return rand() % N_UNIQUES;
}
