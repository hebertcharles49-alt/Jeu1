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
    /* Warp dédié aux plaques — fréquence plus basse que celui des provinces */
    float wx=stb_perlin_fbm_noise3(nx*1.5f+0.f,ny*1.5f+0.f,seed_f+800.f,2.f,0.5f,4)*28.f;
    float wy=stb_perlin_fbm_noise3(nx*1.5f+6.1f,ny*1.5f+3.4f,seed_f+810.f,2.f,0.5f,4)*28.f;
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

static void step_geology(float *height, float seed_f) {
    plates_init();
    for (int y=0;y<SCPS_H;y++) for (int x=0;x<SCPS_W;x++) {
        float nx=(float)x/SCPS_W, ny=(float)y/SCPS_H;
        float lat=fabsf(ny-0.5f)*2.f;
        float base = stb_perlin_fbm_noise3(nx*4.f,ny*3.2f,seed_f,2.f,0.5f,7);
        height[scps_idx(x,y)] = base - 0.20f*lat*lat;
    }
    /* Frontières de plaques → chaînes de montagnes */
    for (int y=0;y<SCPS_H;y++) for (int x=0;x<SCPS_W;x++) {
        int pa,pb;
        float bs=plate_boundary(x,y,&pa,&pb,seed_f);
        if (bs<0.04f) continue;
        float dot=g_plates[pa].dx*g_plates[pb].dx+g_plates[pa].dy*g_plates[pb].dy;
        float conv=(1.f-dot)*0.5f;
        float bump=0.f;
        if (!g_plates[pa].oceanic && !g_plates[pb].oceanic) bump=bs*conv*0.80f;
        else if (g_plates[pa].oceanic != g_plates[pb].oceanic) bump=bs*conv*0.45f;
        if (bump>0.f) {
            float nx2=(float)x/SCPS_W, ny2=(float)y/SCPS_H;
            float r=stb_perlin_ridge_noise3(nx2*10.f,ny2*8.f,seed_f+50.f,2.f,0.5f,1.f,5);
            height[scps_idx(x,y)] += bump*(0.5f+0.5f*r);
        }
    }
    normalize_f(height,SCPS_N);
}

/* ========================================================================
 * COUCHE 2 — ARCHITECTURE
 * Crêtes, falaises, vallées encaissées
 * ====================================================================== */
static void step_architecture(float *height, float seed_f) {
    for (int y=0;y<SCPS_H;y++) for (int x=0;x<SCPS_W;x++) {
        float nx=(float)x/SCPS_W, ny=(float)y/SCPS_H;
        float h=height[scps_idx(x,y)];
        float mtn_frac = clampf((h-0.48f)/0.4f,0.f,1.f);
        float low_frac = clampf((0.62f-h)/0.4f,0.f,1.f);
        float r = stb_perlin_ridge_noise3(nx*12.f,ny*9.f,seed_f+200.f,2.f,0.5f,1.f,5);
        float v = stb_perlin_fbm_noise3  (nx*8.f, ny*6.f,seed_f+300.f,2.f,0.5f,4);
        height[scps_idx(x,y)] += r*0.14f*mtn_frac + v*0.07f*low_frac;
    }
    normalize_f(height,SCPS_N);
}

/* ========================================================================
 * COUCHE 3 — ÉROSION
 * D8 flow + accumulation → rivières + creusement
 * ====================================================================== */
static void step_erosion(float *height, Cell *cells) {
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

    for (int i=0;i<SCPS_N;i++) {
        cells[i].flow_dir=fdir[i];          /* conservé pour le tracé aval */
        /* Échelle log : un fleuve de 5000 cellules amont reste lisible face
         * à un ruisseau de 5. */
        float rs=logf(1.f+accum[i])/lmax;
        cells[i].river=(uint8_t)(clampf(rs,0.f,1.f)*255.f);
        if (rs>0.45f && height[i]>SEA_LEVEL) height[i]-=(rs-0.45f)*0.06f; /* creuse le lit */
    }
    normalize_f(height,SCPS_N);
    free(fdir); free(accum);
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
                        float *temperature, const float *odist, float seed_f) {
    Cell *cells = w->cell;

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
        float cont_heat = odist[i]*(1.f-lat)*0.10f;  /* déserts continentaux brûlants */
        temperature[i]=clampf(1.f-lat-alt_cold+cont_heat+t_cont+t_reg+t_loc,0.f,1.f);
    }

    /* ---- 2. Advection d'humidité ----------------------------------- */
    float *rain=(float*)calloc(SCPS_N,sizeof(float));
    if(!rain) return;
    const float HUM_CAP=1.0f, EVAP=0.16f, RAIN_K=0.05f, ORO_K=4.5f;

    for (int y=0;y<SCPS_H;y++) {
        float lat=fabsf((float)y/SCPS_H-0.5f)*2.f;
        int dir=wind_dir_x(lat);
        float humidity=0.f;
        /* Deux balayages : le 1er amorce l'humidité (air venu du large par
         * enroulement), le 2nd enregistre la précipitation. */
        for (int pass=0;pass<2;pass++)
        for (int k=0;k<SCPS_W;k++) {
            int x = (dir>0) ? k : (SCPS_W-1-k);
            int i=scps_idx(x,y);
            float h=height[i];
            if (h<SEA_LEVEL) {
                /* Océan : évaporation (eau chaude → plus de vapeur) */
                float t=temperature[i];
                humidity += EVAP*(0.4f+0.6f*t)*(HUM_CAP-humidity);
                if(humidity>HUM_CAP)humidity=HUM_CAP;
            } else {
                int xu=clampi(x-dir,0,SCPS_W-1);
                float rise=h-height[scps_idx(xu,y)];
                if(rise<0.f)rise=0.f;
                float oro = humidity*rise*ORO_K;   /* soulèvement orographique */
                float base= humidity*RAIN_K;        /* pluie de fond */
                float p=base+oro; if(p>humidity)p=humidity;
                humidity-=p;
                if(pass==1) rain[i]=p;
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
        float fbm=stb_perlin_fbm_noise3(nx*4.f,ny*3.f,seed_f+700.f,2.f,0.5f,4)*0.08f;

        float m = 0.08f
                + 0.48f*rain[i]      /* advection : continentalité + ombre pluvio. */
                + 0.22f*tropical     /* pluies de convection tropicale */
                + 0.20f*coastal      /* proximité de l'océan */
                + fbm;

        /* Corridor riparien : "pas d'eau sans montagne" — le fleuve né en
         * altitude verdit sa vallée jusque dans le désert (effet Nil). */
        m += (cells[i].river/255.f)*0.30f;

        moisture[i]=clampf(m,0.f,1.f);
    }
    free(rain);
}

/* ========================================================================
 * BIOMES (Whittaker adapté fantasy)
 * ====================================================================== */
static Biome assign_biome(float h, float m, float t) {
    if (h<SEA_LEVEL-0.14f) return BIO_DEEP_OCEAN;
    if (h<SEA_LEVEL-0.04f) return BIO_OCEAN;
    if (h<SEA_LEVEL)       return BIO_SHALLOW;
    if (h<SEA_LEVEL+0.025f) return BIO_COAST;
    if (h>=PEAK_H)          return (t<0.16f)?BIO_GLACIER:BIO_PEAK;
    if (h>=MOUNTAIN_H)      return BIO_MOUNTAINS;
    if (h>=MOUNTAIN_H-0.09f)return (t<0.30f)?BIO_HIGHLANDS:BIO_HILLS;
    if (t<0.17f)  return (m>0.40f)?BIO_FOREST:BIO_GLACIER;
    if (t<0.33f) {
        if (m>0.58f) return BIO_FOREST;
        if (m>0.35f) return BIO_WOODS;
        return BIO_STEPPE;
    }
    if (t<0.52f) {
        if (m>0.66f) return BIO_FOREST;
        if (m>0.48f) return BIO_WOODS;
        if (m>0.32f) return BIO_GRASSLAND;
        if (m>0.16f) return BIO_PLAINS;
        return BIO_STEPPE;
    }
    if (t<0.70f) {
        if (m>0.62f) return (h<SEA_LEVEL+0.07f)?BIO_MARSH:BIO_JUNGLE;
        if (m>0.42f) return BIO_FARMLAND;
        if (m>0.26f) return BIO_SAVANNA;
        if (m>0.12f) return BIO_DRYLANDS;
        return (h<SEA_LEVEL+0.06f)?BIO_COASTAL_DESERT:BIO_DESERT;
    }
    if (m>0.66f) return BIO_JUNGLE;
    if (m>0.40f) return BIO_SAVANNA;
    if (m>0.18f) return BIO_DRYLANDS;
    return (h<SEA_LEVEL+0.06f)?BIO_COASTAL_DESERT:BIO_DESERT;
}

/* ========================================================================
 * LACS
 * ====================================================================== */
static void fill_lakes(float *height, Cell *cells) {
    for (int y=1;y<SCPS_H-1;y++) for (int x=1;x<SCPS_W-1;x++) {
        int i=scps_idx(x,y);
        if (height[i]<SEA_LEVEL+0.015f) continue;
        bool dep=true;
        for (int d=0;d<8;d+=2) {
            int nx2=x+DDX[d],ny2=y+DDY[d];
            if (height[scps_idx(nx2,ny2)]<height[i]){dep=false;break;}
        }
        if (dep) { cells[i].lake=true; height[i]=SEA_LEVEL+0.005f; }
    }
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
               -3.5f*slope;
        cells[i].fertility=clampf(f,0.f,1.f);
    }
    free(irrig); free(delta);
}

/* ========================================================================
 * PROVINCES — Voronoï double domain-warped + coût de terrain
 *
 * Double domain-warp (technique Inigo Quilez) :
 *   q = warp1(p)         — grandes sinuosités
 *   r = warp2(q)         — enroule les sinuosités sur elles-mêmes
 *   distance(r, seed)    — Voronoï sur les coordonnées doublement warpées
 * → frontières vraiment organiques, sans aucune droite résiduelle.
 * Le coût de terrain fait converger les frontières vers les obstacles.
 * ====================================================================== */
#define WARP1         18.f   /* amplitude 1er warp (grandes déformations) */
#define WARP2         10.f   /* amplitude 2e warp  (sinuosités fines)     */
#define MIN_PROV_DIST 26

static int g_pseedx[SCPS_MAX_PROV];
static int g_pseedy[SCPS_MAX_PROV];

static int pick_seeds(Cell *cells, int want) {
    /* Distribution pondérée par la fertilité, avec espacement minimum */
    float total=0.f;
    for (int i=0;i<SCPS_N;i++) total+=cells[i].fertility;
    if (total<1.f) total=1.f;
    int n=0, tries=0;
    while (n<want && tries<SCPS_N*4) {
        tries++;
        float r=rng_f()*total;
        float s=0.f; int chosen=0;
        for (int i=0;i<SCPS_N;i++){s+=cells[i].fertility;if(s>=r){chosen=i;break;}}
        int cx=chosen%SCPS_W, cy=chosen/SCPS_W;
        bool ok=true;
        for (int k=0;k<n&&ok;k++){
            int dx=cx-g_pseedx[k],dy=cy-g_pseedy[k];
            if (dx*dx+dy*dy<MIN_PROV_DIST*MIN_PROV_DIST) ok=false;
        }
        if (ok){g_pseedx[n]=cx;g_pseedy[n]=cy;n++;}
    }
    return n;
}

static float terrain_cost(const Cell *c) {
    float cost=1.f;
    if (c->river>90)  cost+=2.8f*(c->river/255.f);
    if (c->height>MOUNTAIN_H)       cost+=5.f;
    else if (c->height>MOUNTAIN_H-0.09f) cost+=2.5f;
    return cost;
}

static void assign_provinces(World *w, float *height, float seed_f) {
    int n=pick_seeds(w->cell, SCPS_MAX_PROV);
    if (n<4) n=4;
    w->n_provinces=n;

    /* Voronoï domain-warped + coût de terrain */
    for (int y=0;y<SCPS_H;y++) for (int x=0;x<SCPS_W;x++) {
        int i=scps_idx(x,y);
        if (height[i]<SEA_LEVEL){w->cell[i].province=-1;continue;}

        /* Double domain warp -------------------------------------------- */
        float nx=(float)x/SCPS_W, ny=(float)y/SCPS_H;
        /* Passage 1 : grandes déformations */
        float wx1=stb_perlin_fbm_noise3(nx*2.5f+0.0f,ny*2.5f+0.0f,seed_f+10.f,2.f,0.5f,4)*WARP1;
        float wy1=stb_perlin_fbm_noise3(nx*2.5f+5.2f,ny*2.5f+1.3f,seed_f+20.f,2.f,0.5f,4)*WARP1;
        /* Passage 2 : warp du warp → sinuosités enroulées */
        float px2=(nx+wx1/SCPS_W)*3.f, py2=(ny+wy1/SCPS_H)*3.f;
        float wx2=stb_perlin_fbm_noise3(px2+8.3f,py2+2.8f,seed_f+30.f,2.f,0.5f,3)*WARP2;
        float wy2=stb_perlin_fbm_noise3(px2+3.7f,py2+9.1f,seed_f+40.f,2.f,0.5f,3)*WARP2;
        float qx=(float)x+wx1+wx2, qy=(float)y+wy1+wy2;

        float cost=terrain_cost(&w->cell[i]);
        float best=1e30f; int bestp=0;
        for (int p=0;p<n;p++) {
            float dx=qx-g_pseedx[p], dy=qy-g_pseedy[p];
            float d=(dx*dx+dy*dy)*cost;
            if (d<best){best=d;bestp=p;}
        }
        w->cell[i].province=(int16_t)bestp;
    }

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
 * RÉGIONS — Voronoï de second niveau
 * ====================================================================== */
static int g_rseedx[SCPS_MAX_REG];
static int g_rseedy[SCPS_MAX_REG];

static void assign_regions(World *w, float *height, float seed_f) {
    /* Germes des régions : un sous-ensemble espacé des germes de provinces */
    int step=w->n_provinces/SCPS_MAX_REG+1, n=0;
    for (int p=0;p<w->n_provinces&&n<SCPS_MAX_REG;p+=step) {
        g_rseedx[n]=w->province[p].seed_x;
        g_rseedy[n]=w->province[p].seed_y;
        n++;
    }
    if (n<2) n=2;
    w->n_regions=n;

    /* Double warp pour les régions (échelle plus grande) */
    for (int y=0;y<SCPS_H;y++) for (int x=0;x<SCPS_W;x++) {
        int i=scps_idx(x,y);
        if (height[i]<SEA_LEVEL){w->cell[i].region=-1;continue;}
        float nx=(float)x/SCPS_W, ny=(float)y/SCPS_H;
        float rw1=WARP1*1.6f, rw2=WARP2*1.4f;
        float wx1=stb_perlin_fbm_noise3(nx*2.f+0.f,ny*2.f+0.f,seed_f+50.f,2.f,0.5f,4)*rw1;
        float wy1=stb_perlin_fbm_noise3(nx*2.f+7.3f,ny*2.f+3.9f,seed_f+60.f,2.f,0.5f,4)*rw1;
        float px2=(nx+wx1/SCPS_W)*2.5f, py2=(ny+wy1/SCPS_H)*2.5f;
        float wx2=stb_perlin_fbm_noise3(px2+4.1f,py2+6.8f,seed_f+70.f,2.f,0.5f,3)*rw2;
        float wy2=stb_perlin_fbm_noise3(px2+9.5f,py2+1.2f,seed_f+80.f,2.f,0.5f,3)*rw2;
        float qx=(float)x+wx1+wx2, qy=(float)y+wy1+wy2;
        float best=1e30f; int bestr=0;
        for (int r=0;r<n;r++) {
            float dx=qx-g_rseedx[r],dy=qy-g_rseedy[r];
            float d=dx*dx+dy*dy;
            if (d<best){best=d;bestr=r;}
        }
        w->cell[i].region=(int16_t)bestr;
    }

    /* Initialiser régions */
    for (int r=0;r<n;r++) {
        w->region[r].seed_x=g_rseedx[r];
        w->region[r].seed_y=g_rseedy[r];
        w->region[r].n_provinces=0;
        w->region[r].color=province_palette(r*7+3);
        snprintf(w->region[r].name,sizeof(w->region[r].name),"Région %d",r+1);
    }

    /* Affecter les provinces aux régions */
    for (int p=0;p<w->n_provinces;p++) {
        int cx=w->province[p].seed_x, cy=w->province[p].seed_y;
        if (cx<0||cx>=SCPS_W||cy<0||cy>=SCPS_H) continue;
        int r=w->cell[scps_idx(cx,cy)].region;
        if (r<0) r=0;
        w->province[p].region=(int16_t)r;
        Region *rg=&w->region[r];
        if (rg->n_provinces<SCPS_MAX_PROV)
            rg->province_ids[rg->n_provinces++]=(int16_t)p;
    }
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

    /* Frontières (compare province/region avec voisins E et S) */
    for (int y=0;y<SCPS_H-1;y++) for (int x=0;x<SCPS_W-1;x++) {
        Cell *c  =&w->cell[scps_idx(x,y)];
        Cell *ce =&w->cell[scps_idx(x+1,y)];
        Cell *cs =&w->cell[scps_idx(x,y+1)];
        c->border_prov=(c->province!=ce->province && (c->province>=0||ce->province>=0))
                      ||(c->province!=cs->province && (c->province>=0||cs->province>=0));
        c->border_reg =(c->region!=ce->region && (c->region>=0||ce->region>=0))
                      ||(c->region!=cs->region && (c->region>=0||cs->region>=0));
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
 * POINT D'ENTRÉE
 * ====================================================================== */
void world_generate(World *w, uint32_t seed) {
    memset(w,0,sizeof(*w));
    w->seed=seed;
    rng_seed(seed);
    float seed_f=(float)(seed&0xFFFF)/(float)0x10000;

    float *height =  (float*)malloc(SCPS_N*sizeof(float));
    float *moisture= (float*)malloc(SCPS_N*sizeof(float));
    float *temp    = (float*)malloc(SCPS_N*sizeof(float));
    float *odist   = (float*)malloc(SCPS_N*sizeof(float));
    if (!height||!moisture||!temp||!odist){fprintf(stderr,"scps: OOM\n");goto end;}

    printf("[scps] géologie...     "); fflush(stdout);
    step_geology(height,seed_f);          printf("ok\n");

    printf("[scps] architecture... "); fflush(stdout);
    step_architecture(height,seed_f);     printf("ok\n");

    printf("[scps] érosion...      "); fflush(stdout);
    step_erosion(height,w->cell);         printf("ok\n");

    printf("[scps] continentalité..."); fflush(stdout);
    compute_ocean_distance(height,odist);  printf("ok\n");

    printf("[scps] climat (vent)... "); fflush(stdout);
    gen_climate(w,height,moisture,temp,odist,seed_f); printf("ok\n");

    printf("[scps] biomes...       "); fflush(stdout);
    /* Jitter haute fréquence sur t et m pour briser les lignes de seuil */
    for (int y=0;y<SCPS_H;y++) for (int x=0;x<SCPS_W;x++) {
        int i=scps_idx(x,y);
        float nx2=(float)x/SCPS_W, ny2=(float)y/SCPS_H;
        float jt=stb_perlin_noise3(nx2*16.f,ny2*15.f,seed_f+900.f,0,0,0)*0.045f;
        float jm=stb_perlin_noise3(nx2*15.f,ny2*16.f,seed_f+901.f,0,0,0)*0.035f;
        w->cell[i].height     =height[i];
        w->cell[i].moisture   =moisture[i];
        w->cell[i].temperature=temp[i];
        w->cell[i].biome=assign_biome(height[i],moisture[i]+jm,temp[i]+jt);
    }
    printf("ok\n");

    fill_lakes(height,w->cell);
    for (int i=0;i<SCPS_N;i++) w->cell[i].height=height[i];

    printf("[scps] fertilité...    "); fflush(stdout);
    compute_fertility(height,moisture,temp,w->cell); printf("ok\n");

    printf("[scps] provinces...    "); fflush(stdout);
    assign_provinces(w,height,seed_f);
    printf("ok (%d prov.)\n",w->n_provinces);

    printf("[scps] régions...      "); fflush(stdout);
    assign_regions(w,height,seed_f);
    printf("ok (%d rég.)\n",w->n_regions);

    printf("[scps] flags rendu...  "); fflush(stdout);
    compute_render_flags(w,height);       printf("ok\n");

    printf("[scps] SCPS...         "); fflush(stdout);
    gen_scps(w);                          printf("ok\n");

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
    };
    return (b>=0&&b<BIO_COUNT)?N[(int)b]:"?";
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
