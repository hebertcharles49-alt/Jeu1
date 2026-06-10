#ifndef SCPS_AGENCY_H
#define SCPS_AGENCY_H
/*
 * scps_agency.h — LA COUCHE D'AGENCY : actions, temps, bâtiments (§1-§2)
 *
 * tick = 1 JOUR ; une partie = 250 ans ≈ 91 250 jours. Le jour est l'atome ;
 * tout se joue sur des mois, des années, des décennies.
 *
 * Règle non négociable : une action est un LEVIER qui déplace une COORDONNÉE
 * (K, P, H…), jamais un bonus plat. Un bâtiment n'ajoute pas « +10% » : c'est
 * de la densité institutionnelle réalisée, accumulée dans RegionEconomy.build
 * (ProvBuild), que le moteur d'ordre (prosperity_tick) et la légitimité LISENT.
 *
 * Ce module est l'ÉCRIVAIN de ces accumulateurs ; prosperity/legitimacy les
 * relisent. Le joueur voit des bâtiments nommés (membrane), dessous K/H/P bouge.
 */
#include "scps_econ.h"        /* ProvBuild, WorldEconomy, Resource */
#include "scps_world.h"       /* World (biome) */
#include "scps_legitimacy.h"  /* WorldLegitimacy (défrichement ronge L) */

#define SCPS_DAYS_PER_YEAR 365
#define SCPS_GAME_YEARS    250

/* Édifices — chacun déplace une coordonnée (cf. ProvBuild). */
typedef enum {
    EDI_TRIBUNAL = 0, EDI_CHANCELLERIE, EDI_ACADEMIE,  /* → K (Académie aussi P) */
    EDI_GARNISON, EDI_FORTERESSE, EDI_CITADELLE,        /* → H (ronge L) */
    EDI_PORT, EDI_CARAVANSERAIL,                        /* → P */
    EDI_MARCHE, EDI_ENTREPOT,                           /* → PE local */
    EDI_GRENIER, EDI_IRRIGATION, EDI_AQUEDUC,           /* → food (santé urbaine → croissance) */
    EDI_SANCTUAIRE, EDI_TEMPLE, EDI_CATHEDRALE,         /* → foi (SOUTIENT L) */
    EDI_BIBLIOTHEQUE, EDI_MONASTERE,                    /* → savoir (recherche ; le monastère aussi foi) */
    EDI_COMPTOIR, EDI_BANQUE,                           /* → PE (commerce) */
    EDIFICE_COUNT
} Edifice;

/* Coût d'un bâtiment (§1) : une RECETTE de matériaux, achetée AU MARCHÉ. Le joueur
 * ne tient pas de stock de matériaux : les ressources vont dans le marché, et bâtir
 * les y ACHÈTE en OR (à leur prix courant) — le manque renchérit (rareté = prix). Le
 * coût monte avec le TIER (plus, et des matériaux plus avancés : bois → bois+métal →
 * métal+précieux), pas avec l'étendue (un grenier coûte pareil partout). */
#define BUILD_RES_MAX 3
typedef struct {
    Resource res[BUILD_RES_MAX];
    float    qty[BUILD_RES_MAX];
} BuildCost;

typedef struct {
    const char *name;
    int         days;     /* durée de construction (l'arc de 250 ans) */
    ProvBuild   delta;    /* ce qu'il ajoute à la province à l'achèvement */
    BuildCost   cost;     /* matériaux requis (achetés au marché en or) — §1 */
} EdificeDef;

const EdificeDef *edifice_def(Edifice e);
const char       *edifice_name(Edifice e);

/* Le PRIX en OR de la recette au marché de la région (Σ qty × prix courant) — un
 * nombre de jeu (affichable). Sert au garde de construction ET à l'UI. */
float agency_build_gold(const WorldEconomy *econ, int region, Edifice e);

/* Familles d'action de province (le motif s'étend). */
typedef enum { AGY_BUILD = 0, AGY_CLEAR, AGY_EXPLOIT, AGY_RELOCATE } ActionKind;

/* Une action en cours (file par pays/province). */
typedef struct {
    ActionKind kind;
    int        region;
    int        param;     /* Edifice (BUILD) | Resource (EXPLOIT) | inutilisé (CLEAR) */
    int        days_total, days_done;
    bool       active;
} BuildOrder;

#define SCPS_MAX_BUILDS 512
typedef struct {
    BuildOrder order[SCPS_MAX_BUILDS];
    int        n;
    int        day;       /* compteur de partie (jours écoulés) */
} AgencyState;

void agency_init(AgencyState *a);
/* Met une action en file (false si pleine). NU — sans coût (bas niveau / bancs d'essai). */
bool agency_order_build  (AgencyState *a, int region, Edifice e);

/* Bâtir EN PAYANT (§1) : achète la recette AU MARCHÉ (or du trésor régional ; le
 * marché est consommé), PUIS enfile le chantier. Refuse — pas de chantier — si le
 * trésor ne couvre pas. C'est la voie de la sim vive (IA, joueur). */
bool agency_build(AgencyState *a, WorldEconomy *econ, int region, Edifice e);
/* §4 Défrichement : convertit la terre → food, dérive la SUBSISTANCE locale vers
 * l'agriculture (impérialisme culturel sur la terre), et ronge L en niche
 * forestière (les peuples de la forêt voient leur monde rasé). */
bool agency_order_clear  (AgencyState *a, int region);
/* §3 Exploitation : un aménagement (mine/carrière…) monte l'extraction d'une
 * ressource (matériaux pour bâtir/armer, stratégiques pour la tech/valeur). */
bool agency_order_exploit(AgencyState *a, int region, Resource res);
/* §reloc (sidebar Démographie) : ORDONNE le déplacement d'un ensemencement de pop
 * (RELOC_POP familles) de `region` (source) vers `dst_region` — en JOURS, comme tout
 * ordre. À terme : econ_relocate_pop (la coercition monte à la source — le coût,
 * affiché AVANT). Joueur et IA passent par le MÊME actionneur, jamais l'appel direct. */
bool agency_order_relocate(AgencyState *a, int region, int dst_region);
#define AGY_RELOC_POP 300   /* familles déplacées par ordre (l'ensemencement mesuré) */
/* Annule un ordre encore en cours (index dans order[]) — la file est VISIBLE (struct
 * publique) et un chantier non fini se révoque. */
bool agency_cancel(AgencyState *a, int idx);

/* Avance de `days` jours : progresse les chantiers ; à l'achèvement, applique
 * l'effet (déplace une coordonnée que le moteur LIT). */
void agency_advance(AgencyState *a, World *w, WorldEconomy *econ,
                    WorldLegitimacy *wl, int days);
/* Nombre de chantiers actifs sur une région (pour l'UI). */
int  agency_active_in_region(const AgencyState *a, int region);
int  agency_year(const AgencyState *a);

#endif /* SCPS_AGENCY_H */
