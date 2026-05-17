/*
 * combat.c - armes : init / attache d'elements / firing pipeline.
 *
 * Le reste du systeme de combat est dispatche :
 *   elements.c     - donnees + compute_combo + loops
 *   projectiles.c  - update_projectiles / update_fairies / update_dmgnums
 *   heroes.c       - hero/subclass/enemy names
 *
 * Ce fichier garde :
 *   - les API publiques d'armes (weapon_init_defaults, weapon_attach_element,
 *     weapon_combo_id, weapon_describe, weapon_slot_count, weapon_name)
 *   - les helpers de combat partages (nearest_enemy, do_aoe_at, chain_hit,
 *     burst_particles) exposes via combat_internal.h
 *   - les fire_<kind> + update_weapons
 */
#include "combat_internal.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *weapon_name(WeaponKind w) {
    switch (w) {
        case W_FISTS:  return "Poings";
        case W_SWORD:  return "Epee";
        case W_SHIELD: return "Bouclier";
        case W_BOW:    return "Arc";
        case W_WAND:   return "Baton";
        case W_AXE:    return "Hache";
        default:       return "?";
    }
}

/* nombre de slots talisman selon la qualite :
 *   COMMON/MAGIC -> 1, RARE/EPIC -> 2, LEGENDARY -> 3. */
int weapon_slot_count(Rarity r) {
    int n = 1 + ((int)r) / 2;
    if (n < 1) n = 1;
    if (n > MAX_ELEMENTS_PER_WEAPON) n = MAX_ELEMENTS_PER_WEAPON;
    return n;
}

void weapon_init_defaults(Weapon *w, WeaponKind kind) {
    memset(w, 0, sizeof(*w));
    w->kind = kind;
    w->rarity = R_COMMON;
    switch (kind) {
        case W_FISTS:  w->base_cd = 0.30f; w->base_dmg = 6.f;  w->base_range = 16.f; break;
        case W_SWORD:  w->base_cd = 0.40f; w->base_dmg = 16.f; w->base_range = 26.f; break;
        case W_SHIELD: w->base_cd = 1.7f;  w->base_dmg = 10.f; w->base_range = 26.f; break;
        case W_BOW:    w->base_cd = 0.65f; w->base_dmg = 14.f; w->base_range = 240.f; break;
        case W_WAND:   w->base_cd = 1.4f;  w->base_dmg = 24.f; w->base_range = 130.f; break;
        case W_AXE:    w->base_cd = 1.1f;  w->base_dmg = 26.f; w->base_range = 32.f; break;
        default: break;
    }
}

void weapon_attach_element(Weapon *w, Element e) {
    int max_slots = weapon_slot_count(w->rarity);
    if (w->element_count >= max_slots) {
        /* rotation : on degage le plus ancien dans la limite des slots */
        for (int i = 0; i < max_slots - 1; i++)
            w->elements[i] = w->elements[i + 1];
        w->elements[max_slots - 1] = e;
        w->element_count = max_slots;
    } else {
        w->elements[w->element_count++] = e;
    }
}

int weapon_combo_id(const Weapon *w) {
    uint64_t mask = 0;
    for (int i = 0; i < w->element_count; i++) {
        Element e = w->elements[i];
        if (e > 0 && (int)e < 64) mask |= ((uint64_t)1 << e);
    }
    _Static_assert(EL_COUNT <= 31,
        "weapon_combo_id: passe la signature en uint64_t si EL_COUNT > 31");
    return (int)mask;
}

void weapon_describe(const Weapon *w, char *buf, int bufsz) {
    int n = snprintf(buf, bufsz, "[");
    for (int i = 0; i < w->element_count && n < bufsz - 8; i++) {
        if (i > 0) n += snprintf(buf + n, bufsz - n, "+");
        n += snprintf(buf + n, bufsz - n, "%s", element_name(w->elements[i]));
    }
    if (w->element_count == 0) n += snprintf(buf + n, bufsz - n, "neutre");
    snprintf(buf + n, bufsz - n, "]");
}

/* ---------- HELPERS DE COMBAT (partages avec projectiles.c) ---------- */
int nearest_enemy(Game *g, float x, float y, float range, float *out_d) {
    int best = -1; float bestd = range * range;
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (!g->enemies[i].alive) continue;
        if (g->enemies[i].dying_t > 0.f) continue;
        float dx = g->enemies[i].x - x;
        float dy = g->enemies[i].y - y;
        float d2 = dx * dx + dy * dy;
        if (d2 < bestd) { bestd = d2; best = i; }
    }
    if (out_d) *out_d = sqrtf(bestd);
    return best;
}

void burst_particles(Game *g, float x, float y, int n, uint32_t color, float speed_max) {
    for (int i = 0; i < n; i++) {
        float a = (rand() % 360) * 0.01745f;
        float s = (rand() % 100) / 100.f * speed_max + speed_max * 0.3f;
        particle_spawn_kind(g, x, y, cosf(a) * s, sinf(a) * s, 0.5f, color, 2.5f, 2);
    }
}

void do_aoe_at(Game *g, float x, float y, float radius, float dmg, Element status, uint32_t color) {
    bool attract = g->player.u_explosions_attract;
    /* murs fissures dans le rayon : convertis en T_FLOOR (destruction).
     * Seulement si l AOE a un mini dmg (eviter de tout casser avec une
     * micro explosion). */
    if (dmg > 10.f) {
        int rt = (int)(radius / TILE) + 1;
        int tx0 = (int)(x / TILE) - rt;
        int ty0 = (int)(y / TILE) - rt;
        int tx1 = (int)(x / TILE) + rt;
        int ty1 = (int)(y / TILE) + rt;
        for (int ty = ty0; ty <= ty1; ty++) {
            for (int tx = tx0; tx <= tx1; tx++) {
                if (tx <= 0 || ty <= 0 || tx >= MAP_W - 1 || ty >= MAP_H - 1) continue;
                if (g->dungeon.tiles[ty][tx] != T_WALL_CRACKED) continue;
                float cx = tx * (float)TILE + TILE * 0.5f;
                float cy = ty * (float)TILE + TILE * 0.5f;
                float dxw = cx - x, dyw = cy - y;
                if (dxw*dxw + dyw*dyw > radius * radius) continue;
                g->dungeon.tiles[ty][tx] = T_FLOOR;
                g->dungeon.gen_id++;   /* invalide la cache du mesh */
                /* burst de gravats */
                for (int k = 0; k < 14; k++) {
                    float a = (rand() % 360) * 0.01745f;
                    float s = 60.f + (rand() % 80);
                    particle_spawn_kind(g, cx, cy,
                                        cosf(a) * s, sinf(a) * s,
                                        0.55f, 0x806050FF, 2.5f, 2);
                }
                sfx_play(g, SFX_EXPLODE);
            }
        }
    }
    /* surface au sol : feu / glace selon l element. 40% de chance pour
     * eviter de saturer le pool. */
    if (status == EL_FIRE && (rand() % 100) < 40) {
        surface_spawn(g, SURF_FIRE, x, y, radius * 0.6f, 0.f);
    } else if (status == EL_WATER && (rand() % 100) < 35) {
        surface_spawn(g, SURF_WATER, x, y, radius * 0.7f, 0.f);
    } else if (status == EL_HOLY && (rand() % 100) < 50) {
        surface_spawn(g, SURF_HOLY, x, y, radius * 0.6f, 0.f);
    } else if (status == EL_DARK && (rand() % 100) < 40) {
        surface_spawn(g, SURF_SHADOW, x, y, radius * 0.6f, 0.f);
    }
    for (int i = 0; i < MAX_ENEMIES; i++) {
        Enemy *e = &g->enemies[i];
        if (!e->alive) continue;
        float dx = e->x - x, dy = e->y - y;
        if (dx * dx + dy * dy < radius * radius) {
            /* BUILD-DEF : unique "Pendentif Volcanique" inverse le knockback
             * pour aspirer les ennemis vers le centre de l explosion. */
            float kx = dx * 2.f, ky = dy * 2.f;
            if (attract) { kx = -kx * 1.5f; ky = -ky * 1.5f; }
            world_enemy_damage(g, i, dmg, status, kx, ky);
        }
    }
    int n = (int)(radius * 0.7f); if (n > 50) n = 50;
    for (int i = 0; i < n; i++) {
        float a = (i / (float)n) * 6.2831f;
        particle_spawn_kind(g, x + cosf(a) * radius * 0.4f, y + sinf(a) * radius * 0.4f,
                            cosf(a) * 80.f, sinf(a) * 80.f, 0.4f, color, 3.f, 0);
    }
    burst_particles(g, x, y, 12, color, 80.f);
}

void chain_hit(Game *g, int from_idx, float dmg, Element status, int hops, uint32_t color) {
    if (hops <= 0 || from_idx < 0) return;
    Enemy *src = &g->enemies[from_idx];
    int best = -1; float bestd = 90.f * 90.f;
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (i == from_idx) continue;
        Enemy *e = &g->enemies[i];
        if (!e->alive) continue;
        float dx = e->x - src->x, dy = e->y - src->y;
        float d2 = dx * dx + dy * dy;
        if (d2 < bestd) { bestd = d2; best = i; }
    }
    if (best < 0) return;
    Enemy *t = &g->enemies[best];
    float dx = t->x - src->x, dy = t->y - src->y;
    float d = sqrtf(dx * dx + dy * dy) + 0.01f;
    int steps = (int)(d / 3.f);
    for (int s = 0; s < steps; s++) {
        float fx = src->x + dx * (s / (float)steps);
        float fy = src->y + dy * (s / (float)steps);
        particle_spawn_kind(g, fx, fy, 0, 0, 0.18f, color, 2.f, 0);
    }
    sfx_play(g, SFX_ZAP);
    world_enemy_damage(g, best, dmg, status, dx * 0.3f, dy * 0.3f);
    chain_hit(g, best, dmg * 0.7f, status, hops - 1, color);
}

/* swing animation cue on player */
static void cue_swing(Player *p, int kind, float ax, float ay) {
    p->anim_t = 0.18f;
    p->anim_kind = kind;
    p->anim_dir_x = ax;
    p->anim_dir_y = ay;
}

/* ---------- FIRE_<KIND> ---------- */
static void fire_fists(Game *g, Weapon *w, ComboFx fx) {
    Player *p = &g->player;
    float ax = p->aim_x - p->x, ay = p->aim_y - p->y;
    float al = sqrtf(ax * ax + ay * ay) + 0.001f;
    ax /= al; ay /= al;
    float reach = w->base_range * fx.range_mul;
    float dmg  = (w->base_dmg + p->flat_dmg) * fx.dmg_mul;
    int hits = 0;
    for (int i = 0; i < MAX_ENEMIES; i++) {
        Enemy *e = &g->enemies[i];
        if (!e->alive) continue;
        float dx = e->x - p->x, dy = e->y - p->y;
        float d = sqrtf(dx * dx + dy * dy);
        if (d > reach + e->r) continue;
        float dot = (dx * ax + dy * ay) / (d + 0.001f);
        if (dot < 0.5f) continue;
        world_enemy_damage(g, i, dmg, fx.status, ax * 200.f, ay * 200.f);
        if (fx.lifesteal) {
            p->hp += dmg * 0.05f;
            if (p->hp > p->maxhp) p->hp = p->maxhp;
        }
        hits++;
    }
    cue_swing(p, 5, ax, ay);
    sfx_play(g, hits > 0 ? SFX_PUNCH : SFX_SWING);
    if (hits > 0) { g->shake_t = 0.10f; g->shake_mag = 2.f; g->hitstop_t = 0.04f; }
    if (fx.spawn_fairy) fairy_spawn(g, p->x, p->y, fx.status);
}

static void fire_sword(Game *g, Weapon *w, ComboFx fx) {
    Player *p = &g->player;
    float ax = p->aim_x - p->x, ay = p->aim_y - p->y;
    float al = sqrtf(ax * ax + ay * ay) + 0.001f;
    ax /= al; ay /= al;
    float reach = w->base_range * fx.range_mul;
    float dmg  = (w->base_dmg + p->flat_dmg) * fx.dmg_mul;
    int hits = 0;
    for (int i = 0; i < MAX_ENEMIES; i++) {
        Enemy *e = &g->enemies[i];
        if (!e->alive) continue;
        float dx = e->x - p->x, dy = e->y - p->y;
        float d = sqrtf(dx * dx + dy * dy);
        if (d > reach + e->r) continue;
        float dot = (dx * ax + dy * ay) / (d + 0.001f);
        if (dot < 0.4f) continue;
        world_enemy_damage(g, i, dmg, fx.status, ax * 220.f, ay * 220.f);
        if (fx.chain) chain_hit(g, i, dmg * 0.6f, fx.status, 2, fx.color);
        if (fx.aoe_explode) do_aoe_at(g, e->x, e->y, 30.f, dmg * 0.5f, fx.status, fx.color);
        if (fx.lifesteal) {
            p->hp += dmg * 0.05f;
            if (p->hp > p->maxhp) p->hp = p->maxhp;
        }
        hits++;
    }
    for (int i = 0; i < 14; i++) {
        float t = i / 14.f;
        float a = atan2f(ay, ax) - 0.9f + t * 1.8f;
        float s = reach * (0.6f + (rand() % 50) / 100.f);
        particle_spawn_kind(g, p->x + cosf(a) * s, p->y + sinf(a) * s,
                            cosf(a) * 60, sinf(a) * 60, 0.22f, fx.color, 2.f, 2);
    }
    cue_swing(p, 0, ax, ay);
    sfx_play(g, hits > 0 ? SFX_HIT : SFX_SWING);
    if (hits > 0) { g->shake_t = 0.16f; g->shake_mag = 3.5f; g->hitstop_t = 0.05f; }
    if (fx.spawn_fairy) fairy_spawn(g, p->x, p->y, fx.status);
}

static void fire_shield(Game *g, Weapon *w, ComboFx fx) {
    Player *p = &g->player;
    p->invuln_t = 0.5f;
    float radius = w->base_range * fx.range_mul + 8.f;
    float dmg = (w->base_dmg + p->flat_dmg) * fx.dmg_mul;
    do_aoe_at(g, p->x, p->y, radius, dmg, fx.status, fx.color);
    int reflected = 0;
    for (int i = 0; i < MAX_PROJECTILES; i++) {
        Projectile *pr = &g->projectiles[i];
        if (!pr->alive || pr->owner != 1) continue;
        float dx = pr->x - p->x, dy = pr->y - p->y;
        if (dx * dx + dy * dy < radius * radius) {
            pr->vx = -pr->vx; pr->vy = -pr->vy;
            pr->owner = 0; pr->dmg *= 1.6f;
            pr->primary = fx.status ? fx.status : pr->primary;
            reflected++;
        }
    }
    for (int i = 0; i < 32; i++) {
        float a = (i / 32.f) * 6.2831f;
        particle_spawn_kind(g, p->x + cosf(a) * radius * 0.6f, p->y + sinf(a) * radius * 0.6f,
                            cosf(a) * 100, sinf(a) * 100, 0.40f, fx.color, 2.f, 0);
    }
    cue_swing(p, 2, 0, 0);
    sfx_play(g, SFX_HEAVY_HIT);
    g->shake_t = 0.18f; g->shake_mag = 3.5f;
    if (reflected > 0) g->hitstop_t = 0.04f;
    if (fx.spawn_fairy) fairy_spawn(g, p->x, p->y, fx.status);
}

static void fire_bow(Game *g, Weapon *w, ComboFx fx) {
    Player *p = &g->player;
    int nshots = 1 + fx.extra_proj;
    float ax = p->aim_x - p->x, ay = p->aim_y - p->y;
    float al = sqrtf(ax * ax + ay * ay) + 0.001f;
    ax /= al; ay /= al;
    float dmg = (w->base_dmg + p->flat_dmg) * fx.dmg_mul;
    float speed = 320.f * fx.range_mul;
    for (int s = 0; s < nshots; s++) {
        float spread = (nshots > 1) ? ((s - (nshots - 1) / 2.f) * 0.16f) : 0.f;
        float ca = cosf(spread), sa = sinf(spread);
        float vx = ax * ca - ay * sa;
        float vy = ax * sa + ay * ca;
        Projectile pr = {0};
        pr.x = p->x; pr.y = p->y;
        pr.vx = vx * speed; pr.vy = vy * speed;
        pr.life = 1.0f * fx.range_mul; pr.r = 3.f;
        pr.dmg = dmg; pr.owner = 0;
        pr.pierce = fx.pierces ? 3 : 0;
        pr.chains = fx.chain ? 2 : 0;
        pr.aoe = fx.aoe_explode ? 26.f : 0.f;
        pr.primary = fx.status; pr.homing = fx.homing ? 2.5f : 0.f;
        pr.target_idx = -1; pr.sprite = 1;
        projectile_spawn(g, pr);
    }
    cue_swing(p, 4, ax, ay);
    sfx_play(g, SFX_SHOOT);
    if (fx.spawn_fairy) fairy_spawn(g, p->x, p->y, fx.status);
}

static void fire_wand(Game *g, Weapon *w, ComboFx fx) {
    Player *p = &g->player;
    float ax = p->aim_x - p->x, ay = p->aim_y - p->y;
    float al = sqrtf(ax * ax + ay * ay) + 0.001f;
    ax /= al; ay /= al;
    float speed = 150.f;
    Projectile pr = {0};
    pr.x = p->x; pr.y = p->y;
    pr.vx = ax * speed; pr.vy = ay * speed;
    pr.life = (w->base_range / speed) * fx.range_mul;
    pr.r = 5.f;
    pr.dmg = (w->base_dmg + p->flat_dmg) * fx.dmg_mul;
    pr.owner = 0;
    pr.aoe = 40.f * fx.range_mul;
    pr.primary = fx.status;
    pr.homing = fx.homing ? 3.0f : 0.6f;
    pr.target_idx = -1;
    pr.sprite = 2;
    projectile_spawn(g, pr);
    cue_swing(p, 3, ax, ay);
    sfx_play(g, SFX_ZAP);
    if (fx.spawn_fairy) fairy_spawn(g, p->x, p->y, fx.status);
}

static void fire_axe(Game *g, Weapon *w, ComboFx fx) {
    Player *p = &g->player;
    float radius = w->base_range * fx.range_mul + 12.f;
    float dmg = (w->base_dmg + p->flat_dmg) * fx.dmg_mul;
    do_aoe_at(g, p->x, p->y, radius, dmg, fx.status, fx.color);
    if (fx.chain) {
        int best = nearest_enemy(g, p->x, p->y, radius, NULL);
        if (best >= 0) chain_hit(g, best, dmg * 0.5f, fx.status, 3, fx.color);
    }
    for (int i = 0; i < 36; i++) {
        float a = (i / 36.f) * 6.2831f;
        float s = radius * (0.4f + (rand() % 60) / 100.f);
        particle_spawn_kind(g, p->x + cosf(a) * s, p->y + sinf(a) * s,
                            cosf(a) * 80, sinf(a) * 80, 0.45f, fx.color, 3.f, 2);
    }
    cue_swing(p, 1, 0, 0);
    sfx_play(g, SFX_HEAVY_HIT);
    g->shake_t = 0.25f; g->shake_mag = 5.f;
    g->hitstop_t = 0.07f;
    if (fx.spawn_fairy) fairy_spawn(g, p->x, p->y, fx.status);
}

/* ---------- UPDATE_WEAPONS ---------- */
void update_weapons(Game *g) {
    Player *p = &g->player;
    float dt = g->dt;
    for (int i = 0; i < WEAPON_SLOTS; i++) {
        Weapon *w = &p->weapons[i];
        if (!w->owned) continue;
        w->cooldown -= dt;
        if (w->cooldown > 0.f) continue;
        int mask = weapon_combo_id(w);
        ComboFx fx = combo_compute(mask);
        /* codex : enregistre la decouverte du combo des qu il a un nom. */
        if (mask != 0 && !meta_combo_is_seen(&g->meta, mask)) {
            meta_combo_mark(&g->meta, mask);
            save_write(&g->meta);
        }

        /* stats joueur appliquees au ComboFx */
        bool is_melee = (w->kind == W_FISTS || w->kind == W_SWORD ||
                         w->kind == W_AXE   || w->kind == W_SHIELD);
        bool is_range = (w->kind == W_BOW || w->kind == W_WAND);
        float pmul = p->dmg_mul;
        if (is_melee) pmul *= p->melee_dmg_mul;
        if (is_range) pmul *= p->range_dmg_mul;
        if (w->element_count > 0) pmul *= p->elem_dmg_mul;
        if (fx.status > 0 && fx.status < EL_COUNT) {
            pmul *= (1.f + p->elem_affinity[fx.status]);
        }
        bool crit = (rand() / (float)RAND_MAX) < p->crit_chance;
        if (crit) pmul *= p->crit_dmg;
        /* OVERDRIVE actif : +30% dmg, atk_speed_mul plus court, crit
         * roll redouble (deja roule). */
        if (g->overdrive_t > 0.f) {
            pmul *= 1.30f;
            fx.cd_mul *= 0.67f;       /* atk speed +50% (cd ~0.67x) */
        }
        fx.dmg_mul *= pmul;
        fx.range_mul *= p->range_mul;
        g->current_attack_crit = crit;
        /* BUILD-DEF : "Couronne Spectrale" -> chaque crit reduit la hitbox.
         * Min 2.5 px. On garde r_base pour la restauration sur degats. */
        if (crit && p->u_crit_shrink) {
            if (p->r_base <= 0.f) p->r_base = p->r;
            if (p->r > 2.5f) p->r -= 0.20f;
            /* visuel : aura blanche eclair */
            for (int k = 0; k < 6; k++) {
                float a = (rand() % 360) * 0.01745f;
                particle_spawn_kind(g, p->x, p->y,
                                    cosf(a) * 40.f, sinf(a) * 40.f,
                                    0.25f, 0xFFFFFFE0, 1.8f, 0);
            }
        }

        /* Signature Combo callout (paires + triples) -- visuellement
         * spectaculaire : double anneau de particules + impulse de
         * shake + flash colore + chain particles vers l ennemi le plus
         * proche pour signaler la reaction en chaine. */
        if (w->element_count >= 2 && mask != g->combo_callout_mask && fx.tag) {
            bool is_triple = (w->element_count == 3);
            g->combo_callout_mask  = mask;
            g->combo_callout_t     = is_triple ? 2.5f : 1.2f;
            g->combo_callout_color = fx.color;
            snprintf(g->combo_callout, sizeof(g->combo_callout),
                     "%s%s", fx.tag, is_triple ? " !" : "");
            /* track best combo size pour l ecran de mort */
            if (w->element_count > g->run_best_combo_size)
                g->run_best_combo_size = w->element_count;
            /* anneau interieur rapide */
            int kn = is_triple ? 48 : 18;
            float spd = is_triple ? 160.f : 90.f;
            float life = is_triple ? 0.7f : 0.45f;
            for (int k = 0; k < kn; k++) {
                float a = (k / (float)kn) * 6.2831f;
                particle_spawn_kind(g, p->x, p->y,
                                    cosf(a) * spd, sinf(a) * spd,
                                    life, fx.color, 3.0f, 2);
            }
            /* anneau exterieur plus lent et plus gros (triples uniquement) */
            if (is_triple) {
                int ko = 36;
                for (int k = 0; k < ko; k++) {
                    float a = (k / (float)ko) * 6.2831f + 0.087f;
                    particle_spawn_kind(g, p->x, p->y,
                                        cosf(a) * 240.f, sinf(a) * 240.f,
                                        1.0f, fx.color, 4.0f, 2);
                }
                /* shake + hitstop pour la sensation cinematique */
                if (g->shake_t < 0.20f) {
                    g->shake_t = 0.20f;
                    g->shake_mag = 5.f;
                }
                g->hitstop_t = 0.10f;
            }
            /* chain visuel : faisceau de particules vers l ennemi le plus
             * proche pour rendre visible la "reaction en chaine". */
            int tgt = nearest_enemy(g, p->x, p->y, 200.f, NULL);
            if (tgt >= 0) {
                Enemy *t = &g->enemies[tgt];
                float dx = t->x - p->x, dy = t->y - p->y;
                float d  = sqrtf(dx * dx + dy * dy) + 0.01f;
                int steps = (int)(d / 4.f);
                for (int s = 0; s < steps; s++) {
                    float fx2 = p->x + dx * (s / (float)steps);
                    float fy2 = p->y + dy * (s / (float)steps);
                    particle_spawn_kind(g, fx2, fy2, 0, 0, 0.30f,
                                        fx.color, 2.0f, 0);
                }
            }
            sfx_play(g, is_triple ? SFX_EXPLODE : SFX_ZAP);
        }

        /* Gating : armes "actives" gatees sur la presence d'un ennemi en
         * portee (sauf bouclier qui est defensif). */
        bool fire = true;
        if (w->kind != W_SHIELD) {
            float scan;
            switch (w->kind) {
                case W_AXE:    scan = w->base_range * fx.range_mul + 18.f; break;
                case W_BOW:    scan = 320.f * fx.range_mul; break;
                case W_WAND:   scan = w->base_range * fx.range_mul + 30.f; break;
                default:       scan = w->base_range * fx.range_mul + 10.f; break;
            }
            int t = nearest_enemy(g, p->x, p->y, scan, NULL);
            if (t < 0) fire = false;
        }
        if (!fire) continue;

        /* Triple feedback loop : sync + overload modifiers. */
        if (w->element_count == 3) combo_refresh_active_loop(g, mask);
        combo_apply_loop_modifiers(g, &fx);

        switch (w->kind) {
            case W_FISTS:  fire_fists (g, w, fx); break;
            case W_SWORD:  fire_sword (g, w, fx); break;
            case W_SHIELD: fire_shield(g, w, fx); break;
            case W_BOW:    fire_bow   (g, w, fx); break;
            case W_WAND:   fire_wand  (g, w, fx); break;
            case W_AXE:    fire_axe   (g, w, fx); break;
            default: break;
        }
        g->current_attack_crit = false;
        w->cooldown = w->base_cd * fx.cd_mul * p->atk_speed_mul;
    }
}
