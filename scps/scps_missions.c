/*
 * scps_missions.c — LES MISSIONS DÉCENNALES (factions §8)
 *
 * Contextuelles : le KIND tourne par décennie, la CIBLE vient de l'état du pays —
 * l'éthos dominant choisit l'institution à bâtir, la production choisit la chaîne à
 * renforcer, la recherche choisit le palier à percer. Récompense ∝ ampleur.
 */
#include "scps_missions.h"
#include "scps_factions.h"   /* la faction dominante oriente la mission de bâti */
#include <string.h>
#include <stdio.h>
#include <math.h>

/* Les six coordonnées bâties adressables (cf. ProvBuild) + le mot de la cible. */
enum { CB_K=0, CB_PE, CB_FAITH, CB_SAVOIR, CB_H, CB_FOOD };
static float build_coord(const ProvBuild *b, int c){
    switch (c){ case CB_K: return b->K_inst; case CB_PE: return b->PE_infra;
                case CB_FAITH: return b->faith; case CB_SAVOIR: return b->savoir;
                case CB_H: return b->H_coerc;  default: return b->food_cap; }
}
static const char *coord_word(int c){
    switch (c){ case CB_K: return "les institutions"; case CB_PE: return "le commerce";
                case CB_FAITH: return "la foi"; case CB_SAVOIR: return "le savoir";
                case CB_H: return "la garde";  default: return "les vivres"; }
}
/* L'éthos dominant choisit la coordonnée à ériger (ce que la faction veut de l'État). */
static int coord_for_faction(EthosFaction f){
    switch (f){ case FAC_CONQUERANT: return CB_H;   case FAC_MARCHAND: return CB_PE;
                case FAC_LEGISTE:    return CB_K;   case FAC_GARDIEN:  return CB_FAITH;
                case FAC_TRANSGRESSEUR: return CB_SAVOIR; default: return CB_FOOD; }
}

static int capital_region(const World *w, const WorldEconomy *econ, int cid){
    if (cid<0||cid>=w->n_countries) return -1;
    int cp=w->country[cid].capital_prov; if (cp<0||cp>=w->n_provinces) return -1;
    int cr=w->province[cp].region; return (cr>=0&&cr<econ->n_regions)?cr:-1;
}
static int has_regions(const WorldEconomy *econ, int cid){
    for (int r=0;r<econ->n_regions;r++) if (econ->region[r].owner==cid) return 1;
    return 0;
}
static float country_stock(const WorldEconomy *econ, int cid, Resource g){
    float s=0.f; for (int r=0;r<econ->n_regions;r++) if (econ->region[r].owner==cid) s+=econ->region[r].stock[g];
    return s;
}

void missions_init(MissionsState *ms){ memset(ms,0,sizeof *ms); }

/* ---- Émission contextuelle ------------------------------------------- */
static Mission mission_roll(const World *w, WorldEconomy *econ, const TechState *ts,
                            int cid, int year){
    Mission m; memset(&m,0,sizeof m);
    m.issued_year=year; m.active=true; m.done=false;
    int cr=capital_region(w,econ,cid);
    int decade=year/10;
    MissionKind kind = (MissionKind)(MIS_BUILD + (decade%3));   /* tourne BÂTIR/CHAÎNE/TECH */
    m.kind=kind;
    if (kind==MIS_BUILD){
        float fw[FAC_COUNT]; EthosFaction dom=country_faction_weights(w,econ,cid,fw);
        m.coord=coord_for_faction(dom);
        float cur = (cr>=0)?build_coord(&econ->region[cr].build, m.coord):0.f;
        m.threshold = cur + 2.0f;                          /* deux paliers de plus */
        m.reward_gold = 320.f; m.reward_mat=RES_METAL; m.reward_qty=60.f;
        snprintf(m.text,sizeof m.text, "Ériger %s (atteindre %.0f)", coord_word(m.coord), m.threshold);
    } else if (kind==MIS_CHAIN){
        m.good = RES_TOOLS;                                /* la chaîne d'outils (multiplicateur) */
        float cur = country_stock(econ,cid,m.good);
        m.threshold = cur + 80.f;
        m.reward_gold = 280.f; m.reward_mat=RES_WOOD; m.reward_qty=120.f;
        snprintf(m.text,sizeof m.text, "Renforcer la chaîne d'outils (stock %.0f)", m.threshold);
    } else { /* MIS_TECH */
        float cur = (cid<SCPS_MAX_COUNTRY)?(float)ts[cid].n_unlocked:0.f;
        m.threshold = cur + 3.f;                           /* percer trois technologies de plus */
        m.reward_gold = 360.f; m.reward_mat=RES_METAL; m.reward_qty=40.f;
        snprintf(m.text,sizeof m.text, "Percer la connaissance (atteindre %.0f techs)", m.threshold);
    }
    return m;
}

static bool mission_check(const World *w, WorldEconomy *econ, const TechState *ts,
                          int cid, const Mission *m){
    if (m->kind==MIS_BUILD){
        int cr=capital_region(w,econ,cid);
        return (cr>=0) && build_coord(&econ->region[cr].build, m->coord) >= m->threshold;
    } else if (m->kind==MIS_CHAIN){
        return country_stock(econ,cid,m->good) >= m->threshold;
    } else {
        return (cid<SCPS_MAX_COUNTRY) && (float)ts[cid].n_unlocked >= m->threshold;
    }
}

static void mission_grant(WorldEconomy *econ, int cid, const Mission *m){
    int cr=-1; for (int r=0;r<econ->n_regions;r++) if (econ->region[r].owner==cid){ cr=r; break; }
    if (cr<0) return;
    econ->region[cr].treasury += m->reward_gold;                 /* or au trésor */
    if (m->reward_mat>RES_NONE && m->reward_mat<RES_COUNT)
        econ->region[cr].stock[m->reward_mat] += m->reward_qty;  /* matières au marché */
}

void missions_tick(MissionsState *ms, const World *w, WorldEconomy *econ,
                   const TechState *ts, int year){
    for (int c=0;c<w->n_countries && c<SCPS_MISSIONS_MAX;c++){
        if (w->country[c].role==POLITY_UNCLAIMED || !has_regions(econ,c)) continue;
        Mission *m=&ms->m[c];
        if (year%10==0 && (!m->active || m->issued_year!=year))   /* nouvelle décennie : mission fraîche */
            *m = mission_roll(w,econ,ts,c,year);
        if (m->active && !m->done && mission_check(w,econ,ts,c,m)){
            m->done=true; mission_grant(econ,c,m);                /* accomplie → récompense */
        }
    }
}

const Mission *mission_of(const MissionsState *ms, int cid){
    if (cid<0||cid>=SCPS_MISSIONS_MAX) return NULL;
    return ms->m[cid].active ? &ms->m[cid] : NULL;
}
