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

extern int  hub_perm_cost(int kind);

/* ---------- HUB ---------- */
/* helpers extern (fournis par main.c -> shop_recipe etc) */
extern int  hub_perm_cost(int kind);
/* main.c uses static; redefine local labels for buttons */

static const char *perm_btn_label(int k) {
    switch (k) {
        case 0: return "+10 PV MAX";
        case 1: return "+1 ARMURE";
        case 2: return "+5 VITESSE";
        case 3: return "+5% DEGATS";
        default: return "?";
    }
}
static int perm_btn_cost(int k) {
    switch (k) {
        case 0: return 40;
        case 1: return 60;
        case 2: return 50;
        case 3: return 70;
        default: return 999;
    }
}

void render_hub(Game *g) {
    /* fond degrade */
    for (int yy = 0; yy < INTERNAL_H; yy++) {
        int v = 6 + (INTERNAL_H - yy) / 36;
        fill_rect(g->renderer, 0, yy, INTERNAL_W, 1,
                  (uint32_t)((v << 24) | ((v / 2) << 16) | ((v) << 8) | 0xFF));
    }
    /* embers */
    for (int i = 0; i < 70; i++) {
        int x = (i * 73 + (int)(g->time * 12)) % INTERNAL_W;
        int y = ((i * 37) + (int)(g->time * (i % 5 + 2) * 5)) % INTERNAL_H;
        fill_rect(g->renderer, x, y, 1, 1, (i & 3) ? 0x301820FF : 0xFFA060FF);
    }

    text_draw(g->renderer, INTERNAL_W/2 - text_width("LE SANCTUAIRE")/2, 10,
              "LE SANCTUAIRE", 0xFFE080FF);
    text_drawf(g->renderer, 8, 26, 0xC0E0FFFF,
               "ECLATS %d   COURSES %d   MEILLEUR %d/%d   VICTOIRES %d",
               g->meta.shards, g->meta.total_runs, g->meta.best_floor,
               MAX_FLOORS, g->meta.victories);
    text_draw(g->renderer, 8, 38,
              "ACHATS PERMANENTS - Augmente tes stats pour les futures courses",
              0x80FFC0FF);

    /* 4 boutons stats permanents */
    int boxw = 118, boxh = 50, gap = 6;
    int total_w = 4 * boxw + 3 * gap;
    int sx0 = (INTERNAL_W - total_w) / 2;
    int sy = 60;
    for (int i = 0; i < 4; i++) {
        int sx = sx0 + i * (boxw + gap);
        bool sel = (g->hub_cursor == i);
        bool ok = (g->meta.shards >= perm_btn_cost(i));
        fill_rect(g->renderer, sx, sy, boxw, boxh, sel ? 0x281828FF : 0x14101AFF);
        rect_outline(g->renderer, sx, sy, boxw, boxh,
                     sel ? 0xFFFF40FF : (ok ? 0x404048FF : 0x60303AFF));
        text_draw(g->renderer, sx + 8, sy + 8, perm_btn_label(i),
                  sel ? 0xFFFF40FF : 0xFFFFFFFF);
        text_drawf(g->renderer, sx + 8, sy + 24, 0xFFD040FF, "%d", perm_btn_cost(i));
        fill_rect(g->renderer, sx + 8 + 16, sy + 25, 5, 5, 0xFFD040FF);
        /* etat actuel */
        int cur = 0;
        switch (i) {
            case 0: cur = g->meta.perm_hp; break;
            case 1: cur = g->meta.perm_armor; break;
            case 2: cur = g->meta.perm_speed; break;
            case 3: cur = g->meta.perm_dmg_pct; break;
        }
        text_drawf(g->renderer, sx + 8, sy + 36, 0x80FFC0FF, "actuel +%d", cur);
    }

    /* CODEX */
    int cx = 24, cy = 124;
    text_draw(g->renderer, cx, cy, "CODEX  -  ARMES", 0xFFFF80FF);
    int discovered_w = 0;
    for (int i = 1; i < W_COUNT; i++) if (g->meta.weapon_discovered[i]) discovered_w++;
    text_drawf(g->renderer, cx + 130, cy, 0xCCCCCCFF, "%d / %d", discovered_w, W_COUNT - 1);
    cy += 12;
    for (int i = 1; i < W_COUNT; i++) {
        bool d = g->meta.weapon_discovered[i];
        const char *n = d ? weapon_name((WeaponKind)i) : "??????";
        uint32_t c = d ? 0xFFFFFFFF : 0x404040FF;
        text_drawf(g->renderer, cx, cy, c, "- %s", n);
        cy += 10;
    }

    int cx2 = INTERNAL_W / 2 + 20, cy2 = 124;
    text_draw(g->renderer, cx2, cy2, "CODEX  -  ELEMENTS", 0xFFFF80FF);
    int discovered_e = 0;
    for (int i = 1; i < EL_COUNT; i++) if (g->meta.element_discovered[i]) discovered_e++;
    text_drawf(g->renderer, cx2 + 160, cy2, 0xCCCCCCFF, "%d / %d", discovered_e, EL_COUNT - 1);
    cy2 += 12;
    for (int e = 1; e < EL_COUNT; e++) {
        bool d = g->meta.element_discovered[e];
        const char *n = d ? element_name((Element)e) : "??????";
        uint32_t c = d ? element_color((Element)e) : 0x404040FF;
        text_drawf(g->renderer, cx2, cy2, c, "- %s", n);
        cy2 += 10;
    }

    /* 4 boutons bas : DEBUTER / OPTIONS / CODEX / AIDE */
    int by = INTERNAL_H - 30;
    int bw = 96, bh = 16;
    int gx = INTERNAL_W/2 - (bw * 4 + 18) / 2;
    const char *labels[4] = { "[R] DEBUTER", "[O] OPTIONS", "[K] CODEX", "[H] AIDE" };
    uint32_t col_active[4] = { 0x80FF80FF, 0xCCCCCCFF, 0xC0E0FFFF, 0xCCCCCCFF };
    for (int i = 0; i < 4; i++) {
        int x = gx + i * (bw + 6);
        bool hov = mouse_in_rect(g, x, by, bw, bh);
        fill_rect(g->renderer, x, by, bw, bh, hov ? 0x303060FF : 0x18181EFF);
        rect_outline(g->renderer, x, by, bw, bh, hov ? 0xFFFF80FF : 0x404048FF);
        const char *l = labels[i];
        text_draw(g->renderer, x + (bw - text_width(l)) / 2, by + 5, l,
                  hov ? 0xFFFF80FF : col_active[i]);
    }
    text_draw(g->renderer, INTERNAL_W/2 - text_width("ECHAP : QUITTER")/2,
              INTERNAL_H - 10, "ECHAP : QUITTER", 0x808080FF);
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
        const char *labels[2] = { "DLSS Generatif", "Debug : salle bac-a-sable" };
        const char *vals[2]   = {
            g->settings.dlss_on    ? "ON  (lisse)"  : "OFF (pixel art net)",
            g->settings.debug_room ? "ON"           : "OFF"
        };
        for (int i = 0; i < 2; i++) {
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
            int cost = 60 + i * 25;
            char b[24]; snprintf(b, sizeof(b), "%d ECLATS", cost);
            text_draw(g->renderer, sx - text_width(b) / 2, sy + 32, b, 0xFFC080FF);
        }
    }

    /* description */
    HeroClass cur = (HeroClass)g->hero_cursor;
    if (g->meta.hero_discovered[cur]) {
        text_draw(g->renderer, INTERNAL_W/2 - text_width(hero_desc(cur))/2,
                  INTERNAL_H - 56, hero_desc(cur), 0xCCCCFFFF);
        if (!g->meta.hero_unlocked[cur]) {
            text_draw(g->renderer,
                      INTERNAL_W/2 - text_width("VERROUILLE - CLIC POUR ACHETER")/2,
                      INTERNAL_H - 42, "VERROUILLE - CLIC POUR ACHETER", 0xFFC080FF);
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
    fill_rect(g->renderer, 0, 0, INTERNAL_W, INTERNAL_H, 0x300010C0);
    gfx_set_blend(g->renderer, false);
    text_draw(g->renderer, INTERNAL_W/2 - text_width("VAINCU")/2, 80,
              "VAINCU", 0xFF4060FF);
    text_drawf(g->renderer, INTERNAL_W/2 - 70, 100, 0xFFFFFFFF,
               "ETAGE %d/%d   KILLS %d   AMES %d",
               g->floor_index, MAX_FLOORS, g->run_kills, g->player.souls);
    int gain = g->player.souls + g->run_kills / 4 + g->floor_index * 5;
    text_drawf(g->renderer, INTERNAL_W/2 - 70, 112, 0xFFE080FF,
               "GAIN PERMANENT %d ECLATS", gain);
    text_draw(g->renderer, INTERNAL_W/2 - text_width("ENTREE POUR RETOUR AU SANCTUAIRE")/2,
              140, "ENTREE POUR RETOUR AU SANCTUAIRE", 0xFFFFFFFF);
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

    text_draw(g->renderer, INTERNAL_W/2 - text_width("DOOM x HADES x ISAAC x DIABLO")/2,
              INTERNAL_H/2 - 38, "DOOM x HADES x ISAAC x DIABLO", 0xC0A080FF);

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
    text_draw(g->renderer, 8, y, "SOURIS           VISER + naviguer/cliquer dans les menus", 0xFFFFFFFF); y += 9;
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

