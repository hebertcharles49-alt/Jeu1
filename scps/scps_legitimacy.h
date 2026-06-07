#ifndef SCPS_LEGITIMACY_H
#define SCPS_LEGITIMACY_H
/*
 * scps_legitimacy.h — LA LÉGITIMITÉ (L) PREND VIE
 *
 * L est la seule entrée de scps_order qui n'avait pas de maison. Le
 * consentement n'est pas posé à la main : il ÉMERGE, avec inertie (on n'achète
 * pas la légitimité d'un coup — le consentement se retire en cascade, Kuran).
 *
 * Granularité : la sim (population, culture, coercition) vit au niveau RÉGION ;
 * L vit donc par région (une région = la population qui consent ou non). Le
 * pays agrège par la population. La dispersion des L régionaux EST la pression
 * de sécession (fracture = 0.55·(D̄/10)·(10−L) mord d'autant plus que L est bas
 * ET la diversité haute).
 *
 * D'où vient le consentement (par région, sous la culture régnante du pays) :
 *   - Alignement : une culture lointaine consent moins (distance de CONTENU).
 *   - Aisance    : un peuple qui prospère consent (le pain).
 *   - Ombre de la contrainte : coercition locale + H du pays rongent L.
 *   - Ancienneté : une région fraîchement colonisée ne consent pas ; le
 *     consentement monte avec les années de possession (intégration).
 */
#include "scps_types.h"
#include "scps_econ.h"
#include "scps_tech.h"
/* N'inclut PAS scps_core.h : la légitimité alimente le moteur, elle n'en est pas. */

typedef struct {
    float L[SCPS_MAX_REG];           /* consentement par région [0..10] */
    float years_held[SCPS_MAX_REG];  /* ancienneté de tutelle (intégration) */
    bool  seeded;
} WorldLegitimacy;

/* Pose L neutre, ancienneté pleine pour les régions déjà peuplées au départ. */
void  legitimacy_init(WorldLegitimacy *wl, const World *w, const WorldEconomy *econ);

/* Un pas : L tend lentement (inertie) vers sa cible émergente par région. */
void  legitimacy_tick(WorldLegitimacy *wl, const World *w,
                      const WorldEconomy *econ, const TechState ts[]);

/* Choc : conquête d'une région (consentement effondré, intégration remise à 0). */
void  legitimacy_on_conquest(WorldLegitimacy *wl, int region_id);

/* Agrégat pays : moyenne des L régionaux pondérée par la population.
 * C'est l'entrée L de scps_order. */
float legitimacy_country(const WorldLegitimacy *wl, const World *w,
                         const WorldEconomy *econ, int cid);

#endif /* SCPS_LEGITIMACY_H */
