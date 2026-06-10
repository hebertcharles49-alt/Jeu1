#ifndef SCPS_WARHOST_H
#define SCPS_WARHOST_H
/*
 * scps_warhost.h — LES ARMÉES VIVENT : la mobilisation par pays
 *
 * scps_army (recrutement, armes, pierre-feuille-ciseaux) était complet mais
 * n'était branché à AUCUN tick : les armées existaient en tant que données, sans
 * vivre. Ce module les fait vivre dans la boucle.
 *
 * Chaque pays porte une ArmyState. Une fois l'an, sur PIED DE GUERRE (en guerre ou
 * menacé) il MOBILISE : il fabrique des armes et lève des unités depuis sa pop &
 * ses matériaux (semés à bas coût depuis l'économie de région). La force levée se
 * DÉPOSE en armes (RES_ARMS) sur sa capitale → elle nourrit diplo_mil_power, SANS
 * réécrire la guerre. Boucle de guerre-économie : l'attrition (diplo_war_tick)
 * saigne les armes, la mobilisation les renouvelle pour qui est sur le pied de
 * guerre — la paix démobilise.
 *
 * Membrane : warhost_units renvoie un nombre tangible (paquets de 100).
 */
#include "scps_world.h"
#include "scps_econ.h"
#include "scps_army.h"
#include "scps_labor.h"
#include "scps_diplo.h"

typedef struct {
    ArmyState  army[SCPS_MAX_COUNTRY];   /* l'armée levée de chaque pays (persiste) */
    LaborEcon *scratch;                   /* labor transitoire, re-semé par pays */
    int        levy[SCPS_MAX_COUNTRY];   /* jauge de LEVÉE (sidebar §5) : 0 basse · 1 garde · 2 guerre · 3 masse */
} WarHost;

/* Jauge de levée (décision joueur/IA) : module la cadence de mobilisation. La LEVÉE
 * EN MASSE (3) force la main des familles → coercition à la capitale (le coût,
 * affiché AVANT). Tout est en mots/paliers — aucune coordonnée ne sort. */
#define WH_LEVY_BASSE  0
#define WH_LEVY_GARDE  1
#define WH_LEVY_GUERRE 2
#define WH_LEVY_MASSE  3
void warhost_set_levy(WarHost *h, int cid, int levy);
int  warhost_levy    (const WarHost *h, int cid);
const char *warhost_levy_name(int levy);

void warhost_init(WarHost *h);
void warhost_free(WarHost *h);

/* Mobilisation (dt en ANNÉES) : chaque pays vivant lève des troupes ∝ son pied de
 * guerre ; la force se dépose en armes sur sa capitale (→ mil_power). */
void warhost_tick(WarHost *h, const World *w, WorldEconomy *econ,
                  const DiploState *dp, float dt_years);

long warhost_units (const WarHost *h, int cid);   /* paquets de 100 levés (UI/IA) */

#endif /* SCPS_WARHOST_H */
