/*
 * dump.c — générateur d'images headless (sans SDL)
 * Vérifie la génération en écrivant des PPM des différentes vues.
 *
 *   make scps_dump && ./scps_dump <graine>
 *
 * Réutilise scps_world + scps_render (render_map n'a aucune dépendance SDL :
 * il écrit dans un buffer ARGB que l'on sérialise en PPM).
 */
#include "scps_world.h"
#include "scps_render.h"
#include "scps_econ.h"    /* econ_init + gen_population → histogramme subsistance */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void write_ppm(const char *path, const uint32_t *px, int w, int h) {
    FILE *f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "écriture %s impossible\n", path); return; }
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int i = 0; i < w*h; i++) {
        uint32_t c = px[i];
        unsigned char rgb[3] = {
            (unsigned char)((c >> 16) & 0xFF),
            (unsigned char)((c >>  8) & 0xFF),
            (unsigned char)((c      ) & 0xFF)
        };
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
    printf("  écrit %s\n", path);
}

int main(int argc, char **argv) {
    uint32_t seed = (argc > 1) ? (uint32_t)strtoul(argv[1], NULL, 10)
                               : (uint32_t)time(NULL);

    World *w = (World*)malloc(sizeof(World));
    if (!w) return 1;
    WorldParams params = worldparams_default(seed);
    world_generate(w, &params);

    int W = SCPS_W, H = SCPS_H;
    uint32_t *buf = (uint32_t*)malloc((size_t)W*H*4);
    if (!buf) { free(w); return 1; }

    RenderParams rp = {
        .cam_ox = 0.f, .cam_oy = 0.f, .cam_scale = 1.f,
        .selected_prov = -1,
        .show_rivers = true, .show_borders = true, .show_grid = false
    };

    struct { ViewMode m; const char *file; } views[] = {
        { VIEW_TERRAIN,     "out_terrain.ppm"     },
        { VIEW_MOISTURE,    "out_moisture.ppm"    },
        { VIEW_TEMPERATURE, "out_temperature.ppm" },
        { VIEW_FERTILITY,   "out_fertility.ppm"   },
        { VIEW_POLITICAL,   "out_territoires.ppm" },
        { VIEW_REGIONS,     "out_regions.ppm"     },
        { VIEW_COUNTRIES,   "out_pays.ppm"        },
        { VIEW_CONTINENTS,  "out_continents.ppm"  },
        { VIEW_RESOURCES,    "out_resources.ppm"    },
        { VIEW_HABITABILITY, "out_habitability.ppm" },
    };
    /* Histogramme des ressources (vérification du placement causal) */
    int rescount[RES_COUNT]; for (int r=0;r<RES_COUNT;r++) rescount[r]=0;
    for (int p=0;p<w->n_provinces;p++) rescount[w->province[p].resource]++;
    printf("[dump] ressources :");
    for (int r=1;r<RES_COUNT;r++) if (rescount[r])
        printf(" %s=%d", resource_name((Resource)r), rescount[r]);
    printf("\n");

    /* Histogramme des biomes dominants de province (diagnostic) */
    int bc[BIO_COUNT]; for (int b=0;b<BIO_COUNT;b++) bc[b]=0;
    for (int p=0;p<w->n_provinces;p++) bc[w->province[p].biome_dominant]++;
    printf("[dump] biomes dom.:");
    for (int b=0;b<BIO_COUNT;b++) if (bc[b]) printf(" %s=%d", biome_name((Biome)b), bc[b]);
    printf("\n");

    /* Histogramme de PopCulture.subsistance — vérifie que l'échelle biome→
     * subsistance n'est plus inversée (steppe pastorale ≈ 2-3, plaine ≈ 6,
     * terres intensives ≈ 8). Nécessite l'éco (régions) + le peuplement. */
    {
        WorldEconomy *econ = (WorldEconomy*)malloc(sizeof(WorldEconomy));
        if (econ) {
            econ_init(econ, w);
            gen_population(w, econ);
            int sb[11]; for (int b=0;b<11;b++) sb[b]=0;
            for (int r=0;r<w->n_regions;r++) {
                int b=(int)(econ->region[r].culture.subsistance+0.5f);
                if (b<0) b=0;
                if (b>10) b=10;
                sb[b]++;
            }
            printf("[dump] subsistance (PopCulture) :");
            for (int b=0;b<=10;b++) if (sb[b]) printf(" %d:%d", b, sb[b]);
            printf("\n");
            free(econ);
        }
    }

    printf("[dump] graine %u → vues %dx%d\n", seed, W, H);
    for (size_t i = 0; i < sizeof(views)/sizeof(views[0]); i++) {
        render_map(w, buf, W, H, &rp, views[i].m);
        write_ppm(views[i].file, buf, W, H);
    }

    /* Gros plan : le 1er continent, terrain et frontières, pour vérifier que
     * les frontières épousent fleuves et crêtes. */
    if (w->n_continents>0) {
        /* centre du plus grand continent */
        long sx=0,sy=0,cnt=0;
        for (int yy=0;yy<H;yy++) for (int xx=0;xx<W;xx++)
            if (w->cell[yy*W+xx].continent==0){ sx+=xx; sy+=yy; cnt++; }
        int cx=cnt?(int)(sx/cnt):W/2, cy=cnt?(int)(sy/cnt):H/2;
        int ZW=480, ZH=360; float zs=3.2f;
        uint32_t *zb=(uint32_t*)malloc((size_t)ZW*ZH*4);
        RenderParams zp=rp; zp.cam_scale=zs;
        zp.cam_ox=cx-ZW/(2*zs); zp.cam_oy=cy-ZH/(2*zs);
        render_map(w, zb, ZW, ZH, &zp, VIEW_TERRAIN);
        write_ppm("out_zoom_terrain.ppm", zb, ZW, ZH);
        /* Surimpose UNIQUEMENT les traits de frontière sur le terrain brut :
         * on voit alors si les frontières suivent fleuves et crêtes. */
        for (int sy=0; sy<ZH; sy++) for (int sx=0; sx<ZW; sx++) {
            int wx=(int)(sx/zs+zp.cam_ox), wy=(int)(sy/zs+zp.cam_oy);
            if (wx<0||wx>=W||wy<0||wy>=H) continue;
            if (w->cell[wy*W+wx].border_prov) zb[sy*ZW+sx]=0xFF101010u; /* trait noir */
        }
        write_ppm("out_zoom_borders.ppm", zb, ZW, ZH);
        free(zb);
    }

    free(buf);
    free(w);
    return 0;
}
