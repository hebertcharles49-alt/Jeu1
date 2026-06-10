#ifndef SCPS_FACTIONS_H
#define SCPS_FACTIONS_H
/*
 * scps_factions.h — LES FACTIONS PAR ÉTHOS (passe 1/N : le spectre + l'enracinement)
 *
 * Les factions internes sont des factions d'ÉTHOS : les mêmes axes que les poids
 * de personnalité de l'IA (w_expand/w_trade/w_build/w_faith/w_faustian), faits
 * acteurs internes, et ENRACINÉS dans les groupes culturels (chaque peuple penche
 * vers un éthos). Couche ORTHOGONALE aux classes (strates) : la classe dit
 * COMBIEN tu as (richesse/conso/impôt) ; la faction-éthos dit ce que l'État
 * DEVRAIT être (direction/valeurs).
 *
 * Cette passe pose le SPECTRE (les six factions = les axes IA + le Communautaire)
 * et son ENRACINEMENT : la distribution de factions d'un pays = Σ de ses groupes,
 * pondérée par la population ET le poids social de la classe (l'élite gouverne).
 * Conquérir/absorber un peuple DÉPLACE la distribution — tes guerres reconfigurent
 * ton spectre politique. Les passes suivantes en tireront l'éthos effectif, les
 * leviers-votes, les conflits de valeurs, l'engagement d'âge et les missions.
 */
#include "scps_world.h"   /* World, WorldEconomy */
#include <stdio.h>
#include "scps_econ.h"    /* PopCulture, PopGroup, SocialClass, RegionEconomy */

/* ===================================================================== */
/* LES SIX FACTIONS-ÉTHOS = les axes des poids IA (+ le Communautaire)     */
/* ===================================================================== */
typedef enum {
    FAC_CONQUERANT = 0,   /* w_expand  : force, hiérarchie, gloire — guerre, conscription, H */
    FAC_MARCHAND,         /* w_trade   : échange, ouverture — libre-échange, frontières ouvertes */
    FAC_LEGISTE,          /* w_build   : ordre, institutions, mérite, K — bureaucratie, impôt propre */
    FAC_GARDIEN,          /* w_faith   : foi, tradition, le credo — foi imposée, théocratie */
    FAC_TRANSGRESSEUR,    /* w_faustian: la puissance au-delà des limites — runes, arcane, esclavage */
    FAC_COMMUNAUTAIRE,    /* (anti-expand/faustian) : le peuple, l'harmonie, la subsistance */
    FAC_COUNT
} EthosFaction;

const char *faction_name(EthosFaction f);   /* mot diégétique (membrane) */

/* ===================================================================== */
/* PENCHANT D'ÉTHOS D'UN GROUPE — d'où viennent les factions (§2)          */
/* ===================================================================== */
/* Un vecteur sur les six factions (Σ=1), dérivé de la culture du groupe :
 * son ÉTHOS (la direction première), sa SIGNATURE de race (orque→Conquérant+
 * Transgresseur, nain→Légiste+Transgresseur, halfelin/gnome→Marchand+
 * Communautaire, elfe→arcane+tradition), son CREDO (la ferveur nourrit les
 * Gardiens, la tolérance l'ouverture). */
void group_ethos_lean(const PopCulture *c, float out[FAC_COUNT]);

/* Poids social d'une classe : l'ÉLITE gouverne (pèse plus que la masse). */
float class_clout(SocialClass k);

/* ===================================================================== */
/* DISTRIBUTION DE FACTIONS D'UN PAYS — l'enracinement agrégé (§2)         */
/* ===================================================================== */
/* Σ sur tous les groupes du pays de (population × class_clout × penchant),
 * normalisée (Σ=1). Écrit le profil dans out[], renvoie la faction DOMINANTE.
 * Conquérir un peuple d'un autre éthos déplace ce profil (enracinement vivant). */
EthosFaction country_faction_weights(const World *w, const WorldEconomy *econ, int cid,
                                     float out[FAC_COUNT]);

/* Variante directe sur un jeu de provinces (bancs d'essai / sous-ensembles). */
EthosFaction faction_weights_of(const ProvincePop *provs, int n, float out[FAC_COUNT]);

/* ===================================================================== */
/* L'ÉTHOS EFFECTIF — la résultante que le moteur LIT (§3)                 */
/* ===================================================================== */
/* La/les faction(s) dominante(s) fixent les poids w_* EFFECTIFS du pays — la
 * même grille que la personnalité IA. L'éthos est un équilibre interne qui GLISSE
 * quand la distribution bouge (conquête, migration). Sort cinq axes [0..~1] :
 * expand/trade/build/faith/faustian, le Communautaire RETENANT expand & faustian
 * (le bien-commun bride les aventures). */
typedef struct { float w_expand, w_trade, w_build, w_faith, w_faustian; } EthosWeights;
EthosWeights faction_effective_weights(const float weights[FAC_COUNT]);

/* ===================================================================== */
/* COHÉSION vs FRACTURE DE VALEURS (§6) — la distance interne              */
/* ===================================================================== */
/* Un éthos dominant net → COHÉSION (direction claire, friction basse). Deux
 * factions fortes qui se disputent la tête (45/40) → FRACTURE interne (paralysie,
 * terreau de coup/guerre civile). Mesure le « contesté » de la direction : 0 si
 * une faction écrase, →1 si la seconde talonne la première. Parente de D̄, mais
 * sur les VALEURS — le frein INTERNE à la conquête (avaler des éthos divergents
 * importe des factions qui s'opposent ; l'incohérence te ligue dedans). */
float faction_fracture(const float weights[FAC_COUNT]);
float faction_cohesion(const float weights[FAC_COUNT]);   /* = 1 − fracture */

/* ===================================================================== */
/* OPPOSITION DE VALEURS & TENSION DE COUP (§5)                            */
/* ===================================================================== */
/* L'opposition de VALEURS entre deux factions [0..1] (colonne « Oppose » du §1) :
 * Conquérants↔Communautaires (guerre/paix), Marchands↔Gardiens (ouverture/foi
 * imposée), Légistes↔Transgresseurs (ordre/raccourci), Gardiens↔Transgresseurs
 * (l'orthodoxie interdit / le culte sacralise le faustien — l'épine dorsale). */
float faction_opposition(EthosFaction a, EthosFaction b);

/* La TENSION DE COUP d'un pays : la faction la plus FORTE dont l'éthos S'OPPOSE à
 * la direction effective (la dominante), pondérée par sa part. Élevée = une faction
 * forte aliénée couve un coup pour imposer SON éthos. Écrit la faction aliénée. */
float faction_coup_tension(const float weights[FAC_COUNT], EthosFaction *out_alienated);

/* ===================================================================== */
/* LES LEVIERS COMME DES VOTES (§4) — la politique déplace l'équilibre     */
/* ===================================================================== */
/* Un levier (libre-échange, foi imposée, forge à runes…) AVANCE un éthos et en
 * ALIÈNE d'autres : il RENFORCE la faction alignée (elle gagne du poids effectif)
 * et FÂCHE les opposées (elles accumulent du grief → couvent le coup). Favoriser
 * longtemps fait DÉRIVER le pays vers cet éthos. État de stance PAR PAYS, remis à
 * zéro par sim (faction_levers_reset), qui s'efface s'il n'est pas entretenu. */
void faction_levers_reset(void);
/* sauvegarde : biais/rancœur/capture des factions (statiques du module). */
void faction_save(FILE *f);
bool faction_load(FILE *f);                                  /* début de partie/sim */
void faction_lever_apply(int cid, EthosFaction advanced, float strength);  /* un vote */
void faction_levers_decay(float rate);                            /* la stance non tenue s'efface */
void faction_levers_on_coup(int cid);                             /* un coup DÉCHARGE la rancœur du pays */
float faction_grievance(int cid, EthosFaction f);                 /* 0-1 : la rancœur d'une faction (UI) */
/* §C3 — la concession a un prix : capture de l'État, lue à l'écran en Corruption. */
void         faction_concede(int cid, EthosFaction winner);       /* une concession gorge la faction gagnante */
float        faction_capture_total(int cid);                      /* le « rot » 0..1 (malus noble, K creusé) */
int          faction_corruption_0_100(int cid);                   /* l'indice de Corruption (écran) */
EthosFaction faction_captor(int cid);                             /* la faction qui tient l'État (survol) */

/* La distribution EFFECTIVE = base (groupes) + stance des leviers, normalisée. C'est
 * elle que le moteur lit (éthos effectif §3, tension de coup §5, UI §9). Dominante. */
EthosFaction faction_effective_distribution(const World *w, const WorldEconomy *econ,
                                            int cid, float out[FAC_COUNT]);
/* Tension de coup TENANT COMPTE du grief des opposés aliénés par la politique. */
float faction_coup_tension_c(const World *w, const WorldEconomy *econ,
                             int cid, EthosFaction *out_alienated);

/* ===================================================================== */
/* ENGAGEMENT D'ÂGE (§7) — à chaque lever d'âge, une faction s'avance       */
/* ===================================================================== */
/* La faction PATRONNE d'un âge (age = valeur d'AgeId, passée en int pour éviter un
 * cycle d'en-têtes) : Commerce→Marchands, Raison/Lumières→Légistes, Empires→
 * Conquérants, Brèche→Transgresseurs, Soulèvements→Communautaires, Ordre de Fer→
 * Conquérants. À l'avènement, elle propose une PLEDGE. */
EthosFaction age_patron(int age);

/* Tenir la pledge d'âge : renforce le patron (une pledge tenue = un vote) ET apaise
 * (la satisfaction de l'ordre monte un temps — cohésion du régime). Appelé à
 * l'avènement de l'âge pour un pays (l'IA accepte ; le joueur choisira). */
void faction_age_engage(const World *w, WorldEconomy *econ, int cid, int age);

#endif /* SCPS_FACTIONS_H */
