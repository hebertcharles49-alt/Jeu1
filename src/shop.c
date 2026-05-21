/*
 * shop.c - boutique : recettes data-driven, double-edged
 */
#include "game.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* On utilise des designators partiels pour les ~38 recettes existantes :
 * les nouveaux champs ajoutes a la struct (ex shrine_only) restent en zero.
 * GCC le signale via -Wmissing-field-initializers, mais c'est garanti par
 * la norme C99 ; on silence ce warning specifique pour ce fichier. */
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif

typedef struct {
    const char *name;
    const char *desc;
    int    cost;
    Rarity rarity;
    /* effets additifs (stat += d_*) */
    float d_maxhp, d_speed, d_armor;
    float d_dmg_mul, d_lifesteal, d_regen;
    float d_flat, d_melee, d_range;
    float d_elem, d_atk_speed;
    float d_crit_chance, d_crit_dmg;
    float d_range_mul, d_dodge;
    int   d_aff_el;
    float d_aff_val;
    int   d_aff_el2;
    float d_aff_val2;
    int   shrine_only;     /* 1 = ne peut etre obtenu que via PU_SHRINE */
    float d_shop_discount; /* +X% reduction sur les prix du shop (0.10 = -10%) */
    float d_reroll_discount;/* +X% reduction specifique au reroll */
    int   d_inv_bonus;     /* +N slots inventaire */
    float d_dmg_vs_cat[ENEMY_CAT_COUNT]; /* +X% dmg par categorie ennemi */
    /* utility / counter trinkets */
    float d_coin_drop;     /* +X% drop coin (additif au mul) */
    float d_xp_mul;        /* +X% XP */
    int   d_pixie_on_kill_pct;/* +X% chance pixie / kill */
    int   d_puddle_on_room;/* Element pose dans la salle clear (EL_NONE=off) */
    int   d_reroll_coupons;/* +N coupons reroll instantanes */
    int   d_next_buy_free; /* +N achats gratuits charges */
    int   d_next_buy_double;/* +N achats double effet charges */
    int   d_free_hit_chg;  /* +N coups gratuits charges (free_hit_t mis a 60s) */
    int   d_no_atk_cap;    /* 1 = bypass le plancher de cd minimum */
} Recipe;

static const Recipe RECIPES[] = {
    /* === Trinkets shop : valeurs divisees par 3 (arrondi) par rapport
     * a l'ancienne version pour rester ramassables sans casser la
     * balance. Les boosts elementaires (d_elem et d_aff_el) ont ete
     * RETIRES (Loupe ardente / Talisman feu / eau / Sceau foudroyant
     * / Chaine d'acier / Talisman du Vide / Eau benie / Bracelet de
     * la fee / Pacte du Necromancien / Pierre du dragon / Larme du
     * Cristal) -- ils trivialisaient les builds. */
    /* ---- COMMUN (8-15g) ---- */
    { "Bandage",            "+0.2 regen / s",                    8,  R_COMMON,
      0,0,0, 0,0,0.2f,  0,0,0, 0,0,  0,0,  0,0, 0,0, 0,0 },
    { "Pierre tranchante",  "+1 degats plats",                   10, R_COMMON,
      0,0,0, 0,0,0,    1.f,0,0, 0,0,  0,0,  0,0, 0,0, 0,0 },
    { "Cuir tanne",         "+1 armure",                         10, R_COMMON,
      0,0,1.f, 0,0,0,  0,0,0, 0,0,  0,0,  0,0, 0,0, 0,0 },
    { "Cape ample",         "+3% esquive",                        8, R_COMMON,
      0,0,0, 0,0,0,    0,0,0, 0,0,  0,0,  0,0.03f, 0,0, 0,0 },
    { "Coeur frais",        "+5 PV max",                         12, R_COMMON,
      5.f,0,0, 0,0,0, 0,0,0, 0,0,  0,0,  0,0, 0,0, 0,0 },
    { "Champignon noir",    "+5 PV, -3% degats",                 12, R_COMMON,
      5.f,0,0, -0.03f,0,0, 0,0,0, 0,0,  0,0,  0,0, 0,0, 0,0 },
    { "Sangsue tonique",    "+2% vol de vie",                    12, R_COMMON,
      0,0,0, 0,0.02f,0, 0,0,0, 0,0,  0,0,  0,0, 0,0, 0,0 },

    /* ---- MAGIQUE (15-22g) ---- */
    { "Lame lourde",        "+2 dmg plats, -3% atk speed",       15, R_MAGIC,
      0,0,0, 0,0,0,    2.f,0,0, 0,0.03f, 0,0, 0,0, 0,0, 0,0 },
    { "Chausses du loup",   "+4 vitesse",                        12, R_MAGIC,
      0,4.f,0, 0,0,0, 0,0,0, 0,0,  0,0,  0,0, 0,0, 0,0 },
    { "Bottes du fugitif",  "+5 vitesse, +2% esquive",           18, R_MAGIC,
      0,5.f,0, 0,0,0, 0,0,0, 0,0,  0,0,  0,0.02f, 0,0, 0,0 },
    { "Carquois",           "+7% degats distance",               14, R_MAGIC,
      0,0,0, 0,0,0,    0,0,0.07f,0, 0,  0,0,  0,0, 0,0, 0,0 },
    { "Gantelet de fer",    "+8% melee, -3% distance",           20, R_MAGIC,
      0,0,0, 0,0,0,    0,0.08f,-0.03f, 0,0, 0,0, 0,0, 0,0, 0,0 },
    { "Coeur de pierre",    "+1 armure, -2 vitesse",             14, R_MAGIC,
      0,-2.f,1.f, 0,0,0, 0,0,0, 0,0,  0,0,  0,0, 0,0, 0,0 },
    { "Lame ensorcelee",    "+3% degats, +1% vol vie",           22, R_MAGIC,
      0,0,0, 0.03f,0.01f,0, 0,0,0, 0,0, 0,0,  0,0, 0,0, 0,0 },
    { "Pendule du chasseur","+3% crit",                          16, R_MAGIC,
      0,0,0, 0,0,0,    0,0,0, 0,0,  0.03f,0, 0,0, 0,0, 0,0 },
    { "Mains rapides",      "+4% atk speed",                     16, R_MAGIC,
      0,0,0, 0,0,0,    0,0,0, 0,-0.04f, 0,0, 0,0, 0,0, 0,0 },
    { "Long bras",          "+5% portee",                        14, R_MAGIC,
      0,0,0, 0,0,0,    0,0,0, 0,0,  0,0,  0.05f,0, 0,0, 0,0 },
    { "Anneau de verre",    "+17% crit dmg, -2 PV",              10, R_MAGIC,
      -2.f,0,0, 0,0,0, 0,0,0, 0,0,  0,0.17f, 0,0, 0,0, 0,0 },

    /* ---- RARE (20-30g) ---- */
    { "Couronne de fer",    "+1 armure, +3 PV, -3 vitesse",      25, R_RARE,
      3.f,-3.f,1.f, 0,0,0, 0,0,0, 0,0, 0,0, 0,0, 0,0, 0,0 },
    { "Sang de dragon",     "+7% dmg, -0.3 regen",               30, R_RARE,
      0,0,0, 0.07f,0,-0.3f, 0,0,0, 0,0, 0,0, 0,0, 0,0, 0,0 },
    { "Pacte sombre",       "+10% dmg, -8 PV max",               22, R_RARE,
      -8.f,0,0, 0.10f,0,0, 0,0,0, 0,0, 0,0, 0,0, 0,0, 0,0 },
    { "Beni",               "+0.3 regen, +3% dmg",               22, R_RARE,
      0,0,0, 0.03f,0,0.3f, 0,0,0, 0,0, 0,0, 0,0, 0,0, 0,0 },
    { "Tonneau de poudre",  "+2 dmg plats, +7% crit dmg",        22, R_RARE,
      0,0,0, 0,0,0,    2.f,0,0, 0,0,  0,0.07f, 0,0, 0,0, 0,0 },
    { "Bague de vampire",   "+3% vol vie, -3 PV max",            28, R_RARE,
      -3.f,0,0, 0,0.03f,0, 0,0,0, 0,0, 0,0, 0,0, 0,0, 0,0 },
    { "Pomme empoisonnee",  "+8% dmg, -0.5 regen",               22, R_RARE,
      0,0,0, 0.08f,0,-0.5f, 0,0,0, 0,0, 0,0, 0,0, 0,0, 0,0 },
    { "Lentille folle",     "+2% crit, +8% crit dmg",            20, R_RARE,
      0,0,0, 0,0,0,    0,0,0, 0,0,  0.02f,0.08f, 0,0, 0,0, 0,0 },

    /* ---- TRINKETS META (effets sur l'economie) ---- */
    { "Pierre brute",       "+2 dmg plats",                       9, R_COMMON,
      0,0,0, 0,0,0,    2.f,0,0, 0,0,  0,0,  0,0, 0,0, 0,0, 0, 0.f },
    { "Marteau d'enclume",  "+4 dmg plats, -2 vitesse",          18, R_MAGIC,
      0,-2.f,0, 0,0,0, 4.f,0,0, 0,0, 0,0, 0,0, 0,0, 0,0, 0, 0.f },
    { "Bourse du marchand", "-5% prix shop",                     16, R_MAGIC,
      0,0,0, 0,0,0, 0,0,0, 0,0,  0,0,  0,0, 0,0, 0,0, 0, 0.05f },
    { "Couronne du brocanteur", "-10% prix shop, +2 dmg plats", 35, R_EPIC,
      0,0,0, 0,0,0, 2.f,0,0, 0,0, 0,0,  0,0, 0,0, 0,0, 0, 0.10f, 0.f, 0, {0} },

    /* ---- TRINKETS REROLL (cumulatifs) ---- */
    { "Des pipes",          "-5% prix reroll",                   10, R_COMMON,
      0,0,0, 0,0,0, 0,0,0, 0,0, 0,0, 0,0, 0,0, 0,0, 0, 0.f, 0.05f, 0, {0} },
    { "Boule de cristal",   "-8% prix reroll",                   18, R_RARE,
      0,0,0, 0,0,0, 0,0,0, 0,0, 0,0, 0,0, 0,0, 0,0, 0, 0.f, 0.08f, 0, {0} },
    { "Coupon de marche",   "+1 reroll gratuit",                  6, R_COMMON,
      0,0,0, 0,0,0, 0,0,0, 0,0, 0,0, 0,0, 0,0, 0,0, 0, 0.f, 0.f, 0, {0},
      0.f, 0.f, 0, 0, 1, 0, 0, 0 },
    { "Liasse de coupons",  "+3 rerolls gratuits",               12, R_MAGIC,
      0,0,0, 0,0,0, 0,0,0, 0,0, 0,0, 0,0, 0,0, 0,0, 0, 0.f, 0.f, 0, {0},
      0.f, 0.f, 0, 0, 3, 0, 0, 0 },

    /* ---- TRINKETS INVENTAIRE ---- */
    { "Sac robuste",        "+1 slot inventaire",                14, R_MAGIC,
      0,0,0, 0,0,0, 0,0,0, 0,0, 0,0, 0,0, 0,0, 0,0, 0, 0.f, 0.f, 1, {0} },
    { "Sacoche du voleur",  "+2 slots inventaire",               22, R_RARE,
      0,0,0, 0,0,0, 0,0,0, 0,0, 0,0, 0,0, 0,0, 0,0, 0, 0.f, 0.f, 2, {0} },
    { "Bourse sans-fond",   "+3 slots inventaire",               40, R_EPIC,
      0,0,0, 0,0,0, 0,0,0, 0,0, 0,0, 0,0, 0,0, 0,0, 0, 0.f, 0.f, 3, {0} },

    /* ---- TRINKETS ANTI-CATEGORIE (cumulatifs) ---- */
    { "Os de hyene",        "+5% dmg vs Betes",                   9, R_COMMON,
      0,0,0, 0,0,0, 0,0,0, 0,0, 0,0, 0,0, 0,0, 0,0, 0, 0.f, 0.f, 0, {0.05f, 0, 0, 0, 0} },
    { "Sceau du Pacte",     "+5% dmg vs Demons",                 10, R_MAGIC,
      0,0,0, 0,0,0, 0,0,0, 0,0, 0,0, 0,0, 0,0, 0,0, 0, 0.f, 0.f, 0, {0, 0.05f, 0, 0, 0} },
    { "Crucifix",           "+5% dmg vs Mort-vivants",           10, R_MAGIC,
      0,0,0, 0,0,0, 0,0,0, 0,0, 0,0, 0,0, 0,0, 0,0, 0, 0.f, 0.f, 0, {0, 0, 0.05f, 0, 0} },
    { "Dague humaine",      "+5% dmg vs Humains",                 9, R_MAGIC,
      0,0,0, 0,0,0, 0,0,0, 0,0, 0,0, 0,0, 0,0, 0,0, 0, 0.f, 0.f, 0, {0, 0, 0, 0.05f, 0} },
    { "Glaive du chasseur", "+8% dmg vs Boss",                   22, R_EPIC,
      0,0,0, 0,0,0, 0,0,0, 0,0, 0,0, 0,0, 0,0, 0,0, 0, 0.f, 0.f, 0, {0, 0, 0, 0, 0.08f} },

    /* ---- TRINKETS UTILITY (effets cumulatifs simples) ---- */
    { "Bourse percee",      "+5% drop d'or",                      8, R_COMMON,
      0,0,0, 0,0,0, 0,0,0, 0,0, 0,0, 0,0, 0,0, 0,0, 0, 0.f, 0.f, 0, {0},
      0.05f, 0.f, 0, 0, 0, 0, 0, 0 },
    { "Cristal du savoir",  "+7% XP",                             10, R_MAGIC,
      0,0,0, 0,0,0, 0,0,0, 0,0, 0,0, 0,0, 0,0, 0,0, 0, 0.f, 0.f, 0, {0},
      0.f, 0.07f, 0, 0, 0, 0, 0, 0 },
    { "Patte de pixie",     "+3% chance de spawn pixie / kill",   12, R_MAGIC,
      0,0,0, 0,0,0, 0,0,0, 0,0, 0,0, 0,0, 0,0, 0,0, 0, 0.f, 0.f, 0, {0},
      0.f, 0.f, 3, 0, 0, 0, 0, 0 },
    { "Carte VIP",          "Prochain achat shop GRATUIT",        14, R_RARE,
      0,0,0, 0,0,0, 0,0,0, 0,0, 0,0, 0,0, 0,0, 0,0, 0, 0.f, 0.f, 0, {0},
      0.f, 0.f, 0, 0, 0, 1, 0, 0 },
    { "Bon de change",      "Prochain achat duplique (x2)",       18, R_RARE,
      0,0,0, 0,0,0, 0,0,0, 0,0, 0,0, 0,0, 0,0, 0,0, 0, 0.f, 0.f, 0, {0},
      0.f, 0.f, 0, 0, 0, 0, 1, 0 },
    { "Anneau du gardien",  "Un coup gratuit toutes les 60s",     28, R_EPIC,
      0,0,0, 0,0,0, 0,0,0, 0,0, 0,0, 0,0, 0,0, 0,0, 0, 0.f, 0.f, 0, {0},
      0.f, 0.f, 0, 0, 0, 0, 0, 1 },
    { "Flacon d'eau",       "Flaque d'eau a chaque salle",         8, R_COMMON,
      0,0,0, 0,0,0, 0,0,0, 0,0, 0,0, 0,0, 0,0, 0,0, 0, 0.f, 0.f, 0, {0},
      0.f, 0.f, 0, EL_WATER, 0, 0, 0, 0 },
    { "Fiole d'huile",      "Flaque de feu a chaque salle",       12, R_MAGIC,
      0,0,0, 0,0,0, 0,0,0, 0,0, 0,0, 0,0, 0,0, 0,0, 0, 0.f, 0.f, 0, {0},
      0.f, 0.f, 0, EL_FIRE, 0, 0, 0, 0 },

    /* ---- EPIQUE / LEGENDAIRE (30+g) ---- */
    { "Coeur de geant",     "+13 PV max, -3% atk speed",         35, R_EPIC,
      13.f,0,0, 0,0,0, 0,0,0, 0,0.03f, 0,0, 0,0, 0,0, 0,0 },
    /* Pacte du Necromancien / Pierre du dragon / Larme du Cristal :
     * RETIRES (boosts elementaires). */
    { "Auriculaire du roi", "+7% dmg, +1 armure, +0.2 regen",    45, R_EPIC,
      0,0,1.f, 0.07f,0,0.2f, 0,0,0, 0,0, 0,0, 0,0, 0,0, 0,0 },
    { "Cle des dieux",      "+5% crit, +17% crit dmg",           50, R_LEGENDARY,
      0,0,0, 0,0,0,    0,0,0, 0,0,  0.05f,0.17f, 0,0, 0,0, 0,0 },
    /* Style Isaac "Soy Milk" : -40% degats par coup mais bypass le cap
     * de cadence => build full atk speed degenere. Designated initializer
     * pour ne pas avoir a compter les 40+ champs de la struct. */
    { .name = "Lait de Soja",
      .desc = "-40% degats / aucune limite atk speed",
      .cost = 60, .rarity = R_LEGENDARY,
      .d_dmg_mul = -0.40f,
      .d_no_atk_cap = 1 },

    /* ---- PACTES (PU_SHRINE only) ----
     * Bonus enormes + malus reels. Decision irreversible pour la run.
     * Le dernier champ shrine_only=1 retire la recette du pool shop standard. */
    { "Pacte du Sang",       "+30% degats / -25 PV max",         0, R_RARE,
      -25.f,0,0, 0.30f,0,0, 0,0,0, 0,0,  0,0,  0,0, 0,0, 0,0, 1 },
    { "Pacte Glacial",       "+50% chance crit / -15% degats",    0, R_RARE,
      0,0,0, -0.15f,0,0, 0,0,0, 0,0,  0.50f,0, 0,0, 0,0, 0,0, 1 },
    { "Pacte du Vide",       "+15% vol vie / -1.5 regen",         0, R_RARE,
      0,0,0, 0,0.15f,-1.5f, 0,0,0, 0,0, 0,0, 0,0, 0,0, 0,0, 1 },
    { "Pacte du Vent",       "+35 vitesse / -10 PV max",          0, R_RARE,
      -10.f,35.f,0, 0,0,0, 0,0,0, 0,0,  0,0,  0,0, 0,0, 0,0, 1 },
    { "Pacte Berserk",       "+25% atk speed / +20% dmg / -2 armure", 0, R_EPIC,
      0,0,-2.f, 0.20f,0,0, 0,0,0, 0,-0.25f, 0,0, 0,0, 0,0, 0,0, 1 },
    { "Pacte du Feu Noir",   "+40% degats elem / +15% degats pris",0, R_EPIC,
      0,0,-3.f, 0,0,0, 0,0,0, 0.40f,0, 0,0, 0,0, 0,0, 0,0, 1 },
};
#define NUM_RECIPES ((int)(sizeof(RECIPES) / sizeof(RECIPES[0])))

int shop_recipe_grants_free_hit(int rid) {
    if (rid < 0 || rid >= NUM_RECIPES) return 0;
    return RECIPES[rid].d_free_hit_chg > 0 ? 1 : 0;
}

int shop_recipe_count(void) { return NUM_RECIPES; }

const char *shop_recipe_name(int rid) {
    if (rid < 0 || rid >= NUM_RECIPES) return "?";
    return RECIPES[rid].name;
}

const char *shop_recipe_desc(int rid) {
    if (rid < 0 || rid >= NUM_RECIPES) return "?";
    return RECIPES[rid].desc;
}

int shop_recipe_cost(int rid) {
    if (rid < 0 || rid >= NUM_RECIPES) return 999;
    return RECIPES[rid].cost;
}

uint32_t shop_recipe_color(int rid) {
    if (rid < 0 || rid >= NUM_RECIPES) return 0xCCCCCCFF;
    return rarity_color(RECIPES[rid].rarity);
}

/* Nouvelle API : accumule dans un StatBlock unique au lieu de 15 pointeurs.
 * Cette signature est stable (StatBlock peut grandir sans casser l API). */
void shop_recipe_apply_to_block(int rid, StatBlock *sb) {
    if (!sb) return;
    if (rid < 0 || rid >= NUM_RECIPES) return;
    const Recipe *r = &RECIPES[rid];
    sb->maxhp     += r->d_maxhp;
    sb->speed     += r->d_speed;
    sb->armor     += r->d_armor;
    sb->dmg_mul   += r->d_dmg_mul;
    sb->lifesteal += r->d_lifesteal;
    sb->regen     += r->d_regen;
    sb->flat_dmg  += r->d_flat;
    sb->melee     += r->d_melee;
    sb->range     += r->d_range;
    sb->elem      += r->d_elem;
    /* Convention unifiee : atk_speed est multiplicatif (cd arme * x).
     * d_atk_speed est une FRACTION : 0.10 = +10% rate, donc sb *= 0.90. */
    if (r->d_atk_speed != 0.f) sb->atk_speed *= (1.f - r->d_atk_speed);
    sb->crit_chance += r->d_crit_chance;
    sb->crit_dmg    += r->d_crit_dmg;
    sb->range_mul   += r->d_range_mul;
    sb->dodge       += r->d_dodge;
    sb->shop_discount += r->d_shop_discount;
    sb->reroll_discount += r->d_reroll_discount;
    sb->inv_capacity_bonus += r->d_inv_bonus;
    for (int ci = 0; ci < ENEMY_CAT_COUNT; ci++)
        sb->dmg_vs_cat[ci] += r->d_dmg_vs_cat[ci];
    sb->coin_drop_mul       += r->d_coin_drop;
    sb->xp_mul              += r->d_xp_mul;
    sb->pixie_on_kill_pct   += r->d_pixie_on_kill_pct;
    if (r->d_no_atk_cap) sb->no_atk_speed_cap = true;
    if (r->d_puddle_on_room > EL_NONE && r->d_puddle_on_room < EL_COUNT)
        sb->puddle_on_room = (Element)r->d_puddle_on_room;
    if (r->d_aff_el  > 0 && r->d_aff_el  < EL_COUNT) sb->aff[r->d_aff_el]  += r->d_aff_val;
    if (r->d_aff_el2 > 0 && r->d_aff_el2 < EL_COUNT) sb->aff[r->d_aff_el2] += r->d_aff_val2;
}

/* generation aleatoire selon l'etage : raretes plus elevees plus tard.
 * Filtre les recettes deja achetees dans la run (one-shot par trinket). */
static bool recipe_already_owned(Game *g, int rid) {
    if (!g) return false;
    for (int i = 0; i < g->player.shop_purchased_count; i++)
        if (g->player.shop_purchased[i] == rid) return true;
    return false;
}

static int pick_recipe_for_floor(Game *g, int floor_idx) {
    int weight_total = 0;
    int weights[NUM_RECIPES];
    for (int i = 0; i < NUM_RECIPES; i++) {
        if (recipe_already_owned(g, i)) { weights[i] = 0; continue; }
        Rarity r = RECIPES[i].rarity;
        int w = 0;
        switch (r) {
            case R_COMMON:    w = (floor_idx <= 3) ? 60 : (floor_idx <= 6) ? 30 : 15; break;
            case R_MAGIC:     w = (floor_idx <= 3) ? 30 : (floor_idx <= 6) ? 40 : 25; break;
            case R_RARE:      w = (floor_idx <= 3) ? 8  : (floor_idx <= 6) ? 25 : 35; break;
            case R_EPIC:      w = (floor_idx <= 3) ? 2  : (floor_idx <= 6) ? 5  : 18; break;
            case R_LEGENDARY: w = (floor_idx <= 6) ? 0  : 7; break;
            default: w = 0;
        }
        weights[i] = w;
        weight_total += w;
    }
    /* Si toutes les recettes du tier sont epuisees : fallback sur la
     * premiere recette dispo, sinon 0. Evite un crash dans le pool vide. */
    if (weight_total == 0) {
        for (int i = 0; i < NUM_RECIPES; i++)
            if (!recipe_already_owned(g, i)) return i;
        return 0;
    }
    int roll = rand() % weight_total;
    int acc = 0;
    for (int i = 0; i < NUM_RECIPES; i++) {
        acc += weights[i];
        if (roll < acc) return i;
    }
    return 0;
}

void shop_generate(Game *g) {
    g->shop_visits++;
    for (int i = 0; i < SHOP_SLOTS; i++) {
        ShopItem *si = &g->shop_items[i];
        memset(si, 0, sizeof(*si));
        si->recipe_id = pick_recipe_for_floor(g, g->floor_index);
        /* anti-duplicate intra-shop : evite que le meme trinket apparaisse
         * 2 fois dans la meme salle (les 4 slots doivent etre distincts). */
        for (int k = 0; k < i; k++) {
            int retries = 8;
            while (retries-- > 0 && g->shop_items[k].recipe_id == si->recipe_id) {
                si->recipe_id = pick_recipe_for_floor(g, g->floor_index);
            }
        }
        /* cost ajuste selon etage, puis reduction shop_discount du joueur.
         * Si discount >= 100% le cout passe a 0 (shop gratuit). */
        int base_cost = shop_recipe_cost(si->recipe_id);
        int raw = base_cost + g->floor_index * 2;
        si->cost = (int)(raw * (1.f - g->player.shop_discount));
        if (si->cost < 0) si->cost = 0;
    }
    g->shop_cursor = 0;
    g->shop_reroll_idx = 0;
    int rc = shop_reroll_cost_at(0);
    float disc = g->player.shop_discount + g->player.reroll_discount;
    if (disc > 1.f) disc = 1.f;
    rc = (int)(rc * (1.f - disc));
    if (rc < 0) rc = 0;
    g->shop_reroll_cost = rc;
}

/* Cout du reroll : Fibonacci pur a partir de 3, 3.
 * Sequence : 3 3 6 9 15 24 39 63 102 165 267 432 ...
 * Pas de cycle : la progression est exponentielle, l'economie
 * naturelle limite l'usage. */
int shop_reroll_cost_at(int idx) {
    if (idx < 0) idx = 0;
    int a = 3, b = 3;
    for (int i = 0; i < idx; i++) {
        int c = a + b;
        a = b;
        b = c;
        /* cap a INT_MAX/2 pour rester safe : Fibonacci(45) ~ 2 Mds */
        if (b > 1000000000) b = 1000000000;
    }
    return a;
}

void shop_reroll(Game *g) {
    /* coupon prioritaire : consomme et ne touche pas le cycle Fibonacci */
    if (g->player.reroll_coupons > 0) {
        g->player.reroll_coupons--;
        sfx_play(g, SFX_COIN);
        int idx = g->shop_reroll_idx;
        int prev_visits = g->shop_visits;
        shop_generate(g);
        g->shop_reroll_idx = idx;
        g->shop_visits = prev_visits;
        return;
    }
    if (g->player.coins < g->shop_reroll_cost) return;
    g->player.coins -= g->shop_reroll_cost;
    sfx_play(g, SFX_COIN);
    int idx = g->shop_reroll_idx + 1;
    int prev_visits = g->shop_visits;
    shop_generate(g);
    /* shop_generate reset shop_reroll_idx ; on le repose pour preserver
     * la progression du cycle au sein du shop courant. */
    g->shop_reroll_idx = idx;
    g->shop_visits = prev_visits;
    int rc = shop_reroll_cost_at(idx);
    rc = (int)(rc * (1.f - g->player.shop_discount));
    if (rc < 1) rc = 1;
    g->shop_reroll_cost = rc;
}

void shop_buy(Game *g, int idx) {
    if (idx < 0 || idx >= SHOP_SLOTS) return;
    ShopItem *si = &g->shop_items[idx];
    if (si->bought) return;
    /* charge "next_buy_free" : ce slot devient gratuit, consume la charge. */
    int real_cost = si->cost;
    if (g->player.next_buy_free > 0) { real_cost = 0; g->player.next_buy_free--; }
    if (g->player.coins < real_cost) return;
    g->player.coins -= real_cost;
    si->bought = true;
    sfx_play(g, SFX_COIN);

    /* counters one-shot : on les applique direct sur le joueur. La
     * recette est aussi enregistree dans shop_purchased pour que les
     * effets STAT (route via StatBlock) cumulent. */
    const Recipe *r = (si->recipe_id >= 0 && si->recipe_id < NUM_RECIPES)
                       ? &RECIPES[si->recipe_id] : NULL;
    int times = (g->player.next_buy_double > 0) ? 2 : 1;
    if (g->player.next_buy_double > 0) g->player.next_buy_double--;
    for (int rep = 0; rep < times; rep++) {
        if (g->player.shop_purchased_count < 64) {
            g->player.shop_purchased[g->player.shop_purchased_count++] = si->recipe_id;
        }
        if (r) {
            g->player.reroll_coupons   += r->d_reroll_coupons;
            g->player.next_buy_free    += r->d_next_buy_free;
            g->player.next_buy_double  += r->d_next_buy_double;
            if (r->d_free_hit_chg > 0) {
                /* met le timer a 0 : eligible des le prochain coup */
                if (g->player.free_hit_t > 0.f) g->player.free_hit_t = 0.f;
            }
        }
    }
    game_recompute_player_stats(g);
}

void shop_apply_recipe(Game *g, int recipe_id) {
    if (g->player.shop_purchased_count < 64) {
        g->player.shop_purchased[g->player.shop_purchased_count++] = recipe_id;
    }
    game_recompute_player_stats(g);
}

int shop_sell_item(Game *g, int inv_index) {
    if (inv_index < 0 || inv_index >= INVENTORY_SLOTS) return 0;
    Item *it = &g->player.inventory[inv_index];
    if (!it->occupied) return 0;
    int v = item_sell_value(it);
    if (v <= 0) return 0;
    g->player.coins += v;
    /* unmark si fusion */
    for (int i = 0; i < g->inv_marked_count; i++) {
        if (g->inv_marked[i] == inv_index) {
            for (int j = i; j < g->inv_marked_count - 1; j++)
                g->inv_marked[j] = g->inv_marked[j + 1];
            g->inv_marked_count--;
            break;
        }
    }
    it->occupied = false;
    sfx_play(g, SFX_COIN);
    return v;
}
