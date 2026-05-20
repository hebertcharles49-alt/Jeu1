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
} Recipe;

static const Recipe RECIPES[] = {
    /* ---- COMMUN (8-15g) ---- */
    { "Bandage",            "+0.6 regen / s",                    8,  R_COMMON,
      0,0,0, 0,0,0.6f,  0,0,0, 0,0,  0,0,  0,0, 0,0, 0,0 },
    { "Pierre tranchante",  "+3 degats plats",                   10, R_COMMON,
      0,0,0, 0,0,0,    3.f,0,0, 0,0,  0,0,  0,0, 0,0, 0,0 },
    { "Cuir tanne",         "+1 armure",                         10, R_COMMON,
      0,0,1.f, 0,0,0,  0,0,0, 0,0,  0,0,  0,0, 0,0, 0,0 },
    { "Cape ample",         "+8% esquive",                        8, R_COMMON,
      0,0,0, 0,0,0,    0,0,0, 0,0,  0,0,  0,0.08f, 0,0, 0,0 },
    { "Coeur frais",        "+15 PV max",                        12, R_COMMON,
      15.f,0,0, 0,0,0, 0,0,0, 0,0,  0,0,  0,0, 0,0, 0,0 },
    { "Champignon noir",    "+15 PV, -8% degats",                12, R_COMMON,
      15.f,0,0, -0.08f,0,0, 0,0,0, 0,0,  0,0,  0,0, 0,0, 0,0 },
    { "Sangsue tonique",    "+5% vol de vie",                    12, R_COMMON,
      0,0,0, 0,0.05f,0, 0,0,0, 0,0,  0,0,  0,0, 0,0, 0,0 },

    /* ---- MAGIQUE (15-22g) ---- */
    { "Lame lourde",        "+6 dmg plats, -10% atk speed",      15, R_MAGIC,
      0,0,0, 0,0,0,    6.f,0,0, 0,0.10f, 0,0, 0,0, 0,0, 0,0 },
    { "Chausses du loup",   "+12 vitesse",                       12, R_MAGIC,
      0,12.f,0, 0,0,0, 0,0,0, 0,0,  0,0,  0,0, 0,0, 0,0 },
    { "Bottes du fugitif",  "+15 vitesse, +5% esquive",          18, R_MAGIC,
      0,15.f,0, 0,0,0, 0,0,0, 0,0,  0,0,  0,0.05f, 0,0, 0,0 },
    { "Loupe ardente",      "+25% degats elem",                  18, R_MAGIC,
      0,0,0, 0,0,0,    0,0,0, 0.25f,0, 0,0, 0,0, 0,0, 0,0 },
    { "Carquois",           "+20% degats distance",              14, R_MAGIC,
      0,0,0, 0,0,0,    0,0,0.20f,0, 0,  0,0,  0,0, 0,0, 0,0 },
    { "Gantelet de fer",    "+25% melee, -10% distance",         20, R_MAGIC,
      0,0,0, 0,0,0,    0,0.25f,-0.10f, 0,0, 0,0, 0,0, 0,0, 0,0 },
    { "Coeur de pierre",    "+1 armure, -5 vitesse",             14, R_MAGIC,
      0,-5.f,1.f, 0,0,0, 0,0,0, 0,0,  0,0,  0,0, 0,0, 0,0 },
    { "Lame ensorcelee",    "+10% degats, +3% vol vie",          22, R_MAGIC,
      0,0,0, 0.10f,0.03f,0, 0,0,0, 0,0, 0,0,  0,0, 0,0, 0,0 },
    { "Talisman de feu",    "+30% Feu, -30% Eau",                10, R_MAGIC,
      0,0,0, 0,0,0,    0,0,0, 0,0,  0,0,  0,0, EL_FIRE,0.30f, EL_WATER,-0.30f },
    { "Talisman d'eau",     "+30% Eau, -30% Feu",                10, R_MAGIC,
      0,0,0, 0,0,0,    0,0,0, 0,0,  0,0,  0,0, EL_WATER,0.30f, EL_FIRE,-0.30f },
    { "Pendule du chasseur","+8% crit",                          16, R_MAGIC,
      0,0,0, 0,0,0,    0,0,0, 0,0,  0.08f,0, 0,0, 0,0, 0,0 },
    { "Mains rapides",      "+12% atk speed",                    16, R_MAGIC,
      0,0,0, 0,0,0,    0,0,0, 0,-0.12f, 0,0, 0,0, 0,0, 0,0 },
    { "Long bras",          "+15% portee, +1 dmg",               14, R_MAGIC,
      0,0,0, 0,0,0,    1.f,0,0, 0,0,  0,0,  0.15f,0, 0,0, 0,0 },
    { "Anneau de verre",    "+50% crit dmg, -5 PV",              10, R_MAGIC,
      -5.f,0,0, 0,0,0, 0,0,0, 0,0,  0,0.50f, 0,0, 0,0, 0,0 },

    /* ---- RARE (20-30g) ---- */
    { "Couronne de fer",    "+2 armure, +10 PV, -10 vitesse",    25, R_RARE,
      10.f,-10.f,2.f, 0,0,0, 0,0,0, 0,0, 0,0, 0,0, 0,0, 0,0 },
    { "Sang de dragon",     "+20% dmg, -1 regen",                30, R_RARE,
      0,0,0, 0.20f,0,-1.f, 0,0,0, 0,0, 0,0, 0,0, 0,0, 0,0 },
    { "Pacte sombre",       "+30% dmg, -25 PV max",              22, R_RARE,
      -25.f,0,0, 0.30f,0,0, 0,0,0, 0,0, 0,0, 0,0, 0,0, 0,0 },
    { "Beni",               "+0.8 regen, +10% dmg",              22, R_RARE,
      0,0,0, 0.10f,0,0.8f, 0,0,0, 0,0, 0,0, 0,0, 0,0, 0,0 },
    { "Tonneau de poudre",  "+6 dmg plats, +20% crit dmg",       22, R_RARE,
      0,0,0, 0,0,0,    6.f,0,0, 0,0,  0,0.20f, 0,0, 0,0, 0,0 },
    { "Bague de vampire",   "+10% vol vie, -10 PV max",          28, R_RARE,
      -10.f,0,0, 0,0.10f,0, 0,0,0, 0,0, 0,0, 0,0, 0,0, 0,0 },
    { "Pomme empoisonnee",  "+25% dmg, -1.5 regen",              22, R_RARE,
      0,0,0, 0.25f,0,-1.5f, 0,0,0, 0,0, 0,0, 0,0, 0,0, 0,0 },
    { "Sceau foudroyant",   "+20% Foudre, +8% atk speed",        20, R_RARE,
      0,0,0, 0,0,0,    0,0,0, 0,-0.08f, 0,0, 0,0, EL_LIGHTNING,0.20f, 0,0 },
    { "Chaine d'acier",     "+30% Acier, +1 armure",             22, R_RARE,
      0,0,1.f, 0,0,0, 0,0,0, 0,0,  0,0,  0,0, EL_STEEL,0.30f, 0,0 },
    { "Talisman du Vide",   "+25% Vide, +25% Tenebres",          24, R_RARE,
      0,0,0, 0,0,0,    0,0,0, 0,0,  0,0,  0,0, EL_VOID,0.25f, EL_DARK,0.25f },
    { "Eau benie",          "+25% Sacre, +5% esquive",           20, R_RARE,
      0,0,0, 0,0,0,    0,0,0, 0,0,  0,0,  0,0.05f, EL_HOLY,0.25f, 0,0 },
    { "Bracelet de la fee", "+20% Fee, +0.4 regen",              18, R_RARE,
      0,0,0, 0,0,0.4f, 0,0,0, 0,0,  0,0,  0,0, EL_FAE,0.20f, 0,0 },
    { "Lentille folle",     "+5% crit, +25% crit dmg",           20, R_RARE,
      0,0,0, 0,0,0,    0,0,0, 0,0,  0.05f,0.25f, 0,0, 0,0, 0,0 },

    /* ---- TRINKETS META (effets sur l'economie) ---- */
    { "Pierre brute",       "+5 dmg plats",                       9, R_COMMON,
      0,0,0, 0,0,0,    5.f,0,0, 0,0,  0,0,  0,0, 0,0, 0,0, 0, 0.f },
    { "Marteau d'enclume",  "+12 dmg plats, -5 vitesse",         18, R_MAGIC,
      0,-5.f,0, 0,0,0, 12.f,0,0, 0,0, 0,0, 0,0, 0,0, 0,0, 0, 0.f },
    { "Bourse du marchand", "-15% prix shop",                    16, R_MAGIC,
      0,0,0, 0,0,0, 0,0,0, 0,0,  0,0,  0,0, 0,0, 0,0, 0, 0.15f },
    { "Couronne du brocanteur", "-30% prix shop, +5 dmg plats", 35, R_EPIC,
      0,0,0, 0,0,0, 5.f,0,0, 0,0, 0,0,  0,0, 0,0, 0,0, 0, 0.30f, 0.f, 0, {0} },

    /* ---- TRINKETS REROLL (sequence Fibonacci en plein vol) ---- */
    { "Des pipes",          "-25% prix reroll",                  12, R_COMMON,
      0,0,0, 0,0,0, 0,0,0, 0,0, 0,0, 0,0, 0,0, 0,0, 0, 0.f, 0.25f, 0, {0} },
    { "Boule de cristal",   "-50% prix reroll",                  24, R_RARE,
      0,0,0, 0,0,0, 0,0,0, 0,0, 0,0, 0,0, 0,0, 0,0, 0, 0.f, 0.50f, 0, {0} },
    { "Cle des secrets",    "Reroll gratuit, +10g a l'achat",   40, R_EPIC,
      0,0,0, 0,0,0, 0,0,0, 0,0, 0,0, 0,0, 0,0, 0,0, 0, 0.f, 1.00f, 0, {0} },

    /* ---- TRINKETS INVENTAIRE ---- */
    { "Sac robuste",        "+2 slots inventaire",               14, R_MAGIC,
      0,0,0, 0,0,0, 0,0,0, 0,0, 0,0, 0,0, 0,0, 0,0, 0, 0.f, 0.f, 2, {0} },
    { "Sacoche du voleur",  "+4 slots inventaire",               22, R_RARE,
      0,0,0, 0,0,0, 0,0,0, 0,0, 0,0, 0,0, 0,0, 0,0, 0, 0.f, 0.f, 4, {0} },
    { "Bourse sans-fond",   "+8 slots inventaire",               40, R_EPIC,
      0,0,0, 0,0,0, 0,0,0, 0,0, 0,0, 0,0, 0,0, 0,0, 0, 0.f, 0.f, 8, {0} },

    /* ---- TRINKETS ANTI-CATEGORIE (par categorie d'ennemi) ---- */
    { "Os de hyene",        "+30% dmg vs Betes",                 12, R_COMMON,
      0,0,0, 0,0,0, 0,0,0, 0,0, 0,0, 0,0, 0,0, 0,0, 0, 0.f, 0.f, 0, {0.30f, 0, 0, 0, 0} },
    { "Sceau du Pacte",     "+30% dmg vs Demons",                14, R_MAGIC,
      0,0,0, 0,0,0, 0,0,0, 0,0, 0,0, 0,0, 0,0, 0,0, 0, 0.f, 0.f, 0, {0, 0.30f, 0, 0, 0} },
    { "Crucifix",           "+30% dmg vs Mort-vivants",          14, R_MAGIC,
      0,0,0, 0,0,0, 0,0,0, 0,0, 0,0, 0,0, 0,0, 0,0, 0, 0.f, 0.f, 0, {0, 0, 0.30f, 0, 0} },
    { "Dague humaine",      "+30% dmg vs Humains",               12, R_MAGIC,
      0,0,0, 0,0,0, 0,0,0, 0,0, 0,0, 0,0, 0,0, 0,0, 0, 0.f, 0.f, 0, {0, 0, 0, 0.30f, 0} },
    { "Glaive du chasseur", "+50% dmg vs Boss",                  30, R_EPIC,
      0,0,0, 0,0,0, 0,0,0, 0,0, 0,0, 0,0, 0,0, 0,0, 0, 0.f, 0.f, 0, {0, 0, 0, 0, 0.50f} },

    /* ---- EPIQUE / LEGENDAIRE (30+g) ---- */
    { "Coeur de geant",     "+40 PV max, -10% atk speed",        35, R_EPIC,
      40.f,0,0, 0,0,0, 0,0,0, 0,0.10f, 0,0, 0,0, 0,0, 0,0 },
    { "Pacte du Necromancien","+15% vol vie, +20% Tenebres",     38, R_EPIC,
      0,0,0, 0,0.15f,0, 0,0,0, 0,0,  0,0,  0,0, EL_DARK,0.20f, 0,0 },
    { "Auriculaire du roi", "+20% dmg, +1 armure, +0.5 regen",   45, R_EPIC,
      0,0,1.f, 0.20f,0,0.5f, 0,0,0, 0,0, 0,0, 0,0, 0,0, 0,0 },
    { "Pierre du dragon",   "+50% Feu, +20 PV, -20% Eau",        45, R_EPIC,
      20.f,0,0, 0,0,0, 0,0,0, 0,0,  0,0,  0,0, EL_FIRE,0.50f, EL_WATER,-0.20f },
    { "Cle des dieux",      "+15% crit, +50% crit dmg",          50, R_LEGENDARY,
      0,0,0, 0,0,0,    0,0,0, 0,0,  0.15f,0.50f, 0,0, 0,0, 0,0 },
    { "Larme du Cristal",   "+30% degats elem, +15% portee",     55, R_LEGENDARY,
      0,0,0, 0,0,0,    0,0,0, 0.30f,0, 0,0,  0.15f,0, 0,0, 0,0 },

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
    if (r->d_aff_el  > 0 && r->d_aff_el  < EL_COUNT) sb->aff[r->d_aff_el]  += r->d_aff_val;
    if (r->d_aff_el2 > 0 && r->d_aff_el2 < EL_COUNT) sb->aff[r->d_aff_el2] += r->d_aff_val2;
}

/* generation aleatoire selon l'etage : raretes plus elevees plus tard */
static int pick_recipe_for_floor(int floor_idx) {
    int weight_total = 0;
    int weights[NUM_RECIPES];
    for (int i = 0; i < NUM_RECIPES; i++) {
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
    if (weight_total == 0) return 0;
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
        si->recipe_id = pick_recipe_for_floor(g->floor_index);
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
    if (g->player.coins < si->cost) return;
    g->player.coins -= si->cost;
    si->bought = true;
    sfx_play(g, SFX_COIN);
    /* enregistre l'achat sur le joueur (effet cumulatif) */
    if (g->player.shop_purchased_count < 64) {
        g->player.shop_purchased[g->player.shop_purchased_count++] = si->recipe_id;
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
