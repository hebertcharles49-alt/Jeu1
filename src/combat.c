/*
 * combat.c - weapons, elements, projectiles, fairies
 */
#include "game.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

const char *element_name(Element e) {
    switch (e) {
        case EL_NONE:      return "—";
        case EL_FIRE:      return "Feu";
        case EL_WATER:     return "Eau";
        case EL_EARTH:     return "Terre";
        case EL_LIGHTNING: return "Foudre";
        case EL_AIR:       return "Air";
        case EL_VOID:      return "Vide";
        case EL_FAE:       return "Fee";
        default:           return "?";
    }
}

uint32_t element_color(Element e) {
    switch (e) {
        case EL_FIRE:      return 0xFF6020FF;
        case EL_WATER:     return 0x4080FFFF;
        case EL_EARTH:     return 0x90603CFF;
        case EL_LIGHTNING: return 0xFFEC60FF;
        case EL_AIR:       return 0xC8E0F0FF;
        case EL_VOID:      return 0x8030B0FF;
        case EL_FAE:       return 0xF080F0FF;
        default:           return 0xCCCCCCFF;
    }
}

const char *weapon_name(WeaponKind w) {
    switch (w) {
        case W_SWORD:  return "Epee";
        case W_SHIELD: return "Bouclier";
        case W_BOW:    return "Arc";
        case W_WAND:   return "Baton";
        case W_AXE:    return "Hache";
        default:       return "?";
    }
}

void weapon_init_defaults(Weapon *w, WeaponKind kind) {
    memset(w, 0, sizeof(*w));
    w->kind = kind;
    w->cooldown = 0;
    w->element_count = 0;
    switch (kind) {
        case W_SWORD:
            w->base_cd = 0.45f; w->base_dmg = 14.f; w->base_range = 24.f; break;
        case W_SHIELD:
            w->base_cd = 2.0f;  w->base_dmg = 8.f;  w->base_range = 22.f; break;
        case W_BOW:
            w->base_cd = 0.7f;  w->base_dmg = 12.f; w->base_range = 220.f; break;
        case W_WAND:
            w->base_cd = 1.6f;  w->base_dmg = 22.f; w->base_range = 110.f; break;
        case W_AXE:
            w->base_cd = 1.2f;  w->base_dmg = 24.f; w->base_range = 30.f; break;
        default: break;
    }
}

void weapon_attach_element(Weapon *w, Element e) {
    if (w->element_count >= MAX_ELEMENTS_PER_WEAPON) {
        /* rotate: drop oldest */
        for (int i = 0; i < MAX_ELEMENTS_PER_WEAPON - 1; i++)
            w->elements[i] = w->elements[i + 1];
        w->elements[MAX_ELEMENTS_PER_WEAPON - 1] = e;
    } else {
        w->elements[w->element_count++] = e;
    }
}

/* canonical signature: bits of present elements */
int weapon_combo_id(const Weapon *w) {
    int mask = 0;
    for (int i = 0; i < w->element_count; i++) {
        Element e = w->elements[i];
        if (e > 0 && e < 32) mask |= (1 << e);
    }
    return mask;
}

static bool combo_has(int mask, Element a) {
    return (mask & (1 << a)) != 0;
}

static bool combo_only(int mask, Element a) {
    return mask == (1 << a);
}

void weapon_describe(const Weapon *w, char *buf, int bufsz) {
    int n = snprintf(buf, bufsz, "%s [", weapon_name(w->kind));
    for (int i = 0; i < w->element_count && n < bufsz - 8; i++) {
        if (i > 0) n += snprintf(buf + n, bufsz - n, "+");
        n += snprintf(buf + n, bufsz - n, "%s", element_name(w->elements[i]));
    }
    if (w->element_count == 0)
        n += snprintf(buf + n, bufsz - n, "neutre");
    snprintf(buf + n, bufsz - n, "]");
}

/* ---------- COMBO MULTIPLIERS / TAGS ---------- */
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
    int   extra_proj;
    Element status;     /* primary status to apply on hit */
    uint32_t color;     /* visual tint */
    const char *tag;    /* descriptor */
} ComboFx;

static ComboFx compute_combo(int mask) {
    ComboFx c = {0};
    c.dmg_mul = 1.f;
    c.cd_mul = 1.f;
    c.range_mul = 1.f;
    c.color = 0xFFFFFFFF;
    c.tag = "Brut";

    if (mask == 0) return c;

    /* triple combos (specials) ------------------- */
    int fwl = (1<<EL_FIRE)|(1<<EL_WATER)|(1<<EL_LIGHTNING);
    int fea = (1<<EL_FIRE)|(1<<EL_EARTH)|(1<<EL_AIR);
    int vfl = (1<<EL_VOID)|(1<<EL_FAE)|(1<<EL_LIGHTNING);
    int wel = (1<<EL_WATER)|(1<<EL_EARTH)|(1<<EL_LIGHTNING);
    int wae = (1<<EL_WATER)|(1<<EL_AIR)|(1<<EL_EARTH);
    int faf = (1<<EL_FIRE)|(1<<EL_AIR)|(1<<EL_FAE);
    int vwa = (1<<EL_VOID)|(1<<EL_WATER)|(1<<EL_AIR);
    int eav = (1<<EL_EARTH)|(1<<EL_AIR)|(1<<EL_VOID);

    if (mask == fwl) { c.tag="Tempete";      c.dmg_mul=2.0f; c.cd_mul=0.85f; c.aoe_explode=true; c.chain=true; c.status=EL_LIGHTNING; c.color=0x80F0FFFF; return c; }
    if (mask == fea) { c.tag="Volcan";       c.dmg_mul=2.4f; c.aoe_explode=true; c.status=EL_FIRE; c.color=0xFF8040FF; c.extra_proj=2; return c; }
    if (mask == vfl) { c.tag="Dechirure";    c.dmg_mul=2.6f; c.pierces=true; c.chain=true; c.lifesteal=true; c.status=EL_VOID; c.color=0xC080FFFF; return c; }
    if (mask == wel) { c.tag="Tsunami";      c.dmg_mul=2.0f; c.aoe_explode=true; c.status=EL_WATER; c.color=0x60A0FFFF; c.range_mul=1.3f; return c; }
    if (mask == wae) { c.tag="Marais";       c.dmg_mul=1.6f; c.aoe_explode=true; c.status=EL_WATER; c.cd_mul=0.7f; c.color=0x80A8A0FF; return c; }
    if (mask == faf) { c.tag="Phenix";       c.dmg_mul=2.2f; c.spawn_fairy=true; c.status=EL_FIRE; c.color=0xFFB0F0FF; c.homing=true; return c; }
    if (mask == vwa) { c.tag="Brume Mortelle"; c.dmg_mul=1.8f; c.pierces=true; c.aoe_explode=true; c.color=0x9090C0FF; return c; }
    if (mask == eav) { c.tag="Effondrement"; c.dmg_mul=2.5f; c.aoe_explode=true; c.range_mul=1.2f; c.color=0x806040FF; return c; }

    /* pair combos -------------------------------- */
    int fw = (1<<EL_FIRE)|(1<<EL_WATER);
    int fl = (1<<EL_FIRE)|(1<<EL_LIGHTNING);
    int wl = (1<<EL_WATER)|(1<<EL_LIGHTNING);
    int fe = (1<<EL_FIRE)|(1<<EL_EARTH);
    int we = (1<<EL_WATER)|(1<<EL_EARTH);
    int ea = (1<<EL_EARTH)|(1<<EL_AIR);
    int al = (1<<EL_AIR)|(1<<EL_LIGHTNING);
    int af = (1<<EL_AIR)|(1<<EL_FIRE);
    int aw = (1<<EL_AIR)|(1<<EL_WATER);
    int vf = (1<<EL_VOID)|(1<<EL_FIRE);
    int vw = (1<<EL_VOID)|(1<<EL_WATER);
    int vee= (1<<EL_VOID)|(1<<EL_EARTH);
    int vl = (1<<EL_VOID)|(1<<EL_LIGHTNING);
    int va = (1<<EL_VOID)|(1<<EL_AIR);
    int vfae=(1<<EL_VOID)|(1<<EL_FAE);
    int faef=(1<<EL_FAE)|(1<<EL_FIRE);
    int faew=(1<<EL_FAE)|(1<<EL_WATER);
    int faee=(1<<EL_FAE)|(1<<EL_EARTH);
    int faea=(1<<EL_FAE)|(1<<EL_AIR);
    int fael=(1<<EL_FAE)|(1<<EL_LIGHTNING);

    if (mask == fw) { c.tag="Vapeur";   c.aoe_explode=true; c.dmg_mul=1.4f; c.range_mul=1.2f; c.color=0xC0E0F0FF; return c; }
    if (mask == fl) { c.tag="Plasma";   c.dmg_mul=1.8f; c.chain=true; c.status=EL_LIGHTNING; c.color=0xFFA0F0FF; return c; }
    if (mask == wl) { c.tag="Choc";     c.dmg_mul=1.5f; c.chain=true; c.status=EL_LIGHTNING; c.color=0x80FFFFFF; return c; }
    if (mask == fe) { c.tag="Lave";     c.dmg_mul=1.6f; c.aoe_explode=true; c.status=EL_FIRE; c.color=0xFF6020FF; return c; }
    if (mask == we) { c.tag="Boue";     c.dmg_mul=1.2f; c.status=EL_WATER; c.aoe_explode=true; c.color=0x806040FF; return c; }
    if (mask == ea) { c.tag="Sable";    c.dmg_mul=1.4f; c.aoe_explode=true; c.color=0xD0B080FF; return c; }
    if (mask == al) { c.tag="Orage";    c.dmg_mul=1.6f; c.chain=true; c.cd_mul=0.7f; c.color=0xFFFF80FF; return c; }
    if (mask == af) { c.tag="Brasier";  c.dmg_mul=1.7f; c.status=EL_FIRE; c.range_mul=1.4f; c.color=0xFFA040FF; return c; }
    if (mask == aw) { c.tag="Brume";    c.cd_mul=0.6f; c.dmg_mul=0.9f; c.color=0xC0D8E8FF; return c; }
    if (mask == vf) { c.tag="Feu noir"; c.dmg_mul=1.8f; c.lifesteal=true; c.status=EL_FIRE; c.color=0x802040FF; return c; }
    if (mask == vw) { c.tag="Acide";    c.dmg_mul=1.5f; c.pierces=true; c.color=0x80B040FF; return c; }
    if (mask == vee){ c.tag="Tombeau"; c.dmg_mul=1.7f; c.aoe_explode=true; c.color=0x402030FF; return c; }
    if (mask == vl) { c.tag="Annihile"; c.dmg_mul=2.0f; c.pierces=true; c.color=0xC080FFFF; return c; }
    if (mask == va) { c.tag="Eclipse";  c.cd_mul=0.7f; c.dmg_mul=1.3f; c.color=0x6040A0FF; return c; }
    if (mask == vfae){c.tag="Esprit";   c.dmg_mul=1.6f; c.spawn_fairy=true; c.lifesteal=true; c.color=0xC080FFFF; return c; }
    if (mask == faef){c.tag="Feu fee";  c.dmg_mul=1.5f; c.homing=true; c.status=EL_FIRE; c.color=0xFFA0E0FF; return c; }
    if (mask == faew){c.tag="Source";   c.spawn_fairy=true; c.dmg_mul=1.2f; c.color=0xA0D0FFFF; return c; }
    if (mask == faee){c.tag="Verger";   c.dmg_mul=1.4f; c.spawn_fairy=true; c.color=0xA0FFA0FF; return c; }
    if (mask == faea){c.tag="Sylphe";   c.cd_mul=0.65f; c.homing=true; c.color=0xE0E0FFFF; return c; }
    if (mask == fael){c.tag="Etincelle";c.dmg_mul=1.4f; c.chain=true; c.spawn_fairy=true; c.color=0xFFFFA0FF; return c; }

    /* singles ------------------------------------ */
    if (combo_only(mask, EL_FIRE))      { c.tag="Brule";  c.dmg_mul=1.2f; c.status=EL_FIRE; c.color=0xFF8040FF; return c; }
    if (combo_only(mask, EL_WATER))     { c.tag="Glacial";c.dmg_mul=1.1f; c.status=EL_WATER; c.color=0x80B0FFFF; return c; }
    if (combo_only(mask, EL_EARTH))     { c.tag="Pierre"; c.dmg_mul=1.4f; c.color=0xA08060FF; return c; }
    if (combo_only(mask, EL_LIGHTNING)) { c.tag="Eclair"; c.dmg_mul=1.2f; c.chain=true; c.status=EL_LIGHTNING; c.color=0xFFEC60FF; return c; }
    if (combo_only(mask, EL_AIR))       { c.tag="Vent";   c.cd_mul=0.75f; c.range_mul=1.2f; c.color=0xC0E0FFFF; return c; }
    if (combo_only(mask, EL_VOID))      { c.tag="Vide";   c.pierces=true; c.lifesteal=true; c.color=0x8030B0FF; return c; }
    if (combo_only(mask, EL_FAE))       { c.tag="Feerie"; c.spawn_fairy=true; c.color=0xF080F0FF; return c; }

    /* generic mixes (any other 2-3 not listed): blend basic effects */
    if (combo_has(mask, EL_FIRE))      c.status = EL_FIRE,      c.dmg_mul *= 1.15f;
    if (combo_has(mask, EL_WATER))     c.status = EL_WATER,     c.dmg_mul *= 1.10f;
    if (combo_has(mask, EL_LIGHTNING)) c.chain = true,          c.dmg_mul *= 1.10f;
    if (combo_has(mask, EL_EARTH))     c.dmg_mul *= 1.20f;
    if (combo_has(mask, EL_AIR))       c.cd_mul *= 0.85f, c.range_mul *= 1.10f;
    if (combo_has(mask, EL_VOID))      c.pierces = true, c.lifesteal = true;
    if (combo_has(mask, EL_FAE))       c.spawn_fairy = true;
    c.tag = "Hybride";
    c.color = 0xC0C0FFFF;
    return c;
}

/* ---------- WEAPON FIRING ---------- */

/* find nearest enemy in range */
static int nearest_enemy(Game *g, float x, float y, float range, float *out_d) {
    int best = -1; float bestd = range * range;
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (!g->enemies[i].alive) continue;
        float dx = g->enemies[i].x - x;
        float dy = g->enemies[i].y - y;
        float d2 = dx * dx + dy * dy;
        if (d2 < bestd) { bestd = d2; best = i; }
    }
    if (out_d) *out_d = sqrtf(bestd);
    return best;
}

static void do_aoe_at(Game *g, float x, float y, float radius, float dmg, Element status, uint32_t color) {
    for (int i = 0; i < MAX_ENEMIES; i++) {
        Enemy *e = &g->enemies[i];
        if (!e->alive) continue;
        float dx = e->x - x, dy = e->y - y;
        if (dx * dx + dy * dy < radius * radius) {
            world_enemy_damage(g, i, dmg, status, dx * 2.f, dy * 2.f);
        }
    }
    /* particles */
    int n = (int)(radius * 0.5f); if (n > 30) n = 30;
    for (int i = 0; i < n; i++) {
        float a = (rand() % 360) * 0.01745f;
        float s = 50.f + rand() % 80;
        particle_spawn(g, x, y, cosf(a) * s, sinf(a) * s, 0.5f, color, 3.f);
    }
}

static void chain_hit(Game *g, int from_idx, float dmg, Element status, int hops, uint32_t color) {
    if (hops <= 0 || from_idx < 0) return;
    Enemy *src = &g->enemies[from_idx];
    /* find next nearest enemy not equal to src within 80px */
    int best = -1; float bestd = 80.f * 80.f;
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (i == from_idx) continue;
        Enemy *e = &g->enemies[i];
        if (!e->alive) continue;
        float dx = e->x - src->x, dy = e->y - src->y;
        float d2 = dx * dx + dy * dy;
        if (d2 < bestd) { bestd = d2; best = i; }
    }
    if (best < 0) return;
    /* visual line via particles */
    Enemy *t = &g->enemies[best];
    float dx = t->x - src->x, dy = t->y - src->y;
    float d = sqrtf(dx * dx + dy * dy) + 0.01f;
    int steps = (int)(d / 4.f);
    for (int s = 0; s < steps; s++) {
        float fx = src->x + dx * (s / (float)steps);
        float fy = src->y + dy * (s / (float)steps);
        particle_spawn(g, fx, fy, 0, 0, 0.2f, color, 2.f);
    }
    world_enemy_damage(g, best, dmg, status, dx * 0.3f, dy * 0.3f);
    chain_hit(g, best, dmg * 0.7f, status, hops - 1, color);
}

static void fire_sword(Game *g, Weapon *w, ComboFx fx) {
    Player *p = &g->player;
    /* swing arc in front of facing/aim */
    float ax = p->aim_x - p->x;
    float ay = p->aim_y - p->y;
    float al = sqrtf(ax * ax + ay * ay) + 0.001f;
    ax /= al; ay /= al;
    float reach = w->base_range * fx.range_mul;
    float dmg = w->base_dmg * fx.dmg_mul;
    float cx = p->x + ax * reach * 0.5f;
    float cy = p->y + ay * reach * 0.5f;
    /* hit any enemy within reach in the arc (~120 deg) */
    for (int i = 0; i < MAX_ENEMIES; i++) {
        Enemy *e = &g->enemies[i];
        if (!e->alive) continue;
        float dx = e->x - p->x, dy = e->y - p->y;
        float d = sqrtf(dx * dx + dy * dy);
        if (d > reach + e->r) continue;
        float dot = (dx * ax + dy * ay) / (d + 0.001f);
        if (dot < 0.4f) continue;
        world_enemy_damage(g, i, dmg, fx.status, ax * 80.f, ay * 80.f);
        if (fx.chain) chain_hit(g, i, dmg * 0.6f, fx.status, 2, fx.color);
        if (fx.aoe_explode) do_aoe_at(g, e->x, e->y, 28.f, dmg * 0.5f, fx.status, fx.color);
        if (fx.lifesteal) { p->hp += dmg * 0.05f; if (p->hp > p->maxhp) p->hp = p->maxhp; }
    }
    /* slash particles */
    for (int i = 0; i < 12; i++) {
        float t = i / 12.f;
        float a = atan2f(ay, ax) - 1.0f + t * 2.0f;
        float s = reach * (0.5f + (rand() % 50) / 100.f);
        particle_spawn(g, p->x + cosf(a) * s, p->y + sinf(a) * s,
                       cosf(a) * 30, sinf(a) * 30, 0.2f, fx.color, 2.f);
    }
    if (fx.spawn_fairy) fairy_spawn(g, p->x, p->y, fx.status);
    (void)cx; (void)cy;
}

static void fire_shield(Game *g, Weapon *w, ComboFx fx) {
    Player *p = &g->player;
    /* pulse: brief invuln + reflect projectiles + small radial damage */
    p->invuln_t = 0.4f;
    float radius = w->base_range * fx.range_mul + 6.f;
    float dmg = w->base_dmg * fx.dmg_mul;
    do_aoe_at(g, p->x, p->y, radius, dmg, fx.status, fx.color);
    /* reflect enemy projectiles */
    for (int i = 0; i < MAX_PROJECTILES; i++) {
        Projectile *pr = &g->projectiles[i];
        if (!pr->alive || pr->owner != 1) continue;
        float dx = pr->x - p->x, dy = pr->y - p->y;
        if (dx * dx + dy * dy < radius * radius) {
            pr->vx = -pr->vx; pr->vy = -pr->vy;
            pr->owner = 0;
            pr->dmg *= 1.5f;
            pr->primary = fx.status ? fx.status : pr->primary;
        }
    }
    /* ring particles */
    for (int i = 0; i < 24; i++) {
        float a = (i / 24.f) * 6.2831f;
        particle_spawn(g, p->x + cosf(a) * radius, p->y + sinf(a) * radius,
                       cosf(a) * 30, sinf(a) * 30, 0.4f, fx.color, 2.f);
    }
    if (fx.spawn_fairy) fairy_spawn(g, p->x, p->y, fx.status);
}

static void fire_bow(Game *g, Weapon *w, ComboFx fx) {
    Player *p = &g->player;
    int nshots = 1 + fx.extra_proj + (fx.tag && fx.tag[0]=='S' ? 0 : 0); /* extras come from combos */
    float ax = p->aim_x - p->x, ay = p->aim_y - p->y;
    float al = sqrtf(ax * ax + ay * ay) + 0.001f;
    ax /= al; ay /= al;
    float dmg = w->base_dmg * fx.dmg_mul;
    float speed = 280.f * fx.range_mul;
    for (int s = 0; s < nshots; s++) {
        float spread = (nshots > 1) ? ((s - (nshots - 1) / 2.f) * 0.18f) : 0.f;
        float ca = cosf(spread), sa = sinf(spread);
        float vx = ax * ca - ay * sa;
        float vy = ax * sa + ay * ca;
        Projectile pr = {0};
        pr.x = p->x; pr.y = p->y;
        pr.vx = vx * speed;
        pr.vy = vy * speed;
        pr.life = 1.0f * fx.range_mul;
        pr.r = 3.f;
        pr.dmg = dmg;
        pr.owner = 0;
        pr.pierce = fx.pierces ? 3 : 0;
        pr.chains = fx.chain ? 2 : 0;
        pr.aoe = fx.aoe_explode ? 24.f : 0.f;
        pr.primary = fx.status;
        pr.homing = fx.homing ? 2.5f : 0.f;
        pr.target_idx = -1;
        pr.sprite = 1;
        projectile_spawn(g, pr);
    }
    if (fx.spawn_fairy) fairy_spawn(g, p->x, p->y, fx.status);
}

static void fire_wand(Game *g, Weapon *w, ComboFx fx) {
    Player *p = &g->player;
    /* shoot a slow magic orb that explodes */
    float ax = p->aim_x - p->x, ay = p->aim_y - p->y;
    float al = sqrtf(ax * ax + ay * ay) + 0.001f;
    ax /= al; ay /= al;
    float speed = 140.f;
    Projectile pr = {0};
    pr.x = p->x; pr.y = p->y;
    pr.vx = ax * speed; pr.vy = ay * speed;
    pr.life = (w->base_range / speed) * fx.range_mul;
    pr.r = 5.f;
    pr.dmg = w->base_dmg * fx.dmg_mul;
    pr.owner = 0;
    pr.aoe = 36.f * fx.range_mul;
    pr.primary = fx.status;
    pr.homing = fx.homing ? 3.0f : 0.5f;
    pr.target_idx = -1;
    pr.sprite = 2;
    projectile_spawn(g, pr);
    if (fx.spawn_fairy) fairy_spawn(g, p->x, p->y, fx.status);
}

static void fire_axe(Game *g, Weapon *w, ComboFx fx) {
    Player *p = &g->player;
    /* big cleave around player */
    float radius = w->base_range * fx.range_mul + 12.f;
    float dmg = w->base_dmg * fx.dmg_mul;
    do_aoe_at(g, p->x, p->y, radius, dmg, fx.status, fx.color);
    if (fx.chain) {
        /* find first hit and chain */
        int best = nearest_enemy(g, p->x, p->y, radius, NULL);
        if (best >= 0) chain_hit(g, best, dmg * 0.5f, fx.status, 3, fx.color);
    }
    /* lots of particles */
    for (int i = 0; i < 30; i++) {
        float a = (i / 30.f) * 6.2831f;
        float s = radius * (0.4f + (rand() % 60) / 100.f);
        particle_spawn(g, p->x + cosf(a) * s, p->y + sinf(a) * s,
                       cosf(a) * 60, sinf(a) * 60, 0.4f, fx.color, 2.5f);
    }
    g->shake_t = 0.2f; g->shake_mag = 3.f;
    if (fx.spawn_fairy) fairy_spawn(g, p->x, p->y, fx.status);
}

void update_weapons(Game *g) {
    Player *p = &g->player;
    float dt = g->dt;
    for (int i = 0; i < WEAPON_SLOTS; i++) {
        Weapon *w = &p->weapons[i];
        if (!w->owned) continue;
        w->cooldown -= dt;
        if (w->cooldown > 0.f) continue;
        ComboFx fx = compute_combo(weapon_combo_id(w));
        /* gate weapons that need a target except shield/axe/wand */
        bool need_target = (w->kind == W_SWORD || w->kind == W_BOW);
        if (need_target) {
            float d;
            int t = nearest_enemy(g, p->x, p->y,
                w->kind == W_SWORD ? w->base_range * fx.range_mul + 8.f : 320.f * fx.range_mul, &d);
            if (t < 0) continue;
            /* aim toward this target (override mouse) */
            p->aim_x = g->enemies[t].x;
            p->aim_y = g->enemies[t].y;
        }
        switch (w->kind) {
            case W_SWORD:  fire_sword(g, w, fx);  break;
            case W_SHIELD: fire_shield(g, w, fx); break;
            case W_BOW:    fire_bow(g, w, fx);    break;
            case W_WAND:   fire_wand(g, w, fx);   break;
            case W_AXE:    fire_axe(g, w, fx);    break;
            default: break;
        }
        w->cooldown = w->base_cd * fx.cd_mul;
    }
}

/* ---------- PROJECTILES ---------- */
void update_projectiles(Game *g) {
    float dt = g->dt;
    Player *p = &g->player;

    for (int i = 0; i < MAX_PROJECTILES; i++) {
        Projectile *pr = &g->projectiles[i];
        if (!pr->alive) continue;

        pr->life -= dt;
        if (pr->life <= 0.f) {
            if (pr->owner == 0 && pr->aoe > 0.f) {
                ComboFx fx = {0};
                do_aoe_at(g, pr->x, pr->y, pr->aoe, pr->dmg * 0.7f, pr->primary, element_color(pr->primary));
                (void)fx;
            }
            pr->alive = false;
            continue;
        }

        /* homing for player projectiles */
        if (pr->owner == 0 && pr->homing > 0.f) {
            if (pr->target_idx < 0 || !g->enemies[pr->target_idx].alive) {
                pr->target_idx = nearest_enemy(g, pr->x, pr->y, 200.f, NULL);
            }
            if (pr->target_idx >= 0) {
                Enemy *t = &g->enemies[pr->target_idx];
                float dx = t->x - pr->x, dy = t->y - pr->y;
                float d = sqrtf(dx * dx + dy * dy) + 0.001f;
                float speed = sqrtf(pr->vx * pr->vx + pr->vy * pr->vy);
                pr->vx += (dx / d) * pr->homing * 60.f * dt;
                pr->vy += (dy / d) * pr->homing * 60.f * dt;
                /* normalize back */
                float ns = sqrtf(pr->vx * pr->vx + pr->vy * pr->vy) + 0.001f;
                pr->vx = pr->vx / ns * speed;
                pr->vy = pr->vy / ns * speed;
            }
        }

        pr->x += pr->vx * dt;
        pr->y += pr->vy * dt;

        /* tile collision */
        int tx = (int)(pr->x / TILE), ty = (int)(pr->y / TILE);
        if (tx < 0 || ty < 0 || tx >= MAP_W || ty >= MAP_H || tile_solid(g->dungeon.tiles[ty][tx])) {
            if (pr->owner == 0 && pr->aoe > 0.f) {
                do_aoe_at(g, pr->x, pr->y, pr->aoe, pr->dmg * 0.7f, pr->primary, element_color(pr->primary));
            }
            pr->alive = false;
            continue;
        }

        /* collision with entities */
        if (pr->owner == 0) {
            /* trail */
            if ((rand() % 100) < 40)
                particle_spawn(g, pr->x, pr->y, 0, 0, 0.2f, element_color(pr->primary), 2.f);
            for (int e = 0; e < MAX_ENEMIES; e++) {
                Enemy *en = &g->enemies[e];
                if (!en->alive) continue;
                float dx = en->x - pr->x, dy = en->y - pr->y;
                float rr = en->r + pr->r;
                if (dx * dx + dy * dy < rr * rr) {
                    world_enemy_damage(g, e, pr->dmg, pr->primary, pr->vx * 0.3f, pr->vy * 0.3f);
                    if (pr->aoe > 0.f) {
                        do_aoe_at(g, en->x, en->y, pr->aoe, pr->dmg * 0.6f, pr->primary, element_color(pr->primary));
                    }
                    if (pr->chains > 0) {
                        chain_hit(g, e, pr->dmg * 0.6f, pr->primary, pr->chains, element_color(pr->primary));
                    }
                    if (pr->pierce > 0) {
                        pr->pierce--;
                    } else {
                        pr->alive = false;
                    }
                    break;
                }
            }
        } else {
            float dx = p->x - pr->x, dy = p->y - pr->y;
            float rr = p->r + pr->r;
            if (dx * dx + dy * dy < rr * rr) {
                if (p->invuln_t <= 0.f && p->dash_t <= 0.f) {
                    p->hp -= pr->dmg;
                    p->invuln_t = 0.5f;
                    g->shake_t = 0.2f; g->shake_mag = 3.f;
                }
                pr->alive = false;
            }
        }
    }
}

/* ---------- FAIRIES ---------- */
void update_fairies(Game *g) {
    float dt = g->dt;
    Player *p = &g->player;
    for (int i = 0; i < MAX_FAIRIES; i++) {
        Fairy *f = &g->fairies[i];
        if (!f->alive) continue;
        f->life -= dt;
        f->cd -= dt;
        if (f->life <= 0.f) { f->alive = false; continue; }
        /* wander around player; pick target */
        if (f->target < 0 || !g->enemies[f->target].alive) {
            f->target = nearest_enemy(g, f->x, f->y, 160.f, NULL);
        }
        float tx, ty;
        if (f->target >= 0) {
            tx = g->enemies[f->target].x;
            ty = g->enemies[f->target].y;
        } else {
            tx = p->x + cosf(g->time * 2.f + i) * 28.f;
            ty = p->y + sinf(g->time * 2.f + i) * 28.f;
        }
        float dx = tx - f->x, dy = ty - f->y;
        float d = sqrtf(dx * dx + dy * dy) + 0.01f;
        f->vx += dx / d * 200.f * dt;
        f->vy += dy / d * 200.f * dt;
        f->vx *= 0.92f;
        f->vy *= 0.92f;
        f->x += f->vx * dt;
        f->y += f->vy * dt;
        if (f->target >= 0 && d < 14.f && f->cd <= 0.f) {
            f->cd = 0.5f;
            world_enemy_damage(g, f->target, 6.f, f->element, dx * 0.2f, dy * 0.2f);
            particle_spawn(g, f->x, f->y, 0, 0, 0.4f, element_color(f->element), 3.f);
        }
        /* trail */
        if ((rand() % 100) < 30)
            particle_spawn(g, f->x, f->y, 0, 0, 0.4f, element_color(f->element), 2.f);
    }
}
