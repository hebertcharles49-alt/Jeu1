/*
 * scps_popsim.c — la population précise : la classe ÉMERGE des emplois, par bande
 *
 * Une bande = race × culture × foi. Les emplois (capitale → Nobles, ateliers →
 * Bourgeois) se répartissent sur les bandes au PRORATA de leur taille, par paquets
 * de 100 ; le reste de chaque bande est Journalier. Rien n'est posé : la structure
 * d'emplois sculpte le tissu social, bande par bande.
 */
#include "scps_popsim.h"
#include <string.h>

void popsim_init(PopSim *p){ memset(p, 0, sizeof *p); p->cap_tier = 1; }

int popsim_add_band(PopSim *p, SpeciesArchetype race, Ethos culture,
                    ReligionBranch faith, long count){
    if (count <= 0) return -1;
    for (int i=0;i<p->n_bands;i++)                       /* fusionne une identité déjà présente */
        if (p->band[i].race==race && p->band[i].culture==culture && p->band[i].faith==faith){
            p->band[i].count += count; return i;
        }
    if (p->n_bands >= POPSIM_MAX_BANDS) return -1;
    PopBand *b=&p->band[p->n_bands];
    b->race=race; b->culture=culture; b->faith=faith; b->count=count;
    b->by_class[POPCL_LABORER]=count;                   /* repli : tout Journalier avant l'émergence */
    b->by_class[POPCL_ARTISAN]=0; b->by_class[POPCL_ELITE]=0;
    return p->n_bands++;
}

long popsim_total(const PopSim *p){
    long t=0; for (int i=0;i<p->n_bands;i++) t+=p->band[i].count; return t;
}

void popsim_emerge(PopSim *p){
    long total = popsim_total(p);
    if (total < 1){ return; }
    /* emplois disponibles, arrondis aux paquets de 100. */
    long elite_jobs   = (long)(p->cap_tier < 0 ? 0 : p->cap_tier) * POPSIM_PACK;  /* capitale : tier paquets */
    long artisan_jobs = (p->artisan_jobs/POPSIM_PACK)*POPSIM_PACK;
    if (elite_jobs > total) elite_jobs = (total/POPSIM_PACK)*POPSIM_PACK;
    if (elite_jobs + artisan_jobs > total) artisan_jobs = ((total-elite_jobs)/POPSIM_PACK)*POPSIM_PACK;
    /* répartition PROPORTIONNELLE sur les bandes (par paquets de 100). */
    for (int i=0;i<p->n_bands;i++){
        PopBand *b=&p->band[i];
        long e = ((b->count * elite_jobs   / total)/POPSIM_PACK)*POPSIM_PACK;   /* part noble de la bande */
        long a = ((b->count * artisan_jobs / total)/POPSIM_PACK)*POPSIM_PACK;   /* part bourgeoise */
        if (e > b->count) e = (b->count/POPSIM_PACK)*POPSIM_PACK;
        if (e + a > b->count) a = ((b->count - e)/POPSIM_PACK)*POPSIM_PACK;
        long l = b->count - e - a;                                             /* le reste : Journaliers */
        b->by_class[POPCL_ELITE]   = e;
        b->by_class[POPCL_ARTISAN] = a;
        b->by_class[POPCL_LABORER] = l;
    }
}

/* ---- Lecteurs --------------------------------------------------------- */
long popsim_class_total(const PopSim *p, PopClass c){
    if (c<0 || c>=POPCL_COUNT) return 0;
    long t=0; for (int i=0;i<p->n_bands;i++) t+=p->band[i].by_class[c]; return t;
}
long popsim_faith_total(const PopSim *p, ReligionBranch f){
    long t=0; for (int i=0;i<p->n_bands;i++) if (p->band[i].faith==f) t+=p->band[i].count; return t;
}
long popsim_race_total(const PopSim *p, SpeciesArchetype r){
    long t=0; for (int i=0;i<p->n_bands;i++) if (p->band[i].race==r) t+=p->band[i].count; return t;
}
long popsim_faith_class(const PopSim *p, ReligionBranch f, PopClass c){
    if (c<0 || c>=POPCL_COUNT) return 0;
    long t=0; for (int i=0;i<p->n_bands;i++) if (p->band[i].faith==f) t+=p->band[i].by_class[c]; return t;
}
const char *popclass_name(PopClass c){
    switch (c){ case POPCL_ELITE: return "Nobles"; case POPCL_ARTISAN: return "Bourgeois";
                default: return "Journaliers"; }
}
