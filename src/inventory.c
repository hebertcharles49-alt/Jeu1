/*
 * inventory.c - inventaire 12 slots, equipement 6 slots, fusion, raretes
 */
#include "game.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

const char *slot_name(EquipSlot s) {
    switch (s) {
        case SLOT_HELM:   return "Casque";
        case SLOT_CHEST:  return "Torse";
        case SLOT_LEGS:   return "Jambes";
        case SLOT_BOOTS:  return "Bottes";
        case SLOT_BELT:   return "Ceinture";
        case SLOT_GLOVES: return "Gants";
        default:          return "—";
    }
}

const char *item_kind_name(EquipSlot s) { return slot_name(s); }

const char *rarity_name(Rarity r) {
    switch (r) {
        case R_COMMON:    return "Commun";
        case R_MAGIC:     return "Magique";
        case R_RARE:      return "Rare";
        case R_EPIC:      return "Epique";
        case R_LEGENDARY: return "Legendaire";
        default:          return "?";
    }
}

uint32_t rarity_color(Rarity r) {
    switch (r) {
        case R_COMMON:    return 0xCCCCCCFF;
        case R_MAGIC:     return 0x6090FFFF;
        case R_RARE:      return 0xFFD040FF;
        case R_EPIC:      return 0xC080FFFF;
        case R_LEGENDARY: return 0xFF8030FF;
        default:          return 0xFFFFFFFF;
    }
}

/* multiplicateur de stat par rarete : commune = 1.0, legendaire = 2.0 */
float rarity_mul(Rarity r) {
    switch (r) {
        case R_COMMON:    return 1.00f;
        case R_MAGIC:     return 1.25f;
        case R_RARE:      return 1.50f;
        case R_EPIC:      return 1.75f;
        case R_LEGENDARY: return 2.00f;
        default:          return 1.f;
    }
}

/* base stat per slot (commun, base_kind = 0) */
static float slot_base_stat(EquipSlot s) {
    switch (s) {
        case SLOT_HELM:   return 10.f;   /* +PV max */
        case SLOT_CHEST:  return 1.5f;   /* +armure */
        case SLOT_LEGS:   return 8.f;    /* +vitesse */
        case SLOT_BOOTS:  return 0.05f;  /* +5% reduc dash cd, encode comme fraction */
        case SLOT_BELT:   return 0.5f;   /* +regen / sec */
        case SLOT_GLOVES: return 0.06f;  /* +6% degats */
        default:          return 0.f;
    }
}

const char *slot_stat_unit(EquipSlot s) {
    switch (s) {
        case SLOT_HELM:   return "PV";
        case SLOT_CHEST:  return "ARM";
        case SLOT_LEGS:   return "VIT";
        case SLOT_BOOTS:  return "DASH";
        case SLOT_BELT:   return "REG";
        case SLOT_GLOVES: return "DMG";
        default:          return "";
    }
}

/* ---- AFFIXES ----
 * Plage [min, max] par AFFIX, additive avec rarete et sub_kind :
 *   val_min = base_min + sub_kind * step
 *   val_max = base_max + sub_kind * step
 * Le roll final est uniforme dans [val_min, val_max], multiplie par
 * rarity_mul(rarity) sauf pour les valeurs deja en % (fractions).
 * Le compte d'affixes vient directement de la rarete. */
typedef struct {
    Affix kind;
    float base_min, base_max, step;
    bool  is_fraction;        /* true = stocke comme 0..1 (affichee en %) */
} AffixDef;

/* Magnitudes augmentees x3 vs ancien design pour que les bonus de
 * rarete soient lisibles en jeu (un Epique Offensif tape +45-55% dmg). */
static const AffixDef AFFIX_DEFS[AFFIX_COUNT] = {
    [AFFIX_NONE]        = {0},
    [AFFIX_HP]          = { AFFIX_HP,           15.f,  22.f, 4.0f,  false },
    [AFFIX_ARMOR]       = { AFFIX_ARMOR,        2.0f,  3.0f, 0.5f,  false },
    [AFFIX_SPEED]       = { AFFIX_SPEED,       10.f,  16.f, 3.0f,  false },
    [AFFIX_DMG_PCT]     = { AFFIX_DMG_PCT,      0.10f, 0.15f, 0.02f, true },
    [AFFIX_CRIT_CHANCE] = { AFFIX_CRIT_CHANCE,  0.06f, 0.10f, 0.014f,true },
    [AFFIX_LIFESTEAL]   = { AFFIX_LIFESTEAL,    0.04f, 0.08f, 0.012f,true },
    [AFFIX_REGEN]       = { AFFIX_REGEN,        0.40f, 0.80f, 0.15f, false },
    [AFFIX_ATK_SPEED]   = { AFFIX_ATK_SPEED,    0.10f, 0.16f, 0.022f,true },
    [AFFIX_DODGE]       = { AFFIX_DODGE,        0.05f, 0.10f, 0.014f,true },
    [AFFIX_RANGE_MUL]   = { AFFIX_RANGE_MUL,    0.10f, 0.18f, 0.022f,true },
    [AFFIX_FLAT_DMG]    = { AFFIX_FLAT_DMG,     4.f,   8.f,  1.2f,  false },
};

const char *affix_name(Affix a) {
    switch (a) {
        case AFFIX_HP:          return "PV";
        case AFFIX_ARMOR:       return "Armure";
        case AFFIX_SPEED:       return "Vitesse";
        case AFFIX_DMG_PCT:     return "Degats";
        case AFFIX_CRIT_CHANCE: return "Crit";
        case AFFIX_LIFESTEAL:   return "Vol vie";
        case AFFIX_REGEN:       return "Regen/s";
        case AFFIX_ATK_SPEED:   return "Vit. atk";
        case AFFIX_DODGE:       return "Esquive";
        case AFFIX_RANGE_MUL:   return "Portee";
        case AFFIX_FLAT_DMG:    return "Dmg flat";
        default:                return "?";
    }
}

void affix_label(const ItemAffix *af, char *buf, int bufsz) {
    if (!af || af->kind == AFFIX_NONE) { snprintf(buf, bufsz, "—"); return; }
    const AffixDef *d = &AFFIX_DEFS[af->kind];
    /* signe explicite : negatif imprime "-X", positif imprime "+X" */
    const char *sign = (af->value >= 0.f) ? "+" : "";
    if (d->is_fraction) {
        snprintf(buf, bufsz, "%s%.1f%% %s", sign, af->value * 100.f,
                 affix_name(af->kind));
    } else {
        snprintf(buf, bufsz, "%s%.1f %s", sign, af->value, affix_name(af->kind));
    }
}

/* ---- Archetypes ----
 * Chaque base_kind (0..4) definit un profil : 3 affixes positifs
 * (utilises selon la rarete) + 1 affixe negatif fixe. */
typedef struct {
    Affix pos[3];
    Affix neg_kind;
    float neg_scale;     /* magnitude du malus (fraction ou flat selon affix) */
} ArchetypeProfile;

static const ArchetypeProfile PROFILES[5] = {
    /* 0 Offensif   */ { {AFFIX_DMG_PCT,   AFFIX_CRIT_CHANCE, AFFIX_CRIT_CHANCE}, AFFIX_HP,        -30.f  },
    /* 1 Defensif   */ { {AFFIX_HP,        AFFIX_ARMOR,       AFFIX_REGEN},       AFFIX_DMG_PCT,   -0.30f },
    /* 2 Mobilite   */ { {AFFIX_SPEED,     AFFIX_DODGE,       AFFIX_ATK_SPEED},   AFFIX_DMG_PCT,   -0.22f },
    /* 3 Vampirique */ { {AFFIX_LIFESTEAL, AFFIX_REGEN,       AFFIX_DMG_PCT},     AFFIX_HP,        -35.f  },
    /* 4 Frenetique */ { {AFFIX_ATK_SPEED, AFFIX_RANGE_MUL,   AFFIX_FLAT_DMG},    AFFIX_ARMOR,     -3.f   },
};
static const char *ARCHETYPE_NAMES[5] = {
    "Offensif", "Defensif", "Mobilite", "Vampirique", "Frenetique"
};

const char *archetype_name(int base_kind) {
    return ARCHETYPE_NAMES[base_kind % 5];
}

/* nombre d'affixes positifs selon la rarete : 0/1/2/3.
 * Plus 1 affixe negatif a partir de R_MAGIC. R_LEGENDARY reste 3 + 1
 * (= 4 affixes au total, MAX_AFFIXES). */
static int affix_pos_count_for_rarity(Rarity r) {
    switch (r) {
        case R_COMMON:    return 0;
        case R_MAGIC:     return 1;
        case R_RARE:      return 2;
        case R_EPIC:      return 3;
        case R_LEGENDARY: return 3;
        default:          return 0;
    }
}

/* roule N affixes positifs depuis le profil (pris dans l ordre) +
 * 1 affixe negatif fixe (sauf Commun). Severite du malus croit avec la rarete. */
static void roll_affixes(Item *it) {
    int n_pos = affix_pos_count_for_rarity(it->rarity);
    it->affix_count = 0;
    if (n_pos == 0) return;

    const ArchetypeProfile *prof = &PROFILES[it->base_kind % 5];
    float rmul = rarity_mul(it->rarity);

    /* affixes positifs : ordre fixe (primary -> secondary -> tertiary) */
    for (int i = 0; i < n_pos && i < MAX_AFFIXES - 1; i++) {
        const AffixDef *d = &AFFIX_DEFS[prof->pos[i]];
        float vmin = d->base_min + (float)it->base_kind * d->step;
        float vmax = d->base_max + (float)it->base_kind * d->step;
        float roll = vmin + (vmax - vmin) * ((float)rand() / (float)RAND_MAX);
        roll *= rmul;
        it->affixes[it->affix_count].kind  = prof->pos[i];
        it->affixes[it->affix_count].value = roll;
        it->affix_count++;
    }
    /* malus : severite croissante par rarete. neg_scale est la magnitude
     * MAX du malus (epic atteint 100% de neg_scale). */
    static const float SEVERITY[] = { 0.f, 0.50f, 0.75f, 1.00f, 1.00f };
    float sev = SEVERITY[(int)it->rarity];
    it->affixes[it->affix_count].kind  = prof->neg_kind;
    it->affixes[it->affix_count].value = prof->neg_scale * sev;
    it->affix_count++;
}

/* Pools pour les noms procedurals.
 * Le nom prend la forme : "<Noun_slot> <Adjective>" pour les
 * communs/magiques, et "<Noun_slot> <Adjective> de <Subject>" pour
 * les rares et au-dessus. */
static const char *NOUN_BY_SLOT[EQUIP_SLOTS] = {
    [SLOT_HELM]   = "Casque",
    [SLOT_CHEST]  = "Cuirasse",
    [SLOT_LEGS]   = "Jambieres",
    [SLOT_BOOTS]  = "Bottes",
    [SLOT_BELT]   = "Ceinture",
    [SLOT_GLOVES] = "Gants",
};
static const char *ADJECTIVES[] = {
    "Rouille", "Forge", "Ancien", "Cisele", "Vermeil",
    "Spectral", "Ombre", "Solaire", "Lunaire", "Brulant",
    "Glace", "Sanglant", "Tempetueux", "Ferrugineux", "Saint",
    "Maudit", "Sauvage", "Royal", "Brise", "Hante",
};
static const char *SUBJECTS[] = {
    "Vipere", "Lion", "Corbeau", "Loup", "Dragon",
    "Tisseur", "Pelerin", "Veuve", "Sentinelle", "Forgeron",
    "Cendre", "Tonnerre", "Marche", "Geant", "Sorcier",
    "Profanateur", "Augure", "Eclat", "Cataclysme", "Ravage",
};
#define N_ADJ      ((int)(sizeof(ADJECTIVES)/sizeof(ADJECTIVES[0])))
#define N_SUBJ     ((int)(sizeof(SUBJECTS)/sizeof(SUBJECTS[0])))

void item_generate_name(Item *it) {
    if (!it || !it->occupied) return;
    /* hash deterministe sur les caracteristiques structurelles : meme item
     * = meme nom, mais 2 drops differents auront en general des noms
     * differents. */
    uint32_t h = (uint32_t)it->slot * 131u
               + (uint32_t)it->rarity * 7919u
               + (uint32_t)it->base_kind * 53u
               + (uint32_t)it->affix_count * 1009u;
    for (int i = 0; i < it->affix_count; i++)
        h = h * 31u + (uint32_t)it->affixes[i].kind * 17u
              + (uint32_t)(it->affixes[i].value * 1000.f);
    const char *noun = (it->slot >= 0 && it->slot < EQUIP_SLOTS)
                         ? NOUN_BY_SLOT[it->slot] : "Objet";
    const char *adj  = ADJECTIVES[h % N_ADJ];
    if (it->rarity >= R_RARE) {
        const char *subj = SUBJECTS[(h / N_ADJ) % N_SUBJ];
        snprintf(it->name, sizeof(it->name), "%s %s de %s", noun, adj, subj);
    } else {
        snprintf(it->name, sizeof(it->name), "%s %s", noun, adj);
    }
}

Item item_make(EquipSlot slot, Rarity rarity, int sub_kind) {
    Item it = {0};
    it.occupied = true;
    it.slot     = slot;
    it.rarity   = rarity;
    it.base_kind= sub_kind;
    it.stat_value = slot_base_stat(slot) * rarity_mul(rarity);
    it.stat_value *= 1.f + sub_kind * 0.05f;
    roll_affixes(&it);
    item_generate_name(&it);
    return it;
}

/* ---------- TABLES DE RARETE PAR ETAGE ---------- */

/* table elite : %commun / magique / rare / epique / legendaire */
static const int ELITE_TABLE[10][R_COUNT] = {
    /* etage 1 */ { 100,  0,  0,  0,  0 },
    /* etage 2 */ { 100,  0,  0,  0,  0 },
    /* etage 3 */ {  75, 25,  0,  0,  0 },
    /* etage 4 */ {  55, 30, 15,  0,  0 },
    /* etage 5 */ {  45, 33, 20,  2,  0 },
    /* etage 6 */ {  30, 40, 25,  5,  0 },
    /* etage 7 */ {  19, 30, 40, 10,  1 },
    /* etage 8 */ {  17, 24, 32, 24,  3 },
    /* etage 9 */ {  15, 18, 25, 30, 12 },
    /* etage 10*/ {   5, 10, 20, 40, 25 }
};

/* table boss : plus genereux que les elites */
static const int BOSS_TABLE[10][R_COUNT] = {
    /* etage 1 */ {  30, 50, 20,  0,  0 },
    /* etage 2 */ {  20, 50, 25,  5,  0 },
    /* etage 3 */ {  10, 45, 35, 10,  0 },
    /* etage 4 */ {   5, 35, 40, 18,  2 },
    /* etage 5 */ {   0, 30, 40, 25,  5 },
    /* etage 6 */ {   0, 25, 35, 30, 10 },
    /* etage 7 */ {   0, 15, 30, 40, 15 },
    /* etage 8 */ {   0, 10, 25, 40, 25 },
    /* etage 9 */ {   0,  5, 20, 40, 35 },
    /* etage 10*/ {   0,  0, 15, 40, 45 }
};

static Rarity roll_table(const int *row) {
    int total = 0;
    for (int i = 0; i < R_COUNT; i++) total += row[i];
    if (total <= 0) return R_COMMON;
    int r = rand() % total;
    int acc = 0;
    for (int i = 0; i < R_COUNT; i++) {
        acc += row[i];
        if (r < acc) return (Rarity)i;
    }
    return R_COMMON;
}

/* Important : on indexe la table par le NOMBRE de biome-floors deja
 * visites dans la run, pas par floor_index. Sinon commencer au sanctuaire
 * (floors 9-10) donnerait du legendaire des le premier biome. Le param
 * 'floor_index' garde son nom pour compat API mais represente desormais
 * "progression effective" (clampe 1..10). */
Rarity rarity_for_floor_elite(int floor_index) {
    int idx = floor_index - 1;
    if (idx < 0) idx = 0;
    if (idx >= 10) idx = 9;
    return roll_table(ELITE_TABLE[idx]);
}

Rarity rarity_for_floor_boss(int floor_index) {
    int idx = floor_index - 1;
    if (idx < 0) idx = 0;
    if (idx >= 10) idx = 9;
    return roll_table(BOSS_TABLE[idx]);
}

Item item_drop_for_floor(Game *g, int floor_index, bool elite, bool boss) {
    /* Les uniques ne sortent plus du random : ils sont scheduled
     * (cf game.h talisman_drops / unique_drops). Cette fonction garde
     * unique_roll_drop pour le mode debug eventuel, mais les drops
     * "naturels" passent par le milestone system. */
    int uid = unique_roll_drop(floor_index, elite, boss);
    (void)uid;        /* desactive temporairement -- on laisse la table
                       * pour mode debug futur. */
    uid = -1;
    if (uid >= 0) {
        Item u = unique_make(uid);
        if (g && uid >= 0 && uid < 32) {
            bool already = g->meta.unique_seen[uid];
            g->meta.unique_seen[uid] = true;
            if (!already) {
                char buf[48];
                snprintf(buf, sizeof(buf), "UNIQUE : %s", unique_def_name(uid));
                toast_push(g, buf, 0xFF8030FF, 5.5f);
            }
        }
        return u;
    }
    Rarity rarity;
    if (boss)        rarity = rarity_for_floor_boss(floor_index);
    else if (elite)  rarity = rarity_for_floor_elite(floor_index);
    else {
        int r = rand() % 100;
        if (r < 70 - floor_index * 2)  rarity = R_COMMON;
        else if (r < 92 - floor_index) rarity = R_MAGIC;
        else                            rarity = R_RARE;
    }
    EquipSlot slot = (EquipSlot)(rand() % 6);
    int sub_kind = rand() % 5;
    (void)g;
    return item_make(slot, rarity, sub_kind);
}

const char *item_label(const Item *it, char *buf, int bufsz) {
    if (!it->occupied) { snprintf(buf, bufsz, "—"); return buf; }
    snprintf(buf, bufsz, "%s %s", rarity_name(it->rarity), slot_name(it->slot));
    return buf;
}

/* push an item into first empty inventory slot. Les talismans
 * (ITEM_KIND_ELEMENT) vont dans le sac talisman dedie (5x2). */
void inventory_pickup(Game *g, Item it) {
    Player *p = &g->player;
    /* codex : on enregistre la decouverte (slot, sub_kind, rarete) */
    meta_item_mark(&g->meta, it.slot, it.base_kind, it.rarity);
    save_write(&g->meta);
    if (it.kind == ITEM_KIND_ELEMENT) {
        for (int i = 0; i < TALISMAN_BAG_SLOTS; i++) {
            if (!p->talisman_bag[i].occupied) {
                p->talisman_bag[i] = it;
                sfx_play(g, SFX_PICKUP);
                return;
            }
        }
        /* sac talisman plein (rare : seulement 7 talismans par run) :
         * fallback dans le sac principal pour ne rien perdre. */
    }
    int inv_cap = INVENTORY_SLOTS + g->player.inv_capacity_bonus;
    if (inv_cap > INVENTORY_MAX_SLOTS) inv_cap = INVENTORY_MAX_SLOTS;
    for (int i = 0; i < inv_cap; i++) {
        if (!p->inventory[i].occupied) {
            p->inventory[i] = it;
            sfx_play(g, SFX_PICKUP);
            return;
        }
    }
    /* full: drop on ground near player */
    pickup_spawn_item(g, it, p->x, p->y);
}

/* equip from inventory[idx] : comportement par kind.
 *   EQUIP   : swap avec l'equipement du meme slot (comportement
 *             historique).
 *   WEAPON  : remplace l'arme active (l'ancienne va en inventaire
 *             SI elle n'est pas les poings de base, sinon perdue).
 *   ELEMENT : greffe sur l'arme active si un slot talisman est
 *             libre ; consomme l'objet en inventaire. Sinon echec
 *             (message).
 */
bool inventory_equip(Game *g, int inv_index) {
    Player *p = &g->player;
    if (inv_index < 0 || inv_index >= INVENTORY_SLOTS) return false;
    Item *src = &p->inventory[inv_index];
    if (!src->occupied) return false;
    if (src->kind == ITEM_KIND_EQUIP) {
        Item *dst = &p->equipped[src->slot];
        Item tmp = *dst;
        *dst = *src;
        *src = tmp;
        game_recompute_player_stats(g);
        return true;
    }
    if (src->kind == ITEM_KIND_WEAPON) {
        WeaponKind wk = (WeaponKind)src->base_kind;
        Rarity     wr = src->rarity;
        if (wk <= W_FISTS || wk >= W_COUNT) return false;
        /* Filet de securite : refuse l'equipement d'une arme verrouillee.
         * Normalement les drops sont filtres en amont (enemies.c +
         * player.c PU_CHEST), mais on ne fait pas confiance aux items
         * en inventaire (e.g. import futur de save corrompue). */
        if (!g->meta.weapon_unlocked[wk]) {
            snprintf(g->inv_msg, sizeof(g->inv_msg),
                     "%s : verrouillee (FORGE)", weapon_name(wk));
            g->inv_msg_t = 2.0f;
            return false;
        }
        /* Remplace l'arme active. L'ancienne (si pas poings) revient
         * en inventaire au meme slot. */
        Weapon *w = &p->weapons[p->active_weapon];
        Item old = (Item){0};
        if (w->owned && w->kind != W_FISTS) {
            old.occupied = true;
            old.kind = ITEM_KIND_WEAPON;
            old.base_kind = (int)w->kind;
            old.rarity = w->rarity;
            snprintf(old.name, sizeof(old.name), "%s", weapon_name(w->kind));
        }
        weapon_init_defaults(w, wk);
        w->rarity = wr;
        w->owned = true;
        /* FORGE meta bonus */
        if (wk > 0 && wk < W_COUNT) {
            w->base_dmg += g->meta.weapon_dmg_bonus[wk] * 5.f;
        }
        /* discovery codex */
        if (!g->meta.weapon_discovered[wk]) {
            g->meta.weapon_discovered[wk] = true;
            save_write(&g->meta);
            char buf[48];
            snprintf(buf, sizeof(buf), "ARME DECOUVERTE : %s", weapon_name(wk));
            toast_push(g, buf, 0xC0E0FFFF, 4.0f);
        }
        *src = old;     /* slot inventaire recoit l'ancienne arme ou vide */
        sfx_play(g, SFX_LEVELUP);
        log_push(g, rarity_color(wr), "Equipe : %s", weapon_name(wk));
        game_recompute_player_stats(g);
        return true;
    }
    if (src->kind == ITEM_KIND_ELEMENT) {
        Element e = (Element)src->base_kind;
        if (e <= 0 || e >= EL_COUNT) return false;
        Weapon *w = &p->weapons[p->active_weapon];
        int cap = weapon_slot_count(w->rarity);
        if (w->element_count >= cap) {
            snprintf(g->inv_msg, sizeof(g->inv_msg),
                     "Plus de slot talisman libre sur l'arme active");
            g->inv_msg_t = 2.5f;
            return false;
        }
        /* Pas de doublon : impossible d'avoir deux fois le meme element
         * sur la meme arme (eviter "triple feu"). Force le combo nomme. */
        for (int k = 0; k < w->element_count; k++) {
            if (w->elements[k] == e) {
                snprintf(g->inv_msg, sizeof(g->inv_msg),
                         "Element deja greffe sur cette arme");
                g->inv_msg_t = 2.5f;
                return false;
            }
        }
        weapon_attach_element(w, e);
        /* discovery codex element */
        if (!g->meta.element_discovered[e]) {
            g->meta.element_discovered[e] = true;
            save_write(&g->meta);
            char buf[48];
            snprintf(buf, sizeof(buf), "ELEMENT DECOUVERT : %s", element_name(e));
            toast_push(g, buf, element_color(e), 4.0f);
        }
        int mask = weapon_combo_id(w);
        if (mask != 0 && !meta_combo_is_seen(&g->meta, mask)) {
            meta_combo_mark(&g->meta, mask);
            save_write(&g->meta);
            char buf[48];
            snprintf(buf, sizeof(buf), "COMBO : %s", combo_name(mask));
            toast_push(g, buf, combo_color(mask), 4.0f);
        }
        *src = (Item){0};       /* consomme l'orbe */
        sfx_play(g, SFX_LEVELUP);
        log_push(g, element_color(e), "Greffe : %s", element_name(e));
        game_recompute_player_stats(g);
        return true;
    }
    return false;
}

/* Element dominant d'un item : utilise par le rendu (player_3d /
 * paperdoll / drops) pour teinter les pieces avec une affinite. */
Element item_element(const Item *it) {
    if (!it || !it->occupied) return EL_NONE;
    if (it->kind == ITEM_KIND_ELEMENT) {
        Element e = (Element)it->base_kind;
        if (e > EL_NONE && e < EL_COUNT) return e;
        return EL_NONE;
    }
    if (it->is_unique) return unique_def_element(it->unique_id);
    return EL_NONE;
}

int item_sell_value(const Item *it) {
    if (!it || !it->occupied) return 0;
    /* base : 6 coins, x mul de rarete, +25% pour uniques. */
    static const int BASE[R_COUNT] = { 6, 12, 25, 50, 100 };
    int v = BASE[it->rarity];
    if (it->is_unique) v = (int)(v * 1.25f);
    /* bonus modeste par affixe present (~+2 par roll) */
    v += it->affix_count * 2;
    return v;
}

bool inventory_destroy(Game *g, int inv_index) {
    if (inv_index < 0 || inv_index >= INVENTORY_SLOTS) return false;
    Item *src = &g->player.inventory[inv_index];
    if (!src->occupied) return false;
    /* on refuse de detruire un unique pour ne pas faire perdre un drop rare
     * par accident. Le joueur peut quand meme le vendre. */
    if (src->is_unique) {
        snprintf(g->inv_msg, sizeof(g->inv_msg),
                 "Impossible de detruire un item unique");
        g->inv_msg_t = 2.0f;
        return false;
    }
    src->occupied = false;
    sfx_play(g, SFX_FUSE);
    snprintf(g->inv_msg, sizeof(g->inv_msg), "Detruit");
    g->inv_msg_t = 1.0f;
    return true;
}

bool inventory_unequip(Game *g, int equip_index) {
    Player *p = &g->player;
    if (equip_index < 0 || equip_index >= EQUIP_SLOTS) return false;
    Item *src = &p->equipped[equip_index];
    if (!src->occupied) return false;
    /* find free slot */
    int inv_cap = INVENTORY_SLOTS + g->player.inv_capacity_bonus;
    if (inv_cap > INVENTORY_MAX_SLOTS) inv_cap = INVENTORY_MAX_SLOTS;
    for (int i = 0; i < inv_cap; i++) {
        if (!p->inventory[i].occupied) {
            p->inventory[i] = *src;
            src->occupied = false;
            game_recompute_player_stats(g);
            return true;
        }
    }
    return false;
}

/* helper : trouve le premier groupe de 3 items identiques dans l'inventaire.
 * Renvoie true si trouve et remplit out_a/b/c. Sinon false. */
bool inventory_find_fusion_group(Game *g, int *out_a, int *out_b, int *out_c) {
    Player *p = &g->player;
    /* Regles allegees : 3 items MEME KIND + MEME RARETE.
     * - Equipement : meme slot (helmet vs torse pas fusables).
     * - Armes : meme WeaponKind (sword vs axe pas fusables).
     * - L ARCHETYPE (base_kind sur equip) est IGNORE : fusionner 3
     *   helmets rares de differents archetypes est OK ; le resultat
     *   tire un archetype au sort (33%, cf inventory_fuse). */
    int inv_cap = INVENTORY_SLOTS + g->player.inv_capacity_bonus;
    if (inv_cap > INVENTORY_MAX_SLOTS) inv_cap = INVENTORY_MAX_SLOTS;
    for (int i = 0; i < inv_cap; i++) {
        Item *ii = &p->inventory[i];
        if (!ii->occupied || ii->rarity >= R_LEGENDARY) continue;
        if (ii->kind != ITEM_KIND_EQUIP && ii->kind != ITEM_KIND_WEAPON) continue;
        if (ii->is_unique) continue;
        int matches[3] = { i, -1, -1 };
        int cnt = 1;
        for (int j = i + 1; j < inv_cap && cnt < 3; j++) {
            Item *jj = &p->inventory[j];
            if (!jj->occupied) continue;
            if (jj->kind != ii->kind) continue;        /* meme type d'item */
            if (jj->is_unique) continue;
            if (jj->rarity != ii->rarity) continue;
            /* equip : meme slot. arme : meme WeaponKind (base_kind). */
            if (ii->kind == ITEM_KIND_EQUIP && jj->slot != ii->slot) continue;
            if (ii->kind == ITEM_KIND_WEAPON && jj->base_kind != ii->base_kind) continue;
            matches[cnt++] = j;
        }
        if (cnt == 3) {
            if (out_a) *out_a = matches[0];
            if (out_b) *out_b = matches[1];
            if (out_c) *out_c = matches[2];
            return true;
        }
    }
    return false;
}

/* fuse: si l'utilisateur a marque manuellement 3 items, on les utilise.
 * Sinon on auto-detecte le premier groupe de 3 identiques et on fusionne.
 * Plus besoin de marquer manuellement dans le cas standard. */
bool inventory_fuse(Game *g) {
    Player *p = &g->player;
    /* Lecture depuis fusion_slots[3] (items physiquement deposes
     * dans le fusionneur). Plus de marquage flottant. */
    Item *ia = &g->fusion_slots[0];
    Item *ib = &g->fusion_slots[1];
    Item *ic = &g->fusion_slots[2];
    if (!ia->occupied || !ib->occupied || !ic->occupied) {
        snprintf(g->inv_msg, sizeof(g->inv_msg),
                 "Depose 3 items dans le fusionneur");
        g->inv_msg_t = 2.5f;
        return false;
    }
    /* Fusion : meme slot + meme rarete. Archetype peut differer. */
    bool weap_fuse = (ia->kind == ITEM_KIND_WEAPON &&
                      ib->kind == ITEM_KIND_WEAPON &&
                      ic->kind == ITEM_KIND_WEAPON);
    bool equip_fuse = (ia->kind == ITEM_KIND_EQUIP &&
                       ib->kind == ITEM_KIND_EQUIP &&
                       ic->kind == ITEM_KIND_EQUIP);
    if (!weap_fuse && !equip_fuse) {
        snprintf(g->inv_msg, sizeof(g->inv_msg),
                 "Fusion : 3 items du meme type");
        g->inv_msg_t = 2.f;
        return false;
    }
    if (ia->is_unique || ib->is_unique || ic->is_unique) {
        snprintf(g->inv_msg, sizeof(g->inv_msg),
                 "Impossible de fusionner un unique");
        g->inv_msg_t = 2.f;
        return false;
    }
    if (equip_fuse && !(ia->slot == ib->slot && ib->slot == ic->slot)) {
        snprintf(g->inv_msg, sizeof(g->inv_msg), "Meme slot requis");
        g->inv_msg_t = 2.f;
        return false;
    }
    if (!(ia->rarity == ib->rarity && ib->rarity == ic->rarity)) {
        snprintf(g->inv_msg, sizeof(g->inv_msg), "Meme rarete requise");
        g->inv_msg_t = 2.f;
        return false;
    }
    if (weap_fuse && !(ia->base_kind == ib->base_kind &&
                       ib->base_kind == ic->base_kind)) {
        snprintf(g->inv_msg, sizeof(g->inv_msg),
                 "Armes : meme type requis");
        g->inv_msg_t = 2.f;
        return false;
    }
    if (ia->rarity >= R_LEGENDARY) {
        snprintf(g->inv_msg, sizeof(g->inv_msg), "Deja Legendaire (max)");
        g->inv_msg_t = 2.f;
        return false;
    }
    Rarity newr = (Rarity)(ia->rarity + 1);
    Item fused;
    if (weap_fuse) {
        /* fusion d'armes : meme kind+rarete -> upgrade rarete. Pas de
         * lottery archetype, le base_kind est fixe (= WeaponKind). */
        fused = (Item){0};
        fused.occupied = true;
        fused.kind = ITEM_KIND_WEAPON;
        fused.base_kind = ia->base_kind;
        fused.rarity = newr;
        snprintf(fused.name, sizeof(fused.name), "%s",
                 weapon_name((WeaponKind)fused.base_kind));
    } else {
        /* equipement : si les 3 partagent l'archetype, on garde. Sinon
         * tirage uniforme 33%. */
        int chosen_archetype;
        int sources[3] = { ia->base_kind, ib->base_kind, ic->base_kind };
        if (sources[0] == sources[1] && sources[1] == sources[2]) {
            chosen_archetype = sources[0];
        } else {
            chosen_archetype = sources[rand() % 3];
        }
        fused = item_make(ia->slot, newr, chosen_archetype);
        fused.stat_value *= 1.20f;
    }
    /* Vide les 3 slots fusion. Le resultat va au 1er slot bag libre. */
    g->fusion_slots[0].occupied = false;
    g->fusion_slots[1].occupied = false;
    g->fusion_slots[2].occupied = false;
    int inv_cap_o = INVENTORY_SLOTS + p->inv_capacity_bonus;
    if (inv_cap_o > INVENTORY_MAX_SLOTS) inv_cap_o = INVENTORY_MAX_SLOTS;
    int dst = -1;
    for (int i = 0; i < inv_cap_o; i++)
        if (!p->inventory[i].occupied) { dst = i; break; }
    if (dst < 0) {
        /* sac plein : on perd le resultat (rare, on log) */
        snprintf(g->inv_msg, sizeof(g->inv_msg),
                 "Sac plein -> resultat perdu");
        g->inv_msg_t = 3.0f;
        sfx_play(g, SFX_FUSE);
        return true;
    }
    p->inventory[dst] = fused;
    sfx_play(g, SFX_FUSE);
    if (weap_fuse) {
        snprintf(g->inv_msg, sizeof(g->inv_msg), "Fusion arme -> %s %s",
                 rarity_name(newr), weapon_name((WeaponKind)fused.base_kind));
    } else {
        snprintf(g->inv_msg, sizeof(g->inv_msg), "Fusion -> %s %s",
                 rarity_name(newr), slot_name(fused.slot));
    }
    g->inv_msg_t = 2.5f;
    return true;
}

/* Cycle l'element d'un slot talisman (clic) :
 *   vide -> premier element decouvert -> suivant -> ... -> dernier -> vide.
 * Tient compte uniquement des elements decouverts via meta.element_discovered. */
static void talisman_cycle(Game *g, int weapon_idx, int talisman_idx) {
    Weapon *w = &g->player.weapons[weapon_idx];
    if (!w->owned) return;
    int active_n = weapon_slot_count(w->rarity);
    if (talisman_idx >= active_n) {
        snprintf(g->inv_msg, sizeof(g->inv_msg),
                 "Talisman verrouille (ameliore la qualite de l'arme)");
        g->inv_msg_t = 2.0f;
        return;
    }
    /* construit la liste des elements decouverts */
    Element pool[EL_COUNT]; int pn = 0;
    for (int e = 1; e < EL_COUNT; e++)
        if (g->meta.element_discovered[e]) pool[pn++] = (Element)e;
    if (pn == 0) {
        snprintf(g->inv_msg, sizeof(g->inv_msg),
                 "Aucun element decouvert");
        g->inv_msg_t = 1.5f;
        return;
    }
    Element current = (talisman_idx < w->element_count)
                        ? w->elements[talisman_idx] : EL_NONE;
    /* trouve le prochain : current -> pool[idx+1], avec EL_NONE -> pool[0].
     * On saute les elements deja portes sur d'autres slots de la meme arme
     * pour eviter les doublons (pas de triple feu, etc). */
    int next = -1;
    int start = 0;
    if (current != EL_NONE) {
        for (int i = 0; i < pn; i++) if (pool[i] == current) { start = i + 1; break; }
    }
    for (int step = start; step <= pn; step++) {
        if (step >= pn) { next = pn; break; }  /* fin -> retire */
        Element cand = pool[step];
        bool taken = false;
        for (int k = 0; k < w->element_count; k++) {
            if (k == talisman_idx) continue;
            if (w->elements[k] == cand) { taken = true; break; }
        }
        if (!taken) { next = step; break; }
    }
    if (next < 0) next = pn;  /* tous occupes -> retire */
    if (next >= pn) {
        /* fin de la liste -> retire le talisman */
        if (talisman_idx < w->element_count) {
            for (int k = talisman_idx; k < w->element_count - 1; k++)
                w->elements[k] = w->elements[k + 1];
            w->elements[w->element_count - 1] = EL_NONE;
            w->element_count--;
        }
    } else {
        /* assigne pool[next] dans le slot talisman_idx, en etendant
         * element_count si necessaire (les slots intermediaires ont deja
         * une valeur ; on n'ajoute qu'a la fin pour rester coherent). */
        if (talisman_idx < w->element_count) {
            w->elements[talisman_idx] = pool[next];
        } else {
            w->elements[w->element_count++] = pool[next];
        }
    }
    sfx_play(g, SFX_PICKUP);
}

/* Helpers fusionneur : depose un item sac -> 1er slot fusion libre.
 * Renvoie true si depose. */
static bool fusion_deposit_from_bag(Game *g, int inv_idx) {
    if (inv_idx < 0 || inv_idx >= INVENTORY_MAX_SLOTS) return false;
    Item *it = &g->player.inventory[inv_idx];
    if (!it->occupied) return false;
    if (it->kind != ITEM_KIND_EQUIP && it->kind != ITEM_KIND_WEAPON) {
        snprintf(g->inv_msg, sizeof(g->inv_msg),
                 "Fusion : equipement ou arme uniquement");
        g->inv_msg_t = 1.8f;
        return false;
    }
    for (int s = 0; s < 3; s++) {
        if (!g->fusion_slots[s].occupied) {
            g->fusion_slots[s] = *it;
            it->occupied = false;
            return true;
        }
    }
    snprintf(g->inv_msg, sizeof(g->inv_msg), "Fusionneur plein (3 max)");
    g->inv_msg_t = 1.5f;
    return false;
}

/* Retourne un item du fusionneur vers le sac (1er slot libre). */
static bool fusion_retrieve_to_bag(Game *g, int fusion_idx) {
    if (fusion_idx < 0 || fusion_idx >= 3) return false;
    Item *it = &g->fusion_slots[fusion_idx];
    if (!it->occupied) return false;
    int cap = INVENTORY_SLOTS + g->player.inv_capacity_bonus;
    if (cap > INVENTORY_MAX_SLOTS) cap = INVENTORY_MAX_SLOTS;
    for (int i = 0; i < cap; i++) {
        if (!g->player.inventory[i].occupied) {
            g->player.inventory[i] = *it;
            it->occupied = false;
            return true;
        }
    }
    snprintf(g->inv_msg, sizeof(g->inv_msg), "Sac plein");
    g->inv_msg_t = 1.5f;
    return false;
}

static int fusion_count(const Game *g) {
    int n = 0;
    for (int i = 0; i < 3; i++) if (g->fusion_slots[i].occupied) n++;
    return n;
}

/* keyboard + mouse navigation in inventory screen */
void update_inventory_input(Game *g) {
    /* === FUSIONNEUR : clicks sur les 3 slots + bouton ===
     * Layout en parallele avec render_inventory. Items physiques
     * deposes dans g->fusion_slots[]. Click sur slot occupied = retire. */
    {
        int fx0 = 10, fy0 = 250;        /* INV_FUSION_X / INV_FUSION_Y */
        int slot_sz = 30, slot_y = fy0 + 22;
        int slot_x[3] = { fx0 + 10, fx0 + 60, fx0 + 110 };
        if (mouse_clicked(g)) {
            for (int s = 0; s < 3; s++) {
                if (mouse_in_rect(g, slot_x[s], slot_y, slot_sz, slot_sz)) {
                    if (g->fusion_slots[s].occupied) {
                        fusion_retrieve_to_bag(g, s);
                    }
                    return;
                }
            }
            int bx = fx0 + 10, by = fy0 + 65, bw = (200 - 20), bh = 22;
            if (mouse_in_rect(g, bx, by, bw, bh) && fusion_count(g) == 3) {
                inventory_fuse(g);
                return;
            }
        }
    }
    /* mouse hover/click : on parcourt TOUS les slots possibles (sac,
     * equipement, armes, talismans) en utilisant le layout commun.
     * Skip les slots bag au-dessus de la capacite actuelle. */
    int inv_cap_mouse = INVENTORY_SLOTS + g->player.inv_capacity_bonus;
    if (inv_cap_mouse > INVENTORY_MAX_SLOTS) inv_cap_mouse = INVENTORY_MAX_SLOTS;
    for (int i = 0; i < INV_CURSOR_MAX; i++) {
        if (i < INV_CURSOR_EQUIP_BASE && i >= inv_cap_mouse) continue;
        int x, y, w, h;
        if (!inv_layout_rect(i, &x, &y, &w, &h)) continue;
        if (mouse_in_rect(g, x, y, w, h)) {
            g->inv_cursor = i;
            /* Clic DROIT : depose dans le fusionneur depuis le sac OU
             * directement depuis un slot ARME equipee. */
            if (mouse_right_clicked(g)) {
                if (i < INV_CURSOR_EQUIP_BASE) {
                    fusion_deposit_from_bag(g, i);
                    return;
                }
                if (i >= INV_CURSOR_WEAPON_BASE && i < INV_CURSOR_TALISMAN_BASE) {
                    int wi = i - INV_CURSOR_WEAPON_BASE;
                    Weapon *w_ = &g->player.weapons[wi];
                    if (w_->owned && w_->kind != W_FISTS) {
                        /* trouve 1er fusion slot libre */
                        int fs = -1;
                        for (int s = 0; s < 3; s++)
                            if (!g->fusion_slots[s].occupied) { fs = s; break; }
                        if (fs < 0) {
                            snprintf(g->inv_msg, sizeof(g->inv_msg),
                                     "Fusionneur plein (3 max)");
                            g->inv_msg_t = 1.5f;
                        } else {
                            Item it = (Item){0};
                            it.occupied = true;
                            it.kind = ITEM_KIND_WEAPON;
                            it.base_kind = (int)w_->kind;
                            it.rarity = w_->rarity;
                            snprintf(it.name, sizeof(it.name), "%s",
                                     weapon_name(w_->kind));
                            g->fusion_slots[fs] = it;
                            /* remplace par FISTS */
                            weapon_init_defaults(w_, W_FISTS);
                            w_->owned = true;
                            game_recompute_player_stats(g);
                        }
                        return;
                    }
                }
            }
            if (mouse_clicked(g)) {
                if (i < INV_CURSOR_EQUIP_BASE) {
                    /* Click sur item du sac : equipe directement */
                    inventory_equip(g, i);
                } else if (i < INV_CURSOR_WEAPON_BASE) {
                    /* Click sur slot equipement equipe -> retire dans le sac */
                    inventory_unequip(g, i - INV_CURSOR_EQUIP_BASE);
                } else if (i < INV_CURSOR_TALISMAN_BASE) {
                    /* Click sur slot ARME equipee : retire l'arme dans le
                     * sac et la remplace par FISTS. Filet : empeche de
                     * retirer la seule arme equipable. */
                    int wi = i - INV_CURSOR_WEAPON_BASE;
                    Weapon *w_ = &g->player.weapons[wi];
                    if (w_->owned && w_->kind != W_FISTS) {
                        int cap_w = INVENTORY_SLOTS + g->player.inv_capacity_bonus;
                        if (cap_w > INVENTORY_MAX_SLOTS) cap_w = INVENTORY_MAX_SLOTS;
                        int dst = -1;
                        for (int s = 0; s < cap_w; s++)
                            if (!g->player.inventory[s].occupied) { dst = s; break; }
                        if (dst < 0) {
                            snprintf(g->inv_msg, sizeof(g->inv_msg), "Sac plein");
                            g->inv_msg_t = 1.5f;
                        } else {
                            Item back = (Item){0};
                            back.occupied = true;
                            back.kind = ITEM_KIND_WEAPON;
                            back.base_kind = (int)w_->kind;
                            back.rarity = w_->rarity;
                            snprintf(back.name, sizeof(back.name), "%s",
                                     weapon_name(w_->kind));
                            g->player.inventory[dst] = back;
                            weapon_init_defaults(w_, W_FISTS);
                            w_->owned = true;
                            game_recompute_player_stats(g);
                            snprintf(g->inv_msg, sizeof(g->inv_msg),
                                     "Arme retiree dans le sac");
                            g->inv_msg_t = 1.5f;
                        }
                    }
                } else if (i < INV_CURSOR_TALISBAG_BASE) {
                    /* talisman actif (sur arme) : cycle */
                    int rel = i - INV_CURSOR_TALISMAN_BASE;
                    talisman_cycle(g, rel / 3, rel % 3);
                } else {
                    /* talisbag : equipe le talisman sur arme active.
                     * Path direct : weapon_attach_element + invalidate. */
                    int ti = i - INV_CURSOR_TALISBAG_BASE;
                    Item *t = &g->player.talisman_bag[ti];
                    if (t->occupied && t->kind == ITEM_KIND_ELEMENT) {
                        Element e = (Element)t->base_kind;
                        Weapon *w_ = &g->player.weapons[g->player.active_weapon];
                        int cap_t = weapon_slot_count(w_->rarity);
                        if (w_->element_count >= cap_t) {
                            snprintf(g->inv_msg, sizeof(g->inv_msg),
                                     "Slots talisman pleins sur l'arme");
                            g->inv_msg_t = 2.0f;
                        } else {
                            bool dup = false;
                            for (int k = 0; k < w_->element_count; k++)
                                if (w_->elements[k] == e) { dup = true; break; }
                            if (dup) {
                                snprintf(g->inv_msg, sizeof(g->inv_msg),
                                         "Element deja greffe sur cette arme");
                                g->inv_msg_t = 2.0f;
                            } else {
                                weapon_attach_element(w_, e);
                                if (!g->meta.element_discovered[e]) {
                                    g->meta.element_discovered[e] = true;
                                    save_write(&g->meta);
                                }
                                t->occupied = false;
                                game_recompute_player_stats(g);
                                snprintf(g->inv_msg, sizeof(g->inv_msg),
                                         "Greffe : %s", element_name(e));
                                g->inv_msg_t = 1.5f;
                                sfx_play(g, SFX_LEVELUP);
                            }
                        }
                    }
                }
            }
        }
    }
    /* navigation clavier : on garde la grille du sac (fleches) +
     * Tab pour cycler les "zones" (sac -> equip -> armes -> talismans). */
    if (g->keys[SDL_SCANCODE_TAB] && !g->keys_prev[SDL_SCANCODE_TAB]) {
        if (g->inv_cursor < INV_CURSOR_EQUIP_BASE)         g->inv_cursor = INV_CURSOR_EQUIP_BASE;
        else if (g->inv_cursor < INV_CURSOR_WEAPON_BASE)   g->inv_cursor = INV_CURSOR_WEAPON_BASE;
        else if (g->inv_cursor < INV_CURSOR_TALISMAN_BASE) g->inv_cursor = INV_CURSOR_TALISMAN_BASE;
        else if (g->inv_cursor < INV_CURSOR_TALISBAG_BASE) g->inv_cursor = INV_CURSOR_TALISBAG_BASE;
        else                                                g->inv_cursor = 0;
    }
    /* fleches : navigation locale a chaque zone */
    bool kr = (g->keys[SDL_SCANCODE_RIGHT] && !g->keys_prev[SDL_SCANCODE_RIGHT]);
    bool kl = (g->keys[SDL_SCANCODE_LEFT]  && !g->keys_prev[SDL_SCANCODE_LEFT]);
    bool kd = (g->keys[SDL_SCANCODE_DOWN]  && !g->keys_prev[SDL_SCANCODE_DOWN]);
    bool ku = (g->keys[SDL_SCANCODE_UP]    && !g->keys_prev[SDL_SCANCODE_UP]);
    int inv_cap_local = INVENTORY_SLOTS + g->player.inv_capacity_bonus;
    if (inv_cap_local > INVENTORY_MAX_SLOTS) inv_cap_local = INVENTORY_MAX_SLOTS;
    int n_rows_bag = (inv_cap_local + 3) / 4;
    if (g->inv_cursor < INV_CURSOR_EQUIP_BASE) {
        int row = g->inv_cursor / 4, col = g->inv_cursor % 4;
        if (kr) col = (col + 1) % 4;
        if (kl) col = (col + 3) % 4;
        if (kd) { row++; if (row >= n_rows_bag) { g->inv_cursor = INV_CURSOR_EQUIP_BASE; goto nav_done; } }
        if (ku) { row--; if (row < 0)  { row = 0; } }
        int idx = row * 4 + col;
        if (idx >= inv_cap_local) idx = inv_cap_local - 1;
        if (idx < 0) idx = 0;
        g->inv_cursor = idx;
    } else if (g->inv_cursor < INV_CURSOR_WEAPON_BASE) {
        int e = g->inv_cursor - INV_CURSOR_EQUIP_BASE;
        if (kr) e = (e + 1) % EQUIP_SLOTS;
        if (kl) e = (e + EQUIP_SLOTS - 1) % EQUIP_SLOTS;
        if (kd) { g->inv_cursor = INV_CURSOR_WEAPON_BASE; goto nav_done; }
        if (ku) { g->inv_cursor = 0; goto nav_done; }
        g->inv_cursor = INV_CURSOR_EQUIP_BASE + e;
    } else if (g->inv_cursor < INV_CURSOR_TALISMAN_BASE) {
        int w = g->inv_cursor - INV_CURSOR_WEAPON_BASE;
        if (kr) w = (w + 1) % WEAPON_SLOTS;
        if (kl) w = (w + WEAPON_SLOTS - 1) % WEAPON_SLOTS;
        if (kd) { g->inv_cursor = INV_CURSOR_TALISMAN_BASE + w * 3; goto nav_done; }
        if (ku) { g->inv_cursor = INV_CURSOR_EQUIP_BASE; goto nav_done; }
        g->inv_cursor = INV_CURSOR_WEAPON_BASE + w;
    } else if (g->inv_cursor < INV_CURSOR_TALISBAG_BASE) {
        int rel = g->inv_cursor - INV_CURSOR_TALISMAN_BASE;
        int wi = rel / 3, ti = rel % 3;
        if (kr) { ti++; if (ti >= 3) { wi = (wi + 1) % WEAPON_SLOTS; ti = 0; } }
        if (kl) { ti--; if (ti < 0)  { wi = (wi + WEAPON_SLOTS - 1) % WEAPON_SLOTS; ti = 2; } }
        if (ku) { g->inv_cursor = INV_CURSOR_WEAPON_BASE + wi; goto nav_done; }
        if (kd) { g->inv_cursor = INV_CURSOR_TALISBAG_BASE; goto nav_done; }
        g->inv_cursor = INV_CURSOR_TALISMAN_BASE + wi * 3 + ti;
    } else {
        /* talisbag 5x2 : navigation grille */
        int rel = g->inv_cursor - INV_CURSOR_TALISBAG_BASE;
        int row = rel / 5, col = rel % 5;
        if (kr) col = (col + 1) % 5;
        if (kl) col = (col + 4) % 5;
        if (ku) { row--; if (row < 0) { g->inv_cursor = INV_CURSOR_TALISMAN_BASE; goto nav_done; } }
        if (kd) { row++; if (row >= 2) { g->inv_cursor = 0; goto nav_done; } }
        g->inv_cursor = INV_CURSOR_TALISBAG_BASE + row * 5 + col;
    }
nav_done:;
    /* E = equip / unequip / cycle talisman / equip talisbag */
    if (g->keys[SDL_SCANCODE_E] && !g->keys_prev[SDL_SCANCODE_E]) {
        if (g->inv_cursor < INV_CURSOR_EQUIP_BASE) {
            inventory_equip(g, g->inv_cursor);
        } else if (g->inv_cursor < INV_CURSOR_WEAPON_BASE) {
            inventory_unequip(g, g->inv_cursor - INV_CURSOR_EQUIP_BASE);
        } else if (g->inv_cursor < INV_CURSOR_TALISBAG_BASE &&
                   g->inv_cursor >= INV_CURSOR_TALISMAN_BASE) {
            int rel = g->inv_cursor - INV_CURSOR_TALISMAN_BASE;
            talisman_cycle(g, rel / 3, rel % 3);
        } else if (g->inv_cursor >= INV_CURSOR_TALISBAG_BASE &&
                   g->inv_cursor < INV_CURSOR_TALISBAG_BASE + TALISMAN_BAG_SLOTS) {
            /* equip direct depuis le talisbag (path symetrique au clic) */
            int ti = g->inv_cursor - INV_CURSOR_TALISBAG_BASE;
            Item *t = &g->player.talisman_bag[ti];
            if (t->occupied && t->kind == ITEM_KIND_ELEMENT) {
                Element e = (Element)t->base_kind;
                Weapon *w_ = &g->player.weapons[g->player.active_weapon];
                int cap_t = weapon_slot_count(w_->rarity);
                bool dup = false;
                for (int k = 0; k < w_->element_count; k++)
                    if (w_->elements[k] == e) { dup = true; break; }
                if (w_->element_count < cap_t && !dup) {
                    weapon_attach_element(w_, e);
                    if (!g->meta.element_discovered[e]) {
                        g->meta.element_discovered[e] = true;
                        save_write(&g->meta);
                    }
                    t->occupied = false;
                    game_recompute_player_stats(g);
                    sfx_play(g, SFX_LEVELUP);
                }
            }
        }
    }
    /* M : depose l'item sous le curseur dans le fusionneur (raccourci
     * clavier equivalent au clic droit). */
    if (g->keys[SDL_SCANCODE_M] && !g->keys_prev[SDL_SCANCODE_M]) {
        if (g->inv_cursor < INV_CURSOR_EQUIP_BASE) {
            fusion_deposit_from_bag(g, g->inv_cursor);
        }
    }
    /* F = fuse */
    if (g->keys[SDL_SCANCODE_F] && !g->keys_prev[SDL_SCANCODE_F]) {
        inventory_fuse(g);
    }
    /* X = retire tous les items du fusionneur (clear) -> retour au sac. */
    if (g->keys[SDL_SCANCODE_X] && !g->keys_prev[SDL_SCANCODE_X]) {
        for (int s = 0; s < 3; s++) fusion_retrieve_to_bag(g, s);
    }
    /* DELETE = detruit l'item sous le curseur (sac uniquement). */
    if (g->keys[SDL_SCANCODE_DELETE] && !g->keys_prev[SDL_SCANCODE_DELETE]) {
        if (g->inv_cursor < INV_CURSOR_EQUIP_BASE) {
            inventory_destroy(g, g->inv_cursor);
        }
    }
    /* V = vendre l'item du sac contre des coins (no shop UI requis :
     * gain immediat). */
    if (g->keys[SDL_SCANCODE_V] && !g->keys_prev[SDL_SCANCODE_V]) {
        if (g->inv_cursor < INV_CURSOR_EQUIP_BASE) {
            int gain = shop_sell_item(g, g->inv_cursor);
            if (gain > 0) {
                snprintf(g->inv_msg, sizeof(g->inv_msg), "Vendu : +%d coins", gain);
                g->inv_msg_t = 1.6f;
            }
        }
    }
    if (g->inv_msg_t > 0.f) g->inv_msg_t -= g->dt;
}
