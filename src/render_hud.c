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
    /* badge biome : nom + couleur de l element du biome. */
    {
        int bi = biome_for_floor(g->floor_index);
        Element be = biome_element(bi);
        text_drawf(g->renderer, 230, 22, element_color(be),
                   "* %s (%s)", biome_name(bi), element_name(be));
    }

    /* subclass */
    const char *sc = subclass_name(p->weapons[0].kind, p->weapons[1].kind);
    text_drawf(g->renderer, 4, 30, 0xFF80FFFF, "[%s] %s", hero_name(p->hero), sc);

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
        /* portail de sortie (clignote) si visible */
        if (d->boss_dead) {
            int px = ox + d->exit_x, py = oy + d->exit_y;
            uint32_t pc = ((int)(g->time * 4.f) & 1) ? 0x80E0FFFF : 0x4070C0FF;
            fill_rect(g->renderer, px - 1, py - 1, 3, 3, pc);
        }
        /* joueur : point jaune clignotant */
        int px = ox + ptx, py = oy + pty;
        uint32_t playerc = ((int)(g->time * 5.f) & 1) ? 0xFFFF80FF : 0xFFFFFFFF;
        fill_rect(g->renderer, px - 1, py - 1, 3, 3, playerc);

        /* legende minuscule sous la minimap */
        text_draw(g->renderer, mm_x, mm_y + mm_h + 1, "MAP", 0x808080FF);
    }
}

