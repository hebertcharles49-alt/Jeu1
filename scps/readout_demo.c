/*
 * readout_demo.c — banc d'essai de la MEMBRANE (scps_readout)
 *
 *   make readout_demo && ./readout_demo
 *
 * Rejoue les quatre scénarios du moteur vérifié (scps_core) À TRAVERS la
 * membrane, et prouve :
 *   1. Test décisif de fidélité : coercitif-fragile → « Tenue · Contrainte »
 *      (stable en apparence, condamné en vérité) — la signature SCPS vivante
 *      ET cachée, sans un seul nombre.
 *   2. Couverture du lexique : aucune bande sans mot ni définition.
 *   3. La sortie est faite de MOTS, jamais de flottants SCPS.
 *
 * Ce binaire voit à la fois les floats (scps_core) et les mots (readout) :
 * c'est un test, pas le renderer. Le renderer, lui, n'inclut que scps_readout.h.
 */
#include "scps_core.h"
#include "scps_readout.h"
#include <stdio.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
static void ok(const char *what, bool cond) {
    printf("   %s %s\n", cond ? "✓" : "✗", what);
    if (cond) g_pass++; else g_fail++;
}

/* Affiche le bandeau royaume EN MOTS pour un ScpsState donné. */
static CountryReadout show(const char *titre, ScpsState s,
                           float prosp, float lumiere, float charge) {
    ScpsOrder o = scps_order(&s);
    CountryReadout r = country_readout_from_floats(
        o.SI, o.fragilite, o.fracture, o.pression, s.L, prosp, lumiere, charge);
    printf("\n  ── %s ──\n", titre);
    printf("     Stabilité : %-12s | Assise : %-11s | Légitimité : %s\n",
           label_stab(r.stabilite), label_assise(r.assise), label_legit(r.legitimite));
    printf("     Concorde  : %-12s | Prospérité : %-9s | Savoir : %s\n",
           label_concorde(r.concorde), label_prosp(r.prosperite), label_savoir(r.savoir));
    if (r.presage != PG_CALME)
        printf("     Présage   : %s\n", label_presage(r.presage));
    if (r.augure)
        printf("     ⚑ %s\n", r.augure);
    return r;
}

int main(void) {
    printf("══════════════════════════════════════════════════════════════\n");
    printf(" MEMBRANE SCPS — du flottant vérifié au mot diégétique\n");
    printf("══════════════════════════════════════════════════════════════\n");

    /* Quatre scénarios identiques à core_demo (moteur §2.4 vérifié). */
    ScpsState A = { .D_bar=2, .P=4, .C=4, .K=6, .F=6, .I=4, .H=2, .L=8 };  /* consenti  */
    ScpsState B = { .D_bar=4, .P=5, .C=5, .K=5, .F=2, .I=4, .H=9, .L=1 };  /* coerc-frag*/
    ScpsState C = { .D_bar=8, .P=5, .C=6, .K=4, .F=2, .I=5, .H=3, .L=2 };  /* sécession */
    ScpsState D = { .D_bar=1, .P=7, .C=8, .K=3, .F=2, .I=9, .H=2, .L=2 };  /* révolution*/

    CountryReadout rA = show("Royaume consenti (type Suisse)",        A, 8.f, 5.f, 0.f);
    CountryReadout rB = show("Royaume coercitif-fragile (URSS tard.)", B, 5.f, 3.f, 1.f);
    CountryReadout rC = show("Ancien régime divers (France 1789)",    C, 3.f, 4.f, 0.f);
    CountryReadout rD = show("Empire homogène écrasé",                D, 2.f, 1.f, 6.f);

    /* ---- 1. TEST DÉCISIF : coercitif-fragile = Tenue · Contrainte ---- */
    printf("\n── Test décisif de fidélité ──\n");
    ok("coercitif-fragile → Stabilité « Tenue »",  rB.stabilite == ST_TENU);
    ok("coercitif-fragile → Assise « Contrainte »", rB.assise    == AS_CONTRAINTE);
    ok("coercitif-fragile → augure « par la peur seule »",
       rB.augure && strstr(rB.augure, "peur"));

    /* Contrôles de cohérence des autres modes */
    ok("consenti → Assise « Consentie » (pas de contrainte)", rA.assise == AS_CONSENTIE);
    ok("consenti → Stabilité haute (Assurée/Inébranlable)",
       rA.stabilite >= ST_ASSURE);
    ok("consenti → aucun augure (pas de péril)",              rA.augure == NULL);
    ok("ancien régime divers → Concorde « Sécession »",       rC.concorde == CO_SECESSION);
    ok("ancien régime divers → augure des marges",
       rC.augure && strstr(rC.augure, "marges"));
    ok("empire écrasé → augure de la rue (révolution)",
       rD.augure && strstr(rD.augure, "rue"));

    /* ---- 2. COUVERTURE DU LEXIQUE : aucune bande sans mot ni définition -- */
    printf("\n── Couverture du lexique (un mot + une définition par bande) ──\n");
    bool all_labeled = true;
    #define COVER(fn, hov, count) do { \
        for (int i=0;i<(count);i++){ const char *l=fn(i); \
            if (!l || !l[0] || l[0]=='?') all_labeled=false; } \
        if (!hov() || !hov()[0]) all_labeled=false; } while(0)
    COVER(label_stab,     hover_stab,     5);
    COVER(label_assise,   hover_assise,   4);
    COVER(label_legit,    hover_legit,    5);
    COVER(label_concorde, hover_concorde, 4);
    COVER(label_prosp,    hover_prosp,    5);
    COVER(label_savoir,   hover_savoir,   4);
    COVER(label_presage,  hover_presage,  4);
    COVER(label_stature,  hover_stature,  5);
    COVER(label_flux,     hover_flux,     5);
    COVER(label_aisance,  hover_aisance,  4);
    COVER(label_carrefour,hover_carrefour,4);
    COVER(label_humeur,   hover_humeur,   5);
    COVER(label_lignee,   hover_lignee,   6);
    COVER(label_agitation,hover_agitation,4);
    COVER(label_foi,      hover_foi,      3);
    COVER(label_sedition, hover_sedition, 4);
    #undef COVER
    /* La sédition (politique des factions) projette la tension de coup en bande. */
    ok("tension nulle → Concorde ; tension haute → Séditieuse",
       band_sedition(0.02f)==SED_CALME && band_sedition(0.40f)==SED_SEDITIEUSE);
    ok("chaque bande a un mot ET une définition non vides", all_labeled);

    /* ---- 3. Le vocabulaire d'allégeance (province) lit la structure ------ */
    printf("\n── Allégeance de province (lecture de structure, pas d'arithmétique) ──\n");
    AllegeanceReadout twins = allegeance_from_floats(7.f, /*clock*/8.f, /*content*/1.f, false);
    ok("horloge loin + contenu jumeau → « Sœur lointaine »",
       twins.lignee == LI_SOEUR_LOINTAINE);
    AllegeanceReadout wall = allegeance_from_floats(1.f, 2.f, /*content*/9.f, false);
    ok("axe-mur de contenu → « Inassimilable »", wall.lignee == LI_INASSIMILABLE);
    AllegeanceReadout schism = allegeance_from_floats(6.f, 1.f, 1.f, /*schism*/true);
    ok("schisme religieux proche → « Hérétique proche »",
       schism.lignee == LI_HERETIQUE_PROCHE);
    /* Humeur de FOI : même branche + doctrine proche + ferveur → Dévote ;
     * branche étrangère ou schisme → Hérétique ; pluraliste aligné → Tiède. */
    ok("même branche, doctrine proche, ferveur → Foi « Dévote »",
       band_foi(/*same*/true, /*dist*/1.f, /*schism*/false, /*fervent*/true) == FOI_DEVOTE);
    ok("branche sacrée étrangère → Foi « Hérétique »",
       band_foi(/*same*/false, 1.f, false, true) == FOI_HERETIQUE);
    ok("schisme du même tronc → Foi « Hérétique » (pire que l'infidèle lointain)",
       band_foi(true, 1.f, /*schism*/true, true) == FOI_HERETIQUE);
    ok("pluraliste aligné → Foi « Tiède » (il ne s'embrase pour aucun dogme)",
       band_foi(true, 1.f, false, /*fervent*/false) == FOI_TIEDE);
    printf("     ex. province frondeuse, lignée étrangère : Humeur=%s Lignée=%s\n",
           label_humeur(allegeance_from_floats(3.f,4.f,4.f,false).humeur),
           label_lignee(LI_ETRANGERE));

    /* ---- Membrane de l'ARBRE concentrique (mots + nombres, jamais un float) ---- */
    printf("\n── Arbre de tech concentrique (angle=quartier · rayon=tier · mots) ──\n");
    {
        TechState ts; tech_state_init(&ts, /*ruines*/false);
        unsigned human = tech_race_bit(RACE_HUMAIN);
        tech_research(&ts, TECH_SCRIPTORIUM, human);          /* Savoir·Prod t1 acquis */
        TechTreeReadout tr; tech_tree_readout(&ts, human, /*pop*/10000.f, &tr);
        ok("le readout couvre tout l'arbre (n = TECH_COUNT)", tr.n==TECH_COUNT);
        ok("3 secteurs (thèmes) × 3 anneaux fonctionnels", tr.n_themes==3 && tr.n_functions==3);
        ok("l'angle se LIT (quartier 0..8) et le rayon = tier",
           tr.node[TECH_BIBLIOTHEQUE].quarter>=0 && tr.node[TECH_BIBLIOTHEQUE].quarter<9 &&
           tr.node[TECH_UNIVERSITE].tier==3);
        ok("les bâtiments de base sont ACQUIS d'emblée (le centre)",
           tr.node[TECH_BIBLIOTHEQUE].state==TREE_DONE && tr.node[TECH_BIBLIOTHEQUE].is_base &&
           tr.node[TECH_CASERNE].state==TREE_DONE);
        ok("le nœud recherché est ACQUIS ; le suivant DISPONIBLE ; le profond VERROUILLÉ",
           tr.node[TECH_SCRIPTORIUM].state==TREE_DONE &&
           tr.node[TECH_ACADEMIE].state==TREE_OPEN &&
           tr.node[TECH_UNIVERSITE].state==TREE_LOCKED);
        ok("le bout FAUSTIEN est signalé (L'Éveil)", tr.node[TECH_EVEIL].faustian);
        ok("une signature d'AUTRE race est ORPHELINE pour l'Humain (Forge à runes, naine)",
           tr.node[TECH_FORGE_RUNES].orphan);
        ok("le COÛT est un nombre tangible et croît avec le rayon",
           tr.node[TECH_ACADEMIE].cost>0 && tr.node[TECH_UNIVERSITE].cost>tr.node[TECH_SCRIPTORIUM].cost);
        ok("chaque état a un MOT (jamais un float)", label_tree_state(TREE_OPEN)[0]!='\0');
        printf("     ex. Académie : quartier %d · tier %d · %s · coût %d pts · → %s\n",
               tr.node[TECH_ACADEMIE].quarter, tr.node[TECH_ACADEMIE].tier,
               label_tree_state(tr.node[TECH_ACADEMIE].state), tr.node[TECH_ACADEMIE].cost,
               tr.node[TECH_ACADEMIE].unlocks);
    }

    printf("\n══════════════════════════════════════════════════════════════\n");
    printf(" BILAN : %d réussis, %d échoués\n", g_pass, g_fail);
    printf("══════════════════════════════════════════════════════════════\n");
    return g_fail ? 1 : 0;
}
