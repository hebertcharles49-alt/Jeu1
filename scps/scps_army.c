/*
 * scps_army.c — recrutement, armes, contres, combat au dé (voir scps_army.h)
 *
 * Une unité coûte pop (classe) + armes fabriquées + matériaux + temps. Le combat
 * est non déterministe : un jet de dé par contact, pondéré par le contre (table
 * redondante) et les stats. Le contre prime sur la qualité brute.
 */
#include "scps_army.h"
#include <string.h>
#include <math.h>

/* ---- Calibrage (surface d'équilibrage) -------------------------------- */
#define ARM_THRESHOLD   12     /* d20 + commandement ≥ 12 → le contact porte */
#define ARM_BASE_DMG    18.f
#define ARM_FLANK_GAP   3.f    /* écart de mouvement pour un débordement (contact bonus) */
#define ARM_MAX_ROUNDS  40
#define M_BEAT          2.0f   /* le contre frappe fort (prime sur la qualité) */
#define M_LOSE          0.5f
#define M_NEUTRAL       1.0f
#define RECRUIT_MAT     1      /* coût matériaux par paquet de 100 (hors arme) */

/* ---- Définitions d'unités (§2, §5) ------------------------------------ */
static const UnitDef UNITS[U_COUNT] = {
    [U_PIQUIER]   = { "Piquier",     LAB_LABORER, W_PIQUE,     0.30f, 100.f, 2.f, 3.f },
    [U_LANCIER]   = { "Lancier",     LAB_LABORER, W_LANCE,     0.30f,  95.f, 3.f, 3.f },
    [U_EPEISTE]   = { "Épéiste",     LAB_LABORER, W_EPEE,      0.45f, 100.f, 3.f, 4.f },
    [U_ARCHER]    = { "Archer",      LAB_LABORER, W_ARC,       0.20f,  70.f, 3.f, 3.f },
    [U_ARBALETE]  = { "Arbalétrier", LAB_LABORER, W_ARBALETE,  0.25f, 100.f, 2.f, 4.f },
    [U_CAV_LEGERE]= { "Cav. légère", LAB_ELITE,   W_MONTURE_L, 0.50f,  90.f, 8.f, 6.f },
    [U_CAV_LOURDE]= { "Cav. lourde", LAB_ELITE,   W_MONTURE_H, 0.70f, 115.f, 6.f, 7.f },
    [U_MAGE]      = { "Mage",        LAB_ELITE,   W_BATON,     0.30f,  65.f, 4.f, 6.f },
};
const UnitDef *unit_def(UnitType t){ return (t>=0&&t<U_COUNT)?&UNITS[t]:NULL; }
const char    *unit_name(UnitType t){ return (t>=0&&t<U_COUNT)?UNITS[t].name:"?"; }

/* ---- Recettes d'armes (§1) -------------------------------------------- */
static const WeaponRecipe WEAPONS[W_COUNT] = {
    [W_PIQUE]     = { LR_BOIS,   LR_METAL,      2 },
    [W_LANCE]     = { LR_BOIS,   LR_METAL,      2 },
    [W_EPEE]      = { LR_METAL,  LR_OUTILS,     3 },
    [W_ARC]       = { LR_BOIS,   LR_OUTILS,     2 },
    [W_ARBALETE]  = { LR_BOIS,   LR_OUTILS,     4 },
    [W_MONTURE_L] = { LR_OUTILS, LR_MATERIALS,  3 },
    [W_MONTURE_H] = { LR_METAL,  LR_OUTILS,     5 },
    [W_BATON]     = { LR_OUTILS, LR_MATERIALS,  4 },
};
const WeaponRecipe *weapon_recipe(ArmWeapon wp){ return (wp>=0&&wp<W_COUNT)?&WEAPONS[wp]:NULL; }
static const char *WNAMES[W_COUNT]={ "Pique","Lance","Épée","Arc","Arbalète","Monture légère","Destrier","Bâton" };
const char *weapon_name(ArmWeapon wp){ return (wp>=0&&wp<W_COUNT)?WNAMES[wp]:"?"; }

/* ===================================================================== */
/* LE RÉSEAU DE CONTRES (§3) — redondant, plusieurs triangles recouvrants  */
/* ===================================================================== */
static float MATRIX[U_COUNT][U_COUNT];
static bool  g_matrix_ready=false;
static void build_matrix(void){
    for (int i=0;i<U_COUNT;i++) for (int j=0;j<U_COUNT;j++) MATRIX[i][j]=M_NEUTRAL;
    /* a CONTRE b → MATRIX[a][b]=BEAT, MATRIX[b][a]=LOSE (sans écraser un BEAT). */
    struct { UnitType a, b; } C[] = {
        {U_PIQUIER,U_CAV_LEGERE}, {U_PIQUIER,U_CAV_LOURDE},     /* la pique brise la charge */
        {U_LANCIER,U_CAV_LEGERE}, {U_LANCIER,U_CAV_LOURDE},
        {U_EPEISTE,U_PIQUIER},    {U_EPEISTE,U_LANCIER},        /* la mêlée défait les hampes */
        {U_ARCHER, U_PIQUIER},    {U_ARCHER, U_LANCIER},        /* la flèche pique l'infanterie lente */
        {U_ARBALETE,U_CAV_LOURDE},{U_ARBALETE,U_MAGE},          /* le carreau perce l'armure et l'arcane */
        {U_CAV_LEGERE,U_ARCHER},  {U_CAV_LEGERE,U_ARBALETE}, {U_CAV_LEGERE,U_MAGE}, /* la vitesse croque les tireurs */
        {U_CAV_LOURDE,U_EPEISTE}, {U_CAV_LOURDE,U_ARCHER},      /* le choc enfonce la piétaille */
        {U_MAGE,U_PIQUIER},{U_MAGE,U_LANCIER},{U_MAGE,U_EPEISTE},{U_MAGE,U_ARCHER},{U_MAGE,U_CAV_LOURDE}, /* le mage écrase les 2/3 */
    };
    int n=(int)(sizeof(C)/sizeof(C[0]));
    for (int k=0;k<n;k++){
        MATRIX[C[k].a][C[k].b]=M_BEAT;
        if (MATRIX[C[k].b][C[k].a]==M_NEUTRAL) MATRIX[C[k].b][C[k].a]=M_LOSE;
    }
    g_matrix_ready=true;
}
float matchup(UnitType a, UnitType b){
    if (!g_matrix_ready) build_matrix();
    if (a<0||a>=U_COUNT||b<0||b>=U_COUNT) return M_NEUTRAL;
    return MATRIX[a][b];
}

/* ===================================================================== */
/* ARMES & RECRUTEMENT (§0, §1)                                           */
/* ===================================================================== */
void army_init(ArmyState *a){ memset(a,0,sizeof(*a)); }

long army_fabricate_weapon(ArmyState *a, LaborEcon *e, ArmWeapon wp, long qty){
    if (wp<0||wp>=W_COUNT||qty<=0) return 0;
    const WeaponRecipe *R=&WEAPONS[wp];
    long made=0;
    for (long k=0;k<qty;k++){
        /* manque de MATÉRIAUX → on pompe le marché (temps réel) ; manque d'un
         * brut (métal/bois/outils) → il faut l'extraire d'abord (pas de levée). */
        if (e->stock[R->cost_a]<1 && R->cost_a==LR_MATERIALS) labor_pump_market(e,1);
        if (e->stock[R->cost_b]<1 && R->cost_b==LR_MATERIALS) labor_pump_market(e,1);
        if (e->stock[R->cost_a]<1 || e->stock[R->cost_b]<1) break;   /* intrants épuisés */
        e->stock[R->cost_a]-=1; e->stock[R->cost_b]-=1;
        a->weapons[wp]+=1; made++;
    }
    return made;
}

static long class_free(const ArmyState *a, const LaborEcon *e, LaborClass cl){
    long pool=0; for (int i=0;i<e->n_prov;i++) pool+=e->prov[i].pop_by_class[cl];
    long assigned = a->pop_by_class_in_army[cl];
    return pool - assigned;     /* pop de cette classe NON encore affectée à l'armée */
}

bool army_can_recruit(const ArmyState *a, const LaborEcon *e, UnitType t, long count){
    if (t<0||t>=U_COUNT||count<=0) return false;
    const UnitDef *d=&UNITS[t];
    if (a->weapons[d->weapon] < count) return false;                 /* pas d'armes → pas d'unité */
    if (class_free(a,e,d->from) < count*POP_PER_UNIT) return false;  /* pas la bonne classe */
    return true;
}

long army_recruit(ArmyState *a, LaborEcon *e, UnitType t, long count){
    if (!army_can_recruit(a,e,t,count)) return 0;
    const UnitDef *d=&UNITS[t];
    /* coût matériaux (pompe si manque) */
    long mat = count*RECRUIT_MAT;
    if (e->stock[LR_MATERIALS] < mat) labor_pump_market(e, mat - e->stock[LR_MATERIALS]);
    if (e->stock[LR_MATERIALS] < mat) return 0;
    e->stock[LR_MATERIALS] -= mat;
    a->weapons[d->weapon]  -= count;                 /* consomme les armes */
    /* prélève la pop : AFFECTÉE à l'armée, mais toujours dans le POOL (se reproduit). */
    a->pop_by_class_in_army[d->from] += count*POP_PER_UNIT;
    if (e->n_prov>0) e->prov[0].pop_in_army += count*POP_PER_UNIT;   /* assignation, pas retrait */
    /* fusionne dans une unité existante du même type, sinon en crée une. */
    for (int i=0;i<a->n_units;i++) if (a->units[i].type==t){ a->units[i].count+=count; return count; }
    if (a->n_units<ARMY_MAX_UNITS){
        Unit *u=&a->units[a->n_units++];
        u->type=t; u->count=count; u->moral_courant=0.f;
    }
    return count;
}

/* ===================================================================== */
/* LE COMBAT AU DÉ (§4)                                                   */
/* ===================================================================== */
static uint32_t xs32(uint32_t *s){ uint32_t x=*s; x^=x<<13; x^=x>>17; x^=x<<5; return *s=x?x:1u; }
static int roll_d20(uint32_t *s){ return (int)(xs32(s)%20u)+1; }

bool arm_hit(float commandement, int roll){ return roll + (int)commandement >= ARM_THRESHOLD; }

float arm_damage(UnitType a, UnitType b, long count_a, float disc_b, float terrain){
    float poids = matchup(a,b) * (1.f + UNITS[a].discipline) * terrain;   /* contre × discipline */
    float dmg   = poids * ARM_BASE_DMG * (float)count_a;
    dmg        *= 1.f / (1.f + disc_b);                                   /* la discipline de b réduit */
    return dmg;
}

/* un contact : a frappe b (jet pondéré). */
static void resolve_contact(Unit *a, Unit *b, float terrain, uint32_t *rng){
    if (a->count<=0 || b->moral_courant<=0.f) return;
    if (!arm_hit(UNITS[a->type].commandement, roll_d20(rng))) return;     /* le dé + commandement */
    b->moral_courant -= arm_damage(a->type, b->type, a->count, UNITS[b->type].discipline, terrain);
}
static int first_live(const ArmyState *S){
    for (int i=0;i<S->n_units;i++) if (S->units[i].count>0 && S->units[i].moral_courant>0.f) return i;
    return -1;
}
static int count_routed(const ArmyState *S){
    int r=0; for (int i=0;i<S->n_units;i++) if (S->units[i].count>0 && S->units[i].moral_courant<=0.f) r++;
    return r;
}
static int count_present(const ArmyState *S){
    int r=0; for (int i=0;i<S->n_units;i++) if (S->units[i].count>0) r++;
    return r;
}

BattleResult resolve_battle(ArmyState *A, ArmyState *B, float terrainA, uint32_t *rng){
    BattleResult r; memset(&r,0,sizeof r);
    for (int i=0;i<A->n_units;i++) A->units[i].moral_courant = UNITS[A->units[i].type].moral*(float)A->units[i].count;
    for (int i=0;i<B->n_units;i++) B->units[i].moral_courant = UNITS[B->units[i].type].moral*(float)B->units[i].count;
    float terrainB = 1.f/terrainA;       /* le terrain qui sert A dessert B (et inversement) */

    int nA=count_present(A), nB=count_present(B);
    for (int round=0; round<ARM_MAX_ROUNDS; round++){
        r.rounds=round+1;
        /* A frappe B */
        for (int i=0;i<A->n_units;i++){
            if (A->units[i].count<=0 || A->units[i].moral_courant<=0.f) continue;
            int j=first_live(B); if (j<0) break;
            resolve_contact(&A->units[i], &B->units[j], terrainA, rng);
            /* débordement : un mouvement supérieur offre un contact bonus (flanc) —
             * MAIS la mobilité n'amplifie qu'un matchup FAVORABLE : on ne déborde
             * pas un mur qui vous contre frontalement (le contre prime). */
            if (UNITS[A->units[i].type].mouvement > UNITS[B->units[j].type].mouvement + ARM_FLANK_GAP
                && matchup(A->units[i].type, B->units[j].type) >= 1.0f)
                resolve_contact(&A->units[i], &B->units[j], terrainA, rng);
        }
        /* B frappe A */
        for (int i=0;i<B->n_units;i++){
            if (B->units[i].count<=0 || B->units[i].moral_courant<=0.f) continue;
            int j=first_live(A); if (j<0) break;
            resolve_contact(&B->units[i], &A->units[j], terrainB, rng);
            if (UNITS[B->units[i].type].mouvement > UNITS[A->units[j].type].mouvement + ARM_FLANK_GAP
                && matchup(B->units[i].type, A->units[j].type) >= 1.0f)
                resolve_contact(&B->units[i], &A->units[j], terrainB, rng);
        }
        r.routA=count_routed(A); r.routB=count_routed(B);
        if (r.routB>=nB){ r.winner=-1; return r; }   /* toute l'armée B rompue → A gagne */
        if (r.routA>=nA){ r.winner=+1; return r; }
    }
    /* l'armée la plus rompue recule. */
    if      (r.routA < r.routB) r.winner=-1;
    else if (r.routB < r.routA) r.winner=+1;
    else                        r.winner=0;
    return r;
}

/* ===================================================================== */
/* §1 — LE DÉPLACEMENT SUR LE TERRAIN                                     */
/* ===================================================================== */
#define MARCH_BASE_DAYS   12.f   /* jours-référence pour franchir une case (calibrage) */
#define RIVER_PENALTY     1.8f   /* franchir un cours d'eau : lent et à découvert */
#define ROUTE_SPEEDUP     1.6f   /* une route porte la marche */
#define RELIEF_DRAG       0.55f  /* part de vitesse rognée par le relief le plus haut */

bool terrain_impassable(Biome b){
    switch (b){
        case BIO_DEEP_OCEAN: case BIO_OCEAN: case BIO_SHALLOW:   /* l'eau : pas pour une armée de terre */
        case BIO_PEAK: case BIO_GLACIER: case BIO_VOLCANO:       /* la roche nue, la glace, le feu */
            return true;
        default: return false;
    }
}

float terrain_move_factor(Biome b, float height){
    if (terrain_impassable(b)) return 0.f;
    float base;
    switch (b){
        case BIO_PLAINS: case BIO_FARMLAND: case BIO_GRASSLAND:
        case BIO_STEPPE: case BIO_SAVANNA:               base = 1.20f; break;  /* l'open dévoré */
        case BIO_COAST:  case BIO_DRYLANDS:              base = 1.00f; break;
        case BIO_DESERT: case BIO_COASTAL_DESERT:        base = 0.70f; break;  /* le sable freine */
        case BIO_HILLS:  case BIO_HIGHLANDS:             base = 0.70f; break;
        case BIO_WOODS:  case BIO_FOREST:                base = 0.50f; break;  /* le couvert ralentit */
        case BIO_MARSH:  case BIO_BOG: case BIO_MANGROVE:base = 0.40f; break;  /* la boue colle */
        case BIO_JUNGLE:                                 base = 0.35f; break;
        case BIO_MOUNTAINS:                              base = 0.33f; break;  /* grimper, à pic */
        default:                                         base = 1.00f; break;
    }
    /* le relief rogne en plus : grimper coûte (height 0..1). */
    float h = height < 0.f ? 0.f : (height > 1.f ? 1.f : height);
    return base * (1.f - RELIEF_DRAG * h);
}

float march_attrition_rate(Biome b){
    switch (b){
        case BIO_DESERT: case BIO_COASTAL_DESERT:        return 0.030f;  /* la soif */
        case BIO_JUNGLE:                                 return 0.025f;  /* la fièvre */
        case BIO_MARSH:  case BIO_BOG: case BIO_MANGROVE:return 0.020f;  /* l'épuisement */
        case BIO_MOUNTAINS: case BIO_HIGHLANDS:          return 0.012f;  /* le froid, l'altitude */
        case BIO_STEPPE: case BIO_SAVANNA: case BIO_DRYLANDS: return 0.010f;
        case BIO_DEEP_OCEAN: case BIO_OCEAN: case BIO_SHALLOW:
        case BIO_PEAK: case BIO_GLACIER: case BIO_VOLCANO:    return 0.f;  /* on n'y marche pas */
        default:                                         return 0.006f;  /* la marche ordinaire */
    }
}

float army_slowest_move(const ArmyState *a){
    if (!a) return 0.f;
    float slow = 0.f; bool any=false;
    for (int i=0;i<a->n_units;i++){
        if (a->units[i].count<=0) continue;
        float m = UNITS[a->units[i].type].mouvement;
        if (!any || m < slow){ slow = m; any=true; }   /* on avance au pas du plus lent */
    }
    return any ? slow : 0.f;
}

float army_step_days(const ArmyState *a, Biome to, float height,
                     bool river_crossing, bool on_route){
    if (terrain_impassable(to)) return INFINITY;
    float v = army_slowest_move(a);
    if (v <= 0.f) return INFINITY;                 /* armée vide : ne bouge pas */
    float f = terrain_move_factor(to, height);
    if (f <= 0.f) return INFINITY;
    float days = MARCH_BASE_DAYS / (v * f);
    if (river_crossing) days *= RIVER_PENALTY;
    if (on_route)       days /= ROUTE_SPEEDUP;
    return days;
}

long army_march_attrition(ArmyState *a, Biome b, float days){
    if (!a || days <= 0.f) return 0;
    float rate = march_attrition_rate(b);
    if (rate <= 0.f) return 0;
    /* fraction fondue sur la durée : 1-(1-taux)^jours (compounding journalier). */
    float keep = powf(1.f - rate, days);
    float loss_frac = 1.f - keep;
    long lost_total = 0;
    for (int i=0;i<a->n_units;i++){
        Unit *u=&a->units[i];
        if (u->count<=0) continue;
        long lost = (long)((float)u->count * loss_frac + 0.5f);
        if (lost > u->count) lost = u->count;
        u->count -= lost;
        lost_total += lost;
    }
    return lost_total;
}
