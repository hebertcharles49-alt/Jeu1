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
        { VIEW_RESOURCES,   "out_resources.ppm"   },
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

    printf("[dump] graine %u → vues %dx%d\n", seed, W, H);
    for (size_t i = 0; i < sizeof(views)/sizeof(views[0]); i++) {
        render_map(w, buf, W, H, &rp, views[i].m);
        write_ppm(views[i].file, buf, W, H);
    }

    free(buf);
    free(w);
    return 0;
}
