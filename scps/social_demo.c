/*
 * social_demo.c — le tissu social : brasserie, boisson culturelle, foi
 *
 *   make social_demo && ./social_demo [graine]
 *
 * Première passe du catalogue SOCIAL (au-delà des chaînes matérielles déjà
 * câblées). On vérifie :
 *   1. BRASSERIE — le grain devient de la BIÈRE (la chaîne vivrière du commun).
 *   2. VARIANTE CULTURELLE — le palier MORAL (boisson) est une variante : une
 *      culture de basse subsistance (clans/nains/orques) est CONTENTE avec la
 *      bière et BOUDE le vin ; une culture urbaine, l'inverse.
 *   3. FOI — un Temple bâti SOUTIENT la légitimité locale (sacraliser le trône
 *      apaise sans réprimer) — la coordonnée que la légitimité LIT.
 */
#include "scps_world.h"
#include "scps_econ.h"
#include "scps_legitimacy.h"
#include "scps_agency.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_pass=0, g_fail=0;
static void ok(const char *what, bool cond){
    printf("   %s %s\n", cond?"✓":"✗", what);
    if (cond) g_pass++; else g_fail++;
}

/* Société servie avec UNE boisson donnée, pour une culture de subsistance donnée.
 * Tous les AUTRES biens sociaux sont abondants → la BOISSON est la variable. */
static float society_with_drink(WorldEconomy *e, int r, float subsistance, Resource drink){
    RegionEconomy *re=&e->region[r];
    re->active=true; re->colonized=true; re->culture.settled=true;
    re->culture.subsistance=subsistance; re->owner=0;
    re->n_bld=0; re->coercion=0.f; re->over_tax=0.f;
    for (int k=0;k<RES_COUNT;k++){ re->raw_cap[k]=0.f; re->stock[k]=0.f; re->price[k]=1.0f; }
    re->strata[CLASS_LABORER].pop=1000.f; re->strata[CLASS_LABORER].wealth=1e6f;
    re->strata[CLASS_BOURGEOIS].pop=200.f;re->strata[CLASS_BOURGEOIS].wealth=1e6f;
    re->strata[CLASS_ELITE].pop=50.f;     re->strata[CLASS_ELITE].wealth=1e6f;
    /* vivres + tous les biens sociaux NON-boisson, abondants */
    re->stock[RES_GRAIN]=1e5f; re->stock[RES_FISH]=1e5f; re->stock[RES_WOOD]=1e5f;
    re->stock[RES_CLOTH]=1e5f; re->stock[RES_PAPER]=1e5f; re->stock[RES_SALT]=1e5f;
    re->stock[RES_FUR]=1e5f;   re->stock[RES_PRECIOUS_WARE]=1e5f; re->stock[RES_PRECIOUS_CLOTH]=1e5f;
    /* la SEULE boisson disponible = celle testée */
    re->stock[drink]=1e5f;
    econ_tick(e, 1.f);
    return re->society_sat;
}

/* Satisfaction de l'ÉLITE servie d'UN luxe donné (orfèvrerie ou étoffe), pour une
 * culture de subsistance donnée. Les deux boissons sont servies (palier moral
 * neutralisé) → seul le LUXE varie. */
static float elite_sat_with_luxe(WorldEconomy *e, int r, float subsistance, Resource luxe){
    RegionEconomy *re=&e->region[r];
    re->active=true; re->colonized=true; re->culture.settled=true;
    re->culture.subsistance=subsistance; re->owner=0;
    re->n_bld=0; re->coercion=0.f; re->over_tax=0.f;
    for (int k=0;k<RES_COUNT;k++){ re->raw_cap[k]=0.f; re->stock[k]=0.f; re->price[k]=1.0f; }
    re->strata[CLASS_LABORER].pop=1000.f; re->strata[CLASS_LABORER].wealth=1e6f;
    re->strata[CLASS_BOURGEOIS].pop=200.f;re->strata[CLASS_BOURGEOIS].wealth=1e6f;
    re->strata[CLASS_ELITE].pop=200.f;    re->strata[CLASS_ELITE].wealth=1e6f;
    re->stock[RES_GRAIN]=1e5f; re->stock[RES_FISH]=1e5f; re->stock[RES_WOOD]=1e5f;
    re->stock[RES_CLOTH]=1e5f; re->stock[RES_PAPER]=1e5f; re->stock[RES_SALT]=1e5f; re->stock[RES_FUR]=1e5f;
    re->stock[RES_WINE]=1e5f; re->stock[RES_BEER]=1e5f;     /* boisson satisfaite quoi qu'il arrive */
    re->stock[luxe]=1e5f;                                   /* SEUL ce luxe est disponible */
    econ_tick(e, 1.f);
    return re->strata[CLASS_ELITE].satisfaction;
}

/* Recherche accumulée en un tick pour un niveau de SAVOIR bâti donné (toutes
 * choses égales par ailleurs : mêmes élites, même satisfaction). */
static float tech_with_savoir(WorldEconomy *e, int r, float savoir){
    RegionEconomy *re=&e->region[r];
    re->active=true; re->colonized=true; re->culture.settled=true; re->owner=0;
    re->culture.subsistance=8.f; re->coercion=0.f; re->over_tax=0.f;
    re->n_bld=0;
    for (int k=0;k<RES_COUNT;k++){ re->raw_cap[k]=0.f; re->stock[k]=0.f; re->price[k]=1.0f; }
    re->strata[CLASS_LABORER].pop=500.f; re->strata[CLASS_LABORER].wealth=1e6f;
    re->strata[CLASS_BOURGEOIS].pop=100.f;re->strata[CLASS_BOURGEOIS].wealth=1e6f;
    re->strata[CLASS_ELITE].pop=100.f;    re->strata[CLASS_ELITE].wealth=1e5f;  /* les élites font le savoir */
    re->stock[RES_GRAIN]=1e5f; re->stock[RES_FISH]=1e5f; re->stock[RES_WOOD]=1e5f;
    re->stock[RES_CLOTH]=1e5f; re->stock[RES_PAPER]=1e5f; re->stock[RES_SALT]=1e5f;
    re->stock[RES_FUR]=1e5f; re->stock[RES_PRECIOUS_WARE]=1e5f; re->stock[RES_PRECIOUS_CLOTH]=1e5f;
    re->stock[RES_WINE]=1e5f;
    memset(&re->build,0,sizeof re->build); re->build.savoir=savoir;
    re->tech=0.f;
    econ_tick(e, 1.f);
    return re->tech;
}

int main(int argc, char **argv){
    uint32_t seed=(argc>1)?(uint32_t)strtoul(argv[1],NULL,10):42u;
    World *w=malloc(sizeof(World));
    WorldEconomy *e=malloc(sizeof(WorldEconomy));
    WorldLegitimacy *wl=malloc(sizeof(WorldLegitimacy));
    if(!w||!e||!wl){ fprintf(stderr,"OOM\n"); return 1; }

    printf("══════════════════════════════════════════════════════════════\n");
    printf(" LE TISSU SOCIAL — brasserie · boisson culturelle · foi — graine %u\n", seed);
    printf("══════════════════════════════════════════════════════════════\n");

    WorldParams p=worldparams_default(seed);
    world_generate(w,&p); econ_init(e,w);
    if (e->n_regions<4){ fprintf(stderr,"monde trop petit\n"); return 1; }

    /* ═══ 1. BRASSERIE — grain → bière ══════════════════════════════════ */
    printf("\n── 1. La brasserie : le grain devient de la bière ──\n");
    {
        RegionEconomy *re=&e->region[0];
        re->active=true; re->colonized=true; re->culture.settled=true; re->owner=0;
        for (int k=0;k<RES_COUNT;k++){ re->raw_cap[k]=0.f; re->stock[k]=0.f; re->price[k]=1.0f; }
        re->raw_cap[RES_GRAIN]=30.f;   /* grain ABONDANT : on ne brasse que le SURPLUS */
        re->n_bld=0;
        re->bld[re->n_bld].type=BLD_BREWERY; re->bld[re->n_bld].level=3.f; re->n_bld++;
        re->strata[CLASS_LABORER].pop=400.f; re->strata[CLASS_LABORER].wealth=400.f;
        re->strata[CLASS_BOURGEOIS].pop=80.f; re->strata[CLASS_ELITE].pop=40.f;
        for (int t=0;t<6;t++) econ_tick(e,1.f);
        float beer=re->stock[RES_BEER];
        printf("   après 6 mois de brassage : bière en stock = %.1f\n", beer);
        ok("la Brasserie produit de la BIÈRE (grain → bière)", beer > 0.5f);
    }

    /* ═══ 1b. CHAÎNES MILITAIRES & SANTÉ — armurerie, poudrière, apothicaire ═ */
    printf("\n── 1b. Les chaînes complétées : armes, poudre, remèdes ──\n");
    {
        RegionEconomy *re=&e->region[3];
        re->active=true; re->colonized=true; re->culture.settled=true; re->owner=0;
        for (int k=0;k<RES_COUNT;k++){ re->raw_cap[k]=0.f; re->stock[k]=0.f; re->price[k]=1.0f; }
        re->raw_cap[RES_IRON]=4.f; re->raw_cap[RES_SALTPETER]=4.f; re->raw_cap[RES_COAL]=4.f;
        re->raw_cap[RES_MED_HERBS]=4.f;
        re->n_bld=0;
        re->bld[re->n_bld].type=BLD_ARMORY;     re->bld[re->n_bld].level=3.f; re->n_bld++;
        re->bld[re->n_bld].type=BLD_POWDERMILL; re->bld[re->n_bld].level=3.f; re->n_bld++;
        re->bld[re->n_bld].type=BLD_APOTHECARY; re->bld[re->n_bld].level=3.f; re->n_bld++;
        re->strata[CLASS_LABORER].pop=600.f; re->strata[CLASS_LABORER].wealth=400.f;
        re->strata[CLASS_BOURGEOIS].pop=100.f; re->strata[CLASS_ELITE].pop=50.f;
        for (int t=0;t<6;t++) econ_tick(e,1.f);
        printf("   après 6 mois : armes=%.1f · poudre=%.1f · remèdes=%.1f\n",
               re->stock[RES_ARMS], re->stock[RES_GUNPOWDER], re->stock[RES_REMEDE]);
        ok("l'Armurerie produit des ARMES (fer → armes)",            re->stock[RES_ARMS]>0.5f);
        ok("la Poudrière produit de la POUDRE (salpêtre+charbon)",   re->stock[RES_GUNPOWDER]>0.5f);
        ok("l'Apothicaire produit des REMÈDES (simples → remèdes)",  re->stock[RES_REMEDE]>0.5f);
    }

    /* ═══ 2. VARIANTE CULTURELLE — la bonne boisson contente ════════════ */
    printf("\n── 2. La variante culturelle : chacun sa boisson ──\n");
    float clan_beer = society_with_drink(e, 1, 2.0f, RES_BEER);   /* basse subsistance → bière */
    float clan_wine = society_with_drink(e, 1, 2.0f, RES_WINE);   /* … servi en vin (off-culture) */
    float city_wine = society_with_drink(e, 2, 8.0f, RES_WINE);   /* haute subsistance → vin */
    float city_beer = society_with_drink(e, 2, 8.0f, RES_BEER);   /* … servi en bière (off-culture) */
    printf("   clan (bière=%.2f vs vin=%.2f) · cité (vin=%.2f vs bière=%.2f)\n",
           clan_beer, clan_wine, city_wine, city_beer);
    ok("un peuple de clans est PLUS content avec sa bière qu'avec du vin", clan_beer > clan_wine + 0.02f);
    ok("un peuple des cités est PLUS content avec son vin qu'avec de la bière", city_wine > city_beer + 0.02f);

    /* ── 2b. STATUT (luxe) : orfèvrerie martiale vs étoffe raffinée ── */
    printf("\n── 2b. Le luxe d'élite : à chaque culture son statut ──\n");
    float clan_ware  = elite_sat_with_luxe(e, 1, 2.0f, RES_PRECIOUS_WARE);   /* martial → orfèvrerie */
    float clan_cloth = elite_sat_with_luxe(e, 1, 2.0f, RES_PRECIOUS_CLOTH);  /* … servi en étoffe (off) */
    float city_cloth = elite_sat_with_luxe(e, 2, 8.0f, RES_PRECIOUS_CLOTH);  /* raffiné → étoffe */
    float city_ware  = elite_sat_with_luxe(e, 2, 8.0f, RES_PRECIOUS_WARE);   /* … servi en orfèvrerie (off) */
    printf("   élite martiale (orfèvrerie=%.2f vs étoffe=%.2f) · raffinée (étoffe=%.2f vs orfèvrerie=%.2f)\n",
           clan_ware, clan_cloth, city_cloth, city_ware);
    ok("une élite martiale préfère l'ORFÈVRERIE à l'étoffe", clan_ware > clan_cloth + 0.02f);
    ok("une élite raffinée préfère l'ÉTOFFE à l'orfèvrerie", city_cloth > city_ware + 0.02f);

    /* ═══ 3. FOI — un Temple soutient la légitimité ═════════════════════ */
    printf("\n── 3. La foi : un Temple soutient le consentement ──\n");
    {
        /* Deux régions JUMELLES (même culture, propriétaire, satisfaction) ;
         * SEULE différence : l'une a la foi bâtie. */
        w->country[0].capital_prov = w->region[0].province_ids[0];
        w->country[0].role = POLITY_ANTAGONIST;
        for (int r=0;r<e->n_regions;r++){ e->region[r].owner=-1; }
        int RF=0, RN=3;   /* RF = avec foi, RN = sans */
        for (int two=0; two<2; two++){
            int r=(two==0)?RF:RN;
            RegionEconomy *re=&e->region[r];
            re->active=true; re->colonized=true; re->culture.settled=true; re->owner=0;
            re->culture.valeurs=5; re->culture.subsistance=5; re->culture.parente=5; re->culture.religion=5;
            re->satisfaction=0.5f; re->coercion=0.f;
            memset(&re->build,0,sizeof re->build);
        }
        e->region[RF].build.faith = 3.0f;          /* Temple bâti ici */
        memset(wl,0,sizeof *wl);
        legitimacy_init(wl, w, e);
        for (int t=0;t<60;t++) legitimacy_tick(wl, w, e, NULL);
        float Lf = wl->L[RF], Ln = wl->L[RN];
        printf("   légitimité : avec Temple = %.2f  vs  sans = %.2f\n", Lf, Ln);
        ok("un Temple bâti SOUTIENT la légitimité locale (L plus haute)", Lf > Ln + 0.5f);
    }

    /* ═══ 4. SAVOIR — la Bibliothèque accélère la recherche ═════════════ */
    printf("\n── 4. Le savoir : une Bibliothèque accélère la recherche ──\n");
    {
        float t_lib = tech_with_savoir(e, 1, 2.0f);   /* bibliothèque/monastère bâtis */
        float t_non = tech_with_savoir(e, 2, 0.0f);   /* sans savoir bâti */
        printf("   recherche en un tick : avec savoir bâti = %.3f  vs  sans = %.3f\n", t_lib, t_non);
        ok("une Bibliothèque ACCÉLÈRE la recherche (savoir bâti → +tech)", t_lib > t_non + 1e-4f);
    }

    printf("\n══════════════════════════════════════════════════════════════\n");
    printf(" BILAN : %d réussis, %d échoués\n", g_pass, g_fail);
    printf("══════════════════════════════════════════════════════════════\n");
    free(w); free(e); free(wl);
    return g_fail?1:0;
}
