/*
 * batch.c — planche-contact de N mondes (sans SDL)
 *
 *   make scps_batch && ./scps_batch [graine_base] [n]
 *
 * Génère n mondes (5 par défaut) à partir de graines consécutives et les
 * assemble en UNE image BMP (montage.bmp). Pour chaque monde : une ligne de
 * deux vignettes côte à côte — terrain et politique (territoires) — afin de
 * juger d'un coup d'œil le relief ET le découpage.
 *
 * Le BMP (24 bits non compressé) est choisi car visualisable partout sans
 * dépendance, contrairement au PPM.
 */
#include "scps_world.h"
#include "scps_render.h"
#include "stb_image_write.h"   /* PNG (vendoré) — montage.png, ~10× plus léger que le BMP */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define GAP   6            /* gouttière entre vignettes (px) */
#define BG    0x202428u    /* fond ardoise */

static void put_le32(unsigned char *p, uint32_t v) {
    p[0]=v&0xFF; p[1]=(v>>8)&0xFF; p[2]=(v>>16)&0xFF; p[3]=(v>>24)&0xFF;
}

/* Écrit une image ARGB (W×H) en BMP 24 bits. */
static void write_bmp(const char *path, const uint32_t *px, int w, int h) {
    int rowbytes = w*3;
    int pad = (4 - (rowbytes & 3)) & 3;     /* lignes alignées sur 4 octets */
    int stride = rowbytes + pad;
    uint32_t datasize = (uint32_t)stride*h;
    uint32_t filesize = 54 + datasize;

    FILE *f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "écriture %s impossible\n", path); return; }

    unsigned char hd[54]={0};
    hd[0]='B'; hd[1]='M';
    put_le32(hd+2, filesize);
    put_le32(hd+10, 54);                     /* offset des pixels */
    put_le32(hd+14, 40);                     /* taille DIB header */
    put_le32(hd+18, (uint32_t)w);
    put_le32(hd+22, (uint32_t)h);
    hd[26]=1;                                /* plans */
    hd[28]=24;                               /* bpp */
    put_le32(hd+34, datasize);
    fwrite(hd,1,54,f);

    unsigned char *row=(unsigned char*)malloc(stride);
    memset(row,0,stride);
    for (int y=h-1; y>=0; y--) {             /* BMP : lignes du bas vers le haut */
        for (int x=0;x<w;x++) {
            uint32_t c=px[y*w+x];
            row[x*3+0]=(unsigned char)(c & 0xFF);          /* B */
            row[x*3+1]=(unsigned char)((c>>8) & 0xFF);     /* G */
            row[x*3+2]=(unsigned char)((c>>16) & 0xFF);    /* R */
        }
        fwrite(row,1,stride,f);
    }
    free(row);
    fclose(f);
    printf("  écrit %s (%dx%d)\n", path, w, h);
}

/* Recopie une vignette w×h dans la grande image à (ox,oy). */
static void blit(uint32_t *dst, int DW, const uint32_t *src, int w, int h,
                 int ox, int oy) {
    for (int y=0;y<h;y++) for (int x=0;x<w;x++)
        dst[(oy+y)*DW + (ox+x)] = src[y*w+x];
}

int main(int argc, char **argv) {
    uint32_t base = (argc>1) ? (uint32_t)strtoul(argv[1],NULL,10)
                             : (uint32_t)time(NULL);
    int n = (argc>2) ? atoi(argv[2]) : 5;
    if (n<1) n=1;
    if (n>12) n=12;
    /* argv[3] optionnel : âge du monde [0..1] = dérive des plaques
     * (0 = supercontinent non dérivé ; 1 = continents dispersés). */
    float age = (argc>3) ? (float)atof(argv[3]) : -1.f;

    int VW=SCPS_W, VH=SCPS_H;                 /* taille d'une vignette */
    int DW = VW*2 + GAP*3;                    /* 2 colonnes + gouttières */
    int DH = n*VH + (n+1)*GAP;                /* n lignes */

    uint32_t *big = (uint32_t*)malloc((size_t)DW*DH*4);
    uint32_t *tile= (uint32_t*)malloc((size_t)VW*VH*4);
    World    *w   = (World*)malloc(sizeof(World));
    if (!big||!tile||!w){ fprintf(stderr,"OOM\n"); return 1; }
    for (int i=0;i<DW*DH;i++) big[i]=0xFF000000u|BG;

    RenderParams rp = {
        .cam_ox=0.f, .cam_oy=0.f, .cam_scale=1.f, .selected_prov=-1,
        .show_rivers=true, .show_borders=true, .show_grid=false
    };

    for (int k=0;k<n;k++) {
        uint32_t seed = base + (uint32_t)k;
        WorldParams p = worldparams_default(seed);
        if (age>=0.f) p.world_age = age;        /* dérive imposée (test causal §5) */
        world_generate(w, &p);
        int oy = GAP + k*(VH+GAP);

        render_map(w, tile, VW, VH, &rp, VIEW_TERRAIN);
        blit(big, DW, tile, VW, VH, GAP, oy);

        render_map(w, tile, VW, VH, &rp, VIEW_POLITICAL);
        blit(big, DW, tile, VW, VH, GAP*2+VW, oy);

        printf("[batch] monde %d/%d  graine %u  (%d terr. %d rég. %d pays %d cont.)\n",
               k+1, n, seed, w->n_provinces, w->n_regions,
               w->n_countries, w->n_continents);
    }

    /* PNG d'abord (léger, partout lisible) ; le BMP reste le FILET DE SÉCURITÉ
     * si l'écriture PNG échoue (disque, droits). On convertit l'ARGB → RGB
     * top-down (l'ordre que stb attend). */
    { unsigned char *rgb = (unsigned char*)malloc((size_t)DW*DH*3);
      if (rgb){
          for (int i=0;i<DW*DH;i++){
              uint32_t c=big[i];
              rgb[i*3+0]=(unsigned char)((c>>16)&0xFF);
              rgb[i*3+1]=(unsigned char)((c>>8)&0xFF);
              rgb[i*3+2]=(unsigned char)(c&0xFF);
          }
          if (stbi_write_png("montage.png", DW, DH, 3, rgb, DW*3))
              printf("[batch] écrit montage.png (%dx%d)\n", DW, DH);
          else { write_bmp("montage.bmp", big, DW, DH);
                 printf("[batch] PNG indisponible → repli montage.bmp\n"); }
          free(rgb);
      } else write_bmp("montage.bmp", big, DW, DH);   /* OOM : repli BMP */
    }
    free(big); free(tile); free(w);
    return 0;
}
