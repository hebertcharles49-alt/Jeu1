/*
 * combat_internal.h - structures et helpers partages entre elements.c
 * (data + compute_combo) et combat.c (fire_* + update_weapons).
 *
 * ComboFx est le resultat structurel du calcul d'un combo : il est
 * consomme par les fire_* pour produire un coup d'arme. Le calcul lui-
 * meme est dans elements.c (compute_combo), pour pouvoir etre reutilise
 * cote ennemis (combo_apply_to_enemy_projectile).
 */
#ifndef COMBAT_INTERNAL_H
#define COMBAT_INTERNAL_H
#include "game.h"

typedef struct {
    float dmg_mul;
    float cd_mul;
    float range_mul;
    bool  pierces;
    bool  homing;
    bool  chain;
    bool  aoe_explode;
    bool  spawn_fairy;
    bool  reflect_proj;
    bool  lifesteal;
    bool  engelure;          /* applique slow + cold-DOT sur l'ennemi touche */
    /* === personnalite par tag : effets passifs portes par chaque TAG_*.
     * Compute pass dans apply_tag_personality juste apres Couche 1. === */
    float knockback_mul;     /* TAG_HEAVY *1.5, TAG_AIRY *1.3 */
    float status_dur_mul;    /* TAG_PERSISTENT *1.5, TAG_BURNING/WET *1.2 */
    float crit_chance_add;   /* TAG_UNSTABLE +0.15, TAG_VOLATILE +0.10 */
    float proj_speed_mul;    /* TAG_LIGHT *1.20 */
    bool  freeze_on_hit;     /* TAG_FROZEN : stun 0.8s a l'impact */
    bool  puddle_on_hit;     /* TAG_FLUID : surface eau a l'impact */
    bool  heal_on_kill;      /* TAG_DIVINE : +1 HP sur kill */
    bool  void_on_kill;      /* TAG_SHADOW : surface void sur kill */
    bool  corrosive_stack;   /* TAG_CORROSIVE : +5% dmg recu cumulable */
    int   extra_proj;
    Element status;
    uint32_t color;
    const char *tag;
} ComboFx;

/* Applique l'etat engelure (slow + cold-DOT) sur un ennemi. Helper
 * partage par fire_* (melee) et projectiles.c (range). */
void enemy_apply_engelure(Game *g, int enemy_idx, float dmg);

/* calcule un ComboFx complet (couches 1 a 3) pour un mask d'elements. */
ComboFx combo_compute(int mask);

/* (re)synchronise le loop actif selon le mask. Si le mask change, l'index
 * et le mask actifs sont mis a jour ; l'intensite est conservee. */
void combo_refresh_active_loop(Game *g, int mask);

/* applique les modificateurs overload du loop actif sur un ComboFx. No-op
 * si pas de loop ou intensite < seuil. */
void combo_apply_loop_modifiers(Game *g, ComboFx *fx);

/* ---- helpers de combat partages entre weapons (fire_*) et projectiles ---- */

/* index de l'ennemi vivant le plus proche dans la portee, -1 sinon. */
int  nearest_enemy(Game *g, float x, float y, float range, float *out_d);

/* AOE circulaire avec dmg + status sur les ennemis dans le rayon, plus
 * un anneau de particules. */
void do_aoe_at(Game *g, float x, float y, float radius, float dmg,
               Element status, uint32_t color);

/* chain lightning : touche jusqu'a `hops` ennemis voisins, dmg decroit. */
void chain_hit(Game *g, int from_idx, float dmg, Element status,
               int hops, uint32_t color);

/* burst circulaire de particules. */
void burst_particles(Game *g, float x, float y, int n, uint32_t color,
                     float speed_max);

#endif
