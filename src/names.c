/*
 * names.c - generation procedural de noms
 *
 * Format generique : <Prefix> <Base> <Suffix>
 *  - elite : "<Prefix> <suffix lie a l'element>"
 *  - boss  : "<Prefix>, <Titre> <suffix elementaire>"
 *  - normal : juste le nom de base ("Zombie", "Bandit"...)
 */
#include "game.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static const char *PREFIXES[] = {
    "Vorgar", "Kheth", "Drakos", "Mordan", "Ulrik",
    "Sythia", "Karnis", "Brunis", "Velgar", "Athos",
    "Nyrath", "Zalkar", "Maelis", "Orthos", "Ravik",
    "Tolen",  "Yshara", "Garbol", "Quelis", "Phelan"
};
#define NUM_PREFIXES ((int)(sizeof(PREFIXES) / sizeof(PREFIXES[0])))

static const char *BOSS_TITLES[] = {
    "Roi", "Reine", "Seigneur", "Gardien", "Heraut",
    "Patriarche", "Tyran", "Maitre", "Archonte", "Devoreur"
};
#define NUM_BOSS_TITLES ((int)(sizeof(BOSS_TITLES) / sizeof(BOSS_TITLES[0])))

/* suffixes par element */
typedef struct { Element el; const char *suf; } ElemSuf;
static const ElemSuf SUFFIXES[] = {
    { EL_FIRE,      "le Brulant" },
    { EL_FIRE,      "des Cendres" },
    { EL_FIRE,      "le Pyromane" },
    { EL_FIRE,      "des Foyers" },

    { EL_WATER,     "des Marais" },
    { EL_WATER,     "le Glacial" },
    { EL_WATER,     "des Abimes Bleus" },
    { EL_WATER,     "des Mares" },

    { EL_EARTH,     "le Roc" },
    { EL_EARTH,     "des Cavernes" },
    { EL_EARTH,     "Pierre-Coeur" },
    { EL_EARTH,     "le Tellurien" },

    { EL_LIGHTNING, "le Foudroyant" },
    { EL_LIGHTNING, "des Tempetes" },
    { EL_LIGHTNING, "Eclair-Vif" },

    { EL_AIR,       "des Vents" },
    { EL_AIR,       "le Voile" },
    { EL_AIR,       "Souffle-Brise" },

    { EL_VOID,      "de l'Abime" },
    { EL_VOID,      "Sans-Visage" },
    { EL_VOID,      "le Nul" },

    { EL_FAE,       "le Sylphe" },
    { EL_FAE,       "des Fees" },
    { EL_FAE,       "Aube-Lumiere" },

    { EL_STEEL,     "Lame-Vive" },
    { EL_STEEL,     "le Forgeron" },
    { EL_STEEL,     "des Enclumes" },

    { EL_DARK,      "des Tenebres" },
    { EL_DARK,      "Ombre-Mort" },
    { EL_DARK,      "le Noir" },

    { EL_HOLY,      "Lumiere-Sainte" },
    { EL_HOLY,      "le Beni" },
    { EL_HOLY,      "Eclat-Pur" }
};
#define NUM_SUFFIXES ((int)(sizeof(SUFFIXES) / sizeof(SUFFIXES[0])))

static const char *suffix_for_element(Element el) {
    /* compte les suffixes correspondants */
    int n = 0;
    for (int i = 0; i < NUM_SUFFIXES; i++) if (SUFFIXES[i].el == el) n++;
    if (n == 0) return "l'Anonyme";
    int pick = rand() % n;
    int seen = 0;
    for (int i = 0; i < NUM_SUFFIXES; i++) {
        if (SUFFIXES[i].el == el) {
            if (seen == pick) return SUFFIXES[i].suf;
            seen++;
        }
    }
    return "l'Anonyme";
}

void enemy_generate_name(Enemy *e, int floor_index) {
    if (!e) return;
    if (e->is_boss) {
        const char *title = BOSS_TITLES[rand() % NUM_BOSS_TITLES];
        const char *prefix = PREFIXES[rand() % NUM_PREFIXES];
        const char *suf = suffix_for_element(e->element);
        snprintf(e->name, sizeof(e->name), "%s %s %s", prefix, title, suf);
    } else if (e->is_elite) {
        const char *prefix = PREFIXES[rand() % NUM_PREFIXES];
        const char *suf = suffix_for_element(e->element);
        snprintf(e->name, sizeof(e->name), "%s %s", prefix, suf);
    } else {
        snprintf(e->name, sizeof(e->name), "%s", enemy_name((EnemyKind)e->kind));
    }
    (void)floor_index;
}
