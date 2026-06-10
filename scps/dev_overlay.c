/*
 * dev_overlay.c — l'overlay de dev sur Nuklear (voir dev_overlay.h).
 * Présent UNIQUEMENT sous -DSCPS_DEV : sans le define, ce fichier est vide et
 * aucun symbole Nuklear n'entre dans le binaire.
 */
#ifdef SCPS_DEV

/* Config Nuklear + son backend SDL_Renderer — l'implémentation vit ICI seule. */
#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT
#define NK_IMPLEMENTATION
#define NK_SDL_RENDERER_IMPLEMENTATION
#include "nuklear.h"
#include "nuklear_sdl_renderer.h"

#include "dev_overlay.h"
#include <stdio.h>

static struct nk_context *g_nk = NULL;
static float g_scale = 1.f;

void dev_overlay_init(SDL_Window *win, SDL_Renderer *ren){
    if (!win || !ren) return;
    g_nk = nk_sdl_init(win, ren);
    if (!g_nk) return;
    struct nk_font_atlas *atlas;
    nk_sdl_font_stash_begin(&atlas);
    /* police par défaut bakée (NK_INCLUDE_DEFAULT_FONT) — zéro asset disque */
    nk_sdl_font_stash_end();
}

void dev_overlay_shutdown(void){
    if (g_nk){ nk_sdl_shutdown(); g_nk=NULL; }
}

void dev_overlay_input_begin(void){ if (g_nk) nk_input_begin(g_nk); }
void dev_overlay_input_end(void){ if (g_nk) nk_input_end(g_nk); }

int dev_overlay_handle_event(SDL_Event *e){
    if (!g_nk) return 0;
    return nk_sdl_handle_event(e);
}

/* Une ligne « label : valeur » brute. */
static void row_f(struct nk_context *nk, const char *k, double v){
    char b[64]; snprintf(b,sizeof b,"%s = %.4f", k, v);
    nk_layout_row_dynamic(nk, 16, 1);
    nk_label(nk, b, NK_TEXT_LEFT);
}
static void row_i(struct nk_context *nk, const char *k, long v){
    char b[64]; snprintf(b,sizeof b,"%s = %ld", k, v);
    nk_layout_row_dynamic(nk, 16, 1);
    nk_label(nk, b, NK_TEXT_LEFT);
}

void dev_overlay_draw(const World *w, const WorldEconomy *econ,
                      const TechState *ts, const DiploState *dp,
                      int sel_country, int sel_prov){
    if (!g_nk || !w || !econ) return;
    struct nk_context *nk = g_nk;

    /* Inputs déjà passés par le viewer via dev_overlay_handle_event ; on ouvre/
     * ferme la fenêtre d'inspection. */
    if (nk_begin(nk, "DEV — coordonnées brutes (F3)", nk_rect(20, 60, 320, 520),
                 NK_WINDOW_BORDER|NK_WINDOW_MOVABLE|NK_WINDOW_SCALABLE|
                 NK_WINDOW_MINIMIZABLE|NK_WINDOW_TITLE)){

        /* ── LE PAYS sélectionné : tech (charge faustienne, fracture, H…) ── */
        nk_layout_row_dynamic(nk, 18, 1);
        if (sel_country>=0 && sel_country<w->n_countries){
            char hdr[64]; snprintf(hdr,sizeof hdr,"PAYS %d — %s", sel_country,
                                   w->country[sel_country].name);
            nk_label(nk, hdr, NK_TEXT_LEFT);
            if (ts){ const TechState *t=&ts[sel_country];
                row_f(nk,"charge faustienne", t->charge);
                row_f(nk,"fracture", t->fracture);
                row_f(nk,"coercition H", t->H);
                row_f(nk,"puissance", t->puissance);
                row_i(nk,"techs débloquées", t->n_unlocked);
            }
            if (dp && sel_country<SCPS_MAX_COUNTRY){
                row_f(nk,"momentum (fulgurance)", dp->momentum[sel_country]);
                float ranc=0.f, pir=0.f;
                for (int b=0;b<SCPS_MAX_COUNTRY;b++){ ranc+=dp->rancor[sel_country][b]; pir+=dp->pirate_rancor[sel_country][b]; }
                row_f(nk,"Σ rancune", ranc);
                row_f(nk,"Σ rancune de course", pir);
            }
        } else {
            nk_label(nk, "PAYS : (aucun sélectionné)", NK_TEXT_LEFT);
        }

        /* ── LA PROVINCE/RÉGION sous sélection : l'éco brute + densités bâties ── */
        nk_layout_row_dynamic(nk, 6, 1); nk_label(nk,"",NK_TEXT_LEFT);
        int reg = (sel_prov>=0 && sel_prov<w->n_provinces) ? w->province[sel_prov].region : -1;
        nk_layout_row_dynamic(nk, 18, 1);
        if (reg>=0 && reg<econ->n_regions){
            char hdr[64]; snprintf(hdr,sizeof hdr,"RÉGION %d (prov %d)", reg, sel_prov);
            nk_label(nk, hdr, NK_TEXT_LEFT);
            const RegionEconomy *re=&econ->region[reg];
            row_f(nk,"prospérité", re->prosperity);
            row_f(nk,"satisfaction", re->satisfaction);
            row_f(nk,"food_sat", re->food_sat);
            row_f(nk,"society_sat", re->society_sat);
            row_f(nk,"surtaxe", re->over_tax);
            row_f(nk,"trésor", re->treasury);
            row_f(nk,"K bâti", re->build.K_inst);
            row_f(nk,"H bâti", re->build.H_coerc);
            row_f(nk,"P ouvert", re->build.P_open);
            row_f(nk,"foi bâtie", re->build.faith);
            row_f(nk,"savoir bâti", re->build.savoir);
            row_f(nk,"port", re->build.port);
            row_f(nk,"balafre (j)", re->balafre_days);
            row_i(nk,"propriétaire", re->owner);
        } else {
            nk_label(nk, "RÉGION : (survol une province)", NK_TEXT_LEFT);
        }

        /* ── Quelques CONSTANTES de la surface d'équilibrage (repères). ── */
        nk_layout_row_dynamic(nk, 6, 1); nk_label(nk,"",NK_TEXT_LEFT);
        nk_layout_row_dynamic(nk, 16, 1);
        nk_label(nk, "— surface d'équilibrage —", NK_TEXT_LEFT);
        row_i(nk,"SCPS_MAX_COUNTRY", SCPS_MAX_COUNTRY);
        row_i(nk,"SCPS_MAX_REG", SCPS_MAX_REG);
        row_i(nk,"NAVY_CREW_WAR", 100);
        row_i(nk,"NAVY_CREW_LIGHT", 50);
    }
    nk_end(nk);
    nk_sdl_render(NK_ANTI_ALIASING_ON);
    (void)g_scale;
}

#endif /* SCPS_DEV */
