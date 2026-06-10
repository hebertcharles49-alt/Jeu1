/*
 * scps_diplo.c — diplomatie & guerre (voir scps_diplo.h)
 *
 * Tout est LECTEUR : la menace, la complémentarité, la parenté, le schisme se
 * lisent sur des coordonnées existantes. La conquête déplace l'owner ; la
 * diversité (et la fracture) en découle dans le moteur d'ordre.
 */
#include "scps_diplo.h"
#include "scps_species.h"
#include "scps_culture.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

static inline float clampf(float v,float lo,float hi){return v<lo?lo:(v>hi?hi:v);}
static inline float absf(float v){return v<0?-v:v;}

/* ---- Diplomatie d'équilibre — surface d'équilibrage ------------------- */
#define TRUCE_BASE       (3.f*365.f)   /* trêve de base après une paix (3 ans) */
#define TRUCE_PER_YEAR   (365.f)       /* + 1 an de trêve par an de guerre menée */
#define TRUCE_MAX        (12.f*365.f)  /* plafond (une vie de génération) */
#define MOMENTUM_PER_CONQ 1.0f         /* +1 fulgurance par région prise */
#define MOMENTUM_DECAY   (1.2f/365.f)  /* la fulgurance s'oublie (~ -1.2/an) */
#define MOMENTUM_W       0.7f          /* poids de la fulgurance sur la menace */
/* §D2 — l'alliance lit la menace commune EN RAPPORT au monde du moment (comme la
 * détection d'hégémon lit un ratio, pas une magnitude). Sinon la menace gonfle sans
 * borne (~1 au début, ~100+ en fin) et le seuil fixe finit par être franchi par
 * presque toute paire → la carte se pétrifie en blocs. */
#define THREAT_FLOOR     0.5f          /* plancher d'ambiance : pas de spike en paix de début */
#define K_SHARED         4.0f          /* poids du rapport de menace dans le score d'alliance */
/* Hégémon = menace qui ÉCRASE le champ (domine NETTEMENT la 2e) — critère RELATIF,
 * indépendant de l'échelle (les menaces vont de ~1 au début à ~500 en fin de partie). */
#define HEGEMON_RATIO    1.8f
#define HEGEMON_FLOOR    0.5f
/* ---- Score de guerre (§2) -------------------------------------------- */
#define WAR_BATTLE_W     14.f   /* vitesse du battle_score vers ±50 (par an, à avantage net) */
#define WAR_BATTLE_CAP   50.f   /* les batailles SEULES ne gagnent pas la guerre (la moitié) */
#define WAR_OCCUPY_PER   12.f   /* points d'occupation par région prise (l'autre moitié) */
#define WAR_ATTRITION    0.18f  /* part d'armes perdue/an (saigne les deux ; le perdant ×plus) */
#define WAR_ATTR_LOSER   1.6f
#define WAR_ATTR_WINNER  0.6f
/* ---- Paix proportionnelle (§5) --------------------------------------- */
#define CLAIM_DOM         18.f  /* provinces légitimes de plus par cran de domination militaire */
#define CLAIM_ILLEGIT_MOM 2.0f  /* surcroît de fulgurance par prise ILLÉGITIME (→ coalition) */
#define REP_MIN_SCORE     20.f  /* en-deçà de ce score : match nul → aucune indemnité */
#define REP_RATE          0.5f  /* part max du trésor du perdant exigée (à 100 de score) */
/* ---- Esclavage (§4c) -------------------------------------------------- */
#define SLAVE_FRACTION    0.25f         /* part de la population prise déportée en captivité */
#define SLAVE_DRIFT_BASE  800000        /* plage de drift_id réservée aux groupes d'esclaves */
/* ---- Rancune nationale (§6) ------------------------------------------ */
#define RANCOR_PER_LOSS   1.0f          /* grief par province PERDUE (irrédentisme) */
#define RANCOR_ILLEGIT    1.0f          /* surcroît si la prise fut ILLÉGITIME (agression nue) */
#define RANCOR_DECAY      (1.0f/(10.f*365.f))  /* s'oublie sur ~une génération (10 ans/cran) */
#define RANCOR_CB_SEUIL   0.75f         /* au-delà : casus belli territorial sans adjacence */
#define RANCOR_RALLY_W    0.6f          /* galvanisation max de la guerre de reconquête */
#define RANCOR_RALLY_NORM 3.0f          /* échelle de saturation du ralliement */

static int g_intim_cd[SCPS_MAX_COUNTRY];   /* l'intimidation n'est pas gratuite : ~5 ans entre deux démonstrations */
void diplo_save_statics(FILE *f){ fwrite(g_intim_cd,sizeof g_intim_cd,1,f); }
bool diplo_load_statics(FILE *f){ return fread(g_intim_cd,sizeof g_intim_cd,1,f)==1; }
void diplo_init(DiploState *d){
    memset(d,0,sizeof(*d));
    memset(g_intim_cd,0,sizeof g_intim_cd);
    for (int c=0;c<SCPS_MAX_COUNTRY;c++) d->suzerain[c]=-1;   /* tous libres au départ */
    d->fronde_suz=-1; d->fronde_lead=-1; d->fronde_rng=0x9E3779B9u;
}
/* La graine du MONDE entre dans la fronde : sans elle, chaque partie rejouait la
 * même séquence d'intimidations/ligues. (fronde_rng vit dans DiploState → la
 * sauvegarde la préserve ; on ne sème qu'à la création d'une partie.) */
void diplo_seed_rng(DiploState *d, uint32_t seed){
    d->fronde_rng = 0x9E3779B9u ^ seed;
    if (!d->fronde_rng) d->fronde_rng = 1u;
}

/* ═══ SUZERAINETÉ (brief leviers §3) — quatre contrats, trois voies, la rupture ═══ */
const char *diplo_contrat_name(SuzContrat c){
    static const char *N[5]={ "libre","servage","protectorat","concordat","cité marchande" };
    return (c>=0&&c<5)?N[c]:"?";
}
int diplo_suzerain(const DiploState *d, int cid){
    return (d && cid>=0 && cid<SCPS_MAX_COUNTRY) ? d->suzerain[cid] : -1;
}
SuzContrat diplo_contrat(const DiploState *d, int cid){
    return (d && cid>=0 && cid<SCPS_MAX_COUNTRY) ? (SuzContrat)d->contrat[cid] : CONTRAT_NONE;
}
int diplo_vassal_count(const DiploState *d, int cid){
    int n=0; if(!d) return 0;
    for (int v=0;v<SCPS_MAX_COUNTRY;v++) if (d->suzerain[v]==cid) n++;
    return n;
}
bool diplo_trade_pact(const DiploState *d, int a, int b){
    if (!d||a<0||b<0||a>=SCPS_MAX_COUNTRY||b>=SCPS_MAX_COUNTRY) return false;
    return (d->suzerain[a]==b && d->contrat[a]==CONTRAT_CITE)
        || (d->suzerain[b]==a && d->contrat[b]==CONTRAT_CITE);
}
void diplo_set_vassal(DiploState *d, int suz, int vas, SuzContrat c){
    if (!d||suz<0||vas<0||suz==vas||suz>=SCPS_MAX_COUNTRY||vas>=SCPS_MAX_COUNTRY) return;
    d->suzerain[vas]=(int16_t)suz; d->contrat[vas]=(int8_t)c;
    d->status[suz][vas]=DIPLO_NEUTRAL; d->status[vas][suz]=DIPLO_NEUTRAL;   /* la guerre cesse */
    d->war_years[suz][vas]=d->war_years[vas][suz]=0.f;
    d->truce[suz][vas]=d->truce[vas][suz]=365.f*5.f;                        /* une trêve s'ouvre */
    switch(c){ case CONTRAT_SERVAGE: d->n_servage++; break;
               case CONTRAT_PROTECTORAT: d->n_protectorat++; break;
               case CONTRAT_CONCORDAT: d->n_concordat++; break;
               case CONTRAT_CITE: d->n_cite++; break; default: break; }
}
void diplo_break_vassal(DiploState *d, int vas, bool to_war){
    if (!d||vas<0||vas>=SCPS_MAX_COUNTRY) return;
    int suz=d->suzerain[vas];
    d->suzerain[vas]=-1; d->contrat[vas]=CONTRAT_NONE;
    d->n_defections++;
    if (to_war && suz>=0){                       /* le serf part en guerre d'indépendance */
        d->status[vas][suz]=DIPLO_WAR; d->status[suz][vas]=DIPLO_WAR;
        d->cb[vas][suz]=CB_TERRITORIAL;
    }
}
float diplo_vassal_grief(const DiploState *d, int vassal){
    return (d && vassal>=0 && vassal<SCPS_MAX_COUNTRY && d->suzerain[vassal]>=0)
         ? d->v_grief[vassal] : 0.f;
}
/* puissance — LA MÊME échelle que la menace : éco + mil (pas de seconde échelle). */
static float suz_power(const World *w, const WorldEconomy *econ, const WorldProsperity *wp, int c){
    return diplo_eco_power(wp,c) + diplo_mil_power(w,econ,c);
}
static uint32_t fr_rng(DiploState *d){ uint32_t x=d->fronde_rng; x^=x<<13; x^=x>>17; x^=x<<5; return d->fronde_rng=x; }
static Ethos suz_ethos(const World *w, const WorldEconomy *econ, int c){
    int cp=(c>=0&&c<w->n_countries)?w->country[c].capital_prov:-1;
    int cr=(cp>=0&&cp<w->n_provinces)?w->province[cp].region:-1;
    return (cr>=0&&cr<econ->n_regions)?econ->region[cr].culture.ethos:ETHOS_ORDRE;
}
static Credo suz_credo(const World *w, const WorldEconomy *econ, int c){
    int cp=(c>=0&&c<w->n_countries)?w->country[c].capital_prov:-1;
    int cr=(cp>=0&&cp<w->n_provinces)?w->province[cp].region:-1;
    return (cr>=0&&cr<econ->n_regions)?econ->region[cr].culture.credo:(Credo)0;
}
/* un cran PLUS DOUX (le renversement ne reproduit pas pire — pas tout de suite). */
static SuzContrat contrat_adouci(SuzContrat c){
    return (c==CONTRAT_SERVAGE)?CONTRAT_PROTECTORAT:(c==CONTRAT_PROTECTORAT)?CONTRAT_CONCORDAT:c;
}
#define FRONDE_RATIO     1.2f
#define FRONDE_GRIEF_MIN 0.45f

void diplo_suzerainty_tick(DiploState *d, const World *w, WorldEconomy *econ,
                           const WorldProsperity *wp){
    if (!d||!w||!econ) return;
    int capreg[SCPS_MAX_COUNTRY];                /* où coule le tribut */
    for (int c=0;c<w->n_countries && c<SCPS_MAX_COUNTRY;c++){
        int cp=w->country[c].capital_prov;
        capreg[c]=(cp>=0&&cp<w->n_provinces)?w->province[cp].region:-1;
    }
    /* ── 0. RÉSOLUTION d\'une fronde en cours : la paix entre meneur et maître tranche. */
    if (d->fronde_suz>=0){
        int s0=d->fronde_suz, ld=d->fronde_lead;
        if (ld>=0 && d->status[ld][s0]==DIPLO_WAR){
            /* LA LIGUE PÈSE : le bras-de-fer par paire ignore les co-ligués — on y verse
             * la PRESSION CUMULÉE (Σ puissance des membres vs le maître), chaque année. */
            float pl=0.f;
            for (int v=0;v<SCPS_MAX_COUNTRY;v++) if (d->v_ligue[v]) pl+=suz_power(w,econ,wp,v);
            float ps=suz_power(w,econ,wp,s0);
            d->battle_score[ld][s0] = clampf(d->battle_score[ld][s0]
                                             + clampf((pl/(ps+1.f)-1.f)*8.f, -6.f, 10.f),
                                             -100.f, 50.f);
            d->fronde_score = d->battle_score[ld][s0];      /* capture AVANT la paix (elle solde) */
        } else {
            float sc=d->fronde_score;
            if (sc>=15.f){                                   /* LA LIGUE GAGNE */
                /* le MENEUR se ré-élit à la victoire : le plus fort des membres PRÉSENTS
                 * porte la couronne de la fronde (les puissances ont bougé pendant la guerre). */
                { float bp=suz_power(w,econ,wp,ld);
                  for (int v=0;v<SCPS_MAX_COUNTRY;v++)
                      if (d->v_ligue[v] && suz_power(w,econ,wp,v)>bp){ bp=suz_power(w,econ,wp,v); ld=v; } }
                bool ambitieux=false;
                { Ethos e=suz_ethos(w,econ,ld);
                  float pl=suz_power(w,econ,wp,ld), ps=suz_power(w,econ,wp,s0);
                  /* ambitieux : le tempérament (Dominateur/Honneur), OU la puissance qui
                   * ÉCRASE le maître déchu — on ne s'incline plus devant plus faible que soi. */
                  ambitieux=(e==ETHOS_DOMINATEUR||e==ETHOS_HONNEUR||pl>=1.3f*ps); }
                if (ambitieux && ld>=0){                     /* LE RENVERSEMENT : la pyramide se retourne */
                    for (int v=0;v<SCPS_MAX_COUNTRY;v++){
                        if (!d->v_ligue[v]||v==ld) continue;
                        SuzContrat oc=(SuzContrat)d->contrat[v];
                        d->suzerain[v]=-1; d->contrat[v]=CONTRAT_NONE;
                        diplo_set_vassal(d, ld, v, contrat_adouci(oc));
                        d->v_grief[v]=0.15f;
                    }
                    d->suzerain[ld]=-1; d->contrat[ld]=CONTRAT_NONE;
                    Ethos e=suz_ethos(w,econ,ld);
                    diplo_set_vassal(d, ld, s0, (e==ETHOS_DOMINATEUR||e==ETHOS_HONNEUR)?CONTRAT_SERVAGE:CONTRAT_PROTECTORAT);
                    d->v_grief[s0]=0.30f;
                    d->n_renvers++;
                } else {                                     /* L\'INDÉPENDANCE */
                    for (int v=0;v<SCPS_MAX_COUNTRY;v++){
                        if (!d->v_ligue[v]) continue;
                        d->suzerain[v]=-1; d->contrat[v]=CONTRAT_NONE; d->v_grief[v]=0.f;
                    }
                    d->n_indep++;
                }
            } else {                                         /* LE SUZERAIN GAGNE : on durcit, ça couve */
                for (int v=0;v<SCPS_MAX_COUNTRY;v++){
                    if (!d->v_ligue[v]) continue;
                    if (d->suzerain[v]==s0){
                        d->contrat[v]=CONTRAT_SERVAGE;       /* les contrats se durcissent */
                        d->v_grief[v]=0.35f;                  /* écrasé mais PAS éteint (Kuran : ça couve) */
                        if (capreg[v]>=0&&capreg[v]<econ->n_regions)
                            econ->region[capreg[v]].coercion=fminf(1.f,econ->region[capreg[v]].coercion+0.4f);
                    }
                }
                d->n_ecrase++;
            }
            for (int v=0;v<SCPS_MAX_COUNTRY;v++) d->v_ligue[v]=0;
            d->fronde_suz=-1; d->fronde_lead=-1; d->fronde_score=0.f;
        }
    }
    /* ── 1. le lien vit : tribut, appel, GRIEF, défection ── */
    for (int v=0;v<w->n_countries && v<SCPS_MAX_COUNTRY;v++){
        int s=d->suzerain[v];
        if (s<0) continue;
        if (s>=w->n_countries){ d->suzerain[v]=-1; continue; }
        SuzContrat c=(SuzContrat)d->contrat[v];
        float frac=(c==CONTRAT_SERVAGE)?0.08f:(c==CONTRAT_PROTECTORAT)?0.02f:0.f;
        if (frac>0.f && capreg[s]>=0 && capreg[s]<econ->n_regions){
            float take=0.f;
            for (int r=0;r<econ->n_regions;r++){
                RegionEconomy *re=&econ->region[r];
                if (re->owner!=v) continue;
                float t=re->treasury*frac; re->treasury-=t; take+=t;
            }
            econ->region[capreg[s]].treasury += take;
            if (c==CONTRAT_SERVAGE && capreg[v]>=0 && capreg[v]<econ->n_regions){
                RegionEconomy *cv=&econ->region[capreg[v]];
                cv->coercion = fminf(1.f, cv->coercion+0.04f);
            }
        }
        if (c==CONTRAT_PROTECTORAT||c==CONTRAT_SERVAGE){
            for (int x=0;x<w->n_countries && x<SCPS_MAX_COUNTRY;x++){
                if (x==s||x==v) continue;
                if (d->status[x][v]==DIPLO_WAR && d->status[s][x]!=DIPLO_WAR){
                    d->status[s][x]=DIPLO_WAR; d->status[x][s]=DIPLO_WAR;
                    d->cb[s][x]=CB_TERRITORIAL;
                }
            }
        }
        /* GRIEF — structurel (le contrat) + conjoncturel (éthos, credo, la botte du maître). */
        { float g=d->v_grief[v];
          g += (c==CONTRAT_SERVAGE)?0.10f:(c==CONTRAT_PROTECTORAT)?0.02f:(c==CONTRAT_CITE)?0.02f:0.005f;
          Ethos ev=suz_ethos(w,econ,v);
          if (ev==ETHOS_HONNEUR) g+=0.05f;                       /* l\'Honneur griefe d\'exister en vassal */
          if (suz_credo(w,econ,v)!=suz_credo(w,econ,s)) g+=0.02f;
          if (capreg[s]>=0&&capreg[s]<econ->n_regions&&econ->region[capreg[s]].coercion>0.5f) g+=0.03f;  /* le maître mate/purge : peur ET grief */
          if (d->v_loyal[v]>0.f){ g-=0.04f; d->v_loyal[v]-=365.f; }   /* loyauté achetée : décline + bloque la ligue */
          else g-=0.01f;
          d->v_grief[v]=clampf(g,0.f,1.f);
        }
        /* DÉFECTION individuelle (§4) : trop seul pour se liguer → changer de maître. */
        if (!d->v_ligue[v] && d->v_grief[v]>0.70f){
            float ps=suz_power(w,econ,wp,s);
            int big=-1; float best=ps;
            for (int b=0;b<w->n_countries && b<SCPS_MAX_COUNTRY;b++){
                if (b==v||b==s||d->status[b][v]==DIPLO_WAR) continue;
                if (w->country[b].role==POLITY_UNCLAIMED) continue;
                if (diplo_vassal_count(d,b)>=4) continue;
                float pb=suz_power(w,econ,wp,b);
                if (pb>1.3f*ps && pb>best){ best=pb; big=b; }
            }
            if (big>=0){
                SuzContrat nc=contrat_adouci(c);
                bool paisible=(ps<0.9f*suz_power(w,econ,wp,big));
                d->suzerain[v]=-1; d->contrat[v]=CONTRAT_NONE;
                diplo_set_vassal(d, big, v, nc==CONTRAT_NONE?CONTRAT_PROTECTORAT:nc);
                d->v_grief[v]=0.10f;
                if (paisible) d->n_defect_paix++;
                else { d->status[s][v]=DIPLO_WAR; d->status[v][s]=DIPLO_WAR;
                       d->cb[s][v]=CB_TERRITORIAL; d->n_defect_guerre++; }   /* le CB de REPRISE */
                continue;
            }
        }
        /* l\'ancienne dénonciation simple (le ratio s\'effondre) reste — mais plus rare. */
        { float ms=diplo_mil_power(w,econ,s), mv=diplo_mil_power(w,econ,v);
          if (ms < 0.9f*mv && d->v_grief[v]>0.3f)
              diplo_break_vassal(d, v, c==CONTRAT_SERVAGE); }
    }
    /* ── 2. par SUZERAIN : contre-leviers (selon l\'éthos), puis la LIGUE et la FENÊTRE. */
    for (int s0=0;s0<w->n_countries && s0<SCPS_MAX_COUNTRY;s0++){
        if (g_intim_cd[s0]>0) g_intim_cd[s0]--;
        int   members[SCPS_MAX_COUNTRY], nm=0;
        float psz=suz_power(w,econ,wp,s0), pligue=0.f, gsum=0.f; int nv=0;
        for (int v=0;v<w->n_countries && v<SCPS_MAX_COUNTRY;v++){
            if (d->suzerain[v]!=s0) continue;
            nv++; gsum+=d->v_grief[v];
            /* seuil d\'entrée en ligue : par contrat (le serf fronde tôt, le protégé tard). */
            float seuil=(d->contrat[v]==CONTRAT_SERVAGE)?0.50f:(d->contrat[v]==CONTRAT_CITE)?0.60f:
                        (d->contrat[v]==CONTRAT_PROTECTORAT)?0.65f:0.80f;
            if (suz_ethos(w,econ,v)==ETHOS_HONNEUR) seuil-=0.10f;   /* l\'Honneur fronde tôt */
            bool ligue=(d->v_grief[v]>=seuil && d->v_loyal[v]<=0.f);
            if (ligue && !d->v_ligue[v] && nm==0) d->n_ligues++;     /* une ligue se NOUE */
            d->v_ligue[v]=ligue?1:0;
            if (ligue){ members[nm++]=v; pligue+=suz_power(w,econ,wp,v); }
        }
        if (nv==0) continue;
        float gmoy=gsum/(float)nv;
        /* CONTRE-LEVIERS IA (le maître agit AVANT que la fenêtre ne s\'ouvre) : */
        bool fronde_hesite=false;   /* l'intimidation fait HÉSITER (cette année), sans dissoudre */
        if (d->fronde_suz<0 && pligue>0.9f*psz && gmoy>0.35f){
            Ethos es=suz_ethos(w,econ,s0);
            int worst=-1; float wg=0.f;
            for (int k=0;k<nm;k++) if (d->v_grief[members[k]]>wg){ wg=d->v_grief[members[k]]; worst=members[k]; }
            if (worst<0) for (int v=0;v<SCPS_MAX_COUNTRY;v++) if (d->suzerain[v]==s0 && d->v_grief[v]>wg){ wg=d->v_grief[v]; worst=v; }
            if (worst>=0){
                if (es==ETHOS_MERCANTILE && capreg[s0]>=0 && capreg[worst]>=0
                    && econ->region[capreg[s0]].treasury>200.f){
                    float don=econ->region[capreg[s0]].treasury*0.10f;       /* LE DON — il s\'use */
                    econ->region[capreg[s0]].treasury-=don;
                    econ->region[capreg[worst]].treasury+=don;
                    d->v_grief[worst]=fmaxf(0.f, d->v_grief[worst]-0.25f/(1.f+(float)d->v_dons[worst]));
                    if (d->v_dons[worst]<250) d->v_dons[worst]++;
                    d->v_loyal[worst]=3.f*365.f; d->n_lev_don++;
                } else if ((es==ETHOS_PACIFISTE||es==ETHOS_BUREAUCRATE) && d->contrat[worst]==CONTRAT_SERVAGE){
                    d->contrat[worst]=CONTRAT_PROTECTORAT;                    /* ADOUCIR le contrat */
                    d->v_grief[worst]=fmaxf(0.f,d->v_grief[worst]-0.30f);
                    for (int v=0;v<SCPS_MAX_COUNTRY;v++)                      /* le précédent se voit */
                        if (v!=worst && d->suzerain[v]==s0) d->v_grief[v]=fminf(1.f,d->v_grief[v]+0.04f);
                    d->n_lev_allege++;
                } else if (es==ETHOS_DOMINATEUR && nm>=2){
                    int strong=members[0];                                    /* DIVISER : le privilège */
                    for (int k=1;k<nm;k++) if (suz_power(w,econ,wp,members[k])>suz_power(w,econ,wp,strong)) strong=members[k];
                    d->v_ligue[strong]=0; d->v_loyal[strong]=5.f*365.f;
                    d->v_grief[strong]=fmaxf(0.f,d->v_grief[strong]-0.20f);
                    for (int k=0;k<nm;k++) if (members[k]!=strong) d->v_grief[members[k]]=fminf(1.f,d->v_grief[members[k]]+0.05f);
                    d->n_lev_divise++;
                } else if ((es==ETHOS_DOMINATEUR||es==ETHOS_HONNEUR||es==ETHOS_ORDRE)
                           && g_intim_cd[s0]==0){
                    /* INTIMIDER : la peur monte, la ligue HÉSITE (cette année) — le grief
                     * ne baisse PAS (Kuran : il se tait, il couve, il ressortira). */
                    g_intim_cd[s0]=5;
                    for (int k=0;k<nm;k++){
                        int cv=capreg[members[k]];
                        if (cv>=0&&cv<econ->n_regions)
                            econ->region[cv].coercion=fminf(1.f,econ->region[cv].coercion+0.30f);
                    }
                    fronde_hesite=true;
                    d->n_lev_intim++;
                }
            }
        }
        /* LA FENÊTRE : grief moyen ET ratio — une PROBABILITÉ qui monte, jamais un couperet. */
        if (d->fronde_suz<0 && !fronde_hesite && nm>=2 && pligue > FRONDE_RATIO*psz && gmoy > FRONDE_GRIEF_MIN){
            float p = clampf((pligue/(psz+1.f)-FRONDE_RATIO)*0.8f + (gmoy-FRONDE_GRIEF_MIN), 0.05f, 0.60f);
            if ((float)(fr_rng(d)&0xFFFF)/65535.f < p){
                /* LA FRONDE ÉCLATE : la ligue contre le maître — et les LOYAUX viennent POUR lui. */
                int lead=members[0];
                for (int k=1;k<nm;k++) if (suz_power(w,econ,wp,members[k])>suz_power(w,econ,wp,lead)) lead=members[k];
                for (int k=0;k<nm;k++){
                    int v=members[k];
                    d->status[v][s0]=DIPLO_WAR; d->status[s0][v]=DIPLO_WAR;
                }
                d->cb[lead][s0]=CB_TERRITORIAL;
                for (int v=0;v<w->n_countries && v<SCPS_MAX_COUNTRY;v++){
                    if (d->suzerain[v]!=s0 || d->v_ligue[v]) continue;
                    if (d->v_grief[v]<0.25f)                                  /* le rendement de l\'entretien */
                        for (int k=0;k<nm;k++){ d->status[v][members[k]]=DIPLO_WAR; d->status[members[k]][v]=DIPLO_WAR; }
                }
                d->fronde_suz=(int16_t)s0; d->fronde_lead=(int16_t)lead;
                /* la force RÉUNIE pèse dès le premier jour : une paix expéditive du maître
                 * ne peut pas effacer un rapport de force écrasant (ratio 1.5 → la ligue
                 * part gagnante ; 1.25 → il faudra le gagner sur la durée). */
                d->fronde_score = clampf((pligue/(psz+1.f)-FRONDE_RATIO)*50.f, 0.f, 30.f);
                d->battle_score[lead][s0] = d->fronde_score;
                d->n_frondes++;
            }
        }
    }
    /* ── 3. ACCEPTATION PAR LA MENACE (inchangé) ── */
    for (int v=0;v<w->n_countries && v<SCPS_MAX_COUNTRY;v++){
        if (d->suzerain[v]>=0) continue;
        if (w->country[v].role==POLITY_UNCLAIMED || w->country[v].capital_prov<0) continue;
        if (diplo_ally_count(d,v)>0) continue;
        float mv=diplo_mil_power(w,econ,v); if (mv<=0.f) continue;
        int big=-1; float best=0.f;
        for (int s=0;s<w->n_countries && s<SCPS_MAX_COUNTRY;s++){
            if (s==v || d->status[s][v]==DIPLO_WAR) continue;
            if (w->country[s].role==POLITY_UNCLAIMED) continue;
            if (diplo_vassal_count(d,s)>=4) continue;
            bool adj=false;
            for (int r=0;r<econ->n_regions && !adj;r++){
                if (econ->region[r].owner!=v) continue;
                for (int q=0;q<econ->n_regions;q++)
                    if (econ->adj[r][q] && econ->region[q].owner==s){ adj=true; break; }
            }
            if (!adj) continue;
            float ratio=diplo_mil_power(w,econ,s)/mv;
            if (ratio>=1.8f && ratio>best){ best=ratio; big=s; }
        }
        if (big>=0) diplo_set_vassal(d, big, v, CONTRAT_PROTECTORAT);
    }
}

DiploStatus diplo_status(const DiploState *d, int a, int b){
    if (a<0||a>=SCPS_MAX_COUNTRY||b<0||b>=SCPS_MAX_COUNTRY) return DIPLO_NEUTRAL;
    return d->status[a][b];
}
static void set_sym(DiploState *d, int a, int b, DiploStatus s){
    if (a<0||a>=SCPS_MAX_COUNTRY||b<0||b>=SCPS_MAX_COUNTRY||a==b) return;
    d->status[a][b]=d->status[b][a]=s;
}
void diplo_declare_war  (DiploState *d,int a,int b){ set_sym(d,a,b,DIPLO_WAR); }
void diplo_declare_war_cb(DiploState *d,int a,int b,CasusBelli cb){
    set_sym(d,a,b,DIPLO_WAR);
    if (a>=0&&a<SCPS_MAX_COUNTRY&&b>=0&&b<SCPS_MAX_COUNTRY){ d->cb[a][b]=(int8_t)cb;  /* le but de l'AGRESSEUR */
        if (cb==CB_ANTIPIRATERIE) d->n_war_antipirate++; }
}
CasusBelli diplo_war_goal(const DiploState *d,int a,int b){
    if (a<0||a>=SCPS_MAX_COUNTRY||b<0||b>=SCPS_MAX_COUNTRY) return CB_NONE;
    return (CasusBelli)d->cb[a][b];
}
const char *diplo_cb_name(CasusBelli cb){
    switch(cb){ case CB_TERRITORIAL: return "territorial"; case CB_RELIGIOUS: return "religieux";
                case CB_ECONOMIC: return "économique"; case CB_SUBJUGATION: return "assujettissement";
                case CB_ANTIPIRATERIE: return "anti-piraterie";
                default: return "aucun"; }
}
void diplo_form_alliance(DiploState *d,int a,int b){ set_sym(d,a,b,DIPLO_ALLIED); }
/* §D-sat : nombre d'alliances ACTIVES d'une polité (plafond DIPLO_ALLY_SLOTS). */
int diplo_ally_count(const DiploState *d, int a){
    if (!d || a<0 || a>=SCPS_MAX_COUNTRY) return 0;
    int n=0; for (int b=0;b<SCPS_MAX_COUNTRY;b++) if (b!=a && d->status[a][b]==DIPLO_ALLIED) n++;
    return n;
}
void diplo_make_peace   (DiploState *d,int a,int b){
    set_sym(d,a,b,DIPLO_NEUTRAL);
    if (a>=0&&a<SCPS_MAX_COUNTRY&&b>=0&&b<SCPS_MAX_COUNTRY){
        /* TRÊVE : une longue guerre → une longue trêve. On ne peut redéclarer
         * avant qu'elle fonde — l'enchaînement conquête→reconquête est cassé. */
        float dur = clampf(TRUCE_BASE + TRUCE_PER_YEAR*d->war_years[a][b], 0.f, TRUCE_MAX);
        d->truce[a][b]=d->truce[b][a]=dur;
        d->war_years[a][b]=d->war_years[b][a]=0.f;
        /* COURSE (coques §5) : le but anti-piraterie se SOLDE au verdict — la
         * victime victorieuse fait DÉSARMER le commanditaire (exécuté par la
         * marine) et sa rancune s'éteint ; vaincue, la course continue, légitimée. */
        { float sc=d->battle_score[a][b];
          if ((CasusBelli)d->cb[a][b]==CB_ANTIPIRATERIE && sc>=20.f){
              d->pirate_disarm[b]=1; d->pirate_rancor[a][b]=0.f; }
          if ((CasusBelli)d->cb[b][a]==CB_ANTIPIRATERIE && sc<=-20.f){
              d->pirate_disarm[a]=1; d->pirate_rancor[b][a]=0.f; } }
        d->cb[a][b]=d->cb[b][a]=CB_NONE;   /* le but de guerre s'éteint avec la guerre */
        d->battle_score[a][b]=d->battle_score[b][a]=0.f;   /* le bras-de-fer se solde */
        d->conquered[a][b]=d->conquered[b][a]=0;
        d->conq_value[a][b]=d->conq_value[b][a]=0.f;       /* §5 : le budget dépensé se solde */
    }
}
bool diplo_can_declare(const DiploState *d,int a,int b){
    if (a<0||a>=SCPS_MAX_COUNTRY||b<0||b>=SCPS_MAX_COUNTRY) return false;
    return d->truce[a][b] <= 0.f;
}
float diplo_truce_days(const DiploState *d,int a,int b){
    if (a<0||a>=SCPS_MAX_COUNTRY||b<0||b>=SCPS_MAX_COUNTRY) return 0.f;
    return d->truce[a][b];
}

/* ---- accesseurs ------------------------------------------------------- */
static const PopCulture *cap_culture(const World *w, const WorldEconomy *econ, int cid){
    if (cid<0||cid>=w->n_countries) return NULL;
    int cp=w->country[cid].capital_prov;
    if (cp<0||cp>=w->n_provinces) return NULL;
    int cr=w->province[cp].region;
    if (cr<0||cr>=econ->n_regions) return NULL;
    return &econ->region[cr].culture;
}
static unsigned country_res_mask(const World *w, const WorldEconomy *econ, int cid){
    unsigned m=0;
    for (int r=0;r<econ->n_regions;r++) if (econ->region[r].owner==cid)
        for (int res=1;res<RES_PROD_FIRST && res<32;res++)
            if (econ->region[r].raw_cap[res] > 0.f) m |= (1u<<res);
    (void)w; return m;
}
static int popcount(unsigned x){ int n=0; while(x){n+=x&1u;x>>=1;} return n; }
static float geo_dist(const World *w, int a, int b){
    int pa=w->country[a].capital_prov, pb=w->country[b].capital_prov;
    if (pa<0||pb<0||pa>=w->n_provinces||pb>=w->n_provinces) return 1e6f;
    float dx=(float)(w->province[pa].seed_x-w->province[pb].seed_x);
    float dy=(float)(w->province[pa].seed_y-w->province[pb].seed_y);
    return sqrtf(dx*dx+dy*dy);
}
static float race_influence(const World *w, const WorldEconomy *econ, int cid){
    const PopCulture *pc=cap_culture(w,econ,cid);
    if (!pc) return 0.f;
    SpeciesBuild sb=species_default_build(pc->race);
    return build_leviers(&sb).influence;
}

/* ---- lecteurs --------------------------------------------------------- */
float diplo_eco_power(const WorldProsperity *wp, int cid){
    if (cid<0||cid>=wp->n_countries) return 0.f;
    return wp->country[cid].P_realise;
}
float diplo_mil_power(const World *w, const WorldEconomy *econ, int cid){
    float pop=0.f, H=0.f, arms=0.f, kit=0.f;
    for (int r=0;r<econ->n_regions;r++) if (econ->region[r].owner==cid){
        const RegionEconomy *re=&econ->region[r];
        pop += re->strata[CLASS_LABORER].pop+re->strata[CLASS_BOURGEOIS].pop+re->strata[CLASS_ELITE].pop;
        H   += re->build.H_coerc;
        arms+= re->stock[RES_ENCHANTED_ARMS];                      /* armes enchantées (Forge céleste) */
        kit += re->stock[RES_ARMS] + re->stock[RES_GUNPOWDER];     /* armes & poudre (Armurerie/Poudrière) */
    }
    const PopCulture *pc=cap_culture(w,econ,cid);
    float race_coerc=0.f, mart=0.f;
    if (pc){
        SpeciesBuild sb=species_default_build(pc->race);
        race_coerc=build_leviers(&sb).coercition;
        if (pc->martial==MART_HORDE_MONTEE||pc->martial==MART_LEVEE_MASSIVE||
            pc->martial==MART_THALASSO_PREDATRICE) mart=0.7f;   /* traditions offensives */
    }
    /* Les armes enchantées sont un MULTIPLICATEUR de qualité (l'arcane nourrit la
     * guerre) — rendements décroissants, plafonnés. */
    float ench = 3.0f*(1.f - 1.f/(1.f + arms*0.05f));
    /* Armes & poudre de BASE : équipent la levée — rendements décroissants,
     * plafonnés plus bas que l'arcane (le fer arme, l'arcane décide). */
    float gear = 1.8f*(1.f - 1.f/(1.f + kit*0.03f));
    return sqrtf(pop)*0.04f + H + race_coerc + mart + ench + gear;
}

static float threat_of(const World *w, const WorldEconomy *econ,
                       const WorldProsperity *wp, const DiploState *d, int a, int b){
    float eco=diplo_eco_power(wp,b), mil=diplo_mil_power(w,econ,b);
    float dist=geo_dist(w,a,b);
    float infl=race_influence(w,econ,b);          /* l'influence étend la portée */
    float eff=dist/(1.f+0.10f*infl);
    float base=(eco+mil)/(eff*0.02f + 1.f);
    /* MOMENTUM : la FULGURANCE effraie plus que la masse — un empire qui snowballe
     * alarme bien plus qu'un grand empire immobile à puissance égale. */
    float momentum = (d && b<SCPS_MAX_COUNTRY) ? d->momentum[b] : 0.f;
    return base * (1.f + MOMENTUM_W*momentum);
}

Relation diplo_relation(const World *w, const WorldEconomy *econ,
                        const WorldProsperity *wp, const DiploState *d, int a, int b){
    Relation r; memset(&r,0,sizeof r);
    if (a<0||a>=w->n_countries||b<0||b>=w->n_countries||a==b) return r;

    r.threat = threat_of(w,econ,wp,d,a,b);

    unsigned ra=country_res_mask(w,econ,a), rb=country_res_mask(w,econ,b);
    int uni=popcount(ra|rb), inter=popcount(ra&rb);
    r.complement = uni>0 ? (float)(uni-inter)/(float)uni : 0.f;

    const PopCulture *ca=cap_culture(w,econ,a), *cb=cap_culture(w,econ,b);
    r.kinship = (ca&&cb) ? sphere_distance(species_sphere(ca->race),species_sphere(cb->race)) : 0.f;

    if (ca&&cb){
        bool same_branch=(ca->rel_branch==cb->rel_branch);
        bool both_zeal  =(ca->credo!=CREDO_PLURALISTE && cb->credo!=CREDO_PLURALISTE);
        float dr=absf(ca->religion-cb->religion);
        r.schism = (same_branch&&both_zeal) ? clampf(1.f - dr/5.f, 0.f, 1.f) : 0.f;
    }

    /* alliance = menace partagée (transitoire) + λ·complément + μ·f(parenté)
     *          − ν·distance de valeurs − ξ·schisme. */
    float shared=0.f;
    for (int c=0;c<w->n_countries;c++) if (c!=a&&c!=b){
        float t=threat_of(w,econ,wp,d,a,c), u=threat_of(w,econ,wp,d,b,c);
        float m=(t<u)?t:u; if (m>shared) shared=m;
    }
    /* §D2 : la menace commune en RAPPORT au monde du moment (ne gonfle plus avec
     * l'échelle) → une alliance se noue quand la menace est haute PAR RAPPORT au
     * monde, pas quand les chiffres ont simplement gonflé. Le seuil fixe redevient juste. */
    float amb = (d && d->ambient_threat>1e-4f)? d->ambient_threat : 1.f;
    float shared_rel = shared / amb;
    r.shared_rel = shared_rel;                      /* §D1 : exposé → la dissolution relit la menace */
    float val_dist = (ca&&cb) ? absf(ca->valeurs-cb->valeurs) : 0.f;
    float fk = r.kinship*(10.f-r.kinship)/25.f;     /* cloche sur la parenté */
    r.alliance = K_SHARED*shared_rel + 2.0f*r.complement + 1.0f*fk - 0.3f*val_dist - 2.0f*r.schism;
    return r;
}

/* ---- CASUS BELLI — la raison de la guerre (lue, jamais posée) ---------- */
static bool country_extracts(const WorldEconomy *econ, int cid, Resource g){
    if (g<=RES_NONE||g>=RES_COUNT) return false;
    for (int r=0;r<econ->n_regions;r++)
        if (econ->region[r].owner==cid && econ->region[r].raw_cap[g]>0.1f) return true;
    return false;
}
static bool diplo_adjacent(const WorldEconomy *econ, int a, int b){
    for (int r=0;r<econ->n_regions;r++) if (econ->region[r].owner==a)
        for (int s=0;s<econ->n_regions;s++)
            if (econ->region[s].owner==b && econ->adj[r][s]) return true;
    return false;
}
void diplo_pirate_grief(DiploState *d, int victim, int sponsor, float amount){
    if (!d||victim<0||victim>=SCPS_MAX_COUNTRY||sponsor<0||sponsor>=SCPS_MAX_COUNTRY) return;
    d->pirate_rancor[victim][sponsor]=clampf(d->pirate_rancor[victim][sponsor]+amount,0.f,10.f);
}
float diplo_pirate_rancor(const DiploState *d, int victim, int sponsor){
    if (!d||victim<0||victim>=SCPS_MAX_COUNTRY||sponsor<0||sponsor>=SCPS_MAX_COUNTRY) return 0.f;
    return d->pirate_rancor[victim][sponsor];
}
CasusBelli diplo_casus_belli(const World *w, const WorldEconomy *econ, const WorldProsperity *wp,
                             const DiploState *d, int a, int b, Resource want){
    if (a<0||a>=w->n_countries||b<0||b>=w->n_countries||a==b) return CB_NONE;
    /* ANTI-PIRATERIE (coques §5) — chatouiller un GÉANT trop longtemps : le seuil
     * tombe avec la puissance de la VICTIME (le géant a les moyens de sa colère). */
    if (d && a<SCPS_MAX_COUNTRY && b<SCPS_MAX_COUNTRY && d->pirate_rancor[a][b]>0.f){
        float Pa=diplo_mil_power(w,econ,a), Pb=diplo_mil_power(w,econ,b);
        float seuil=clampf(4.5f*(Pb/(Pa+0.1f)), 1.5f, 6.f);
        if (d->pirate_rancor[a][b]>=seuil) return CB_ANTIPIRATERIE;
    }
    /* ÉCONOMIQUE — le bien AIGU que la cible extrait et que nous n'avons pas (monopole) :
     * le casus belli du Mercantile bloqué (il vise la province-source). */
    if (want>RES_NONE && want<RES_COUNT && country_extracts(econ,b,want) && !country_extracts(econ,a,want))
        return CB_ECONOMIC;
    /* TERRITORIAL par RANCUNE (§6) — l'IRRÉDENTISME : on a perdu des terres au profit
     * de b et on garde le grief → on peut revenir les reprendre, même sans adjacence. */
    if (d && a<SCPS_MAX_COUNTRY && b<SCPS_MAX_COUNTRY && d->rancor[a][b] > RANCOR_CB_SEUIL)
        return CB_TERRITORIAL;
    /* RELIGIEUX — la CROISADE faustienne : une foi orthodoxe contre un empire qui
     * développe l'interdit (Gardiens vs Transgresseurs). */
    if (diplo_faustian_cb(w,econ,d,a,b)) return CB_RELIGIOUS;
    /* RELIGIEUX — schisme (branche proche + prosélytisme = ennemi naturel). */
    Relation rel = diplo_relation(w,econ,wp,d,a,b);
    if (rel.schism > 0.45f) return CB_RELIGIOUS;
    /* TERRITORIAL — adjacence / revendication de frontière (la raison la plus commune). */
    if (diplo_adjacent(econ,a,b)) return CB_TERRITORIAL;
    /* ASSUJETTISSEMENT — on PROJETTE nettement plus de puissance que la cible. */
    if (diplo_mil_power(w,econ,a) > 1.6f*diplo_mil_power(w,econ,b)+1.f) return CB_SUBJUGATION;
    return CB_NONE;   /* aucune raison ne tient → pas de guerre */
}

/* ---- guerre : conquête ------------------------------------------------ */
bool diplo_conquer_region(DiploState *d, World *w, WorldEconomy *econ,
                          WorldLegitimacy *wl, int conqueror, int region, bool conqueror_enslaves){
    if (conqueror<0||conqueror>=w->n_countries) return false;
    if (region<0||region>=econ->n_regions) return false;
    RegionEconomy *re=&econ->region[region];
    if (!re->culture.settled) return false;          /* rien à conquérir */
    int defender=re->owner;
    if (defender==conqueror) return false;           /* déjà à nous */
    if (defender>=0 && diplo_status(d,conqueror,defender)!=DIPLO_WAR) return false;
    float price = diplo_province_price(econ, region);  /* §5 : son prix INTACT (avant le saccage) */
    re->owner = conqueror;            /* transfert : la diversité suit (compute_profile) */
    re->colonized = true;
    re->revolt_scar = 1.0f;           /* la conquête CONVULSE : −50 % dévelop. quelques années */
    if (conqueror<SCPS_MAX_COUNTRY){
        d->momentum[conqueror] += MOMENTUM_PER_CONQ;   /* la fulgurance EFFRAIE (→ coalition) */
        if (defender>=0 && defender<SCPS_MAX_COUNTRY){
            d->conquered[conqueror][defender]++;        /* OCCUPATION : pousse le score de guerre */
            d->conq_value[conqueror][defender] += price;/* §5 : budget de score DÉPENSÉ sur cette prise */
            d->rancor[defender][conqueror] += RANCOR_PER_LOSS;  /* §6 le DÉPOSSÉDÉ garde rancune */
            /* §5 LÉGITIMITÉ : au-delà de ce que la domination militaire justifie (la
             * revendication), la prise est de la SUREXPANSION — surcroît de fulgurance
             * (le monde se ligue) et plaie plus profonde (intégration déjà à zéro). */
            if (d->conquered[conqueror][defender] > diplo_war_claim(d,w,econ,conqueror,defender)){
                d->momentum[conqueror] += CLAIM_ILLEGIT_MOM;
                d->rancor[defender][conqueror] += RANCOR_ILLEGIT;  /* l'agression nue creuse le grief */
                re->revolt_scar = 1.0f;
            }
        }
    }
    legitimacy_on_conquest(wl, region);   /* L au plancher, intégration à zéro */
    /* SACCAGE : la prise DÉPOUILLE la province (or + production → trésor de
     * l'occupant), 1×/5 ans. Le butin afflue vers la capitale du conquérant. */
    int dst=-1, cp=w->country[conqueror].capital_prov;
    if (cp>=0 && cp<w->n_provinces) dst=w->province[cp].region;
    diplo_pillage_region(econ, region, dst);
    diplo_enslave_capture(w, econ, conqueror, region, conqueror_enslaves);   /* §4c : gate = TECH_ESCLAVAGE */
    return true;
}

/* ---- guerre : ESCLAVAGE (§4c) — déporter la population prise ----------- *
 * GATE = la TECH d'asservissement (TECH_ESCLAVAGE, signature Orque) : l'appelant
 * passe `enslaves` = l'empire a-t-il l'Économie servile débloquée. La tech circule
 * avec les peuples (orpheline) → ce sont les Orques et ceux qui les ont absorbés. */
long diplo_enslave_capture(World *w, WorldEconomy *econ, int conqueror, int region, bool enslaves){
    if (!enslaves) return 0;                                          /* société sans l'Économie servile */
    if (conqueror<0||conqueror>=w->n_countries||region<0||region>=econ->n_regions) return 0;
    int cp=w->country[conqueror].capital_prov;
    int crr=(cp>=0&&cp<w->n_provinces)? w->province[cp].region : -1;
    if (crr<0||crr>=econ->n_regions||crr==region) return 0;
    ProvincePop *src=&econ->region[region].pop, *dst=&econ->region[crr].pop;
    /* les captifs sortent du plus GROS groupe de la province prise. */
    int gi=-1; long best=0;
    for (int i=0;i<src->n_groups;i++) if (src->groups[i].count>best){ best=src->groups[i].count; gi=i; }
    if (gi<0) return 0;                              /* province non groupée → rien à déporter ici */
    long captives=(long)((float)src->groups[gi].count*SLAVE_FRACTION);
    if (captives<=0) return 0;
    /* le cœur doit DÉJÀ être représenté en groupes : injecter dans une région
     * mono-groupe (n_groups=0) masquerait sa population native (repli ignoré dès
     * n_groups>0). Pas de place libre non plus → on renonce. */
    if (dst->n_groups<1 || dst->n_groups>=SCPS_MAX_GROUPS) return 0;
    /* déportation au CŒUR : on crée un cohorte DIASPORA non-intégrée (restive) de
     * culture étrangère → le D̄ du maître monte (la fracture s'installe au centre).
     * Toujours DISTINCTE (jamais fondue dans un groupe libre co-culturel). */
    PopGroup ng=src->groups[gi];                      /* garde race/culture/origine */
    ng.count=captives; ng.diaspora=true; ng.integration=0.f;
    ng.drift_id=SLAVE_DRIFT_BASE + region*SCPS_MAX_GROUPS + dst->n_groups;
    dst->groups[dst->n_groups++]=ng;
    src->groups[gi].count-=captives;
    if (src->groups[gi].count<=0){ src->groups[gi]=src->groups[src->n_groups-1]; src->n_groups--; }
    return captives;
}

/* ---- guerre : SACCAGE (§4) — dépouiller la province prise -------------- */
#define PILLAGE_COOLDOWN_Y 5.0f    /* 1 saccage / 5 ans / province (note utilisateur) */
#define PILLAGE_GOLD_FRAC  0.6f    /* part du trésor provincial raflée d'un coup */
#define PILLAGE_STOCK_FRAC 0.5f    /* ~6 mois de production en entrepôt, fondus en or */
float diplo_pillage_region(WorldEconomy *econ, int region, int dst_region){
    if (!econ || region<0 || region>=econ->n_regions) return 0.f;
    RegionEconomy *re=&econ->region[region];
    if (re->pillage_cd > 0.f) return 0.f;          /* déjà dépouillée → plus rien à prendre */
    float loot = PILLAGE_GOLD_FRAC * re->treasury; /* l'or des coffres */
    re->treasury *= (1.f - PILLAGE_GOLD_FRAC);
    for (int g=1; g<RES_COUNT; g++){               /* l'entrepôt, valorisé au prix courant */
        float take = PILLAGE_STOCK_FRAC * re->stock[g];
        loot += take * re->price[g];
        re->stock[g] -= take;
    }
    re->revolt_scar = 1.0f;                         /* le sac CONVULSE : gel du développement */
    re->pillage_cd  = PILLAGE_COOLDOWN_Y;           /* ne pourra être re-saccagée avant ~5 ans */
    if (dst_region>=0 && dst_region<econ->n_regions && dst_region!=region)
        econ->region[dst_region].treasury += loot;  /* fondu dans le trésor de l'occupant */
    return loot;
}

/* ---- Diplomatie d'ÉQUILIBRE — friction & coalition -------------------- */
float diplo_war_widening_cost(const World *w, const WorldEconomy *econ,
                              const DiploState *d, int attacker, int target){
    float c=0.f;
    for (int k=0;k<w->n_countries;k++){
        if (k==attacker || k==target) continue;
        if (diplo_status(d,target,k)==DIPLO_ALLIED)     /* allié susceptible d'entrer en guerre */
            c += diplo_mil_power(w,econ,k);
    }
    return c;   /* renchérit la cible : frapper un protégé d'une puissance ÉLARGIT la guerre */
}
int diplo_perceived_hegemon(const World *w, const WorldEconomy *econ,
                            const WorldProsperity *wp, const DiploState *d, int self){
    int best=-1; float t1=0.f, t2=0.f;            /* les deux plus fortes menaces perçues */
    for (int b=0;b<w->n_countries;b++){
        if (b==self || w->country[b].role==POLITY_UNCLAIMED) continue;
        float t=threat_of(w,econ,wp,d,self,b);
        if (t>t1){ t2=t1; t1=t; best=b; } else if (t>t2) t2=t;
    }
    /* hégémon = une menace qui DOMINE nettement la suivante (aucun script : c'est la
     * lecture de menace de CHACUN ; quand un même pays domine pour plusieurs, ils se
     * comportent de facto en coalition). */
    return (best>=0 && t1 > HEGEMON_RATIO*fmaxf(t2, HEGEMON_FLOOR)) ? best : -1;
}

/* ---- Score de guerre : le bras-de-fer + l'attrition ------------------- */
static void deplete_arms(WorldEconomy *econ, int cid, float frac){
    frac = clampf(frac, 0.f, 0.95f);
    for (int r=0;r<econ->n_regions;r++) if (econ->region[r].owner==cid){
        econ->region[r].stock[RES_ARMS]          *= (1.f-frac);
        econ->region[r].stock[RES_GUNPOWDER]      *= (1.f-frac);
        econ->region[r].stock[RES_ENCHANTED_ARMS] *= (1.f-frac);
    }
}
void diplo_war_tick(DiploState *d, World *w, WorldEconomy *econ,
                    const WorldProsperity *wp, float dt){
    /* §D2 : menace AMBIANTE du monde (moyenne des paires) — la référence RELATIVE des
     * alliances, recalculée ici (la passe qui a w/econ/wp sous la main, une fois/tick). */
    { float sum=0.f; int n=0;
      for (int a=0;a<w->n_countries;a++) for (int b=0;b<w->n_countries;b++)
          if (a!=b && w->country[a].role!=POLITY_UNCLAIMED && w->country[b].role!=POLITY_UNCLAIMED){
              sum += threat_of(w,econ,wp,d,a,b); n++; }
      d->ambient_threat = (n>0)? fmaxf(sum/(float)n, THREAT_FLOOR) : THREAT_FLOOR;
    }
    for (int a=0;a<w->n_countries;a++) for (int b=0;b<w->n_countries;b++){
        if (a==b || d->status[a][b]!=DIPLO_WAR) continue;
        if (d->cb[a][b]==CB_NONE) continue;             /* a est l'ATTAQUANT (il porte le CB) */
        float pA=diplo_mil_power(w,econ,a), pB=diplo_mil_power(w,econ,b);
        float ratio = pA/(pA+pB+0.01f);                  /* avantage militaire de l'attaquant */
        /* RALLIEMENT (§6) : qui reprend SES terres se bat avec fureur — la rancune
         * galvanise (saturation douce) → le bras-de-fer penche plus vite vers le lésé. */
        float rally = 1.f + RANCOR_RALLY_W*(d->rancor[a][b]/(d->rancor[a][b]+RANCOR_RALLY_NORM));
        /* BATAILLES : l'avantage pousse le battle_score vers +50 ; un attaquant plus
         * FAIBLE le voit chuter (la voie défensive de l'adversaire vers −100). */
        d->battle_score[a][b] = clampf(d->battle_score[a][b] + WAR_BATTLE_W*(ratio-0.5f)*2.f*dt*rally,
                                       -100.f, WAR_BATTLE_CAP);
        d->battle_score[b][a] = d->battle_score[a][b];   /* miroir lisible */
        /* ATTRITION : la guerre SAIGNE les armes des deux ; le perdant de l'échange
         * en perd plus → mil_power baisse → la guerre s'épuise (pression à la paix). */
        float lossA = WAR_ATTRITION*dt*(ratio<0.5f?WAR_ATTR_LOSER:WAR_ATTR_WINNER);
        float lossB = WAR_ATTRITION*dt*(ratio>0.5f?WAR_ATTR_LOSER:WAR_ATTR_WINNER);
        deplete_arms(econ,a,lossA); deplete_arms(econ,b,lossB);
    }
}
float diplo_war_score(const DiploState *d, int a, int b){
    if (a<0||a>=SCPS_MAX_COUNTRY||b<0||b>=SCPS_MAX_COUNTRY) return 0.f;
    float occ = fminf(50.f, WAR_OCCUPY_PER*(float)d->conquered[a][b]);  /* +50→+100 par l'occupation */
    return clampf(d->battle_score[a][b] + occ, -100.f, 100.f);
}

/* ---- Paix proportionnelle (§5) : la victoire achète des termes -------- */
int diplo_war_claim(const DiploState *d, const World *w, const WorldEconomy *econ, int a, int b){
    if (a<0||a>=SCPS_MAX_COUNTRY||b<0||b>=SCPS_MAX_COUNTRY) return 0;
    CasusBelli cb=(CasusBelli)d->cb[a][b];
    float pA=diplo_mil_power(w,econ,a), pB=diplo_mil_power(w,econ,b);
    float ratio=pA/(pA+pB+0.01f);                        /* domination militaire de l'attaquant */
    if (cb==CB_NONE)        return ratio>0.5f ? 1 : 0;   /* conquête nue : 1 tampon si l'on domine */
    if (cb!=CB_TERRITORIAL) return 1;                    /* humiliation/source/vassalité : une prise */
    return 1 + (int)(CLAIM_DOM*fmaxf(0.f, ratio-0.5f));  /* territorial : ∝ domination */
}

/* ---- §5 COMBAT : le PRIX d'une province (∝ valeur développée) ---------- */
#define PRICE_BASE   4.f    /* un arrière-pays nu : ~5-8 */
#define PRICE_BUILT  0.9f   /* par point de densité bâtie (K/H/P/food/foi/savoir) */
#define PRICE_PROS   1.2f   /* par point de prospérité locale */
#define PRICE_POP    0.002f /* par âme (un cœur peuplé coûte plus) — une GROSSE province ~40 */
float diplo_province_price(const WorldEconomy *econ, int region){
    if (!econ || region<0 || region>=econ->n_regions) return PRICE_BASE;
    const RegionEconomy *re=&econ->region[region];
    const ProvBuild *b=&re->build;
    float built = b->K_inst + b->H_coerc + b->PE_infra + b->food_cap + b->faith + b->savoir;
    float pop   = re->strata[CLASS_LABORER].pop + re->strata[CLASS_BOURGEOIS].pop
                + re->strata[CLASS_ELITE].pop;
    float dev   = PRICE_BUILT*built + PRICE_PROS*re->prosperity + PRICE_POP*pop;
    /* le SACCAGE effondre la valeur (revolt_scar) → la province pillée coûte moins. */
    float scar = (re->revolt_scar>0.f) ? (1.f - 0.45f*clampf(re->revolt_scar,0.f,1.f)) : 1.f;
    return PRICE_BASE + dev*scar;
}
#define BUDGET_DOM    300.f  /* valeur achetable par cran de domination militaire (au-delà de 0.5) */
#define BUDGET_SCORE  0.40f  /* … + une prime du score accumulé (une victoire décisive prend plus) */
float diplo_war_budget(const DiploState *d, const World *w, const WorldEconomy *econ, int a, int b){
    /* Le budget de conquête = la DOMINATION militaire (disponible d'emblée — sinon, le
     * score partant de 0, l'attaquant ne pourrait jamais s'offrir la 1re province) PLUS
     * une prime du score de guerre accumulé (l'occupation/les batailles → prend davantage). */
    float pA=diplo_mil_power(w,econ,a), pB=diplo_mil_power(w,econ,b);
    float ratio=pA/(pA+pB+0.01f);
    float dom = BUDGET_DOM * fmaxf(0.f, ratio-0.5f);
    float sc  = diplo_war_score(d,a,b); if (sc<0.f) sc=0.f;
    return dom + BUDGET_SCORE*sc;
}
/* La valeur TOTALE du territoire d'un pays (Σ prix des provinces) — un budget qui la
 * couvre toute = victoire DÉCISIVE (annexion possible) ; sinon le pays est protégé. */
float diplo_country_value(const WorldEconomy *econ, int cid){
    if (!econ || cid<0) return 0.f;
    float v=0.f;
    for (int r=0;r<econ->n_regions;r++)
        if (econ->region[r].owner==cid && econ->region[r].culture.settled)
            v += diplo_province_price(econ, r);
    return v;
}

float diplo_reparations(DiploState *d, World *w, WorldEconomy *econ, int a, int b){
    if (a<0||a>=w->n_countries||b<0||b>=w->n_countries||a==b) return 0.f;
    float s=diplo_war_score(d,a,b);                      /* point de vue de a */
    if (absf(s) < REP_MIN_SCORE) return 0.f;             /* match nul → pas de vainqueur net */
    int winner=(s>0.f)?a:b, loser=(s>0.f)?b:a;
    float frac=REP_RATE*fminf(1.f, absf(s)/100.f);       /* plus la défaite est nette, plus on saigne */
    int cap=w->country[winner].capital_prov;
    int dst=(cap>=0&&cap<w->n_provinces)?w->province[cap].region:-1;
    float total=0.f;
    for (int r=0;r<econ->n_regions;r++) if (econ->region[r].owner==loser){
        float pay=frac*econ->region[r].treasury;
        econ->region[r].treasury-=pay; total+=pay;       /* indemnité prélevée sur tout le royaume */
    }
    if (dst>=0&&dst<econ->n_regions) econ->region[dst].treasury+=total;
    return total;
}

/* §5 — LE BUTIN : le budget de score NON dépensé en terres (« les 95 % restants »)
 * VIDE les coffres du vaincu vers la capitale du vainqueur. Une victoire écrasante sur
 * un petit pays prend ses quelques provinces ET son or ; sur un grand, on prend ce que
 * le budget couvre en terre, le reste en pillage. */
#define LOOT_GOLD_PER_VALUE 16.f   /* conversion : un point de budget restant convoite ~16 or */
float diplo_loot(World *w, WorldEconomy *econ, int attacker, int defender, float leftover_value){
    if (attacker<0||attacker>=w->n_countries||defender<0||defender>=w->n_countries||attacker==defender) return 0.f;
    if (leftover_value<=0.f) return 0.f;
    float want = leftover_value * LOOT_GOLD_PER_VALUE;
    int cap=w->country[attacker].capital_prov;
    int dst=(cap>=0&&cap<w->n_provinces)?w->province[cap].region:-1;
    float total=0.f;
    for (int r=0;r<econ->n_regions && total<want;r++) if (econ->region[r].owner==defender){
        float take=fminf(econ->region[r].treasury, want-total);
        if (take>0.f){ econ->region[r].treasury-=take; total+=take; }   /* on vide les coffres */
    }
    if (dst>=0&&dst<econ->n_regions) econ->region[dst].treasury+=total;
    return total;
}

float diplo_rancor(const DiploState *d, int a, int b){
    if (a<0||a>=SCPS_MAX_COUNTRY||b<0||b>=SCPS_MAX_COUNTRY) return 0.f;
    return d->rancor[a][b];
}

/* ---- Croisade contre le faustien : Gardiens vs Transgresseurs ---------- */
#define FAUST_CRUSADE_ORTHO  0.22f   /* foi régnante austère (orthodoxe) en deçà */
#define FAUST_CRUSADE_TAINT  4.0f    /* souillure faustienne nette au-delà (charge ~Brèche=5) */
void  diplo_set_faustian(DiploState *d, int cid, float level){
    if (cid>=0 && cid<SCPS_MAX_COUNTRY) d->faustian[cid]=level;
}
float diplo_faustian(const DiploState *d, int cid){
    return (cid>=0&&cid<SCPS_MAX_COUNTRY) ? d->faustian[cid] : 0.f;
}
/* posture de la foi régnante (orthodoxe bas ↔ permissif haut), lue de l'éthos. */
static float diplo_orthodoxy_of(const World *w, const WorldEconomy *econ, int cid){
    const PopCulture *pc=cap_culture(w,econ,cid);
    if (!pc) return 0.3f;
    switch(pc->ethos){
        case ETHOS_ORDRE: return 0.10f; case ETHOS_BUREAUCRATE: return 0.14f;
        case ETHOS_PACIFISTE: return 0.20f; case ETHOS_MERCANTILE: return 0.26f;
        case ETHOS_HONNEUR: return 0.30f; case ETHOS_DOMINATEUR: return 0.36f;
        default: return 0.20f;
    }
}
bool diplo_faustian_cb(const World *w, const WorldEconomy *econ, const DiploState *d,
                       int attacker, int target){
    if (attacker<0||attacker>=w->n_countries||target<0||target>=w->n_countries||attacker==target) return false;
    return diplo_orthodoxy_of(w,econ,attacker) < FAUST_CRUSADE_ORTHO
        && diplo_faustian(d,target) > FAUST_CRUSADE_TAINT;
}

void diplo_tick(DiploState *d, float dt){
    for (int a=0;a<SCPS_MAX_COUNTRY;a++){
        /* la fulgurance s'oublie : un conquérant arrêté cesse d'effrayer. */
        d->momentum[a] = fmaxf(0.f, d->momentum[a] - MOMENTUM_DECAY*dt);
        /* la RANCUNE (§6) s'estompe sur une génération (asymétrique : plein balayage). */
        for (int b=0;b<SCPS_MAX_COUNTRY;b++){
            if (d->rancor[a][b]>0.f) d->rancor[a][b]=fmaxf(0.f, d->rancor[a][b]-RANCOR_DECAY*dt);
            if (d->pirate_rancor[a][b]>0.f)   /* la rancune de course s'oublie au même pas */
                d->pirate_rancor[a][b]=fmaxf(0.f, d->pirate_rancor[a][b]-RANCOR_DECAY*dt);
        }
        for (int b=a+1;b<SCPS_MAX_COUNTRY;b++){
            if (d->status[a][b]==DIPLO_WAR){
                d->war_years[a][b]+=dt/365.f; d->war_years[b][a]=d->war_years[a][b];
            }
            if (d->truce[a][b]>0.f){            /* la trêve fond comme le revanchisme */
                d->truce[a][b]=fmaxf(0.f, d->truce[a][b]-dt);
                d->truce[b][a]=d->truce[a][b];
            }
        }
    }
}
