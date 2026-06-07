/*
 * revolt_demo.c — la révolte incarnée : qui se soulève, combien, ce qu'il advient
 *
 *   make revolt_demo && ./revolt_demo [graine]
 *
 * Une révolte n'est plus un fanion : c'est un GROUPE qui prend les armes. On
 * vérifie la chaîne entière :
 *   1. DÉFICIT — un groupe affamé/sur-taxé/étranger/non-intégré a un fort déficit ;
 *               un natif rassasié et loyal en a un faible.
 *   2. MOBILISATION — le déficit lève des combattants ; sans déficit, personne.
 *   3. NATURE — l'élite fait un COUP ; une nation conquise une SÉCESSION ; un
 *               natif mécontent une JACQUERIE.
 *   4. CHOC ÉCONOMIQUE — les mobilisés QUITTENT la main-d'œuvre (l'atelier perd ses bras).
 *   5. ÉCRASEMENT — une grosse garnison écrase les rebelles : morts, pas de pays neuf.
 *   6. SÉCESSION — garnison faible + nation étrangère → un PAYS NAÎT (la carte change).
 *   7. CONCESSION — une jacquerie victorieuse arrache une hausse de satisfaction.
 */
#include "scps_world.h"
#include "scps_econ.h"
#include "scps_demography.h"
#include "scps_prosperity.h"
#include "scps_legitimacy.h"
#include "scps_revolt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_pass=0, g_fail=0;
static void ok(const char *what, bool cond){
    printf("   %s %s\n", cond?"✓":"✗", what);
    if (cond) g_pass++; else g_fail++;
}

/* Une culture-test à axes connus (langue ignorée par la distance). */
static PopCulture cult(float v,float s,float p,float r,SpeciesArchetype race){
    PopCulture c; memset(&c,0,sizeof c);
    c.valeurs=v; c.subsistance=s; c.parente=p; c.religion=r; c.race=race; c.settled=true;
    return c;
}
static PopGroup grp(SpeciesArchetype race, SocialClass k, long n, float L, float integ, PopCulture cu, int id){
    PopGroup g; memset(&g,0,sizeof g);
    g.race=race; g.klass=k; g.count=n; g.L=L; g.integration=integ; g.culture=cu; g.origin=cu;
    g.agit_base=0.f; g.drift_id=id; g.origin_sphere=species_sphere(race);
    return g;
}

/* Fige une région en banc d'essai : propriétaire, crown, satisfaction, garnison. */
static void rig(WorldEconomy *e, int r, int owner, float food, float soc, float coerc, float H){
    RegionEconomy *re=&e->region[r];
    re->active=true; re->colonized=true; re->culture.settled=true;
    re->owner=(int16_t)owner;
    re->food_sat=food; re->society_sat=soc; re->coercion=coerc; re->satisfaction=0.5f;
    re->build.H_coerc=H;
    re->pop.n_groups=0;
    for (int k=0;k<CLASS_COUNT;k++){ re->strata[k].pop=0.f; re->strata[k].wealth=100.f; }
}
static void push(WorldEconomy *e, int r, PopGroup g){
    RegionEconomy *re=&e->region[r];
    re->strata[g.klass].pop += (float)g.count;
    re->pop.groups[re->pop.n_groups++]=g;
}
/* Isole un soulèvement : SEULE la région `r` appartient à `owner` (sinon la
 * puissance militaire de la couronne — somme sur ses régions — fausserait la
 * garnison d'un test à l'autre). */
static void solo_owner(WorldEconomy *e, int r, int owner){
    for (int i=0;i<e->n_regions;i++) e->region[i].owner=-1;
    e->region[r].owner=(int16_t)owner;
}

int main(int argc, char **argv){
    uint32_t seed=(argc>1)?(uint32_t)strtoul(argv[1],NULL,10):42u;
    World *w=malloc(sizeof(World));
    WorldEconomy *e=malloc(sizeof(WorldEconomy));
    WorldLegitimacy *wl=malloc(sizeof(WorldLegitimacy));
    WorldProsperity *wp=malloc(sizeof(WorldProsperity));
    ModifierStack *drift=malloc(sizeof(ModifierStack));
    if(!w||!e||!wl||!wp||!drift){ fprintf(stderr,"OOM\n"); return 1; }
    memset(wl,0,sizeof *wl); memset(wp,0,sizeof *wp); memset(drift,0,sizeof *drift);

    printf("══════════════════════════════════════════════════════════════\n");
    printf(" LA RÉVOLTE INCARNÉE — qui se soulève, combien, ce qu'il advient — graine %u\n", seed);
    printf("══════════════════════════════════════════════════════════════\n");

    WorldParams p=worldparams_default(seed);
    world_generate(w,&p); econ_init(e,w);
    if (e->n_regions<5){ fprintf(stderr,"monde trop petit\n"); return 1; }

    /* table rase : on maîtrise propriétaires et populations. */
    for (int r=0;r<e->n_regions;r++){ e->region[r].owner=-1; e->region[r].pop.n_groups=0; }
    for (int i=0;i<SCPS_MAX_REG;i++){ wl->L[i]=6.f; wl->years_held[i]=50.f; }

    /* la COURONNE = culture de la région-capitale du pays 5 (humaine, médiane).
     * On laisse de la place dans la table des pays pour qu'une sécession PUISSE
     * faire naître un nouveau pays (le worldgen en remplit parfois jusqu'au plafond). */
    w->n_countries=8;
    PopCulture crown=cult(5,5,5,5,RACE_HUMAIN);
    int OWNER=5;
    w->country[OWNER].capital_prov=w->region[0].province_ids[0];
    w->country[OWNER].role=POLITY_ANTAGONIST;
    w->country[OWNER].n_regions=1; w->country[OWNER].region_ids[0]=0;
    e->region[0].culture=crown;                 /* crown_of(5) lira ceci */
    int n_countries_0=w->n_countries;

    /* ═══ 1. DÉFICIT — le grief est ANCRÉ ═══════════════════════════════ */
    printf("\n── 1. Le déficit : la misère ANCRÉE sur un groupe ──\n");
    PopCulture foreign=cult(9,9,2,9,RACE_ORQUE);   /* loin de la couronne (D≈4) */
    PopGroup misery = grp(RACE_ORQUE, CLASS_LABORER, 8000, 2.f, 0.15f, foreign, 101);
    PopGroup content= grp(RACE_HUMAIN, CLASS_LABORER, 8000, 7.f, 1.00f, crown, 102);
    float d_mis = revolt_group_deficit(&misery,  drift, &crown, 0.05f, 0.20f, 0.4f, 0.4f);
    float d_con = revolt_group_deficit(&content, drift, &crown, 0.95f, 0.90f, 0.0f, 0.0f);
    printf("   déficit : affamé/étranger=%.2f  vs  rassasié/natif=%.2f\n", d_mis, d_con);
    ok("le groupe affamé/étranger/non-intégré a un FORT déficit", d_mis>0.55f);
    ok("le groupe rassasié, natif et loyal a un FAIBLE déficit",  d_con<0.15f);

    /* ═══ 2. MOBILISATION — le déficit lève des bras ════════════════════ */
    printf("\n── 2. La mobilisation : le déficit prend les armes ──\n");
    long m_mis=revolt_mobilized(&misery, d_mis);
    long m_con=revolt_mobilized(&content, d_con);
    printf("   mobilisés : misère=%ld  vs  contentement=%ld (sur 8000)\n", m_mis, m_con);
    ok("un fort déficit MOBILISE des combattants", m_mis>500);
    ok("un faible déficit ne mobilise personne",   m_con==0);
    ok("la mobilisation est PLAFONNÉE (jamais >40 %)", m_mis<=3200);

    /* ═══ 3. NATURE — qui se lève → ce qu'il veut ═══════════════════════ */
    printf("\n── 3. La nature : qui se lève détermine ce qu'il veut ──\n");
    PopGroup noble = grp(RACE_HUMAIN, CLASS_ELITE, 400, 3.f, 1.0f, crown, 103);
    ok("l'ÉLITE vise le trône (coup d'État)",        revolt_classify(&noble, drift,&crown)==REBEL_COUP);
    ok("la nation étrangère mal liée fait SÉCESSION", revolt_classify(&misery,drift,&crown)==REBEL_SECESSION);
    ok("le natif mécontent fait une JACQUERIE",       revolt_classify(&content,drift,&crown)==REBEL_CLASS);

    /* ═══ 4. CHOC ÉCONOMIQUE — les bras quittent l'atelier ══════════════ */
    printf("\n── 4. Le choc économique : les mobilisés quittent la main-d'œuvre ──\n");
    RevoltState rs; revolt_init(&rs);
    rig(e, 1, OWNER, 0.05f, 0.20f, 0.4f, 0.f);
    push(e, 1, grp(RACE_ORQUE, CLASS_LABORER, 8000, 2.f, 0.15f, foreign, 201));
    float labor_before=e->region[1].strata[CLASS_LABORER].pop;
    int idx=revolt_ignite(&rs, w, e, drift, 1, 0.4f);
    float labor_after=e->region[1].strata[CLASS_LABORER].pop;
    printf("   main-d'œuvre : %.0f → %.0f (partis au combat : %ld)\n",
           labor_before, labor_after, idx>=0?rs.list[idx].mobilized:0);
    ok("le soulèvement s'ALLUME sur le groupe au pire déficit", idx>=0);
    ok("les mobilisés QUITTENT la main-d'œuvre (choc productif)", labor_after < labor_before-100.f);

    /* ═══ 5. ÉCRASEMENT — la grosse garnison gagne ══════════════════════ */
    printf("\n── 5. L'écrasement : une forte garnison brise la jacquerie ──\n");
    revolt_init(&rs);
    solo_owner(e, 2, OWNER);
    rig(e, 2, OWNER, 0.05f, 0.20f, 0.3f, 20.f);   /* H=20 → garnison écrasante */
    push(e, 2, grp(RACE_HUMAIN, CLASS_LABORER, 4000, 2.f, 1.0f, crown, 202)); /* natif → jacquerie */
    int before_countries=w->n_countries;
    long pop_lost_before=rs.pop_lost;
    int ix=revolt_ignite(&rs, w, e, drift, 2, 0.4f);
    revolt_tick(&rs, w, e, drift, wl, wp, 120);
    printf("   verdict : %s | morts=%ld | pays avant/après=%d/%d\n",
           ix>=0?revolt_outcome_word(rs.list[ix].outcome):"(rien)",
           rs.pop_lost-pop_lost_before, before_countries, w->n_countries);
    ok("la jacquerie est ÉCRASÉE par la garnison", ix>=0 && rs.list[ix].outcome==OUT_CRUSHED);
    ok("l'échec se paie en MORTS",                 rs.pop_lost>pop_lost_before);
    ok("aucun pays ne naît d'une révolte écrasée",  w->n_countries==before_countries);

    /* ═══ 6. SÉCESSION — un pays naît ═══════════════════════════════════ */
    printf("\n── 6. La sécession : garnison faible + nation étrangère → un PAYS NAÎT ──\n");
    revolt_init(&rs);
    solo_owner(e, 3, OWNER);
    rig(e, 3, OWNER, 0.02f, 0.10f, 0.5f, 0.f);    /* H=0 → garnison faible */
    push(e, 3, grp(RACE_ORQUE, CLASS_LABORER, 9000, 2.f, 0.12f, foreign, 203));
    int sec_before=w->n_countries;
    int iy=revolt_ignite(&rs, w, e, drift, 3, 0.5f);
    revolt_tick(&rs, w, e, drift, wl, wp, 120);
    int born = (iy>=0)?rs.list[iy].spawned:-1;
    printf("   verdict : %s | pays né n°%d (table %d→%d) | propriétaire région 3 : %d→%d\n",
           iy>=0?revolt_outcome_word(rs.list[iy].outcome):"(rien)",
           born, sec_before, w->n_countries, OWNER, e->region[3].owner);
    ok("la nation étrangère fait SÉCESSION",        iy>=0 && rs.list[iy].outcome==OUT_SECEDED);
    ok("un PAYS NAÎT (slot neuf ou vacant réutilisé)", born>=0 && born!=OWNER);
    ok("la région passe à la NOUVELLE couronne",     e->region[3].owner==born && born>=0);

    /* ═══ 7. CONCESSION — la couronne cède ══════════════════════════════ */
    printf("\n── 7. La concession : une jacquerie victorieuse arrache un mieux ──\n");
    revolt_init(&rs);
    solo_owner(e, 4, OWNER);
    rig(e, 4, OWNER, 0.0f, 0.0f, 1.0f, 0.f);      /* misère totale, garnison faible */
    push(e, 4, grp(RACE_HUMAIN, CLASS_LABORER, 9000, 2.f, 1.0f, crown, 204)); /* natif désespéré */
    float sat_before=e->region[4].satisfaction;
    int iz=revolt_ignite(&rs, w, e, drift, 4, 1.0f);
    revolt_tick(&rs, w, e, drift, wl, wp, 120);
    float sat_after=e->region[4].satisfaction;
    printf("   verdict : %s | satisfaction %.2f→%.2f\n",
           iz>=0?revolt_outcome_word(rs.list[iz].outcome):"(rien)", sat_before, sat_after);
    ok("la jacquerie victorieuse arrache une CONCESSION", iz>=0 && rs.list[iz].outcome==OUT_CONCESSION);
    ok("la satisfaction MONTE (la couronne a cédé)",       sat_after>sat_before);

    /* ═══ 8. SCAN — la misère SOUTENUE finit par lever la région ════════ */
    printf("\n── 8. Le scan : la misère soutenue (mois après mois) finit par soulever ──\n");
    revolt_init(&rs);
    solo_owner(e, 1, OWNER);
    rig(e, 1, OWNER, 0.02f, 0.05f, 0.5f, 0.f);
    push(e, 1, grp(RACE_ORQUE, CLASS_LABORER, 9000, 2.f, 0.12f, foreign, 211));
    int months=0;
    for (; months<6 && revolt_active_count(&rs)==0; months++) revolt_scan(&rs, w, e, drift, 30);
    printf("   région désespérée : soulèvement au bout de %d mois de misère soutenue\n", months);
    ok("une région désespérée finit par se SOULEVER (misère soutenue)", revolt_active_count(&rs)>0);
    /* une région contente, scannée autant, ne se lève JAMAIS */
    revolt_init(&rs);
    solo_owner(e, 2, OWNER);
    rig(e, 2, OWNER, 0.95f, 0.90f, 0.0f, 0.f);
    push(e, 2, grp(RACE_HUMAIN, CLASS_LABORER, 9000, 7.f, 1.0f, crown, 212));
    for (int mo=0; mo<6; mo++) revolt_scan(&rs, w, e, drift, 30);
    ok("une région contente ne se soulève jamais (aucun grief)", revolt_active_count(&rs)==0);

    /* ═══ 9. REVANCHISME — subir la conquête arme le séparatisme ════════ */
    printf("\n── 9. Le revanchisme : la province fraîchement conquise se libère « quoi qu'il arrive » ──\n");
    /* Garnison FORTE (citadelle H=10) : sans revanchisme, la sécession est écrasée… */
    revolt_init(&rs);
    solo_owner(e, 1, OWNER);
    rig(e, 1, OWNER, 0.4f, 0.4f, 0.3f, 10.f);
    push(e, 1, grp(RACE_ORQUE, CLASS_LABORER, 6000, 2.f, 0.12f, foreign, 221));
    int j1=revolt_ignite(&rs, w, e, drift, 1, 0.3f);
    revolt_tick(&rs, w, e, drift, wl, wp, 120);
    int out_plain = (j1>=0)?rs.list[j1].outcome:-1;
    /* …MÊME garnison, mais la province vient d'être conquise (revanchisme actif) → libre. */
    revolt_init(&rs);
    solo_owner(e, 2, OWNER);
    rig(e, 2, OWNER, 0.4f, 0.4f, 0.3f, 10.f);
    push(e, 2, grp(RACE_ORQUE, CLASS_LABORER, 6000, 2.f, 0.12f, foreign, 222));
    revolt_on_conquest(&rs, 2);                /* on vient de la soumettre */
    int j2=revolt_ignite(&rs, w, e, drift, 2, 0.3f);
    revolt_tick(&rs, w, e, drift, wl, wp, 120);
    int out_rev = (j2>=0)?rs.list[j2].outcome:-1;
    printf("   même citadelle (H=10) : sans revanchisme → %s ; juste conquise → %s\n",
           revolt_outcome_word(out_plain), revolt_outcome_word(out_rev));
    ok("sans revanchisme, la citadelle ÉCRASE la sécession", out_plain==OUT_CRUSHED);
    ok("le REVANCHISME post-conquête donne l'INDÉPENDANCE malgré la citadelle", out_rev==OUT_SECEDED);

    /* ═══ 10. CICATRICE — une révolte résolue stunt la province ════════ */
    printf("\n── 10. La cicatrice : une révolte résolue laisse une plaie (−50 %% dévelop.) ──\n");
    revolt_init(&rs);
    solo_owner(e, 4, OWNER);
    rig(e, 4, OWNER, 0.05f, 0.20f, 0.3f, 20.f);   /* H haut → écrasement */
    push(e, 4, grp(RACE_HUMAIN, CLASS_LABORER, 4000, 2.f, 1.0f, crown, 241));
    e->region[4].revolt_scar=0.f;
    revolt_ignite(&rs, w, e, drift, 4, 0.4f);
    revolt_tick(&rs, w, e, drift, wl, wp, 120);
    printf("   cicatrice après écrasement : %.2f\n", e->region[4].revolt_scar);
    ok("une révolte résolue LAISSE une cicatrice de développement", e->region[4].revolt_scar > 0.4f);

    (void)n_countries_0;
    printf("\n══════════════════════════════════════════════════════════════\n");
    printf(" BILAN : %d réussis, %d échoués\n", g_pass, g_fail);
    printf("══════════════════════════════════════════════════════════════\n");
    free(w); free(e); free(wl); free(wp); free(drift);
    return g_fail?1:0;
}
