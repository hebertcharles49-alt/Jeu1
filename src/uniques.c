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
} UniqueDef;

/* 20 uniques = 2 par triple combo (Tempete, Volcan, Dechirure, Phenix,
 * Tsunami, Marais, Brume Mortelle, Effondrement, Forge Solaire, Jugement). */
static const UniqueDef UNIQUE_DEFS[] = {
    /* === Tempete (Feu+Eau+Foudre) === */
    { "Anneau de Tempete",       "+20% atk speed, -10% PV max",
      SLOT_BELT,    .d_maxhp=-10.f, .d_atk_speed_red=0.20f },
    { "Cuirasse de Foudre",      "+3 armure, foudre +30%",
      SLOT_CHEST,   .d_armor=3.f, .d_aff_el=EL_LIGHTNING, .d_aff_val=0.30f },
    /* === Volcan (Feu+Terre+Air) === */
    { "Pendentif Volcanique",    "+15% degats, +regen 1/s",
      SLOT_BELT,    .d_dmg_mul=0.15f, .d_regen=1.f },
    { "Bottes Volcaniques",      "+30 vitesse, feu +25%",
      SLOT_BOOTS,   .d_speed=30.f, .d_aff_el=EL_FIRE, .d_aff_val=0.25f },
    /* === Dechirure (Vide+Fee+Foudre) === */
    { "Voile du Vide",           "+15% vol de vie, vide +20%",
      SLOT_CHEST,   .d_lifesteal=0.15f, .d_aff_el=EL_VOID, .d_aff_val=0.20f },
    { "Couronne Spectrale",      "+20% crit, +0.5 crit dmg",
      SLOT_HELM,    .d_crit_chance=0.20f, .d_crit_dmg=0.5f },
    /* === Phenix (Feu+Air+Fee) === */
    { "Plume du Phenix",         "+5 regen/s, fee +25%",
      SLOT_BELT,    .d_regen=5.f, .d_aff_el=EL_FAE, .d_aff_val=0.25f },
    { "Manteau Solaire",         "+30% degats elementaires",
      SLOT_CHEST,   .d_dmg_mul=0.30f, .d_aff_el=EL_FIRE, .d_aff_val=0.20f },
    /* === Tsunami (Eau+Terre+Foudre) === */
    { "Bouclier Maremoteur",     "+5 armure, eau +25%",
      SLOT_CHEST,   .d_armor=5.f, .d_aff_el=EL_WATER, .d_aff_val=0.25f },
    { "Laniere Tsunami",         "+10% atk speed, eau +20%",
      SLOT_BELT,    .d_atk_speed_red=0.10f, .d_aff_el=EL_WATER, .d_aff_val=0.20f },
    /* === Marais (Eau+Air+Terre) === */
    { "Bottes Marecage",         "+30% esquive, +15 vitesse",
      SLOT_BOOTS,   .d_dodge=0.30f, .d_speed=15.f },
    { "Talisman Brumeux",        "+25% PV max",
      SLOT_HELM,    .d_maxhp=25.f },
    /* === Brume Mortelle (Vide+Eau+Air) === */
    { "Voile de Brume",          "+15% esquive, tenebres +20%",
      SLOT_CHEST,   .d_dodge=0.15f, .d_aff_el=EL_DARK, .d_aff_val=0.20f },
    { "Couronne Empoisonneuse",  "+20% degats, +5% crit",
      SLOT_HELM,    .d_dmg_mul=0.20f, .d_crit_chance=0.05f },
    /* === Effondrement (Terre+Air+Vide) === */
    { "Ceinture Effondrement",   "+30 PV max, +2 armure",
      SLOT_BELT,    .d_maxhp=30.f, .d_armor=2.f },
    { "Pendentif Tellurique",    "+25% degats melee, terre +25%",
      SLOT_GLOVES,  .d_dmg_mul=0.25f, .d_aff_el=EL_EARTH, .d_aff_val=0.25f },
    /* === Forge Solaire (Acier+Feu+Foudre) === */
    { "Gantelets de Forge",      "+40% degats melee",
      SLOT_GLOVES,  .d_dmg_mul=0.40f, .d_aff_el=EL_STEEL, .d_aff_val=0.25f },
    { "Bottes du Marteau",       "+20% atk speed",
      SLOT_BOOTS,   .d_atk_speed_red=0.20f },
    /* === Jugement (Tenebres+Sacre+Foudre) === */
    { "Couronne du Jugement",    "+25% crit, +1 crit dmg",
      SLOT_HELM,    .d_crit_chance=0.25f, .d_crit_dmg=1.f },
    { "Pendentif Sanctifie",     "+30% degats, sacre +25%",
      SLOT_BELT,    .d_dmg_mul=0.30f, .d_aff_el=EL_HOLY, .d_aff_val=0.25f },
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
