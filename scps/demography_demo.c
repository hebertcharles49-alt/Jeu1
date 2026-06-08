/*
 * demography_demo.c — la clé de voûte : la province contient des groupes
 *
 *   make demography_demo && ./demography_demo [graine]
 *
 * Prouve les huit points du cahier :
 *   1. D réel par province (mixte → D∞ haut ; homogène → 0).
 *   2. H jouable : réprimer baisse l'agitation MAIS ronge la L et monte la fragilité.
 *   3. Réversibilité (Kuran) : H tombe + L basse → le groupe revient frondeur.
 *   4. Assimilation vraie : dérive DURABLE, timer ∝ D∞ (Halfelin ~20 ans, Orque ~80).
 *   5. Migration : un groupe afflue, devient diaspora (garde sa race/culture), crée du D.
 *   6. Agrégation : country_D∞ alimente scps_order ; conquérir du lointain → fracture↑.
 *   7. UI : composition (race/culture/classe/loyauté/état) sans un nom SCPS.
 *   8. Non-régression : une province mono-groupe = les nombres d'aujourd'hui.
 */
#include "scps_demography.h"
#include "scps_core.h"   /* le verdict vérifié (banc d'essai seulement) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_pass=0, g_fail=0;
static void ok(const char *what, bool cond){
    printf("   %s %s\n", cond?"✓":"✗", what);
    if (cond) g_pass++; else g_fail++;
}
static float absf(float v){ return v<0?-v:v; }
static float cdist(const PopCulture*a,const PopCulture*b){
    float dv=absf(a->valeurs-b->valeurs),ds=absf(a->subsistance-b->subsistance);
    float dp=absf(a->parente-b->parente),dr=absf(a->religion-b->religion);
    float m=dv;if(ds>m)m=ds;if(dp>m)m=dp;if(dr>m)m=dr;return m;
}
static float agitL(float L){ float a=(6.f-L)*15.f; return a<0?0:(a>100?100:a); }

static PopCulture cult(float v,float s,float p,float r,Ethos e){
    PopCulture c; memset(&c,0,sizeof c);
    c.langue=5; c.valeurs=v; c.subsistance=s; c.parente=p; c.religion=r;
    c.ethos=e; c.lifeway=LIFE_FARMER; c.structure=STRUCT_LIGNAGER; c.credo=CREDO_PLURALISTE;
    c.rel_branch=REL_ABRAHAMIQUE; c.martial=MART_MUR_BOUCLIERS; c.settled=true; return c;
}
static int g_id=1;
static PopGroup grp(SpeciesArchetype race, Sphere sph, PopCulture o, SocialClass k,
                    long count, float L, float integ, bool diaspora){
    PopGroup g; memset(&g,0,sizeof g);
    g.race=race; g.origin_sphere=sph; g.origin=o; g.culture=o; g.klass=k; g.count=count;
    g.L=L; g.agit_base=agitL(L); g.integration=integ; g.diaspora=diaspora; g.drift_id=g_id++;
    return g;
}

int main(int argc, char **argv){
    uint32_t seed=(argc>1)?(uint32_t)strtoul(argv[1],NULL,10):42u; (void)seed;
    ModifierStack *drift=malloc(sizeof(ModifierStack)); memset(drift,0,sizeof(*drift));

    printf("══════════════════════════════════════════════════════════════\n");
    printf(" DÉMOGRAPHIE — la province contient des GROUPES (clé de voûte)\n");
    printf("══════════════════════════════════════════════════════════════\n");

    /* La couronne (culture régnante du pays). */
    PopCulture crown = cult(2,6,3,2, ETHOS_ORDRE);
    PopCulture humc  = crown;                                   /* natifs = couronne */
    PopCulture halfc = cult(4,6,5,4, ETHOS_BUREAUCRATE);        /* proche : D∞≈2 */
    PopCulture orcc  = cult(9,0,9,10, ETHOS_DOMINATEUR);        /* gouffre : D∞≈8 */

    /* ═══ 1. D RÉEL PAR PROVINCE ════════════════════════════════════════ */
    printf("\n── 1. D interne PAR province (entre groupes) ──\n");
    ProvincePop mixed; memset(&mixed,0,sizeof mixed);
    mixed.groups[0]=grp(RACE_HUMAIN,SPHERE_HOMMES,humc,CLASS_LABORER,700,8.0f,1.0f,false);
    mixed.groups[1]=grp(RACE_ORQUE, SPHERE_ETRANGERS,orcc,CLASS_LABORER,300,1.5f,0.2f,false);
    mixed.n_groups=2;
    ProvincePop homo; memset(&homo,0,sizeof homo);
    homo.groups[0]=grp(RACE_HUMAIN,SPHERE_HOMMES,humc,CLASS_LABORER,1000,8.0f,1.0f,false);
    homo.n_groups=1;
    float dmix=province_Dinf(&mixed,drift), dhom=province_Dinf(&homo,drift);
    printf("   province 70%% Humains / 30%% Orques : D∞=%.1f | province homogène : D∞=%.1f\n", dmix, dhom);
    ok("une province conquise mixte a un D∞ interne VRAI (élevé)", dmix>6.f);
    ok("une province homogène a un D∞ interne nul", dhom==0.f);

    /* ═══ 8. NON-RÉGRESSION (mono-groupe = aujourd'hui) ════════════════ */
    printf("\n── 8. Non-régression : une province mono-groupe = les nombres d'hier ──\n");
    ok("mono-groupe : D∞=0, L = la L du groupe, pop = l'effectif",
       province_Dinf(&homo,drift)==0.f && province_L(&homo)==8.0f
       && province_total_pop(&homo)==1000);

    /* ═══ 2. H JOUABLE — il SUPPRIME (et ronge L, monte la fragilité) ══ */
    printf("\n── 2. H jouable : on réprime la minorité restive d'UNE province ──\n");
    float agit0=province_agitation(&mixed,drift), orcL0=mixed.groups[1].L;
    CoercionEffect ce=province_apply_coercion(&mixed,drift,8.0f);
    float agit1=province_agitation(&mixed,drift), orcL1=mixed.groups[1].L;
    printf("   agitation province %.0f→%.0f | L du groupe orque %.2f→%.2f | fragilité +%.1f\n",
           agit0,agit1, orcL0,orcL1, ce.fragility_rise);
    ok("la coercition BAISSE l'agitation immédiatement (réprime)", agit1 < agit0 - 5.f);
    ok("… mais RONGE la L du groupe coercé (on règne par la force)", orcL1 < orcL0);
    ok("… et MONTE la fragilité (l'ordre tenu par la botte)", ce.fragility_rise > 0.f);

    /* ═══ 3. RÉVERSIBILITÉ (Kuran) — la botte se lève ═════════════════ */
    printf("\n── 3. Réversibilité : H tombe + L basse → le groupe revient frondeur ──\n");
    float orc_supp=group_agitation_effective(&mixed.groups[1],drift);  /* agitation orque réprimée */
    province_lift_coercion(&mixed,drift);                              /* la botte se lève */
    float orc_ret=group_agitation_effective(&mixed.groups[1],drift);   /* … elle resurgit */
    printf("   agitation du groupe orque : réprimée=%.0f → la botte se lève → %.0f (préférence falsifiée)\n",
           orc_supp, orc_ret);
    ok("la suppression n'a RIEN métabolisé : l'agitation revient (Kuran)",
       orc_ret > orc_supp + 30.f && mixed.groups[1].L < 4.f);

    /* ═══ 4. ASSIMILATION VRAIE — dérive durable, timer ∝ D∞ ═══════════ */
    printf("\n── 4. Assimilation : dérive DURABLE vers la dominante, timer ∝ D∞ ──\n");
    printf("   estimation : Halfelin (D∞≈2) ~%.0f ans ; Orque (D∞≈8) ~%.0f ans\n",
           assimilation_years(2.f,5.f,5.f), assimilation_years(8.f,5.f,5.f));
    ok("le gouffre allonge le timer (Halfelin ≪ Orque)",
       assimilation_years(2.f,5.f,5.f) < assimilation_years(8.f,5.f,5.f)*0.5f);
    ProvincePop town; memset(&town,0,sizeof town);
    town.groups[0]=grp(RACE_HUMAIN, SPHERE_HOMMES,   humc, CLASS_LABORER,1000,8.f,1.f,false);
    town.groups[1]=grp(RACE_HALFELIN,SPHERE_HOMMES,  halfc,CLASS_BOURGEOIS, 200,5.f,0.5f,false);
    town.groups[2]=grp(RACE_ORQUE,   SPHERE_ETRANGERS,orcc, CLASS_LABORER, 200,2.f,0.2f,false);
    town.n_groups=3;
    int half_id=town.groups[1].drift_id, orc_id=town.groups[2].drift_id;
    float half_d0=cdist(&town.groups[1].origin,&crown), orc_d0=cdist(&town.groups[2].origin,&crown);
    for (int yr=0; yr<40; yr++) assimilation_tick(&town, drift, 5.f, 5.f, 1.f);
    /* le Halfelin a-t-il fusionné (ou quasi) ? l'Orque traîne-t-il encore ? */
    bool half_gone=true; float half_d=0, orc_d=0;
    for (int i=0;i<town.n_groups;i++){
        PopCulture eff=group_culture_effective(&town.groups[i],drift);
        if (town.groups[i].drift_id==half_id){ half_gone=false; half_d=cdist(&eff,&crown); }
        if (town.groups[i].drift_id==orc_id){ orc_d=cdist(&eff,&crown); }
    }
    printf("   après 40 ans : Halfelin %s (départ D∞=%.0f) | Orque D∞ %.0f→%.1f (gouffre, lent)\n",
           half_gone?"ASSIMILÉ (fusionné)":"en cours", half_d0, orc_d0, orc_d);
    ok("le proche s'assimile (fusion) bien avant le lointain (timer ∝ D∞)",
       (half_gone || half_d<1.f) && orc_d>4.f);
    /* Durabilité : la dérive métabolisée ne saute PAS quand la coercition se lève. */
    PopCulture orc_eff_before=group_culture_effective(&town.groups[town.n_groups-1],drift);
    province_apply_coercion(&town,drift,5.f); province_lift_coercion(&town,drift);
    PopCulture orc_eff_after=group_culture_effective(&town.groups[town.n_groups-1],drift);
    ok("l'assimilation est DURABLE (la dérive métabolisée ne saute pas à la levée de H)",
       cdist(&orc_eff_before,&orc_eff_after) < 0.01f);

    /* ═══ 4b. CONVERSION RELIGIEUSE — la FOI converge vers le TRÔNE ════ */
    printf("\n── 4b. Conversion : la branche sacrée bascule vers la couronne (si prosélyte) ──\n");
    /* Une minorité HÉRÉTIQUE : culture quasi identique au trône, mais d'une
     * AUTRE branche sacrée (Dharmique vs Abrahamique) → un mur de fracture. */
    PopCulture heretic = cult(2,6,3,5, ETHOS_ORDRE);    /* v/s/p = couronne ; foi=5 vs 2 */
    heretic.rel_branch = REL_DHARMIQUE;                 /* l'autre branche : le vrai clivage */
    /* (a) TOLÉRANCE : un trône pluraliste ne convertit personne. */
    PopCulture crown_plu = crown; crown_plu.credo = CREDO_PLURALISTE; crown_plu.religion = 2.f;
    ProvincePop plu; memset(&plu,0,sizeof plu);
    plu.groups[0]=grp(RACE_HUMAIN,SPHERE_HOMMES,crown_plu,CLASS_LABORER,800,8.f,1.f,false);
    plu.groups[1]=grp(RACE_HUMAIN,SPHERE_HOMMES,heretic,  CLASS_LABORER,200,6.f,0.9f,false);
    plu.n_groups=2;
    for (int yr=0; yr<200; yr++) faith_convert_tick(&plu, &crown_plu, 300.f, 1.f);
    ok("un trône PLURALISTE ne convertit personne (tolérance : empire multi-confessionnel)",
       plu.groups[1].origin.rel_branch == REL_DHARMIQUE);
    /* (b) PURIFICATION : la branche hérétique BASCULE vers la couronne, puis fusionne. */
    PopCulture crown_pur = crown; crown_pur.credo = CREDO_PURIFICATEUR; crown_pur.religion = 2.f;
    ProvincePop pur; memset(&pur,0,sizeof pur);
    pur.groups[0]=grp(RACE_HUMAIN,SPHERE_HOMMES,crown_pur,CLASS_LABORER,800,8.f,1.f,false);
    pur.groups[1]=grp(RACE_HUMAIN,SPHERE_HOMMES,heretic,  CLASS_LABORER,200,6.f,0.9f,false);
    pur.n_groups=2;
    int her_id = pur.groups[1].drift_id;
    bool flipped=false;
    for (int yr=0; yr<120 && !flipped; yr++){
        faith_convert_tick(&pur, &crown_pur, 300.f, 1.f);
        for (int i=0;i<pur.n_groups;i++)
            if (pur.groups[i].drift_id==her_id && pur.groups[i].origin.rel_branch==REL_ABRAHAMIQUE) flipped=true;
    }
    ok("un trône PURIFICATEUR fait BASCULER la branche hérétique vers la sienne", flipped);
    /* … et l'axe ayant convergé, la conversion ACHÈVE l'assimilation : fusion. */
    bool fused_after_conv=false;
    for (int yr=0; yr<120; yr++){
        faith_convert_tick(&pur, &crown_pur, 300.f, 1.f);
        assimilation_tick(&pur, drift, 5.f, 5.f, 1.f);
    }
    fused_after_conv = (pur.n_groups==1);  /* l'hérétique converti a fondu dans la dominante */
    ok("la conversion fait tomber le mur de branche → l'assimilation peut ACHEVER (fusion)",
       fused_after_conv);
    /* (c) GRADIENT de prosélytisme : à 30 ans de règne, le purificateur a déjà
     *     converti ; l'évangéliste, plus lent (deux générations), pas encore. */
    PopCulture crown_eva = crown; crown_eva.credo = CREDO_EVANGELISTE; crown_eva.religion = 2.f;
    ProvincePop eva; memset(&eva,0,sizeof eva);
    eva.groups[0]=grp(RACE_HUMAIN,SPHERE_HOMMES,crown_eva,CLASS_LABORER,800,8.f,1.f,false);
    eva.groups[1]=grp(RACE_HUMAIN,SPHERE_HOMMES,heretic,  CLASS_LABORER,200,6.f,0.9f,false);
    eva.n_groups=2;
    ProvincePop pur30; memset(&pur30,0,sizeof pur30);
    pur30.groups[0]=grp(RACE_HUMAIN,SPHERE_HOMMES,crown_pur,CLASS_LABORER,800,8.f,1.f,false);
    pur30.groups[1]=grp(RACE_HUMAIN,SPHERE_HOMMES,heretic, CLASS_LABORER,200,6.f,0.9f,false);
    pur30.n_groups=2;
    for (int yr=0; yr<30; yr++){
        faith_convert_tick(&eva,   &crown_eva, 30.f, 1.f);
        faith_convert_tick(&pur30, &crown_pur, 30.f, 1.f);
    }
    ok("le PURIFICATEUR convertit en une génération ; l'ÉVANGÉLISTE traîne (deux)",
       pur30.groups[1].origin.rel_branch==REL_ABRAHAMIQUE &&
       eva.groups[1].origin.rel_branch==REL_DHARMIQUE);

    /* ═══ 5. MIGRATION — emporte race + culture, crée du D ════════════ */
    printf("\n── 5. Migration : un groupe afflue vers la prospérité, devient diaspora ──\n");
    ProvincePop poor; memset(&poor,0,sizeof poor); poor.prosperity=3.f;
    poor.groups[0]=grp(RACE_ORQUE,SPHERE_ETRANGERS,orcc,CLASS_LABORER,500,4.f,0.5f,false);
    poor.n_groups=1;
    ProvincePop rich; memset(&rich,0,sizeof rich); rich.prosperity=8.f;
    rich.groups[0]=grp(RACE_HUMAIN,SPHERE_HOMMES,humc,CLASS_LABORER,800,8.f,1.f,false);
    rich.n_groups=1;
    float rich_d_before=province_Dinf(&rich,drift);
    bool moved = (rich.prosperity>poor.prosperity) && migration_move(&poor,&rich,0,200, g_id++);
    float rich_d_after=province_Dinf(&rich,drift);
    bool diaspora = (rich.n_groups==2 && rich.groups[1].diaspora && rich.groups[1].race==RACE_ORQUE);
    printf("   les Orques affluent (prospérité 3→8) : province d'accueil D∞ %.0f→%.1f, diaspora=%d\n",
           rich_d_before, rich_d_after, diaspora);
    ok("le groupe migre vers la province plus prospère", moved);
    ok("il y devient minorité/diaspora en GARDANT sa race/culture → crée du D interne",
       diaspora && rich_d_after>4.f && rich_d_before==0.f);

    /* ═══ 6. AGRÉGATION PAYS → scps_order (verdict inchangé) ══════════ */
    printf("\n── 6. Le pays agrège ; conquérir du lointain monte D∞ → fracture ──\n");
    ProvincePop country[2]; memset(country,0,sizeof country);
    country[0]=homo;                                            /* province humaine homogène */
    country[1].groups[0]=grp(RACE_HUMAIN,SPHERE_HOMMES,humc,CLASS_LABORER,600,8.f,1.f,false);
    country[1].n_groups=1;
    float cd0=country_Dbar(country,2,drift), cL0=country_L(country,2);
    ScpsState s0={0}; s0.D_bar=cd0; s0.C=5; s0.P=5; s0.K=5; s0.H=2; s0.F=5; s0.I=4; s0.L=cL0;
    float frac0=scps_order(&s0).fracture;
    /* CONQUÊTE : une province orque lointaine entre dans le pays. */
    country[1].groups[1]=grp(RACE_ORQUE,SPHERE_ETRANGERS,orcc,CLASS_LABORER,400,1.5f,0.1f,false);
    country[1].n_groups=2;
    float cd1=country_Dbar(country,2,drift), cdinf=country_Dinf(country,2,drift), cL1=country_L(country,2);
    ScpsState s1={0}; s1.D_bar=cd1; s1.C=5; s1.P=5; s1.K=5; s1.H=2; s1.F=5; s1.I=4; s1.L=cL1;
    float frac1=scps_order(&s1).fracture;
    printf("   pays : D̄ %.1f→%.1f (D∞ maillon faible %.1f), L %.1f→%.1f → fracture %.2f→%.2f\n",
           cd0,cd1, cdinf, cL0,cL1, frac0,frac1);
    ok("conquérir du lointain monte le D du pays ET fait chuter sa L", cd1>cd0 && cL1<cL0);
    ok("le verdict VÉRIFIÉ (scps_order, inchangé) en tire plus de fracture", frac1>frac0);

    /* ═══ 7. UI — la composition par la membrane (mots, pas de SCPS) ══ */
    printf("\n── 7. Le panneau de province : la COMPOSITION en mots ──\n");
    GroupReadout comp[DEMO_MAX_GROUPS];
    int nc=province_composition(&town,drift,&crown,5.f,5.f,comp,DEMO_MAX_GROUPS);
    int sumpct=0;
    for (int i=0;i<nc;i++){
        printf("   %3d%%  %-9s · %-11s · %-10s · %s\n",
               comp[i].percent, comp[i].race, comp[i].klass, label_humeur(comp[i].loyaute), comp[i].etat);
        sumpct+=comp[i].percent;
    }
    ok("la composition montre race / classe / loyauté(MOT) / état, sans un nom SCPS",
       nc>=2 && sumpct>=95 && sumpct<=100
       && comp[0].race && comp[0].klass && comp[0].etat);

    printf("\n══════════════════════════════════════════════════════════════\n");
    printf(" BILAN : %d réussis, %d échoués\n", g_pass, g_fail);
    printf("══════════════════════════════════════════════════════════════\n");
    free(drift);
    return g_fail?1:0;
}
