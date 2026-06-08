/*
 * scps_campaign.c — les armées sur la carte : marche, siège, bataille (voir .h)
 *
 * Une force expéditionnaire par pays, posée sur une RÉGION, qui marche de région
 * en région (au pas du convoi, le terrain décidant des jours — §1), assiège en
 * arrivant (le siège : 14 j si nu, ≤ 2 ans sinon) et livre bataille (§2/§3) quand
 * deux forces hostiles se croisent. Lecture seule sur econ : non-invasif vis-à-vis
 * de la conquête abstraite.
 */
#include "scps_campaign.h"
#include <string.h>

/* ---- Calibrage : ce que l'éco régionale dit au siège ------------------ */
#define DEF_BASE          1.0f   /* défense de base d'une région colonisée */
#define DEF_PER_BLD       0.25f  /* chaque édifice durcit la place */
#define FOOD_MONTHS_FULL  12.f   /* food_sat 1.0 → un an de vivres en magasin */
#define RIVER_BATTLE_MIN  40     /* débit (0..255) au-delà duquel la région a une ligne d'eau */
#define RIVER_COMBAT_EDGE 1.25f  /* franchir sous le feu : le défenseur ×1.25 (annulé par un pont) */

/* ---- Outils ----------------------------------------------------------- */
static long force_units(const ArmyState *a){
    long t=0; for (int i=0;i<a->n_units;i++) if (a->units[i].count>0) t+=a->units[i].count;
    return t;
}
static bool region_ok(const Campaign *c, const WorldEconomy *e, int r){
    if (r<0 || r>=e->n_regions) return false;
    if (e->region[r].impassable) return false;          /* zone morte */
    if (terrain_impassable(c->reg_biome[r])) return false; /* roche/glace/eau */
    return true;
}

/* Prochaine région depuis `from` vers `dest` (BFS sur l'adjacence praticable).
 * Renvoie le voisin de `from` au plus court chemin, ou -1 si injoignable. */
static int next_hop(const Campaign *c, const WorldEconomy *e, int from, int dest){
    if (from==dest) return dest;
    int nr=e->n_regions;
    static int dist[SCPS_MAX_REG];           /* sim mono-thread : static, comme scps_trade */
    static int queue[SCPS_MAX_REG];
    for (int i=0;i<nr;i++) dist[i]=-1;
    int qh=0,qt=0;
    if (!region_ok(c,e,dest)) return -1;
    dist[dest]=0; queue[qt++]=dest;
    while (qh<qt){
        int u=queue[qh++];
        for (int v=0;v<nr;v++){
            if (!e->adj[u][v] || dist[v]>=0) continue;
            if (!region_ok(c,e,v)) continue;
            dist[v]=dist[u]+1; queue[qt++]=v;
        }
    }
    if (from<0 || from>=nr || dist[from]<0) return -1;   /* from injoignable */
    int best=-1, bd=1<<30;
    for (int v=0;v<nr;v++){
        if (!e->adj[from][v] || dist[v]<0) continue;
        if (dist[v]<bd){ bd=dist[v]; best=v; }            /* le voisin qui rapproche le plus */
    }
    return best;
}

static float region_defense(const WorldEconomy *e, int r){
    const RegionEconomy *R=&e->region[r];
    if (!R->colonized) return 0.f;                        /* vacante : on entre (14 j) */
    return DEF_BASE + DEF_PER_BLD * (float)R->n_bld;
}
static float region_food_months(const WorldEconomy *e, int r){
    float f=e->region[r].food_sat;
    if (f<0.f) f=0.f;
    if (f>1.f) f=1.f;
    return f * FOOD_MONTHS_FULL;
}

/* ---- Init ------------------------------------------------------------- */
void campaign_init(Campaign *c, const World *w, const WorldEconomy *econ){
    memset(c,0,sizeof(*c));
    c->n_regions = econ->n_regions;
    for (int r=0; r<econ->n_regions && r<SCPS_MAX_REG; r++){
        const Region *R=&w->region[r];
        Biome b=BIO_PLAINS; float hsum=0.f; int n=0;
        for (int k=0; k<R->n_provinces && k<12; k++){
            int pid=R->province_ids[k];
            if (pid<0 || pid>=w->n_provinces) continue;
            if (n==0) b=w->province[pid].biome_dominant;  /* biome représentatif = 1re province */
            hsum += w->province[pid].height_avg; n++;
        }
        c->reg_biome[r]  = b;
        c->reg_height[r] = (n>0) ? hsum/(float)n : 0.2f;
    }
    /* lignes d'eau : une région qu'un cours d'eau notable traverse (franchissement
     * coûteux au choc — sauf pont, cf. field_battle). */
    for (int i=0;i<SCPS_N;i++){
        const Cell *cell=&w->cell[i];
        if (cell->region<0 || cell->region>=SCPS_MAX_REG) continue;
        if (cell->river >= RIVER_BATTLE_MIN) c->reg_river[cell->region]=true;
    }
    for (int i=0;i<SCPS_MAX_COUNTRY;i++){
        c->army[i].active=false; c->army[i].owner=i;
        c->army[i].loc=-1; c->army[i].dest=-1; c->army[i].next=-1;
        c->army[i].phase=FA_IDLE;
    }
}

/* ---- Ordre de campagne ------------------------------------------------ */
bool campaign_order(Campaign *c, const WorldEconomy *econ, int owner,
                    int from_region, int target_region, const ArmyState *src_force){
    if (owner<0 || owner>=SCPS_MAX_COUNTRY || !src_force) return false;
    if (from_region<0 || from_region>=econ->n_regions) return false;
    if (target_region<0 || target_region>=econ->n_regions) return false;
    if (force_units(src_force)<=0) return false;
    int hop = next_hop(c, econ, from_region, target_region);
    if (hop<0) return false;                              /* injoignable par terre */

    FieldArmy *a=&c->army[owner];
    a->active=true; a->owner=owner; a->loc=from_region; a->dest=target_region;
    a->force=*src_force;                                  /* copie du détachement */
    a->taken=0; a->legs=0; a->battles=0;
    if (from_region==target_region){                     /* déjà sur place */
        a->phase=FA_IDLE; a->next=-1; a->days_left=0.f; a->leg_days=0.f; return true;
    }
    a->next=hop; a->phase=FA_MARCH;
    a->leg_days  = army_step_days(&a->force, c->reg_biome[hop], c->reg_height[hop], false, false);
    a->days_left = a->leg_days;
    return true;
}

/* ---- La bataille de rencontre (§2/§3 + terrain défensif) -------------- *
 * Le défenseur d'un FORT paie au choc selon le terrain (pente/couvert) et profite
 * d'une rivière non pontée. EST défenseur : celui qui RELÈVE le siège (l'autre
 * assiège), sinon le propriétaire de la région (garnison). Forts uniquement — une
 * province sans défense ne donne aucun bonus.                                  */
static void field_battle(const Campaign *c, const WorldEconomy *e, int loc,
                         FieldArmy *A, FieldArmy *B, uint32_t *rng){
    float terrainA = 1.f;                                /* neutre par défaut */
    if (region_defense(e, loc) > 0.f){                   /* FORT (pas une province nue) */
        int defender = 0;                                /* -1 = A défend, +1 = B défend, 0 = personne */
        if      (A->phase==FA_SIEGE && B->phase!=FA_SIEGE) defender=+1;  /* A assiège → B relève → B défend */
        else if (B->phase==FA_SIEGE && A->phase!=FA_SIEGE) defender=-1;  /* B assiège → A défend (on relève) */
        else if (e->region[loc].owner==A->owner)           defender=-1;  /* garnison de A sur sa terre */
        else if (e->region[loc].owner==B->owner)           defender=+1;  /* garnison de B */
        if (defender!=0){
            float adv = terrain_combat_bonus(c->reg_biome[loc]);         /* coline +5 %, montagne +20 %… */
            bool bridged = e->region[loc].route_pe > 0.f;                /* une route = un pont */
            if (c->reg_river[loc] && !bridged) adv *= RIVER_COMBAT_EDGE; /* franchir sous le feu */
            terrainA = (defender<0) ? adv : (1.f/adv);                   /* le défenseur profite du sol */
        }
    }
    BattleResult r = resolve_battle(&A->force, &B->force, terrainA, rng);
    A->battles++; B->battles++;
    FieldArmy *loser = (r.winner<0) ? B : (r.winner>0 ? A : NULL);
    if (!loser) return;                                  /* nul : nul ne cède */
    if (force_units(&loser->force) <= 0){                /* anéantie : elle se dissout */
        loser->active=false; loser->phase=FA_IDLE; loser->dest=-1; loser->next=-1;
    } else {                                             /* battue : elle stoppe, lèche ses plaies */
        loser->phase=FA_IDLE; loser->dest=-1; loser->next=-1;
    }
}

/* ---- Le tick ---------------------------------------------------------- */
void campaign_tick(Campaign *c, const World *w, const WorldEconomy *e,
                   const DiploState *dp, uint32_t *rng, float dt_days){
    (void)w;
    if (dt_days<=0.f) return;

    /* 1. batailles : paires hostiles (en guerre) partageant une région. */
    for (int i=0;i<SCPS_MAX_COUNTRY;i++){
        if (!c->army[i].active) continue;
        for (int j=i+1;j<SCPS_MAX_COUNTRY;j++){
            if (!c->army[j].active) continue;
            if (c->army[i].loc != c->army[j].loc) continue;
            if (c->army[i].owner == c->army[j].owner) continue;
            if (diplo_status(dp, c->army[i].owner, c->army[j].owner)!=DIPLO_WAR) continue;
            field_battle(c, e, c->army[i].loc, &c->army[i], &c->army[j], rng);
        }
    }

    /* 2. avancement : on consomme dt_days à travers les étapes ET le siège. */
    for (int i=0;i<SCPS_MAX_COUNTRY;i++){
        FieldArmy *a=&c->army[i];
        if (!a->active) continue;
        float t=dt_days; int guard=0;
        while (t>0.f && a->active && ++guard<100000){
            if (a->phase==FA_MARCH){
                if (t < a->days_left){ a->days_left-=t; t=0.f; break; }
                t -= a->days_left; a->days_left=0.f;
                int to=a->next;                                  /* on arrive */
                army_march_attrition(&a->force, c->reg_biome[to], a->leg_days);  /* la marche use */
                a->loc=to; a->legs++;
                if (force_units(&a->force)<=0){ a->active=false; a->phase=FA_IDLE; break; }
                if (a->loc==a->dest){
                    if (e->region[a->loc].owner==a->owner){       /* notre terre : rien à réduire */
                        a->phase=FA_IDLE; a->dest=-1; a->next=-1; break;
                    }
                    a->phase=FA_SIEGE; a->next=-1;                /* l'ennemi : on assiège */
                    a->days_left = siege_days(region_defense(e,a->loc),
                                              region_food_months(e,a->loc),
                                              terrain_defense_mult(c->reg_biome[a->loc], c->reg_height[a->loc]));
                } else {
                    int hop=next_hop(c,e,a->loc,a->dest);         /* étape suivante */
                    if (hop<0){ a->phase=FA_IDLE; a->dest=-1; a->next=-1; break; }
                    a->next=hop;
                    a->leg_days  = army_step_days(&a->force, c->reg_biome[hop], c->reg_height[hop], false, false);
                    a->days_left = a->leg_days;
                }
            } else if (a->phase==FA_SIEGE){
                if (t < a->days_left){ a->days_left-=t; t=0.f; break; }
                t -= a->days_left; a->days_left=0.f;
                a->taken++;                                      /* RÉDUITE (enregistré, pas appliqué à econ) */
                a->phase=FA_IDLE; a->dest=-1;
                break;
            } else break;                                        /* FA_IDLE */
        }
    }
}

/* ---- Lecteurs --------------------------------------------------------- */
bool campaign_active(const Campaign *c, int o){
    return (o>=0 && o<SCPS_MAX_COUNTRY) && c->army[o].active;
}
int campaign_location(const Campaign *c, int o){
    return (o>=0 && o<SCPS_MAX_COUNTRY && c->army[o].active) ? c->army[o].loc : -1;
}
FieldPhase campaign_phase(const Campaign *c, int o){
    return (o>=0 && o<SCPS_MAX_COUNTRY) ? c->army[o].phase : FA_IDLE;
}
long campaign_units(const Campaign *c, int o){
    if (o<0 || o>=SCPS_MAX_COUNTRY) return 0;
    return force_units(&c->army[o].force);
}
int campaign_taken(const Campaign *c, int o){
    return (o>=0 && o<SCPS_MAX_COUNTRY) ? c->army[o].taken : 0;
}
const char *campaign_phase_name(FieldPhase ph){
    switch (ph){
        case FA_IDLE:  return "Au repos";
        case FA_MARCH: return "En marche";
        case FA_SIEGE: return "En siège";
        default:       return "?";
    }
}
