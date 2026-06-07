/*
 * army_demo.c — les armées : recrutement, armes, contres, combat au dé
 *
 *   make army_demo && ./army_demo [graine]
 *
 * Prouve les six points du cahier :
 *   1. Pas un bouton : lever échoue sans armes/matériaux ; fabriquer une arme
 *      consomme la chaîne de matériaux (économie).
 *   2. Classe : pas de cavalerie noble sans élite ; la masse fournit la piétaille.
 *   3. Contres : un mur de piquiers brise la cavalerie ; l'arbalète défait la
 *      cavalerie lourde ; la cavalerie légère croque les archers.
 *   4. Spécial à talon : le mage écrase les 2/3 du roster mais tombe au dernier tiers.
 *   5. Dé pondéré : à matchup égal le résultat varie ; meilleur commandement →
 *      plus de jets réussis ; meilleure discipline → frappe plus fort / encaisse mieux.
 *   6. Moral : une unité dont le moral tombe ROMPT et fait reculer l'armée.
 */
#include "scps_labor.h"
#include "scps_army.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_pass=0, g_fail=0;
static void ok(const char *what, bool cond){
    printf("   %s %s\n", cond?"✓":"✗", what);
    if (cond) g_pass++; else g_fail++;
}

/* Une économie de jouet : pop par classe + bruts pour fabriquer des armes. */
static void setup_labor(LaborEcon *e, long laborer, long elite, long raw){
    memset(e,0,sizeof(*e)); e->n_prov=1;
    LProvince *p=&e->prov[0]; p->prov=0; p->colonized=true;
    p->pop_by_class[LAB_LABORER]=laborer; p->pop_by_class[LAB_ELITE]=elite;
    p->pop=laborer+elite;
    e->stock[LR_MATERIALS]=raw; e->stock[LR_BOIS]=raw; e->stock[LR_METAL]=raw; e->stock[LR_OUTILS]=raw;
    e->stock[LR_GOLD]=2000; e->market.supply=1.f; e->market.price=1.f;
}
static ArmyState one(UnitType t, long count){
    ArmyState a; army_init(&a); a.n_units=1; a.units[0].type=t; a.units[0].count=count;
    a.units[0].moral_courant=0.f; return a;
}
/* Combien de fois A bat B sur N batailles (dés différents) ? */
static int winrate(UnitType ta,long ca, UnitType tb,long cb, float terrain, int N, uint32_t seed){
    int wins=0;
    for (int k=0;k<N;k++){
        ArmyState A=one(ta,ca), B=one(tb,cb);
        uint32_t rng = seed + (uint32_t)k*2654435761u + 1u;
        if (resolve_battle(&A,&B,terrain,&rng).winner==-1) wins++;
    }
    return wins;
}

int main(int argc, char **argv){
    uint32_t seed=(argc>1)?(uint32_t)strtoul(argv[1],NULL,10):42u;
    LaborEcon *e=malloc(sizeof(LaborEcon));
    if(!e){ fprintf(stderr,"OOM\n"); return 1; }

    printf("══════════════════════════════════════════════════════════════\n");
    printf(" LES ARMÉES — recrutement, armes, contres, combat au dé (graine %u)\n", seed);
    printf("══════════════════════════════════════════════════════════════\n");

    /* ═══ 1. PAS UN BOUTON : pop + armes fabriquées + matériaux + temps ══ */
    printf("\n── 1. Lever une armée coûte pop, ARMES fabriquées, matériaux ──\n");
    setup_labor(e, 2000, 200, 50);
    ArmyState army; army_init(&army);
    ok("sans armes en stock, lever un piquier ÉCHOUE (ce n'est pas un bouton)",
       !army_can_recruit(&army,e,U_PIQUIER,1) && army_recruit(&army,e,U_PIQUIER,1)==0);
    long bois0=e->stock[LR_BOIS], metal0=e->stock[LR_METAL];
    long made=army_fabricate_weapon(&army,e,W_PIQUE,5);   /* pique = bois + métal */
    printf("   fabrication de %ld piques : bois %ld→%ld, métal %ld→%ld (la chaîne de matériaux)\n",
           made, bois0,e->stock[LR_BOIS], metal0,e->stock[LR_METAL]);
    ok("fabriquer des armes CONSOMME la chaîne de matériaux (bois + métal)",
       made==5 && e->stock[LR_BOIS]<bois0 && e->stock[LR_METAL]<metal0 && army.weapons[W_PIQUE]==5);
    long got=army_recruit(&army,e,U_PIQUIER,2);
    printf("   levée de 2 piquiers : %ld unités ; armes restantes %ld ; pop en armée %ld\n",
           got, army.weapons[W_PIQUE], labor_pop_in_army(e));
    ok("avec armes + pop + matériaux, la levée RÉUSSIT (consomme les armes)",
       got==2 && army.weapons[W_PIQUE]==3 && labor_pop_in_army(e)==200);

    /* ═══ 2. LA CLASSE : cavalerie ← élite ; piétaille ← commun ═════════ */
    printf("\n── 2. La cavalerie noble vient de l'ÉLITE ; la masse fournit la piétaille ──\n");
    setup_labor(e, 3000, 0, 200);            /* aucune élite */
    ArmyState a2; army_init(&a2);
    army_fabricate_weapon(&a2,e,W_MONTURE_H,3);
    ok("sans élite, impossible de lever de la cavalerie lourde",
       !army_can_recruit(&a2,e,U_CAV_LOURDE,1));
    setup_labor(e, 3000, 300, 200);          /* avec élite */
    ArmyState a3; army_init(&a3);
    army_fabricate_weapon(&a3,e,W_MONTURE_H,2); army_fabricate_weapon(&a3,e,W_PIQUE,5);
    ok("avec une élite, la cavalerie lourde se lève", army_recruit(&a3,e,U_CAV_LOURDE,2)==2);
    ok("la masse (commun) fournit la piétaille", army_recruit(&a3,e,U_PIQUIER,5)==5);

    /* ═══ 3. LES CONTRES (le réseau pierre-feuille-ciseaux) ════════════ */
    printf("\n── 3. Le contre PRIME sur la qualité (sur 21 batailles, à dés variés) ──\n");
    int N=21;
    int pk = winrate(U_PIQUIER,1,  U_CAV_LOURDE,1, 1.f, N, seed);
    int ab = winrate(U_ARBALETE,1, U_CAV_LOURDE,1, 1.f, N, seed);
    int cl = winrate(U_CAV_LEGERE,1,U_ARCHER,1,    1.f, N, seed);
    printf("   piquier > cav. lourde : %d/%d | arbalète > cav. lourde : %d/%d | cav. légère > archer : %d/%d\n",
           pk,N, ab,N, cl,N);
    ok("un mur de piquiers brise une charge de cavalerie d'élite (le contre prime)", pk>=15);
    ok("une arbalète défait une cavalerie lourde", ab>=15);
    ok("une cavalerie légère croque les archers (contre + mobilité)", cl>=15);

    /* ═══ 4. LE SPÉCIAL À TALON : le mage ══════════════════════════════ */
    printf("\n── 4. Le mage écrase les 2/3 du roster — mais tombe au dernier tiers ──\n");
    int mg_e = winrate(U_MAGE,1, U_EPEISTE,1,    1.f, N, seed);   /* dans les 2/3 */
    int mg_c = winrate(U_MAGE,1, U_CAV_LEGERE,1, 1.f, N, seed);   /* le talon */
    printf("   mage > épéiste (2/3) : %d/%d | mage vs cav. légère (talon) : %d/%d\n", mg_e,N, mg_c,N);
    ok("le mage écrase les deux tiers du roster (ex. l'épéiste)", mg_e>=15);
    ok("… mais se fait défaire par son talon (la cavalerie légère rapide) — jamais universel", mg_c<=6);

    /* ═══ 5. LE DÉ PONDÉRÉ PAR LE CONTRE ET LES STATS ══════════════════ */
    printf("\n── 5. L'incertitude du dé, penchée par le commandement et la discipline ──\n");
    /* Variance : à matchup égal, le nombre de tours varie d'une bataille à l'autre. */
    int seen[64]; int nseen=0; bool varies=false;
    for (int k=0;k<14;k++){
        ArmyState A=one(U_EPEISTE,2), B=one(U_EPEISTE,2);   /* miroir : tout au dé */
        uint32_t rng=seed+(uint32_t)k*40503u+7u;
        int rounds=resolve_battle(&A,&B,1.f,&rng).rounds;
        bool found=false; for(int q=0;q<nseen;q++) if(seen[q]==rounds) found=true;
        if(!found && nseen<64) seen[nseen++]=rounds;
        if (nseen>1) varies=true;
    }
    ok("à matchup égal, le résultat VARIE d'une bataille à l'autre (le dé)", varies);
    /* Commandement : meilleur commandement → plus de jets réussis. */
    int hi=0, lo=0; for (int roll=1; roll<=20; roll++){ if(arm_hit(7.f,roll))hi++; if(arm_hit(3.f,roll))lo++; }
    printf("   jets réussis sur 20 faces : commandement 7 → %d | commandement 3 → %d\n", hi, lo);
    ok("un meilleur commandement réussit PLUS de jets", hi>lo);
    /* Discipline : frappe plus fort ET encaisse mieux. */
    float d_disc = arm_damage(U_EPEISTE,U_PIQUIER,1,0.3f,1.f);   /* épéiste : discipline 0.45 */
    float d_weak = arm_damage(U_ARCHER, U_PIQUIER,1,0.3f,1.f);   /* archer  : discipline 0.20 */
    ok("une meilleure discipline FRAPPE plus fort (même contre)", d_disc > d_weak);
    float d_vsHi = arm_damage(U_EPEISTE,U_PIQUIER,1,0.7f,1.f);    /* cible très disciplinée */
    float d_vsLo = arm_damage(U_EPEISTE,U_PIQUIER,1,0.2f,1.f);    /* cible peu disciplinée  */
    ok("une meilleure discipline ENCAISSE mieux (réduit les dégâts reçus)", d_vsHi < d_vsLo);

    /* ═══ 6. LE MORAL — la rupture fait reculer l'armée ════════════════ */
    printf("\n── 6. Le moral s'épuise ; l'unité rompue fuit, l'armée recule ──\n");
    ArmyState A6=one(U_PIQUIER,2), B6=one(U_CAV_LOURDE,2);
    uint32_t rng6=seed^0x6;
    BattleResult r6=resolve_battle(&A6,&B6,1.f,&rng6);
    printf("   piquiers vs cav. lourde : vainqueur %s, rompues A=%d B=%d, en %d tours\n",
           r6.winner<0?"piquiers":(r6.winner>0?"cavalerie":"nul"), r6.routA, r6.routB, r6.rounds);
    ok("l'unité dont le moral tombe ROMPT (le perdant a des unités en déroute)",
       (r6.winner<0 && r6.routB>r6.routA) || (r6.winner>0 && r6.routA>r6.routB));
    ok("l'armée la plus rompue RECULE (le vainqueur en a le moins)",
       (r6.winner<0 && r6.routA<r6.routB) || (r6.winner>0 && r6.routB<r6.routA));

    printf("\n══════════════════════════════════════════════════════════════\n");
    printf(" BILAN : %d réussis, %d échoués\n", g_pass, g_fail);
    printf("══════════════════════════════════════════════════════════════\n");
    free(e);
    return g_fail?1:0;
}
