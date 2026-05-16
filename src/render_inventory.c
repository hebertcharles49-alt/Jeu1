/*
 * render_inventory.c - inventaire paper-doll Diablo : personnage central
 * + 6 cases equipement + 2 armes + jusqu a 3 talismans / arme + sac 4x3.
 * Cf game.h pour le mapping INV_CURSOR_*.
 */
#include "ui_common.h"
#include "gfx.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

/* ---------- INVENTORY ---------- */
/* Layout Diablo paper-doll :
 *   - colonne gauche  : panneau STATS
 *   - colonne centre  : personnage + 6 slots equipement disposes autour +
 *                       2 slots arme en bas + 1 a 3 slots talisman sous chaque
 *   - colonne droite  : sac 4x3 + panneau details
 *
 * Mapping inv_cursor : voir constantes INV_CURSOR_* dans game.h.
 */

/* positions (en pixels internes 640x360) des cases equipement autour du
 * personnage. Centrees autour du sprite peint a (cx, cy) = (310, 100). */
typedef struct { int dx, dy; const char *short_name; } EquipSlotLayout;
static const EquipSlotLayout EQUIP_LAYOUT[EQUIP_SLOTS] = {
    [SLOT_HELM]   = { -14, -70, "CASQUE"  },  /* au-dessus de la tete */
    [SLOT_CHEST]  = { -52, -28, "TORSE"   },  /* gauche du torse */
    [SLOT_GLOVES] = { -52,   8, "GANTS"   },  /* gauche bas */
    [SLOT_BELT]   = {  26, -28, "CEINT."  },  /* droite torse */
    [SLOT_LEGS]   = {  26,   8, "JAMBES"  },  /* droite bas */
    [SLOT_BOOTS]  = { -14,  44, "BOTTES"  },  /* dessous les pieds */
};

/* Centres du paper-doll : DOIVENT rester synchronises avec render_inventory.
 * Si tu changes ces constantes, change-les aussi dans render_inventory. */
#define INV_PAPERDOLL_CX 240
#define INV_PAPERDOLL_CY 110
#define INV_BAG_X        410
#define INV_BAG_Y         22
#define INV_BAG_CELL      26
#define INV_WEAPON_Y     (INV_PAPERDOLL_CY + 80)
#define INV_WEAPON_X0    (INV_PAPERDOLL_CX - 44)
#define INV_WEAPON_X1    (INV_PAPERDOLL_CX +  8)

bool inv_layout_rect(int cursor_idx, int *x, int *y, int *w, int *h) {
    if (cursor_idx < 0 || cursor_idx >= INV_CURSOR_MAX) return false;
    if (cursor_idx < INV_CURSOR_EQUIP_BASE) {
        int i = cursor_idx;
        int r = i / 4, c = i % 4;
        *x = INV_BAG_X + c * INV_BAG_CELL;
        *y = INV_BAG_Y + r * INV_BAG_CELL;
        *w = 24; *h = 24;
        return true;
    }
    if (cursor_idx < INV_CURSOR_WEAPON_BASE) {
        int i = cursor_idx - INV_CURSOR_EQUIP_BASE;
        *x = INV_PAPERDOLL_CX + EQUIP_LAYOUT[i].dx;
        *y = INV_PAPERDOLL_CY + EQUIP_LAYOUT[i].dy;
        *w = 26; *h = 26;
        return true;
    }
    if (cursor_idx < INV_CURSOR_TALISMAN_BASE) {
        int i = cursor_idx - INV_CURSOR_WEAPON_BASE;   /* 0 ou 1 */
        *x = (i == 0) ? INV_WEAPON_X0 : INV_WEAPON_X1;
        *y = INV_WEAPON_Y;
        *w = 36; *h = 36;
        return true;
    }
    /* talisman : weapon = (idx - base) / 3, sub = (idx - base) % 3 */
    int rel = cursor_idx - INV_CURSOR_TALISMAN_BASE;
    int wi = rel / 3, ti = rel % 3;
    int wx = (wi == 0) ? INV_WEAPON_X0 : INV_WEAPON_X1;
    *x = (wx - 2) + ti * 14;
    *y = INV_WEAPON_Y + 50;
    *w = 12; *h = 12;
    return true;
}

static void render_item_slot(Game *g, int sx, int sy, int sz, Item *it,
                              bool sel, bool marked, const char *empty_label) {
    uint32_t border = 0x404048FF;
    if (sel)    border = 0xFFFF40FF;
    if (marked) border = 0x80FF80FF;
    fill_rect(g->renderer, sx, sy, sz, sz, 0x14101AFF);
    rect_outline(g->renderer, sx, sy, sz, sz, border);
    if (it && it->occupied) {
        uint32_t col = rarity_color(it->rarity);
        int pad = (sz >= 24) ? 4 : 2;
        fill_rect(g->renderer, sx + pad, sy + pad, sz - 2*pad, sz - 2*pad, col);
        rect_outline(g->renderer, sx + pad, sy + pad, sz - 2*pad, sz - 2*pad, 0x000000FF);
        const char *abbr = "?";
        switch (it->slot) {
            case SLOT_HELM:   abbr = "HE"; break;
            case SLOT_CHEST:  abbr = "TO"; break;
            case SLOT_LEGS:   abbr = "JA"; break;
            case SLOT_BOOTS:  abbr = "BO"; break;
            case SLOT_BELT:   abbr = "CE"; break;
            case SLOT_GLOVES: abbr = "GA"; break;
            default: break;
        }
        text_draw(g->renderer, sx + sz/2 - 6, sy + sz/2 - 3, abbr, 0x000000FF);
    } else if (empty_label) {
        /* libelle discret quand le slot est vide -- utile pour le paper-doll */
        text_draw(g->renderer, sx + sz/2 - text_width(empty_label)/2, sy + sz/2 - 3,
                  empty_label, 0x606068FF);
    }
}

/* personnage centre sur (cx, cy), reutilise la palette des heros. */
static void draw_paperdoll_figure(Game *g, int cx, int cy) {
    uint32_t cape, tunic;
    hero_palette(g->player.hero, &cape, &tunic);
    /* cape */
    fill_rect(g->renderer, cx - 16, cy - 20, 32, 38, cape);
    /* tunique */
    fill_rect(g->renderer, cx - 14, cy - 10, 28, 26, tunic);
    /* tete */
    fill_rect(g->renderer, cx - 8,  cy - 30, 16, 14, 0xE8C089FF);
    fill_rect(g->renderer, cx - 8,  cy - 32, 16, 4,  0x402010FF);
    /* yeux */
    fill_rect(g->renderer, cx - 5,  cy - 26, 3, 3, 0x000000FF);
    fill_rect(g->renderer, cx + 2,  cy - 26, 3, 3, 0x000000FF);
    /* jambes */
    fill_rect(g->renderer, cx - 10, cy + 16, 8,  14, 0x303038FF);
    fill_rect(g->renderer, cx + 2,  cy + 16, 8,  14, 0x303038FF);
    /* bottes */
    fill_rect(g->renderer, cx - 11, cy + 28, 10, 4, 0x202020FF);
    fill_rect(g->renderer, cx + 1,  cy + 28, 10, 4, 0x202020FF);
}

/* dessine un slot d'arme + ses talismans dessous (1 a 3 selon la qualite).
 * sx,sy = coin sup-gauche du slot arme (40x32). cursor_w = INV_CURSOR_WEAPON_BASE+slot.
 * cursor_t0 = INV_CURSOR_TALISMAN_BASE + slot*3 (premier talisman). */
static void draw_weapon_and_talismans(Game *g, int sx, int sy,
                                       int weapon_slot, int cursor_w, int cursor_t0)
{
    Weapon *w = &g->player.weapons[weapon_slot];
    /* slot arme : grand carre 36x36 */
    bool sel_w = (g->inv_cursor == cursor_w);
    uint32_t border = sel_w ? 0xFFFF40FF : 0x404048FF;
    fill_rect(g->renderer, sx, sy, 36, 36, 0x14101AFF);
    rect_outline(g->renderer, sx, sy, 36, 36, border);
    if (w->owned) {
        uint32_t rc = rarity_color(w->rarity);
        fill_rect(g->renderer, sx + 4, sy + 4, 28, 28, rc);
        rect_outline(g->renderer, sx + 4, sy + 4, 28, 28, 0x000000FF);
        const char *nm = weapon_name(w->kind);
        /* abreviation 1 lettre centree */
        char abbr[3] = { nm[0], 0, 0 };
        if (nm[1] && nm[1] != ' ') abbr[1] = nm[1];
        text_draw(g->renderer, sx + 18 - text_width(abbr)/2, sy + 15, abbr, 0x000000FF);
    } else {
        text_draw(g->renderer, sx + 18 - text_width("ARME")/2, sy + 15, "ARME", 0x606068FF);
    }

    /* libelle qualite sous le nom d'arme */
    if (w->owned) {
        text_draw(g->renderer, sx + 18 - text_width(rarity_name(w->rarity))/2, sy + 38,
                  rarity_name(w->rarity), rarity_color(w->rarity));
    }

    /* talismans : weapon_slot_count(w->rarity) cases actives, le reste grise */
    int active_n = w->owned ? weapon_slot_count(w->rarity) : 0;
    int tsx = sx - 2;  /* aligne sous l'arme : 3*14 = 42, arme = 36 -> -3 px */
    int tsy = sy + 50;
    for (int t = 0; t < MAX_ELEMENTS_PER_WEAPON; t++) {
        int x = tsx + t * 14;
        bool sel_t = (g->inv_cursor == cursor_t0 + t);
        bool active = (t < active_n);
        uint32_t bd = sel_t ? 0xFFFF40FF : (active ? 0x80809CFF : 0x303034FF);
        uint32_t bg = active ? 0x18141EFF : 0x0C0810FF;
        fill_rect(g->renderer, x, tsy, 12, 12, bg);
        rect_outline(g->renderer, x, tsy, 12, 12, bd);
        if (active && t < w->element_count && w->elements[t] != EL_NONE) {
            uint32_t ec = element_color(w->elements[t]);
            fill_rect(g->renderer, x + 2, tsy + 2, 8, 8, ec);
            rect_outline(g->renderer, x + 2, tsy + 2, 8, 8, 0x000000FF);
        } else if (!active) {
            /* X discret pour signaler "verrouille par qualite" */
            text_draw(g->renderer, x + 3, tsy + 3, "x", 0x404048FF);
        }
    }
}

void render_inventory(Game *g) {
    gfx_set_blend(g->renderer, true);
    fill_rect(g->renderer, 0, 0, INTERNAL_W, INTERNAL_H, 0x000000E0);
    gfx_set_blend(g->renderer, false);
    text_draw(g->renderer, INTERNAL_W/2 - text_width("INVENTAIRE")/2, 6,
              "INVENTAIRE", 0xFFE080FF);

    Player *p = &g->player;

    /* ---- STATS PANEL (gauche) ---- */
    int spx = 10, spy = 22;
    text_draw(g->renderer, spx, spy, "STATS", 0xFFE080FF);
    text_drawf(g->renderer, spx, spy + 12, 0xFFFFFFFF, "PV %d/%d", (int)p->hp, (int)p->maxhp);
    text_drawf(g->renderer, spx, spy + 22, 0xFFFFFFFF, "Armure %.0f",  p->armor);
    text_drawf(g->renderer, spx, spy + 32, 0xFFFFFFFF, "Vitesse %.0f", p->speed);
    text_drawf(g->renderer, spx, spy + 42, 0xFFFFFFFF, "Degats x%.2f", p->dmg_mul);
    text_drawf(g->renderer, spx, spy + 52, 0xFFFFFFFF, "Crit %.0f%%",  p->crit_chance * 100.f);
    text_drawf(g->renderer, spx, spy + 62, 0xFFFFFFFF, "Regen %.1f/s", p->regen_per_sec);
    text_drawf(g->renderer, spx, spy + 72, 0xFFFFFFFF, "Vol vie %.0f%%", p->lifesteal * 100.f);
    text_drawf(g->renderer, spx, spy + 82, 0xCCCCCCFF, "%s", hero_name(p->hero));

    /* ---- PAPER DOLL (centre) ---- */
    int cx = 240, cy = 110;
    /* fond box subtil pour le perso */
    fill_rect(g->renderer, cx - 70, cy - 80, 140, 200, 0x100C18FF);
    rect_outline(g->renderer, cx - 70, cy - 80, 140, 200, 0x303040FF);
    draw_paperdoll_figure(g, cx, cy);

    /* 6 slots equipement autour du personnage */
    for (int i = 0; i < EQUIP_SLOTS; i++) {
        int sx = cx + EQUIP_LAYOUT[i].dx;
        int sy = cy + EQUIP_LAYOUT[i].dy;
        bool sel = (g->inv_cursor == INV_CURSOR_EQUIP_BASE + i);
        render_item_slot(g, sx, sy, 26, &p->equipped[i], sel, false,
                         EQUIP_LAYOUT[i].short_name);
    }

    /* 2 armes en bas du paper-doll, avec talismans sous chacune */
    int wy = cy + 80;
    int wx0 = cx - 44, wx1 = cx + 8;
    draw_weapon_and_talismans(g, wx0, wy, 0,
                              INV_CURSOR_WEAPON_BASE + 0,
                              INV_CURSOR_TALISMAN_BASE + 0);
    draw_weapon_and_talismans(g, wx1, wy, 1,
                              INV_CURSOR_WEAPON_BASE + 1,
                              INV_CURSOR_TALISMAN_BASE + 3);

    /* ---- SAC (droite, 4x3) ---- */
    int bag_x = 410, bag_y = 22, cell = 26;
    text_draw(g->renderer, bag_x, bag_y - 9, "SAC (12)", 0xCCCCFFFF);
    int fa = -1, fb = -1, fc = -1;
    bool has_auto_fuse = inventory_find_fusion_group(g, &fa, &fb, &fc);
    for (int i = 0; i < INVENTORY_SLOTS; i++) {
        int row = i / 4, col = i % 4;
        int sx = bag_x + col * cell;
        int sy = bag_y + row * cell;
        bool sel = (g->inv_cursor == i);
        bool mk = false;
        for (int m = 0; m < g->inv_marked_count; m++)
            if (g->inv_marked[m] == i) mk = true;
        if (g->inv_marked_count == 0 && has_auto_fuse &&
            (i == fa || i == fb || i == fc)) mk = true;
        render_item_slot(g, sx, sy, 24, &p->inventory[i], sel, mk, NULL);
    }

    /* ---- DETAILS PANEL (sous le sac) ---- */
    int dpx = bag_x, dpy = bag_y + 3 * cell + 12;
    int dpw = INTERNAL_W - dpx - 8, dph = INTERNAL_H - dpy - 26;
    fill_rect(g->renderer, dpx, dpy, dpw, dph, 0x14101AFF);
    rect_outline(g->renderer, dpx, dpy, dpw, dph, 0x404048FF);
    /* contenu : depend du cursor */
    if (g->inv_cursor < INV_CURSOR_WEAPON_BASE) {
        bool is_equip = (g->inv_cursor >= INV_CURSOR_EQUIP_BASE);
        Item *it = is_equip
                     ? &p->equipped[g->inv_cursor - INV_CURSOR_EQUIP_BASE]
                     : &p->inventory[g->inv_cursor];
        if (it->occupied) {
            text_drawf(g->renderer, dpx + 4, dpy + 4, rarity_color(it->rarity),
                       "%s%s", rarity_name(it->rarity), is_equip ? " (equipe)" : "");
            text_drawf(g->renderer, dpx + 4, dpy + 14, 0xFFFFFFFF, "%s",
                       slot_name(it->slot));
            const char *unit = "";
            switch (it->slot) {
                case SLOT_HELM:   unit = "PV";    break;
                case SLOT_CHEST:  unit = "ARM";   break;
                case SLOT_LEGS:   unit = "VIT";   break;
                case SLOT_BOOTS:  unit = "DASH";  break;
                case SLOT_BELT:   unit = "REG";   break;
                case SLOT_GLOVES: unit = "DMG";   break;
                default: break;
            }
            int ay = dpy + 26;
            if (it->slot == SLOT_GLOVES || it->slot == SLOT_BOOTS) {
                text_drawf(g->renderer, dpx + 4, ay, 0x80FFC0FF,
                           "+%.0f%% %s", it->stat_value * 100.f, unit);
            } else {
                text_drawf(g->renderer, dpx + 4, ay, 0x80FFC0FF,
                           "+%.1f %s", it->stat_value, unit);
            }
            ay += 10;
            for (int a = 0; a < it->affix_count; a++) {
                char ab[40]; affix_label(&it->affixes[a], ab, sizeof(ab));
                text_draw(g->renderer, dpx + 4, ay, ab, 0xC0E0FFFF);
                ay += 9;
            }
        } else if (is_equip) {
            text_drawf(g->renderer, dpx + 4, dpy + 4, 0x808080FF, "Slot vide : %s",
                       slot_name((EquipSlot)(g->inv_cursor - INV_CURSOR_EQUIP_BASE)));
        } else {
            text_draw(g->renderer, dpx + 4, dpy + 4, "(slot vide)", 0x808080FF);
        }
    } else if (g->inv_cursor < INV_CURSOR_TALISMAN_BASE) {
        Weapon *w = &p->weapons[g->inv_cursor - INV_CURSOR_WEAPON_BASE];
        if (w->owned) {
            text_drawf(g->renderer, dpx + 4, dpy + 4, rarity_color(w->rarity),
                       "%s", weapon_name(w->kind));
            text_drawf(g->renderer, dpx + 4, dpy + 14, 0xCCCCCCFF,
                       "Qualite : %s", rarity_name(w->rarity));
            text_drawf(g->renderer, dpx + 4, dpy + 24, 0x80FFC0FF,
                       "Talismans : %d / %d",
                       w->element_count, weapon_slot_count(w->rarity));
            char descbuf[64];
            weapon_describe(w, descbuf, sizeof(descbuf));
            text_draw(g->renderer, dpx + 4, dpy + 34, descbuf, 0xC0C0FFFF);
        } else {
            text_draw(g->renderer, dpx + 4, dpy + 4, "Slot arme vide", 0x808080FF);
        }
    } else {
        int wi = (g->inv_cursor - INV_CURSOR_TALISMAN_BASE) / 3;
        int ti = (g->inv_cursor - INV_CURSOR_TALISMAN_BASE) % 3;
        Weapon *w = &p->weapons[wi];
        int active_n = w->owned ? weapon_slot_count(w->rarity) : 0;
        text_drawf(g->renderer, dpx + 4, dpy + 4, 0xFFE080FF,
                   "Talisman %d (arme %d)", ti + 1, wi + 1);
        if (ti >= active_n) {
            text_draw(g->renderer, dpx + 4, dpy + 14, "Verrouille", 0xFF8080FF);
            text_draw(g->renderer, dpx + 4, dpy + 24, "ameliore la qualite", 0xCCCCCCFF);
            text_draw(g->renderer, dpx + 4, dpy + 32, "de l'arme pour debloquer", 0xCCCCCCFF);
        } else if (ti < w->element_count) {
            Element el = w->elements[ti];
            text_drawf(g->renderer, dpx + 4, dpy + 14, element_color(el),
                       "%s", element_name(el));
            text_draw(g->renderer, dpx + 4, dpy + 24, "CLIC : cycler", 0xCCCCCCFF);
            text_draw(g->renderer, dpx + 4, dpy + 32, "(retire si fin)", 0xCCCCCCFF);
        } else {
            text_draw(g->renderer, dpx + 4, dpy + 14, "(vide)", 0x808080FF);
            text_draw(g->renderer, dpx + 4, dpy + 24, "CLIC : ajouter element", 0xCCCCCCFF);
        }
    }

    /* ---- FUSION PREVIEW (bas-gauche) ---- */
    int fpx = 10, fpy = INTERNAL_H - 60;
    if (g->inv_marked_count > 0) {
        text_drawf(g->renderer, fpx, fpy, 0x80FF80FF,
                   "FUSION (%d/3 marques)", g->inv_marked_count);
        if (g->inv_marked_count == 3) {
            Item *base = &p->inventory[g->inv_marked[0]];
            if (base->occupied && base->rarity < R_LEGENDARY) {
                text_drawf(g->renderer, fpx, fpy + 10, 0x80FF80FF,
                           "F = %s %s +20%%",
                           rarity_name(base->rarity + 1), slot_name(base->slot));
            } else if (base->occupied) {
                text_draw(g->renderer, fpx, fpy + 10, "Deja max", 0xFFC080FF);
            }
        }
    } else if (has_auto_fuse) {
        Item *base = &p->inventory[fa];
        text_draw(g->renderer, fpx, fpy, "FUSION DETECTEE", 0x80FF80FF);
        text_drawf(g->renderer, fpx, fpy + 10, 0x80FF80FF,
                   "F = %s %s", rarity_name(base->rarity + 1), slot_name(base->slot));
    }

    /* ---- message + footer ---- */
    if (g->inv_msg_t > 0.f) {
        text_draw(g->renderer, INTERNAL_W/2 - text_width(g->inv_msg)/2,
                  INTERNAL_H - 28, g->inv_msg, 0xFFFF40FF);
    }
    const char *foot1 = "CLIC EQUIPE/CYCLE  E EQUIPER  F FUSION  M MARQUER  X RAZ";
    text_draw(g->renderer, INTERNAL_W/2 - text_width(foot1)/2,
              INTERNAL_H - 18, foot1, 0xCCCCCCFF);
    text_draw(g->renderer, INTERNAL_W/2 - text_width("ECHAP POUR FERMER")/2,
              INTERNAL_H - 8, "ECHAP POUR FERMER", 0xFFFF80FF);
}
