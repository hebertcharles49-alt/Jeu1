/*
 * meta.c - sauvegarde / chargement
 */
#include "game.h"
#include <stdio.h>
#include <string.h>

#define SAVE_PATH "crucible_save.dat"
#define SAVE_MAGIC 0x43525542u   /* 'CRUB' */
#define SAVE_VERSION 8u

/* re-initialise les champs ajoutes apres v3 (item_seen_rarity = -1 partout). */
static void meta_defaults_post(MetaSave *m) {
    for (int s = 0; s < EQUIP_SLOTS; s++)
        for (int k = 0; k < 5; k++)
            m->item_seen_rarity[s][k] = -1;
}

void save_load(MetaSave *m) {
    memset(m, 0, sizeof(*m));
    meta_defaults_post(m);
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
        meta_defaults_post(m);
    }
    fclose(f);
}

/* ---- helpers codex ---- */
void meta_combo_mark(MetaSave *m, int mask) {
    if (mask == 0) return;
    for (int i = 0; i < m->combo_seen_count; i++)
        if (m->combo_seen[i] == mask) return;
    if (m->combo_seen_count < (int)(sizeof(m->combo_seen)/sizeof(m->combo_seen[0]))) {
        m->combo_seen[m->combo_seen_count++] = mask;
    }
}

bool meta_combo_is_seen(const MetaSave *m, int mask) {
    if (mask == 0) return false;
    for (int i = 0; i < m->combo_seen_count; i++)
        if (m->combo_seen[i] == mask) return true;
    return false;
}

void meta_item_mark(MetaSave *m, EquipSlot slot, int sub_kind, Rarity r) {
    if ((int)slot < 0 || (int)slot >= EQUIP_SLOTS) return;
    if (sub_kind < 0 || sub_kind >= 5) return;
    if ((int)r > m->item_seen_rarity[slot][sub_kind]) {
        m->item_seen_rarity[slot][sub_kind] = (int)r;
    }
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
