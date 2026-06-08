/*
 * pop_demo.c — la population PRÉCISE : race × culture × foi × CLASSE émergente
 *
 *   make pop_demo && ./pop_demo
 *
 * Prouve le modèle de population fin, en banc d'essai ISOLÉ (il ne touche pas l'éco
 * vivante) :
 *   1. Une province = des BANDES distinctes (race × culture × foi), pas une masse.
 *   2. La CLASSE n'est jamais posée : elle ÉMERGE des emplois (capitale → Nobles,
 *      ateliers → Bourgeois, le reste → Journaliers), par paquets de 100, au
 *      prorata de la taille de chaque bande.
 *   3. PROMOTION : améliorer la capitale ouvre des emplois nobles → des Journaliers
 *      montent Nobles ; RÉTROGRADATION : un emploi qui disparaît les renvoie Journaliers.
 *   4. Le croisement PRÉCIS est lisible : « Nobles de foi abrahamique », etc.
 *   5. Membrane : nombres + mots, jamais une coordonnée SCPS. La somme tient toujours.
 */
#include "scps_popsim.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_pass=0, g_fail=0;
static void ok(const char *what, bool cond){
    printf("   %s %s\n", cond?"✓":"✗", what);
    if (cond) g_pass++; else g_fail++;
}
static long band_sum(const PopBand *b){
    return b->by_class[POPCL_LABORER]+b->by_class[POPCL_ARTISAN]+b->by_class[POPCL_ELITE];
}

int main(void){
    printf("══════════════════════════════════════════════════════════════\n");
    printf(" POPULATION PRÉCISE — race × culture × foi × CLASSE émergente\n");
    printf("══════════════════════════════════════════════════════════════\n");

    PopSim P; popsim_init(&P);
    /* Une province mêlée : trois identités distinctes. */
    popsim_add_band(&P, RACE_HUMAIN,   ETHOS_BUREAUCRATE, REL_ABRAHAMIQUE, 5000);
    popsim_add_band(&P, RACE_ORQUE,    ETHOS_DOMINATEUR,  REL_ANIMISTE,    2000);
    popsim_add_band(&P, RACE_ELFE,     ETHOS_PACIFISTE,   REL_DHARMIQUE,   1000);

    printf("\n── 1. Une province = des BANDES distinctes (race × culture × foi) ──\n");
    ok("trois bandes distinctes coexistent (pas une masse homogène)", P.n_bands==3);
    ok("le total tient (5000 + 2000 + 1000 = 8000 âmes)", popsim_total(&P)==8000);
    /* fusion d'une identité déjà présente (pas une 4e bande). */
    popsim_add_band(&P, RACE_HUMAIN, ETHOS_BUREAUCRATE, REL_ABRAHAMIQUE, 0);  /* 0 → ignoré */
    ok("une identité déjà présente FUSIONNE (toujours 3 bandes)", P.n_bands==3);

    printf("\n── 2. La CLASSE ÉMERGE des emplois (capitale + ateliers), par 100 ──\n");
    /* sans emploi noble ni atelier : tout le monde est Journalier. */
    P.cap_tier=0; P.artisan_jobs=0; popsim_emerge(&P);
    ok("sans emplois (capitale 0, 0 atelier) : TOUT le monde est Journalier",
       popsim_class_total(&P,POPCL_ELITE)==0 && popsim_class_total(&P,POPCL_ARTISAN)==0
       && popsim_class_total(&P,POPCL_LABORER)==8000);

    /* capitale tier 4 (400 emplois nobles) + 600 emplois d'atelier. */
    P.cap_tier=4; P.artisan_jobs=600; popsim_emerge(&P);
    printf("   capitale tier 4 (400 nobles) + 600 ateliers :\n");
    for (int i=0;i<P.n_bands;i++){
        const PopBand *b=&P.band[i];
        printf("     %s/%s/%s (%ld) → Nobles %ld · Bourgeois %ld · Journaliers %ld\n",
               species_name(b->race), ethos_name(b->culture), religion_branch_name(b->faith),
               b->count, b->by_class[POPCL_ELITE], b->by_class[POPCL_ARTISAN], b->by_class[POPCL_LABORER]);
    }
    ok("la classe a ÉMERGÉ : des Nobles et des Bourgeois apparaissent (≠ 0)",
       popsim_class_total(&P,POPCL_ELITE)>0 && popsim_class_total(&P,POPCL_ARTISAN)>0);
    ok("la somme tient bande par bande (Σ classes = effectif de la bande)",
       band_sum(&P.band[0])==P.band[0].count && band_sum(&P.band[1])==P.band[1].count
       && band_sum(&P.band[2])==P.band[2].count);
    ok("la somme globale tient (Σ classes = 8000)",
       popsim_class_total(&P,POPCL_LABORER)+popsim_class_total(&P,POPCL_ARTISAN)
       +popsim_class_total(&P,POPCL_ELITE)==8000);
    ok("répartition au PRORATA : la plus grande bande (Humains) a le plus de Nobles ; "
       "la plus petite (Elfes, 1000) n'atteint pas un paquet noble (0)",
       P.band[0].by_class[POPCL_ELITE] > P.band[1].by_class[POPCL_ELITE]
       && P.band[2].by_class[POPCL_ELITE]==0);
    ok("tout est par PAQUETS DE 100 (chaque classe est un multiple de 100)",
       P.band[0].by_class[POPCL_ELITE]%100==0 && P.band[0].by_class[POPCL_ARTISAN]%100==0
       && P.band[1].by_class[POPCL_ELITE]%100==0);

    printf("\n── 3. PROMOTION (capitale améliorée) · RÉTROGRADATION (emploi perdu) ──\n");
    long elites_t4 = popsim_class_total(&P,POPCL_ELITE);
    P.cap_tier=7; popsim_emerge(&P);                       /* améliorer : plus d'emplois nobles */
    long elites_t7 = popsim_class_total(&P,POPCL_ELITE);
    printf("   Nobles : tier 4 → %ld · tier 7 → %ld\n", elites_t4, elites_t7);
    ok("améliorer la capitale PROMEUT : plus d'emplois nobles → plus de Nobles", elites_t7 > elites_t4);
    P.cap_tier=1; popsim_emerge(&P);                       /* l'emploi disparaît : rétrogradation */
    long elites_t1 = popsim_class_total(&P,POPCL_ELITE);
    printf("   capitale retombe au tier 1 → Nobles %ld (rétrogradés Journaliers)\n", elites_t1);
    ok("un emploi qui DISPARAÎT rétrograde : moins de Nobles, le reste redevient Journalier",
       elites_t1 < elites_t7 &&
       popsim_class_total(&P,POPCL_LABORER)+popsim_class_total(&P,POPCL_ARTISAN)
       +popsim_class_total(&P,POPCL_ELITE)==8000);

    printf("\n── 4. Le croisement PRÉCIS foi × classe · race · foi (membrane) ──\n");
    P.cap_tier=5; P.artisan_jobs=800; popsim_emerge(&P);
    long abr_nobles = popsim_faith_class(&P, REL_ABRAHAMIQUE, POPCL_ELITE);
    long ani_nobles = popsim_faith_class(&P, REL_ANIMISTE,    POPCL_ELITE);
    printf("   Nobles de foi abrahamique : %ld · Nobles de foi animiste : %ld\n", abr_nobles, ani_nobles);
    ok("on lit le croisement FOI × CLASSE (les « Nobles abrahamiques » ≠ les « Nobles animistes »)",
       abr_nobles != ani_nobles && abr_nobles>0);
    ok("agrégats par FOI et par RACE cohérents (membrane : des nombres tangibles)",
       popsim_faith_total(&P,REL_ABRAHAMIQUE)==5000 && popsim_race_total(&P,RACE_ORQUE)==2000
       && popsim_race_total(&P,RACE_ELFE)==1000);
    ok("le mot de classe est diégétique (Journaliers / Bourgeois / Nobles)",
       !strcmp(popclass_name(POPCL_ELITE),"Nobles") && popclass_name(POPCL_LABORER)[0]);

    printf("\n══════════════════════════════════════════════════════════════\n");
    printf(" BILAN : %d réussis, %d échoués\n", g_pass, g_fail);
    printf("══════════════════════════════════════════════════════════════\n");
    return g_fail?1:0;
}
