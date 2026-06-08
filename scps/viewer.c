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
#include "scps_readout.h"   /* la membrane : viewer ne voit QUE des bandes + mots.
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
#include "scps_factions.h"  /* §4 : leviers de factions (reset/decay par sim) */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <math.h>

/* ---- Configuration fenêtre ------------------------------------------- */
#define WIN_W 1280
#define WIN_H  640

/* ---- Temps de jeu : du snapshot au JEU VIVANT ------------------------ */
typedef enum { SPEED_PAUSE=0, SPEED_1, SPEED_2, SPEED_5, SPEED_COUNT } GameSpeed;
static const double DAYS_PER_SEC[SPEED_COUNT] = { 0.0, 3.0, 8.0, 24.0 };
static const char  *SPEED_LABEL[SPEED_COUNT]  = { "❙❙ Pause", "▸ ×1", "▸▸ ×2", "▸▸▸ ×5" };
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

/* ---- Palette (bleu nuit & cuivre, §5.4) ------------------------------- */
static const SDL_Color COL_PANEL  = { 0x0f,0x16,0x22,0xf2 };
static const SDL_Color COL_PANEL2 = { 0x16,0x20,0x30,0xf2 };
static const SDL_Color COL_COPPER = { 0xb8,0x73,0x33,0xff };
static const SDL_Color COL_PARCH  = { 0xe7,0xdc,0xc4,0xff };
static const SDL_Color COL_DIM    = { 0x9a,0x8f,0x78,0xff };

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
    draw_box(ren, x-1, y-1, gw+2, gh+2, COL_DIM);
    int mx = x + (int)(value/100.f*(gw-1));
    fill_rect(ren, mx-1, y-2, 3, gh+4, COL_PARCH);   /* le curseur à la valeur */
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
static void draw_tech_tree(SDL_Renderer *ren, int win_w, int win_h,
                           WorldEconomy *econ, TechState *ts, World *w, int cid){
    fill_rect(ren, 0,0, win_w, win_h, (SDL_Color){0x0a,0x0e,0x16,0xff});
    if (cid<0 || cid>=w->n_countries) return;
    TechTreeReadout tr;
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
        if (g_font_small){                                               /* NOM compact sous la bulle ; le détail au survol */
            SDL_Color lc = (nd->state==TREE_DONE)?COL_PARCH
                         : (nd->state==TREE_OPEN)?COL_DIM : (SDL_Color){0x5b,0x56,0x4b,0xff};
            int lw=text_w(g_font_small,nd->name);
            draw_text(ren,g_font_small, x-lw/2, y+sz+1, lc, nd->name);
        }
        /* SURVOL : le bâtiment + son UTILITÉ concrète, le coût et l'état */
        snprintf(g_tree_hov[i],sizeof g_tree_hov[i], "%s%s%s — %s · coût %d pts (%s%s)",
                 nd->name,
                 strcmp(nd->name,nd->unlocks)? " · bâtit " : "",
                 strcmp(nd->name,nd->unlocks)? nd->unlocks : "",
                 nd->effet, nd->cost, label_tree_state(nd->state),
                 nd->orphan? ", orpheline : greffe par la population" : "");
        zone_add((SDL_Rect){x-sz-3,y-sz-3,sz*2+6,sz*2+6}, g_tree_hov[i]);
        if (nd->faustian && (g_tree_demo<0 || nd->state==TREE_DONE)) g_tree_demo=i;  /* un faustien pour la démo */
    }
    for (int th=0;th<3;th++){                                            /* étiquettes de thème EXCENTRÉES (hors des nœuds) */
        float a=(th*120.f+60.f)*D2R + TOP, rl=GAP+4.75f*ring;
        int lx=cx+(int)(cosf(a)*rl), ly=cy+(int)(sinf(a)*rl);
        draw_text(ren,g_font_big,lx-text_w(g_font_big,tr.theme[th])/2,ly-9,tcol[th],tr.theme[th]);
    }
    char hdr[220];
    snprintf(hdr,sizeof hdr,"ARBRE DE TECH — %s   ·   %d points de recherche   ·   SURVOLE un nœud pour son effet & son coût",
             w->country[cid].name, tr.points);
    draw_text(ren,g_font_big,18,12,COL_COPPER,hdr);
    draw_text(ren,g_font,18,win_h-40,COL_DIM,
      "centre = point (les 6 bases, au survol) · anneau = tier · 3 secteurs = thèmes · cadre rouge = faustien · cadre gris = orphelin");
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
        if (frontier>=0)
            campaign_order(s->camp, s->econ, c, frontier, target, &s->host->army[c]);
    }
    campaign_tick(s->camp, w, s->econ, s->dp, &s->camp_rng, 365.f);
}

static void sim_day(Sim *s, World *w) {
    /* — quotidien — */
    agency_advance(s->ag, w, s->econ, s->wl, 1);            /* les actions progressent */
    routes_advance(s->rn, w, s->econ, 1);
    for (int c=0;c<w->n_countries;c++) if (s->ai_on[c]){    /* les voisins VIVENT (cadence étalée) */
        ai_step(&s->ai[c], w, s->econ, s->wp, s->wl, s->ag, s->rn, s->dp, s->day);
        ai_research_step(&s->ai[c], &s->ts[c], w, s->econ, s->wp, s->day);  /* l'arbre vivant */
    }
    world_events_tick(s->ev, w, s->econ, s->wl, s->wp, s->sc, s->rn, s->ts, 1);
    labor_tick(s->labor);
    /* — mensuel : ÉCONOMIE + réputation diplomatique (O(n²)) + démographie, tous
     * au pas dt=1/12 → même rythme annuel, mais plus fluide qu'un saut yearly — */
    if (s->day % 30 == 29) {
        econ_tick(s->econ, 1.f/12.f);
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
        }
    }
    if (++s->day % 365 == 0) s->year++;
}

/* (Ré)initialise la partie VIVANTE : monde déjà généré, on installe TOUS les
 * sous-systèmes, on attache les GROUPES démographiques, on amorce ~3 ans, puis
 * la partie avance par sim_day (plus de snapshot figé). */
static void sim_rebuild(Sim *s, World *w) {
    if (!s->econ || !s->wp || !s->wl || !s->net || !s->ts || !s->sc
        || !s->ag || !s->ev || !s->drift || !s->labor || !s->rs || !s->host || !s->camp) return;
    econ_init(s->econ, w);
    gen_population(w, s->econ);
    worldgen_seed_peoples(w, s->econ, RACE_HUMAIN);
    legitimacy_init(s->wl, w, s->econ);
    prosperity_init(s->wp, w);
    trade_network_build(s->net, w, s->econ);
    statecraft_init(s->sc, w);
    agency_init(s->ag);
    diplo_init(s->dp);
    routes_init(s->rn);
    for (int c=0;c<w->n_countries;c++) tech_state_init(&s->ts[c], false);
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
    events_init(s->ev, w, w->seed);
    labor_init(s->labor, w);
    labor_seed_from_world(s->labor, w, s->econ, s->player);
    revolt_init(s->rs);                                  /* les soulèvements incarnés */
    warhost_init(s->host);                               /* les armées levées par pays */
    campaign_init(s->camp, w, s->econ);                  /* … qui marcheront sur la carte (terrain + RAZ) */
    s->camp_rng = w->seed ^ 0xCA117A11u;                 /* graine de campagne propre à la partie */
    missions_init(s->missions);                          /* missions décennales */
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
    int tw = text_w(fs, text), w = tw + 16;
    fill_rect(ren, x, y, w, 18, COL_PANEL);
    fill_rect(ren, x, y, 3, 18, accent);             /* l'accent de nature */
    draw_box (ren, x, y, w, 18, COL_DIM);
    draw_text(ren, fs, x+8, y+1, COL_PARCH, text);
    if (hov) zone_add((SDL_Rect){x,y,w,18}, hov);
    return x + w + 6;
}

static void draw_topbar(SDL_Renderer *ren, int win_w, const Sim *s, const World *w, int cid,
                        GameSpeed sp) {
    CountryReadout r = country_readout(s->wp, s->ts, w, cid);
    r.influence = statecraft_influence(s->sc, cid);
    int bh = 74;
    fill_rect(ren, 0,0, win_w, bh, COL_PANEL);
    fill_rect(ren, 0,bh, win_w, 2, COL_COPPER);

    /* — Rang A : DÉPENSABLE | ACCUMULABLE (clusters séparés) · temps/âge/vitesse (droite).
     *   Le bandeau est un SOMMAIRE : chaque ressource ouvre son système (clic). — */
    int x=12, yA=6;
    /* Dépensable : Or · Nourriture · Matériaux (stock + flux +N/j). */
    x = draw_res(ren,x,yA, lres_name(LR_GOLD),      s->labor->stock[LR_GOLD],      (float)s->labor->flow[LR_GOLD],
                 "Or en caisse (clic → Finances : revenus, commerce, taxation réglable). Taxes + surplus vendu au marché.");
    x = draw_res(ren,x,yA, lres_name(LR_FOOD),      s->labor->stock[LR_FOOD],      (float)s->labor->flow[LR_FOOD],
                 "Vivres (clic → Subsistance & démographie). La famine stoppe la croissance de la population.");
    x = draw_res(ren,x,yA, lres_name(LR_MATERIALS), s->labor->stock[LR_MATERIALS], (float)s->labor->flow[LR_MATERIALS],
                 "Matériaux (clic → Chaînes de production & stock du marché). Bâtir, coloniser et armer en consomment.");
    x = topbar_sep(ren, x, yA);
    /* Accumulable : Savoir (points de recherche) · Influence — stock + flux. */
    float ppop = ai_country_population(w, s->econ, cid);
    x = draw_res(ren,x,yA, "Savoir",    (long)r.m_savoir.value, ai_research_income(&s->ts[cid], ppop),
                 "Savoir — le niveau de lumière du royaume 0-100 (clic → Arbre de tech) ; le flux est la recherche que la population produit par jour.");
    x = draw_res(ren,x,yA, "Influence", (long)statecraft_influence(s->sc,cid), statecraft_influence_flux(s->sc,s->econ,s->wp,cid),
                 "Influence diplomatique (clic → Diplomatie). Prospérité + taille + accords tenus la nourrissent ; elle plafonne les diplomates.");
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
    zone_add((SDL_Rect){speedx-3,yA-2,wspeed+6,19}, "Vitesse du temps. Espace = pause ; + / − = accélérer / ralentir.");

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
        static const char *AB[6]   = {"Conqu","March","Légis","Gardi","Trans","Commu"};
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
            fill_rect(ren, xc, yC+2, 9, 9, FCOL[f]);              /* pastille d'identité */
            draw_box (ren, xc, yC+2, 9, 9, COL_DIM);
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

static void draw_province_panel(SDL_Renderer *ren, int win_w, int win_h,
                                const World *w, const WorldEconomy *econ,
                                const WorldProsperity *wp, const WorldLegitimacy *wl,
                                const ModifierStack *drift, int pid) {
    ProvinceReadout p = province_readout(w, econ, wp, wl, pid);
    int pw=312, px=win_w-pw, py=56, ph=win_h-py-26;
    fill_rect(ren, px,py, pw,ph, COL_PANEL);
    fill_rect(ren, px,py, 2,ph, COL_COPPER);
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
    snprintf(line,sizeof line, "%s · %s · %s", p.climat, p.relief, label_stature(p.stature));
    draw_text(ren, g_font, x+hsz+8, y+18, COL_PARCH, line);
    zone_add((SDL_Rect){x+hsz+6, y+16, rw-hsz-6, 18}, "Climat · relief · taille de la province.");
    y += hsz + 8;
    /* HABITANTS — un nombre, rien de plus (le détail va aux camemberts). */
    snprintf(line,sizeof line, "%ld habitants", p.ames);
    draw_text(ren, g_font, x, y, COL_PARCH, line);
    zone_add((SDL_Rect){x-2,y-2,rw,19}, "Le nombre total d'habitants de la province."); y += 22;

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
            long cp[3] = { (long)re2->strata[CLASS_LABORER].pop,
                           (long)re2->strata[CLASS_BOURGEOIS].pop,
                           (long)re2->strata[CLASS_ELITE].pop };
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
            fill_rect(ren, sx, sy, sw, sw, built ? fc : COL_PANEL2);
            draw_box(ren, sx, sy, sw, sw, COL_DIM);
            if (S[i].special) draw_box(ren, sx-1, sy-1, sw+2, sw+2, COL_COPPER);  /* liseré distinct */
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

/* Survol = définition : le hover de la zone sous le curseur, en pied d'écran. */
static void draw_hover_footer(SDL_Renderer *ren, int win_w, int win_h, int mx, int my){
    const char *def = zone_hit(mx,my);
    if (!def) return;
    int fh=22;
    fill_rect(ren, 0, win_h-fh, win_w, fh, COL_PANEL2);
    fill_rect(ren, 0, win_h-fh-1, win_w, 1, COL_COPPER);
    draw_text(ren, g_font, 12, win_h-fh+3, COL_PARCH, def);
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

int main(int argc, char **argv) {
    bool shot = false, shot_tree = false, shot_war = false;
    uint32_t shot_seed = 0; bool have_shot_seed = false;
    for (int i=1;i<argc;i++) {
        if (!strcmp(argv[i], "--shot")) shot = true;
        else if (!strcmp(argv[i], "--tree")) { shot = true; shot_tree = true; }
        else if (!strcmp(argv[i], "--war"))  { shot = true; shot_war  = true; }  /* §4 : capturer les armées sur la carte */
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
    sim.ai   = (AiActor*)         calloc(SCPS_MAX_COUNTRY, sizeof(AiActor));
    sim.ai_on= (bool*)            calloc(SCPS_MAX_COUNTRY, sizeof(bool));

    int win_w = WIN_W, win_h = WIN_H;
    PixBuf pb = pixbuf_create(ren, win_w, win_h);

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
    printf("[scps] Prêt. TAB/1-0=vues  A=arbre de tech  R=regénère  clic=territoire\n");
    printf("[scps] Réglages (régénèrent) : c=continents g=âge e=érosion\n");
    printf("       l=terres m=montagnes t=température h=humidité (Maj=baisse)\n");

    /* Mode capture (--shot) : une frame (carte + bandeau + panneau sur une
     * province peuplée), sérialisée en PPM, puis sortie — vérifie l'UI sans écran. */
    if (shot) {
        if (shot_tree) for (int d=0; d<60*365; d++) sim_day(&sim, world);  /* laisse l'arbre POUSSER */
        int cid = country_for_panel(world, -1);
        int pcap = (cid>=0 && cid<world->n_countries) ? world->country[cid].capital_prov : -1;
        selected = (pcap>=0) ? pcap : 0;
        rp.cam_ox=cam.ox; rp.cam_oy=cam.oy; rp.cam_scale=cam.scale; rp.selected_prov=selected;
        SDL_RenderClear(ren);
        if (shot_tree && sim.ready && g_font) {
            draw_tech_tree(ren, win_w, win_h, sim.econ, sim.ts, world, cid);   /* l'arbre concentrique du pays */
            if (g_tree_demo>=0){                                              /* démo : un survol (nom + effet) */
                draw_box(ren, g_tree_x[g_tree_demo]-9, g_tree_y[g_tree_demo]-9, 18,18, COL_PARCH);
                draw_hover_footer(ren, win_w, win_h, g_tree_x[g_tree_demo], g_tree_y[g_tree_demo]);
            }
        } else {
            if (shot_war)                    /* §4 : laisse une guerre mûrir → des armées sur la carte */
                for (int y=0; y<120 && !any_field_army(&sim, world); y++)
                    for (int d=0; d<365; d++) sim_day(&sim, world);
            render_map(world, pb.pixels, pb.w, pb.h, &rp, VIEW_COUNTRIES);
            pixbuf_upload(&pb);
            if (pb.tex) SDL_RenderCopy(ren, pb.tex, NULL, NULL);
            if (sim.ready && g_font) {
                zone_reset();
                draw_army_markers(ren, &cam, &sim, world, win_w, win_h);   /* §4 : les armées sur la carte */
                draw_topbar(ren, win_w, &sim, world, cid, speed);
                draw_province_panel(ren, win_w, win_h, world, sim.econ, sim.wp, sim.wl, sim.drift, selected);
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
        while (SDL_PollEvent(&ev)) {
            switch (ev.type) {

            case SDL_QUIT: running = false; break;

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
                switch (ev.key.keysym.sym) {
                case SDLK_ESCAPE:
                case SDLK_q:     running = false; break;
                /* --- Contrôle du TEMPS (§1) : Espace = pause ; +/- = vitesse --- */
                case SDLK_SPACE:
                    speed = (speed==SPEED_PAUSE) ? SPEED_1 : SPEED_PAUSE; break;
                case SDLK_a:     show_tree = !show_tree; break;   /* A = l'Arbre de tech concentrique */
                case SDLK_PLUS: case SDLK_EQUALS: case SDLK_KP_PLUS:
                    if (speed<SPEED_5) speed++;
                    if (speed==SPEED_PAUSE) speed=SPEED_1;
                    break;
                case SDLK_MINUS: case SDLK_KP_MINUS:
                    if (speed>SPEED_1) speed--;
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
                case SDLK_f:     cam_fit(&cam,win_w,win_h); dirty=true; break;
                case SDLK_r:
                    seed ^= (uint32_t)time(NULL) * 2654435761u;
                    params.seed = seed;
                    regen = true;
                    break;

                /* --- Réglages de génération (Maj = diminuer) --- */
                case SDLK_c: {  /* nombre de continents 1..6 */
                    bool dn = (ev.key.keysym.mod & KMOD_SHIFT);
                    params.n_continents += dn?-1:1;
                    if (params.n_continents<1) params.n_continents=6;
                    if (params.n_continents>6) params.n_continents=1;
                    regen=true; break;
                }
                #define ADJ(field) { bool dn=(ev.key.keysym.mod&KMOD_SHIFT); \
                    params.field += dn?-0.25f:0.25f; \
                    if(params.field<-0.001f)params.field=1.f; \
                    else if(params.field>1.001f)params.field=0.f; \
                    regen=true; }
                case SDLK_g:  ADJ(world_age)   break;  /* âge du monde   */
                case SDLK_e:  ADJ(erosion)     break;  /* érosion        */
                case SDLK_l:  ADJ(land_amount) break;  /* quantité terre */
                case SDLK_m:  ADJ(mountains)   break;  /* relief         */
                case SDLK_t:  ADJ(temperature) break;  /* température    */
                case SDLK_h:  ADJ(humidity)    break;  /* humidité       */
                #undef ADJ
                default: break;
                }
                break;
            }
        }

        if (regen) {
            printf("\n[scps] Génération — graine %u · continents %d · âge %.2f"
                   " · érosion %.2f · terres %.2f · relief %.2f · temp %.2f · humid %.2f\n",
                   params.seed, params.n_continents, params.world_age, params.erosion,
                   params.land_amount, params.mountains, params.temperature, params.humidity);
            world_generate(world, &params);
            sim_rebuild(&sim, world);
            selected = -1; dirty = true; regen = false;
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
            render_map(world, pb.pixels, pb.w, pb.h, &rp, mode);
            pixbuf_upload(&pb);
            dirty = false;
        }

        SDL_RenderClear(ren);
        if (pb.tex) SDL_RenderCopy(ren, pb.tex, NULL, NULL);
        /* Overlay diégétique : bandeau royaume + panneau de province, via la
         * membrane (bandes + mots). Le viewer ne touche aucun flottant SCPS. */
        if (sim.ready && g_font) {
            int mx2,my2; SDL_GetMouseState(&mx2,&my2);
            zone_reset(); bslot_reset();
            int cid = country_for_panel(world, selected);
            if (show_tree) {                                    /* superposition de l'arbre (Tab) */
                draw_tech_tree(ren, win_w, win_h, sim.econ, sim.ts, world, cid);
            } else {
                draw_army_markers(ren, &cam, &sim, world, win_w, win_h);   /* §4 : les armées sur la carte */
                draw_topbar(ren, win_w, &sim, world, cid, speed);
                if (selected >= 0)
                    draw_province_panel(ren, win_w, win_h, world, sim.econ, sim.wp, sim.wl, sim.drift, selected);
            }
            draw_hover_footer(ren, win_w, win_h, mx2, my2);     /* survol : nom + EFFET du nœud */
        }
        SDL_RenderPresent(ren);

        /* Status console */
        int mx, my; SDL_GetMouseState(&mx, &my);
        int cx=(int)(mx/cam.scale+cam.ox), cy=(int)(my/cam.scale+cam.oy);
        status_line(world, mode, seed, cx, cy, selected);

        SDL_Delay(8); /* ~120fps max */
    }

    printf("\n");
    pixbuf_destroy(&pb);
    free(world);
    free(sim.econ); free(sim.wp); free(sim.wl); free(sim.net); free(sim.ts); free(sim.sc);
    free(sim.ag); free(sim.ev); free(sim.drift); free(sim.labor);
    warhost_free(sim.host);
    free(sim.dp); free(sim.rn); free(sim.rs); free(sim.host); free(sim.camp); free(sim.ai); free(sim.ai_on);
    if (g_font)     TTF_CloseFont(g_font);
    if (g_font_big) TTF_CloseFont(g_font_big);
    if (g_font_small) TTF_CloseFont(g_font_small);
    TTF_Quit();
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
