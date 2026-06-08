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
#include <math.h>

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

    /* ═══ 7. LE DÉPLACEMENT : le terrain décide du temps (§1) ══════════ */
    printf("\n── 7. Le terrain décide du temps : la plaine se dévore, le sommet se refuse ──\n");
    ArmyState pieton = one(U_PIQUIER, 10);                    /* infanterie lente (mouvement 2) */
    float d_plaine  = army_step_days(&pieton, BIO_PLAINS,    0.10f, false, false);
    float d_foret   = army_step_days(&pieton, BIO_FOREST,    0.20f, false, false);
    float d_mont    = army_step_days(&pieton, BIO_MOUNTAINS, 0.80f, false, false);
    printf("   franchir une case (10 piquiers) : plaine %.1f j | forêt %.1f j | montagne %.1f j\n",
           d_plaine, d_foret, d_mont);
    ok("la plaine se franchit plus vite que la forêt, la forêt plus vite que la montagne",
       d_plaine < d_foret && d_foret < d_mont);
    ok("un sommet, un glacier, un volcan, l'océan : INFRANCHISSABLES (jours infinis)",
       terrain_impassable(BIO_PEAK) && terrain_impassable(BIO_GLACIER) &&
       terrain_impassable(BIO_VOLCANO) && terrain_impassable(BIO_OCEAN) &&
       isinf(army_step_days(&pieton, BIO_PEAK, 0.95f, false, false)));

    /* on avance au pas du convoi : ajouter de la cavalerie rapide ne presse rien. */
    ArmyState mixte; army_init(&mixte);
    mixte.n_units=2;
    mixte.units[0].type=U_CAV_LEGERE; mixte.units[0].count=5;   /* mouvement 8 */
    mixte.units[1].type=U_PIQUIER;    mixte.units[1].count=5;   /* mouvement 2 (le plus lent) */
    ok("la vitesse de l'armée = celle de l'unité LA PLUS LENTE (pas du convoi)",
       army_slowest_move(&mixte) == unit_def(U_PIQUIER)->mouvement);
    float d_mixte = army_step_days(&mixte, BIO_PLAINS, 0.10f, false, false);
    ok("une armée mixte avance au pas du piéton, pas du cavalier", d_mixte == d_plaine);

    /* la rivière ralentit (à découvert) ; la route presse. */
    float d_riviere = army_step_days(&pieton, BIO_PLAINS, 0.10f, true,  false);
    float d_route   = army_step_days(&pieton, BIO_PLAINS, 0.10f, false, true);
    printf("   plaine : à gué %.1f j | sur route %.1f j (réf. %.1f j)\n", d_riviere, d_route, d_plaine);
    ok("franchir un cours d'eau RALENTIT la marche (lent, à découvert)", d_riviere > d_plaine);
    ok("une route ACCÉLÈRE la marche", d_route < d_plaine);

    /* la marche use : le désert et le marais saignent plus que la plaine. */
    ArmyState mil = one(U_EPEISTE, 100);
    ArmyState mil2 = one(U_EPEISTE, 100);
    long perdu_des = army_march_attrition(&mil,  BIO_DESERT, 12.f);
    long perdu_pla = army_march_attrition(&mil2, BIO_PLAINS, 12.f);
    printf("   attrition sur 12 jours (100 paquets) : désert -%ld | plaine -%ld\n", perdu_des, perdu_pla);
    ok("la marche use les effectifs ; le désert saigne plus que la plaine",
       perdu_des > perdu_pla && perdu_des > 0);
    ok("le terrain infranchissable n'inflige pas d'attrition de marche (on n'y va pas)",
       march_attrition_rate(BIO_GLACIER) == 0.f);

    /* ═══ 8. LA BATAILLE DANS LE TEMPS : choc → retrait → poursuite (§2) ═ */
    printf("\n── 8. La bataille a un ARC : le choc use, la poursuite tue (les armées sont CLOUÉES) ──\n");
    /* (a) Une bataille COÛTE du temps ; les deux armées y sont immobilisées. */
    ArmyState A8=one(U_EPEISTE,12), B8=one(U_EPEISTE,3);     /* miroir décisif (12 vs 3) */
    uint32_t rng8=seed^0x8a;
    BattleResult r8=resolve_battle(&A8,&B8,1.f,&rng8);
    printf("   miroir 12 vs 3 : vainqueur %s, %d manches → %.1f j, phase « %s », poursuivis %d\n",
           r8.winner<0?"A":(r8.winner>0?"B":"nul"), r8.rounds, r8.days,
           battle_phase_name(r8.last_phase), r8.pursued);
    ok("une bataille COÛTE du temps (les deux armées sont clouées pendant r.days)", r8.days > 0.f);
    ok("à vitesse ÉGALE, le vainqueur ne court pas le vaincu : il ROMPT sans le tailler (retrait, 0 poursuivi)",
       r8.winner!=0 && r8.last_phase==PH_RETRAIT && r8.pursued==0);

    /* (b) La POURSUITE : une pointe rapide rattrape et fauche le vaincu lent. */
    ArmyState A8b=one(U_CAV_LOURDE,20), B8b=one(U_EPEISTE,6); /* cav (mvt 6) écrase épéistes (mvt 3) */
    uint32_t rng8b=seed^0xb2;
    BattleResult r8b=resolve_battle(&A8b,&B8b,1.f,&rng8b);
    printf("   cav. lourde 20 vs épéistes 6 : vainqueur %s, phase « %s », poursuivis %d paquets, %.1f j\n",
           r8b.winner<0?"cavalerie":(r8b.winner>0?"épéistes":"nul"),
           battle_phase_name(r8b.last_phase), r8b.pursued, r8b.days);
    ok("un vainqueur plus RAPIDE rattrape : la POURSUITE fauche le vaincu (phase poursuite, paquets perdus)",
       r8b.winner<0 && r8b.last_phase==PH_POURSUITE && r8b.pursued>0);
    ok("la poursuite AJOUTE du temps (le vainqueur s'éparpille à la curée)",
       r8b.days > (float)r8b.rounds*0.18f);

    /* (c) army_fastest_move : la pointe qui poursuit ≥ le pas du convoi. */
    ok("la pointe rapide (fastest) ≥ le pas du convoi (slowest) pour une armée mixte",
       army_fastest_move(&mixte) >= army_slowest_move(&mixte) &&
       army_fastest_move(&mixte) == unit_def(U_CAV_LEGERE)->mouvement);

    /* ═══ 9. LA CASCADE TECHNOLOGIQUE : l'arbre façonne la doctrine (§3) ═ */
    printf("\n── 9. La tech façonne l'armée : Forge→armes, Société→moral, Savoir→arcane/invocation ──\n");
    /* on isole chaque branche dans son propre arbre pour prouver une vertu à la fois. */
    TechState t0; tech_state_init(&t0, true);              /* seulement la base */
    ArmyDoctrine d0 = army_doctrine(&t0);
    ok("un empire à la seule base est NEUTRE (doctrine 1·1·1, pas d'invocation)",
       d0.weapon_power==1.f && d0.moral_mul==1.f && d0.arcane_power==1.f && !d0.can_summon);

    TechState tf; tech_state_init(&tf, true);              /* FORGE·Armée pure */
    tf.unlocked[TECH_ARMURERIE]=tf.unlocked[TECH_POUDRIERE]=true;
    tf.unlocked[TECH_FORGE_RUNES]=tf.unlocked[TECH_OEUVRE_NOIRE]=true;
    ArmyDoctrine df = army_doctrine(&tf);
    printf("   forge complète → weapon_power %.2f | société → moral_mul ? | savoir → arcane ?\n", df.weapon_power);
    ok("FORGE·Armée muscle l'ARME (weapon_power > 1) sans toucher moral ni arcane",
       df.weapon_power>1.f && df.moral_mul==1.f && df.arcane_power==1.f);

    TechState tsoc; tech_state_init(&tsoc, true);          /* SOCIÉTÉ·Armée pure */
    tsoc.unlocked[TECH_CONSCRIPTION]=tsoc.unlocked[TECH_ORGANISATION]=true;
    tsoc.unlocked[TECH_CASTE_MARTIALE]=true;
    ArmyDoctrine ds = army_doctrine(&tsoc);
    ok("SOCIÉTÉ·Armée muscle le MORAL (moral_mul > 1) sans toucher l'arme",
       ds.moral_mul>1.f && ds.weapon_power==1.f);

    TechState tk; tech_state_init(&tk, true);              /* SAVOIR·Armée */
    tk.unlocked[TECH_SAVOIR_GUERRE]=tk.unlocked[TECH_MAGIE_BATAILLE]=true;
    ArmyDoctrine dk1 = army_doctrine(&tk);
    ok("SAVOIR·Armée non faustien : l'arcane monte, l'INVOCATION reste verrouillée",
       dk1.arcane_power>1.f && !dk1.can_summon);
    tk.unlocked[TECH_INVOCATION]=true;                     /* le bord faustien */
    ArmyDoctrine dk = army_doctrine(&tk);
    ok("le bord FAUSTIEN (Invocation) déverrouille l'INVOCATION (l'armée sans pop)",
       dk.can_summon && dk.arcane_power>dk1.arcane_power);

    /* (a) doctrine EN ACTION : à épéistes ÉGAUX, la meilleure FORGE l'emporte. */
    int forged=0, N9=21;
    for (int k=0;k<N9;k++){
        ArmyState F=one(U_EPEISTE,4), P=one(U_EPEISTE,4);
        F.doctrine=df;                                     /* l'un forgé, l'autre nu */
        uint32_t rng=seed+(uint32_t)k*2246822519u+3u;
        if (resolve_battle(&F,&P,1.f,&rng).winner==-1) forged++;
    }
    printf("   à épéistes égaux, l'armée FORGÉE gagne %d/%d\n", forged, N9);
    ok("la cascade décide la bataille : à armes égales, la meilleure FORGE l'emporte", forged>=15);

    /* (b) l'ORGANISATION en action : plus de moral → on survit au grain. */
    int held=0;
    for (int k=0;k<N9;k++){
        ArmyState O=one(U_EPEISTE,4), P=one(U_EPEISTE,4);
        O.doctrine=ds;                                     /* moral renforcé */
        uint32_t rng=seed+(uint32_t)k*2654435761u+5u;
        if (resolve_battle(&O,&P,1.f,&rng).winner==-1) held++;
    }
    printf("   à épéistes égaux, l'armée ORGANISÉE (moral) gagne %d/%d\n", held, N9);
    ok("l'organisation décide : un moral renforcé tient et l'emporte au grain", held>=15);

    /* (c) l'ARCANE en action : un mage doté de SAVOIR·Armée écrase un mage nu. */
    int arc=0;
    for (int k=0;k<N9;k++){
        ArmyState M=one(U_MAGE,3), m=one(U_MAGE,3);
        M.doctrine=dk;                                     /* arcane boosté (weapon_power=1) */
        uint32_t rng=seed+(uint32_t)k*40503u+11u;
        if (resolve_battle(&M,&m,1.f,&rng).winner==-1) arc++;
    }
    printf("   miroir de mages, l'un doté de SAVOIR·Armée : il gagne %d/%d\n", arc, N9);
    ok("l'ARCANE décide le duel de mages (SAVOIR·Armée pèse sur les dégâts du mage)", arc>=15);

    /* ═══ 10. LE SIÈGE : contrôler une province coûte du temps ════════ */
    printf("\n── 10. Le SIÈGE : 14 j si nue, sinon un siège (≤ 2 ans) selon fortif/vivres/terrain ──\n");
    float walk = siege_days(0.f, 12.f, 2.f);   /* sans défense : vivres et terrain n'y font rien */
    ok("une province SANS défense est prise en 14 jours (on plante le drapeau)", walk==14.f);
    ok("une province DÉFENDUE résiste bien au-delà de 14 jours", siege_days(1.f,0.f,1.f) > 14.f);

    float s_lowdef = siege_days(1.f, 2.f, 1.f), s_hidef = siege_days(4.f, 2.f, 1.f);
    float s_lowfood= siege_days(2.f, 1.f, 1.f), s_hifood= siege_days(2.f, 10.f,1.f);
    printf("   siège : défense 1→4 = %.0f→%.0f j | vivres 1→10 mois = %.0f→%.0f j\n",
           s_lowdef, s_hidef, s_lowfood, s_hifood);
    ok("plus la place est FORTIFIÉE, plus le siège s'étire", s_hidef > s_lowdef);
    ok("plus la garnison a de VIVRES stockés, plus elle tient", s_hifood > s_lowfood);

    float s_plain = siege_days(3.f, 4.f, terrain_defense_mult(BIO_PLAINS,   0.10f));
    float s_mount = siege_days(3.f, 4.f, terrain_defense_mult(BIO_MOUNTAINS,0.80f));
    printf("   même garnison : plaine %.0f j vs montagne %.0f j (le relief abrite, cf. §1)\n", s_plain, s_mount);
    ok("le TERRAIN abrite : une forteresse de montagne tient plus qu'une de plaine", s_mount > s_plain);
    ok("la montagne défend mieux que la plaine (multiplicateur de terrain ≥ 1)",
       terrain_defense_mult(BIO_MOUNTAINS,0.8f) > terrain_defense_mult(BIO_PLAINS,0.1f) &&
       terrain_defense_mult(BIO_PLAINS,0.1f) >= 1.f);
    ok("le siège est PLAFONNÉ à 2 ans (730 j) — même une citadelle pleine finit par tomber",
       siege_days(20.f, 60.f, 3.f) == 730.f);

    /* ═══ 11. LE TERRAIN AU CHOC : le défenseur paie selon le sol ═════ */
    printf("\n── 11. Le terrain au CHOC (distinct du siège) : coline +5%%, montagne +20%% (défenseur) ──\n");
    float bo_plain = terrain_combat_bonus(BIO_PLAINS);
    float bo_hill  = terrain_combat_bonus(BIO_HILLS);
    float bo_for   = terrain_combat_bonus(BIO_FOREST);
    float bo_mount = terrain_combat_bonus(BIO_MOUNTAINS);
    printf("   bonus défensif : plaine %.2f | coline %.2f | forêt %.2f | montagne %.2f\n",
           bo_plain, bo_hill, bo_for, bo_mount);
    ok("la plaine ne donne RIEN au défenseur (×1.00)", bo_plain==1.f);
    ok("la coline donne +5 % et la montagne +20 % (les deux ancres)", bo_hill==1.05f && bo_mount==1.20f);
    ok("le reste se range ENTRE les deux (plaine < coline < forêt < montagne)",
       bo_plain < bo_hill && bo_hill < bo_for && bo_for < bo_mount);
    /* le bonus EN ACTION : à forces égales, le défenseur sur terrain fort l'emporte. */
    int defw=0, defN=31;
    for (int k=0;k<defN;k++){
        ArmyState ATK=one(U_EPEISTE,6), DEF=one(U_EPEISTE,6);
        uint32_t rng=seed+(uint32_t)k*2654435761u+17u;
        /* DEF = B défend une montagne → terrainA = 1/bonus (désavantage l'attaquant). */
        if (resolve_battle(&ATK,&DEF,1.f/bo_mount,&rng).winner==+1) defw++;   /* +1 = B (défenseur) gagne */
    }
    printf("   à épéistes égaux, le défenseur de MONTAGNE gagne %d/%d\n", defw, defN);
    ok("le terrain DÉCIDE : à forces égales, le défenseur de montagne l'emporte", defw>=18);

    printf("\n══════════════════════════════════════════════════════════════\n");
    printf(" BILAN : %d réussis, %d échoués\n", g_pass, g_fail);
    printf("══════════════════════════════════════════════════════════════\n");
    free(e);
    return g_fail?1:0;
}
