#ifndef SCPS_MODIFIER_H
#define SCPS_MODIFIER_H
/*
 * scps_modifier.h — PILE DE MODIFICATEURS PERSISTANTS (clé de voûte v2)
 *
 * Réutilisable par les syncrétismes, les technologies et les événements.
 *
 * Principe SCPS : on ne FIXE jamais une sortie (PE, SI…), on la LIT. Un effet
 * persistant n'écrit donc pas PE/SI une fois pour toutes (la v1 le faisait :
 * le recalcul du tick suivant écrasait le delta → effet évaporé). Il dépose
 * ici un modificateur que prosperity_tick AGRÈGE et relit À CHAQUE tick.
 *
 * Deux familles de canaux :
 *   - ENTRÉE  : ajoutée aux entrées EFFECTIVES avant le calcul du moteur
 *               (K, L, P, F, I, H, Mil, Mag, CF, Div). La sortie suit.
 *   - SORTIE  : ajoutée à PE/SI APRÈS le calcul (effet net voulu par le
 *               designer). La fragilité reste une pure sortie, sans canal.
 */
#include <stdbool.h>

typedef struct {
    int   source_id;      /* id de l'entrée syncrétique (dédup / retrait)  */
    int   country;        /* pays sur lequel s'applique l'effet            */
    int   other;          /* l'autre pays de la paire (pour rupture guerre)*/

    /* canaux d'ENTRÉE (échelle 0..10 du moteur ; F = fédéralisme) */
    float dK, dL, dP, dF, dI, dH;
    float dMil, dMag, dCF, dDiv;    /* Mil = force militaire, Mag = magie  */

    /* canaux de SORTIE */
    float dPE, dSI;
    float dPE_per_route;            /* bonus PE par route active (produits) */

    /* cycle de vie */
    int   tick_started;
    int   expires_tick;             /* -1 = permanent                       */
    bool  breaks_on_war;            /* fusions culturelles                  */
    int   war_break_years;

    /* DÉRIVE CULTURELLE PAR GROUPE (assimilation démographique, §5) — la
     * « pâture » de cette pile : la fiche EFFECTIVE d'un groupe = origine + ces
     * deltas (recalcul, pas mutation), ce qui permet la RÉVERSIBILITÉ. */
    int   group_key;     /* clé de groupe (démographie) ; les effets-pays l'ignorent */
    float dCv, dCs, dCp, dCr;   /* dérive sur valeurs/subsistance/parenté/religion */
    float dAgit;                /* suppression d'agitation (coercition) */
    bool  reversible;           /* SUPPRIMÉ (saute si H tombe) vs MÉTABOLISÉ (durable) */
} Modifier;

#define SCPS_MAX_MODIFIERS 1024
typedef struct { Modifier items[SCPS_MAX_MODIFIERS]; int n; } ModifierStack;

/* Empile un modificateur. Renvoie false si la pile est pleine. */
bool modstack_push(ModifierStack *ms, Modifier m);

/* Agrégat des deltas d'un pays (somme de tous ses modificateurs actifs). */
typedef struct {
    float K, L, P, F, I, H, Mil, Mag, CF, Div;   /* entrées */
    float PE, SI, PE_route;                       /* sorties */
} ModAccum;

ModAccum modstack_accumulate(const ModifierStack *ms, int country);

/* ---- Dérive culturelle par GROUPE (assimilation / suppression, §5) ----- *
 * Une pile DÉDIÉE par pays tient, par groupe minoritaire, sa dérive : durable
 * (métabolisée par consentement) OU réversible (supprimée par la coercition,
 * saute si la botte se lève). La fiche effective = origine + somme de la dérive. */
typedef struct { float dCv, dCs, dCp, dCr, dAgit; } GroupDrift;

/* Somme de TOUTE la dérive d'un groupe (durable + réversible). */
GroupDrift modstack_group_drift(const ModifierStack *ms, int group_key);
/* Accumule un pas dans le modificateur (group_key, reversible) du groupe — le
 * crée s'il n'existe pas (borne la pile : ≤ 2 modificateurs/groupe). */
void modstack_accumulate_drift(ModifierStack *ms, int group_key, GroupDrift step, bool reversible);
/* Retire les modificateurs RÉVERSIBLES d'un groupe (la coercition se lève → la
 * préférence falsifiée resurgit : Kuran). */
void modstack_drop_reversible(ModifierStack *ms, int group_key);

#endif /* SCPS_MODIFIER_H */
