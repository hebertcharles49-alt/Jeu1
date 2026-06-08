#ifndef SCPS_FAITH_H
#define SCPS_FAITH_H
/*
 * scps_faith.h — LA RELIGION : une foi est une CONFIGURATION, générée par monde
 * (comme les cultures), portée par les groupes/régions, PARALLÈLE à la culture.
 *
 * Une foi est la FACE SACRÉE d'un éthos : elle en bénit un (sanctifies) et
 * renforce sa faction. Elle se tient sur la COLONNE VERTÉBRALE résilience↔faustien
 * (forbidden_stance : orthodoxe = frein qui INTERDIT le faustien … culte = qui le
 * SACRALISE). Elle est ACTIVE : elle convertit, lie, fracture (cf. faith_distance).
 *
 * Marqueur parallèle : une foi UNIVERSELLE lie des cultures différentes ; un
 * SCHISME divise une même culture. (Pass 1 : la config + les lecteurs. Les hooks
 * cohésion / légitimité / faustien / conversion / schisme suivent.)
 *
 * Membrane : des NOMS de foi et des MOTS (« dévot / tiède / hérétique »), jamais
 * un float SCPS (ni forbidden_stance ni charge à l'écran).
 */
#include "scps_world.h"
#include "scps_econ.h"   /* PopCulture, Ethos, Credo, ReligionBranch */
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    int            id;
    char           name[40];
    Ethos          sanctifies;      /* l'ÉTHOS béni (la foi renforce sa faction) */
    ReligionBranch branch;          /* l'arbre généalogique de la foi */
    float          religion_center; /* [0..10] centre sur l'axe credo (la bande) */
    float          forbidden_stance;/* [0..1] orthodoxe(0, interdit faustien) ↔ culte(1, sacralise) */
    float          proselytism;     /* [0..1] appétit de conversion */
} Faith;

#define FAITH_MAX 12
typedef struct { Faith faith[FAITH_MAX]; int n; } FaithSet;

/* Génère le jeu de foi du monde (DÉTERMINISTE) : agrège les cultures-régions en
 * foi par (branche × bande de l'axe religion) ; chaque foi hérite l'éthos MODAL
 * de ses fidèles (ce qu'elle sanctifie) et son prosélytisme de leur credo. */
void faith_generate(FaithSet *fs, const World *w, const WorldEconomy *econ, uint32_t seed);

/* La foi d'une culture (lecture du marqueur : branche + bande). -1 si jeu vide. */
int          faith_of  (const FaithSet *fs, const PopCulture *pc);
const Faith *faith_get (const FaithSet *fs, int id);
const char  *faith_name(const FaithSet *fs, int id);

/* DISTANCE de foi [0..10] (l'axe ACTIF, §1) : 0 = co-religionnaires (assimilation
 * rapide, cohésion) ; moyenne = schisme dans la même branche ; haute = autre foi
 * (résistance, fracture). */
float faith_distance(const FaithSet *fs, const PopCulture *a, const PopCulture *b);

/* COLONNE VERTÉBRALE (§4) : l'orthodoxe INTERDIT le faustien (frein religieux) ;
 * le culte le SACRALISE (déchaînement). Lecture du forbidden_stance. */
bool faith_forbids_faustian   (const Faith *f);
bool faith_sacralizes_faustian(const Faith *f);

/* Membrane — l'humeur d'un groupe envers la foi de la COURONNE, en MOT. */
typedef enum { FAITH_DEVOT = 0, FAITH_TIEDE, FAITH_HERETIQUE } FaithMood;
FaithMood   faith_mood     (const FaithSet *fs, const PopCulture *crown, const PopCulture *group);
const char *faith_mood_word(FaithMood m);            /* "dévot"/"tiède"/"hérétique" */
const char *faith_branch_name(ReligionBranch b);     /* "animiste"/"du Livre"/… */

#endif /* SCPS_FAITH_H */
