/*
 * paradox/worldgen.c
 * Pipeline de génération de monde en 4 passes de bruit :
 *   Géologie → Architecture → Érosion → Civilisation
 * puis biomes, lacs, provinces Voronoï, fiche SCPS.
 */
#define STB_PERLIN_IMPLEMENTATION
#include "../src/stb_perlin.h"
#include "worldgen.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>

/* ========================================================================
 * Utilitaires de base
 * ====================================================================== */

static uint32_t g_rng;
static void rng_seed(uint32_t s) { g_rng = s ^ 0x9E3779B9u; }
static uint32_t rng_next(void) {
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 17;
    g_rng ^= g_rng << 5;
    return g_rng;
}
static float rng_f(void) { return (rng_next() & 0xFFFFFFu) * (1.0f/0x1000000u); }

static inline int   clampx(int x) { return x < 0 ? 0 : x >= PX_W ? PX_W-1 : x; }
static inline int   clampy(int y) { return y < 0 ? 0 : y >= PX_H ? PX_H-1 : y; }
static inline float clampf(float v, float lo, float hi) {
    return v < lo ? lo : v > hi ? hi : v;
}

static void normalize_arr(float *a, int n) {
    float mn = a[0], mx = a[0];
    for (int i = 1; i < n; i++) {
        if (a[i] < mn) mn = a[i];
        if (a[i] > mx) mx = a[i];
    }
    float r = mx - mn;
    if (r < 1e-7f) r = 1e-7f;
    for (int i = 0; i < n; i++) a[i] = (a[i] - mn) / r;
}

/* Directions 8-connexes (N, NE, E, SE, S, SW, W, NW) */
static const int DDX[8] = { 0, 1, 1, 1, 0,-1,-1,-1};
static const int DDY[8] = {-1,-1, 0, 1, 1, 1, 0,-1};
/* Distance diagonale */
static const float DDIST[8] = {1.f,1.414f,1.f,1.414f,1.f,1.414f,1.f,1.414f};

/* ========================================================================
 * COUCHE 1 — GÉOLOGIE
 * Plaques tectoniques → relief de base + chaînes de montagnes
 * ====================================================================== */

#define N_PLATES 16

typedef struct {
    float cx, cy;
    int   oceanic;   /* 1 = plaque océanique */
    float drift_x, drift_y;
} Plate;

static Plate g_plates[N_PLATES];

static void gen_plates(void) {
    for (int i = 0; i < N_PLATES; i++) {
        g_plates[i].cx = rng_f() * PX_W;
        g_plates[i].cy = rng_f() * PX_H;
        g_plates[i].oceanic = (rng_f() < 0.38f) ? 1 : 0;
        float angle = rng_f() * 6.2832f;
        g_plates[i].drift_x = cosf(angle);
        g_plates[i].drift_y = sinf(angle);
    }
}

/* Retourne le score de frontière [0..1] et les deux plaques les plus proches */
static float plate_boundary(int px, int py, int *pa, int *pb) {
    float x = (float)px, y = (float)py;
    float d1 = 1e30f, d2 = 1e30f;
    *pa = 0; *pb = 1;
    for (int i = 0; i < N_PLATES; i++) {
        float dx = x - g_plates[i].cx;
        float dy = y - g_plates[i].cy;
        float d  = sqrtf(dx*dx + dy*dy);
        if (d < d1) { d2 = d1; *pb = *pa; d1 = d; *pa = i; }
        else if (d < d2) { d2 = d; *pb = i; }
    }
    /* Rayon typique d'une plaque */
    float r = sqrtf((float)(PX_W * PX_H) / N_PLATES);
    float gap = d2 - d1;
    return 1.0f - clampf(gap / (r * 0.3f), 0.f, 1.f);
}

static void step_geology(float *height) {
    gen_plates();

    for (int y = 0; y < PX_H; y++) {
        for (int x = 0; x < PX_W; x++) {
            float nx = (float)x / PX_W;
            float ny = (float)y / PX_H;
            float lat = fabsf(ny - 0.5f) * 2.0f;

            /* FBM de base — forme continents/océans */
            float base = stb_perlin_fbm_noise3(nx * 4.0f, ny * 3.0f, 0.0f, 2.f, 0.5f, 6);

            /* Gradient polaire : pousse les pôles sous le niveau de la mer */
            float polar = -0.18f * lat * lat;

            height[px_idx(x,y)] = base + polar;
        }
    }

    /* Contribution des frontières de plaques */
    for (int y = 0; y < PX_H; y++) {
        for (int x = 0; x < PX_W; x++) {
            int pa, pb;
            float bs = plate_boundary(x, y, &pa, &pb);
            if (bs < 0.05f) continue;

            /* Convergence : produit scalaire des drifts */
            float dot = g_plates[pa].drift_x * g_plates[pb].drift_x
                      + g_plates[pa].drift_y * g_plates[pb].drift_y;
            float conv = (1.0f - dot) * 0.5f; /* 0=parallèles, 1=convergents */

            float bump = 0.0f;
            int oa = g_plates[pa].oceanic, ob = g_plates[pb].oceanic;
            if (!oa && !ob) {
                /* Collision continentale : grandes chaînes */
                bump = bs * conv * 0.75f;
            } else if (oa != ob) {
                /* Subduction : montagnes côtières */
                bump = bs * conv * 0.45f;
            }
            /* Modulation ridge pour éviter les bosses plates */
            float nx2 = (float)x / PX_W;
            float ny2 = (float)y / PX_H;
            float r = stb_perlin_ridge_noise3(nx2*9.f, ny2*7.f, 50.f, 2.f,0.5f,1.f,5);
            height[px_idx(x,y)] += bump * (0.55f + 0.45f * r);
        }
    }

    normalize_arr(height, PX_N);
}

/* ========================================================================
 * COUCHE 2 — ARCHITECTURE
 * Détail de relief : crêtes, falaises, vallées encaissées
 * ====================================================================== */

static void step_architecture(float *height) {
    for (int y = 0; y < PX_H; y++) {
        for (int x = 0; x < PX_W; x++) {
            float nx = (float)x / PX_W;
            float ny = (float)y / PX_H;
            float h  = height[px_idx(x,y)];

            /* Crêtes de montagne (ridge noise haute fréquence) */
            float r = stb_perlin_ridge_noise3(nx*11.f, ny*9.f, 200.f, 2.f,0.5f,1.f,5);
            float mountain_detail = r * 0.13f * clampf((h - 0.5f) / 0.4f, 0.f, 1.f);

            /* Méandres de vallée (FBM basse amplitude sur terrain bas) */
            float v = stb_perlin_fbm_noise3(nx*7.f, ny*6.f, 300.f, 2.f,0.5f,4);
            float valley_detail = v * 0.07f * clampf((0.65f - h) / 0.4f, 0.f, 1.f);

            height[px_idx(x,y)] += mountain_detail + valley_detail;
        }
    }
    normalize_arr(height, PX_N);
}

/* ========================================================================
 * COUCHE 3 — ÉROSION
 * D8 flow direction + accumulation + creusement des rivières
 * ====================================================================== */

static void step_erosion(float *height, Cell *cells) {
    /* --- Direction de flux D8 ----------------------------------------- */
    int8_t *fdir  = (int8_t*)malloc(PX_N * sizeof(int8_t));
    float  *accum = (float *)malloc(PX_N * sizeof(float));
    if (!fdir || !accum) { free(fdir); free(accum); return; }

    for (int y = 0; y < PX_H; y++) {
        for (int x = 0; x < PX_W; x++) {
            float h = height[px_idx(x,y)];
            int   best = -1;
            float drop = 0.f;
            for (int d = 0; d < 8; d++) {
                int nx2 = x + DDX[d], ny2 = y + DDY[d];
                if (nx2 < 0 || nx2 >= PX_W || ny2 < 0 || ny2 >= PX_H) continue;
                float dh = (h - height[px_idx(nx2,ny2)]) / DDIST[d];
                if (dh > drop) { drop = dh; best = d; }
            }
            fdir[px_idx(x,y)] = (int8_t)best;
        }
    }

    /* --- Accumulation de flux (passes multiples, amont→aval) ----------- */
    for (int i = 0; i < PX_N; i++) accum[i] = 1.0f;

    for (int pass = 0; pass < 48; pass++) {
        /* Descente : de haut (y=0 = nord) vers bas */
        for (int y = 0; y < PX_H; y++) {
            for (int x = 0; x < PX_W; x++) {
                int d = fdir[px_idx(x,y)];
                if (d < 0) continue;
                int nx2 = x + DDX[d], ny2 = y + DDY[d];
                if (nx2 < 0 || nx2 >= PX_W || ny2 < 0 || ny2 >= PX_H) continue;
                accum[px_idx(nx2,ny2)] += accum[px_idx(x,y)] * 0.85f;
            }
        }
    }

    /* --- Normalise et affecte aux cellules ------------------------------ */
    float max_a = 1.f;
    for (int i = 0; i < PX_N; i++) if (accum[i] > max_a) max_a = accum[i];

    for (int i = 0; i < PX_N; i++) {
        cells[i].flow_dir = fdir[i];

        float a = accum[i] / max_a;
        float rs = 0.f;
        if (a > 0.004f)
            rs = clampf(logf(1.f + a * 300.f) / logf(301.f), 0.f, 1.f);
        cells[i].river = (uint8_t)(rs * 255.f);

        /* Creusement : abaisse légèrement le fond du lit */
        if (rs > 0.08f && height[i] > SEA_LEVEL)
            height[i] -= rs * 0.04f;
    }

    normalize_arr(height, PX_N);
    free(fdir);
    free(accum);
}

/* ========================================================================
 * COUCHE 4 — CIVILISATION
 * Potentiel de peuplement = fertilité + eau + température douce + pente faible
 * ====================================================================== */

static void step_civilization(float *height, float *moisture, float *temperature,
                               float *civ, Cell *cells) {
    /* Bonus de proximité rivière (fenêtre 7×7) */
    float *river_prox = (float*)calloc(PX_N, sizeof(float));
    if (!river_prox) return;

    for (int y = 0; y < PX_H; y++) {
        for (int x = 0; x < PX_W; x++) {
            float best = 0.f;
            for (int dy = -3; dy <= 3; dy++) {
                for (int dx = -3; dx <= 3; dx++) {
                    int nx2 = clampx(x+dx), ny2 = clampy(y+dy);
                    float r = cells[px_idx(nx2,ny2)].river / 255.f;
                    float dist = sqrtf((float)(dx*dx+dy*dy)) + 1.f;
                    float v = r / dist;
                    if (v > best) best = v;
                }
            }
            river_prox[px_idx(x,y)] = best;
        }
    }

    for (int y = 0; y < PX_H; y++) {
        for (int x = 0; x < PX_W; x++) {
            int i = px_idx(x,y);
            float h = height[i], m = moisture[i], t = temperature[i];

            if (h < SEA_LEVEL) { civ[i] = 0.f; continue; }

            /* Pente locale */
            float slope = 0.f;
            for (int d = 0; d < 4; d++) {
                int nx2 = clampx(x+DDX[d*2]), ny2 = clampy(y+DDY[d*2]);
                slope += fabsf(h - height[px_idx(nx2,ny2)]);
            }
            slope /= 4.f;

            /* Température douce = meilleure pour l'agriculture */
            float t_score = 1.f - fabsf(t - 0.55f) * 1.8f;

            float f = 0.35f * m
                    + 0.25f * clampf(t_score, 0.f, 1.f)
                    + 0.25f * clampf(river_prox[i] * 2.5f, 0.f, 1.f)
                    - 0.6f  * clampf((h - MOUNTAIN_H) / 0.2f, 0.f, 1.f)
                    - 3.0f  * slope;

            civ[i] = clampf(f, 0.f, 1.f);
        }
    }
    free(river_prox);
}

/* ========================================================================
 * CLIMAT — température et humidité
 * ====================================================================== */

static void gen_climate(float *height, float *moisture, float *temperature) {
    for (int y = 0; y < PX_H; y++) {
        for (int x = 0; x < PX_W; x++) {
            float nx2 = (float)x / PX_W;
            float ny2 = (float)y / PX_H;
            float lat = fabsf(ny2 - 0.5f) * 2.f;
            float h   = height[px_idx(x,y)];

            /* Température : chaud équateur, froid pôles, froid altitude */
            float base_t = 1.f - lat;
            float alt_cold = clampf((h - 0.52f) * 2.f, 0.f, 1.f);
            float t_noise  = stb_perlin_fbm_noise3(nx2*4.f, ny2*3.f, 500.f, 2.f,0.5f,4) * 0.08f;
            temperature[px_idx(x,y)] = clampf(base_t - alt_cold + t_noise, 0.f, 1.f);

            /* Humidité : ITCZ tropical, ceinture sèche subtropicale */
            float trop    = clampf(1.f - lat * 2.8f, 0.f, 1.f);
            float subtrop = clampf(1.f - fabsf(lat - 0.28f) * 6.f, 0.f, 1.f) * (-0.45f);
            float m_noise = stb_perlin_fbm_noise3(nx2*5.f, ny2*4.f, 700.f, 2.f,0.5f,5) * 0.18f;
            moisture[px_idx(x,y)] = clampf(0.42f + trop*0.38f + subtrop + m_noise, 0.f, 1.f);
        }
    }
}

/* ========================================================================
 * BIOMES (diagramme de Whittaker adapté)
 * ====================================================================== */

static Biome assign_biome(float h, float m, float t) {
    if (h < SEA_LEVEL - 0.12f) return BIOME_DEEP_OCEAN;
    if (h < SEA_LEVEL - 0.04f) return BIOME_OCEAN;
    if (h < SEA_LEVEL)         return BIOME_SHALLOW;
    if (h < SEA_LEVEL + 0.025f) return BIOME_COAST;

    if (h >= PEAK_H)           return (t < 0.18f) ? BIOME_GLACIER : BIOME_PEAK;
    if (h >= MOUNTAIN_H)       return BIOME_MOUNTAINS;
    if (h >= MOUNTAIN_H - 0.08f) return (t < 0.25f) ? BIOME_HIGHLANDS : BIOME_HILLS;

    /* Terrain plat : température × humidité */
    if (t < 0.18f) {
        return (m > 0.38f) ? BIOME_FOREST : BIOME_GLACIER;
    }
    if (t < 0.34f) {
        if (m > 0.55f) return BIOME_FOREST;
        if (m > 0.32f) return BIOME_WOODS;
        return BIOME_STEPPE;
    }
    if (t < 0.52f) {
        if (m > 0.65f) return BIOME_FOREST;
        if (m > 0.48f) return BIOME_WOODS;
        if (m > 0.30f) return BIOME_GRASSLAND;
        if (m > 0.15f) return BIOME_PLAINS;
        return BIOME_STEPPE;
    }
    if (t < 0.70f) {
        if (m > 0.60f) return (h < SEA_LEVEL + 0.06f) ? BIOME_MARSH : BIOME_JUNGLE;
        if (m > 0.40f) return BIOME_FARMLAND;
        if (m > 0.22f) return BIOME_SAVANNA;
        if (m > 0.10f) return BIOME_DRYLANDS;
        return (h < SEA_LEVEL + 0.05f) ? BIOME_COASTAL_DESERT : BIOME_DESERT;
    }
    /* Chaud (tropical) */
    if (m > 0.65f) return BIOME_JUNGLE;
    if (m > 0.42f) return BIOME_SAVANNA;
    if (m > 0.20f) return BIOME_DRYLANDS;
    return (h < SEA_LEVEL + 0.06f) ? BIOME_COASTAL_DESERT : BIOME_DESERT;
}

/* ========================================================================
 * LACS — remplissage des dépressions terrestres
 * ====================================================================== */

static void fill_lakes(float *height, Cell *cells) {
    for (int y = 1; y < PX_H-1; y++) {
        for (int x = 1; x < PX_W-1; x++) {
            int i = px_idx(x,y);
            float h = height[i];
            if (h < SEA_LEVEL + 0.02f) continue;

            /* Dépression : tous les voisins 4-connexes plus hauts */
            bool depression = true;
            for (int d = 0; d < 8; d += 2) {
                int nx2 = x + DDX[d], ny2 = y + DDY[d];
                if (height[px_idx(nx2,ny2)] < h) { depression = false; break; }
            }
            if (depression) {
                cells[i].lake = true;
                height[i] = SEA_LEVEL + 0.01f;
            }
        }
    }
}

/* ========================================================================
 * PROVINCES — Voronoï pondéré par le potentiel de civilisation
 * ====================================================================== */

#define MIN_PROVINCE_SPACING 22

static int g_prov_seeds_x[PX_MAX_PROVINCES];
static int g_prov_seeds_y[PX_MAX_PROVINCES];

static int pick_province_seeds(float *civ, int n_want) {
    /* Tirage pondéré par la civilisation, avec espacement minimum */
    float total = 0.f;
    for (int i = 0; i < PX_N; i++) total += civ[i];
    if (total < 1.f) total = 1.f;

    int n = 0;
    int attempts = 0;
    while (n < n_want && attempts < PX_N * 3) {
        attempts++;
        /* Tirage aléatoire pondéré */
        float r = rng_f() * total;
        float sum = 0.f;
        int chosen = 0;
        for (int i = 0; i < PX_N; i++) {
            sum += civ[i];
            if (sum >= r) { chosen = i; break; }
        }
        int cx = chosen % PX_W, cy = chosen / PX_W;

        /* Vérification d'espacement */
        bool ok = true;
        for (int k = 0; k < n; k++) {
            int dx = cx - g_prov_seeds_x[k];
            int dy = cy - g_prov_seeds_y[k];
            if (dx*dx + dy*dy < MIN_PROVINCE_SPACING*MIN_PROVINCE_SPACING) {
                ok = false; break;
            }
        }
        if (ok) {
            g_prov_seeds_x[n] = cx;
            g_prov_seeds_y[n] = cy;
            n++;
        }
    }
    return n;
}

static void assign_provinces(World *w, float *height) {
    int n = pick_province_seeds(/* civ layer */ NULL, PX_MAX_PROVINCES);
    /* Fallback : utiliser civilization des cellules */
    float *civ = (float*)malloc(PX_N * sizeof(float));
    if (!civ) return;
    for (int i = 0; i < PX_N; i++) civ[i] = w->cell[i].civilization;

    n = pick_province_seeds(civ, PX_MAX_PROVINCES);
    free(civ);

    if (n < 4) n = 4;
    w->n_provinces = n;

    /* Voronoï : assigner chaque cellule terrestre à la province la plus proche */
    for (int y = 0; y < PX_H; y++) {
        for (int x = 0; x < PX_W; x++) {
            int i = px_idx(x,y);
            if (height[i] < SEA_LEVEL) { w->cell[i].province = -1; continue; }

            float best_d = 1e30f;
            int   best_p = 0;
            for (int p = 0; p < n; p++) {
                float dx = (float)(x - g_prov_seeds_x[p]);
                float dy = (float)(y - g_prov_seeds_y[p]);
                /* Léger surcoût pour franchir une rivière ou une montagne */
                float cost = 1.f;
                if (w->cell[i].river > 80)   cost = 2.5f;
                if (height[i] > MOUNTAIN_H)  cost = 4.0f;
                float d = (dx*dx + dy*dy) * cost;
                if (d < best_d) { best_d = d; best_p = p; }
            }
            w->cell[i].province = (int16_t)best_p;
        }
    }

    /* Calcul des stats de province */
    int biome_count[PX_MAX_PROVINCES][BIOME_COUNT];
    memset(biome_count, 0, sizeof(biome_count));
    int area[PX_MAX_PROVINCES];
    memset(area, 0, sizeof(area));
    float lat_sum[PX_MAX_PROVINCES];
    memset(lat_sum, 0, sizeof(lat_sum));

    for (int y = 0; y < PX_H; y++) {
        for (int x = 0; x < PX_W; x++) {
            int i = px_idx(x,y);
            int p = w->cell[i].province;
            if (p < 0) continue;
            area[p]++;
            lat_sum[p] += fabsf((float)y / PX_H - 0.5f) * 2.f;
            biome_count[p][(int)w->cell[i].biome]++;
        }
    }

    for (int p = 0; p < n; p++) {
        w->province[p].seed_x = g_prov_seeds_x[p];
        w->province[p].seed_y = g_prov_seeds_y[p];
        w->province[p].area   = area[p];
        w->province[p].lat    = (area[p] > 0) ? lat_sum[p] / area[p] : 0.5f;

        /* Biome dominant */
        int best_b = 0, best_cnt = 0;
        for (int b = 0; b < BIOME_COUNT; b++) {
            if (biome_count[p][b] > best_cnt) {
                best_cnt = biome_count[p][b];
                best_b   = b;
            }
        }
        w->province[p].dominant = (Biome)best_b;
    }
}

/* ========================================================================
 * FICHE SCPS — génération des axes culturels par province
 * ====================================================================== */

static float subsistance_from_biome(Biome b) {
    switch (b) {
        case BIOME_STEPPE:
        case BIOME_SAVANNA:
        case BIOME_GRASSLAND:  return 8.5f; /* pastoral */
        case BIOME_FARMLAND:
        case BIOME_PLAINS:     return 3.0f; /* agriculture intensive */
        case BIOME_COAST:
        case BIOME_SHALLOW:    return 5.0f; /* maritime */
        case BIOME_FOREST:
        case BIOME_WOODS:
        case BIOME_JUNGLE:     return 7.0f; /* cueillette/horticulture */
        case BIOME_MARSH:      return 6.0f; /* pêche et riziculture */
        case BIOME_DESERT:
        case BIOME_DRYLANDS:   return 9.0f; /* nomadisme extrême */
        case BIOME_HIGHLANDS:
        case BIOME_HILLS:      return 7.5f; /* agropastoral de montagne */
        default:               return 5.0f;
    }
}

static void gen_scps_fiches(World *w) {
    /* Phase 1 : axes indépendants par province */
    for (int p = 0; p < w->n_provinces; p++) {
        Province *pr = &w->province[p];
        pr->subsistance = subsistance_from_biome(pr->dominant)
                        + (rng_f() - 0.5f) * 1.5f;
        pr->subsistance = clampf(pr->subsistance, 0.f, 10.f);

        /* Valeurs : opposition pôle pastoral (honneur/domination) vs agricole (hiérarchie/ordre) */
        float base_val = (pr->subsistance > 7.f) ? 7.f + rng_f()*2.5f
                                                  : 3.f + rng_f()*3.5f;
        pr->valeurs = clampf(base_val, 0.f, 10.f);

        /* Religion : base régionale + latitude */
        pr->religion = clampf(pr->lat * 6.f + rng_f() * 4.f, 0.f, 10.f);

        /* Parenté structurelle : calquée sur la subsistance avec bruit */
        pr->parente = clampf(pr->subsistance * 0.7f + rng_f() * 3.f, 0.f, 10.f);
    }

    /* Phase 2 : langue = horloge phylogénétique
     * Propagation depuis 3 proto-familles linguistiques (continents). */
    int n_families = 3;
    int fam_cx[3] = {
        PX_W / 6,
        PX_W / 2,
        PX_W * 5 / 6
    };
    int fam_cy[3] = {PX_H/2, PX_H/2, PX_H/2};

    for (int p = 0; p < w->n_provinces; p++) {
        float min_d = 1e30f;
        int   fam   = 0; (void)fam;
        for (int f = 0; f < n_families; f++) {
            float dx = (float)(w->province[p].seed_x - fam_cx[f]);
            float dy = (float)(w->province[p].seed_y - fam_cy[f]);
            float d  = sqrtf(dx*dx + dy*dy);
            if (d < min_d) { min_d = d; fam = f; }
        }
        /* Langue = distance normalisée dans l'arbre (0=proto, 10=divergé) */
        float max_r = sqrtf((float)(PX_W*PX_W + PX_H*PX_H)) / 2.f;
        w->province[p].langue = clampf(min_d / max_r * 10.f + rng_f()*1.5f - 0.75f, 0.f, 10.f);
    }
}

/* ========================================================================
 * TRACÉ DES RIVIÈRES PRINCIPALES
 * ====================================================================== */

static void trace_rivers(World *w, float *height) {
    int n = 0;
    /* Trouve les sources : cellules de haute altitude avec fort flux */
    for (int y = 2; y < PX_H-2 && n < PX_MAX_RIVERS; y += 4) {
        for (int x = 2; x < PX_W-2 && n < PX_MAX_RIVERS; x += 4) {
            int i = px_idx(x,y);
            if (height[i] < MOUNTAIN_H - 0.05f) continue;
            if (w->cell[i].river < 60) continue;

            River *rv = &w->river[n];
            rv->len = 0;
            rv->flow_max = 0.f;

            int cx = x, cy = y;
            bool seen[PX_N]; /* évite les boucles */
            memset(seen, 0, PX_N);

            for (int step = 0; step < PX_RIVER_MAXLEN; step++) {
                if (cx < 0 || cx >= PX_W || cy < 0 || cy >= PX_H) break;
                int ci = px_idx(cx, cy);
                if (seen[ci]) break;
                seen[ci] = true;

                rv->x[rv->len] = (int16_t)cx;
                rv->y[rv->len] = (int16_t)cy;
                rv->len++;

                float flow = w->cell[ci].river / 255.f;
                if (flow > rv->flow_max) rv->flow_max = flow;

                /* Atteint la mer → on s'arrête */
                if (height[ci] < SEA_LEVEL) break;

                int dir = w->cell[ci].flow_dir;
                if (dir < 0) break;
                cx += DDX[dir];
                cy += DDY[dir];
            }

            if (rv->len > 8) n++;
        }
    }
    w->n_rivers = n;
}

/* ========================================================================
 * POINT D'ENTRÉE PUBLIC
 * ====================================================================== */

void world_generate(World *w, uint32_t seed) {
    memset(w, 0, sizeof(*w));
    w->seed = seed;
    rng_seed(seed);

    float *height      = (float*)malloc(PX_N * sizeof(float));
    float *moisture    = (float*)malloc(PX_N * sizeof(float));
    float *temperature = (float*)malloc(PX_N * sizeof(float));
    float *civ         = (float*)malloc(PX_N * sizeof(float));

    if (!height || !moisture || !temperature || !civ) {
        fprintf(stderr, "paradox: allocation échouée\n");
        goto cleanup;
    }

    printf("[paradox] géologie...      ");  fflush(stdout);
    step_geology(height);
    printf("ok\n");

    printf("[paradox] architecture...  ");  fflush(stdout);
    step_architecture(height);
    printf("ok\n");

    printf("[paradox] érosion...       ");  fflush(stdout);
    step_erosion(height, w->cell);
    printf("ok\n");

    printf("[paradox] climat...        ");  fflush(stdout);
    gen_climate(height, moisture, temperature);
    printf("ok\n");

    printf("[paradox] biomes...        ");  fflush(stdout);
    for (int i = 0; i < PX_N; i++) {
        w->cell[i].height      = height[i];
        w->cell[i].moisture    = moisture[i];
        w->cell[i].temperature = temperature[i];
        w->cell[i].biome       = assign_biome(height[i], moisture[i], temperature[i]);
    }
    printf("ok\n");

    printf("[paradox] lacs...          ");  fflush(stdout);
    fill_lakes(height, w->cell);
    for (int i = 0; i < PX_N; i++) w->cell[i].height = height[i];
    printf("ok\n");

    printf("[paradox] civilisation...  ");  fflush(stdout);
    step_civilization(height, moisture, temperature, civ, w->cell);
    for (int i = 0; i < PX_N; i++) w->cell[i].civilization = civ[i];
    printf("ok\n");

    printf("[paradox] provinces...     ");  fflush(stdout);
    assign_provinces(w, height);
    printf("ok (%d provinces)\n", w->n_provinces);

    printf("[paradox] fiches SCPS...   ");  fflush(stdout);
    gen_scps_fiches(w);
    printf("ok\n");

    printf("[paradox] rivières...      ");  fflush(stdout);
    trace_rivers(w, height);
    printf("ok (%d rivières)\n", w->n_rivers);

cleanup:
    free(height);
    free(moisture);
    free(temperature);
    free(civ);
}

/* ========================================================================
 * COULEURS ET NOMS
 * ====================================================================== */

uint32_t biome_color(Biome b, bool river, bool lake) {
    if (lake)  return 0xFF4080C0u;
    if (river) return 0xFF5090D8u;

    static const uint32_t COLORS[BIOME_COUNT] = {
        0xFF0F1E2Du,  /* DEEP_OCEAN    */
        0xFF162844u,  /* OCEAN         */
        0xFF1E4870u,  /* SHALLOW       */
        0xFFD4BE82u,  /* COAST         */
        0xFF82B050u,  /* PLAINS        */
        0xFF9CC040u,  /* FARMLAND      */
        0xFF50983Au,  /* GRASSLAND     */
        0xFFB89858u,  /* STEPPE        */
        0xFFE8C848u,  /* SAVANNA       */
        0xFFCC9840u,  /* DRYLANDS      */
        0xFFE8D050u,  /* DESERT        */
        0xFFD8C870u,  /* COASTAL_DESERT*/
        0xFF286828u,  /* FOREST        */
        0xFF407838u,  /* WOODS         */
        0xFF187015u,  /* JUNGLE        */
        0xFF40806Au,  /* MARSH         */
        0xFF888060u,  /* HIGHLANDS     */
        0xFF907850u,  /* HILLS         */
        0xFF706050u,  /* MOUNTAINS     */
        0xFFB0A898u,  /* PEAK          */
        0xFFE8F0F8u,  /* GLACIER       */
    };
    if (b < 0 || b >= BIOME_COUNT) return 0xFFFF00FFu;
    return COLORS[(int)b];
}

uint32_t province_color(int id) {
    /* Palette de 16 teintes saturées distinctes, cyclique */
    static const uint32_t PAL[16] = {
        0xC0C03030u, 0xC03040C0u, 0xC030A030u, 0xC0C07020u,
        0xC03090A0u, 0xC0A02880u, 0xC060A820u, 0xC0C04060u,
        0xC02860B0u, 0xC08C2020u, 0xC020A870u, 0xC0B85020u,
        0xC04050C0u, 0xC0A08020u, 0xC020A0A0u, 0xC07030A0u,
    };
    if (id < 0) return 0x00000000u;
    return PAL[id % 16];
}

const char *biome_name(Biome b) {
    static const char *NAMES[BIOME_COUNT] = {
        "Océan profond","Océan","Eaux peu profondes","Côte",
        "Plaines","Terres cultivées","Prairies","Steppe",
        "Savane","Terres sèches","Désert","Désert côtier",
        "Forêt","Bois","Jungle","Marais",
        "Hauts plateaux","Collines","Montagnes","Sommets","Glacier",
    };
    if (b < 0 || b >= BIOME_COUNT) return "?";
    return NAMES[(int)b];
}
