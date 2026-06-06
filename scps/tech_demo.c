/*
 * tech_demo.c — banc d'essai de l'arbre de technologies (console)
 *
 *   make tech_demo && ./tech_demo
 *
 * Démontre la thèse centrale : la Société (qui monte K) est la seule porte de
 * métabolisation du flux Forge/Magie. Deux runs comparées :
 *   A. « Ruée magique » — on prend la Magie sans monter K → déréalisation.
 *   B. « Socle d'abord » — on monte K via la Société, PUIS la même Magie → la
 *      puissance est encaissée.
 */
#include "scps_tech.h"
#include <stdio.h>

static void dump(const TechState *s, const char *tag) {
    printf("  %-22s  K=%4.1f L=%4.1f F=%3.1f | eco=%4.1f mil=%4.1f puiss=%4.1f"
           " | frac=%4.1f charge=%4.1f\n",
           tag, s->K, s->L, s->F, s->eco, s->mil, s->puissance,
           s->fracture, s->charge);
    printf("  %-22s  flux=%4.1f  DEREAL=%5.2f  proximité-crise=%3.0f%%"
           "  fragilité=%4.1f  choc=%5.1f\n",
           "", tech_flux(s), tech_dereal(s),
           tech_crisis_proximity(s)*100.f, tech_fragility(s),
           tech_shock_amplitude(s));
}

static void research(TechState *s, TechId id) {
    if (tech_research(s,id))
        printf("  + %-26s [%s t.%d]\n",
               tech_name(id), tech_branch_name(tech_node(id)->branch),
               tech_node(id)->tier);
    else
        printf("  ✗ %-26s (prérequis/porte manquants)\n", tech_name(id));
}

int main(void) {
    printf("════════════════════════════════════════════════════════════\n");
    printf(" ARBRE DE TECHNOLOGIES — l'arbre EST l'axe de défaite\n");
    printf("════════════════════════════════════════════════════════════\n");

    /* ---- Run A : ruée magique sans socle --------------------------------- */
    printf("\n── RUN A : « Ruée magique » (Magie sans K) ──\n");
    TechState a; tech_state_init(&a, /*ruines*/true);
    dump(&a,"départ");
    research(&a, TECH_III1_SAVOIR);
    research(&a, TECH_III2_RUNES);
    research(&a, TECH_III4_ELEMENTS);
    research(&a, TECH_III5_PACTES);
    dump(&a,"après Maîtrise+Pactes");
    research(&a, TECH_III6_EVEIL);   /* tire la gâchette */
    dump(&a,"après l'Éveil");
    printf("  → DEREAL %.2f : l'empire se fissure AVANT de profiter de la "
           "puissance.\n", tech_dereal(&a));

    /* ---- Run B : socle d'abord, puis la même magie ----------------------- */
    printf("\n── RUN B : « Socle d'abord » (Société → K, puis Magie) ──\n");
    TechState b; tech_state_init(&b, /*ruines*/true);
    research(&b, TECH_I1_COUTUME);
    research(&b, TECH_I2_CHARTE);
    research(&b, TECH_I5_CHANCELLERIE);  /* +K fort : absorbe le flux */
    research(&b, TECH_I4_CONSEIL);
    research(&b, TECH_I6_INTEGRATION);
    dump(&b,"socle Société monté");
    research(&b, TECH_III1_SAVOIR);
    research(&b, TECH_III2_RUNES);
    research(&b, TECH_III4_ELEMENTS);
    research(&b, TECH_III5_PACTES);
    dump(&b,"même magie qu'en A");
    research(&b, TECH_III6_EVEIL);
    dump(&b,"après l'Éveil");
    printf("  → DEREAL %.2f : K métabolise le flux ; la puissance tient "
           "(jusqu'au choc de fin).\n", tech_dereal(&b));

    /* ---- Capstones résilience (build Suisse) ----------------------------- */
    printf("\n── RUN C : « Pacte des peuples » (capstone résilience) ──\n");
    TechState c; tech_state_init(&c, false);
    TechId path[]={TECH_I1_COUTUME,TECH_I2_CHARTE,TECH_I5_CHANCELLERIE,
                   TECH_I4_CONSEIL,TECH_I6_INTEGRATION,TECH_I7_PACTE};
    for (size_t i=0;i<sizeof(path)/sizeof(path[0]);i++) research(&c,path[i]);
    dump(&c,"Pacte atteint");
    printf("  → charge=%.1f : ordre consenti en diversité extrême, sans dette "
           "faustienne.\n", c.charge);

    /* ---- Fusion de techs (§7) ------------------------------------------- */
    printf("\n── FUSION DE TECHS (intrants géologiques + tech habilitante) ──\n");
    /* On dote l'empire B de quelques intrants. */
    bool ing[ING_COUNT]={false};
    ing[ING_COMBURANT]=true; ing[ING_COMBUSTIBLE]=true;
    ing[ING_MINERAI]=true;   ing[ING_CATALYSEUR]=true;
    const FusionRecipe *F=tech_fusion_table();
    /* B a la Fonderie ? non. Donnons-lui un état Forge minimal. */
    TechState d; tech_state_init(&d,true);
    research(&d, TECH_II1_METALLURGIE);
    research(&d, TECH_II3_FONDERIE);
    research(&d, TECH_II2_HYDRAULIQUE);
    research(&d, TECH_III1_SAVOIR);
    research(&d, TECH_III2_RUNES);
    printf("  intrants : salpêtre, soufre, fer, catalyseur\n");
    for (int r=0;r<FUSION_COUNT;r++) {
        bool ok=tech_fusion_available(&d,r,ing);
        printf("   %-18s %s\n", F[r].name, ok?"✓ réalisable":"— (manque enabler/intrant)");
    }

    printf("\n════════════════════════════════════════════════════════════\n");
    printf(" Thèse : la puissance exige la diversité, la diversité exige la\n");
    printf(" fragilité. Société = porte de métabolisation rendue concrète.\n");
    printf("════════════════════════════════════════════════════════════\n");
    return 0;
}
