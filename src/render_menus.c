/*
 * render_menus.c - ecrans menus : hub, options, choose_hero, dead,
 * victory, title, lore, help. draw_hero_portrait reste prive ici (seul
 * render_choose_hero le consomme).
 */
#include "ui_common.h"
#include "gfx.h"
#include "combat_internal.h"
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

/* sous-panneau TEMPLE : work in progress placeholder. */
static void render_hub_temple(Game *g) {
    int x, y, w, h;
    sub_panel_bg(g, &x, &y, &w, &h, "TEMPLE");
    text_draw(g->renderer, x + w / 2 - text_width("[ WORK IN PROGRESS ]") / 2,
              y + 50, "[ WORK IN PROGRESS ]", 0xFFA040FF);
    text_draw(g->renderer, x + w / 2 - text_width("Le sanctuaire des stats permanentes") / 2,
              y + 80, "Le sanctuaire des stats permanentes", 0xCCCCCCFF);
    text_draw(g->renderer, x + w / 2 - text_width("sera disponible dans une future mise a jour.") / 2,
              y + 92, "sera disponible dans une future mise a jour.", 0xCCCCCCFF);
    text_draw(g->renderer, x + w / 2 - text_width("(+ PV max / armure / vitesse / degats)") / 2,
              y + 120, "(+ PV max / armure / vitesse / degats)", 0x707080FF);
    text_draw(g->renderer, x + w / 2 - text_width("ENTREE OU ECHAP POUR FERMER") / 2,
              y + h - 14, "ENTREE OU ECHAP POUR FERMER", 0xFFFF80FF);
}

/* sous-panneau FORGE : choisis ton arme pour la run (5 options). */
static void render_hub_forge(Game *g) {
    int x, y, w, h;
    sub_panel_bg(g, &x, &y, &w, &h, "FORGE");
    text_draw(g->renderer, x + w / 2 - text_width("Choisis ton arme pour la course.") / 2,
              y + 22, "Choisis ton arme pour la course.", 0xCCCCCCFF);
    static const WeaponKind PICKS[5] = { W_SWORD, W_SHIELD, W_BOW, W_WAND, W_AXE };
    static const char *DESCR[5] = {
        "Slash transversal rapide, motion blur.",
        "Bash + reflet de projectiles, defensif.",
        "Tir a distance, drag-back + release.",
        "Projectile magique a tete chercheuse.",
        "Chop overhead AOE lourd, lent."
    };
    int rowh = 22;
    int N = 5;
    for (int k = 0; k < N; k++) {
        int sy = y + 50 + k * rowh;
        bool sel = (g->hub_sub_cursor == k);
        bool chosen = (g->player.weapons[0].kind == PICKS[k]);
        uint32_t bg = sel ? 0x281828FF : 0x18141EFF;
        uint32_t bd = sel ? 0xFFFF40FF : (chosen ? 0x60D040FF : 0x504048FF);
        fill_rect(g->renderer, x + 10, sy, w - 20, rowh - 2, bg);
        rect_outline(g->renderer, x + 10, sy, w - 20, rowh - 2, bd);
        text_draw(g->renderer, x + 16, sy + 3,
                  weapon_name(PICKS[k]),
                  sel ? 0xFFFF40FF : (chosen ? 0x60D040FF : 0xFFFFFFFF));
        text_draw(g->renderer, x + 16, sy + 12,
                  DESCR[k], 0x808890FF);
        if (chosen) {
            text_draw(g->renderer, x + w - 50, sy + 6, "EQUIPEE", 0x60D040FF);
        }
    }
    text_draw(g->renderer, x + w / 2 - text_width("ENTREE / CLIC POUR EQUIPER -- ECHAP POUR FERMER") / 2,
              y + h - 14, "ENTREE / CLIC POUR EQUIPER -- ECHAP POUR FERMER", 0xFFFF80FF);
}

/* sous-panneau LICHE : work in progress placeholder. */
static void render_hub_liche(Game *g) {
    int x, y, w, h;
    sub_panel_bg(g, &x, &y, &w, &h, "LICHE");
    text_draw(g->renderer, x + w / 2 - text_width("[ WORK IN PROGRESS ]") / 2,
              y + 50, "[ WORK IN PROGRESS ]", 0xFFA040FF);
    text_draw(g->renderer, x + w / 2 - text_width("Les modificateurs de course") / 2,
              y + 80, "Les modificateurs de course", 0xCCCCCCFF);
    text_draw(g->renderer, x + w / 2 - text_width("seront disponibles dans une future mise a jour.") / 2,
              y + 92, "seront disponibles dans une future mise a jour.", 0xCCCCCCFF);
    text_draw(g->renderer, x + w / 2 - text_width("(ennemis empoisonnes, portes a sens unique...)") / 2,
              y + 120, "(ennemis empoisonnes, portes a sens unique...)", 0x707080FF);
    text_draw(g->renderer, x + w / 2 - text_width("ENTREE OU ECHAP POUR FERMER") / 2,
              y + h - 14, "ENTREE OU ECHAP POUR FERMER", 0xFFFF80FF);
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

    /* prompt [E] interaction : le nom du batiment proche + action.
     * DONJON est verrouille tant que arme + classe ne sont pas choisies. */
    int near_idx = g->hub_cursor;
    int n = hub_building_count();
    if (g->hub_sub_open == 0 && near_idx >= 0 && near_idx < n) {
        const HubBuilding *b = hub_building_get(near_idx);
        if (b) {
            const char *act;
            int sid = hub_building_sub_id(b);
            bool locked = false;
            switch (sid) {
                case  0:
                    if (!g->hub_weapon_chosen && !g->hub_hero_chosen) {
                        act = "VERROUILLE -- choisis une arme (Forge) + une classe (Taverne)";
                        locked = true;
                    } else if (!g->hub_weapon_chosen) {
                        act = "VERROUILLE -- choisis une arme a la Forge";
                        locked = true;
                    } else if (!g->hub_hero_chosen) {
                        act = "VERROUILLE -- choisis une classe a la Taverne";
                        locked = true;
                    } else {
                        act = "ENTRER DANS LE DONJON";
                    }
                    break;
                case -1:
                    act = g->hub_hero_chosen ? "Changer de classe" : "RECRUTER UN HEROS";
                    break;
                case  1: act = "(WIP) Sanctuaire des stats"; locked = true; break;
                case  2:
                    act = g->hub_weapon_chosen ? "Changer d'arme" : "FORGER UNE ARME";
                    break;
                case  3: act = "(WIP) Modificateurs de course"; locked = true; break;
                default: act = "INTERAGIR";             break;
            }
            char buf[160];
            if (locked) {
                snprintf(buf, sizeof(buf), "%s -- %s",
                         hub_building_name(b), act);
            } else {
                snprintf(buf, sizeof(buf), "[E] %s -- %s",
                         hub_building_name(b), act);
            }
            int tw = text_width(buf);
            int px = INTERNAL_W/2 - tw/2;
            int py = INTERNAL_H - 30;
            uint32_t bg_col = locked ? 0x301010C0 : 0x000000C0;
            uint32_t bd_col = locked ? 0xFF6060FF : 0xFFE080FF;
            uint32_t tx_col = locked ? 0xFF8080FF : 0xFFFF80FF;
            fill_rect(gc, px - 4, py - 2, tw + 8, 11, bg_col);
            rect_outline(gc, px - 4, py - 2, tw + 8, 11, bd_col);
            text_draw(gc, px, py, buf, tx_col);
        }
    }

    /* banniere statique au-dessus de l'ecran : indique l'etat (sans arme /
     * sans classe / pret). */
    {
        const char *banner = NULL;
        uint32_t bcol = 0xFFFF80FF;
        if (!g->hub_weapon_chosen && !g->hub_hero_chosen) {
            banner = "PAYSAN -- Forge ton arme et choisis ta classe avant de descendre.";
            bcol = 0xFFC080FF;
        } else if (!g->hub_weapon_chosen) {
            banner = "Il te manque une arme. Visite la FORGE.";
            bcol = 0xFFC080FF;
        } else if (!g->hub_hero_chosen) {
            banner = "Il te manque une classe. Visite la TAVERNE.";
            bcol = 0xFFC080FF;
        } else {
            banner = "Tu es pret. La porte du DONJON t'attend.";
            bcol = 0x80FFA0FF;
        }
        int tw = text_width(banner);
        int px = INTERNAL_W / 2 - tw / 2;
        int py = 20;
        fill_rect(gc, px - 4, py - 2, tw + 8, 11, 0x000000C0);
        text_draw(gc, px, py, banner, bcol);
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
        const char *labels[2] = {
            "Debug : salle bac-a-sable",
            "Barres de vie flottantes"
        };
        const char *vals[2]   = {
            g->settings.debug_room     ? "ON"           : "OFF",
            g->settings.mob_healthbars ? "ON"           : "OFF"
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

/* 10 elements disponibles comme orbes interactifs sur le titre.
 * Reflete les talismans potentiels : a chaque run, 7 sont tires au
 * sort, les 3 autres restent en reserve. Ici on les montre tous
 * pour que le joueur visualise l'univers d'options. */
static const Element TITLE_ORBIT_ELEMS[10] = {
    EL_FIRE, EL_WATER, EL_EARTH, EL_LIGHTNING, EL_AIR,
    EL_VOID, EL_FAE, EL_STEEL, EL_DARK, EL_HOLY
};

/* Orbe : position + velocite + element + respawn_t. Drag-and-drop pour
 * fusionner deux orbes en explosion d'element pre-conceptualise (Vapeur,
 * Plasma, etc.) via combo_compute(). */
typedef struct {
    float x, y;            /* position ecran */
    float vx, vy;          /* derive lente */
    Element elem;
    float respawn_t;       /* >0 = invisible, decompte avant respawn */
} TitleOrb;

#define N_TITLE_ORBS 10
static TitleOrb g_orbs[N_TITLE_ORBS];
static int g_orbs_init = 0;
static int g_drag_idx  = -1;        /* index orbe attrape, -1 sinon */

/* effet de fusion (bref) : centre + element resultant + timer. */
static float g_fuse_t   = 0.f;
static float g_fuse_x   = 0.f;
static float g_fuse_y   = 0.f;
static int   g_fuse_mask = 0;
static uint32_t g_fuse_col = 0xFFFFFFFF;
static const char *g_fuse_label = NULL;

static void title_orb_reset(int i) {
    TitleOrb *o = &g_orbs[i];
    o->elem = TITLE_ORBIT_ELEMS[i];
    /* positions de depart : cercle large autour du titre, points
     * equidistants pour ne pas se chevaucher */
    float angle = (i / (float)N_TITLE_ORBS) * 6.2831f;
    float radius_x = 160.f, radius_y = 70.f;
    o->x = INTERNAL_W / 2.f + cosf(angle) * radius_x;
    o->y = INTERNAL_H / 2.f - 50.f + sinf(angle) * radius_y;
    /* derive lente, direction tangentielle */
    o->vx = -sinf(angle) * 10.f;
    o->vy =  cosf(angle) * 6.f;
    o->respawn_t = 0.f;
}

static void title_orbs_init_all(void) {
    for (int i = 0; i < N_TITLE_ORBS; i++) title_orb_reset(i);
    g_orbs_init = 1;
    g_drag_idx = -1;
    g_fuse_t = 0.f;
}

void render_title(Game *g) {
    GfxCtx *gc = g->renderer;
    int CX = INTERNAL_W / 2;

    /* === FOND : degrade vertical + halo radial subtil === */
    for (int y = 0; y < INTERNAL_H; y++) {
        int v = 6 + (INTERNAL_H - y) / 28;
        uint32_t col = (uint32_t)((v << 24) | ((v / 2) << 16) | (v << 8) | 0xFF);
        fill_rect(gc, 0, y, INTERNAL_W, 1, col);
    }
    /* halo central : 6 anneaux concentriques attenues */
    for (int k = 0; k < 6; k++) {
        int rr = 60 + k * 30;
        int alpha = 18 - k * 2;
        if (alpha < 0) continue;
        uint32_t col = (uint32_t)((0xFF << 24) | (0xA0 << 16) | (0x40 << 8) | (uint32_t)alpha);
        gfx_set_blend(gc, true);
        fill_rect(gc, CX - rr, INTERNAL_H/2 - 64 - 2, rr * 2, 1, col);
        fill_rect(gc, CX - rr, INTERNAL_H/2 - 64 + 2, rr * 2, 1, col);
        gfx_set_blend(gc, false);
    }

    /* === PARTICULES : cendres lentes + braises rapides === */
    for (int i = 0; i < 70; i++) {
        int x = (i * 73 + (int)(g->time * 8)) % INTERNAL_W;
        int y = ((i * 37) + (int)(g->time * (i % 5 + 1) * 4)) % INTERNAL_H;
        uint32_t col = (i & 3) ? 0x30202060 : 0x80604080;
        gfx_set_blend(gc, true);
        fill_rect(gc, x, y, 1, 1, col);
        gfx_set_blend(gc, false);
    }
    /* braises rapides plus rares, plus chaudes */
    for (int i = 0; i < 18; i++) {
        int x = (i * 127 + (int)(g->time * 26)) % INTERNAL_W;
        int y = INTERNAL_H - (((int)(g->time * (12 + i * 3)) + i * 53) % (INTERNAL_H - 20));
        uint32_t col = (i & 1) ? 0xFFB060FF : 0xFFE090FF;
        fill_rect(gc, x, y, 1, 1, col);
    }

    /* === ORBES ELEMENTAIRES INTERACTIFS ===
     * 7 orbes flottent dans le titre. Drag-and-drop pour les fusionner :
     * 2 orbes superposes -> combo_compute() declenche une explosion qui
     * affiche le nom du combo pre-conceptualise (Vapeur, Plasma, Lave..).
     */
    if (!g_orbs_init) title_orbs_init_all();

    float dt = g->dt > 0.f ? g->dt : 1.f / 60.f;
    if (dt > 0.05f) dt = 0.05f;
    float mx = (float)g->mouse_x;
    float my = (float)g->mouse_y;
    bool mouse_down = (g->mouse_btn != 0);
    bool mouse_pressed = mouse_down && !g->mouse_btn_prev;
    bool mouse_released = !mouse_down && g->mouse_btn_prev;
    float orb_r = 7.f;

    /* tick respawn + decay des orbes "absorbees" */
    for (int i = 0; i < N_TITLE_ORBS; i++) {
        TitleOrb *o = &g_orbs[i];
        if (o->respawn_t > 0.f) {
            o->respawn_t -= dt;
            if (o->respawn_t <= 0.f) title_orb_reset(i);
        }
    }

    /* selection au press : trouve l'orbe survolee */
    if (mouse_pressed && g_drag_idx < 0) {
        /* on n'attrape pas dans la zone du menu (laisse le clic faire son
         * activate). Zone menu = bandeau central. */
        bool in_menu = (mx > CX - 110 && mx < CX + 110 &&
                        my > INTERNAL_H/2 + 12 && my < INTERNAL_H/2 + 90);
        if (!in_menu) {
            float best_d2 = 1e9f;
            int best = -1;
            for (int i = 0; i < N_TITLE_ORBS; i++) {
                TitleOrb *o = &g_orbs[i];
                if (o->respawn_t > 0.f) continue;
                float dx = o->x - mx, dy = o->y - my;
                float d2 = dx * dx + dy * dy;
                if (d2 < orb_r * orb_r * 4.f && d2 < best_d2) {
                    best = i; best_d2 = d2;
                }
            }
            if (best >= 0) g_drag_idx = best;
        }
    }

    /* drag : suit la souris */
    if (g_drag_idx >= 0 && mouse_down) {
        TitleOrb *o = &g_orbs[g_drag_idx];
        o->x = mx;
        o->y = my;
        o->vx = 0.f; o->vy = 0.f;
    }

    /* relache : check fusion target */
    if (g_drag_idx >= 0 && mouse_released) {
        TitleOrb *src = &g_orbs[g_drag_idx];
        int target = -1;
        float best_d2 = 1e9f;
        for (int i = 0; i < N_TITLE_ORBS; i++) {
            if (i == g_drag_idx) continue;
            TitleOrb *o = &g_orbs[i];
            if (o->respawn_t > 0.f) continue;
            float dx = o->x - src->x, dy = o->y - src->y;
            float d2 = dx * dx + dy * dy;
            float rr = orb_r * 2.5f;
            if (d2 < rr * rr && d2 < best_d2) {
                target = i; best_d2 = d2;
            }
        }
        if (target >= 0) {
            /* fusion : combo_compute sur le mask des 2 elements */
            TitleOrb *dst = &g_orbs[target];
            int mask = (1 << (int)src->elem) | (1 << (int)dst->elem);
            ComboFx fx = combo_compute(mask);
            g_fuse_x = (src->x + dst->x) * 0.5f;
            g_fuse_y = (src->y + dst->y) * 0.5f;
            g_fuse_mask = mask;
            g_fuse_col = fx.color ? fx.color : element_color(src->elem);
            g_fuse_label = fx.tag;
            if (!g_fuse_label || !g_fuse_label[0]) g_fuse_label = combo_name(mask);
            g_fuse_t = 1.5f;
            sfx_play(g, SFX_FUSE);
            /* despawn les 2 orbes ; respawn dans 4s aux positions de depart */
            src->respawn_t = 4.f;
            dst->respawn_t = 4.f;
        }
        g_drag_idx = -1;
    }
    /* relache hors fusion : l'orbe garde sa derniere derive (vx=vy=0) */
    if (g_drag_idx >= 0 && !mouse_down) g_drag_idx = -1;

    /* derive idle + rebond sur les bords */
    for (int i = 0; i < N_TITLE_ORBS; i++) {
        TitleOrb *o = &g_orbs[i];
        if (o->respawn_t > 0.f) continue;
        if (g_drag_idx == i) continue;
        o->x += o->vx * dt;
        o->y += o->vy * dt;
        /* bornes */
        float margin = 12.f;
        if (o->x < margin)              { o->x = margin; o->vx = fabsf(o->vx); }
        if (o->x > INTERNAL_W - margin) { o->x = INTERNAL_W - margin; o->vx = -fabsf(o->vx); }
        if (o->y < margin)              { o->y = margin; o->vy = fabsf(o->vy); }
        if (o->y > INTERNAL_H - margin) { o->y = INTERNAL_H - margin; o->vy = -fabsf(o->vy); }
        /* leger sinus pour donner vie */
        o->vy += sinf(g->time * 0.7f + i) * 0.4f * dt;
    }

    /* === dessin des orbes === */
    for (int i = 0; i < N_TITLE_ORBS; i++) {
        TitleOrb *o = &g_orbs[i];
        if (o->respawn_t > 0.f) continue;
        uint32_t col = element_color(o->elem);
        int px = (int)o->x;
        int py = (int)o->y;
        bool hover = (g_drag_idx == i) || ((mx - o->x)*(mx - o->x) +
                                           (my - o->y)*(my - o->y) < orb_r * orb_r * 4.f);
        int sz = hover ? 7 : 5;
        gfx_set_blend(gc, true);
        /* halo */
        uint32_t halo = (col & 0xFFFFFF00u) | 0x40;
        fill_rect(gc, px - sz - 2, py - sz/2, (sz + 2) * 2, sz, halo);
        fill_rect(gc, px - sz/2, py - sz - 2, sz, (sz + 2) * 2, halo);
        /* corps */
        fill_rect(gc, px - sz/2, py - sz/2, sz, sz, col);
        /* highlight */
        fill_rect(gc, px - 1, py - 1, 1, 1, 0xFFFFFFFF);
        gfx_set_blend(gc, false);
    }

    /* fusion : explosion de particules + texte du combo */
    if (g_fuse_t > 0.f) {
        g_fuse_t -= dt;
        float k = g_fuse_t / 1.5f; if (k < 0.f) k = 0.f;
        /* anneau qui s'expand */
        int n_ring = 28;
        float ring_r = (1.f - k) * 60.f;
        gfx_set_blend(gc, true);
        for (int s = 0; s < n_ring; s++) {
            float a = (s / (float)n_ring) * 6.2831f;
            int px = (int)(g_fuse_x + cosf(a) * ring_r);
            int py = (int)(g_fuse_y + sinf(a) * ring_r);
            uint8_t alpha = (uint8_t)(255.f * k);
            uint32_t col = (g_fuse_col & 0xFFFFFF00u) | alpha;
            fill_rect(gc, px - 1, py - 1, 3, 3, col);
        }
        /* texte du combo (Vapeur, Plasma, etc.) */
        if (g_fuse_label && g_fuse_label[0]) {
            int tw = text_width(g_fuse_label);
            uint8_t a = (uint8_t)(255.f * (k > 0.7f ? 1.f : k / 0.7f));
            uint32_t lcol = (g_fuse_col & 0xFFFFFF00u) | a;
            int tx2 = (int)g_fuse_x - tw / 2;
            int ty2 = (int)g_fuse_y - 4 - (int)((1.f - k) * 12.f);
            text_draw(gc, tx2 + 1, ty2 + 1, g_fuse_label, 0x000000C0);
            text_draw(gc, tx2,     ty2,     g_fuse_label, lcol);
        }
        gfx_set_blend(gc, false);
    }

    /* === TITRE : ombre + glow + scale-pulse subtil === */
    const char *t = "ELEMENT DUNGEON";
    int tw = text_width(t);
    int tx = CX - tw / 2;
    int ty = INTERNAL_H / 2 - 60;
    /* glow lent : 9 offsets en croix */
    for (int dx = -2; dx <= 2; dx++) {
        for (int dy = -2; dy <= 2; dy++) {
            if (!dx && !dy) continue;
            int dist = (dx * dx + dy * dy);
            uint8_t a = (uint8_t)(50 - dist * 6);
            if (a == 0) continue;
            uint32_t col = (uint32_t)((0xFF << 24) | (0x80 << 16) | (0x30 << 8) | a);
            gfx_set_blend(gc, true);
            text_draw(gc, tx + dx, ty + dy, t, col);
            gfx_set_blend(gc, false);
        }
    }
    text_draw(gc, tx + 1, ty + 2, t, 0x000000FF);
    text_draw(gc, tx,     ty,     t, 0xFFD060FF);

    const char *tagline = "ROGUELITE ELEMENTAIRE";
    int gw = text_width(tagline);
    text_draw(gc, CX - gw / 2, INTERNAL_H / 2 - 38, tagline, 0xC0A080FF);

    /* === MENU avec curseur clavier === */
    const char *items[5] = { "JOUER", "CODEX", "OPTIONS", "AIDE", "QUITTER" };
    int n_items = 5;
    int yA = INTERNAL_H / 2 + 14;
    int rowh = 14;
    /* clamp cursor (safety) */
    if (g->title_cursor < 0)         g->title_cursor = 0;
    if (g->title_cursor >= n_items)  g->title_cursor = n_items - 1;
    /* survol souris : met a jour le cursor */
    for (int i = 0; i < n_items; i++) {
        int yi = yA + i * rowh;
        if (mouse_in_rect(g, CX - 100, yi, 200, 12)) {
            g->title_cursor = i;
        }
    }
    for (int i = 0; i < n_items; i++) {
        int yi = yA + i * rowh;
        bool sel = (g->title_cursor == i);
        uint32_t col;
        if (sel) col = 0xFFFF80FF;
        else if (i == 0) col = 0x80FFA0FF;     /* JOUER reste vert */
        else col = 0xAAAAAAFF;
        int w = text_width(items[i]);
        if (sel) {
            /* fond translucide sur la ligne focused */
            gfx_set_blend(gc, true);
            fill_rect(gc, CX - 110, yi - 1, 220, 13, 0x402020A0);
            gfx_set_blend(gc, false);
        }
        text_draw(gc, CX - w / 2, yi + 2, items[i], col);
        if (sel) {
            /* fleches qui battent doucement */
            int pulse = (int)((sinf(g->time * 6.f) * 0.5f + 0.5f) * 4.f);
            text_draw(gc, CX - w / 2 - 14 - pulse, yi + 2, ">", col);
            text_draw(gc, CX + w / 2 + 8  + pulse, yi + 2, "<", col);
        }
    }

    /* === PANNEAU META en bas a gauche === */
    int px = 8;
    int py = INTERNAL_H - 58;
    fill_rect(gc, px - 2, py - 2, 130, 50, 0x00000080);
    rect_outline(gc, px - 2, py - 2, 130, 50, 0x60504AFF);
    text_drawf(gc, px, py,      0xFFD040FF, "* %d eclats", g->meta.shards);
    text_drawf(gc, px, py + 10, 0xCCCCCCFF, "Courses     %d", g->meta.total_runs);
    text_drawf(gc, px, py + 20, 0xCCCCCCFF, "Meilleur    %d/%d",
               g->meta.best_floor, MAX_FLOORS);
    /* elements decouverts parmi les 10 jouables (FIRE..HOLY) */
    int discov = 0;
    for (int i = (int)EL_FIRE; i <= (int)EL_HOLY; i++)
        if (g->meta.element_discovered[i]) discov++;
    text_drawf(gc, px, py + 30, 0x80C0FFFF, "Elements    %d/10", discov);

    /* === TIP rotation en bas centre === */
    static const char *TIPS[] = {
        "Astuce : maintiens CLIC pour enchainer les attaques.",
        "Astuce : ESPACE = dash invincible.",
        "Astuce : 3 elements greffes = boucle de combo + aura.",
        "Astuce : I ouvre l'inventaire ; F fusionne 3 items identiques.",
        "Astuce : chaque element a une faiblesse. Frappe la bonne couleur.",
        "Astuce : ESQUIVER reduit les degats encaisses.",
        "Astuce : 1 / 2 / TAB switche d'arme. Le swap est strategique.",
    };
    int n_tips = (int)(sizeof(TIPS) / sizeof(TIPS[0]));
    int tip_idx = ((int)(g->time / 4.f)) % n_tips;
    if (tip_idx < 0) tip_idx = 0;
    const char *tip = TIPS[tip_idx];
    int twi = text_width(tip);
    text_draw(gc, CX - twi / 2, INTERNAL_H - 22, tip, 0x90A0B0FF);

    /* === version en bas a droite === */
    const char *ver = "v1.0 - C99 + SDL2 + GL3.3";
    text_draw(gc, INTERNAL_W - text_width(ver) - 8, INTERNAL_H - 12, ver, 0x606080FF);
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
/* ---------- PAUSE ---------- */
void render_pause(Game *g) {
    GfxCtx *gc = g->renderer;
    int CX = INTERNAL_W / 2;
    /* voile assombri */
    gfx_set_blend(gc, true);
    fill_rect(gc, 0, 0, INTERNAL_W, INTERNAL_H, 0x000000C0);
    gfx_set_blend(gc, false);
    /* titre */
    const char *t = "PAUSE";
    int tw = text_width(t);
    text_draw(gc, CX - tw/2 + 1, INTERNAL_H/2 - 40 + 1, t, 0x000000FF);
    text_draw(gc, CX - tw/2,     INTERNAL_H/2 - 40,     t, 0xFFE080FF);
    text_draw(gc, CX - text_width("ECHAP pour reprendre rapidement")/2,
              INTERNAL_H/2 - 24, "ECHAP pour reprendre rapidement", 0x808080FF);
    /* menu : Reprendre / Abandonner */
    const char *items[2] = { "REPRENDRE", "ABANDONNER LA COURSE" };
    int cy0 = INTERNAL_H/2 + 10;
    int rowh = 18;
    for (int i = 0; i < 2; i++) {
        bool sel = (g->pause_cursor == i);
        uint32_t col = sel ? 0xFFFF80FF : (i == 0 ? 0x80FFA0FF : 0xFF8080FF);
        int w = text_width(items[i]);
        int yi = cy0 + i * rowh;
        if (sel) {
            gfx_set_blend(gc, true);
            fill_rect(gc, CX - 110, yi - 2, 220, 14, 0x402020A0);
            gfx_set_blend(gc, false);
            text_draw(gc, CX - w/2 - 14, yi + 2, ">", col);
            text_draw(gc, CX + w/2 + 8,  yi + 2, "<", col);
        }
        text_draw(gc, CX - w/2, yi + 2, items[i], col);
    }
    text_draw(gc, CX - text_width("ENTREE pour valider")/2,
              cy0 + 2 * rowh + 8, "ENTREE pour valider", 0x808080FF);
}

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

