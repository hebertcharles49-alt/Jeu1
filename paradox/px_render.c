/*
 * px_render.c — rendu EU4-style
 *
 * Pipeline par pixel :
 *   1. Eau       : gradient profondeur (côte clair → profond sombre)
 *   2. Terrain   : couleur biome × hillshading
 *   3. Ombre côte : assombrit les cellules terrestres adjacentes à la mer
 *   4. Overlay   : couleur de province/région selon le mode (alpha blending)
 *   5. Rivières  : overlay bleu proportionnel au flux
 *   6. Frontières: ligne sombre entre provinces (1px) / régions (2px)
 *   7. Sélection : surligné jaune sur la province active
 */
#include "px_render.h"
#include "px_world.h"
#include <math.h>
#include <string.h>

const char *VIEW_NAMES[VIEW_COUNT] = {
    "Terrain","Politique","Régions","Altimétrie","Fertilité"
};

/* ---- Primitives couleur ---------------------------------------------- */
static inline float    clampf(float v,float lo,float hi){return v<lo?lo:v>hi?hi:v;}
static inline uint8_t  u8(float v) { return (v<0.f)?0:(v>1.f)?255:(uint8_t)(v*255.f); }
static inline float    ch_r(uint32_t c) { return ((c>>16)&0xFF)/255.f; }
static inline float    ch_g(uint32_t c) { return ((c>> 8)&0xFF)/255.f; }
static inline float    ch_b(uint32_t c) { return ((c    )&0xFF)/255.f; }
static inline uint32_t rgba(float r,float g,float b,float a) {
    (void)a;
    return 0xFF000000u|((uint32_t)u8(r)<<16)|((uint32_t)u8(g)<<8)|u8(b);
}

/* Multiplie la luminosité (shade) d'une couleur ARGB */
static inline uint32_t shade_color(uint32_t c, float s) {
    return rgba(ch_r(c)*s, ch_g(c)*s, ch_b(c)*s, 1.f);
}

/* Mélange linéaire entre deux couleurs */
static inline uint32_t lerp_color(uint32_t a, uint32_t b, float t) {
    if (t<=0.f) return a;
    if (t>=1.f) return b;
    float it=1.f-t;
    return rgba(ch_r(a)*it+ch_r(b)*t,
                ch_g(a)*it+ch_g(b)*t,
                ch_b(a)*it+ch_b(b)*t, 1.f);
}

/* Alpha-blending : superpose src (avec alpha) sur dst */
static inline uint32_t alpha_over(uint32_t dst, uint32_t src, float alpha) {
    if (alpha<=0.f) return dst;
    if (alpha>=1.f) return src;
    float ia=1.f-alpha;
    return rgba(ch_r(dst)*ia+ch_r(src)*alpha,
                ch_g(dst)*ia+ch_g(src)*alpha,
                ch_b(dst)*ia+ch_b(src)*alpha, 1.f);
}

/* ---- Eau : gradient profondeur --------------------------------------- */
static const uint32_t WATER_SHALLOW = 0xFF2C6898u;
static const uint32_t WATER_DEEP    = 0xFF0A1828u;

static uint32_t water_color(float height) {
    /* depth 0=côte (SEA_LEVEL) → 1=profond */
    float depth = clampf((SEA_LEVEL - height) / SEA_LEVEL * 2.f, 0.f, 1.f);
    uint32_t col = lerp_color(WATER_SHALLOW, WATER_DEEP, depth);
    /* Légère variation texture (banding subtil) */
    float band = 0.92f + 0.08f * sinf(height * 180.f);
    return shade_color(col, band);
}

/* ---- Heatmap [0..1] → ARGB ------------------------------------------ */
static uint32_t heatmap(float v) {
    v = clampf(v, 0.f, 1.f);
    float r,g,b;
    if      (v<0.25f){float t=v/0.25f;       r=0.f;   g=t;     b=1.f;}
    else if (v<0.50f){float t=(v-0.25f)/0.25f;r=0.f;  g=1.f;   b=1.f-t;}
    else if (v<0.75f){float t=(v-0.50f)/0.25f;r=t;    g=1.f;   b=0.f;}
    else             {float t=(v-0.75f)/0.25f;r=1.f;  g=1.f-t; b=0.f;}
    return rgba(r,g,b,1.f);
}

/* ---- Rendu d'une cellule individuelle -------------------------------- */
static uint32_t cell_color(const World *w, int cx, int cy,
                            ViewMode mode, int selected_prov) {
    const Cell *c = px_cellc(w, cx, cy);
    float h = c->height;

    /* ---- Eau --------------------------------------------------------- */
    if (h < SEA_LEVEL) {
        if (mode == VIEW_HEIGHT) {
            float d = h / SEA_LEVEL;
            return rgba(d*0.2f, d*0.3f, d*0.5f+0.1f, 1.f);
        }
        return water_color(h);
    }

    /* ---- Altimétrie -------------------------------------------------- */
    if (mode == VIEW_HEIGHT) {
        float t = (h - SEA_LEVEL) / (1.f - SEA_LEVEL);
        return rgba(t, t, t, 1.f);
    }

    /* ---- Fertilité --------------------------------------------------- */
    if (mode == VIEW_FERTILITY) return heatmap(c->fertility);

    /* ---- Terrain de base + hillshading ------------------------------- */
    uint32_t base = biome_base_color(c->biome);
    float    sh   = c->shade;

    /* Ombre côtière : assombrit le bord des terres */
    if (c->coast) sh *= 0.80f;

    /* Effet "soulèvement" des rivières : légère clarté en fond de vallée */
    if (c->river > 60 && !c->lake) {
        float rs = c->river / 255.f;
        sh = sh * (1.f - rs * 0.15f) + rs * 0.08f;
    }

    uint32_t terrain = shade_color(base, sh);

    /* ---- Lacs -------------------------------------------------------- */
    if (c->lake) {
        terrain = lerp_color(terrain, 0xFF3878A8u, 0.65f);
    }

    /* ---- Overlay politique ------------------------------------------- */
    if (mode == VIEW_POLITICAL && c->province >= 0) {
        const Province *pv = &w->province[c->province];
        uint32_t pcol = pv->color;
        /* Blend plus fort sur les zones plates, plus faible sur les reliefs */
        float blend = 0.50f - (h - SEA_LEVEL) * 0.3f;
        blend = clampf(blend, 0.30f, 0.58f);
        terrain = alpha_over(terrain, pcol, blend);
    }

    /* ---- Overlay régions --------------------------------------------- */
    if (mode == VIEW_REGIONS && c->region >= 0) {
        uint32_t rcol = w->region[c->region].color;
        terrain = alpha_over(terrain, rcol, 0.45f);
    }

    /* ---- Rivières (overlay bleu) ------------------------------------- */
    if (c->river > 70 && !c->lake) {
        float rs = clampf(c->river / 255.f, 0.f, 1.f);
        terrain = alpha_over(terrain, 0xFF3888D8u, rs * 0.72f);
    }

    /* ---- Frontières -------------------------------------------------- */
    bool draw_prov_border = (mode==VIEW_POLITICAL || mode==VIEW_REGIONS)
                          && c->border_prov;
    bool draw_reg_border  = (mode==VIEW_POLITICAL || mode==VIEW_REGIONS)
                          && c->border_reg;

    if (draw_reg_border) {
        /* Frontière de région : ligne sombre épaisse */
        terrain = lerp_color(terrain, 0xFF101820u, 0.70f);
    } else if (draw_prov_border) {
        /* Frontière de province : ligne sombre fine */
        terrain = lerp_color(terrain, 0xFF202838u, 0.60f);
    }

    /* ---- Province sélectionnée : surligné jaune ---------------------- */
    if (selected_prov >= 0 && c->province == selected_prov) {
        if (c->border_prov) {
            terrain = lerp_color(terrain, 0xFFFFDD00u, 0.80f);
        } else {
            /* Très léger tint doré sur l'intérieur */
            terrain = lerp_color(terrain, 0xFFFFDD00u, 0.12f);
        }
    }

    return terrain;
}

/* ========================================================================
 * RENDU PRINCIPAL
 * ====================================================================== */
void render_map(const World *w, uint32_t *pixels, int pw, int ph,
                const RenderParams *p, ViewMode mode) {
    if (!w || !pixels) return;

    float inv_scale = 1.f / p->cam_scale;

    for (int sy = 0; sy < ph; sy++) {
        float wy = sy * inv_scale + p->cam_oy;
        for (int sx = 0; sx < pw; sx++) {
            float wx = sx * inv_scale + p->cam_ox;
            int cx = (int)wx, cy = (int)wy;

            uint32_t col;
            if (cx < 0 || cx >= PX_W || cy < 0 || cy >= PX_H) {
                /* Hors carte : fond sombre */
                col = 0xFF080C10u;
            } else {
                col = cell_color(w, cx, cy, mode, p->selected_prov);
            }
            pixels[sy * pw + sx] = col;
        }
    }
}
