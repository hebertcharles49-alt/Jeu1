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
#include <stdio.h>
#include "scps_routes.h"
#include "scps_diplo.h"

/* Un pas de commerce inter-pays : pour chaque route OUVERTE entre régions de PAYS
 * DIFFÉRENTS et NON en guerre, arbitre les biens (le cher s'approvisionne au bon
 * marché, plafonné par la route) ; l'exportateur encaisse l'or, les prix
 * convergent. À appeler après routes_advance(). `dp` peut être NULL (pas d'embargo). */
void intertrade_tick(WorldEconomy *e, const RouteNetwork *rn, const DiploState *dp);

/* Lecteurs (IA / UI) — sur le dernier tick. */
float intertrade_imports_value(const WorldEconomy *e);   /* valeur totale échangée au dernier tick */
/* COMMERCE ASYMÉTRIQUE : volumes du dernier tick par SENS (aval = le sens
 * facile), et leur composition vrac (grain/bois/charbon/bétail) vs précieux
 * (étoffe précieuse/orfèvrerie/remèdes/armes). */
void intertrade_asym_stats(float *vdown, float *vup,
                           float *bulk_down, float *bulk_up,
                           float *prec_down, float *prec_up);
int  intertrade_precious_upstream_events(void);   /* la niche du luxe : il REMONTE */
int   intertrade_active_routes(const WorldEconomy *e, const RouteNetwork *rn,
                               const DiploState *dp, int cid);  /* routes marchandes vivantes d'un pays */

/* ---- DÉTAIL par pays × bien (sidebar Import/Export) — nombres de jeu --------
 * Accumulé par intertrade_tick (dernier tick = l'année écoulée) : volumes
 * importés/exportés par bien, partenaire DOMINANT du flux, or encaissé à
 * l'export, et la valeur échangée par PAIRE (le coût d'un embargo, lisible
 * AVANT de le décréter). Tout est tangible : volume, or, identifiants. */
float intertrade_import_vol (int cid, int good);
float intertrade_export_vol (int cid, int good);
int   intertrade_import_from(int cid, int good);   /* pays-source dominant (-1 si aucun) */
int   intertrade_export_to  (int cid, int good);   /* pays-client dominant (-1 si aucun) */
float intertrade_export_gold(int cid);             /* or encaissé à l'export (dernier tick) */
float intertrade_pair_value (int cid, int other);  /* valeur échangée avec ce partenaire */

/* ---- EMBARGO DÉCRÉTÉ (décision joueur/IA) -----------------------------------
 * En sus de l'embargo de guerre (automatique) : un pays peut DÉCRÉTER l'embargo
 * contre un autre — aucune route ne porte de goods entre eux tant qu'il tient.
 * intertrade_reset() remet embargos & flux à zéro (init de chaque partie/sim). */
void  intertrade_order_embargo(int cid, int target, bool on);
bool  intertrade_embargoed    (int cid, int target);   /* l'un OU l'autre a décrété */
void  intertrade_reset(void);
/* sauvegarde (shell §6) : le module possède sa sérialisation — embargos décrétés
 * (les flux du dernier tick se recalculent, eux). */
void  intertrade_save(FILE *f);
bool  intertrade_load(FILE *f);

#endif /* SCPS_INTERTRADE_H */
