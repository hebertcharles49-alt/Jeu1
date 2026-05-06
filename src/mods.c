/*
 * mods.c - support de mods minimal
 *
 * Charge tous les fichiers mods/[NOM].cfg au demarrage. Format :
 *   # commentaire
 *   key = value
 * Les valeurs sont conservees en memoire pour usage ulterieur
 * (overrides de constantes du jeu, balance, etc).
 *
 * C'est volontairement un stub : le but est d'avoir une porte
 * d'entree stable pour les mods. Au fur et a mesure, le code du
 * jeu peut consulter mod_get_float/int/string pour overrider une
 * valeur.
 */
#include "game.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#endif

#define MAX_MOD_KEYS 256

typedef struct {
    char key[64];
    char val[128];
} ModEntry;

static ModEntry g_mods[MAX_MOD_KEYS];
static int      g_mod_count = 0;

static void mod_set(const char *key, const char *val) {
    if (g_mod_count >= MAX_MOD_KEYS) return;
    /* override existant ? */
    for (int i = 0; i < g_mod_count; i++) {
        if (strcmp(g_mods[i].key, key) == 0) {
            snprintf(g_mods[i].val, sizeof(g_mods[i].val), "%s", val);
            return;
        }
    }
    snprintf(g_mods[g_mod_count].key, sizeof(g_mods[g_mod_count].key), "%s", key);
    snprintf(g_mods[g_mod_count].val, sizeof(g_mods[g_mod_count].val), "%s", val);
    g_mod_count++;
}

static void load_cfg_file(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '#' || *p == ';' || *p == '\n' || *p == '\r' || *p == 0) continue;
        char *eq = strchr(p, '=');
        if (!eq) continue;
        *eq = 0;
        char *k = p;
        char *v = eq + 1;
        /* trim */
        char *e = k + strlen(k);
        while (e > k && (e[-1] == ' ' || e[-1] == '\t')) { e--; *e = 0; }
        while (*v == ' ' || *v == '\t') v++;
        e = v + strlen(v);
        while (e > v && (e[-1] == '\n' || e[-1] == '\r' || e[-1] == ' ' || e[-1] == '\t')) { e--; *e = 0; }
        if (*k && *v) mod_set(k, v);
    }
    fclose(f);
}

void mods_load(Game *g) {
    (void)g;
    g_mod_count = 0;
#ifdef _WIN32
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA("mods\\*.cfg", &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            char path[260];
            snprintf(path, sizeof(path), "mods\\%s", fd.cFileName);
            load_cfg_file(path);
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
#else
    DIR *d = opendir("mods");
    if (d) {
        struct dirent *de;
        while ((de = readdir(d)) != NULL) {
            const char *n = de->d_name;
            size_t len = strlen(n);
            if (len > 4 && strcmp(n + len - 4, ".cfg") == 0) {
                char path[260];
                snprintf(path, sizeof(path), "mods/%s", n);
                load_cfg_file(path);
            }
        }
        closedir(d);
    }
#endif
    if (g_mod_count > 0) {
        fprintf(stderr, "[mods] %d cles chargees depuis mods/*.cfg\n", g_mod_count);
    }
}

/* API d'acces (utilisable plus tard depuis n'importe quel module) */
const char *mod_get_string(const char *key, const char *def) {
    for (int i = 0; i < g_mod_count; i++)
        if (strcmp(g_mods[i].key, key) == 0) return g_mods[i].val;
    return def;
}
float mod_get_float(const char *key, float def) {
    const char *v = mod_get_string(key, NULL);
    if (!v) return def;
    return (float)atof(v);
}
int mod_get_int(const char *key, int def) {
    const char *v = mod_get_string(key, NULL);
    if (!v) return def;
    return atoi(v);
}
