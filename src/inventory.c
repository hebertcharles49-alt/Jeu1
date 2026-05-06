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

Item item_make(EquipSlot slot, Rarity rarity, int sub_kind) {
    Item it = {0};
    it.occupied = true;
    it.slot     = slot;
    it.rarity   = rarity;
    it.base_kind= sub_kind;
    it.stat_value = slot_base_stat(slot) * rarity_mul(rarity);
    /* sub_kind variations bump it slightly */
    it.stat_value *= 1.f + sub_kind * 0.05f;
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

/* fuse: 3 items in inventory, identical (slot, rarity, base_kind) -> next rarity, same slot */
bool inventory_fuse(Game *g) {
    Player *p = &g->player;
    if (g->inv_marked_count != 3) {
        snprintf(g->inv_msg, sizeof(g->inv_msg), "Marque 3 items identiques (F)");
        g->inv_msg_t = 2.f;
        return false;
    }
    int a = g->inv_marked[0], b = g->inv_marked[1], c = g->inv_marked[2];
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
    fused.stat_value *= 1.20f;     /* +20% bonus via fusion */
    /* clear sources */
    ia->occupied = false;
    ib->occupied = false;
    ic->occupied = false;
    /* place fused in slot a */
    p->inventory[a] = fused;
    g->inv_marked_count = 0;
    sfx_play(g, SFX_FUSE);
    snprintf(g->inv_msg, sizeof(g->inv_msg), "Fusion -> %s %s",
             rarity_name(newr), slot_name(fused.slot));
    g->inv_msg_t = 2.5f;
    return true;
}

/* keyboard + mouse navigation in inventory screen */
void update_inventory_input(Game *g) {
    /* mouse hover/click sur slots */
    int cell = 28;
    for (int i = 0; i < INVENTORY_SLOTS; i++) {
        int row = i / 4, col = i % 4;
        int sx = 30 + col * cell;
        int sy = 24 + row * cell;
        if (mouse_in_rect(g, sx, sy, 26, 26)) {
            g->inv_cursor = i;
            if (mouse_clicked(g)) {
                inventory_equip(g, i);
            }
        }
    }
    int eqx = INTERNAL_W - 60;
    for (int i = 0; i < EQUIP_SLOTS; i++) {
        int sx = eqx;
        int sy = 24 + i * cell;
        if (mouse_in_rect(g, sx, sy, 26, 26)) {
            g->inv_cursor = 12 + i;
            if (mouse_clicked(g)) {
                inventory_unequip(g, i);
            }
        }
    }
    /* 12 inventory slots laid out 4x3, then 6 equipment slots */
    /* cursor < 12 = inv, 12..17 = equip */
    if (g->keys[SDL_SCANCODE_RIGHT] && !g->keys_prev[SDL_SCANCODE_RIGHT]) {
        if (g->inv_cursor < 12) {
            int row = g->inv_cursor / 4;
            int col = g->inv_cursor % 4;
            col++;
            if (col >= 4) col = 3;
            g->inv_cursor = row * 4 + col;
        } else {
            int e = g->inv_cursor - 12;
            e = (e + 1) % EQUIP_SLOTS;
            g->inv_cursor = 12 + e;
        }
    }
    if (g->keys[SDL_SCANCODE_LEFT] && !g->keys_prev[SDL_SCANCODE_LEFT]) {
        if (g->inv_cursor < 12) {
            int row = g->inv_cursor / 4;
            int col = g->inv_cursor % 4;
            col--; if (col < 0) col = 0;
            g->inv_cursor = row * 4 + col;
        } else {
            int e = g->inv_cursor - 12;
            e = (e - 1 + EQUIP_SLOTS) % EQUIP_SLOTS;
            g->inv_cursor = 12 + e;
        }
    }
    if (g->keys[SDL_SCANCODE_DOWN] && !g->keys_prev[SDL_SCANCODE_DOWN]) {
        if (g->inv_cursor < 12) {
            int row = g->inv_cursor / 4;
            int col = g->inv_cursor % 4;
            row++;
            if (row >= 3) g->inv_cursor = 12;        /* down past inventory -> equip */
            else g->inv_cursor = row * 4 + col;
        }
    }
    if (g->keys[SDL_SCANCODE_UP] && !g->keys_prev[SDL_SCANCODE_UP]) {
        if (g->inv_cursor >= 12) {
            g->inv_cursor = 8;                       /* enter inv from equip */
        } else {
            int row = g->inv_cursor / 4;
            int col = g->inv_cursor % 4;
            row--;
            if (row >= 0) g->inv_cursor = row * 4 + col;
        }
    }
    /* E = equip / unequip */
    if (g->keys[SDL_SCANCODE_E] && !g->keys_prev[SDL_SCANCODE_E]) {
        if (g->inv_cursor < 12) inventory_equip(g, g->inv_cursor);
        else                    inventory_unequip(g, g->inv_cursor - 12);
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
