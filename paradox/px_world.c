/*
 * px_world.c — pipeline de génération de monde en 4+N couches
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
#include "px_world.h"
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
        g_plates[i].cx = rng_f()*PX_W;
        g_plates[i].cy = rng_f()*PX_H;
        g_plates[i].oceanic = (rng_f()<0.38f)?1:0;
        float a = rng_f()*6.2832f;
        g_plates[i].dx = cosf(a);
        g_plates[i].dy = sinf(a);
    }
}

/* Score de frontière [0..1] et indices des deux plaques les plus proches */
static float plate_boundary(int px, int py, int *pa, int *pb) {
    float x=(float)px, y=(float)py;
    float d1=1e30f, d2=1e30f;
    *pa=0; *pb=1;
    for (int i=0;i<N_PLATES;i++) {
        float dx=x-g_plates[i].cx, dy=y-g_plates[i].cy;
        float d=sqrtf(dx*dx+dy*dy);
        if (d<d1){d2=d1;*pb=*pa;d1=d;*pa=i;}
        else if(d<d2){d2=d;*pb=i;}
    }
    float r=sqrtf((float)(PX_W*PX_H)/N_PLATES);
    return 1.f - clampf((d2-d1)/(r*0.28f),0.f,1.f);
}

static void step_geology(float *height, float seed_f) {
    plates_init();
    for (int y=0;y<PX_H;y++) for (int x=0;x<PX_W;x++) {
        float nx=(float)x/PX_W, ny=(float)y/PX_H;
        float lat=fabsf(ny-0.5f)*2.f;
        float base = stb_perlin_fbm_noise3(nx*4.f,ny*3.2f,seed_f,2.f,0.5f,7);
        height[px_idx(x,y)] = base - 0.20f*lat*lat;
    }
    /* Frontières de plaques → chaînes de montagnes */
    for (int y=0;y<PX_H;y++) for (int x=0;x<PX_W;x++) {
        int pa,pb;
        float bs=plate_boundary(x,y,&pa,&pb);
        if (bs<0.04f) continue;
        float dot=g_plates[pa].dx*g_plates[pb].dx+g_plates[pa].dy*g_plates[pb].dy;
        float conv=(1.f-dot)*0.5f;
        float bump=0.f;
        if (!g_plates[pa].oceanic && !g_plates[pb].oceanic) bump=bs*conv*0.80f;
        else if (g_plates[pa].oceanic != g_plates[pb].oceanic) bump=bs*conv*0.45f;
        if (bump>0.f) {
            float nx2=(float)x/PX_W, ny2=(float)y/PX_H;
            float r=stb_perlin_ridge_noise3(nx2*10.f,ny2*8.f,seed_f+50.f,2.f,0.5f,1.f,5);
            height[px_idx(x,y)] += bump*(0.5f+0.5f*r);
        }
    }
    normalize_f(height,PX_N);
}

/* ========================================================================
 * COUCHE 2 — ARCHITECTURE
 * Crêtes, falaises, vallées encaissées
 * ====================================================================== */
static void step_architecture(float *height, float seed_f) {
    for (int y=0;y<PX_H;y++) for (int x=0;x<PX_W;x++) {
        float nx=(float)x/PX_W, ny=(float)y/PX_H;
        float h=height[px_idx(x,y)];
        float mtn_frac = clampf((h-0.48f)/0.4f,0.f,1.f);
        float low_frac = clampf((0.62f-h)/0.4f,0.f,1.f);
        float r = stb_perlin_ridge_noise3(nx*12.f,ny*9.f,seed_f+200.f,2.f,0.5f,1.f,5);
        float v = stb_perlin_fbm_noise3  (nx*8.f, ny*6.f,seed_f+300.f,2.f,0.5f,4);
        height[px_idx(x,y)] += r*0.14f*mtn_frac + v*0.07f*low_frac;
    }
    normalize_f(height,PX_N);
}

/* ========================================================================
 * COUCHE 3 — ÉROSION
 * D8 flow + accumulation → rivières + creusement
 * ====================================================================== */
static void step_erosion(float *height, Cell *cells) {
    int8_t *fdir  = (int8_t*)malloc(PX_N*sizeof(int8_t));
    float  *accum = (float *)malloc(PX_N*sizeof(float));
    if (!fdir||!accum) { free(fdir);free(accum);return; }

    /* D8 : direction vers le voisin le plus bas */
    for (int y=0;y<PX_H;y++) for (int x=0;x<PX_W;x++) {
        float h=height[px_idx(x,y)];
        int best=-1; float drop=0.f;
        for (int d=0;d<8;d++) {
            int nx2=x+DDX[d],ny2=y+DDY[d];
            if (nx2<0||nx2>=PX_W||ny2<0||ny2>=PX_H) continue;
            float dh=(h-height[px_idx(nx2,ny2)])/DDIST[d];
            if (dh>drop){drop=dh;best=d;}
        }
        fdir[px_idx(x,y)]=(int8_t)best;
    }

    /* Accumulation de flux (passes amont→aval) */
    for (int i=0;i<PX_N;i++) accum[i]=1.f;
    for (int pass=0;pass<56;pass++)
        for (int y=0;y<PX_H;y++) for (int x=0;x<PX_W;x++) {
            int d=fdir[px_idx(x,y)]; if(d<0)continue;
            int nx2=x+DDX[d],ny2=y+DDY[d];
            if (nx2<0||nx2>=PX_W||ny2<0||ny2>=PX_H)continue;
            accum[px_idx(nx2,ny2)]+=accum[px_idx(x,y)]*0.88f;
        }

    float max_a=1.f;
    for (int i=0;i<PX_N;i++) if(accum[i]>max_a)max_a=accum[i];

    for (int i=0;i<PX_N;i++) {
        cells[i].river=0;
        float a=accum[i]/max_a;
        float rs=0.f;
        if (a>0.003f) rs=clampf(logf(1.f+a*400.f)/logf(401.f),0.f,1.f);
        cells[i].river=(uint8_t)(rs*255.f);
        if (rs>0.06f && height[i]>SEA_LEVEL) height[i]-=rs*0.045f;
    }
    normalize_f(height,PX_N);
    free(fdir); free(accum);
}

/* ========================================================================
 * CLIMAT
 * ====================================================================== */
static void gen_climate(float *height, float *moisture, float *temperature,
                         float seed_f) {
    for (int y=0;y<PX_H;y++) for (int x=0;x<PX_W;x++) {
        float nx=(float)x/PX_W, ny=(float)y/PX_H;
        float lat=fabsf(ny-0.5f)*2.f;
        float h=height[px_idx(x,y)];

        float alt_cold=clampf((h-0.50f)*2.2f,0.f,1.f);
        float tn=stb_perlin_fbm_noise3(nx*4.f,ny*3.f,seed_f+500.f,2.f,0.5f,4)*0.07f;
        temperature[px_idx(x,y)]=clampf(1.f-lat-alt_cold+tn,0.f,1.f);

        float trop   =clampf(1.f-lat*2.6f,0.f,1.f);
        float subtrop=clampf(1.f-fabsf(lat-0.28f)*5.5f,0.f,1.f)*(-0.42f);
        float mn=stb_perlin_fbm_noise3(nx*5.f,ny*4.f,seed_f+700.f,2.f,0.5f,5)*0.18f;
        moisture[px_idx(x,y)]=clampf(0.42f+trop*0.36f+subtrop+mn,0.f,1.f);
    }
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
    for (int y=1;y<PX_H-1;y++) for (int x=1;x<PX_W-1;x++) {
        int i=px_idx(x,y);
        if (height[i]<SEA_LEVEL+0.015f) continue;
        bool dep=true;
        for (int d=0;d<8;d+=2) {
            int nx2=x+DDX[d],ny2=y+DDY[d];
            if (height[px_idx(nx2,ny2)]<height[i]){dep=false;break;}
        }
        if (dep) { cells[i].lake=true; height[i]=SEA_LEVEL+0.005f; }
    }
}

/* ========================================================================
 * FERTILITÉ (couche civilisation)
 * ====================================================================== */
static void compute_fertility(float *height, float *moisture, float *temperature,
                               Cell *cells) {
    /* Proximité de rivière — fenêtre 9×9 */
    float *rprox=(float*)calloc(PX_N,sizeof(float)); if(!rprox)return;
    for (int y=0;y<PX_H;y++) for (int x=0;x<PX_W;x++) {
        float best=0.f;
        for (int dy=-4;dy<=4;dy++) for (int dx=-4;dx<=4;dx++) {
            int nx2=clampi(x+dx,0,PX_W-1), ny2=clampi(y+dy,0,PX_H-1);
            float r=cells[px_idx(nx2,ny2)].river/255.f;
            float dist=sqrtf((float)(dx*dx+dy*dy))+1.f;
            if (r/dist>best) best=r/dist;
        }
        rprox[px_idx(x,y)]=best;
    }

    for (int y=0;y<PX_H;y++) for (int x=0;x<PX_W;x++) {
        int i=px_idx(x,y);
        float h=height[i];
        if (h<SEA_LEVEL){cells[i].fertility=0.f;continue;}
        /* Pente */
        float slope=0.f;
        for (int d=0;d<4;d++) {
            int nx2=clampi(x+DDX[d*2],0,PX_W-1),ny2=clampi(y+DDY[d*2],0,PX_H-1);
            slope+=fabsf(h-height[px_idx(nx2,ny2)]);
        }
        slope/=4.f;
        float t=temperature[i], m=moisture[i];
        float t_score=1.f-fabsf(t-0.55f)*1.9f;
        float f=0.32f*m+0.26f*clampf(t_score,0.f,1.f)
               +0.28f*clampf(rprox[i]*2.8f,0.f,1.f)
               -0.55f*clampf((h-MOUNTAIN_H)/0.18f,0.f,1.f)
               -3.5f*slope;
        cells[i].fertility=clampf(f,0.f,1.f);
    }
    free(rprox);
}

/* ========================================================================
 * PROVINCES — Voronoï domain-warped + coût de terrain
 *
 * Le domain-warping déplace les coordonnées de requête par un champ FBM
 * avant le calcul de distance → frontières organiques, non mécaniques.
 * Le coût de terrain (rivières, montagnes) fait "migrer" naturellement
 * les frontières vers ces obstacles géographiques.
 * ====================================================================== */
#define WARP_STRENGTH 22.f
#define MIN_PROV_DIST 26

static int g_pseedx[PX_MAX_PROV];
static int g_pseedy[PX_MAX_PROV];

static int pick_seeds(Cell *cells, int want) {
    /* Distribution pondérée par la fertilité, avec espacement minimum */
    float total=0.f;
    for (int i=0;i<PX_N;i++) total+=cells[i].fertility;
    if (total<1.f) total=1.f;
    int n=0, tries=0;
    while (n<want && tries<PX_N*4) {
        tries++;
        float r=rng_f()*total;
        float s=0.f; int chosen=0;
        for (int i=0;i<PX_N;i++){s+=cells[i].fertility;if(s>=r){chosen=i;break;}}
        int cx=chosen%PX_W, cy=chosen/PX_W;
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
    int n=pick_seeds(w->cell, PX_MAX_PROV);
    if (n<4) n=4;
    w->n_provinces=n;

    /* Voronoï domain-warped + coût de terrain */
    for (int y=0;y<PX_H;y++) for (int x=0;x<PX_W;x++) {
        int i=px_idx(x,y);
        if (height[i]<SEA_LEVEL){w->cell[i].province=-1;continue;}

        /* Domain warp : décalage FBM des coordonnées de requête */
        float nx=(float)x/PX_W, ny=(float)y/PX_H;
        float wx=stb_perlin_fbm_noise3(nx*3.f+0.f,ny*3.f+0.f,seed_f+10.f,2.f,0.5f,3)*WARP_STRENGTH;
        float wy=stb_perlin_fbm_noise3(nx*3.f+5.2f,ny*3.f+1.3f,seed_f+20.f,2.f,0.5f,3)*WARP_STRENGTH;
        float qx=(float)x+wx, qy=(float)y+wy;

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
    int biome_cnt[PX_MAX_PROV][BIO_COUNT]={0};
    int area[PX_MAX_PROV]={0};
    float lat_s[PX_MAX_PROV]={0};
    float h_s[PX_MAX_PROV]={0};
    for (int y=0;y<PX_H;y++) for (int x=0;x<PX_W;x++) {
        int i=px_idx(x,y); int p=w->cell[i].province;
        if (p<0)continue;
        area[p]++;
        lat_s[p]+=fabsf((float)y/PX_H-0.5f)*2.f;
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
static int g_rseedx[PX_MAX_REG];
static int g_rseedy[PX_MAX_REG];

static void assign_regions(World *w, float *height, float seed_f) {
    /* Germes des régions : un sous-ensemble espacé des germes de provinces */
    int step=w->n_provinces/PX_MAX_REG+1, n=0;
    for (int p=0;p<w->n_provinces&&n<PX_MAX_REG;p+=step) {
        g_rseedx[n]=w->province[p].seed_x;
        g_rseedy[n]=w->province[p].seed_y;
        n++;
    }
    if (n<2) n=2;
    w->n_regions=n;

    /* Domain warp plus faible pour les régions */
    float warp=WARP_STRENGTH*1.8f;
    for (int y=0;y<PX_H;y++) for (int x=0;x<PX_W;x++) {
        int i=px_idx(x,y);
        if (height[i]<SEA_LEVEL){w->cell[i].region=-1;continue;}
        float nx=(float)x/PX_W, ny=(float)y/PX_H;
        float wx=stb_perlin_fbm_noise3(nx*2.f,ny*2.f,seed_f+30.f,2.f,0.5f,3)*warp;
        float wy=stb_perlin_fbm_noise3(nx*2.f+3.7f,ny*2.f+2.1f,seed_f+40.f,2.f,0.5f,3)*warp;
        float qx=(float)x+wx, qy=(float)y+wy;
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
        if (cx<0||cx>=PX_W||cy<0||cy>=PX_H) continue;
        int r=w->cell[px_idx(cx,cy)].region;
        if (r<0) r=0;
        w->province[p].region=(int16_t)r;
        Region *rg=&w->region[r];
        if (rg->n_provinces<PX_MAX_PROV)
            rg->province_ids[rg->n_provinces++]=(int16_t)p;
    }
}

/* ========================================================================
 * FLAGS DE RENDU — côtes, frontières, hillshading (précalculé)
 * ====================================================================== */
static void compute_render_flags(World *w, float *height) {
    /* Côtes : cellule terrestre adjacente à la mer */
    for (int y=0;y<PX_H;y++) for (int x=0;x<PX_W;x++) {
        Cell *c=&w->cell[px_idx(x,y)];
        c->coast=false;
        if (height[px_idx(x,y)]<SEA_LEVEL) continue;
        for (int d=0;d<4;d++) {
            int nx2=clampi(x+DDX[d*2],0,PX_W-1),ny2=clampi(y+DDY[d*2],0,PX_H-1);
            if (height[px_idx(nx2,ny2)]<SEA_LEVEL){c->coast=true;break;}
        }
    }

    /* Frontières (compare province/region avec voisins E et S) */
    for (int y=0;y<PX_H-1;y++) for (int x=0;x<PX_W-1;x++) {
        Cell *c  =&w->cell[px_idx(x,y)];
        Cell *ce =&w->cell[px_idx(x+1,y)];
        Cell *cs =&w->cell[px_idx(x,y+1)];
        c->border_prov=(c->province!=ce->province && (c->province>=0||ce->province>=0))
                      ||(c->province!=cs->province && (c->province>=0||cs->province>=0));
        c->border_reg =(c->region!=ce->region && (c->region>=0||ce->region>=0))
                      ||(c->region!=cs->region && (c->region>=0||cs->region>=0));
    }

    /* Hillshading — lumière NW (convention cartographique standard)
     * Normale de surface calculée depuis les gradients de hauteur. */
    static const float LX=-0.6f, LY=-0.6f, LZ=0.5f; /* direction lumière (normalisée) */
    static const float LLEN=0.9165f;                  /* ||L|| */
    for (int y=0;y<PX_H;y++) for (int x=0;x<PX_W;x++) {
        int i=px_idx(x,y);
        float he=height[px_idx(clampi(x+1,0,PX_W-1),y)];
        float hw=height[px_idx(clampi(x-1,0,PX_W-1),y)];
        float hs=height[px_idx(x,clampi(y+1,0,PX_H-1))];
        float hn=height[px_idx(x,clampi(y-1,0,PX_H-1))];
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
    int famx[3]={PX_W/6, PX_W/2, PX_W*5/6};
    int famy[3]={PX_H/2, PX_H/4, PX_H*3/4};
    float maxr=sqrtf((float)(PX_W*PX_W+PX_H*PX_H))/2.f;

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
    for (int y=3;y<PX_H-3&&n<PX_MAX_RIVERS;y+=5)
    for (int x=3;x<PX_W-3&&n<PX_MAX_RIVERS;x+=5) {
        int i=px_idx(x,y);
        if (height[i]<MOUNTAIN_H-0.08f) continue;
        if (w->cell[i].river<50) continue;

        River *rv=&w->river[n];
        rv->len=0; rv->flow_max=0.f;
        bool *seen=(bool*)calloc(PX_N,sizeof(bool));
        if (!seen) break;

        int cx=x,cy=y;
        for (int s=0;s<PX_RIVER_MAXLEN;s++) {
            if (cx<0||cx>=PX_W||cy<0||cy>=PX_H) break;
            int ci=px_idx(cx,cy);
            if (seen[ci]) break;
            seen[ci]=true;
            rv->x[rv->len]=(int16_t)cx;
            rv->y[rv->len]=(int16_t)cy;
            rv->len++;
            float fl=w->cell[ci].river/255.f;
            if (fl>rv->flow_max) rv->flow_max=fl;
            if (height[ci]<SEA_LEVEL) break;
            int dir=(int)w->cell[ci].river; /* reuse: flow_dir stored elsewhere? */
            /* Cherche le voisin le plus bas */
            float mh=height[ci]; int best=-1;
            for (int d=0;d<8;d++){
                int nx2=cx+DDX[d],ny2=cy+DDY[d];
                if (nx2<0||nx2>=PX_W||ny2<0||ny2>=PX_H)continue;
                if (height[px_idx(nx2,ny2)]<mh){mh=height[px_idx(nx2,ny2)];best=d;}
            }
            if (best<0) break;
            cx+=DDX[best]; cy+=DDY[best];
            (void)dir;
        }
        free(seen);
        if (rv->len>10) n++;
    }
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

    float *height =  (float*)malloc(PX_N*sizeof(float));
    float *moisture= (float*)malloc(PX_N*sizeof(float));
    float *temp    = (float*)malloc(PX_N*sizeof(float));
    if (!height||!moisture||!temp){fprintf(stderr,"paradox: OOM\n");goto end;}

    printf("[paradox] géologie...     "); fflush(stdout);
    step_geology(height,seed_f);          printf("ok\n");

    printf("[paradox] architecture... "); fflush(stdout);
    step_architecture(height,seed_f);     printf("ok\n");

    printf("[paradox] érosion...      "); fflush(stdout);
    step_erosion(height,w->cell);         printf("ok\n");

    printf("[paradox] climat...       "); fflush(stdout);
    gen_climate(height,moisture,temp,seed_f); printf("ok\n");

    printf("[paradox] biomes...       "); fflush(stdout);
    for (int i=0;i<PX_N;i++) {
        w->cell[i].height     =height[i];
        w->cell[i].moisture   =moisture[i];
        w->cell[i].temperature=temp[i];
        w->cell[i].biome=assign_biome(height[i],moisture[i],temp[i]);
    }
    printf("ok\n");

    fill_lakes(height,w->cell);
    for (int i=0;i<PX_N;i++) w->cell[i].height=height[i];

    printf("[paradox] fertilité...    "); fflush(stdout);
    compute_fertility(height,moisture,temp,w->cell); printf("ok\n");

    printf("[paradox] provinces...    "); fflush(stdout);
    assign_provinces(w,height,seed_f);
    printf("ok (%d prov.)\n",w->n_provinces);

    printf("[paradox] régions...      "); fflush(stdout);
    assign_regions(w,height,seed_f);
    printf("ok (%d rég.)\n",w->n_regions);

    printf("[paradox] flags rendu...  "); fflush(stdout);
    compute_render_flags(w,height);       printf("ok\n");

    printf("[paradox] SCPS...         "); fflush(stdout);
    gen_scps(w);                          printf("ok\n");

    printf("[paradox] rivières...     "); fflush(stdout);
    trace_rivers(w,height);
    printf("ok (%d riv.)\n",w->n_rivers);

end:
    free(height); free(moisture); free(temp);
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
