/*
 * render_inventory.c - inventaire paper-doll : personnage central
 * + 6 cases equipement + 2 armes + jusqu a 3 talismans / arme + sac 4x3.
 * Cf game.h pour le mapping INV_CURSOR_*.
 */
#include "ui_common.h"
#include "gfx.h"

/* externs : on appelle le vrai render 3D du joueur (utilise dans
 * render_world.c) pour le paperdoll. Pas de duplication de code. */
v3 player_world_pos(Player *p);
void draw_player_3d(Game *g);
#include <math.h>
#include <stdio.h>
#include <string.h>

/* ---------- INVENTORY ---------- */
/* Layout paper-doll :
 *   - colonne gauche  : panneau STATS
 *   - colonne centre  : personnage + 6 slots equipement disposes autour +
 *                       2 slots arme en bas + 1 a 3 slots talisman sous chaque
 *   - colonne droite  : sac 4x3 + panneau details
 *
 * Mapping inv_cursor : voir constantes INV_CURSOR_* dans game.h.
 */

/* positions (en pixels internes 640x360) des cases equipement autour du
 * personnage. Centrees autour du sprite peint a (cx, cy) = (310, 100). */
typedef struct { int dx, dy; const char *short_name; } EquipSlotLayout;
static const EquipSlotLayout EQUIP_LAYOUT[EQUIP_SLOTS] = {
    [SLOT_HELM]   = { -14, -70, "CASQUE"  },  /* au-dessus de la tete */
    [SLOT_CHEST]  = { -52, -28, "TORSE"   },  /* gauche du torse */
    [SLOT_GLOVES] = { -52,   8, "GANTS"   },  /* gauche bas */
    [SLOT_BELT]   = {  26, -28, "CEINT."  },  /* droite torse */
    [SLOT_LEGS]   = {  26,   8, "JAMBES"  },  /* droite bas */
    [SLOT_BOOTS]  = { -14,  44, "BOTTES"  },  /* dessous les pieds */
};

/* Centres du paper-doll : DOIVENT rester synchronises avec render_inventory.
 * Si tu changes ces constantes, change-les aussi dans render_inventory. */
#define INV_PAPERDOLL_CX 240
#define INV_PAPERDOLL_CY 110
#define INV_BAG_X        410
#define INV_BAG_Y         22
#define INV_BAG_CELL      26
#define INV_WEAPON_Y     (INV_PAPERDOLL_CY + 80)
#define INV_WEAPON_X0    (INV_PAPERDOLL_CX - 44)
#define INV_WEAPON_X1    (INV_PAPERDOLL_CX +  8)

bool inv_layout_rect(int cursor_idx, int *x, int *y, int *w, int *h) {
    if (cursor_idx < 0 || cursor_idx >= INV_CURSOR_MAX) return false;
    if (cursor_idx < INV_CURSOR_EQUIP_BASE) {
        int i = cursor_idx;
        int r = i / 4, c = i % 4;
        *x = INV_BAG_X + c * INV_BAG_CELL;
        *y = INV_BAG_Y + r * INV_BAG_CELL;
        *w = 24; *h = 24;
        return true;
    }
    if (cursor_idx < INV_CURSOR_WEAPON_BASE) {
        int i = cursor_idx - INV_CURSOR_EQUIP_BASE;
        *x = INV_PAPERDOLL_CX + EQUIP_LAYOUT[i].dx;
        *y = INV_PAPERDOLL_CY + EQUIP_LAYOUT[i].dy;
        *w = 26; *h = 26;
        return true;
    }
    if (cursor_idx < INV_CURSOR_TALISMAN_BASE) {
        int i = cursor_idx - INV_CURSOR_WEAPON_BASE;   /* 0 ou 1 */
        *x = (i == 0) ? INV_WEAPON_X0 : INV_WEAPON_X1;
        *y = INV_WEAPON_Y;
        *w = 36; *h = 36;
        return true;
    }
    if (cursor_idx < INV_CURSOR_TALISBAG_BASE) {
        /* talisman actifs sur arme : (idx - base) / 3 = weapon, % 3 = slot */
        int rel = cursor_idx - INV_CURSOR_TALISMAN_BASE;
        int wi = rel / 3, ti = rel % 3;
        int wx = (wi == 0) ? INV_WEAPON_X0 : INV_WEAPON_X1;
        *x = (wx - 2) + ti * 14;
        *y = INV_WEAPON_Y + 50;
        *w = 12; *h = 12;
        return true;
    }
    /* Sac talisman dedie : 5 colonnes x 2 lignes, sous la zone armes */
    {
        int rel = cursor_idx - INV_CURSOR_TALISBAG_BASE;
        if (rel < 0 || rel >= TALISMAN_BAG_SLOTS) return false;
        int col = rel % 5;
        int row = rel / 5;
        *x = INV_BAG_X + col * 20;
        *y = INV_WEAPON_Y + 70 + row * 20;
        *w = 18; *h = 18;
        return true;
    }
}

static void render_item_slot(Game *g, int sx, int sy, int sz, Item *it,
                              bool sel, bool marked, const char *empty_label) {
    uint32_t border = 0x404048FF;
    if (sel)    border = 0xFFFF40FF;
    if (marked) border = 0x80FF80FF;
    fill_rect(g->renderer, sx, sy, sz, sz, 0x14101AFF);
    rect_outline(g->renderer, sx, sy, sz, sz, border);
    if (it && it->occupied) {
        uint32_t col = rarity_color(it->rarity);
        if (it->kind == ITEM_KIND_ELEMENT)
            col = element_color((Element)it->base_kind);
        int pad = (sz >= 24) ? 4 : 2;
        fill_rect(g->renderer, sx + pad, sy + pad, sz - 2*pad, sz - 2*pad, col);
        rect_outline(g->renderer, sx + pad, sy + pad, sz - 2*pad, sz - 2*pad, 0x000000FF);
        const char *abbr = "?";
        if (it->kind == ITEM_KIND_WEAPON) {
            /* abbreviation par WeaponKind */
            switch ((WeaponKind)it->base_kind) {
                case W_SWORD:  abbr = "EP"; break;
                case W_SHIELD: abbr = "BC"; break;
                case W_BOW:    abbr = "AR"; break;
                case W_WAND:   abbr = "BA"; break;
                case W_AXE:    abbr = "HA"; break;
                case W_FISTS:  abbr = "PG"; break;
                default: break;
            }
        } else if (it->kind == ITEM_KIND_ELEMENT) {
            /* premiere lettre de l element (en majuscule) */
            const char *n = element_name((Element)it->base_kind);
            static char one[3];
            one[0] = n[0]; one[1] = (n[1] ? n[1] : 0); one[2] = 0;
            abbr = one;
        } else {
            switch (it->slot) {
                case SLOT_HELM:   abbr = "HE"; break;
                case SLOT_CHEST:  abbr = "TO"; break;
                case SLOT_LEGS:   abbr = "JA"; break;
                case SLOT_BOOTS:  abbr = "BO"; break;
                case SLOT_BELT:   abbr = "CE"; break;
                case SLOT_GLOVES: abbr = "GA"; break;
                default: break;
            }
        }
        text_draw(g->renderer, sx + sz/2 - 6, sy + sz/2 - 3, abbr, 0x000000FF);
    } else if (empty_label) {
        /* libelle discret quand le slot est vide -- utile pour le paper-doll */
        text_draw(g->renderer, sx + sz/2 - text_width(empty_label)/2, sy + sz/2 - 3,
                  empty_label, 0x606068FF);
    }
}

/* helper : tint un slot d'equipement par couleur d'element si l'item
 * en a une, sinon par couleur de rarete (ou orange unique). */
static uint32_t equip_tint_color(Item *it, uint32_t default_col) {
    if (!it || !it->occupied) return default_col;
    uint32_t cc = it->is_unique ? 0xFF8030FF : rarity_color(it->rarity);
    /* priorite element si l'item en a un */
    Element el = item_element(it);
    if (el > EL_NONE && el < EL_COUNT) cc = element_color(el);
    /* blend 60% item + 40% defaut pour rester lisible */
    float mix = (it->rarity >= R_LEGENDARY || it->is_unique) ? 0.75f : 0.60f;
    if (el > EL_NONE) mix = 0.80f;       /* element : tres marque */
    int dr = (default_col >> 24) & 0xFF;
    int dg = (default_col >> 16) & 0xFF;
    int db = (default_col >> 8) & 0xFF;
    int tr = (cc >> 24) & 0xFF;
    int tg = (cc >> 16) & 0xFF;
    int tb = (cc >> 8) & 0xFF;
    int r = (int)(dr * (1.f - mix) + tr * mix);
    int g = (int)(dg * (1.f - mix) + tg * mix);
    int b = (int)(db * (1.f - mix) + tb * mix);
    return (uint32_t)((r << 24) | (g << 16) | (b << 8) | 0xFF);
}

/* personnage centre sur (cx, cy), reflet de la silhouette 3D + tints
 * d'equipement. NOTE : remplace par un rendu 3D du vrai joueur via
 * draw_player_3d dans un viewport limite. Garde le code en fallback. */
__attribute__((unused))
static void draw_paperdoll_figure(Game *g, int cx, int cy) {
    Player *p = &g->player;
    uint32_t cape, tunic;
    hero_palette(p->hero, &cape, &tunic);

    /* couleurs effectives par slot equipe */
    uint32_t chest_col  = equip_tint_color(&p->equipped[SLOT_CHEST],  tunic);
    uint32_t legs_col   = equip_tint_color(&p->equipped[SLOT_LEGS],   0x303038FF);
    uint32_t boots_col  = equip_tint_color(&p->equipped[SLOT_BOOTS],  0x202020FF);
    uint32_t gloves_col = equip_tint_color(&p->equipped[SLOT_GLOVES], chest_col);
    Item *eq_helm = &p->equipped[SLOT_HELM];
    Item *eq_belt = &p->equipped[SLOT_BELT];

    /* fond / piedestal sous le perso */
    fill_rect(g->renderer, cx - 22, cy + 32, 44, 4, 0x18141EFF);
    rect_outline(g->renderer, cx - 22, cy + 32, 44, 4, 0x402838FF);

    /* cape : trapeze derriere */
    fill_rect(g->renderer, cx - 18, cy - 18, 36, 42, cape);
    fill_rect(g->renderer, cx - 20, cy + 16, 40, 10, cape);

    /* tunique / plastron */
    fill_rect(g->renderer, cx - 14, cy - 10, 28, 26, chest_col);
    /* epaulettes plus claires si chest equipe (rarete >= legendaire) */
    if (p->equipped[SLOT_CHEST].occupied &&
        (p->equipped[SLOT_CHEST].rarity >= R_RARE ||
         p->equipped[SLOT_CHEST].is_unique)) {
        uint32_t hl = chest_col;
        /* eclaircit de 30% */
        int r = ((hl >> 24) & 0xFF), gr = ((hl >> 16) & 0xFF), b = ((hl >> 8) & 0xFF);
        r = r + (255 - r) * 30 / 100;
        gr = gr + (255 - gr) * 30 / 100;
        b = b + (255 - b) * 30 / 100;
        uint32_t bright = (uint32_t)((r << 24) | (gr << 16) | (b << 8) | 0xFF);
        fill_rect(g->renderer, cx - 16, cy - 10, 6, 8, bright);
        fill_rect(g->renderer, cx + 10, cy - 10, 6, 8, bright);
    }
    /* ceinture */
    if (eq_belt->occupied) {
        uint32_t bc = eq_belt->is_unique ? 0xFF8030FF : rarity_color(eq_belt->rarity);
        fill_rect(g->renderer, cx - 15, cy + 13, 30, 4, bc);
        /* boucle doree au centre */
        fill_rect(g->renderer, cx - 2, cy + 12, 4, 6, 0xFFD040FF);
    }

    /* bras gauche + droit + gants */
    fill_rect(g->renderer, cx - 19, cy - 8, 5, 22, chest_col);
    fill_rect(g->renderer, cx + 14, cy - 8, 5, 22, chest_col);
    /* gants */
    if (p->equipped[SLOT_GLOVES].occupied) {
        fill_rect(g->renderer, cx - 20, cy + 12, 7, 6, gloves_col);
        fill_rect(g->renderer, cx + 13, cy + 12, 7, 6, gloves_col);
    }

    /* tete (peau) */
    fill_rect(g->renderer, cx - 8,  cy - 30, 16, 14, 0xE8C089FF);
    /* cheveux */
    fill_rect(g->renderer, cx - 8,  cy - 32, 16, 4,  0x402010FF);
    /* casque */
    if (eq_helm->occupied) {
        uint32_t hc = eq_helm->is_unique ? 0xFF8030FF : rarity_color(eq_helm->rarity);
        /* dome qui couvre tete + cheveux */
        fill_rect(g->renderer, cx - 10, cy - 34, 20, 12, hc);
        /* visiere sombre */
        fill_rect(g->renderer, cx - 8,  cy - 24, 16, 4, 0x100810FF);
        /* corne ou crete pour les legendaire / unique */
        if (eq_helm->rarity >= R_LEGENDARY || eq_helm->is_unique) {
            fill_rect(g->renderer, cx - 2, cy - 40, 4, 6, hc);
            fill_rect(g->renderer, cx - 1, cy - 42, 2, 2, 0xFFFFFFFF);
        }
    } else {
        /* yeux quand pas de casque */
        fill_rect(g->renderer, cx - 5, cy - 26, 3, 3, 0x000000FF);
        fill_rect(g->renderer, cx + 2, cy - 26, 3, 3, 0x000000FF);
    }

    /* jambes */
    fill_rect(g->renderer, cx - 10, cy + 16, 8,  14, legs_col);
    fill_rect(g->renderer, cx + 2,  cy + 16, 8,  14, legs_col);
    /* bottes */
    fill_rect(g->renderer, cx - 11, cy + 28, 10, 4, boots_col);
    fill_rect(g->renderer, cx + 1,  cy + 28, 10, 4, boots_col);

    /* arme tenue visible : petit cube sur le cote droit */
    Weapon *aw = &p->weapons[p->active_weapon];
    if (aw->owned && aw->kind != W_FISTS) {
        uint32_t wc = rarity_color(aw->rarity);
        /* tige verticale pour symboliser l'arme */
        fill_rect(g->renderer, cx + 19, cy - 14, 3, 28, wc);
        /* "garde" horizontale pour epee/hache */
        if (aw->kind == W_SWORD || aw->kind == W_AXE) {
            fill_rect(g->renderer, cx + 16, cy - 4, 9, 3, wc);
        }
        /* "cristal" en haut pour wand */
        if (aw->kind == W_WAND) {
            fill_rect(g->renderer, cx + 18, cy - 18, 5, 5, wc);
        }
    }

    /* aura : priorite a un element equipe, sinon legendaire / unique */
    int legcount = 0;
    Item *all_eq[6] = { &p->equipped[0], &p->equipped[1], &p->equipped[2],
                        &p->equipped[3], &p->equipped[4], &p->equipped[5] };
    Element elem_seen[6]; int n_el = 0;
    for (int i = 0; i < 6; i++) {
        if (!all_eq[i]->occupied) continue;
        if (all_eq[i]->rarity >= R_LEGENDARY || all_eq[i]->is_unique) legcount++;
        Element el = item_element(all_eq[i]);
        if (el > EL_NONE && el < EL_COUNT) {
            bool dup = false;
            for (int j = 0; j < n_el; j++) if (elem_seen[j] == el) { dup = true; break; }
            if (!dup) elem_seen[n_el++] = el;
        }
    }
    if (n_el > 0 || legcount > 0) {
        float pulse = 0.5f + 0.5f * sinf(g->time * 3.f);
        uint8_t a = (uint8_t)(60 + pulse * 40);
        uint32_t aura;
        if (n_el > 0) {
            /* anneau aux couleurs des elements actifs (cycle sur les 4
             * anneaux si plusieurs elements). */
            (void)aura;
            gfx_set_blend(g->renderer, true);
            for (int s = 0; s < 4; s++) {
                int sz = 50 + s * 4;
                uint32_t ac = element_color(elem_seen[s % n_el]);
                ac = (ac & 0xFFFFFF00u) | a;
                rect_outline(g->renderer, cx - sz/2, cy - sz/2, sz, sz + 12, ac);
            }
            gfx_set_blend(g->renderer, false);
        } else {
            aura = legcount >= 3 ? 0xFF8030 : 0xFFD040;
            aura = (aura << 8) | a;
            gfx_set_blend(g->renderer, true);
            for (int s = 0; s < 4; s++) {
                int sz = 50 + s * 4;
                rect_outline(g->renderer, cx - sz/2, cy - sz/2, sz, sz + 12, aura);
            }
            gfx_set_blend(g->renderer, false);
        }
    }
}

/* dessine un slot d'arme + ses talismans dessous (1 a 3 selon la qualite).
 * sx,sy = coin sup-gauche du slot arme (40x32). cursor_w = INV_CURSOR_WEAPON_BASE+slot.
 * cursor_t0 = INV_CURSOR_TALISMAN_BASE + slot*3 (premier talisman). */
static void draw_weapon_and_talismans(Game *g, int sx, int sy,
                                       int weapon_slot, int cursor_w, int cursor_t0)
{
    Weapon *w = &g->player.weapons[weapon_slot];
    /* slot arme : grand carre 36x36 */
    bool sel_w = (g->inv_cursor == cursor_w);
    uint32_t border = sel_w ? 0xFFFF40FF : 0x404048FF;
    fill_rect(g->renderer, sx, sy, 36, 36, 0x14101AFF);
    rect_outline(g->renderer, sx, sy, 36, 36, border);
    if (w->owned) {
        uint32_t rc = rarity_color(w->rarity);
        fill_rect(g->renderer, sx + 4, sy + 4, 28, 28, rc);
        rect_outline(g->renderer, sx + 4, sy + 4, 28, 28, 0x000000FF);
        const char *nm = weapon_name(w->kind);
        /* abreviation 1 lettre centree */
        char abbr[3] = { nm[0], 0, 0 };
        if (nm[1] && nm[1] != ' ') abbr[1] = nm[1];
        text_draw(g->renderer, sx + 18 - text_width(abbr)/2, sy + 15, abbr, 0x000000FF);
    } else {
        text_draw(g->renderer, sx + 18 - text_width("ARME")/2, sy + 15, "ARME", 0x606068FF);
    }

    /* libelle qualite sous le nom d'arme */
    if (w->owned) {
        text_draw(g->renderer, sx + 18 - text_width(rarity_name(w->rarity))/2, sy + 38,
                  rarity_name(w->rarity), rarity_color(w->rarity));
    }

    /* talismans : weapon_slot_count(w->rarity) cases actives, le reste grise */
    int active_n = w->owned ? weapon_slot_count(w->rarity) : 0;
    int tsx = sx - 2;  /* aligne sous l'arme : 3*14 = 42, arme = 36 -> -3 px */
    int tsy = sy + 50;
    for (int t = 0; t < MAX_ELEMENTS_PER_WEAPON; t++) {
        int x = tsx + t * 14;
        bool sel_t = (g->inv_cursor == cursor_t0 + t);
        bool active = (t < active_n);
        uint32_t bd = sel_t ? 0xFFFF40FF : (active ? 0x80809CFF : 0x303034FF);
        uint32_t bg = active ? 0x18141EFF : 0x0C0810FF;
        fill_rect(g->renderer, x, tsy, 12, 12, bg);
        rect_outline(g->renderer, x, tsy, 12, 12, bd);
        if (active && t < w->element_count && w->elements[t] != EL_NONE) {
            uint32_t ec = element_color(w->elements[t]);
            fill_rect(g->renderer, x + 2, tsy + 2, 8, 8, ec);
            rect_outline(g->renderer, x + 2, tsy + 2, 8, 8, 0x000000FF);
        } else if (!active) {
            /* X discret pour signaler "verrouille par qualite" */
            text_draw(g->renderer, x + 3, tsy + 3, "x", 0x404048FF);
        }
    }
}

void render_inventory(Game *g) {
    gfx_set_blend(g->renderer, true);
    fill_rect(g->renderer, 0, 0, INTERNAL_W, INTERNAL_H, 0x000000E0);
    gfx_set_blend(g->renderer, false);
    text_draw(g->renderer, INTERNAL_W/2 - text_width("INVENTAIRE")/2, 6,
              "INVENTAIRE", 0xFFE080FF);

    Player *p = &g->player;

    /* ---- STATS PANEL (gauche) ---- */
    int spx = 10, spy = 22;
    text_draw(g->renderer, spx, spy, "STATS", 0xFFE080FF);
    text_drawf(g->renderer, spx, spy + 12, 0xFFFFFFFF, "PV %d/%d", (int)p->hp, (int)p->maxhp);
    text_drawf(g->renderer, spx, spy + 22, 0xFFFFFFFF, "Armure %.0f",  p->armor);
    text_drawf(g->renderer, spx, spy + 32, 0xFFFFFFFF, "Vitesse %.0f", p->speed);
    text_drawf(g->renderer, spx, spy + 42, 0xFFFFFFFF, "Degats x%.2f", p->dmg_mul);
    text_drawf(g->renderer, spx, spy + 52, 0xFFFFFFFF, "Crit %.0f%%",  p->crit_chance * 100.f);
    text_drawf(g->renderer, spx, spy + 62, 0xFFFFFFFF, "Regen %.1f/s", p->regen_per_sec);
    text_drawf(g->renderer, spx, spy + 72, 0xFFFFFFFF, "Vol vie %.0f%%", p->lifesteal * 100.f);
    text_drawf(g->renderer, spx, spy + 82, 0xCCCCCCFF, "%s", hero_name(p->hero));

    /* ---- PAPER DOLL (centre) ---- */
    int cx = 240, cy = 110;
    /* fond box subtil pour le perso */
    fill_rect(g->renderer, cx - 70, cy - 80, 140, 200, 0x100C18FF);
    rect_outline(g->renderer, cx - 70, cy - 80, 140, 200, 0x303040FF);
    /* Paperdoll 3D : on rend le vrai joueur 3D dans un viewport limite
     * a la box du paperdoll, via draw_player_3d. Temporairement on
     * deplace le joueur a une position "showcase" (centre du monde,
     * vitesse zero pour figer le bobbing) puis on restore. */
    {
        Player *pp = &g->player;
        float sx = pp->x, sy = pp->y, svx = pp->vx, svy = pp->vy;
        float sinvuln = pp->invuln_t, shit = pp->hit_t;
        int   sanim = pp->anim_kind;
        /* place le joueur a (28*TILE, 28*TILE) (centre HUB-like) immobile.
         * draw_player_3d utilise player_world_pos qui retourne pos/TILE,
         * donc on ramene le joueur a (28*TILE, 28*TILE) -> world (28, 28). */
        pp->x = 28.f * TILE; pp->y = 28.f * TILE;
        pp->vx = pp->vy = 0.f;
        pp->invuln_t = 0.f; pp->hit_t = 0.f;
        pp->anim_kind = 0;
        /* viewport rect en coords FBO (== INTERNAL_W/H) sur la box paperdoll */
        int vp_x = cx - 70, vp_y = cy - 80, vp_w = 140, vp_h = 200;
        gfx_set_viewport_rect(g->renderer, vp_x, vp_y, vp_w, vp_h);
        gfx_clear_depth_rect (g->renderer, vp_x, vp_y, vp_w, vp_h);
        /* camera frontale 3/4 cadree sur le joueur (~3 units away) */
        v3 target = v3_make(28.5f, 0.6f, 28.5f);
        v3 eye    = v3_make(target.x + 2.2f, target.y + 1.8f, target.z + 2.8f);
        m4 view = m4_lookat(eye, target, v3_make(0, 1, 0));
        float aspect = (float)vp_w / (float)vp_h;
        m4 proj = m4_perspective(0.85f, aspect, 0.1f, 30.0f);
        gfx_set_camera(g->renderer, view, proj);
        /* pedestal sous le perso pour ne pas voir dans le vide */
        gfx_box_draw(g->renderer, v3_make(28.5f, 0.04f, 28.5f),
                     v3_make(1.20f, 0.08f, 1.20f),
                     0.18f, 0.14f, 0.24f);
        draw_player_3d(g);
        gfx_reset_viewport(g->renderer);
        /* restore */
        pp->x = sx; pp->y = sy; pp->vx = svx; pp->vy = svy;
        pp->invuln_t = sinvuln; pp->hit_t = shit; pp->anim_kind = sanim;
    }

    /* 6 slots equipement autour du personnage */
    for (int i = 0; i < EQUIP_SLOTS; i++) {
        int sx = cx + EQUIP_LAYOUT[i].dx;
        int sy = cy + EQUIP_LAYOUT[i].dy;
        bool sel = (g->inv_cursor == INV_CURSOR_EQUIP_BASE + i);
        render_item_slot(g, sx, sy, 26, &p->equipped[i], sel, false,
                         EQUIP_LAYOUT[i].short_name);
    }

    /* 2 armes en bas du paper-doll, avec talismans sous chacune */
    int wy = cy + 80;
    int wx0 = cx - 44, wx1 = cx + 8;
    draw_weapon_and_talismans(g, wx0, wy, 0,
                              INV_CURSOR_WEAPON_BASE + 0,
                              INV_CURSOR_TALISMAN_BASE + 0);
    draw_weapon_and_talismans(g, wx1, wy, 1,
                              INV_CURSOR_WEAPON_BASE + 1,
                              INV_CURSOR_TALISMAN_BASE + 3);

    /* ---- SAC (droite, 4 cols x N rows, N depend de la capacite) ---- */
    int inv_cap = INVENTORY_SLOTS + p->inv_capacity_bonus;
    if (inv_cap > INVENTORY_MAX_SLOTS) inv_cap = INVENTORY_MAX_SLOTS;
    int bag_x = 410, bag_y = 22, cell = 26;
    int bag_rows = (inv_cap + 3) / 4;
    if (bag_rows < 3) bag_rows = 3;
    char bag_hdr[24];
    snprintf(bag_hdr, sizeof(bag_hdr), "SAC (%d)", inv_cap);
    text_draw(g->renderer, bag_x, bag_y - 9, bag_hdr, 0xCCCCFFFF);
    int fa = -1, fb = -1, fc = -1;
    bool has_auto_fuse = inventory_find_fusion_group(g, &fa, &fb, &fc);
    for (int i = 0; i < inv_cap; i++) {
        int row = i / 4, col = i % 4;
        int sx = bag_x + col * cell;
        int sy = bag_y + row * cell;
        bool sel = (g->inv_cursor == i);
        bool mk = false;
        for (int m = 0; m < g->inv_marked_count; m++)
            if (g->inv_marked[m] == i) mk = true;
        if (g->inv_marked_count == 0 && has_auto_fuse &&
            (i == fa || i == fb || i == fc)) mk = true;
        render_item_slot(g, sx, sy, 24, &p->inventory[i], sel, mk, NULL);
    }

    /* ---- SAC TALISMAN dedie (5 col x 2 lignes), sous les armes ---- */
    text_draw(g->renderer, INV_BAG_X, INV_WEAPON_Y + 60, "TALISMANS", 0xC0E0FFFF);
    for (int i = 0; i < TALISMAN_BAG_SLOTS; i++) {
        int col = i % 5;
        int row = i / 5;
        int sx = INV_BAG_X + col * 20;
        int sy = INV_WEAPON_Y + 70 + row * 20;
        bool sel = (g->inv_cursor == INV_CURSOR_TALISBAG_BASE + i);
        render_item_slot(g, sx, sy, 18, &p->talisman_bag[i], sel, false, NULL);
    }

    /* ---- DETAILS PANEL (sous le sac) ---- */
    int dpx = bag_x, dpy = bag_y + bag_rows * cell + 12;
    int dpw = INTERNAL_W - dpx - 8, dph = INTERNAL_H - dpy - 26;
    fill_rect(g->renderer, dpx, dpy, dpw, dph, 0x14101AFF);
    rect_outline(g->renderer, dpx, dpy, dpw, dph, 0x404048FF);
    /* contenu : depend du cursor */
    if (g->inv_cursor < INV_CURSOR_WEAPON_BASE) {
        bool is_equip = (g->inv_cursor >= INV_CURSOR_EQUIP_BASE);
        Item *it = is_equip
                     ? &p->equipped[g->inv_cursor - INV_CURSOR_EQUIP_BASE]
                     : &p->inventory[g->inv_cursor];
        if (it->occupied) {
            /* dispatch d'affichage par kind */
            if (it->kind == ITEM_KIND_WEAPON) {
                uint32_t nc = rarity_color(it->rarity);
                text_drawf(g->renderer, dpx + 4, dpy + 4, nc, "%s", it->name);
                text_drawf(g->renderer, dpx + 4, dpy + 14, nc,
                           "Arme %s", rarity_name(it->rarity));
                text_draw(g->renderer, dpx + 4, dpy + 28,
                          "E ou clic : equipe (remplace l'arme active).",
                          0xCCCCCCFF);
                text_draw(g->renderer, dpx + 4, dpy + 38,
                          "L'arme actuelle revient en inventaire.",
                          0x808080FF);
                goto detail_done;
            }
            if (it->kind == ITEM_KIND_ELEMENT) {
                Element el = (Element)it->base_kind;
                uint32_t nc = element_color(el);
                text_drawf(g->renderer, dpx + 4, dpy + 4, nc, "%s",
                           element_name(el));
                text_draw(g->renderer, dpx + 4, dpy + 14, "Element / Talisman", 0xCCCCCCFF);
                text_draw(g->renderer, dpx + 4, dpy + 28,
                          "E ou clic : greffe sur l'arme active.",
                          0xCCCCCCFF);
                text_draw(g->renderer, dpx + 4, dpy + 38,
                          "Echoue si tous les slots talisman sont pris.",
                          0x808080FF);
                goto detail_done;
            }
            /* nom procedural / unique en haut, en couleur de rarete (orange
             * pour les uniques pour les distinguer des autres legendaires). */
            uint32_t name_col = it->is_unique ? 0xFF8030FF : rarity_color(it->rarity);
            text_drawf(g->renderer, dpx + 4, dpy + 4, name_col, "%s",
                       it->name[0] ? it->name : slot_name(it->slot));
            text_drawf(g->renderer, dpx + 4, dpy + 14, rarity_color(it->rarity),
                       "%s%s%s",
                       it->is_unique ? "UNIQUE " : "",
                       rarity_name(it->rarity),
                       is_equip ? " (equipe)" : "");
            /* archetype : nom retire (cote dev seulement). */
            int ay = dpy + 26;
            if (it->is_unique) {
                /* description fixe du unique au lieu de la stat de base. */
                text_drawf(g->renderer, dpx + 4, ay, 0x80FFC0FF, "%s",
                           unique_def_desc(it->unique_id));
                ay += 10;
            } else {
                const char *unit = "";
                switch (it->slot) {
                    case SLOT_HELM:   unit = "PV";    break;
                    case SLOT_CHEST:  unit = "ARM";   break;
                    case SLOT_LEGS:   unit = "VIT";   break;
                    case SLOT_BOOTS:  unit = "DASH";  break;
                    case SLOT_BELT:   unit = "REG";   break;
                    case SLOT_GLOVES: unit = "DMG";   break;
                    default: break;
                }
                if (it->slot == SLOT_GLOVES || it->slot == SLOT_BOOTS) {
                    text_drawf(g->renderer, dpx + 4, ay, 0x80FFC0FF,
                               "+%.0f%% %s", it->stat_value * 100.f, unit);
                } else {
                    text_drawf(g->renderer, dpx + 4, ay, 0x80FFC0FF,
                               "+%.1f %s", it->stat_value, unit);
                }
                ay += 10;
                for (int a = 0; a < it->affix_count; a++) {
                    char ab[40]; affix_label(&it->affixes[a], ab, sizeof(ab));
                    text_draw(g->renderer, dpx + 4, ay, ab, 0xC0E0FFFF);
                    ay += 9;
                }
            }
            /* valeur de revente */
            text_drawf(g->renderer, dpx + 4, ay + 2, 0xFFD080FF,
                       "Vente : %d coins", item_sell_value(it));
            /* ===== COMPARAISON =====
             * Si on hover un equipement (inventaire), affiche l'equipe
             * actuel pour le meme slot en dessous, pour comparer.
             * Pareil pour les armes : montre l'arme active. */
            int cmp_y = ay + 18;
            if (it->kind == ITEM_KIND_EQUIP && !is_equip) {
                Item *cur = &p->equipped[it->slot];
                fill_rect(g->renderer, dpx + 2, cmp_y - 2, dpw - 4, 1, 0x404048FF);
                text_drawf(g->renderer, dpx + 4, cmp_y + 1, 0x808080FF,
                           "Actuel (%s)", slot_name(it->slot));
                cmp_y += 11;
                if (cur->occupied) {
                    uint32_t cc = cur->is_unique ? 0xFF8030FF
                                                : rarity_color(cur->rarity);
                    text_drawf(g->renderer, dpx + 4, cmp_y, cc, "%s",
                               cur->name[0] ? cur->name : slot_name(cur->slot));
                    cmp_y += 10;
                    text_drawf(g->renderer, dpx + 4, cmp_y, rarity_color(cur->rarity),
                               "%s%s", cur->is_unique ? "UNIQUE " : "",
                               rarity_name(cur->rarity));
                    cmp_y += 10;
                    if (cur->is_unique) {
                        text_drawf(g->renderer, dpx + 4, cmp_y, 0x80FFC0FF,
                                   "%s", unique_def_desc(cur->unique_id));
                    } else {
                        const char *u2 = "";
                        switch (cur->slot) {
                            case SLOT_HELM:   u2 = "PV";    break;
                            case SLOT_CHEST:  u2 = "ARM";   break;
                            case SLOT_LEGS:   u2 = "VIT";   break;
                            case SLOT_BOOTS:  u2 = "DASH";  break;
                            case SLOT_BELT:   u2 = "REG";   break;
                            case SLOT_GLOVES: u2 = "DMG";   break;
                            default: break;
                        }
                        if (cur->slot == SLOT_GLOVES || cur->slot == SLOT_BOOTS) {
                            text_drawf(g->renderer, dpx + 4, cmp_y, 0x80FFC0FF,
                                       "+%.0f%% %s", cur->stat_value * 100.f, u2);
                        } else {
                            text_drawf(g->renderer, dpx + 4, cmp_y, 0x80FFC0FF,
                                       "+%.1f %s", cur->stat_value, u2);
                        }
                        cmp_y += 10;
                        for (int a = 0; a < cur->affix_count; a++) {
                            char ab[40]; affix_label(&cur->affixes[a], ab, sizeof(ab));
                            text_draw(g->renderer, dpx + 4, cmp_y, ab, 0xC0E0FFFF);
                            cmp_y += 9;
                        }
                    }
                } else {
                    text_draw(g->renderer, dpx + 4, cmp_y, "(rien d'equipe)", 0x606060FF);
                }
            } else if (it->kind == ITEM_KIND_WEAPON && !is_equip) {
                Weapon *aw = &p->weapons[p->active_weapon];
                fill_rect(g->renderer, dpx + 2, cmp_y - 2, dpw - 4, 1, 0x404048FF);
                text_drawf(g->renderer, dpx + 4, cmp_y + 1, 0x808080FF,
                           "Arme active");
                cmp_y += 11;
                if (aw->owned) {
                    text_drawf(g->renderer, dpx + 4, cmp_y, rarity_color(aw->rarity),
                               "%s (%s)", weapon_name(aw->kind),
                               rarity_name(aw->rarity));
                    cmp_y += 10;
                    text_drawf(g->renderer, dpx + 4, cmp_y, 0x80FFC0FF,
                               "Talismans : %d / %d",
                               aw->element_count, weapon_slot_count(aw->rarity));
                }
            }
            detail_done: ;
        } else if (is_equip) {
            text_drawf(g->renderer, dpx + 4, dpy + 4, 0x808080FF, "Slot vide : %s",
                       slot_name((EquipSlot)(g->inv_cursor - INV_CURSOR_EQUIP_BASE)));
        } else {
            text_draw(g->renderer, dpx + 4, dpy + 4, "(slot vide)", 0x808080FF);
        }
    } else if (g->inv_cursor < INV_CURSOR_TALISMAN_BASE) {
        Weapon *w = &p->weapons[g->inv_cursor - INV_CURSOR_WEAPON_BASE];
        if (w->owned) {
            text_drawf(g->renderer, dpx + 4, dpy + 4, rarity_color(w->rarity),
                       "%s", weapon_name(w->kind));
            text_drawf(g->renderer, dpx + 4, dpy + 14, 0xCCCCCCFF,
                       "Qualite : %s", rarity_name(w->rarity));
            text_drawf(g->renderer, dpx + 4, dpy + 24, 0x80FFC0FF,
                       "Talismans : %d / %d",
                       w->element_count, weapon_slot_count(w->rarity));
            char descbuf[64];
            weapon_describe(w, descbuf, sizeof(descbuf));
            text_draw(g->renderer, dpx + 4, dpy + 34, descbuf, 0xC0C0FFFF);
        } else {
            text_draw(g->renderer, dpx + 4, dpy + 4, "Slot arme vide", 0x808080FF);
        }
    } else {
        int wi = (g->inv_cursor - INV_CURSOR_TALISMAN_BASE) / 3;
        int ti = (g->inv_cursor - INV_CURSOR_TALISMAN_BASE) % 3;
        Weapon *w = &p->weapons[wi];
        int active_n = w->owned ? weapon_slot_count(w->rarity) : 0;
        text_drawf(g->renderer, dpx + 4, dpy + 4, 0xFFE080FF,
                   "Talisman %d (arme %d)", ti + 1, wi + 1);
        if (ti >= active_n) {
            text_draw(g->renderer, dpx + 4, dpy + 14, "Verrouille", 0xFF8080FF);
            text_draw(g->renderer, dpx + 4, dpy + 24, "ameliore la qualite", 0xCCCCCCFF);
            text_draw(g->renderer, dpx + 4, dpy + 32, "de l'arme pour debloquer", 0xCCCCCCFF);
        } else if (ti < w->element_count) {
            Element el = w->elements[ti];
            text_drawf(g->renderer, dpx + 4, dpy + 14, element_color(el),
                       "%s", element_name(el));
            text_draw(g->renderer, dpx + 4, dpy + 24, "CLIC : cycler", 0xCCCCCCFF);
            text_draw(g->renderer, dpx + 4, dpy + 32, "(retire si fin)", 0xCCCCCCFF);
        } else {
            text_draw(g->renderer, dpx + 4, dpy + 14, "(vide)", 0x808080FF);
            text_draw(g->renderer, dpx + 4, dpy + 24, "CLIC : ajouter element", 0xCCCCCCFF);
        }
    }

    /* ---- FUSION PREVIEW (bas-gauche) ---- */
    int fpx = 10, fpy = INTERNAL_H - 60;
    if (g->inv_marked_count > 0) {
        text_drawf(g->renderer, fpx, fpy, 0x80FF80FF,
                   "FUSION (%d/3 marques)", g->inv_marked_count);
        if (g->inv_marked_count == 3) {
            Item *base = &p->inventory[g->inv_marked[0]];
            if (base->occupied && base->rarity < R_LEGENDARY) {
                text_drawf(g->renderer, fpx, fpy + 10, 0x80FF80FF,
                           "F = %s %s +20%%",
                           rarity_name(base->rarity + 1), slot_name(base->slot));
            } else if (base->occupied) {
                text_draw(g->renderer, fpx, fpy + 10, "Deja max", 0xFFC080FF);
            }
        }
    } else if (has_auto_fuse) {
        Item *base = &p->inventory[fa];
        text_draw(g->renderer, fpx, fpy, "FUSION DETECTEE", 0x80FF80FF);
        text_drawf(g->renderer, fpx, fpy + 10, 0x80FF80FF,
                   "F = %s %s", rarity_name(base->rarity + 1), slot_name(base->slot));
    }

    /* ---- TRINKETS DE SHOP : panel sous le paperdoll ----
     * Liste les recettes achetees au shop, par ordre d'acquisition. Une
     * cellule = un trinket. Couleur = couleur de rarete. */
    {
        int tx = 12, ty = 245, tw = 390, th = 60;
        fill_rect(g->renderer, tx, ty, tw, th, 0x100C18FF);
        rect_outline(g->renderer, tx, ty, tw, th, 0x303040FF);
        char hdr[48];
        snprintf(hdr, sizeof(hdr), "TRINKETS (%d)", p->shop_purchased_count);
        text_draw(g->renderer, tx + 4, ty + 2, hdr, 0xCCCCFFFF);
        int cell_sz = 14;
        int cols = (tw - 8) / (cell_sz + 2);
        int rows = (th - 14) / (cell_sz + 2);
        int total_visible = cols * rows;
        int n_show = p->shop_purchased_count > total_visible
                      ? total_visible : p->shop_purchased_count;
        for (int i = 0; i < n_show; i++) {
            int rid = p->shop_purchased[i];
            uint32_t col = shop_recipe_color(rid);
            int row = i / cols;
            int col_ = i % cols;
            int sx = tx + 4 + col_ * (cell_sz + 2);
            int sy = ty + 14 + row * (cell_sz + 2);
            /* mouse hover : tooltip nom + desc */
            bool hov = mouse_in_rect(g, sx, sy, cell_sz, cell_sz);
            uint32_t border = hov ? 0xFFFF80FF : 0x000000FF;
            fill_rect(g->renderer, sx, sy, cell_sz, cell_sz, col);
            rect_outline(g->renderer, sx, sy, cell_sz, cell_sz, border);
            /* lettre initiale de la recette pour mini-identite */
            const char *nm = shop_recipe_name(rid);
            if (nm && nm[0]) {
                char ab[2] = { nm[0], 0 };
                text_draw(g->renderer, sx + cell_sz/2 - 2, sy + cell_sz/2 - 3,
                          ab, 0x000000FF);
            }
            if (hov) {
                /* tooltip flottant : nom + desc */
                const char *desc = shop_recipe_desc(rid);
                int tw_n = text_width(nm);
                int tw_d = desc ? text_width(desc) : 0;
                int box_w = (tw_n > tw_d ? tw_n : tw_d) + 12;
                int box_h = (desc && desc[0]) ? 24 : 14;
                int bx = sx + cell_sz + 4;
                int by = sy;
                if (bx + box_w > INTERNAL_W - 4) bx = sx - box_w - 4;
                if (by + box_h > INTERNAL_H - 12) by = INTERNAL_H - 12 - box_h;
                gfx_set_blend(g->renderer, true);
                fill_rect(g->renderer, bx, by, box_w, box_h, 0x000000E0);
                gfx_set_blend(g->renderer, false);
                rect_outline(g->renderer, bx, by, box_w, box_h, col);
                text_draw(g->renderer, bx + 4, by + 3, nm, col);
                if (desc && desc[0])
                    text_draw(g->renderer, bx + 4, by + 13, desc, 0xCCCCCCFF);
            }
        }
        if (p->shop_purchased_count > total_visible) {
            char overflow[16];
            snprintf(overflow, sizeof(overflow), "+%d",
                     p->shop_purchased_count - total_visible);
            text_draw(g->renderer, tx + tw - 24, ty + 2, overflow, 0xFFD040FF);
        }
    }

    /* ---- message + footer ---- */
    if (g->inv_msg_t > 0.f) {
        text_draw(g->renderer, INTERNAL_W/2 - text_width(g->inv_msg)/2,
                  INTERNAL_H - 28, g->inv_msg, 0xFFFF40FF);
    }
    const char *foot1 = "E EQUIPER  F FUSION  V VENDRE  SUPPR DETRUIRE  M MARQUER";
    text_draw(g->renderer, INTERNAL_W/2 - text_width(foot1)/2,
              INTERNAL_H - 18, foot1, 0xCCCCCCFF);
    text_draw(g->renderer, INTERNAL_W/2 - text_width("ECHAP POUR FERMER")/2,
              INTERNAL_H - 8, "ECHAP POUR FERMER", 0xFFFF80FF);

    /* ---- TOOLTIPS RETIRES ----
     * La boite descriptive permanente (cote droit, focused item) suffit.
     * Pas besoin de doubler avec un tooltip flottant qui suit le curseur.
     * On garde le bloc pour reference si besoin de re-activer.
     */
    if (0) for (int idx = 0; idx < INV_CURSOR_MAX; idx++) {
        int x, y, w, h;
        if (!inv_layout_rect(idx, &x, &y, &w, &h)) continue;
        if (!mouse_in_rect(g, x, y, w, h)) continue;
        /* on a besoin du Item correspondant */
        Item *it = NULL;
        const char *title = NULL;
        const char *line2 = NULL;
        char buf2[64];
        if (idx < INV_CURSOR_EQUIP_BASE) {
            it = &g->player.inventory[idx];
            if (!it->occupied) continue;
            title = it->name[0] ? it->name : slot_name(it->slot);
        } else if (idx < INV_CURSOR_WEAPON_BASE) {
            it = &g->player.equipped[idx - INV_CURSOR_EQUIP_BASE];
            if (!it->occupied) continue;
            title = it->name[0] ? it->name : slot_name(it->slot);
        } else if (idx < INV_CURSOR_TALISMAN_BASE) {
            Weapon *w2 = &g->player.weapons[idx - INV_CURSOR_WEAPON_BASE];
            if (!w2->owned) continue;
            title = weapon_name(w2->kind);
            snprintf(buf2, sizeof(buf2), "%s  %d/%d talismans",
                     rarity_name(w2->rarity),
                     w2->element_count, weapon_slot_count(w2->rarity));
            line2 = buf2;
        } else {
            int rel = idx - INV_CURSOR_TALISMAN_BASE;
            int wi = rel / 3, ti = rel % 3;
            Weapon *w2 = &g->player.weapons[wi];
            if (!w2->owned) continue;
            int active_n = weapon_slot_count(w2->rarity);
            if (ti >= active_n) {
                title = "Slot verrouille";
                line2 = "Ameliore la qualite de l arme";
            } else if (ti < w2->element_count) {
                title = element_name(w2->elements[ti]);
                line2 = "CLIC : cycler / retirer";
            } else {
                title = "Slot vide";
                line2 = "CLIC : ajouter un element";
            }
        }
        if (!title) continue;
        /* layout : 132 wide, hauteur variable selon affixes */
        int tw_title = text_width(title);
        int n_lines = 1 + (line2 ? 1 : 0);
        int n_affixes = (it && it->occupied) ? it->affix_count : 0;
        bool is_unique = (it && it->occupied && it->is_unique);
        if (it && it->occupied) {
            n_lines += 1;                          /* rarete + slot */
            n_lines += n_affixes;
            if (is_unique) n_lines += 1;
            n_lines += 1;                          /* vente */
        }
        int box_w = tw_title + 12;
        if (box_w < 132) box_w = 132;
        int box_h = 8 + n_lines * 9;
        int tx = g->mouse_x + 10;
        int ty = g->mouse_y + 10;
        if (tx + box_w > INTERNAL_W - 4) tx = g->mouse_x - box_w - 10;
        if (ty + box_h > INTERNAL_H - 24) ty = INTERNAL_H - 24 - box_h;
        if (tx < 4) tx = 4;
        if (ty < 4) ty = 4;
        gfx_set_blend(g->renderer, true);
        fill_rect(g->renderer, tx, ty, box_w, box_h, 0x000000E0);
        gfx_set_blend(g->renderer, false);
        rect_outline(g->renderer, tx, ty, box_w, box_h, 0x806040FF);
        /* contenu */
        int ly = ty + 4;
        uint32_t title_col = 0xFFFFFFFF;
        if (it && it->occupied) {
            title_col = it->is_unique ? 0xFF8030FF : rarity_color(it->rarity);
        }
        text_draw(g->renderer, tx + 6, ly, title, title_col);
        ly += 9;
        if (it && it->occupied) {
            text_drawf(g->renderer, tx + 6, ly, rarity_color(it->rarity),
                       "%s%s",
                       is_unique ? "UNIQUE " : "",
                       rarity_name(it->rarity));
            ly += 9;
            if (!is_unique && it->rarity > R_COMMON) {
                text_drawf(g->renderer, tx + 6, ly, 0x808080FF,
                           "Archetype : %s", archetype_name(it->base_kind));
                ly += 9;
            }
            if (is_unique) {
                text_drawf(g->renderer, tx + 6, ly, 0x80FFC0FF,
                           "%s", unique_def_desc(it->unique_id));
                ly += 9;
            }
            for (int a = 0; a < it->affix_count; a++) {
                char ab[40]; affix_label(&it->affixes[a], ab, sizeof(ab));
                text_draw(g->renderer, tx + 6, ly, ab, 0xC0E0FFFF);
                ly += 9;
            }
            text_drawf(g->renderer, tx + 6, ly, 0xFFD080FF,
                       "Vente : %d coins", item_sell_value(it));
            ly += 9;
        }
        if (line2) {
            text_draw(g->renderer, tx + 6, ly, line2, 0xCCCCCCFF);
            ly += 9;
        }
        break;     /* un seul tooltip a la fois */
    }
}
