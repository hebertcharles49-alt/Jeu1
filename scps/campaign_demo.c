/*
 * campaign_demo.c — les armées SUR LA CARTE : marche, siège, bataille de rencontre
 *
 *   make campaign_demo && ./campaign_demo [graine]
 *
 * Prouve que les primitives combat-dans-le-temps VIVENT sur une vraie carte :
 *   1. Une force expéditionnaire part de la frontière et MARCHE vers une région
 *      ennemie (le terrain décide des jours — §1) ; la marche use (attrition).
 *   2. En arrivant en terre ennemie, elle ASSIÈGE (≤ 2 ans selon fortif/vivres/
 *      terrain) ; le siège abouti RÉDUIT la région (enregistré).
 *   3. Quand une armée hostile défend la place, il y a BATAILLE (§2/§3) avant.
 *   4. NON-INVASIF : la propriété econ de la région ne bouge pas (la conquête
 *      abstraite — prix/volume — reste intacte).
 */
#include "scps_world.h"
#include "scps_econ.h"
#include "scps_culture.h"
#include "scps_species.h"
#include "scps_diplo.h"
#include "scps_army.h"
#include "scps_labor.h"
#include "scps_campaign.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_pass=0,g_fail=0;
static void ok(const char*w,bool c){ printf("   %s %s\n",c?"✓":"✗",w); if(c)g_pass++; else g_fail++; }

/* Un détachement de campagne, monté à la main (déterministe). */
static ArmyState make_force(long pik,long epe,long cav){
    ArmyState a; army_init(&a); a.n_units=0;
    if(pik>0){ a.units[a.n_units].type=U_PIQUIER;    a.units[a.n_units].count=pik; a.n_units++; }
    if(epe>0){ a.units[a.n_units].type=U_EPEISTE;    a.units[a.n_units].count=epe; a.n_units++; }
    if(cav>0){ a.units[a.n_units].type=U_CAV_LEGERE; a.units[a.n_units].count=cav; a.n_units++; }
    return a;
}

int main(int argc,char**argv){
    uint32_t seed=(argc>1)?(uint32_t)strtoul(argv[1],NULL,10):42u;
    World*w=malloc(sizeof(World)); WorldEconomy*econ=malloc(sizeof(WorldEconomy));
    Campaign*camp=malloc(sizeof(Campaign)), *camp2=malloc(sizeof(Campaign));
    if(!w||!econ||!camp||!camp2){ fprintf(stderr,"OOM\n"); free(w);free(econ);free(camp);free(camp2); return 1; }

    printf("══════════════════════════════════════════════════════════════\n");
    printf(" LA CAMPAGNE — les armées marchent, assiègent, livrent bataille (graine %u)\n", seed);
    printf("══════════════════════════════════════════════════════════════\n");

    WorldParams p=worldparams_default(seed);
    world_generate(w,&p);
    econ_init(econ,w); gen_population(w,econ); worldgen_seed_peoples(w,econ,RACE_HUMAIN);
    for(int t=0;t<12;t++) econ_tick(econ,1.f);    /* peuple les régions, les fait croître/se toucher */

    /* On cherche une frontière : région A adjacente à une région B d'un autre pays. */
    int frontier=-1, target=-1, A=-1, B=-1;
    for(int r=0;r<econ->n_regions && frontier<0;r++){
        int oa=econ->region[r].owner;
        if(oa<0 || !econ->region[r].colonized) continue;
        for(int s=0;s<econ->n_regions;s++){
            if(!econ->adj[r][s]) continue;
            int ob=econ->region[s].owner;
            if(ob<0 || ob==oa || !econ->region[s].colonized) continue;
            frontier=r; target=s; A=oa; B=ob; break;
        }
    }
    if(frontier<0){
        printf("\n (pas de frontière terrestre entre deux pays sur cette graine — démo neutre)\n");
        ok("(monde trop fragmenté : pas de front à éprouver)", true);
        goto done;
    }
    printf("   front : pays %d (région %d) ⇆ pays %d (région %d, biome représentatif éprouvé)\n",
           A, frontier, B, target);

    DiploState dp; diplo_init(&dp);
    diplo_declare_war(&dp, A, B);
    int owner_before = econ->region[target].owner;

    /* ═══ 1. ORDRE & MARCHE ═══════════════════════════════════════════ */
    printf("\n── 1. La force part de la frontière et MARCHE vers la région ennemie ──\n");
    campaign_init(camp, w, econ);
    ArmyState force = make_force(20,15,8);            /* 43 paquets : piquiers, épéistes, cavalerie */
    ArmyState empty; army_init(&empty); empty.n_units=0;
    ok("une force VIDE ne peut être ordonnée (ce n'est pas un clic)",
       !campaign_order(camp, econ, A, frontier, target, &empty));
    bool ordered = campaign_order(camp, econ, A, frontier, target, &force);
    ok("l'ordre est accepté : la force part vers la région ennemie (itinéraire trouvé)", ordered);
    ok("la force est ACTIVE, posée sur la frontière, EN MARCHE",
       campaign_active(camp,A) && campaign_location(camp,A)==frontier && campaign_phase(camp,A)==FA_MARCH);
    long u0 = campaign_units(camp,A);

    /* ═══ 2. ARRIVÉE → SIÈGE → RÉDUCTION ══════════════════════════════ */
    printf("\n── 2. Arrivée en terre ennemie → SIÈGE (dans le temps) → région réduite ──\n");
    int arrived=0, besieging=0; float siege_len=0.f;
    for(int it=0; it<800 && campaign_taken(camp,A)==0; it++){
        uint32_t rng = 0x51u + (uint32_t)it*2654435761u;
        campaign_tick(camp, w, econ, &dp, &rng, 5.f);
        if(campaign_location(camp,A)==target) arrived=1;
        if(campaign_phase(camp,A)==FA_SIEGE){ besieging=1; if(siege_len<=0.f) siege_len=camp->army[A].days_left; }
    }
    printf("   étapes franchies %d · arrivée %s · assiégé %s · siège initial ≈ %.0f j · réduite %d\n",
           camp->army[A].legs, arrived?"oui":"non", besieging?"oui":"non", siege_len, campaign_taken(camp,A));
    ok("la force a MARCHÉ jusqu'à la région-but (terrain → jours, §1)", arrived && camp->army[A].legs>=1);
    ok("arrivée en terre ennemie colonisée, elle ASSIÈGE plus longtemps que 14 j", besieging && siege_len>14.f);
    ok("le siège abouti RÉDUIT la région (taken=1, puis au repos)",
       campaign_taken(camp,A)==1 && campaign_phase(camp,A)==FA_IDLE);
    ok("la marche n'AUGMENTE jamais les effectifs (l'attrition peut mordre)", campaign_units(camp,A) <= u0);
    ok("NON-INVASIF : econ inchangé — la propriété de la région-but n'a pas bougé",
       econ->region[target].owner == owner_before);

    /* ═══ 3. BATAILLE DE RENCONTRE : un défenseur conteste la place ════ */
    printf("\n── 3. Quand une armée hostile défend la place, il y a BATAILLE (§2/§3) ──\n");
    campaign_init(camp2, w, econ);
    ArmyState defender = make_force(10,8,3);          /* la garnison de campagne de B, sur sa région */
    ArmyState invader  = make_force(22,16,9);         /* l'assaillant de A, un peu plus fort */
    bool dord = campaign_order(camp2, econ, B, target, target, &defender);  /* B se tient sur target */
    bool aord = campaign_order(camp2, econ, A, frontier, target, &invader);
    ok("le défenseur se tient sur sa région ; l'assaillant marche dessus", dord && aord);
    int fought=0;
    for(int it=0; it<800 && !fought; it++){
        uint32_t rng = 0x9e3779b9u + (uint32_t)it*40503u;
        campaign_tick(camp2, w, econ, &dp, &rng, 5.f);
        if(camp2->army[A].battles>0 || camp2->army[B].battles>0) fought=1;
    }
    printf("   batailles livrées : assaillant %d · défenseur %d\n",
           camp2->army[A].battles, camp2->army[B].battles);
    ok("les deux forces se sont CROISÉES sur la région et ont LIVRÉ BATAILLE", fought);

    /* ═══ 4. LECTEURS (membrane : tangibles) ═══════════════════════════ */
    printf("\n── 4. Lecteurs de campagne (mots & nombres tangibles, pour l'UI §4) ──\n");
    ok("les phases ont des noms diégétiques distincts (repos / marche / siège)",
       campaign_phase_name(FA_MARCH)[0] && campaign_phase_name(FA_SIEGE)[0] &&
       campaign_phase_name(FA_IDLE)[0]);
    /* composition par grand type d'arme (survol UI §4) + mot de taille (asymétrie). */
    campaign_init(camp, w, econ);
    ArmyState mix = make_force(20,15,8);              /* 20 inf, 8 cav ; +archers à la main */
    mix.units[mix.n_units].type=U_ARCHER; mix.units[mix.n_units].count=5; mix.n_units++;
    campaign_order(camp, econ, A, frontier, target, &mix);
    ArmyComposition cp = campaign_composition(camp, A);
    printf("   composition : %ld inf · %ld arch · %ld cav · %ld mages (total %ld) — taille « %s »\n",
           cp.infanterie, cp.archers, cp.cavalerie, cp.mages, cp.total, army_host_word(cp.total));
    ok("la composition se range par grand type d'arme (inf 35, arch 5, cav 8)",
       cp.infanterie==35 && cp.archers==5 && cp.cavalerie==8 && cp.total==48);
    ok("le mot de taille grandit avec les effectifs (asymétrie d'info ennemie)",
       army_host_word(3)[0] && strcmp(army_host_word(5), army_host_word(200))!=0);

done:
    printf("\n══════════════════════════════════════════════════════════════\n");
    printf(" BILAN : %d réussis, %d échoués\n",g_pass,g_fail);
    printf("══════════════════════════════════════════════════════════════\n");
    free(w); free(econ); free(camp); free(camp2);
    return g_fail?1:0;
}
