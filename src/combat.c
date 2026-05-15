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

/* nombre de slots talisman selon la qualite de l'arme :
 *   COMMON / MAGIC -> 1   (qualite 1 ou 2 = 1 slot)
 *   RARE / EPIC    -> 2   (qualite 3 ou 4 = 2 slots)
 *   LEGENDARY      -> 3   (qualite 5 = 3 slots)
 * Cap dur a MAX_ELEMENTS_PER_WEAPON. */
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
    int max_slots = weapon_slot_count(w->rarity);
    if (w->element_count >= max_slots) {
        /* rotation : on degage le plus ancien pour faire de la place,
         * dans la limite des slots autorises par la qualite de l'arme. */
        for (int i = 0; i < max_slots - 1; i++)
            w->elements[i] = w->elements[i + 1];
        w->elements[max_slots - 1] = e;
        w->element_count = max_slots;
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

void weapon_describe(const Weapon *w, char *buf, int bufsz) {
    int n = snprintf(buf, bufsz, "[");
    for (int i = 0; i < w->element_count && n < bufsz - 8; i++) {
        if (i > 0) n += snprintf(buf + n, bufsz - n, "+");
        n += snprintf(buf + n, bufsz - n, "%s", element_name(w->elements[i]));
    }
    if (w->element_count == 0) n += snprintf(buf + n, bufsz - n, "neutre");
    snprintf(buf + n, bufsz - n, "]");
}

/* ==============================================================
   BEHAVIOR TAGS
   ============================================================== */
typedef enum {
    TAG_NONE        = 0,
    TAG_HOT         = 1 << 0,
    TAG_FLUID       = 1 << 1,
    TAG_HEAVY       = 1 << 2,
    TAG_LIGHT       = 1 << 3,
    TAG_CONDUCTIVE  = 1 << 4,
    TAG_PERSISTENT  = 1 << 5,
    TAG_UNSTABLE    = 1 << 6,
    TAG_CORROSIVE   = 1 << 7,
    TAG_DIVINE      = 1 << 8,
    TAG_SHADOW      = 1 << 9,
    TAG_METALLIC    = 1 << 10,
    TAG_HOMING_TAG  = 1 << 11,
} BehaviorTag;

#define HAS_TAGS(t, req) (((t) & (req)) == (req))

/* ==============================================================
   COMBOFX  (struct inchangee -- les fire_* en dependent)
   ============================================================== */
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

/* ==============================================================
   COUCHE 1 -- ElemBase : contribution de chaque element
   dmg_add  : ADDITIF depuis 1.0  (FIRE = +0.15 -> dmg_mul = 1.15)
   cd_mul   : MULTIPLICATIF       (AIR  =  0.80 -> -20% cooldown)
   0.0f sur cd_mul / range_mul = "pas de modification"
   ============================================================== */
typedef struct {
    uint32_t tags;
    float    dmg_add;
    float    cd_mul;
    float    range_mul;
    bool     pierces;
    bool     homing;
    bool     chain;
    bool     aoe_explode;
    bool     spawn_fairy;
    bool     lifesteal;
    int      extra_proj;
    Element  status;
    uint32_t color_tint;
} ElemBase;

static const ElemBase ELEM_BASE[EL_COUNT] = {
    [EL_NONE]      = { 0 },
    [EL_FIRE]      = { TAG_HOT|TAG_PERSISTENT,      .dmg_add= 0.15f,                                .status=EL_FIRE,      .color_tint=0xFF8040FF },
    [EL_WATER]     = { TAG_FLUID|TAG_CONDUCTIVE,                     .cd_mul=0.90f,                  .status=EL_WATER,     .color_tint=0x80B0FFFF },
    [EL_EARTH]     = { TAG_HEAVY,                   .dmg_add= 0.20f,               .pierces=true,                          .color_tint=0xA08060FF },
    [EL_LIGHTNING] = { TAG_CONDUCTIVE|TAG_UNSTABLE, .dmg_add= 0.15f,               .chain=true,      .status=EL_LIGHTNING, .color_tint=0xFFEC60FF },
    [EL_AIR]       = { TAG_LIGHT,                                    .cd_mul=0.80f, .range_mul=1.20f,                       .color_tint=0xC0E0FFFF },
    [EL_VOID]      = { TAG_CORROSIVE|TAG_UNSTABLE,  .dmg_add= 0.25f,               .pierces=true,    .lifesteal=true,      .color_tint=0x8030B0FF },
    [EL_FAE]       = { TAG_HOMING_TAG|TAG_LIGHT,                                    .homing=true,     .spawn_fairy=true,    .status=EL_FAE, .color_tint=0xF080F0FF },
    [EL_STEEL]     = { TAG_METALLIC|TAG_HEAVY,      .dmg_add= 0.15f,               .pierces=true,                          .color_tint=0xC0C8D0FF },
    [EL_DARK]      = { TAG_SHADOW|TAG_CORROSIVE,    .dmg_add= 0.10f,                                 .lifesteal=true,      .status=EL_DARK, .color_tint=0x505060FF },
    [EL_HOLY]      = { TAG_DIVINE,                  .dmg_add= 0.10f,               .aoe_explode=true, .status=EL_HOLY,     .color_tint=0xFFE890FF },
};
_Static_assert(sizeof(ELEM_BASE)/sizeof(ELEM_BASE[0]) == EL_COUNT,
    "ELEM_BASE desynchronise avec EL_COUNT");

/* ==============================================================
   COUCHE 2 -- EmergentRule : interactions tag-paires
   required_tags : les DEUX bits doivent etre dans active_tags
   0.0f / false = pas de modification
   ============================================================== */
typedef struct {
    uint32_t required_tags;
    float    dmg_add;
    float    cd_mul;
    float    range_mul;
    bool     chain;
    bool     aoe_explode;
    bool     homing;
    bool     pierces;
    bool     spawn_fairy;
    int      extra_proj;
} EmergentRule;

static const EmergentRule EMERGENT_RULES[] = {
    { TAG_HOT|TAG_FLUID,             .dmg_add=-0.10f, .range_mul=1.20f, .aoe_explode=true  },
    { TAG_CONDUCTIVE|TAG_UNSTABLE,   .dmg_add= 0.20f, .chain=true                          },
    { TAG_HEAVY|TAG_HOT,             .dmg_add= 0.25f, .cd_mul=1.20f                        },
    { TAG_LIGHT|TAG_HOT,             .extra_proj=2,   .range_mul=1.10f                     },
    { TAG_CORROSIVE|TAG_HOMING_TAG,  .dmg_add=-0.10f, .extra_proj=2,   .homing=true        },
    { TAG_DIVINE|TAG_HOT,            .dmg_add= 0.20f, .aoe_explode=true                    },
    { TAG_METALLIC|TAG_CONDUCTIVE,   .pierces=true,   .range_mul=1.30f                     },
    { TAG_SHADOW|TAG_UNSTABLE,       .dmg_add= 0.20f, .homing=true                         },
    { TAG_DIVINE|TAG_FLUID,          .aoe_explode=true, .spawn_fairy=true                  },
    { TAG_FLUID|TAG_HEAVY,           .dmg_add= 0.10f, .aoe_explode=true, .cd_mul=1.20f     },
};
static const int N_EMERGENT = (int)(sizeof(EMERGENT_RULES)/sizeof(EMERGENT_RULES[0]));

/* ==============================================================
   COUCHE 3 -- ComboName : cosmetique seul, aucune logique
   Sentinel { 0, NULL, 0 } obligatoire en fin de tableau.
   ============================================================== */
typedef struct { int mask; const char *name; uint32_t color; } ComboName;

static const ComboName COMBO_NAMES[] = {
    { (1<<EL_FIRE)|(1<<EL_WATER)|(1<<EL_LIGHTNING),  "Tempete",        0x80F0FFFF },
    { (1<<EL_FIRE)|(1<<EL_EARTH)|(1<<EL_AIR),         "Volcan",         0xFF8040FF },
    { (1<<EL_VOID)|(1<<EL_FAE)|(1<<EL_LIGHTNING),     "Dechirure",      0xC080FFFF },
    { (1<<EL_WATER)|(1<<EL_EARTH)|(1<<EL_LIGHTNING),  "Tsunami",        0x60A0FFFF },
    { (1<<EL_WATER)|(1<<EL_AIR)|(1<<EL_EARTH),        "Marais",         0x80A8A0FF },
    { (1<<EL_FIRE)|(1<<EL_AIR)|(1<<EL_FAE),           "Phenix",         0xFFB0F0FF },
    { (1<<EL_VOID)|(1<<EL_WATER)|(1<<EL_AIR),         "Brume Mortelle", 0x9090C0FF },
    { (1<<EL_EARTH)|(1<<EL_AIR)|(1<<EL_VOID),         "Effondrement",   0x806040FF },
    { (1<<EL_FIRE)|(1<<EL_WATER),       "Vapeur",     0xC0E0F0FF },
    { (1<<EL_FIRE)|(1<<EL_LIGHTNING),   "Plasma",     0xFFA0F0FF },
    { (1<<EL_WATER)|(1<<EL_LIGHTNING),  "Choc",       0x80FFFFFF },
    { (1<<EL_FIRE)|(1<<EL_EARTH),       "Lave",       0xFF6020FF },
    { (1<<EL_WATER)|(1<<EL_EARTH),      "Boue",       0x806040FF },
    { (1<<EL_EARTH)|(1<<EL_AIR),        "Sable",      0xD0B080FF },
    { (1<<EL_AIR)|(1<<EL_LIGHTNING),    "Orage",      0xFFFF80FF },
    { (1<<EL_AIR)|(1<<EL_FIRE),         "Brasier",    0xFFA040FF },
    { (1<<EL_AIR)|(1<<EL_WATER),        "Brume",      0xC0D8E8FF },
    { (1<<EL_VOID)|(1<<EL_FIRE),        "Feu noir",   0x802040FF },
    { (1<<EL_VOID)|(1<<EL_WATER),       "Acide",      0x80B040FF },
    { (1<<EL_VOID)|(1<<EL_EARTH),       "Tombeau",    0x402030FF },
    { (1<<EL_VOID)|(1<<EL_LIGHTNING),   "Annihile",   0xC080FFFF },
    { (1<<EL_VOID)|(1<<EL_AIR),         "Eclipse",    0x6040A0FF },
    { (1<<EL_VOID)|(1<<EL_FAE),         "Esprit",     0xC080FFFF },
    { (1<<EL_FAE)|(1<<EL_FIRE),         "Feu fee",    0xFFA0E0FF },
    { (1<<EL_FAE)|(1<<EL_WATER),        "Source",     0xA0D0FFFF },
    { (1<<EL_FAE)|(1<<EL_EARTH),        "Verger",     0xA0FFA0FF },
    { (1<<EL_FAE)|(1<<EL_AIR),          "Sylphe",     0xE0E0FFFF },
    { (1<<EL_FAE)|(1<<EL_LIGHTNING),    "Etincelle",  0xFFFFA0FF },
    { (1<<EL_FIRE),       "Brule",    0xFF8040FF },
    { (1<<EL_WATER),      "Glacial",  0x80B0FFFF },
    { (1<<EL_EARTH),      "Pierre",   0xA08060FF },
    { (1<<EL_LIGHTNING),  "Eclair",   0xFFEC60FF },
    { (1<<EL_AIR),        "Vent",     0xC0E0FFFF },
    { (1<<EL_VOID),       "Vide",     0x8030B0FF },
    { (1<<EL_FAE),        "Feerie",   0xF080F0FF },
    { (1<<EL_STEEL),      "Fer",      0xC0C8D0FF },
    { (1<<EL_DARK),       "Ombre",    0x505060FF },
    { (1<<EL_HOLY),       "Sacre",    0xFFE890FF },
    { 0, NULL, 0 }  /* sentinel */
};

/* ==============================================================
   COUCHE 4 -- TripleLoopDef : feedback loop des triples
   Tous les effets overload sont dans la donnee.
   Aucun if/else sur le mask dans les fonctions.
   ============================================================== */
typedef struct {
    int      mask;
    int      loop_idx;
    float    gain_on_hit;
    float    gain_on_kill;
    float    decay_rate;
    float    overload_threshold;
    uint32_t aura_color;
    /* effets overload -- 0 = inactif */
    float    ov_dmg_mul;          /* dmg_mul += ov_dmg_mul * overload  */
    float    ov_chain_add;        /* chain active si > 0               */
    float    ov_proj_add;         /* extra_proj += (int)(ov * val)     */
    float    ov_lifesteal_mul;    /* lifesteal amplifie                */
    float    ov_self_dmg;         /* degat/s sur le joueur si overloaded */
    bool     ov_explode_on_spawn;
    bool     ov_spawn_fairy;
} TripleLoopDef;

static const TripleLoopDef TRIPLE_LOOPS[] = {
    {
        .mask=(1<<EL_FIRE)|(1<<EL_WATER)|(1<<EL_LIGHTNING), .loop_idx=0,
        .gain_on_hit=0.06f, .gain_on_kill=0.15f, .decay_rate=0.08f,
        .overload_threshold=1.0f, .aura_color=0x80C0FFFF,
        .ov_dmg_mul=0.40f, .ov_chain_add=3.0f, .ov_self_dmg=1.0f,
    },
    {
        .mask=(1<<EL_FIRE)|(1<<EL_EARTH)|(1<<EL_AIR), .loop_idx=1,
        .gain_on_hit=0.08f, .gain_on_kill=0.10f, .decay_rate=0.05f,
        .overload_threshold=1.0f, .aura_color=0xFF6020FF,
        .ov_proj_add=2.0f, .ov_dmg_mul=-0.35f, .ov_explode_on_spawn=true,
    },
    {
        .mask=(1<<EL_VOID)|(1<<EL_FAE)|(1<<EL_LIGHTNING), .loop_idx=2,
        .gain_on_hit=0.12f, .gain_on_kill=0.08f, .decay_rate=0.03f,
        .overload_threshold=1.0f, .aura_color=0xA040C0FF,
        .ov_lifesteal_mul=2.0f, .ov_self_dmg=0.5f,
    },
    {
        .mask=(1<<EL_FIRE)|(1<<EL_AIR)|(1<<EL_FAE), .loop_idx=3,
        .gain_on_hit=0.05f, .gain_on_kill=0.20f, .decay_rate=0.10f,
        .overload_threshold=1.0f, .aura_color=0xFF8040FF,
        .ov_proj_add=1.5f, .ov_spawn_fairy=true,
    },
    { .mask=0 }  /* sentinel */
};

/* couleur d'aura par loop_idx (consomme par render.c). Sentinel ignoree. */
uint32_t triple_loop_aura_color(int loop_idx) {
    if (loop_idx < 0) return 0;
    int n = (int)(sizeof(TRIPLE_LOOPS)/sizeof(TRIPLE_LOOPS[0])) - 1;  /* sentinel */
    if (loop_idx >= n) return 0;
    return TRIPLE_LOOPS[loop_idx].aura_color;
}

/* -------------------------------------------------------------- */
static const TripleLoopDef *triple_find(int mask) {
    for (int i = 0; TRIPLE_LOOPS[i].mask != 0; i++)
        if (TRIPLE_LOOPS[i].mask == mask) return &TRIPLE_LOOPS[i];
    return NULL;
}

/* -------------------------------------------------------------- */
static void apply_loop_modifiers(ComboFx *fx,
                                  const TripleLoopDef *def,
                                  const LoopState *ls)
{
    if (!def || !ls || ls->intensity <= def->overload_threshold) return;
    float ov = ls->intensity - def->overload_threshold;  /* 0..1 */
    fx->dmg_mul    += def->ov_dmg_mul * ov;
    fx->extra_proj += (int)(def->ov_proj_add * ov);
    if (def->ov_chain_add > 0.f)  fx->chain       = true;
    if (def->ov_spawn_fairy)       fx->spawn_fairy = true;
}

/* -------------------------------------------------------------- */
void loop_on_hit(Game *g) {
    int idx = g->player.active_loop_idx;
    if (idx < 0) return;
    LoopState *ls = &g->player.loop_states[idx];
    ls->intensity += TRIPLE_LOOPS[idx].gain_on_hit;
    if (ls->intensity > 2.0f) ls->intensity = 2.0f;
    ls->proc_count++;
    ls->overloaded = (ls->intensity > TRIPLE_LOOPS[idx].overload_threshold);
}

/* -------------------------------------------------------------- */
void loop_on_kill(Game *g) {
    int idx = g->player.active_loop_idx;
    if (idx < 0) return;
    LoopState *ls = &g->player.loop_states[idx];
    ls->intensity += TRIPLE_LOOPS[idx].gain_on_kill;
    if (ls->intensity > 2.0f) ls->intensity = 2.0f;
    ls->proc_count++;
    ls->overloaded = (ls->intensity > TRIPLE_LOOPS[idx].overload_threshold);
}

/* -------------------------------------------------------------- */
void loop_decay(Game *g, float dt) {
    int idx = g->player.active_loop_idx;
    if (idx < 0 || g->enemy_alive_count > 0) return;
    LoopState *ls = &g->player.loop_states[idx];
    ls->intensity -= TRIPLE_LOOPS[idx].decay_rate * dt;
    if (ls->intensity < 0.0f) ls->intensity = 0.0f;
    ls->overloaded = (ls->intensity > TRIPLE_LOOPS[idx].overload_threshold);
}

/* -------------------------------------------------------------- */
static void refresh_active_loop(Game *g, int mask) {
    if (mask == g->player.active_loop_mask) return;
    const TripleLoopDef *def = triple_find(mask);
    if (!def) { g->player.active_loop_idx = -1; g->player.active_loop_mask = 0; return; }
    g->player.active_loop_idx  = def->loop_idx;
    g->player.active_loop_mask = mask;
    /* intensity conservee si meme salle -- reset dans game_next_floor */
}

/* ==============================================================
   compute_combo -- aucun if sur un mask, aucun return anticipe
   ============================================================== */
static ComboFx compute_combo(int mask) {
    ComboFx c = {0};
    c.dmg_mul = 1.0f; c.cd_mul = 1.0f; c.range_mul = 1.0f;
    c.color = 0xFFFFFFFF; c.tag = "Brut";
    if (mask == 0) return c;

    /* Couche 1 */
    uint32_t active_tags = 0;
    for (int e = 1; e < EL_COUNT; e++) {
        if (!(mask & (1 << e))) continue;
        const ElemBase *b = &ELEM_BASE[e];
        active_tags    |= b->tags;
        c.dmg_mul      += b->dmg_add;
        if (b->cd_mul    != 0.0f) c.cd_mul    *= b->cd_mul;
        if (b->range_mul != 0.0f) c.range_mul *= b->range_mul;
        c.pierces      |= b->pierces;
        c.homing       |= b->homing;
        c.chain        |= b->chain;
        c.aoe_explode  |= b->aoe_explode;
        c.spawn_fairy  |= b->spawn_fairy;
        c.lifesteal    |= b->lifesteal;
        c.extra_proj   += b->extra_proj;
        if (b->status != EL_NONE) c.status = b->status;
        c.color         = b->color_tint;
    }

    /* Couche 2 */
    for (int i = 0; i < N_EMERGENT; i++) {
        const EmergentRule *r = &EMERGENT_RULES[i];
        if (!HAS_TAGS(active_tags, r->required_tags)) continue;
        c.dmg_mul      += r->dmg_add;
        if (r->cd_mul    != 0.0f) c.cd_mul    *= r->cd_mul;
        if (r->range_mul != 0.0f) c.range_mul *= r->range_mul;
        c.chain        |= r->chain;
        c.aoe_explode  |= r->aoe_explode;
        c.homing       |= r->homing;
        c.pierces      |= r->pierces;
        c.spawn_fairy  |= r->spawn_fairy;
        c.extra_proj   += r->extra_proj;
    }

    /* Couche 3 */
    bool named = false;
    for (const ComboName *n = COMBO_NAMES; n->name != NULL; n++) {
        if (n->mask == mask) { c.tag = n->name; c.color = n->color; named = true; break; }
    }
    if (!named) { c.tag = "Hybride"; c.color = 0xC0C0FFFF; }

    return c;
}

/* ==============================================================
   API combos pour les ennemis : on resout le meme compute_combo et on
   projette les proprietes selectionnees sur leurs projectiles. C'est la
   bonne porte d'entree pour faire heriter chain/AOE/homing/element a un
   tir d'ennemi sans dupliquer la table de combos.
   ============================================================== */
void combo_apply_to_enemy_projectile(int mask, Projectile *pr) {
    if (mask == 0 || !pr) return;
    ComboFx fx = compute_combo(mask);
    /* chain : seulement si pas deja chain (eviter de re-stack) */
    if (fx.chain        && pr->chains  == 0) pr->chains  = 2;
    if (fx.aoe_explode  && pr->aoe     == 0.f) pr->aoe   = 28.f;
    if (fx.homing       && pr->homing  == 0.f) pr->homing = 1.2f;
    if (fx.pierces      && pr->pierce  == 0) pr->pierce  = 1;
    /* extra_proj : non gere ici (le spawn est cote AI) */
    /* dmg : on amplifie modestement, le boss a deja son scaling de plancher */
    pr->dmg *= (0.6f + fx.dmg_mul * 0.4f);
    /* element : on prend la composante "status" si l'AI ne l'a pas deja
     * positionne sur un element fort. */
    if (fx.status && pr->primary == EL_NONE) pr->primary = fx.status;
}

/* nom + couleur de la signature : utilises par render.c pour afficher
 * "Vorgar Tempete" au-dessus d'un boss. */
const char *combo_name(int mask) {
    if (mask == 0) return "";
    ComboFx fx = compute_combo(mask);
    return fx.tag ? fx.tag : "";
}
uint32_t combo_color(int mask) {
    if (mask == 0) return 0xFFFFFFFF;
    ComboFx fx = compute_combo(mask);
    return fx.color;
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
        int mask = weapon_combo_id(w);
        ComboFx fx = compute_combo(mask);

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
        /* expose le flag de crit aux degats : enemy_take_damage l'utilisera
         * pour colorer + scaler le dmgnum et le hitstop. */
        g->current_attack_crit = crit;

        /* Signature Combo callout : un combo triple (3 elements distincts)
         * vient de se declencher. On affiche son nom une fois quand le
         * loadout change, pour eviter le spam. */
        if (w->element_count == 3) {
            if (mask != g->combo_callout_mask && fx.tag) {
                g->combo_callout_mask = mask;
                g->combo_callout_t = 2.5f;
                g->combo_callout_color = fx.color;
                snprintf(g->combo_callout, sizeof(g->combo_callout),
                         "%s !", fx.tag);
                /* burst circulaire de particules autour du joueur */
                for (int k = 0; k < 36; k++) {
                    float a = (k / 36.f) * 6.2831f;
                    particle_spawn_kind(g, p->x, p->y,
                                        cosf(a) * 130.f, sinf(a) * 130.f,
                                        0.7f, fx.color, 3.5f, 2);
                }
            }
        }

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

        /* Triple feedback loop : si l'arme porte un triple combo, on
         * (re)synchronise le loop actif et on applique son boost si
         * l'intensite a depasse l'overload threshold. */
        if (w->element_count == 3) {
            refresh_active_loop(g, mask);
        }
        {
            const TripleLoopDef *tdef = triple_find(g->player.active_loop_mask);
            apply_loop_modifiers(&fx, tdef,
                g->player.active_loop_idx >= 0
                    ? &g->player.loop_states[g->player.active_loop_idx]
                    : NULL);
        }

        switch (w->kind) {
            case W_FISTS:  fire_fists(g, w, fx);  break;
            case W_SWORD:  fire_sword(g, w, fx);  break;
            case W_SHIELD: fire_shield(g, w, fx); break;
            case W_BOW:    fire_bow(g, w, fx);    break;
            case W_WAND:   fire_wand(g, w, fx);   break;
            case W_AXE:    fire_axe(g, w, fx);    break;
            default: break;
        }
        g->current_attack_crit = false;   /* ne fuit pas vers l'arme suivante */
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
