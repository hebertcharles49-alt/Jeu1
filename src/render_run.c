/*
 * render_run.c - ecrans contextuels en cours de run : levelup et shop.
 */
#include "ui_common.h"
#include "gfx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- LEVELUP ---------- */
void render_levelup(Game *g) {
    gfx_set_blend(g->renderer, true);
    fill_rect(g->renderer, 0, 0, INTERNAL_W, INTERNAL_H, 0x000000C0);
    gfx_set_blend(g->renderer, false);
    text_draw(g->renderer, INTERNAL_W/2 - text_width("MONTEE DE NIVEAU")/2, 30,
              "MONTEE DE NIVEAU", 0xFFFF80FF);
    text_draw(g->renderer, INTERNAL_W/2 - text_width("Trois talents -- chacun avec sa qualite")/2,
              46, "Trois talents -- chacun avec sa qualite", 0xC0C0FFFF);

    int boxw = 150, boxh = 80;
    int total_w = boxw * 3 + 12;
    int sx = (INTERNAL_W - total_w) / 2;
    int sy = 70;
    for (int c = 0; c < 3; c++) {
        int axis  = g->levelup_choices[c];
        Rarity r  = (Rarity)g->levelup_choice_rarity[c];
        if (r < 0 || r >= R_COUNT) r = R_COMMON;
        uint32_t rcol = rarity_color(r);

        fill_rect(g->renderer, sx, sy, boxw, boxh, 0x18101DFF);
        rect_outline(g->renderer, sx, sy, boxw, boxh, rcol);
        /* bandeau couleur de rarete en haut */
        fill_rect(g->renderer, sx, sy, boxw, 4, rcol);

        text_drawf(g->renderer, sx + 6, sy + 8, 0xFFFF80FF, "[%d]", c + 1);
        text_draw(g->renderer, sx + 24, sy + 8, rarity_name(r), rcol);

        const char *axn = level_stat_name(axis);
        float v = level_stat_value_for(axis, r);
        char buf[64];
        /* axes pourcentage : 2, 5, 6, 7. Les autres sont des valeurs flat. */
        if (axis == 2 || axis == 5 || axis == 6 || axis == 7) {
            snprintf(buf, sizeof(buf), "+%d%% %s", (int)(v * 100.f + 0.5f), axn);
        } else {
            /* affichage compact : entier si presque rond, sinon 1 decimale */
            if (fabsf(v - (int)(v + 0.5f)) < 0.05f)
                snprintf(buf, sizeof(buf), "+%d %s", (int)(v + 0.5f), axn);
            else
                snprintf(buf, sizeof(buf), "+%.1f %s", v, axn);
        }
        text_draw(g->renderer, sx + 6, sy + 30, buf, 0xFFFFFFFF);

        /* aperçu chiffre permanent */
        text_drawf(g->renderer, sx + 6, sy + boxh - 14, 0x808088FF,
                   "x%.2f vs commun", v / (level_stat_value_for(axis, R_COMMON) + 0.0001f));
        sx += boxw + 6;
    }
    text_draw(g->renderer, INTERNAL_W/2 - text_width("1 / 2 / 3 ou CLIC POUR CHOISIR   I INVENTAIRE")/2,
              INTERNAL_H - 20,
              "1 / 2 / 3 ou CLIC POUR CHOISIR   I INVENTAIRE", 0xFFFFFFFF);
}


/* ---------- SHOP ---------- */
void render_shop(Game *g) {
    /* fond degrade ambiance taverne */
    for (int y = 0; y < INTERNAL_H; y++) {
        int v = 12 + (INTERNAL_H - y) / 28;
        fill_rect(g->renderer, 0, y, INTERNAL_W, 1,
                  (uint32_t)((v << 24) | ((v / 2) << 16) | ((v / 3) << 8) | 0xFF));
    }
    /* particules braise */
    for (int i = 0; i < 60; i++) {
        int x = (i * 73 + (int)(g->time * 16)) % INTERNAL_W;
        int y = ((i * 91) + (int)(g->time * (i % 5 + 2) * 4)) % INTERNAL_H;
        uint32_t col = (i & 3) ? 0x402030FF : 0xFFB060FF;
        fill_rect(g->renderer, x, y, 1, 1, col);
    }

    text_draw(g->renderer, INTERNAL_W/2 - text_width("MARCHE DE PALIER")/2, 14,
              "MARCHE DE PALIER", 0xFFE080FF);
    text_drawf(g->renderer, 8, 30, 0xFFD040FF, "PIECES %d", g->player.coins);
    text_drawf(g->renderer, 8, 40, 0xC0E0FFFF, "ETAGE %d -> %d",
               g->floor_index, g->floor_index + 1);
    text_drawf(g->renderer, INTERNAL_W - 110, 30, 0x80FFC0FF,
               "ACHATS  %d", g->player.shop_purchased_count);

    int boxw = 130, boxh = 150, gap = 8;
    int total_w = SHOP_SLOTS * boxw + (SHOP_SLOTS - 1) * gap;
    int sx0 = (INTERNAL_W - total_w) / 2;
    int sy = 56;
    for (int i = 0; i < SHOP_SLOTS; i++) {
        ShopItem *si = &g->shop_items[i];
        int sx = sx0 + i * (boxw + gap);
        bool sel = (g->shop_cursor == i);
        bool affordable = (g->player.coins >= si->cost);
        uint32_t border = sel ? 0xFFFF40FF : (affordable ? 0x404048FF : 0x60303AFF);
        fill_rect(g->renderer, sx, sy, boxw, boxh, sel ? 0x1A1422FF : 0x10080FFF);
        rect_outline(g->renderer, sx, sy, boxw, boxh, border);

        /* badge rarete couleur */
        uint32_t rcol = shop_recipe_color(si->recipe_id);
        fill_rect(g->renderer, sx + 4, sy + 4, boxw - 8, 2, rcol);
        fill_rect(g->renderer, sx, sy, 6, boxh, rcol);

        /* nom */
        text_draw(g->renderer, sx + 10, sy + 12, shop_recipe_name(si->recipe_id), rcol);
        /* desc en multi-ligne grossier (decoupe a l'espace si trop long) */
        const char *desc = shop_recipe_desc(si->recipe_id);
        int max_chars = (boxw - 16) / 6;
        char line[64];
        int desc_y = sy + 26;
        const char *p = desc;
        while (*p && desc_y < sy + boxh - 28) {
            int n = 0;
            while (p[n] && n < max_chars && p[n] != '\n') n++;
            /* coupe a un espace si possible */
            if (p[n] && n == max_chars) {
                int back = n;
                while (back > 0 && p[back] != ' ') back--;
                if (back > 0) n = back;
            }
            int len = n; if (len > 60) len = 60;
            memcpy(line, p, len); line[len] = 0;
            text_draw(g->renderer, sx + 10, desc_y, line, 0xCCCCDDFF);
            desc_y += 9;
            p += n;
            while (*p == ' ') p++;
        }

        /* prix / status */
        int by = sy + boxh - 18;
        if (si->bought) {
            text_draw(g->renderer, sx + 10, by, "ACHETE", 0x80FF80FF);
        } else {
            uint32_t pcol = affordable ? 0xFFD040FF : 0x806020FF;
            text_drawf(g->renderer, sx + 10, by, pcol, "%d", si->cost);
            fill_rect(g->renderer, sx + 10 + 22, by + 1, 5, 5, pcol);
            if (sel) text_draw(g->renderer, sx + boxw - 56, by, "[ACHETER]",
                              affordable ? 0x80FF80FF : 0x808080FF);
        }
    }

    /* bouton REROLL */
    int rrw = 110, rrh = 18;
    int rrx = (INTERNAL_W - rrw) / 2;
    int rry = sy + boxh + 8;
    bool rrhov = mouse_in_rect(g, rrx, rry, rrw, rrh);
    bool rrok  = (g->player.coins >= g->shop_reroll_cost);
    fill_rect(g->renderer, rrx, rry, rrw, rrh, rrhov ? 0x303060FF : 0x18181EFF);
    rect_outline(g->renderer, rrx, rry, rrw, rrh, rrhov ? 0xFFFF80FF : 0x404048FF);
    text_drawf(g->renderer, rrx + 8, rry + 6,
               rrok ? (rrhov ? 0xFFFF80FF : 0xCCCCCCFF) : 0x808080FF,
               "REROLL  %d", g->shop_reroll_cost);
    fill_rect(g->renderer, rrx + 8 + text_width("REROLL  ") + 6, rry + 7, 5, 5,
              rrok ? 0xFFD040FF : 0x806020FF);

    if (g->inv_msg_t > 0.f)
        text_draw(g->renderer, INTERNAL_W/2 - text_width(g->inv_msg)/2,
                  INTERNAL_H - 38, g->inv_msg, 0xFFFF40FF);

    text_draw(g->renderer, INTERNAL_W/2 - text_width("CLIC ACHETER  R REROLL  I INVENTAIRE")/2,
              INTERNAL_H - 26, "CLIC ACHETER  R REROLL  I INVENTAIRE", 0xCCCCCCFF);
    int fbx = INTERNAL_W/2 - 100, fby = INTERNAL_H - 14;
    bool fbhov = mouse_in_rect(g, fbx, fby, 200, 12);
    text_draw(g->renderer, INTERNAL_W/2 - text_width("[ESPACE / CLIC] ETAGE SUIVANT")/2,
              fby + 1, "[ESPACE / CLIC] ETAGE SUIVANT",
              fbhov ? 0xFFFF80FF : 0x80FF80FF);
}

