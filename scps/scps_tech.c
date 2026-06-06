/*
 * scps_tech.c — arbre de technologies (voir scps_tech.h)
 *
 * Table de nœuds data-driven. Les valeurs sont indicatives ; la structure
 * (prérequis, directions d'écriture SCPS, charge) suit le document de
 * conception. Aucune dépendance au reste du moteur : ce module ne fait
 * qu'écrire dans un TechState. Le branchement sur le World/économie viendra.
 */
#include "scps_tech.h"
#include <math.h>
#include <stddef.h>

#define NONE TECH_COUNT   /* sentinelle « pas de prérequis » */

/* ---- Constantes de calibrage (à régler) ------------------------------- */
#define CRISIS_SCALE   12.0f   /* échelle de la courbe proximité = f(charge) */
#define SHOCK_MAGIE    0.50f   /* la magie gonfle l'ampleur du choc */
#define DEREAL_P_COEF  0.10f   /* terme (P/10)·C */

/* ====================================================================== */
/* TABLE DES NŒUDS                                                         */
/* ====================================================================== */
/* Champs : name, branch, tier, {prereq0,prereq1}, needs_ruins, capstone,
 *          dK,dL,dF, dEco,dMil, dH, dFracture, dPuissance, flux, charge,
 *          triggers_crisis */
static const TechNode NODES[TECH_COUNT] = {
/* ---- Branche I — SOCIÉTÉ (charge ≈ 0, monte K/L/F, baisse fracture) ---- */
[TECH_I1_COUTUME] = {
    "Coutume codifiée", TBR_SOCIETY, 1, {NONE,NONE}, false, false,
    0.0f, 1.5f, 0.5f,  0,0, 0, 0.0f, 0, 0.0f, 0.0f, false },
[TECH_I2_CHARTE] = {
    "Charte des terres", TBR_SOCIETY, 2, {TECH_I1_COUTUME,NONE}, false, false,
    1.0f, 0.0f, 1.0f,  0,0, 0, -1.0f, 0, 0.0f, 0.0f, false },
[TECH_I3_GRENIERS] = {
    "Greniers communs", TBR_SOCIETY, 2, {TECH_I1_COUTUME,NONE}, false, false,
    0.5f, 0.0f, 0.0f,  0.5f,0, 0, -0.5f, 0, 0.0f, 0.0f, false },
[TECH_I4_CONSEIL] = {
    "Conseil des sphères", TBR_SOCIETY, 3, {TECH_I2_CHARTE,NONE}, false, false,
    0.0f, 2.5f, 1.0f,  0,0, 0, 0.0f, 0, 0.0f, 0.0f, false },
[TECH_I5_CHANCELLERIE] = {
    "Chancellerie (archive vivante)", TBR_SOCIETY, 3, {TECH_I2_CHARTE,NONE}, false, false,
    3.0f, 0.0f, 0.0f,  0,0, 0, 0.0f, 0, 0.0f, 0.0f, false },
[TECH_I6_INTEGRATION] = {
    "Droit d'intégration", TBR_SOCIETY, 4, {TECH_I4_CONSEIL,TECH_I5_CHANCELLERIE}, false, false,
    0.0f, 1.0f, 0.0f,  0,0, 0, -2.0f, 0, 0.0f, 0.0f, false },
[TECH_I7_PACTE] = {
    "Pacte des peuples", TBR_SOCIETY, 5, {TECH_I6_INTEGRATION,NONE}, false, true,
    2.0f, 4.0f, 2.0f,  0,0, 0, -1.0f, 0, 0.0f, 0.0f, false },

/* ---- Branche II — FORGE (eco/mil immédiats, bascule en profondeur) ----- */
[TECH_II1_METALLURGIE] = {
    "Métallurgie", TBR_FORGE, 1, {NONE,NONE}, false, false,
    0,0,0,  1.0f,1.0f, 0, 0.0f, 0, 0.05f, 0.3f, false },
[TECH_II2_HYDRAULIQUE] = {
    "Génie hydraulique", TBR_FORGE, 2, {TECH_II1_METALLURGIE,NONE}, false, false,
    0,0,0,  2.0f,0.0f, 0, 0.0f, 0, 0.05f, 0.3f, false },
[TECH_II3_FONDERIE] = {
    "Fonderie", TBR_FORGE, 2, {TECH_II1_METALLURGIE,NONE}, false, false,
    0,0,0,  0.0f,2.0f, 0, 0.0f, 0, 0.10f, 0.6f, false },
[TECH_II4_MANUFACTURE] = {
    "Manufacture", TBR_FORGE, 3, {TECH_II2_HYDRAULIQUE,TECH_II3_FONDERIE}, false, false,
    0,0,0,  3.0f,0.0f, 0, 1.0f, 0, 0.30f, 1.0f, false },
[TECH_II5_GUERRE] = {
    "Ingénierie de guerre", TBR_FORGE, 3, {TECH_II3_FONDERIE,NONE}, false, false,
    0,0,0,  0.0f,3.0f, 0, 0.0f, 0, 0.20f, 1.0f, false },
[TECH_II6_FORGE_PROF] = {
    "Forge profonde", TBR_FORGE, 4, {TECH_II4_MANUFACTURE,NONE}, false, false,
    0,0,0,  1.0f,1.0f, 1.0f, 2.5f, 3.0f, 0.60f, 2.0f, false },
[TECH_II7_INDUSTRIE] = {
    "Industrie de masse", TBR_FORGE, 4, {TECH_II4_MANUFACTURE,NONE}, false, false,
    0,0,0,  4.0f,2.0f, 0, 1.5f, 0, 1.00f, 3.0f, false },
[TECH_II8_OEUVRE] = {
    "L'Œuvre noire", TBR_FORGE, 5, {TECH_II6_FORGE_PROF,TECH_II7_INDUSTRIE}, false, true,
    0,0,0,  5.0f,5.0f, 3.0f, 2.0f, 2.0f, 1.50f, 5.0f, false },

/* ---- Branche III — MAGIE (puissance brute, charge ↑↑, appelle la fin) -- */
[TECH_III1_SAVOIR] = {
    "Savoir ancien", TBR_MAGIC, 1, {NONE,NONE}, true, false,
    0,0,0,  0,0, 0, 0.0f, 1.0f, 0.50f, 1.5f, false },
[TECH_III2_RUNES] = {
    "Runes liantes", TBR_MAGIC, 2, {TECH_III1_SAVOIR,NONE}, false, false,
    0,0,0,  0.0f,2.0f, 0, 0.0f, 1.0f, 1.00f, 2.0f, false },
[TECH_III3_MIROIRS] = {
    "Miroirs lointains", TBR_MAGIC, 2, {TECH_III1_SAVOIR,NONE}, false, false,
    0,-0.5f,0,  0,0, 0, 0.0f, 0.5f, 0.80f, 2.0f, false },
[TECH_III4_ELEMENTS] = {
    "Maîtrise des éléments", TBR_MAGIC, 3, {TECH_III2_RUNES,NONE}, false, false,
    0,0,0,  2.0f,3.0f, 0, 0.0f, 2.0f, 2.00f, 3.0f, false },
[TECH_III5_PACTES] = {
    "Pactes anciens", TBR_MAGIC, 3, {TECH_III2_RUNES,NONE}, false, false,
    0,0,0,  0,0, 0, 0.0f, 5.0f, 2.50f, 4.0f, false },
[TECH_III6_EVEIL] = {
    "L'Éveil", TBR_MAGIC, 4, {TECH_III4_ELEMENTS,TECH_III5_PACTES}, false, false,
    0,0,0,  0,0, 0, 0.0f, 6.0f, 3.00f, 6.0f, true },
[TECH_III7_COURONNE] = {
    "La Couronne ardente", TBR_MAGIC, 5, {TECH_III6_EVEIL,NONE}, false, true,
    0,0,0,  0,0, 0, 0.0f, 8.0f, 4.00f, 8.0f, true },
};

/* NB : III.6 a prereq {III.4, III.5} dans la table, mais le doc dit
 * « III.4 OU III.5 ». On gère ce OU dans tech_can_research. */

/* ====================================================================== */
/* RECETTES DE FUSION (§7.2)                                               */
/* ====================================================================== */
static const FusionRecipe FUSIONS[FUSION_COUNT] = {
    { "Poudre noire", ING_COMBURANT, ING_COMBUSTIBLE, TECH_II3_FONDERIE,
      2.0f, 0.0f, 0.0f, 0.0f },
    { "Acier",        ING_COMBUSTIBLE, ING_MINERAI,   TECH_II6_FORGE_PROF,
      2.0f, 0.0f, 0.0f, 0.2f },
    { "Béton/ciment", ING_LIANT, ING_MINERAI,         TECH_II2_HYDRAULIQUE,
      0.0f, 1.5f, 0.0f, 0.0f },
    { "Armes enchantées", ING_MINERAI, ING_CATALYSEUR, TECH_III2_RUNES,
      3.0f, 0.0f, 1.0f, 1.0f },
    { "Cœur de pacte", ING_CATALYSEUR, ING_CATALYSEUR, TECH_III5_PACTES,
      0.0f, 0.0f, 2.0f, 3.0f },
};

const FusionRecipe *tech_fusion_table(void) { return FUSIONS; }

/* ====================================================================== */
/* API                                                                    */
/* ====================================================================== */

void tech_state_init(TechState *s, bool has_ruins_access) {
    for (int i=0;i<TECH_COUNT;i++) s->unlocked[i]=false;
    s->n_unlocked=0;
    /* Socle de départ neutre : un empire commence avec un peu de K/L/F. */
    s->K=3.0f; s->L=3.0f; s->F=2.0f;
    s->eco=0.f; s->mil=0.f; s->puissance=0.f;
    s->H=0.f; s->fracture=0.f; s->charge=0.f;
    s->has_ruins_access=has_ruins_access;
    s->crisis_triggered=false;
}

const char *tech_name(TechId id) {
    return (id>=0 && id<TECH_COUNT) ? NODES[id].name : "?";
}
const char *tech_branch_name(TechBranch b) {
    static const char *N[TBR_COUNT]={"Société","Forge","Magie"};
    return (b>=0&&b<TBR_COUNT)?N[b]:"?";
}
const TechNode *tech_node(TechId id) {
    return (id>=0 && id<TECH_COUNT) ? &NODES[id] : NULL;
}

bool tech_can_research(const TechState *s, TechId id) {
    if (id<0||id>=TECH_COUNT) return false;
    if (s->unlocked[id]) return false;
    const TechNode *n=&NODES[id];
    /* Porte d'entrée de la Magie : accès à des ruines/reliques. */
    if (n->needs_ruins && !s->has_ruins_access) return false;

    /* Cas particulier III.6 : prérequis « III.4 OU III.5 ». */
    if (id==TECH_III6_EVEIL)
        return s->unlocked[TECH_III4_ELEMENTS] || s->unlocked[TECH_III5_PACTES];

    /* Cas général : TOUS les prérequis listés doivent être acquis. */
    for (int k=0;k<2;k++) {
        TechId pr=n->prereq[k];
        if (pr==NONE) continue;
        if (!s->unlocked[pr]) return false;
    }
    return true;
}

bool tech_research(TechState *s, TechId id) {
    if (!tech_can_research(s,id)) return false;
    const TechNode *n=&NODES[id];
    s->K        += n->dK;
    s->L        += n->dL;
    s->F        += n->dF;
    s->eco      += n->dEco;
    s->mil      += n->dMil;
    s->H        += n->dH;
    s->fracture += n->dFracture; if (s->fracture<0.f) s->fracture=0.f;
    s->puissance+= n->dPuissance;
    s->charge   += n->charge;          /* sens unique : jamais remboursé */
    if (n->triggers_crisis) s->crisis_triggered=true;
    s->unlocked[id]=true;
    s->n_unlocked++;
    return true;
}

float tech_flux(const TechState *s) {
    float f=0.f;
    for (int i=0;i<TECH_COUNT;i++)
        if (s->unlocked[i]) f+=NODES[i].flux;
    return f;
}

float tech_dereal(const TechState *s) {
    float flux=tech_flux(s);
    float d = DEREAL_P_COEF*s->puissance*s->charge + flux - s->K;
    return (d>0.f)?d:0.f;
}

float tech_crisis_proximity(const TechState *s) {
    if (s->crisis_triggered) return 1.0f;
    /* courbe saturante : 1 − exp(−C/échelle) */
    return 1.0f - expf(-s->charge/CRISIS_SCALE);
}

/* Somme des charges des nœuds magiques pris (l'ampleur du dragon). */
static float magic_charge(const TechState *s) {
    float m=0.f;
    for (int i=0;i<TECH_COUNT;i++)
        if (s->unlocked[i] && NODES[i].branch==TBR_MAGIC) m+=NODES[i].charge;
    return m;
}

float tech_shock_amplitude(const TechState *s) {
    return s->charge * (1.0f + SHOCK_MAGIE*magic_charge(s));
}

float tech_fragility(const TechState *s) {
    /* fracture rapportée à l'ordre consenti : un L solide encaisse, un L
     * effondré (coercition) craque. ≥5 ≈ on ne plie pas, on casse. */
    float order = s->L - s->H;          /* ordre net (légitime − coercitif) */
    if (order < 0.5f) order = 0.5f;
    return s->fracture / order;
}

bool tech_fusion_available(const TechState *s, int recipe_idx,
                           const bool has_ingredient[ING_COUNT]) {
    if (recipe_idx<0||recipe_idx>=FUSION_COUNT) return false;
    const FusionRecipe *r=&FUSIONS[recipe_idx];
    if (!s->unlocked[r->enabler]) return false;
    if (!has_ingredient[r->in1]) return false;
    if (!has_ingredient[r->in2]) return false;
    return true;
}
