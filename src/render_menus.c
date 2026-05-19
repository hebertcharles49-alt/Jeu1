/*
 * render_menus.c - ecrans menus : hub, options, choose_hero, dead,
 * victory, title, lore, help. draw_hero_portrait reste prive ici (seul
 * render_choose_hero le consomme).
 */
#include "ui_common.h"
#include "gfx.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- HUB ---------- */

/* ============================================================
 *  HUB : "Cimetiere" pre-run avec 5 zones interactives.
 *  TEMPLE / FORGE / LICHE / TAVERNE + Porte du Donjon.
 *  Cf game.h hub_sub_open pour les sous-panneaux.
 * ============================================================ */

/* sub-panneau commun : fond translucide + cadre. Renvoie l origine x/y. */
static void sub_panel_bg(Game *g, int *out_x, int *out_y, int *out_w, int *out_h,
                          const char *title)
{
    int w = 280, h = 220;
    int x = INTERNAL_W / 2 - w / 2;
    int y = INTERNAL_H / 2 - h / 2;
    gfx_set_blend(g->renderer, true);
    fill_rect(g->renderer, 0, 0, INTERNAL_W, INTERNAL_H, 0x000000C0);
    gfx_set_blend(g->renderer, false);
    fill_rect(g->renderer, x, y, w, h, 0x14101AFF);
    rect_outline(g->renderer, x, y, w, h, 0xFFE080FF);
    text_draw(g->renderer, x + w / 2 - text_width(title) / 2, y + 8,
              title, 0xFFE080FF);
    text_drawf(g->renderer, x + w - 80, y + 8, 0xFFD040FF,
               "* %d", g->meta.shards);
    *out_x = x; *out_y = y; *out_w = w; *out_h = h;
}

/* sous-panneau TEMPLE : les 4 stats permanentes (l ancien systeme). */
static void render_hub_temple(Game *g) {
    int x, y, w, h;
    sub_panel_bg(g, &x, &y, &w, &h, "TEMPLE");
    text_draw(g->renderer, x + 10, y + 22,
              "Augmente tes capacites physiques.", 0xCCCCCCFF);
    int boxw = 124, boxh = 56, gap = 6;
    int sx0 = x + (w - 2 * boxw - gap) / 2;
    int sy0 = y + 40;
    for (int i = 0; i < 4; i++) {
        int sx = sx0 + (i % 2) * (boxw + gap);
        int sy = sy0 + (i / 2) * (boxh + gap);
        bool sel = (g->hub_sub_cursor == i);
        int level = perm_stat_level(&g->meta, i);
        int cost  = perm_stat_cost (&g->meta, i);
        bool maxed = (level >= PERM_MAX_LEVEL);
        bool ok    = (!maxed && g->meta.shards >= cost);
        fill_rect(g->renderer, sx, sy, boxw, boxh, sel ? 0x281828FF : 0x18141EFF);
        rect_outline(g->renderer, sx, sy, boxw, boxh,
                     sel ? 0xFFFF40FF : (maxed ? 0x40A040FF : (ok ? 0x404048FF : 0x603030FF)));
        text_drawf(g->renderer, sx + 6, sy + 4,
                   sel ? 0xFFFF40FF : 0xFFFFFFFF,
                   "+%d %s", perm_stat_step(i), perm_stat_label(i));
        text_drawf(g->renderer, sx + 6, sy + 14, 0x80C0FFFF,
                   "Niv %d / %d", level, PERM_MAX_LEVEL);
        int bw = boxw - 12;
        fill_rect(g->renderer, sx + 6, sy + 24, bw, 4, 0x201824FF);
        int filled = (level * bw) / PERM_MAX_LEVEL;
        fill_rect(g->renderer, sx + 6, sy + 24, filled, 4, 0x80FFC0FF);
        rect_outline(g->renderer, sx + 6, sy + 24, bw, 4, 0x40404AFF);
        if (maxed) {
            text_draw(g->renderer, sx + 6, sy + 34, "MAXIMUM", 0x80FF80FF);
        } else {
            text_drawf(g->renderer, sx + 6, sy + 34,
                       ok ? 0xFFD040FF : 0xC07070FF, "%d eclats", cost);
        }
    }
    text_draw(g->renderer, x + w / 2 - text_width("ECHAP POUR FERMER") / 2,
              y + h - 14, "ECHAP POUR FERMER", 0xFFFF80FF);
}

/* sous-panneau FORGE : 6 armes (W_FISTS..W_AXE) avec upgrade +dmg. */
static void render_hub_forge(Game *g) {
    int x, y, w, h;
    sub_panel_bg(g, &x, &y, &w, &h, "FORGE");
    text_draw(g->renderer, x + 10, y + 22,
              "Ameliore le degat de base de chaque arme.", 0xCCCCCCFF);
    int rowh = 22;
    for (int k = 0; k < W_COUNT; k++) {
        int sy = y + 38 + k * rowh;
        bool sel = (g->hub_sub_cursor == k);
        bool seen = (k == W_FISTS) || g->meta.weapon_discovered[k];
        int lvl = forge_level(&g->meta, (WeaponKind)k);
        int cost = forge_cost(&g->meta, (WeaponKind)k);
        bool maxed = (lvl >= FORGE_MAX_LEVEL);
        bool ok    = seen && !maxed && g->meta.shards >= cost;
        uint32_t bg = sel ? 0x281828FF : 0x18141EFF;
        uint32_t bd = sel ? 0xFFFF40FF :
                      (!seen ? 0x404048FF :
                      (maxed ? 0x40A040FF : (ok ? 0x504048FF : 0x603030FF)));
        fill_rect(g->renderer, x + 10, sy, w - 20, rowh - 2, bg);
        rect_outline(g->renderer, x + 10, sy, w - 20, rowh - 2, bd);
        if (!seen) {
            text_draw(g->renderer, x + 16, sy + 6, "??????", 0x606060FF);
        } else {
            text_drawf(g->renderer, x + 16, sy + 6,
                       sel ? 0xFFFF40FF : 0xFFFFFFFF,
                       "%s  +%d dmg", weapon_name((WeaponKind)k), lvl * 5);
            /* mini progress bar */
            int bw = 50;
            fill_rect(g->renderer, x + 130, sy + 8, bw, 4, 0x201824FF);
            int fw = (lvl * bw) / FORGE_MAX_LEVEL;
            fill_rect(g->renderer, x + 130, sy + 8, fw, 4, 0x80FFC0FF);
            rect_outline(g->renderer, x + 130, sy + 8, bw, 4, 0x40404AFF);
            if (maxed) {
                text_draw(g->renderer, x + w - 80, sy + 6, "MAXIMUM", 0x80FF80FF);
            } else {
                text_drawf(g->renderer, x + w - 80, sy + 6,
                           ok ? 0xFFD040FF : 0xC07070FF,
                           "%d eclats", cost);
            }
        }
    }
    text_draw(g->renderer, x + w / 2 - text_width("ECHAP POUR FERMER") / 2,
              y + h - 14, "ECHAP POUR FERMER", 0xFFFF80FF);
}

/* sous-panneau LICHE : stub (modificateurs de run a venir). */
static void render_hub_liche(Game *g) {
    int x, y, w, h;
    sub_panel_bg(g, &x, &y, &w, &h, "LICHE");
    text_draw(g->renderer, x + w / 2 - text_width("Modificateurs de run") / 2,
              y + 60, "Modificateurs de run", 0xFFE080FF);
    text_draw(g->renderer, x + w / 2 - text_width("Disponible prochainement.") / 2,
              y + 80, "Disponible prochainement.", 0xCCCCCCFF);
    text_draw(g->renderer, x + w / 2 -
              text_width("La Liche tissera des anomalies sur ta course :") / 2,
              y + 110, "La Liche tissera des anomalies sur ta course :", 0x808080FF);
    text_draw(g->renderer, x + 30, y + 124,
              "- ennemis empoisonnes pour +bounty", 0x606060FF);
    text_draw(g->renderer, x + 30, y + 134,
              "- portes a sens unique", 0x606060FF);
    text_draw(g->renderer, x + 30, y + 144,
              "- malediction de tempete", 0x606060FF);
    text_draw(g->renderer, x + w / 2 - text_width("ECHAP POUR FERMER") / 2,
              y + h - 14, "ECHAP POUR FERMER", 0xFFFF80FF);
}

/* HUB walkable : le 3D est rendu via render_world(), donc ici on ne
 * dessine plus que le HUD overlay (eclats / compteurs / raccourcis)
 * et le sous-panneau ouvert le cas echeant. */
void render_hub(Game *g) {
    GfxCtx *gc = g->renderer;

    /* etiquettes 3D au-dessus de chaque batiment, projetees a l ecran. */
    int nbld = hub_building_count();
    for (int i = 0; i < nbld; i++) {
        const HubBuilding *b = hub_building_get(i);
        if (!b) continue;
        v3 head = v3_make(hub_building_x(b) / (float)TILE,
                          2.6f,
                          hub_building_y(b) / (float)TILE);
        int sx, sy;
        if (!world_to_screen(gc, head, &sx, &sy)) continue;
        const char *nm = hub_building_name(b);
        int tw = text_width(nm);
        int near_i = (i == g->hub_cursor);
        uint32_t col = near_i ? 0xFFFF80FF : 0xFFE0A0FF;
        /* fond sombre */
        gfx_set_blend(gc, true);
        fill_rect(gc, sx - tw/2 - 3, sy - 2, tw + 6, 10, 0x000000B0);
        gfx_set_blend(gc, false);
        text_draw(gc, sx - tw/2 + 1, sy + 1 - 0, nm, 0x000000FF);
        text_draw(gc, sx - tw/2,     sy,         nm, col);
    }

    /* HUD top : eclats + compteurs */
    fill_rect(gc, 0, 0, INTERNAL_W, 14, 0x000000A0);
    text_drawf(gc, 8, 4, 0xFFD040FF, "* %d ECLATS", g->meta.shards);
    text_drawf(gc, INTERNAL_W - 220, 4, 0xCCCCCCFF,
               "Courses %d   Meilleur %d/%d   Victoires %d",
               g->meta.total_runs, g->meta.best_floor, MAX_FLOORS,
               g->meta.victories);
    /* titre discret au centre */
    text_draw(gc, INTERNAL_W/2 - text_width("LE CIMETIERE")/2, 4,
              "LE CIMETIERE", 0xFFE080FF);

    /* raccourcis bas */
    int by = INTERNAL_H - 12;
    fill_rect(gc, 0, by - 2, INTERNAL_W, 14, 0x000000A0);
    const char *labels[3] = { "[O] OPTIONS", "[K] CODEX", "[H] AIDE" };
    int total_w = 0;
    for (int i = 0; i < 3; i++) total_w += text_width(labels[i]) + 16;
    int x = INTERNAL_W/2 - total_w/2;
    for (int i = 0; i < 3; i++) {
        text_draw(gc, x, by, labels[i], 0xCCCCCCFF);
        x += text_width(labels[i]) + 16;
    }

    /* prompt [E] interaction : le nom du batiment proche + action */
    int near = g->hub_cursor;
    int n = hub_building_count();
    if (g->hub_sub_open == 0 && near >= 0 && near < n) {
        const HubBuilding *b = hub_building_get(near);
        if (b) {
            const char *act;
            int sid = hub_building_sub_id(b);
            switch (sid) {
                case  0: act = "ENTRER DANS LE DONJON"; break;
                case -1: act = "RECRUTER UN HEROS";     break;
                case  1: act = "AMELIORATIONS";         break;
                case  2: act = "FORGER UNE ARME";       break;
                case  3: act = "MODIFICATEURS";         break;
                default: act = "INTERAGIR";             break;
            }
            char buf[96];
            snprintf(buf, sizeof(buf), "[E] %s -- %s",
                     hub_building_name(b), act);
            int tw = text_width(buf);
            int px = INTERNAL_W/2 - tw/2;
            int py = INTERNAL_H - 30;
            fill_rect(gc, px - 4, py - 2, tw + 8, 11, 0x000000C0);
            rect_outline(gc, px - 4, py - 2, tw + 8, 11, 0xFFE080FF);
            text_draw(gc, px, py, buf, 0xFFFF80FF);
        }
    }

    /* sous-panneau actif par-dessus */
    if (g->hub_sub_open == 1) render_hub_temple(g);
    if (g->hub_sub_open == 2) render_hub_forge(g);
    if (g->hub_sub_open == 3) render_hub_liche(g);
}

/* ---------- OPTIONS ---------- */

void render_options(Game *g) {
    fill_rect(g->renderer, 0, 0, INTERNAL_W, INTERNAL_H, 0x080612FF);
    text_draw(g->renderer, INTERNAL_W/2 - text_width("OPTIONS")/2, 8,
              "OPTIONS", 0xFFE080FF);

    /* tabs */
    const char *tabs[3] = { "CONTROLES", "AUDIO", "VIDEO" };
    int tabw = 100, tabh = 14;
    int tx = (INTERNAL_W - tabw * 3 - 8) / 2;
    for (int i = 0; i < 3; i++) {
        bool sel = (g->opt_section == i);
        fill_rect(g->renderer, tx, 22, tabw, tabh, sel ? 0x303060FF : 0x18181EFF);
        rect_outline(g->renderer, tx, 22, tabw, tabh, sel ? 0xFFFF80FF : 0x404048FF);
        text_draw(g->renderer, tx + (tabw - text_width(tabs[i])) / 2, 26,
                  tabs[i], sel ? 0xFFFF80FF : 0xCCCCCCFF);
        tx += tabw + 4;
    }

    int y = 50;
    if (g->opt_section == 0) {
        text_draw(g->renderer, 30, y, "ACTION", 0xCCCCCCFF);
        text_draw(g->renderer, 280, y, "TOUCHE", 0xCCCCCCFF);
        y += 12;
        for (int i = 0; i < BIND_COUNT; i++) {
            bool sel = (g->opt_cursor == i);
            uint32_t col = sel ? 0xFFFF40FF : 0xFFFFFFFF;
            text_drawf(g->renderer, sel ? 22 : 30, y, col, "%s%s",
                       sel ? "> " : "  ", bind_action_name((BindAction)i));
            const char *kn = scancode_label(g->settings.keys[i]);
            text_draw(g->renderer, 280, y, kn, col);
            y += 11;
        }
        y += 6;
        text_draw(g->renderer, 30, y, "WASD / FLECHES sont reserves au deplacement.", 0x808080FF); y += 9;
        text_draw(g->renderer, 30, y, "ENTREE = remapper   GAUCHE/DROITE = restaurer defaut", 0x808080FF);
    } else if (g->opt_section == 1) {
        const char *labels[2] = { "Couper le son (mute)", "Volume" };
        for (int i = 0; i < 2; i++) {
            bool sel = (g->opt_cursor == i);
            uint32_t col = sel ? 0xFFFF40FF : 0xFFFFFFFF;
            text_drawf(g->renderer, sel ? 22 : 30, y, col, "%s%s",
                       sel ? "> " : "  ", labels[i]);
            if (i == 0) {
                text_draw(g->renderer, 280, y,
                          g->settings.sfx_mute ? "ON" : "OFF", col);
            } else {
                /* volume bar */
                int bx = 280, bw = 80;
                fill_rect(g->renderer, bx, y, bw, 6, 0x202028FF);
                int filled = (g->settings.sfx_volume * bw) / 4;
                fill_rect(g->renderer, bx, y, filled, 6, sel ? 0xFFFF80FF : 0x80C0FFFF);
                rect_outline(g->renderer, bx, y, bw, 6, 0x404048FF);
                text_drawf(g->renderer, bx + bw + 6, y, col, "%d/4", g->settings.sfx_volume);
            }
            y += 11;
        }
        y += 6;
        text_draw(g->renderer, 30, y, "ENTREE bascule mute.   GAUCHE/DROITE ajuste le volume.", 0x808080FF);
    } else if (g->opt_section == 2) {
        const char *labels[3] = {
            "DLSS Generatif",
            "Debug : salle bac-a-sable",
            "Barres de vie flottantes"
        };
        const char *vals[3]   = {
            g->settings.dlss_on        ? "ON  (lisse)"  : "OFF (pixel art net)",
            g->settings.debug_room     ? "ON"           : "OFF",
            g->settings.mob_healthbars ? "ON"           : "OFF"
        };
        for (int i = 0; i < 3; i++) {
            bool sel = (g->opt_cursor == i);
            uint32_t col = sel ? 0xFFFF40FF : 0xFFFFFFFF;
            text_drawf(g->renderer, sel ? 22 : 30, y, col, "%s%s",
                       sel ? "> " : "  ", labels[i]);
            text_draw(g->renderer, 280, y, vals[i], col);
            y += 11;
        }
        y += 6;
        text_draw(g->renderer, 30, y, "DLSS : filtrage lineaire AI-like sur la sortie finale.", 0x808080FF); y += 9;
        text_draw(g->renderer, 30, y, "Debug : ajoute une salle a cote de l'entree, peuplee", 0x808080FF); y += 9;
        text_draw(g->renderer, 30, y, "d'un exemplaire de chaque arme / element / equipement", 0x808080FF); y += 9;
        text_draw(g->renderer, 30, y, "legendaire (effet a la prochaine run / etage).", 0x808080FF); y += 9;
        text_draw(g->renderer, 30, y, "Barres : visibles meme a pleine vie pour lire le combat.", 0x808080FF); y += 9;
        text_draw(g->renderer, 30, y, "ENTREE bascule.", 0x808080FF);
    }

    if (g->opt_waiting_rebind) {
        gfx_set_blend(g->renderer, true);
        fill_rect(g->renderer, 0, INTERNAL_H/2 - 16, INTERNAL_W, 32, 0x000000C0);
        gfx_set_blend(g->renderer, false);
        const char *msg = "APPUIE SUR UNE TOUCHE...   (ECHAP POUR ANNULER)";
        text_draw(g->renderer, INTERNAL_W/2 - text_width(msg)/2,
                  INTERNAL_H/2 - 4, msg, 0xFFFF40FF);
    }

    if (g->opt_msg_t > 0.f) {
        text_draw(g->renderer, INTERNAL_W/2 - text_width(g->opt_msg)/2,
                  INTERNAL_H - 32, g->opt_msg, 0xFFFF40FF);
    }
    text_draw(g->renderer, INTERNAL_W/2 - text_width("Q/TAB SECTION   FLECHES NAVIGUER   ECHAP RETOUR")/2,
              INTERNAL_H - 18, "Q/TAB SECTION   FLECHES NAVIGUER   ECHAP RETOUR", 0xCCCCCCFF);
}


/* ---------- HERO SELECT ---------- */
static void draw_hero_portrait(Game *g, int sx, int sy, HeroClass h,
                               bool selected, bool discovered) {
    uint32_t cape, tunic;
    hero_palette(h, &cape, &tunic);
    if (!discovered) { cape = 0x101010FF; tunic = 0x202020FF; }
    if (selected) rect_outline(g->renderer, sx - 22, sy - 30, 44, 60, 0xFFFF40FF);
    fill_rect(g->renderer, sx - 14, sy - 16, 28, 32, cape);
    fill_rect(g->renderer, sx - 12, sy - 8, 24, 22, tunic);
    fill_rect(g->renderer, sx - 8, sy - 22, 16, 12,
              discovered ? 0xE8C089FF : 0x303030FF);
    fill_rect(g->renderer, sx - 8, sy - 24, 16, 4,
              discovered ? 0x402010FF : 0x101010FF);
    if (discovered) {
        fill_rect(g->renderer, sx - 5, sy - 18, 3, 3, 0x000000FF);
        fill_rect(g->renderer, sx + 2, sy - 18, 3, 3, 0x000000FF);
    } else {
        /* point d'interrogation gigantesque */
        text_draw(g->renderer, sx - 3, sy - 22, "?", 0x808080FF);
    }
}

void render_choose_hero(Game *g) {
    /* fond degrade */
    for (int yy = 0; yy < INTERNAL_H; yy++) {
        int v = 6 + (INTERNAL_H - yy) / 32;
        fill_rect(g->renderer, 0, yy, INTERNAL_W, 1,
                  (uint32_t)((v << 24) | (v << 16) | ((v + 4) << 8) | 0xFF));
    }
    text_draw(g->renderer, INTERNAL_W/2 - text_width("CHOISIS TON HEROS")/2, 10,
              "CHOISIS TON HEROS", 0xFFE080FF);
    text_drawf(g->renderer, 8, 24, 0xC0E0FFFF, "ECLATS %d", g->meta.shards);

    int discovered_n = 0;
    for (int i = 0; i < HERO_COUNT; i++) if (g->meta.hero_discovered[i]) discovered_n++;
    text_drawf(g->renderer, INTERNAL_W - 110, 24, 0xCCCCCCFF,
               "DECOUVERTS %d/%d", discovered_n, HERO_COUNT);

    /* 2 rangees de 5 portraits */
    int per_row = 5;
    int gap_x = INTERNAL_W / (per_row + 1);
    int row_y[2] = { (INTERNAL_H * 4) / 12, (INTERNAL_H * 8) / 12 };
    for (int i = 0; i < HERO_COUNT; i++) {
        int row = i / per_row;
        int col = i % per_row;
        int sx = gap_x * (col + 1);
        int sy = row_y[row];
        bool sel = (g->hero_cursor == i);
        bool disc = g->meta.hero_discovered[i];
        draw_hero_portrait(g, sx, sy, (HeroClass)i, sel, disc);
        const char *n = disc ? hero_name((HeroClass)i) : "??????";
        uint32_t c = disc ? (sel ? 0xFFFF40FF : 0xFFFFFFFF) : 0x606060FF;
        text_draw(g->renderer, sx - text_width(n) / 2, sy + 22, n, c);
        if (disc && !g->meta.hero_unlocked[i]) {
            /* badge "LOCK" + cout colore selon la solvabilite */
            int cost = 60 + i * 25;
            bool can = (g->meta.shards >= cost);
            uint32_t col = can ? 0x80FF80FF : 0xFF8080FF;
            /* petit cadenas dessine en pixels (3x4) */
            int lx = sx - 8, ly = sy - 26;
            fill_rect(g->renderer, lx, ly, 7, 5, 0x000000FF);
            fill_rect(g->renderer, lx + 1, ly + 1, 5, 3, 0xC0A040FF);
            fill_rect(g->renderer, lx + 2, ly - 2, 3, 2, 0xC0A040FF);
            fill_rect(g->renderer, lx + 1, ly - 2, 1, 2, 0xC0A040FF);
            fill_rect(g->renderer, lx + 5, ly - 2, 1, 2, 0xC0A040FF);
            /* cout : icone coin + texte */
            char b[24]; snprintf(b, sizeof(b), "%d", cost);
            int bw = text_width(b);
            text_draw(g->renderer, sx - bw/2 + 6, sy + 32, b, col);
            fill_rect(g->renderer, sx - bw/2 - 2, sy + 33, 5, 5, col);
        } else if (disc) {
            /* deja debloque : petit checkmark vert */
            int cx = sx + 14, cy = sy - 22;
            fill_rect(g->renderer, cx,     cy + 2, 1, 2, 0x80FF80FF);
            fill_rect(g->renderer, cx + 1, cy + 3, 1, 2, 0x80FF80FF);
            fill_rect(g->renderer, cx + 2, cy + 2, 1, 1, 0x80FF80FF);
            fill_rect(g->renderer, cx + 3, cy + 1, 1, 1, 0x80FF80FF);
            fill_rect(g->renderer, cx + 4, cy,     1, 1, 0x80FF80FF);
        }
    }

    /* description */
    HeroClass cur = (HeroClass)g->hero_cursor;
    if (g->meta.hero_discovered[cur]) {
        text_draw(g->renderer, INTERNAL_W/2 - text_width(hero_desc(cur))/2,
                  INTERNAL_H - 56, hero_desc(cur), 0xCCCCFFFF);
        if (!g->meta.hero_unlocked[cur]) {
            int cost = 60 + (int)cur * 25;
            bool can = (g->meta.shards >= cost);
            char msg[64];
            snprintf(msg, sizeof(msg),
                     can ? "VERROUILLE - CLIC POUR DEBLOQUER (%d eclats)"
                         : "VERROUILLE - manque %d eclats",
                     can ? cost : (cost - g->meta.shards));
            text_draw(g->renderer, INTERNAL_W/2 - text_width(msg)/2,
                      INTERNAL_H - 42, msg,
                      can ? 0xFFC080FF : 0xFF8080FF);
        } else {
            text_draw(g->renderer,
                      INTERNAL_W/2 - text_width("CLIC OU ENTREE POUR PARTIR EN COURSE")/2,
                      INTERNAL_H - 42, "CLIC OU ENTREE POUR PARTIR EN COURSE", 0x80FF80FF);
        }
    } else {
        text_draw(g->renderer,
                  INTERNAL_W/2 - text_width("HEROS INCONNU - terrasse un boss pour le reveler")/2,
                  INTERNAL_H - 42,
                  "HEROS INCONNU - terrasse un boss pour le reveler",
                  0x808080FF);
    }
    text_draw(g->renderer, INTERNAL_W/2 - text_width("A/D NAVIGUER   SOURIS HOVER/CLIC   ECHAP RETOUR")/2,
              INTERNAL_H - 18, "A/D NAVIGUER   SOURIS HOVER/CLIC   ECHAP RETOUR",
              0xCCCCCCFF);
}


void render_dead(Game *g) {
    gfx_set_blend(g->renderer, true);
    fill_rect(g->renderer, 0, 0, INTERNAL_W, INTERNAL_H, 0x300010DC);
    gfx_set_blend(g->renderer, false);
    /* titre */
    int tw = text_width("VAINCU");
    int tx = INTERNAL_W/2 - tw/2;
    text_draw(g->renderer, tx + 1, 50 + 1, "VAINCU", 0x000000FF);
    text_draw(g->renderer, tx,     50,     "VAINCU", 0xFF4060FF);
    text_draw(g->renderer, INTERNAL_W/2 - text_width("Ta course s'acheve.")/2,
              66, "Ta course s'acheve.", 0xCCCCCCFF);

    /* panneau stats centre */
    int px = INTERNAL_W/2 - 110;
    int py = 84;
    int pw = 220, ph = 110;
    fill_rect(g->renderer, px, py, pw, ph, 0x14101AFF);
    rect_outline(g->renderer, px, py, pw, ph, 0x80405080);
    text_draw(g->renderer, px + 6, py + 4, "RESUME DE COURSE", 0xFFE080FF);

    /* deux colonnes */
    int col1 = px + 6;
    int col2 = px + 110;
    int row = py + 18;

    /* helper inline : ligne "label    valeur" couleur valeur */
    #define ROW(label, val_fmt, vcol, ...) do {                          \
        text_draw(g->renderer, col1, row, label, 0xCCCCCCFF);            \
        text_drawf(g->renderer, col2, row, vcol, val_fmt, __VA_ARGS__);  \
        row += 11;                                                       \
    } while (0)

    int hero_name_color = 0xFF80FFFF;
    text_drawf(g->renderer, col1, row, hero_name_color, "%s", hero_name(g->player.hero));
    row += 11;
    ROW("Etage atteint",  "%d / %d",     0xFFFFFFFF, g->floor_index, MAX_FLOORS);
    ROW("Temps de run",   "%.1fs",       0xFFFFFFFF, g->run_time);
    ROW("Kills",          "%d",          0xFFFFFFFF, g->run_kills);
    ROW("Degats infliges","%d",          0xFFA070FF, g->run_damage_dealt);
    /* meilleur combo : 1 = solo, 2 = paire, 3 = triple */
    {
        const char *combo_label = "neant";
        uint32_t combo_col = 0x808080FF;
        if (g->run_best_combo_size == 1) { combo_label = "Solo";   combo_col = rarity_color(R_COMMON); }
        else if (g->run_best_combo_size == 2) { combo_label = "Paire";  combo_col = rarity_color(R_RARE); }
        else if (g->run_best_combo_size == 3) { combo_label = "Triple"; combo_col = rarity_color(R_EPIC); }
        text_draw(g->renderer, col1, row, "Meilleur combo", 0xCCCCCCFF);
        text_drawf(g->renderer, col2, row, combo_col, "%s",
                   g->run_best_combo_size > 0 ? combo_label : "—");
        row += 11;
    }
    ROW("Drops legendaires", "%d", 0xFFD030FF, g->run_legendary_drops);
    ROW("Ames recoltees", "%d",    0xC0FFC0FF, g->player.souls);
    ROW("Seed",          "%u",     0x808080FF, g->run_seed);
    #undef ROW

    /* gain permanent en bas */
    int gain = g->player.souls + g->run_kills / 4 + g->floor_index * 5;
    text_drawf(g->renderer, INTERNAL_W/2 - 100, py + ph + 8, 0xFFE080FF,
               "GAIN PERMANENT  +%d ECLATS", gain);
    text_draw(g->renderer, INTERNAL_W/2 - text_width("ENTREE POUR RETOUR AU SANCTUAIRE")/2,
              INTERNAL_H - 20, "ENTREE POUR RETOUR AU SANCTUAIRE", 0xFFFFFFFF);
}

/* ---------- VICTORY ---------- */
void render_victory(Game *g) {
    gfx_set_blend(g->renderer, true);
    fill_rect(g->renderer, 0, 0, INTERNAL_W, INTERNAL_H, 0x102030E0);
    gfx_set_blend(g->renderer, false);
    /* fireworks */
    for (int i = 0; i < 80; i++) {
        int x = (int)((sinf(g->time + i) * 0.5f + 0.5f) * INTERNAL_W);
        int y = (int)((cosf(g->time * 1.3f + i) * 0.5f + 0.5f) * INTERNAL_H);
        uint32_t cols[5] = { 0xFFD040FF, 0xFF80C0FF, 0x80E0FFFF, 0xC080FFFF, 0xFFE080FF };
        fill_rect(g->renderer, x, y, 2, 2, cols[i % 5]);
    }
    text_draw(g->renderer, INTERNAL_W/2 - text_width("VICTOIRE")*2/2, 50,
              "VICTOIRE", 0xFFE080FF);
    text_draw(g->renderer, INTERNAL_W/2 - text_width("LES 10 ETAGES SONT TOMBES")/2, 80,
              "LES 10 ETAGES SONT TOMBES", 0xFFFFFFFF);
    text_drawf(g->renderer, INTERNAL_W/2 - 70, 100, 0xCCCCFFFF,
               "KILLS %d   AMES %d   PIECES %d",
               g->run_kills, g->player.souls, g->player.coins);
    text_draw(g->renderer, INTERNAL_W/2 - text_width("ENTREE POUR LE SANCTUAIRE")/2,
              160, "ENTREE POUR LE SANCTUAIRE", 0x80FF80FF);
}

/* ---------- TITLE ---------- */
void render_title(Game *g) {
    /* fond degrade vertical sombre */
    for (int y = 0; y < INTERNAL_H; y++) {
        int v = 8 + (INTERNAL_H - y) / 24;
        fill_rect(g->renderer, 0, y, INTERNAL_W, 1, (uint32_t)((v << 24) | (v / 2 << 16) | (v << 8) | 0xFF));
    }
    /* particules embers / cendres animees */
    for (int i = 0; i < 100; i++) {
        int x = (i * 73 + (int)(g->time * 14)) % INTERNAL_W;
        int y = ((i * 37) + (int)(g->time * (i % 7 + 2) * 6)) % INTERNAL_H;
        uint32_t col = (i & 3) ? 0x402030FF : 0xFFB060FF;
        fill_rect(g->renderer, x, y, 1, 1, col);
    }

    /* titre chevele : ombre + coeur + glow */
    const char *t = "ELEMENT DUNGEON";
    int tw = text_width(t);
    int tx = INTERNAL_W/2 - tw/2;
    int ty = INTERNAL_H/2 - 60;
    /* glow */
    for (int dx = -2; dx <= 2; dx++) for (int dy = -2; dy <= 2; dy++) {
        if (!dx && !dy) continue;
        text_draw(g->renderer, tx + dx, ty + dy, t, 0x402010FF);
    }
    text_draw(g->renderer, tx, ty + 1, t, 0x000000FF);
    text_draw(g->renderer, tx, ty, t, 0xFFD060FF);

    const char *tagline = "ROGUELITE ELEMENTAIRE";
    text_draw(g->renderer, INTERNAL_W/2 - text_width(tagline)/2,
              INTERNAL_H/2 - 38, tagline, 0xC0A080FF);

    /* menu vertical (lore retire : distille en jeu via parchemins) */
    const char *items[4] = { "JOUER", "OPTIONS", "AIDE", "QUITTER" };
    int yA = INTERNAL_H/2 + 20;
    int rowh = 16;
    for (int i = 0; i < 4; i++) {
        int yi = yA + i * rowh;
        bool hov = mouse_in_rect(g, INTERNAL_W/2 - 100, yi, 200, 12);
        uint32_t col = hov ? 0xFFFF80FF : (i == 0 ? 0x80FFA0FF : 0xCCCCCCFF);
        int w = text_width(items[i]);
        text_draw(g->renderer, INTERNAL_W/2 - w/2, yi + 2, items[i], col);
        if (hov) {
            /* fleches */
            text_draw(g->renderer, INTERNAL_W/2 - w/2 - 16, yi + 2, ">", col);
            text_draw(g->renderer, INTERNAL_W/2 + w/2 + 10, yi + 2, "<", col);
        }
    }

    text_draw(g->renderer, 8, INTERNAL_H - 14, "v1 -- C + SDL2", 0x606080FF);
}

/* ---------- LORE ---------- */
void render_lore(Game *g) {
    /* fond noir avec etoiles bleutees */
    fill_rect(g->renderer, 0, 0, INTERNAL_W, INTERNAL_H, 0x040208FF);
    for (int i = 0; i < 80; i++) {
        int x = (i * 53 + (int)(g->time * 6)) % INTERNAL_W;
        int y = ((i * 91) ) % INTERNAL_H;
        fill_rect(g->renderer, x, y, 1, 1, ((i % 3) ? 0x404068FF : 0x80A0E0FF));
    }
    text_draw(g->renderer, INTERNAL_W/2 - text_width("LE PROLOGUE")/2, 18,
              "LE PROLOGUE", 0xFFD060FF);

    const char *paragraphs[] = {
        "Il y a sept mille ans, le Cristal Originel se brisa dans le ciel.",
        "Sept eclats tomberent dans l'abime. Chacun devint un element :",
        "FEU, EAU, TERRE, FOUDRE, AIR, VIDE et FEE.",
        "",
        "Les rois batirent un Donjon pour les contenir. Sous terre, dix",
        "etages, dix cles, dix Gardiens. Le temps fit son oeuvre :",
        "les murs furent oublies, les eclats endormis sous la mousse.",
        "",
        "La porte vient de s'ouvrir. Quelque chose remonte. Les Gardiens",
        "se reveillent. Les ennemis qui te traquent portent les couleurs",
        "des elements -- frappe-les avec leur faiblesse.",
        "",
        "Toi, Heros sans nom : descend. Combine les eclats. Forge ton",
        "arme, ta classe, ta legende. Ou meurs comme tous les autres.",
        "",
        "                            *      *      *",
    };
    int n = (int)(sizeof(paragraphs) / sizeof(paragraphs[0]));
    int y = 50;
    for (int i = 0; i < n; i++) {
        int w = text_width(paragraphs[i]);
        uint32_t col = 0xCCCCDDFF;
        if (i == 2 || i == 9 || i == 10) col = 0xFFD0A0FF;
        text_draw(g->renderer, INTERNAL_W/2 - w/2, y, paragraphs[i], col);
        y += 11;
    }
    text_draw(g->renderer, INTERNAL_W/2 - text_width("ENTREE / CLIC / ECHAP POUR REVENIR")/2,
              INTERNAL_H - 18, "ENTREE / CLIC / ECHAP POUR REVENIR", 0xFFFF80FF);
}

/* ---------- HELP ---------- */
void render_help(Game *g) {
    fill_rect(g->renderer, 0, 0, INTERNAL_W, INTERNAL_H, 0x080612FF);
    text_draw(g->renderer, 8, 6, "AIDE", 0xFFE080FF);
    int y = 18;
    text_draw(g->renderer, 8, y, "WASD / FLECHES   DEPLACEMENT", 0xFFFFFFFF); y += 9;
    text_draw(g->renderer, 8, y, "SOURIS           VISER", 0xFFFFFFFF); y += 9;
    text_draw(g->renderer, 8, y, "CLIC GAUCHE      ATTAQUER (maintenir pour enchainer)", 0xFFFFFFFF); y += 9;
    text_draw(g->renderer, 8, y, "ESPACE           DASH", 0xFFFFFFFF); y += 9;
    text_draw(g->renderer, 8, y, "1 / 2 / TAB      ARME ACTIVE", 0xFFFFFFFF); y += 9;
    text_draw(g->renderer, 8, y, "I                INVENTAIRE", 0xFFFFFFFF); y += 9;
    text_draw(g->renderer, 8, y, "ECHAP            ABANDONNER / QUITTER", 0xFFFFFFFF); y += 12;
    text_draw(g->renderer, 8, y, "REGLES", 0xFFE080FF); y += 9;
    text_draw(g->renderer, 8, y, "Tu commences avec tes POINGS sur 2 slots", 0xCCCCCCFF); y += 9;
    text_draw(g->renderer, 8, y, "Trouve / achete des armes pour debloquer ta sous-classe", 0xCCCCCCFF); y += 9;
    text_draw(g->renderer, 8, y, "Greffe jusqu'a 3 elements par arme (ramassage = arme active)", 0xCCCCCCFF); y += 9;
    text_draw(g->renderer, 8, y, "Nettoie les pieces, abats le BOSS, prends le portail", 0xCCCCCCFF); y += 9;
    text_draw(g->renderer, 8, y, "Shop entre etages: pieces dorees", 0xCCCCCCFF); y += 9;
    text_draw(g->renderer, 8, y, "Inventaire: 12 slots + 6 equipements (casque/torse/etc)", 0xCCCCCCFF); y += 9;
    text_draw(g->renderer, 8, y, "FUSION: si 3 items identiques presents -> F les fusionne automatiquement", 0xCCCCCCFF); y += 9;
    text_draw(g->renderer, 8, y, "Raretes: Commun < Magique < Rare < Epique < Legendaire (+100%)", 0xCCCCCCFF); y += 12;
    text_draw(g->renderer, 8, y, "ELEMENTS (sensibilites Pokemon-like):", 0xFFE080FF); y += 9;
    text_draw(g->renderer, 8, y, " EAU > FEU   FOUDRE > EAU   AIR > FOUDRE   TERRE > AIR", 0x80FFC0FF); y += 9;
    text_draw(g->renderer, 8, y, " VIDE <-> FEE (mutuels)   FEU > FEE   EAU > TERRE", 0x80FFC0FF); y += 12;
    text_draw(g->renderer, 8, y, "ELITES: brillent dans leur couleur. Frappe avec leur faiblesse", 0xFFC0C0FF); y += 9;
    text_draw(g->renderer, 8, y, "10 etages, 1 boss par etage, +15% stats ennemis a chaque etage", 0xFFC0C0FF); y += 9;
    text_draw(g->renderer, 8, INTERNAL_H - 12, "ECHAP POUR REVENIR", 0xFFFF80FF);
}

