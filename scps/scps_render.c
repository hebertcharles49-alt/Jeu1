/*
 * scps_render.c — rendu EU4-style
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
#include "scps_render.h"
#include "scps_world.h"
#include <math.h>
#include <string.h>

const char *VIEW_NAMES[VIEW_COUNT] = {
    "Terrain","Territoires","Régions","Pays","Continents","Altimétrie",
    "Fertilité","Humidité","Température","Ressources","Habitabilité",
    "Culture","Foi"
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

/* ---- Eau : gradient profondeur propre (sans relief sous-marin apparent) */
static const uint32_t WATER_COAST   = 0xFF3B7EAAu;  /* plateau continental */
static const uint32_t WATER_MID     = 0xFF1A4870u;  /* mer ouverte */
static const uint32_t WATER_DEEP    = 0xFF091626u;  /* abysses */

static uint32_t water_color(float height) {
    float depth = clampf((SEA_LEVEL - height) / 0.55f, 0.f, 1.f); /* normalisé */
    /* Trois stops : côte → mer → abysses, sans sinusoïde sur height */
    uint32_t col;
    if (depth < 0.35f)
        col = lerp_color(WATER_COAST, WATER_MID,  depth/0.35f);
    else
        col = lerp_color(WATER_MID,   WATER_DEEP, (depth-0.35f)/0.65f);
    return col;
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

/* Fondu BILINÉAIRE des couleurs de biome entre les 4 cellules voisines (le
 * « blending » de présentation, §1a) : les bords de biome deviennent des
 * dégradés au lieu de bandes nettes. Le poids d'une cellule MER est annulé →
 * la côte reste franche (pas de boue terre/mer). Enroulé en X (monde rond).
 * PRÉSENTATION pure : ne lit que la carte (biome/hauteur), jamais une
 * coordonnée SCPS, et ne touche ni la simulation ni les biomes stockés. */
static uint32_t biome_blend(const World *w, int cx, int cy, float fx, float fy) {
    int x1=(cx+1)%SCPS_W, y1=(cy+1<SCPS_H)?cy+1:cy;
    const Cell *q[4]={ scps_cellc(w,cx,cy), scps_cellc(w,x1,cy),
                       scps_cellc(w,cx,y1), scps_cellc(w,x1,y1) };
    float wt[4]={ (1.f-fx)*(1.f-fy), fx*(1.f-fy), (1.f-fx)*fy, fx*fy };
    float sum=0.f, r=0.f, g=0.f, b=0.f;
    for (int i=0;i<4;i++){
        if (q[i]->height<SEA_LEVEL) continue;      /* la mer ne déteint pas sur la terre */
        uint32_t col=biome_base_color(q[i]->biome);
        r+=ch_r(col)*wt[i]; g+=ch_g(col)*wt[i]; b+=ch_b(col)*wt[i]; sum+=wt[i];
    }
    if (sum<1e-4f) return biome_base_color(q[0]->biome);
    return rgba(r/sum, g/sum, b/sum, 1.f);
}

/* ---- Rendu d'une cellule individuelle -------------------------------- */
static uint32_t cell_color(const World *w, int cx, int cy, float fx, float fy,
                            ViewMode mode, int selected_prov, const uint32_t *region_tint) {
    const Cell *c = scps_cellc(w, cx, cy);
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

    /* ---- Humidité (ocre sec → bleu humide) --------------------------- */
    if (mode == VIEW_MOISTURE) {
        float m = c->moisture;
        return rgba(0.85f-0.62f*m, 0.72f-0.12f*m, 0.32f+0.60f*m, 1.f);
    }

    /* ---- Température (bleu froid → rouge chaud) ----------------------- */
    if (mode == VIEW_TEMPERATURE) return heatmap(c->temperature);

    /* ---- Habitabilité (rouge=mort, jaune=marginal, vert=fertile) ----- */
    if (mode == VIEW_HABITABILITY) {
        if (h < SEA_LEVEL) return water_color(h);
        float hab = 0.f;
        if (c->province >= 0) hab = w->province[c->province].habitability;
        /* 0→rouge vif, 0.15→orange, 0.40→jaune, 0.70→vert clair, 1→vert */
        float r2, g2, b2;
        if (hab < 0.15f) {
            float t2 = hab / 0.15f;
            r2=1.f; g2=t2*0.55f; b2=0.f;
        } else if (hab < 0.40f) {
            float t2 = (hab-0.15f)/0.25f;
            r2=1.f-t2*0.3f; g2=0.55f+t2*0.35f; b2=0.f;
        } else if (hab < 0.70f) {
            float t2 = (hab-0.40f)/0.30f;
            r2=0.7f-t2*0.7f; g2=0.90f; b2=t2*0.20f;
        } else {
            float t2 = (hab-0.70f)/0.30f;
            r2=0.f; g2=0.90f-t2*0.25f; b2=0.20f+t2*0.10f;
        }
        /* Conserver le hillshading léger pour la lisibilité du relief */
        float sh2 = 0.60f + c->shade*0.40f;
        return rgba(r2*sh2, g2*sh2, b2*sh2, 1.f);
    }

    /* ---- Ressources (couleur du bien commercial × hillshading) ------- */
    if (mode == VIEW_RESOURCES) {
        if (c->province < 0) return shade_color(biome_base_color(c->biome), c->shade);
        uint32_t rc = resource_color(w->province[c->province].resource);
        uint32_t col = shade_color(rc, 0.55f + 0.45f*c->shade);
        if (c->border_prov) col = lerp_color(col, 0xFF101820u, 0.55f);
        return col;
    }

    /* ---- Terrain de base (fondu de biome) + hillshading -------------- */
    uint32_t base = biome_blend(w, cx, cy, fx, fy);
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
        terrain = alpha_over(terrain, w->region[c->region].color, 0.45f);
    }
    /* ---- Overlay pays ------------------------------------------------ */
    if (mode == VIEW_COUNTRIES && c->country >= 0) {
        terrain = alpha_over(terrain, w->country[c->country].color, 0.52f);
    }
    /* ---- Overlay continents ------------------------------------------ */
    if (mode == VIEW_CONTINENTS && c->continent >= 0) {
        terrain = alpha_over(terrain, w->continent[c->continent].color, 0.50f);
    }
    /* ---- Overlay CULTURE / FOI : teinte par région, fournie par l'appelant
     *      (le viewer la calcule depuis l'éco ; le renderer ne lit rien de SCPS). */
    if ((mode == VIEW_CULTURE || mode == VIEW_FAITH) && region_tint && c->region >= 0) {
        terrain = alpha_over(terrain, region_tint[c->region], 0.55f);
    }

    /* ---- Rivières (overlay bleu) ------------------------------------- */
    if (c->river > 70 && !c->lake) {
        float rs = clampf(c->river / 255.f, 0.f, 1.f);
        terrain = alpha_over(terrain, 0xFF3888D8u, rs * 0.72f);
    }

    /* ---- Frontières (selon le niveau affiché) ------------------------ */
    bool political = (mode==VIEW_POLITICAL||mode==VIEW_REGIONS||
                      mode==VIEW_COUNTRIES||mode==VIEW_CONTINENTS);
    if (political) {
        if (mode==VIEW_CONTINENTS) {
            /* pas de frontière interne, juste le trait de côte (géré ailleurs) */
        } else if (mode==VIEW_COUNTRIES && c->border_country) {
            terrain = lerp_color(terrain, 0xFF0C1018u, 0.78f);  /* frontière de pays */
        } else if ((mode==VIEW_REGIONS||mode==VIEW_POLITICAL) && c->border_reg) {
            terrain = lerp_color(terrain, 0xFF101820u, 0.70f);
        } else if (mode==VIEW_POLITICAL && c->border_prov) {
            terrain = lerp_color(terrain, 0xFF202838u, 0.55f);
        }
        /* En vue Pays, souligner aussi finement les régions internes */
        if (mode==VIEW_COUNTRIES && c->border_reg && !c->border_country)
            terrain = lerp_color(terrain, 0xFF202838u, 0.30f);
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
            if (cx < 0 || cx >= SCPS_W || cy < 0 || cy >= SCPS_H) {
                /* Hors carte : fond sombre */
                col = 0xFF080C10u;
            } else {
                /* position fractionnaire dans la cellule → fondu bilinéaire */
                col = cell_color(w, cx, cy, wx-(float)cx, wy-(float)cy, mode, p->selected_prov, p->region_tint);
            }
            pixels[sy * pw + sx] = col;
        }
    }
}
