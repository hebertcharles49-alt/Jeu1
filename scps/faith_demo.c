/*
 * faith_demo.c — banc d'essai de la RELIGION (scps_faith)
 *
 *   make faith_demo && ./faith_demo [graine]
 *
 * Prouve (passe 1) : la foi est une CONFIGURATION générée par monde (un éthos
 * sanctifié + une posture sur l'interdit + un prosélytisme) ; elle est ACTIVE
 * (co-religionnaire → distance 0, autre foi → fracture, foi universelle → lie des
 * cultures différentes) ; et elle médie la colonne vertébrale (orthodoxe interdit
 * le faustien, culte le sacralise). Membrane : des mots (dévot/tiède/hérétique).
 */
#include "scps_world.h"
#include "scps_econ.h"
#include "scps_culture.h"
#include "scps_species.h"
#include "scps_faith.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_pass=0,g_fail=0;
static void ok(const char*w,bool c){ printf("   %s %s\n",c?"✓":"✗",w); if(c)g_pass++; else g_fail++; }

int main(int argc,char**argv){
    uint32_t seed=(argc>1)?(uint32_t)strtoul(argv[1],NULL,10):42u;
    World*w=malloc(sizeof(World)); WorldEconomy*econ=malloc(sizeof(WorldEconomy));
    if(!w||!econ){fprintf(stderr,"OOM\n");return 1;}

    printf("══════════════════════════════════════════════════════════════\n");
    printf(" RELIGION — la foi générée par monde, face sacrée d'un éthos (graine %u)\n",seed);
    printf("══════════════════════════════════════════════════════════════\n");

    WorldParams p=worldparams_default(seed);
    world_generate(w,&p);
    econ_init(econ,w); gen_population(w,econ); worldgen_seed_peoples(w,econ,RACE_HUMAIN);

    FaithSet fs; faith_generate(&fs,w,econ,seed);

    /* ---- 1. Foi = configuration (éthos sanctifié + posture + prosélytisme) ---- */
    printf("\n── 1. Le jeu de foi du monde ──\n");
    for(int i=0;i<fs.n;i++){ const Faith*f=&fs.faith[i];
        printf("   • %-26s sanctifie l'éthos %d · prosél %.2f · %s\n",
               f->name, (int)f->sanctifies, f->proselytism,
               faith_forbids_faustian(f)?"orthodoxe (interdit le faustien)"
               : faith_sacralizes_faustian(f)?"CULTE (sacralise l'interdit)":"tolérant");
    }
    ok("le monde a généré au moins une foi", fs.n>0);
    bool valid=true;
    for(int i=0;i<fs.n;i++){ const Faith*f=&fs.faith[i];
        if(f->sanctifies<0||f->sanctifies>=ETHOS_COUNT) valid=false;
        if(f->proselytism<0.f||f->proselytism>1.f)      valid=false;
        if(f->forbidden_stance<0.f||f->forbidden_stance>1.f) valid=false;
        if(!f->name[0]) valid=false; }
    ok("chaque foi : un éthos sanctifié + prosélytisme [0..1] + posture [0..1] + un nom", valid);

    /* ---- 2. La foi se LIT de la culture ; cohésion / fracture (axe actif) ---- */
    printf("\n── 2. La foi est active : co-religion → cohésion, autre foi → fracture ──\n");
    const PopCulture *c1=NULL;
    for(int r=0;r<econ->n_regions;r++) if(econ->region[r].culture.settled){ c1=&econ->region[r].culture; break; }
    const PopCulture *c2=NULL;
    if(c1) for(int r=0;r<econ->n_regions;r++){ const PopCulture*pc=&econ->region[r].culture;
        if(pc->settled && pc->rel_branch!=c1->rel_branch){ c2=pc; break; } }
    if(c1){
        printf("   foi d'une région : « %s »\n", faith_name(&fs, faith_of(&fs,c1)));
        ok("une culture a une foi (faith_of ≥ 0)", faith_of(&fs,c1)>=0);
        ok("co-religionnaires → distance 0 (assimilation rapide, cohésion)", faith_distance(&fs,c1,c1)==0.f);
        /* foi UNIVERSELLE : une culture DIFFÉRENTE (langue/valeurs) mais même foi → liée. */
        PopCulture uni=*c1; uni.langue=(c1->langue<5.f?9.f:1.f); uni.valeurs=(c1->valeurs<5.f?9.f:1.f);
        ok("une foi UNIVERSELLE lie des cultures différentes (même foi → distance 0)",
           faith_distance(&fs,c1,&uni)==0.f);
        /* SCHISME : même branche, autre bande de l'axe religion → fracture interne. */
        PopCulture sch=*c1; sch.religion=(c1->religion<5.f? c1->religion+5.f : c1->religion-5.f);
        ok("un SCHISME (autre bande de la même branche) FRACTURE (distance > 0)",
           faith_distance(&fs,c1,&sch) > 0.f);
        if(c2) ok("une AUTRE BRANCHE de foi → fracture forte (distance ≥ 6)",
                  faith_distance(&fs,c1,c2) >= 6.f);
        else   ok("(monde mono-branche : pas de fracture inter-branche à tester)", true);
    } else ok("(monde sans culture peuplée)", true);

    /* ---- 3. Colonne vertébrale (§4) : orthodoxe interdit, culte sacralise ---- */
    printf("\n── 3. La colonne vertébrale : orthodoxe ↔ culte ──\n");
    Faith ortho; memset(&ortho,0,sizeof ortho); ortho.forbidden_stance=0.10f;
    Faith cult;  memset(&cult,0,sizeof cult);   cult.forbidden_stance =0.80f;
    Faith tol;   memset(&tol,0,sizeof tol);     tol.forbidden_stance  =0.50f;
    ok("une foi ORTHODOXE interdit le faustien (frein religieux)",
       faith_forbids_faustian(&ortho) && !faith_sacralizes_faustian(&ortho));
    ok("un CULTE faustien le sacralise (déchaînement)",
       faith_sacralizes_faustian(&cult) && !faith_forbids_faustian(&cult));
    ok("une foi tolérante ne fait ni l'un ni l'autre",
       !faith_forbids_faustian(&tol) && !faith_sacralizes_faustian(&tol));
    ok("au départ, les foi générées sont surtout ORTHODOXES (le culte vient du schisme)",
       (fs.n==0) || !faith_sacralizes_faustian(&fs.faith[0]));

    /* ---- 4. Membrane : l'humeur de foi en MOT ---- */
    printf("\n── 4. Membrane : l'humeur de foi (dévot / tiède / hérétique) ──\n");
    if(c1){
        ok("même foi → « dévot »", faith_mood(&fs,c1,c1)==FAITH_DEVOT);
        if(c2) ok("autre branche → « hérétique »", faith_mood(&fs,c1,c2)==FAITH_HERETIQUE);
        else   ok("(pas d'autre branche pour tester l'hérésie)", true);
        ok("chaque humeur a un mot", faith_mood_word(FAITH_DEVOT)[0] && faith_mood_word(FAITH_HERETIQUE)[0]);
    } else ok("(pas de culture pour l'humeur)", true);

    printf("\n══════════════════════════════════════════════════════════════\n");
    printf(" BILAN : %d réussis, %d échoués\n",g_pass,g_fail);
    printf("══════════════════════════════════════════════════════════════\n");
    free(w); free(econ);
    return g_fail?1:0;
}
