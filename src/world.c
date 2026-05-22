/*
 * world.c - "tick" generique du gameplay : particles, pickups (hover),
 * room logic. Le reste de world a ete decoupe :
 *   dungeon.c  - generation procedurale
 *   entities.c - pool spawners + collision tile
 *   player.c   - update_player + pickup collection + take_damage
 *   enemies.c  - update_enemies + drops + boss + world_enemy_damage
 */
#include "game.h"
#include <stdlib.h>
#include <math.h>

/* ---------- PARTICLES ---------- */
void update_particles(Game *g) {
    float dt = g->dt;
    for (int i = 0; i < MAX_PARTICLES; i++) {
        Particle *p = &g->particles[i];
        if (!p->alive) continue;
        p->x += p->vx * dt;
        p->y += p->vy * dt;
        p->vx *= 0.92f;
        p->vy *= 0.92f;
        p->life -= dt;
        if (p->life <= 0.f) p->alive = false;
    }
    /* === Particules ambiantes par biome (effet "ciel vivant") ===
     * Spawn aleatoire autour du joueur dans un rayon visible. Type +
     * couleur + vecteur de drift dependent du biome courant. */
    static float ambient_timer = 0.f;
    ambient_timer += dt;
    /* Plus dense que avant (0.06s -> 0.025s = ~40/s pour saturer le
     * visuel sparkles Valheim) */
    if (ambient_timer < 0.025f) return;
    ambient_timer = 0.f;
    if (g->state != GS_RUN) return;
    /* Sparkles autour des torches : pour chaque tile T_TORCH proche
     * du joueur, spawn 1 ember additionnel chaque update. */
    {
        Dungeon *d = &g->dungeon;
        int ptx = (int)(g->player.x / TILE);
        int pty = (int)(g->player.y / TILE);
        for (int dz = -8; dz <= 8; dz++) {
            for (int dx = -8; dx <= 8; dx++) {
                int tx = ptx + dx, ty = pty + dz;
                if (tx < 0 || ty < 0 || tx >= MAP_W || ty >= MAP_H) continue;
                if (d->tiles[ty][tx] != T_TORCH) continue;
                if ((rand() % 100) > 35) continue;
                float sx = tx * TILE + TILE / 2 + (rand() % 8) - 4;
                float sy = ty * TILE + TILE / 2 + (rand() % 8) - 4;
                float vy = -25.f - (rand() % 20);
                particle_spawn_kind(g, sx, sy, (rand() % 14) - 7, vy,
                                     1.5f, 0xFFB050E0, 2.2f, 0);
            }
        }
    }
    /* Sparkles autour du joueur quand il porte un unique / element :
     * petite aura. Une particule legere par tick. */
    if ((rand() % 100) < 50) {
        Player *pp = &g->player;
        bool sparkle = false;
        for (int s = 0; s < EQUIP_SLOTS; s++) {
            Item *it = &pp->equipped[s];
            if (it->occupied && (it->is_unique || it->rarity >= R_EPIC)) {
                sparkle = true; break;
            }
        }
        if (sparkle) {
            float ang = (rand() % 360) * 0.01745f;
            float r2 = 12.f + (rand() % 14);
            uint32_t cc = 0xFFE040C0;
            particle_spawn_kind(g, pp->x + cosf(ang) * r2, pp->y + sinf(ang) * r2,
                                 ((rand() % 30) - 15) * 0.4f,
                                 -10.f - (rand() % 15),
                                 1.2f, cc, 1.5f, 0);
        }
    }
    int bi = biome_for_floor(g->floor_index);
    /* spawn area : autour du joueur en world coords (TILE-based) */
    float px = g->player.x, py = g->player.y;
    float sx = px + ((rand() % 400) - 200);
    float sy = py + ((rand() % 250) - 200);
    uint32_t col;
    float vx, vy, life, sz;
    int kind = 0;
    switch (bi) {
        case 0: /* Cimetiere : pollen violet drift bas */
            col = 0x9078A0A0;
            vx  = ((rand() % 40) - 20) * 0.2f;
            vy  = 8.f + (rand() % 10);
            life = 4.0f;
            sz   = 1.5f;
            break;
        case 1: /* Forge : embers oranges qui montent */
            col = 0xFFA040C0;
            vx  = ((rand() % 30) - 15) * 0.3f;
            vy  = -20.f - (rand() % 20);
            life = 3.5f;
            sz   = 1.8f;
            break;
        case 2: /* Marais : motes vert-bleues legeres */
            col = 0x80E0A080;
            vx  = ((rand() % 30) - 15) * 0.4f;
            vy  = -6.f + ((rand() % 12) - 6);
            life = 5.0f;
            sz   = 1.4f;
            break;
        case 3: /* Verger : pollen dore + spores */
            col = 0xFFE060B0;
            vx  = ((rand() % 50) - 25) * 0.3f;
            vy  = -10.f + ((rand() % 14) - 7);
            life = 5.5f;
            sz   = 1.5f;
            break;
        case 4: /* Sanctuaire : flocons blancs */
            col = 0xE0E0FFD0;
            vx  = ((rand() % 30) - 15) * 0.3f;
            vy  = 14.f + (rand() % 10);
            life = 4.5f;
            sz   = 1.8f;
            break;
        default: /* etage 0 / archimage : motes violet cosmiques */
            col = 0xA060FFB0;
            vx  = ((rand() % 30) - 15) * 0.3f;
            vy  = ((rand() % 30) - 15) * 0.3f;
            life = 6.0f;
            sz   = 1.6f;
            break;
    }
    particle_spawn_kind(g, sx, sy, vx, vy, life, col, sz, kind);
}

/* ---------- PICKUPS ---------- */
void update_pickups(Game *g) {
    float dt = g->dt;
    /* Collision items vs portail : un item ne peut pas rester sur le
     * portail de sortie de donjon (le joueur passait sans le ramasser).
     * Ejection radiale douce jusqu'a sortir du rayon. */
    const float PORTAL_REPEL_R = 18.f;
    for (int i = 0; i < MAX_PICKUPS; i++) {
        Pickup *po = &g->pickups[i];
        if (!po->alive || po->kind != PU_PORTAL) continue;
        for (int j = 0; j < MAX_PICKUPS; j++) {
            if (i == j) continue;
            Pickup *it = &g->pickups[j];
            if (!it->alive) continue;
            /* on n'ejecte que les pickups "lourds" (equipement, armes,
             * talismans, coffres). Les coins/food gardent leur position. */
            if (it->kind != PU_ITEM && it->kind != PU_WEAPON &&
                it->kind != PU_ELEMENT && it->kind != PU_CHEST) continue;
            float ddx = it->x - po->x;
            float ddy = it->y - po->y;
            float d2 = ddx * ddx + ddy * ddy;
            if (d2 < PORTAL_REPEL_R * PORTAL_REPEL_R) {
                float d = sqrtf(d2);
                if (d < 0.5f) {
                    /* exactement sur le portail : pousse dans une dir random */
                    float a = (rand() % 360) * 0.01745f;
                    ddx = cosf(a); ddy = sinf(a); d = 1.f;
                }
                float push = (PORTAL_REPEL_R - d) + 1.f;
                it->x += (ddx / d) * push;
                it->y += (ddy / d) * push;
            }
        }
    }
    for (int i = 0; i < MAX_PICKUPS; i++) {
        Pickup *p = &g->pickups[i];
        if (!p->alive) continue;
        p->hover_t += dt * 4.f;
    }
}

/* ---------- ROOM LOGIC ---------- */
static bool point_in_room(Room *r, float x, float y) {
    int tx = (int)(x / TILE);
    int ty = (int)(y / TILE);
    return tx >= r->x && tx < r->x + r->w && ty >= r->y && ty < r->y + r->h;
}

void update_room_logic(Game *g) {
    Player *p = &g->player;
    for (int i = 0; i < g->dungeon.room_count; i++) {
        Room *r = &g->dungeon.rooms[i];
        if (!point_in_room(r, p->x, p->y)) continue;
        bool first_visit = !r->visited;
        r->visited = true;     /* la salle apparait sur la minimap */
        /* Lock des portes : a la 1ere entree d'une salle non-clear avec
         * des ennemis (ou boss room non finie), on verrouille. Les
         * portes T_DOOR de la salle deviennent solides. Reset a false
         * automatiquement quand cleared dans update_enemies. */
        if (first_visit && !r->cleared &&
            (r->enemies_to_spawn > 0 || r->is_boss_room)) {
            r->locked = true;
            log_push(g, 0xFF6060FF, "Portes verrouillees -- elimine les ennemis");
        }
        if (first_visit) {
            /* === FLOOR 0 : spawn des 7 portails en cercle ===
             * value du portail :
             *   -1  = HUB (retour cimetiere)
             *   0..4= biome i (target floor = i*2 + 1)
             *   5   = ARCHIMAGE (target floor 11), debloque si 5 biomes
             *         cleared
             * Pickup_spawn pour chaque, dispose en cercle. */
            if (g->floor_index == 0) {
                int cx = g->dungeon.spawn_x * TILE + TILE / 2;
                int cy = g->dungeon.spawn_y * TILE + TILE / 2;
                float rad_px = 130.f;
                int dest[7] = { -1, 0, 1, 2, 3, 4, 5 };
                for (int k = 0; k < 7; k++) {
                    float a = (k / 7.f) * 6.2831f - 1.57f;
                    float px = cx + cosf(a) * rad_px;
                    float py = cy + sinf(a) * rad_px;
                    pickup_spawn(g, PU_PORTAL, dest[k], px, py);
                }
                /* 3 batiments sur un anneau interieur, decales par rapport
                 * aux portails. Marchand / Altar / Slot machine. */
                float rad_inner = 60.f;
                pickup_spawn(g, PU_MERCHANT,    0, cx,          cy - rad_inner);
                pickup_spawn(g, PU_ALTAR,       0, cx + rad_inner * 0.866f, cy + rad_inner * 0.5f);
                pickup_spawn(g, PU_SLOTMACHINE, 0, cx - rad_inner * 0.866f, cy + rad_inner * 0.5f);
                log_push(g, 0xFFE090FF,
                         "Choisis ton chemin (7 portails autour de toi)");
                log_push(g, 0xFFD040FF,
                         "Marchand / Autel / Machine a sous au centre");
            }
            /* reset per-salle des charges uniques */
            p->phoenix_charge    = true;
            p->last_stand_charge = true;
            p->hazard_stacks     = 0;
            /* kill_stack_count : reset SEULEMENT en entrant chez le boss */
            if (r->is_boss_room) p->kill_stack_count = 0;
            /* Salles speciales : spawn de loot a l'entree. */
            int cx = (r->x + r->w / 2) * TILE + TILE / 2;
            int cy = (r->y + r->h / 2) * TILE + TILE / 2;
            if (r->kind == ROOM_KIND_TREASURE) {
                /* 3 coffres en triangle + 2 PU_COIN */
                pickup_spawn(g, PU_CHEST, 0, cx - 24, cy);
                pickup_spawn(g, PU_CHEST, 0, cx + 24, cy);
                pickup_spawn(g, PU_CHEST, 0, cx, cy - 20);
                pickup_spawn(g, PU_COIN, 3, cx - 8, cy + 16);
                pickup_spawn(g, PU_COIN, 3, cx + 8, cy + 16);
                log_push(g, 0xFFD040FF, "Salle au tresor");
            } else if (r->kind == ROOM_KIND_INN) {
                /* coeur + 2 food + scroll. Heal au passage. */
                pickup_spawn(g, PU_HEART, 0, cx, cy);
                pickup_spawn(g, PU_FOOD, 18, cx - 18, cy + 8);
                pickup_spawn(g, PU_FOOD, 18, cx + 18, cy + 8);
                pickup_spawn(g, PU_SCROLL, 0, cx, cy - 18);
                /* heal modere immediat (auberge accueillante) */
                p->hp += 25.f;
                if (p->hp > p->maxhp) p->hp = p->maxhp;
                log_push(g, 0x80E0A0FF, "Auberge : +25 PV");
            } else if (r->kind == ROOM_KIND_CHALLENGE) {
                log_push(g, 0xFF8040FF, "Salle de defi !");
            }
        }

        if (r->is_boss_room) {
            if (!r->boss_spawned) {
                r->boss_spawned = true;
                int sx = r->x + r->w / 2;
                int sy = r->y + r->h / 2;
                int idx = enemy_spawn(g, EK_BOSS,
                                      sx * TILE + TILE / 2,
                                      sy * TILE + TILE / 2);
                if (g->floor_index >= MAX_FLOORS) {
                    /* Etage 11 : l'ARCHIMAGE. Boss final. */
                    if (idx >= 0) {
                        Enemy *e = &g->enemies[idx];
                        e->is_archmage = true;
                        e->maxhp = 3000.f;
                        e->hp    = 3000.f;
                        e->arch_grow = 0.f;
                        e->dmg_flat  = 12.f;
                        e->element   = EL_NONE;     /* tous les elements */
                    }
                    snprintf(g->boss_name, sizeof(g->boss_name),
                             "Archimage des Onze");
                    /* permutation des 10 elements pour l'ordre des attaques */
                    int order[10];
                    for (int i = 0; i < 10; i++) order[i] = i + EL_FIRE;
                    for (int i = 9; i > 0; i--) {
                        int j = rand() % (i + 1);
                        int t = order[i]; order[i] = order[j]; order[j] = t;
                    }
                    for (int i = 0; i < 10; i++) g->arch_element_order[i] = order[i];
                    g->arch_phase = 0;
                    g->arch_phase_timer = 0.f;
                    g->arch_stasis_t = 0.f;
                    g->arch_attack_cd = 2.5f;     /* delay avant 1er coup */
                    g->arch_attack_pattern = 0;
                    g->arch_summoned[0] = g->arch_summoned[1] = g->arch_summoned[2] = -1;
                    g->arch_intro_done = false;
                    snprintf(g->arch_speech, sizeof(g->arch_speech),
                             "...Tu oses encore te dresser ? Approche, mortel.");
                    g->arch_speech_t = 5.0f;
                    g->boss_intro_t = 3.0f;
                    log_push(g, 0xFFA040FF, "L'Archimage des Onze t'attend...");
                } else {
                    /* nom thematique du boss : variant = biome courant. */
                    int bv = biome_for_floor(g->floor_index);
                    snprintf(g->boss_name, sizeof(g->boss_name),
                             "%s (E%d)", boss_title_for_variant(bv), g->floor_index);
                    g->boss_intro_t = 2.5f;
                }
                sfx_play(g, SFX_BOSS);
            }
            return;
        }

        if (r->cleared) continue;
        if (r->enemies_to_spawn > 0) {
            r->spawn_timer_ms += (int)(g->dt * 1000.f);
            int spawn_interval = 600 - g->floor_index * 25;
            if (spawn_interval < 120) spawn_interval = 120;
            while (r->spawn_timer_ms >= spawn_interval && r->enemies_to_spawn > 0) {
                r->spawn_timer_ms -= spawn_interval;
                /* Spawn pool floor-gated : la variete s ouvre avec la
                 * progression pour eviter de noyer le joueur etage 1.
                 * Biome bias : l ennemi aligne au biome courant a 2 slots
                 * supplementaires (independamment de la gate par etage,
                 * pour donner du caractere a chaque biome). */
                int kind;
                int fi = g->floor_index;
                int pool[20]; int pn = 0;
                pool[pn++] = EK_ZOMBIE; pool[pn++] = EK_ZOMBIE;     /* x2 commun */
                pool[pn++] = EK_SLIME;
                pool[pn++] = EK_RAT;    pool[pn++] = EK_RAT;        /* swarm */
                if (fi >= 2) pool[pn++] = EK_BANDIT;
                if (fi >= 3) pool[pn++] = EK_GHOST;
                if (fi >= 4) pool[pn++] = EK_CHARGER;
                if (fi >= 4) pool[pn++] = EK_HEALER;
                if (fi >= 5) pool[pn++] = EK_DEMON;
                if (fi >= 5) pool[pn++] = EK_BUFFER;
                if (fi >= 6) pool[pn++] = EK_MAGE;
                if (fi >= 6) pool[pn++] = EK_NECROMANCER;
                int aligned = biome_aligned_kind(biome_for_floor(fi));
                if (aligned >= 0 && pn < (int)(sizeof(pool)/sizeof(pool[0])) - 1) {
                    pool[pn++] = aligned;
                    pool[pn++] = aligned;
                }
                kind = pool[rand() % pn];
                int sx = r->x + 1 + rand() % (r->w - 2);
                int sy = r->y + 1 + rand() % (r->h - 2);
                enemy_spawn(g, kind, sx * TILE + TILE / 2, sy * TILE + TILE / 2);
                r->enemies_to_spawn--;
            }
        } else {
            bool any = false;
            for (int e = 0; e < MAX_ENEMIES; e++) {
                if (!g->enemies[e].alive) continue;
                if (point_in_room(r, g->enemies[e].x, g->enemies[e].y)) { any = true; break; }
            }
            if (!any) {
                r->cleared = true;
                /* Unlock des portes : la salle est videe, le joueur peut
                 * passer librement vers les salles adjacentes. */
                r->locked = false;
                /* trinket puddle_on_room : pose une flaque au centre de
                 * la salle quand elle est nettoyee. Element = water/fire/
                 * lightning/etc selon trinket equipe. */
                if (p->puddle_on_room > EL_NONE && p->puddle_on_room < EL_COUNT) {
                    int kind = SURF_WATER;
                    switch (p->puddle_on_room) {
                        case EL_FIRE:      kind = SURF_FIRE; break;
                        case EL_WATER:     kind = SURF_WATER; break;
                        case EL_LIGHTNING: kind = SURF_ELECTRIFIED; break;
                        default: kind = SURF_WATER; break;
                    }
                    surface_spawn(g, kind,
                                  (r->x + r->w / 2) * TILE,
                                  (r->y + r->h / 2) * TILE,
                                  28.f, 12.f);
                }
                if ((rand() % 100) < 80) {
                    pickup_spawn(g, PU_CHEST, 0,
                                 (r->x + r->w / 2) * TILE,
                                 (r->y + r->h / 2) * TILE);
                }
                /* Salle de defi : bonus chest garanti + un coin pour la
                 * difficulte accrue. */
                if (r->kind == ROOM_KIND_CHALLENGE) {
                    pickup_spawn(g, PU_CHEST, 0,
                                 (r->x + r->w / 2 + 1) * TILE,
                                 (r->y + r->h / 2 + 1) * TILE);
                    pickup_spawn(g, PU_COIN, 5,
                                 (r->x + r->w / 2) * TILE,
                                 (r->y + r->h / 2 - 1) * TILE);
                    log_push(g, 0xFF8040FF, "Defi reussi : +bonus !");
                }
                /* Loot contextuel "petit objet flottant".
                 * 35% : un PU_FOOD pose entre coffre et entree.
                 * 12% supp : un parchemin de lore (rare).
                 * Position : un coin du milieu de la piece (pas pile sur
                 * le coffre, pour qu'on les distingue). */
                if ((rand() % 100) < 35) {
                    int t = rand() % 3;
                    int heal = (t == 0) ? 8 : (t == 1) ? 16 : 12;
                    int ox = r->x + 1 + rand() % (r->w - 2);
                    int oy = r->y + 1 + rand() % (r->h - 2);
                    pickup_spawn(g, PU_FOOD, heal,
                                 ox * TILE + TILE/2, oy * TILE + TILE/2);
                }
                if ((rand() % 100) < 12) {
                    int ox = r->x + 1 + rand() % (r->w - 2);
                    int oy = r->y + 1 + rand() % (r->h - 2);
                    pickup_spawn(g, PU_SCROLL, 0,
                                 ox * TILE + TILE/2, oy * TILE + TILE/2);
                }
            }
        }
        return;
    }
}
