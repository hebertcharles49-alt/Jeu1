/*
 * scps_tech.c — arbre concentrique & fractal (voir scps_tech.h)
 *
 * Table de nœuds data-driven : 9 quartiers (3 thèmes × 3 fonctions), rayon = tier.
 * Aucune dépendance au reste du moteur : ce module n'écrit QUE dans un TechState
 * et répond des LECTURES (coût, accès, coordonnées). Le branchement IA/sim/UI se
 * fait par l'appelant (qui paie le coût, fournit le masque de races, lit l'arbre).
 */
#include "scps_tech.h"
#include <math.h>
#include <stddef.h>

#define NONE TECH_COUNT     /* sentinelle « pas de prérequis » */
#define UNIV RACE_COUNT     /* sentinelle « tech universelle (pas de race native) » */

/* ---- Constantes de calibrage (surface d'équilibrage) ------------------ */
#define CRISIS_SCALE    12.0f   /* échelle de la courbe proximité = f(charge) */
#define SHOCK_MAGIE      0.50f   /* l'arcane gonfle l'ampleur du choc */
#define DEREAL_P_COEF    0.10f   /* terme (P/10)·C */
/* COÛT ∝ étendue ∝ population (§5) */
#define EXTENT_W         0.6f    /* poids de l'étendue sur le coût (le seul levier) */
#define EXTENT_POP_BASE  5000.f  /* population de référence (1 « cran » d'étendue) */
static const float BASE_COST[6] = { 0.f, 40.f, 90.f, 160.f, 260.f, 400.f }; /* par tier (rayon) */

/* ====================================================================== */
/* TABLE DES NŒUDS — 9 quartiers (angle), tier (rayon)                     */
/* Champs : name, unlocks, theme, func, tier, prereq, faustian, needs_ruins,
 *          native, dK,dL,dF, dEco,dMil, dH, dFracture, dPuissance, flux,
 *          charge, triggers_crisis                                        */
/* ====================================================================== */
static const TechNode NODES[TECH_COUNT] = {
/* ---- SAVOIR · PRODUCTION (spine sûre : vitesse de recherche, +K) ------ */
[TECH_BIBLIOTHEQUE] = { "Bibliothèque","Bibliothèque", THM_SAVOIR,FN_PRODUCTION,0, NONE, false,false,UNIV,
    1,0,0, 0,0, 0, 0, 0, 0, 0, false },
[TECH_SCRIPTORIUM] = { "Scriptorium","Scriptorium", THM_SAVOIR,FN_PRODUCTION,1, TECH_BIBLIOTHEQUE, false,false,UNIV,
    1,0,0, 0,0, 0, 0, 0, 0, 0, false },
[TECH_ACADEMIE] = { "Académie","Académie", THM_SAVOIR,FN_PRODUCTION,2, TECH_SCRIPTORIUM, false,false,UNIV,
    2,0,0, 0,0, 0, 0, 0, 0, 0, false },
[TECH_UNIVERSITE] = { "Université","Université", THM_SAVOIR,FN_PRODUCTION,3, TECH_ACADEMIE, false,false,UNIV,
    3,0,0, 0,0, 0, 0, 0, 0, 0, false },
/* ---- SAVOIR · ARMÉE (arcane offensif — faustien) --------------------- */
[TECH_SAVOIR_GUERRE] = { "Savoir de guerre","Collège de guerre", THM_SAVOIR,FN_ARMEE,1, TECH_BIBLIOTHEQUE, false,false,UNIV,
    0,0,0, 0,1.0f, 0, 0, 0, 0.05f, 0.3f, false },
[TECH_MAGIE_BATAILLE] = { "Magie de bataille","Tour de mages", THM_SAVOIR,FN_ARMEE,2, TECH_SAVOIR_GUERRE, false,false,UNIV,
    0,0,0, 0,2.0f, 0, 0, 1.0f, 0.50f, 1.5f, false },
[TECH_INVOCATION] = { "Invocation","Cercle d'invocation", THM_SAVOIR,FN_ARMEE,3, TECH_MAGIE_BATAILLE, true,true,RACE_ELFE,
    0,0,0, 0,2.0f, 0, 0, 3.0f, 1.50f, 3.0f, false },
[TECH_EVEIL] = { "L'Éveil","Le Réveil (armée sans pop)", THM_SAVOIR,FN_ARMEE,4, TECH_MAGIE_BATAILLE, true,true,UNIV,
    0,0,0, 0,0, 0, 0, 6.0f, 3.00f, 6.0f, true },
/* ---- SAVOIR · RENFORCEMENT (arcane durable — faustien) --------------- */
[TECH_WARDS] = { "Gardes runiques","Gardes runiques (Wards)", THM_SAVOIR,FN_RENFORCEMENT,1, TECH_BIBLIOTHEQUE, false,false,UNIV,
    0,0.5f,1.0f, 0,0, 0, 0, 0, 0, 0.3f, false },
[TECH_SCRYING] = { "Scrying","Bassin de scrying", THM_SAVOIR,FN_RENFORCEMENT,2, TECH_WARDS, false,false,UNIV,
    0,0,0.5f, 0,0, 0, 0, 0.5f, 0.30f, 1.0f, false },
[TECH_COMMUNION] = { "Communion","Bosquet de communion", THM_SAVOIR,FN_RENFORCEMENT,3, TECH_SCRYING, false,false,RACE_ELFE,
    0,1.0f,2.0f, 0,0, 0, -1.0f, 0.5f, 0.10f, 0.5f, false },
[TECH_SAVOIR_INTERDIT] = { "Savoir interdit","Crypte interdite", THM_SAVOIR,FN_RENFORCEMENT,4, TECH_SCRYING, true,true,UNIV,
    0,0,0, 0,0, 0, 0, 4.0f, 2.00f, 4.0f, false },

/* ---- FORGE · PRODUCTION (sortie — le multiplicateur de rendement) ----- */
[TECH_COLLECTE_BOIS] = { "Collecte de bois","Camp de bûcherons", THM_FORGE,FN_PRODUCTION,0, NONE, false,false,UNIV,
    0,0,0, 0.5f,0, 0, 0, 0, 0, 0, false },
[TECH_COLLECTE_ARGILE] = { "Collecte d'argile","Carrière d'argile", THM_FORGE,FN_PRODUCTION,0, NONE, false,false,UNIV,
    0,0,0, 0.5f,0, 0, 0, 0, 0, 0, false },
[TECH_FONDERIE] = { "Fonderie","Fonderie", THM_FORGE,FN_PRODUCTION,1, TECH_COLLECTE_BOIS, false,false,UNIV,
    0,0,0, 1.5f,0, 0, 0, 0, 0.05f, 0.3f, false },
[TECH_OUTILLAGE] = { "Outillage","Atelier d'outillage", THM_FORGE,FN_PRODUCTION,2, TECH_FONDERIE, false,false,UNIV,
    0,0,0, 2.5f,0, 0, 0, 0, 0.05f, 0.3f, false },
[TECH_MANUFACTURE] = { "Manufacture","Manufacture", THM_FORGE,FN_PRODUCTION,3, TECH_OUTILLAGE, false,false,UNIV,
    0,0,0, 3.0f,0, 0, 1.0f, 0, 0.30f, 1.0f, false },
[TECH_INDUSTRIE] = { "Industrie de masse","Complexe industriel", THM_FORGE,FN_PRODUCTION,4, TECH_MANUFACTURE, false,false,UNIV,
    0,0,0, 4.0f,2.0f, 0, 1.5f, 0, 1.00f, 3.0f, false },
/* ---- FORGE · ARMÉE (armes — faustien) -------------------------------- */
[TECH_ARMURERIE] = { "Armurerie","Armurerie", THM_FORGE,FN_ARMEE,1, TECH_COLLECTE_BOIS, false,false,UNIV,
    0,0,0, 0,1.5f, 0, 0, 0, 0, 0.3f, false },
[TECH_POUDRIERE] = { "Poudrière","Poudrière", THM_FORGE,FN_ARMEE,2, TECH_ARMURERIE, false,false,UNIV,
    0,0,0, 0,2.5f, 0, 0, 0, 0.20f, 1.0f, false },
[TECH_FORGE_RUNES] = { "Forge à runes","Forge céleste", THM_FORGE,FN_ARMEE,3, TECH_POUDRIERE, true,false,RACE_NAIN,
    0,0,0, 0,3.0f, 0, 0, 3.0f, 1.00f, 2.0f, false },
[TECH_OEUVRE_NOIRE] = { "L'Œuvre noire","L'Œuvre noire", THM_FORGE,FN_ARMEE,4, TECH_POUDRIERE, true,false,UNIV,
    0,0,0, 2.0f,5.0f, 3.0f, 2.0f, 2.0f, 1.50f, 5.0f, false },
/* ---- FORGE · RENFORCEMENT (durabilité / fortification) ---------------- */
[TECH_ATELIER] = { "Atelier de construction","Atelier de construction", THM_FORGE,FN_RENFORCEMENT,0, NONE, false,false,UNIV,
    0,0,0.5f, 0,0, 0, 0, 0, 0, 0, false },
[TECH_QUALITE_MATERIAUX] = { "Qualité des matériaux","Chantier (béton→marbre)", THM_FORGE,FN_RENFORCEMENT,1, TECH_ATELIER, false,false,UNIV,
    0,0,1.0f, 0.5f,0, 0, 0, 0, 0, 0, false },
[TECH_FORTIFICATIONS] = { "Fortifications","Forteresse → Citadelle", THM_FORGE,FN_RENFORCEMENT,2, TECH_QUALITE_MATERIAUX, false,false,UNIV,
    0,0,1.5f, 0,1.0f, 0, 0, 0, 0, 0.2f, false },
[TECH_AUTOMATES] = { "Automates","Grand Engrenage (Golems)", THM_FORGE,FN_RENFORCEMENT,3, TECH_FORTIFICATIONS, true,false,RACE_GNOME,
    0,0,0, 3.0f,3.0f, 0, 0, 1.0f, 1.00f, 2.0f, false },

/* ---- SOCIÉTÉ · PRODUCTION (croissance / commerce / impôt — sûre) ------ */
[TECH_COLLECTE_NOURRITURE] = { "Collecte de nourriture","Collecte (liée au biome)", THM_SOCIETE,FN_PRODUCTION,0, NONE, false,false,UNIV,
    0,0,0, 0.5f,0, 0, 0, 0, 0, 0, false },
[TECH_IRRIGATION] = { "Irrigation & greniers","Greniers communs", THM_SOCIETE,FN_PRODUCTION,1, TECH_COLLECTE_NOURRITURE, false,false,UNIV,
    0,0,0.5f, 1.0f,0, 0, -0.5f, 0, 0, 0, false },
[TECH_COMMERCE] = { "Commerce","Marché → Banque", THM_SOCIETE,FN_PRODUCTION,2, TECH_IRRIGATION, false,false,UNIV,
    0,0,0, 2.0f,0, 0, 0, 0, 0, 0, false },
[TECH_CADASTRE] = { "Cadastre","Cadastre (impôt)", THM_SOCIETE,FN_PRODUCTION,3, TECH_COMMERCE, false,false,UNIV,
    0,0.5f,0, 1.5f,0, 0, 0, 0, 0, 0, false },
[TECH_ABONDANCE] = { "Abondance","Grenier d'abondance", THM_SOCIETE,FN_PRODUCTION,3, TECH_COMMERCE, false,false,RACE_HALFELIN,
    0,1.0f,0, 3.0f,0, 0, -0.5f, 0, 0, 0, false },
/* ---- SOCIÉTÉ · ARMÉE (levée — faustien : l'esclavage) ---------------- */
[TECH_CASERNE] = { "Caserne","Caserne", THM_SOCIETE,FN_ARMEE,0, NONE, false,false,UNIV,
    0,0,0, 0,0.5f, 0, 0, 0, 0, 0, false },
[TECH_CONSCRIPTION] = { "Conscription","Levée / Conscription", THM_SOCIETE,FN_ARMEE,1, TECH_CASERNE, false,false,UNIV,
    0,0,0, 0,1.5f, 0, 0, 0, 0, 0, false },
[TECH_ORGANISATION] = { "Organisation militaire","État-major", THM_SOCIETE,FN_ARMEE,2, TECH_CONSCRIPTION, false,false,UNIV,
    0,0,0.5f, 0,2.0f, 0, 0, 0, 0, 0, false },
[TECH_ESCLAVAGE] = { "Économie servile","Marché aux esclaves", THM_SOCIETE,FN_ARMEE,3, TECH_ORGANISATION, true,false,RACE_ORQUE,
    0,0,0, 3.0f,2.0f, 1.0f, 3.0f, 0, 0, 2.0f, false },
[TECH_CASTE_MARTIALE] = { "Caste martiale","Caste martiale", THM_SOCIETE,FN_ARMEE,4, TECH_ORGANISATION, true,false,UNIV,
    0,0,0, 0,4.0f, 2.0f, 2.0f, 0, 0, 2.5f, false },
/* ---- SOCIÉTÉ · RENFORCEMENT (K / L / intégration — la spine résiliente) */
[TECH_CHANCELLERIE] = { "Chancellerie","Tribunal / Chancellerie", THM_SOCIETE,FN_RENFORCEMENT,1, TECH_COLLECTE_NOURRITURE, false,false,UNIV,
    3.0f,0,0, 0,0, 0, 0, 0, 0, 0, false },
[TECH_FOI] = { "Foi","Temple → Cathédrale", THM_SOCIETE,FN_RENFORCEMENT,2, TECH_CHANCELLERIE, false,false,UNIV,
    0,3.0f,0, 0,0, 0, 0, 0, 0, 0, false },
[TECH_INTEGRATION] = { "Droit d'intégration","Creuset (assimilation)", THM_SOCIETE,FN_RENFORCEMENT,3, TECH_FOI, false,false,RACE_HUMAIN,
    0,1.0f,0, 0,0, 0, -3.0f, 0, 0, 0, false },
[TECH_CULTE_IMPERIAL] = { "Culte impérial","Mythe homogénéisant", THM_SOCIETE,FN_RENFORCEMENT,4, TECH_FOI, true,false,UNIV,
    1.0f,2.0f,0, 0,0, 0, -2.0f, 0, 0.50f, 3.0f, false },
};

/* ====================================================================== */
/* RECETTES DE FUSION (intrants géologiques + enabler)                     */
/* ====================================================================== */
static const FusionRecipe FUSIONS[FUSION_COUNT] = {
    { "Poudre noire",     ING_COMBURANT,   ING_COMBUSTIBLE, TECH_FONDERIE,         2.0f,0.0f,0.0f,0.0f },
    { "Acier",            ING_COMBUSTIBLE, ING_MINERAI,     TECH_MANUFACTURE,      2.0f,0.0f,0.0f,0.2f },
    { "Béton/ciment",     ING_LIANT,       ING_MINERAI,     TECH_QUALITE_MATERIAUX,0.0f,1.5f,0.0f,0.0f },
    { "Armes enchantées", ING_MINERAI,     ING_CATALYSEUR,  TECH_FORGE_RUNES,      3.0f,0.0f,1.0f,1.0f },
    { "Cœur de pacte",    ING_CATALYSEUR,  ING_CATALYSEUR,  TECH_SAVOIR_INTERDIT,  0.0f,0.0f,2.0f,3.0f },
};
const FusionRecipe *tech_fusion_table(void) { return FUSIONS; }

/* ====================================================================== */
/* API                                                                    */
/* ====================================================================== */
void tech_state_init(TechState *s, bool has_ruins_access) {
    for (int i=0;i<TECH_COUNT;i++) s->unlocked[i]=false;
    s->n_unlocked=0;
    /* Socle de départ : un peu de K/L/F, et les 6 BÂTIMENTS DE BASE (centre)
     * déjà acquis — au début, rien d'autre. */
    s->K=3.0f; s->L=3.0f; s->F=2.0f;
    s->eco=0.f; s->mil=0.f; s->puissance=0.f;
    s->H=0.f; s->fracture=0.f; s->charge=0.f;
    s->has_ruins_access=has_ruins_access;
    s->crisis_triggered=false;
    for (int i=0;i<TECH_COUNT;i++) if (NODES[i].tier==0){ s->unlocked[i]=true; s->n_unlocked++; }
}

const TechNode *tech_node(TechId id){ return (id>=0&&id<TECH_COUNT)?&NODES[id]:NULL; }
const char *tech_name(TechId id){ return (id>=0&&id<TECH_COUNT)?NODES[id].name:"?"; }
const char *tech_unlocks(TechId id){ return (id>=0&&id<TECH_COUNT)?NODES[id].unlocks:"?"; }
const char *tech_theme_name(TechTheme t){
    static const char *N[THM_COUNT]={"Savoir","Forge","Société"};
    return (t>=0&&t<THM_COUNT)?N[t]:"?";
}
const char *tech_function_name(TechFunction f){
    static const char *N[FN_COUNT]={"Production","Armée","Renforcement"};
    return (f>=0&&f<FN_COUNT)?N[f]:"?";
}
int  tech_quarter(TechTheme t, TechFunction f){ return (int)t*FN_COUNT + (int)f; }
bool tech_is_base(TechId id){ return (id>=0&&id<TECH_COUNT)&&NODES[id].tier==0; }

unsigned tech_race_bit(SpeciesArchetype r){ return (r>=0&&r<RACE_COUNT)?(1u<<r):0u; }

bool tech_can_research(const TechState *s, TechId id, unsigned race_access) {
    if (id<0||id>=TECH_COUNT) return false;
    if (s->unlocked[id]) return false;
    const TechNode *n=&NODES[id];
    /* ACCÈS DE RACE : une tech native d'une race est ORPHELINE ailleurs — il faut
     * avoir cette race dans sa population (conquise/migrée) ou un pacte. */
    if (n->native!=UNIV && !(race_access & tech_race_bit(n->native))) return false;
    /* Porte arcane : les bouts faustiens du Savoir profond exigent une ruine. */
    if (n->needs_ruins && !s->has_ruins_access) return false;
    /* Prérequis : le nœud précédent du quartier doit être acquis. */
    if (n->prereq!=NONE && !s->unlocked[n->prereq]) return false;
    return true;
}

bool tech_research(TechState *s, TechId id, unsigned race_access) {
    if (!tech_can_research(s,id,race_access)) return false;
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

float tech_cost(TechId id, float population){
    const TechNode *n=tech_node(id);
    if (!n) return 0.f;
    int t=n->tier; if (t<0) t=0; if (t>5) t=5;
    float extent = (population>0.f?population:0.f) / EXTENT_POP_BASE;   /* l'étendue ∝ pop */
    return BASE_COST[t] * (1.f + EXTENT_W*extent);
}

/* ---- La Brèche (verrou SCPS, inchangé) -------------------------------- */
float tech_flux(const TechState *s) {
    float f=0.f;
    for (int i=0;i<TECH_COUNT;i++) if (s->unlocked[i]) f+=NODES[i].flux;
    return f;
}
float tech_dereal(const TechState *s) {
    float d = DEREAL_P_COEF*s->puissance*s->charge + tech_flux(s) - s->K;
    return (d>0.f)?d:0.f;
}
float tech_crisis_proximity(const TechState *s) {
    if (s->crisis_triggered) return 1.0f;
    return 1.0f - expf(-s->charge/CRISIS_SCALE);
}
/* Somme des charges des nœuds ARCANES pris (Savoir faustien = l'ampleur du dragon). */
static float arcane_charge(const TechState *s) {
    float m=0.f;
    for (int i=0;i<TECH_COUNT;i++)
        if (s->unlocked[i] && NODES[i].theme==THM_SAVOIR && NODES[i].faustian) m+=NODES[i].charge;
    return m;
}
float tech_shock_amplitude(const TechState *s) {
    return s->charge * (1.0f + SHOCK_MAGIE*arcane_charge(s));
}
float tech_fragility(const TechState *s) {
    float order = s->L - s->H;
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
