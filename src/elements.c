/*
 * elements.c - donnees + logique du systeme elementaire :
 *
 *   COUCHE 1 - ELEM_BASE       : contribution par element
 *   COUCHE 2 - EMERGENT_RULES  : interactions tag-paires
 *   COUCHE 3 - COMBO_NAMES     : noms cosmetiques par mask
 *   COUCHE 4 - TRIPLE_LOOPS    : feedback loops des triples
 *
 *   API publique (game.h)         : element_name/color, elem_effectiveness,
 *                                    combo_name/color, combo_apply_to_enemy_projectile,
 *                                    combo_table_*, combo_mask_element_count,
 *                                    loop_on_hit/kill/decay, triple_loop_aura_color
 *   API combat-interne (combat_internal.h) : combo_compute, combo_refresh_active_loop,
 *                                            combo_apply_loop_modifiers
 *
 * Extrait de combat.c qui ne garde plus que les armes (fire_* + update_weapons).
 */
#include "combat_internal.h"

/* ---------- elem_effectiveness (Pokemon-like) ---------- */
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

/* ---------- BEHAVIOR TAGS ---------- */
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

/* ---------- COUCHE 1 : ElemBase ---------- */
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

/* ---------- COUCHE 2 : EmergentRule ---------- */
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
    { TAG_DIVINE|TAG_SHADOW,         .dmg_add= 0.40f, .cd_mul=1.25f,    .aoe_explode=true  },
    { TAG_METALLIC|TAG_HOT,          .dmg_add= 0.25f, .pierces=true                        },
    { TAG_FLUID|TAG_LIGHT,           .extra_proj=1,   .range_mul=1.15f                     },
    { TAG_HEAVY|TAG_CORROSIVE,       .dmg_add= 0.15f, .aoe_explode=true                    },
    { TAG_PERSISTENT|TAG_CONDUCTIVE, .chain=true,     .dmg_add= 0.10f                      },
    { TAG_DIVINE|TAG_CORROSIVE,      .aoe_explode=true, .homing=true                       },
    { TAG_METALLIC|TAG_LIGHT,        .pierces=true,   .range_mul=1.25f                     },
    { TAG_HOMING_TAG|TAG_HOT,        .extra_proj=1,   .homing=true,     .dmg_add= 0.10f    },
};
static const int N_EMERGENT = (int)(sizeof(EMERGENT_RULES)/sizeof(EMERGENT_RULES[0]));

/* ---------- COUCHE 3 : COMBO_NAMES (cosmetique) ---------- */
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
    /* === paires Acier === */
    { (1<<EL_STEEL)|(1<<EL_FIRE),       "Forge",         0xFFB060FF },
    { (1<<EL_STEEL)|(1<<EL_WATER),      "Trempe",        0x90B0D0FF },
    { (1<<EL_STEEL)|(1<<EL_EARTH),      "Lame",          0xA0A090FF },
    { (1<<EL_STEEL)|(1<<EL_LIGHTNING),  "Magneto",       0xC0E0FFFF },
    { (1<<EL_STEEL)|(1<<EL_AIR),        "Lame du Vent",  0xD0E0F0FF },
    { (1<<EL_STEEL)|(1<<EL_VOID),       "Acier Noir",    0x404048FF },
    { (1<<EL_STEEL)|(1<<EL_FAE),        "Mithril",       0xE0E0F8FF },
    { (1<<EL_STEEL)|(1<<EL_DARK),       "Lame Maudite",  0x303038FF },
    { (1<<EL_STEEL)|(1<<EL_HOLY),       "Excalibur",     0xFFF0A0FF },
    /* === paires Tenebres (sans EL_FIRE deja "Feu noir") === */
    { (1<<EL_DARK)|(1<<EL_WATER),       "Encre",         0x202040FF },
    { (1<<EL_DARK)|(1<<EL_EARTH),       "Necrose",       0x504030FF },
    { (1<<EL_DARK)|(1<<EL_LIGHTNING),   "Anatheme",      0x6040A0FF },
    { (1<<EL_DARK)|(1<<EL_AIR),         "Murmure",       0x404048FF },
    { (1<<EL_DARK)|(1<<EL_VOID),        "Neant",         0x101018FF },
    { (1<<EL_DARK)|(1<<EL_FAE),         "Spectre",       0x804080FF },
    { (1<<EL_DARK)|(1<<EL_HOLY),        "Crepuscule",    0x806080FF },
    /* === paires Sacre === */
    { (1<<EL_HOLY)|(1<<EL_FIRE),        "Flamme Sainte", 0xFFD060FF },
    { (1<<EL_HOLY)|(1<<EL_WATER),       "Benediction",   0xC0E0FFFF },
    { (1<<EL_HOLY)|(1<<EL_EARTH),       "Sanctuaire",    0xE0D8A0FF },
    { (1<<EL_HOLY)|(1<<EL_LIGHTNING),   "Foudre Divine", 0xFFF0A0FF },
    { (1<<EL_HOLY)|(1<<EL_AIR),         "Souffle Divin", 0xFFF8D0FF },
    { (1<<EL_HOLY)|(1<<EL_VOID),        "Exorcisme",     0xE0C0FFFF },
    { (1<<EL_HOLY)|(1<<EL_FAE),         "Grace",         0xFFE0FFFF },
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

/* ---------- COUCHE 4 : TripleLoopDef ----------
 * L'index dans le tableau == l'identite du loop ; on ne stocke plus
 * loop_idx dans la donnee pour eviter l'invariant manuel fragile. */
typedef struct {
    int      mask;
    float    gain_on_hit;
    float    gain_on_kill;
    float    decay_rate;
    float    overload_threshold;
    uint32_t aura_color;
    /* effets overload -- 0 = inactif */
    float    ov_dmg_mul;
    float    ov_chain_add;
    float    ov_proj_add;
    float    ov_lifesteal_mul;
    float    ov_self_dmg;
    bool     ov_explode_on_spawn;
    bool     ov_spawn_fairy;
} TripleLoopDef;

static const TripleLoopDef TRIPLE_LOOPS[] = {
    { .mask=(1<<EL_FIRE)|(1<<EL_WATER)|(1<<EL_LIGHTNING),  /* Tempete   */
      .gain_on_hit=0.06f, .gain_on_kill=0.15f, .decay_rate=0.08f,
      .overload_threshold=1.0f, .aura_color=0x80C0FFFF,
      .ov_dmg_mul=0.40f, .ov_chain_add=3.0f, .ov_self_dmg=1.0f },
    { .mask=(1<<EL_FIRE)|(1<<EL_EARTH)|(1<<EL_AIR),        /* Volcan    */
      .gain_on_hit=0.08f, .gain_on_kill=0.10f, .decay_rate=0.05f,
      .overload_threshold=1.0f, .aura_color=0xFF6020FF,
      .ov_proj_add=2.0f, .ov_dmg_mul=-0.35f, .ov_explode_on_spawn=true },
    { .mask=(1<<EL_VOID)|(1<<EL_FAE)|(1<<EL_LIGHTNING),    /* Dechirure */
      .gain_on_hit=0.12f, .gain_on_kill=0.08f, .decay_rate=0.03f,
      .overload_threshold=1.0f, .aura_color=0xA040C0FF,
      .ov_lifesteal_mul=2.0f, .ov_self_dmg=0.5f },
    { .mask=(1<<EL_FIRE)|(1<<EL_AIR)|(1<<EL_FAE),          /* Phenix    */
      .gain_on_hit=0.05f, .gain_on_kill=0.20f, .decay_rate=0.10f,
      .overload_threshold=1.0f, .aura_color=0xFF8040FF,
      .ov_proj_add=1.5f, .ov_spawn_fairy=true },
    { .mask=(1<<EL_WATER)|(1<<EL_EARTH)|(1<<EL_LIGHTNING), /* Tsunami   */
      .gain_on_hit=0.07f, .gain_on_kill=0.12f, .decay_rate=0.06f,
      .overload_threshold=1.0f, .aura_color=0x60A0FFFF,
      .ov_dmg_mul=0.30f, .ov_explode_on_spawn=true, .ov_chain_add=2.0f },
    { .mask=(1<<EL_WATER)|(1<<EL_AIR)|(1<<EL_EARTH),       /* Marais    */
      .gain_on_hit=0.06f, .gain_on_kill=0.10f, .decay_rate=0.04f,
      .overload_threshold=1.0f, .aura_color=0x80A8A0FF,
      .ov_dmg_mul=0.20f, .ov_explode_on_spawn=true },
    { .mask=(1<<EL_VOID)|(1<<EL_WATER)|(1<<EL_AIR),        /* Brume Mortelle */
      .gain_on_hit=0.10f, .gain_on_kill=0.08f, .decay_rate=0.04f,
      .overload_threshold=1.0f, .aura_color=0x9090C0FF,
      .ov_dmg_mul=0.25f, .ov_lifesteal_mul=1.0f, .ov_self_dmg=0.4f },
    { .mask=(1<<EL_EARTH)|(1<<EL_AIR)|(1<<EL_VOID),        /* Effondrement */
      .gain_on_hit=0.07f, .gain_on_kill=0.15f, .decay_rate=0.06f,
      .overload_threshold=1.0f, .aura_color=0x806040FF,
      .ov_dmg_mul=0.45f, .ov_proj_add=1.0f, .ov_explode_on_spawn=true },
    { .mask=(1<<EL_STEEL)|(1<<EL_FIRE)|(1<<EL_LIGHTNING),  /* Forge Solaire */
      .gain_on_hit=0.08f, .gain_on_kill=0.10f, .decay_rate=0.06f,
      .overload_threshold=1.0f, .aura_color=0xFFB060FF,
      .ov_dmg_mul=0.50f, .ov_chain_add=2.0f },
    { .mask=(1<<EL_DARK)|(1<<EL_HOLY)|(1<<EL_LIGHTNING),   /* Jugement  */
      .gain_on_hit=0.10f, .gain_on_kill=0.10f, .decay_rate=0.04f,
      .overload_threshold=1.0f, .aura_color=0xFFE890FF,
      .ov_dmg_mul=0.35f, .ov_chain_add=2.0f, .ov_explode_on_spawn=true,
      .ov_self_dmg=0.3f },
    { .mask=0 }  /* sentinel */
};

uint32_t triple_loop_aura_color(int loop_idx) {
    if (loop_idx < 0) return 0;
    int n = (int)(sizeof(TRIPLE_LOOPS)/sizeof(TRIPLE_LOOPS[0])) - 1;
    if (loop_idx >= n) return 0;
    return TRIPLE_LOOPS[loop_idx].aura_color;
}

static const TripleLoopDef *triple_find(int mask) {
    for (int i = 0; TRIPLE_LOOPS[i].mask != 0; i++)
        if (TRIPLE_LOOPS[i].mask == mask) return &TRIPLE_LOOPS[i];
    return NULL;
}

static void apply_loop_modifiers_internal(ComboFx *fx,
                                            const TripleLoopDef *def,
                                            const LoopState *ls)
{
    if (!def || !ls || ls->intensity <= def->overload_threshold) return;
    float ov = ls->intensity - def->overload_threshold;
    fx->dmg_mul    += def->ov_dmg_mul * ov;
    fx->extra_proj += (int)(def->ov_proj_add * ov);
    if (def->ov_chain_add > 0.f)        fx->chain        = true;
    if (def->ov_spawn_fairy)             fx->spawn_fairy  = true;
    if (def->ov_explode_on_spawn)        fx->aoe_explode  = true;
    if (def->ov_lifesteal_mul > 0.f)     fx->lifesteal    = true;
}

void loop_on_hit(Game *g) {
    int idx = g->player.active_loop_idx;
    if (idx < 0) return;
    LoopState *ls = &g->player.loop_states[idx];
    ls->intensity += TRIPLE_LOOPS[idx].gain_on_hit;
    if (ls->intensity > 2.0f) ls->intensity = 2.0f;
    ls->proc_count++;
    ls->overloaded = (ls->intensity > TRIPLE_LOOPS[idx].overload_threshold);
}

void loop_on_kill(Game *g) {
    int idx = g->player.active_loop_idx;
    if (idx < 0) return;
    LoopState *ls = &g->player.loop_states[idx];
    ls->intensity += TRIPLE_LOOPS[idx].gain_on_kill;
    if (ls->intensity > 2.0f) ls->intensity = 2.0f;
    ls->proc_count++;
    ls->overloaded = (ls->intensity > TRIPLE_LOOPS[idx].overload_threshold);
}

/* Tick par frame : decay HORS combat + drain HP en overload. */
void loop_decay(Game *g, float dt) {
    int idx = g->player.active_loop_idx;
    if (idx < 0) return;
    LoopState *ls = &g->player.loop_states[idx];
    const TripleLoopDef *def = &TRIPLE_LOOPS[idx];
    if (g->enemy_alive_count == 0 && ls->intensity > 0.f) {
        ls->intensity -= def->decay_rate * dt;
        if (ls->intensity < 0.0f) ls->intensity = 0.0f;
    }
    if (ls->intensity > def->overload_threshold && def->ov_self_dmg > 0.f) {
        g->player.hp -= def->ov_self_dmg * dt;
        if (g->player.hp < 1.f) g->player.hp = 1.f;
    }
    ls->overloaded = (ls->intensity > def->overload_threshold);
}

void combo_refresh_active_loop(Game *g, int mask) {
    if (mask == g->player.active_loop_mask) return;
    const TripleLoopDef *def = triple_find(mask);
    if (!def) { g->player.active_loop_idx = -1; g->player.active_loop_mask = 0; return; }
    g->player.active_loop_idx  = (int)(def - TRIPLE_LOOPS);
    g->player.active_loop_mask = mask;
}

void combo_apply_loop_modifiers(Game *g, ComboFx *fx) {
    const TripleLoopDef *def = triple_find(g->player.active_loop_mask);
    const LoopState *ls = (g->player.active_loop_idx >= 0)
                            ? &g->player.loop_states[g->player.active_loop_idx]
                            : NULL;
    apply_loop_modifiers_internal(fx, def, ls);
}

/* Priorite de status quand plusieurs elements en posent un. */
static int status_priority(Element e) {
    switch (e) {
        case EL_VOID:      return 90;
        case EL_DARK:      return 85;
        case EL_HOLY:      return 80;
        case EL_FIRE:      return 70;
        case EL_LIGHTNING: return 60;
        case EL_WATER:     return 50;
        case EL_FAE:       return 40;
        default:           return 0;
    }
}

/* ---------- compute_combo : 3 couches additives ---------- */
ComboFx combo_compute(int mask) {
    ComboFx c = {0};
    c.dmg_mul = 1.0f; c.cd_mul = 1.0f; c.range_mul = 1.0f;
    c.color = 0xFFFFFFFF; c.tag = "Brut";
    if (mask == 0) return c;

    /* Couche 1 */
    uint32_t active_tags = 0;
    int      best_status_prio = -1;
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
        if (b->status != EL_NONE) {
            int p = status_priority(b->status);
            if (p > best_status_prio) { c.status = b->status; best_status_prio = p; }
        }
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
    /* Couche 3 (cosmetique) */
    bool named = false;
    for (const ComboName *n = COMBO_NAMES; n->name != NULL; n++) {
        if (n->mask == mask) { c.tag = n->name; c.color = n->color; named = true; break; }
    }
    if (!named) { c.tag = "Hybride"; c.color = 0xC0C0FFFF; }
    return c;
}

/* ---------- API publique pour les ennemis ---------- */
void combo_apply_to_enemy_projectile(int mask, Projectile *pr) {
    if (mask == 0 || !pr) return;
    ComboFx fx = combo_compute(mask);
    if (fx.chain        && pr->chains  == 0)   pr->chains  = 2;
    if (fx.aoe_explode  && pr->aoe     == 0.f) pr->aoe     = 28.f;
    if (fx.homing       && pr->homing  == 0.f) pr->homing  = 1.2f;
    if (fx.pierces      && pr->pierce  == 0)   pr->pierce  = 1;
    pr->dmg *= (0.6f + fx.dmg_mul * 0.4f);
    if (fx.status && pr->primary == EL_NONE) pr->primary = fx.status;
}

const char *combo_name(int mask) {
    if (mask == 0) return "";
    ComboFx fx = combo_compute(mask);
    return fx.tag ? fx.tag : "";
}

uint32_t combo_color(int mask) {
    if (mask == 0) return 0xFFFFFFFF;
    ComboFx fx = combo_compute(mask);
    return fx.color;
}

int combo_table_count(void) {
    int n = 0;
    for (const ComboName *c = COMBO_NAMES; c->name != NULL; c++) n++;
    return n;
}

bool combo_table_get(int i, int *out_mask, const char **out_name, uint32_t *out_color) {
    int n = combo_table_count();
    if (i < 0 || i >= n) return false;
    const ComboName *c = &COMBO_NAMES[i];
    if (out_mask)  *out_mask  = c->mask;
    if (out_name)  *out_name  = c->name;
    if (out_color) *out_color = c->color;
    return true;
}

int combo_mask_element_count(int mask) {
    int n = 0;
    for (int b = 1; b < 32; b++) if (mask & (1 << b)) n++;
    return n;
}
