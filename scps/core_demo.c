/*
 * core_demo.c — banc d'essai du moteur SCPS headless (§2 + annexe)
 *
 *   make core_demo && ./core_demo
 *
 * Étape 0 de §14 : on teste les équations §2 contre des cas de référence,
 * sans aucune UI. Toutes les valeurs attendues sont calculées À LA MAIN depuis
 * l'annexe (vérifiables ligne à ligne) et contrôlées à 0.01 près. Le binaire
 * sort avec un code ≠ 0 si un seul contrôle échoue (utilisable en CI).
 *
 * Les quatre scénarios d'ordre interne exercent les quatre modes de §2.5 :
 * consenti, coercitif-fragile, submergé→révolution, submergé→sécession.
 */
#include "scps_core.h"
#include <stdio.h>
#include <math.h>

/* ---- Micro-framework de contrôle -------------------------------------- */
static int g_pass = 0, g_fail = 0;

static void check(const char *label, float got, float want) {
    bool ok = fabsf(got - want) <= 0.01f;
    printf("   %s %-26s = %8.3f  (attendu %8.3f)\n",
           ok ? "✓" : "✗", label, got, want);
    if (ok) g_pass++; else g_fail++;
}
static void check_mode(const char *label, ScpsMode got, ScpsMode want) {
    bool ok = (got == want);
    printf("   %s %-26s = %-22s (attendu %s)\n",
           ok ? "✓" : "✗", label, scps_mode_name(got), scps_mode_name(want));
    if (ok) g_pass++; else g_fail++;
}

/* ---- Affichage d'un diagnostic d'ordre complet ------------------------ */
static void dump_order(const ScpsState *s, const ScpsOrder *o) {
    printf("     entrée : D̄=%.1f P=%.1f C=%.1f K=%.1f F=%.1f I=%.1f H=%.1f L=%.1f\n",
           s->D_bar, s->P, s->C, s->K, s->F, s->I, s->H, s->L);
    printf("     K'=%.3f coercition=%.3f R=%.3f I'=%.3f ampli=%.3f\n",
           o->K_prime, o->coercition, o->R, o->I_prime, o->amplificateur);
    printf("     pression=%.3f dereal=%.3f fracture=%.3f S=%.3f\n",
           o->pression, o->dereal, o->fracture, o->S);
    printf("     → SI=%.3f  fragilité=%.3f  → %s\n",
           o->SI, o->fragilite, scps_mode_name(scps_mode(o)));
}

int main(void) {
    printf("══════════════════════════════════════════════════════════════\n");
    printf(" MOTEUR SCPS HEADLESS — vérification des équations §2 (annexe)\n");
    printf("══════════════════════════════════════════════════════════════\n");

    /* ================================================================== *
     * 1. COUCHE DISTANCE (§2.2) — exemples travaillés §4.4 et §4.5
     * ================================================================== */
    printf("\n── §2.2/§4 Distance : horloge (cousinage) vs contenu (friction) ──\n");

    /* §4.4 elfe ↔ orc : mêmes deltas que le tableau (langue 2, parenté 7,
     * religion 5, subsistance 9, valeurs 9). D∞ doit valoir 9, l'horloge 2. */
    ScpsFiche elf = {{ 5.f, 1.f, 3.f, 1.f, 1.f }};
    ScpsFiche orc = {{ 7.f, 8.f, 8.f, 10.f, 10.f }};
    printf("  Elfe↔Orc (§4.4) : sang commun, contenu opposé\n");
    check("D∞ (friction, contenu)", scps_dist_inf(&elf, &orc), 9.0f);
    check("horloge (cousinage)",     scps_clock(&elf, &orc),    2.0f);
    check("D̄ (5 axes, annexe)",      scps_dist_bar(&elf, &orc), sqrtf(48.f)); /* √48≈6.928 */

    /* §4.5 jumeaux transcontinentaux : horloge lointaine (8), contenu jumeau
     * → D∞ minuscule, ils fusionnent « sans se reconnaître ». */
    ScpsFiche twinA = {{ 1.f, 7.0f, 6.0f, 2.5f, 7.0f }};
    ScpsFiche twinB = {{ 9.f, 7.2f, 6.1f, 2.6f, 7.1f }};
    printf("  Jumeaux transcontinentaux (§4.5) : horloge à 8, contenu jumeau\n");
    check("D∞ (minuscule → fusion)", scps_dist_inf(&twinA, &twinB), 0.2f);
    check("horloge (étrangers)",     scps_clock(&twinA, &twinB),    8.0f);
    /* La porte de métabolisation s'ouvre grand malgré l'horloge lointaine. */
    check("métabolisation (P=5,K=5)", scps_metabolisation(5.f, scps_dist_inf(&twinA,&twinB), 5.f), 0.979f);

    /* ================================================================== *
     * 2. PROSPÉRITÉ EXTERNE (§2.3) — reproduit l'exemple de la doc
     * ================================================================== */
    printf("\n── §2.3 Prospérité externe : cloche × porte ──\n");
    check("f(D̄=3)",                 scps_bell(3.f),                    0.84f);
    check("f(D̄=5) pic",             scps_bell(5.f),                    1.00f);
    check("f(D̄=0) rien à échanger", scps_bell(0.f),                    0.00f);
    check("métab(P=6,D∞=5,K=5)",     scps_metabolisation(6.f, 5.f, 5.f), 0.690f);
    check("PE(C=6,P=6,K=5,D̄=3,D∞=5)", scps_PE(6.f, 6.f, 5.f, 3.f, 5.f),  3.477f);
    /* Mur infranchissable : D∞ ≫ P → σ(0.8(4−9))=σ(−4)=0.0180 étrangle PE. */
    check("PE mur (D∞=9 ≫ P=4)",     scps_PE(6.f, 4.f, 5.f, 4.f, 9.f),  0.104f);

    /* ================================================================== *
     * 3. ORDRE INTERNE (§2.4–2.5) — les quatre modes
     * ================================================================== */
    printf("\n── §2.4/§2.5 Ordre interne : les quatre modes d'effondrement ──\n");

    /* (A) CONSENTI (type Suisse) — golden test, intermédiaires vérifiés un à un.
     *     Faible diversité, forte légitimité, peu de coercition. */
    printf("\n  (A) Consenti — golden test (intermédiaires à 0.01) :\n");
    ScpsState A = { .D_bar=2.f, .P=4.f, .C=4.f, .K=6.f, .F=6.f, .I=4.f, .H=2.f, .L=8.f, .flux_faustien=0.f };
    ScpsOrder oA = scps_order(&A);
    dump_order(&A, &oA);
    check("K'",        oA.K_prime,    6.960f);
    check("coercition",oA.coercition, 0.278f);
    check("R",         oA.R,          6.709f);
    check("I'",        oA.I_prime,    1.280f);
    check("pression",  oA.pression,   1.485f);
    check("fracture",  oA.fracture,   0.220f);
    check("dereal",    oA.dereal,     0.000f);
    check("S",         oA.S,          1.705f);
    check("SI",        oA.SI,         9.821f);
    check("fragilité", oA.fragilite,  0.249f);
    check_mode("mode", scps_mode(&oA), SCPS_CONSENTI);

    /* (B) COERCITIF-FRAGILE (type URSS tardive / apartheid) — SI≥5 mais tenu
     *     par la contrainte : fragilité ≥ 5, il craque sous un choc. */
    printf("\n  (B) Coercitif-fragile — H fort, L effondrée :\n");
    ScpsState B = { .D_bar=4.f, .P=5.f, .C=5.f, .K=5.f, .F=2.f, .I=4.f, .H=9.f, .L=1.f, .flux_faustien=0.f };
    ScpsOrder oB = scps_order(&B);
    dump_order(&B, &oB);
    check("SI (≥5, tient)",  oB.SI,        8.677f);
    check("fragilité (≥5)",  oB.fragilite, 5.612f);
    check_mode("mode", scps_mode(&oB), SCPS_COERCITIF_FRAGILE);

    /* (C) SUBMERGÉ → SÉCESSION (ancien régime / France 1789 : très divers,
     *     peu consenti) — la fracture domine, déchirure aux coutures. */
    printf("\n  (C) Submergé → sécession — diversité haute, légitimité basse :\n");
    ScpsState C = { .D_bar=8.f, .P=5.f, .C=6.f, .K=4.f, .F=2.f, .I=5.f, .H=3.f, .L=2.f, .flux_faustien=0.f };
    ScpsOrder oC = scps_order(&C);
    dump_order(&C, &oC);
    check("SI (<5, submergé)", oC.SI,       0.321f);
    check("fracture",          oC.fracture, 3.520f);
    check("pression",          oC.pression, 3.444f);
    check_mode("mode (fracture>pression)", scps_mode(&oC), SCPS_SUBMERGE_SECESSION);

    /* (D) SUBMERGÉ → RÉVOLUTION (homogène mais écrasé) — la pression domine,
     *     renversement du centre, société homogène. */
    printf("\n  (D) Submergé → révolution — homogène, pression écrasante :\n");
    ScpsState D = { .D_bar=1.f, .P=7.f, .C=8.f, .K=3.f, .F=2.f, .I=9.f, .H=2.f, .L=2.f, .flux_faustien=0.f };
    ScpsOrder oD = scps_order(&D);
    dump_order(&D, &oD);
    check("SI (<5, submergé)", oD.SI,       0.006f);
    check("pression",          oD.pression, 9.446f);
    check("fracture",          oD.fracture, 0.440f);
    check_mode("mode (pression≥fracture)", scps_mode(&oD), SCPS_SUBMERGE_REVOLUTION);

    /* ================================================================== *
     * 4. BOUCLE FAUSTIENNE (§8/§10) — le flux faustien lève la déréalisation
     * ================================================================== */
    printf("\n── §8/§10 Verrou faustien : flux > K → déréalisation → charge ──\n");
    /* Même État consenti que (A), mais on pousse la magie : flux_faustien
     * franchit K et la déréalisation apparaît, gonflant la charge S. */
    ScpsState Fa = A; Fa.flux_faustien = 6.f; Fa.P = 7.f; Fa.C = 7.f;
    ScpsOrder oFa = scps_order(&Fa);
    dump_order(&Fa, &oFa);
    /* dereal = max(0, 0.7·7 + 6 − 6) = 4.9 ; sans le flux il serait nul. */
    check("dereal (flux faustien)", oFa.dereal, 4.900f);
    printf("     → « la puissance exige la diversité, la diversité exige la fragilité » (§9)\n");

    /* ================================================================== *
     * Bilan
     * ================================================================== */
    printf("\n══════════════════════════════════════════════════════════════\n");
    printf(" BILAN : %d réussis, %d échoués\n", g_pass, g_fail);
    printf("══════════════════════════════════════════════════════════════\n");
    return g_fail ? 1 : 0;
}
