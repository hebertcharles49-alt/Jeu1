/*
 * factions_demo.c — LES FACTIONS PAR ÉTHOS (passe 1) : le spectre + l'enracinement
 *
 *   make factions_demo && ./factions_demo
 *
 * Prouve : (1) six factions = les axes IA + le Communautaire ; (2) le penchant
 * d'un groupe vient de son éthos / sa race / son credo ; (3) la distribution d'un
 * pays = Σ groupes, et CONQUÉRIR un peuple orque MONTE les Conquérants ; (4) la
 * classe pèse — l'élite qui gouverne amplifie son éthos.
 */
#include "scps_factions.h"
#include <stdio.h>
#include <string.h>

static int g_pass=0, g_fail=0;
static void ok(const char *what, int cond){
    printf("   %s %s\n", cond?"✓":"✗", what);
    if (cond) g_pass++; else g_fail++;
}

static PopCulture cult(Ethos e, SpeciesArchetype race, Credo credo){
    PopCulture c; memset(&c,0,sizeof c);
    c.langue=5; c.valeurs=5; c.subsistance=5; c.parente=5; c.religion=5;
    c.ethos=e; c.race=race; c.credo=credo; c.rel_branch=REL_ABRAHAMIQUE; c.settled=true;
    return c;
}
static PopGroup grp(PopCulture cu, SocialClass k, long n){
    PopGroup g; memset(&g,0,sizeof g);
    g.race=cu.race; g.origin=cu; g.culture=cu; g.klass=k; g.count=n; g.integration=1.f;
    return g;
}
static int argmax(const float w[FAC_COUNT]){
    int b=0; for (int f=1;f<FAC_COUNT;f++) if (w[f]>w[b]) b=f; return b;
}

int main(void){
    printf("══════════════════════════════════════════════════════════════\n");
    printf(" FACTIONS PAR ÉTHOS (passe 1) — le spectre + l'enracinement\n");
    printf("══════════════════════════════════════════════════════════════\n");

    /* ═══ 1. LE SPECTRE — six factions = les axes IA (+ Communautaire) ═══ */
    printf("\n── 1. Le spectre : six factions-éthos (les axes des poids IA) ──\n");
    ok("six factions exactement", FAC_COUNT==6);
    printf("   ");
    for (int f=0; f<FAC_COUNT; f++) printf("%s%s", faction_name(f), f<FAC_COUNT-1?" · ":"\n");
    ok("chaque faction a un mot diégétique",
       faction_name(FAC_CONQUERANT)[0] && faction_name(FAC_COMMUNAUTAIRE)[0]
       && faction_name(FAC_TRANSGRESSEUR)[0]);

    /* ═══ 2. LE PENCHANT D'UN GROUPE — éthos / race / credo ═══════════════ */
    printf("\n── 2. Le penchant d'un groupe vient de sa culture ──\n");
    {
        float w[FAC_COUNT];
        group_ethos_lean(&(PopCulture){.ethos=ETHOS_DOMINATEUR,.race=RACE_ORQUE,.credo=CREDO_PLURALISTE}, w);
        ok("orque dominateur → Conquérant dominant, Transgresseur fort",
           argmax(w)==FAC_CONQUERANT && w[FAC_TRANSGRESSEUR]>w[FAC_MARCHAND] && w[FAC_TRANSGRESSEUR]>0.2f);
        group_ethos_lean(&(PopCulture){.ethos=ETHOS_MERCANTILE,.race=RACE_HALFELIN,.credo=CREDO_PLURALISTE}, w);
        ok("halfelin mercantile → Marchand dominant, Communautaire présent",
           argmax(w)==FAC_MARCHAND && w[FAC_COMMUNAUTAIRE]>0.1f);
        group_ethos_lean(&(PopCulture){.ethos=ETHOS_BUREAUCRATE,.race=RACE_HUMAIN,.credo=CREDO_PLURALISTE}, w);
        ok("humain bureaucrate → Légiste dominant", argmax(w)==FAC_LEGISTE);
        group_ethos_lean(&(PopCulture){.ethos=ETHOS_PACIFISTE,.race=RACE_GNOME,.credo=CREDO_PLURALISTE}, w);
        ok("gnome pacifiste → Communautaire dominant", argmax(w)==FAC_COMMUNAUTAIRE);
        group_ethos_lean(&(PopCulture){.ethos=ETHOS_ORDRE,.race=RACE_NAIN,.credo=CREDO_PLURALISTE}, w);
        ok("nain d'ordre → Transgresseur présent (forge à runes)", w[FAC_TRANSGRESSEUR]>0.15f);
        /* CREDO : la ferveur nourrit les Gardiens. */
        float wp[FAC_COUNT], wz[FAC_COUNT];
        group_ethos_lean(&(PopCulture){.ethos=ETHOS_ORDRE,.race=RACE_HUMAIN,.credo=CREDO_PLURALISTE}, wp);
        group_ethos_lean(&(PopCulture){.ethos=ETHOS_ORDRE,.race=RACE_HUMAIN,.credo=CREDO_PURIFICATEUR}, wz);
        ok("le credo purificateur RENFORCE les Gardiens (vs pluraliste)",
           wz[FAC_GARDIEN] > wp[FAC_GARDIEN] + 0.1f);
        /* somme normalisée. */
        float s=0; for (int f=0;f<FAC_COUNT;f++) s+=wz[f];
        ok("le penchant est un profil normalisé (Σ≈1)", s>0.98f && s<1.02f);
    }

    /* ═══ 3. ENRACINEMENT — la distribution d'un pays = Σ groupes ═════════ */
    printf("\n── 3. Enracinement : conquérir un peuple orque MONTE les Conquérants ──\n");
    {
        /* Un pays marchand (halfelins). */
        ProvincePop prov; memset(&prov,0,sizeof prov);
        prov.groups[0]=grp(cult(ETHOS_MERCANTILE,RACE_HALFELIN,CREDO_PLURALISTE),CLASS_LABORER,1000);
        prov.n_groups=1;
        float before[FAC_COUNT]; EthosFaction dom0=faction_weights_of(&prov,1,before);
        /* CONQUÊTE : une province orque entre dans le pays. */
        ProvincePop conq; memset(&conq,0,sizeof conq);
        conq.groups[0]=grp(cult(ETHOS_DOMINATEUR,RACE_ORQUE,CREDO_PLURALISTE),CLASS_LABORER,600);
        conq.n_groups=1;
        ProvincePop country[2]={prov,conq};
        float after[FAC_COUNT]; EthosFaction dom1=faction_weights_of(country,2,after);
        printf("   avant : dominante %s (Conquérants %.0f%%) → après conquête orque : %s (Conquérants %.0f%%)\n",
               faction_name(dom0), before[FAC_CONQUERANT]*100, faction_name(dom1), after[FAC_CONQUERANT]*100);
        ok("le pays marchand est d'abord dominé par les Marchands", dom0==FAC_MARCHAND);
        ok("avaler une province orque MONTE les Conquérants (le spectre se reconfigure)",
           after[FAC_CONQUERANT] > before[FAC_CONQUERANT] + 0.1f);
        ok("la somme reste un profil (Σ≈1)",
           after[0]+after[1]+after[2]+after[3]+after[4]+after[5] > 0.98f);
    }

    /* ═══ 4. LA CLASSE PÈSE — l'élite qui gouverne amplifie son éthos ═════ */
    printf("\n── 4. Qui gouverne compte : l'élite pèse plus que la masse ──\n");
    {
        PopCulture half=cult(ETHOS_MERCANTILE,RACE_HALFELIN,CREDO_PLURALISTE);
        PopCulture orc =cult(ETHOS_DOMINATEUR,RACE_ORQUE,CREDO_PLURALISTE);
        /* Même masse orque, une fois LABOUREURS, une fois ÉLITE régnante. */
        ProvincePop asLab; memset(&asLab,0,sizeof asLab);
        asLab.groups[0]=grp(half,CLASS_LABORER,1000); asLab.groups[1]=grp(orc,CLASS_LABORER,200); asLab.n_groups=2;
        ProvincePop asElite; memset(&asElite,0,sizeof asElite);
        asElite.groups[0]=grp(half,CLASS_LABORER,1000); asElite.groups[1]=grp(orc,CLASS_ELITE,200); asElite.n_groups=2;
        float wl[FAC_COUNT], we[FAC_COUNT];
        faction_weights_of(&asLab,1,wl); faction_weights_of(&asElite,1,we);
        printf("   orque laboureur : Conquérants %.0f%% | orque ÉLITE régnante : Conquérants %.0f%%\n",
               wl[FAC_CONQUERANT]*100, we[FAC_CONQUERANT]*100);
        ok("la même minorité orque pèse PLUS comme élite que comme masse (qui gouverne compte)",
           we[FAC_CONQUERANT] > wl[FAC_CONQUERANT] + 0.05f);
    }

    /* ═══ 5. L'ÉTHOS EFFECTIF (§3) — la résultante que le moteur lit ══════ */
    printf("\n── 5. L'éthos effectif : la dominante fixe les poids w_* ──\n");
    {
        /* Un pays conquérant (orques) vs un pays marchand (halfelins). */
        float wc[FAC_COUNT], wm[FAC_COUNT];
        ProvincePop pc; memset(&pc,0,sizeof pc);
        pc.groups[0]=grp(cult(ETHOS_DOMINATEUR,RACE_ORQUE,CREDO_PLURALISTE),CLASS_ELITE,300);
        pc.groups[1]=grp(cult(ETHOS_DOMINATEUR,RACE_ORQUE,CREDO_PLURALISTE),CLASS_LABORER,800); pc.n_groups=2;
        ProvincePop pm; memset(&pm,0,sizeof pm);
        pm.groups[0]=grp(cult(ETHOS_MERCANTILE,RACE_HALFELIN,CREDO_PLURALISTE),CLASS_LABORER,1000); pm.n_groups=1;
        faction_weights_of(&pc,1,wc); faction_weights_of(&pm,1,wm);
        EthosWeights ec=faction_effective_weights(wc), em=faction_effective_weights(wm);
        printf("   conquérant : w_expand=%.2f w_trade=%.2f | marchand : w_expand=%.2f w_trade=%.2f\n",
               ec.w_expand, ec.w_trade, em.w_expand, em.w_trade);
        ok("le pays conquérant a un w_expand effectif PLUS fort que le marchand", ec.w_expand > em.w_expand);
        ok("le pays marchand a un w_trade effectif PLUS fort que le conquérant", em.w_trade > ec.w_trade);
    }

    /* ═══ 6. COHÉSION vs FRACTURE DE VALEURS (§6) — le frein interne ══════ */
    printf("\n── 6. Fracture de valeurs : un empire 45/45 est paralysé, un mono-éthos cohésif ──\n");
    {
        /* Mono-éthos : un seul peuple → cohésion (fracture basse). */
        ProvincePop mono; memset(&mono,0,sizeof mono);
        mono.groups[0]=grp(cult(ETHOS_BUREAUCRATE,RACE_HUMAIN,CREDO_PLURALISTE),CLASS_LABORER,1000); mono.n_groups=1;
        float wmono[FAC_COUNT]; faction_weights_of(&mono,1,wmono);
        /* Deux blocs forts et opposés (Conquérants ~ Marchands) → fracture. */
        ProvincePop split; memset(&split,0,sizeof split);
        split.groups[0]=grp(cult(ETHOS_DOMINATEUR,RACE_ORQUE,CREDO_PLURALISTE),CLASS_LABORER,1000);
        split.groups[1]=grp(cult(ETHOS_MERCANTILE,RACE_HALFELIN,CREDO_PLURALISTE),CLASS_LABORER,1000);
        split.n_groups=2;
        float wsplit[FAC_COUNT]; faction_weights_of(&split,1,wsplit);
        printf("   mono-éthos : fracture %.2f (cohésion %.2f) | bloc 50/50 opposé : fracture %.2f\n",
               faction_fracture(wmono), faction_cohesion(wmono), faction_fracture(wsplit));
        ok("un mono-éthos est COHÉSIF (fracture basse)", faction_fracture(wmono) < 0.40f);
        ok("deux factions fortes et opposées DÉCHIRENT (fracture nettement plus haute)",
           faction_fracture(wsplit) > 0.45f && faction_fracture(wsplit) > faction_fracture(wmono) + 0.15f);
        ok("fracture = 1 − cohésion", faction_fracture(wsplit) + faction_cohesion(wsplit) > 0.99f
                                   && faction_fracture(wsplit) + faction_cohesion(wsplit) < 1.01f);
    }

    /* ═══ 7. TENSION DE COUP (§5) — une faction forte aliénée vise le trône ══ */
    printf("\n── 7. Tension de coup : une faction forte OPPOSÉE à la direction couve un coup ──\n");
    {
        ok("l'opposition Gardiens↔Transgresseurs est maximale (l'épine dorsale faustienne)",
           faction_opposition(FAC_GARDIEN,FAC_TRANSGRESSEUR) > 0.95f);
        ok("une faction ne s'oppose pas à elle-même", faction_opposition(FAC_MARCHAND,FAC_MARCHAND)==0.f);
        /* Régime marchand (dominante) avec un bloc CONQUÉRANT fort et opposé. */
        ProvincePop pp; memset(&pp,0,sizeof pp);
        pp.groups[0]=grp(cult(ETHOS_MERCANTILE,RACE_HALFELIN,CREDO_PLURALISTE),CLASS_LABORER,1100);
        pp.groups[1]=grp(cult(ETHOS_DOMINATEUR,RACE_ORQUE,CREDO_PLURALISTE),CLASS_ELITE,300);
        pp.n_groups=2;
        float w[FAC_COUNT]; faction_weights_of(&pp,1,w);
        EthosFaction alien; float tension=faction_coup_tension(w,&alien);
        printf("   régime marchand + élite orque conquérante : tension de coup %.2f (faction aliénée : %s)\n",
               tension, faction_name(alien));
        ok("un bloc fort et opposé à la direction crée une TENSION de coup", tension > 0.15f);
        /* Mono-éthos : nulle opposition forte → quasi nulle tension. */
        ProvincePop mono; memset(&mono,0,sizeof mono);
        mono.groups[0]=grp(cult(ETHOS_BUREAUCRATE,RACE_HUMAIN,CREDO_PLURALISTE),CLASS_LABORER,1000);
        mono.n_groups=1;
        float wm[FAC_COUNT]; faction_weights_of(&mono,1,wm);
        ok("un mono-éthos ne couve aucun coup (nulle faction forte opposée)",
           faction_coup_tension(wm,&alien) < tension - 0.10f);
    }

    /* ═══ 8. LES LEVIERS COMME DES VOTES (§4) ═══════════════════════════ */
    printf("\n── 8. Un levier AVANCE un éthos et AIGRIT les opposés (un vote) ──\n");
    {
        faction_levers_reset();
        ok("au départ, nulle rancœur", faction_grievance(0, FAC_MARCHAND)==0.f);
        faction_lever_apply(0, FAC_GARDIEN, 0.4f);   /* foi imposée → Gardiens */
        ok("imposer la foi (Gardiens) AIGRIT les Marchands (opposés)",
           faction_grievance(0,FAC_MARCHAND) > 0.2f);
        ok("… et n'aigrit PAS la faction avancée elle-même", faction_grievance(0,FAC_GARDIEN)==0.f);
        faction_lever_apply(1, FAC_TRANSGRESSEUR, 0.4f);  /* forge à runes → Transgresseurs */
        ok("la forge à runes (Transgresseurs) aigrit les Communautaires",
           faction_grievance(1,FAC_COMMUNAUTAIRE) > 0.2f);
        float g0=faction_grievance(0,FAC_MARCHAND);
        faction_levers_decay(0.5f);
        ok("une stance non entretenue S'EFFACE (la rancœur retombe)",
           faction_grievance(0,FAC_MARCHAND) < g0 - 0.05f);
        faction_levers_reset();
        ok("reset remet tout à zéro (début de sim)",
           faction_grievance(0,FAC_MARCHAND)==0.f && faction_grievance(1,FAC_COMMUNAUTAIRE)==0.f);
    }

    /* ═══ 9. ENGAGEMENT D'ÂGE (§7) — la faction de l'heure s'avance ══════ */
    printf("\n── 9. Engagement d'âge : chaque âge avance SA faction-patronne ──\n");
    {
        ok("l'âge du Commerce s'avance pour les Marchands",       age_patron(0)==FAC_MARCHAND);
        ok("l'âge des Empires s'avance pour les Conquérants",     age_patron(2)==FAC_CONQUERANT);
        ok("l'âge de la Brèche s'avance pour les Transgresseurs", age_patron(3)==FAC_TRANSGRESSEUR);
        ok("l'âge des Soulèvements s'avance pour les Communautaires", age_patron(5)==FAC_COMMUNAUTAIRE);
    }

    printf("\n══════════════════════════════════════════════════════════════\n");
    printf(" BILAN : %d réussis, %d échoués\n", g_pass, g_fail);
    printf("══════════════════════════════════════════════════════════════\n");
    return g_fail?1:0;
}
