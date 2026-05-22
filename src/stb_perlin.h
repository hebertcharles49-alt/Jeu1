/* stb_perlin.h - v0.5 - perlin noise
 * public domain single-header (compact subset of stb_perlin)
 * https://github.com/nothings/stb/blob/master/stb_perlin.h
 *
 * Usage :
 *   #define STB_PERLIN_IMPLEMENTATION
 *   #include "stb_perlin.h"
 *
 * Renvoie un float dans ~[-1, 1].
 *   stb_perlin_noise3(x, y, z, wrap_x, wrap_y, wrap_z)
 * Pass 0 for wrap_* pour ne pas wraper.
 *
 * Implementation classique : permutation table + fade + grad + lerp.
 * Compact ~70 lignes (le stb full a beaucoup d'extras qu'on n'utilise pas). */

#ifndef STB_PERLIN_H
#define STB_PERLIN_H

#ifdef __cplusplus
extern "C" {
#endif

float stb_perlin_noise3(float x, float y, float z,
                        int wrap_x, int wrap_y, int wrap_z);

/* Variante : ridge noise (abs(perlin) inversé) pour effet "veines de stone" */
float stb_perlin_ridge_noise3(float x, float y, float z,
                              float lacunarity, float gain, float offset,
                              int octaves);

/* fbm : Fractal Brownian Motion -- superposition d'octaves */
float stb_perlin_fbm_noise3(float x, float y, float z,
                            float lacunarity, float gain, int octaves);

#ifdef __cplusplus
}
#endif

#endif /* STB_PERLIN_H */

#ifdef STB_PERLIN_IMPLEMENTATION

#include <math.h>

/* table de permutation aleatoire 0..255, doublee pour wrap simple */
static const unsigned char stb__perlin_randtab[512] = {
    23,125,161,52,103,117,70,37,247,101,203,169,124,126,44,123,
    152,238,145,45,171,114,253,10,192,136,4,157,249,30,35,72,
    175,63,77,90,181,16,96,111,133,104,75,162,93,56,66,240,
    8,50,84,229,49,210,173,239,141,1,87,18,2,198,143,57,
    225,160,58,217,168,206,245,204,199,6,73,60,20,230,211,233,
    94,200,88,9,74,155,33,15,219,130,226,202,83,236,42,172,
    165,218,55,222,46,107,98,154,109,67,196,178,127,158,13,243,
    65,79,166,248,25,224,115,80,68,51,184,128,232,208,151,122,
    26,212,105,43,179,213,235,148,146,89,14,195,28,78,112,76,
    250,47,24,251,140,108,186,190,228,170,183,139,39,188,244,246,
    132,48,119,144,180,138,134,193,82,182,120,121,86,220,209,3,
    91,241,149,85,205,150,113,216,31,100,41,164,177,214,153,231,
    38,71,185,174,97,201,29,95,7,92,54,254,191,118,34,221,
    131,11,163,99,234,81,227,147,156,176,17,142,69,12,110,62,
    27,255,0,194,59,116,242,252,19,21,187,53,207,129,64,135,
    61,40,167,237,102,223,106,159,197,189,215,137,36,32,22,5,

    /* second copy (wrap) */
    23,125,161,52,103,117,70,37,247,101,203,169,124,126,44,123,
    152,238,145,45,171,114,253,10,192,136,4,157,249,30,35,72,
    175,63,77,90,181,16,96,111,133,104,75,162,93,56,66,240,
    8,50,84,229,49,210,173,239,141,1,87,18,2,198,143,57,
    225,160,58,217,168,206,245,204,199,6,73,60,20,230,211,233,
    94,200,88,9,74,155,33,15,219,130,226,202,83,236,42,172,
    165,218,55,222,46,107,98,154,109,67,196,178,127,158,13,243,
    65,79,166,248,25,224,115,80,68,51,184,128,232,208,151,122,
    26,212,105,43,179,213,235,148,146,89,14,195,28,78,112,76,
    250,47,24,251,140,108,186,190,228,170,183,139,39,188,244,246,
    132,48,119,144,180,138,134,193,82,182,120,121,86,220,209,3,
    91,241,149,85,205,150,113,216,31,100,41,164,177,214,153,231,
    38,71,185,174,97,201,29,95,7,92,54,254,191,118,34,221,
    131,11,163,99,234,81,227,147,156,176,17,142,69,12,110,62,
    27,255,0,194,59,116,242,252,19,21,187,53,207,129,64,135,
    61,40,167,237,102,223,106,159,197,189,215,137,36,32,22,5,
};

static float stb__perlin_fade(float t) {
    return ((6.0f * t - 15.0f) * t + 10.0f) * t * t * t;
}

static float stb__perlin_grad(int h, float x, float y, float z) {
    /* 12 gradients aux aretes d'un cube. h indexe sur 16, plie sur 12. */
    int h12 = h & 11;
    static const float G[12 * 3] = {
        1, 1, 0,  -1, 1, 0,  1,-1, 0,  -1,-1, 0,
        1, 0, 1,  -1, 0, 1,  1, 0,-1,  -1, 0,-1,
        0, 1, 1,   0,-1, 1,  0, 1,-1,   0,-1,-1,
    };
    return G[h12*3+0]*x + G[h12*3+1]*y + G[h12*3+2]*z;
}

static int stb__perlin_fastfloor(float x) {
    int xi = (int)x;
    return (x < xi) ? xi - 1 : xi;
}

float stb_perlin_noise3(float x, float y, float z,
                        int wrap_x, int wrap_y, int wrap_z) {
    int xi = stb__perlin_fastfloor(x);
    int yi = stb__perlin_fastfloor(y);
    int zi = stb__perlin_fastfloor(z);
    float xf = x - xi;
    float yf = y - yi;
    float zf = z - zi;
    int xm = wrap_x ? (wrap_x - 1) : 0xFF;
    int ym = wrap_y ? (wrap_y - 1) : 0xFF;
    int zm = wrap_z ? (wrap_z - 1) : 0xFF;
    xi &= xm; yi &= ym; zi &= zm;
    int xi1 = (xi + 1) & xm;
    float u = stb__perlin_fade(xf);
    float v = stb__perlin_fade(yf);
    float w = stb__perlin_fade(zf);
    int A  = stb__perlin_randtab[xi]  + yi;
    int AA = stb__perlin_randtab[A]   + zi;
    int AB = stb__perlin_randtab[A+1] + zi;
    int B  = stb__perlin_randtab[xi1] + yi;
    int BA = stb__perlin_randtab[B]   + zi;
    int BB = stb__perlin_randtab[B+1] + zi;
    float n000 = stb__perlin_grad(stb__perlin_randtab[AA  ], xf,   yf,   zf  );
    float n100 = stb__perlin_grad(stb__perlin_randtab[BA  ], xf-1, yf,   zf  );
    float n010 = stb__perlin_grad(stb__perlin_randtab[AB  ], xf,   yf-1, zf  );
    float n110 = stb__perlin_grad(stb__perlin_randtab[BB  ], xf-1, yf-1, zf  );
    float n001 = stb__perlin_grad(stb__perlin_randtab[AA+1], xf,   yf,   zf-1);
    float n101 = stb__perlin_grad(stb__perlin_randtab[BA+1], xf-1, yf,   zf-1);
    float n011 = stb__perlin_grad(stb__perlin_randtab[AB+1], xf,   yf-1, zf-1);
    float n111 = stb__perlin_grad(stb__perlin_randtab[BB+1], xf-1, yf-1, zf-1);
    float x00 = n000 + u * (n100 - n000);
    float x10 = n010 + u * (n110 - n010);
    float x01 = n001 + u * (n101 - n001);
    float x11 = n011 + u * (n111 - n011);
    float y0  = x00  + v * (x10  - x00);
    float y1  = x01  + v * (x11  - x01);
    return y0 + w * (y1 - y0);
}

float stb_perlin_fbm_noise3(float x, float y, float z,
                            float lacunarity, float gain, int octaves) {
    float sum = 0.0f, amp = 1.0f, freq = 1.0f, maxv = 0.0f;
    for (int i = 0; i < octaves; i++) {
        sum  += amp * stb_perlin_noise3(x * freq, y * freq, z * freq, 0, 0, 0);
        maxv += amp;
        amp  *= gain;
        freq *= lacunarity;
    }
    return (maxv > 0.f) ? sum / maxv : 0.f;
}

float stb_perlin_ridge_noise3(float x, float y, float z,
                              float lacunarity, float gain, float offset,
                              int octaves) {
    float sum = 0.0f, amp = 0.5f, freq = 1.0f, prev = 1.0f;
    for (int i = 0; i < octaves; i++) {
        float n = stb_perlin_noise3(x*freq, y*freq, z*freq, 0, 0, 0);
        n = offset - (n < 0 ? -n : n);
        n = n * n * prev;
        sum  += n * amp;
        prev = n;
        freq *= lacunarity;
        amp  *= gain;
    }
    return sum;
}

#endif /* STB_PERLIN_IMPLEMENTATION */
