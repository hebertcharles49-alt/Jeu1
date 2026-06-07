/*
 * species_demo.c — banc d'essai du roster & des traits (console)
 *
 *   make species_demo && ./species_demo
 *
 * Valide le système et sort ≠ 0 si un contrôle échoue (CI) :
 *   1. Structure : 6 atouts + 6 défauts par catégorie ; antonymes réciproques,
 *      même catégorie, signes opposés.
 *   2. Roster : les 6 races par défaut sont VALIDES (un trait/catégorie,
 *      équilibrées à 0).
 *   3. Flexibilité Stellaris : orque charismatique & elfe belliqueux légaux.
 *   4. Le validateur REJETTE les builds illégaux (déséquilibrés, hors catégorie).
 *
 * NB — les deux exemples de flexibilité du document de design avaient des
 * coquilles (l'orque mettait deux traits Sociaux ; l'elfe « corrigé » tombait
 * à +2). On montre ici les versions LÉGALES ; le validateur est la vérité.
 */
#include "scps_species.h"
#include <stdio.h>

static int g_pass=0, g_fail=0;
static void ok(const char *what, bool cond){
    printf("   %s %s\n", cond?"✓":"✗", what);
    if (cond) g_pass++; else g_fail++;
}

static void print_build(const char *who, const SpeciesBuild *b){
    int bud = build_budget(b);
    printf("   %-22s %-14s %-14s %-16s  budget=%d %s\n",
           who, trait_name(b->trait[CAT_PHYSIQUE]),
           trait_name(b->trait[CAT_SOCIAL]), trait_name(b->trait[CAT_INTELLECTUEL]),
           bud, build_is_valid(b)?"✓ valide":"✗ INVALIDE");
}

int main(void){
    printf("══════════════════════════════════════════════════════════════\n");
    printf(" ROSTER DE RACES & TRAITS — un trait par catégorie, équilibré à 0\n");
    printf("══════════════════════════════════════════════════════════════\n");

    /* ---- 1. Structure des pools ---------------------------------------- */
    printf("\n── 1. Structure des 36 traits ──\n");
    int atouts[CAT_COUNT]={0}, defauts[CAT_COUNT]={0};
    bool antos_ok=true, signs_ok=true, cats_ok=true;
    for (int t=0;t<TRAIT_COUNT;t++){
        const TraitDef *d=trait_def((TraitId)t);
        if (d->pts>0) atouts[d->cat]++; else defauts[d->cat]++;
        const TraitDef *an=trait_def(d->antonym);
        if ((int)an->antonym != t)    antos_ok=false;   /* réciprocité */
        if (an->cat != d->cat)        cats_ok=false;    /* même catégorie */
        if ((an->pts>0)==(d->pts>0))  signs_ok=false;   /* signes opposés */
    }
    bool pools_ok=true;
    for (int c=0;c<CAT_COUNT;c++) if (atouts[c]!=6||defauts[c]!=6) pools_ok=false;
    ok("chaque catégorie a 6 atouts + 6 défauts", pools_ok);
    ok("antonymes réciproques", antos_ok);
    ok("antonymes de même catégorie", cats_ok);
    ok("antonymes de signes opposés (atout↔défaut)", signs_ok);

    /* ---- 2. Le roster ------------------------------------------------- */
    printf("\n── 2. Roster (builds par défaut) ──\n");
    bool roster_ok=true;
    for (int r=0;r<RACE_COUNT;r++){
        SpeciesBuild b=species_default_build((SpeciesArchetype)r);
        char tag[48]; snprintf(tag,sizeof tag,"%s [%s]",
                               species_name((SpeciesArchetype)r),
                               sphere_name(species_sphere((SpeciesArchetype)r)));
        print_build(tag,&b);
        if (!build_is_valid(&b)) roster_ok=false;
    }
    ok("les 6 races par défaut sont valides (équilibrées à 0)", roster_ok);

    /* ---- 3. Flexibilité Stellaris (versions légales) ------------------ */
    printf("\n── 3. Flexibilité — on se compose un peuple (légalement) ──\n");
    /* orque charismatique : Belliqueux→Charismatique, Borné→Indolent (Intel −1). */
    SpeciesBuild orc_charm = {{ T_ENDURANT, T_CHARISMATIQUE, T_INDOLENT }};
    /* elfe belliqueux : Débonnaire→Belliqueux, Arcanique→Sourd à l'arcane. */
    SpeciesBuild elf_war   = {{ T_LONGEVIF, T_BELLIQUEUX, T_SOURD_ARCANE }};
    print_build("Orque charismatique", &orc_charm);
    print_build("Elfe belliqueux",     &elf_war);
    ok("orque charismatique légal & équilibré", build_is_valid(&orc_charm));
    ok("elfe belliqueux légal & équilibré",     build_is_valid(&elf_war));

    /* ---- 4. Le validateur rejette l'illégal -------------------------- */
    printf("\n── 4. Le validateur tient la règle ──\n");
    /* L'exemple LITTÉRAL du doc : Endurant, Charismatique, Frondeur — deux
     * Sociaux, aucun Intellectuel → doit être REJETÉ. */
    SpeciesBuild illegal_cat = {{ T_ENDURANT, T_CHARISMATIQUE, T_FRONDEUR }};
    ok("rejette deux traits de la même catégorie (Frondeur en slot Intel)",
       !build_is_valid(&illegal_cat));
    /* build déséquilibré : trois atouts → budget ≠ 0. */
    SpeciesBuild unbalanced = {{ T_ROBUSTE, T_BELLIQUEUX, T_INVENTIF }};
    ok("rejette un build déséquilibré (budget != 0)", !build_is_valid(&unbalanced));
    printf("     (Robuste+Belliqueux+Inventif : budget=%d → trois atouts, illégal)\n",
           build_budget(&unbalanced));

    /* ---- Leviers composés (le code voit les chiffres ; le joueur des mots) */
    printf("\n── Leviers composés par race (échelle moteur) ──\n");
    for (int r=0;r<RACE_COUNT;r++){
        SpeciesBuild b=species_default_build((SpeciesArchetype)r);
        SpeciesLeviers L=build_leviers(&b);
        printf("   %-9s : démo×%.2f prod×%.2f | K%+.1f P%+.1f H%+.1f arcane%+.1f"
               " fract%+.1f dérive×%.2f infl%+.2f\n",
               species_name((SpeciesArchetype)r),
               1.f+L.demographie, 1.f+L.productivite,
               L.capacite, L.permeabilite, L.coercition, L.arcane,
               L.fracture, 1.f+L.derive, L.influence);
    }

    /* ---- Matrice de sphères (continuum, pas mur) --------------------- */
    printf("\n── Distance de sphère (continuum : 5 demi-elfes, 7 demi-orques) ──\n");
    printf("   %-11s","");
    for (int j=0;j<SPHERE_COUNT;j++) printf("%-11s", sphere_name((Sphere)j));
    printf("\n");
    for (int i=0;i<SPHERE_COUNT;i++){
        printf("   %-11s", sphere_name((Sphere)i));
        for (int j=0;j<SPHERE_COUNT;j++) printf("%-11.0f", sphere_distance((Sphere)i,(Sphere)j));
        printf("\n");
    }

    printf("\n══════════════════════════════════════════════════════════════\n");
    printf(" BILAN : %d réussis, %d échoués\n", g_pass, g_fail);
    printf("══════════════════════════════════════════════════════════════\n");
    return g_fail?1:0;
}
