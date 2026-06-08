#ifndef SCPS_MISSIONS_H
#define SCPS_MISSIONS_H
/*
 * scps_missions.h — LES MISSIONS DÉCENNALES (factions §8)
 *
 * Tous les 10 ans, une mission est émise par pays — CONTEXTUELLE (tirée de l'état /
 * de l'éthos dominant / des besoins) : bâtir une institution, renforcer une chaîne
 * de production, percer des technologies. L'accomplir verse OR + MATIÈRES (∝ ampleur)
 * — un rythme, une direction, une injection de ressources, décennie après décennie.
 * Maintenant que bâtir COÛTE (l'or et les matériaux comptent), la récompense pèse.
 *
 * Passif : la mission s'émet et se récompense seule à l'accomplissement (l'IA, qui
 * bâtit/recherche naturellement, en complète ; le joueur y lit un but). Diégétique :
 * la mission porte un texte (membrane), jamais une coordonnée SCPS.
 */
#include "scps_world.h"   /* World, WorldEconomy */
#include "scps_econ.h"    /* RegionEconomy, Resource, ProvBuild */
#include "scps_tech.h"    /* TechState */

typedef enum { MIS_NONE = 0, MIS_BUILD, MIS_CHAIN, MIS_TECH } MissionKind;

typedef struct {
    MissionKind kind;
    int      coord;        /* MIS_BUILD : quelle coordonnée bâtie (0..5, cf. .c) */
    Resource good;         /* MIS_CHAIN : le bien à produire */
    float    threshold;    /* la cible (coordonnée bâtie / stock / nb de techs) */
    int      issued_year;
    bool     active, done;
    float    reward_gold;
    Resource reward_mat;   /* un lot de matières en récompense */
    float    reward_qty;
    char     text[80];     /* la mission, en mots (membrane) */
} Mission;

#define SCPS_MISSIONS_MAX SCPS_MAX_COUNTRY
typedef struct { Mission m[SCPS_MISSIONS_MAX]; } MissionsState;

void missions_init(MissionsState *ms);

/* Appelé une fois par AN : à chaque décennie émet une mission contextuelle par pays ;
 * chaque an, vérifie l'accomplissement et verse la récompense (or au trésor de la
 * capitale + matières au marché). */
void missions_tick(MissionsState *ms, const World *w, WorldEconomy *econ,
                   const TechState *ts, int year);

/* Lecture membrane (UI) : la mission courante d'un pays (NULL si aucune). */
const Mission *mission_of(const MissionsState *ms, int cid);

#endif /* SCPS_MISSIONS_H */
