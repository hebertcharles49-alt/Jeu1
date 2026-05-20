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
    /* HP bar */
    fill_rect(g->renderer, 4, 4, 110, 9, 0x000000FF);
    fill_rect(g->renderer, 5, 5, 108, 7, 0x202020FF);
    int hf = (int)(108 * (p->hp / p->maxhp));
    if (hf < 0) hf = 0;
    fill_rect(g->renderer, 5, 5, hf, 7, 0xC03030FF);
    fill_rect(g->renderer, 5, 5, hf, 2, 0xE05050FF);
    text_drawf(g->renderer, 7, 5, 0xFFFFFFFF, "PV %d/%d", (int)p->hp, (int)p->maxhp);

    /* XP bar */
    fill_rect(g->renderer, 4, 15, 110, 4, 0x102040FF);
    int xf = p->xp_to_next > 0 ? (110 * p->xp / p->xp_to_next) : 0;
    fill_rect(g->renderer, 4, 15, xf, 4, 0x40A0FFFF);
    text_drawf(g->renderer, 120, 5,  0xCCCCFFFF, "LV %d", p->level);
    text_drawf(g->renderer, 120, 14, 0xFFD040FF, "%d", p->coins);
    fill_rect(g->renderer, 142, 14, 5, 5, 0xFFD040FF);
    text_drawf(g->renderer, 158, 14, 0xC0FFC0FF, "AME %d", p->souls);

    text_drawf(g->renderer, 4, 22, 0xFFE0A0FF, "ETAGE %d/%d  KILLS %d  T %.0f  ARMURE %.0f",
               g->floor_index, MAX_FLOORS, g->run_kills, g->run_time, p->armor);
    /* badge biome : nom + couleur de l element du biome. Aligne a droite
       pour ne pas chevaucher la ligne ETAGE/KILLS sur les longues valeurs. */
    {
        int bi = biome_for_floor(g->floor_index);
        Element be = biome_element(bi);
        char bbuf[64];
        snprintf(bbuf, sizeof(bbuf), "* %s (%s)", biome_name(bi), element_name(be));
        int bw = text_width(bbuf);
        text_draw(g->renderer, INTERNAL_W - bw - 6, 5, bbuf, element_color(be));
    }

    /* subclass */
    const char *sc = subclass_name(p->weapons[0].kind, p->weapons[1].kind);
    text_drawf(g->renderer, 4, 30, 0xFF80FFFF, "[%s] %s", hero_name(p->hero), sc);

    /* ---- PANNEAU STATS (gauche) ---- */
    {
        int x = 4, y = 40;
        gfx_set_blend(g->renderer, true);
        fill_rect(g->renderer, x - 1, y - 1, 90, 122, 0x000000A0);
        gfx_set_blend(g->renderer, false);
        rect_outline(g->renderer, x - 1, y - 1, 90, 122, 0x30303AFF);
        text_draw(g->renderer, x + 2, y, "STATS", 0xFFE080FF);
        y += 10;
        text_drawf(g->renderer, x + 2, y, 0xFFFFFFFF,
                   "PV  %d/%d", (int)p->hp, (int)p->maxhp); y += 9;
        text_drawf(g->renderer, x + 2, y, 0xFFFFFFFF,
                   "ATK x%.2f", p->dmg_mul); y += 9;
        text_drawf(g->renderer, x + 2, y, 0xFFFFFFFF,
                   "ARM %.0f", p->armor); y += 9;
        text_drawf(g->renderer, x + 2, y, 0xFFFFFFFF,
                   "VIT %.0f", p->speed); y += 9;
        text_drawf(g->renderer, x + 2, y, 0xFFFFFFFF,
                   "CRIT %.0f%%", p->crit_chance * 100.f); y += 9;
        text_drawf(g->renderer, x + 2, y, 0xFFFFFFFF,
                   "VOL %.0f%%", p->lifesteal * 100.f); y += 9;
        text_drawf(g->renderer, x + 2, y, 0xFFFFFFFF,
                   "REG %.1f/s", p->regen_per_sec); y += 9;
        text_drawf(g->renderer, x + 2, y, 0xFFFFFFFF,
                   "ESQ %.0f%%", p->dodge * 100.f); y += 9;
        /* separateur */
        fill_rect(g->renderer, x + 2, y, 84, 1, 0x40404AFF);
        y += 3;
        /* DPS smoothed (debug TTK). 0 si pas de combat actif. */
        text_drawf(g->renderer, x + 2, y, 0xC0E0FFFF,
                   "DPS %.0f", g->dps_smooth); y += 9;
        /* TTK estime sur le boss vivant si on tape. */
        if (g->dps_smooth > 1.f) {
            for (int i = 0; i < MAX_ENEMIES; i++) {
                Enemy *be = &g->enemies[i];
                if (!be->alive || !be->is_boss || be->dying_t > 0.f) continue;
                float ttk = be->hp / g->dps_smooth;
                text_drawf(g->renderer, x + 2, y, 0xFFD080FF,
                           "TTK %.1fs", ttk);
                y += 9;
                break;
            }
        }
        /* seed en bas, couleur discrete. Permet de partager une run. */
        text_drawf(g->renderer, x + 2, y, 0x808080FF,
                   "SEED %u", g->run_seed % 100000);
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

    /* ---- MINIMAP (top-right) ---- */
    {
        Dungeon *d = &g->dungeon;
        int mm_w = 60, mm_h = 60;
        int mm_x = INTERNAL_W - mm_w - 4;
        int mm_y = 4;
        int ox = mm_x + 2, oy = mm_y + 2;
        /* fond */
        gfx_set_blend(g->renderer, true);
        fill_rect(g->renderer, mm_x, mm_y, mm_w, mm_h, 0x000000B0);
        gfx_set_blend(g->renderer, false);
        rect_outline(g->renderer, mm_x, mm_y, mm_w, mm_h, 0x404048FF);
        /* salles visitees (1 px par tile, MAP_W=56 -> fit dans 56x56) */
        int ptx = (int)(g->player.x / TILE);
        int pty = (int)(g->player.y / TILE);
        for (int i = 0; i < d->room_count; i++) {
            Room *r = &d->rooms[i];
            if (!r->visited) continue;
            int rx = ox + r->x;
            int ry = oy + r->y;
            uint32_t col = 0x606068FF;          /* visitee normale */
            if (!r->cleared)         col = 0xA0A040FF;
            if (r->is_boss_room)     col = 0xC04040FF;
            if (r->is_debug_room)    col = 0x40A0C0FF;
            fill_rect(g->renderer, rx, ry, r->w, r->h, col);
            /* contour subtil */
            rect_outline(g->renderer, rx, ry, r->w, r->h, 0x18181EFF);
        }
        /* portes visibles : on traverse une bande etroite autour de chaque
         * salle visitee et on pose un pixel chaud sur les T_DOOR. */
        for (int i = 0; i < d->room_count; i++) {
            Room *r = &d->rooms[i];
            if (!r->visited) continue;
            for (int x = r->x - 1; x <= r->x + r->w; x++) {
                for (int y = r->y - 1; y <= r->y + r->h; y++) {
                    if (x < 0 || y < 0 || x >= MAP_W || y >= MAP_H) continue;
                    if (d->tiles[y][x] == T_DOOR) {
                        fill_rect(g->renderer, ox + x, oy + y, 1, 1, 0xFFC080FF);
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
            int cx = ox + r->x + r->w / 2;
            int cy = oy + r->y + r->h / 2;
            if (r->is_boss_room) {
                /* boss : croix rouge clignote (skull-ish) */
                uint32_t bc = ((int)(g->time * 3.f) & 1) ? 0xFF6060FF : 0xC02020FF;
                fill_rect(g->renderer, cx - 2, cy,     5, 1, bc);
                fill_rect(g->renderer, cx,     cy - 2, 1, 5, bc);
                if (d->boss_dead) {
                    /* boss vaincu : croix plus sombre, ne clignote plus */
                    fill_rect(g->renderer, cx - 2, cy,     5, 1, 0x603030FF);
                    fill_rect(g->renderer, cx,     cy - 2, 1, 5, 0x603030FF);
                }
            } else if (r->is_debug_room) {
                /* etoile 4-branches cyan */
                fill_rect(g->renderer, cx,     cy - 2, 1, 5, 0x80FFFFFF);
                fill_rect(g->renderer, cx - 2, cy,     5, 1, 0x80FFFFFF);
            } else if (i == 0) {
                /* spawn : S mini blanc */
                text_draw(g->renderer, cx - 2, cy - 3, "S", 0xFFFFFFFF);
            }
        }
        /* pickups dans une salle visitee : dot dore. Coffres + items
         * uniquement, pour eviter de spammer xp/coin sur la mini. */
        for (int i = 0; i < MAX_PICKUPS; i++) {
            Pickup *pk = &g->pickups[i];
            if (!pk->alive) continue;
            if (pk->kind != PU_ITEM && pk->kind != PU_CHEST &&
                pk->kind != PU_WEAPON) continue;
            int tx = (int)(pk->x / TILE), ty = (int)(pk->y / TILE);
            /* on n affiche que si la tile est dans une salle visitee */
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
            fill_rect(g->renderer, ox + tx, oy + ty, 1, 1, pc);
        }
        /* portail de sortie (clignote) si visible */
        if (d->boss_dead) {
            int px = ox + d->exit_x, py = oy + d->exit_y;
            uint32_t pc = ((int)(g->time * 4.f) & 1) ? 0x80E0FFFF : 0x4070C0FF;
            fill_rect(g->renderer, px - 1, py - 1, 3, 3, pc);
        }
        /* joueur : point jaune clignotant (par-dessus tout) */
        int px = ox + ptx, py = oy + pty;
        uint32_t playerc = ((int)(g->time * 5.f) & 1) ? 0xFFFF80FF : 0xFFFFFFFF;
        fill_rect(g->renderer, px - 1, py - 1, 3, 3, playerc);

        /* legende minuscule sous la minimap */
        text_draw(g->renderer, mm_x, mm_y + mm_h + 1, "MAP", 0x808080FF);
    }
}

