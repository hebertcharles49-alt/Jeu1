/*
 * viewer.c — visualiseur SDL2 du moteur SCPS
 *
 * Contrôles :
 *   Clic gauche      — sélectionne la province sous la souris
 *   Drag bouton mil. — pan de la caméra
 *   Molette          — zoom centré sur le curseur
 *   TAB / 1-5        — modes de vue
 *   R                — nouvelle graine aléatoire
 *   ESC / Q          — quitte
 *
 * Architecture :
 *   viewer.c  = shell applicatif fin
 *   scps_world  = génération  (indépendant du rendu)
 *   scps_render = rendu       (indépendant de SDL)
 *   → scps_diplo, scps_economy... viendront se brancher sur World
 */
#include <SDL.h>
#include <SDL_ttf.h>
#include "scps_world.h"
#include "scps_render.h"
#include "scps_econ.h"
#include "scps_trade.h"
#include "scps_tech.h"
#include "scps_legitimacy.h"
#include "scps_prosperity.h"
#include "scps_readout.h"
#include "scps_crypt.h"   /* sauvegardes chiffrées (ChaCha20 + empreinte du clair) */   /* la membrane : viewer ne voit QUE des bandes + mots.
                             * (n'inclut PAS scps_core.h — cloison vérifiée par grep) */
#include "scps_statecraft.h"/* Influence/Opinion/Diplomates : API en ENTIERS de jeu */
#include "scps_agency.h"    /* actions du joueur (file de construction, en JOURS) */
#include "scps_routes.h"
#include "scps_diplo.h"
#include "scps_events.h"    /* chocs, évènements culturels, ÂGES */
#include "scps_modifier.h"  /* pile de dérive démographique */
#include "scps_demography.h"/* GROUPES par province (composition) + H/intégration */
#include "scps_labor.h"     /* topbar : Or / Nourriture / Matériaux */
#include "scps_ai.h"        /* les voisins VIVENT : lecteurs de coordonnées, mêmes leviers */
#include "scps_revolt.h"    /* la révolte INCARNÉE : sécessions/coups dans le jeu vivant */
#include "scps_intertrade.h"/* commerce inter-pays : grandes routes marchandes + embargo */
#include "scps_warhost.h"   /* les armées VIVENT : mobilisation par pays */
#include "scps_campaign.h"  /* … et MARCHENT : campagne sur la carte (marche/siège/bataille) */
#include "scps_missions.h"  /* missions décennales : rythme + injection de ressources */
#include "scps_navy.h"     /* la flotte (mer §5) : coques, chantier, entretien, outre-mer */
#include "scps_lang.h"     /* la table de chaînes : tout mot face-joueur vient des tables */
#include "stb_image_write.h"  /* F12 : capture d'écran PNG (vendoré) */
#include "scps_audio.h"       /* la prise audio (miniaudio) — preuve de vie sur alerte */
#ifdef SCPS_DEV
#include "dev_overlay.h"      /* §6 : l'inspecteur de coordonnées brutes (F3) — JAMAIS en release */
static bool g_dev_overlay = false;
#endif
#include "scps_factions.h"  /* §4 : leviers de factions (reset/decay par sim) */
#include <stdlib.h>
/* mkdir portable (la sauvegarde crée saves/ sans passer par system(), qui
 * échouait sous Windows : cmd.exe ne connaît pas `mkdir -p`). */
#ifdef _WIN32
# include <direct.h>
# define scps_mkdir(p) _mkdir(p)
#else
# include <sys/stat.h>
# define scps_mkdir(p) mkdir((p), 0755)
#endif
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <math.h>

/* ---- Configuration fenêtre ------------------------------------------- */
#define WIN_W 1200
#define WIN_H 1080

/* ---- Temps de jeu : du snapshot au JEU VIVANT ------------------------ */
typedef enum { SPEED_PAUSE=0, SPEED_1, SPEED_2, SPEED_3, SPEED_5, SPEED_COUNT } GameSpeed;
static const double DAYS_PER_SEC[SPEED_COUNT] = { 0.0, 3.0, 8.0, 14.0, 24.0 };
static const char  *SPEED_LABEL[SPEED_COUNT]  = { "❙❙", "▸ ×1", "▸▸ ×2", "▸▸ ×3", "▸▸▸ ×5" };
static SDL_Rect g_speed_zone;   /* le cran est CLIQUABLE (clic = cran suivant, repasse à ×1) */
static GameSpeed g_last_speed = SPEED_1;   /* Espace repart sur le DERNIER cran choisi */
static SpeciesArchetype g_player_race = RACE_HUMAIN;   /* le choix de l'écran de création (shell) */
#define GAME_YEARS 250

/* ---- État caméra ----------------------------------------------------- */
typedef struct {
    float ox, oy;    /* offset en cellules */
    float scale;     /* pixels par cellule */
} Cam;

static void cam_zoom(Cam *c, float factor, float screen_x, float screen_y) {
    /* Zoom centré sur le point écran (screen_x, screen_y) */
    float wx = screen_x / c->scale + c->ox;
    float wy = screen_y / c->scale + c->oy;
    c->scale *= factor;
    if (c->scale < 0.20f) c->scale = 0.20f;
    if (c->scale > 16.0f) c->scale = 16.0f;
    c->ox = wx - screen_x / c->scale;
    c->oy = wy - screen_y / c->scale;
}

static void cam_pan(Cam *c, float dpx, float dpy) {
    c->ox -= dpx / c->scale;
    c->oy -= dpy / c->scale;
}

static void cam_fit(Cam *c, int win_w, int win_h) {
    /* Ajuste pour montrer toute la carte dans la fenêtre */
    float sx = (float)win_w / SCPS_W;
    float sy = (float)win_h / SCPS_H;
    c->scale = (sx < sy) ? sx : sy;
    c->ox = (SCPS_W - win_w / c->scale) * 0.5f;
    c->oy = (SCPS_H - win_h / c->scale) * 0.5f;
}

/* ---- Pixel buffer ---------------------------------------------------- */
typedef struct {
    SDL_Texture *tex;
    uint32_t    *pixels;
    int          w, h;
} PixBuf;

static PixBuf pixbuf_create(SDL_Renderer *ren, int w, int h) {
    PixBuf pb;
    pb.w = w; pb.h = h;
    pb.tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888,
                               SDL_TEXTUREACCESS_STREAMING, w, h);
    pb.pixels = (uint32_t*)malloc((size_t)(w*h)*4);
    return pb;
}
static void pixbuf_destroy(PixBuf *pb) {
    SDL_DestroyTexture(pb->tex);
    free(pb->pixels);
    pb->tex = NULL; pb->pixels = NULL;
}
static void pixbuf_upload(PixBuf *pb) {
    SDL_UpdateTexture(pb->tex, NULL, pb->pixels, pb->w * 4);
}

/* ---- Info province (console) ----------------------------------------- */
static void print_province_info(const World *w, int prov_id) {
    if (prov_id < 0 || prov_id >= w->n_provinces) return;
    const Province *p = &w->province[prov_id];
    printf("\n┌─ Province #%d ─────────────────────────────────\n", prov_id);
    printf("│  Biome dominant  : %s\n", biome_name(p->biome_dominant));
    printf("│  Surface         : %d cellules%s\n", p->area, p->coastal?" (côtière)":"");
    printf("│  Altitude moy.   : %.2f\n", p->height_avg);
    printf("│  Latitude        : %.2f\n", p->lat);
    printf("│  Ressource       : %s\n", resource_name(p->resource));
    printf("│  Hiérarchie      : région %d · pays %d · continent %d\n",
           (int)p->region, (int)p->country, (int)p->continent);
    /* La culture (langue/valeurs/subsistance/parenté/religion) n'appartient
     * plus à la Province : c'est un attribut de la population régionale
     * (RegionEconomy.culture), absente de ce visualiseur purement géographique. */
    printf("└────────────────────────────────────────────────\n");
    fflush(stdout);
}

/* ---- Barre de status (console, une ligne) ---------------------------- */
static void status_line(const World *w, ViewMode mode, uint32_t seed,
                        int cx, int cy, int selected) {
    printf("\r[%s] graine=%u  ", VIEW_NAMES[mode], seed);
    if (cx >= 0 && cx < SCPS_W && cy >= 0 && cy < SCPS_H) {
        const Cell *c = scps_cellc(w, cx, cy);
        printf("(%3d,%3d) %-18s h=%.2f m=%.2f t=%.2f  prov=%d",
               cx, cy, biome_name(c->biome),
               c->height, c->moisture, c->temperature,
               (int)c->province);
    }
    if (selected >= 0) printf("  [sél: #%d]", selected);
    fflush(stdout);
}

/* ====================================================================== *
 * UI DIÉGÉTIQUE — bandeau royaume + panneau de province (via la membrane)
 *
 * Le viewer ne lit QUE des Readout (bandes + mots) : aucun flottant SCPS,
 * aucun appel à scps_core. Tout passe par country_readout / province_readout
 * + label_X / hover_X.
 * ====================================================================== */

/* ---- Palette (bleu nuit & cuivre, §5.4) — affinée pour la profondeur --- */
static const SDL_Color COL_PANEL    = { 0x0d,0x14,0x20,0xf6 };  /* navy profond (corps de panneau) */
static const SDL_Color COL_PANEL2   = { 0x17,0x23,0x35,0xf6 };  /* navy clair (champs, pastilles) */
static const SDL_Color COL_PANEL_HI = { 0x26,0x36,0x4c,0x4d };  /* voile clair translucide (sheen) */
static const SDL_Color COL_COPPER   = { 0xc8,0x82,0x3e,0xff };  /* cuivre, plus chaud & vif */
static const SDL_Color COL_PARCH    = { 0xed,0xe3,0xcd,0xff };  /* parchemin, un brin plus clair */
static const SDL_Color COL_DIM      = { 0x96,0x8d,0x79,0xff };  /* texte secondaire */
static const SDL_Color COL_EDGE     = { 0x34,0x42,0x57,0xff };  /* bord DOUX (au lieu d'un trait dur) */
static const SDL_Color COL_SHADOW   = { 0x00,0x02,0x05,0x6e };  /* ombre portée (relief) */

/* Bande → couleur de sens (vert favorable → ambre → rouge défavorable). */
static SDL_Color sense_color(float good) {
    if (good < 0.f) good = 0.f;
    if (good > 1.f) good = 1.f;
    SDL_Color c; c.a = 0xff;
    if (good >= 0.5f) { float t=(good-0.5f)*2.f;            /* ambre → vert */
        c.r=(Uint8)(0xc8+(0x6a-0xc8)*t); c.g=(Uint8)(0xa0+(0x9a-0xa0)*t); c.b=(Uint8)(0x4a+(0x5b-0x4a)*t);
    } else { float t=good*2.f;                              /* rouge → ambre */
        c.r=(Uint8)(0xb1+(0xc8-0xb1)*t); c.g=(Uint8)(0x50+(0xa0-0x50)*t); c.b=(Uint8)(0x3c+(0x4a-0x3c)*t);
    }
    return c;
}
static SDL_Color band_good(int idx, int n, bool higher_better) {
    float g = (n>1) ? (float)idx/(float)(n-1) : 0.5f;
    return sense_color(higher_better ? g : 1.f-g);
}

/* ---- Texte (SDL_ttf) -------------------------------------------------- */
static TTF_Font *g_font = NULL, *g_font_big = NULL, *g_font_small = NULL;
static void draw_text(SDL_Renderer *ren, TTF_Font *f, int x, int y, SDL_Color col, const char *s) {
    if (!f || !s || !s[0]) return;
    SDL_Surface *su = TTF_RenderUTF8_Blended(f, s, col);
    if (!su) return;
    SDL_Texture *tx = SDL_CreateTextureFromSurface(ren, su);
    SDL_Rect d = { x, y, su->w, su->h };
    SDL_FreeSurface(su);
    if (tx) { SDL_RenderCopy(ren, tx, NULL, &d); SDL_DestroyTexture(tx); }
}
static int text_w(TTF_Font *f, const char *s){ int w=0; if (f&&s) TTF_SizeUTF8(f,s,&w,NULL); return w; }
static void fill_rect(SDL_Renderer *ren, int x,int y,int w,int h, SDL_Color c) {
    SDL_SetRenderDrawColor(ren, c.r,c.g,c.b,c.a);
    SDL_Rect r={x,y,w,h}; SDL_RenderFillRect(ren,&r);
}
/* Rectangle à COINS ARRONDIS (douceur) — corps en 3 bandes + 4 quarts de disque. */
static void fill_round(SDL_Renderer *ren, int x,int y,int w,int h, SDL_Color c, int r){
    if (r<1){ fill_rect(ren,x,y,w,h,c); return; }
    if (r*2>w) r=w/2;
    if (r*2>h) r=h/2;
    fill_rect(ren, x+r, y, w-2*r, h, c);
    fill_rect(ren, x, y+r, r, h-2*r, c);
    fill_rect(ren, x+w-r, y+r, r, h-2*r, c);
    SDL_SetRenderDrawColor(ren, c.r,c.g,c.b,c.a);
    for (int dy=0; dy<r; dy++) for (int dx=0; dx<r; dx++){
        if (dx*dx+dy*dy <= r*r){
            SDL_RenderDrawPoint(ren, x+r-1-dx,   y+r-1-dy);
            SDL_RenderDrawPoint(ren, x+w-r+dx,   y+r-1-dy);
            SDL_RenderDrawPoint(ren, x+r-1-dx,   y+h-r+dy);
            SDL_RenderDrawPoint(ren, x+w-r+dx,   y+h-r+dy);
        }
    }
}
/* Contour à coins arrondis (anneau d'un fill_round). */
static void round_box(SDL_Renderer *ren, int x,int y,int w,int h, SDL_Color c, int r){
    if (r<1){ SDL_SetRenderDrawColor(ren,c.r,c.g,c.b,c.a);
              SDL_Rect rc={x,y,w,h}; SDL_RenderDrawRect(ren,&rc); return; }
    if (r*2>w) r=w/2;
    if (r*2>h) r=h/2;
    SDL_SetRenderDrawColor(ren, c.r,c.g,c.b,c.a);
    SDL_RenderDrawLine(ren, x+r,y, x+w-r,y);
    SDL_RenderDrawLine(ren, x+r,y+h-1, x+w-r,y+h-1);
    SDL_RenderDrawLine(ren, x,y+r, x,y+h-r);
    SDL_RenderDrawLine(ren, x+w-1,y+r, x+w-1,y+h-r);
    for (int a=0;a<=90;a+=6){
        float rad=a*0.0174533f; int dx=(int)(r*cosf(rad)), dy=(int)(r*sinf(rad));
        SDL_RenderDrawPoint(ren, x+r-dx,   y+r-dy);
        SDL_RenderDrawPoint(ren, x+w-r+dx, y+r-dy);
        SDL_RenderDrawPoint(ren, x+r-dx,   y+h-r+dy);
        SDL_RenderDrawPoint(ren, x+w-r+dx, y+h-r+dy);
    }
}
/* Fond de PANNEAU « smooth » : ombre portée + corps navy arrondi + voile clair en
 * haut (sheen) + une BORDURE ÉPAISSE qui déborde vers l'EXTÉRIEUR (relief, cadre). */
static void panel_bg(SDL_Renderer *ren, int x,int y,int w,int h){
    fill_round(ren, x+4, y+6, w, h, COL_SHADOW, 10);    /* ombre portée, décalée */
    fill_round(ren, x, y, w, h, COL_PANEL, 8);          /* corps arrondi */
    fill_round(ren, x+2, y+2, w-4, h/5, COL_PANEL_HI, 7);/* voile clair en haut */
    round_box(ren, x,   y,   w,   h,   COL_EDGE,   8);  /* liseré intérieur doux */
    round_box(ren, x-1, y-1, w+2, h+2, COL_COPPER, 9);  /* bordure cuivre, débordante… */
    round_box(ren, x-2, y-2, w+4, h+4, COL_COPPER, 10); /* …épaissie vers l'extérieur */
}

/* ===================================================================== */
/* ARBRE DE TECH CONCENTRIQUE — la membrane (TechTreeReadout) → des anneaux */
/* angle = quartier (thème×fonction) · rayon = tier. Aucun flottant de tech : */
/* on ne lit QUE le readout (mots + nombres tangibles).                    */
/* ===================================================================== */
static void draw_ring(SDL_Renderer *ren, int cx, int cy, float r, SDL_Color c){
    SDL_SetRenderDrawColor(ren, c.r,c.g,c.b,c.a);
    int seg=120; float px=0,py=0;
    for (int i=0;i<=seg;i++){
        float a=(float)i/seg*6.2831853f, x=cx+cosf(a)*r, y=cy+sinf(a)*r;
        if (i>0) SDL_RenderDrawLine(ren,(int)px,(int)py,(int)x,(int)y);
        px=x; py=y;
    }
}
static void draw_box(SDL_Renderer *ren, int x,int y,int w,int h, SDL_Color c){
    fill_rect(ren,x,y,w,1,c); fill_rect(ren,x,y+h-1,w,1,c);
    fill_rect(ren,x,y,1,h,c); fill_rect(ren,x+w-1,y,1,h,c);
}
/* Jauge 0-100 : un dégradé ROUGE(bas)→VERT(haut) avec un marqueur clair à la
 * valeur. SDL n'a pas de gradient → on peint colonne par colonne (sense_color). */
static void draw_gauge(SDL_Renderer *ren, int x,int y,int gw,int gh,int value){
    if (value<0) value=0;
    if (value>100) value=100;
    for (int i=0;i<gw;i++){
        float t = (gw>1)? (float)i/(gw-1) : 0.f;     /* 0=rouge … 1=vert */
        fill_rect(ren, x+i, y, 1, gh, sense_color(t));
    }
    round_box(ren, x-1, y-1, gw+2, gh+2, COL_EDGE, 3);
    int mx = x + (int)(value/100.f*(gw-1));
    fill_round(ren, mx-1, y-2, 3, gh+4, COL_PARCH, 1);   /* le curseur à la valeur */
}
/* Camembert : des PARTS (percent[]) en couleurs (cols[]), peint disque par
 * pixel (SDL n'a pas de remplissage d'arc) — 0 en haut, sens horaire. */
static void draw_pie(SDL_Renderer *ren, int cx,int cy,int r,
                     const int *percent, const SDL_Color *cols, int n){
    for (int dy=-r; dy<=r; dy++) for (int dx=-r; dx<=r; dx++){
        if (dx*dx+dy*dy > r*r) continue;
        float a = atan2f((float)dx, (float)-dy);      /* 0 en haut */
        if (a<0) a += 6.2831853f;
        float frac100 = a/6.2831853f*100.f;           /* 0..100 horaire */
        SDL_Color c = COL_PANEL2; int acc=0;
        for (int i=0;i<n;i++){ acc+=percent[i]; if (frac100 < acc){ c=cols[i]; break; } }
        SDL_SetRenderDrawColor(ren, c.r,c.g,c.b,c.a);
        SDL_RenderDrawPoint(ren, cx+dx, cy+dy);
    }
    draw_ring(ren, cx, cy, (float)r, COL_DIM);
}
/* Palette de parts (camemberts, barres empilées) — cuivre, teals, parchemin… */
static const SDL_Color SLICE_PAL[8] = {
    {0xb8,0x73,0x33,0xff}, {0x4e,0x8d,0x8a,0xff}, {0xc9,0xa2,0x4b,0xff}, {0x7a,0x5c,0x99,0xff},
    {0x9a,0x8f,0x78,0xff}, {0x5f,0x8a,0xb0,0xff}, {0xa8,0x5a,0x5a,0xff}, {0x6f,0x9a,0x5a,0xff},
};
/* Un VISAGE : cercle + yeux + bouche parabolique dont la courbure suit l'humeur
 * (0 = triste/∩, 1 = content/∪). Allumé = en couleur ; éteint = gris muet. */
static void draw_face(SDL_Renderer *ren, int cx,int cy,int r, float mood, bool lit){
    SDL_Color c = lit ? sense_color(mood) : (SDL_Color){0x4a,0x52,0x5e,0xff};
    draw_ring(ren, cx, cy, (float)r, c);
    fill_rect(ren, cx-r/2,   cy-r/4, 2,2, c);     /* œil gauche */
    fill_rect(ren, cx+r/2-1, cy-r/4, 2,2, c);     /* œil droit */
    float curve=(mood-0.5f)*2.f;                  /* -1 = grimace … +1 = sourire */
    int span=r/2, my=cy+r/4, prevx=0, prevy=0;
    SDL_SetRenderDrawColor(ren, c.r,c.g,c.b,c.a);
    for (int k=0;k<=8;k++){
        float t=(float)k/8.f*2.f-1.f;             /* -1..1 */
        int mxk=cx+(int)(t*span);
        int myk=my+(int)(curve*(r/3.f)*(1.f-t*t));
        if (k>0) SDL_RenderDrawLine(ren, prevx,prevy, mxk,myk);
        prevx=mxk; prevy=myk;
    }
}
static void zone_add(SDL_Rect r, const char *def);   /* (défini plus bas — survol) */
/* survol : nom + EFFET de chaque nœud (mots de jeu) ; positions pour la capture. */
static char g_tree_hov[TECH_COUNT][240];
static int  g_tree_x[TECH_COUNT], g_tree_y[TECH_COUNT], g_tree_demo;
static int  g_tree_open = -1;                 /* tech dont l'anneau de SOUS-TECHS est ouvert (clic) ; -1 = aucun */
#define SYNC_HOV_SZ 200
static char g_sync_hov[SYNC_COUNT][SYNC_HOV_SZ];   /* survol 2-colonnes des sous-techs */
static int  sync_children(int techid, int *out){   /* indices des nœuds syncrétiques pendant de `techid` */
    int n=0; for (int k=0;k<SYNC_COUNT;k++) if ((int)tech_sync_node(k)->parent==techid) out[n++]=k; return n;
}
static void draw_tech_tree(SDL_Renderer *ren, int win_w, int win_h,
                           WorldEconomy *econ, TechState *ts, World *w, int cid){
    fill_rect(ren, 0,0, win_w, win_h, (SDL_Color){0x0a,0x0e,0x16,0xff});
    if (cid<0 || cid>=w->n_countries) return;
    TechTreeReadout tr;
    ai_sync_refresh(w, econ, &ts[cid], cid);   /* §syncrétique : cercle à jour à l'image (hors cadence IA) */
    unsigned acc = ai_race_access(w, econ, cid);
    float    pop = ai_country_population(w, econ, cid);
    tech_tree_readout(&ts[cid], acc, pop, &tr);

    int cx=win_w/2, cy=win_h/2 - 4;
    float ring = (float)win_h * 0.082f;            /* 4 anneaux (tiers 1..4) tiennent dans la hauteur */
    float GAP  = 0.45f*ring;                        /* écart du POINT central au 1er anneau */
    const float D2R=0.01745329f, TOP=-1.5707963f;  /* quartier 0 au sommet */
    SDL_Color tcol[3] = { {0x5a,0x86,0xd8,0xff}, {0xd8,0x86,0x42,0xff}, {0x5c,0xb8,0x6e,0xff} };

    for (int t=1;t<=4;t++) draw_ring(ren,cx,cy, GAP+t*ring, COL_PANEL2); /* anneaux = tiers (1..4) */
    for (int q=0;q<=9;q++){                                              /* rayons : thèmes (cuivre) & quartiers */
        float a=(q*40.f)*D2R + TOP; SDL_Color c=(q%3==0)?COL_COPPER:COL_PANEL2;
        SDL_SetRenderDrawColor(ren,c.r,c.g,c.b,c.a);
        SDL_RenderDrawLine(ren, cx+(int)(cosf(a)*ring*0.30f),     cy+(int)(sinf(a)*ring*0.30f),
                                cx+(int)(cosf(a)*(GAP+4.4f*ring)),cy+(int)(sinf(a)*(GAP+4.4f*ring)));
    }
    /* LE CENTRE (anneau 0) = un POINT. Les 6 bâtiments de base s'y logent ; le
     * survol les décrit (ils sont le départ, acquis d'emblée). */
    static char center_hov[300];
    { int p=snprintf(center_hov,sizeof center_hov,
        "Le centre (anneau 0) — les 6 bâtiments de base, acquis au départ : ");
      bool first=true;
      for (int i=0;i<tr.n && p<(int)sizeof center_hov-2;i++) if (tr.node[i].is_base){
          p += snprintf(center_hov+p, sizeof center_hov-p, "%s%s", first?"":" · ", tr.node[i].name);
          first=false;
      } }
    fill_rect(ren, cx-4, cy-4, 8, 8, COL_COPPER);                        /* le point central */
    draw_box(ren, cx-6, cy-6, 12, 12, COL_PARCH);
    zone_add((SDL_Rect){cx-9,cy-9,18,18}, center_hov);

    int cnt[9][8]={{0}}, seen[9][8]={{0}};
    for (int i=0;i<tr.n;i++){ int q=tr.node[i].quarter,t=tr.node[i].tier; if(q>=0&&q<9&&t>=1&&t<8)cnt[q][t]++; }
    g_tree_demo=-1;
    for (int i=0;i<tr.n;i++){
        const TreeNodeReadout *nd=&tr.node[i];
        if (nd->is_base) continue;                                       /* les bases SONT le centre */
        int q=nd->quarter,t=nd->tier; if(q<0||q>=9||t<1||t>=8) continue;
        int k=cnt[q][t], j=seen[q][t]++;
        float off=(k>1)? ((float)j-(k-1)/2.f)*(40.f/(k+1.f)) : 0.f;
        float ang=(q*40.f+20.f+off)*D2R + TOP, rad=GAP+t*ring;
        int x=cx+(int)(cosf(ang)*rad), y=cy+(int)(sinf(ang)*rad), theme=q/3;
        g_tree_x[i]=x; g_tree_y[i]=y;
        SDL_Color c=tcol[theme];
        if (nd->state==TREE_LOCKED){ c.r/=3;c.g/=3;c.b/=3; }
        else if (nd->state==TREE_OPEN){ c.r=(uint8_t)((c.r+255)/2);c.g=(uint8_t)((c.g+255)/2);c.b=(uint8_t)((c.b+255)/2); }
        int sz=5;
        fill_rect(ren,x-sz,y-sz,sz*2,sz*2,c);
        if (nd->faustian) draw_box(ren,x-sz-2,y-sz-2,sz*2+4,sz*2+4,(SDL_Color){0xe0,0x44,0x30,0xff});
        else if (nd->orphan) draw_box(ren,x-sz-2,y-sz-2,sz*2+4,sz*2+4,(SDL_Color){0x80,0x80,0x80,0xff});
        if (sync_children(i,(int[SYNC_COUNT]){0})>0)                      /* a des SOUS-TECHS : cliquable (anneau) */
            draw_ring(ren, x, y, (float)(sz+5), COL_COPPER);
        if (g_font_small){                                               /* NOM compact sous la bulle ; le détail au survol */
            SDL_Color lc = (nd->state==TREE_DONE)?COL_PARCH
                         : (nd->state==TREE_OPEN)?COL_DIM : (SDL_Color){0x5b,0x56,0x4b,0xff};
            int lw=text_w(g_font_small,nd->name);
            draw_text(ren,g_font_small, x-lw/2, y+sz+1, lc, nd->name);
        }
        /* SURVOL 2 COLONNES : titre = le bâtiment ; à GAUCHE le PRIX (+état), à DROITE l'EFFET. */
        snprintf(g_tree_hov[i],sizeof g_tree_hov[i], "%s%s%s\x1f" "coût %d pts · %s%s\x1f%s",
                 nd->name,
                 strcmp(nd->name,nd->unlocks)? " · bâtit " : "",
                 strcmp(nd->name,nd->unlocks)? nd->unlocks : "",
                 nd->cost, label_tree_state(nd->state),
                 nd->orphan? " · orpheline" : "",
                 nd->effet);
        zone_add((SDL_Rect){x-sz-3,y-sz-3,sz*2+6,sz*2+6}, g_tree_hov[i]);
        if (nd->faustian && (g_tree_demo<0 || nd->state==TREE_DONE)) g_tree_demo=i;  /* un faustien pour la démo */
    }
    for (int th=0;th<3;th++){                                            /* étiquettes de thème EXCENTRÉES (hors des nœuds) */
        float a=(th*120.f+60.f)*D2R + TOP, rl=GAP+4.75f*ring;
        int lx=cx+(int)(cosf(a)*rl), ly=cy+(int)(sinf(a)*rl);
        draw_text(ren,g_font_big,lx-text_w(g_font_big,tr.theme[th])/2,ly-9,tcol[th],tr.theme[th]);
    }
    /* ── ANNEAU SYNCRÉTIQUE (§11/§12) — au CLIC sur une tech, ses SOUS-TECHS (diffusion par
     *    contact) s'ouvrent en anneau autour d'elle ; disponibles seulement si la parente est
     *    ACQUISE. Pastille = état d'accès ; survol = hover 2-colonnes (profondeur · capacité). ── */
    if (g_tree_open>=0 && g_tree_open<TECH_COUNT &&
        (g_tree_x[g_tree_open] || g_tree_y[g_tree_open])){
        int kids[SYNC_COUNT], nk = sync_children(g_tree_open, kids);
        if (nk>0){
            int ox=g_tree_x[g_tree_open], oy=g_tree_y[g_tree_open];
            bool parent_done = ts[cid].unlocked[g_tree_open];
            float rr = ring*0.66f;
            draw_ring(ren, ox,oy, rr, COL_COPPER);                       /* l'anneau des sous-techs */
            for (int j=0;j<nk;j++){
                float a = TOP + (nk>1 ? (j-(nk-1)/2.f)*0.85f : 0.f);     /* en éventail au-dessus du parent */
                int x=ox+(int)(cosf(a)*rr), y=oy+(int)(sinf(a)*rr);
                SDL_RenderDrawLine(ren, ox,oy, x,y);                      /* trait parent → sous-tech */
                SyncReadout sr = sync_node_readout(&ts[cid], kids[j]);
                SDL_Color c = parent_done ? band_good((int)sr.acces,4,true) : (SDL_Color){0x44,0x40,0x38,0xff};
                int sz=6; fill_rect(ren,x-sz,y-sz,sz*2,sz*2,c);
                draw_box(ren,x-sz-1,y-sz-1,sz*2+2,sz*2+2,COL_PARCH);
                if (g_font_small){ int lw=text_w(g_font_small,sr.nom);
                    draw_text(ren,g_font_small,x-lw/2,y+sz+1,parent_done?COL_PARCH:COL_DIM,sr.nom); }
                /* hover 2 colonnes : GAUCHE = accès · profondeur requise ; DROITE = la capacité diffusée. */
                snprintf(g_sync_hov[kids[j]],SYNC_HOV_SZ,"%s\x1f%s · %s\x1f%s",
                         sr.nom,
                         parent_done? label_acces(sr.acces) : "parente requise",
                         label_profondeur(sr.requise),
                         tech_sync_node(kids[j])->unlocks);
                zone_add((SDL_Rect){x-sz-3,y-sz-3,sz*2+6,sz*2+6}, g_sync_hov[kids[j]]);
            }
        }
    }
    char hdr[220];
    snprintf(hdr,sizeof hdr,"ARBRE DE TECH — %s   ·   %d points de recherche   ·   SURVOLE = effet & prix · CLIC = sous-techs",
             w->country[cid].name, tr.points);
    draw_text(ren,g_font_big,18,12,COL_COPPER,hdr);
    draw_text(ren,g_font,18,win_h-40,COL_DIM,
      "anneau = tier · 3 secteurs = thèmes · cadre rouge = faustien · cadre gris = orphelin · cercle cuivré = a des SOUS-TECHS (clic → anneau de diffusion)");
    draw_text(ren,g_font,18,win_h-22,COL_DIM,
      "Savoir (bleu) · Forge (cuivre) · Société (vert)  —  vif = acquis · clair = disponible · sombre = verrouillé");
}

/* ---- Zones de survol → « un mot, une définition » --------------------- */
typedef struct { SDL_Rect r; const char *def; } HoverZone;
static HoverZone g_zones[160]; static int g_nzones;
static void zone_reset(void){ g_nzones=0; }
static void zone_add(SDL_Rect r, const char *def){
    if (g_nzones<160 && def){ g_zones[g_nzones].r=r; g_zones[g_nzones].def=def; g_nzones++; }
}
static const char *zone_hit(int mx,int my){
    for (int i=0;i<g_nzones;i++){ SDL_Rect *r=&g_zones[i].r;
        if (mx>=r->x && mx<r->x+r->w && my>=r->y && my<r->y+r->h) return g_zones[i].def; }
    return NULL;
}
/* SLOTS DE BÂTIMENT cliquables (§4 panneau) : le panneau les pose chaque frame,
 * la boucle d'évènements les teste au clic (vide → bâtir l'édifice). */
typedef struct { SDL_Rect r; int reg; int edifice; } BuildSlot;
static BuildSlot g_bslots[8]; static int g_nbslots;
static void bslot_reset(void){ g_nbslots=0; }
static void bslot_add(SDL_Rect r, int reg, int edifice){
    if (g_nbslots<8){ g_bslots[g_nbslots].r=r; g_bslots[g_nbslots].reg=reg; g_bslots[g_nbslots].edifice=edifice; g_nbslots++; }
}
/* LIGNES de l'OUTLINER cliquables : clic sur la ligne → saute à la région ; clic
 * sur le marteau → bâtir. `prov` = province à sélectionner ; `hammer_reg` = région
 * où bâtir (-1 = pas de marteau). */
typedef struct { SDL_Rect row; SDL_Rect ham; int prov; int hammer_reg; } OutRow;
static OutRow g_orows[80]; static int g_norows;
static SDL_Rect g_refill_btn; static int g_refill_owner = -1;   /* bouton « remplir » l'armée */
static void orow_reset(void){ g_norows=0; g_refill_owner=-1; }
static void orow_add(SDL_Rect row, SDL_Rect ham, int prov, int hammer_reg){
    if (g_norows<80){ g_orows[g_norows].row=row; g_orows[g_norows].ham=ham;
                      g_orows[g_norows].prov=prov; g_orows[g_norows].hammer_reg=hammer_reg; g_norows++; }
}
/* BOUTONS de mode de carte (§5) : posés chaque frame, testés au clic. */
typedef struct { SDL_Rect r; int mode; } ModeBtn;
static ModeBtn g_modebtns[6]; static int g_nmodebtns;
static void modebtn_reset(void){ g_nmodebtns=0; }
static void modebtn_add(SDL_Rect r, int mode){
    if (g_nmodebtns<6){ g_modebtns[g_nmodebtns].r=r; g_modebtns[g_nmodebtns].mode=mode; g_nmodebtns++; }
}
/* §1 — Le bandeau est un SOMMAIRE : chaque ressource OUVRE son système (clic). */
enum { SYS_FINANCES=0, SYS_SUBSISTANCE, SYS_CHAINES, SYS_TECH, SYS_DIPLO };
typedef struct { SDL_Rect r; int sys; } TopBtn;
static TopBtn g_topbtns[8]; static int g_ntopbtns;
static void topbtn_reset(void){ g_ntopbtns=0; }
static void topbtn_add(SDL_Rect r, int sys){
    if (g_ntopbtns<8){ g_topbtns[g_ntopbtns].r=r; g_topbtns[g_ntopbtns].sys=sys; g_ntopbtns++; }
}
/* Teintes diégétiques par culture (éthos) et par foi (branche) — le viewer les
 * calcule, le renderer les blende (membrane : pas de flottant SCPS au rendu). */
static uint32_t ethos_tint(int e){
    static const uint32_t P[6]={0xFFb0413a,0xFFc06a2e,0xFFc9a24b,0xFF4e8d8a,0xFF5f8ab0,0xFF6f9a5a};
    return P[(e>=0&&e<6)?e:0];
}
static uint32_t faith_tint(int b){
    static const uint32_t P[4]={0xFF6f9a5a,0xFFc9a24b,0xFF7a5c99,0xFF5f8ab0};
    return P[(b>=0&&b<4)?b:0];
}

/* ---- Sim branchée (snapshot de N ticks, déterministe par graine) ------ */
typedef struct {
    WorldEconomy    *econ;
    WorldProsperity *wp;
    WorldLegitimacy *wl;
    TradeNetwork    *net;
    TechState       *ts;
    Statecraft      *sc;
    AgencyState     *ag;       /* file d'actions (joueur ET IA) — en jours */
    EventsState     *ev;       /* chocs / évènements / âges */
    ModifierStack   *drift;    /* pile de dérive démographique */
    LaborEcon       *labor;    /* économie de pop du joueur (topbar) */
    DiploState      *dp;       /* relations / guerres */
    RouteNetwork    *rn;       /* routes commerciales */
    RevoltState     *rs;       /* soulèvements incarnés (sécessions, coups) */
    WarHost         *host;     /* armées levées par pays (mobilisation) */
    Campaign        *camp;     /* armées de campagne : marche/siège/bataille sur la carte (non-invasif) */
    uint32_t         camp_rng;
    MissionsState   *missions; /* missions décennales (rythme + injection de ressources) */
    NavyState       *navy;     /* la flotte (mer §5) : coques, chantier, entretien */
    int              prev_dawned; /* dernier âge avéné traité (engagement d'âge §7) */
    AiActor         *ai;       /* un acteur IA par pays voisin (cadence étalée) */
    bool            *ai_on;    /* ce pays est-il piloté par l'IA ? */
    int16_t          prev_owner_mo[SCPS_MAX_REG];  /* propriétaires du mois (détection de conquête) */
    int              day;      /* jour de jeu (1 tick = 1 jour) */
    int              year;
    int              player;   /* pays du joueur */
    bool             ready;
} Sim;

/* UN JOUR de jeu vivant (§1). Chaque sous-système avance à SA cadence calibrée :
 * agency/évènements/statecraft/labor en JOURS ; l'économie/légitimité/prospérité/
 * démographie à l'ANNÉE (comme les bancs d'essai — on ne dérègle pas le pacing).
 * Le joueur n'est qu'un acteur : ses actions sont déjà en file dans s->ag. */
/* Les armées de CAMPAGNE : chaque pays mobilisé ET en guerre projette sa force vers
 * le front ennemi adjacent (marche §1 → siège → bataille §2/§3). NON-INVASIF : lit
 * econ, ne change JAMAIS la propriété des régions — les armées VIVENT sur la carte
 * (la fondation que l'UI §4 dessine). */
static void sim_campaign_year(Sim *s, World *w) {
    for (int c=0; c<w->n_countries && c<SCPS_MAX_COUNTRY; c++) {
        if (campaign_active(s->camp,c) && campaign_phase(s->camp,c)!=FA_IDLE) continue;
        if (warhost_units(s->host, c) <= 0) continue;
        int frontier=-1, target=-1;
        for (int r=0; r<s->econ->n_regions && frontier<0; r++) {
            if (s->econ->region[r].owner!=c) continue;
            for (int sn=0; sn<s->econ->n_regions; sn++) {
                if (!s->econ->adj[r][sn]) continue;
                int ob=s->econ->region[sn].owner;
                if (ob<0 || ob==c || diplo_status(s->dp,c,ob)!=DIPLO_WAR) continue;
                frontier=r; target=sn; break;
            }
        }
        if (frontier>=0){
            campaign_order(s->camp, s->econ, c, frontier, target, &s->host->army[c]);
        } else if (c!=s->player){
            /* pas de frontière TERRESTRE : la guerre passe la mer si un port, des
             * transports et un chemin existent (mer §6 — contraint par le champ). */
            int port=navy_best_port(w,s->econ,c);
            if (port>=0 && navy_transport_packets_free(s->navy,c)>0){
                int tgt=-1;
                for (int r2=0;r2<s->econ->n_regions && tgt<0;r2++){
                    int ob=s->econ->region[r2].owner;
                    if (ob<0||ob==c||diplo_status(s->dp,c,ob)!=DIPLO_WAR) continue;
                    if (!s->econ->region[r2].coastal) continue;
                    tgt=r2;
                }
                if (tgt>=0)
                    campaign_order_sea(s->camp, w, s->econ, s->navy, c, port, tgt, &s->host->army[c]);
            }
        }
    }
    campaign_tick(s->camp, w, s->econ, s->dp, &s->camp_rng, 365.f);
    campaign_release_transports(s->camp, s->navy);   /* les transports rentrent à la rade */
}

/* État de l'écran (déclaré tôt : sim_day gate l'alerte audio sur GS_PLAYING). */
typedef enum { GS_MENU=0, GS_SETUP, GS_OPENING, GS_PLAYING } GameState;
static GameState g_gs = GS_MENU;

static void sim_day(Sim *s, World *w) {
    /* — quotidien — */
    agency_advance(s->ag, w, s->econ, s->wl, s->drift, 1);     /* les actions progressent */
    /* leviers intérieurs : draine les coûts SCPS différés (purge/mater) vers TechState */
    for (int c=0;c<w->n_countries && c<SCPS_MAX_COUNTRY;c++){
        float ch,fr,hh;
        if (agency_drain_levier_costs(c,&ch,&fr,&hh)){
            s->ts[c].charge+=ch; s->ts[c].fracture+=fr; s->ts[c].H+=hh;
        }
    }
    routes_advance(s->rn, w, s->econ, 1);
    for (int c=0;c<w->n_countries;c++) if (s->ai_on[c]){    /* les voisins VIVENT (cadence étalée) */
        ai_step(&s->ai[c], w, s->econ, s->wp, s->wl, s->ag, s->rn, s->dp, s->day);
        ai_research_step(&s->ai[c], &s->ts[c], w, s->econ, s->wp, s->day);  /* l'arbre vivant */
    }
    world_events_tick(s->ev, w, s->econ, s->wl, s->wp, s->sc, s->rn, s->ts, 1);
    labor_tick(s->labor);
    navy_tick(s->navy, w, s->econ, 1.f);   /* chantier + entretien : la chaîne navale TIRE */
    /* — mensuel : ÉCONOMIE + réputation diplomatique (O(n²)) + démographie, tous
     * au pas dt=1/12 → même rythme annuel, mais plus fluide qu'un saut yearly — */
    if (s->day % 30 == 29) {
        econ_apply_country_tech(s->econ, s->ts, SCPS_MAX_COUNTRY);  /* §B1 : techs de prod du pays → prod_mult région */
        econ_tick(s->econ, 1.f/12.f);
        navy_colonize_tick(s->navy, w, s->econ, 30.f);   /* mer §8 : on découvre ce que la volta touche */
        navy_course_tick(s->navy, w, s->econ, s->dp, s->rn, &s->camp_rng,
                         s->player, 30.f);   /* coques : la course (raids - saignee - blocus - verdicts) */
        navy_interception_tick(s->navy, s->camp, w, s->econ, s->dp, &s->camp_rng);   /* les convois se chassent */
        for (int c=0;c<w->n_countries && c<SCPS_MAX_COUNTRY;c++){   /* IA navale frugale (mer §5) */
            if (!s->ai_on[c]) continue;
            int hr=s->ai[c].home_region;
            if (hr<0||hr>=s->econ->n_regions) continue;
            RegionEconomy *re=&s->econ->region[hr];
            if (re->owner!=c) continue;
            if (re->coastal && re->build.port<=0.f && re->treasury>400.f){
                agency_build(s->ag, s->econ, hr, EDI_PORT);
            } else if (navy_best_port(w,s->econ,c)>=0 && s->navy->n[c].build_hull<0){
                if (s->navy->n[c].hull[HULL_TRANSPORT]<2 && re->treasury>500.f)
                    navy_order_build(s->navy, w, s->econ, c, HULL_TRANSPORT);
                else if (s->navy->n[c].hull[HULL_MERCHANT]<1 && re->treasury>700.f)
                    navy_order_build(s->navy, w, s->econ, c, HULL_MERCHANT);
            }
            if (s->day%180==29 && navy_region_is_port(w,s->econ,hr)){
                int mine=0;
                for (int i=0;i<s->rn->n;i++){
                    const TradeRoute *t=&s->rn->route[i];
                    if (t->maritime && (t->ra==hr||t->rb==hr)) mine++;
                }
                for (int r2=0;r2<s->econ->n_regions && mine<3;r2++){
                    if (s->econ->region[r2].owner==c||s->econ->region[r2].owner<0) continue;
                    if (!navy_region_is_port(w,s->econ,r2)) continue;
                    if (routes_order(s->rn, w, s->econ, hr, r2, true)){ mine++; break; }
                }
            }
        }
        statecraft_tick(s->sc, w, s->econ, s->wp, s->wl, s->dp, s->rn, 30);
        demography_tick(w, s->econ, s->wl, s->drift, 5.f, 5.f, 1.f/12.f);
        /* — conquête du mois : un peuple passé sous une couronne ÉTRANGÈRE devient
         *   restif (intégration à zéro, L au plancher) → terreau de sécession. */
        for (int r=0;r<s->econ->n_regions && r<SCPS_MAX_REG;r++){
            int16_t no=s->econ->region[r].owner, po=s->prev_owner_mo[r];
            if (po>=0 && no>=0 && no!=po){
                demography_on_conquest(w, s->econ, s->drift, r, no);
                revolt_on_conquest(s->rs, r);
            }
            s->prev_owner_mo[r]=no;
        }
        /* — la révolte INCARNÉE : misère soutenue → soulèvement → sécession/coup/
         *   jacquerie/écrasement ; un pays NÉ d'une sécession prend vie (IA). */
        revolt_scan(s->rs, w, s->econ, s->drift, 30);
        revolt_tick(s->rs, w, s->econ, s->drift, s->wl, s->wp, 30);
        if (s->rs->last_spawned>=0){
            for (int c=0;c<w->n_countries && c<SCPS_MAX_COUNTRY;c++){
                if (c==s->player || s->ai_on[c]) continue;
                int nreg=0; for (int r=0;r<s->econ->n_regions;r++) if (s->econ->region[r].owner==c) nreg++;
                if (w->country[c].role==POLITY_ANTAGONIST && w->country[c].capital_prov>=0 && nreg>0){
                    s->ai_on[c]=true;
                    ai_actor_init(&s->ai[c], w, s->econ, c, w->seed ^ (uint32_t)(c*2654435761u));
                }
            }
            /* une sécession a changé des propriétaires CE mois : resynchroniser, sinon
             * la détection du mois prochain prendrait l'indépendance pour une invasion. */
            for (int r=0;r<s->econ->n_regions && r<SCPS_MAX_REG;r++)
                s->prev_owner_mo[r]=s->econ->region[r].owner;
        }
    }
    /* — annuel (le tour stratégique) — */
    if (s->day % 365 == 364) {
        econ_colonize_tick(s->econ, w);
        econ_migrate_tick(s->econ, w);
        world_tick(w, s->econ, 1.0f);
        legitimacy_tick(s->wl, w, s->econ, s->ts);
        trade_network_build(s->net, w, s->econ);
        trade_tick(s->econ, s->net);
        intertrade_tick(s->econ, s->rn, s->dp);   /* grandes routes marchandes (goods inter-pays + embargo) */
        prosperity_tick(s->wp, w, s->econ, s->net, s->ts, s->wl);
        /* Diplomatie annuelle : usure de guerre, fonte des trêves/momentum, score de guerre. */
        warhost_tick(s->host, w, s->econ, s->dp, 1.0f);   /* la mobilisation : les armées vivent */
        sim_campaign_year(s, w);                           /* … et MARCHENT : campagne sur la carte */
        for (int c=0;c<w->n_countries && c<SCPS_MAX_COUNTRY;c++)
            diplo_set_faustian(s->dp, c, s->ts[c].charge);  /* souillure faustienne → croisades */
        diplo_tick(s->dp, 365.f);
        diplo_suzerainty_tick(s->dp, w, s->econ, s->wp);   /* suzeraineté + FRONDE : tributs, ligues, défections */
        diplo_war_tick(s->dp, w, s->econ, s->wp, 1.0f);
        missions_tick(s->missions, w, s->econ, s->ts, s->year);  /* missions décennales */
        faction_levers_decay(0.07f);   /* §4 : une stance non entretenue s'efface */
        if (s->ev->ages.last_dawned != s->prev_dawned){          /* §7 : un âge se lève → engagement */
            int age=s->ev->ages.last_dawned;
            if (age>=0) for (int c=0;c<w->n_countries && c<SCPS_MAX_COUNTRY;c++){
                if (w->country[c].role==POLITY_UNCLAIMED) continue;
                int nr=0; for (int r=0;r<s->econ->n_regions;r++) if (s->econ->region[r].owner==c) nr++;
                if (nr>0) faction_age_engage(w, s->econ, c, age);
            }
            s->prev_dawned = s->ev->ages.last_dawned;
            if (g_gs==GS_PLAYING) audio_alert();    /* §5 preuve de vie : un âge se lève → l'alerte discrète */
        }
    }
    if (++s->day % 365 == 0) s->year++;
}

/* (Ré)initialise la partie VIVANTE : monde déjà généré, on installe TOUS les
 * sous-systèmes, on attache les GROUPES démographiques, on amorce ~3 ans, puis
 * la partie avance par sim_day (plus de snapshot figé). */
static void sim_rebuild(Sim *s, World *w) {
    if (!s->econ || !s->wp || !s->wl || !s->net || !s->ts || !s->sc
        || !s->ag || !s->ev || !s->drift || !s->labor || !s->rs || !s->host || !s->camp || !s->navy) return;
    econ_init(s->econ, w);
    gen_population(w, s->econ);
    worldgen_seed_peoples(w, s->econ, g_player_race);   /* la race CHOISIE ancre le gradient */
    legitimacy_init(s->wl, w, s->econ);
    prosperity_init(s->wp, w);
    trade_network_build(s->net, w, s->econ);
    statecraft_init(s->sc, w);
    agency_init(s->ag);
    diplo_init(s->dp);
    diplo_seed_rng(s->dp, w->seed);   /* la fronde tire sa graine du MONDE (sinon même séquence à chaque partie) */
    routes_init(s->rn);
    intertrade_reset();   /* embargos décrétés + flux : RAZ par partie */
    /* RAZ PLEINE PLAGE : n_countries grandit par sécession ; à la RÉGÉNÉRATION (touche R)
     * les slots hauts gardaient ai_on/TechState périmés d'un monde précédent (cf. chronicle). */
    for (int c=0;c<SCPS_MAX_COUNTRY;c++){ s->ai_on[c]=false; tech_state_init(&s->ts[c], false); }
    s->player = 0;
    for (int c=0;c<w->n_countries;c++) if (w->country[c].role==POLITY_PLAYER){ s->player=c; break; }
    /* Chaque voisin (non-vierge, non-joueur) reçoit un acteur IA — sa personnalité
     * sort de sa fiche, ses actions empruntent la MÊME couche d'agency que le joueur. */
    for (int c=0;c<w->n_countries;c++){
        s->ai_on[c] = (c!=s->player && w->country[c].role!=POLITY_UNCLAIMED
                       && w->country[c].capital_prov>=0);
        if (s->ai_on[c]) ai_actor_init(&s->ai[c], w, s->econ, c, w->seed ^ (uint32_t)(c*2654435761u));
    }
    demography_attach(w, s->econ, s->drift);             /* 1 groupe substrat/région (non-régression) */
    demography_dyn_id_rebase(s->econ);                   /* compteur de drift_id au-dessus de l'existant */
    events_init(s->ev, w, w->seed);
    labor_init(s->labor, w);
    labor_seed_from_world(s->labor, w, s->econ, s->player);
    revolt_init(s->rs);                                  /* les soulèvements incarnés */
    warhost_init(s->host);                               /* les armées levées par pays */
    campaign_init(s->camp, w, s->econ);                  /* … qui marcheront sur la carte (terrain + RAZ) */
    s->camp_rng = w->seed ^ 0xCA117A11u;                 /* graine de campagne propre à la partie */
    missions_init(s->missions);                          /* missions décennales */
    navy_init(s->navy);                                  /* la flotte : chantiers vides, rades à trouver */
    faction_levers_reset();                              /* §4 : stances de factions à zéro */
    s->prev_dawned=-1;                                   /* §7 : aucun âge encore traité */
    for (int r=0;r<s->econ->n_regions && r<SCPS_MAX_REG;r++)   /* photo des propriétaires (conquête) */
        s->prev_owner_mo[r]=s->econ->region[r].owner;
    s->day=0; s->year=0;
    for (int t=0; t<3*365; t++) sim_day(s, w);           /* amorce : une carte déjà vivante */
    s->ready = true;
}

static int country_for_panel(const World *w, int selected) {
    if (selected>=0 && selected<w->n_provinces) {
        int c = w->province[selected].country;
        if (c>=0 && c<w->n_countries) return c;
    }
    for (int c=0;c<w->n_countries;c++) if (w->country[c].role==POLITY_PLAYER) return c;
    return 0;
}

/* Une lecture « Catégorie NN » : la catégorie en cuivre sourd, puis le NOMBRE de
 * jeu (0-100, sur 100 — le joueur le sait) coloré par le sens ; toute la pastille
 * est survolable (la définition au survol). Une métrique chiffrée se lit au seul
 * nombre : pas de mot redondant derrière. value < 0 ⇒ un MOT seul (signature sans
 * échelle : Assise, Présage). */
static int draw_reading(SDL_Renderer *ren, int x, int y, const char *cat,
                        int value, const char *word, SDL_Color wc, const char *def) {
    draw_text(ren, g_font, x, y, COL_DIM, cat);
    int xx = x + text_w(g_font, cat) + 6;
    char num[16];
    const char *shown = word;
    if (value >= 0) { snprintf(num, sizeof num, "%d", value); shown = num; }
    draw_text(ren, g_font, xx, y, wc, shown);
    int total = (xx - x) + text_w(g_font, shown);
    SDL_Rect z = { x-3, y-2, total+6, 21 };
    zone_add(z, def);
    return x + total + 18;
}

/* Une ressource « Nom stock +flux » (flux rouge si négatif). Survolable. */
static int draw_res(SDL_Renderer *ren, int x, int y, const char *name, long stock, float flow,
                    const char *def){
    char buf[48]; snprintf(buf,sizeof buf, "%s %ld", name, stock);
    draw_text(ren, g_font, x, y, COL_PARCH, buf);
    int w1 = text_w(g_font, buf);
    char fb[24];                                  /* le flux, TOUJOURS en +N/j */
    if (flow>=10.f || flow<=-10.f) snprintf(fb,sizeof fb, " %+d/j", (int)flow);
    else                            snprintf(fb,sizeof fb, " %+.1f/j", flow);
    SDL_Color fc = (flow<0) ? sense_color(0.12f) : sense_color(0.82f);
    draw_text(ren, g_font, x+w1, y, fc, fb);
    int total = w1 + text_w(g_font, fb);
    zone_add((SDL_Rect){x-3,y-2,total+6,19}, def);
    return x + total + 20;
}
/* Séparateur vertical fin entre clusters du bandeau. */
static int topbar_sep(SDL_Renderer *ren, int x, int y){
    fill_rect(ren, x+2, y-1, 1, 16, COL_DIM);
    return x + 9;
}

/* LA TOPBAR (§2) : ressources · métriques 0-100 · temps/âge/vitesse. Deux rangs,
 * cuivre sur bleu nuit. Aucun flottant SCPS — tout par la membrane (mots + 0-100). */
/* Une pastille d'ALERTE : un accent de couleur (nature) + le texte diégétique.
 * ambre = opportunité/mission · rouge = menace · bleu = information. Extensible :
 * les pastilles s'empilent horizontalement. Renvoie le x suivant. */
static int draw_alert(SDL_Renderer *ren, int x, int y, SDL_Color accent,
                      const char *text, const char *hov){
    TTF_Font *fs = g_font_small ? g_font_small : g_font;
    int tw = text_w(fs, text), w = tw + 18;
    fill_round(ren, x, y, w, 18, COL_PANEL2, 5);     /* corps arrondi */
    fill_round(ren, x+2, y+2, 4, 14, accent, 2);     /* la barre d'accent (nature) */
    round_box (ren, x-1, y-1, w+2, 20, accent, 6);   /* bordure colorée, débordante */
    draw_text (ren, fs, x+11, y+1, COL_PARCH, text);
    if (hov) zone_add((SDL_Rect){x,y,w,18}, hov);
    return x + w + 7;
}

static void draw_topbar(SDL_Renderer *ren, int win_w, const Sim *s, const World *w, int cid,
                        GameSpeed sp) {
    CountryReadout r = country_readout(s->wp, s->ts, w, cid);
    r.influence = statecraft_influence(s->sc, cid);
    int bh = 74;
    fill_rect(ren, 0,0, win_w, bh, COL_PANEL);
    fill_rect(ren, 0,0, win_w, 10, COL_PANEL_HI);          /* voile clair en haut (relief) */
    fill_rect(ren, 0,bh,   win_w, 3, COL_COPPER);          /* bordure cuivre ÉPAISSE */
    fill_rect(ren, 0,bh+3, win_w, 5, COL_SHADOW);          /* ombre portée sous le bandeau */

    /* — Rang A : DÉPENSABLE | ACCUMULABLE (clusters séparés) · temps/âge/vitesse (droite).
     *   Le bandeau est un SOMMAIRE : chaque ressource ouvre son système (clic). — */
    int x=12, yA=6, x0;
    /* Dépensable : Or · Nourriture · Matériaux (stock + flux +N/j) — chaque ressource
     * est une PORTE : un clic ouvre le menu de son système. */
    x0=x; x = draw_res(ren,x,yA, lres_name(LR_GOLD),      s->labor->stock[LR_GOLD],      (float)s->labor->flow[LR_GOLD],
                 "Or en caisse (clic → Finances : revenus, commerce, taxation réglable). Taxes + surplus vendu au marché.");
    topbtn_add((SDL_Rect){x0-3,yA-2,x-x0,19}, SYS_FINANCES);
    x0=x; x = draw_res(ren,x,yA, lres_name(LR_FOOD),      s->labor->stock[LR_FOOD],      (float)s->labor->flow[LR_FOOD],
                 "Vivres (clic → Subsistance & démographie). La famine stoppe la croissance de la population.");
    topbtn_add((SDL_Rect){x0-3,yA-2,x-x0,19}, SYS_SUBSISTANCE);
    x0=x; x = draw_res(ren,x,yA, lres_name(LR_MATERIALS), s->labor->stock[LR_MATERIALS], (float)s->labor->flow[LR_MATERIALS],
                 "Matériaux (clic → Chaînes de production & stock du marché). Bâtir, coloniser et armer en consomment.");
    topbtn_add((SDL_Rect){x0-3,yA-2,x-x0,19}, SYS_CHAINES);
    x = topbar_sep(ren, x, yA);
    /* Accumulable : Savoir (clic → l'Arbre de tech, qui EXISTE) · Influence. */
    float ppop = ai_country_population(w, s->econ, cid);
    x0=x; x = draw_res(ren,x,yA, "Savoir",    (long)r.m_savoir.value, ai_research_income(&s->ts[cid], ppop),
                 "Savoir — le niveau de lumière du royaume 0-100 (clic → Arbre de tech) ; le flux est la recherche que la population produit par jour.");
    topbtn_add((SDL_Rect){x0-3,yA-2,x-x0,19}, SYS_TECH);
    x0=x; x = draw_res(ren,x,yA, "Influence", (long)statecraft_influence(s->sc,cid), statecraft_influence_flux(s->sc,s->econ,s->wp,cid),
                 "Influence diplomatique (clic → Diplomatie). Prospérité + taille + accords tenus la nourrissent ; elle plafonne les diplomates.");
    topbtn_add((SDL_Rect){x0-3,yA-2,x-x0,19}, SYS_DIPLO);
    /* droite : date · barre 250 ans · âge · VITESSE (Espace = pause, +/-) */
    char date[48]; snprintf(date,sizeof date, "An %d / %d", s->year, GAME_YEARS);
    const char *age = (s->ev->ages.last_dawned>=0) ? age_name((AgeId)s->ev->ages.last_dawned) : "Aube du monde";
    const char *spl = SPEED_LABEL[sp];
    int wdate=text_w(g_font,date), wage=text_w(g_font,age), wspeed=text_w(g_font,spl);
    int bar_w=110, gap=14;
    int speedx = win_w - 12 - wspeed;
    int agex   = speedx - gap - wage;
    int barx   = agex - gap - bar_w;
    int datex  = barx - gap - wdate;
    if (datex < x+10) datex = x+10;
    draw_text(ren, g_font, datex, yA, COL_PARCH, date);
    zone_add((SDL_Rect){datex-3,yA-2,wdate+6,19}, "L'an de la partie (borne de fin : 250). Les âges montent la pression de la fin.");
    fill_rect(ren, barx, yA+4, bar_w, 8, COL_PANEL2);
    int filled = (int)((double)bar_w * (s->year<GAME_YEARS?s->year:GAME_YEARS) / GAME_YEARS);
    fill_rect(ren, barx, yA+4, filled, 8, COL_COPPER);
    draw_text(ren, g_font, agex, yA, sense_color(0.5f), age);
    zone_add((SDL_Rect){agex-3,yA-2,wage+6,19}, "L'âge courant du monde — reconnu quand le monde a atteint son état.");
    draw_text(ren, g_font, speedx, yA, (sp==SPEED_PAUSE)?sense_color(0.5f):COL_COPPER, spl);
    g_speed_zone=(SDL_Rect){speedx-3,yA-2,wspeed+6,19};   /* CLIQUABLE : cran suivant */
    zone_add(g_speed_zone, "Vitesse du temps — CLIC = cran suivant ; Espace = pause/reprise (dernier cran) ; +/− = accélérer/ralentir.");

    /* — Rang B : pays + INDICES 0-100 (colorés par valeur, rouge bas → vert haut).
     *   Pas d'« Assise » (on ne nomme pas le type de légitimité). Savoir/Influence
     *   sont passés en ACCUMULABLE (rang A) ; le présage part en alertes. — */
    int yB=28;
    const char *name = (cid>=0 && cid<w->n_countries) ? w->country[cid].name : "—";
    int xb=12;
    draw_text(ren, g_font, xb, yB, COL_COPPER, name); xb += text_w(g_font,name) + 18;
    xb = draw_reading(ren,xb,yB,"Stabilité", r.m_stabilite.value, label_stab(r.stabilite),   band_good(r.stabilite,5,true),  hover_stab());
    xb = draw_reading(ren,xb,yB,"Légitimité",r.m_legitimite.value,label_legit(r.legitimite), band_good(r.legitimite,5,true), hover_legit());
    xb = draw_reading(ren,xb,yB,"Cohésion",  r.m_cohesion.value,  label_concorde(r.concorde),band_good(r.concorde,4,false),  hover_concorde());
    xb = draw_reading(ren,xb,yB,"Prospérité",r.m_prosperite.value,label_prosp(r.prosperite), band_good(r.prosperite,5,true), hover_prosp());
    (void)xb;

    /* — Rang C : les FACTIONS-ÉTHOS par leur SATISFACTION (la signature, §9). Pastille
     *   d'identité + % coloré (vert content → ambre tiède → rouge aliéné). PAS de mot
     *   (ni « mène », ni « Sédition ») : le coup se lit d'une faction ROUGE & PUISSANTE.
     *   Triées par satisfaction décroissante : l'aliéné saute à droite. */
    {
        FactionsReadout fc = faction_readout(w, s->econ, cid);
        TTF_Font *fs = g_font_small ? g_font_small : g_font;
        int yC=52, xc=12;
        static const char *AB[6]   = {"Conquérants","Marchands","Légistes","Gardiens","Transgresseurs","Communs"};
        static const SDL_Color FCOL[6] = {
            {0xc0,0x55,0x4a,0xff}, {0xc9,0xa2,0x4b,0xff}, {0x5f,0x8a,0xb0,0xff},
            {0x7a,0x5c,0x99,0xff}, {0x8a,0x3a,0x6a,0xff}, {0x6f,0x9a,0x5a,0xff},
        };
        draw_text(ren, fs, xc, yC+2, COL_DIM, "Factions · satisfaction"); xc += 150;
        int ord[6]={0,1,2,3,4,5};
        for (int i=0;i<6;i++) for (int j=i+1;j<6;j++)
            if (fc.faction[ord[j]].satisfaction > fc.faction[ord[i]].satisfaction){ int t=ord[i];ord[i]=ord[j];ord[j]=t; }
        static char fhov[6][160];
        for (int k=0;k<6;k++){
            int f=ord[k];
            fill_round(ren, xc, yC+2, 10, 10, FCOL[f], 3);        /* pastille d'identité arrondie */
            round_box (ren, xc, yC+2, 10, 10, COL_EDGE, 3);
            draw_text(ren, fs, xc+13, yC, COL_DIM, AB[f]);
            int aw = text_w(fs, AB[f]);
            char pz[8]; snprintf(pz,sizeof pz, "%d%%", fc.faction[f].satisfaction);
            draw_text(ren, fs, xc+13+aw+4, yC, sense_color(fc.faction[f].satisfaction/100.f), pz);
            int total = 13+aw+4+text_w(fs,pz);
            bool coup = (!fc.faction[f].aligned && fc.faction[f].part >= 25);
            snprintf(fhov[k],sizeof fhov[k], "%s — satisfaction %d%% · part de pouvoir %d%%%s",
                     fc.faction[f].name, fc.faction[f].satisfaction, fc.faction[f].part,
                     coup ? " · ALIÉNÉE & PUISSANTE : le coup couve." : "");
            zone_add((SDL_Rect){xc-2,yC-2, total+6, 20}, fhov[k]);
            xc += total + 14;
        }
    }

    /* — La RANGÉE D'ALERTES : tout ce qui réclame une décision, en pastilles
     *   diégétiques empilables, accent par nature (ambre opportunité · rouge menace ·
     *   bleu information). Scalable : elles s'ajoutent à mesure qu'elles surgissent. — */
    {
        SDL_Color AMBER = sense_color(0.62f), RED = sense_color(0.10f), BLUE = (SDL_Color){0x5f,0x8a,0xb0,0xff};
        int ay=bh+4, ax=8, used=0;
        fill_rect(ren, 0, bh+2, win_w, 24, COL_PANEL2);
        /* 1. mission décennale (opportunité) */
        const Mission *mis = mission_of(s->missions, cid);
        if (mis && !mis->done){
            char mz[120]; snprintf(mz,sizeof mz, "Mission · %s", mis->text);
            ax = draw_alert(ren, ax, ay, AMBER, mz,
                            "Mission décennale : un but tiré de l'état du pays ; l'accomplir verse or + matières."); used++;
        }
        /* 2. faction aliénée & puissante (le coup qui couve — menace) */
        {
            FactionsReadout fc = faction_readout(w, s->econ, cid);
            for (int f=0; f<6; f++)
                if (!fc.faction[f].aligned && fc.faction[f].part >= 25){
                    char cz[120]; snprintf(cz,sizeof cz, "Cour · les %ss s'aliènent", fc.faction[f].name);
                    static char chov[140]; snprintf(chov,sizeof chov,
                        "Une faction PUISSANTE (%d%% du pouvoir) et ALIÉNÉE (satisfaction %d%%) : le coup couve.",
                        fc.faction[f].part, fc.faction[f].satisfaction);
                    ax = draw_alert(ren, ax, ay, RED, cz, chov); used++;
                    break;   /* une seule pastille de cour suffit */
                }
        }
        /* 3. l'augure (menace lue de l'état : sécession, révolte, coercition fragile) */
        if (r.augure) ax = draw_alert(ren, ax, ay, RED, r.augure,
                          "Un péril lu de l'état du royaume : sécession qui gronde, révolte, ou poigne qui s'effrite."), used++;
        /* 4. présage (information) */
        if (r.presage != PG_CALME)
            ax = draw_alert(ren, ax, ay, BLUE, label_presage(r.presage), hover_presage()), used++;
        if (!used) draw_text(ren, g_font_small?g_font_small:g_font, 10, bh+5, COL_DIM, "Rien ne presse.");
    }
}

static void ui_section(SDL_Renderer *ren, int x, int *y, const char *title){
    *y += 9;
    draw_text(ren, g_font, x, *y, COL_COPPER, title);
    *y += 21;
}
static void ui_row(SDL_Renderer *ren, int x, int *y, int pw, const char *cat,
                   const char *word, SDL_Color wc, const char *def){
    draw_text(ren, g_font, x,     *y, COL_DIM,  cat);
    draw_text(ren, g_font, x+104, *y, wc,       word);
    zone_add((SDL_Rect){x-2, *y-2, pw, 19}, def);
    *y += 20;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * SIDEBAR GAUCHE (§sidebar) — l'interface d'EMPIRE + la couche de DÉCISION.
 * Rail fixe (icônes-onglets) + tiroir glissant. Deux lois : (1) ne lit QUE la
 * membrane (bandes + nombres de jeu) ; (2) toute décision passe par les
 * ACTIONNEURS (agency / intertrade / warhost / campaign) — les MÊMES leviers
 * que l'IA, en JOURS, jamais une écriture directe ni un effet instantané.
 * ═══════════════════════════════════════════════════════════════════════════ */
#define SB_RAIL_W   46
#define SB_DRAWER_W 380
typedef enum { SBT_ECO=0, SBT_DEMO, SBT_STOCKS, SBT_ARMEE, SBT_FILTRES, SBT_COUNT } SbTab;
typedef struct {
    int     tab;                  /* -1 = replié */
    int     eco_sub;              /* 0 Commerce · 1 Marché · 2 Import/Export */
    int     scroll[SBT_COUNT];
    float   anim;                 /* 0..1 — glissement du tiroir (~150 ms) */
    int     reloc_src;            /* étape 1 de la relocalisation (-1 = pas choisie) */
    bool    show_currents;        /* filtre COURANTS : le champ marin (physique → continu légitime) */
    MapLens lens;                 /* lentille readout (LENS_NONE = vues classiques) */
    bool    purge_arm;            /* purge : 1er clic ARME, 2e confirme (acte irréversible) */
} Sidebar;
static Sidebar g_sb = { -1, 1, {0,0,0,0,0}, 0.f, -1, false, LENS_NONE, false };

/* cibles cliquables du tiroir (reconstruites chaque frame, comme zone_add) */
enum { SBH_TAB=1, SBH_ECOSUB, SBH_EMBARGO, SBH_RELOC_SRC, SBH_RELOC_DST, SBH_RELOC_CLR,
       SBH_EXPLOIT, SBH_LEVY, SBH_POSTURE, SBH_CHIP_MODE, SBH_CHIP_LENS, SBH_REFILL,
       SBH_MARCH, SBH_MUSTER, SBH_CANCEL,
       SBH_CHIP_CUR,
       SBH_NAVY_BUILD /* a=HullType */, SBH_SAIL /* a=région-cible */, SBH_NAVY_CONV /* a=1 vers pirate */,
       SBH_LEV_REPRESS, SBH_LEV_ASSIM, SBH_LEV_PURGE, SBH_LEV_EMBARGO,
       SBH_LEV_CONTRAT /* a=SuzContrat, b=pays cible */ };
typedef struct { SDL_Rect r; int kind, a, b; } SbHit;
static SbHit g_sbhits[120]; static int g_nsbhits;
static void sbhit_reset(void){ g_nsbhits=0; }
static void sbhit_add(SDL_Rect r, int kind, int a, int b){
    if (g_nsbhits<120){ g_sbhits[g_nsbhits].r=r; g_sbhits[g_nsbhits].kind=kind;
                        g_sbhits[g_nsbhits].a=a; g_sbhits[g_nsbhits].b=b; g_nsbhits++; }
}

/* Cache d'agrégats PAYS — recalculé au PAS DE JOUR (jamais par frame, §8). */
typedef struct {
    int   day;                    /* jour du dernier calcul (-1 = jamais) */
    float dem[RES_COUNT], sup[RES_COUNT], stk[RES_COUNT], prix[RES_COUNT];
    float prix_prev[RES_COUNT];   /* échantillon précédent (tendance ▲▼) */
    float cls_pop[CLASS_COUNT], cls_sat[CLASS_COUNT];
    long  pop_total; int n_reg;
} SbCache;
static SbCache g_sbc = { -1, {0},{0},{0},{0},{0},{0},{0}, 0, 0 };
static void sb_cache_refresh(const Sim *s, int day){
    if (g_sbc.day==day) return;
    memcpy(g_sbc.prix_prev, g_sbc.prix, sizeof g_sbc.prix);
    memset(g_sbc.dem,0,sizeof g_sbc.dem); memset(g_sbc.sup,0,sizeof g_sbc.sup);
    memset(g_sbc.stk,0,sizeof g_sbc.stk); memset(g_sbc.prix,0,sizeof g_sbc.prix);
    memset(g_sbc.cls_pop,0,sizeof g_sbc.cls_pop); memset(g_sbc.cls_sat,0,sizeof g_sbc.cls_sat);
    g_sbc.pop_total=0; g_sbc.n_reg=0;
    int me=s->player;
    for (int r=0;r<s->econ->n_regions;r++){
        const RegionEconomy *re=&s->econ->region[r];
        if (re->owner!=me || !re->colonized) continue;
        g_sbc.n_reg++;
        for (int g=1;g<RES_COUNT;g++){
            g_sbc.dem[g]+=re->demand[g]; g_sbc.sup[g]+=re->supply[g];
            g_sbc.stk[g]+=re->stock[g];  g_sbc.prix[g]+=re->price[g];
        }
        for (int c=0;c<CLASS_COUNT;c++){
            g_sbc.cls_pop[c]+=re->strata[c].pop;
            g_sbc.cls_sat[c]+=re->strata[c].satisfaction*re->strata[c].pop;
        }
    }
    for (int g=1;g<RES_COUNT;g++) if (g_sbc.n_reg>0) g_sbc.prix[g]/=(float)g_sbc.n_reg;
    for (int c=0;c<CLASS_COUNT;c++){
        g_sbc.pop_total+=(long)g_sbc.cls_pop[c];
        g_sbc.cls_sat[c]=(g_sbc.cls_pop[c]>0.f)?g_sbc.cls_sat[c]/g_sbc.cls_pop[c]:0.f;
    }
    if (g_sbc.day<0) memcpy(g_sbc.prix_prev,g_sbc.prix,sizeof g_sbc.prix);
    g_sbc.day=day;
}

/* petits pinceaux */
static SDL_Color sb_marche_col(BandMarche m){
    static const SDL_Color C[5]={ {0x60,0x60,0x60,0xff},{0xd0,0x50,0x40,0xff},{0xc8,0x90,0x40,0xff},
                                  {0x7a,0xa8,0x78,0xff},{0x8a,0x9a,0xb8,0xff} };
    return C[(m>=0&&m<=MARCHE_ENGORGE)?(int)m:0];
}
static int sb_chip(SDL_Renderer *ren, int x, int y, const char *txt, bool actif,
                   int kind, int a, int b, const char *hov){
    int w=text_w(g_font_small,txt)+14;
    fill_rect(ren,x,y,w,17, actif?(SDL_Color){0x3a,0x2c,0x1a,0xff}:(SDL_Color){0x16,0x1e,0x2c,0xff});
    draw_box (ren,x,y,w,17, actif?COL_COPPER:COL_PANEL2);
    draw_text(ren,g_font_small,x+7,y+2, actif?COL_COPPER:COL_DIM, txt);
    sbhit_add((SDL_Rect){x,y,w,17}, kind, a, b);
    if (hov) zone_add((SDL_Rect){x,y,w,17}, hov);
    return x+w+5;
}
static const char *sb_country_name(const World *w, int cid){
    return (cid>=0 && cid<w->n_countries) ? w->country[cid].name : "—";
}
/* région-capitale du joueur (point de ralliement par défaut) */
static int sb_capital_region(const Sim *s, const World *w){
    int cp=(s->player>=0&&s->player<w->n_countries)?w->country[s->player].capital_prov:-1;
    return (cp>=0&&cp<w->n_provinces)?w->province[cp].region:-1;
}

/* ── panneau ÉCONOMIE : Commerce · Marché · Import/Export ─────────────────── */
static void sb_panel_eco(SDL_Renderer *ren, int x, int y, int w, int h, Sim *s, const World *world){
    static char buf[64][120]; int nb=0;
    int me=s->player;
    int cx=x+10;
    cx=sb_chip(ren,cx,y,"Commerce",   g_sb.eco_sub==0, SBH_ECOSUB,0,0,"Les routes marchandes vivantes — et l'EMBARGO, qui se décrète ici.");
    cx=sb_chip(ren,cx,y,"Marché",     g_sb.eco_sub==1, SBH_ECOSUB,1,0,"La table des biens : prix en or, tendance, état du marché en mots.");
    (void)sb_chip(ren,cx,y,"Imp/Exp", g_sb.eco_sub==2, SBH_ECOSUB,2,0,"Ce que le pays importe et exporte (volumes, partenaires, or encaissé).");
    y+=24;
    if (g_sb.eco_sub==0){
        int routes=intertrade_active_routes(s->econ, s->rn, s->dp, me);
        float gold=intertrade_export_gold(me);
        snprintf(buf[nb],120,"%d route(s) vivante(s) · export %.0f or/an", routes, gold);
        draw_text(ren,g_font_small,x+10,y,COL_PARCH,buf[nb]); nb++; y+=20;
        draw_text(ren,g_font_small,x+10,y,COL_DIM,"partenaires :"); y+=16;
        int shown=0;
        for (int c=0;c<world->n_countries && shown<10 && y<h-22;c++){
            if (c==me) continue;
            float v=intertrade_pair_value(me,c);
            bool emb=intertrade_embargoed(me,c);
            bool war=diplo_status(s->dp,me,c)==DIPLO_WAR;
            if (v<=0.5f && !emb && !war) continue;
            const char *etat = war?"GUERRE":emb?"EMBARGO":(v>200.f)?"florissant":"modeste";
            snprintf(buf[nb],120,"%-18.18s %7.0f or/an  %s", sb_country_name(world,c), v, etat);
            SDL_Color col= war?(SDL_Color){0xd0,0x50,0x40,0xff}: emb?(SDL_Color){0xc8,0x90,0x40,0xff}:COL_PARCH;
            draw_text(ren,g_font_small,x+12,y,col,buf[nb]);
            sbhit_add((SDL_Rect){x+8,y-2,w-16,16}, SBH_EMBARGO, c, emb?0:1);
            static char hv[64][140]; snprintf(hv[shown],140, emb?
                "Lever l'embargo contre %s — le négoce (~%.0f or/an) pourra reprendre.":
                "Décréter l'EMBARGO contre %s — coupe ~%.0f or/an de négoce (le coût, avant de signer).",
                sb_country_name(world,c), v);
            zone_add((SDL_Rect){x+8,y-2,w-16,16}, hv[shown]);
            nb++; shown++; y+=16;
        }
    } else if (g_sb.eco_sub==1){
        draw_text(ren,g_font_small,x+10,y,COL_DIM,"bien            prix(or)  tend.  état"); y+=16;
        int idx[RES_COUNT], n=0;
        for (int g=1;g<RES_COUNT;g++) if (g_sbc.dem[g]>0.05f||g_sbc.sup[g]>0.05f) idx[n++]=g;
        for (int i=1;i<n;i++){ int k=idx[i],j=i;        /* tri : tension d'abord */
            while(j>0 && (int)band_marche(g_sbc.dem[idx[j-1]],g_sbc.sup[idx[j-1]]+g_sbc.stk[idx[j-1]])
                       > (int)band_marche(g_sbc.dem[k],g_sbc.sup[k]+g_sbc.stk[k])){ idx[j]=idx[j-1]; j--; }
            idx[j]=k; }
        int off=g_sb.scroll[SBT_ECO]; if(off>n-1)off=n>0?n-1:0; if(off<0)off=0;
        for (int i=off;i<n && y<h-20;i++){
            int g=idx[i];
            BandMarche m=band_marche(g_sbc.dem[g], g_sbc.sup[g]+g_sbc.stk[g]);
            float d=g_sbc.prix[g]-g_sbc.prix_prev[g];
            const char *tend=(d>0.02f)?"\xe2\x96\xb2":(d<-0.02f)?"\xe2\x96\xbc":"\xc2\xb7";
            snprintf(buf[nb],120,"%-14.14s %7.2f   %s    %s",
                     resource_name((Resource)g), g_sbc.prix[g], tend, label_marche(m));
            draw_text(ren,g_font_small,x+12,y,sb_marche_col(m),buf[nb]); nb++; y+=15;
        }
    } else {
        draw_text(ren,g_font_small,x+10,y,COL_DIM,"bien            import (de)        export (vers)"); y+=16;
        int shown=0, off=g_sb.scroll[SBT_ECO];
        for (int g=1, seen=0; g<RES_COUNT && y<h-20; g++){
            float iv=intertrade_import_vol(me,g), ev=intertrade_export_vol(me,g);
            if (iv<0.05f && ev<0.05f) continue;
            if (seen++ < off) continue;
            int from=intertrade_import_from(me,g), to=intertrade_export_to(me,g);
            snprintf(buf[nb],120,"%-12.12s %5.0f %-10.10s   %5.0f %-10.10s",
                     resource_name((Resource)g),
                     iv, iv>0.05f?sb_country_name(world,from):"—",
                     ev, ev>0.05f?sb_country_name(world,to):"—");
            draw_text(ren,g_font_small,x+12,y,COL_PARCH,buf[nb]); nb++; y+=15; shown++;
        }
    }
}

/* ── panneau DÉMOGRAPHIE : classes, peuples, croissance + RELOCALISATION ───── */
static void sb_panel_demo(SDL_Renderer *ren, int x, int y, int w, int h, Sim *s, const World *world){
    static char buf[48][130]; int nb=0;
    (void)world;
    static const char *CN[CLASS_COUNT]={"Journaliers","Bourgeois","Nobles"};
    snprintf(buf[nb],130,"population : %ldk  ·  %d région(s)", g_sbc.pop_total/1000, g_sbc.n_reg);
    draw_text(ren,g_font,x+10,y,COL_PARCH,buf[nb]); nb++; y+=22;
    for (int c=0;c<CLASS_COUNT;c++){
        float part=(g_sbc.pop_total>0)?100.f*g_sbc.cls_pop[c]/(float)g_sbc.pop_total:0.f;
        snprintf(buf[nb],130,"%-12s %6.0f  (%2.0f%%)   satisfaction %2.0f%%",
                 CN[c], g_sbc.cls_pop[c], part, 100.f*g_sbc.cls_sat[c]);
        draw_text(ren,g_font_small,x+12,y,COL_PARCH,buf[nb]); nb++; y+=15;
    }
    y+=8;
    draw_text(ren,g_font_small,x+10,y,COL_COPPER,"Relocalisation"); y+=16;
    /* file d'ordres en cours (annulables) */
    for (int i=0;i<s->ag->n && y<h-120;i++){
        const BuildOrder *o=&s->ag->order[i];
        if (!o->active || o->kind!=AGY_RELOCATE) continue;
        if (o->region<0||o->region>=s->econ->n_regions) continue;
        if (s->econ->region[o->region].owner!=s->player) continue;
        snprintf(buf[nb],130,"convoi %d→%d · %d j restants   [annuler]",
                 o->region, o->param, o->days_total-o->days_done);
        draw_text(ren,g_font_small,x+12,y,COL_DIM,buf[nb]); nb++;
        sbhit_add((SDL_Rect){x+8,y-2,w-16,15}, SBH_CANCEL, i, 0);
        zone_add((SDL_Rect){x+8,y-2,w-16,15},"Révoquer ce convoi (rien n'est appliqué tant qu'il n'est pas arrivé).");
        y+=15;
    }
    if (g_sb.reloc_src<0){
        draw_text(ren,g_font_small,x+10,y,COL_DIM,"source :"); y+=15;
        int picked[6], np=0;
        for (int k=0;k<6;k++){ int best=-1; float bp=0.f;
            for (int r=0;r<s->econ->n_regions;r++){
                const RegionEconomy *re=&s->econ->region[r];
                if (re->owner!=s->player||!re->colonized) continue;
                bool used=false; for(int q=0;q<np;q++) if(picked[q]==r) used=true;
                if (used) continue;
                float lp=re->strata[CLASS_LABORER].pop;
                if (lp>bp){ bp=lp; best=r; }
            }
            if (best<0) break;
            picked[np++]=best;
        }
        for (int q=0;q<np && y<h-20;q++){
            int r=picked[q];
            snprintf(buf[nb],130,"région %-3d  ·  %5.0f journaliers", r, s->econ->region[r].strata[CLASS_LABORER].pop);
            draw_text(ren,g_font_small,x+12,y,COL_PARCH,buf[nb]); nb++;
            sbhit_add((SDL_Rect){x+8,y-2,w-16,15}, SBH_RELOC_SRC, r, 0);
            zone_add((SDL_Rect){x+8,y-2,w-16,15},"Prendre les familles ICI — la coercition montera à la source (le coût du déplacement forcé).");
            y+=15;
        }
    } else {
        snprintf(buf[nb],130,"source : région %d   [changer]", g_sb.reloc_src);
        draw_text(ren,g_font_small,x+10,y,COL_COPPER,buf[nb]); nb++;
        sbhit_add((SDL_Rect){x+8,y-2,w-16,15}, SBH_RELOC_CLR, 0,0); y+=16;
        draw_text(ren,g_font_small,x+10,y,COL_DIM,"cible :"); y+=15;
        int shown=0;
        for (int r=0;r<s->econ->n_regions && shown<6 && y<h-20;r++){
            const RegionEconomy *re=&s->econ->region[r];
            if (re->owner!=s->player||!re->colonized||r==g_sb.reloc_src) continue;
            if (re->habitability<0.20f) continue;
            if (re->strata[CLASS_LABORER].pop > 600.f) continue;     /* déjà peuplée */
            int gbest=-1; float sbest=0.f;
            for (int g=1;g<RES_COUNT;g++){
                if (re->raw_cap[g]<=0.f) continue;
                float shortfall=g_sbc.dem[g]-(g_sbc.sup[g]+g_sbc.stk[g]);
                if (shortfall>sbest){ sbest=shortfall; gbest=g; }
            }
            if (gbest<0) continue;
            snprintf(buf[nb],130,"région %-3d  ·  %-12.12s  ·  %4.0f bras",
                     r, resource_name((Resource)gbest), re->strata[CLASS_LABORER].pop);
            draw_text(ren,g_font_small,x+12,y,COL_PARCH,buf[nb]); nb++;
            sbhit_add((SDL_Rect){x+8,y-2,w-16,15}, SBH_RELOC_DST, r, 0);
            static char hv2[8][150];
            snprintf(hv2[shown],150,"ORDONNER le convoi : ~%d familles, 90 jours de route — la coercition montera dans la région %d.",
                     AGY_RELOC_POP, g_sb.reloc_src);
            zone_add((SDL_Rect){x+8,y-2,w-16,15},hv2[shown]);
            y+=15; shown++;
        }
    }
}

/* ── panneau STOCKS — sobre : l'état par la COULEUR et l'ORDRE, jamais par les mots.
 * Un bien mort n'existe pas à l'écran. L'exploitation est un chip de LIGNE (un ordre
 * disponible = un état), pas une suggestion. ───────────────────────────────── */
static bool sb_good_alive(int g){
    return g_sbc.stk[g]>0.5f || g_sbc.dem[g]>0.05f || g_sbc.sup[g]>0.05f;
}
static void sb_panel_stocks(SDL_Renderer *ren, int x, int y, int w, int h, Sim *s, const World *world){
    static char buf[56][130]; int nb=0;
    (void)world; (void)w;
    static const Resource STRAT[3]={RES_SALTPETER,RES_CELESTIAL_IRON,RES_ARCANE_CRYSTAL};
    draw_text(ren,g_font_small,x+10,y,COL_DIM,"bien            stock   net/j   couv."); y+=16;
    int idx[RES_COUNT], n=0;
    for (int k=0;k<3;k++) if (sb_good_alive((int)STRAT[k])) idx[n++]=(int)STRAT[k];   /* ★ vivants seulement */
    int base=n;
    for (int g=1;g<RES_COUNT;g++){
        bool strat=false; for(int k2=0;k2<3;k2++) if((int)STRAT[k2]==g) strat=true;
        if (strat || !sb_good_alive(g)) continue;
        idx[n++]=g;
    }
    for (int i=base+1;i<n;i++){ int k=idx[i],j=i;                  /* tensions en tête */
        while(j>base && (int)band_marche(g_sbc.dem[idx[j-1]],g_sbc.sup[idx[j-1]]+g_sbc.stk[idx[j-1]])
                      > (int)band_marche(g_sbc.dem[k],g_sbc.sup[k]+g_sbc.stk[k])){ idx[j]=idx[j-1]; j--; }
        idx[j]=k; }
    int off=g_sb.scroll[SBT_STOCKS]; if(off<0)off=0; if(off>n-1&&n>0)off=n-1;
    int chips=0;
    for (int i=off;i<n && y<h-18;i++){
        int g=idx[i];
        BandMarche m=band_marche(g_sbc.dem[g], g_sbc.sup[g]+g_sbc.stk[g]);
        float net=(g_sbc.sup[g]-g_sbc.dem[g])/30.f;                /* le tick éco est mensuel */
        if (net>-0.05f && net<0.05f) net=0.f;                       /* fini les −0.0 */
        char couv[12]="";
        if (net<-0.05f){
            float dj=g_sbc.stk[g]/(-net);
            if (dj>365.f) snprintf(couv,12,">1 an"); else snprintf(couv,12,"%.0f j",dj);
        }
        char netl[12];
        if (net==0.f) snprintf(netl,12,"  0.0"); else snprintf(netl,12,"%+5.1f",net);
        snprintf(buf[nb],130,"%s%-12.12s %6.0f  %s  %6s",
                 (i<base)?"\xe2\x98\x85 ":"  ", resource_name((Resource)g),
                 g_sbc.stk[g], netl, couv);
        draw_text(ren,g_font_small,x+8,y,sb_marche_col(m),buf[nb]); nb++;
        /* un ordre DISPONIBLE est un état : le chip [exploiter] sur la ligne en tension,
         * si une terre à toi porte ce bien (sinon : rien — le silence informe). */
        if (chips<4 && (m==MARCHE_PENURIE||m==MARCHE_TENDU)){
            for (int r=0;r<s->econ->n_regions;r++){
                const RegionEconomy *re=&s->econ->region[r];
                if (re->owner!=s->player||!re->colonized||re->raw_cap[g]<=0.f) continue;
                sbhit_add((SDL_Rect){x+w-92,y-1,80,14}, SBH_EXPLOIT, r, g);
                draw_text(ren,g_font_small,x+w-92,y,COL_COPPER,"[exploiter]");
                zone_add((SDL_Rect){x+w-92,y-1,80,14},
                    "Aménager l'extraction (mine/carrière) sur ta terre qui porte ce bien — en file, en jours.");
                chips++; break;
            }
        }
        y+=15;
    }
}

/* ── panneau ARMÉE : jauge de levée + armée de campagne (posture, ordres) ──── */
static void sb_panel_armee(SDL_Renderer *ren, int x, int y, int w, int h, Sim *s, const World *world, int selected){
    static char buf[32][140]; int nb=0;
    int me=s->player;
    long units=warhost_units(s->host, me);
    int lv=warhost_levy(s->host, me);
    snprintf(buf[nb],140,"force mobilisée : %ld paquet(s) de 100", units);
    draw_text(ren,g_font,x+10,y,COL_PARCH,buf[nb]); nb++; y+=22;
    draw_text(ren,g_font_small,x+10,y,COL_DIM,"jauge de levée :"); y+=16;
    { int cx=x+12;
      static const char *HV[4]={
        "Levée basse : on rend les bras à l'économie (mobilisation 0.4×).",
        "Garde : l'entretien normal du temps de paix (1×).",
        "Pied de guerre : la mobilisation presse (1.6×).",
        "LEVÉE EN MASSE (2.6×) : on force la main des familles — la coercition montera à la capitale." };
      for (int l=0;l<4;l++) cx=sb_chip(ren,cx,y,warhost_levy_name(l), lv==l, SBH_LEVY,l,0,HV[l]);
    } y+=24;
    if (campaign_active(s->camp, me)){
        int loc=campaign_location(s->camp, me);
        FieldPhase ph=campaign_phase(s->camp, me);
        ArmyComposition comp=campaign_composition(s->camp, me);
        snprintf(buf[nb],140,"armée de campagne — région %d · %s", loc, campaign_phase_name(ph));
        draw_text(ren,g_font_small,x+10,y,COL_COPPER,buf[nb]); nb++; y+=15;
        snprintf(buf[nb],140,"  inf %ld · arch %ld · cav %ld · mages %ld  (Σ %ld paquets)",
                 comp.infanterie, comp.archers, comp.cavalerie, comp.mages, comp.total);
        draw_text(ren,g_font_small,x+10,y,COL_PARCH,buf[nb]); nb++; y+=16;
        draw_text(ren,g_font_small,x+10,y,COL_DIM,"posture :"); y+=15;
        { int cx=x+12; int po=campaign_posture(s->camp, me);
          static const char *HV[3]={
            "Prudente : marche et siège LENTS — on préserve la troupe.",
            "Standard : l'allure du manuel.",
            "Agressive : marche vive, siège tambour battant — on presse." };
          for (int p=0;p<3;p++) cx=sb_chip(ren,cx,y,campaign_posture_name(p), po==p, SBH_POSTURE,p,0,HV[p]);
        } y+=24;
        if (campaign_can_refill(s->camp, s->econ, me)){
            long men=0, mat=0; campaign_refill_cost(s->camp, me, &men, &mat);
            snprintf(buf[nb],140,"[renforcer]  +1 paquet/type — %ld hommes · %ld matériaux", men, mat);
            draw_text(ren,g_font_small,x+12,y,COL_PARCH,buf[nb]); nb++;
            sbhit_add((SDL_Rect){x+8,y-2,w-16,15}, SBH_REFILL, 0,0);
            zone_add((SDL_Rect){x+8,y-2,w-16,15},"Recompléter en territoire AMI : on lève des hommes et l'on fabrique les armes (payé sur l'économie).");
            y+=17;
        }
        if (selected>=0 && selected<world->n_provinces){
            int tr=world->province[selected].region;
            if (tr>=0 && tr<s->econ->n_regions && tr!=loc){
                snprintf(buf[nb],140,"[marcher] sur la province sélectionnée (région %d)", tr);
                draw_text(ren,g_font_small,x+12,y,COL_PARCH,buf[nb]); nb++;
                sbhit_add((SDL_Rect){x+8,y-2,w-16,15}, SBH_MARCH, tr, 0);
                zone_add((SDL_Rect){x+8,y-2,w-16,15},"ORDONNER la marche (itinéraire en jours, terrain décidant) ; une terre ennemie sera ASSIÉGÉE à l'arrivée.");
                y+=17;
            }
        }
    } else {
        if (units>0){
            int cr=sb_capital_region(s,world);
            snprintf(buf[nb],140,"[lever l'ost] rassembler à la capitale (région %d)", cr);
            draw_text(ren,g_font_small,x+12,y,COL_PARCH,buf[nb]); nb++;
            sbhit_add((SDL_Rect){x+8,y-2,w-16,15}, SBH_MUSTER, cr, 0);
            zone_add((SDL_Rect){x+8,y-2,w-16,15},"Déployer la force mobilisée en ARMÉE DE CAMPAGNE au camp de la capitale — elle marchera sur ordre.");
        }
    }
    /* ── LA FLOTTE (mer §5) : coques · chantier · rade — et l'embarquement ── */
    y+=8;
    { const Navy *nv=&s->navy->n[me];
      int port=navy_best_port(world, s->econ, me);
      draw_text(ren,g_font_small,x+10,y,COL_DIM,"Flotte"); y+=15;
      if (port<0){
          draw_text(ren,g_font_small,x+12,y,COL_DIM,
                    s->econ->region[sb_capital_region(s,world)].coastal
                    ? "aucune rade — bâtir un Port (panneau Bâtir)"
                    : "pays sans côte — la mer est ailleurs"); y+=17;
      } else {
          snprintf(buf[nb],140,"%d combat · %d transport(s) (%d en mer) · %d marchand(s) · %d pirate(s)",
                   nv->hull[HULL_WAR], nv->hull[HULL_TRANSPORT], nv->at_sea, nv->hull[HULL_MERCHANT], nv->hull[HULL_PIRATE]);
          draw_text(ren,g_font_small,x+12,y,COL_PARCH,buf[nb]); nb++; y+=17;
          { float worst=0.f;   /* coques §7 : l'état des routes, en mots */
            const TradeRoute *asym=NULL; bool asym_ab_down=true;
            for (int i=0;i<s->rn->n;i++){ const TradeRoute *t=&s->rn->route[i];
                if (!t->open) continue;
                bool mine=(s->econ->region[t->ra].owner==me || s->econ->region[t->rb].owner==me);
                if (!mine) continue;
                if (t->maritime && t->pirate_press>worst) worst=t->pirate_press;
                /* commerce asym. §5 : la première route DIRECTIONNELLE se détaille en mots */
                if (!asym && (t->fluvial || (t->maritime && t->days_ab>0.f && t->days_ba>0.f
                                             && fabsf(t->days_ab-t->days_ba)>2.f))){
                    asym=t;
                    asym_ab_down = t->fluvial ? (t->fluvial==1) : (t->days_ab<t->days_ba);
                } }
            if (worst>0.f){
                snprintf(buf[nb],140,"routes : %s", worst>=90.f?"BLOQUÉES":worst>2.f?"infestées":"harcelées");
                draw_text(ren,g_font_small,x+12,y,worst>2.f?COL_COPPER:COL_DIM,buf[nb]); nb++; y+=17;
            }
            if (asym){
                int down_end = asym_ab_down ? asym->rb : asym->ra;
                snprintf(buf[nb],140,"%s rég. %d : en aval — abondant · retour : maigre et précieux",
                         asym->fluvial?"fleuve vers":"couloir vers", down_end);
                draw_text(ren,g_font_small,x+12,y,COL_DIM,buf[nb]); nb++;
                zone_add((SDL_Rect){x+8,y-2,w-16,15},"Le coût de transport a un SENS : le vrac (grain, bois, charbon) ne paie que la descente ; seul le précieux (étoffe, orfèvrerie, remèdes, armes) paie la remontée. Le volume suit la facilité — divisé à contre-courant, jamais nul.");
                y+=17;
            } }
          if (nv->hull[HULL_MERCHANT]>0 || nv->hull[HULL_PIRATE]>0){
              bool top=(nv->hull[HULL_MERCHANT]>0);
              snprintf(buf[nb],140,"[convertir : %s]  la course %s",
                       top?"marchand → pirate":"pirate → marchand",
                       top?"(revenu déniable — la rancune s'armera)":"(désarmer apaise)");
              draw_text(ren,g_font_small,x+12,y,COL_PARCH,buf[nb]); nb++;
              sbhit_add((SDL_Rect){x+8,y-2,w-16,15}, SBH_NAVY_CONV, top?1:0, 0);
              zone_add((SDL_Rect){x+8,y-2,w-16,15},"Conversion AU CHANTIER, peu coûteuse, réversible : c'est sa nature. Le pirate niche en eaux mortes, pille les côtes (1/10), saigne les routes — et désigne son commanditaire s'il est pris.");
              y+=17;
          }
          if (nv->build_hull>=0){
              snprintf(buf[nb],140,"chantier : %s — %d j", navy_hull_name((HullType)nv->build_hull),(int)nv->build_days);
              draw_text(ren,g_font_small,x+12,y,COL_DIM,buf[nb]); nb++; y+=17;
          } else {
              static const HullType BB[3]={HULL_TRANSPORT,HULL_MERCHANT,HULL_WAR};
              for (int k=0;k<3;k++){
                  snprintf(buf[nb],140,"[chantier : %s]  %.0f or — fournitures navales + bois%s",
                           navy_hull_name(BB[k]), navy_build_gold(s->econ,port,BB[k]),
                           BB[k]==HULL_WAR?" + métal":"");
                  draw_text(ren,g_font_small,x+12,y,COL_PARCH,buf[nb]); nb++;
                  sbhit_add((SDL_Rect){x+8,y-2,w-16,15}, SBH_NAVY_BUILD, (int)BB[k], 0);
                  zone_add((SDL_Rect){x+8,y-2,w-16,15},"Commander une coque au chantier de la rade : la recette s'achète AU MARCHÉ (la Scierie navale a un débouché).");
                  y+=17;
              }
          }
          /* l'embarquement : armée au port + capacité + province visée côtière */
          const FieldArmy *fa2=&s->camp->army[me];
          if (fa2->active && fa2->phase==FA_IDLE && fa2->loc==port
              && selected>=0 && selected<world->n_provinces){
              int tr=world->province[selected].region;
              if (tr>=0 && tr<s->econ->n_regions && tr!=port && s->econ->region[tr].coastal){
                  float aller=navy_sea_days_regions(world,port,tr);
                  float retour=navy_sea_days_regions(world,tr,port);
                  long pk=campaign_units(s->camp,me);
                  int need=(int)((pk+9)/10);
                  if (aller>=0.f && navy_transport_packets_free(s->navy,me)>=(int)pk){
                      snprintf(buf[nb],140,"[embarquer] vers la région %d — %.0f j (retour %.0f j) · %d transport(s)",
                               tr, aller, retour, need);
                      draw_text(ren,g_font_small,x+12,y,COL_PARCH,buf[nb]); nb++;
                      sbhit_add((SDL_Rect){x+8,y-2,w-16,15}, SBH_SAIL, tr, 0);
                      zone_add((SDL_Rect){x+8,y-2,w-16,15},"Embarquer l'armée : charger (jours), traverser (l'ALLER ne vaut pas le RETOUR — la volta), débarquer (hors port : plus lent, exposé).");
                      y+=17;
                  } else if (aller>=0.f){
                      snprintf(buf[nb],140,"traversée %.0f j — transports insuffisants (%d requis)", aller, need);
                      draw_text(ren,g_font_small,x+12,y,COL_DIM,buf[nb]); nb++; y+=17;
                  }
              }
          }
      }
    }
    (void)h;
}

/* ── panneau FILTRES : chips → ViewMode existants + lentilles readout ──────── */
static void sb_panel_filtres(SDL_Renderer *ren, int x, int y, int w, int h, ViewMode mode){
    (void)w; (void)h;
    struct { const char *grp; struct { const char *lbl; int mode; } it[7]; int n; } G[3]={
        { "Souveraineté", { {"Politique",VIEW_POLITICAL},{"Pays",VIEW_COUNTRIES},{"Régions",VIEW_REGIONS},{"Continents",VIEW_CONTINENTS} }, 4 },
        { "Peuples",      { {"Culture",VIEW_CULTURE},{"Foi",VIEW_FAITH} }, 2 },
        { "Terre",        { {"Relief",VIEW_TERRAIN},{"Altitude",VIEW_HEIGHT},{"Fertilité",VIEW_FERTILITY},{"Humidité",VIEW_MOISTURE},{"Température",VIEW_TEMPERATURE},{"Ressources",VIEW_RESOURCES},{"Habitabilité",VIEW_HABITABILITY} }, 7 },
    };
    for (int g=0;g<3;g++){
        draw_text(ren,g_font_small,x+10,y,COL_DIM,G[g].grp); y+=15;
        int cx=x+12;
        for (int i=0;i<G[g].n;i++){
            if (cx>x+SB_DRAWER_W-90){ cx=x+12; y+=21; }
            bool actif=(g_sb.lens==LENS_NONE && mode==(ViewMode)G[g].it[i].mode);
            cx=sb_chip(ren,cx,y,G[g].it[i].lbl,actif,SBH_CHIP_MODE,G[g].it[i].mode,0,VIEW_NAMES[G[g].it[i].mode]);
        }
        y+=24;
    }
    { int cx=x+12;
      (void)sb_chip(ren,cx,y,"Courants", g_sb.show_currents, SBH_CHIP_CUR,0,0,
          "Le champ des COURANTS marins (physique du monde — flux continus) : couloirs, eaux vives ; les eaux MORTES se lisent en creux.");
    } y+=24;
    draw_text(ren,g_font_small,x+10,y,COL_DIM,"Empire (lentilles — par bande, 5 teintes)"); y+=15;
    { int cx=x+12;
      static const char *HV[3]={
        "Chaque province colorée par sa bande de PROSPÉRITÉ (misère → opulence).",
        "Où ça gronde : la bande d'HUMEUR locale (révoltée → dévouée).",
        "Où sont les PÉNURIES : la pire tension du panier (grain/outils/fer/étoffe/vin)." };
      cx=sb_chip(ren,cx,y,"Prospérité", g_sb.lens==LENS_PROSP,  SBH_CHIP_LENS, LENS_PROSP,0, HV[0]);
      cx=sb_chip(ren,cx,y,"Humeur",     g_sb.lens==LENS_HUMEUR, SBH_CHIP_LENS, LENS_HUMEUR,0,HV[1]);
      (void)sb_chip(ren,cx,y,"Marché",  g_sb.lens==LENS_MARCHE, SBH_CHIP_LENS, LENS_MARCHE,0,HV[2]);
    }
}

/* ── zone basse : LES LEVIERS (brief leviers §1) — gouverner d'où l'on observe ──
 * Toujours la même, quel que soit l'onglet. Intérieur (mater·former·purger — cible :
 * la province SÉLECTIONNÉE, à toi) + Couronne (embargo · contrats — cible : le pays
 * de la sélection). Chaque bouton : coût AVANT (survol), ordre en file/en jours.
 * La PURGE exige DEUX clics — un acte irréversible mérite deux temps. */
static void sb_panel_leviers(SDL_Renderer *ren, int x, int y, int w, Sim *s,
                             const World *world, int selected){
    fill_rect(ren,x+6,y-4,w-12,1,COL_PANEL2);
    draw_text(ren,g_font_small,x+10,y,COL_COPPER,"LEVIERS");
    int selreg=(selected>=0&&selected<world->n_provinces)?world->province[selected].region:-1;
    bool mine=(selreg>=0&&selreg<s->econ->n_regions&&s->econ->region[selreg].owner==s->player);
    int tgt=(selreg>=0&&selreg<s->econ->n_regions)?s->econ->region[selreg].owner:-1;
    bool foreign=(tgt>=0&&tgt!=s->player);
    /* rangée INTÉRIEUR */
    { int cx=x+10, ry=y+14;
      draw_text(ren,g_font_small,cx,ry,COL_DIM,"Intérieur:"); cx+=62;
      if (mine){
          cx=sb_chip(ren,cx,ry,"Mater",false,SBH_LEV_REPRESS,selreg,0,
              "MATER (30 j) : la botte — l'agitation se TAIT (le grief est masqué, il ressortira amplifié) ; l'Assise glisse vers Contrainte.");
          bool creuset = s->ts[s->player].unlocked[TECH_INTEGRATION];
          cx=sb_chip(ren,cx,ry,creuset?"Former (Creuset)":"Former",false,SBH_LEV_ASSIM,selreg,creuset?1:0,
              "FORMER (1 an) : écoles & magistrats — la minorité s'assimile plus vite. AVERTISSEMENT : cette culture est une source de savoir — la former, c'est TARIR le canal.");
          (void)sb_chip(ren,cx,ry, g_sb.purge_arm?"CONFIRMER LA PURGE ?":"Purger…",
              g_sb.purge_arm,SBH_LEV_PURGE,selreg,0,
              g_sb.purge_arm? "SECOND CLIC = la purge commence : ~12 % du groupe périra PAR AN pendant 4 ans · la stabilité vacillera des années · la Brèche se RAPPROCHERA."
                            : "PROCLAMER LA PURGE (4 ans, par tranches, arrêtable au prix du gâchis) : des familles périront · stabilité plombée · CHARGE faustienne. Deux clics.");
      }
    }
    /* rangée COURONNE */
    { int cx=x+10, ry=y+38;
      draw_text(ren,g_font_small,cx,ry,COL_DIM,"Couronne:"); cx+=62;
      cx=sb_chip(ren,cx,ry,"Embargo…",false,SBH_LEV_EMBARGO,0,0,
          "Ouvre Économie·Commerce : décréter/lever l'embargo par partenaire (le commerce perdu se lit AVANT).");
      if (foreign){
          float ws=diplo_war_score(s->dp, s->player, tgt);
          bool at_war=(diplo_status(s->dp,s->player,tgt)==DIPLO_WAR);
          bool winning=(at_war && ws>=15.f);
          bool free_v=(diplo_suzerain(s->dp,tgt)<0);
          if (winning && free_v){
              cx=sb_chip(ren,cx,ry,"Imposer: servage",false,SBH_LEV_CONTRAT,CONTRAT_SERVAGE,tgt,
                  "IMPOSER LE SERVAGE au vaincu : tribut LOURD + levées — mais la coercition et la fracture montent CHEZ TOI aussi (parenté servile) ; un serf complote.");
              cx=sb_chip(ren,cx,ry,"protectorat",false,SBH_LEV_CONTRAT,CONTRAT_PROTECTORAT,tgt,
                  "IMPOSER LE PROTECTORAT : tribut léger, un glacis — ses guerres t'APPELLERONT (un protecteur qui ne vient pas perd le contrat ET la face).");
          } else if (free_v && !at_war){
              if (world->country[tgt].role==POLITY_CITY_STATE)
                  cx=sb_chip(ren,cx,ry,"Lier la cité",false,SBH_LEV_CONTRAT,CONTRAT_CITE,tgt,
                      "CONTRAT DE CITÉ : route marchande GARANTIE (ni guerre ni embargo) — la cité reste LIBRE et inannexable tant que le contrat tient.");
              else {
                  int cr=sb_capital_region(s,world);
                  int tr=(world->country[tgt].capital_prov>=0)?world->province[world->country[tgt].capital_prov].region:-1;
                  bool same_credo=(cr>=0&&tr>=0&&tr<s->econ->n_regions&&cr<s->econ->n_regions
                                   && s->econ->region[cr].culture.credo==s->econ->region[tr].culture.credo);
                  if (same_credo)
                      cx=sb_chip(ren,cx,ry,"Concordat",false,SBH_LEV_CONTRAT,CONTRAT_CONCORDAT,tgt,
                          "CONCORDAT (co-religionnaires) : légitimité partagée, canal de foi — pas d'or ; l'hérésie de l'un devient le souci de l'autre.");
              }
          }
          (void)cx;
      }
    }
}

/* ── le RAIL + le TIROIR ───────────────────────────────────────────────────── */
static void draw_sidebar(SDL_Renderer *ren, int win_w, int win_h, Sim *s, World *world,
                         ViewMode mode, int selected){
    (void)win_w;
    sbhit_reset();
    if (s->ready) sb_cache_refresh(s, s->day);                /* agrégats : au pas de jour (§8) */
    /* glissement (~150 ms, dt interne — l'overlay est redessiné chaque frame) */
    { static Uint32 sb_last=0; Uint32 now=SDL_GetTicks();
      float dtf = sb_last ? (float)(now-sb_last)/1000.f : 0.016f;
      if (dtf>0.1f) dtf=0.1f;
      sb_last=now;
      float target=(g_sb.tab>=0)?1.f:0.f;
      if (g_sb.anim<target) { g_sb.anim+=dtf/0.15f; if(g_sb.anim>1.f)g_sb.anim=1.f; }
      if (g_sb.anim>target) { g_sb.anim-=dtf/0.15f; if(g_sb.anim<0.f)g_sb.anim=0.f; }
    }
    int dw=(int)(SB_DRAWER_W*g_sb.anim);
    /* tiroir — il ÉPOUSE son contenu (borné), commence SOUS la topbar et s'arrête
     * AU-DESSUS de la rangée de chips : aucun calque ne recouvre l'autre. */
    if (dw>2 && s->ready){
        int dx=SB_RAIL_W, dy=70;
        int content=120;                              /* mesure grossière par onglet */
        switch(g_sb.tab){
            case SBT_STOCKS:{ int nn=0; for(int g=1;g<RES_COUNT;g++) if (sb_good_alive(g)) nn++;
                              content=20+nn*15+8; } break;
            case SBT_ECO:{ int nn=0;
                if (g_sb.eco_sub==1){ for(int g=1;g<RES_COUNT;g++) if (g_sbc.dem[g]>0.05f||g_sbc.sup[g]>0.05f) nn++; }
                else if (g_sb.eco_sub==2){ for(int g=1;g<RES_COUNT;g++) if (intertrade_import_vol(s->player,g)>0.05f||intertrade_export_vol(s->player,g)>0.05f) nn++; }
                else { for(int c=0;c<world->n_countries;c++){ if(c==s->player) continue;
                        if (intertrade_pair_value(s->player,c)>0.5f||intertrade_embargoed(s->player,c)
                            ||diplo_status(s->dp,s->player,c)==DIPLO_WAR) nn++; } if(nn>10)nn=10; nn+=2; }
                content=24+20+nn*16+8; } break;
            case SBT_DEMO: content=22+3*15+8+16+15+6*15+24; break;
            case SBT_ARMEE: content=22+16+24+6*16+3*17+8; break;
            case SBT_FILTRES: content=3*40+15+24+30; break;
            default: break;
        }
        int dh=32+content+96;                          /* titre + contenu + zone Leviers */
        int dhmax=win_h-dy-52;                         /* au-dessus des chips de mode */
        if (dh>dhmax) dh=dhmax;
        if (dh<200)   dh=200;
        panel_bg(ren,dx,dy,dw,dh);
        if (g_sb.anim>0.95f){
            static const char *TITRE[SBT_COUNT]={"ÉCONOMIE","DÉMOGRAPHIE","STOCKS","ARMÉE","FILTRES DE CARTE"};
            if (g_sb.tab>=0&&g_sb.tab<SBT_COUNT)
                draw_text(ren,g_font,dx+10,dy+8,COL_COPPER,TITRE[g_sb.tab]);
            int py=dy+32, ph=dy+dh-96;          /* on réserve la zone basse aux LEVIERS */
            switch(g_sb.tab){
                case SBT_ECO:     sb_panel_eco   (ren,dx,py,dw,ph,s,world); break;
                case SBT_DEMO:    sb_panel_demo  (ren,dx,py,dw,ph,s,world); break;
                case SBT_STOCKS:  sb_panel_stocks(ren,dx,py,dw,ph,s,world); break;
                case SBT_ARMEE:   sb_panel_armee (ren,dx,py,dw,ph,s,world,selected); break;
                case SBT_FILTRES: sb_panel_filtres(ren,dx,py,dw,ph,mode); break;
                default: break;
            }
            sb_panel_leviers(ren, dx, dy+dh-92, dw, s, world, selected);   /* zone basse : toujours là */
        }
    }
    /* rail (toujours visible, par-dessus) */
    fill_rect(ren,0,0,SB_RAIL_W,win_h,(SDL_Color){0x0c,0x12,0x1d,0xfa});
    fill_rect(ren,SB_RAIL_W-1,0,1,win_h,COL_PANEL2);
    static const char *IC[SBT_COUNT] ={"E","D","S","A","F"};
    static const char *NM[SBT_COUNT] ={"Économie (E) — commerce, marché, import/export",
                                       "Démographie (D) — classes, peuples, relocalisation",
                                       "Stocks (S) — tensions, couverture, exploiter",
                                       "Armée (A) — levée, campagne, posture",
                                       "Filtres (F) — vues de carte & lentilles d'empire"};
    for (int t=0;t<SBT_COUNT;t++){
        int by=52+t*44;
        bool actif=(g_sb.tab==t);
        if (actif) fill_rect(ren,0,by-6,SB_RAIL_W,32,(SDL_Color){0x2a,0x20,0x14,0xff});
        draw_text(ren,g_font_big,14,by,actif?COL_COPPER:COL_DIM,IC[t]);
        sbhit_add((SDL_Rect){0,by-6,SB_RAIL_W,32}, SBH_TAB, t, 0);
        zone_add((SDL_Rect){0,by-6,SB_RAIL_W,32}, NM[t]);
    }
}

/* clic dans la sidebar — exécute la DÉCISION par l'actionneur ; true si consommé */
static bool sidebar_click(Sim *s, World *world, int mx, int my, ViewMode *mode, bool *dirty){
    int hit=-1;
    for (int i=0;i<g_nsbhits;i++){ SDL_Rect *r=&g_sbhits[i].r;
        if (mx>=r->x&&mx<r->x+r->w&&my>=r->y&&my<r->y+r->h){ hit=i; break; } }
    if (hit<0){
        int dw=(int)(SB_DRAWER_W*g_sb.anim);
        return (mx < SB_RAIL_W + ((g_sb.tab>=0)?dw:0));   /* le tiroir absorbe le clic perdu */
    }
    SbHit *hh=&g_sbhits[hit]; *dirty=true;
    if (hh->kind!=SBH_LEV_PURGE) g_sb.purge_arm=false;   /* tout autre clic DÉSARME la purge */
    switch(hh->kind){
        case SBH_TAB:      g_sb.tab=(g_sb.tab==hh->a)?-1:hh->a; break;
        case SBH_ECOSUB:   g_sb.eco_sub=hh->a; break;
        case SBH_EMBARGO:
            intertrade_order_embargo(s->player, hh->a, hh->b!=0);
            printf("\n[scps] %s contre %s — le négoce %s.\n",
                   hh->b? "EMBARGO décrété":"Embargo LEVÉ", sb_country_name(world,hh->a),
                   hh->b? "se ferme":"peut reprendre");
            break;
        case SBH_RELOC_SRC: g_sb.reloc_src=hh->a; break;
        case SBH_RELOC_CLR: g_sb.reloc_src=-1; break;
        case SBH_RELOC_DST:
            if (g_sb.reloc_src>=0 && agency_order_relocate(s->ag, g_sb.reloc_src, hh->a))
                printf("\n[scps] Convoi ordonné : ~%d familles, région %d → %d (90 j) — la coercition montera à la source.\n",
                       AGY_RELOC_POP, g_sb.reloc_src, hh->a);
            g_sb.reloc_src=-1;
            break;
        case SBH_EXPLOIT:
            if (agency_order_exploit(s->ag, hh->a, (Resource)hh->b))
                printf("\n[scps] Exploitation en file : %s, région %d (en jours).\n",
                       resource_name((Resource)hh->b), hh->a);
            break;
        case SBH_LEVY:
            warhost_set_levy(s->host, s->player, hh->a);
            printf("\n[scps] Levée : %s.\n", warhost_levy_name(hh->a));
            break;
        case SBH_POSTURE:  campaign_set_posture(s->camp, s->player, hh->a); break;
        case SBH_REFILL: {
            int got=campaign_refill(s->camp, s->player, s->labor);
            printf("\n[scps] Renfort : %d paquet(s) levés en territoire ami.\n", got);
        } break;
        case SBH_MARCH: {
            const FieldArmy *fa=&s->camp->army[s->player];
            if (campaign_order(s->camp, s->econ, s->player, fa->loc, hh->a, &fa->force))
                printf("\n[scps] L'armée marche sur la région %d (itinéraire en jours).\n", hh->a);
            else if (campaign_order_sea(s->camp, world, s->econ, s->navy, s->player, fa->loc, hh->a, &fa->force))
                printf("\n[scps] La terre ne mène pas là : l'armée EMBARQUE pour la région %d.\n", hh->a);
        } break;
        case SBH_SAIL: {
            const FieldArmy *fa=&s->camp->army[s->player];
            if (campaign_order_sea(s->camp, world, s->econ, s->navy, s->player, fa->loc, hh->a, &fa->force))
                printf("\n[scps] L'armée embarque pour la région %d (la volta décidera des jours).\n", hh->a);
        } break;
        case SBH_NAVY_BUILD:
            if (navy_order_build(s->navy, world, s->econ, s->player, (HullType)hh->a))
                printf("\n[scps] Chantier naval : %s en construction.\n", navy_hull_name((HullType)hh->a));
            break;
        case SBH_NAVY_CONV:
            if (navy_convert(s->navy, world, s->econ, s->player, hh->a!=0))
                printf("\n[scps] Chantier : %s.\n", hh->a?"un marchand passe à la COURSE":"un pirate rentre dans le rang");
            break;
        case SBH_MUSTER:
            if (hh->a>=0 && campaign_order(s->camp, s->econ, s->player, hh->a, hh->a, &s->host->army[s->player]))
                printf("\n[scps] L'ost se rassemble au camp de la capitale (région %d).\n", hh->a);
            break;
        case SBH_CANCEL:   agency_cancel(s->ag, hh->a); break;
        case SBH_LEV_REPRESS:
            g_sb.purge_arm=false;
            if (agency_order_repress(s->ag, hh->a))
                printf("\n[scps] MATER : la garnison marche sur la région %d (30 j) — l'agitation se taira ; le grief, lui, attendra.\n", hh->a);
            break;
        case SBH_LEV_ASSIM:
            g_sb.purge_arm=false;
            if (agency_order_assimilate(s->ag, hh->a, hh->b!=0))
                printf("\n[scps] FORMER : écoles et magistrats en région %d (1 an%s) — une source de savoir va se tarir.\n",
                       hh->a, hh->b?", Creuset":"");
            break;
        case SBH_LEV_PURGE:
            if (!g_sb.purge_arm){ g_sb.purge_arm=true; }       /* 1er clic : ARME */
            else {
                g_sb.purge_arm=false;
                if (agency_order_purge(s->ag, hh->a))
                    printf("\n[scps] LA PURGE EST PROCLAMÉE (région %d) : 4 ans de tranches — des familles périront, la Brèche se rapproche.\n", hh->a);
            }
            break;
        case SBH_LEV_EMBARGO: g_sb.purge_arm=false; g_sb.tab=SBT_ECO; g_sb.eco_sub=0; break;
        case SBH_LEV_CONTRAT:
            g_sb.purge_arm=false;
            diplo_set_vassal(s->dp, s->player, hh->b, (SuzContrat)hh->a);
            printf("\n[scps] CONTRAT : %s — %s passe sous ta couronne (le lien tiendra tant que ta force le tient).\n",
                   diplo_contrat_name((SuzContrat)hh->a), sb_country_name(world,hh->b));
            break;
        case SBH_CHIP_MODE: *mode=(ViewMode)hh->a; g_sb.lens=LENS_NONE; break;
        case SBH_CHIP_LENS: g_sb.lens=(g_sb.lens==(MapLens)hh->a)?LENS_NONE:(MapLens)hh->a; break;
        case SBH_CHIP_CUR:  g_sb.show_currents=!g_sb.show_currents; break;
        default: break;
    }
    return true;
}
/* molette sur le tiroir → défilement du panneau (sinon : zoom carte) */
static bool sidebar_wheel(int mx, int my, int wheel_y){
    (void)my;
    if (g_sb.tab<0) return false;
    int dw=(int)(SB_DRAWER_W*g_sb.anim);
    if (mx >= SB_RAIL_W+dw) return false;
    int t=g_sb.tab; g_sb.scroll[t]-= wheel_y*3;
    if (g_sb.scroll[t]<0) g_sb.scroll[t]=0;
    if (g_sb.scroll[t]>RES_COUNT) g_sb.scroll[t]=RES_COUNT;
    return true;
}

static void draw_province_panel(SDL_Renderer *ren, int win_w, int win_h,
                                const World *w, const WorldEconomy *econ,
                                const WorldProsperity *wp, const WorldLegitimacy *wl,
                                const ModifierStack *drift, int pid) {
    ProvinceReadout p = province_readout(w, econ, wp, wl, pid);
    (void)win_w;
    int pw=312, px=0, py=102, ph=win_h-py-26;   /* à GAUCHE, sous le bandeau + alertes (§7) */
    panel_bg(ren, px,py, pw,ph);
    fill_rect(ren, px+pw-2,py, 2,ph, COL_COPPER);   /* liseré cuivre sur le bord intérieur (droite) */
    int x=px+16, y=py+14, rw=pw-30;
    char line[192];
    bool restive=false;     /* une minorité frondeuse présente → chemins H / Intégrer */

    /* EN-TÊTE : place d'héraldique réservée · nom · climat·relief·taille · jauge
     * de prospérité (rouge→vert, chiffrée, calée en haut à droite). */
    int hsz=30;
    draw_box(ren, x, y+2, hsz, hsz, COL_COPPER);
    fill_rect(ren, x+1, y+3, hsz-2, hsz-2, COL_PANEL2);
    zone_add((SDL_Rect){x,y+2,hsz,hsz}, "Place réservée à l'héraldique du royaume (à venir).");
    draw_text(ren, g_font_big, x+hsz+8, y, COL_COPPER, p.nom);
    {   /* la jauge de prospérité, en haut à droite, avec son chiffre. */
        int gw=64, gh=10, gx=px+pw-16-gw, gy=y+4;
        draw_gauge(ren, gx, gy, gw, gh, p.m_aisance.value);
        char nb[8]; snprintf(nb,sizeof nb,"%d", p.m_aisance.value);
        int nbw=text_w(g_font, nb);
        draw_text(ren, g_font, gx-nbw-6, y, COL_PARCH, nb);
        zone_add((SDL_Rect){gx-nbw-8, y-2, gw+nbw+12, gh+8},
                 "Prospérité de la province (0-100) : l'aisance matérielle, du dénuement au faste — "
                 "tirée par la production, le commerce et la paix.");
    }
    snprintf(line,sizeof line, "%s · %s · %s", p.climat, p.relief, capitale_status(capitale_max_tier(p.ames)));
    draw_text(ren, g_font, x+hsz+8, y+18, COL_PARCH, line);
    zone_add((SDL_Rect){x+hsz+6, y+16, rw-hsz-6, 18},
             "Climat · relief · TAILLE (le statut vient du tier de la capitale : Hameau → Métropole).");
    y += hsz + 8;
    /* HABITANTS — un nombre, rien de plus (le détail va aux camemberts). */
    snprintf(line,sizeof line, "%ld habitants", p.ames);
    draw_text(ren, g_font, x, y, COL_PARCH, line);
    zone_add((SDL_Rect){x-2,y-2,rw,19}, "Le nombre total d'habitants de la province."); y += 22;
    { int breg=(pid>=0&&pid<w->n_provinces)?w->province[pid].region:-1;   /* coques §7 : la balafre SE VOIT */
      if (breg>=0 && breg<econ->n_regions && econ->region[breg].balafre_days>0.f){
          char bal[64];
          snprintf(bal,sizeof bal,"côte balafrée — pillée il y a %d mois",
                   (int)((365.f-econ->region[breg].balafre_days)/30.f));
          draw_text(ren, g_font_small, x, y, COL_COPPER, bal);
          zone_add((SDL_Rect){x-2,y-2,rw,17}, "Des pirates ont pillé cette côte (1/10 des stocks) : production entaillée ~1 an ; la province est immunisée ~5 ans (la course ne trait pas deux fois la même vache).");
          y += 18;
      } }

    /* CAMEMBERTS — Culture + Religion côte à côte (la race SUIT la culture :
     * pas de 3ᵉ disque). Surface sobre ; le détail vit dans le survol. */
    {
        int reg = (pid>=0 && pid<w->n_provinces) ? w->province[pid].region : -1;
        if (reg>=0 && reg<econ->n_regions && econ->region[reg].pop.n_groups>0) {
            int owner = econ->region[reg].owner;
            const PopCulture *crown = &econ->region[reg].culture;     /* repli */
            if (owner>=0 && owner<w->n_countries) {
                int cp=w->country[owner].capital_prov;
                if (cp>=0 && cp<w->n_provinces) { int cr=w->province[cp].region;
                    if (cr>=0 && cr<econ->n_regions) crown=&econ->region[cr].culture; }
            }
            GroupReadout gr[SCPS_MAX_GROUPS];
            int ng = province_composition(&econ->region[reg].pop, drift, crown, 5.f, 5.f,
                                          gr, SCPS_MAX_GROUPS);
            /* parts de CULTURE : un secteur par groupe (la race le suit, en survol). */
            int cper[SCPS_MAX_GROUPS]={0}; SDL_Color ccol[SCPS_MAX_GROUPS]={{0}};
            for (int i=0;i<ng;i++){ cper[i]=gr[i].percent; ccol[i]=SLICE_PAL[i&7];
                if (i>0 && gr[i].loyaute<=HU_FRONDEUSE) restive=true; }
            /* parts de RELIGION : agrégées par confession. */
            const char *rnm[SCPS_MAX_GROUPS]={0}; int rper[SCPS_MAX_GROUPS]={0}; SDL_Color rcol[SCPS_MAX_GROUPS]={{0}}; int nr=0;
            for (int i=0;i<ng;i++){
                int f=-1; for (int j=0;j<nr;j++) if (!strcmp(rnm[j],gr[i].religion)){ f=j; break; }
                if (f<0){ rnm[nr]=gr[i].religion; rper[nr]=gr[i].percent; rcol[nr]=SLICE_PAL[nr&7]; nr++; }
                else rper[f]+=gr[i].percent;
            }
            int pr_=22, cyc=y+pr_+4, cx1=x+pr_+6, cx2=x+rw/2+pr_+2;
            draw_pie(ren, cx1, cyc, pr_, cper, ccol, ng);
            draw_pie(ren, cx2, cyc, pr_, rper, rcol, nr);
            draw_text(ren, g_font_small, cx1-pr_, cyc+pr_+3, COL_DIM, "Culture");
            draw_text(ren, g_font_small, cx2-pr_, cyc+pr_+3, COL_DIM, "Religion");
            /* survols : les compositions détaillées (mots, pas un flottant SCPS). */
            static char chov[320], rhov[320]; int cn=0, rn2=0;
            cn += snprintf(chov+cn, sizeof chov-cn, "Culture : ");
            for (int i=0;i<ng && cn<(int)sizeof chov-48;i++)
                cn += snprintf(chov+cn, sizeof chov-cn, "%s%d%% %s (%s — %s)",
                               i?" · ":"", gr[i].percent, gr[i].culture, gr[i].race, label_humeur(gr[i].loyaute));
            rn2 += snprintf(rhov+rn2, sizeof rhov-rn2, "Religion · culte du trône : %s — ",
                            religion_branch_name(crown->rel_branch));
            for (int i=0;i<nr && rn2<(int)sizeof rhov-40;i++)
                rn2 += snprintf(rhov+rn2, sizeof rhov-rn2, "%s%d%% %s", i?" · ":"", rper[i], rnm[i]);
            zone_add((SDL_Rect){cx1-pr_,cyc-pr_,2*pr_+4,2*pr_+14}, chov);
            zone_add((SDL_Rect){cx2-pr_,cyc-pr_,2*pr_+4,2*pr_+14}, rhov);
            y = cyc + pr_ + 16;
        } else {
            ui_section(ren, x, &y, "PEUPLE");
            ui_row(ren,x,&y,rw,"Race", p.race, COL_PARCH,
                   "L'espèce de la population.");
        }
    }

    /* HUMEUR — une rangée de VISAGES (triste→content), le courant allumé, + le chiffre. */
    {
        ui_section(ren, x, &y, "HUMEUR");
        int nf=5, fr=9, gap=8, fy=y+fr;
        float moodv = p.m_humeur.value/100.f;
        int lit = (int)(moodv*(nf-1)+0.5f);
        for (int i=0;i<nf;i++)
            draw_face(ren, x+fr + i*(2*fr+gap), fy, fr, (float)i/(nf-1), i==lit);
        char nb[16]; snprintf(nb,sizeof nb,"%d", p.m_humeur.value);
        draw_text(ren, g_font, x + nf*(2*fr+gap) + 6, y, sense_color(moodv), nb);
        static char hh[200];
        snprintf(hh,sizeof hh,
                 "Humeur %d/100 — l'allégeance ressentie (légitimité). Agitation %d/100 : ce qui la mine "
                 "(légitimité basse, coercition, tension de diversité).", p.m_humeur.value, p.agitation.value);
        zone_add((SDL_Rect){x-2,y-2,rw,2*fr+4}, hh);
        y = fy + fr + 8;
    }

    /* POPULATION — barre EMPILÉE des classes + nom + chiffre ; le qui-fait-quoi et
     * le penchant de faction vont au SURVOL (rien de plus en surface). */
    {
        int reg = (pid>=0 && pid<w->n_provinces) ? w->province[pid].region : -1;
        if (reg>=0 && reg<econ->n_regions) {
            ui_section(ren, x, &y, "POPULATION");
            const RegionEconomy *re2=&econ->region[reg];
            /* la composition de classe ÉMERGE des groupes (§pop précise) : Σ des
             * pop_by_class de chaque groupe race×culture×foi. Repli sur les strates. */
            long cp[3] = {0,0,0};
            const ProvincePop *pp2=&re2->pop;
            if (pp2->n_groups>0){
                for (int gi=0; gi<pp2->n_groups; gi++)
                    for (int cc=0; cc<3; cc++) cp[cc]+=pp2->groups[gi].pop_by_class[cc];
            } else {
                cp[0]=(long)re2->strata[CLASS_LABORER].pop;
                cp[1]=(long)re2->strata[CLASS_BOURGEOIS].pop;
                cp[2]=(long)re2->strata[CLASS_ELITE].pop;
            }
            long tot=cp[0]+cp[1]+cp[2]; if(tot<1) tot=1;
            const SDL_Color cc[3]={ SLICE_PAL[0], SLICE_PAL[1], SLICE_PAL[3] };
            const char *chov[3]={
                "Laboureurs — aux terres et à l'armée. Penchant de faction : Communautaire.",
                "Artisans (bourgeois) — aux ateliers. Penchant de faction : Marchand.",
                "Noblesse — officiels et armée. Penchant de faction : Conquérant · Légiste." };
            /* la barre empilée */
            int bh=12, acc=0;
            for (int i=0;i<3;i++){
                int segw = (i==2) ? (rw-acc) : (int)((float)cp[i]/tot*rw);
                if (segw<0) segw=0;
                fill_rect(ren, x+acc, y, segw, bh, cc[i]);
                zone_add((SDL_Rect){x+acc,y,segw,bh}, chov[i]);
                acc += segw;
            }
            draw_box(ren, x, y, rw, bh, COL_DIM);
            y += bh+5;
            /* la légende : pastille + nom + chiffre, par classe. */
            for (int i=0;i<3;i++){
                fill_rect(ren, x, y+3, 9,9, cc[i]);
                char l[64]; snprintf(l,sizeof l, "%s %ld", labor_class_word((SocialClass)i), cp[i]);
                draw_text(ren, g_font, x+16, y, COL_PARCH, l);
                zone_add((SDL_Rect){x-2,y-2,rw,18}, chov[i]);
                y += 18;
            }
        }
    }

    /* RESSOURCES + REVENUS — la province produit, en flux JOURNALIER (+N/j). */
    IncomeReadout inc = province_income(econ, (pid>=0&&pid<w->n_provinces)?w->province[pid].region:-1);
    {
        ui_section(ren, x, &y, "RESSOURCES");
        char res[96]; int rn=0, shown=0; res[0]=0;
        for (int i=0;i<inc.n && shown<2;i++){
            if (inc.line[i].manufactured) continue;                    /* les brutes locales en tête */
            rn += snprintf(res+rn, sizeof res-rn, "%s%s", shown?" · ":"", inc.line[i].source);
            shown++;
        }
        if (shown==0) snprintf(res,sizeof res, "%s", p.ressource);      /* repli : la ressource géo */
        draw_text(ren, g_font, x, y, COL_PARCH, res);
        char rhov[160]; snprintf(rhov,sizeof rhov,
                 "Les biens extraits sur place (vocation : %s). Les quantités produites sont dans Production.", p.vocation);
        zone_add((SDL_Rect){x-2,y-2,rw,19}, rhov); y += 22;
    }
    {
        ui_section(ren, x, &y, "PRODUCTION");
        for (int i=0;i<inc.n;i++){
            char l[24]; snprintf(l,sizeof l, "+%.1f/j", inc.line[i].per_day);
            draw_text(ren, g_font, x, y, sense_color(0.62f), l);
            draw_text(ren, g_font, x+74, y, COL_DIM, inc.line[i].source);
            char hv[176]; snprintf(hv,sizeof hv,
                     "%s · %s : +%.1f unité(s)/jour. C'est l'income en RESSOURCE (en or si c'est de l'or) ; "
                     "la VENTE (→ or par le commerce) est une autre histoire.",
                     inc.line[i].manufactured?"Sortie d'atelier (bourgeois)":"Collecte (laboureurs)",
                     inc.line[i].source, inc.line[i].per_day);
            zone_add((SDL_Rect){x-2,y-2,rw,18}, hv); y += 18;
        }
        if (inc.n==0){ draw_text(ren, g_font, x, y, COL_DIM, "rien de notable"); y += 18; }
        y += 4;
    }

    /* Le seuil de révolte reste signalé (gameplay) ; lignée/foi vivent dans les
     * camemberts, l'agitation dans le survol de l'humeur — surface non dense. */
    if (p.seuil_revolte) {
        snprintf(line,sizeof line, "⚑ Au bord de la révolte (agitation %d)", p.agitation.value);
        draw_text(ren, g_font, x, y, sense_color(0.06f), line);
        zone_add((SDL_Rect){x-2,y-2,rw,19},
                 "L'agitation a franchi le seuil : maintenue, elle vire à la révolte ouverte. "
                 "Lignée, foi et moteurs d'humeur : voir les camemberts et le survol des visages.");
        y += 22;
    }

    /* CAPITALE — l'ossature administrative (§capitale) : son TIER (que la pop débloque)
     * donne le statut ; les Nobles en poste (paquets de 100) délivrent logement +
     * services + productivité. Calculée de l'éco (pop + Nobles). */
    {
        int reg = (pid>=0 && pid<w->n_provinces) ? w->province[pid].region : -1;
        if (reg>=0 && reg<econ->n_regions) {
            long pop = p.ames;
            int  tier  = capitale_max_tier(pop);
            /* Nobles à l'administration : la mobilité ÉMERGENTE en promeut autant que
             * l'admin en réclame (tier·100), borné par la pop disponible (paquets de 100). */
            long admin = capitale_admin_pop(tier); if (admin > pop) admin = (pop/100)*100;
            long house = capitale_housing(tier, admin);
            long serv  = capitale_housing(tier, admin);
            int  prodp = (int)((capitale_prodmult(tier, admin)-1.f)*100.f + 0.5f);
            ui_section(ren, x, &y, "CAPITALE");
            char l[96];
            snprintf(l,sizeof l, "%s · tier %d", capitale_status(tier), tier);
            ui_row(ren,x,&y,rw,"Statut", l, COL_COPPER,
                   "L'ossature administrative. La pop DÉBLOQUE le tier, une recette de plus en plus précieuse le PAIE ; le tier nomme la taille.");
            long libres = house - pop;
            if (libres>=0) snprintf(l,sizeof l, "%ld libres / %ld", libres, house);
            else           snprintf(l,sizeof l, "complet · manque %ld → agitation", -libres);
            ui_row(ren,x,&y,rw,"Logement", l, libres>=0?COL_PARCH:sense_color(0.12f),
                   "Le LOGEMENT délivré par la capitale (au prorata des paquets de Nobles en poste). Surpeuplé = grogne.");
            snprintf(l,sizeof l, "%ld", serv);
            ui_row(ren,x,&y,rw,"Services", l, serv>=pop?COL_PARCH:sense_color(0.30f),
                   "Les SERVICES délivrés ; sous-équipé, le contentement baisse.");
            snprintf(l,sizeof l, "+%d %%", prodp);
            ui_row(ren,x,&y,rw,"Productivité", l, prodp>0?sense_color(0.75f):COL_DIM,
                   "La PRODUCTIVITÉ que la capitale ajoute à la collecte (+5 % par tier servi par un paquet de Nobles).");
        }
    }

    /* BÂTIMENTS — une grille 6 + 2 : 6 emplacements ordinaires + 2 SPÉCIAUX
     * (optimisation · défense), visuellement distincts (liseré cuivre). Survol =
     * l'effet (si bâti) ou ce qu'on peut y bâtir. PAS de bouton « Bâtir » : on
     * clique un slot (vide → bâtir ; plein → améliorer/remplacer). */
    ui_section(ren, x, &y, "BÂTIMENTS");
    {
        int reg = (pid>=0 && pid<w->n_provinces) ? w->province[pid].region : -1;
        ProvBuild b; memset(&b,0,sizeof b);
        int nbld=0;
        if (reg>=0 && reg<econ->n_regions){ b=econ->region[reg].build; nbld=econ->region[reg].n_bld; }
        bool opt_built = (nbld>0);
        struct { const char *name,*abbr,*eff,*todo; float lvl; bool special; int edi; } S[8] = {
            {"Ordre","Or",  "monte la capacité du royaume (K)",       "Tribunal : ordre & administration",            b.K_inst,  false, EDI_TRIBUNAL},
            {"Vivres","Vi", "loge et nourrit la croissance",         "Grenier : nourrir & loger",                    b.food_cap,false, EDI_GRENIER},
            {"Foi","Fo",    "apaise l'agitation, soutient la légitimité","Temple : apaiser & légitimer",              b.faith,   false, EDI_TEMPLE},
            {"Savoir","Sa", "accélère la recherche locale",          "Bibliothèque : hâter le savoir",               b.savoir,  false, EDI_BIBLIOTHEQUE},
            {"Marché","Ma", "capte la prospérité locale (PE)",       "Marché : capter la prospérité",                b.PE_infra,false, EDI_MARCHE},
            {"Ouvert.","Ov","perméabilité aux échanges",             "Port : perméer aux échanges",                  b.P_open,  false, EDI_PORT},
            {"Optim.","Op", p.specialisation,                        "Entrepôt : optimiser la production locale",    opt_built?1.f:0.f, true, EDI_ENTREPOT},
            {"Défense","Df",p.defense,                               "fortifier : garnison → forteresse → citadelle",b.H_coerc, true, EDI_GARNISON},
        };
        int sw=30, sg=(rw-4*sw)/3, sy0=y;
        static char bhov[8][192];
        for (int i=0;i<8;i++){
            int col=i%4, row=i/4, sx=x+col*(sw+sg), sy=sy0+row*(sw+8);
            bool built = S[i].lvl > 0.3f;
            SDL_Color fc = S[i].special ? COL_COPPER : SLICE_PAL[i%6];
            fill_round(ren, sx, sy, sw, sw, built ? fc : COL_PANEL2, 5);
            round_box (ren, sx, sy, sw, sw, COL_EDGE, 5);
            if (S[i].special){ round_box(ren, sx-1, sy-1, sw+2, sw+2, COL_COPPER, 6);  /* liseré cuivre… */
                               round_box(ren, sx-2, sy-2, sw+4, sw+4, COL_COPPER, 7); } /* …épaissi vers l'extérieur */
            int aw=text_w(g_font_small, S[i].abbr);
            draw_text(ren, g_font_small, sx+(sw-aw)/2, sy+sw/2-7, built?COL_PANEL:COL_DIM, S[i].abbr);
            if (built) snprintf(bhov[i],sizeof bhov[i],
                       "%s — bâti : %s. Clic : améliorer ou remplacer.", S[i].name, S[i].eff?S[i].eff:"—");
            else       snprintf(bhov[i],sizeof bhov[i],
                       "%s — vide. À bâtir : %s. Clic : bâtir (payé au marché, construit en jours).", S[i].name, S[i].todo);
            zone_add((SDL_Rect){sx,sy,sw,sw}, bhov[i]);
            if (reg>=0) bslot_add((SDL_Rect){sx,sy,sw,sw}, reg, S[i].edi);   /* cliquable */
        }
        y = sy0 + 2*(sw+8) + 4;
    }

    if (restive) {
        ui_section(ren, x, &y, "ACTIONS");
        draw_text(ren, g_font, x, y, sense_color(0.30f), "▸ Réprimer (la poigne)");
        zone_add((SDL_Rect){x-2,y-2,rw,19},
                 "Réprimer : calme immédiat de l'agitation — MAIS la légitimité du groupe est rongée "
                 "et la fragilité monte. Réversible : la révolte resurgit si la botte se lève (rien n'est métabolisé).");
        y += 20;
        draw_text(ren, g_font, x, y, sense_color(0.75f), "▸ Intégrer (la patience)");
        zone_add((SDL_Rect){x-2,y-2,rw,19},
                 "Intégrer : métabolise lentement (capacité + ouverture + légitimité + temps) — durable et vrai, "
                 "mais long (∝ la distance culturelle : le gouffre prend des générations).");
        y += 20;
    }
}

/* ===================================================================== */
/* L'OUTLINER — « ce que je possède », groupé par URBANISATION (§6)        */
/* ===================================================================== */
/* L'urbanisation vient du TIER de la capitale (§capitale) : on groupe l'outliner
 * par tier (statut), pas par terrain ni par pop brute. */
static long region_pop_of(const RegionEconomy *re){
    return (long)(re->strata[CLASS_LABORER].pop + re->strata[CLASS_BOURGEOIS].pop + re->strata[CLASS_ELITE].pop);
}
/* Un emplacement institutionnel encore LIBRE (un axe ProvBuild vide) ? Avec un
 * bâtiment de base toujours posable, c'est la condition du MARTEAU (§6b). */
static bool region_free_slot(const RegionEconomy *re){
    const ProvBuild *b=&re->build;
    return b->K_inst<0.3f||b->food_cap<0.3f||b->faith<0.3f||b->savoir<0.3f||b->PE_infra<0.3f||b->P_open<0.3f;
}
static char g_ohov[80][200];
static void draw_outliner(SDL_Renderer *ren, int win_w, int win_h, const Sim *s, const World *w){
    int player=s->player; if (player<0) return;
    int pw=234, px=win_w-pw, py=102, ph=win_h-py-26;
    panel_bg(ren, px,py, pw,ph);
    fill_rect(ren, px,py, 2,ph, COL_COPPER);          /* liseré cuivre sur le bord intérieur (gauche) */
    TTF_Font *fs = g_font_small?g_font_small:g_font;
    int x=px+12, y=py+10, rw=pw-22, bottom=py+ph-4, oi=0;
    draw_text(ren, g_font, x, y, COL_COPPER, "Domaine"); y+=20;
    /* régions groupées par URBANISATION = le TIER de la capitale (grand → petit). */
    for (int tier=7; tier>=1 && y<bottom-16; tier--){
        int cnt=0;
        for (int r=0;r<s->econ->n_regions;r++){
            const RegionEconomy *re=&s->econ->region[r];
            if (re->owner!=player) continue;
            if (capitale_max_tier(region_pop_of(re))==tier) cnt++;
        }
        if (!cnt) continue;
        const char *st=capitale_status(tier); size_t sl=strlen(st);   /* pluriel FR : -eau → -eaux */
        const char *plur=(sl>=3 && !strcmp(st+sl-3,"eau"))?"x":"s";
        char gh[44]; snprintf(gh,sizeof gh,"%s%s (%d)", st, plur, cnt);
        draw_text(ren, fs, x, y, COL_DIM, gh); y+=15;
        for (int r=0;r<s->econ->n_regions && y<bottom-15;r++){
            const RegionEconomy *re=&s->econ->region[r];
            if (re->owner!=player) continue;
            long pop=region_pop_of(re);
            if (capitale_max_tier(pop)!=tier) continue;
            int pid=(r<w->n_regions && w->region[r].n_provinces>0)? w->region[r].province_ids[0]:-1;
            Resource res=(pid>=0 && pid<w->n_provinces)? w->province[pid].resource:RES_NONE;
            fill_rect(ren, x+2, y+2, 9,9, SLICE_PAL[((int)res)&7]); draw_box(ren,x+2,y+2,9,9,COL_DIM);  /* icône-ressource */
            const char *nm=(r<w->n_regions && w->region[r].name[0])? w->region[r].name:"—";
            draw_text(ren, fs, x+16, y, COL_PARCH, nm);
            char pz[24]; snprintf(pz,sizeof pz,"%ld",pop); int pzw=text_w(fs,pz);
            draw_text(ren, fs, x+rw-pzw, y, COL_DIM, pz);                 /* population : la stat-clé */
            bool ham=region_free_slot(re); SDL_Rect hr={0,0,0,0};
            if (ham){ hr=(SDL_Rect){x+rw-pzw-15, y, 11,12};               /* le MARTEAU (slot libre × tech) */
                fill_rect(ren,hr.x,hr.y+1,10,10,COL_COPPER); draw_box(ren,hr.x,hr.y+1,10,10,COL_DIM);
                draw_text(ren, fs, hr.x+2, hr.y, COL_PANEL, "+"); }
            if (oi<80){                                                   /* survol : détail rapide */
                IncomeReadout inc=province_income(s->econ,r); int n=0;
                n+=snprintf(g_ohov[oi]+n,(size_t)(sizeof g_ohov[oi])-n,"%s — Laboureurs %ld · Artisans %ld · Nobles %ld",
                            nm,(long)re->strata[0].pop,(long)re->strata[1].pop,(long)re->strata[2].pop);
                if (inc.n>0) n+=snprintf(g_ohov[oi]+n,(size_t)(sizeof g_ohov[oi])-n," · produit +%.1f %s/j",inc.line[0].per_day,inc.line[0].source);
                n+=snprintf(g_ohov[oi]+n,(size_t)(sizeof g_ohov[oi])-n, ham?" · slot libre (marteau : bâtir)":" · complet");
                zone_add((SDL_Rect){x,y-1,rw,14}, g_ohov[oi]);
                orow_add((SDL_Rect){x,y-1,rw-pzw-17,14}, hr, pid, ham?r:-1);
                oi++;
            }
            y+=15;
        }
        y+=3;
    }
    /* ARMÉES — l'effectif en HOMMES (×100) ; survol = composition. */
    if (s->camp && campaign_active(s->camp,player) && y<bottom-15 && oi<80){
        draw_text(ren, fs, x, y, COL_DIM, "Armées (1)"); y+=15;
        long men=campaign_units(s->camp,player)*100;
        fill_rect(ren, x+2,y+2,9,9, COL_COPPER); draw_box(ren,x+2,y+2,9,9,COL_DIM);
        draw_text(ren, fs, x+16, y, COL_PARCH, "Armée de campagne");
        char pz[24]; snprintf(pz,sizeof pz,"%ld",men); int pzw=text_w(fs,pz);
        draw_text(ren, fs, x+rw-pzw, y, COL_DIM, pz);
        ArmyComposition cp=campaign_composition(s->camp,player);
        snprintf(g_ohov[oi],sizeof g_ohov[oi],"Armée de campagne — %ld hommes : inf %ld · arch %ld · cav %ld · %s",
                 men, cp.infanterie*100, cp.archers*100, cp.cavalerie*100, campaign_phase_name(campaign_phase(s->camp,player)));
        zone_add((SDL_Rect){x,y-1,rw,14}, g_ohov[oi]);
        int loc=campaign_location(s->camp,player);
        int lp=(loc>=0&&loc<w->n_regions&&w->region[loc].n_provinces>0)?w->region[loc].province_ids[0]:-1;
        orow_add((SDL_Rect){x,y-1,rw,14}, (SDL_Rect){0,0,0,0}, lp, -1);
        y+=15;
        /* « REMPLIR » : recompléter l'armée en TERRITOIRE AMI (note). */
        if (campaign_can_refill(s->camp, s->econ, player) && y<bottom-16){
            long rm, mat; campaign_refill_cost(s->camp, player, &rm, &mat);
            int bw=text_w(fs,"Remplir")+16;
            fill_round(ren, x+16, y, bw, 15, COL_PANEL2, 4);
            round_box (ren, x+16, y, bw, 15, COL_COPPER, 4);
            draw_text (ren, fs, x+24, y+1, COL_COPPER, "Remplir");
            static char rh[180];
            snprintf(rh,sizeof rh,
                     "Remplir l'armée (en territoire ami) : +%ld hommes levés · %ld matériaux pour les armes "
                     "(achetés au marché, en or si le stock manque).", rm, mat);
            zone_add((SDL_Rect){x+16,y,bw,15}, rh);
            g_refill_btn=(SDL_Rect){x+16,y,bw,15}; g_refill_owner=player;
            y+=17;
        }
    }
}

/* §5 — la MINICARTE (coin bas-droit) : le monde en petit + le cadre de vue. */
#define MM_W 212
#define MM_H 100
static void minimap_fit(float *scale, float *ox, float *oy){
    float sx=(float)MM_W/SCPS_W, sy=(float)MM_H/SCPS_H;
    float sc = sx<sy?sx:sy;
    *scale=sc; *ox=(SCPS_W - MM_W/sc)*0.5f; *oy=(SCPS_H - MM_H/sc)*0.5f;
}
static void minimap_rect(int win_w, int win_h, SDL_Rect *r){
    r->x=win_w-MM_W-12; r->y=win_h-MM_H-36; r->w=MM_W; r->h=MM_H;
}
static void draw_minimap(SDL_Renderer *ren, PixBuf *mm, int win_w, int win_h, const Cam *cam){
    SDL_Rect m; minimap_rect(win_w,win_h,&m);
    panel_bg(ren, m.x-5, m.y-5, MM_W+10, MM_H+10);
    if (mm->tex){ SDL_RenderCopy(ren, mm->tex, NULL, &m); }
    round_box(ren, m.x-1,m.y-1,MM_W+2,MM_H+2, COL_COPPER, 3);
    /* le cadre de VUE (où regarde la caméra). */
    float sc,ox,oy; minimap_fit(&sc,&ox,&oy);
    int vx=m.x+(int)((cam->ox-ox)*sc), vy=m.y+(int)((cam->oy-oy)*sc);
    int vw=(int)((win_w/cam->scale)*sc), vh=(int)((win_h/cam->scale)*sc);
    if (vx<m.x){ vw-=(m.x-vx); vx=m.x; } if (vy<m.y){ vh-=(m.y-vy); vy=m.y; }
    if (vx+vw>m.x+MM_W) vw=m.x+MM_W-vx;
    if (vy+vh>m.y+MM_H) vh=m.y+MM_H-vy;
    if (vw>2 && vh>2) draw_box(ren, vx, vy, vw, vh, (SDL_Color){0xff,0xf4,0xe0,0xff});
}

/* §5 — BOUTONS de mode de carte (Politique · Culture · Foi · Relief), le COURANT
 * en cuivre. Posés en bas (zone carte), testés au clic. */
static void draw_mode_buttons(SDL_Renderer *ren, int win_h, ViewMode cur){
    struct { const char *name; ViewMode m; } B[4] = {
        {"Politique", VIEW_COUNTRIES}, {"Culture", VIEW_CULTURE},
        {"Foi", VIEW_FAITH}, {"Relief", VIEW_TERRAIN} };
    TTF_Font *fs=g_font_small?g_font_small:g_font;
    int x=322, y=win_h-26-32, h=24;
    for (int i=0;i<4;i++){
        int tw=text_w(fs,B[i].name), w=tw+18;
        bool on=(cur==B[i].m);
        fill_round(ren, x, y, w, h, on?COL_PANEL2:COL_PANEL, 6);
        round_box (ren, x, y, w, h, on?COL_COPPER:COL_EDGE, 6);
        if (on) round_box(ren, x-1,y-1,w+2,h+2, COL_COPPER, 7);   /* le courant : rim cuivre */
        draw_text(ren, fs, x+9, y+5, on?COL_COPPER:COL_DIM, B[i].name);
        modebtn_add((SDL_Rect){x,y,w,h}, (int)B[i].m);
        x += w+7;
    }
}

/* Survol = définition : le hover de la zone sous le curseur, en pied d'écran. */
static void draw_hover_footer(SDL_Renderer *ren, int win_w, int win_h, int mx, int my){
    const char *def = zone_hit(mx,my);
    if (!def) return;
    const char *s1 = strchr(def, '\x1f');
    if (!s1){                                                   /* survol SIMPLE : bandeau en bas */
        int fh=22;
        fill_rect(ren, 0, win_h-fh, win_w, fh, COL_PANEL2);
        fill_rect(ren, 0, win_h-fh-1, win_w, 1, COL_COPPER);
        draw_text(ren, g_font, 12, win_h-fh+3, COL_PARCH, def);
        return;
    }
    /* survol DEUX COLONNES : « titre \x1f gauche(prix) \x1f droite(effet) » → une boîte près
     * du curseur (comme l'exemple : nom en titre, input/prix à gauche, effet à droite). */
    char title[120]={0}, left[120]={0}, right[160]={0};
    int n1=(int)(s1-def); if(n1>119)n1=119; memcpy(title,def,(size_t)n1);
    const char *s2=strchr(s1+1,'\x1f');
    if (s2){
        int nl=(int)(s2-(s1+1)); if(nl>119)nl=119; memcpy(left,s1+1,(size_t)nl);
        snprintf(right,sizeof right,"%s",s2+1);
    } else {
        snprintf(left,sizeof left,"%s",s1+1);
    }
    int wt=text_w(g_font_big,title), wl=text_w(g_font,left), wr=text_w(g_font,right);
    int gap=46, inner=wl+gap+wr; if (wt>inner) inner=wt;
    int bw=inner+24, bh=58, bx=mx+16, by=my+10;
    if (bx+bw>win_w-4) bx=win_w-bw-4;
    if (bx<4) bx=4;
    if (by+bh>win_h-4) by=win_h-bh-4;
    if (by<4) by=4;
    panel_bg(ren, bx,by, bw,bh);
    draw_text(ren, g_font_big, bx+12, by+6,  COL_COPPER, title);
    fill_rect(ren, bx+10, by+27, bw-20, 1, COL_PANEL2);         /* la ligne de séparation */
    draw_text(ren, g_font, bx+12, by+33, COL_PARCH, left);                    /* PRIX à gauche */
    if (right[0]) draw_text(ren, g_font, bx+bw-12-wr, by+33,
                            (SDL_Color){0x8c,0xd0,0x9c,0xff}, right);          /* EFFET à droite (vert doux) */
}

/* Le PEUPLE dominant d'un empire (via la membrane : groupe majoritaire de sa
 * capitale). Mot diégétique, jamais un nom SCPS. */
static const char *army_people(const World *w, const WorldEconomy *econ,
                               const ModifierStack *drift, int cid){
    if (cid<0 || cid>=w->n_countries) return "—";
    int cp=w->country[cid].capital_prov; if (cp<0 || cp>=w->n_provinces) return "—";
    int reg=w->province[cp].region;      if (reg<0 || reg>=econ->n_regions) return "—";
    if (econ->region[reg].pop.n_groups<=0) return "—";
    GroupReadout gr[SCPS_MAX_GROUPS];
    int ng=province_composition(&econ->region[reg].pop, drift, &econ->region[reg].culture,
                                5.f, 5.f, gr, SCPS_MAX_GROUPS);
    return (ng>0) ? gr[0].race : "—";
}

/* Centre-écran d'une région = barycentre des graines de ses PROVINCES (coords de
 * grille géographiques ; Region.seed_x/y n'est pas peuplé). */
static bool region_screen_pos(const World *w, const Cam *cam, int reg, int *osx, int *osy){
    if (reg<0 || reg>=w->n_regions) return false;
    const Region *R=&w->region[reg];
    long ax=0, ay=0; int n=0;
    for (int k=0;k<R->n_provinces && k<12;k++){
        int pid=R->province_ids[k];
        if (pid<0 || pid>=w->n_provinces) continue;
        ax += w->province[pid].seed_x; ay += w->province[pid].seed_y; n++;
    }
    if (n==0) return false;
    *osx=(int)(((float)ax/n - cam->ox)*cam->scale);
    *osy=(int)(((float)ay/n - cam->oy)*cam->scale);
    return true;
}

/* §4 — LE MARQUEUR D'ARMÉE : une armée de campagne posée sur la carte, en COULEUR
 * D'EMPIRE, dimensionnée par l'effectif ; le survol détaille (peuple, inf/arch/cav,
 * phase). Pour une armée ENNEMIE : ASYMÉTRIE d'information — on montre la TAILLE
 * (un mot), pas le décompte. Membrane : ne lit que des nombres tangibles (paquets),
 * des mots et la GÉOGRAPHIE (position de région) ; aucun flottant SCPS. */
static char g_army_tip[SCPS_MAX_COUNTRY][192];
static void draw_army_markers(SDL_Renderer *ren, const Cam *cam, const Sim *s,
                              const World *w, int win_w, int win_h){
    if (!s->camp) return;
    for (int c=0; c<w->n_countries && c<SCPS_MAX_COUNTRY; c++){
        if (!campaign_active(s->camp, c)) continue;
        int reg = campaign_location(s->camp, c);
        int sx, sy;
        if (!region_screen_pos(w, cam, reg, &sx, &sy)) continue;
        if (sx < -24 || sy < -24 || sx > win_w+24 || sy > win_h+24) continue;   /* hors champ */
        long paquets   = campaign_units(s->camp, c);
        FieldPhase ph  = campaign_phase(s->camp, c);
        bool mine      = (c == s->player);
        uint32_t col   = w->country[c].color;
        SDL_Color ec = { (uint8_t)((col>>16)&0xFF), (uint8_t)((col>>8)&0xFF), (uint8_t)(col&0xFF), 0xFF };
        SDL_Color dk = { 0x10,0x12,0x16,0xFF };

        /* trait de marche : une fine ligne vers la région-but (ses propres armées). */
        if (mine && ph==FA_MARCH){
            int dx, dy;
            if (region_screen_pos(w, cam, s->camp->army[c].dest, &dx, &dy)){
                SDL_SetRenderDrawColor(ren, ec.r, ec.g, ec.b, 0x99);
                SDL_RenderDrawLine(ren, sx, sy, dx, dy);
            }
        }
        /* jeton : carré en couleur d'empire (taille ∝ effectif, 5..11), liseré clair. */
        int rr = 5 + (int)(paquets/18); if (rr>11) rr=11;
        fill_rect(ren, sx-rr, sy-rr, 2*rr, 2*rr, ec);
        draw_box (ren, sx-rr, sy-rr, 2*rr, 2*rr, dk);
        draw_box (ren, sx-rr-1, sy-rr-1, 2*rr+2, 2*rr+2, COL_PARCH);
        if (ph==FA_SIEGE) draw_ring(ren, sx, sy, (float)(rr+3), COL_COPPER);   /* halo de siège */

        /* étiquette : les SIENNES → le NOMBRE D'HOMMES (multiple de 100) ; l'ENNEMIE
         * → le MOT de taille (asymétrie). Jamais « 12 » : on lève par paquets de 100. */
        char lab[24];
        if (mine) snprintf(lab, sizeof lab, "%ld", paquets*100);
        else      snprintf(lab, sizeof lab, "%s", army_host_word(paquets));
        if (g_font_small){
            int lw=text_w(g_font_small, lab);
            SDL_Color labbg = { 0x0f,0x16,0x22,0xcc };
            fill_rect(ren, sx-lw/2-2, sy+rr+1, lw+4, 13, labbg);
            draw_text(ren, g_font_small, sx-lw/2, sy+rr+1, COL_PARCH, lab);
        }

        /* survol : détail pour les siennes, taille seule pour l'ennemie. */
        const char *people = army_people(w, s->econ, s->drift, c);
        ArmyComposition cp = campaign_composition(s->camp, c);
        if (mine)
            snprintf(g_army_tip[c], sizeof g_army_tip[c],
                     "%s (%s) — %s · %ld hommes : inf %ld · arch %ld · cav %ld%s · %s",
                     w->country[c].name, people, army_host_word(paquets), paquets*100,
                     cp.infanterie*100, cp.archers*100, cp.cavalerie*100,
                     cp.mages? " · mages":"", campaign_phase_name(ph));
        else
            snprintf(g_army_tip[c], sizeof g_army_tip[c],
                     "%s (%s) — %s (%s) · armée ENNEMIE : on en juge la taille, pas le détail",
                     w->country[c].name, people, army_host_word(paquets), campaign_phase_name(ph));
        zone_add((SDL_Rect){sx-rr-2, sy-rr-2, 2*rr+4, 2*rr+16}, g_army_tip[c]);
    }
}

/* Une armée de campagne EST-ELLE en mouvement quelque part ? (sert au mode --war
 * pour laisser une guerre mûrir avant la capture.) */
static bool any_field_army(const Sim *s, const World *w){
    for (int c=0;c<w->n_countries && c<SCPS_MAX_COUNTRY;c++)
        if (campaign_active(s->camp,c) && campaign_phase(s->camp,c)!=FA_IDLE) return true;
    return false;
}

/* Capture hors-écran (mode --shot) : sérialise le rendu courant en PPM, pour
 * vérifier l'UI sans display interactif. */
static void save_ppm(const char *path, const uint32_t *px, int w, int h) {
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int i=0;i<w*h;i++) {
        uint32_t c = px[i];
        unsigned char rgb[3] = { (unsigned char)((c>>16)&0xFF),
                                 (unsigned char)((c>>8)&0xFF),
                                 (unsigned char)(c&0xFF) };
        fwrite(rgb,1,3,f);
    }
    fclose(f);
    printf("[scps] capture écrite : %s\n", path);
}

/* ======================================================================= */

/* ═══════════════════════════════════════════════════════════════════════════
 * LE SHELL DU JEU (brief shell) — menu → création → litanie → OUVERTURE EN PAUSE
 * → partie. Discipline ESC : fermer la couche du dessus, JAMAIS quitter l'appli
 * (quitter = bouton + confirmation, seul chemin). Sauvegarde : chantier suivant
 * (le menu vit avec « Charger » grisé — l'ordre du brief lui-même).
 * ═══════════════════════════════════════════════════════════════════════════ */
static bool g_pause_menu=false, g_quit_confirm=false, g_show_tuto=false;
static int  g_tuto_page=0;
static int  g_setup_ethos=5, g_setup_race=(int)RACE_HUMAIN, g_setup_terre=5;  /* défauts : Pacifiste? non → voir tables */
static char g_open_terre_line[120]="";
static WorldParams g_stage;            /* l'écran de création édite une COPIE */
static bool g_pending_open=false;      /* après la forge : entrer en OUVERTURE */
static int  g_save_pick=0, g_load_pick=0;   /* surcouches : choisir un slot (1=ouvert) */
static int  g_load_confirm=-1;               /* slot à charger après confirmation (partie en cours) */
static bool g_game_started=false;            /* une partie a commencé (→ confirmation au chargement) */
/* cibles cliquables du shell */
enum { SH_SLOT_SAVE=100, SH_SLOT_LOAD, SH_PICK_CLOSE, SH_LOADC_YES, SH_LOADC_NO,
       SH_MENU_ITEM=1, SH_SLIDER_DN, SH_SLIDER_UP, SH_SEED_DICE, SH_PICK_ETHOS,
       SH_PICK_RACE, SH_PICK_TERRE, SH_FORGER, SH_BACK, SH_OPEN_GO, SH_OPEN_REROLL,
       SH_PM_ITEM, SH_QC_YES, SH_QC_NO, SH_TUTO_PREV, SH_TUTO_NEXT };
typedef struct { SDL_Rect r; int kind, a; } ShellHit;
static ShellHit g_shhits[80]; static int g_nshhits;
static void shhit_reset(void){ g_nshhits=0; }
static void shhit_add(SDL_Rect r,int k,int a){ if(g_nshhits<80){ g_shhits[g_nshhits].r=r; g_shhits[g_nshhits].kind=k; g_shhits[g_nshhits].a=a; g_nshhits++; } }

/* ── LE TUTORIEL — texte embarqué tel quel (brief §7), pages courtes ── */
static const char *SH_ETHOS_N[6]={"Dominateur","Honneur","Mercantile","Bureaucrate","Ordre","Pacifiste"};
static const char *SH_ETHOS_L[6]={
    "la conquête est un droit","la parole vaut le sang","tout s'achète, surtout la paix",
    "l'archive gouverne mieux que l'épée","la discipline tient le monde","rien ne vaut le sang"};
static const char *SH_TERRE_N[6]={"Plaines","Côte","Estuaire","Collines","Forêt","Au hasard"};

static void sh_button(SDL_Renderer *ren,int x,int y,int w,const char *txt,bool on,bool grise,int kind,int a){
    fill_rect(ren,x,y,w,26, grise?(SDL_Color){0x12,0x16,0x20,0xff}: on?(SDL_Color){0x3a,0x2c,0x1a,0xff}:(SDL_Color){0x18,0x20,0x30,0xff});
    draw_box(ren,x,y,w,26, grise?COL_PANEL2: on?COL_COPPER:COL_PANEL2);
    int tw=text_w(g_font,txt);
    draw_text(ren,g_font,x+(w-tw)/2,y+5, grise?(SDL_Color){0x55,0x55,0x55,0xff}: on?COL_COPPER:COL_PARCH, txt);
    if (!grise) shhit_add((SDL_Rect){x,y,w,26},kind,a);
}
/* un slider du monde : étiquette, valeur, [−][+] */
static int sh_slider(SDL_Renderer *ren,int x,int y,const char *lbl,const char *val,int idx,const char *hov){
    draw_text(ren,g_font,x,y,COL_DIM,lbl);
    draw_text(ren,g_font,x+150,y,COL_PARCH,val);
    fill_rect(ren,x+230,y+1,18,16,(SDL_Color){0x18,0x20,0x30,0xff}); draw_box(ren,x+230,y+1,18,16,COL_PANEL2);
    draw_text(ren,g_font,x+235,y,COL_PARCH,"-"); shhit_add((SDL_Rect){x+230,y+1,18,16},SH_SLIDER_DN,idx);
    fill_rect(ren,x+252,y+1,18,16,(SDL_Color){0x18,0x20,0x30,0xff}); draw_box(ren,x+252,y+1,18,16,COL_PANEL2);
    draw_text(ren,g_font,x+257,y,COL_PARCH,"+"); shhit_add((SDL_Rect){x+252,y+1,18,16},SH_SLIDER_UP,idx);
    if (hov) zone_add((SDL_Rect){x,y-2,270,20},hov);
    return y+24;
}
static void sh_apply_slider(WorldParams *p,int idx,int dir){
    float st=0.05f*dir;
    switch(idx){
        case 0: p->n_continents+=dir; if(p->n_continents<1)p->n_continents=1; if(p->n_continents>8)p->n_continents=8; break;
        case 1: p->land_amount+=st;  if(p->land_amount<0)p->land_amount=0; if(p->land_amount>1)p->land_amount=1; break;
        case 2: p->world_age+=st;    if(p->world_age<0)p->world_age=0; if(p->world_age>1)p->world_age=1; break;
        case 3: p->erosion+=st;      if(p->erosion<0)p->erosion=0; if(p->erosion>1)p->erosion=1; break;
        case 4: p->mountains+=st;    if(p->mountains<0)p->mountains=0; if(p->mountains>1)p->mountains=1; break;
        case 5: p->temperature+=st;  if(p->temperature<0)p->temperature=0; if(p->temperature>1)p->temperature=1; break;
        case 6: p->humidity+=st;     if(p->humidity<0)p->humidity=0; if(p->humidity>1)p->humidity=1; break;
        case 7: p->n_empires+=dir;   if(p->n_empires<2)p->n_empires=2; if(p->n_empires>15)p->n_empires=15; break;
        case 8: p->n_city_states+=dir; if(p->n_city_states<0)p->n_city_states=0; if(p->n_city_states>20)p->n_city_states=20; break;
    }
}
/* le VŒU DE TERRE — best-effort, jamais un mur : on déplace la capitale du joueur
 * vers une de SES provinces qui honore le vœu ; sinon ligne honnête. */
static void sh_apply_terre(World *w, Sim *s){
    g_open_terre_line[0]=0;
    if (g_setup_terre>=5) return;                          /* au hasard : on prend ce que la gen a donné */
    int me=s->player;
    Biome want1=BIO_PLAINS, want2=BIO_FARMLAND;
    switch(g_setup_terre){
        case 0: want1=BIO_PLAINS;  want2=BIO_FARMLAND; break;
        case 1: want1=BIO_COAST;   want2=BIO_MANGROVE; break;
        case 2: want1=BIO_COAST;   want2=BIO_MARSH;    break;   /* estuaire ≈ côte d'embouchure */
        case 3: want1=BIO_HILLS;   want2=BIO_HIGHLANDS;break;
        case 4: want1=BIO_FOREST;  want2=BIO_WOODS;    break;
    }
    int best=-1;
    for (int p=0;p<w->n_provinces;p++){
        if (w->province[p].country!=me) continue;
        Biome b=w->province[p].biome_dominant;
        bool estu = (g_setup_terre==2) ? (w->province[p].coastal && b!=BIO_DESERT) : false;
        if (b==want1 || b==want2 || estu){ best=p; break; }
    }
    if (best>=0) w->country[me].capital_prov=best;
    else snprintf(g_open_terre_line,sizeof g_open_terre_line,
                  "Nulle %s libre sur ce monde — vous voilà où la terre a voulu.",
                  SH_TERRE_N[g_setup_terre]);
}
static void sh_center_capital(const World *w, const Sim *s, Cam *cam, int win_w, int win_h){
    int cp=(s->player>=0&&s->player<w->n_countries)?w->country[s->player].capital_prov:-1;
    if (cp<0||cp>=w->n_provinces) return;
    cam->scale=3.0f;
    cam->ox=(float)w->province[cp].seed_x - (float)win_w/(2.f*cam->scale);
    cam->oy=(float)w->province[cp].seed_y - (float)win_h/(2.f*cam->scale);
}
/* l'ouverture applique les CHOIX du joueur sur le monde fraîchement forgé */
static void sh_enter_opening(World *w, Sim *s, Cam *cam, int win_w, int win_h, GameSpeed *speed){
    sh_apply_terre(w,s);
    { int cp=w->country[s->player].capital_prov;                    /* l'ÉTHOS choisi s'ancre au trône */
      int cr=(cp>=0&&cp<w->n_provinces)?w->province[cp].region:-1;
      if (cr>=0&&cr<s->econ->n_regions) s->econ->region[cr].culture.ethos=(Ethos)g_setup_ethos; }
    sh_center_capital(w,s,cam,win_w,win_h);
    *speed=SPEED_PAUSE;                                              /* PAUSE FORCÉE : on ne vole pas le premier regard */
    g_gs=GS_OPENING;
}
/* la LITANIE de forge (une frame d'ambiance avant la gen synchrone) */
static void sh_draw_litanie(SDL_Renderer *ren,int win_w,int win_h,uint32_t seedv){
    fill_rect(ren,0,0,win_w,win_h,(SDL_Color){0x0a,0x0e,0x16,0xff});
    draw_text(ren,g_font_big,win_w/2-90,win_h/2-110,COL_COPPER,"LE MONDE SE FORGE");
    static const char *L[]={ "géologie…","architecture…","altération…","érosion…","côtes…",
                             "climat (vents)…","vallées…","biomes…","fertilité…","territoires…",
                             "hiérarchie…","peuples…","ressources…","sites de départ…","rivières…" };
    for (int i=0;i<15;i++) draw_text(ren,g_font,win_w/2-70,win_h/2-70+i*16,COL_DIM,L[i]);
    char sd[48]; snprintf(sd,sizeof sd,"graine %u",seedv);
    draw_text(ren,g_font,win_w/2-40,win_h/2+180,COL_PARCH,sd);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * LA SAUVEGARDE (brief shell §6) — en-tête versionné + sections TAGUÉES.
 * Chaque struct de système est PLATE (audité) ; les modules à états statiques
 * (intertrade, agency, diplo, factions) possèdent leur sérialisation. Version
 * qui ne matche pas = refus poli (« sauvegarde d'une ère antérieure »).
 * ═══════════════════════════════════════════════════════════════════════════ */
#define SAVE_MAGIC   0x53504353u   /* "SCPS" */
#define SAVE_VERSION 4u            /* v4 : LA MER — flotte (NAVY), port réel (ProvBuild.port), routes en jours de mer (v3 courants) */
#define SAVE_F_CRYPT 1u
typedef struct {
    uint32_t magic, version;
    uint32_t seed;
    int32_t  day, year, player;
    WorldParams params;
    int64_t  stamp;            /* horodatage (time) */
    char     line[96];         /* « An 87 — Empire de X, 12 régions » (écran Charger) */
    uint32_t payload;          /* taille attendue après l'en-tête (intégrité) */
    uint32_t flags;            /* SAVE_F_CRYPT : sections chiffrées (l'en-tête reste en CLAIR) */
    uint64_t nonce;            /* nonce ChaCha20 — unique par sauvegarde */
    uint64_t plain_ck;         /* empreinte FNV-1a du CLAIR : un octet altéré = refus net */
} SaveHeader;
typedef struct { int32_t day, year, player, prev_dawned; uint32_t camp_rng;
                 int32_t race, ethos; int16_t prev_owner[SCPS_MAX_REG]; } SaveMisc;
static const char *save_slot_path(int slot){
    static char p[64]; snprintf(p,sizeof p,"saves/slot_%d.scps",slot); return p;
}
static bool save_slot_info(int slot, SaveHeader *out){
    FILE *f=fopen(save_slot_path(slot),"rb");
    if (!f) return false;
    bool ok = fread(out,sizeof *out,1,f)==1 && out->magic==SAVE_MAGIC;
    fclose(f); return ok;
}
#define SV_TAG(a,b,c,d) ((uint32_t)(a)|((uint32_t)(b)<<8)|((uint32_t)(c)<<16)|((uint32_t)(d)<<24))
static bool sv_w(FILE *f, uint32_t tag, const void *p, size_t sz){
    uint32_t z=(uint32_t)sz;
    return fwrite(&tag,4,1,f)==1 && fwrite(&z,4,1,f)==1 && (sz==0 || fwrite(p,sz,1,f)==1);
}
static bool sv_r(FILE *f, uint32_t tag, void *p, size_t sz){
    uint32_t t,z;
    if (fread(&t,4,1,f)!=1 || fread(&z,4,1,f)!=1) return false;
    if (t!=tag || z!=(uint32_t)sz) return false;
    return sz==0 || fread(p,sz,1,f)==1;
}
/* sauve la partie ENTIÈRE dans un slot ; renvoie false si l'écriture échoue. */
static bool game_save(int slot, World *w, Sim *s, const WorldParams *params){
    if (slot<1 || slot>3) return false;
    scps_mkdir("saves");                         /* EEXIST inoffensif ; l'échec réel tombe au fopen */
    FILE *f=fopen(save_slot_path(slot),"w+b");   /* w+ : la post-passe de chiffrement RELIT le payload */
    if (!f) return false;
    SaveHeader h; memset(&h,0,sizeof h);
    h.magic=SAVE_MAGIC; h.version=SAVE_VERSION; h.seed=params->seed;
    h.day=s->day; h.year=s->year; h.player=s->player; h.params=*params;
    h.stamp=(int64_t)time(NULL);
    { int nreg=0; for (int r=0;r<s->econ->n_regions;r++) if (s->econ->region[r].owner==s->player) nreg++;
      snprintf(h.line,sizeof h.line,"An %d — %s, %d région(s)",
               s->year, (s->player>=0&&s->player<w->n_countries)?w->country[s->player].name:"?", nreg); }
    bool ok = fwrite(&h,sizeof h,1,f)==1;        /* disque plein → échec NET, pas silencieux */
    long p0=ftell(f);
    ok&=sv_w(f,SV_TAG('W','R','L','D'), w,        sizeof *w);
    ok&=sv_w(f,SV_TAG('E','C','O','N'), s->econ,  sizeof *s->econ);
    ok&=sv_w(f,SV_TAG('P','R','O','S'), s->wp,    sizeof *s->wp);
    ok&=sv_w(f,SV_TAG('L','E','G','I'), s->wl,    sizeof *s->wl);
    ok&=sv_w(f,SV_TAG('N','E','T','W'), s->net,   sizeof *s->net);
    ok&=sv_w(f,SV_TAG('T','E','C','H'), s->ts,    sizeof(TechState)*SCPS_MAX_COUNTRY);
    ok&=sv_w(f,SV_TAG('S','T','A','T'), s->sc,    sizeof *s->sc);
    ok&=sv_w(f,SV_TAG('A','G','C','Y'), s->ag,    sizeof *s->ag);
    ok&=sv_w(f,SV_TAG('E','V','N','T'), s->ev,    sizeof *s->ev);
    ok&=sv_w(f,SV_TAG('D','R','F','T'), s->drift, sizeof *s->drift);
    ok&=sv_w(f,SV_TAG('L','A','B','O'), s->labor, sizeof *s->labor);
    ok&=sv_w(f,SV_TAG('D','I','P','L'), s->dp,    sizeof *s->dp);
    ok&=sv_w(f,SV_TAG('R','T','E','S'), s->rn,    sizeof *s->rn);
    ok&=sv_w(f,SV_TAG('R','V','L','T'), s->rs,    sizeof *s->rs);
    ok&=sv_w(f,SV_TAG('M','I','S','S'), s->missions, sizeof *s->missions);
    ok&=sv_w(f,SV_TAG('C','A','M','P'), s->camp,  sizeof *s->camp);
    ok&=sv_w(f,SV_TAG('N','A','V','Y'), s->navy,  sizeof *s->navy);
    ok&=sv_w(f,SV_TAG('H','A','R','M'), s->host->army, sizeof s->host->army);   /* WarHost SANS scratch */
    ok&=sv_w(f,SV_TAG('H','L','V','Y'), s->host->levy, sizeof s->host->levy);
    ok&=sv_w(f,SV_TAG('A','I','A','C'), s->ai,    sizeof(AiActor)*SCPS_MAX_COUNTRY);
    ok&=sv_w(f,SV_TAG('A','I','O','N'), s->ai_on, sizeof(bool)*SCPS_MAX_COUNTRY);
    { SaveMisc m; memset(&m,0,sizeof m);
      m.day=s->day; m.year=s->year; m.player=s->player; m.prev_dawned=s->prev_dawned;
      m.camp_rng=s->camp_rng; m.race=(int32_t)g_player_race; m.ethos=g_setup_ethos;
      memcpy(m.prev_owner,s->prev_owner_mo,sizeof m.prev_owner);
      ok&=sv_w(f,SV_TAG('M','I','S','C'), &m, sizeof m); }
    /* les modules à ÉTATS STATIQUES possèdent leur sérialisation */
    ok&=sv_w(f,SV_TAG('I','T','R','D'), NULL,0); intertrade_save(f);
    ok&=sv_w(f,SV_TAG('A','G','Y','S'), NULL,0); agency_save(f);
    ok&=sv_w(f,SV_TAG('D','P','L','S'), NULL,0); diplo_save_statics(f);
    ok&=sv_w(f,SV_TAG('F','A','C','T'), NULL,0); faction_save(f);
    /* intégrité + CHIFFREMENT (post-passe) : on relit le payload CLAIR, on prend son
     * empreinte, on le chiffre (ChaCha20, nonce unique), on le réécrit en place.
     * L'en-tête reste en clair (l'écran Charger lit la ligne sans déchiffrer). */
    long p1=ftell(f);
    h.payload=(uint32_t)(p1-p0);
    { uint8_t *buf=(uint8_t*)malloc(h.payload);
      if (!buf){ fclose(f); return false; }
      fseek(f,p0,SEEK_SET);
      if (fread(buf,1,h.payload,f)!=h.payload){ free(buf); fclose(f); return false; }
      h.plain_ck = scrypt_fnv1a(buf,h.payload);
      /* Nonce : modèle « obfuscation, pas secret » (la clé vit dans le binaire) —
       * l'unicité n'est requise que pour éviter un keystream identique entre deux
       * sauvegardes ; le compteur monotone couvre les sauvegardes rapprochées. */
      { static uint64_t seq=0; ++seq;
        h.nonce = ((uint64_t)time(NULL)<<32) ^ (uint64_t)SDL_GetTicks()
                ^ ((uint64_t)params->seed<<13) ^ (uint64_t)(uintptr_t)buf
                ^ (seq<<48) ^ (uint64_t)clock(); }
      h.flags = SAVE_F_CRYPT;
      scrypt_stream(h.nonce, buf, h.payload);
      fseek(f,p0,SEEK_SET);
      ok &= fwrite(buf,1,h.payload,f)==h.payload;
      free(buf); }
    fseek(f,0,SEEK_SET);
    ok &= fwrite(&h,sizeof h,1,f)==1;            /* l'en-tête final (payload/nonce/empreinte) doit passer */
    if (fclose(f)!=0) ok=false;                  /* le flush peut échouer (disque plein) */
    return ok;
}
/* Garde-fou post-chargement : le moteur entier boucle sur ces comptes et indices
 * en LEUR FAISANT CONFIANCE (n_countries ≤ 56, cell.province < n_provinces…).
 * L'empreinte FNV-1a n'est pas un MAC (la clé vit dans le binaire) : un fichier
 * FORGÉ passe l'intégrité — on revalide donc les invariants avant de déclarer
 * la partie prête. Refus net, comme le reste du chargeur. */
static bool save_sane(const World *w, const Sim *s, int player){
    if (w->n_provinces <0 || w->n_provinces >SCPS_MAX_PROV)      return false;
    if (w->n_regions   <0 || w->n_regions   >SCPS_MAX_REG)       return false;
    if (w->n_countries <0 || w->n_countries >SCPS_MAX_COUNTRY)   return false;
    if (w->n_continents<0 || w->n_continents>SCPS_MAX_CONTINENT) return false;
    if (w->n_rivers    <0 || w->n_rivers    >SCPS_MAX_RIVERS)    return false;
    for (int i=0;i<w->n_rivers;i++)
        if (w->river[i].len<0 || w->river[i].len>SCPS_RIVER_MAXLEN) return false;
    if (player<0 || player>=w->n_countries) return false;
    for (int i=0;i<SCPS_N;i++){ const Cell *c=&w->cell[i];
        if (c->province >= w->n_provinces || c->region    >= w->n_regions ||
            c->country  >= w->n_countries || c->continent >= w->n_continents) return false; }
    for (int p=0;p<w->n_provinces;p++){ const Province *pr=&w->province[p];
        if (pr->region >= w->n_regions || pr->country >= w->n_countries) return false; }
    for (int r=0;r<w->n_regions;r++){ const Region *rg=&w->region[r];
        if (rg->n_provinces<0 || rg->n_provinces>12 || rg->country>=w->n_countries) return false;
        for (int k=0;k<rg->n_provinces;k++)
            if (rg->province_ids[k]<0 || rg->province_ids[k]>=w->n_provinces) return false; }
    for (int c=0;c<w->n_countries;c++){ const Country *ct=&w->country[c];
        if (ct->n_regions<0 || ct->n_regions>12 || ct->capital_prov>=w->n_provinces) return false;
        for (int k=0;k<ct->n_regions;k++)
            if (ct->region_ids[k]<0 || ct->region_ids[k]>=w->n_regions) return false; }
    if (s->econ->n_regions<0 || s->econ->n_regions>SCPS_MAX_REG) return false;
    for (int r=0;r<s->econ->n_regions;r++){ const RegionEconomy *re=&s->econ->region[r];
        if (re->owner >= w->n_countries) return false;
        if (re->pop.n_groups<0 || re->pop.n_groups>SCPS_MAX_GROUPS) return false; }
    if (s->rn->n<0 || s->rn->n>SCPS_MAX_ROUTES) return false;
    for (int i=0;i<s->rn->n;i++){ const TradeRoute *rt=&s->rn->route[i];
        if (rt->ra<0 || rt->ra>=s->econ->n_regions || rt->rb<0 || rt->rb>=s->econ->n_regions) return false; }
    for (int i=0;i<SCPS_MAX_COUNTRY;i++){ const FieldArmy *a=&s->camp->army[i];
        if (!a->active) continue;
        if (a->owner<0 || a->owner>=w->n_countries) return false;
        if (a->loc <0 || a->loc >=s->econ->n_regions) return false;
        if (a->dest>=s->econ->n_regions || a->next>=s->econ->n_regions) return false; }
    for (int i=0;i<SCPS_MAX_COUNTRY;i++){ const Navy *nv=&s->navy->n[i];
        for (int t=0;t<HULL_COUNT;t++) if (nv->hull[t]<0 || nv->hull[t]>100000) return false;
        if (nv->at_sea<0 || nv->build_hull<-1 || nv->build_hull>=HULL_COUNT) return false;
        if (nv->home_port>=s->econ->n_regions) return false; }
    return true;
}
/* charge un slot. 0 = ok ; 1 = absent/corrompu ; 2 = « ère antérieure » (version). */
#define SAVE_MAX_PAYLOAD (256u<<20)   /* plafond de vraisemblance : pas de malloc(4 Go) sur en-tête forgé */
static int game_load(int slot, World *w, Sim *s, WorldParams *params){
    if (slot<1 || slot>3) return 1;
    FILE *f=fopen(save_slot_path(slot),"rb");
    if (!f) return 1;
    SaveHeader h;
    if (fread(&h,sizeof h,1,f)!=1 || h.magic!=SAVE_MAGIC){ fclose(f); return 1; }
    if (h.version!=SAVE_VERSION){ fclose(f); return 2; }
    if (h.payload==0 || h.payload>SAVE_MAX_PAYLOAD){ fclose(f); return 1; }
    /* payload → mémoire ; déchiffré si marqué ; l'EMPREINTE du clair fait foi
     * (un octet altéré — chiffré ou non — et le chargement REFUSE net). */
    { uint8_t *buf=(uint8_t*)malloc(h.payload?h.payload:1);
      if (!buf){ fclose(f); return 1; }
      if (fread(buf,1,h.payload,f)!=h.payload){ free(buf); fclose(f); return 1; }
      fclose(f);
      if (h.flags & SAVE_F_CRYPT) scrypt_stream(h.nonce, buf, h.payload);
      if (scrypt_fnv1a(buf,h.payload)!=h.plain_ck){ free(buf); return 1; }   /* altéré → refus */
      f=tmpfile();
      if (!f){ free(buf); return 1; }
      if (fwrite(buf,1,h.payload,f)!=h.payload){ free(buf); fclose(f); return 1; }
      free(buf); rewind(f); }
    long p0=ftell(f);
    bool ok=true;
    ok&=sv_r(f,SV_TAG('W','R','L','D'), w,        sizeof *w);
    ok&=sv_r(f,SV_TAG('E','C','O','N'), s->econ,  sizeof *s->econ);
    ok&=sv_r(f,SV_TAG('P','R','O','S'), s->wp,    sizeof *s->wp);
    ok&=sv_r(f,SV_TAG('L','E','G','I'), s->wl,    sizeof *s->wl);
    ok&=sv_r(f,SV_TAG('N','E','T','W'), s->net,   sizeof *s->net);
    ok&=sv_r(f,SV_TAG('T','E','C','H'), s->ts,    sizeof(TechState)*SCPS_MAX_COUNTRY);
    ok&=sv_r(f,SV_TAG('S','T','A','T'), s->sc,    sizeof *s->sc);
    ok&=sv_r(f,SV_TAG('A','G','C','Y'), s->ag,    sizeof *s->ag);
    ok&=sv_r(f,SV_TAG('E','V','N','T'), s->ev,    sizeof *s->ev);
    ok&=sv_r(f,SV_TAG('D','R','F','T'), s->drift, sizeof *s->drift);
    ok&=sv_r(f,SV_TAG('L','A','B','O'), s->labor, sizeof *s->labor);
    ok&=sv_r(f,SV_TAG('D','I','P','L'), s->dp,    sizeof *s->dp);
    ok&=sv_r(f,SV_TAG('R','T','E','S'), s->rn,    sizeof *s->rn);
    ok&=sv_r(f,SV_TAG('R','V','L','T'), s->rs,    sizeof *s->rs);
    ok&=sv_r(f,SV_TAG('M','I','S','S'), s->missions, sizeof *s->missions);
    ok&=sv_r(f,SV_TAG('C','A','M','P'), s->camp,  sizeof *s->camp);
    ok&=sv_r(f,SV_TAG('N','A','V','Y'), s->navy,  sizeof *s->navy);
    ok&=sv_r(f,SV_TAG('H','A','R','M'), s->host->army, sizeof s->host->army);
    ok&=sv_r(f,SV_TAG('H','L','V','Y'), s->host->levy, sizeof s->host->levy);
    ok&=sv_r(f,SV_TAG('A','I','A','C'), s->ai,    sizeof(AiActor)*SCPS_MAX_COUNTRY);
    ok&=sv_r(f,SV_TAG('A','I','O','N'), s->ai_on, sizeof(bool)*SCPS_MAX_COUNTRY);
    { SaveMisc m;
      ok&=sv_r(f,SV_TAG('M','I','S','C'), &m, sizeof m);
      if (ok){ s->day=m.day; s->year=m.year; s->player=m.player; s->prev_dawned=m.prev_dawned;
               s->camp_rng=m.camp_rng;
               if (m.race <0 || m.race >=(int32_t)RACE_COUNT)  m.race =(int32_t)RACE_HUMAIN;
               if (m.ethos<0 || m.ethos>=(int32_t)ETHOS_COUNT) m.ethos=0;
               g_player_race=(SpeciesArchetype)m.race; g_setup_ethos=m.ethos;
               memcpy(s->prev_owner_mo,m.prev_owner,sizeof m.prev_owner); } }
    ok&=sv_r(f,SV_TAG('I','T','R','D'), NULL,0); ok&=intertrade_load(f);
    ok&=sv_r(f,SV_TAG('A','G','Y','S'), NULL,0); ok&=agency_load(f);
    ok&=sv_r(f,SV_TAG('D','P','L','S'), NULL,0); ok&=diplo_load_statics(f);
    ok&=sv_r(f,SV_TAG('F','A','C','T'), NULL,0); ok&=faction_load(f);
    long p1=ftell(f); fclose(f);
    if (!ok || (uint32_t)(p1-p0)!=h.payload) return 1;     /* taille/section : refus net */
    if (!save_sane(w, s, s->player)) return 1;             /* invariants du moteur : refus net */
    demography_dyn_id_rebase(s->econ);                     /* drift_id dynamiques au-dessus du chargé */
    *params=h.params;
    s->ready=true;
    return 0;
}

/* F12 — capture d'écran (brief build §3) : on relit le framebuffer du renderer
 * en RGB top-down (l'ordre que stb attend) et on écrit un PNG horodaté dans
 * screenshots/. Utile au joueur, et à la boucle où une session de code regarde
 * ses propres rendus. */
static void viewer_screenshot(SDL_Renderer *ren){
    int w=0,h=0; SDL_GetRendererOutputSize(ren,&w,&h);
    if (w<=0||h<=0) return;
    unsigned char *px=(unsigned char*)malloc((size_t)w*h*3);
    if (!px) return;
    if (SDL_RenderReadPixels(ren,NULL,SDL_PIXELFORMAT_RGB24,px,w*3)==0){
        scps_mkdir("screenshots");
        char name[96]; time_t t=time(NULL);
        struct tm *lt=localtime(&t);
        if (lt) strftime(name,sizeof name,"screenshots/scps_%Y%m%d_%H%M%S.png",lt);
        else    snprintf(name,sizeof name,"screenshots/scps_%ld.png",(long)t);
        if (stbi_write_png(name,w,h,3,px,w*3)) printf("\n[scps] capture : %s\n",name);
    }
    free(px);
}

/* ── rendu du shell : écrans pleins + surcouches (pause · tuto · confirmation) ── */
static void shell_draw(SDL_Renderer *ren,int win_w,int win_h,World *w,Sim *s,
                       WorldParams *stage){
    shhit_reset();
    if (g_gs==GS_MENU){
        fill_rect(ren,0,0,win_w,win_h,(SDL_Color){0x07,0x0b,0x12,0xb8});   /* le monde respire derrière */
        draw_text(ren,g_font_big,win_w/2-44,win_h/4,COL_COPPER,"S C P S");
        draw_text(ren,g_font,win_w/2-150,win_h/4+26,COL_DIM,tr(STR_MENU_SOUS_TITRE));
        int bx=win_w/2-90, by=win_h/4+70;
        sh_button(ren,bx,by,180,tr(STR_MENU_JOUER),false,false,SH_MENU_ITEM,0); by+=34;
        { SaveHeader hh; bool any=false;
          for (int sl=1;sl<=3 && !any;sl++) any=save_slot_info(sl,&hh);
          sh_button(ren,bx,by,180,tr(STR_MENU_CHARGER),false,!any,SH_MENU_ITEM,1); by+=34; }
        sh_button(ren,bx,by,180,tr(STR_MENU_TUTORIEL),false,false,SH_MENU_ITEM,2); by+=34;
        sh_button(ren,bx,by,180,tr(STR_MENU_QUITTER),false,false,SH_MENU_ITEM,3); by+=34;
        { char lng[48]; tr_fmt(lng,sizeof lng,STR_MENU_LANGUE,lang_name(lang_get()));
          sh_button(ren,bx,by,180,lng,false,false,SH_MENU_ITEM,4); }   /* Options : FR/EN à chaud */
    }
    else if (g_gs==GS_SETUP){
        fill_rect(ren,0,0,win_w,win_h,(SDL_Color){0x0a,0x0e,0x16,0xf2});
        draw_text(ren,g_font_big,40,24,COL_COPPER,tr(STR_SETUP_TITRE));
        /* colonne MONDE */
        int x=60,y=70; char v[24];
        draw_text(ren,g_font,x,y,COL_COPPER,"Le monde"); y+=24;
        snprintf(v,24,"%d",stage->n_continents); y=sh_slider(ren,x,y,"Continents",v,0,"Le nombre de masses (1-8).");
        snprintf(v,24,"%.2f",stage->land_amount); y=sh_slider(ren,x,y,"Terres",v,1,"La part de terre émergée.");
        snprintf(v,24,"%.2f",stage->world_age);   y=sh_slider(ren,x,y,"Âge du monde",v,2,"Un monde vieux a le relief usé.");
        snprintf(v,24,"%.2f",stage->erosion);     y=sh_slider(ren,x,y,"Érosion",v,3,"L'eau qui ronge la pierre.");
        snprintf(v,24,"%.2f",stage->mountains);   y=sh_slider(ren,x,y,"Montagnes",v,4,"Le relief qui sépare et protège.");
        snprintf(v,24,"%.2f",stage->temperature); y=sh_slider(ren,x,y,"Température",v,5,"Des glaces aux fournaises.");
        snprintf(v,24,"%.2f",stage->humidity);    y=sh_slider(ren,x,y,"Humidité",v,6,"La pluie fait les forêts.");
        snprintf(v,24,"%d",stage->n_empires);     y=sh_slider(ren,x,y,"Empires",v,7,"Les couronnes rivales (2-15).");
        snprintf(v,24,"%d",stage->n_city_states); y=sh_slider(ren,x,y,"Cités-états",v,8,"Les cités libres (0-20).");
        y+=6; char sd[40]; snprintf(sd,40,"graine  %u",stage->seed);
        draw_text(ren,g_font,x,y,COL_DIM,sd);
        sh_button(ren,x+170,y-4,60,"dé",false,false,SH_SEED_DICE,0);
        /* colonne JOUEUR */
        int px=win_w/2+30, py=70;
        draw_text(ren,g_font,px,py,COL_COPPER,"Le joueur"); py+=24;
        draw_text(ren,g_font_small,px,py,COL_DIM,"Éthos"); py+=18;
        for (int e=0;e<6;e++){
            char lab[80]; snprintf(lab,80,"%s — %s",SH_ETHOS_N[e],SH_ETHOS_L[e]);
            bool on=(g_setup_ethos==e);
            fill_rect(ren,px,py,330,20,on?(SDL_Color){0x3a,0x2c,0x1a,0xff}:(SDL_Color){0x12,0x18,0x24,0xff});
            draw_text(ren,g_font_small,px+8,py+3,on?COL_COPPER:COL_PARCH,lab);
            shhit_add((SDL_Rect){px,py,330,20},SH_PICK_ETHOS,e); py+=22;
        }
        py+=8; draw_text(ren,g_font_small,px,py,COL_DIM,"Race"); py+=18;
        for (int r=0;r<(int)RACE_COUNT;r++){
            bool on=(g_setup_race==r);
            int cx2=px+(r%3)*112, cy2=py+(r/3)*24;
            fill_rect(ren,cx2,cy2,104,20,on?(SDL_Color){0x3a,0x2c,0x1a,0xff}:(SDL_Color){0x12,0x18,0x24,0xff});
            draw_text(ren,g_font_small,cx2+8,cy2+3,on?COL_COPPER:COL_PARCH,species_name((SpeciesArchetype)r));
            shhit_add((SDL_Rect){cx2,cy2,104,20},SH_PICK_RACE,r);
        }
        py+=56; draw_text(ren,g_font_small,px,py,COL_DIM,"Terre de départ (un vœu — jamais un mur)"); py+=18;
        for (int t=0;t<6;t++){
            bool on=(g_setup_terre==t);
            int cx2=px+(t%3)*112, cy2=py+(t/3)*24;
            fill_rect(ren,cx2,cy2,104,20,on?(SDL_Color){0x3a,0x2c,0x1a,0xff}:(SDL_Color){0x12,0x18,0x24,0xff});
            draw_text(ren,g_font_small,cx2+8,cy2+3,on?COL_COPPER:COL_PARCH,SH_TERRE_N[t]);
            shhit_add((SDL_Rect){cx2,cy2,104,20},SH_PICK_TERRE,t);
        }
        /* récapitulatif diégétique + Forger */
        char rec[200];
        snprintf(rec,200,"Un monde %s%s · %d empires · un peuple %s %s cherchant %s.",
                 stage->world_age<0.4f?"jeune":"vieux",
                 stage->mountains>0.6f?" et montagneux":"",
                 stage->n_empires, species_name((SpeciesArchetype)g_setup_race),
                 SH_ETHOS_N[g_setup_ethos],
                 g_setup_terre<5?SH_TERRE_N[g_setup_terre]:"sa chance");
        draw_text(ren,g_font,60,win_h-86,COL_DIM,rec);
        sh_button(ren,60,win_h-52,200,"[ Forger le monde ]",true,false,SH_FORGER,0);
        sh_button(ren,280,win_h-52,120,"Retour",false,false,SH_BACK,0);
    }
    else if (g_gs==GS_OPENING){
        int pw=560, px=(win_w-pw)/2, py=win_h/2-120;
        panel_bg(ren,px,py,pw,236);
        draw_text(ren,g_font_big,px+24,py+18,COL_COPPER,"Vous voilà. Une région. 4 000 âmes.");
        char l1[160];
        int nemp=0,ncit=0;
        for (int c2=0;c2<w->n_countries;c2++){
            if (c2==s->player) continue;
            if (w->country[c2].role==POLITY_CITY_STATE) ncit++;
            else if (w->country[c2].role!=POLITY_UNCLAIMED) nemp++;
        }
        snprintf(l1,160,"Autour de vous : %d empires, %d cités libres, et un monde qui ne vous attend pas.",
                 nemp, ncit);
        draw_text(ren,g_font,px+24,py+52,COL_PARCH,l1);
        if (g_open_terre_line[0]) draw_text(ren,g_font_small,px+24,py+76,COL_DIM,g_open_terre_line);
        draw_text(ren,g_font_big,px+24,py+108,COL_PARCH,"« Voici comment votre empire tombera. »");
        sh_button(ren,px+24, py+170,220,"[ Reroll le monde ]",false,false,SH_OPEN_REROLL,0);
        sh_button(ren,px+pw-24-180,py+170,180,"[ Commencer ]",true,false,SH_OPEN_GO,0);
        draw_text(ren,g_font_small,px+24,py+206,COL_DIM,"La partie s'ouvre EN PAUSE — votre premier geste sera Espace.");
    }
    /* surcouches (jouables aussi en partie) */
    if (g_pause_menu && g_gs==GS_PLAYING){
        fill_rect(ren,0,0,win_w,win_h,(SDL_Color){0x05,0x08,0x0e,0x99});
        int bx=win_w/2-100, by=win_h/2-80;
        panel_bg(ren,bx-20,by-20,240,228);
        draw_text(ren,g_font_big,bx,by-6,COL_COPPER,tr(STR_PAUSE_TITRE)); by+=30;
        sh_button(ren,bx,by,200,tr(STR_PM_REPRENDRE),false,false,SH_PM_ITEM,0); by+=32;
        sh_button(ren,bx,by,200,tr(STR_PM_SAUVER),false,false,SH_PM_ITEM,4); by+=32;
        sh_button(ren,bx,by,200,tr(STR_PM_TUTORIEL),false,false,SH_PM_ITEM,1); by+=32;
        sh_button(ren,bx,by,200,tr(STR_PM_MENU),false,false,SH_PM_ITEM,2); by+=32;
        sh_button(ren,bx,by,200,tr(STR_PM_QUITTER),false,false,SH_PM_ITEM,3);
    }
    if (g_save_pick||g_load_pick){
        int pw=460, px=(win_w-pw)/2, py=win_h/2-90;
        fill_rect(ren,0,0,win_w,win_h,(SDL_Color){0x05,0x08,0x0e,0x99});
        panel_bg(ren,px,py,pw,180);
        draw_text(ren,g_font_big,px+20,py+12,COL_COPPER, g_save_pick?tr(STR_PICK_SAUVER):tr(STR_PICK_CHARGER));
        for (int sl=1;sl<=3;sl++){
            SaveHeader hh; bool has=save_slot_info(sl,&hh);
            char lab[140];
            { char num[16]; snprintf(num,sizeof num,"%d",sl);
              if (has && hh.version==SAVE_VERSION) tr_fmt(lab,sizeof lab,STR_SLOT_LINE,num,hh.line);
              else if (has)                        tr_fmt(lab,sizeof lab,STR_SLOT_ANCIEN,num);
              else                                 tr_fmt(lab,sizeof lab,STR_SLOT_VIDE,num); }
            bool grise = g_load_pick && (!has || hh.version!=SAVE_VERSION);
            sh_button(ren,px+20,py+46+(sl-1)*34,pw-40,lab,false,grise,
                      g_save_pick?SH_SLOT_SAVE:SH_SLOT_LOAD, sl);
        }
        sh_button(ren,px+pw-130,py+150,110,"Retour",false,false,SH_PICK_CLOSE,0);
    }
    if (g_load_confirm>=0){
        int pw=440, px=(win_w-pw)/2, py=win_h/2-50;
        fill_rect(ren,0,0,win_w,win_h,(SDL_Color){0x05,0x08,0x0e,0x99});
        panel_bg(ren,px,py,pw,110);
        draw_text(ren,g_font,px+20,py+16,COL_PARCH,"Charger ? La partie en cours sera perdue.");
        sh_button(ren,px+30,py+58,150,"Charger",false,false,SH_LOADC_YES,g_load_confirm);
        sh_button(ren,px+pw-30-150,py+58,150,"Rester",true,false,SH_LOADC_NO,0);
    }
    if (g_show_tuto){
        int pw=620, ph=240, px=(win_w-pw)/2, py=(win_h-ph)/2;
        fill_rect(ren,0,0,win_w,win_h,(SDL_Color){0x05,0x08,0x0e,0x88});
        panel_bg(ren,px,py,pw,ph);
        draw_text(ren,g_font_big,px+20,py+14,COL_COPPER,tr_band(STR_TUTO_TITLE_0,g_tuto_page,7));
        { const char *t=tr_band(STR_TUTO_PAGE_0,g_tuto_page,7); int ly=py+48; char line[200]; int li=0;
          for (const char *c2=t;;c2++){
              if (*c2=='\n'||*c2==0){ line[li]=0; draw_text(ren,g_font,px+20,ly,COL_PARCH,line); ly+=20; li=0; if(!*c2)break; }
              else if (li<198) line[li++]=*c2;
          } }
        char pg[24]; { char num[16]; snprintf(num,sizeof num,"%d",g_tuto_page+1);
                       tr_fmt(pg,sizeof pg,STR_TUTO_PAGEFMT,num); }
        draw_text(ren,g_font_small,px+pw/2-12,py+ph-26,COL_DIM,pg);
        if (g_tuto_page>0) sh_button(ren,px+16,py+ph-34,90,tr(STR_TUTO_PREC),false,false,SH_TUTO_PREV,0);
        if (g_tuto_page<6) sh_button(ren,px+pw-106,py+ph-34,90,tr(STR_TUTO_SUIV),false,false,SH_TUTO_NEXT,0);
        draw_text(ren,g_font_small,px+16,py+ph-12,COL_DIM,"ESC ferme");
    }
    if (g_quit_confirm){
        int pw=430, px=(win_w-pw)/2, py=win_h/2-60;
        fill_rect(ren,0,0,win_w,win_h,(SDL_Color){0x05,0x08,0x0e,0x99});
        panel_bg(ren,px,py,pw,120);
        draw_text(ren,g_font,px+20,py+16,COL_PARCH,"Quitter ? Toute progression non sauvée sera perdue.");
        sh_button(ren,px+30,py+62,150,"Quitter",false,false,SH_QC_YES,0);
        sh_button(ren,px+pw-30-150,py+62,150,"Rester",true,false,SH_QC_NO,0);
    }
}

int main(int argc, char **argv) {
    bool shot = false, shot_tree = false, shot_war = false, shot_culture = false, shot_sidebar = false;
    int  shot_shell = 0;
    bool savetest = false;
    bool shot_cur = false;
    uint32_t shot_seed = 0; bool have_shot_seed = false;
    for (int i=1;i<argc;i++) {
        if (!strcmp(argv[i], "--shot")) shot = true;
        else if (!strcmp(argv[i], "--tree")) { shot = true; shot_tree = true; }
        else if (!strcmp(argv[i], "--sidebar")) { shot = true; shot_sidebar = true; }   /* tiroir Stocks + lentille Marché */
        else if (!strcmp(argv[i], "--shellshot") && i+1<argc) { shot=true; shot_shell=1+atoi(argv[++i]); }  /* 1=menu 2=setup 3=ouverture */
        else if (!strcmp(argv[i], "--savetest")) savetest=true;   /* vérif sauvegarde : sauver-recharger = continuation identique */
        else if (!strcmp(argv[i], "--curshot")) { shot=true; shot_cur=true; }   /* carte + champ des courants */
        else if (!strcmp(argv[i], "--war"))  { shot = true; shot_war  = true; }  /* §4 : capturer les armées sur la carte */
        else if (!strcmp(argv[i], "--culture")) { shot = true; shot_culture = true; }  /* §5 : vue culture */
        else { shot_seed = (uint32_t)strtoul(argv[i], NULL, 10); have_shot_seed = true; }
    }

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window *win = SDL_CreateWindow(
        "SCPS — Moteur de carte",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        WIN_W, WIN_H,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    SDL_Renderer *ren = SDL_CreateRenderer(win, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!win || !ren) { fprintf(stderr,"SDL: %s\n",SDL_GetError()); return 1; }
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);

    /* La prise audio (§5) : ouvre un device si présent ; sinon MUET, sans erreur
     * (conteneur/serveur sans carte → le release tourne quand même). */
    if (!audio_init()) fprintf(stderr,"[scps] audio : aucun device — silence.\n");
#ifdef SCPS_DEV
    dev_overlay_init(win, ren);   /* §6 : l'overlay de dev (F3) — build -DSCPS_DEV seul */
#endif

    /* Police diégétique (SDL_ttf) — DejaVu couvre les accents français. */
    if (TTF_Init() != 0) fprintf(stderr, "TTF_Init: %s\n", TTF_GetError());
    static const char *font_paths[] = {
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/dejavu/DejaVuSans.ttf",
        "/Library/Fonts/Arial.ttf",
        "C:/Windows/Fonts/arial.ttf",
    };
    for (size_t i=0; i<sizeof(font_paths)/sizeof(font_paths[0]) && !g_font; i++) {
        g_font     = TTF_OpenFont(font_paths[i], 14);
        g_font_big = TTF_OpenFont(font_paths[i], 18);
        g_font_small = TTF_OpenFont(font_paths[i], 10);
    }
    if (!g_font) fprintf(stderr, "[scps] police introuvable — panneau sans texte\n");

    /* Simulation branchée sous la carte (alimente bandeau + panneau). */
    Sim sim = {0};
    sim.econ = (WorldEconomy*)    malloc(sizeof(WorldEconomy));
    sim.wp   = (WorldProsperity*) malloc(sizeof(WorldProsperity));
    sim.wl   = (WorldLegitimacy*) malloc(sizeof(WorldLegitimacy));
    sim.net  = (TradeNetwork*)    malloc(sizeof(TradeNetwork));
    sim.ts   = (TechState*)       calloc(SCPS_MAX_COUNTRY, sizeof(TechState));
    sim.sc   = (Statecraft*)      malloc(sizeof(Statecraft));
    sim.ag   = (AgencyState*)     malloc(sizeof(AgencyState));
    sim.ev   = (EventsState*)     malloc(sizeof(EventsState));
    sim.drift= (ModifierStack*)   malloc(sizeof(ModifierStack));
    sim.labor= (LaborEcon*)       malloc(sizeof(LaborEcon));
    sim.dp   = (DiploState*)      malloc(sizeof(DiploState));
    sim.rn   = (RouteNetwork*)    malloc(sizeof(RouteNetwork));
    sim.rs   = (RevoltState*)     malloc(sizeof(RevoltState));
    sim.host = (WarHost*)         malloc(sizeof(WarHost));
    sim.camp = (Campaign*)        malloc(sizeof(Campaign));
    sim.missions = (MissionsState*) malloc(sizeof(MissionsState));
    sim.navy = (NavyState*)       malloc(sizeof(NavyState));
    sim.ai   = (AiActor*)         calloc(SCPS_MAX_COUNTRY, sizeof(AiActor));
    sim.ai_on= (bool*)            calloc(SCPS_MAX_COUNTRY, sizeof(bool));

    int win_w = WIN_W, win_h = WIN_H;
    PixBuf pb = pixbuf_create(ren, win_w, win_h);
    PixBuf mm_pb = pixbuf_create(ren, MM_W, MM_H);   /* §5 : la minicarte (taille fixe) */

    World *world = (World*)malloc(sizeof(World));
    if (!world) { fprintf(stderr,"OOM\n"); return 1; }

    uint32_t  seed     = have_shot_seed ? shot_seed : (uint32_t)time(NULL);
    WorldParams params = worldparams_default(seed);
    ViewMode  mode     = VIEW_TERRAIN;
    GameSpeed  speed   = SPEED_1;        /* le temps coule (Espace = pause) */
    double     day_accum = 0.0;
    uint32_t   last_ticks = SDL_GetTicks();
    int       selected = -1;
    bool      show_tree = false;     /* superposition : l'arbre de tech (Tab) */
    bool      dirty    = true;
    bool      running  = true;
    bool      regen    = false;   /* demande de régénération du monde */

    /* Caméra : ajuste pour montrer toute la carte */
    Cam cam;
    cam_fit(&cam, win_w, win_h);

    /* Paramètres de rendu */
    RenderParams rp = {
        .cam_ox = cam.ox, .cam_oy = cam.oy, .cam_scale = cam.scale,
        .selected_prov = -1,
        .show_rivers = true, .show_borders = true, .show_grid = false
    };

    /* Pan à la souris */
    bool  panning = false;
    int   pan_sx = 0, pan_sy = 0;

    printf("[scps] Génération (graine %u)…\n", seed);
    world_generate(world, &params);
    sim_rebuild(&sim, world);   /* peuple + simule 30 ans (bandeau + panneau) */
    g_gs = shot ? GS_PLAYING : GS_MENU;      /* le jeu COMMENCE au menu (le monde respire derrière) */
    g_stage = params;

    /* ── --savetest : LA VÉRIF du brief (4) — sauver puis recharger restitue la
     * partie AU JOUR PRÈS : on avance N jours, on sauve, on avance M jours (digest A) ;
     * on recharge, on ré-avance M jours (digest B) ; A doit ÉGALER B. ── */
    if (savetest){
        #define DIGEST(tag) do{ double dpop=0,dgld=0; long dtech=0; unsigned long downer=5381; \
            for (int r=0;r<sim.econ->n_regions;r++){ const RegionEconomy *re=&sim.econ->region[r]; \
                for (int c2=0;c2<CLASS_COUNT;c2++) dpop+=re->strata[c2].pop; \
                dgld+=re->treasury; downer=downer*33+(unsigned long)(re->owner+2); } \
            for (int c2=0;c2<SCPS_MAX_COUNTRY;c2++) dtech+=sim.ts[c2].n_unlocked; \
            snprintf(tag,sizeof tag,"day=%d pop=%.1f or=%.1f tech=%ld own=%lu pays=%d frondes=%d", \
                     sim.day,dpop,dgld,dtech,downer,world->n_countries,sim.dp->n_frondes); }while(0)
        char dA[200], dB[200];
        for (int d2=0;d2<600;d2++) sim_day(&sim, world);
        if (!game_save(3, world, &sim, &params)){ printf("savetest: ÉCHEC d'écriture\n"); return 1; }
        for (int d2=0;d2<400;d2++) sim_day(&sim, world);
        DIGEST(dA);
        int rc=game_load(3, world, &sim, &params);
        if (rc!=0){ printf("savetest: ÉCHEC de lecture (%d)\n", rc); return 1; }
        for (int d2=0;d2<400;d2++) sim_day(&sim, world);
        DIGEST(dB);
        bool same = (strcmp(dA,dB)==0);
        /* 2e contrôle : un octet ALTÉRÉ au milieu du payload chiffré → REFUS net. */
        bool tamper_ok=false;
        { FILE *tf=fopen(save_slot_path(3),"r+b");
          if (tf){ SaveHeader th;
            if (fread(&th,sizeof th,1,tf)==1){
                long mid=(long)sizeof th + (long)th.payload/2;
                fseek(tf,mid,SEEK_SET); int c2=fgetc(tf);
                fseek(tf,mid,SEEK_SET); fputc(c2^0x5A,tf);
            }
            fclose(tf);
            tamper_ok = (game_load(3, world, &sim, &params)!=0);   /* doit ÉCHOUER */
          } }
        printf("A: %s\nB: %s\n  altération d'un octet → %s\n"
               "══════════════════════════════════════\n BILAN : %d réussis, %d échoués\n",
               dA, dB, tamper_ok?"REFUSÉE (empreinte)":"ACCEPTÉE (BUG)",
               (same?1:0)+(tamper_ok?1:0), (same?0:1)+(tamper_ok?0:1));
        return (same&&tamper_ok)?0:1;
    }
    printf("[scps] Prêt. TAB/1-0=vues  T=arbre de tech  E/D/S/A/F=sidebar (éco·démo·stocks·armée·filtres)  Z=cadrer  R=regénère  clic=territoire\n");
    printf("[scps] Réglages (régénèrent) : c=continents g=âge e=érosion\n");
    printf("       l=terres m=montagnes t=température h=humidité (Maj=baisse)\n");

    /* Mode capture (--shot) : une frame (carte + bandeau + panneau sur une
     * province peuplée), sérialisée en PPM, puis sortie — vérifie l'UI sans écran. */
    if (shot) {
        if (shot_tree || shot_sidebar) for (int d=0; d<60*365; d++) sim_day(&sim, world);  /* laisse le monde POUSSER */
        int cid = country_for_panel(world, -1);
        int pcap = (cid>=0 && cid<world->n_countries) ? world->country[cid].capital_prov : -1;
        selected = (pcap>=0) ? pcap : 0;
        rp.cam_ox=cam.ox; rp.cam_oy=cam.oy; rp.cam_scale=cam.scale; rp.selected_prov=selected;
        SDL_RenderClear(ren);
        if (shot_cur) {
            rp.region_tint=NULL;
            render_map(world, pb.pixels, pb.w, pb.h, &rp, VIEW_TERRAIN); pixbuf_upload(&pb);
            if (pb.tex) SDL_RenderCopy(ren, pb.tex, NULL, NULL);
            for (int sy=8; sy<win_h-8; sy+=12) for (int sx=8; sx<win_w-8; sx+=12){
                int cx2=(int)(sx/cam.scale+cam.ox), cy2=(int)(sy/cam.scale+cam.oy);
                if (cx2<0||cy2<0||cx2>=SCPS_W||cy2>=SCPS_H) continue;
                const Cell *cc=scps_cellc(world,cx2,cy2);
                if (cc->sea<=SEA_CABOTAGE) continue;
                float vx2=cc->cur_vx/100.f, vy2=cc->cur_vy/100.f;
                float m=sqrtf(vx2*vx2+vy2*vy2); if (m<0.10f) continue;
                float l=(cc->sea==SEA_COURANT)?9.f:5.f;
                SDL_Color cl=(cc->sea==SEA_COURANT)?COL_COPPER:(SDL_Color){0x6a,0x8a,0xb0,0xff};
                SDL_SetRenderDrawColor(ren,cl.r,cl.g,cl.b,(cc->sea==SEA_COURANT)?0xE0:0x90);
                SDL_RenderDrawLine(ren,sx,sy,sx+(int)(vx2/m*l),sy+(int)(vy2/m*l));
            }
        } else if (shot_shell && sim.ready && g_font) {
            ViewMode smode0=VIEW_TERRAIN; rp.region_tint=NULL;
            render_map(world, pb.pixels, pb.w, pb.h, &rp, smode0); pixbuf_upload(&pb);
            if (pb.tex) SDL_RenderCopy(ren, pb.tex, NULL, NULL);
            zone_reset();
            g_gs = (shot_shell==1)?GS_MENU:(shot_shell==2)?GS_SETUP:GS_OPENING;
            if (g_gs==GS_OPENING){ snprintf(g_open_terre_line,sizeof g_open_terre_line,
                "Nulle plaine libre — vous voilà sur la côte."); }
            shell_draw(ren,win_w,win_h,world,&sim,&g_stage);
        } else if (shot_tree && sim.ready && g_font) {
            g_tree_open = TECH_CONSCRIPTION;                                  /* démo : un anneau de sous-techs ouvert (à gauche, loin du survol) */
            draw_tech_tree(ren, win_w, win_h, sim.econ, sim.ts, world, cid);   /* l'arbre concentrique du pays */
            if (g_tree_demo>=0){                                              /* démo : un survol (boîte 2 colonnes) */
                draw_box(ren, g_tree_x[g_tree_demo]-9, g_tree_y[g_tree_demo]-9, 18,18, COL_PARCH);
                draw_hover_footer(ren, win_w, win_h, g_tree_x[g_tree_demo], g_tree_y[g_tree_demo]);
            }
        } else {
            if (shot_war)                    /* §4 : laisse une guerre mûrir → des armées sur la carte */
                for (int y=0; y<120 && !any_field_army(&sim, world); y++)
                    for (int d=0; d<365; d++) sim_day(&sim, world);
            ViewMode smode = shot_culture ? VIEW_CULTURE : VIEW_COUNTRIES;
            rp.region_tint = NULL;
            if (shot_sidebar){                       /* démo sidebar : tiroir Stocks + lentille Marché */
                g_sb.tab=SBT_STOCKS; g_sb.anim=1.f; g_sb.lens=LENS_MARCHE;
                static uint32_t lt[SCPS_MAX_REG];
                map_lens_tints(sim.econ, sim.wl, g_sb.lens, lt);
                rp.region_tint = lt; smode = VIEW_CULTURE;
            } else if (smode==VIEW_CULTURE){
                static uint32_t tnt[SCPS_MAX_REG];
                for (int r=0;r<sim.econ->n_regions && r<SCPS_MAX_REG;r++)
                    tnt[r]=ethos_tint((int)sim.econ->region[r].culture.ethos);
                rp.region_tint = tnt;
            }
            render_map(world, pb.pixels, pb.w, pb.h, &rp, smode);
            pixbuf_upload(&pb);
            if (pb.tex) SDL_RenderCopy(ren, pb.tex, NULL, NULL);
            if (mm_pb.pixels){ RenderParams mmp=rp; mmp.selected_prov=-1;
                minimap_fit(&mmp.cam_scale,&mmp.cam_ox,&mmp.cam_oy);
                render_map(world, mm_pb.pixels, mm_pb.w, mm_pb.h, &mmp, smode); pixbuf_upload(&mm_pb); }
            if (sim.ready && g_font) {
                zone_reset(); bslot_reset(); orow_reset(); modebtn_reset(); topbtn_reset();
                draw_army_markers(ren, &cam, &sim, world, win_w, win_h);   /* §4 : les armées sur la carte */
                draw_topbar(ren, win_w, &sim, world, cid, speed);
                draw_outliner(ren, win_w, win_h, &sim, world);            /* §6 : l'outliner */
                draw_mode_buttons(ren, win_h, smode);                     /* §5 : modes de carte */
                draw_minimap(ren, &mm_pb, win_w, win_h, &cam);            /* §5 : la minicarte */
                draw_province_panel(ren, win_w, win_h, world, sim.econ, sim.wp, sim.wl, sim.drift, selected);
                if (shot_sidebar) draw_sidebar(ren, win_w, win_h, &sim, world, smode, selected);
            }
        }
        SDL_RenderPresent(ren);
        uint32_t *cap = (uint32_t*)malloc((size_t)win_w*win_h*4);
        if (cap && SDL_RenderReadPixels(ren, NULL, SDL_PIXELFORMAT_ARGB8888, cap, win_w*4)==0)
            save_ppm("scps_ui.ppm", cap, win_w, win_h);
        else fprintf(stderr, "[scps] capture impossible\n");
        free(cap);
        running = false;
    }

    while (running) {
        SDL_Event ev;
#ifdef SCPS_DEV
        dev_overlay_input_begin();
#endif
        while (SDL_PollEvent(&ev)) {
#ifdef SCPS_DEV
            if (g_dev_overlay && ev.key.keysym.sym!=SDLK_F3
                && dev_overlay_handle_event(&ev)) continue;   /* Nuklear a la main (sauf le toggle F3) */
#endif
            switch (ev.type) {

            case SDL_QUIT: g_quit_confirm=true; dirty=true; break;   /* la croix passe par la CONFIRMATION */

            case SDL_WINDOWEVENT:
                if (ev.window.event == SDL_WINDOWEVENT_RESIZED) {
                    win_w = ev.window.data1;
                    win_h = ev.window.data2;
                    pixbuf_destroy(&pb);
                    pb = pixbuf_create(ren, win_w, win_h);
                    dirty = true;
                }
                break;

            case SDL_MOUSEWHEEL: {
                int mx, my; SDL_GetMouseState(&mx, &my);
                if (sidebar_wheel(mx, my, ev.wheel.y)) { dirty=true; break; }   /* le tiroir défile (hit-test d'abord) */
                float factor = (ev.wheel.y > 0) ? 1.25f : 0.80f;
                cam_zoom(&cam, factor, (float)mx, (float)my);
                dirty = true;
                break;
            }

            case SDL_MOUSEBUTTONDOWN:
                if (ev.button.button == SDL_BUTTON_MIDDLE ||
                    ev.button.button == SDL_BUTTON_RIGHT) {
                    panning = true;
                    pan_sx = ev.button.x;
                    pan_sy = ev.button.y;
                } else if (ev.button.button == SDL_BUTTON_LEFT) {
                    /* LE SHELL capte d'abord (menu/création/ouverture + surcouches). */
                    if (g_quit_confirm||g_show_tuto||g_pause_menu||g_save_pick||g_load_pick||g_load_confirm>=0||g_gs!=GS_PLAYING){
                        int hit=-1;
                        for (int i2=0;i2<g_nshhits;i2++){ SDL_Rect *r2=&g_shhits[i2].r;
                            if (ev.button.x>=r2->x&&ev.button.x<r2->x+r2->w&&ev.button.y>=r2->y&&ev.button.y<r2->y+r2->h){ hit=i2; break; } }
                        if (hit>=0){
                            ShellHit *sh2=&g_shhits[hit];
                            switch(sh2->kind){
                                case SH_QC_YES: running=false; break;
                                case SH_PICK_CLOSE: g_save_pick=g_load_pick=0; break;
                                case SH_SLOT_SAVE:
                                    if (game_save(sh2->a, world, &sim, &params))
                                        printf("\n[scps] Sauvé — slot %d.\n", sh2->a);
                                    else printf("\n[scps] Échec d'écriture (slot %d).\n", sh2->a);
                                    g_save_pick=0;
                                    break;
                                case SH_SLOT_LOAD:
                                    if (g_game_started){ g_load_confirm=sh2->a; g_load_pick=0; }
                                    else {
                                        int rc=game_load(sh2->a, world, &sim, &params);
                                        if (rc==0){ seed=params.seed; g_load_pick=0; g_pause_menu=false;
                                            g_game_started=true; speed=SPEED_PAUSE; g_sbc.day=-1; selected=-1;
                                            sh_center_capital(world,&sim,&cam,win_w,win_h);
                                            g_gs=GS_PLAYING; }
                                        else printf("\n[scps] %s\n", rc==2?"Sauvegarde d'une ère antérieure — refusée poliment.":"Slot illisible.");
                                    }
                                    break;
                                case SH_LOADC_YES: {
                                    int rc=game_load(sh2->a, world, &sim, &params);
                                    if (rc==0){ seed=params.seed; g_pause_menu=false; speed=SPEED_PAUSE;
                                        g_sbc.day=-1; selected=-1; g_game_started=true;
                                        sh_center_capital(world,&sim,&cam,win_w,win_h); g_gs=GS_PLAYING; }
                                    else printf("\n[scps] %s\n", rc==2?"Sauvegarde d'une ère antérieure — refusée poliment.":"Slot illisible.");
                                    g_load_confirm=-1;
                                } break;
                                case SH_LOADC_NO: g_load_confirm=-1; break;
                                case SH_QC_NO:  g_quit_confirm=false; break;
                                case SH_TUTO_PREV: if(g_tuto_page>0)g_tuto_page--; break;
                                case SH_TUTO_NEXT: if(g_tuto_page<6)g_tuto_page++; break;
                                case SH_MENU_ITEM:
                                    if (sh2->a==0){ g_stage=params; g_gs=GS_SETUP; }
                                    else if (sh2->a==1){ g_load_pick=1; }
                                    else if (sh2->a==2){ g_show_tuto=true; g_tuto_page=0; }
                                    else if (sh2->a==3) g_quit_confirm=true;
                                    else if (sh2->a==4) lang_set(lang_get()==LANG_FR?LANG_EN:LANG_FR);
                                    break;
                                case SH_SLIDER_DN: sh_apply_slider(&g_stage,sh2->a,-1); break;
                                case SH_SLIDER_UP: sh_apply_slider(&g_stage,sh2->a,+1); break;
                                case SH_SEED_DICE: g_stage.seed ^= (uint32_t)SDL_GetTicks()*2654435761u; if(!g_stage.seed)g_stage.seed=1u; break;
                                case SH_PICK_ETHOS: g_setup_ethos=sh2->a; break;
                                case SH_PICK_RACE:  g_setup_race=sh2->a; g_player_race=(SpeciesArchetype)sh2->a; break;
                                case SH_PICK_TERRE: g_setup_terre=sh2->a; break;
                                case SH_BACK: g_gs=GS_MENU; break;
                                case SH_FORGER:
                                    params=g_stage; seed=params.seed;
                                    sh_draw_litanie(ren,win_w,win_h,params.seed); SDL_RenderPresent(ren);
                                    regen=true; g_pending_open=true;
                                    break;
                                case SH_OPEN_REROLL:
                                    params.seed ^= (uint32_t)SDL_GetTicks()*2654435761u; if(!params.seed)params.seed=1u;
                                    seed=params.seed;
                                    sh_draw_litanie(ren,win_w,win_h,params.seed); SDL_RenderPresent(ren);
                                    regen=true; g_pending_open=true;
                                    break;
                                case SH_OPEN_GO: g_gs=GS_PLAYING; g_game_started=true; break;   /* la pause TIENT : Espace sera le premier geste */
                                case SH_PM_ITEM:
                                    if (sh2->a==0) g_pause_menu=false;
                                    else if (sh2->a==4){ g_save_pick=1; }
                                    else if (sh2->a==1){ g_show_tuto=true; g_tuto_page=0; }
                                    else if (sh2->a==2){ g_pause_menu=false; g_gs=GS_MENU; speed=SPEED_PAUSE; }
                                    else g_quit_confirm=true;
                                    break;
                            }
                        }
                        dirty=true; break;
                    }
                    /* cran de VITESSE cliquable (topbar) */
                    if (g_gs==GS_PLAYING && ev.button.x>=g_speed_zone.x && ev.button.x<g_speed_zone.x+g_speed_zone.w
                        && ev.button.y>=g_speed_zone.y && ev.button.y<g_speed_zone.y+g_speed_zone.h){
                        speed=(GameSpeed)((speed+1)%SPEED_COUNT);
                        if (speed!=SPEED_PAUSE) g_last_speed=speed;
                        dirty=true; break;
                    }
                    /* SIDEBAR d'abord (rail + tiroir) : la décision capte le clic. */
                    if (sim.ready && !show_tree) {
                        bool sbd=false;
                        if (sidebar_click(&sim, world, ev.button.x, ev.button.y, &mode, &sbd)){
                            if (sbd) g_sbc.day=-1;           /* décision → recalcul des agrégats */
                            dirty=true; break;
                        }
                    }
                    /* ARBRE OUVERT : un clic sur une tech ouvre/ferme l'anneau de ses
                     * SOUS-TECHS (clic ailleurs = ferme). L'écran de l'arbre capte le clic. */
                    if (show_tree) {
                        int hit=-1;
                        for (int i=0;i<TECH_COUNT;i++){
                            if (!g_tree_x[i] && !g_tree_y[i]) continue;        /* nœud non positionné (base) */
                            int dx=ev.button.x-g_tree_x[i], dy=ev.button.y-g_tree_y[i];
                            if (dx*dx+dy*dy <= 11*11){ hit=i; break; }
                        }
                        g_tree_open = (hit>=0 && hit!=g_tree_open) ? hit : -1;  /* bascule / ferme */
                        dirty=true; break;
                    }
                    /* §1 : le bandeau est un SOMMAIRE — un clic sur une RESSOURCE ouvre
                     * son système. Le Savoir ouvre l'ARBRE DE TECH (qui existe) ; les
                     * autres écrans (finances, subsistance, chaînes, diplomatie) sont à
                     * venir — le clic les annonce. */
                    int tb=-1;
                    for (int i=0;i<g_ntopbtns;i++){ SDL_Rect *r=&g_topbtns[i].r;
                        if (ev.button.x>=r->x && ev.button.x<r->x+r->w &&
                            ev.button.y>=r->y && ev.button.y<r->y+r->h){ tb=i; break; } }
                    if (tb>=0){
                        /* le bandeau est un SOMMAIRE : chaque ressource ouvre SON panneau de la sidebar */
                        switch (g_topbtns[tb].sys){
                            case SYS_TECH:        show_tree = !show_tree; g_tree_open = -1; break;
                            case SYS_FINANCES:    g_sb.tab=SBT_ECO;    g_sb.eco_sub=1; break;  /* Or → Marché */
                            case SYS_SUBSISTANCE: g_sb.tab=SBT_DEMO;   break;                   /* Nourriture → Démographie */
                            case SYS_CHAINES:     g_sb.tab=SBT_STOCKS; break;                   /* Matériaux → Stocks */
                            default:              g_sb.tab=SBT_ECO;    g_sb.eco_sub=0; break;  /* Diplomatie → Commerce/embargo */
                        }
                        dirty=true; break;
                    }
                    /* §5 : un clic sur un BOUTON DE MODE change la vue de carte. */
                    int mb=-1;
                    for (int i=0;i<g_nmodebtns;i++){ SDL_Rect *r=&g_modebtns[i].r;
                        if (ev.button.x>=r->x && ev.button.x<r->x+r->w &&
                            ev.button.y>=r->y && ev.button.y<r->y+r->h){ mb=i; break; } }
                    if (mb>=0){ mode=(ViewMode)g_modebtns[mb].mode; dirty=true; break; }
                    /* §5 : un clic sur la MINICARTE recentre la caméra. */
                    { SDL_Rect m; minimap_rect(win_w,win_h,&m);
                      if (ev.button.x>=m.x && ev.button.x<m.x+m.w && ev.button.y>=m.y && ev.button.y<m.y+m.h){
                          float sc,ox,oy; minimap_fit(&sc,&ox,&oy);
                          float wx=(ev.button.x-m.x)/sc+ox, wy=(ev.button.y-m.y)/sc+oy;
                          cam.ox = wx - win_w/(2.f*cam.scale); cam.oy = wy - win_h/(2.f*cam.scale);
                          dirty=true; break;
                      } }
                    /* §4 panneau : un clic sur un SLOT de bâtiment bâtit l'édifice
                     * (payé au marché, en jours) — pas de bouton « Bâtir ». */
                    int hit=-1;
                    for (int i=0;i<g_nbslots;i++){ SDL_Rect *r=&g_bslots[i].r;
                        if (ev.button.x>=r->x && ev.button.x<r->x+r->w &&
                            ev.button.y>=r->y && ev.button.y<r->y+r->h){ hit=i; break; } }
                    if (hit>=0){
                        if (sim.ready && agency_build(sim.ag, sim.econ, g_bslots[hit].reg, g_bslots[hit].edifice))
                            printf("\n[scps] Bâtir %s (région %d) — payé au marché, construit en jours.\n",
                                   edifice_name(g_bslots[hit].edifice), g_bslots[hit].reg);
                        else
                            printf("\n[scps] Bâtir : trésor insuffisant pour les matériaux.\n");
                        dirty = true;
                        break;
                    }
                    /* Note armée : clic sur « Remplir » → recomplète l'armée (territoire ami). */
                    if (g_refill_owner>=0 &&
                        ev.button.x>=g_refill_btn.x && ev.button.x<g_refill_btn.x+g_refill_btn.w &&
                        ev.button.y>=g_refill_btn.y && ev.button.y<g_refill_btn.y+g_refill_btn.h){
                        int added = campaign_refill(sim.camp, g_refill_owner, sim.labor);
                        printf("\n[scps] Remplir : +%d paquet(s) levé(s) (territoire ami, payé au marché).\n", added);
                        dirty=true; break;
                    }
                    /* §6 OUTLINER : clic sur le marteau → bâtir ; clic sur la ligne → saut. */
                    int orhit=-1;
                    for (int i=0;i<g_norows;i++){
                        SDL_Rect *hm=&g_orows[i].ham;
                        if (g_orows[i].hammer_reg>=0 && hm->w>0 &&
                            ev.button.x>=hm->x && ev.button.x<hm->x+hm->w &&
                            ev.button.y>=hm->y && ev.button.y<hm->y+hm->h){
                            if (sim.ready) agency_build(sim.ag, sim.econ, g_orows[i].hammer_reg, EDI_TRIBUNAL);
                            dirty=true; orhit=-2; break;
                        }
                        SDL_Rect *rw=&g_orows[i].row;
                        if (ev.button.x>=rw->x && ev.button.x<rw->x+rw->w &&
                            ev.button.y>=rw->y && ev.button.y<rw->y+rw->h){ orhit=i; break; }
                    }
                    if (orhit==-2) break;                       /* marteau cliqué */
                    if (orhit>=0){ selected = g_orows[orhit].prov; dirty=true; break; }   /* saut à la région */
                    /* Sinon : sélectionner la province au clic */
                    int cx = (int)(ev.button.x / cam.scale + cam.ox);
                    int cy = (int)(ev.button.y / cam.scale + cam.oy);
                    if (cx>=0&&cx<SCPS_W&&cy>=0&&cy<SCPS_H) {
                        int p = (int)scps_cellc(world, cx, cy)->province;
                        if (p != selected) {
                            selected = p;
                            if (p >= 0) print_province_info(world, p);
                        } else {
                            selected = -1;
                        }
                        dirty = true;
                    }
                }
                break;

            case SDL_MOUSEBUTTONUP:
                if (ev.button.button == SDL_BUTTON_MIDDLE ||
                    ev.button.button == SDL_BUTTON_RIGHT)
                    panning = false;
                break;

            case SDL_MOUSEMOTION:
                if (panning) {
                    cam_pan(&cam, (float)(ev.motion.x - pan_sx),
                                  (float)(ev.motion.y - pan_sy));
                    pan_sx = ev.motion.x;
                    pan_sy = ev.motion.y;
                    dirty = true;
                }
                break;

            case SDL_KEYDOWN:
                /* réglage worldgen ±0.25 (Maj = baisser) — utilisé par g/l/m/h et Ctrl+e/t */
                #define ADJ(field) { bool dn=(ev.key.keysym.mod&KMOD_SHIFT); \
                    params.field += dn?-0.25f:0.25f; \
                    if(params.field<-0.001f)params.field=1.f; \
                    else if(params.field>1.001f)params.field=0.f; \
                    regen=true; }
                switch (ev.key.keysym.sym) {
                case SDLK_ESCAPE:
                    /* L'ÉCHELLE ESC : fermer la couche du dessus — JAMAIS quitter l'appli. */
                    if      (g_quit_confirm){ g_quit_confirm=false; }
                    else if (g_load_confirm>=0){ g_load_confirm=-1; }
                    else if (g_save_pick||g_load_pick){ g_save_pick=g_load_pick=0; }
                    else if (g_show_tuto)   { g_show_tuto=false; }
                    else if (g_pause_menu)  { g_pause_menu=false; }
                    else if (g_gs==GS_SETUP){ g_gs=GS_MENU; }
                    else if (g_gs==GS_OPENING){ g_gs=GS_MENU; }
                    else if (g_gs==GS_MENU) { /* rien : on ne quitte que par le bouton */ }
                    else if (show_tree)     { show_tree=false; g_tree_open=-1; }
                    else if (g_sb.tab>=0)   { g_sb.tab=-1; }
                    else { g_pause_menu=true; g_last_speed=(speed==SPEED_PAUSE)?g_last_speed:speed; speed=SPEED_PAUSE; }
                    dirty=true; break;
                /* --- Contrôle du TEMPS (§1) : Espace = pause ; +/- = vitesse --- */
                case SDLK_SPACE:
                    if (g_gs!=GS_PLAYING||g_pause_menu||g_show_tuto||g_quit_confirm) break;
                    if (speed==SPEED_PAUSE) speed=g_last_speed;            /* on repart sur SON cran */
                    else { g_last_speed=speed; speed=SPEED_PAUSE; }
                    break;
                case SDLK_t:     /* T = l'Arbre de Tech (Ctrl+T = réglage température) */
                    if (SDL_GetModState() & KMOD_CTRL) { ADJ(temperature) }
                    else { show_tree = !show_tree; g_tree_open = -1; }
                    break;
                /* --- SIDEBAR (§sidebar) : E/D/S/A/F = onglets d'empire --- */
                case SDLK_e:
                    if (SDL_GetModState() & KMOD_CTRL) { ADJ(erosion) }
                    else { g_sb.tab=(g_sb.tab==SBT_ECO)?-1:SBT_ECO; dirty=true; }
                    break;
                case SDLK_d:     g_sb.tab=(g_sb.tab==SBT_DEMO)?-1:SBT_DEMO;     dirty=true; break;
                case SDLK_s:     g_sb.tab=(g_sb.tab==SBT_STOCKS)?-1:SBT_STOCKS; dirty=true; break;
                case SDLK_a:     g_sb.tab=(g_sb.tab==SBT_ARMEE)?-1:SBT_ARMEE;   dirty=true; break;
                case SDLK_PLUS: case SDLK_EQUALS: case SDLK_KP_PLUS:
                    if (speed<SPEED_5) speed++;
                    if (speed==SPEED_PAUSE) speed=SPEED_1;
                    if (speed!=SPEED_PAUSE) g_last_speed=speed;
                    break;
                case SDLK_MINUS: case SDLK_KP_MINUS:
                    if (speed>SPEED_1) speed--;
                    if (speed!=SPEED_PAUSE) g_last_speed=speed;
                    break;
                /* --- ACTION (§4) : bâtir, via la couche d'agency, en JOURS --- */
                case SDLK_b:
                    if (sim.ready && selected>=0 && selected<world->n_provinces) {
                        int reg = world->province[selected].region;
                        if (reg>=0 && agency_build(sim.ag, sim.econ, reg, EDI_TRIBUNAL))
                            printf("\n[scps] Action : Tribunal mis en file (région %d) — payé au marché, construit en jours.\n", reg);
                        else
                            printf("\n[scps] Tribunal : trésor insuffisant pour acheter les matériaux.\n");
                    }
                    break;
                case SDLK_F12:   viewer_screenshot(ren); break;   /* capture PNG horodatée */
#ifdef SCPS_DEV
                case SDLK_F3:    g_dev_overlay=!g_dev_overlay; break;   /* §6 : l'inspecteur brut */
#endif
                case SDLK_TAB:   mode=(ViewMode)((mode+1)%VIEW_COUNT); dirty=true; printf("\n"); break;
                case SDLK_1:     mode=VIEW_TERRAIN;     dirty=true; break;
                case SDLK_2:     mode=VIEW_POLITICAL;   dirty=true; break;
                case SDLK_3:     mode=VIEW_REGIONS;     dirty=true; break;
                case SDLK_4:     mode=VIEW_COUNTRIES;   dirty=true; break;
                case SDLK_5:     mode=VIEW_CONTINENTS;  dirty=true; break;
                case SDLK_6:     mode=VIEW_HEIGHT;      dirty=true; break;
                case SDLK_7:     mode=VIEW_FERTILITY;   dirty=true; break;
                case SDLK_8:     mode=VIEW_MOISTURE;    dirty=true; break;
                case SDLK_9:     mode=VIEW_TEMPERATURE; dirty=true; break;
                case SDLK_0:     mode=VIEW_RESOURCES;    dirty=true; break;
                case SDLK_i:     mode=VIEW_HABITABILITY; dirty=true; break;
                case SDLK_f:     g_sb.tab=(g_sb.tab==SBT_FILTRES)?-1:SBT_FILTRES; dirty=true; break;
                case SDLK_z:     cam_fit(&cam,win_w,win_h); dirty=true; break;   /* Z = cadrer la carte */
                case SDLK_r:
                    seed ^= (uint32_t)time(NULL) * 2654435761u;
                    params.seed = seed;
                    regen = true;
                    break;

                /* --- Réglages de génération (Maj = diminuer) — e/t passés sous Ctrl (sidebar/arbre) --- */
                case SDLK_c: {  /* nombre de continents 1..6 */
                    bool dn = (ev.key.keysym.mod & KMOD_SHIFT);
                    params.n_continents += dn?-1:1;
                    if (params.n_continents<1) params.n_continents=6;
                    if (params.n_continents>6) params.n_continents=1;
                    regen=true; break;
                }
                case SDLK_g:  ADJ(world_age)   break;  /* âge du monde   */
                case SDLK_l:  ADJ(land_amount) break;  /* quantité terre */
                case SDLK_m:  ADJ(mountains)   break;  /* relief         */
                case SDLK_h:  ADJ(humidity)    break;  /* humidité       */
                #undef ADJ
                default: break;
                }
                break;
            }
        }
#ifdef SCPS_DEV
        dev_overlay_input_end();
#endif

        if (regen) {
            printf("\n[scps] Génération — graine %u · continents %d · âge %.2f"
                   " · érosion %.2f · terres %.2f · relief %.2f · temp %.2f · humid %.2f\n",
                   params.seed, params.n_continents, params.world_age, params.erosion,
                   params.land_amount, params.mountains, params.temperature, params.humidity);
            world_generate(world, &params);
            sim_rebuild(&sim, world);
            selected = -1; dirty = true; regen = false;
            if (g_pending_open){ g_pending_open=false;
                sh_enter_opening(world,&sim,&cam,win_w,win_h,&speed); }
        }

        /* --- LE TEMPS COULE : la partie avance selon la vitesse (§1) --- */
        {
            uint32_t now = SDL_GetTicks();
            double frame_dt = (now - last_ticks) / 1000.0; last_ticks = now;
            if (frame_dt > 0.25) frame_dt = 0.25;               /* anti spirale de la mort */
            if (sim.ready && speed != SPEED_PAUSE && sim.year < GAME_YEARS) {
                day_accum += frame_dt * DAYS_PER_SEC[speed];
                int steps=0;
                while (day_accum >= 1.0 && steps < 40) { sim_day(&sim, world); day_accum -= 1.0; steps++; }
                if (steps>0) dirty = true;                      /* propriété/overlays peuvent changer */
            } else day_accum = 0.0;
        }

        if (dirty && pb.pixels) {
            rp.cam_ox = cam.ox; rp.cam_oy = cam.oy; rp.cam_scale = cam.scale;
            rp.selected_prov = selected;
            rp.region_tint = NULL;
            ViewMode rmode = mode;
            if (g_sb.lens!=LENS_NONE && sim.ready) {
                /* LENTILLE readout (§6) : la membrane calcule les teintes (bandes
                 * discrètes) ; le viewer ne fait que les poser sur la carte. */
                static uint32_t g_lens_tint[SCPS_MAX_REG];
                map_lens_tints(sim.econ, sim.wl, g_sb.lens, g_lens_tint);
                rp.region_tint = g_lens_tint;
                rmode = VIEW_CULTURE;                       /* le chemin de rendu teinté par région */
            } else if ((mode==VIEW_CULTURE || mode==VIEW_FAITH) && sim.ready) {
                static uint32_t g_region_tint[SCPS_MAX_REG];
                for (int r=0;r<sim.econ->n_regions && r<SCPS_MAX_REG;r++){
                    const PopCulture *cu=&sim.econ->region[r].culture;
                    g_region_tint[r] = (mode==VIEW_CULTURE) ? ethos_tint((int)cu->ethos)
                                                            : faith_tint((int)cu->rel_branch);
                }
                rp.region_tint = g_region_tint;
            }
            render_map(world, pb.pixels, pb.w, pb.h, &rp, rmode);
            pixbuf_upload(&pb);
            /* §5 : la minicarte — le monde entier en petit (même mode + teinte). */
            if (mm_pb.pixels){
                RenderParams mmp = rp; mmp.selected_prov = -1;
                minimap_fit(&mmp.cam_scale, &mmp.cam_ox, &mmp.cam_oy);
                render_map(world, mm_pb.pixels, mm_pb.w, mm_pb.h, &mmp, rmode);
                pixbuf_upload(&mm_pb);
            }
            dirty = false;
        }

        SDL_RenderClear(ren);
        if (pb.tex) SDL_RenderCopy(ren, pb.tex, NULL, NULL);
        /* ── filtre COURANTS : lignes de flux sur la mer (les MORTES = le creux) ── */
        if (g_sb.show_currents && g_gs==GS_PLAYING){
            for (int sy=8; sy<win_h-8; sy+=14) for (int sx=8; sx<win_w-8; sx+=14){
                int cx2=(int)(sx/cam.scale+cam.ox), cy2=(int)(sy/cam.scale+cam.oy);
                if (cx2<0||cy2<0||cx2>=SCPS_W||cy2>=SCPS_H) continue;
                const Cell *cc=scps_cellc(world,cx2,cy2);
                if (cc->sea<=SEA_CABOTAGE) continue;          /* terre/côte : rien ; MORTE : creux */
                float vx2=cc->cur_vx/100.f, vy2=cc->cur_vy/100.f;
                float m=sqrtf(vx2*vx2+vy2*vy2);
                if (m<0.10f) continue;
                float l=(cc->sea==SEA_COURANT)?9.f:5.f;
                SDL_Color cl=(cc->sea==SEA_COURANT)?COL_COPPER:(SDL_Color){0x6a,0x8a,0xb0,0xff};
                SDL_SetRenderDrawColor(ren,cl.r,cl.g,cl.b,(cc->sea==SEA_COURANT)?0xE0:0x90);
                SDL_RenderDrawLine(ren,sx,sy,sx+(int)(vx2/m*l),sy+(int)(vy2/m*l));
                SDL_RenderDrawPoint(ren,sx+(int)(vx2/m*l),sy+(int)(vy2/m*l));
            }
        }
        /* mer §9 : au survol d'une tuile de mer, LE MOT — cabotage · eaux mortes ·
         * eaux vives · courant (et son sens). Des mots, jamais un vecteur nu. */
        if (g_gs==GS_PLAYING && g_font_small && sim.ready){
            int hmx,hmy; SDL_GetMouseState(&hmx,&hmy);
            int hcx=(int)(hmx/cam.scale+cam.ox), hcy=(int)(hmy/cam.scale+cam.oy);
            if (hcx>=0&&hcy>=0&&hcx<SCPS_W&&hcy<SCPS_H){
                const Cell *hc=scps_cellc(world,hcx,hcy);
                if (hc->sea){
                    char sw[64]; const char *dir="";
                    if (hc->sea==SEA_COURANT||hc->sea==SEA_VIVE){
                        int ax=hc->cur_vx, ay=hc->cur_vy;
                        dir = (abs(ax)>=abs(ay)) ? (ax>=0?" vers l'est":" vers l'ouest")
                                                 : (ay>=0?" vers le sud":" vers le nord");
                    }
                    snprintf(sw,sizeof sw,"%s%s",
                             hc->sea==SEA_CABOTAGE?"cabotage — lent mais sûr":
                             hc->sea==SEA_MORTE   ?"eaux mortes — rien ne pousse un navire":
                             hc->sea==SEA_VIVE    ?"eaux vives":
                                                   "courant favorable",
                             (hc->sea==SEA_COURANT||hc->sea==SEA_VIVE)?dir:"");
                    draw_text(ren,g_font_small,hmx+14,hmy+10,COL_PARCH,sw);
                }
            }
        }
        /* Overlay diégétique : bandeau royaume + panneau de province, via la
         * membrane (bandes + mots). Le viewer ne touche aucun flottant SCPS. */
        if (sim.ready && g_font) {
            int mx2,my2; SDL_GetMouseState(&mx2,&my2);
            zone_reset(); bslot_reset(); orow_reset(); modebtn_reset(); topbtn_reset();
            int cid = country_for_panel(world, selected);
            if (g_gs!=GS_PLAYING) {                              /* le SHELL tient l'écran */
                shell_draw(ren,win_w,win_h,world,&sim,&g_stage);
                draw_hover_footer(ren, win_w, win_h, mx2, my2);
                SDL_RenderPresent(ren);
                int cx0,cy0; SDL_GetMouseState(&cx0,&cy0); (void)cx0;(void)cy0;
                SDL_Delay(8);
                continue;                                        /* pas de HUD hors partie */
            }
            if (show_tree) {                                    /* superposition de l'arbre (Tab) */
                draw_tech_tree(ren, win_w, win_h, sim.econ, sim.ts, world, cid);
            } else {
                draw_army_markers(ren, &cam, &sim, world, win_w, win_h);   /* §4 : les armées sur la carte */
                draw_topbar(ren, win_w, &sim, world, cid, speed);
                draw_outliner(ren, win_w, win_h, &sim, world);            /* §6 : « ce que je possède » */
                draw_mode_buttons(ren, win_h, mode);                      /* §5 : modes de carte */
                draw_minimap(ren, &mm_pb, win_w, win_h, &cam);            /* §5 : la minicarte */
                if (selected >= 0)
                    draw_province_panel(ren, win_w, win_h, world, sim.econ, sim.wp, sim.wl, sim.drift, selected);
                draw_sidebar(ren, win_w, win_h, &sim, world, mode, selected);   /* §sidebar : rail + tiroir d'empire */
            }
            shell_draw(ren,win_w,win_h,world,&sim,&g_stage);     /* surcouches : pause · tuto · confirmation */
            /* mer au survol : le MOT (repli de plus basse priorité — ajouté en dernier) */
            { int cx3=(int)(mx2/cam.scale+cam.ox), cy3=(int)(my2/cam.scale+cam.oy);
              if (cx3>=0&&cy3>=0&&cx3<SCPS_W&&cy3<SCPS_H){
                  const Cell *cc=scps_cellc(world,cx3,cy3);
                  if (cc->sea!=SEA_NONE){
                      static char seahov[96];
                      if (cc->sea==SEA_CABOTAGE) snprintf(seahov,sizeof seahov,"Cabotage — lent mais sûr, de port en port.");
                      else if (cc->sea==SEA_MORTE) snprintf(seahov,sizeof seahov,"Eaux mortes — rien ne pousse un navire ici : on contourne.");
                      else {
                          const char *d = (abs(cc->cur_vx)>=abs(cc->cur_vy))
                                        ? (cc->cur_vx>0?"l'est":"l'ouest") : (cc->cur_vy>0?"le sud":"le nord");
                          snprintf(seahov,sizeof seahov,"%s — courant favorable vers %s.",
                                   cc->sea==SEA_COURANT?"Couloir":"Eaux vives", d);
                      }
                      zone_add((SDL_Rect){0,0,win_w,win_h}, seahov);
                  } } }
            draw_hover_footer(ren, win_w, win_h, mx2, my2);     /* survol : nom + EFFET du nœud */
        }
#ifdef SCPS_DEV
        if (g_dev_overlay)   /* §6 : l'inspecteur brut PAR-DESSUS le jeu (dev seul) */
            dev_overlay_draw(world, sim.econ, sim.ts, sim.dp, country_for_panel(world, selected), selected);
#endif
        SDL_RenderPresent(ren);

        /* Status console */
        int mx, my; SDL_GetMouseState(&mx, &my);
        int cx=(int)(mx/cam.scale+cam.ox), cy=(int)(my/cam.scale+cam.oy);
        status_line(world, mode, seed, cx, cy, selected);

        SDL_Delay(8); /* ~120fps max */
    }

    printf("\n");
    pixbuf_destroy(&pb);
    pixbuf_destroy(&mm_pb);
    free(world);
    free(sim.econ); free(sim.wp); free(sim.wl); free(sim.net); free(sim.ts); free(sim.sc);
    free(sim.ag); free(sim.ev); free(sim.drift); free(sim.labor);
    warhost_free(sim.host);
    free(sim.dp); free(sim.rn); free(sim.rs); free(sim.host); free(sim.camp); free(sim.ai); free(sim.ai_on);
    if (g_font)     TTF_CloseFont(g_font);
    if (g_font_big) TTF_CloseFont(g_font_big);
    if (g_font_small) TTF_CloseFont(g_font_small);
    TTF_Quit();
#ifdef SCPS_DEV
    dev_overlay_shutdown();
#endif
    audio_shutdown();
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
