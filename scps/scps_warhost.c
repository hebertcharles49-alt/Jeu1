/*
 * scps_warhost.c — la mobilisation par pays (voir scps_warhost.h)
 */
#include "scps_warhost.h"
#include <stdlib.h>
#include <string.h>

#define WH_BATCH_WAR    7.0f   /* paquets fabriqués/levés par an en guerre */
#define WH_BATCH_PEACE  1.5f   /* entretien minimal en paix */
#define WH_ARMS_PER_UNIT 2.0f  /* armes déposées par paquet levé (→ mil_power) */

void warhost_init(WarHost *h){
    memset(h->army, 0, sizeof(h->army));
    for (int c=0;c<SCPS_MAX_COUNTRY;c++) army_init(&h->army[c]);
    h->scratch = (LaborEcon*)calloc(1, sizeof(LaborEcon));
}
void warhost_free(WarHost *h){ if (h){ free(h->scratch); h->scratch=NULL; } }

long warhost_units(const WarHost *h, int cid){
    if (!h || cid<0 || cid>=SCPS_MAX_COUNTRY) return 0;
    long n=0; for (int u=0;u<h->army[cid].n_units;u++) n += h->army[cid].units[u].count;
    return n;
}

/* Semer le labor transitoire depuis le pays (pop par classe par province), puis
 * DOTER la capacité matérielle de guerre ∝ population (on ne tick pas le labor :
 * la mobilisation puise dans un stock de guerre, pas dans la production courante). */
static long seed_scratch(LaborEcon *e, const World *w, const WorldEconomy *econ, int cid){
    labor_seed_from_world(e, w, econ, cid);
    long pop=0, elite=0;
    for (int p=0;p<e->n_prov;p++){ pop += e->prov[p].pop; elite += e->prov[p].pop_by_class[LAB_ELITE]; }
    long mat = pop/6 + 100;                      /* matériaux de guerre ∝ pop */
    e->stock[LR_BOIS]=mat; e->stock[LR_METAL]=mat; e->stock[LR_ARGILE]=mat;
    e->stock[LR_PIERRE]=mat; e->stock[LR_CALCAIRE]=mat; e->stock[LR_OUTILS]=mat; e->stock[LR_MATERIALS]=mat;
    e->stock[LR_GOLD] += mat;
    e->market.supply=1.f; e->market.price=1.f;
    return elite;
}

void warhost_tick(WarHost *h, const World *w, WorldEconomy *econ,
                  const DiploState *dp, float dt){
    if (!h || !h->scratch || dt<=0.f) return;
    for (int c=0;c<w->n_countries && c<SCPS_MAX_COUNTRY;c++){
        if (w->country[c].role==POLITY_UNCLAIMED || w->country[c].capital_prov<0) continue;
        int nreg=0; for (int r=0;r<econ->n_regions;r++) if (econ->region[r].owner==c){ nreg++; }
        if (nreg==0) continue;
        bool at_war=false;
        for (int b=0;b<w->n_countries;b++)
            if (b!=c && diplo_status(dp,c,b)==DIPLO_WAR){ at_war=true; break; }
        float batchf = (at_war?WH_BATCH_WAR:WH_BATCH_PEACE)*dt;
        long  batch  = (long)(batchf+0.5f);
        if (batch<=0) continue;

        long elite = seed_scratch(h->scratch, w, econ, c);
        /* fabriquer puis lever : piquiers (masse) + épéistes ; cavalerie si élite. */
        army_fabricate_weapon(&h->army[c], h->scratch, W_PIQUE, batch);
        army_fabricate_weapon(&h->army[c], h->scratch, W_EPEE,  batch/2 + 1);
        army_recruit(&h->army[c], h->scratch, U_PIQUIER, batch);
        army_recruit(&h->army[c], h->scratch, U_EPEISTE, batch/2 + 1);
        if (elite > 200){
            army_fabricate_weapon(&h->army[c], h->scratch, W_MONTURE_H, batch/3 + 1);
            army_recruit(&h->army[c], h->scratch, U_CAV_LOURDE, batch/3 + 1);
        }
        /* déposer la force en ARMES sur la capitale → nourrit diplo_mil_power. */
        long units = warhost_units(h, c);
        int cp = w->country[c].capital_prov;
        int crr = (cp>=0 && cp<w->n_provinces) ? w->province[cp].region : -1;
        if (crr>=0 && crr<econ->n_regions && units>0)
            econ->region[crr].stock[RES_ARMS] += (float)units * WH_ARMS_PER_UNIT * dt;
    }
}
