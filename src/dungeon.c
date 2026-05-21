/*
 * dungeon.c - generation procedurale du donjon (rooms + corridors + portes)
 * + salle bac-a-sable du mode debug.
 *
 * Extrait de world.c pour separer la generation du donjon du gameplay
 * (player/enemies/pickups/room_logic). Les helpers carve_*, rand_range,
 * room_center_dist, place_doors_for_room sont prives a ce fichier.
 */
#include "game.h"
#include <stdlib.h>
#include <string.h>

bool tile_solid(TileKind t) {
    return t == T_VOID || t == T_WALL || t == T_WALL_CRACKED;
}

/* ---------- DUNGEON GENERATION ---------- */
static void carve_room(Dungeon *d, int x, int y, int w, int h) {
    for (int yy = y; yy < y + h; yy++) {
        for (int xx = x; xx < x + w; xx++) {
            if (xx <= 0 || yy <= 0 || xx >= MAP_W - 1 || yy >= MAP_H - 1) continue;
            d->tiles[yy][xx] = T_FLOOR;
        }
    }
}

static void carve_corridor(Dungeon *d, int x1, int y1, int x2, int y2) {
    int x = x1, y = y1;
    while (x != x2) {
        if (x > 0 && y > 0 && x < MAP_W - 1 && y < MAP_H - 1) d->tiles[y][x] = T_FLOOR;
        x += (x2 > x) ? 1 : -1;
    }
    while (y != y2) {
        if (x > 0 && y > 0 && x < MAP_W - 1 && y < MAP_H - 1) d->tiles[y][x] = T_FLOOR;
        y += (y2 > y) ? 1 : -1;
    }
    if (x > 0 && y > 0 && x < MAP_W - 1 && y < MAP_H - 1) d->tiles[y][x] = T_FLOOR;
}

static int rand_range(int lo, int hi) {
    if (hi <= lo) return lo;
    return lo + rand() % (hi - lo);
}

/* distance Manhattan entre les centres de 2 salles */
static int room_center_dist(const Room *a, const Room *b) {
    int ax = a->x + a->w / 2, ay = a->y + a->h / 2;
    int bx = b->x + b->w / 2, by = b->y + b->h / 2;
    int dx = ax - bx; if (dx < 0) dx = -dx;
    int dy = ay - by; if (dy < 0) dy = -dy;
    return dx + dy;
}

/* Pose des T_DOOR a la jonction entre un couloir et l interieur d une
 * salle : pour chaque tile sur le rectangle des "murs" entourant la salle
 * (les tiles juste en dehors de x..x+w, y..y+h), si elle est devenue
 * T_FLOOR (carvee par un corridor) on la transforme en T_DOOR. */
static void place_doors_for_room(Dungeon *d, const Room *r) {
    for (int x = r->x - 1; x <= r->x + r->w; x++) {
        if (x <= 0 || x >= MAP_W - 1) continue;
        int yt = r->y - 1, yb = r->y + r->h;
        if (yt > 0       && d->tiles[yt][x] == T_FLOOR) d->tiles[yt][x] = T_DOOR;
        if (yb < MAP_H-1 && d->tiles[yb][x] == T_FLOOR) d->tiles[yb][x] = T_DOOR;
    }
    for (int y = r->y - 1; y <= r->y + r->h; y++) {
        if (y <= 0 || y >= MAP_H - 1) continue;
        int xl = r->x - 1, xr = r->x + r->w;
        if (xl > 0       && d->tiles[y][xl] == T_FLOOR) d->tiles[y][xl] = T_DOOR;
        if (xr < MAP_W-1 && d->tiles[y][xr] == T_FLOOR) d->tiles[y][xr] = T_DOOR;
    }
}

void dungeon_generate(Dungeon *d, int floor_index, unsigned seed) {
    srand(seed);
    int prev_gen = d->gen_id;
    memset(d, 0, sizeof(*d));
    d->gen_id = prev_gen + 1;          /* invalide la cache mesh render */
    d->level_index = floor_index;
    for (int y = 0; y < MAP_H; y++)
        for (int x = 0; x < MAP_W; x++)
            d->tiles[y][x] = T_WALL;

    /* === ETAGE 0 : arene-pivot circulaire avec 7 portails ===
     * Une seule grande salle circulaire, runes au sol, torches aux
     * extremites. Les portails sont spawnes par world.c (update_room
     * logic, depuis g->biome_cleared). */
    if (floor_index == 0) {
        int cx = MAP_W / 2, cy = MAP_H / 2;
        int rad = 14;
        for (int y = -rad; y <= rad; y++)
            for (int x = -rad; x <= rad; x++) {
                if (x*x + y*y <= rad * rad) {
                    int tx = cx + x, ty = cy + y;
                    if (tx >= 0 && tx < MAP_W && ty >= 0 && ty < MAP_H)
                        d->tiles[ty][tx] = T_FLOOR;
                }
            }
        /* runes circulaires au centre + torches aux 4 cardinaux du bord */
        d->tiles[cy][cx] = T_RUNE;
        for (int k = 0; k < 8; k++) {
            float a = (k / 8.f) * 6.2831f;
            int rx = cx + (int)(cosf(a) * 4.f);
            int ry = cy + (int)(sinf(a) * 4.f);
            if (rx > 0 && ry > 0 && rx < MAP_W-1 && ry < MAP_H-1)
                d->tiles[ry][rx] = T_RUNE;
        }
        for (int k = 0; k < 4; k++) {
            int tx = cx + (k == 0 ? -rad+2 : (k == 1 ? rad-2 : 0));
            int ty = cy + (k == 2 ? -rad+2 : (k == 3 ? rad-2 : 0));
            if (tx > 0 && ty > 0 && tx < MAP_W-1 && ty < MAP_H-1)
                d->tiles[ty][tx] = T_TORCH;
        }
        d->spawn_x = cx; d->spawn_y = cy;
        d->rooms[0].x = cx - rad; d->rooms[0].y = cy - rad;
        d->rooms[0].w = rad * 2 + 1; d->rooms[0].h = rad * 2 + 1;
        d->rooms[0].cleared = true;
        d->rooms[0].visited = false;     /* false pour que first_visit
                                            spawne les portails */
        d->rooms[0].enemies_to_spawn = 0;
        d->rooms[0].is_boss_room = false;
        d->room_count = 1;
        d->boss_room_idx = -1;
        return;
    }

    /* === ETAGE 11 : arene Archimage ===
     * Salle ronde dediee, T_RUNE au centre, torches. Pas de couloirs.
     * Le boss spawn via la logique is_boss_room normale. */
    if (floor_index == 11) {
        int cx = MAP_W / 2, cy = MAP_H / 2;
        int rad = 16;
        for (int y = -rad; y <= rad; y++)
            for (int x = -rad; x <= rad; x++) {
                /* arene en forme d'oeil (allongee verticalement) */
                float fx = (float)x / rad;
                float fy = (float)y / (rad * 0.85f);
                if (fx*fx + fy*fy <= 1.0f) {
                    int tx = cx + x, ty = cy + y;
                    if (tx >= 0 && tx < MAP_W && ty >= 0 && ty < MAP_H)
                        d->tiles[ty][tx] = T_FLOOR;
                }
            }
        /* pentacle au centre : 5 runes */
        for (int k = 0; k < 5; k++) {
            float a = (k / 5.f) * 6.2831f - 1.57f;
            int rx = cx + (int)(cosf(a) * 6.f);
            int ry = cy + (int)(sinf(a) * 6.f);
            if (rx > 0 && ry > 0 && rx < MAP_W-1 && ry < MAP_H-1)
                d->tiles[ry][rx] = T_RUNE;
        }
        d->tiles[cy][cx] = T_RUNE;
        /* torches aux 4 cardinaux distants */
        for (int k = 0; k < 4; k++) {
            int tx = cx + (k == 0 ? -rad+3 : (k == 1 ? rad-3 : 0));
            int ty = cy + (k == 2 ? -rad+3 : (k == 3 ? rad-3 : 0));
            if (tx > 0 && ty > 0 && tx < MAP_W-1 && ty < MAP_H-1)
                d->tiles[ty][tx] = T_TORCH;
        }
        d->spawn_x = cx; d->spawn_y = cy + rad - 4;
        d->rooms[0].x = cx - rad; d->rooms[0].y = cy - rad;
        d->rooms[0].w = rad * 2 + 1; d->rooms[0].h = rad * 2 + 1;
        d->rooms[0].cleared = false;
        d->rooms[0].visited = false;
        d->rooms[0].enemies_to_spawn = 0;
        d->rooms[0].is_boss_room = true;
        d->rooms[0].boss_spawned = false;
        d->boss_room_idx = 0;
        d->room_count = 1;
        return;
    }

    int target_rooms = 6 + floor_index / 2;
    if (target_rooms > 12) target_rooms = 12;
    /* portee max d un couloir (Manhattan) entre deux salles : evite les
     * couloirs interminables qui traversent toute la carte. */
    const int MAX_LINK_DIST = 22;
    int placed = 0, attempts = 0;
    while (placed < target_rooms && attempts < 400) {
        attempts++;
        /* Salles agrandies +25% par rapport a l'ancien (7..13 / 7..11)
         * pour donner plus d'espace au combat. Le HUB et l'etage 0
         * (pivot 7 portails) ont leur propre arene fixee, non touchee. */
        int rw = rand_range(9, 16);
        int rh = rand_range(9, 14);
        int rx = rand_range(2, MAP_W - rw - 2);
        int ry = rand_range(2, MAP_H - rh - 2);
        bool overlap = false;
        for (int i = 0; i < placed; i++) {
            Room *o = &d->rooms[i];
            if (rx < o->x + o->w + 1 && rx + rw + 1 > o->x &&
                ry < o->y + o->h + 1 && ry + rh + 1 > o->y) {
                overlap = true; break;
            }
        }
        if (overlap) continue;
        if (placed > 0) {
            Room candidate = { rx, ry, rw, rh, 0,0,0,0,0,0,0,0, ROOM_KIND_NORMAL };
            bool ok = false;
            for (int i = 0; i < placed; i++) {
                if (room_center_dist(&candidate, &d->rooms[i]) <= MAX_LINK_DIST) {
                    ok = true; break;
                }
            }
            if (!ok) continue;
        }
        d->rooms[placed].x = rx;
        d->rooms[placed].y = ry;
        d->rooms[placed].w = rw;
        d->rooms[placed].h = rh;
        d->rooms[placed].cleared = false;
        /* count par salle : 3 + floor (croissance lineaire) + jitter 3.
         * Au-dela de floor 5 on accelere legerement pour densifier la
         * fin de run sans noyer le debut. */
        int n_base = 3 + floor_index + rand() % 3;
        if (floor_index > 5) n_base += (floor_index - 5) / 2;
        d->rooms[placed].enemies_to_spawn = n_base;
        d->rooms[placed].spawn_timer_ms = 0;
        d->rooms[placed].is_boss_room = false;
        d->rooms[placed].boss_spawned = false;
        carve_room(d, rx, ry, rw, rh);
        placed++;
    }
    d->room_count = placed;

    /* connexion en MST simple (Prim) sur les centres de salles. */
    bool connected[32] = {0};
    if (placed > 0) connected[0] = true;
    for (int k = 1; k < placed; k++) {
        int best_from = -1, best_to = -1, best_d = 1 << 30;
        for (int i = 0; i < placed; i++) {
            if (!connected[i]) continue;
            for (int j = 0; j < placed; j++) {
                if (connected[j]) continue;
                int dij = room_center_dist(&d->rooms[i], &d->rooms[j]);
                if (dij < best_d) { best_d = dij; best_from = i; best_to = j; }
            }
        }
        if (best_to < 0) break;
        Room *a = &d->rooms[best_from];
        Room *b = &d->rooms[best_to];
        carve_corridor(d, a->x + a->w / 2, a->y + a->h / 2,
                          b->x + b->w / 2, b->y + b->h / 2);
        connected[best_to] = true;
    }

    if (placed >= 1) {
        d->spawn_x = d->rooms[0].x + d->rooms[0].w / 2;
        d->spawn_y = d->rooms[0].y + d->rooms[0].h / 2;
        d->rooms[0].cleared = true;
        d->rooms[0].enemies_to_spawn = 0;
        d->rooms[0].visited = true;        /* salle d'entree deja sur la minimap */
    }

    /* Boss room : la salle la PLUS LOIN de la salle de spawn. */
    d->boss_room_idx = -1;
    if (placed >= 2) {
        int best = -1, bestd = -1;
        for (int i = 1; i < placed; i++) {
            int dd = room_center_dist(&d->rooms[0], &d->rooms[i]);
            if (dd > bestd) { bestd = dd; best = i; }
        }
        if (best > 0) {
            d->boss_room_idx = best;
            Room *r = &d->rooms[best];
            r->is_boss_room = true;
            r->enemies_to_spawn = 0;
            r->cleared = false;
            d->exit_x = r->x + r->w / 2;
            d->exit_y = r->y + r->h / 2;
            int cx = r->x + r->w / 2, cy = r->y + r->h / 2;
            for (int yy = -2; yy <= 2; yy++) for (int xx = -2; xx <= 2; xx++) {
                if (abs(xx) + abs(yy) == 3 && cx+xx>0 && cy+yy>0 && cx+xx<MAP_W-1 && cy+yy<MAP_H-1) {
                    d->tiles[cy + yy][cx + xx] = T_RUNE;
                }
            }
        }
    }

    /* Salles speciales : MAXIMUM UNE par etage (le joueur trouvait avant
     * 3 salles paisibles en sprint -- on en garde une seule, tiree au
     * sort entre TREASURE / INN / CHALLENGE pour preserver la variete.
     * Challenge n'est pas paisible donc on l'autorise toujours en plus
     * si la salle pacifique tiree est TREASURE ou INN. */
    {
        int candidates[32]; int n_cand = 0;
        for (int i = 1; i < placed; i++) {
            if (d->rooms[i].is_boss_room) continue;
            candidates[n_cand++] = i;
        }
        /* shuffle Fisher-Yates */
        for (int i = n_cand - 1; i > 0; i--) {
            int j = rand() % (i + 1);
            int t = candidates[i]; candidates[i] = candidates[j]; candidates[j] = t;
        }
        /* Tire UNE seule salle peaceful (treasure ou inn, 50/50). Si on a
         * encore une salle dispo, on peut placer un CHALLENGE en plus
         * (combat avec loot bonus, pas pacifique). */
        if (n_cand >= 1) {
            Room *r = &d->rooms[candidates[0]];
            r->kind = (rand() % 2 == 0) ? ROOM_KIND_TREASURE : ROOM_KIND_INN;
            r->enemies_to_spawn = 0;
            r->cleared = true;
        }
        if (n_cand >= 2) {
            Room *r = &d->rooms[candidates[1]];
            r->kind = ROOM_KIND_CHALLENGE;
            r->enemies_to_spawn = (int)(r->enemies_to_spawn * 1.5f) + 1;
        }
    }

    /* portes : a poser APRES tous les corridors, sur le perimetre des salles */
    for (int i = 0; i < placed; i++) {
        place_doors_for_room(d, &d->rooms[i]);
    }
    /* murs fissures : ~3 par salle non-boss sur le perimetre (T_WALL ->
     * T_WALL_CRACKED). Permet au joueur de creer ses propres raccourcis
     * via les AOE. Aucun cracked wall en boss room (gardons l arene). */
    for (int i = 0; i < placed; i++) {
        Room *r = &d->rooms[i];
        if (r->is_boss_room) continue;
        int n_cracks = 2 + (rand() % 2);
        int attempts = 0;
        while (n_cracks > 0 && attempts < 20) {
            attempts++;
            /* tire un cote au hasard, puis une position sur ce cote */
            int side = rand() % 4;
            int x, y;
            switch (side) {
                case 0: x = r->x + rand() % r->w; y = r->y - 1;          break;
                case 1: x = r->x + rand() % r->w; y = r->y + r->h;       break;
                case 2: x = r->x - 1;          y = r->y + rand() % r->h; break;
                default: x = r->x + r->w;      y = r->y + rand() % r->h; break;
            }
            if (x <= 0 || y <= 0 || x >= MAP_W - 1 || y >= MAP_H - 1) continue;
            if (d->tiles[y][x] != T_WALL) continue;
            d->tiles[y][x] = T_WALL_CRACKED;
            n_cracks--;
        }
    }
    /* decorate other rooms */
    for (int ri = 0; ri < placed; ri++) {
        Room *r = &d->rooms[ri];
        int xs[2] = { r->x + 1, r->x + r->w - 2 };
        int ys[2] = { r->y + 1, r->y + r->h - 2 };
        for (int a = 0; a < 2; a++) for (int b = 0; b < 2; b++) {
            int tx = xs[a], ty = ys[b];
            if (d->tiles[ty][tx] == T_FLOOR) d->tiles[ty][tx] = T_TORCH;
        }
        if (!r->is_boss_room && ri > 0) {
            for (int k = 0; k < 2 + rand() % 3; k++) {
                int tx = r->x + 1 + rand() % (r->w - 2);
                int ty = r->y + 1 + rand() % (r->h - 2);
                if (d->tiles[ty][tx] == T_FLOOR) d->tiles[ty][tx] = (rand() % 2) ? T_BLOOD : T_BONES;
            }
        }
    }
}

/* ---------- DEBUG ROOM ----------
 * Salle bac-a-sable adjacente a la salle de spawn, peuplee d'un exemplaire
 * de chaque arme, element et piece d'equipement legendaire. Idempotent. */
void dungeon_add_debug_room(Game *g) {
    Dungeon *d = &g->dungeon;
    if (d->room_count <= 0 || d->room_count >= 32) return;
    int debug_idx = -1;
    for (int i = 0; i < d->room_count; i++)
        if (d->rooms[i].is_debug_room) { debug_idx = i; break; }

    if (debug_idx < 0) {
        Room *spawn = &d->rooms[0];
        int rw = 11, rh = 9;
        int candidates[4][2] = {
            { spawn->x + spawn->w + 2, spawn->y                },   /* droite */
            { spawn->x - rw - 2,        spawn->y                },   /* gauche */
            { spawn->x,                 spawn->y + spawn->h + 2 },   /* dessous */
            { spawn->x,                 spawn->y - rh - 2       },   /* dessus */
        };
        int rx = -1, ry = -1;
        for (int c = 0; c < 4; c++) {
            int tx = candidates[c][0], ty = candidates[c][1];
            if (tx <= 1 || ty <= 1 || tx + rw >= MAP_W - 1 || ty + rh >= MAP_H - 1) continue;
            bool overlap = false;
            for (int i = 0; i < d->room_count; i++) {
                Room *o = &d->rooms[i];
                if (tx < o->x + o->w + 1 && tx + rw + 1 > o->x &&
                    ty < o->y + o->h + 1 && ty + rh + 1 > o->y) {
                    overlap = true; break;
                }
            }
            if (!overlap) { rx = tx; ry = ty; break; }
        }
        if (rx < 0) return;
        carve_room(d, rx, ry, rw, rh);
        carve_corridor(d,
                       spawn->x + spawn->w / 2, spawn->y + spawn->h / 2,
                       rx + rw / 2,             ry + rh / 2);
        Room *r = &d->rooms[d->room_count];
        r->x = rx; r->y = ry; r->w = rw; r->h = rh;
        r->cleared = true;
        r->enemies_to_spawn = 0;
        r->is_boss_room = false;
        r->boss_spawned = false;
        r->is_debug_room = true;
        debug_idx = d->room_count;
        d->room_count++;
        d->gen_id++;
    }
    Room *r = &d->rooms[debug_idx];

    float ox = (r->x + 1) * (float)TILE + TILE / 2.f;
    float oy = (r->y + 1) * (float)TILE + TILE / 2.f;
    float step = (float)TILE;
    int col = 0, row = 0;
    #define DBG_PLACE(spawn_call) do { \
        float xx = ox + col * step; \
        float yy = oy + row * step; \
        spawn_call; \
        col++; \
        if (col >= (r->w - 2)) { col = 0; row++; } \
    } while (0)

    for (int wk = W_SWORD; wk < W_COUNT; wk++) {
        DBG_PLACE(pickup_spawn(g, PU_WEAPON, wk | (((int)R_COMMON)    << 8), xx, yy));
    }
    col = 0; row++;
    for (int wk = W_SWORD; wk < W_COUNT; wk++) {
        DBG_PLACE(pickup_spawn(g, PU_WEAPON, wk | (((int)R_LEGENDARY) << 8), xx, yy));
    }
    col = 0; row++;
    for (int e = 1; e < EL_COUNT; e++) {
        DBG_PLACE(pickup_spawn(g, PU_ELEMENT, e, xx, yy));
    }
    col = 0; row++;
    for (int s = 0; s < EQUIP_SLOTS; s++) {
        Item it = item_make((EquipSlot)s, R_LEGENDARY, 4);
        DBG_PLACE(pickup_spawn_item(g, it, xx, yy));
    }
    col = 0; row++;
    DBG_PLACE(pickup_spawn(g, PU_FOOD, 16, xx, yy));
    DBG_PLACE(pickup_spawn(g, PU_FOOD, 16, xx, yy));
    DBG_PLACE(pickup_spawn(g, PU_COIN, 10, xx, yy));
    DBG_PLACE(pickup_spawn(g, PU_COIN, 10, xx, yy));
    DBG_PLACE(pickup_spawn(g, PU_SOUL, 0,  xx, yy));

    #undef DBG_PLACE
}
