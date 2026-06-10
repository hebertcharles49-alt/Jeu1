/*
 * scps_revolt.c — la révolte incarnée : qui se soulève, combien, ce qu'il advient
 *
 * Le grief n'est plus un flottant de région : il est ANCRÉ sur un groupe. On
 * lit son déficit, on en mobilise une fraction (qui quitte le travail), on lui
 * donne une force réelle, et on tranche contre la garnison. Une sécession FAIT
 * NAÎTRE un pays ; un coup change la couronne ; une jacquerie arrache une
 * concession ; l'échec se paie en morts et en raideur.
 */
#include "scps_revolt.h"
#include "scps_species.h"   /* species_name */
#include "scps_culture.h"   /* ethos_name (via culture nom) */
#include "scps_factions.h"  /* §5 : la tension de coup d'une faction forte aliénée */
#include "scps_labor.h"     /* capitale_* : la capacité de service (logement/services) de la région */
#include <string.h>
#include <math.h>
#include <stdio.h>

/* ---- Déficit (poids qui somment à ~1 → résultat naturellement [0..1]) -- *
 * Le conquis fraîchement soumis (non-intégré, sous une couronne étrangère) est
 * le moteur classique de la sécession : aliénation + non-intégration pèsent lourd. */
#define W_BASKET    0.32f   /* faim + manque social (la faim domine, ci-dessous) */
#define W_TAX       0.10f   /* sur-taxe ressentie */
#define W_ALIEN     0.24f   /* aliénation : distance culturelle à la couronne */
#define W_REPRESS   0.06f   /* la botte (coercition/H) attise autant qu'elle masque */
#define W_UNINTEG   0.28f   /* le conquis/diaspora mal intégré */
#define ALIEN_NORM  6.0f    /* distance de contenu de saturation (≥6 = étranger total) */
/* ---- Mobilisation : fraction qui prend les armes ---------------------- */
#define MOBIL_K     0.62f
#define MOBIL_FLOOR 0.15f   /* sous ce déficit, on grogne mais on ne se lève pas */
#define MOBIL_CAP   0.40f   /* jamais plus de 40 % d'un groupe au combat */
/* ---- Allumage --------------------------------------------------------- */
#define IGNITE_DEFICIT 0.20f
#define MIN_REBELS     20L
/* ---- Scan : la misère SOUTENUE finit par lever une région ------------- */
#define SCAN_DEFICIT   0.48f   /* au-delà : CRISE aiguë (pas la pauvreté chronique douce) */
#define SCAN_SUSTAIN   120     /* jours de désespérance avant le soulèvement */

/* SUREXTENSION : au-delà d'un seuil de régions, un empire tient mal ses marches —
 * chaque région excédentaire pousse le déficit séparatiste. Les conquêtes mal
 * digérées finissent par se détacher → de NOUVEAUX pays émergent de la démesure.
 * (Remplace l'« invariant 0 absorbé » statique par une carte politique vivante.) */
#define OVEREXT_FREE    6       /* régions « gratuites » : un empire compact tient bien */
#define OVEREXT_PER_REG 0.035f  /* déficit ajouté PAR région au-delà du seuil */
#define OVEREXT_CAP     0.45f   /* plafond du grief de surextension */

/* CAPITALE sous-équipée : poids du grief de mal-logement/mal-service dans le déficit
 * (surface d'équilibrage). Ne mord que les régions surpeuplées vs leur capacité bâtie. */
#define K_CAP_UNREST    0.30f

/* §C3 — la concession CREUSE l'institution, SANS rebond (cumulatif). */
#define C3_K_HOLLOW     0.20f   /* K_inst rongé par concession (l'ossature descend, reste basse) */
#define C3_L_HOLLOW     0.30f   /* légitimité régionale qui ploie d'un cran par concession */
/* ---- Revanchisme : subir la conquête arme le séparatisme --------------- */
#define REVANCHISM_DAYS  (10*365)  /* la blessure de la conquête (≈10 ans) */
#define REVANCHISM_MOBIL  1.45f    /* la rage gonfle les rangs rebelles */
#define REVANCHISM_REBEL  1.45f    /* … et leur ardeur au combat */
#define REVANCHISM_GARR   0.55f    /* une province hostile se garnisonne MAL */
/* ---- Classification --------------------------------------------------- */
#define SECEDE_D      2.6f   /* au-delà : nation étrangère sous la couronne */
#define SECEDE_INTEG  0.55f  /* et mal intégrée → elle veut partir */
/* ---- Résolution ------------------------------------------------------- */
#define REBEL_DECIDE_DAYS 90   /* le soulèvement se décide en ~un trimestre */
#define ZEAL_CLASS    1.0f
#define ZEAL_SECEDE   1.25f
#define ZEAL_COUP     3.0f     /* peu nombreux mais au cœur du pouvoir */
#define GARR_LOYAL    0.16f    /* part de la pop LOYALE (pondérée intégration) levable */
#define GARR_H        220.f    /* chaque point de coercition bâtie = une garnison */
#define REINFORCE     8.f      /* renforts de la couronne par point de mil_power (l'empire
                                * n'est pas partout : une province lointaine se défend seule) */
#define REINFORCE_CAP 600.f    /* la couronne ne peut projeter qu'une part de son armée ici */
#define REVOLT_COOLDOWN 1095.f /* après TOUT soulèvement (maté ou apaisé), la province se tait ~3 ans */

/* §5 — COUP D'ÉTHOS : grief POLITIQUE d'un groupe ÉTABLI (intégré à la polité, pas
 * une conquête fraîche qui, elle, SÉCÈDE) dont l'éthos appartient à une faction forte
 * et ALIÉNÉE — opposée à la direction effective. Ses membres veulent SAISIR l'État
 * pour imposer leur éthos. L'éthos d'un groupe survit à l'assimilation (signature de
 * race + trait d'éthos), donc une minorité enracinée reste porteuse de SA faction. */
#define COUP_ETHOS_W       1.0f   /* une faction fortement aliénée peut soulever seule (motif politique) */
#define COUP_ETHOS_TRIGGER 0.18f  /* §C2 : seuil RELEVÉ 0.12→0.18 — le coup exige un grief plus net
                                   * (le 0.12 faisait tomber le couperet trop tôt → 0-ou-92). */
/* §C2 — COOLDOWN au niveau PAYS (distinct du REVOLT_COOLDOWN de PROVINCE) : après
 * qu'un coup a changé la couronne, le pays ENTIER observe un répit (le nouveau régime
 * se consolide) avant qu'un autre coup ne puisse partir — même si l'affamement persiste.
 * Casse la BOUCLE de fréquence (un empire qui re-coupait via une autre province). */
#define COUP_GRACE_DAYS 1825.f    /* ~5 ans de répit post-coup, par pays */
static float g_coup_grace[SCPS_MAX_COUNTRY];   /* jours de répit restants, par PAYS */
static float ethos_coup_boost(const PopGroup *g, EthosFaction alien_fac, float coup_tension){
    if (coup_tension<=0.f || g->diaspora || g->integration < SECEDE_INTEG) return 0.f;  /* établi, pas sécessionniste */
    float lean[FAC_COUNT]; group_ethos_lean(&g->culture, lean);
    int gf=0; for (int f=1; f<FAC_COUNT; f++) if (lean[f]>lean[gf]) gf=f;  /* la faction de ce groupe */
    return (gf==(int)alien_fac) ? COUP_ETHOS_W*coup_tension : 0.f;
}
#define CRUSH_KILL    0.55f    /* part des mobilisés tués si écrasés */

static inline float clampf(float v,float lo,float hi){ return v<lo?lo:(v>hi?hi:v); }
static inline float absf(float v){ return v<0?-v:v; }

/* Distance de CONTENU (valeurs/subsistance/parenté/religion, langue exclue) —
 * la même friction culturelle que la démographie. */
static float content_dist(const PopCulture *a, const PopCulture *b){
    if (!a||!b) return 0.f;
    float dv=absf(a->valeurs-b->valeurs), ds=absf(a->subsistance-b->subsistance);
    float dp=absf(a->parente-b->parente), dr=absf(a->religion-b->religion);
    float m=dv; if(ds>m)m=ds; if(dp>m)m=dp; if(dr>m)m=dr; return m;
}
/* Culture régnante d'un pays = celle de sa région-capitale. */
static const PopCulture *crown_of(const World *w, const WorldEconomy *econ, int cid){
    if (cid<0||cid>=w->n_countries) return NULL;
    int cp=w->country[cid].capital_prov; if(cp<0||cp>=w->n_provinces) return NULL;
    int cr=w->province[cp].region; if(cr<0||cr>=econ->n_regions) return NULL;
    return &econ->region[cr].culture;
}
static int find_group(const ProvincePop *pp, int drift_id){
    for (int i=0;i<pp->n_groups;i++) if (pp->groups[i].drift_id==drift_id) return i;
    return -1;
}

void revolt_init(RevoltState *rs){ memset(rs,0,sizeof *rs); rs->last_spawned=-1;
    memset(g_coup_grace,0,sizeof g_coup_grace);   /* §C2 : répit pays remis à zéro par sim */
}

void revolt_on_conquest(RevoltState *rs, int region){
    if (region>=0 && region<SCPS_MAX_REG) rs->revanchism_days[region]=(float)REVANCHISM_DAYS;
}
/* Le séparatisme post-conquête FOND avec sa fenêtre : facteur [0..1] = plein à
 * la conquête fraîche, décroissant à zéro sur ~10 ans (la durée est CÂBLÉE sur
 * l'effet : la rage de l'indépendance s'éteint à mesure que la plaie se referme). */
static inline float revanchism_factor(const RevoltState *rs, int region){
    if (region<0 || region>=SCPS_MAX_REG) return 0.f;
    return clampf(rs->revanchism_days[region] / (float)REVANCHISM_DAYS, 0.f, 1.f);
}

/* ===================================================================== */
/* LES TROIS LECTEURS PURS (déficit, mobilisation, nature)                */
/* ===================================================================== */
float revolt_group_deficit(const PopGroup *g, const ModifierStack *drift,
                           const PopCulture *crown, float food_sat, float society_sat,
                           float tax_pressure, float coercion){
    /* panier : la faim pèse double sur le manque social (un peuple affamé se lève) */
    float basket = clampf(0.70f*(1.f-food_sat) + 0.30f*(1.f-society_sat), 0.f, 1.f);
    PopCulture eff = group_culture_effective(g, drift);
    float alien = clampf(content_dist(&eff, crown)/ALIEN_NORM, 0.f, 1.f);
    float tax   = clampf(tax_pressure, 0.f, 1.f);
    float repr  = clampf(coercion, 0.f, 1.f);
    float unint = clampf(1.f - g->integration, 0.f, 1.f);
    float d = W_BASKET*basket + W_TAX*tax + W_ALIEN*alien
            + W_REPRESS*repr + W_UNINTEG*unint;
    return clampf(d, 0.f, 1.f);
}

long revolt_mobilized(const PopGroup *g, float deficit){
    float frac = clampf(MOBIL_K*deficit - MOBIL_FLOOR, 0.f, MOBIL_CAP);
    long m = (long)((float)g->count * frac);
    return m;
}

RebelKind revolt_classify(const PopGroup *g, const ModifierStack *drift, const PopCulture *crown){
    PopCulture eff = group_culture_effective(g, drift);
    float alien = content_dist(&eff, crown);
    bool unintegrated = (g->integration < SECEDE_INTEG);
    /* SÉCESSION d'abord : une nation conquise sur SA terre (non-diaspora) mal
     * intégrée veut l'INDÉPENDANCE — la conquête EST le grief, quelle que soit la
     * distance culturelle ; une minorité profondément étrangère le veut aussi. */
    if (!g->diaspora && unintegrated)              return REBEL_SECESSION;
    if (alien >= SECEDE_D && unintegrated)         return REBEL_SECESSION;
    /* COUP : seule l'élite ÉTABLIE (native, intégrée au royaume) vise le trône —
     * pas une élite étrangère/diaspora sous occupation (elle, c'est CLASS/SÉCESSION). */
    if (g->klass==CLASS_ELITE && !g->diaspora)     return REBEL_COUP;
    return REBEL_CLASS;                            /* sinon : on réclame, on ne part pas */
}

/* ===================================================================== */
/* ALLUMAGE — incarne le soulèvement sur le pire groupe                    */
/* ===================================================================== */
int revolt_ignite(RevoltState *rs, World *w, WorldEconomy *econ,
                  const ModifierStack *drift, int region, float tax_pressure){
    if (region<0||region>=econ->n_regions) return -1;
    RegionEconomy *re=&econ->region[region];
    int owner=re->owner;
    if (owner<0) return -1;
    ProvincePop *pp=&re->pop;
    if (pp->n_groups<=0) return -1;
    /* un seul soulèvement vif par région (la colère couve déjà ici) */
    for (int i=0;i<rs->count;i++) if (rs->list[i].active && rs->list[i].region==region) return -1;
    const PopCulture *crown = crown_of(w,econ,owner);

    /* §5 : la tension de coup du pays — une faction forte aliénée porte son élite. */
    float ct=0.f; EthosFaction cf=FAC_COMMUNAUTAIRE;
    ct = faction_coup_tension_c(w,econ,owner,&cf);   /* tension de coup AVEC le grief des leviers (§4) */

    /* le groupe au plus fort déficit porte le soulèvement (grief politique compris) */
    int worst=-1; float wd=0.f;
    for (int i=0;i<pp->n_groups;i++){
        float d=revolt_group_deficit(&pp->groups[i], drift, crown,
                                     re->food_sat, re->society_sat, tax_pressure, re->coercion)
              + ethos_coup_boost(&pp->groups[i], cf, ct);
        if (d>1.f) d=1.f;
        if (d>wd){ wd=d; worst=i; }
    }
    if (worst<0 || wd<IGNITE_DEFICIT) return -1;
    PopGroup *g=&pp->groups[worst];
    /* §C2 : ce soulèvement serait-il un COUP ? Si oui ET le pays est en RÉPIT post-coup,
     * on l'étouffe (le régime fraîchement installé n'est pas renversé l'année d'après). */
    bool would_coup = (ethos_coup_boost(g, cf, ct) >= COUP_ETHOS_TRIGGER);
    if (would_coup && owner>=0 && owner<SCPS_MAX_COUNTRY && g_coup_grace[owner]>0.f) return -1;
    long mob=revolt_mobilized(g, wd);
    { float rf=revanchism_factor(rs,region);   /* la rage grossit les rangs, ∝ fraîcheur de la conquête */
      if (rf>0.f) mob=(long)((float)mob*(1.f + (REVANCHISM_MOBIL-1.f)*rf)); }
    if (mob<MIN_REBELS) return -1;
    if (mob>g->count) mob=g->count;

    /* slot */
    int slot=-1;
    for (int i=0;i<rs->count;i++) if (!rs->list[i].active){ slot=i; break; }
    if (slot<0){ if (rs->count>=REVOLT_MAX) return -1; slot=rs->count++; }
    Rebellion *rb=&rs->list[slot];
    memset(rb,0,sizeof *rb);
    rb->active=true; rb->region=region; rb->owner=owner;
    /* §5 : si le grief POLITIQUE (faction forte aliénée) domine, c'est un COUP — la
     * faction saisit l'État pour imposer son éthos. Sinon, la nature usuelle (sécession
     * d'une nation conquise, jacquerie de classe). */
    rb->kind = would_coup ? REBEL_COUP : revolt_classify(g, drift, crown);
    rb->race=g->race; rb->klass=g->klass;
    rb->culture=group_culture_effective(g, drift);
    rb->drift_id=g->drift_id; rb->mobilized=mob; rb->deficit=wd;
    rb->outcome=OUT_ONGOING; rb->spawned=-1;

    /* CHOC ÉCONOMIQUE : les mobilisés QUITTENT la main-d'œuvre. La province perd
     * ses bras (l'atelier tourne au ralenti) — la révolte a un coût immédiat. */
    g->count -= mob;
    float take=(float)mob;
    if (re->strata[CLASS_LABORER].pop>=take) re->strata[CLASS_LABORER].pop-=take;
    else { take-=re->strata[CLASS_LABORER].pop; re->strata[CLASS_LABORER].pop=0.f;
           re->strata[CLASS_BOURGEOIS].pop=fmaxf(0.f, re->strata[CLASS_BOURGEOIS].pop-take); }

    rs->n_ignited++;
    return slot;
}

/* ===================================================================== */
/* SCAN — la misère soutenue d'une région finit par la soulever            */
/* ===================================================================== */
void revolt_scan(RevoltState *rs, World *w, WorldEconomy *econ,
                 const ModifierStack *drift, int days){
    /* §5 : tension de coup PAR PAYS (faction forte aliénée) — calculée à la demande,
     * mise en cache (un pays a la même tension dans toutes ses régions ce tick). */
    float ctens[SCPS_MAX_COUNTRY]; EthosFaction cfac[SCPS_MAX_COUNTRY];
    char  cdone[SCPS_MAX_COUNTRY]; memset(cdone,0,sizeof cdone);
    /* §C2 : le répit post-coup s'écoule (par pays). */
    for (int c=0;c<SCPS_MAX_COUNTRY;c++) if (g_coup_grace[c]>0.f) g_coup_grace[c]-=(float)days;
    /* SUREXTENSION : on compte les régions par pays UNE fois (cache O(n)). */
    int owned[SCPS_MAX_COUNTRY]; memset(owned,0,sizeof owned);
    for (int r=0;r<econ->n_regions;r++){ int o=econ->region[r].owner;
        if (o>=0 && o<SCPS_MAX_COUNTRY) owned[o]++; }
    for (int r=0;r<econ->n_regions && r<SCPS_MAX_REG;r++){
        RegionEconomy *re=&econ->region[r];
        if (rs->revanchism_days[r]>0.f) rs->revanchism_days[r]=fmaxf(0.f, rs->revanchism_days[r]-(float)days);
        if (re->owner<0 || !re->culture.settled || re->pop.n_groups<=0){
            rs->desperation_days[r]=0.f; continue;
        }
        const PopCulture *crown=crown_of(w,econ,re->owner);
        int o=re->owner; float ct=0.f; EthosFaction cf=FAC_COMMUNAUTAIRE;
        if (o>=0 && o<SCPS_MAX_COUNTRY){
            if (!cdone[o]){ ctens[o]=faction_coup_tension_c(w,econ,o,&cfac[o]); cdone[o]=1; }
            ct=ctens[o]; cf=cfac[o];
        }
        float worst=0.f, min_integ=1.f;
        for (int i=0;i<re->pop.n_groups;i++){
            float d=revolt_group_deficit(&re->pop.groups[i], drift, crown,
                                         re->food_sat, re->society_sat, re->over_tax, re->coercion)
                  + ethos_coup_boost(&re->pop.groups[i], cf, ct);   /* §5 : grief politique */
            if (d>1.f) d=1.f;
            if (d>worst) worst=d;
            if (re->pop.groups[i].integration < min_integ) min_integ = re->pop.groups[i].integration;
        }
        /* SUREXTENSION : un empire trop vaste tient mal ses MARCHES — le grief monte
         * avec la taille, mais surtout là où la province est MAL INTÉGRÉE (conquête
         * étrangère) → SÉCESSION (un pays naît), pas coup du cœur natif. Le cœur ne
         * subit qu'un tiers du grief (un empire homogène fragmente moins). */
        if (o>=0 && o<SCPS_MAX_COUNTRY && owned[o]>OVEREXT_FREE){
            float overext = clampf((float)(owned[o]-OVEREXT_FREE)*OVEREXT_PER_REG, 0.f, OVEREXT_CAP);
            overext *= (0.30f + 0.70f*(1.f - clampf(min_integ,0.f,1.f)));   /* biais marches étrangères */
            worst = clampf(worst + overext, 0.f, 1.f);
        }
        /* CAPITALE SOUS-ÉQUIPÉE : la pop qui dépasse la capacité de SERVICE de la
         * région (capitale tier·1000 + édifices civiques) gronde — mal-logés/mal-servis.
         * Une région qui croît SANS bâtir ses institutions devient agitée. (C3 rétrécira
         * la part capitale + K par (1−rot) : une élite capturée délivre moins.) */
        {
            long rpop = (long)(re->strata[CLASS_LABORER].pop + re->strata[CLASS_BOURGEOIS].pop
                             + re->strata[CLASS_ELITE].pop);
            if (rpop>0){
                int  ctier = capitale_max_tier(rpop);
                long nob   = capitale_admin_pop(ctier); if (nob>rpop) nob=rpop;
                float rot  = (o>=0 && o<SCPS_MAX_COUNTRY)? faction_capture_total(o) : 0.f;  /* §C3 */
                float serv = ((float)capitale_housing(ctier, nob)                       /* la capitale */
                            + (re->build.K_inst + re->build.savoir + re->build.faith)*700.f) /* les autres bâtiments */
                           * (1.f - rot);          /* §C3 : une élite CAPTURÉE délivre moins de service → plus d'agitation */
                float unserved = (float)rpop - serv;
                if (unserved>0.f) worst = clampf(worst + (unserved/(float)rpop)*K_CAP_UNREST, 0.f, 1.f);
            }
        }
        /* le séparatisme post-conquête désespère la province « quoi qu'il arrive » */
        if (worst>=SCAN_DEFICIT || revanchism_factor(rs,r)>0.f) rs->desperation_days[r] += (float)days;
        else rs->desperation_days[r] = fmaxf(0.f, rs->desperation_days[r]-(float)days);
        if (rs->desperation_days[r] >= (float)SCAN_SUSTAIN){
            if (revolt_ignite(rs, w, econ, drift, r, re->over_tax)>=0)
                rs->desperation_days[r]=0.f;       /* la colère s'est faite acte */
        }
    }
}

/* ===================================================================== */
/* RÉSOLUTION — la garnison contre les rebelles, puis le verdict           */
/* ===================================================================== */
/* Rend les survivants au travail (après échec/concession/coup). */
static void demobilize(WorldEconomy *econ, Rebellion *rb, long survivors){
    if (survivors<=0) return;
    RegionEconomy *re=&econ->region[rb->region];
    int gi=find_group(&re->pop, rb->drift_id);
    if (gi>=0) re->pop.groups[gi].count += survivors;
    re->strata[CLASS_LABORER].pop += (float)survivors;
}

/* Un emplacement de pays RÉUTILISABLE : un placeholder vierge (UNCLAIMED) qui ne
 * tient aucune région — la sécession s'y installe quand la table est pleine
 * (le worldgen la remplit souvent jusqu'au plafond). */
static int free_country_slot(const World *w, const WorldEconomy *econ, int avoid){
    for (int c=0;c<w->n_countries;c++){
        if (c==avoid || w->country[c].role!=POLITY_UNCLAIMED) continue;
        int held=0; for (int r=0;r<econ->n_regions;r++) if (econ->region[r].owner==c){ held=1; break; }
        if (!held) return c;
    }
    return -1;
}
/* SÉCESSION : un pays naît, prend la région, s'installe sur le groupe rebelle. */
static int spawn_secession(World *w, WorldEconomy *econ, WorldLegitimacy *wl, Rebellion *rb){
    /* GARDE AMONT : sans région valide, le spawn n'a aucun sens — et la suite ÉCRIT
     * dans econ->region[rb->region] (hors-bornes si négatif). On refuse net. */
    if (rb->region<0 || rb->region>=w->n_regions || rb->region>=econ->n_regions) return -1;
    int nid = free_country_slot(w, econ, rb->owner);      /* d'abord un slot vierge */
    if (nid<0){                                            /* sinon on agrandit la table */
        if (w->n_countries>=SCPS_MAX_COUNTRY) return -1;
        nid=w->n_countries++;
    }
    Country *nc=&w->country[nid]; memset(nc,0,sizeof *nc);
    nc->role=POLITY_ANTAGONIST;                 /* un nouvel empire libre */
    bool rb_reg_ok = (rb->region>=0 && rb->region<w->n_regions);   /* garde : index région valide (≥0 ET < n) */
    nc->continent=rb_reg_ok ? w->region[rb->region].continent : 0;
    nc->capital_prov=(rb_reg_ok && w->region[rb->region].n_provinces>0)
                     ? w->region[rb->region].province_ids[0] : -1;
    nc->n_regions=1; nc->region_ids[0]=(int16_t)rb->region;
    nc->color=0xC08040u;
    snprintf(nc->name,sizeof nc->name,"%s libre", species_name(rb->race));

    RegionEconomy *re=&econ->region[rb->region];
    re->owner=(int16_t)nid;
    if (rb->region<w->n_regions) w->region[rb->region].country=(int16_t)nid;
    /* la nation libérée se relégitime ; les colons de l'ancienne couronne fondent */
    if (rb->region<SCPS_MAX_REG){ wl->L[rb->region]=7.0f; wl->years_held[rb->region]=0.f; }
    re->coercion=0.f;
    int gi=find_group(&re->pop, rb->drift_id);
    if (gi>=0){ re->pop.groups[gi].L=7.f; re->pop.groups[gi].integration=1.f;
                re->pop.groups[gi].agit_base=0.f; re->pop.groups[gi].diaspora=false; }
    return nid;
}

void revolt_tick(RevoltState *rs, World *w, WorldEconomy *econ, ModifierStack *drift,
                 WorldLegitimacy *wl, const WorldProsperity *wp, int days){
    (void)drift;
    rs->last_spawned=-1;
    for (int i=0;i<rs->count;i++){
        Rebellion *rb=&rs->list[i];
        if (!rb->active) continue;
        rb->days += days;
        RegionEconomy *re=&econ->region[rb->region];

        /* la couronne a-t-elle changé / la région perdue ? le soulèvement tombe. */
        if (re->owner!=rb->owner){ rb->active=false; continue; }

        if (rb->days < REBEL_DECIDE_DAYS) continue;   /* la lutte couve */

        /* ── Forces en présence (en équivalents-combattants) ────────────── */
        float zeal = (rb->kind==REBEL_COUP)?ZEAL_COUP
                   : (rb->kind==REBEL_SECESSION)?ZEAL_SECEDE : ZEAL_CLASS;
        float rebel = (float)rb->mobilized * zeal;
        /* Séparatisme à durée CÂBLÉE : l'élan d'indépendance ∝ fraîcheur de la
         * conquête (plein à chaud, nul une fois la plaie refermée). */
        float rf = (rb->kind==REBEL_SECESSION) ? revanchism_factor(rs, rb->region) : 0.f;
        rebel *= (1.f + (REVANCHISM_REBEL-1.f)*rf);   /* l'indépendance galvanise */

        /* Garnison locale : seuls les groupes LOYAUX (pondérés par leur intégration)
         * se lèvent pour l'ordre — le groupe soulevé, lui, ne se garnisonne pas. Une
         * province fraîchement conquise (peuple restif, mince élite coloniale) tient
         * donc mal : c'est là que naissent les sécessions. */
        float loyal_pop = 0.f;
        for (int gi=0; gi<re->pop.n_groups; gi++){
            const PopGroup *g=&re->pop.groups[gi];
            if (g->drift_id==rb->drift_id) continue;
            loyal_pop += (float)g->count * clampf(g->integration, 0.f, 1.f);
        }
        float milp = diplo_mil_power(w, econ, rb->owner);
        float reinforce = fminf(milp*REINFORCE, REINFORCE_CAP);   /* l'armée ne tient pas TOUT le pays ici */
        float morale = (rb->owner<wp->n_countries) ? clampf(0.6f+wp->country[rb->owner].SI/12.f,0.6f,1.4f) : 1.f;
        float garrison = (loyal_pop*GARR_LOYAL + re->build.H_coerc*GARR_H + reinforce) * morale;
        garrison *= (1.f - (1.f-REVANCHISM_GARR)*rf);  /* une province fraîchement prise tient mal */

        if (garrison >= rebel){
            /* ── ÉCRASÉS : morts + le pays se raidit (coercition, L brisée) ── */
            long killed=(long)((float)rb->mobilized*CRUSH_KILL);
            long survivors=rb->mobilized-killed;
            rs->pop_lost += killed; rs->n_crushed++;
            demobilize(econ, rb, survivors);
            int gi=find_group(&re->pop, rb->drift_id);
            if (gi>=0){ re->pop.groups[gi].L=clampf(re->pop.groups[gi].L-2.f,0.f,10.f);
                        re->pop.groups[gi].agit_base=clampf(re->pop.groups[gi].agit_base+15.f,0.f,100.f); }
            re->coercion=1.f;                                   /* loi martiale durable */
            if (rb->region<SCPS_MAX_REG) wl->L[rb->region]*=0.75f;  /* régner par la peur ronge L */
            rb->outcome=OUT_CRUSHED;
        } else {
            /* ── LES REBELLES L'EMPORTENT : le verdict suit leur nature ───── */
            switch (rb->kind){
                case REBEL_SECESSION: {
                    int nid=spawn_secession(w, econ, wl, rb);
                    rb->spawned=nid; rs->last_spawned=nid;
                    if (nid>=0){ rs->n_seceded++; rb->outcome=OUT_SECEDED; }
                    else { demobilize(econ, rb, rb->mobilized); rb->outcome=OUT_CONCESSION; }
                    break; }
                case REBEL_COUP: {
                    /* l'élite prend le trône : la couronne adopte SA culture, lune
                     * de miel de légitimité ; les hommes rentrent (affaire de palais). */
                    re->culture = rb->culture;
                    int cap = (rb->owner<w->n_countries)?w->country[rb->owner].capital_prov:-1;
                    int cr  = (cap>=0&&cap<w->n_provinces)?w->province[cap].region:-1;
                    if (cr>=0&&cr<econ->n_regions) econ->region[cr].culture=rb->culture;
                    for (int r=0;r<econ->n_regions;r++) if (econ->region[r].owner==rb->owner && r<SCPS_MAX_REG)
                        wl->L[r]=clampf(wl->L[r]+1.5f,0.f,10.f);
                    re->coercion=fmaxf(0.f, re->coercion-0.3f);
                    demobilize(econ, rb, rb->mobilized);
                    faction_levers_on_coup(rb->owner);   /* §4 : le coup purge la rancœur (plus de spirale) */
                    if (rb->owner>=0 && rb->owner<SCPS_MAX_COUNTRY)
                        g_coup_grace[rb->owner]=COUP_GRACE_DAYS;   /* §C2 : répit du nouveau régime */
                    rs->n_coup++; rb->outcome=OUT_COUP;
                    break; }
                default: {  /* REBEL_CLASS : la couronne CÈDE (concession) */
                    re->satisfaction=clampf(re->satisfaction+0.20f,0.f,1.f);
                    re->coercion=fmaxf(0.f, re->coercion-0.4f);
                    int gi=find_group(&re->pop, rb->drift_id);
                    if (gi>=0){ re->pop.groups[gi].L=clampf(re->pop.groups[gi].L+2.f,0.f,10.f);
                                re->pop.groups[gi].agit_base=clampf(re->pop.groups[gi].agit_base-25.f,0.f,100.f); }
                    demobilize(econ, rb, rb->mobilized);
                    /* §C3 — la concession a un PRIX : la faction de l'extorqueur CAPTURE
                     * l'État (rot↑ → malus noble), et l'OSSATURE ploie sans rebond
                     * (K creusé + légitimité d'un cran) → l'empire concédant devient flasque. */
                    { float lean[FAC_COUNT]; group_ethos_lean(&rb->culture, lean);
                      int wf=0; for (int f=1;f<FAC_COUNT;f++) if (lean[f]>lean[wf]) wf=f;
                      faction_concede(rb->owner, (EthosFaction)wf); }
                    re->build.K_inst = fmaxf(0.f, re->build.K_inst - C3_K_HOLLOW);
                    if (rb->region<SCPS_MAX_REG)
                        wl->L[rb->region] = clampf(wl->L[rb->region]-C3_L_HOLLOW, 0.f, 10.f);
                    rs->n_concession++; rb->outcome=OUT_CONCESSION;
                    break; }
            }
        }
        /* après TOUT soulèvement résolu, la province est épuisée : elle se tait
         * quelques années (le grief doit se reconstruire) — fin des re-flambées. */
        if (rb->region<SCPS_MAX_REG && rb->outcome!=OUT_SECEDED)
            rs->desperation_days[rb->region] = -REVOLT_COOLDOWN;
        /* CICATRICE : la province convulsée se développe mal quelques années
         * (−50 % croissance & production) — la révolte laisse une plaie économique. */
        if (rb->region>=0 && rb->region<econ->n_regions)
            econ->region[rb->region].revolt_scar = 1.0f;
        /* usure : le slot se libère (la liste se compacte au prochain allumage) */
        rb->active=false;
    }
    /* compacter la queue inactive */
    while (rs->count>0 && !rs->list[rs->count-1].active) rs->count--;
}

/* ===================================================================== */
/* MEMBRANE — mots/nombres de JEU                                          */
/* ===================================================================== */
int revolt_active_count(const RevoltState *rs){
    int n=0; for (int i=0;i<rs->count;i++) if (rs->list[i].active) n++; return n;
}
const char *revolt_kind_word(RebelKind k){
    switch(k){ case REBEL_SECESSION: return "sécession";
               case REBEL_COUP:      return "coup d'État";
               case REBEL_CLASS:     return "jacquerie";
               default:              return "agitation"; }
}
const char *revolt_outcome_word(int outcome){
    switch(outcome){ case OUT_CRUSHED:    return "écrasée";
                     case OUT_SECEDED:    return "indépendance";
                     case OUT_CONCESSION: return "concession arrachée";
                     case OUT_COUP:       return "trône renversé";
                     default:             return "en cours"; }
}
const char *revolt_class_word(SocialClass k){
    switch(k){ case CLASS_ELITE: return "Noblesse"; case CLASS_BOURGEOIS: return "Artisans";
               default: return "Laboureurs"; }
}
