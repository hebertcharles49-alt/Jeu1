/*
 * combat.c - armes, elements, projectiles, fees, combos
 */
#include "game.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* table d'efficacite element atk vs element def (Pokemon-like)
 * 1.0 = neutre, 2.0 = super-efficace, 0.5 = resiste
 *
 * IMPORTANT : cette table doit avoir EXACTEMENT EL_COUNT lignes et colonnes,
 * et les lignes doivent etre dans l'ordre de l'enum Element. Si tu reorganises
 * Element, tu dois mettre a jour ce tableau dans le meme ordre.
 * L'assert ci-dessous attrape une desyncronisation enum vs table. */
float elem_effectiveness(Element atk, Element def) {
    if (def == EL_NONE || atk == EL_NONE) return 1.0f;
    static const float T[EL_COUNT][EL_COUNT] = {
        /*atk \\ def    NONE FIRE WATER EARTH LGT   AIR  VOID FAE  STEEL DARK HOLY */
        /*NONE */ { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f },
        /*FIRE */ { 1.0f, 0.5f, 0.5f, 1.0f, 1.0f, 1.0f, 1.0f, 2.0f, 2.0f, 1.5f, 0.5f },
        /*WATER*/ { 1.0f, 2.0f, 0.5f, 0.5f, 1.0f, 1.0f, 1.0f, 1.0f, 1.5f, 1.0f, 1.0f },
        /*EARTH*/ { 1.0f, 1.0f, 2.0f, 0.5f, 2.0f, 0.5f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f },
        /*LGT  */ { 1.0f, 1.0f, 2.0f, 0.5f, 0.5f, 2.0f, 1.0f, 1.0f, 2.0f, 1.0f, 1.0f },
        /*AIR  */ { 1.0f, 1.0f, 1.0f, 2.0f, 0.5f, 0.5f, 1.0f, 1.0f, 0.5f, 1.0f, 1.0f },
        /*VOID */ { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.5f, 2.0f, 1.0f, 0.5f, 0.5f },
        /*FAE  */ { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 2.0f, 0.5f, 0.5f, 0.5f, 1.5f },
        /*STEEL*/ { 1.0f, 0.5f, 1.0f, 2.0f, 0.5f, 1.5f, 1.0f, 1.5f, 0.5f, 1.0f, 1.0f },
        /*DARK */ { 1.0f, 0.5f, 1.0f, 1.0f, 1.0f, 1.0f, 1.5f, 1.5f, 1.0f, 0.5f, 2.0f },
        /*HOLY */ { 1.0f, 1.5f, 1.0f, 1.0f, 1.0f, 1.0f, 1.5f, 0.5f, 1.0f, 2.0f, 0.5f },
    };
    /* compile-time : explose si EL_COUNT bouge sans qu'on touche la table */
    _Static_assert(sizeof(T) / sizeof(T[0]) == EL_COUNT,
        "elem_effectiveness: table T desynchronisee avec EL_COUNT");
    _Static_assert(sizeof(T[0]) / sizeof(float) == EL_COUNT,
        "elem_effectiveness: largeur de T desynchronisee avec EL_COUNT");
    return T[atk][def];
}

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
        case EL_STEEL:     return "Acier";
        case EL_DARK:      return "Tenebres";
        case EL_HOLY:      return "Sacre";
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
        case EL_STEEL:     return 0xC0C8D0FF;
        case EL_DARK:      return 0x202028FF;
        case EL_HOLY:      return 0xFFE890FF;
        default:           return 0xCCCCCCFF;
    }
}

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

const char *hero_name(HeroClass h) {
    switch (h) {
        case HERO_GUERRIER:  return "Guerrier";
        case HERO_VOLEUR:    return "Voleur";
        case HERO_MAGE:      return "Mage";
        case HERO_BERSERKER: return "Berserker";
        case HERO_PALADIN:   return "Paladin";
        case HERO_DRUIDE:    return "Druide";
        case HERO_ASSASSIN:  return "Assassin";
        case HERO_RANGER:    return "Ranger";
        case HERO_TEMPLIER:  return "Templier";
        case HERO_NECROMANT: return "Necromant";
        default:             return "?";
    }
}

const char *hero_desc(HeroClass h) {
    switch (h) {
        case HERO_GUERRIER:  return "+25 PV   +15% degats melee";
        case HERO_VOLEUR:    return "+20 vitesse   dash plus long";
        case HERO_MAGE:      return "+30% degats elementaires   PV bas";
        case HERO_BERSERKER: return "+8% vol de vie   +20% degats   fragile";
        case HERO_PALADIN:   return "+2 armure   regen 1 PV/s";
        case HERO_DRUIDE:    return "+50% degats elem   -30% degats melee";
        case HERO_ASSASSIN:  return "+25% crit   x2 crit dmg   -25 PV max";
        case HERO_RANGER:    return "+40% degats distance   -25% degats melee";
        case HERO_TEMPLIER:  return "+3 armure   +15 PV   -15% atk speed";
        case HERO_NECROMANT: return "+15% vol de vie   -1 regen/s   +20% dmg vide";
        default: return "";
    }
}

const char *enemy_name(EnemyKind k) {
    switch (k) {
        case EK_ZOMBIE: return "Zombie";
        case EK_BANDIT: return "Bandit";
        case EK_DEMON:  return "Demon";
        case EK_SLIME:  return "Slime";
        case EK_BOSS:   return "Boss";
        default: return "?";
    }
}

/* sous-classe en fonction de la combinaison de 2 armes */
const char *subclass_name(WeaponKind a, WeaponKind b) {
    /* normalize order */
    if (a > b) { WeaponKind t = a; a = b; b = t; }
    /* solo (only fists+X) -> base name */
    if (a == W_FISTS && b == W_FISTS) return "Pugiliste";
    if (a == W_FISTS) {
        switch (b) {
            case W_SWORD:  return "Spadassin";
            case W_SHIELD: return "Sentinelle";
            case W_BOW:    return "Archer";
            case W_WAND:   return "Initie";
            case W_AXE:    return "Bucheron";
            default: break;
        }
    }
    if (a == W_SWORD  && b == W_SHIELD) return "Garde";
    if (a == W_SWORD  && b == W_BOW)    return "Eclaireur";
    if (a == W_SWORD  && b == W_WAND)   return "Sorcelame";
    if (a == W_SWORD  && b == W_AXE)    return "Bretteur";
    if (a == W_SHIELD && b == W_BOW)    return "Sentinelle Royale";
    if (a == W_SHIELD && b == W_WAND)   return "Templier";
    if (a == W_SHIELD && b == W_AXE)    return "Croise";
    if (a == W_BOW    && b == W_WAND)   return "Archimage";
    if (a == W_BOW    && b == W_AXE)    return "Traqueur";
    if (a == W_WAND   && b == W_AXE)    return "Chamane";
    if (a == b) {
        switch (a) {
            case W_SWORD:  return "Duelliste";
            case W_SHIELD: return "Citadelle";
            case W_BOW:    return "Tireur d'Elite";
            case W_WAND:   return "Archonte";
            case W_AXE:    return "Boucher";
            default: break;
        }
    }
    return "Aventurier";
}

void weapon_init_defaults(Weapon *w, WeaponKind kind) {
    memset(w, 0, sizeof(*w));
    w->kind = kind;
    switch (kind) {
        case W_FISTS:
            w->base_cd = 0.30f; w->base_dmg = 6.f;  w->base_range = 16.f; break;
        case W_SWORD:
            w->base_cd = 0.40f; w->base_dmg = 16.f; w->base_range = 26.f; break;
        case W_SHIELD:
            w->base_cd = 1.7f;  w->base_dmg = 10.f; w->base_range = 26.f; break;
        case W_BOW:
            w->base_cd = 0.65f; w->base_dmg = 14.f; w->base_range = 240.f; break;
        case W_WAND:
            w->base_cd = 1.4f;  w->base_dmg = 24.f; w->base_range = 130.f; break;
        case W_AXE:
            w->base_cd = 1.1f;  w->base_dmg = 26.f; w->base_range = 32.f; break;
        default: break;
    }
}

void weapon_attach_element(Weapon *w, Element e) {
    if (w->element_count >= MAX_ELEMENTS_PER_WEAPON) {
        for (int i = 0; i < MAX_ELEMENTS_PER_WEAPON - 1; i++)
            w->elements[i] = w->elements[i + 1];
        w->elements[MAX_ELEMENTS_PER_WEAPON - 1] = e;
    } else {
        w->elements[w->element_count++] = e;
    }
}

int weapon_combo_id(const Weapon *w) {
    /* bitmask des elements presents : 1 bit par enum (sauf EL_NONE).
     * Stocke en uint64_t pour autoriser jusqu'a 63 elements (la limite
     * pratique etant l'explosion combinatoire de compute_combo bien avant).
     * Le retour reste int (toujours <=2^31 valeurs en pratique) pour
     * compat avec le reste du code. */
    uint64_t mask = 0;
    for (int i = 0; i < w->element_count; i++) {
        Element e = w->elements[i];
        if (e > 0 && (int)e < 64) mask |= ((uint64_t)1 << e);
    }
    /* on tronque en int car aucun element au-dela de 31 n'est utilise pour
     * l'instant ; si tu en ajoutes au-dela, change egalement la signature */
    _Static_assert(EL_COUNT <= 31,
        "weapon_combo_id: passe la signature en uint64_t si EL_COUNT > 31");
    return (int)mask;
}

static bool combo_has(int mask, Element a) { return (mask & (1 << a)) != 0; }
static bool combo_only(int mask, Element a) { return mask == (1 << a); }

void weapon_describe(const Weapon *w, char *buf, int bufsz) {
    int n = snprintf(buf, bufsz, "[");
    for (int i = 0; i < w->element_count && n < bufsz - 8; i++) {
        if (i > 0) n += snprintf(buf + n, bufsz - n, "+");
        n += snprintf(buf + n, bufsz - n, "%s", element_name(w->elements[i]));
    }
    if (w->element_count == 0) n += snprintf(buf + n, bufsz - n, "neutre");
    snprintf(buf + n, bufsz - n, "]");
}

/* ---------- COMBO FX ---------- */
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
    Element status;
    uint32_t color;
    const char *tag;
} ComboFx;

static ComboFx compute_combo(int mask) {
    ComboFx c = {0};
    c.dmg_mul = 1.f; c.cd_mul = 1.f; c.range_mul = 1.f;
    c.color = 0xFFFFFFFF; c.tag = "Brut";
    if (mask == 0) return c;

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
    if (mask == aw) { c.tag="Brume";    c.cd_mul=0.6f;  c.dmg_mul=0.9f; c.color=0xC0D8E8FF; return c; }
    if (mask == vf) { c.tag="Feu noir"; c.dmg_mul=1.8f; c.lifesteal=true; c.status=EL_FIRE; c.color=0x802040FF; return c; }
    if (mask == vw) { c.tag="Acide";    c.dmg_mul=1.5f; c.pierces=true; c.color=0x80B040FF; return c; }
    if (mask == vee){ c.tag="Tombeau"; c.dmg_mul=1.7f; c.aoe_explode=true; c.color=0x402030FF; return c; }
    if (mask == vl) { c.tag="Annihile"; c.dmg_mul=2.0f; c.pierces=true; c.color=0xC080FFFF; return c; }
    if (mask == va) { c.tag="Eclipse";  c.cd_mul=0.7f;  c.dmg_mul=1.3f; c.color=0x6040A0FF; return c; }
    if (mask == vfae){c.tag="Esprit";   c.dmg_mul=1.6f; c.spawn_fairy=true; c.lifesteal=true; c.color=0xC080FFFF; return c; }
    if (mask == faef){c.tag="Feu fee";  c.dmg_mul=1.5f; c.homing=true; c.status=EL_FIRE; c.color=0xFFA0E0FF; return c; }
    if (mask == faew){c.tag="Source";   c.spawn_fairy=true; c.dmg_mul=1.2f; c.color=0xA0D0FFFF; return c; }
    if (mask == faee){c.tag="Verger";   c.dmg_mul=1.4f; c.spawn_fairy=true; c.color=0xA0FFA0FF; return c; }
    if (mask == faea){c.tag="Sylphe";   c.cd_mul=0.65f; c.homing=true; c.color=0xE0E0FFFF; return c; }
    if (mask == fael){c.tag="Etincelle";c.dmg_mul=1.4f; c.chain=true; c.spawn_fairy=true; c.color=0xFFFFA0FF; return c; }

    if (combo_only(mask, EL_FIRE))      { c.tag="Brule";  c.dmg_mul=1.2f; c.status=EL_FIRE; c.color=0xFF8040FF; return c; }
    if (combo_only(mask, EL_WATER))     { c.tag="Glacial";c.dmg_mul=1.1f; c.status=EL_WATER; c.color=0x80B0FFFF; return c; }
    if (combo_only(mask, EL_EARTH))     { c.tag="Pierre"; c.dmg_mul=1.4f; c.color=0xA08060FF; return c; }
    if (combo_only(mask, EL_LIGHTNING)) { c.tag="Eclair"; c.dmg_mul=1.2f; c.chain=true; c.status=EL_LIGHTNING; c.color=0xFFEC60FF; return c; }
    if (combo_only(mask, EL_AIR))       { c.tag="Vent";   c.cd_mul=0.75f; c.range_mul=1.2f; c.color=0xC0E0FFFF; return c; }
    if (combo_only(mask, EL_VOID))      { c.tag="Vide";   c.pierces=true; c.lifesteal=true; c.color=0x8030B0FF; return c; }
    if (combo_only(mask, EL_FAE))       { c.tag="Feerie"; c.spawn_fairy=true; c.color=0xF080F0FF; return c; }

    if (combo_has(mask, EL_FIRE))      { c.status = EL_FIRE;      c.dmg_mul *= 1.15f; }
    if (combo_has(mask, EL_WATER))     { c.status = EL_WATER;     c.dmg_mul *= 1.10f; }
    if (combo_has(mask, EL_LIGHTNING)) { c.chain = true;          c.dmg_mul *= 1.10f; }
    if (combo_has(mask, EL_EARTH))     { c.dmg_mul *= 1.20f; }
    if (combo_has(mask, EL_AIR))       { c.cd_mul *= 0.85f; c.range_mul *= 1.10f; }
    if (combo_has(mask, EL_VOID))      { c.pierces = true; c.lifesteal = true; }
    if (combo_has(mask, EL_FAE))       { c.spawn_fairy = true; }
    c.tag = "Hybride"; c.color = 0xC0C0FFFF;
    return c;
}

/* ---------- WEAPON FIRING ---------- */
static int nearest_enemy(Game *g, float x, float y, float range, float *out_d) {
    int best = -1; float bestd = range * range;
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (!g->enemies[i].alive) continue;
        if (g->enemies[i].dying_t > 0.f) continue;   /* skip cadavres */
        float dx = g->enemies[i].x - x;
        float dy = g->enemies[i].y - y;
        float d2 = dx * dx + dy * dy;
        if (d2 < bestd) { bestd = d2; best = i; }
    }
    if (out_d) *out_d = sqrtf(bestd);
    return best;
}

static void burst_particles(Game *g, float x, float y, int n, uint32_t color, float speed_max) {
    for (int i = 0; i < n; i++) {
        float a = (rand() % 360) * 0.01745f;
        float s = (rand() % 100) / 100.f * speed_max + speed_max * 0.3f;
        particle_spawn_kind(g, x, y, cosf(a) * s, sinf(a) * s, 0.5f, color, 2.5f, 2);
    }
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
    /* visual ring */
    int n = (int)(radius * 0.7f); if (n > 50) n = 50;
    for (int i = 0; i < n; i++) {
        float a = (i / (float)n) * 6.2831f;
        particle_spawn_kind(g, x + cosf(a) * radius * 0.4f, y + sinf(a) * radius * 0.4f,
                            cosf(a) * 80.f, sinf(a) * 80.f, 0.4f, color, 3.f, 0);
    }
    burst_particles(g, x, y, 12, color, 80.f);
}

static void chain_hit(Game *g, int from_idx, float dmg, Element status, int hops, uint32_t color) {
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
            if (p->hp > p->maxhp) p->hp = p->maxhp;   /* cap */
        }
        hits++;
    }
    cue_swing(p, 5, ax, ay);
    sfx_play(g, hits > 0 ? SFX_PUNCH : SFX_SWING);
    if (hits > 0) {
        g->shake_t = 0.10f; g->shake_mag = 2.f;
        g->hitstop_t = 0.04f;
    }
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
            if (p->hp > p->maxhp) p->hp = p->maxhp;   /* cap */
        }
        hits++;
    }
    /* slash arc */
    for (int i = 0; i < 14; i++) {
        float t = i / 14.f;
        float a = atan2f(ay, ax) - 0.9f + t * 1.8f;
        float s = reach * (0.6f + (rand() % 50) / 100.f);
        particle_spawn_kind(g, p->x + cosf(a) * s, p->y + sinf(a) * s,
                            cosf(a) * 60, sinf(a) * 60, 0.22f, fx.color, 2.f, 2);
    }
    cue_swing(p, 0, ax, ay);
    sfx_play(g, hits > 0 ? SFX_HIT : SFX_SWING);
    if (hits > 0) {
        g->shake_t = 0.16f; g->shake_mag = 3.5f;
        g->hitstop_t = 0.05f;
    }
    if (fx.spawn_fairy) fairy_spawn(g, p->x, p->y, fx.status);
}

static void fire_shield(Game *g, Weapon *w, ComboFx fx) {
    Player *p = &g->player;
    p->invuln_t = 0.5f;
    float radius = w->base_range * fx.range_mul + 8.f;
    float dmg = (w->base_dmg + p->flat_dmg) * fx.dmg_mul;
    do_aoe_at(g, p->x, p->y, radius, dmg, fx.status, fx.color);
    /* reflect projectiles */
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
    /* visual */
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
    /* big radial particles */
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

void update_weapons(Game *g) {
    Player *p = &g->player;
    float dt = g->dt;
    /* aim TOUJOURS souris (deja calcule dans update_player). On gate
       les armes melee/AOE sur la presence d'un ennemi en portee pour
       eviter le bruit visuel/sonore d'attaques dans le vide. */
    for (int i = 0; i < WEAPON_SLOTS; i++) {
        Weapon *w = &p->weapons[i];
        if (!w->owned) continue;
        w->cooldown -= dt;
        if (w->cooldown > 0.f) continue;
        ComboFx fx = compute_combo(weapon_combo_id(w));

        /* applique les stats joueur dans le combo fx */
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
        fx.dmg_mul *= pmul;
        fx.range_mul *= p->range_mul;

        /* Gating coherent : toutes les armes "actives" (qui produisent un
         * impact visible / coute du calcul) sont gatees sur la presence
         * d'un ennemi en portee, sauf le bouclier qui est defensif et
         * doit pouvoir reflechir des projectiles meme sans cible. */
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

        switch (w->kind) {
            case W_FISTS:  fire_fists(g, w, fx);  break;
            case W_SWORD:  fire_sword(g, w, fx);  break;
            case W_SHIELD: fire_shield(g, w, fx); break;
            case W_BOW:    fire_bow(g, w, fx);    break;
            case W_WAND:   fire_wand(g, w, fx);   break;
            case W_AXE:    fire_axe(g, w, fx);    break;
            default: break;
        }
        /* atk_speed_mul < 1 = plus rapide */
        w->cooldown = w->base_cd * fx.cd_mul * p->atk_speed_mul;
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
                do_aoe_at(g, pr->x, pr->y, pr->aoe, pr->dmg * 0.7f, pr->primary, element_color(pr->primary));
                sfx_play(g, SFX_EXPLODE);
            }
            pr->alive = false;
            continue;
        }

        if (pr->owner == 0 && pr->homing > 0.f) {
            if (pr->target_idx < 0 || !g->enemies[pr->target_idx].alive) {
                pr->target_idx = nearest_enemy(g, pr->x, pr->y, 220.f, NULL);
            }
            if (pr->target_idx >= 0) {
                Enemy *t = &g->enemies[pr->target_idx];
                float dx = t->x - pr->x, dy = t->y - pr->y;
                float d = sqrtf(dx * dx + dy * dy) + 0.001f;
                float speed = sqrtf(pr->vx * pr->vx + pr->vy * pr->vy);
                pr->vx += (dx / d) * pr->homing * 60.f * dt;
                pr->vy += (dy / d) * pr->homing * 60.f * dt;
                float ns = sqrtf(pr->vx * pr->vx + pr->vy * pr->vy) + 0.001f;
                pr->vx = pr->vx / ns * speed;
                pr->vy = pr->vy / ns * speed;
            }
        }

        pr->x += pr->vx * dt;
        pr->y += pr->vy * dt;

        int tx = (int)(pr->x / TILE), ty = (int)(pr->y / TILE);
        if (tx < 0 || ty < 0 || tx >= MAP_W || ty >= MAP_H || tile_solid(g->dungeon.tiles[ty][tx])) {
            if (pr->owner == 0 && pr->aoe > 0.f) {
                do_aoe_at(g, pr->x, pr->y, pr->aoe, pr->dmg * 0.7f, pr->primary, element_color(pr->primary));
                sfx_play(g, SFX_EXPLODE);
            }
            pr->alive = false;
            continue;
        }

        if (pr->owner == 0) {
            if ((rand() % 100) < 40)
                particle_spawn_kind(g, pr->x, pr->y, 0, 0, 0.18f, element_color(pr->primary), 2.f, 0);
            for (int e = 0; e < MAX_ENEMIES; e++) {
                Enemy *en = &g->enemies[e];
                if (!en->alive) continue;
                float dx = en->x - pr->x, dy = en->y - pr->y;
                float rr = en->r + pr->r;
                if (dx * dx + dy * dy < rr * rr) {
                    world_enemy_damage(g, e, pr->dmg, pr->primary, pr->vx * 0.3f, pr->vy * 0.3f);
                    if (pr->aoe > 0.f) {
                        do_aoe_at(g, en->x, en->y, pr->aoe, pr->dmg * 0.6f, pr->primary, element_color(pr->primary));
                        sfx_play(g, SFX_EXPLODE);
                    }
                    if (pr->chains > 0) {
                        chain_hit(g, e, pr->dmg * 0.6f, pr->primary, pr->chains, element_color(pr->primary));
                    }
                    if (pr->pierce > 0) pr->pierce--;
                    else                pr->alive = false;
                    break;
                }
            }
        } else {
            float dx = p->x - pr->x, dy = p->y - pr->y;
            float rr = p->r + pr->r;
            if (dx * dx + dy * dy < rr * rr) {
                if (p->invuln_t <= 0.f && p->dash_t <= 0.f) {
                    player_take_damage(g, pr->dmg);
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
        if (f->target < 0 || !g->enemies[f->target].alive) {
            f->target = nearest_enemy(g, f->x, f->y, 180.f, NULL);
        }
        float tx, ty;
        if (f->target >= 0) {
            tx = g->enemies[f->target].x; ty = g->enemies[f->target].y;
        } else {
            tx = p->x + cosf(g->time * 2.f + i) * 30.f;
            ty = p->y + sinf(g->time * 2.f + i) * 30.f;
        }
        float dx = tx - f->x, dy = ty - f->y;
        float d = sqrtf(dx * dx + dy * dy) + 0.01f;
        f->vx += dx / d * 220.f * dt;
        f->vy += dy / d * 220.f * dt;
        f->vx *= 0.92f; f->vy *= 0.92f;
        f->x += f->vx * dt; f->y += f->vy * dt;
        if (f->target >= 0 && d < 14.f && f->cd <= 0.f) {
            f->cd = 0.45f;
            world_enemy_damage(g, f->target, 7.f, f->element, dx * 0.2f, dy * 0.2f);
            particle_spawn_kind(g, f->x, f->y, 0, 0, 0.4f, element_color(f->element), 3.f, 0);
        }
        if ((rand() % 100) < 30)
            particle_spawn_kind(g, f->x, f->y, 0, 0, 0.4f, element_color(f->element), 2.f, 0);
    }
}

/* ---------- DAMAGE NUMBERS ---------- */
void update_dmgnums(Game *g) {
    float dt = g->dt;
    for (int i = 0; i < MAX_DMGNUM; i++) {
        DamageNumber *d = &g->dmgnums[i];
        if (!d->alive) continue;
        d->life -= dt;
        d->y += d->vy * dt;
        d->vy += 30.f * dt;          /* mild gravity */
        if (d->life <= 0.f) d->alive = false;
    }
}
