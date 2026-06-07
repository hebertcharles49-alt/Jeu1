#ifndef SCPS_DIPLO_H
#define SCPS_DIPLO_H
/*
 * scps_diplo.h — DIPLOMATIE (§6) & GUERRE (§5)
 *
 * Les relations se LISENT (lecteurs), jamais ne se posent à la main :
 *   - threat     = (eco + mil) / (distance + projection)   [ta formule]
 *   - complement = complémentarité de ressources (durable)
 *   - kinship    = distance de sphère (du même sang … étranger d'une autre sphère)
 *   - schism     = branche religieuse proche + prosélytisme = ennemi naturel
 *   - alliance   = menace partagée (transitoire) + complément (durable) − valeurs − schisme
 *
 * Guerre = acquisition de territoire ET montée de la diversité interne :
 * conquérir une culture lointaine monte le D̄ du conquérant → fracture tant que
 * K ne métabolise pas. La récompense est gatée par le K.
 */
#include "scps_world.h"
#include "scps_econ.h"
#include "scps_prosperity.h"
#include "scps_legitimacy.h"

typedef struct {
    float threat;       /* menace de b sur a */
    float complement;   /* complémentarité de ressources [0..1] */
    float kinship;      /* distance de sphère [0..7] (haut = étranger) */
    float schism;       /* ennemi naturel religieux [0..1] */
    float alliance;     /* score d'alliance (haut = allié naturel) */
} Relation;

typedef enum { DIPLO_NEUTRAL = 0, DIPLO_ALLIED, DIPLO_WAR } DiploStatus;

typedef struct {
    DiploStatus status[SCPS_MAX_COUNTRY][SCPS_MAX_COUNTRY];
    float       war_years[SCPS_MAX_COUNTRY][SCPS_MAX_COUNTRY];
} DiploState;

void diplo_init(DiploState *d);

/* ---- Lecteurs ---------------------------------------------------------- */
float    diplo_eco_power(const WorldProsperity *wp, int cid);
float    diplo_mil_power(const World *w, const WorldEconomy *econ, int cid);
Relation diplo_relation (const World *w, const WorldEconomy *econ,
                         const WorldProsperity *wp, const DiploState *d, int a, int b);

/* ---- Actions ----------------------------------------------------------- */
void        diplo_declare_war (DiploState *d, int a, int b);
void        diplo_form_alliance(DiploState *d, int a, int b);
void        diplo_make_peace  (DiploState *d, int a, int b);
DiploStatus diplo_status      (const DiploState *d, int a, int b);

/* Conquête (§5) : transfère une région ennemie au conquérant (suppose la guerre
 * et l'issue militaire favorable). L'owner change → la diversité du conquérant
 * monte ; L de la région s'effondre (puis réintègre sur des générations).
 * Renvoie true si la conquête a lieu. */
bool diplo_conquer_region(DiploState *d, World *w, WorldEconomy *econ,
                          WorldLegitimacy *wl, int conqueror, int region);

void diplo_tick(DiploState *d, float dt);   /* usure de guerre (war_years++) */

#endif /* SCPS_DIPLO_H */
