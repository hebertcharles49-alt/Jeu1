/*
 * surfaces.c - "physique" du sol : flaques eau / huile / feu / glace /
 * eau electrifiee. Reagissent entre elles et appliquent des effets aux
 * entites (joueur + ennemis) qui marchent dessus.
 *
 * Pool dans Game.surfaces[MAX_SURFACES]. Lifecycle :
 *   - surface_spawn() : add un slot libre, capped au pool. Fusionne si
 *     une surface du meme kind existe a < 0.5*r (evite empilement).
 *   - update_surfaces() :
 *       1. decay life
 *       2. interactions surface<->surface (eau+feu = vapeur, etc.)
 *       3. effets sur joueur + ennemis (slow, dmg)
 *   - render_surfaces() : disque colore au sol, taille pulse selon
 *     l etat (eau electrifiee clignote vif).
 *
 * Conduction de la foudre : appelable via surface_electrify() depuis
 * projectiles.c quand un projectile EL_LIGHTNING traverse une SURF_WATER.
 */
#include "game.h"
#include "gfx.h"
#include <math.h>
#include <stdlib.h>

/* fwd decl interne : convertit toutes les SURF_WATER dans le rayon en
 * SURF_ELECTRIFIED. Static car expose via update_surfaces (foudre proj). */
static void surfaces_electrify_at(Game *g, float x, float y, float radius);

/* tableau de spec par kind : life par defaut, dmg par seconde, slow
 * multiplier sur la vitesse de l entite, ignition (= SURF_OIL prend
 * feu si surface adjacente FIRE/ELECTRIFIED). */
typedef struct {
    float life_def;
    float dmg_ps;
    float slow_mul;       /* 0..1 ; 1 = pas de slow */
    bool  flammable;
    bool  shocks;
} SurfaceSpec;

static const SurfaceSpec S_SPEC[SURF_COUNT] = {
    [SURF_NONE]        = { 0 },
    [SURF_WATER]       = { 4.5f, 0.0f, 0.65f, false, false },
    [SURF_OIL]         = { 7.0f, 0.0f, 0.55f, true,  false },
    [SURF_FIRE]        = { 3.0f, 8.0f, 1.00f, false, false },
    [SURF_ICE]         = { 5.0f, 0.0f, 0.50f, false, false },
    [SURF_ELECTRIFIED] = { 2.0f, 14.f, 0.80f, false, true  },
    /* derives */
    [SURF_STEAM]       = { 1.5f, 0.0f, 0.85f, false, false },
    [SURF_MUD]         = { 6.0f, 0.0f, 0.35f, false, false },
    [SURF_BLOOD]       = { 8.0f, -1.f, 0.80f, false, false },  /* dmg<0 = regen */
    /* tier 2 elemental */
    [SURF_HOLY]        = { 6.0f, -2.f, 1.00f, false, false },  /* regen rapide */
    [SURF_SHADOW]      = { 5.0f, 4.0f, 0.85f, false, false },
    [SURF_TAR]         = { 9.0f, 0.0f, 0.25f, true,  false },  /* extreme slow */
};

int surface_spawn(Game *g, SurfaceKind kind, float x, float y,
                  float r, float life)
{
    if (!g || kind <= SURF_NONE || kind >= SURF_COUNT) return -1;
    if (life <= 0.f) life = S_SPEC[kind].life_def;
    /* fusion : si on a deja une surface du meme kind a moins de 0.4*r,
     * on prolonge sa life et grossit son rayon plutot que d empiler. */
    for (int i = 0; i < MAX_SURFACES; i++) {
        Surface *s = &g->surfaces[i];
        if (!s->alive || s->kind != kind) continue;
        float dx = s->x - x, dy = s->y - y;
        if (dx * dx + dy * dy < (r * 0.4f) * (r * 0.4f)) {
            if (life > s->life) s->life = life;
            if (life > s->life_max) s->life_max = life;
            if (r > s->r) s->r = (s->r + r) * 0.5f;
            return i;
        }
    }
    /* slot libre ou recycle le plus vieux */
    int slot = -1, oldest = 0;
    float oldest_life = 1e9f;
    for (int i = 0; i < MAX_SURFACES; i++) {
        if (!g->surfaces[i].alive) { slot = i; break; }
        if (g->surfaces[i].life < oldest_life) {
            oldest_life = g->surfaces[i].life; oldest = i;
        }
    }
    if (slot < 0) slot = oldest;
    Surface *s = &g->surfaces[slot];
    s->alive    = true;
    s->kind     = kind;
    s->x        = x; s->y = y;
    s->r        = r;
    s->life     = life;
    s->life_max = life;
    return slot;
}

/* fait passer toutes les SURF_WATER dans le rayon a SURF_ELECTRIFIED. */
static void surfaces_electrify_at(Game *g, float x, float y, float radius) {
    float r2 = radius * radius;
    for (int i = 0; i < MAX_SURFACES; i++) {
        Surface *s = &g->surfaces[i];
        if (!s->alive || s->kind != SURF_WATER) continue;
        float dx = s->x - x, dy = s->y - y;
        if (dx * dx + dy * dy < r2 + s->r * s->r) {
            s->kind = SURF_ELECTRIFIED;
            s->life = S_SPEC[SURF_ELECTRIFIED].life_def;
            s->life_max = s->life;
            /* sparks */
            for (int k = 0; k < 14; k++) {
                float a = (rand() % 360) * 0.01745f;
                particle_spawn_kind(g, s->x, s->y,
                                    cosf(a) * 60.f, sinf(a) * 60.f,
                                    0.45f, 0xFFFF80FF, 2.0f, 0);
            }
            sfx_play(g, SFX_ZAP);
        }
    }
}

/* --- interactions surface<->surface --- */
static void surface_interact_pair(Game *g, Surface *a, Surface *b) {
    if (!a->alive || !b->alive) return;
    float dx = a->x - b->x, dy = a->y - b->y;
    float touch = (a->r + b->r) * 0.85f;
    if (dx * dx + dy * dy > touch * touch) return;

    /* WATER + FIRE : phase transition -> SURF_STEAM (les 2 meurent, le
     * nuage condensera en eau a sa mort). Bouillonement particules. */
    if ((a->kind == SURF_WATER && b->kind == SURF_FIRE) ||
        (a->kind == SURF_FIRE  && b->kind == SURF_WATER)) {
        float cx = (a->x + b->x) * 0.5f;
        float cy = (a->y + b->y) * 0.5f;
        float cr = (a->r + b->r) * 0.6f;
        for (int k = 0; k < 18; k++) {
            float ang = (rand() % 360) * 0.01745f;
            particle_spawn_kind(g, cx, cy,
                                cosf(ang) * 70.f, sinf(ang) * 70.f - 20.f,
                                0.80f, 0xC0E0E0C0, 2.8f, 0);
        }
        sfx_play(g, SFX_EXPLODE);
        a->alive = false; b->alive = false;
        surface_spawn(g, SURF_STEAM, cx, cy, cr, 0.f);
        return;
    }
    /* STEAM + n importe quelle source de froid (ICE, WATER) : la vapeur
     * accelere sa condensation -> sa life chute a 0.3s pour declencher
     * le respawn d eau plus vite. */
    if ((a->kind == SURF_STEAM && (b->kind == SURF_ICE || b->kind == SURF_WATER)) ||
        (b->kind == SURF_STEAM && (a->kind == SURF_ICE || a->kind == SURF_WATER))) {
        Surface *steam = (a->kind == SURF_STEAM) ? a : b;
        if (steam->life > 0.3f) steam->life = 0.3f;
        return;
    }
    /* OIL + FIRE : ignition, l huile devient FIRE (radius gonfle).
     * Petite explosion pour signaler. */
    if ((a->kind == SURF_OIL && b->kind == SURF_FIRE) ||
        (a->kind == SURF_FIRE && b->kind == SURF_OIL)) {
        Surface *oil  = (a->kind == SURF_OIL) ? a : b;
        Surface *fire = (a->kind == SURF_OIL) ? b : a;
        oil->kind = SURF_FIRE;
        oil->r *= 1.4f;
        oil->life = S_SPEC[SURF_FIRE].life_def;
        oil->life_max = oil->life;
        (void)fire;
        for (int k = 0; k < 22; k++) {
            float ang = (rand() % 360) * 0.01745f;
            particle_spawn_kind(g, oil->x, oil->y,
                                cosf(ang) * 110.f, sinf(ang) * 110.f - 30.f,
                                0.70f, 0xFF6020FF, 3.0f, 2);
        }
        sfx_play(g, SFX_EXPLODE);
        return;
    }
    /* ICE + FIRE : la glace fond */
    if ((a->kind == SURF_ICE && b->kind == SURF_FIRE) ||
        (a->kind == SURF_FIRE && b->kind == SURF_ICE)) {
        Surface *ice = (a->kind == SURF_ICE) ? a : b;
        ice->alive = false;
        return;
    }
    /* HOLY + SHADOW : annulation cosmique (purification mutuelle, gros
     * burst dore). Le pendant surface des tags DIVINE + CURSED. */
    if ((a->kind == SURF_HOLY && b->kind == SURF_SHADOW) ||
        (a->kind == SURF_SHADOW && b->kind == SURF_HOLY)) {
        float cx = (a->x + b->x) * 0.5f;
        float cy = (a->y + b->y) * 0.5f;
        for (int k = 0; k < 24; k++) {
            float ang = (rand() % 360) * 0.01745f;
            particle_spawn_kind(g, cx, cy,
                                cosf(ang) * 100.f, sinf(ang) * 100.f,
                                0.55f, 0xFFE890FF, 2.5f, 2);
        }
        sfx_play(g, SFX_EXPLODE);
        a->alive = false; b->alive = false;
        return;
    }
    /* HOLY + BLOOD : le sang est purifie -> le pool de regen passe en
     * HOLY (regen plus rapide), HOLY consomme. */
    if ((a->kind == SURF_HOLY && b->kind == SURF_BLOOD) ||
        (a->kind == SURF_BLOOD && b->kind == SURF_HOLY)) {
        Surface *blood = (a->kind == SURF_BLOOD) ? a : b;
        Surface *holy  = (a->kind == SURF_HOLY)  ? a : b;
        blood->kind = SURF_HOLY;
        blood->life = S_SPEC[SURF_HOLY].life_def;
        blood->life_max = blood->life;
        holy->alive = false;
        return;
    }
    /* TAR + FIRE : ignition violente (boule de feu r*1.8). */
    if ((a->kind == SURF_TAR && b->kind == SURF_FIRE) ||
        (a->kind == SURF_FIRE && b->kind == SURF_TAR)) {
        Surface *tar = (a->kind == SURF_TAR) ? a : b;
        tar->kind = SURF_FIRE;
        tar->r *= 1.8f;
        tar->life = S_SPEC[SURF_FIRE].life_def * 1.5f;
        tar->life_max = tar->life;
        for (int k = 0; k < 30; k++) {
            float ang = (rand() % 360) * 0.01745f;
            particle_spawn_kind(g, tar->x, tar->y,
                                cosf(ang) * 140.f, sinf(ang) * 140.f - 30.f,
                                0.85f, 0xFF4020FF, 3.2f, 2);
        }
        sfx_play(g, SFX_EXPLODE);
        return;
    }
}

static void surface_tick_interactions(Game *g) {
    for (int i = 0; i < MAX_SURFACES; i++) {
        if (!g->surfaces[i].alive) continue;
        for (int j = i + 1; j < MAX_SURFACES; j++) {
            if (!g->surfaces[j].alive) continue;
            surface_interact_pair(g, &g->surfaces[i], &g->surfaces[j]);
        }
    }
}

/* applique les effets a une entite (joueur ou ennemi). Renvoie le slow
 * cumulatif (1.0 = aucun slow, 0.4 = lourd). */
static float surface_apply_to_pos(Game *g, float x, float y, float r,
                                   bool is_player, int enemy_idx,
                                   float dt)
{
    float slow = 1.f;
    for (int i = 0; i < MAX_SURFACES; i++) {
        Surface *s = &g->surfaces[i];
        if (!s->alive) continue;
        float dx = s->x - x, dy = s->y - y;
        float rr = s->r + r;
        if (dx * dx + dy * dy >= rr * rr) continue;
        const SurfaceSpec *sp = &S_SPEC[s->kind];
        if (sp->slow_mul < slow) slow = sp->slow_mul;
        if (sp->dmg_ps < 0.f) {
            /* dmg negatif = regen au contact (BLOOD). On bypass invuln_t
             * pour que le sang serve quand le joueur saigne. */
            if (is_player && g->player.hp < g->player.maxhp) {
                static float acc_heal = 0.f;
                acc_heal += -sp->dmg_ps * dt;
                if (acc_heal >= 1.f) {
                    g->player.hp += acc_heal;
                    if (g->player.hp > g->player.maxhp)
                        g->player.hp = g->player.maxhp;
                    acc_heal = 0.f;
                }
            }
        } else if (sp->dmg_ps > 0.f) {
            float dmg = sp->dmg_ps * dt;
            if (is_player) {
                /* u_hazard_immune : le joueur ne prend pas de dmg de
                 * cette surface. u_hazard_stacks : chaque tile traversee
                 * ajoute 1 stack (cap 10 = +50% dmg). On declenche
                 * l increment au passage (frame-based). */
                if (g->player.u_hazard_immune) {
                    if (g->player.u_hazard_stacks &&
                        g->player.hazard_stacks < 10 &&
                        (rand() % 100) < 1) {
                        g->player.hazard_stacks++;
                    }
                } else if (g->player.invuln_t <= 0.f && g->player.dash_t <= 0.f) {
                    static float acc_p = 0.f;
                    acc_p += dmg;
                    if (acc_p >= 1.f) {
                        player_take_damage(g, acc_p);
                        acc_p = 0.f;
                    }
                }
            } else if (enemy_idx >= 0) {
                /* mini dmg pour les ennemis (ne passe pas par dmgnum
                 * pour pas spammer). On tape directement le hp. */
                Enemy *e = &g->enemies[enemy_idx];
                if (e->alive && e->dying_t <= 0.f) {
                    e->hp -= dmg;
                    if (e->hp <= 0.f) {
                        /* mort par tick : on declenche enemy_take_damage
                         * avec 0 dmg pour le clean up. */
                        e->hp = 0.001f;
                        world_enemy_damage(g, enemy_idx, 0.001f,
                                           (s->kind == SURF_FIRE) ? EL_FIRE :
                                           (s->kind == SURF_ELECTRIFIED) ? EL_LIGHTNING :
                                           EL_NONE, 0, 0);
                    }
                }
            }
        }
        if (sp->shocks) {
            if (is_player) {
                if ((rand() % 100) < 8) g->player.invuln_t = 0.f; /* no-op */
            } else if (enemy_idx >= 0) {
                Enemy *e = &g->enemies[enemy_idx];
                if (e->stun_t < 0.10f) e->stun_t = 0.10f;
            }
        }
    }
    return slow;
}

void update_surfaces(Game *g) {
    float dt = g->dt;
    /* 1. life decay + condensation : la vapeur qui meurt a 40% de
     * chance de laisser une petite flaque d eau (cycle eau<->vapeur). */
    for (int i = 0; i < MAX_SURFACES; i++) {
        Surface *s = &g->surfaces[i];
        if (!s->alive) continue;
        s->life -= dt;
        if (s->life <= 0.f) {
            if (s->kind == SURF_STEAM && (rand() % 100) < 40) {
                /* on capture les coords AVANT d effacer */
                float wx = s->x, wy = s->y, wr = s->r * 0.6f;
                s->alive = false;
                surface_spawn(g, SURF_WATER, wx, wy, wr, 3.0f);
            } else {
                s->alive = false;
            }
        }
    }
    /* 2. interactions entre surfaces */
    surface_tick_interactions(g);
    /* 3. effets sur joueur + ennemis. Particles de pied. */
    surface_apply_to_pos(g, g->player.x, g->player.y, g->player.r, true,
                         -1, dt);
    for (int i = 0; i < MAX_ENEMIES; i++) {
        Enemy *e = &g->enemies[i];
        if (!e->alive || e->dying_t > 0.f) continue;
        float slow = surface_apply_to_pos(g, e->x, e->y, e->r, false, i, dt);
        if (slow < 1.f) {
            /* impose un slow_t court pour l effet visuel (le mouvement
             * est ralenti dans ai_move_toward qui regarde slow_t). */
            if (e->slow_t < 0.2f) e->slow_t = 0.2f;
        }
    }
    /* 4. quelques particules d ambiance sur chaque surface vivante */
    for (int i = 0; i < MAX_SURFACES; i++) {
        Surface *s = &g->surfaces[i];
        if (!s->alive) continue;
        if ((rand() % 100) >= 18) continue;
        float a = (rand() % 360) * 0.01745f;
        float ox = s->x + cosf(a) * s->r * 0.6f;
        float oy = s->y + sinf(a) * s->r * 0.6f;
        switch (s->kind) {
            case SURF_WATER:
                particle_spawn_kind(g, ox, oy, 0, -8.f, 0.40f,
                                    0x80B0FFA0, 1.5f, 0); break;
            case SURF_OIL:
                particle_spawn_kind(g, ox, oy, 0, -3.f, 0.50f,
                                    0x402030A0, 1.4f, 0); break;
            case SURF_FIRE:
                particle_spawn_kind(g, ox, oy, 0, -28.f, 0.40f,
                                    0xFF8030FF, 2.0f, 0); break;
            case SURF_ICE:
                particle_spawn_kind(g, ox, oy, 0, -4.f, 0.40f,
                                    0xC0E0FFA0, 1.3f, 0); break;
            case SURF_ELECTRIFIED:
                particle_spawn_kind(g, ox, oy, 0, -40.f, 0.20f,
                                    0xFFFF80FF, 2.2f, 0); break;
            case SURF_STEAM:
                particle_spawn_kind(g, ox, oy, 0, -22.f, 0.55f,
                                    0xE0E0E0A0, 2.4f, 0); break;
            case SURF_MUD:
                particle_spawn_kind(g, ox, oy, 0, 1.f, 0.40f,
                                    0x60402080, 1.6f, 0); break;
            case SURF_BLOOD:
                particle_spawn_kind(g, ox, oy, 0, -2.f, 0.45f,
                                    0xA02020A0, 1.5f, 0); break;
            case SURF_HOLY:
                particle_spawn_kind(g, ox, oy, 0, -18.f, 0.60f,
                                    0xFFE890C0, 2.0f, 0); break;
            case SURF_SHADOW:
                particle_spawn_kind(g, ox, oy, 0, -6.f, 0.55f,
                                    0x402060A0, 1.8f, 0); break;
            case SURF_TAR:
                particle_spawn_kind(g, ox, oy, 0, 2.f, 0.55f,
                                    0x201020FF, 1.7f, 0); break;
            default: break;
        }
    }
}

void render_surfaces(Game *g) {
    GfxCtx *gc = g->renderer;
    for (int i = 0; i < MAX_SURFACES; i++) {
        Surface *s = &g->surfaces[i];
        if (!s->alive) continue;
        float frac = s->life / s->life_max;
        if (frac > 1.f) frac = 1.f;
        float r = 0.5f, gr = 0.5f, b = 0.5f;
        switch (s->kind) {
            case SURF_WATER:       r=0.30f; gr=0.55f; b=0.95f; break;
            case SURF_OIL:         r=0.18f; gr=0.10f; b=0.16f; break;
            case SURF_FIRE:        r=1.00f; gr=0.45f; b=0.10f; break;
            case SURF_ICE:         r=0.65f; gr=0.85f; b=0.95f; break;
            case SURF_ELECTRIFIED: {
                float p = 0.5f + 0.5f * sinf(g->time * 18.f);
                r = 1.00f * p; gr = 0.95f * p; b = 0.35f * p;
                break;
            }
            case SURF_STEAM:       r=0.85f; gr=0.85f; b=0.85f; break;
            case SURF_MUD:         r=0.40f; gr=0.28f; b=0.18f; break;
            case SURF_BLOOD:       r=0.55f; gr=0.10f; b=0.12f; break;
            case SURF_HOLY: {
                float p = 0.7f + 0.3f * sinf(g->time * 2.f);
                r = 1.00f * p; gr = 0.92f * p; b = 0.55f * p;
                break;
            }
            case SURF_SHADOW:      r=0.15f; gr=0.10f; b=0.20f; break;
            case SURF_TAR:         r=0.08f; gr=0.06f; b=0.10f; break;
            default: break;
        }
        /* fade out a fin de vie */
        r *= 0.4f + 0.6f * frac;
        gr *= 0.4f + 0.6f * frac;
        b *= 0.4f + 0.6f * frac;
        /* disque plat tres bas au sol (y=0.04 pour eviter z-fight) */
        gfx_box_draw(gc,
            v3_make(s->x / (float)TILE, 0.04f, s->y / (float)TILE),
            v3_make(s->r / (float)TILE * 2.f, 0.02f, s->r / (float)TILE * 2.f),
            r, gr, b);
    }
}

/* expose pour projectiles.c : conduction de la foudre quand un
 * projectile EL_LIGHTNING traverse une SURF_WATER. */
void surface_lightning_hit(Game *g, float x, float y, float radius) {
    surfaces_electrify_at(g, x, y, radius);
}

/* EARTH proj sur eau -> boue. Pas de cooldown : si proj earth passe,
 * l eau qu il touche est immediatement transformee. */
void surface_earth_hit(Game *g, float x, float y, float radius) {
    float r2 = radius * radius;
    for (int i = 0; i < MAX_SURFACES; i++) {
        Surface *s = &g->surfaces[i];
        if (!s->alive || s->kind != SURF_WATER) continue;
        float dx = s->x - x, dy = s->y - y;
        if (dx * dx + dy * dy >= r2 + s->r * s->r) continue;
        s->kind = SURF_MUD;
        s->life = S_SPEC[SURF_MUD].life_def;
        s->life_max = s->life;
        for (int k = 0; k < 10; k++) {
            float a = (rand() % 360) * 0.01745f;
            particle_spawn_kind(g, s->x, s->y,
                                cosf(a) * 18.f, sinf(a) * 18.f,
                                0.45f, 0x806040FF, 1.8f, 0);
        }
    }
}

/* mort d ennemi -> "vie + eau = sang". Si une SURF_WATER existe a
 * moins de 25 px, elle est convertie en SURF_BLOOD. Sinon 20% de
 * chance d une petite flaque de sang frais. */
void surface_blood_drop(Game *g, float x, float y) {
    for (int i = 0; i < MAX_SURFACES; i++) {
        Surface *s = &g->surfaces[i];
        if (!s->alive || s->kind != SURF_WATER) continue;
        float dx = s->x - x, dy = s->y - y;
        if (dx * dx + dy * dy < 25.f * 25.f) {
            s->kind = SURF_BLOOD;
            s->life = S_SPEC[SURF_BLOOD].life_def;
            s->life_max = s->life;
            for (int k = 0; k < 8; k++) {
                float a = (rand() % 360) * 0.01745f;
                particle_spawn_kind(g, s->x, s->y,
                                    cosf(a) * 20.f, sinf(a) * 20.f,
                                    0.40f, 0x802020FF, 1.7f, 0);
            }
            return;     /* on convertit une seule flaque */
        }
    }
    if ((rand() % 100) < 20) {
        surface_spawn(g, SURF_BLOOD, x, y, 10.f, 0.f);
    }
}

/* VOID / DARK proj sur huile -> goudron (extreme slow). */
void surface_void_hit(Game *g, float x, float y, float radius) {
    float r2 = radius * radius;
    for (int i = 0; i < MAX_SURFACES; i++) {
        Surface *s = &g->surfaces[i];
        if (!s->alive || s->kind != SURF_OIL) continue;
        float dx = s->x - x, dy = s->y - y;
        if (dx * dx + dy * dy >= r2 + s->r * s->r) continue;
        s->kind = SURF_TAR;
        s->life = S_SPEC[SURF_TAR].life_def;
        s->life_max = s->life;
        for (int k = 0; k < 10; k++) {
            float a = (rand() % 360) * 0.01745f;
            particle_spawn_kind(g, s->x, s->y,
                                cosf(a) * 20.f, sinf(a) * 20.f,
                                0.55f, 0x301020FF, 1.8f, 0);
        }
    }
}

/* === BIOME SEEDING ===
 * Pose des surfaces ambient dans chaque salle (sauf spawn) selon le
 * biome courant. Cree un environnement coherent des l entree, qui
 * interagit avec les attaques du joueur sans qu il ait a tout
 * generer lui-meme. */
void surfaces_seed_biome(Game *g) {
    int bi = biome_for_floor(g->floor_index);
    for (int i = 1; i < g->dungeon.room_count; i++) {     /* skip spawn */
        const Room *r = &g->dungeon.rooms[i];
        if (r->is_debug_room) continue;
        /* count = 1..3 selon le biome ; les rooms tres petites en ont moins */
        int budget = 1 + rand() % 3;
        if (r->w * r->h < 50) budget = 1;
        if (r->is_boss_room) budget = 4;     /* la salle du boss est marquee */
        for (int k = 0; k < budget; k++) {
            int tx = r->x + 1 + rand() % (r->w - 2);
            int ty = r->y + 1 + rand() % (r->h - 2);
            float wx = tx * (float)TILE + (float)TILE * 0.5f;
            float wy = ty * (float)TILE + (float)TILE * 0.5f;
            SurfaceKind kind = SURF_NONE;
            float radius = 14.f;
            switch (bi) {
                case 0: /* Crypte (DARK)     */ kind = SURF_BLOOD;  break;
                case 1: /* Cavernes (EARTH)  */ kind = SURF_MUD;    break;
                case 2: /* Marais (WATER)    */ kind = SURF_WATER;  radius = 18.f; break;
                case 3: /* Forge (FIRE)      */ kind = SURF_OIL;    radius = 16.f; break;
                case 4: /* Sanctuaire (HOLY) */ kind = SURF_HOLY;   break;
                default: break;
            }
            if (kind != SURF_NONE) {
                /* vie longue car ce sont des features de la salle, pas
                 * des consequences d attaques. */
                surface_spawn(g, kind, wx, wy, radius, 60.f);
            }
        }
    }
}
