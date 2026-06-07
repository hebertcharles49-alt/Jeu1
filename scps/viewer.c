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
static TTF_Font *g_font = NULL, *g_font_big = NULL;
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
    AiActor         *ai;       /* un acteur IA par pays voisin (cadence étalée) */
    bool            *ai_on;    /* ce pays est-il piloté par l'IA ? */
    int              day;      /* jour de jeu (1 tick = 1 jour) */
    int              year;
    int              player;   /* pays du joueur */
    bool             ready;
} Sim;

/* UN JOUR de jeu vivant (§1). Chaque sous-système avance à SA cadence calibrée :
 * agency/évènements/statecraft/labor en JOURS ; l'économie/légitimité/prospérité/
 * démographie à l'ANNÉE (comme les bancs d'essai — on ne dérègle pas le pacing).
 * Le joueur n'est qu'un acteur : ses actions sont déjà en file dans s->ag. */
static void sim_day(Sim *s, World *w) {
    /* — quotidien — */
    agency_advance(s->ag, w, s->econ, s->wl, 1);            /* les actions progressent */
    routes_advance(s->rn, w, s->econ, 1);
    for (int c=0;c<w->n_countries;c++) if (s->ai_on[c])     /* les voisins VIVENT (cadence étalée) */
        ai_step(&s->ai[c], w, s->econ, s->wp, s->wl, s->ag, s->rn, s->dp, s->day);
    world_events_tick(s->ev, w, s->econ, s->wl, s->wp, s->sc, s->rn, s->ts, 1);
    labor_tick(s->labor);
    /* — mensuel : ÉCONOMIE + réputation diplomatique (O(n²)) + démographie, tous
     * au pas dt=1/12 → même rythme annuel, mais plus fluide qu'un saut yearly — */
    if (s->day % 30 == 29) {
        econ_tick(s->econ, 1.f/12.f);
        statecraft_tick(s->sc, w, s->econ, s->wp, s->wl, s->dp, s->rn, 30);
        demography_tick(w, s->econ, s->wl, s->drift, 5.f, 5.f, 1.f/12.f);
    }
    /* — annuel (le tour stratégique) — */
    if (s->day % 365 == 364) {
        econ_colonize_tick(s->econ, w);
        econ_migrate_tick(s->econ, w);
        world_tick(w, s->econ, 1.0f);
        legitimacy_tick(s->wl, w, s->econ, s->ts);
        trade_network_build(s->net, w, s->econ);
        trade_tick(s->econ, s->net);
        prosperity_tick(s->wp, w, s->econ, s->net, s->ts, s->wl);
    }
    if (++s->day % 365 == 0) s->year++;
}

/* (Ré)initialise la partie VIVANTE : monde déjà généré, on installe TOUS les
 * sous-systèmes, on attache les GROUPES démographiques, on amorce ~3 ans, puis
 * la partie avance par sim_day (plus de snapshot figé). */
static void sim_rebuild(Sim *s, World *w) {
    if (!s->econ || !s->wp || !s->wl || !s->net || !s->ts || !s->sc
        || !s->ag || !s->ev || !s->drift || !s->labor) return;
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
static int draw_res(SDL_Renderer *ren, int x, int y, const char *name, long stock, long flow,
                    const char *def){
    char buf[48]; snprintf(buf,sizeof buf, "%s %ld", name, stock);
    draw_text(ren, g_font, x, y, COL_PARCH, buf);
    int w1 = text_w(g_font, buf);
    char fb[24]; snprintf(fb,sizeof fb, " %+ld", flow);
    SDL_Color fc = (flow<0) ? sense_color(0.12f) : sense_color(0.82f);
    draw_text(ren, g_font, x+w1, y, fc, fb);
    int total = w1 + text_w(g_font, fb);
    zone_add((SDL_Rect){x-3,y-2,total+6,19}, def);
    return x + total + 20;
}

/* LA TOPBAR (§2) : ressources · métriques 0-100 · temps/âge/vitesse. Deux rangs,
 * cuivre sur bleu nuit. Aucun flottant SCPS — tout par la membrane (mots + 0-100). */
static void draw_topbar(SDL_Renderer *ren, int win_w, const Sim *s, const World *w, int cid,
                        GameSpeed sp) {
    CountryReadout r = country_readout(s->wp, s->ts, w, cid);
    r.influence = statecraft_influence(s->sc, cid);
    int bh = 52;
    fill_rect(ren, 0,0, win_w, bh, COL_PANEL);
    fill_rect(ren, 0,bh, win_w, 2, COL_COPPER);

    /* — Rang A : ressources (gauche) · temps/âge/vitesse (droite) — */
    int x=12, yA=6;
    x = draw_res(ren,x,yA, lres_name(LR_GOLD),      s->labor->stock[LR_GOLD],      s->labor->flow[LR_GOLD],
                 "L'or en caisse : taxes et surplus commercial vendu au marché.");
    x = draw_res(ren,x,yA, lres_name(LR_FOOD),      s->labor->stock[LR_FOOD],      s->labor->flow[LR_FOOD],
                 "Les vivres ; la famine stoppe la croissance de la population.");
    x = draw_res(ren,x,yA, lres_name(LR_MATERIALS), s->labor->stock[LR_MATERIALS], s->labor->flow[LR_MATERIALS],
                 "Les matériaux de construction ; bâtir, coloniser et armer en consomment.");
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

    /* — Rang B : pays + métriques 0-100 — */
    int yB=28;
    const char *name = (cid>=0 && cid<w->n_countries) ? w->country[cid].name : "—";
    int xb=12;
    draw_text(ren, g_font, xb, yB, COL_COPPER, name); xb += text_w(g_font,name) + 18;
    xb = draw_reading(ren,xb,yB,"Stabilité", r.m_stabilite.value, label_stab(r.stabilite),   band_good(r.stabilite,5,true),  hover_stab());
    xb = draw_reading(ren,xb,yB,"Assise",    -1,                  label_assise(r.assise),    band_good(r.assise,4,false),    hover_assise());
    xb = draw_reading(ren,xb,yB,"Légitimité",r.m_legitimite.value,label_legit(r.legitimite), band_good(r.legitimite,5,true), hover_legit());
    xb = draw_reading(ren,xb,yB,"Cohésion",  r.m_cohesion.value,  label_concorde(r.concorde),band_good(r.concorde,4,false),  hover_concorde());
    xb = draw_reading(ren,xb,yB,"Prospérité",r.m_prosperite.value,label_prosp(r.prosperite), band_good(r.prosperite,5,true), hover_prosp());
    xb = draw_reading(ren,xb,yB,"Savoir",    r.m_savoir.value,    label_savoir(r.savoir),    band_good(r.savoir,4,true),     hover_savoir());
    xb = draw_reading(ren,xb,yB,"Influence", r.influence,         "",                        COL_PARCH,
                      "La réputation diplomatique : prospérité, taille et accords tenus la nourrissent ; elle plafonne les diplomates en mission.");
    if (r.presage != PG_CALME)
        draw_reading(ren,xb,yB,"Présage",   -1,                  label_presage(r.presage),  band_good(r.presage,4,false),   hover_presage());

    if (r.augure) {  /* ligne d'alerte / augure sous la topbar (réemploi machinerie IA) */
        fill_rect(ren, 0,bh+2, win_w, 20, COL_PANEL2);
        draw_text(ren, g_font, 12, bh+3, sense_color(0.12f), r.augure);
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

    draw_text(ren, g_font_big, x, y, COL_COPPER, p.nom); y += 28;
    snprintf(line,sizeof line, "%s · %s", p.terrain, label_stature(p.stature));
    draw_text(ren, g_font, x, y, COL_PARCH, line);
    zone_add((SDL_Rect){x-2,y-2,rw,19}, hover_stature()); y += 22;

    /* COMPOSITION — la ventilation des GROUPES de la province (le payoff du
     * refactor démographique). race/classe diégétiques ; loyauté en MOT ; état. */
    {
        int reg = (pid>=0 && pid<w->n_provinces) ? w->province[pid].region : -1;
        if (reg>=0 && reg<econ->n_regions && econ->region[reg].pop.n_groups>0) {
            ui_section(ren, x, &y, "COMPOSITION");
            int owner = econ->region[reg].owner;
            const PopCulture *crown = &econ->region[reg].culture;     /* repli */
            if (owner>=0 && owner<w->n_countries) {
                int cp=w->country[owner].capital_prov;
                if (cp>=0 && cp<w->n_provinces) { int cr=w->province[cp].region;
                    if (cr>=0 && cr<econ->n_regions) crown=&econ->region[cr].culture; }
            }
            static char comp_hov[SCPS_MAX_GROUPS][160];
            GroupReadout gr[SCPS_MAX_GROUPS];
            int ng = province_composition(&econ->region[reg].pop, drift, crown, 5.f, 5.f,
                                          gr, SCPS_MAX_GROUPS);
            for (int i=0;i<ng;i++) {
                int barw = 6 + (int)(gr[i].percent*0.38f);     /* barre de proportion (≤44) */
                SDL_Color lc = band_good(gr[i].loyaute,5,true);
                fill_rect(ren, x, y+3, barw, 10, lc);
                char buf[80];
                snprintf(buf,sizeof buf, "%d%% %s — %s", gr[i].percent, gr[i].race, label_humeur(gr[i].loyaute));
                draw_text(ren, g_font, x+50, y, COL_PARCH, buf);
                int bw=text_w(g_font,buf);
                char et[44]; snprintf(et,sizeof et, " · %s", gr[i].etat);
                draw_text(ren, g_font, x+50+bw, y, COL_DIM, et);
                snprintf(comp_hov[i],sizeof comp_hov[i],
                         "%s · %s — %s (%s). Race/classe diégétiques ; loyauté en mot ; jamais un nom SCPS.",
                         gr[i].race, gr[i].klass, label_humeur(gr[i].loyaute), gr[i].etat);
                zone_add((SDL_Rect){x-2,y-2,rw,19}, comp_hov[i]);
                if (i>0 && gr[i].loyaute <= HU_FRONDEUSE) restive=true;   /* minorité restive */
                y += 20;
            }
            snprintf(line,sizeof line, "Âmes %ld", p.ames);
            draw_text(ren, g_font, x, y, COL_DIM, line);
            zone_add((SDL_Rect){x-2,y-2,rw,19}, "Le nombre total d'habitants de la province."); y += 20;
        } else {
            ui_section(ren, x, &y, "PEUPLE");
            ui_row(ren,x,&y,rw,"Race", p.race, COL_PARCH,
                   "L'espèce de la population.");
            snprintf(line,sizeof line, "Âmes %ld", p.ames);
            draw_text(ren, g_font, x, y, COL_PARCH, line);
            zone_add((SDL_Rect){x-2,y-2,rw,19}, "Le nombre d'habitants."); y += 20;
        }
    }
    ui_row(ren,x,&y,rw,"Flux", label_flux(p.flux), band_good(p.flux,5,true), hover_flux());

    ui_section(ren, x, &y, "ÉCONOMIE");
    ui_row(ren,x,&y,rw,"Vocation", p.vocation, COL_PARCH,
           "Ce que la province fait de mieux ; sa place dans l'économie du royaume.");
    ui_row(ren,x,&y,rw,"Ressource", p.ressource, COL_PARCH,
           "Le bien que la province extrait ou produit.");
    ui_row(ren,x,&y,rw,"Aisance", label_aisance(p.aisance), band_good(p.aisance,4,true), hover_aisance());
    if (p.carrefour != CF_NONE)
        ui_row(ren,x,&y,rw,"Carrefour", label_carrefour(p.carrefour), band_good(p.carrefour,4,true), hover_carrefour());

    ui_section(ren, x, &y, "ALLÉGEANCE");
    ui_row(ren,x,&y,rw,"Humeur", label_humeur(p.humeur), band_good(p.humeur,5,true), hover_humeur());
    ui_row(ren,x,&y,rw,"Lignée", label_lignee(p.lignee), band_good(p.lignee,6,false), hover_lignee());
    snprintf(line,sizeof line, "%d", p.agitation.value);
    ui_row(ren,x,&y,rw,"Agitation", line, band_good(band_agitation(p.agitation.value),4,false), hover_agitation());
    if (p.seuil_revolte) {
        draw_text(ren, g_font, x, y, sense_color(0.06f), "⚑ Au bord de la révolte");
        zone_add((SDL_Rect){x-2,y-2,rw,19},
                 "L'agitation a franchi le seuil : maintenue, elle vire à la révolte ouverte.");
        y += 20;
    }

    /* BÂTIMENTS — chaque âme consomme 1 logement + 1 service : on affiche les places
     * ENCORE LIBRES (capacité − pop), pas un score. Plus deux SLOTS RÉSERVÉS :
     * Défense (palissade→citadelle) et Spécialisation (port/mine/atelier). */
    ui_section(ren, x, &y, "BÂTIMENTS");
    if (p.logements_libres>=0) snprintf(line,sizeof line, "%ld libres / %ld", p.logements_libres, p.logements_cap);
    else                       snprintf(line,sizeof line, "complet · manque %ld", -p.logements_libres);
    ui_row(ren,x,&y,rw,"Logements", line, p.logements_libres>=0?COL_PARCH:sense_color(0.10f),
           "Places d'habitat encore libres (capacité bâtie − population). Saturé = bâtir greniers/aqueducs pour loger la croissance.");
    if (p.services_libres>=0) snprintf(line,sizeof line, "%ld libres / %ld", p.services_libres, p.services_cap);
    else                      snprintf(line,sizeof line, "sous-équipé · manque %ld", -p.services_libres);
    ui_row(ren,x,&y,rw,"Services", line, p.services_libres>=0?COL_PARCH:sense_color(0.10f),
           "Services encore disponibles (capacité bâtie − population). Sous-équipé = bâtir tribunal/temple/bibliothèque.");
    ui_row(ren,x,&y,rw,"Défense", p.defense,
           (p.defense[0]=='a')?COL_DIM:COL_COPPER, p.defense_hover);
    ui_row(ren,x,&y,rw,"Spécialisation", p.specialisation, COL_COPPER, p.specialisation_hover);

    /* ACTIONS (§4) — tout passe par la couche d'agency, en JOURS. Le survol dit
     * le coût. Sur une minorité restive : DEUX chemins distincts (réprimer vs intégrer). */
    ui_section(ren, x, &y, "ACTIONS");
    draw_text(ren, g_font, x, y, COL_PARCH, "▸ Bâtir  [B]");
    zone_add((SDL_Rect){x-2,y-2,rw,19},
             "Met une construction en file (couche d'agency) : rien d'instantané, tout en jours ; "
             "coûte des matériaux (prix de marché si le stock manque). [B] file un Tribunal.");
    y += 20;
    if (restive) {
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
    bool shot = false;
    uint32_t shot_seed = 0; bool have_shot_seed = false;
    for (int i=1;i<argc;i++) {
        if (!strcmp(argv[i], "--shot")) shot = true;
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
    printf("[scps] Prêt. TAB/1-0=vues  R=regénère  clic=territoire\n");
    printf("[scps] Réglages (régénèrent) : c=continents g=âge e=érosion\n");
    printf("       l=terres m=montagnes t=température h=humidité (Maj=baisse)\n");

    /* Mode capture (--shot) : une frame (carte + bandeau + panneau sur une
     * province peuplée), sérialisée en PPM, puis sortie — vérifie l'UI sans écran. */
    if (shot) {
        int cid = country_for_panel(world, -1);
        int pcap = (cid>=0 && cid<world->n_countries) ? world->country[cid].capital_prov : -1;
        selected = (pcap>=0) ? pcap : 0;
        rp.cam_ox=cam.ox; rp.cam_oy=cam.oy; rp.cam_scale=cam.scale; rp.selected_prov=selected;
        render_map(world, pb.pixels, pb.w, pb.h, &rp, VIEW_COUNTRIES);
        pixbuf_upload(&pb);
        SDL_RenderClear(ren);
        if (pb.tex) SDL_RenderCopy(ren, pb.tex, NULL, NULL);
        if (sim.ready && g_font) {
            zone_reset();
            draw_topbar(ren, win_w, &sim, world, cid, speed);
            draw_province_panel(ren, win_w, win_h, world, sim.econ, sim.wp, sim.wl, sim.drift, selected);
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
                    /* Sélectionner la province au clic */
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
                        if (reg>=0 && agency_order_build(sim.ag, reg, EDI_TRIBUNAL))
                            printf("\n[scps] Action : Tribunal mis en file (région %d) — construit en jours.\n", reg);
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
            zone_reset();
            int cid = country_for_panel(world, selected);
            draw_topbar(ren, win_w, &sim, world, cid, speed);
            if (selected >= 0)
                draw_province_panel(ren, win_w, win_h, world, sim.econ, sim.wp, sim.wl, sim.drift, selected);
            draw_hover_footer(ren, win_w, win_h, mx2, my2);
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
    free(sim.dp); free(sim.rn); free(sim.ai); free(sim.ai_on);
    if (g_font)     TTF_CloseFont(g_font);
    if (g_font_big) TTF_CloseFont(g_font_big);
    TTF_Quit();
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
