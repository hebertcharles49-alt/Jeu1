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

static const AffixDef AFFIX_DEFS[AFFIX_COUNT] = {
    [AFFIX_NONE]        = {0},
    [AFFIX_HP]          = { AFFIX_HP,           5.f,  10.f, 2.0f,  false },
    [AFFIX_ARMOR]       = { AFFIX_ARMOR,        1.f,   2.f, 0.4f,  false },
    [AFFIX_SPEED]       = { AFFIX_SPEED,        3.f,   6.f, 1.2f,  false },
    [AFFIX_DMG_PCT]     = { AFFIX_DMG_PCT,      0.03f, 0.05f, 0.01f, true },
    [AFFIX_CRIT_CHANCE] = { AFFIX_CRIT_CHANCE,  0.02f, 0.04f, 0.008f, true },
    [AFFIX_LIFESTEAL]   = { AFFIX_LIFESTEAL,    0.01f, 0.03f, 0.006f, true },
    [AFFIX_REGEN]       = { AFFIX_REGEN,        0.20f, 0.40f, 0.08f, false },
    [AFFIX_ATK_SPEED]   = { AFFIX_ATK_SPEED,    0.03f, 0.06f, 0.012f, true },
    [AFFIX_DODGE]       = { AFFIX_DODGE,        0.02f, 0.04f, 0.008f, true },
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
        default:                return "?";
    }
}

void affix_label(const ItemAffix *af, char *buf, int bufsz) {
    if (!af || af->kind == AFFIX_NONE) { snprintf(buf, bufsz, "—"); return; }
    const AffixDef *d = &AFFIX_DEFS[af->kind];
    if (d->is_fraction) {
        snprintf(buf, bufsz, "+%.1f%% %s", af->value * 100.f, affix_name(af->kind));
    } else {
        snprintf(buf, bufsz, "+%.1f %s", af->value, affix_name(af->kind));
    }
}

/* nombre d'affixes selon la rarete : 0/1/2/3/4. */
static int affix_count_for_rarity(Rarity r) {
    switch (r) {
        case R_COMMON:    return 0;
        case R_MAGIC:     return 1;
        case R_RARE:      return 2;
        case R_EPIC:      return 3;
        case R_LEGENDARY: return 4;
        default:          return 0;
    }
}

/* roule N affixes distincts (sans doublon) sur l'item. */
static void roll_affixes(Item *it) {
    int n = affix_count_for_rarity(it->rarity);
    if (n > MAX_AFFIXES) n = MAX_AFFIXES;
    /* pool : tous les affixes sauf AFFIX_NONE */
    Affix pool[AFFIX_COUNT - 1];
    int pn = 0;
    for (int a = 1; a < AFFIX_COUNT; a++) pool[pn++] = (Affix)a;
    /* Fisher-Yates partiel */
    for (int i = 0; i < n && i < pn; i++) {
        int j = i + rand() % (pn - i);
        Affix tmp = pool[i]; pool[i] = pool[j]; pool[j] = tmp;
    }
    it->affix_count = 0;
    float rmul = rarity_mul(it->rarity);  /* aussi applique aux affixes */
    for (int i = 0; i < n; i++) {
        const AffixDef *d = &AFFIX_DEFS[pool[i]];
        float vmin = d->base_min + (float)it->base_kind * d->step;
        float vmax = d->base_max + (float)it->base_kind * d->step;
        float roll = vmin + (vmax - vmin) * ((float)rand() / (float)RAND_MAX);
        roll *= rmul;
        it->affixes[i].kind  = pool[i];
        it->affixes[i].value = roll;
        it->affix_count++;
    }
}

Item item_make(EquipSlot slot, Rarity rarity, int sub_kind) {
    Item it = {0};
    it.occupied = true;
    it.slot     = slot;
    it.rarity   = rarity;
    it.base_kind= sub_kind;
    it.stat_value = slot_base_stat(slot) * rarity_mul(rarity);
    /* sub_kind variations bump it slightly */
    it.stat_value *= 1.f + sub_kind * 0.05f;
    roll_affixes(&it);
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
    Rarity rarity;
    if (boss)        rarity = rarity_for_floor_boss(floor_index);
    else if (elite)  rarity = rarity_for_floor_elite(floor_index);
    else {
        /* drop normal : commun majoritaire avec quelques magiques */
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

/* push an item into first empty inventory slot */
void inventory_pickup(Game *g, Item it) {
    Player *p = &g->player;
    /* codex : on enregistre la decouverte (slot, sub_kind, rarete) */
    meta_item_mark(&g->meta, it.slot, it.base_kind, it.rarity);
    save_write(&g->meta);
    for (int i = 0; i < INVENTORY_SLOTS; i++) {
        if (!p->inventory[i].occupied) {
            p->inventory[i] = it;
            sfx_play(g, SFX_PICKUP);
            return;
        }
    }
    /* full: drop on ground near player */
    pickup_spawn_item(g, it, p->x, p->y);
}

/* equip from inventory[idx]; swap with currently equipped of same slot */
bool inventory_equip(Game *g, int inv_index) {
    Player *p = &g->player;
    if (inv_index < 0 || inv_index >= INVENTORY_SLOTS) return false;
    Item *src = &p->inventory[inv_index];
    if (!src->occupied) return false;
    Item *dst = &p->equipped[src->slot];
    Item tmp = *dst;
    *dst = *src;
    *src = tmp;
    game_recompute_player_stats(g);
    return true;
}

bool inventory_unequip(Game *g, int equip_index) {
    Player *p = &g->player;
    if (equip_index < 0 || equip_index >= EQUIP_SLOTS) return false;
    Item *src = &p->equipped[equip_index];
    if (!src->occupied) return false;
    /* find free slot */
    for (int i = 0; i < INVENTORY_SLOTS; i++) {
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
    for (int i = 0; i < INVENTORY_SLOTS; i++) {
        Item *ii = &p->inventory[i];
        if (!ii->occupied || ii->rarity >= R_LEGENDARY) continue;
        int matches[3] = { i, -1, -1 };
        int cnt = 1;
        for (int j = i + 1; j < INVENTORY_SLOTS && cnt < 3; j++) {
            Item *jj = &p->inventory[j];
            if (!jj->occupied) continue;
            if (jj->slot == ii->slot && jj->rarity == ii->rarity &&
                jj->base_kind == ii->base_kind) {
                matches[cnt++] = j;
            }
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
    int a, b, c;
    if (g->inv_marked_count == 3) {
        a = g->inv_marked[0]; b = g->inv_marked[1]; c = g->inv_marked[2];
    } else {
        if (!inventory_find_fusion_group(g, &a, &b, &c)) {
            snprintf(g->inv_msg, sizeof(g->inv_msg),
                     "Aucune fusion possible : il faut 3 items identiques");
            g->inv_msg_t = 2.5f;
            return false;
        }
    }
    Item *ia = &p->inventory[a];
    Item *ib = &p->inventory[b];
    Item *ic = &p->inventory[c];
    if (!ia->occupied || !ib->occupied || !ic->occupied) {
        snprintf(g->inv_msg, sizeof(g->inv_msg), "Slots invalides");
        g->inv_msg_t = 2.f;
        return false;
    }
    if (!(ia->slot == ib->slot && ib->slot == ic->slot &&
          ia->rarity == ib->rarity && ib->rarity == ic->rarity &&
          ia->base_kind == ib->base_kind && ib->base_kind == ic->base_kind)) {
        snprintf(g->inv_msg, sizeof(g->inv_msg), "Items pas identiques");
        g->inv_msg_t = 2.f;
        return false;
    }
    if (ia->rarity >= R_LEGENDARY) {
        snprintf(g->inv_msg, sizeof(g->inv_msg), "Deja Legendaire (max)");
        g->inv_msg_t = 2.f;
        return false;
    }
    Rarity newr = (Rarity)(ia->rarity + 1);
    Item fused = item_make(ia->slot, newr, ia->base_kind);
    fused.stat_value *= 1.20f;
    ia->occupied = false;
    ib->occupied = false;
    ic->occupied = false;
    p->inventory[a] = fused;
    g->inv_marked_count = 0;
    sfx_play(g, SFX_FUSE);
    snprintf(g->inv_msg, sizeof(g->inv_msg), "Fusion -> %s %s",
             rarity_name(newr), slot_name(fused.slot));
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
    /* trouve le prochain : current -> pool[idx+1], avec EL_NONE -> pool[0] */
    int next = -1;
    if (current == EL_NONE) {
        next = 0;
    } else {
        for (int i = 0; i < pn; i++) if (pool[i] == current) { next = i + 1; break; }
        /* current pas trouve (element non decouvert) -> repart sur pool[0] */
        if (next < 0) next = 0;
    }
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

/* keyboard + mouse navigation in inventory screen */
void update_inventory_input(Game *g) {
    /* mouse hover/click : on parcourt TOUS les slots possibles (sac,
     * equipement, armes, talismans) en utilisant le layout commun. */
    for (int i = 0; i < INV_CURSOR_MAX; i++) {
        int x, y, w, h;
        if (!inv_layout_rect(i, &x, &y, &w, &h)) continue;
        if (mouse_in_rect(g, x, y, w, h)) {
            g->inv_cursor = i;
            if (mouse_clicked(g)) {
                if (i < INV_CURSOR_EQUIP_BASE) {
                    inventory_equip(g, i);
                } else if (i < INV_CURSOR_WEAPON_BASE) {
                    inventory_unequip(g, i - INV_CURSOR_EQUIP_BASE);
                } else if (i < INV_CURSOR_TALISMAN_BASE) {
                    /* slot arme : pas d'action click pour l'instant
                     * (les armes ne se "deposent" pas dans le sac). */
                } else {
                    int rel = i - INV_CURSOR_TALISMAN_BASE;
                    talisman_cycle(g, rel / 3, rel % 3);
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
        else                                                g->inv_cursor = 0;
    }
    /* fleches : navigation locale a chaque zone */
    bool kr = (g->keys[SDL_SCANCODE_RIGHT] && !g->keys_prev[SDL_SCANCODE_RIGHT]);
    bool kl = (g->keys[SDL_SCANCODE_LEFT]  && !g->keys_prev[SDL_SCANCODE_LEFT]);
    bool kd = (g->keys[SDL_SCANCODE_DOWN]  && !g->keys_prev[SDL_SCANCODE_DOWN]);
    bool ku = (g->keys[SDL_SCANCODE_UP]    && !g->keys_prev[SDL_SCANCODE_UP]);
    if (g->inv_cursor < INV_CURSOR_EQUIP_BASE) {
        int row = g->inv_cursor / 4, col = g->inv_cursor % 4;
        if (kr) col = (col + 1) % 4;
        if (kl) col = (col + 3) % 4;
        if (kd) { row++; if (row >= 3) { g->inv_cursor = INV_CURSOR_EQUIP_BASE; goto nav_done; } }
        if (ku) { row--; if (row < 0)  { row = 0; } }
        g->inv_cursor = row * 4 + col;
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
    } else {
        int rel = g->inv_cursor - INV_CURSOR_TALISMAN_BASE;
        int wi = rel / 3, ti = rel % 3;
        if (kr) { ti++; if (ti >= 3) { wi = (wi + 1) % WEAPON_SLOTS; ti = 0; } }
        if (kl) { ti--; if (ti < 0)  { wi = (wi + WEAPON_SLOTS - 1) % WEAPON_SLOTS; ti = 2; } }
        if (ku) { g->inv_cursor = INV_CURSOR_WEAPON_BASE + wi; goto nav_done; }
        g->inv_cursor = INV_CURSOR_TALISMAN_BASE + wi * 3 + ti;
    }
nav_done:;
    /* E = equip / unequip / cycle talisman */
    if (g->keys[SDL_SCANCODE_E] && !g->keys_prev[SDL_SCANCODE_E]) {
        if (g->inv_cursor < INV_CURSOR_EQUIP_BASE) {
            inventory_equip(g, g->inv_cursor);
        } else if (g->inv_cursor < INV_CURSOR_WEAPON_BASE) {
            inventory_unequip(g, g->inv_cursor - INV_CURSOR_EQUIP_BASE);
        } else if (g->inv_cursor >= INV_CURSOR_TALISMAN_BASE) {
            int rel = g->inv_cursor - INV_CURSOR_TALISMAN_BASE;
            talisman_cycle(g, rel / 3, rel % 3);
        }
    }
    /* M = mark for fusion */
    if (g->keys[SDL_SCANCODE_M] && !g->keys_prev[SDL_SCANCODE_M]) {
        if (g->inv_cursor < 12 && g->player.inventory[g->inv_cursor].occupied) {
            int idx = g->inv_cursor;
            /* toggle */
            int found = -1;
            for (int i = 0; i < g->inv_marked_count; i++)
                if (g->inv_marked[i] == idx) { found = i; break; }
            if (found >= 0) {
                for (int i = found; i < g->inv_marked_count - 1; i++)
                    g->inv_marked[i] = g->inv_marked[i + 1];
                g->inv_marked_count--;
            } else if (g->inv_marked_count < 3) {
                g->inv_marked[g->inv_marked_count++] = idx;
            } else {
                snprintf(g->inv_msg, sizeof(g->inv_msg), "Deja 3 marques");
                g->inv_msg_t = 1.5f;
            }
        }
    }
    /* F = fuse */
    if (g->keys[SDL_SCANCODE_F] && !g->keys_prev[SDL_SCANCODE_F]) {
        inventory_fuse(g);
    }
    /* X = drop / clear marks */
    if (g->keys[SDL_SCANCODE_X] && !g->keys_prev[SDL_SCANCODE_X]) {
        g->inv_marked_count = 0;
    }
    if (g->inv_msg_t > 0.f) g->inv_msg_t -= g->dt;
}
