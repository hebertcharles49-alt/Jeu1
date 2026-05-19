/*
 * options.c - menu Options : touches / son / DLSS, plus persistance
 */
#include "game.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define SETTINGS_PATH    "crucible_settings.dat"
#define SETTINGS_MAGIC   0x53455453u  /* 'SETS' */
#define SETTINGS_VERSION 3

void settings_defaults(Settings *s) {
    memset(s, 0, sizeof(*s));
    s->version = SETTINGS_VERSION;
    s->keys[BIND_DASH]        = SDL_SCANCODE_SPACE;
    s->keys[BIND_INVENTORY]   = SDL_SCANCODE_I;
    s->keys[BIND_WEAPON_SWAP] = SDL_SCANCODE_TAB;
    s->keys[BIND_WEAPON_1]    = SDL_SCANCODE_1;
    s->keys[BIND_WEAPON_2]    = SDL_SCANCODE_2;
    s->keys[BIND_INTERACT]    = SDL_SCANCODE_E;
    s->sfx_volume = 4;     /* 0..4 */
    s->sfx_mute   = 0;
    s->dlss_on    = 0;     /* off par defaut : pixel art net */
    s->debug_room = 0;     /* off par defaut : pas de salle bac-a-sable */
    s->mob_healthbars = 1; /* on par defaut : barres flottantes visibles */
}

void settings_load(Settings *s) {
    settings_defaults(s);
    FILE *f = fopen(SETTINGS_PATH, "rb");
    if (!f) return;
    uint32_t magic = 0;
    int      ver   = 0;
    if (fread(&magic, sizeof(magic), 1, f) != 1 ||
        fread(&ver,   sizeof(ver),   1, f) != 1 ||
        magic != SETTINGS_MAGIC || ver != SETTINGS_VERSION) {
        fclose(f);
        return;
    }
    Settings tmp;
    if (fread(&tmp, sizeof(tmp), 1, f) == 1) *s = tmp;
    fclose(f);
}

void settings_write(const Settings *s) {
    FILE *f = fopen(SETTINGS_PATH, "wb");
    if (!f) return;
    uint32_t magic = SETTINGS_MAGIC;
    int      ver   = SETTINGS_VERSION;
    fwrite(&magic, sizeof(magic), 1, f);
    fwrite(&ver,   sizeof(ver),   1, f);
    fwrite(s, sizeof(*s), 1, f);
    fclose(f);
}

const char *bind_action_name(BindAction a) {
    switch (a) {
        case BIND_DASH:        return "Dash";
        case BIND_INVENTORY:   return "Inventaire";
        case BIND_WEAPON_SWAP: return "Changer arme";
        case BIND_WEAPON_1:    return "Arme 1";
        case BIND_WEAPON_2:    return "Arme 2";
        case BIND_INTERACT:    return "Interagir";
        default:               return "?";
    }
}

const char *scancode_label(SDL_Scancode sc) {
    const char *n = SDL_GetScancodeName(sc);
    if (!n || !*n) return "—";
    return n;
}

void apply_render_filter(Game *g) {
    /* DLSS toggle : la config est lue par gfx_frame_end pour piloter le
       filtrage du blit FBO -> backbuffer. Rien a creer ici (FBO unique). */
    (void)g;
}

/* ---------- update ---------- */
/* layout :
 *   section 0 (Controles) : 0..BIND_COUNT-1 = touches
 *   section 1 (Audio) : 0 = mute, 1 = volume
 *   section 2 (Video) : 0 = DLSS
 */
static int section_row_count(int section) {
    switch (section) {
        case 0: return BIND_COUNT;
        case 1: return 2;
        case 2: return 3;       /* DLSS + Debug room + barres de vies */
        default: return 0;
    }
}

void update_options(Game *g) {
    Settings *s = &g->settings;

    /* mouse: tabs en haut + lignes */
    {
        int tabw = 100, tabh = 14, gap = 4;
        int tx = (INTERNAL_W - tabw * 3 - gap * 2) / 2;
        for (int i = 0; i < 3; i++) {
            int x = tx + i * (tabw + gap);
            if (mouse_in_rect(g, x, 22, tabw, tabh)) {
                if (mouse_clicked(g)) {
                    g->opt_section = i;
                    g->opt_cursor = 0;
                }
            }
        }
        int rc = section_row_count(g->opt_section);
        for (int i = 0; i < rc; i++) {
            int y = 50 + (g->opt_section == 0 ? 12 : 0) + i * 11;
            if (mouse_in_rect(g, 22, y - 1, INTERNAL_W - 60, 11)) {
                g->opt_cursor = i;
            }
        }
    }

    /* attente d'une touche pour rebind */
    if (g->opt_waiting_rebind) {
        if (g->opt_last_keydown != SDL_SCANCODE_UNKNOWN) {
            SDL_Scancode k = g->opt_last_keydown;
            g->opt_last_keydown = SDL_SCANCODE_UNKNOWN;
            if (k == SDL_SCANCODE_ESCAPE) {
                g->opt_waiting_rebind = false;
            } else if (g->opt_section == 0 && g->opt_cursor >= 0 && g->opt_cursor < BIND_COUNT) {
                /* refuser scancodes reserves WASD/fleches */
                if (k == SDL_SCANCODE_W || k == SDL_SCANCODE_A ||
                    k == SDL_SCANCODE_S || k == SDL_SCANCODE_D ||
                    k == SDL_SCANCODE_UP || k == SDL_SCANCODE_DOWN ||
                    k == SDL_SCANCODE_LEFT || k == SDL_SCANCODE_RIGHT) {
                    snprintf(g->opt_msg, sizeof(g->opt_msg),
                             "WASD / fleches reserves au deplacement");
                    g->opt_msg_t = 2.f;
                } else {
                    s->keys[g->opt_cursor] = k;
                    settings_write(s);
                }
                g->opt_waiting_rebind = false;
            } else {
                g->opt_waiting_rebind = false;
            }
        }
        return;
    }

    /* navigation entre sections : Q/E ou page-up/down */
    if (g->keys[SDL_SCANCODE_Q] && !g->keys_prev[SDL_SCANCODE_Q]) {
        g->opt_section = (g->opt_section + 2) % 3;
        g->opt_cursor = 0;
    }
    if (g->keys[SDL_SCANCODE_TAB] && !g->keys_prev[SDL_SCANCODE_TAB]) {
        g->opt_section = (g->opt_section + 1) % 3;
        g->opt_cursor = 0;
    }
    int rc = section_row_count(g->opt_section);
    if (rc > 0) {
        if ((g->keys[SDL_SCANCODE_DOWN] && !g->keys_prev[SDL_SCANCODE_DOWN]) ||
            (g->keys[SDL_SCANCODE_S]    && !g->keys_prev[SDL_SCANCODE_S]))
            g->opt_cursor = (g->opt_cursor + 1) % rc;
        if ((g->keys[SDL_SCANCODE_UP] && !g->keys_prev[SDL_SCANCODE_UP]) ||
            (g->keys[SDL_SCANCODE_W]  && !g->keys_prev[SDL_SCANCODE_W]))
            g->opt_cursor = (g->opt_cursor + rc - 1) % rc;
    }

    bool press_left  = (g->keys[SDL_SCANCODE_LEFT]  && !g->keys_prev[SDL_SCANCODE_LEFT])  ||
                       (g->keys[SDL_SCANCODE_A]     && !g->keys_prev[SDL_SCANCODE_A]);
    bool press_right = (g->keys[SDL_SCANCODE_RIGHT] && !g->keys_prev[SDL_SCANCODE_RIGHT]) ||
                       (g->keys[SDL_SCANCODE_D]     && !g->keys_prev[SDL_SCANCODE_D]);
    bool press_enter = (g->keys[SDL_SCANCODE_RETURN] && !g->keys_prev[SDL_SCANCODE_RETURN]) ||
                       (g->keys[SDL_SCANCODE_SPACE]  && !g->keys_prev[SDL_SCANCODE_SPACE]);
    /* click on focused row = press_enter equivalent, dans la zone de la ligne */
    {
        int y = 50 + (g->opt_section == 0 ? 12 : 0) + g->opt_cursor * 11;
        if (mouse_in_rect(g, 22, y - 1, INTERNAL_W - 60, 11) && mouse_clicked(g)) {
            press_enter = true;
        }
    }

    if (g->opt_section == 0) {
        if (press_enter) {
            g->opt_waiting_rebind = true;
            g->opt_last_keydown = SDL_SCANCODE_UNKNOWN;
            snprintf(g->opt_msg, sizeof(g->opt_msg),
                     "Appuie sur une touche... (ECHAP pour annuler)");
            g->opt_msg_t = 5.f;
        }
        if (press_left || press_right) {
            /* reset to defaults for this binding */
            Settings def; settings_defaults(&def);
            s->keys[g->opt_cursor] = def.keys[g->opt_cursor];
            settings_write(s);
            snprintf(g->opt_msg, sizeof(g->opt_msg), "Defaut restaure");
            g->opt_msg_t = 1.5f;
        }
    } else if (g->opt_section == 1) {
        if (g->opt_cursor == 0) {
            if (press_enter || press_left || press_right) {
                s->sfx_mute = !s->sfx_mute;
                settings_write(s);
            }
        } else {
            if (press_left)  { if (s->sfx_volume > 0) s->sfx_volume--; settings_write(s); }
            if (press_right) { if (s->sfx_volume < 4) s->sfx_volume++; settings_write(s); }
            if (press_enter) sfx_play(g, SFX_PICKUP);
        }
    } else if (g->opt_section == 2) {
        if (g->opt_cursor == 0) {
            if (press_enter || press_left || press_right) {
                s->dlss_on = !s->dlss_on;
                apply_render_filter(g);
                settings_write(s);
                snprintf(g->opt_msg, sizeof(g->opt_msg),
                         s->dlss_on ? "DLSS Generatif : ON  (lisse)" :
                                      "DLSS Generatif : OFF  (pixel art net)");
                g->opt_msg_t = 2.f;
            }
        } else if (g->opt_cursor == 1) {
            if (press_enter || press_left || press_right) {
                s->debug_room = !s->debug_room;
                settings_write(s);
                snprintf(g->opt_msg, sizeof(g->opt_msg),
                         s->debug_room
                            ? "Debug : salle bac-a-sable a la prochaine run"
                            : "Debug : OFF");
                g->opt_msg_t = 2.5f;
            }
        } else if (g->opt_cursor == 2) {
            if (press_enter || press_left || press_right) {
                s->mob_healthbars = !s->mob_healthbars;
                settings_write(s);
                snprintf(g->opt_msg, sizeof(g->opt_msg),
                         s->mob_healthbars
                            ? "Barres de vie : visibles"
                            : "Barres de vie : caches");
                g->opt_msg_t = 2.f;
            }
        }
    }

    if (g->opt_msg_t > 0.f) g->opt_msg_t -= g->dt;
}
