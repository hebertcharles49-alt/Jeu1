/*
 * render_codex.c - codex de decouvertes (combos / talismans / equipement / armes).
 */
#include "ui_common.h"
#include "gfx.h"
#include <stdio.h>
#include <stdlib.h>

/* ---------- CODEX ----------
 * Bestiaire / encyclopedie des decouvertes. 4 onglets :
 *   0 COMBOS       : tous les combos definis (paires + triples) avec leur
 *                    couleur ; "?" en couleur de rarete pour les non-vus.
 *   1 TALISMANS    : 10 elements, "?" en couleur de l element pour les non-vus.
 *   2 EQUIPEMENT   : 6 slots x 5 sub_kinds = 30 cases, montre la meilleure
 *                    rarete vue (item_seen_rarity[s][k]).
 *   3 ARMES        : 6 types, marques decouverts via weapon_discovered.
 *
 * Couleur du "?" pour les combos : derivee du nombre d elements distincts
 * (1 = commun, 2 = rare, 3 = epique, 4+ = legendaire) -> donne au joueur
 * une indication de "puissance" avant la decouverte.
 */
static const char *CODEX_TAB_NAMES[4] = { "COMBOS", "TALISMANS", "EQUIPEMENT", "ARMES" };

static uint32_t codex_combo_rarity_color(int mask) {
    int n = combo_mask_element_count(mask);
    if (n <= 1) return rarity_color(R_COMMON);
    if (n == 2) return rarity_color(R_RARE);
    if (n == 3) return rarity_color(R_EPIC);
    return rarity_color(R_LEGENDARY);
}

/* compte les entrees dans l onglet courant (pour clamp cursor) */
static int codex_tab_count(int tab) {
    switch (tab) {
        case 0: return combo_table_count();
        case 1: return EL_COUNT - 1;        /* on saute EL_NONE */
        case 2: return EQUIP_SLOTS * 5;
        case 3: return W_COUNT - 1;         /* on saute W_FISTS */
        default: return 0;
    }
}

/* dessine une entree de la liste a (sx, sy), largeur lw, hauteur 14. */
static void codex_draw_entry(Game *g, int sx, int sy, int lw, int tab, int idx,
                              bool selected)
{
    uint32_t bg = selected ? 0x282038FF : 0x14101AFF;
    uint32_t bd = selected ? 0xFFFF40FF : 0x303038FF;
    fill_rect(g->renderer, sx, sy, lw, 14, bg);
    rect_outline(g->renderer, sx, sy, lw, 14, bd);

    if (tab == 0) {
        int mask = 0; const char *nm = NULL; uint32_t col = 0;
        if (!combo_table_get(idx, &mask, &nm, &col)) return;
        bool seen = meta_combo_is_seen(&g->meta, mask);
        /* puce de couleur a gauche : couleur du combo si decouvert, couleur
         * de rarete si "?" pour donner une indication de puissance. */
        uint32_t puce = seen ? col : codex_combo_rarity_color(mask);
        fill_rect(g->renderer, sx + 2, sy + 3, 8, 8, puce);
        rect_outline(g->renderer, sx + 2, sy + 3, 8, 8, 0x000000FF);
        if (seen) {
            int n = combo_mask_element_count(mask);
            text_drawf(g->renderer, sx + 14, sy + 4, col, "%s", nm);
            text_drawf(g->renderer, sx + lw - 60, sy + 4, 0xCCCCCCFF,
                       "%d elem", n);
        } else {
            text_draw(g->renderer, sx + 14, sy + 4, "?", puce);
            text_draw(g->renderer, sx + lw - 90, sy + 4, "non decouvert", 0x606068FF);
        }
    } else if (tab == 1) {
        Element e = (Element)(idx + 1);
        bool seen = (e < EL_COUNT) && g->meta.element_discovered[e];
        uint32_t ec = element_color(e);
        fill_rect(g->renderer, sx + 2, sy + 3, 8, 8, seen ? ec : 0x202028FF);
        rect_outline(g->renderer, sx + 2, sy + 3, 8, 8, 0x000000FF);
        if (seen) {
            text_drawf(g->renderer, sx + 14, sy + 4, ec, "%s", element_name(e));
        } else {
            text_draw(g->renderer, sx + 14, sy + 4, "?", ec);
        }
    } else if (tab == 2) {
        int s = idx / 5;
        int k = idx % 5;
        int seen_r = g->meta.item_seen_rarity[s][k];
        uint32_t puce = (seen_r >= 0) ? rarity_color((Rarity)seen_r)
                                       : rarity_color(R_COMMON);
        fill_rect(g->renderer, sx + 2, sy + 3, 8, 8, puce);
        rect_outline(g->renderer, sx + 2, sy + 3, 8, 8, 0x000000FF);
        if (seen_r >= 0) {
            text_drawf(g->renderer, sx + 14, sy + 4, puce,
                       "%s #%d", slot_name((EquipSlot)s), k);
            text_drawf(g->renderer, sx + lw - 90, sy + 4, puce,
                       "%s", rarity_name((Rarity)seen_r));
        } else {
            text_drawf(g->renderer, sx + 14, sy + 4, puce, "%s ?",
                       slot_name((EquipSlot)s));
            text_draw(g->renderer, sx + lw - 90, sy + 4, "non decouvert", 0x606068FF);
        }
    } else if (tab == 3) {
        WeaponKind wk = (WeaponKind)(idx + 1);   /* skip W_FISTS */
        bool seen = (wk < W_COUNT) && g->meta.weapon_discovered[wk];
        fill_rect(g->renderer, sx + 2, sy + 3, 8, 8,
                  seen ? rarity_color(R_RARE) : rarity_color(R_COMMON));
        rect_outline(g->renderer, sx + 2, sy + 3, 8, 8, 0x000000FF);
        if (seen) {
            text_drawf(g->renderer, sx + 14, sy + 4, 0xFFFFFFFF, "%s",
                       weapon_name(wk));
        } else {
            text_draw(g->renderer, sx + 14, sy + 4, "?", rarity_color(R_RARE));
        }
    }
}

void render_codex(Game *g) {
    /* fond plein ecran */
    fill_rect(g->renderer, 0, 0, INTERNAL_W, INTERNAL_H, 0x080612FF);
    text_draw(g->renderer, INTERNAL_W/2 - text_width("CODEX")/2, 6, "CODEX", 0xFFE080FF);

    /* tabs */
    int tabw = 110, tabh = 14, gap = 4;
    int total_w = tabw * 4 + gap * 3;
    int tx = (INTERNAL_W - total_w) / 2;
    for (int i = 0; i < 4; i++) {
        int x = tx + i * (tabw + gap);
        bool sel = (g->codex_tab == i);
        fill_rect(g->renderer, x, 22, tabw, tabh, sel ? 0x303060FF : 0x18181EFF);
        rect_outline(g->renderer, x, 22, tabw, tabh, sel ? 0xFFFF80FF : 0x404048FF);
        const char *nm = CODEX_TAB_NAMES[i];
        text_draw(g->renderer, x + (tabw - text_width(nm)) / 2, 26, nm,
                  sel ? 0xFFFF80FF : 0xCCCCCCFF);
    }

    /* compteur de decouverte de l onglet */
    int n_total = codex_tab_count(g->codex_tab);
    int n_seen  = 0;
    for (int i = 0; i < n_total; i++) {
        switch (g->codex_tab) {
            case 0: {
                int m; if (combo_table_get(i, &m, NULL, NULL) &&
                           meta_combo_is_seen(&g->meta, m)) n_seen++;
                break;
            }
            case 1: if (g->meta.element_discovered[i + 1]) n_seen++; break;
            case 2: if (g->meta.item_seen_rarity[i/5][i%5] >= 0) n_seen++; break;
            case 3: if (g->meta.weapon_discovered[i + 1]) n_seen++; break;
        }
    }
    text_drawf(g->renderer, INTERNAL_W - 90, 8, 0xCCCCCCFF,
               "%d/%d decouverts", n_seen, n_total);

    /* liste defilante : 18 lignes max visibles */
    int list_x = 30, list_y = 46, list_w = INTERNAL_W - 60;
    int row_h = 16;
    int max_visible = (INTERNAL_H - list_y - 26) / row_h;
    if (max_visible < 1) max_visible = 1;

    /* clamp scroll */
    if (g->codex_scroll < 0) g->codex_scroll = 0;
    if (g->codex_scroll > n_total - max_visible)
        g->codex_scroll = n_total > max_visible ? n_total - max_visible : 0;

    for (int i = 0; i < max_visible; i++) {
        int idx = g->codex_scroll + i;
        if (idx >= n_total) break;
        int sy = list_y + i * row_h;
        codex_draw_entry(g, list_x, sy, list_w, g->codex_tab, idx,
                         idx == g->codex_cursor);
    }

    /* footer */
    text_draw(g->renderer, INTERNAL_W/2 - text_width("Q/E ou TAB : onglet   FLECHES/MOLETTE : defiler   ECHAP : retour")/2,
              INTERNAL_H - 12,
              "Q/E ou TAB : onglet   FLECHES/MOLETTE : defiler   ECHAP : retour",
              0xCCCCCCFF);
}

void update_codex(Game *g) {
    int n_total = codex_tab_count(g->codex_tab);
    int list_x = 30, list_y = 46, list_w = INTERNAL_W - 60;
    int row_h = 16;
    int max_visible = (INTERNAL_H - list_y - 26) / row_h;
    if (max_visible < 1) max_visible = 1;

    /* tabs souris */
    int tabw = 110, tabh = 14, gap = 4;
    int total_w = tabw * 4 + gap * 3;
    int tx = (INTERNAL_W - total_w) / 2;
    for (int i = 0; i < 4; i++) {
        int x = tx + i * (tabw + gap);
        if (mouse_in_rect(g, x, 22, tabw, tabh) && mouse_clicked(g)) {
            g->codex_tab = i;
            g->codex_cursor = 0;
            g->codex_scroll = 0;
        }
    }
    /* hover sur la liste */
    for (int i = 0; i < max_visible; i++) {
        int idx = g->codex_scroll + i;
        if (idx >= n_total) break;
        int sy = list_y + i * row_h;
        if (mouse_in_rect(g, list_x, sy, list_w, 14)) {
            g->codex_cursor = idx;
        }
    }
    /* molette */
    if (g->mouse_wheel != 0) {
        g->codex_scroll -= g->mouse_wheel * 3;
    }

    /* clavier : onglets Q/E ou Tab */
    if ((g->keys[SDL_SCANCODE_Q]   && !g->keys_prev[SDL_SCANCODE_Q]) ||
        (g->keys[SDL_SCANCODE_TAB] && !g->keys_prev[SDL_SCANCODE_TAB] &&
         g->keys[SDL_SCANCODE_LSHIFT])) {
        g->codex_tab = (g->codex_tab + 3) % 4;
        g->codex_cursor = 0; g->codex_scroll = 0;
    }
    if ((g->keys[SDL_SCANCODE_E]   && !g->keys_prev[SDL_SCANCODE_E]) ||
        (g->keys[SDL_SCANCODE_TAB] && !g->keys_prev[SDL_SCANCODE_TAB] &&
         !g->keys[SDL_SCANCODE_LSHIFT])) {
        g->codex_tab = (g->codex_tab + 1) % 4;
        g->codex_cursor = 0; g->codex_scroll = 0;
    }
    /* defilement */
    if (g->keys[SDL_SCANCODE_DOWN] && !g->keys_prev[SDL_SCANCODE_DOWN]) {
        g->codex_cursor++;
        if (g->codex_cursor >= n_total) g->codex_cursor = n_total - 1;
        if (g->codex_cursor < 0) g->codex_cursor = 0;
    }
    if (g->keys[SDL_SCANCODE_UP] && !g->keys_prev[SDL_SCANCODE_UP]) {
        g->codex_cursor--;
        if (g->codex_cursor < 0) g->codex_cursor = 0;
    }
    if (g->keys[SDL_SCANCODE_PAGEDOWN] && !g->keys_prev[SDL_SCANCODE_PAGEDOWN]) {
        g->codex_cursor += max_visible;
        if (g->codex_cursor >= n_total) g->codex_cursor = n_total - 1;
    }
    if (g->keys[SDL_SCANCODE_PAGEUP] && !g->keys_prev[SDL_SCANCODE_PAGEUP]) {
        g->codex_cursor -= max_visible;
        if (g->codex_cursor < 0) g->codex_cursor = 0;
    }
    /* auto-scroll pour garder le cursor visible */
    if (g->codex_cursor < g->codex_scroll) g->codex_scroll = g->codex_cursor;
    if (g->codex_cursor >= g->codex_scroll + max_visible) {
        g->codex_scroll = g->codex_cursor - max_visible + 1;
    }
}

