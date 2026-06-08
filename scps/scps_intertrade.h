#ifndef SCPS_INTERTRADE_H
#define SCPS_INTERTRADE_H
/*
 * scps_intertrade.h — LE COMMERCE INTER-PAYS (les grandes routes marchandes)
 *
 * Hiérarchie : marché régional (autarcie) → commerce inter-régional (scps_trade,
 * intra-pays + frontières adjacentes) → COMMERCE INTER-PAYS (ici).
 *
 * Les grandes routes (scps_routes) ne portaient que du PE (prospérité). Ici elles
 * portent des GOODS sur de longues distances ENTRE PAYS : un royaume importe le
 * bien stratégique qui lui manque d'un partenaire lointain (arbitrage cheap→cher,
 * plafonné par la route) ; l'exportateur encaisse l'OR. → des empires marchands.
 *
 * Couplage géopolitique (la pièce qui manquait) :
 *   - EMBARGO : aucune route ne porte de goods entre deux pays EN GUERRE
 *     (guerre commerciale : on prive l'ennemi de son accès aux ressources).
 *   - Cela donne des DENTS au casus belli économique : un bien monopolisé qu'on
 *     ne peut plus importer → il faut CONQUÉRIR la source.
 *
 * Membrane : ce module est SIM. Les lecteurs renvoient des nombres tangibles (or,
 * volume) ; la traduction en mots reste à la membrane.
 */
#include "scps_econ.h"
#include "scps_routes.h"
#include "scps_diplo.h"

/* Un pas de commerce inter-pays : pour chaque route OUVERTE entre régions de PAYS
 * DIFFÉRENTS et NON en guerre, arbitre les biens (le cher s'approvisionne au bon
 * marché, plafonné par la route) ; l'exportateur encaisse l'or, les prix
 * convergent. À appeler après routes_advance(). `dp` peut être NULL (pas d'embargo). */
void intertrade_tick(WorldEconomy *e, const RouteNetwork *rn, const DiploState *dp);

/* Lecteurs (IA / UI) — sur le dernier tick. */
float intertrade_imports_value(const WorldEconomy *e);   /* valeur totale échangée au dernier tick */
int   intertrade_active_routes(const WorldEconomy *e, const RouteNetwork *rn,
                               const DiploState *dp, int cid);  /* routes marchandes vivantes d'un pays */

#endif /* SCPS_INTERTRADE_H */
