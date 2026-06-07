/*
 * culture_demo.c — banc d'essai des pools culturels (console)
 *
 *   make culture_demo && ./culture_demo
 *
 * Démontre :
 *   1. Deux cultures par biome (même corps, âme contraire — le pire rival).
 *   2. Le moteur de mutation : éthos hérité × mode de vie verrouillé →
 *      hybride émergent nommé (« le viking des steppes »).
 *   3. La lecture de distance : D_inf (contenu) vs horloge, et le vocabulaire
 *      de relation lisible.
 *   4. Le syncrétisme : substrat A + élite B → hybride aux traits mutés.
 */
#include "scps_culture.h"
#include <stdio.h>

static void print_culture(const Culture *c, const char *tag){
    printf("  %-10s « %s »%s\n", tag, c->name, c->is_hybrid?"  [hybride]":"");
    printf("     éthos=%-11s  mode=%-22s  struct=%-15s  credo=%s/%s\n",
           ethos_name(c->ethos), lifeway_name(c->lifeway),
           structure_name(c->structure), religion_branch_name(c->rel_branch),
           credo_name(c->credo));
    printf("     martial=%-22s  éco=%s\n",
           martial_name(c->martial), econ_name(c->econ));
    printf("     axes : val=%.1f subs=%.1f par=%.1f rel=%.1f langue=%.1f\n",
           c->valeurs, c->subsistance, c->parente, c->religion, c->langue);
}

static void print_relation(const Culture *a, const Culture *b){
    printf("  %-24s ↔ %-24s\n", a->name, b->name);
    printf("     D_inf(contenu)=%.1f  horloge=%.1f  →  %s\n",
           culture_content_distance(a,b), culture_clock_distance(a,b),
           relation_name(culture_relation(a,b)));
}

int main(void){
    printf("══════════════════════════════════════════════════════════════\n");
    printf(" POOLS CULTURELS — un trait est une coordonnée d'axe qu'on lit\n");
    printf("══════════════════════════════════════════════════════════════\n");

    /* 1. Deux cultures par biome (§8) -------------------------------------- */
    printf("\n── 1. DEUX CULTURES PAR BIOME (même vie, âme contraire) ──\n");
    Biome biomes[]={BIO_STEPPE, BIO_MOUNTAINS, BIO_COAST, BIO_PLAINS, BIO_JUNGLE};
    for (unsigned i=0;i<sizeof(biomes)/sizeof(biomes[0]);i++){
        Culture a,b; culture_pair_for_biome(biomes[i], &a, &b, REL_ANIMISTE);
        printf("\n  [%s]\n", lifeway_name(lifeway_for_biome(biomes[i])));
        print_culture(&a,"A");
        print_culture(&b,"B");
        printf("     → pire rival : même corps (subs %.1f), âme contraire "
               "(val %.1f vs %.1f)\n", a.subsistance, a.valeurs, b.valeurs);
    }

    /* 2. Le viking des steppes (§4) --------------------------------------- */
    printf("\n── 2. MUTATION : éthos hérité × biome verrouillé ──\n");
    /* Honneur d'origine maritime (razzia) débarque sur la steppe : pas de mer. */
    Culture viking_orig = culture_make(BIO_COAST, ETHOS_HONNEUR, REL_ANIMISTE, CREDO_PLURALISTE);
    Culture viking_step = culture_make(BIO_STEPPE, ETHOS_HONNEUR, REL_ANIMISTE, CREDO_PLURALISTE);
    printf("  Honneur sur la CÔTE :\n"); print_culture(&viking_orig,"origine");
    printf("  Le même Honneur sur la STEPPE (pas de mer) :\n");
    print_culture(&viking_step,"muté");
    printf("  → la razzia maritime n'a plus d'océan : elle MUTE en %s.\n",
           martial_name(viking_step.martial));

    Culture merch_mount = culture_make(BIO_MOUNTAINS, ETHOS_MERCANTILE, REL_ABRAHAMIQUE, CREDO_PLURALISTE);
    printf("\n  Mercantile enclavé en MONTAGNE :\n");
    print_culture(&merch_mount,"muté");
    printf("  → commerce sans voies → %s ; garde → %s.\n",
           econ_name(merch_mount.econ), martial_name(merch_mount.martial));

    /* 3. Distance & relations (§9) ---------------------------------------- */
    printf("\n── 3. LECTURE DE DISTANCE (contenu vs horloge) ──\n");
    Culture grenier = culture_make(BIO_PLAINS, ETHOS_BUREAUCRATE, REL_SINIQUE, CREDO_PLURALISTE);
    /* jumeau transcontinental : même contenu, horloge éloignée */
    Culture step_far = viking_step; step_far.langue = 9.5f;  /* horloge divergée */
    print_relation(&viking_step, &grenier);     /* étrangers/cousins */
    print_relation(&viking_step, &step_far);    /* jumeaux convergents */
    /* schisme : même branche, prosélytisme haut des deux côtés */
    Culture zealA = culture_make(BIO_DESERT, ETHOS_HONNEUR, REL_ABRAHAMIQUE, CREDO_EVANGELISTE);
    Culture zealB = culture_make(BIO_DESERT, ETHOS_DOMINATEUR, REL_ABRAHAMIQUE, CREDO_PURIFICATEUR);
    print_relation(&zealA, &zealB);             /* ennemis-schismatiques */

    /* 4. Syncrétisme : le GOUFFRE est un continuum, pas un mur (v3) -------- */
    printf("\n── 4. SYNCRÉTISME — tout s'assimile, à différentes échelles ──\n");
    Culture substrat = culture_make(BIO_PLAINS, ETHOS_HONNEUR, REL_DHARMIQUE, CREDO_PLURALISTE);
    Culture elite    = culture_make(BIO_PLAINS, ETHOS_BUREAUCRATE, REL_SINIQUE, CREDO_EVANGELISTE);
    /* paire LOINTAINE (le « gouffre ») : forêt pacifiste ↔ désert dominateur purificateur */
    Culture doux = culture_make(BIO_FOREST, ETHOS_PACIFISTE,  REL_ANIMISTE,    CREDO_PLURALISTE);
    Culture dur  = culture_make(BIO_DESERT, ETHOS_DOMINATEUR, REL_ABRAHAMIQUE, CREDO_PURIFICATEUR);

    struct { const char *tag; const Culture *a, *b; } pr[] = {
        { "proche  (demi-elfes)",  &substrat, &elite },
        { "lointain (le gouffre)", &doux,     &dur   },
    };
    struct { const char *who; float P, K; } st[] = {
        { "État moyen (P=5,K=5)",            5.f, 5.f },
        { "empire ouvert+capable (P=9,K=9)", 9.f, 9.f },
    };
    for (unsigned i=0;i<2;i++){
        printf("\n  %s — D∞=%.1f\n", pr[i].tag, culture_content_distance(pr[i].a, pr[i].b));
        for (unsigned j=0;j<2;j++){
            SyncFeasibility f = culture_can_syncretize(pr[i].a, pr[i].b, st[j].P, st[j].K);
            printf("     %-34s porte=%.2f  %-18s  temps≈%.0f ticks\n",
                   st[j].who, f.openness,
                   f.feasible ? "OUVERTE" : "fermée (monte P+K)", f.time_ticks);
        }
    }
    printf("\n  → jamais un « impossible » : le gouffre s'ouvre à forte P+K, lentement (temps ∝ D∞).\n");
    Culture fused;
    if (culture_syncretize(&doux, &dur, &fused)){   /* la fusion produit toujours une fiche */
        printf("  fusion forêt↔désert (porte ouverte, après le temps requis) :\n");
        print_culture(&fused,"hybride");
    }

    printf("\n══════════════════════════════════════════════════════════════\n");
    printf(" La friction lit D_inf sur le contenu ; le cousinage lit l'horloge.\n");
    printf(" Tout le drame vit dans l'écart entre les deux canaux.\n");
    printf("══════════════════════════════════════════════════════════════\n");
    return 0;
}
