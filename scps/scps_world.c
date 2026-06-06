/*
 * scps_world.c — pipeline de génération de monde en 4+N couches
 *
 * Ordre causal (doc §3) :
 *   1. Géologie   : FBM + plaques tectoniques → relief de base
 *   2. Architecture : crêtes, vallées, detail
 *   3. Érosion    : D8 + accumulation → rivières, creusement
 *   4. Climat     : température, humidité (latitude + altitude)
 *   5. Biomes     : Whittaker fantasy
 *   6. Lacs       : remplissage des dépressions
 *   7. Fertilité  : potentiel de civilisation
 *   8. Provinces  : Voronoï domain-warped + coût de terrain organique
 *   9. Régions    : Voronoï de second niveau
 *  10. Flags de rendu : côtes, frontières, hillshading
 *  11. Fiche SCPS par province
 *  12. Tracé des rivières principales
 */
#define STB_PERLIN_IMPLEMENTATION
#include "../src/stb_perlin.h"
#include "scps_world.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
#include <ctype.h>

/* ========================================================================
 * RNG (Xorshift32 — rapide, reproductible)
 * ====================================================================== */
static uint32_t g_rng;
static void     rng_seed(uint32_t s) { g_rng = s ^ 0x9E3779B9u; if (!g_rng) g_rng = 1; }
static uint32_t rng_u(void) {
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 17;
    g_rng ^= g_rng << 5;
    return g_rng;
}
static float rng_f(void)       { return (rng_u() & 0xFFFFFFu) * (1.f/0x1000000u); }

/* ========================================================================
 * Utilitaires
 * ====================================================================== */
static inline float clampf(float v, float lo, float hi) {
    return v < lo ? lo : v > hi ? hi : v;
}
static inline int clampi(int v, int lo, int hi) {
    return v < lo ? lo : v > hi ? hi : v;
}

static void normalize_f(float *a, int n) {
    float mn = a[0], mx = a[0];
    for (int i=1;i<n;i++) { if(a[i]<mn)mn=a[i]; if(a[i]>mx)mx=a[i]; }
    float r = mx - mn; if (r < 1e-7f) r = 1e-7f;
    for (int i=0;i<n;i++) a[i] = (a[i]-mn)/r;
}

/* Directions 8-connexes */
static const int DDX[8] = { 0, 1, 1, 1, 0,-1,-1,-1};
static const int DDY[8] = {-1,-1, 0, 1, 1, 1, 0,-1};
static const float DDIST[8]={1.f,1.414f,1.f,1.414f,1.f,1.414f,1.f,1.414f};

/* ========================================================================
 * COUCHE 1 — GÉOLOGIE
 * Plaques tectoniques (Voronoï) + FBM → relief de base
 * ====================================================================== */
#define N_PLATES 18

typedef struct { float cx,cy; int oceanic; float dx,dy; } Plate;
static Plate g_plates[N_PLATES];

static void plates_init(void) {
    for (int i=0;i<N_PLATES;i++) {
        g_plates[i].cx = rng_f()*SCPS_W;
        g_plates[i].cy = rng_f()*SCPS_H;
        g_plates[i].oceanic = (rng_f()<0.38f)?1:0;
        float a = rng_f()*6.2832f;
        g_plates[i].dx = cosf(a);
        g_plates[i].dy = sinf(a);
    }
}

/* Score de frontière [0..1] et indices des deux plaques les plus proches.
 * Coordonnées domain-warpées pour éviter les frontières rectilignes. */
static float plate_boundary(int px, int py, int *pa, int *pb, float seed_f) {
    float nx=(float)px/SCPS_W, ny=(float)py/SCPS_H;
    /* Warp dédié aux plaques — deux octaves (grossière + fine) pour dissoudre
     * le motif polygonal Voronoï dans les chaînes (arcs sinueux, pas droits) */
    float wx=stb_perlin_fbm_noise3(nx*1.5f+0.f,ny*1.5f+0.f,seed_f+800.f,2.f,0.5f,4)*28.f
            +stb_perlin_fbm_noise3(nx*4.2f+1.f,ny*4.2f+2.f,seed_f+820.f,2.f,0.5f,4)*11.f;
    float wy=stb_perlin_fbm_noise3(nx*1.5f+6.1f,ny*1.5f+3.4f,seed_f+810.f,2.f,0.5f,4)*28.f
            +stb_perlin_fbm_noise3(nx*4.2f+3.f,ny*4.2f+5.f,seed_f+830.f,2.f,0.5f,4)*11.f;
    float x=(float)px+wx, y=(float)py+wy;
    float d1=1e30f, d2=1e30f;
    *pa=0; *pb=1;
    for (int i=0;i<N_PLATES;i++) {
        float dx=x-g_plates[i].cx, dy=y-g_plates[i].cy;
        float d=sqrtf(dx*dx+dy*dy);
        if (d<d1){d2=d1;*pb=*pa;d1=d;*pa=i;}
        else if(d<d2){d2=d;*pb=i;}
    }
    float r=sqrtf((float)(SCPS_W*SCPS_H)/N_PLATES);
    return 1.f - clampf((d2-d1)/(r*0.28f),0.f,1.f);
}

/* Masque continental — formes multi-lobes (corps + péninsules) et archipels
 *
 * Chaque continent = 1 corps principal + 0-3 péninsules/bras elliptiques.
 * Les archipels et ponts type Béringie sont des îles indépendantes.
 */
#define MAX_LOBES 5
typedef struct {
    float cx, cy;        /* centre en px carte  */
    float ax, ay;        /* demi-axes en px     */
    float cosA, sinA;    /* rotation            */
    float strength;      /* [0.5..1.0]          */
} ContLobe;

#define MAX_CONTSHAPE 8
typedef struct { ContLobe lobe[MAX_LOBES]; int n; } ContShape;
static ContShape g_cshape[MAX_CONTSHAPE];
static int       g_ncont = 3;

#define MAX_ISLET 14
typedef struct { float cx,cy,ax,ay,cosA,sinA; } Islet;
static Islet g_islet[MAX_ISLET];
static int   g_nislet;

static void continents_init(int n, float seed_f) {
    (void)seed_f;
    if (n<1) n=1;
    if (n>MAX_CONTSHAPE) n=MAX_CONTSHAPE;
    g_ncont=n; g_nislet=0;
    float R0=(0.78f/sqrtf((float)n))*SCPS_H;  /* +50% : masses plus vastes →
                                                 place pour les mers intérieures */

    for (int i=0;i<n;i++) {
        ContShape *cs=&g_cshape[i];
        float u=(i+0.5f)/(float)n;
        float cx=(0.10f+0.80f*u)*SCPS_W+(rng_f()-0.5f)*0.08f*SCPS_W;
        float cy=(0.28f+0.44f*rng_f())*SCPS_H;

        cs->n=1+(int)(rng_f()*4.f);
        if(cs->n>MAX_LOBES)cs->n=MAX_LOBES;

        /* Lobe principal */
        {
            ContLobe *cl=&cs->lobe[0];
            cl->cx=cx; cl->cy=cy;
            float asp=0.75f+rng_f()*0.30f;   /* max 1.05 → moins de "saucisse" */
            cl->ax=R0*asp; cl->ay=R0;
            float a=rng_f()*6.2832f;
            cl->cosA=cosf(a); cl->sinA=sinf(a);
            cl->strength=1.0f;
        }
        /* Péninsules / bras — trapus et bien RECOUVRANTS (sinon ils pointent
         * hors du corps en fines « oreilles de lapin »). Portée réduite (ils
         * chevauchent le corps), aspect modéré, rayon court plus large. */
        for (int l=1;l<cs->n;l++) {
            ContLobe *cl=&cs->lobe[l];
            float angle=rng_f()*6.2832f;
            float reach=R0*(0.30f+rng_f()*0.40f);    /* recouvre le corps */
            cl->cx=cx+cosf(angle)*reach;
            cl->cy=cy+sinf(angle)*reach;
            float asp=1.10f+rng_f()*0.55f;           /* max 1.65 — péninsules moins filiformes */
            float shortR=R0*(0.26f+rng_f()*0.22f);   /* plus large */
            cl->ax=shortR*asp; cl->ay=shortR;
            cl->cosA=cosf(angle); cl->sinA=sinf(angle);
            cl->strength=0.70f+rng_f()*0.28f;        /* assez fort pour fusionner */
        }
    }

    /* Archipels : 0-3 chaînes d'îles indépendantes */
    int nchains=(int)(rng_f()*4.f);
    for (int c=0;c<nchains;c++) {
        int nisles=1+(int)(rng_f()*3.f);
        float bx=rng_f()*SCPS_W;
        float by=(0.08f+0.84f*rng_f())*SCPS_H;
        float dir=rng_f()*6.2832f;
        float spacing=0.05f*SCPS_W;
        float iR=0.012f*SCPS_H*(0.5f+rng_f()*1.5f);
        for (int k=0;k<nisles&&g_nislet<MAX_ISLET;k++) {
            Islet *il=&g_islet[g_nislet++];
            il->cx=bx+cosf(dir)*spacing*(float)k+(rng_f()-0.5f)*spacing*0.3f;
            il->cy=by+sinf(dir)*spacing*(float)k+(rng_f()-0.5f)*spacing*0.3f;
            float asp=1.f+rng_f()*1.0f;  /* max 2.0 → îlots moins filiformes */
            il->ax=iR*asp; il->ay=iR;
            float a=rng_f()*6.2832f;
            il->cosA=cosf(a); il->sinA=sinf(a);
        }
    }

    /* Béringie : pont fin entre deux continents (probabilité 35%) */
    if (n>=2 && rng_f()<0.35f && g_nislet+2<=MAX_ISLET) {
        float ax=g_cshape[0].lobe[0].cx, ay=g_cshape[0].lobe[0].cy;
        float bxc=g_cshape[1].lobe[0].cx, byc=g_cshape[1].lobe[0].cy;
        float mx=(ax+bxc)*0.5f, my=(ay+byc)*0.5f;
        my+=(rng_f()<0.5f?-1.f:1.f)*SCPS_H*0.22f; /* décalage polaire */
        float bridgeLen=0.06f*SCPS_W;
        float brAngle=rng_f()*6.2832f;
        for (int k=0;k<2;k++) {
            Islet *il=&g_islet[g_nislet++];
            il->cx=mx+cosf(brAngle)*bridgeLen*(k-0.5f);
            il->cy=my+sinf(brAngle)*bridgeLen*(k-0.5f);
            il->ax=0.018f*SCPS_W; il->ay=0.008f*SCPS_H;
            il->cosA=cosf(brAngle); il->sinA=sinf(brAngle);
        }
    }
}

/* Smooth-maximum polynomial (k = largeur de fusion). Fond deux lobes en une
 * union arrondie au lieu d'une jointure nette → péninsules soudées au corps,
 * plus d'« oreilles » saillantes. */
static inline float smaxf(float a, float b, float k) {
    float h=clampf(0.5f+0.5f*(a-b)/k,0.f,1.f);
    return (b*(1.f-h)+a*h)+k*h*(1.f-h);
}

static float continental_mask(int x, int y, float seed_f) {
    float nx=(float)x/SCPS_W, ny=(float)y/SCPS_H;
    /* Domain warping multi-échelle (Inigo Quilez, 2nd ordre) :
     *   q = fbm(p) ; r = fbm(p + q) ; on interroge le masque en p + (q,r).
     * Deux échelles superposées → l'iso-contour côtier devient fractal
     * (baies, péninsules, détroits) au lieu d'une ellipse lisse. */
    float qx=stb_perlin_fbm_noise3(nx*1.6f,     ny*1.6f,     seed_f+1200.f,2.f,0.5f,5);
    float qy=stb_perlin_fbm_noise3(nx*1.6f+5.2f,ny*1.6f+1.3f,seed_f+1210.f,2.f,0.5f,5);
    float rx=stb_perlin_fbm_noise3(nx*3.4f+qx*1.6f,     ny*3.4f+qy*1.6f,     seed_f+1220.f,2.f,0.5f,5);
    float ry=stb_perlin_fbm_noise3(nx*3.4f+qx*1.6f+3.7f,ny*3.4f+qy*1.6f+2.1f,seed_f+1230.f,2.f,0.5f,5);
    float wx=qx*0.15f+rx*0.085f;
    float wy=qy*0.15f+ry*0.085f;
    float fx=(nx+wx)*SCPS_W, fy=(ny+wy)*SCPS_H;

    float best=0.f;
    for (int i=0;i<g_ncont;i++) {
        ContShape *cs=&g_cshape[i];
        /* Union LISSE des lobes d'un MÊME continent (corps + péninsules) :
         * les bras se soudent au corps sans pointe. */
        float cval=0.f;
        for (int l=0;l<cs->n;l++) {
            ContLobe *cl=&cs->lobe[l];
            float rx=(fx-cl->cx)*cl->cosA+(fy-cl->cy)*cl->sinA;
            float ry=-(fx-cl->cx)*cl->sinA+(fy-cl->cy)*cl->cosA;
            float d=sqrtf((rx/cl->ax)*(rx/cl->ax)+(ry/cl->ay)*(ry/cl->ay));
            float lobe=1.f-clampf(d,0.f,1.f);
            lobe=lobe*lobe*(3.f-2.f*lobe)*cl->strength;
            cval = (l==0) ? lobe : smaxf(cval,lobe,0.28f);
        }
        if (cval>best) best=cval;   /* continents distincts : union dure */
    }
    for (int i=0;i<g_nislet;i++) {
        Islet *il=&g_islet[i];
        float rx=(fx-il->cx)*il->cosA+(fy-il->cy)*il->sinA;
        float ry=-(fx-il->cx)*il->sinA+(fy-il->cy)*il->cosA;
        float d=sqrtf((rx/il->ax)*(rx/il->ax)+(ry/il->ay)*(ry/il->ay));
        float lobe=1.f-clampf(d,0.f,1.f);
        lobe=lobe*lobe*(3.f-2.f*lobe)*0.55f;
        if (lobe>best) best=lobe;
    }
    float edge=clampf(ny*6.f,0,1)*clampf((1.f-ny)*6.f,0,1)
              *clampf(nx*8.f,0,1)*clampf((1.f-nx)*8.f,0,1);
    return best*edge;
}

/* ========================================================================
 * FEATURES OCÉANIQUES — fosses abyssales et hauts-fonds (récifs)
 * ====================================================================== */
static void step_ocean_features(float *height, float seed_f) {
    /* Fosses : 2-5 arcs linéaires dans l'océan profond */
    int ntr=2+(int)(rng_f()*4.f);
    for (int t=0;t<ntr;t++) {
        float cx=rng_f()*SCPS_W, cy=(0.05f+0.90f*rng_f())*SCPS_H;
        float aLen=(0.07f+rng_f()*0.12f)*SCPS_W;
        float aWid=(0.008f+rng_f()*0.008f)*SCPS_H;
        float angle=rng_f()*3.14159f;
        float cosA=cosf(angle), sinA=sinf(angle);
        for (int y=0;y<SCPS_H;y++) for (int x=0;x<SCPS_W;x++) {
            int i=scps_idx(x,y);
            if (height[i]>=SEA_LEVEL-0.08f) continue;
            float dx=(float)x-cx, dy=(float)y-cy;
            float along=dx*cosA+dy*sinA;
            float perp =-dx*sinA+dy*cosA;
            float da=along/aLen, dp=perp/aWid;
            if (da<-1.f||da>1.f) continue;
            float d=sqrtf(dp*dp+da*da*0.1f);
            if (d>1.f) continue;
            height[i]-=(1.f-d*d)*0.10f;
        }
    }
    /* Hauts-fonds / récifs : bruit haute fréquence sur la marge continentale */
    for (int y=0;y<SCPS_H;y++) for (int x=0;x<SCPS_W;x++) {
        int i=scps_idx(x,y);
        float h=height[i];
        if (h>=SEA_LEVEL||h<SEA_LEVEL-0.12f) continue;
        float nx=(float)x/SCPS_W, ny=(float)y/SCPS_H;
        float reef=stb_perlin_fbm_noise3(nx*28.f,ny*28.f,seed_f+3100.f,2.f,0.5f,3);
        if (reef>0.20f) height[i]+=clampf((reef-0.20f)*0.06f,0.f,0.05f);
    }
}

/* ========================================================================
 * VOLCANS — cônes isolés avec caldeira (injectés dans step_architecture)
 * ====================================================================== */
#define MAX_VOLC 8
typedef struct {
    float cx,cy,r,peak;
    float fdx,fdy;  /* direction d'écoulement dominant (gradient aval normalisé) */
} Volcano;
static Volcano g_volc[MAX_VOLC];
static int     g_nvolc=0;

/* Place les volcans le long des arcs de subduction (plaque océanique plonge
 * sous plaque continentale). L'offset d'arc (14-26 cellules vers l'intérieur
 * de la plaque continentale) reproduit la géographie réelle : Andes, Japon,
 * Cascades. Aucun volcan aléatoire — tout découle de la géologie. */
static void volcanoes_init(const float *height, float seed_f) {
    g_nvolc = 0;
    int want = 2 + (int)(rng_f() * 3.f);   /* 2-4 volcans */

    /* 1. Collecte des points de frontière de subduction */
    typedef struct { short x, y; int cont_plate; } SubPt;
    SubPt *sub = (SubPt*)malloc(SCPS_N * sizeof(SubPt));
    if (!sub) return;
    int nsub = 0;

    for (int y=1; y<SCPS_H-1; y++) for (int x=1; x<SCPS_W-1; x++) {
        int pa, pb;
        float bs = plate_boundary(x, y, &pa, &pb, seed_f);
        if (bs < 0.12f) continue;
        bool oa = g_plates[pa].oceanic, ob = g_plates[pb].oceanic;
        if (oa == ob) continue;                 /* collision crust/crust ou dorsale */
        /* Convergence : les plaques s'approchent */
        float dot = g_plates[pa].dx*g_plates[pb].dx + g_plates[pa].dy*g_plates[pb].dy;
        if (dot >= 0.f) continue;
        sub[nsub].x = (short)x;
        sub[nsub].y = (short)y;
        sub[nsub].cont_plate = oa ? pb : pa;   /* plaque continentale (dessus) */
        nsub++;
    }

    /* 2. Tire want positions décalées vers l'intérieur de la plaque cont. */
    for (int tries = 0; tries < want*60 && g_nvolc < want && nsub > 0; tries++) {
        int idx = (int)(rng_f() * nsub) % nsub;
        SubPt sp = sub[idx];
        float dx = g_plates[sp.cont_plate].cx - sp.x;
        float dy = g_plates[sp.cont_plate].cy - sp.y;
        float d  = sqrtf(dx*dx+dy*dy); if (d < 1.f) d = 1.f;
        float arc    = 14.f + rng_f() * 12.f;   /* profondeur d'arc volcanique */
        float jitter = (rng_f() - 0.5f) * 10.f; /* jitter le long de la chaîne */
        float tx = -dy/d, ty = dx/d;             /* tangente à la frontière */
        int vx = (int)(sp.x + dx/d*arc + tx*jitter);
        int vy = (int)(sp.y + dy/d*arc + ty*jitter);
        if (vx < 2 || vx >= SCPS_W-2 || vy < 2 || vy >= SCPS_H-2) continue;
        if (height[scps_idx(vx,vy)] < MOUNTAIN_H) continue;   /* biome montagneux seulement */
        bool ok = true;
        for (int v = 0; v < g_nvolc && ok; v++) {
            float ddx = vx - g_volc[v].cx, ddy = vy - g_volc[v].cy;
            if (ddx*ddx + ddy*ddy < 30.f*30.f) ok = false;
        }
        if (!ok) continue;
        /* Direction d'écoulement : gradient de hauteur sur un rayon large */
        float ghx = height[scps_idx(clampi(vx+4,0,SCPS_W-1),vy)]
                  - height[scps_idx(clampi(vx-4,0,SCPS_W-1),vy)];
        float ghy = height[scps_idx(vx,clampi(vy+4,0,SCPS_H-1))]
                  - height[scps_idx(vx,clampi(vy-4,0,SCPS_H-1))];
        float glen = sqrtf(ghx*ghx+ghy*ghy);
        if (glen < 1e-4f) { ghx=0.f; ghy=1.f; glen=1.f; }
        g_volc[g_nvolc].cx   = (float)vx;
        g_volc[g_nvolc].cy   = (float)vy;
        g_volc[g_nvolc].r    = 9.f + rng_f() * 14.f;
        g_volc[g_nvolc].peak = 0.08f + rng_f() * 0.12f;
        g_volc[g_nvolc].fdx  = -ghx/glen;   /* pointe vers le bas */
        g_volc[g_nvolc].fdy  = -ghy/glen;
        g_nvolc++;
    }
    free(sub);
}

static void volcanoes_inject(float *height) {
    for (int v=0;v<g_nvolc;v++) {
        float cx=g_volc[v].cx, cy=g_volc[v].cy;
        float r=g_volc[v].r, pk=g_volc[v].peak;
        float calR=r*0.22f;
        int x0=(int)(cx-r*2.f), x1=(int)(cx+r*2.f);
        int y0=(int)(cy-r*2.f), y1=(int)(cy+r*2.f);
        for (int y=y0;y<=y1;y++) for (int x=x0;x<=x1;x++) {
            if (x<0||x>=SCPS_W||y<0||y>=SCPS_H) continue;
            float dx=(float)x-cx, dy=(float)y-cy;
            float d=sqrtf(dx*dx+dy*dy);
            if (d>r*1.8f) continue;
            float cone=expf(-d*d/(r*r*0.45f))*pk;
            float caldera=expf(-d*d/(calR*calR*0.5f))*pk*0.65f;
            height[scps_idx(x,y)]+=cone-caldera;
        }
    }
}

/* Sol volcanique enrichi : uniquement côté aval (cendres + coulées de lave).
 * Reproduit l'effet Naples : un seul versant du volcan est fertile. Le
 * facteur directionnel est un cosinus remapé [0..1] → annule la contribution
 * sur le versant au vent et la maximise côté sous-le-vent. */
static float volcanic_soil(int x, int y) {
    float best=0.f;
    for (int v=0;v<g_nvolc;v++) {
        float dx=(float)x-g_volc[v].cx, dy=(float)y-g_volc[v].cy;
        float d=sqrtf(dx*dx+dy*dy);
        float r=g_volc[v].r;
        if (d>r*2.6f || d<r*0.30f) continue;
        float radial = 1.f-clampf((d-r*0.30f)/(r*2.3f),0.f,1.f);
        /* Projection sur la direction d'écoulement : > 0 = côté aval */
        float dot = (d>0.f) ? (dx*g_volc[v].fdx+dy*g_volc[v].fdy)/d : 0.f;
        float dir  = clampf(dot*0.7f+0.3f, 0.f, 1.f); /* 0 au vent → 1 sous le vent */
        float s = radial * dir;
        if (s>best) best=s;
    }
    return best;
}

/* Marque la caldeira/cône nu en biome volcanique.
 * Le rayon effectif est modulé par un FBM → contour irrégulier (cratère
 * ébréché, coulées de roche figée) plutôt qu'un disque parfait. */
static void volcanoes_mark(World *w, const float *height) {
    for (int v=0;v<g_nvolc;v++) {
        float cx=g_volc[v].cx, cy=g_volc[v].cy, r=g_volc[v].r;
        float bareR=r*0.60f;
        int x0=(int)(cx-bareR*1.6f), x1=(int)(cx+bareR*1.6f);
        int y0=(int)(cy-bareR*1.6f), y1=(int)(cy+bareR*1.6f);
        for (int y=y0;y<=y1;y++) for (int x=x0;x<=x1;x++) {
            if (x<0||x>=SCPS_W||y<0||y>=SCPS_H) continue;
            if (height[scps_idx(x,y)]<SEA_LEVEL) continue;
            float dx=(float)x-cx, dy=(float)y-cy;
            float d=sqrtf(dx*dx+dy*dy);
            /* Rayon local modulé par un bruit pour un contour organique */
            float nx=(float)x/SCPS_W, ny=(float)y/SCPS_H;
            float warp=stb_perlin_fbm_noise3(nx*18.f,ny*18.f,(float)v*7.3f+13.f,
                                             2.f,0.5f,3)*0.30f + 1.f;
            if (d <= bareR*warp)
                w->cell[scps_idx(x,y)].biome=BIO_VOLCANO;
        }
    }
}

static void step_geology(float *height, float seed_f, const WorldParams *P) {
    plates_init();
    continents_init(P->n_continents, seed_f);

    /* land_bias : décale la mer (0.5 neutre). mountains : amplitude. */
    float land_bias = (P->land_amount-0.5f)*0.5f;
    float mtn_amp   = 0.6f + P->mountains*0.9f;

    for (int y=0;y<SCPS_H;y++) for (int x=0;x<SCPS_W;x++) {
        float nx=(float)x/SCPS_W, ny=(float)y/SCPS_H;
        float lat=fabsf(ny-0.5f)*2.f;
        /* Domain warp IQ 1er ordre sur le bruit de détail : casse l'axe-alignment
         * du Perlin brut → collines, vallées et plateaux organiques, pas rectangulaires. */
        float dqx=stb_perlin_fbm_noise3(nx*2.1f,     ny*2.1f,     seed_f+10.f,2.f,0.5f,5);
        float dqy=stb_perlin_fbm_noise3(nx*2.1f+3.7f,ny*2.1f+1.9f,seed_f+11.f,2.f,0.5f,5);
        float detail = stb_perlin_fbm_noise3(nx*4.5f+dqx*0.38f,
                                             ny*3.6f+dqy*0.38f,
                                             seed_f,2.f,0.5f,7);
        float mask    = continental_mask(x,y,seed_f);
        /* Mer profonde hors masque ; terre détaillée dans le masque.
         * Plateau interne (smoothstep du masque) → continents pleins, peu de
         * mer intérieure, tout en laissant l'océan entre les masses. */
        float plat = clampf((mask-0.18f)/0.34f,0.f,1.f);
        plat = plat*plat*(3.f-2.f*plat);
        /* sqrt(mask) au lieu de mask : le détail de relief ne s'annule plus
         * brutalement à la côte → littoraux moins convexes, plus découpés. */
        float h = (plat*0.42f - 0.06f) + detail*0.42f*sqrtf(clampf(mask,0.f,1.f)) + land_bias;
        height[scps_idx(x,y)] = h - 0.16f*lat*lat;
    }
    /* Frontières de plaques → chaînes de montagnes DIRECTIONNELLES.
     *
     * La clé : projeter les coordonnées dans le REPÈRE DE LA FRONTIÈRE avant
     * d'appeler le ridge noise. La tangente tx/ty = direction ALONG la chaîne ;
     * le ridge noise est anisotrope (fréquence forte ⊥ chaîne, faible ∥) →
     * les crêtes courent le long de la suture, pas dans tous les sens.
     * Les contreforts (R2) sont warpés par R1 → branchent sur la dorsale.
     */
    for (int y=0;y<SCPS_H;y++) for (int x=0;x<SCPS_W;x++) {
        float mask=continental_mask(x,y,seed_f);
        if (mask<0.30f) continue;   /* pas de montagnes près des côtes */
        int pa,pb;
        float bs=plate_boundary(x,y,&pa,&pb,seed_f);
        if (bs<0.04f) continue;
        float dot=g_plates[pa].dx*g_plates[pb].dx+g_plates[pa].dy*g_plates[pb].dy;
        float conv=(1.f-dot)*0.5f;
        float bump=0.f;
        if (!g_plates[pa].oceanic && !g_plates[pb].oceanic) bump=bs*conv*1.10f;
        else if (g_plates[pa].oceanic != g_plates[pb].oceanic) bump=bs*conv*0.65f;
        if (bump<=0.f) continue;

        /* Tangente à la frontière = perpendiculaire au vecteur inter-centres */
        float pdx=g_plates[pb].cx-g_plates[pa].cx;
        float pdy=g_plates[pb].cy-g_plates[pa].cy;
        float plen=sqrtf(pdx*pdx+pdy*pdy); if(plen<1.f)plen=1.f;
        float tx=-pdy/plen, ty=pdx/plen;       /* le long de la chaîne */
        float lx=(float)x*tx+(float)y*ty;      /* coord. ∥ chaîne */
        float ly=(float)x*(-ty)+(float)y*tx;   /* coord. ⊥ chaîne */

        /* R1 : dorsale primaire — fréquence faible ∥ (longues crêtes),
         *      fréquence forte ⊥ (flancs raides) */
        float r1=stb_perlin_ridge_noise3(lx*0.022f, ly*0.055f,
                                         seed_f+50.f,2.f,0.5f,1.f,6);
        /* R2 : contreforts secondaires warpés par R1 → perpendiculaires à la
         *      dorsale, insérés entre les cols */
        float r2=stb_perlin_ridge_noise3(lx*0.045f+r1*1.8f, ly*0.028f+r1*1.8f,
                                         seed_f+55.f,2.f,0.5f,1.f,5);
        height[scps_idx(x,y)] += bump*(r1*0.68f+r2*0.32f)*mtn_amp*(mask*mask);
    }
    normalize_f(height,SCPS_N);
    step_ocean_features(height, seed_f);
    /* Volcans tectoniques : placés le long des zones de subduction */
    volcanoes_init(height, seed_f);
}

/* ========================================================================
 * COUCHE 2 — ARCHITECTURE
 *
 * Réseau de crêtes hiérarchique à 3 niveaux :
 *   R1 — chaîne principale (grande longueur d'onde) : dorsale primaire
 *   R2 — contreforts secondaires warpés par R1 : les contreforts SUIVENT
 *        la chaîne principale et s'y raccordent (pas d'objets isolés)
 *   R3 — éperons tertiaires warpés par R2 : ramifications latérales
 *        fines, val·lées et cols entre les éperons
 *
 * La warp-chain est la clé : r2 est évalué aux coordonnées déformées par
 * r1, donc ses crêtes sont orthogonales/parallèles à r1. Même logique pour
 * r3 vis-à-vis de r2. On obtient un réseau arborescent, pas des boudins.
 * ====================================================================== */
static void step_architecture(float *height, float seed_f) {
    for (int y=0;y<SCPS_H;y++) for (int x=0;x<SCPS_W;x++) {
        float nx=(float)x/SCPS_W, ny=(float)y/SCPS_H;
        float h=height[scps_idx(x,y)];
        float mtn = clampf((h-0.46f)/0.38f,0.f,1.f);
        float low = clampf((0.60f-h)/0.38f,0.f,1.f);

        /* R1 — structure topologique basse fréquence, sert uniquement de warp
         * pour R2/R3 : PAS de contribution directe à la hauteur (évite les
         * chaînes indépendantes non tectoniques). */
        float r1=stb_perlin_ridge_noise3(nx*5.5f, ny*4.0f, seed_f+200.f,2.f,0.5f,1.f,6);

        /* R2 — détail de flanc warped par R1 : texture sur relief existant */
        float r2=stb_perlin_ridge_noise3(nx*11.f+r1*2.2f, ny*8.5f+r1*2.2f,
                                         seed_f+210.f,2.f,0.5f,1.f,5);

        /* R3 — micro-éperons warpés par R2 */
        float r3=stb_perlin_ridge_noise3(nx*22.f+r2*1.6f, ny*17.f+r2*1.6f,
                                         seed_f+220.f,2.f,0.5f,1.f,4);

        /* Vallées / bassins versants — warpées par r1 ET une composante transverse */
        float v=stb_perlin_fbm_noise3(nx*9.f+r1*1.1f+r2*0.4f,
                                      ny*7.f+r1*1.1f+r2*0.4f,
                                      seed_f+300.f,2.f,0.5f,5);

        /* Texture pure : R2+R3 ajoutent du détail aux montagnes existantes,
         * R1 n'est pas additionné (il ne crée pas de montagnes seul). */
        float mtn_add = r2*0.11f + r3*0.05f;
        float low_add = v*0.06f;
        height[scps_idx(x,y)] += mtn_add*mtn + low_add*low;
    }
    volcanoes_inject(height);
    normalize_f(height,SCPS_N);
}

/* ========================================================================
 * COUCHE 3 — ÉROSION
 * D8 flow + accumulation → rivières + creusement
 * ====================================================================== */
static void step_erosion(float *height, Cell *cells, float erosion) {
    int8_t *fdir  = (int8_t*)malloc(SCPS_N*sizeof(int8_t));
    float  *accum = (float *)malloc(SCPS_N*sizeof(float));
    if (!fdir||!accum) { free(fdir);free(accum);return; }

    /* D8 : direction vers le voisin le plus bas */
    for (int y=0;y<SCPS_H;y++) for (int x=0;x<SCPS_W;x++) {
        float h=height[scps_idx(x,y)];
        int best=-1; float drop=0.f;
        for (int d=0;d<8;d++) {
            int nx2=x+DDX[d],ny2=y+DDY[d];
            if (nx2<0||nx2>=SCPS_W||ny2<0||ny2>=SCPS_H) continue;
            float dh=(h-height[scps_idx(nx2,ny2)])/DDIST[d];
            if (dh>drop){drop=dh;best=d;}
        }
        fdir[scps_idx(x,y)]=(int8_t)best;
    }

    /* Accumulation de flux EN ORDRE TOPOLOGIQUE (haut → bas).
     * Chaque cellule verse son aire de drainage à son exutoire exactement
     * une fois → accum[i] = nombre de cellules en amont (aire de bassin).
     * (L'ancienne version sommait sur 56 passes, faisant exploser accum le
     *  long des longues chaînes ; le seuil de normalisation annulait alors
     *  presque tous les débits.) */
    int *order=(int*)malloc(SCPS_N*sizeof(int));
    if (!order){ free(fdir); free(accum); return; }
    for (int i=0;i<SCPS_N;i++){ order[i]=i; accum[i]=1.f; }

    /* Tri des indices par hauteur décroissante (tri par dénombrement sur
     * 1024 niveaux : O(N), suffisant pour ordonner amont→aval). */
    {
        const int NB=1024;
        int *cnt=(int*)calloc(NB+1,sizeof(int));
        int *tmp=(int*)malloc(SCPS_N*sizeof(int));
        if (cnt && tmp) {
            for (int i=0;i<SCPS_N;i++){
                int b=(int)(clampf(height[i],0.f,1.f)*(NB-1));
                cnt[NB-b]++;            /* NB-b : hauteur décroissante */
            }
            for (int b=1;b<=NB;b++) cnt[b]+=cnt[b-1];
            for (int i=0;i<SCPS_N;i++){
                int b=(int)(clampf(height[i],0.f,1.f)*(NB-1));
                tmp[cnt[NB-1-b]++]=i;
            }
            memcpy(order,tmp,SCPS_N*sizeof(int));
        }
        free(cnt); free(tmp);
    }

    /* Verse l'aire de drainage vers l'aval, une passe en ordre topologique */
    for (int k=0;k<SCPS_N;k++) {
        int i=order[k];
        int d=fdir[i]; if(d<0)continue;
        int x=i%SCPS_W, y=i/SCPS_W;
        int nx2=x+DDX[d],ny2=y+DDY[d];
        if (nx2<0||nx2>=SCPS_W||ny2<0||ny2>=SCPS_H)continue;
        accum[scps_idx(nx2,ny2)]+=accum[i];
    }
    free(order);

    float max_a=1.f;
    for (int i=0;i<SCPS_N;i++) if(accum[i]>max_a)max_a=accum[i];
    float lmax=logf(1.f+max_a);

    float carve = 0.03f + erosion*0.07f;    /* intensité de creusement (param) */
    for (int i=0;i<SCPS_N;i++) {
        cells[i].flow_dir=fdir[i];          /* conservé pour le tracé aval */
        /* Échelle log : un fleuve de 5000 cellules amont reste lisible face
         * à un ruisseau de 5. */
        float rs=logf(1.f+accum[i])/lmax;
        cells[i].river=(uint8_t)(clampf(rs,0.f,1.f)*255.f);
        if (rs>0.45f && height[i]>SEA_LEVEL) height[i]-=(rs-0.45f)*carve; /* creuse le lit */
    }
    normalize_f(height,SCPS_N);
    free(fdir); free(accum);
}

/* ========================================================================
 * CÔTES FRACTALES — détail haute fréquence sur la seule bande littorale
 *
 * Appliquée APRÈS l'érosion (sinon l'érosion thermique relisse le détail).
 * On ne touche QUE les cellules proches du niveau de la mer : on y injecte
 * un bruit fractal multi-octave, domain-warpé, d'amplitude suffisante pour
 * faire franchir le rivage localement → criques, caps, détroits et petites
 * îles satellites. Le large et l'intérieur ne bougent pas (fenêtre nulle). */
static void step_coastline(float *height, float seed_f) {
    const float BAND=0.095f;    /* bande large : saisit plateau continental + rivage */
    float *out=(float*)malloc(SCPS_N*sizeof(float));
    if (!out) return;
    memcpy(out,height,SCPS_N*sizeof(float));
    for (int y=0;y<SCPS_H;y++) for (int x=0;x<SCPS_W;x++) {
        int i=scps_idx(x,y);
        float d=height[i]-SEA_LEVEL;
        if (d<-BAND || d>BAND) continue;
        float nx=(float)x/SCPS_W, ny=(float)y/SCPS_H;

        /* Warp passe 1 — grande échelle (décale baies entières) */
        float wx1=stb_perlin_fbm_noise3(nx*5.f,   ny*5.f,   seed_f+2400.f,2.f,0.5f,5)*0.048f;
        float wy1=stb_perlin_fbm_noise3(nx*5.f+2.f,ny*5.f+1.f,seed_f+2405.f,2.f,0.5f,5)*0.048f;
        float px=nx+wx1, py=ny+wy1;

        /* Warp passe 2 — petite échelle (rugosité de falaise) */
        float wx2=stb_perlin_fbm_noise3(px*14.f,   py*14.f,   seed_f+2410.f,2.f,0.5f,4)*0.020f;
        float wy2=stb_perlin_fbm_noise3(px*14.f+3.f,py*14.f+1.f,seed_f+2415.f,2.f,0.5f,4)*0.020f;
        px+=wx2; py+=wy2;

        /* 5 octaves fractales — chaque octave ×2 en fréquence, ×0.5 en amplitude
         * (fBm classique) + octave ultrafine pour rugosité au zoom ×10 */
        float n = stb_perlin_fbm_noise3(px* 7.f,py* 7.f,seed_f+2420.f,2.f,0.5f,4)*0.48f
                + stb_perlin_fbm_noise3(px*15.f,py*15.f,seed_f+2430.f,2.f,0.5f,4)*0.26f
                + stb_perlin_fbm_noise3(px*30.f,py*30.f,seed_f+2440.f,2.f,0.5f,3)*0.14f
                + stb_perlin_fbm_noise3(px*60.f,py*60.f,seed_f+2450.f,2.f,0.5f,3)*0.08f
                + stb_perlin_fbm_noise3(px*120.f,py*120.f,seed_f+2460.f,2.f,0.5f,2)*0.04f;

        float wnd=1.f-(d<0?-d:d)/BAND; wnd*=wnd;
        out[i]=height[i]+n*0.092f*wnd;
    }
    memcpy(height,out,SCPS_N*sizeof(float));
    free(out);
}

/* ========================================================================
 * CARTE FANTÔME — relief secondaire à niveau de mer très bas
 *
 * On génère une SECONDE heightmap, indépendante de la première (autres
 * graines de bruit), domain-warpée. On la lit avec un « niveau de mer »
 * fantôme TRÈS BAS : presque tout son relief est « émergé », mais on n'en
 * applique la part émergée QUE dans l'océan de la carte principale. Ses
 * crêtes percent la surface → archipels, chapelets d'îles, hauts-fonds et
 * récifs dispersés qui peuplent les mers vides. La terre principale n'est
 * pas touchée (on ne modifie que les cellules sous le niveau de mer). */
static void step_ghost_layer(float *height, float seed_f) {
    const float GSEA = 0.30f;   /* niveau de mer fantôme bas → archipels généreux */
    const float AMP  = 0.34f;   /* amplitude d'émergence */
    for (int y=0;y<SCPS_H;y++) for (int x=0;x<SCPS_W;x++) {
        int i=scps_idx(x,y);
        if (height[i]>=SEA_LEVEL) continue;            /* terre : intouchée */

        float nx=(float)x/SCPS_W, ny=(float)y/SCPS_H;
        /* Domain warp propre à la carte fantôme (décalages de graine distincts) */
        float wx=stb_perlin_fbm_noise3(nx*2.1f,ny*2.1f,seed_f+5200.f,2.f,0.5f,5)*0.20f;
        float wy=stb_perlin_fbm_noise3(nx*2.1f+4.f,ny*2.1f+2.f,seed_f+5210.f,2.f,0.5f,5)*0.20f;
        float px=nx+wx, py=ny+wy;
        /* fBm multi-octave → [0..1] approx. */
        float g=0.5f+0.5f*stb_perlin_fbm_noise3(px*2.8f,py*2.8f,seed_f+5220.f,2.f,0.5f,6);
        g+=0.18f*stb_perlin_fbm_noise3(px*7.f,py*7.f,seed_f+5230.f,2.f,0.5f,4); /* détail fin */

        /* Fondu de bord de carte (pas d'îles collées au cadre) */
        float edge=clampf(ny*6.f,0,1)*clampf((1.f-ny)*6.f,0,1)
                  *clampf(nx*8.f,0,1)*clampf((1.f-nx)*8.f,0,1);
        g*=edge;

        float emerged=g-GSEA;
        if (emerged<=0.f) continue;
        /* Atténuation en eau profonde : les îles fantômes naissent surtout sur
         * les plateaux (proche surface) ; au-dessus des fosses, seuls les plus
         * hauts reliefs percent → cohérent avec une dorsale océanique. */
        float shelf=clampf((height[i]-(SEA_LEVEL-0.16f))/0.16f,0.25f,1.f);
        height[i]+=emerged*AMP*shelf;
    }
}

/* ========================================================================
 * LISSAGE OCÉAN — plusieurs passes de blur gaussien 3×3 sur les cellules
 * sous-marines uniquement.
 *
 * Les couches fantômes (positive et négative) injectent du relief haute
 * fréquence partout. Sous l'eau, ce relief percolait visuellement comme
 * un « texture parasitaire ». On le lisse ici AVANT le calcul du climat
 * (les îles fantômes restent, mais leur voisinage sous-marin est propre).
 * La terre n'est jamais modifiée.
 * ====================================================================== */
static void step_smooth_ocean(float *height, int passes) {
    float *tmp=(float*)malloc(SCPS_N*sizeof(float));
    if (!tmp) return;
    for (int p=0;p<passes;p++) {
        memcpy(tmp,height,SCPS_N*sizeof(float));
        for (int y=1;y<SCPS_H-1;y++) for (int x=1;x<SCPS_W-1;x++) {
            int i=scps_idx(x,y);
            if (height[i]>=SEA_LEVEL) continue;  /* terre : intouchée */
            /* Gaussien 3×3 : centre ×4, cardinal ×2, diagonal ×1 */
            float s= height[i]*4.f
                   + height[scps_idx(x+1,y)]  *2.f
                   + height[scps_idx(x-1,y)]  *2.f
                   + height[scps_idx(x,y+1)]  *2.f
                   + height[scps_idx(x,y-1)]  *2.f
                   + height[scps_idx(x+1,y+1)]*1.f
                   + height[scps_idx(x-1,y+1)]*1.f
                   + height[scps_idx(x+1,y-1)]*1.f
                   + height[scps_idx(x-1,y-1)]*1.f;
            tmp[i]=s/16.f;
        }
        memcpy(height,tmp,SCPS_N*sizeof(float));
    }
    free(tmp);
}

/* ========================================================================
 * CARTE FANTÔME NÉGATIVE — creuse la terre (gouffres & lacs)
 *
 * Symétrique de step_ghost_layer mais à l'envers : une 3e heightmap
 * indépendante, lue avec un seuil haut (relief fantôme rare), appliquée
 * UNIQUEMENT sur la terre, en SOUSTRACTION. Là où ses crêtes coïncident
 * avec la terre, le sol s'effondre → gorges, gouffres, et cuvettes qui,
 * passant sous le niveau de mer, deviennent lacs/mers intérieures.
 * Le creusement est accentué en altitude (montagnes → gouffres profonds). */
static void step_ghost_negative(float *height, float seed_f) {
    for (int y=0;y<SCPS_H;y++) for (int x=0;x<SCPS_W;x++) {
        int i=scps_idx(x,y);
        if (height[i]<SEA_LEVEL) continue;             /* mer : intouchée */

        float nx=(float)x/SCPS_W, ny=(float)y/SCPS_H;
        /* Heightmap fantôme #2 (graines de bruit encore distinctes) */
        float wx=stb_perlin_fbm_noise3(nx*2.3f,ny*2.3f,seed_f+5400.f,2.f,0.5f,5)*0.18f;
        float wy=stb_perlin_fbm_noise3(nx*2.3f+4.f,ny*2.3f+2.f,seed_f+5410.f,2.f,0.5f,5)*0.18f;
        float px=nx+wx, py=ny+wy;

        float relief=clampf((height[i]-SEA_LEVEL)/(1.f-SEA_LEVEL),0.f,1.f);

        /* (A) BASSINS LARGES — basse fréquence, creusés fort et SANS égard au
         *     relief : de vastes étendues plongent sous le niveau de mer →
         *     mers intérieures type Méditerranée/Caspienne (et non de simples
         *     gorges). Le warp leur donne un contour organique presque fermé. */
        float gB=0.5f+0.5f*stb_perlin_fbm_noise3(px*1.55f,py*1.55f,seed_f+5420.f,2.f,0.5f,6);
        float eb=gB-0.50f;
        if (eb>0.f) {
            /* creusement large et profond ; un léger surcreusement au cœur du
             * bassin (eb² ) façonne une cuvette franche plutôt qu'un plat. */
            height[i]-=eb*1.05f + eb*eb*0.9f;
        }

        /* (B) GORGES & GOUFFRES — moyenne fréquence, accentués en altitude
         *     (montagne → gorge spectaculaire ; plaine → cuvette/lac). */
        float gD=0.5f+0.5f*stb_perlin_fbm_noise3(px*3.6f,py*3.6f,seed_f+5430.f,2.f,0.5f,6);
        gD+=0.15f*stb_perlin_fbm_noise3(px*8.5f,py*8.5f,seed_f+5440.f,2.f,0.5f,4);
        float ed=gD-0.56f;
        if (ed>0.f)
            height[i]-=ed*0.46f*(0.45f+0.55f*relief);
    }
}

/* ========================================================================
 * CONTINENTALITÉ — distance à l'océan (chamfer, deux passes)
 * Sortie [0..1] : 0 = côte/mer, 1 = intérieur profond.
 * Pilote l'assèchement et l'amplitude thermique loin des côtes.
 * ====================================================================== */
#define OCEAN_DIST_SCALE 70.f   /* cellules pour saturer à 1.0 */

static void compute_ocean_distance(const float *height, float *odist) {
    const float BIG=1e9f;
    for (int i=0;i<SCPS_N;i++)
        odist[i] = (height[i]<SEA_LEVEL) ? 0.f : BIG;

    /* Passe avant (haut-gauche → bas-droite) */
    for (int y=0;y<SCPS_H;y++) for (int x=0;x<SCPS_W;x++) {
        int i=scps_idx(x,y);
        if (odist[i]==0.f) continue;
        float m=odist[i];
        if (x>0)            { float v=odist[i-1]+1.f;      if(v<m)m=v; }
        if (y>0)            { float v=odist[i-SCPS_W]+1.f; if(v<m)m=v; }
        if (x>0&&y>0)       { float v=odist[i-SCPS_W-1]+1.414f; if(v<m)m=v; }
        if (x<SCPS_W-1&&y>0){ float v=odist[i-SCPS_W+1]+1.414f; if(v<m)m=v; }
        odist[i]=m;
    }
    /* Passe arrière (bas-droite → haut-gauche) */
    for (int y=SCPS_H-1;y>=0;y--) for (int x=SCPS_W-1;x>=0;x--) {
        int i=scps_idx(x,y);
        if (odist[i]==0.f) continue;
        float m=odist[i];
        if (x<SCPS_W-1)             { float v=odist[i+1]+1.f;      if(v<m)m=v; }
        if (y<SCPS_H-1)             { float v=odist[i+SCPS_W]+1.f; if(v<m)m=v; }
        if (x<SCPS_W-1&&y<SCPS_H-1) { float v=odist[i+SCPS_W+1]+1.414f; if(v<m)m=v; }
        if (x>0&&y<SCPS_H-1)        { float v=odist[i+SCPS_W-1]+1.414f; if(v<m)m=v; }
        odist[i]=m;
    }
    for (int i=0;i<SCPS_N;i++)
        odist[i]=clampf(odist[i]/OCEAN_DIST_SCALE,0.f,1.f);
}

/* ========================================================================
 * CLIMAT — simulation atmosphérique causale
 *
 * 1. Température : latitude − altitude + continentalité (intérieurs chauds
 *    aux basses latitudes) + bruit multi-échelle.
 * 2. Humidité : ADVECTION par le vent. Les cellules atmosphériques portent
 *    la vapeur depuis l'océan ; l'air qui monte sur un relief précipite
 *    (pluie orographique au vent) et redescend asséché (ombre pluvio. =
 *    désert sous le vent). La vapeur s'épuise vers l'intérieur des terres.
 * 3. Corridors ripariens : un fleuve verdit sa vallée même en plein désert.
 * ====================================================================== */

/* Vent zonal dominant par bande de latitude (cellules de Hadley/Ferrel) :
 *   tropiques (alizés)    → est→ouest (-x)
 *   moyennes lat (ouest)  → ouest→est (+x)
 *   polaires (easterlies) → est→ouest (-x)
 * La bascule alizés↔westerlies vers 30° engendre la ceinture sèche
 * subtropicale (Sahara, Atacama, outback) — fait physique, pas artefact. */
static int wind_dir_x(float lat) {
    if (lat < 0.33f) return -1;
    if (lat < 0.66f) return +1;
    return -1;
}

static void gen_climate(World *w, float *height, float *moisture,
                        float *temperature, const float *odist, float seed_f,
                        const WorldParams *P) {
    Cell *cells = w->cell;
    float t_bias = (P->temperature-0.5f)*0.6f;   /* slider température */
    float m_bias = (P->humidity   -0.5f)*0.6f;   /* slider humidité   */

    /* ---- 1. Température --------------------------------------------- */
    for (int y=0;y<SCPS_H;y++) for (int x=0;x<SCPS_W;x++) {
        int i=scps_idx(x,y);
        float nx=(float)x/SCPS_W, ny=(float)y/SCPS_H;
        float lat=fabsf(ny-0.5f)*2.f;
        float h=height[i];
        float alt_cold = clampf((h-0.50f)*2.2f,0.f,1.f);
        float t_cont = stb_perlin_fbm_noise3(nx*1.8f,ny*1.5f,seed_f+510.f,2.f,0.5f,4)*0.15f;
        float t_reg  = stb_perlin_fbm_noise3(nx*4.0f,ny*3.5f,seed_f+500.f,2.f,0.5f,4)*0.10f;
        float t_loc  = stb_perlin_fbm_noise3(nx*9.0f,ny*8.0f,seed_f+520.f,2.f,0.5f,3)*0.05f;
        float cont_heat = odist[i]*(1.f-lat)*0.12f;  /* déserts continentaux brûlants */
        float cont_cold = odist[i]*lat*0.20f;          /* gel polaire intérieur (Sibérie) */
        /* Calotte polaire : refroidissement fort >60° lat */
        float polar_cap = clampf((lat-0.60f)/0.25f,0.f,1.f)*0.50f;
        temperature[i]=clampf(1.f-lat-alt_cold+cont_heat-cont_cold-polar_cap
                              +t_cont+t_reg+t_loc+t_bias,0.f,1.f);
    }

    /* ---- 2. Advection d'humidité ----------------------------------- */
    float *rain=(float*)calloc(SCPS_N,sizeof(float));
    if(!rain) return;
    /* ORO_K élevé = ombre pluviométrique forte (montagne bloque les nuages).
     * FOREST_EVAP = transpiration forestière (forêt régénère l'humidité
     * sous-vent — "les forêts font la pluie"). */
    const float HUM_CAP=1.0f, EVAP=0.16f, RAIN_K=0.055f, ORO_K=6.5f;
    const float FOREST_EVAP=0.045f;  /* re-évaporation par la canopée */

    for (int y=0;y<SCPS_H;y++) {
        float lat=fabsf((float)y/SCPS_H-0.5f)*2.f;
        int dir=wind_dir_x(lat);
        float humidity=0.f;
        for (int pass=0;pass<2;pass++)
        for (int k=0;k<SCPS_W;k++) {
            int x = (dir>0) ? k : (SCPS_W-1-k);
            int i=scps_idx(x,y);
            float h=height[i];
            if (h<SEA_LEVEL) {
                float t=temperature[i];
                humidity += EVAP*(0.4f+0.6f*t)*(HUM_CAP-humidity);
                if(humidity>HUM_CAP)humidity=HUM_CAP;
            } else {
                int xu=clampi(x-dir,0,SCPS_W-1);
                float rise=h-height[scps_idx(xu,y)];
                if(rise<0.f)rise=0.f;
                float oro = humidity*rise*ORO_K;   /* ombre pluviométrique */
                float base= humidity*RAIN_K;
                float p=base+oro; if(p>humidity)p=humidity;
                humidity-=p;
                if(pass==1) rain[i]=p;
                /* Ré-évaporation forestière : la forêt rejette de la vapeur
                 * dans l'air sous-vent → humidifie la zone aval.
                 * (On lit le biome du pass précédent ; il sera affiné plus tard.) */
                Biome b=cells[i].biome;
                if (b==BIO_FOREST||b==BIO_WOODS||b==BIO_JUNGLE||b==BIO_MANGROVE) {
                    humidity+=FOREST_EVAP*(rain[i]/0.3f+0.4f)*(HUM_CAP-humidity);
                    if(humidity>HUM_CAP)humidity=HUM_CAP;
                }
            }
        }
    }
    /* Normaliser la pluie sur les terres */
    float rmax=1e-6f;
    for (int i=0;i<SCPS_N;i++)
        if(height[i]>=SEA_LEVEL && rain[i]>rmax) rmax=rain[i];
    for (int i=0;i<SCPS_N;i++) rain[i]=clampf(rain[i]/rmax,0.f,1.f);

    /* ---- 3. Composition de l'humidité ------------------------------ */
    for (int y=0;y<SCPS_H;y++) for (int x=0;x<SCPS_W;x++) {
        int i=scps_idx(x,y);
        float nx=(float)x/SCPS_W, ny=(float)y/SCPS_H;
        float lat=fabsf(ny-0.5f)*2.f;

        cells[i].ocean_dist=odist[i];
        cells[i].rainfall  =rain[i];

        if (height[i]<SEA_LEVEL){ moisture[i]=1.f; continue; }

        float tropical=clampf(1.f-lat*2.6f,0.f,1.f);  /* mousson équatoriale */
        float coastal =1.f-odist[i];                   /* humidité côtière */
        float fbm=stb_perlin_fbm_noise3(nx*4.f,ny*3.f,seed_f+700.f,2.f,0.5f,4)*0.07f;

        /* Ceinture subtropicale sèche (descente de l'air de Hadley ~25-35°)
         * C'est ce qui crée le Sahara, le Gobi et l'Outback.
         * Double cloche : l'une pour chaque hémisphère (lat est toujours |y|). */
        float subtrop = expf(-((lat-0.30f)*(lat-0.30f))/(0.07f*0.07f))*0.52f;

        /* Assèchement continental profond (Gobi / intérieur de l'Asie centrale) */
        float inland_dry = odist[i]*odist[i]*0.40f;

        float m = 0.07f
                + 0.44f*rain[i]      /* advection : pluie orographique + ombre */
                + 0.19f*tropical     /* convection tropicale */
                + 0.18f*coastal      /* proximité de l'océan */
                + fbm + m_bias
                - subtrop            /* ceinture subtropicale → Sahara/Gobi */
                - inland_dry;        /* intérieur profond → steppes/déserts froids */

        /* Corridor riparien : le fleuve verdit sa vallée même dans le désert */
        m += (cells[i].river/255.f)*0.28f;

        moisture[i]=clampf(m,0.f,1.f);
    }
    free(rain);
}

/* ========================================================================
 * BIOMES (Whittaker adapté fantasy)
 * ====================================================================== */
/* Diagramme de Whittaker réglé pour un monde SAUVAGE :
 *   - la forêt domine partout où il y a assez d'eau (seuils bas) ;
 *   - la savane est réduite à une étroite frange chaude semi-aride ;
 *   - AUCUNE terre cultivée naturelle : les flatlands fertiles naissent en
 *     prairie/bois ; la « terre cultivée » est un défrichage civilisationnel
 *     (donc absent tant qu'aucune culture n'a défriché — cf. doc).
 *   - prairies/plaines restreintes aux marges semi-sèches. */
static Biome assign_biome(float h, float m, float t) {
    if (h<SEA_LEVEL-0.14f) return BIO_DEEP_OCEAN;
    if (h<SEA_LEVEL-0.04f) return BIO_OCEAN;
    if (h<SEA_LEVEL)       return BIO_SHALLOW;
    if (h<SEA_LEVEL+0.042f) return BIO_COAST;  /* bande littorale plus visible */
    if (h>=PEAK_H)          return (t<0.16f)?BIO_GLACIER:BIO_PEAK;
    if (h>=MOUNTAIN_H)      return BIO_MOUNTAINS;
    if (h>=MOUNTAIN_H-0.09f)return (t<0.30f)?BIO_HIGHLANDS:BIO_HILLS;

    /* --- Froid (boréal) : taïga dès qu'un peu d'humidité --- */
    if (t<0.17f) {
        if (m>0.28f) return BIO_FOREST;          /* forêt boréale */
        if (m>0.16f) return BIO_WOODS;
        return BIO_GLACIER;
    }
    /* --- Frais (tempéré froid) : forêt très étendue --- */
    if (t<0.33f) {
        if (m>0.40f) return BIO_FOREST;
        if (m>0.22f) return BIO_WOODS;
        if (m>0.12f) return BIO_STEPPE;
        return BIO_STEPPE;
    }
    /* --- Tempéré : forêt domine, prairie en marge sèche --- */
    if (t<0.55f) {
        if (m>0.44f) return BIO_FOREST;
        if (m>0.30f) return BIO_WOODS;
        if (m>0.18f) return BIO_GRASSLAND;
        if (m>0.10f) return BIO_PLAINS;
        return BIO_DRYLANDS;
    }
    /* --- Chaud : jungle/forêt si humide, frange savane étroite --- */
    if (t<0.72f) {
        if (m>0.58f) return (h<SEA_LEVEL+0.07f)?BIO_MARSH:BIO_JUNGLE;
        if (m>0.40f) return BIO_FOREST;          /* forêt tropicale humide */
        if (m>0.26f) return BIO_WOODS;
        if (m>0.16f) return BIO_SAVANNA;         /* frange étroite */
        if (m>0.08f) return BIO_DRYLANDS;
        return (h<SEA_LEVEL+0.06f)?BIO_COASTAL_DESERT:BIO_DESERT;
    }
    /* --- Torride --- */
    if (m>0.60f) return BIO_JUNGLE;
    if (m>0.42f) return BIO_FOREST;
    if (m>0.24f) return BIO_SAVANNA;
    if (m>0.12f) return BIO_DRYLANDS;
    return (h<SEA_LEVEL+0.06f)?BIO_COASTAL_DESERT:BIO_DESERT;
}

/* ========================================================================
 * LACS
 * ====================================================================== */
/* Détecte les dépressions topographiques terrestres et les inonde jusqu'à
 * leur niveau de débordement (brim-fill simple). Les lacs résultants sont
 * naturellement dans les vallées et ont une forme liée au terrain, pas un
 * disque parfait. Pour garder les performances on limite la taille : un lac
 * qui demanderait > MAX_LAKE_CELLS cellules n'est pas retenu. */
#define MAX_LAKE_CELLS 120
static void fill_lakes(float *height, Cell *cells) {
    bool *inlake=(bool*)calloc(SCPS_N,sizeof(bool));
    int  *stk   =(int*)malloc(SCPS_N*sizeof(int));
    if (!inlake||!stk){ free(inlake);free(stk);return; }

    for (int y=2;y<SCPS_H-2;y++) for (int x=2;x<SCPS_W-2;x++) {
        int i=scps_idx(x,y);
        if (height[i]<SEA_LEVEL+0.015f||inlake[i]) continue;
        /* Point de dépression : tous les voisins cardinaux sont plus hauts */
        bool dep=true;
        for (int d=0;d<8;d+=2) {
            if (height[scps_idx(x+DDX[d],y+DDY[d])]<height[i]){dep=false;break;}
        }
        if (!dep) continue;

        /* Flood-fill à hauteur du col le plus bas (brim) */
        float brim=height[i];
        /* Cherche le col : hauteur max des voisins directs */
        for (int d=0;d<8;d+=2) {
            float hn=height[scps_idx(x+DDX[d],y+DDY[d])];
            if (hn>brim) brim=hn;
        }
        /* Réduit le brim d'une marge (évite lacs trop grands) */
        brim=height[i]+(brim-height[i])*0.55f;

        /* Fill itératif */
        int top=0, sz=0;
        stk[top++]=i;
        bool ok=true;
        bool *visited=(bool*)calloc(SCPS_N,sizeof(bool));
        if (!visited){ok=false;}
        int *batch=(int*)malloc(MAX_LAKE_CELLS*sizeof(int));
        if (!batch){free(visited);ok=false;}

        if (ok) {
            visited[i]=true;
            while (top>0&&sz<MAX_LAKE_CELLS) {
                int cur=stk[--top];
                batch[sz++]=cur;
                int cx2=cur%SCPS_W, cy2=cur/SCPS_W;
                for (int d=0;d<8;d+=2) {
                    int nx2=cx2+DDX[d],ny2=cy2+DDY[d];
                    if (nx2<1||nx2>=SCPS_W-1||ny2<1||ny2>=SCPS_H-1) continue;
                    int j=scps_idx(nx2,ny2);
                    if (visited[j]||inlake[j]) continue;
                    if (height[j]<=brim&&height[j]>=SEA_LEVEL+0.008f) {
                        visited[j]=true; stk[top++]=j;
                    }
                }
            }
            if (sz>=2&&sz<=MAX_LAKE_CELLS) {
                for (int k=0;k<sz;k++) {
                    cells[batch[k]].lake=true;
                    height[batch[k]]=SEA_LEVEL+0.005f;
                    inlake[batch[k]]=true;
                }
            }
        }
        free(visited); free(batch);
    }
    free(inlake); free(stk);
}

/* ========================================================================
 * FERTILITÉ (couche civilisation)
 * ====================================================================== */
static void compute_fertility(float *height, float *moisture, float *temperature,
                               Cell *cells) {
    /* Irrigation : un gros fleuve (fort débit accumulé = il a drainé de
     * nombreuses régions en amont) irrigue une plaine alluviale plus large
     * et plus riche. La portée du rayon croît avec le débit. */
    float *irrig=(float*)calloc(SCPS_N,sizeof(float)); if(!irrig)return;
    for (int y=0;y<SCPS_H;y++) for (int x=0;x<SCPS_W;x++) {
        float best=0.f;
        for (int dy=-5;dy<=5;dy++) for (int dx=-5;dx<=5;dx++) {
            int nx2=clampi(x+dx,0,SCPS_W-1), ny2=clampi(y+dy,0,SCPS_H-1);
            float r=cells[scps_idx(nx2,ny2)].river/255.f;   /* débit ∈ [0..1] */
            if (r<0.02f) continue;
            /* La portée d'irrigation s'élargit avec le débit du fleuve */
            float reach=1.f+r*4.f;
            float dist=sqrtf((float)(dx*dx+dy*dy));
            float v=r*clampf(1.f-dist/reach,0.f,1.f);
            if (v>best) best=v;
        }
        irrig[scps_idx(x,y)]=best;
    }

    /* Bonus de delta : embouchure (fleuve qui rencontre la mer) = limon */
    float *delta=(float*)calloc(SCPS_N,sizeof(float));
    if(delta) for (int y=0;y<SCPS_H;y++) for (int x=0;x<SCPS_W;x++) {
        int i=scps_idx(x,y);
        if (height[i]<SEA_LEVEL || cells[i].river<70) continue;
        for (int d=0;d<8;d++){
            int nx2=clampi(x+DDX[d],0,SCPS_W-1),ny2=clampi(y+DDY[d],0,SCPS_H-1);
            if (height[scps_idx(nx2,ny2)]<SEA_LEVEL){
                delta[i]=cells[i].river/255.f; break;
            }
        }
    }

    for (int y=0;y<SCPS_H;y++) for (int x=0;x<SCPS_W;x++) {
        int i=scps_idx(x,y);
        float h=height[i];
        if (h<SEA_LEVEL){cells[i].fertility=0.f;continue;}
        /* Pente */
        float slope=0.f;
        for (int d=0;d<4;d++) {
            int nx2=clampi(x+DDX[d*2],0,SCPS_W-1),ny2=clampi(y+DDY[d*2],0,SCPS_H-1);
            slope+=fabsf(h-height[scps_idx(nx2,ny2)]);
        }
        slope/=4.f;
        float t=temperature[i], m=moisture[i];
        float t_score=1.f-fabsf(t-0.55f)*1.9f;
        float coastal=1.f-cells[i].ocean_dist;     /* accès commerce/pêche */
        float f=0.26f*m+0.22f*clampf(t_score,0.f,1.f)
               +0.34f*clampf(irrig[i]*2.4f,0.f,1.f)  /* plaine alluviale */
               +(delta?delta[i]*0.35f:0.f)           /* delta limoneux */
               +0.08f*coastal
               -0.55f*clampf((h-MOUNTAIN_H)/0.18f,0.f,1.f)
               -3.5f*slope
               +0.22f*volcanic_soil(x,y);             /* terres volcaniques riches */
        if (cells[i].biome==BIO_VOLCANO) f=0.f;        /* roche nue : stérile */
        cells[i].fertility=clampf(f,0.f,1.f);
    }
    free(irrig); free(delta);
}

/* ========================================================================
 * TERRITOIRES — Voronoï GÉODÉSIQUE (frontières naturelles)
 *
 * Les germes (pondérés par la fertilité) croissent en bassins via un
 * Dijkstra multi-source sur un champ de coût de franchissement élevé sur
 * les fleuves, les crêtes et les fortes pentes. Là où deux bassins se
 * rencontrent, la frontière tombe sur l'obstacle → fleuves et montagnes
 * deviennent des frontières naturelles, sans les tracer à la main.
 * ====================================================================== */
#define MIN_PROV_DIST 9      /* serré → territoires nombreux (place pour 4 niveaux) */

static int g_pseedx[SCPS_MAX_PROV];
static int g_pseedy[SCPS_MAX_PROV];

static int pick_seeds(Cell *cells, int want) {
    /* Distribution pondérée par la fertilité, avec espacement minimum.
     * Somme préfixe (une passe) + recherche dichotomique → chaque tirage est
     * en O(log N) au lieu de O(N) : indispensable quand l'espacement serré
     * sature la terre et multiplie les tentatives. */
    static float pref[SCPS_N];
    float total=0.f;
    for (int i=0;i<SCPS_N;i++){ total+=cells[i].fertility; pref[i]=total; }
    if (total<1e-6f) return 0;

    int n=0, fails=0;
    const int MAXFAILS=4000;     /* arrêt quand la terre est saturée */
    while (n<want && fails<MAXFAILS) {
        float r=rng_f()*total;
        int lo=0, hi=SCPS_N-1;            /* plus petit i avec pref[i] >= r */
        while (lo<hi){ int mid=(lo+hi)>>1; if (pref[mid]<r) lo=mid+1; else hi=mid; }
        int cx=lo%SCPS_W, cy=lo/SCPS_W;
        bool ok=true;
        for (int k=0;k<n&&ok;k++){
            int dx=cx-g_pseedx[k],dy=cy-g_pseedy[k];
            if (dx*dx+dy*dy<MIN_PROV_DIST*MIN_PROV_DIST) ok=false;
        }
        if (ok){ g_pseedx[n]=cx; g_pseedy[n]=cy; n++; fails=0; }
        else fails++;
    }
    return n;
}

/* Coût de FRANCHISSEMENT d'une cellule. Élevé sur les obstacles naturels →
 * l'expansion géodésique y ralentit et la frontière entre deux territoires
 * s'y immobilise. C'est ainsi que « les frontières naturelles priment » :
 *   - rivières : barrière croissante avec le débit (fleuve ≫ ruisseau) ;
 *   - crêtes  : reliefs quasi infranchissables ;
 *   - pente   : les versants raides coûtent cher ;
 *   - bruit   : sinuosité résiduelle là où il n'y a aucun obstacle. */
static void build_cross_cost(const World *w, const float *height,
                             float *ccost, float seed_f) {
    for (int y=0;y<SCPS_H;y++) for (int x=0;x<SCPS_W;x++) {
        int i=scps_idx(x,y);
        if (height[i]<SEA_LEVEL){ ccost[i]=1e30f; continue; }  /* mer : infranchie */
        float cost=1.0f;
        float r=w->cell[i].river/255.f;
        if (r>0.06f) cost += 13.0f*powf((r-0.06f)/0.94f,1.15f);  /* rivière/fleuve */
        if (height[i]>MOUNTAIN_H)            cost += 18.0f;        /* crête */
        else if (height[i]>MOUNTAIN_H-0.08f) cost += 7.0f;        /* piémont */
        float hx=fabsf(height[scps_idx(clampi(x+1,0,SCPS_W-1),y)]
                      -height[scps_idx(clampi(x-1,0,SCPS_W-1),y)]);
        float hy=fabsf(height[scps_idx(x,clampi(y+1,0,SCPS_H-1))]
                      -height[scps_idx(x,clampi(y-1,0,SCPS_H-1))]);
        cost += (hx+hy)*24.0f;                                    /* pente */
        float nx=(float)x/SCPS_W, ny=(float)y/SCPS_H;
        /* Domain warp sur le bruit de sinuosité des frontières :
         * sans warp, les iso-coûts sont trop rectilignes → frontières trop droites. */
        float cwx=stb_perlin_fbm_noise3(nx*4.5f,     ny*4.5f,     seed_f+91.f,2.f,0.5f,4)*0.07f;
        float cwy=stb_perlin_fbm_noise3(nx*4.5f+2.3f,ny*4.5f+1.1f,seed_f+92.f,2.f,0.5f,4)*0.07f;
        cost += 0.55f*(0.5f+0.5f*stb_perlin_fbm_noise3((nx+cwx)*9.f,(ny+cwy)*9.f,
                                                        seed_f+90.f,2.f,0.5f,4));
        ccost[i]=cost;
    }
}

/* Tas-min binaire (coût, cellule) pour le Dijkstra multi-source. */
typedef struct { float c; int cell; } HNode;
static HNode *g_heap=NULL; static int g_hn=0, g_hcap=0;
static void heap_clear(void){ g_hn=0; }
static void heap_push(float c,int cell){
    if (g_hn>=g_hcap){ g_hcap=g_hcap?g_hcap*2:4096;
        g_heap=(HNode*)realloc(g_heap,(size_t)g_hcap*sizeof(HNode)); }
    int i=g_hn++; g_heap[i].c=c; g_heap[i].cell=cell;
    while(i>0){ int p=(i-1)/2; if(g_heap[p].c<=g_heap[i].c)break;
        HNode t=g_heap[p];g_heap[p]=g_heap[i];g_heap[i]=t; i=p; }
}
static HNode heap_pop(void){
    HNode top=g_heap[0]; g_heap[0]=g_heap[--g_hn];
    int i=0;
    for(;;){
        int l=2*i+1,r=l+1,m=i;
        if(l<g_hn&&g_heap[l].c<g_heap[m].c)m=l;
        if(r<g_hn&&g_heap[r].c<g_heap[m].c)m=r;
        if(m==i)break;
        HNode t=g_heap[m];g_heap[m]=g_heap[i];g_heap[i]=t; i=m;
    }
    return top;
}

static void assign_provinces(World *w, float *height, float seed_f) {
    int n=pick_seeds(w->cell, SCPS_MAX_PROV);
    if (n<4) n=4;
    w->n_provinces=n;

    /* ---- Voronoï GÉODÉSIQUE : expansion multi-source (Dijkstra) sur le
     * coût de franchissement. Les bassins de chaque germe croissent jusqu'à
     * se heurter sur fleuves et crêtes → frontières naturelles. ---------- */
    float *ccost=(float*)malloc(SCPS_N*sizeof(float));
    float *dist =(float*)malloc(SCPS_N*sizeof(float));
    if (!ccost||!dist){ free(ccost); free(dist); return; }
    build_cross_cost(w,height,ccost,seed_f);

    for (int i=0;i<SCPS_N;i++){ dist[i]=1e30f; w->cell[i].province=-1; }
    heap_clear();
    for (int p=0;p<n;p++){
        int i=scps_idx(g_pseedx[p],g_pseedy[p]);
        dist[i]=0.f; w->cell[i].province=(int16_t)p; heap_push(0.f,i);
    }
    while (g_hn>0){
        HNode top=heap_pop();
        int i=top.cell;
        if (top.c>dist[i]) continue;              /* entrée périmée */
        int x=i%SCPS_W, y=i/SCPS_W, pid=w->cell[i].province;
        for (int d=0;d<8;d++){
            int nx2=x+DDX[d], ny2=y+DDY[d];
            if (nx2<0||nx2>=SCPS_W||ny2<0||ny2>=SCPS_H) continue;
            int j=scps_idx(nx2,ny2);
            if (ccost[j]>1e29f) continue;         /* mer : non franchie */
            float nd=dist[i]+ccost[j]*DDIST[d];
            if (nd<dist[j]){ dist[j]=nd; w->cell[j].province=(int16_t)pid; heap_push(nd,j); }
        }
    }
    free(ccost); free(dist);

    /* Stats de province */
    int biome_cnt[SCPS_MAX_PROV][BIO_COUNT]={0};
    int area[SCPS_MAX_PROV]={0};
    float lat_s[SCPS_MAX_PROV]={0};
    float h_s[SCPS_MAX_PROV]={0};
    for (int y=0;y<SCPS_H;y++) for (int x=0;x<SCPS_W;x++) {
        int i=scps_idx(x,y); int p=w->cell[i].province;
        if (p<0)continue;
        area[p]++;
        lat_s[p]+=fabsf((float)y/SCPS_H-0.5f)*2.f;
        h_s[p]+=height[i];
        biome_cnt[p][(int)w->cell[i].biome]++;
    }
    for (int p=0;p<n;p++) {
        w->province[p].seed_x=(int16_t)g_pseedx[p];
        w->province[p].seed_y=(int16_t)g_pseedy[p];
        w->province[p].area=area[p];
        w->province[p].lat=(area[p]>0)?lat_s[p]/area[p]:0.5f;
        w->province[p].height_avg=(area[p]>0)?h_s[p]/area[p]:0.5f;
        w->province[p].color=province_palette(p);
        int bb=0,bc=0;
        for (int b=0;b<BIO_COUNT;b++) if(biome_cnt[p][b]>bc){bc=biome_cnt[p][b];bb=b;}
        w->province[p].biome_dominant=(Biome)bb;
    }
}

/* ========================================================================
 * CONTINENTS — masses continentales géographiques (remplissage par diffusion)
 *
 * Une « plaque de jeu » : composante connexe de terre (4-connexité). Les
 * grandes masses deviennent des continents distincts (doc §3) ; les petites
 * îles sont versées dans un bucket « archipel ».
 * ====================================================================== */
static void compute_continents(World *w, const float *height) {
    int16_t *comp=(int16_t*)malloc(SCPS_N*sizeof(int16_t));
    int     *stack=(int*)malloc(SCPS_N*sizeof(int));
    if (!comp||!stack){ free(comp); free(stack); return; }
    for (int i=0;i<SCPS_N;i++) comp[i] = (height[i]<SEA_LEVEL)? -1 : -2;

    /* Flood fill : composantes brutes + aire */
    enum { MAXTMP=2048 };
    int tarea[MAXTMP]; int ntmp=0;
    for (int s=0;s<SCPS_N && ntmp<MAXTMP;s++) {
        if (comp[s]!=-2) continue;
        int id=ntmp++, a=0, sp=0;
        stack[sp++]=s; comp[s]=id;
        while (sp>0) {
            int c=stack[--sp]; a++;
            int cx=c%SCPS_W, cy=c/SCPS_W;
            for (int d=0;d<8;d+=2) {              /* 4-connexité */
                int nx=cx+DDX[d], ny=cy+DDY[d];
                if (nx<0||nx>=SCPS_W||ny<0||ny>=SCPS_H) continue;
                int ni=scps_idx(nx,ny);
                if (comp[ni]==-2){ comp[ni]=id; stack[sp++]=ni; }
            }
        }
        tarea[id]=a;
    }

    /* Classe les composantes par aire ; les plus grandes = continents. */
    int order[MAXTMP];
    for (int i=0;i<ntmp;i++) order[i]=i;
    for (int i=0;i<ntmp;i++) for (int j=i+1;j<ntmp;j++)
        if (tarea[order[j]]>tarea[order[i]]){ int t=order[i];order[i]=order[j];order[j]=t; }

    int remap[MAXTMP];
    int keep = ntmp<SCPS_MAX_CONTINENT ? ntmp : SCPS_MAX_CONTINENT;
    bool has_bucket = ntmp>SCPS_MAX_CONTINENT;
    if (has_bucket) keep = SCPS_MAX_CONTINENT-1;   /* dernier slot = archipel */
    for (int i=0;i<ntmp;i++) {
        int tid=order[i];
        remap[tid] = (i<keep) ? i : (has_bucket ? SCPS_MAX_CONTINENT-1 : keep-1);
    }
    int ncont = has_bucket ? SCPS_MAX_CONTINENT : keep;
    if (ncont<1) ncont=1;
    w->n_continents=ncont;

    for (int c=0;c<ncont;c++) {
        w->continent[c].area=0; w->continent[c].n_countries=0;
        w->continent[c].color=province_palette(c*5+11);
        snprintf(w->continent[c].name,sizeof(w->continent[c].name),
                 (has_bucket&&c==ncont-1)?"Archipel":"Continent %d",c+1);
    }
    for (int i=0;i<SCPS_N;i++) {
        if (comp[i]<0){ w->cell[i].continent=-1; continue; }
        int c=remap[comp[i]];
        w->cell[i].continent=(int16_t)c;
        w->continent[c].area++;
    }
    free(comp); free(stack);
}

/* ========================================================================
 * HIÉRARCHIE — territoires → régions → pays (agglomération par contiguïté)
 *
 * Croissance gloutonne : on amorce un groupe sur un membre libre, puis on
 * agrège ses voisins contigus jusqu'à atteindre une taille cible (3-5).
 * Les groupes trop petits fusionnent ensuite dans un voisin. La contiguïté
 * étant terrestre, un groupe ne franchit jamais l'océan → régions et pays
 * restent automatiquement à l'intérieur d'un continent.
 * ====================================================================== */

/* Adjacence de provinces : matrice booléenne compacte. */
static bool *build_prov_adjacency(World *w) {
    int np=w->n_provinces;
    bool *adj=(bool*)calloc((size_t)np*np,sizeof(bool));
    if (!adj) return NULL;
    for (int y=0;y<SCPS_H;y++) for (int x=0;x<SCPS_W;x++) {
        int p=w->cell[scps_idx(x,y)].province;
        if (p<0) continue;
        if (x+1<SCPS_W){ int q=w->cell[scps_idx(x+1,y)].province;
            if (q>=0&&q!=p){ adj[p*np+q]=adj[q*np+p]=true; } }
        if (y+1<SCPS_H){ int q=w->cell[scps_idx(x,y+1)].province;
            if (q>=0&&q!=p){ adj[p*np+q]=adj[q*np+p]=true; } }
    }
    return adj;
}

/* Agglomère n éléments (graphe adj n×n) en groupes de taille [tmin..tmax].
 * Écrit le numéro de groupe de chaque élément dans grp[], renvoie le nombre
 * de groupes. cont[] = continent de chaque élément (ne pas franchir). */
static int agglomerate(const bool *adj, int n, const int16_t *cont,
                       int tmin, int tmax, int *grp) {
    for (int i=0;i<n;i++) grp[i]=-1;
    int ng=0;
    int *frontier=(int*)malloc((size_t)n*sizeof(int));
    if (!frontier) return 0;

    for (int s=0;s<n;s++) {
        if (grp[s]>=0) continue;
        int gid=ng++, target=tmin+(int)(rng_f()*(tmax-tmin+1)); if(target<tmin)target=tmin;
        int size=0, fn=0;
        grp[s]=gid; frontier[fn++]=s; size++;
        /* BFS gloutonne limitée à la taille cible et au continent */
        for (int f=0; f<fn && size<target; f++) {
            int a=frontier[f];
            for (int b=0;b<n && size<target;b++) {
                if (grp[b]>=0 || !adj[a*n+b]) continue;
                if (cont && cont[b]!=cont[s]) continue;
                grp[b]=gid; frontier[fn++]=b; size++;
            }
        }
    }
    free(frontier);

    /* Fusion des groupes sous-dimensionnés dans un voisin du même continent */
    int *gsize=(int*)calloc(ng,sizeof(int));
    for (int i=0;i<n;i++) gsize[grp[i]]++;
    for (int g=0; g<ng; g++) {
        if (gsize[g]>=tmin || gsize[g]==0) continue;
        /* cherche un groupe voisin */
        int target_g=-1;
        for (int i=0;i<n && target_g<0;i++) {
            if (grp[i]!=g) continue;
            for (int j=0;j<n;j++) {
                if (!adj[i*n+j]) continue;
                int gj=grp[j];
                if (gj!=g && (!cont||cont[j]==cont[i])) { target_g=gj; break; }
            }
        }
        if (target_g>=0) {
            for (int i=0;i<n;i++) if (grp[i]==g) grp[i]=target_g;
            gsize[target_g]+=gsize[g]; gsize[g]=0;
        }
    }
    /* Renumérotation compacte */
    int *remap=(int*)malloc(ng*sizeof(int));
    int m=0;
    for (int g=0; g<ng; g++) remap[g]=(gsize[g]>0)?m++:-1;
    for (int i=0;i<n;i++) grp[i]=remap[grp[i]];
    free(gsize); free(remap);
    return m;
}

/* ========================================================================
 * TOPONYMIE — noms de régions dans les 4 langues (elfe, humain, nain, orc)
 *
 * Chaque nom est composé par morphèmes liés à l'ENVIRONNEMENT dominant de la
 * région (forêt, montagne, marais…). La langue commune (humaine) est
 * descriptive — « Bois Doré » — avec un adjectif tiré du climat ; les trois
 * autres peuples ont une phonologie propre (préfixe + racine + suffixe).
 * ====================================================================== */
typedef enum {
    ENV_FOREST=0, ENV_MOUNTAIN, ENV_HILLS, ENV_DESERT, ENV_STEPPE,
    ENV_PLAINS, ENV_MARSH, ENV_COLD, ENV_COAST, ENV_COUNT
} EnvKind;

static EnvKind env_of_biome(Biome b) {
    switch (b) {
        case BIO_FOREST: case BIO_WOODS: case BIO_JUNGLE: return ENV_FOREST;
        case BIO_MOUNTAINS: case BIO_PEAK: case BIO_HIGHLANDS:
        case BIO_VOLCANO:                                 return ENV_MOUNTAIN;
        case BIO_HILLS:                                   return ENV_HILLS;
        case BIO_DESERT: case BIO_COASTAL_DESERT: case BIO_DRYLANDS:
                                                          return ENV_DESERT;
        case BIO_STEPPE: case BIO_SAVANNA:                return ENV_STEPPE;
        case BIO_PLAINS: case BIO_FARMLAND: case BIO_GRASSLAND:
                                                          return ENV_PLAINS;
        case BIO_MARSH: case BIO_BOG: case BIO_MANGROVE:  return ENV_MARSH;
        case BIO_GLACIER:                                 return ENV_COLD;
        case BIO_COAST: case BIO_SHALLOW:                 return ENV_COAST;
        default:                                          return ENV_PLAINS;
    }
}

/* Sélection déterministe d'un morphème (rng global = reproductible/graine). */
#define PICK(arr) (arr)[(int)(rng_f()*(sizeof(arr)/sizeof((arr)[0])))]

static void name_human(char *out, int n, EnvKind e, float lat, bool warm, bool wet) {
    static const char *NOUN[ENV_COUNT][4]={
        {"Bois","Forêt","Sylve","Futaie"},     /* FOREST   */
        {"Mont","Pic","Cime","Crête"},          /* MOUNTAIN */
        {"Coteau","Colline","Butte","Tertre"},  /* HILLS    */
        {"Désert","Dune","Reg","Sablière"},     /* DESERT   */
        {"Steppe","Lande","Plateau","Prairie"}, /* STEPPE   */
        {"Champ","Val","Pré","Plaine"},         /* PLAINS   */
        {"Marais","Gué","Fagne","Tourbière"},   /* MARSH    */
        {"Toundra","Gel","Névé","Banquise"},    /* COLD     */
        {"Rive","Anse","Cap","Havre"},          /* COAST    */
    };
    const char *adj;
    static const char *COLD_A[]={"Gelé","Blanc","Givré","Pâle"};
    static const char *ARID_A[]={"Brûlant","Ocre","Cendré","Aride"};
    static const char *LUSH_A[]={"Doré","Verdoyant","Profond","Vert","Sombre"};
    static const char *HIGH_A[]={"Haut","Altier","Gris","Noir"};
    static const char *WILD_A[]={"Vieux","Sauvage","Brumeux","Perdu"};
    if (lat>0.72f || e==ENV_COLD)                 adj=PICK(COLD_A);
    else if (e==ENV_DESERT || (e==ENV_STEPPE&&!wet)) adj=PICK(ARID_A);
    else if (e==ENV_FOREST || e==ENV_MARSH || (e==ENV_PLAINS&&wet)) adj=PICK(LUSH_A);
    else if (e==ENV_MOUNTAIN || e==ENV_HILLS)     adj=PICK(HIGH_A);
    else                                          adj=PICK(WILD_A);
    (void)warm;
    snprintf(out,n,"%s %s",PICK(NOUN[e]),adj);
}

static void name_elf(char *out, int n, EnvKind e) {
    static const char *PRE[ENV_COUNT][4]={
        {"Eryn","Taur","Lothlor","Galadh"},
        {"Ered","Orod","Caran","Thang"},
        {"Amon","Tyn","Emyn","Dol"},
        {"Lithui","Anor","Calad","Sîr"},
        {"Rhûn","Parth","Ladu","Nan"},
        {"Imloth","Nan","Lad","Mel"},
        {"Nîn","Loeg","Aelin","Hîth"},
        {"Helch","Ring","Niphred","Gwael"},
        {"Aer","Linn","Mith","Cír"},
    };
    static const char *SUF[]={"dor","ion","iel","las","wen","loth","rond",
                              "mar","thel","ven","riel","gorn"};
    snprintf(out,n,"%s%s",PICK(PRE[e]),PICK(SUF));
}

static void name_dwarf(char *out, int n, EnvKind e) {
    static const char *PRE[]={"Karak","Khaz","Kron","Dol","Grun","Bur","Zhuf"};
    static const char *ROOT[ENV_COUNT]={
        "gal","zorn","dur","dush","vrak","bok","mok","fros","zar"
    };
    static const char *SUF[]={"grund","dûm","bar","grim","hold","mar","kar","ank"};
    char tail[24];
    snprintf(tail,sizeof(tail),"%s%s",ROOT[e],PICK(SUF));
    tail[0]=(char)toupper((unsigned char)tail[0]);
    snprintf(out,n,"%s %s",PICK(PRE),tail);
}

static void name_orc(char *out, int n, EnvKind e) {
    static const char *PRE[]={"Gor","Mor","Grish","Uruk","Naz","Skarr","Drak"};
    static const char *ROOT[ENV_COUNT]={
        "gnar","gron","brak","skar","vog","grub","glob","hrim","zlak"
    };
    static const char *SUF[]={"nak","uk","gash","mog","dûr","snaga","grut","zog"};
    snprintf(out,n,"%s%s%s",PICK(PRE),ROOT[e],PICK(SUF));
}
#undef PICK

static void gen_region_names(World *w) {
    for (int r=0;r<w->n_regions;r++) {
        Region *rg=&w->region[r];
        /* Environnement dominant : vote des biomes des provinces membres. */
        int evote[ENV_COUNT]={0};
        float lat_s=0.f; int np=0; bool warm=false, wet=false;
        for (int k=0;k<rg->n_provinces;k++) {
            int p=rg->province_ids[k];
            if (p<0||p>=w->n_provinces) continue;
            evote[(int)env_of_biome(w->province[p].biome_dominant)]++;
            lat_s+=w->province[p].lat; np++;
        }
        EnvKind e=ENV_PLAINS; int best=-1;
        for (int i=0;i<ENV_COUNT;i++) if(evote[i]>best){best=evote[i];e=(EnvKind)i;}
        float lat=np?lat_s/np:0.5f;
        warm=(lat<0.45f);
        /* humide si l'env dominant est forêt/marais/plaine non aride */
        wet=(e==ENV_FOREST||e==ENV_MARSH||e==ENV_PLAINS);

        name_human(rg->name_hum,sizeof(rg->name_hum),e,lat,warm,wet);
        name_elf  (rg->name_elf,  sizeof(rg->name_elf),  e);
        name_dwarf(rg->name_dwarf,sizeof(rg->name_dwarf),e);
        name_orc  (rg->name_orc,  sizeof(rg->name_orc),  e);
        /* nom courant = variante humaine */
        snprintf(rg->name,sizeof(rg->name),"%s",rg->name_hum);
    }
}

static void build_hierarchy(World *w) {
    int np=w->n_provinces;
    if (np<1){ w->n_regions=w->n_countries=0; return; }

    /* Continent de chaque province (majorité de ses cellules — déjà posé sur
     * les cellules ; on relit la cellule-germe pour faire simple). */
    for (int p=0;p<np;p++) {
        int cx=w->province[p].seed_x, cy=w->province[p].seed_y;
        int16_t c=w->cell[scps_idx(cx,cy)].continent;
        if (c<0) c=0;
        w->province[p].continent=c;
    }

    bool *padj=build_prov_adjacency(w);
    if (!padj){ w->n_regions=w->n_countries=0; return; }

    int16_t *pcont=(int16_t*)malloc(np*sizeof(int16_t));
    int     *pgrp =(int*)malloc(np*sizeof(int));
    for (int p=0;p<np;p++) pcont[p]=w->province[p].continent;

    /* --- Niveau 1 : territoires → régions --- */
    int nreg=agglomerate(padj,np,pcont,SCPS_REG_TARGET_MIN,SCPS_REG_TARGET_MAX,pgrp);
    if (nreg>SCPS_MAX_REG) nreg=SCPS_MAX_REG;
    for (int r=0;r<nreg;r++){ w->region[r].n_provinces=0; }
    for (int p=0;p<np;p++) {
        int r=pgrp[p]; if(r<0||r>=SCPS_MAX_REG) r=0;
        w->province[p].region=(int16_t)r;
        Region *rg=&w->region[r];
        rg->continent=w->province[p].continent;
        if (rg->n_provinces<12) rg->province_ids[rg->n_provinces++]=(int16_t)p;
    }
    w->n_regions=nreg;

    /* --- Adjacence de régions (héritée de l'adjacence des provinces) --- */
    bool *radj=(bool*)calloc((size_t)nreg*nreg,sizeof(bool));
    int16_t *rcont=(int16_t*)malloc(nreg*sizeof(int16_t));
    int     *rgrp =(int*)malloc(nreg*sizeof(int));
    for (int r=0;r<nreg;r++) rcont[r]=w->region[r].continent;
    for (int p=0;p<np;p++) for (int q=0;q<np;q++) {
        if (!padj[p*np+q]) continue;
        int rp=w->province[p].region, rq=w->province[q].region;
        if (rp!=rq && rp<nreg && rq<nreg){ radj[rp*nreg+rq]=radj[rq*nreg+rp]=true; }
    }

    /* --- Niveau 2 : régions → pays --- */
    int ncty=agglomerate(radj,nreg,rcont,SCPS_CTY_TARGET_MIN,SCPS_CTY_TARGET_MAX,rgrp);
    if (ncty>SCPS_MAX_COUNTRY) ncty=SCPS_MAX_COUNTRY;
    for (int c=0;c<ncty;c++){ w->country[c].n_regions=0; w->country[c].capital_prov=-1; }
    for (int r=0;r<nreg;r++) {
        int c=rgrp[r]; if(c<0||c>=SCPS_MAX_COUNTRY) c=0;
        w->region[r].country=(int16_t)c;
        Country *ct=&w->country[c];
        ct->continent=w->region[r].continent;
        if (ct->n_regions<12) ct->region_ids[ct->n_regions++]=(int16_t)r;
    }
    w->n_countries=ncty;

    /* Propage pays → provinces ; rattache les pays aux continents. */
    for (int p=0;p<np;p++) {
        int r=w->province[p].region;
        w->province[p].country=(r<nreg)?w->region[r].country:0;
    }
    for (int c=0;c<ncty;c++) {
        int ci=w->country[c].continent; if(ci<0||ci>=w->n_continents)ci=0;
        Continent *cont=&w->continent[ci];
        if (cont->n_countries<SCPS_MAX_COUNTRY)
            cont->country_ids[cont->n_countries++]=(int16_t)c;
        w->country[c].color=province_palette(c*9+5);
        snprintf(w->country[c].name,sizeof(w->country[c].name),"Pays %d",c+1);
    }
    for (int r=0;r<nreg;r++)
        w->region[r].color=province_palette(r*7+3);
    gen_region_names(w);   /* toponymie elfe/humaine/naine/orque par environnement */

    /* Capitale de pays = province la plus fertile (proxy) — via aire faute
     * de fertilité stockée sur la province ; on prend la plus vaste. */
    for (int p=0;p<np;p++) {
        int c=w->province[p].country; if(c<0||c>=ncty)continue;
        int cap=w->country[c].capital_prov;
        if (cap<0 || w->province[p].area>w->province[cap].area)
            w->country[c].capital_prov=p;
    }

    /* Propage région/pays/continent sur les cellules (pour le rendu). */
    for (int i=0;i<SCPS_N;i++) {
        int p=w->cell[i].province;
        if (p<0){ w->cell[i].region=w->cell[i].country=-1; continue; }
        w->cell[i].region =w->province[p].region;
        w->cell[i].country=w->province[p].country;
    }

    free(padj); free(pcont); free(pgrp);
    free(radj); free(rcont); free(rgrp);
}

/* ========================================================================
 * FLAGS DE RENDU — côtes, frontières, hillshading (précalculé)
 * ====================================================================== */
static void compute_render_flags(World *w, float *height) {
    /* Côtes : cellule terrestre adjacente à la mer */
    for (int y=0;y<SCPS_H;y++) for (int x=0;x<SCPS_W;x++) {
        Cell *c=&w->cell[scps_idx(x,y)];
        c->coast=false;
        if (height[scps_idx(x,y)]<SEA_LEVEL) continue;
        for (int d=0;d<4;d++) {
            int nx2=clampi(x+DDX[d*2],0,SCPS_W-1),ny2=clampi(y+DDY[d*2],0,SCPS_H-1);
            if (height[scps_idx(nx2,ny2)]<SEA_LEVEL){c->coast=true;break;}
        }
    }

    /* Frontières par niveau (compare avec voisins E et S) */
    for (int y=0;y<SCPS_H-1;y++) for (int x=0;x<SCPS_W-1;x++) {
        Cell *c  =&w->cell[scps_idx(x,y)];
        Cell *ce =&w->cell[scps_idx(x+1,y)];
        Cell *cs =&w->cell[scps_idx(x,y+1)];
        c->border_prov=(c->province!=ce->province && (c->province>=0||ce->province>=0))
                      ||(c->province!=cs->province && (c->province>=0||cs->province>=0));
        c->border_reg =(c->region!=ce->region && (c->region>=0||ce->region>=0))
                      ||(c->region!=cs->region && (c->region>=0||cs->region>=0));
        c->border_country=(c->country!=ce->country && (c->country>=0||ce->country>=0))
                         ||(c->country!=cs->country && (c->country>=0||cs->country>=0));
        c->border_continent=(c->continent!=ce->continent)||(c->continent!=cs->continent);
    }

    /* Hillshading — lumière NW (convention cartographique standard)
     * Normale de surface calculée depuis les gradients de hauteur. */
    static const float LX=-0.6f, LY=-0.6f, LZ=0.5f; /* direction lumière (normalisée) */
    static const float LLEN=0.9165f;                  /* ||L|| */
    for (int y=0;y<SCPS_H;y++) for (int x=0;x<SCPS_W;x++) {
        int i=scps_idx(x,y);
        float he=height[scps_idx(clampi(x+1,0,SCPS_W-1),y)];
        float hw=height[scps_idx(clampi(x-1,0,SCPS_W-1),y)];
        float hs=height[scps_idx(x,clampi(y+1,0,SCPS_H-1))];
        float hn=height[scps_idx(x,clampi(y-1,0,SCPS_H-1))];
        float gx=(he-hw)*5.f, gy=(hs-hn)*5.f;
        float nlen=sqrtf(gx*gx+gy*gy+1.f);
        float dot=((-gx)*LX+(-gy)*LY+(1.f/nlen)*LZ)/(nlen*LLEN);
        w->cell[i].shade=clampf(0.35f+0.65f*dot,0.f,1.f);
    }
}

/* ========================================================================
 * FICHE SCPS PAR PROVINCE
 * ====================================================================== */
static float subsistance_for_biome(Biome b) {
    switch(b){
        case BIO_STEPPE: case BIO_SAVANNA: return 8.5f;
        case BIO_GRASSLAND: case BIO_HIGHLANDS: return 7.5f;
        case BIO_FARMLAND: case BIO_PLAINS:     return 3.0f;
        case BIO_COAST: case BIO_SHALLOW:       return 5.0f;
        case BIO_FOREST: case BIO_WOODS:        return 7.0f;
        case BIO_JUNGLE:                        return 6.5f;
        case BIO_MARSH:                         return 6.0f;
        case BIO_DESERT: case BIO_DRYLANDS:     return 9.0f;
        default:                                return 5.0f;
    }
}

static void gen_scps(World *w) {
    /* Familles linguistiques : 3 proto-langues réparties sur la carte */
    int famx[3]={SCPS_W/6, SCPS_W/2, SCPS_W*5/6};
    int famy[3]={SCPS_H/2, SCPS_H/4, SCPS_H*3/4};
    float maxr=sqrtf((float)(SCPS_W*SCPS_W+SCPS_H*SCPS_H))/2.f;

    for (int p=0;p<w->n_provinces;p++) {
        Province *pr=&w->province[p];
        pr->subsistance=clampf(subsistance_for_biome(pr->biome_dominant)+(rng_f()-0.5f)*1.8f,0.f,10.f);
        float bv=(pr->subsistance>7.f)?7.f+rng_f()*2.5f:3.f+rng_f()*3.5f;
        pr->valeurs=clampf(bv,0.f,10.f);
        pr->religion=clampf(pr->lat*5.5f+rng_f()*4.5f,0.f,10.f);
        pr->parente =clampf(pr->subsistance*0.65f+rng_f()*3.5f,0.f,10.f);

        /* Langue = horloge phylogénétique (distance à la proto-famille la plus proche) */
        float min_d=1e30f;
        for (int f=0;f<3;f++){
            float dx=(float)(pr->seed_x-famx[f]),dy=(float)(pr->seed_y-famy[f]);
            float d=sqrtf(dx*dx+dy*dy);
            if (d<min_d) min_d=d;
        }
        pr->langue=clampf(min_d/maxr*10.f+(rng_f()-0.5f)*1.5f,0.f,10.f);

        snprintf(pr->name,sizeof(pr->name),"Prov.%d",p+1);
    }
}

/* ========================================================================
 * TRACÉ DES RIVIÈRES PRINCIPALES
 * ====================================================================== */
static void trace_rivers(World *w, float *height) {
    int n=0;
    /* Cellules déjà couvertes par un fleuve, pour éviter de retracer dix fois
     * le même cours d'eau depuis des sources voisines. */
    uint8_t *traced=(uint8_t*)calloc(SCPS_N,sizeof(uint8_t));
    bool    *seen  =(bool*)   calloc(SCPS_N,sizeof(bool));
    if (!traced||!seen){ free(traced); free(seen); w->n_rivers=0; return; }

    for (int y=2;y<SCPS_H-2&&n<SCPS_MAX_RIVERS;y+=3)
    for (int x=2;x<SCPS_W-2&&n<SCPS_MAX_RIVERS;x+=3) {
        int i=scps_idx(x,y);
        /* Source = cellule d'altitude (le débit y est nul par construction ;
         * il grossit en descendant). On ne filtre PAS sur le débit ici. */
        if (height[i]<MOUNTAIN_H-0.10f) continue;
        if (traced[i]) continue;

        River *rv=&w->river[n];
        rv->len=0; rv->flow_max=0.f;
        memset(seen,0,SCPS_N*sizeof(bool));

        int cx=x,cy=y;
        for (int s=0;s<SCPS_RIVER_MAXLEN;s++) {
            if (cx<0||cx>=SCPS_W||cy<0||cy>=SCPS_H) break;
            int ci=scps_idx(cx,cy);
            if (seen[ci]) break;          /* anti-boucle */
            seen[ci]=true;
            rv->x[rv->len]=(int16_t)cx;
            rv->y[rv->len]=(int16_t)cy;
            rv->len++;
            float fl=w->cell[ci].river/255.f;
            if (fl>rv->flow_max) rv->flow_max=fl;
            if (height[ci]<SEA_LEVEL) break;   /* atteint la mer */

            /* Descente : voisin le plus bas (D8 stocké, sinon recherche) */
            int dir=w->cell[ci].flow_dir;
            if (dir<0) {
                float mh=height[ci]; int best=-1;
                for (int d=0;d<8;d++){
                    int nx2=cx+DDX[d],ny2=cy+DDY[d];
                    if (nx2<0||nx2>=SCPS_W||ny2<0||ny2>=SCPS_H)continue;
                    if (height[scps_idx(nx2,ny2)]<mh){mh=height[scps_idx(nx2,ny2)];best=d;}
                }
                if (best<0) break;          /* cuvette : fin du cours */
                dir=best;
            }
            cx+=DDX[dir]; cy+=DDY[dir];
        }

        /* On retient le fleuve s'il est long ET devient un vrai cours d'eau */
        if (rv->len>14 && rv->flow_max>0.30f) {
            for (int s=0;s<rv->len;s++) traced[scps_idx(rv->x[s],rv->y[s])]=1;
            n++;
        }
    }
    free(traced); free(seen);
    w->n_rivers=n;
}

/* ========================================================================
 * ALTÉRATION — « 10 000 ans de météo de merde »
 *
 * Deux temps :
 *   A. Érosion thermique (avant l'hydrologie) : le talus s'éboule, les
 *      reliefs s'arrondissent → monde ancien, usé, lissé.
 *   B. Reconquête écologique (après les biomes) : laissé à l'abandon, le
 *      monde se gorge d'eau et se couvre de végétation — marais, tourbières,
 *      mangroves, bois envahissants. Impression de « terre inconnue », pas
 *      civilisée.
 * ====================================================================== */

/* A. Érosion thermique : éboulement du talus au-delà d'une pente seuil. */
static void step_thermal_erosion(float *height, int iters) {
    float *delta=(float*)malloc(SCPS_N*sizeof(float));
    if(!delta) return;
    const float TALUS=0.010f, RATE=0.30f;
    for (int it=0; it<iters; it++) {
        memset(delta,0,SCPS_N*sizeof(float));
        for (int y=0;y<SCPS_H;y++) for (int x=0;x<SCPS_W;x++) {
            int i=scps_idx(x,y);
            float h=height[i];
            int bj=-1; float bd=0.f;
            for (int d=0;d<8;d++) {
                int nx2=x+DDX[d],ny2=y+DDY[d];
                if(nx2<0||nx2>=SCPS_W||ny2<0||ny2>=SCPS_H)continue;
                float diff=h-height[scps_idx(nx2,ny2)];
                if (diff>bd){bd=diff;bj=scps_idx(nx2,ny2);}
            }
            if (bj>=0 && bd>TALUS) {
                float move=(bd-TALUS)*RATE*0.5f;
                delta[i]-=move; delta[bj]+=move;
            }
        }
        for (int i=0;i<SCPS_N;i++) height[i]+=delta[i];
    }
    free(delta);
    normalize_f(height,SCPS_N);
}

/* Biome déterminé par le relief : ne doit pas être lissé (sinon les crêtes
 * fines disparaissent). */
static bool biome_is_relief(Biome b) {
    return b==BIO_HIGHLANDS||b==BIO_HILLS||b==BIO_MOUNTAINS||
           b==BIO_PEAK||b==BIO_GLACIER;
}

/* B. Reconquête écologique. */
static void step_weathering(World *w, const float *height, float seed_f) {
    Cell *c=w->cell;

    /* B1. Reconquête forestière : prairies/plaines/savanes tempérées et
     *     humides repassent en bois/forêt (la végétation reprend ses droits). */
    for (int y=0;y<SCPS_H;y++) for (int x=0;x<SCPS_W;x++) {
        int i=scps_idx(x,y);
        if (height[i]<SEA_LEVEL) continue;
        Biome b=c[i].biome;
        if (b!=BIO_GRASSLAND&&b!=BIO_PLAINS&&b!=BIO_FARMLAND&&b!=BIO_SAVANNA) continue;
        float t=c[i].temperature, m=c[i].moisture;
        float nx=(float)x/SCPS_W, ny=(float)y/SCPS_H;
        float overgrow=stb_perlin_fbm_noise3(nx*6.f,ny*5.f,seed_f+1300.f,2.f,0.5f,4);
        if (m>0.40f && t>0.28f && t<0.74f && overgrow>-0.05f)
            c[i].biome=(m>0.60f)?BIO_FOREST:BIO_WOODS;
    }

    /* B2. Zones humides : mangrove (côte tropicale), marais (chaud) /
     *     tourbière (froid) dans les bas-fonds plats et gorgés d'eau. */
    for (int y=0;y<SCPS_H;y++) for (int x=0;x<SCPS_W;x++) {
        int i=scps_idx(x,y);
        if (height[i]<SEA_LEVEL) continue;
        float t=c[i].temperature, m=c[i].moisture;
        bool near_sea=false, near_lake=false;
        float minh=height[i], maxh=height[i];
        for (int d=0;d<8;d++) {
            int j=scps_idx(clampi(x+DDX[d],0,SCPS_W-1),clampi(y+DDY[d],0,SCPS_H-1));
            float hh=height[j];
            if (hh<SEA_LEVEL) near_sea=true;
            if (c[j].lake)    near_lake=true;
            if (hh<minh) minh=hh;
            if (hh>maxh) maxh=hh;
        }
        float relief=maxh-minh;                    /* faible = plat */
        bool wet=(m>0.55f)||(c[i].river>60)||near_lake;

        if (near_sea && t>0.63f && m>0.50f && height[i]<SEA_LEVEL+0.022f) {
            c[i].biome=BIO_MANGROVE;               /* palétuviers tropicaux */
        } else if (relief<0.03f && wet &&
                   (height[i]<SEA_LEVEL+0.06f || near_lake || c[i].river>90)) {
            c[i].biome=(t<0.32f)?BIO_BOG:BIO_MARSH;
        }
    }

    /* B2c. Aspérités rocheuses dans les steppes et pelouses sèches. */
    for (int y=0;y<SCPS_H;y++) for (int x=0;x<SCPS_W;x++) {
        int i=scps_idx(x,y);
        if (height[i]<SEA_LEVEL) continue;
        Biome b=c[i].biome;
        if (b!=BIO_STEPPE&&b!=BIO_DRYLANDS&&b!=BIO_GRASSLAND) continue;
        float nx=(float)x/SCPS_W, ny=(float)y/SCPS_H;
        float rock=stb_perlin_ridge_noise3(nx*14.f,ny*14.f,seed_f+4200.f,2.f,0.5f,1.f,4);
        if      (rock>0.72f)              c[i].biome=BIO_HIGHLANDS;
        else if (rock>0.62f&&b==BIO_STEPPE) c[i].biome=BIO_HILLS;
    }

    /* B2d. Zones mortes / toundra : forêt boréale très continentale → steppe/glacier.
     *      Effet Sibérie : intérieur froid + éloigné de l'océan = zone inhospitalière. */
    for (int y=0;y<SCPS_H;y++) for (int x=0;x<SCPS_W;x++) {
        int i=scps_idx(x,y);
        if (height[i]<SEA_LEVEL) continue;
        if (c[i].temperature>0.22f) continue;
        if (c[i].ocean_dist<0.62f) continue;
        Biome b=c[i].biome;
        if (b!=BIO_FOREST&&b!=BIO_WOODS) continue;
        float nx=(float)x/SCPS_W, ny=(float)y/SCPS_H;
        float tundra=stb_perlin_fbm_noise3(nx*5.f,ny*5.f,seed_f+4300.f,2.f,0.5f,3);
        if (tundra>0.0f)
            c[i].biome=(c[i].moisture<0.25f)?BIO_GLACIER:BIO_STEPPE;
    }

    /* B3. Despeckle : 2 passes de filtre majoritaire pour fondre les pixels
     *     isolés en taches cohérentes (le monde « se lisse »). Les biomes de
     *     relief sont préservés. */
    Biome *snap=(Biome*)malloc(SCPS_N*sizeof(Biome));
    if (!snap) return;
    for (int pass=0;pass<2;pass++) {
        for (int i=0;i<SCPS_N;i++) snap[i]=c[i].biome;
        for (int y=1;y<SCPS_H-1;y++) for (int x=1;x<SCPS_W-1;x++) {
            int i=scps_idx(x,y);
            if (height[i]<SEA_LEVEL || biome_is_relief(snap[i])) continue;
            int cnt[BIO_COUNT]={0}, self=0;
            for (int d=0;d<8;d++) {
                int j=scps_idx(x+DDX[d],y+DDY[d]);
                if (height[j]<SEA_LEVEL || biome_is_relief(snap[j])) continue;
                cnt[(int)snap[j]]++;
                if (snap[j]==snap[i]) self++;
            }
            if (self<=1) {                         /* pixel isolé → mode voisin */
                int bb=(int)snap[i], bc=-1;
                for (int b=0;b<BIO_COUNT;b++) if(cnt[b]>bc){bc=cnt[b];bb=b;}
                if (bc>0) c[i].biome=(Biome)bb;
            }
        }
    }
    free(snap);

    /* B4. Artefacts FINAUX — posés APRÈS le despeckle pour qu'il ne les fonde
     *     pas. Ce sont des particularités voulues, isolées, qui font la beauté
     *     d'un monde (clairières, cônes volcaniques). */

    /* B4a. Clairières : percées lumineuses dans la forêt dense. */
    for (int y=0;y<SCPS_H;y++) for (int x=0;x<SCPS_W;x++) {
        int i=scps_idx(x,y);
        if (height[i]<SEA_LEVEL) continue;
        Biome b=c[i].biome;
        if (b!=BIO_FOREST&&b!=BIO_WOODS) continue;
        float nx=(float)x/SCPS_W, ny=(float)y/SCPS_H;
        float gap =stb_perlin_fbm_noise3(nx*18.f,ny*18.f,seed_f+4100.f,2.f,0.5f,3);
        float gap2=stb_perlin_fbm_noise3(nx*9.f, ny*9.f, seed_f+4110.f,2.f,0.5f,2);
        if (gap>0.35f&&gap2>0.10f)
            c[i].biome=(c[i].moisture>0.48f)?BIO_GRASSLAND:BIO_PLAINS;
    }

    /* B4b. Cônes volcaniques : la caldeira nue marque le biome ; les pentes
     *      proches gardent leur biome mais reçoivent un sol volcanique
     *      fertile (cf. compute_fertility, qui relit g_volc). */
    volcanoes_mark(w, height);

    /* Télémétrie : part des terres reconquises par les milieux sauvages */
    int marsh=0,bog=0,mang=0,wood=0,land=0;
    for (int i=0;i<SCPS_N;i++){
        if (height[i]<SEA_LEVEL) continue;
        land++;
        switch (c[i].biome){
            case BIO_MARSH: marsh++; break;
            case BIO_BOG:   bog++;   break;
            case BIO_MANGROVE: mang++; break;
            case BIO_WOODS: case BIO_FOREST: wood++; break;
            default: break;
        }
    }
    if (land<1) land=1;
    printf("(marais %d%% bois %d%% mangrove %d%%) ",
           (marsh+bog)*100/land, wood*100/land, mang*100/land);
}

/* ========================================================================
 * RESSOURCES — bien commercial principal par province
 *
 * Placement causal (la géographie décide), jamais un tirage à plat :
 * un poids est accumulé par bien selon biome, latitude, altitude et accès
 * à la mer, puis on tire au sort parmi les candidats pondérés.
 *   - l'or vient surtout des montagnes ;
 *   - les épices/bois tropical de la jungle ;
 *   - la fourrure des forêts froides ;
 *   - le poisson/sel des côtes ; etc.
 * ====================================================================== */
static void gen_resources(World *w) {
    /* Agrégats par province : côte, fertilité/humidité/température moyennes,
     * débit fluvial maximal (pour le poisson de fleuve). */
    static float moist_s[SCPS_MAX_PROV], temp_s[SCPS_MAX_PROV];
    static int   cnt[SCPS_MAX_PROV], rivmax[SCPS_MAX_PROV];
    static bool  coastal[SCPS_MAX_PROV];
    for (int p=0;p<w->n_provinces;p++){ moist_s[p]=temp_s[p]=0.f;
        cnt[p]=0; rivmax[p]=0; coastal[p]=false; }
    for (int i=0;i<SCPS_N;i++) {
        int p=w->cell[i].province;
        if (p<0) continue;
        moist_s[p]+=w->cell[i].moisture;
        temp_s[p]+=w->cell[i].temperature;
        cnt[p]++;
        if (w->cell[i].coast) coastal[p]=true;
        if (w->cell[i].river>rivmax[p]) rivmax[p]=w->cell[i].river;
    }

    for (int p=0;p<w->n_provinces;p++) {
        Province *pr=&w->province[p];
        pr->coastal = coastal[p];
        int   n     = cnt[p]>0?cnt[p]:1;
        float moist = moist_s[p]/n, tmp = temp_s[p]/n;
        float H     = pr->height_avg;
        Biome B     = pr->biome_dominant;
        bool  warm  = tmp>0.55f, cold = tmp<0.34f;
        bool  bigriver = rivmax[p]>150;

        bool flat       = (B==BIO_PLAINS||B==BIO_FARMLAND||B==BIO_GRASSLAND||
                           B==BIO_STEPPE||B==BIO_SAVANNA||B==BIO_DRYLANDS);
        bool humid_flat = (B==BIO_PLAINS||B==BIO_FARMLAND||B==BIO_GRASSLAND)&&moist>0.45f;
        bool pastoral   = (B==BIO_GRASSLAND||B==BIO_STEPPE||B==BIO_SAVANNA);
        bool arid       = (B==BIO_DRYLANDS||B==BIO_DESERT||B==BIO_COASTAL_DESERT||
                           B==BIO_SAVANNA)||moist<0.30f;
        bool forested   = (B==BIO_FOREST||B==BIO_WOODS||B==BIO_JUNGLE||B==BIO_MANGROVE);
        bool hills      = (B==BIO_HILLS||B==BIO_HIGHLANDS);
        bool mtn        = (B==BIO_MOUNTAINS||B==BIO_PEAK||H>0.70f);
        bool relief     = biome_is_relief(B)||H>0.58f;
        bool mesa       = arid && (hills||H>0.55f);

        float wt[RES_COUNT]; for (int r=0;r<RES_COUNT;r++) wt[r]=0.f;
        #define ADD(R,V) wt[R]+=(V)

        /* --- Agricole & élevage --- */
        if (B==BIO_FARMLAND)       ADD(RES_GRAIN,     3.6f);   /* terres cultivées */
        if (B==BIO_PLAINS)         ADD(RES_GRAIN,     2.6f);
        if (B==BIO_GRASSLAND)      ADD(RES_GRAIN,     2.0f);   /* arable aussi */
        if (humid_flat)            ADD(RES_GRAIN,     0.8f);
        if (flat && !arid)         ADD(RES_LIVESTOCK, 1.6f);
        if (pastoral)            { ADD(RES_LIVESTOCK, 2.2f); ADD(RES_WOOL, 1.8f); }
        if (hills)               { ADD(RES_WOOL,      2.0f); ADD(RES_LIVESTOCK, 1.0f); }
        if (flat && arid && warm)  ADD(RES_COTTON,    2.6f);   /* flatlands arides */

        /* --- Poisson : côte ou fleuve à fort débit (sans voler la terre
         *     productive : poids modéré, gagne surtout les côtes pauvres) --- */
        if (coastal[p])            ADD(RES_FISH, 1.4f);
        if (bigriver)              ADD(RES_FISH, 1.7f);

        /* --- Fourrure : régions froides et sauvages --- */
        if (cold && (forested||B==BIO_BOG||B==BIO_GLACIER||B==BIO_STEPPE))
                                   ADD(RES_FUR, 3.0f);

        /* --- Sel : déserts et côtes --- */
        if (arid)                  ADD(RES_SALT, 1.8f);
        if (coastal[p])            ADD(RES_SALT, 0.8f);

        /* --- Sucre : côtes arides chaudes --- */
        if (coastal[p] && arid && warm) ADD(RES_SUGAR, 2.6f);

        /* --- Bois : régions boisées --- */
        if (forested)              ADD(RES_WOOD, 3.6f);

        /* --- Herbes médicinales : zones humides d'altitude --- */
        if (B==BIO_BOG)            ADD(RES_MED_HERBS, 2.6f);
        if ((hills||H>0.55f) && moist>0.55f) ADD(RES_MED_HERBS, 1.4f);

        /* --- Minéraux de relief --- */
        if (relief) {
            ADD(RES_COPPER, mtn?2.0f:1.4f);
            ADD(RES_IRON,   mtn?2.0f:1.4f);
            ADD(RES_COAL,   1.6f);
            ADD(RES_GOLD,   mtn?2.6f:0.6f);            /* l'or, surtout en montagne */
            ADD(RES_PRECIOUS_METAL, mtn?1.2f:0.2f);    /* mithril, adamantium */
            ADD(RES_SULFUR, mtn?1.4f:0.4f);            /* volcanique */
        }
        if (mesa) { ADD(RES_COPPER,1.5f); ADD(RES_IRON,1.5f); }  /* mesas */

        /* --- Salpêtre : arides et grottes de montagne (→ poudre, doc §9) --- */
        if (B==BIO_DESERT||B==BIO_DRYLANDS) ADD(RES_SALTPETER, 1.6f);
        if (mtn)                            ADD(RES_SALTPETER, 0.6f);
        #undef ADD

        /* Tirage pondéré — UNIQUEMENT parmi les ressources BRUTES.
         * Les biens de production (≥ RES_PROD_FIRST) seront posés plus tard
         * par les chaînes de transformation. */
        float tot=0.f; for (int r=1;r<RES_PROD_FIRST;r++) tot+=wt[r];
        if (tot<1e-4f){ pr->resource = forested?RES_WOOD:RES_GRAIN; continue; }
        float roll=rng_f()*tot, acc=0.f; Resource chosen=RES_GRAIN;
        for (int r=1;r<RES_PROD_FIRST;r++){ acc+=wt[r]; if(acc>=roll){chosen=(Resource)r;break;} }
        pr->resource=chosen;
    }
}

/* ========================================================================
 * POINT D'ENTRÉE
 * ====================================================================== */
WorldParams worldparams_default(uint32_t seed) {
    WorldParams p;
    p.seed         = seed;
    p.n_continents = 4;      /* doc §3 — assez pour archipels & ponts type Béringie */
    p.land_amount  = 0.5f;
    p.world_age    = 0.5f;
    p.erosion      = 0.5f;
    p.mountains    = 0.5f;
    p.temperature  = 0.5f;
    p.humidity     = 0.5f;
    return p;
}

void world_generate(World *w, const WorldParams *P) {
    WorldParams def;
    if (!P){ def=worldparams_default((uint32_t)0); P=&def; }
    memset(w,0,sizeof(*w));
    w->seed=P->seed;
    rng_seed(P->seed);
    float seed_f=(float)(P->seed&0xFFFF)/(float)0x10000;

    /* world_age → nombre d'itérations d'érosion thermique (vieux = usé) */
    int thermal_iters = 2 + (int)(P->world_age*14.f);

    float *height =  (float*)malloc(SCPS_N*sizeof(float));
    float *moisture= (float*)malloc(SCPS_N*sizeof(float));
    float *temp    = (float*)malloc(SCPS_N*sizeof(float));
    float *odist   = (float*)malloc(SCPS_N*sizeof(float));
    if (!height||!moisture||!temp||!odist){fprintf(stderr,"scps: OOM\n");goto end;}

    printf("[scps] géologie...     "); fflush(stdout);
    step_geology(height,seed_f,P);        printf("ok\n");

    printf("[scps] architecture... "); fflush(stdout);
    step_architecture(height,seed_f);     printf("ok\n");

    printf("[scps] altération...   "); fflush(stdout);
    step_thermal_erosion(height,thermal_iters); printf("ok\n");

    printf("[scps] érosion...      "); fflush(stdout);
    step_erosion(height,w->cell,P->erosion); printf("ok\n");

    printf("[scps] côtes fract...  "); fflush(stdout);
    step_coastline(height,seed_f);        printf("ok\n");

    printf("[scps] carte fantôme.. "); fflush(stdout);
    step_ghost_layer(height,seed_f);      printf("ok\n");

    printf("[scps] fantôme négat.. "); fflush(stdout);
    step_ghost_negative(height,seed_f);   printf("ok\n");

    printf("[scps] lissage océan... "); fflush(stdout);
    step_smooth_ocean(height,4);          printf("ok\n");

    printf("[scps] continentalité..."); fflush(stdout);
    compute_ocean_distance(height,odist);  printf("ok\n");

    printf("[scps] climat (vent)... "); fflush(stdout);
    gen_climate(w,height,moisture,temp,odist,seed_f,P); printf("ok\n");

    /* Atténuation des rivières en zones arides : le débit D8 est purement
     * topographique ; on corrige après le climat pour effacer les « fleuves »
     * fantômes qui traverseraient un désert ou une steppe très sèche.
     * Quadratique : en dessous de moisture=0.25 le débit s'annule presque. */
    for (int i=0; i<SCPS_N; i++) {
        if (height[i] < SEA_LEVEL) continue;
        float m = moisture[i];
        if (m < 0.30f) {
            float damp = (m / 0.30f);
            damp = damp * damp;
            w->cell[i].river = (uint8_t)(w->cell[i].river * damp);
        }
    }

    /* Lissage de moisture et temp sur 2 passes 3×3 (terrestre uniquement) :
     * adoucit les gradients trop nets avant l'assignation des biomes →
     * transitions plus organiques entre zones sèche/humide et chaud/froid. */
    {
        float *sm=(float*)malloc(SCPS_N*sizeof(float));
        float *st=(float*)malloc(SCPS_N*sizeof(float));
        if (sm&&st) {
            for (int pass=0;pass<2;pass++) {
                for (int y=0;y<SCPS_H;y++) for (int x=0;x<SCPS_W;x++) {
                    int i=scps_idx(x,y);
                    if (height[i]<SEA_LEVEL){ sm[i]=moisture[i];st[i]=temp[i];continue; }
                    float wm=0.f,wt=0.f,ws=0.f;
                    for (int dy=-1;dy<=1;dy++) for (int dx=-1;dx<=1;dx++) {
                        int nx2=clampi(x+dx,0,SCPS_W-1),ny2=clampi(y+dy,0,SCPS_H-1);
                        int j=scps_idx(nx2,ny2);
                        if (height[j]<SEA_LEVEL) continue;
                        float w2=(dx==0&&dy==0)?4.f:(dx*dy==0)?2.f:1.f; /* Gauss 3×3 */
                        wm+=moisture[j]*w2; wt+=temp[j]*w2; ws+=w2;
                    }
                    sm[i]=(ws>0.f)?wm/ws:moisture[i];
                    st[i]=(ws>0.f)?wt/ws:temp[i];
                }
                memcpy(moisture,sm,SCPS_N*sizeof(float));
                memcpy(temp,st,SCPS_N*sizeof(float));
            }
        }
        free(sm); free(st);
    }

    printf("[scps] biomes...       "); fflush(stdout);
    /* Jitter haute fréquence sur t et m pour briser les lignes de seuil */
    for (int y=0;y<SCPS_H;y++) for (int x=0;x<SCPS_W;x++) {
        int i=scps_idx(x,y);
        float nx2=(float)x/SCPS_W, ny2=(float)y/SCPS_H;
        /* Deux couches : grosse (brise les bandes latitudinales) + fine (hf) */
        float jt= stb_perlin_fbm_noise3(nx2*5.f, ny2*4.5f,seed_f+900.f,2.f,0.5f,4)*0.065f
                + stb_perlin_noise3    (nx2*16.f,ny2*15.f, seed_f+902.f,0,0,0)     *0.035f;
        float jm= stb_perlin_fbm_noise3(nx2*5.f, ny2*4.5f,seed_f+901.f,2.f,0.5f,4)*0.055f
                + stb_perlin_noise3    (nx2*15.f,ny2*16.f, seed_f+903.f,0,0,0)     *0.028f;
        w->cell[i].height     =height[i];
        w->cell[i].moisture   =moisture[i];
        w->cell[i].temperature=temp[i];
        w->cell[i].biome=assign_biome(height[i],moisture[i]+jm,temp[i]+jt);
    }
    printf("ok\n");

    fill_lakes(height,w->cell);
    for (int i=0;i<SCPS_N;i++) w->cell[i].height=height[i];

    printf("[scps] reconquête...   "); fflush(stdout);
    step_weathering(w,height,seed_f);     printf("ok\n");

    printf("[scps] fertilité...    "); fflush(stdout);
    compute_fertility(height,moisture,temp,w->cell); printf("ok\n");

    printf("[scps] territoires...  "); fflush(stdout);
    assign_provinces(w,height,seed_f);
    printf("ok (%d terr.)\n",w->n_provinces);

    printf("[scps] continents...   "); fflush(stdout);
    compute_continents(w,height);
    printf("ok (%d cont.)\n",w->n_continents);

    printf("[scps] hiérarchie...   "); fflush(stdout);
    build_hierarchy(w);
    printf("ok (%d rég. %d pays)\n",w->n_regions,w->n_countries);

    printf("[scps] flags rendu...  "); fflush(stdout);
    compute_render_flags(w,height);       printf("ok\n");

    printf("[scps] SCPS...         "); fflush(stdout);
    gen_scps(w);                          printf("ok\n");

    printf("[scps] ressources...   "); fflush(stdout);
    gen_resources(w);                     printf("ok\n");

    printf("[scps] rivières...     "); fflush(stdout);
    trace_rivers(w,height);
    printf("ok (%d riv.)\n",w->n_rivers);

end:
    free(height); free(moisture); free(temp); free(odist);
}

/* ========================================================================
 * COULEURS ET NOMS
 * ====================================================================== */
uint32_t biome_base_color(Biome b) {
    /* Palette "carte physique historique" — désaturée, chaude */
    static const uint32_t C[BIO_COUNT]={
        0xFF0C1824u, /* DEEP_OCEAN     */
        0xFF142440u, /* OCEAN          */
        0xFF1C4070u, /* SHALLOW        */
        0xFFCCB87Au, /* COAST          */
        0xFF8AAE58u, /* PLAINS         */
        0xFF98BE48u, /* FARMLAND       */
        0xFF4C9040u, /* GRASSLAND      */
        0xFFB09058u, /* STEPPE         */
        0xFFB88C40u, /* SAVANNA        */
        0xFFC89448u, /* DRYLANDS       */
        0xFFDCCC58u, /* DESERT         */
        0xFFD0BC6Cu, /* COASTAL_DESERT */
        0xFF24601Cu, /* FOREST         */
        0xFF3C6C30u, /* WOODS          */
        0xFF146010u, /* JUNGLE         */
        0xFF3C7860u, /* MARSH          */
        0xFF847860u, /* HIGHLANDS      */
        0xFF887050u, /* HILLS          */
        0xFF685848u, /* MOUNTAINS      */
        0xFFACA090u, /* PEAK           */
        0xFFDCECF8u, /* GLACIER        */
        0xFF2E6848u, /* MANGROVE       */
        0xFF566848u, /* BOG            */
        0xFF402820u, /* VOLCANO — basalte sombre */
    };
    return (b>=0&&b<BIO_COUNT)?C[(int)b]:0xFFFF00FFu;
}

const char *biome_name(Biome b) {
    static const char *N[BIO_COUNT]={
        "Océan profond","Océan","Eaux côtières","Littoral",
        "Plaines","Terres cultivées","Prairies","Steppe",
        "Savane","Terres sèches","Désert","Désert côtier",
        "Forêt","Bois","Jungle","Marais",
        "Hauts plateaux","Collines","Montagnes","Sommets","Glacier",
        "Mangrove","Tourbière","Volcan",
    };
    return (b>=0&&b<BIO_COUNT)?N[(int)b]:"?";
}

const char *resource_name(Resource r) {
    static const char *N[RES_COUNT]={
        "—",
        /* brutes agricoles */
        "Céréales","Bétail","Laine","Poisson","Fourrure",
        "Sel","Coton","Sucre","Bois","Herbes médicinales",
        /* brutes minérales */
        "Cuivre","Fer","Charbon","Soufre","Salpêtre",
        "Or","Métaux précieux",
        /* production */
        "Étoffe","Fournitures navales","Vin",
        "Bien précieux","Étoffe précieuse","Papier",
    };
    return (r>=0&&r<RES_COUNT)?N[(int)r]:"?";
}

uint32_t resource_color(Resource r) {
    static const uint32_t C[RES_COUNT]={
        0xFF404040u,                                              /* NONE */
        /* agricoles */
        0xFFE8C84Cu,0xFFB07840u,0xFFE0D0B0u,0xFF4078A0u,0xFF7B4A28u, /* grain,livestock,wool,fish,fur */
        0xFFF0F0F0u,0xFFF0E0E0u,0xFFE0A040u,0xFF386020u,0xFF80B070u, /* salt,cotton,sugar,wood,herbs */
        /* minéraux */
        0xFFB87333u,0xFF8090A0u,0xFF303030u,0xFFD8D040u,0xFFC8B090u, /* copper,iron,coal,sulfur,saltpeter */
        0xFFFFD000u,0xFF80E0E0u,                                     /* gold, precious metal */
        /* production */
        0xFFC8B0C0u,0xFF386848u,0xFF902848u,                          /* cloth,naval,wine */
        0xFF60C0C0u,0xFFE8E0F0u,0xFFF0E8D0u,                          /* precious ware,cloth,paper */
    };
    return (r>=0&&r<RES_COUNT)?C[(int)r]:0xFFFF00FFu;
}

/* Palette province : angle d'or en HSL, couleurs carte-like */
static float hue2rgb_f(float p,float q,float t){
    if(t<0.f)t+=1.f;
    if(t>1.f)t-=1.f;
    if(t<1.f/6)return p+(q-p)*6*t;
    if(t<0.5f) return q;
    if(t<2.f/3)return p+(q-p)*(2.f/3-t)*6;
    return p;
}
uint32_t province_palette(int id) {
    float h=fmodf(id*137.508f,360.f)/360.f;
    float s=0.42f+0.14f*sinf((float)id*0.53f+1.f);
    float l=0.50f+0.10f*cosf((float)id*0.71f+2.f);
    float q=(l<0.5f)?l*(1+s):l+s-l*s, p=2*l-q;
    uint8_t r=(uint8_t)(hue2rgb_f(p,q,h+1.f/3)*255);
    uint8_t g=(uint8_t)(hue2rgb_f(p,q,h      )*255);
    uint8_t bv=(uint8_t)(hue2rgb_f(p,q,h-1.f/3)*255);
    return 0xFF000000u|((uint32_t)r<<16)|((uint32_t)g<<8)|bv;
}
