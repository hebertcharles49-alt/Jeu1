/*
 * meta.c - sauvegarde / chargement progression permanente
 */
#include "game.h"
#include <stdio.h>
#include <string.h>

#define SAVE_PATH "crucible_save.dat"
#define SAVE_MAGIC 0x43525542u   /* 'CRUB' */
#define SAVE_VERSION 1u

void save_load(MetaSave *m) {
    memset(m, 0, sizeof(*m));
    FILE *f = fopen(SAVE_PATH, "rb");
    if (!f) return;
    uint32_t magic, ver;
    if (fread(&magic, sizeof(magic), 1, f) != 1 ||
        fread(&ver,   sizeof(ver),   1, f) != 1 ||
        magic != SAVE_MAGIC || ver != SAVE_VERSION) {
        fclose(f);
        return;
    }
    if (fread(m, sizeof(*m), 1, f) != 1) {
        memset(m, 0, sizeof(*m));
    }
    fclose(f);
}

void save_write(const MetaSave *m) {
    FILE *f = fopen(SAVE_PATH, "wb");
    if (!f) return;
    uint32_t magic = SAVE_MAGIC, ver = SAVE_VERSION;
    fwrite(&magic, sizeof(magic), 1, f);
    fwrite(&ver,   sizeof(ver),   1, f);
    fwrite(m, sizeof(*m), 1, f);
    fclose(f);
}
