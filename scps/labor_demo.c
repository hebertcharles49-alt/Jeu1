/*
 * labor_demo.c — l'économie des populations : jobs, matériaux, marché
 *
 *   make labor_demo && ./labor_demo [graine]
 *
 * Prouve les deux principes (la prod scale sur les JOBS REMPLIS ; les sorties
 * LISENT la géo) et les sept points du cahier :
 *   1. Jobs, pas bâtiments : doubler les jobs double la prod ; un bâtiment vide = 0.
 *   2. Géo du marché : un marché au désert ~+0.3 ; le même florissant ~+15.
 *   3. Départ cohérent : 4000 pop, 2 ateliers + 2 collecteurs, 200/200/200 → +4 sans géo.
 *   4. Chaîne : produire des matériaux occupe 300 pop ; sans intrants, rien.
 *   5. Marché dynamique : forte demande → prix monte ; pomper coûte au prix courant.
 *   6. Niveaux : un bâtiment niveau 6 offre 2200 pop de jobs.
 *   7. Plein dev : province pleine à Prospérité 100 → pop +15 % plus vite.
 */
#include "scps_world.h"
#include "scps_econ.h"
#include "scps_labor.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_pass=0, g_fail=0;
static void ok(const char *what, bool cond){
    printf("   %s %s\n", cond?"✓":"✗", what);
    if (cond) g_pass++; else g_fail++;
}
static bool near_f(float a,float b,float eps){ float d=a-b; if(d<0)d=-d; return d<=eps; }

int main(int argc, char **argv){
    uint32_t seed=(argc>1)?(uint32_t)strtoul(argv[1],NULL,10):42u;
    World *w=malloc(sizeof(World)); LaborEcon *e=malloc(sizeof(LaborEcon));
    if(!w||!e){ fprintf(stderr,"OOM\n"); return 1; }

    printf("══════════════════════════════════════════════════════════════\n");
    printf(" ÉCONOMIE DES POPULATIONS — jobs, matériaux, marché (graine %u)\n", seed);
    printf("══════════════════════════════════════════════════════════════\n");

    WorldParams p=worldparams_default(seed);
    world_generate(w,&p);
    labor_init(e,w);   /* relit la géographie du worldgen */

    /* ═══ 1. JOBS, PAS BÂTIMENTS ════════════════════════════════════════ */
    printf("\n── 1. La prod scale sur les JOBS REMPLIS (pas le nombre de bâtiments) ──\n");
    e->n_prov=1;
    memset(&e->prov[0],0,sizeof(LProvince));
    e->prov[0].prov=0; e->prov[0].colonized=true; e->prov[0].n_bld=1;
    e->prov[0].bld[0]=(LBuilding){ LB_COLLECTOR, 0, 0 };   /* niveau 0, VIDE */
    LRes res;
    float out0=labor_building_output(e,0,0,&res);
    e->prov[0].bld[0].jobs_filled=1; float out1=labor_building_output(e,0,0,&res);
    e->prov[0].bld[0].jobs_filled=2; float out2=labor_building_output(e,0,0,&res);
    printf("   collecteur : 0 job → %.2f | 1 job → %.2f | 2 jobs → %.2f\n", out0,out1,out2);
    ok("un bâtiment VIDE (0 job) ne produit rien", near_f(out0,0.f,0.001f));
    ok("doubler les jobs remplis DOUBLE la prod", out1>0.f && near_f(out2, 2.f*out1, 0.001f));

    /* ═══ 2. LA GÉO DU MARCHÉ (le carrefour, pas le bonus plat) ═════════ */
    printf("\n── 2. Le marché LIT le flux : désert ~+0.3, florissant ~+15 ──\n");
    int desert=-1, floris=-1; float lo=1e9f, hi=-1.f;
    for (int pr=0; pr<w->n_provinces; pr++){
        float f=province_trade_flow(e,pr);
        if (f<lo){ lo=f; desert=pr; }
        if (f>hi){ hi=f; floris=pr; }
    }
    float md=labor_market_output(e,desert,1), mf=labor_market_output(e,floris,1);
    printf("   marché (1 job) au désert (prov %d, flux %.2f) → +%.2f | florissant (prov %d, flux %.2f) → +%.2f\n",
           desert, lo, md, floris, hi, mf);
    ok("le même marché rend une misère à la marge et une fortune au carrefour (lit le flux)",
       mf > 8.0f && md < mf/6.0f);

    /* ═══ 3. DÉPART COHÉRENT (4000 pop, 2+2, 200/200/200, +4 sans géo) ══ */
    printf("\n── 3. Le départ canonique : net +4 nourriture sans la géographie ──\n");
    labor_seed_start(e, 0);
    int ncol=0, nate=0;
    for (int b=0;b<e->prov[0].n_bld;b++){
        if (e->prov[0].bld[b].type==LB_COLLECTOR) ncol++;
        if (e->prov[0].bld[b].type==LB_WORKSHOP)  nate++;
    }
    float fert0=e->g_fert[0];
    e->g_fert[0]=0.f;                                   /* on retire le bonus géo */
    long bal_nogeo=labor_food_balance(e);
    e->g_fert[0]=fert0;                                 /* on restaure la géo */
    long bal_geo=labor_food_balance(e);
    printf("   pop=%ld  collecteurs=%d ateliers=%d  stock F/O/M=%ld/%ld/%ld  | net food: sans géo=%ld, avec géo=%ld\n",
           e->prov[0].pop, ncol, nate, e->stock[LR_FOOD], e->stock[LR_GOLD], e->stock[LR_MATERIALS], bal_nogeo, bal_geo);
    ok("4000 pop, 2 collecteurs + 2 ateliers, 200/200/200 au stock",
       e->prov[0].pop==4000 && ncol==2 && nate==2 &&
       e->stock[LR_FOOD]==200 && e->stock[LR_GOLD]==200 && e->stock[LR_MATERIALS]==200);
    ok("net = +4 nourriture SANS bonus géographique (le plancher)", bal_nogeo==4);
    ok("la géographie ne fait QU'AJOUTER (jamais retirer la base)", bal_geo>=4);

    /* ═══ 4. LA CHAÎNE DE MATÉRIAUX (300 pop ; sans intrants, rien) ═════ */
    printf("\n── 4. Les matériaux sortent d'une CHAÎNE (bois + argile + atelier = 300 pop) ──\n");
    /* Une province : scierie + argilière + atelier, 1 job chacun. */
    memset(e,0,sizeof(*e)); labor_init(e,w); e->n_prov=1;
    LProvince *pc=&e->prov[0];
    memset(pc,0,sizeof(*pc)); pc->prov=0; pc->colonized=true; pc->n_bld=3;
    pc->bld[0]=(LBuilding){ LB_SAWMILL, 0, 1 };
    pc->bld[1]=(LBuilding){ LB_CLAYPIT, 0, 1 };
    pc->bld[2]=(LBuilding){ LB_WORKSHOP,0, 1 };
    e->g_pres[0][LR_BOIS]=1.f; e->g_pres[0][LR_ARGILE]=1.f;   /* la géo fournit bois & argile */
    long chain_pop = (pc->bld[0].jobs_filled+pc->bld[1].jobs_filled+pc->bld[2].jobs_filled)*POP_PER_SLOT;
    long mat_before=e->stock[LR_MATERIALS];
    for (int t=0;t<3;t++) labor_tick(e);
    long mat_after=e->stock[LR_MATERIALS];
    printf("   chaîne = %ld pop (scierie+argilière+atelier) → matériaux %ld→%ld\n", chain_pop, mat_before, mat_after);
    ok("la chaîne complète occupe 300 pop (3 jobs)", chain_pop==300);
    ok("avec intrants (bois+argile extraits), l'atelier PRODUIT des matériaux", mat_after>mat_before);
    /* Sans intrants : un atelier seul, stock de bruts vide → 0 matériau. */
    memset(e,0,sizeof(*e)); labor_init(e,w); e->n_prov=1;
    memset(&e->prov[0],0,sizeof(LProvince));
    e->prov[0].prov=0; e->prov[0].colonized=true; e->prov[0].n_bld=1;
    e->prov[0].bld[0]=(LBuilding){ LB_WORKSHOP, 0, 1 };       /* atelier seul, aucun brut */
    long m0=e->stock[LR_MATERIALS]; labor_tick(e); long m1=e->stock[LR_MATERIALS];
    ok("sans intrants, l'atelier ne produit AUCUN matériau", m1==m0);

    /* ═══ 5. LE MARCHÉ DYNAMIQUE (demande → prix ; pomper coûte) ════════ */
    printf("\n── 5. Le prix des matériaux est GÉNÉRÉ par la demande ──\n");
    memset(e,0,sizeof(*e)); labor_init(e,w);
    e->stock[LR_GOLD]=1000;
    e->market.supply=10.f; e->market.demand=5.f;  float price_low=labor_material_price(e);
    e->market.demand=80.f;                          float price_high=labor_material_price(e);
    printf("   prix matériaux : faible demande → %.2f | tout le monde bâtit (forte demande) → %.2f\n",
           price_low, price_high);
    ok("forte demande (tout le monde bâtit) → le prix MONTE", price_high > price_low);
    long gold_before=e->stock[LR_GOLD];
    float price_now=labor_material_price(e);
    long cost=labor_pump_market(e, 20);            /* on POMPE 20 matériaux au prix courant */
    printf("   pomper 20 matériaux au prix %.2f → coût %ld or (stock or %ld→%ld, matériaux +20)\n",
           price_now, cost, gold_before, e->stock[LR_GOLD]);
    ok("pomper le manque coûte au PRIX COURANT (montant × prix)",
       cost==(long)(20*price_now+0.5f) && e->stock[LR_GOLD]==gold_before-cost && e->stock[LR_MATERIALS]==20);

    /* ═══ 6. LES NIVEAUX — les jobs qui scalent ════════════════════════ */
    printf("\n── 6. Six niveaux ; capacité de jobs cumulée 100→1000 ──\n");
    printf("   capacité (pop) par niveau atteint : ");
    for (int L=0;L<6;L++) printf("%d ", building_job_capacity_pop(L));
    printf("\n");
    ok("un bâtiment niveau 6 offre 2200 pop de jobs (100+100+200+300+500+1000)",
       building_job_capacity_pop(5)==2200 && building_job_slots(5)==22);
    ok("un bâtiment niveau 1 offre 100 pop de jobs (1 slot)", building_job_capacity_pop(0)==100);

    /* ═══ 7. LE BONUS DE PLEIN DÉVELOPPEMENT ═══════════════════════════ */
    printf("\n── 7. Plein dev (6 bâtiments) + Prospérité 100 → +15 %% de vitesse ──\n");
    LProvince full; memset(&full,0,sizeof full);
    full.colonized=true; full.n_bld=LAB_BUILDINGS_PER_PROV;   /* 6 emplacements occupés */
    LProvince partial=full; partial.n_bld=4;                  /* pas pleine */
    printf("   dev : pleine@100=%.2f | pleine@80=%.2f | partielle@100=%.2f\n",
           labor_pop_dev_speed(&full,100), labor_pop_dev_speed(&full,80), labor_pop_dev_speed(&partial,100));
    ok("une province pleine à Prospérité 100 développe sa pop +15 %% plus vite",
       near_f(labor_pop_dev_speed(&full,100), 1.15f, 0.001f));
    ok("sinon (pas pleine, ou Prospérité < 100) → vitesse normale",
       near_f(labor_pop_dev_speed(&full,80),1.0f,0.001f) && near_f(labor_pop_dev_speed(&partial,100),1.0f,0.001f));

    /* ═══ 8. LA POP EST UN POOL — libre · en job · en armée ════════════ */
    printf("\n── 8. Topbar pop : la main-d'œuvre engagée reste dans le pool et se reproduit ──\n");
    memset(e,0,sizeof(*e)); labor_init(e,w); e->n_prov=1;
    LProvince *pp=&e->prov[0]; memset(pp,0,sizeof(*pp));
    pp->prov=0; pp->colonized=true; pp->pop=1000; pp->pop_by_class[LAB_LABORER]=1000;
    /* on AFFECTE tout : 6 collecteurs (600 en job) + 400 enrôlés → 0 libre. */
    for (int b=0;b<6;b++) pp->bld[b]=(LBuilding){ LB_COLLECTOR, 0, 1 };
    pp->n_bld=6; pp->pop_in_army=400;
    e->stock[LR_FOOD]=500;                               /* pas de famine */
    PopBreakdown k0=labor_pop_breakdown(e);
    labor_print_topbar(e);
    ok("répartition cohérente : total = libre + en job + en armée",
       k0.total==1000 && k0.in_jobs==600 && k0.in_army==400 && k0.free==0 &&
       (k0.free+k0.in_jobs+k0.in_army)==k0.total);
    long pop_before=labor_pop_total(e);
    labor_tick(e);                                       /* un jour : prod, nourriture, croissance */
    PopBreakdown k1=labor_pop_breakdown(e);
    printf("   après un jour (libre=0 au départ) :\n");
    labor_print_topbar(e);
    ok("la pop ENGAGÉE se reproduit : le pool croît même avec 0 libre", k1.total > pop_before);
    ok("emploi & armée sont des AFFECTATIONS (inchangées) ; les nouveau-nés sont libres",
       k1.in_jobs==600 && k1.in_army==400 && k1.free==(k1.total-1000));
    /* Famine : un site SANS collecteur épuise sa nourriture → la croissance
     * s'inverse (la nourriture gate la croissance, §2). */
    memset(e,0,sizeof(*e)); labor_init(e,w); e->n_prov=1;
    LProvince *pf=&e->prov[0]; memset(pf,0,sizeof(*pf));
    pf->prov=0; pf->colonized=true; pf->pop=1000; pf->pop_by_class[LAB_LABORER]=1000;
    for (int b=0;b<3;b++) pf->bld[b]=(LBuilding){ LB_WORKSHOP, 0, 1 };   /* consomment, ne collectent pas */
    pf->n_bld=3; e->stock[LR_FOOD]=2;
    long pf0=labor_pop_total(e); labor_tick(e); long pf1=labor_pop_total(e);
    ok("la famine (nourriture épuisée) stoppe et inverse la croissance", pf1 < pf0);

    /* ═══ 9. INTÉGRATION — l'économie d'un VRAI pays lit sa géographie ══ */
    printf("\n── 9. Intégration : seeder un pays du monde ; la richesse suit la terre ──\n");
    WorldEconomy *econ=malloc(sizeof(WorldEconomy));
    if (econ){
        econ_init(econ,w); gen_population(w,econ); worldgen_seed_peoples(w,econ,RACE_HUMAIN);
        /* moyenne du flux géo par pays sur ses régions possédées → riche vs pauvre. */
        int cRich=-1,cPoor=-1; float bestF=-1.f, worstF=1e9f;
        for (int c=0;c<w->n_countries;c++){
            if (w->country[c].role==POLITY_UNCLAIMED) continue;
            double sf=0; int n=0;
            for (int r=0;r<econ->n_regions;r++) if (econ->region[r].owner==c && econ->region[r].culture.settled){
                int pid=w->region[r].province_ids[0]; sf+=province_trade_flow(e,pid); n++;
            }
            if (n<1) continue;
            float mf=(float)(sf/n);
            if (mf>bestF){ bestF=mf; cRich=c; }
            if (mf<worstF){ worstF=mf; cPoor=c; }
        }
        if (cRich>=0 && cPoor>=0 && cRich!=cPoor){
            labor_seed_from_world(e,w,econ,cRich); for(int t=0;t<5;t++) labor_tick(e);
            float idxR=labor_prosperity_index(e); long goldR=e->flow[LR_GOLD];
            labor_seed_from_world(e,w,econ,cPoor); for(int t=0;t<5;t++) labor_tick(e);
            float idxP=labor_prosperity_index(e); long goldP=e->flow[LR_GOLD];
            printf("   pays RICHE (flux %.2f) : indice prospérité %.1f → Prospérité %d  (or +%ld/j)\n",
                   bestF, idxR, (int)(idxR*10.f+0.5f), goldR);
            printf("   pays PAUVRE (flux %.2f) : indice prospérité %.1f → Prospérité %d  (or +%ld/j)\n",
                   worstF, idxP, (int)(idxP*10.f+0.5f), goldP);
            ok("l'économie est seedée du vrai pays et son indice tient dans [0..10]",
               idxR>=0.f && idxR<=10.f && idxP>=0.f && idxP<=10.f);
            ok("la richesse SUIT la géographie (meilleur flux → plus de revenu/prospérité)",
               goldR > goldP && idxR >= idxP);
        } else { ok("(monde trop homogène pour le test d'intégration — ignoré)", true);
                 ok("(idem)", true); }
        free(econ);
    } else { ok("(OOM econ — ignoré)", true); ok("(idem)", true); }

    /* ═══ §CAPITALE : bâtiment obligatoire AMÉLIORABLE + mobilité de classe ═══ */
    printf("\n── §capitale : capitale obligatoire/améliorable (payante) + mobilité de classe ──\n");
    {
        LaborEcon *ce = malloc(sizeof(LaborEcon));
        if (ce){
            labor_init(ce, w);
            labor_seed_start(ce, 0);
            LProvince *cp = &ce->prov[0];
            /* 1. OBLIGATOIRE + STATUT issu du TIER. */
            ok("capitale OBLIGATOIRE ; le tier de départ suit la pop débloquée (4000→4 ; une fondation 500→1)",
               cp->cap_tier==capitale_max_tier(4000) && cp->cap_tier>=1 && capitale_max_tier(500)==1);
            ok("le STATUT vient du TIER : tier 1 « Hameau », 5 « Cité », 7 « Mégapole »",
               !strcmp(capitale_status(1),"Hameau") && !strcmp(capitale_status(5),"Cité")
               && !strcmp(capitale_status(7),"Mégapole"));
            /* 2. la POP PLAFONNE le tier ; la RECETTE de plus en plus précieuse PAIE. */
            ok("la population PLAFONNE le tier (500→1, 4000→4, 10000→7)",
               capitale_max_tier(500)==1 && capitale_max_tier(4000)==4 && capitale_max_tier(10000)==7);
            CapCost c2=capitale_upgrade_cost(2), c4=capitale_upgrade_cost(4), c7=capitale_upgrade_cost(7);
            ok("la recette d'amélioration devient PLUS PRÉCIEUSE (bois → métal+outils → outils)",
               c2.a==LR_BOIS && c4.a==LR_METAL && c4.b==LR_OUTILS && c7.a==LR_OUTILS);
            ce->stock[LR_BOIS]=2000; ce->stock[LR_METAL]=2000; ce->stock[LR_OUTILS]=2000; cp->pop=4000; cp->cap_tier=1;
            long bois0=ce->stock[LR_BOIS];
            bool up=capitale_upgrade(cp,ce);
            printf("   amélioration tier 1→2 : payée %ld bois (stock %ld→%ld) ; tier %d\n",
                   bois0-ce->stock[LR_BOIS], bois0, ce->stock[LR_BOIS], cp->cap_tier);
            ok("améliorer CONSOMME la recette et monte le tier (ce n'est pas gratuit)",
               up && cp->cap_tier==2 && ce->stock[LR_BOIS]<bois0);
            cp->pop=500; cp->cap_tier=1; ce->stock[LR_BOIS]=99999;
            ok("la POP plafonne : 500 hab ne débloque RIEN au-delà du tier 1, même riche", !capitale_upgrade(cp,ce));
            /* 3-5. PAQUETS DE 100 + GATING + productivité. */
            ok("Nobles par PAQUETS de 100 : l'admin emploie tier·100 (tier 3 → 300)", capitale_admin_pop(3)==300);
            ok("GATING : 0 paquet noble → 0 logement, +0 %, MÊME à haut tier",
               capitale_housing(5,0)==0 && capitale_prodmult(5,0)==1.f);
            ok("délivrance au PRORATA : 1 paquet/tier 5 → 1000 log. +5 % ; 3 paquets → 3000 log. +15 %",
               capitale_housing(5,100)==1000 && capitale_prodmult(5,100)==1.05f
               && capitale_housing(5,300)==3000 && capitale_prodmult(5,300)>1.149f);
            /* 6. MOBILITÉ : les classes ÉMERGENT des emplois (par 100). */
            cp->pop=3000; cp->cap_tier=3; cp->n_bld=0;
            capitale_mobility_tick(cp);
            ok("sans atelier : 0 Bourgeois ; les Nobles ÉMERGENT de la capitale (tier 3 → 300)",
               cp->pop_by_class[LAB_ARTISAN]==0 && cp->pop_by_class[LAB_ELITE]==300);
            cp->bld[0]=(LBuilding){LB_WORKSHOP,2,2}; cp->n_bld=1;
            capitale_mobility_tick(cp);
            printf("   3000 hab, capitale tier 3 + 1 atelier : Nobles %ld · Bourgeois %ld · Journaliers %ld\n",
                   cp->pop_by_class[LAB_ELITE], cp->pop_by_class[LAB_ARTISAN], cp->pop_by_class[LAB_LABORER]);
            ok("ouvrir un atelier fait ÉMERGER des Bourgeois (par 100) ; la somme reste le pool",
               cp->pop_by_class[LAB_ARTISAN]==200 &&
               cp->pop_by_class[LAB_ELITE]+cp->pop_by_class[LAB_ARTISAN]+cp->pop_by_class[LAB_LABORER]==3000);
            /* DÉFENSE passive : un niveau de défense par tier (siège plus long, pas de bonus combat). */
            ok("chaque tier de capitale apporte un niveau de DÉFENSE provinciale passive",
               capitale_defense(1)==1 && capitale_defense(7)==7);
            /* AGITATION : la pop SANS logement/service monte la grogne. */
            cp->pop=5000; cp->house_cap=2000; cp->serv_cap=5000;
            ok("la pop SANS LOGEMENT (surpeuplement) compte comme mal-lotie → agitation",
               capitale_unhoused(cp)==3000 && capitale_unserved(cp)==0 && capitale_unrest(cp)>0.59f);
            cp->house_cap=5000; cp->serv_cap=5000;
            ok("bien logée ET servie : aucune grogne de capacité (unrest 0)",
               capitale_unhoused(cp)==0 && capitale_unserved(cp)==0 && capitale_unrest(cp)==0.f);
            free(ce);
        } else ok("(OOM capitale — ignoré)", true);
    }

    printf("\n══════════════════════════════════════════════════════════════\n");
    printf(" BILAN : %d réussis, %d échoués\n", g_pass, g_fail);
    printf("══════════════════════════════════════════════════════════════\n");
    free(w); free(e);
    return g_fail?1:0;
}
