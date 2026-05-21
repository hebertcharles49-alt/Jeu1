/*
 * render_hud.c - HUD en jeu (PV, XP, armes, minimap).
 */
#include "ui_common.h"
#include "gfx.h"
#include <math.h>
#include <stdio.h>

/* ---------- HUD ---------- */
void render_hud(Game *g) {
    Player *p = &g->player;
    /* === Top-left : HP / XP / Endurance dash (bars empilees) === */
    fill_rect(g->renderer, 4, 4, 110, 11, 0x000000FF);
    fill_rect(g->renderer, 5, 5, 108, 9, 0x202020FF);
    int hf = (int)(108 * (p->hp / p->maxhp));
    if (hf < 0) hf = 0;
    fill_rect(g->renderer, 5, 5, hf, 9, 0xC03030FF);
    fill_rect(g->renderer, 5, 5, hf, 3, 0xE05050FF);
    text_drawf(g->renderer, 7, 6, 0xFFFFFFFF, "PV %d/%d", (int)p->hp, (int)p->maxhp);

    /* XP bar sous la HP bar : y 17..22 */
    fill_rect(g->renderer, 4, 17, 110, 5, 0x102040FF);
    int xf = p->xp_to_next > 0 ? (110 * p->xp / p->xp_to_next) : 0;
    fill_rect(g->renderer, 4, 17, xf, 5, 0x40A0FFFF);

    /* Endurance dash : barre verte sous la XP bar. Remplit en 4s.
     * Quand pleine (dash_cd == 0), barre saturee + petite icone "DASH".
     * Pendant le CD, fond gris + remplissage vert proportionnel a
     * (1 - dash_cd / 4.0). */
    {
        const float DASH_CD_MAX = 4.0f;
        float ratio = 1.f - (p->dash_cd / DASH_CD_MAX);
        if (ratio < 0.f) ratio = 0.f;
        if (ratio > 1.f) ratio = 1.f;
        fill_rect(g->renderer, 4, 24, 110, 5, 0x102018FF);
        uint32_t bar_col = (p->dash_cd <= 0.001f) ? 0x60FF60FF : 0x40C040FF;
        fill_rect(g->renderer, 4, 24, (int)(110 * ratio), 5, bar_col);
        /* texte ETD/READY a droite de la barre */
        if (p->dash_cd > 0.001f) {
            text_drawf(g->renderer, 116, 24, 0x80C080FF, "%.1fs", p->dash_cd);
        } else {
            text_draw(g->renderer, 116, 24, "DASH", 0x80FF80FF);
        }
    }

    /* Ligne stats principales : y=33 */
    text_drawf(g->renderer, 4,   33, 0xCCCCFFFF, "LV %d", p->level);
    text_drawf(g->renderer, 40,  33, 0xFFD040FF, "%d", p->coins);
    fill_rect (g->renderer, 62, 34, 5, 5, 0xFFD040FF);
    text_drawf(g->renderer, 76,  33, 0xC0FFC0FF, "AME %d", p->souls);

    /* Ligne contexte run : y=43 */
    text_drawf(g->renderer, 4, 43, 0xFFE0A0FF, "ETAGE %d/%d  KILLS %d  T %.0f",
               g->floor_index, MAX_FLOORS, g->run_kills, g->run_time);

    /* Subclass : y=53 */
    const char *sc = subclass_name(p->weapons[0].kind, p->weapons[1].kind);
    text_drawf(g->renderer, 4, 53, 0xFF80FFFF, "[%s] %s", hero_name(p->hero), sc);

    /* Badge biome a droite, sous la minimap (rendu plus bas). On le
     * dessine ici en small au-dessus du minimap. */
    {
        int bi = biome_for_floor(g->floor_index);
        Element be = biome_element(bi);
        char bbuf[64];
        snprintf(bbuf, sizeof(bbuf), "%s (%s)", biome_name(bi), element_name(be));
        int bw = text_width(bbuf);
        text_draw(g->renderer, INTERNAL_W - bw - 6, 6, bbuf, element_color(be));
    }

    /* weapon slots */
    int sw = 130, sh = 28, gap = 4;
    int total_w = WEAPON_SLOTS * sw + (WEAPON_SLOTS - 1) * gap;
    int sx = (INTERNAL_W - total_w) / 2;
    int sy = INTERNAL_H - sh - 4;
    char buf[64];
    for (int i = 0; i < WEAPON_SLOTS; i++) {
        Weapon *w = &p->weapons[i];
        bool active = (i == p->active_weapon);
        fill_rect(g->renderer, sx, sy, sw, sh, active ? 0x303060FF : 0x18181EFF);
        rect_outline(g->renderer, sx, sy, sw, sh, active ? 0xFFFF80FF : 0x404048FF);
        weapon_describe(w, buf, sizeof(buf));
        text_drawf(g->renderer, sx + 4, sy + 3, active ? 0xFFFF80FF : 0xCCCCCCFF,
                   "[%d] %s", i + 1, weapon_name(w->kind));
        text_draw(g->renderer, sx + 4, sy + 12, buf, 0xFFC0FFFF);
        float frac = (w->base_cd > 0.f) ? (1.f - w->cooldown / w->base_cd) : 1.f;
        if (frac < 0) frac = 0;
        if (frac > 1) frac = 1;
        fill_rect(g->renderer, sx + 4, sy + sh - 5, sw - 8, 3, 0x202028FF);
        fill_rect(g->renderer, sx + 4, sy + sh - 5, (int)((sw - 8) * frac), 3,
                  active ? 0xFFFF80FF : 0x80C0FFFF);
        sx += sw + gap;
    }

    /* tip */
    text_draw(g->renderer, INTERNAL_W - 122, INTERNAL_H - 12, "TAB ARMES   I INVENTAIRE", 0x808080FF);
    text_draw(g->renderer, INTERNAL_W - 122, INTERNAL_H - 4,  "ESPACE DASH", 0x808080FF);

    /* ---- MINIMAP (top-right) plus grande ----
     * 110x110 (au lieu de 60x60). Affiche les tiles a 2px chacune
     * (donc on couvre jusqu'a 55x55 tiles), suffisant pour MAP_W=56. */
    {
        Dungeon *d = &g->dungeon;
        int mm_w = 110, mm_h = 110;
        int mm_x = INTERNAL_W - mm_w - 4;
        int mm_y = 16;       /* sous le badge biome */
        int ox = mm_x + 4, oy = mm_y + 4;
        int sc = 2;          /* echelle : 2 px par tile */
        /* fond */
        gfx_set_blend(g->renderer, true);
        fill_rect(g->renderer, mm_x, mm_y, mm_w, mm_h, 0x000000B0);
        gfx_set_blend(g->renderer, false);
        rect_outline(g->renderer, mm_x, mm_y, mm_w, mm_h, 0x404048FF);
        /* salles visitees (sc px par tile) */
        int ptx = (int)(g->player.x / TILE);
        int pty = (int)(g->player.y / TILE);
        for (int i = 0; i < d->room_count; i++) {
            Room *r = &d->rooms[i];
            if (!r->visited) continue;
            int rx = ox + r->x * sc;
            int ry = oy + r->y * sc;
            uint32_t col = 0x606068FF;
            if (!r->cleared)         col = 0xA0A040FF;
            if (r->is_boss_room)     col = 0xC04040FF;
            if (r->is_debug_room)    col = 0x40A0C0FF;
            fill_rect(g->renderer, rx, ry, r->w * sc, r->h * sc, col);
            rect_outline(g->renderer, rx, ry, r->w * sc, r->h * sc, 0x18181EFF);
        }
        /* portes T_DOOR : pixel chaud (sc x sc) */
        for (int i = 0; i < d->room_count; i++) {
            Room *r = &d->rooms[i];
            if (!r->visited) continue;
            for (int x = r->x - 1; x <= r->x + r->w; x++) {
                for (int y = r->y - 1; y <= r->y + r->h; y++) {
                    if (x < 0 || y < 0 || x >= MAP_W || y >= MAP_H) continue;
                    if (d->tiles[y][x] == T_DOOR) {
                        fill_rect(g->renderer, ox + x * sc, oy + y * sc,
                                  sc, sc, 0xFFC080FF);
                    }
                }
            }
        }
        /* icones de salles speciales : on dessine PAR-DESSUS le fond pour
         * que ce soit visible meme sur les petites salles.
         *   BOSS  : croix rouge clignotante (skull-ish)
         *   DEBUG : etoile cyan (4 branches)
         *   SPAWN : "S" mini blanc (room 0)
         *   PICKUPS dans salle visitee : minuscule point dore (loot non
         *     ramasse, hint pour faire un nettoyage rapide). */
        for (int i = 0; i < d->room_count; i++) {
            Room *r = &d->rooms[i];
            if (!r->visited) continue;
            int cx = ox + (r->x + r->w / 2) * sc;
            int cy = oy + (r->y + r->h / 2) * sc;
            if (r->is_boss_room) {
                uint32_t bc = ((int)(g->time * 3.f) & 1) ? 0xFF6060FF : 0xC02020FF;
                fill_rect(g->renderer, cx - 3, cy,     7, 2, bc);
                fill_rect(g->renderer, cx,     cy - 3, 2, 7, bc);
                if (d->boss_dead) {
                    fill_rect(g->renderer, cx - 3, cy,     7, 2, 0x603030FF);
                    fill_rect(g->renderer, cx,     cy - 3, 2, 7, 0x603030FF);
                }
            } else if (r->is_debug_room) {
                fill_rect(g->renderer, cx,     cy - 3, 2, 7, 0x80FFFFFF);
                fill_rect(g->renderer, cx - 3, cy,     7, 2, 0x80FFFFFF);
            } else if (i == 0) {
                text_draw(g->renderer, cx - 2, cy - 3, "S", 0xFFFFFFFF);
            }
        }
        /* pickups (dot dore, 2x2 sur la map x2) */
        for (int i = 0; i < MAX_PICKUPS; i++) {
            Pickup *pk = &g->pickups[i];
            if (!pk->alive) continue;
            if (pk->kind != PU_ITEM && pk->kind != PU_CHEST &&
                pk->kind != PU_WEAPON) continue;
            int tx = (int)(pk->x / TILE), ty = (int)(pk->y / TILE);
            bool in_visited = false;
            for (int r = 0; r < d->room_count && !in_visited; r++) {
                Room *rr = &d->rooms[r];
                if (!rr->visited) continue;
                if (tx >= rr->x && tx < rr->x + rr->w &&
                    ty >= rr->y && ty < rr->y + rr->h) in_visited = true;
            }
            if (!in_visited) continue;
            uint32_t pc = 0xFFD040FF;
            if (pk->kind == PU_ITEM && pk->item.is_unique) pc = 0xFF8030FF;
            fill_rect(g->renderer, ox + tx * sc, oy + ty * sc, sc, sc, pc);
        }
        /* portail de sortie (clignote) si visible */
        if (d->boss_dead) {
            int px = ox + d->exit_x * sc, py = oy + d->exit_y * sc;
            uint32_t pc = ((int)(g->time * 4.f) & 1) ? 0x80E0FFFF : 0x4070C0FF;
            fill_rect(g->renderer, px - 2, py - 2, 5, 5, pc);
        }
        /* joueur : point jaune clignotant 3x3 */
        int px = ox + ptx * sc, py = oy + pty * sc;
        uint32_t playerc = ((int)(g->time * 5.f) & 1) ? 0xFFFF80FF : 0xFFFFFFFF;
        fill_rect(g->renderer, px - 2, py - 2, 4, 4, playerc);

        /* === PANNEAU STATS sous la minimap ===
         * Liste compacte des stats du joueur (PV, ATK, ARM, VIT, CRIT,
         * VOL, REG, ESQ). Plus de DPS / TTK / SEED ici. */
        {
            int sx2 = mm_x, sy2 = mm_y + mm_h + 6;
            int sw2 = mm_w, sh2 = 102;
            gfx_set_blend(g->renderer, true);
            fill_rect(g->renderer, sx2, sy2, sw2, sh2, 0x000000A0);
            gfx_set_blend(g->renderer, false);
            rect_outline(g->renderer, sx2, sy2, sw2, sh2, 0x30303AFF);
            text_draw(g->renderer, sx2 + 3, sy2 + 2, "STATS", 0xFFE080FF);
            int sy3 = sy2 + 12;
            text_drawf(g->renderer, sx2 + 3, sy3, 0xFFFFFFFF,
                       "PV  %d/%d", (int)p->hp, (int)p->maxhp); sy3 += 9;
            text_drawf(g->renderer, sx2 + 3, sy3, 0xFFFFFFFF,
                       "ATK x%.2f", p->dmg_mul); sy3 += 9;
            text_drawf(g->renderer, sx2 + 3, sy3, 0xFFFFFFFF,
                       "ARM %.0f", p->armor); sy3 += 9;
            text_drawf(g->renderer, sx2 + 3, sy3, 0xFFFFFFFF,
                       "VIT %.0f", p->speed); sy3 += 9;
            text_drawf(g->renderer, sx2 + 3, sy3, 0xFFFFFFFF,
                       "CRIT %.0f%%", p->crit_chance * 100.f); sy3 += 9;
            text_drawf(g->renderer, sx2 + 3, sy3, 0xFFFFFFFF,
                       "VOL %.0f%%", p->lifesteal * 100.f); sy3 += 9;
            text_drawf(g->renderer, sx2 + 3, sy3, 0xFFFFFFFF,
                       "REG %.1f/s", p->regen_per_sec); sy3 += 9;
            text_drawf(g->renderer, sx2 + 3, sy3, 0xFFFFFFFF,
                       "ESQ %.0f%%", p->dodge * 100.f);
        }
    }
}

