/*
 * scps_factions.c — LES FACTIONS PAR ÉTHOS (passe 1/N)
 *
 * Le spectre (six factions = axes IA + Communautaire) et son enracinement dans
 * les groupes culturels. Aucune mutation du moteur ici : on LIT les groupes et
 * on en tire un profil de factions. Les passes suivantes feront agir ce profil.
 */
#include "scps_factions.h"
#include "scps_species.h"   /* SpeciesArchetype */

const char *faction_name(EthosFaction f){
    static const char *N[FAC_COUNT] = {
        "Conquérants", "Marchands", "Légistes", "Gardiens", "Transgresseurs", "Communautaires"
    };
    return (f>=0 && f<FAC_COUNT) ? N[f] : "?";
}

float class_clout(SocialClass k){
    /* Qui gouverne compte : l'élite pèse bien plus que la masse laborieuse. */
    switch (k){
        case CLASS_ELITE:    return 3.0f;
        case CLASS_BOURGEOIS:return 1.6f;
        default:             return 1.0f;   /* CLASS_LABORER */
    }
}

void group_ethos_lean(const PopCulture *c, float w[FAC_COUNT]){
    for (int f=0; f<FAC_COUNT; f++) w[f]=0.f;
    if (!c){ w[FAC_COMMUNAUTAIRE]=1.f; return; }

    /* 1) SOCLE — l'éthos de la culture donne la direction première. */
    switch (c->ethos){
        case ETHOS_DOMINATEUR:  w[FAC_CONQUERANT]+=1.0f; w[FAC_TRANSGRESSEUR]+=0.3f; break;
        case ETHOS_HONNEUR:     w[FAC_CONQUERANT]+=0.9f; w[FAC_GARDIEN]+=0.2f;       break; /* gloire + code */
        case ETHOS_ORDRE:       w[FAC_GARDIEN]+=0.7f;    w[FAC_LEGISTE]+=0.4f;       break; /* hiérarchie, tradition */
        case ETHOS_BUREAUCRATE: w[FAC_LEGISTE]+=1.0f;    break;
        case ETHOS_MERCANTILE:  w[FAC_MARCHAND]+=1.0f;   break;
        case ETHOS_PACIFISTE:   w[FAC_COMMUNAUTAIRE]+=1.0f; break;
        default: break;
    }
    /* 2) SIGNATURE de race — le penchant inné du peuple (§2). */
    switch (c->race){
        case RACE_ORQUE:    w[FAC_CONQUERANT]+=0.5f;    w[FAC_TRANSGRESSEUR]+=0.5f; break; /* guerre + interdit */
        case RACE_NAIN:     w[FAC_LEGISTE]+=0.4f;       w[FAC_TRANSGRESSEUR]+=0.4f; break; /* forge à runes */
        case RACE_HALFELIN: w[FAC_MARCHAND]+=0.4f;      w[FAC_COMMUNAUTAIRE]+=0.5f; break;
        case RACE_GNOME:    w[FAC_MARCHAND]+=0.5f;      w[FAC_COMMUNAUTAIRE]+=0.3f; break; /* négoce, bien commun */
        case RACE_ELFE:     w[FAC_TRANSGRESSEUR]+=0.4f; w[FAC_GARDIEN]+=0.3f;       break; /* arcane + tradition */
        case RACE_HUMAIN:   w[FAC_MARCHAND]+=0.2f;      w[FAC_LEGISTE]+=0.2f;       break; /* l'intégrateur */
        default: break;
    }
    /* 3) CREDO — la ferveur nourrit les Gardiens ; la tolérance, l'ouverture. */
    if      (c->credo==CREDO_PURIFICATEUR) w[FAC_GARDIEN]+=0.7f;
    else if (c->credo==CREDO_EVANGELISTE)  w[FAC_GARDIEN]+=0.4f;
    else { w[FAC_MARCHAND]+=0.15f; w[FAC_COMMUNAUTAIRE]+=0.15f; }   /* pluraliste : tolère, s'ouvre */

    /* normalise → un PROFIL de penchants (Σ=1). */
    float s=0.f; for (int f=0;f<FAC_COUNT;f++) s+=w[f];
    if (s>0.f) for (int f=0;f<FAC_COUNT;f++) w[f]/=s;
    else w[FAC_COMMUNAUTAIRE]=1.f;
}

/* ---- Agrégation : Σ groupes (pop × class_clout × penchant) ------------- */
static void accumulate(const ProvincePop *pp, double acc[FAC_COUNT]){
    for (int i=0; i<pp->n_groups; i++){
        const PopGroup *g=&pp->groups[i];
        if (g->count<=0) continue;
        float lean[FAC_COUNT]; group_ethos_lean(&g->culture, lean);
        double wgt = (double)g->count * (double)class_clout(g->klass);
        for (int f=0; f<FAC_COUNT; f++) acc[f] += wgt * lean[f];
    }
}

static EthosFaction finalize(double acc[FAC_COUNT], float out[FAC_COUNT]){
    double s=0.0; for (int f=0;f<FAC_COUNT;f++) s+=acc[f];
    int dom=FAC_COMMUNAUTAIRE; double best=-1.0;
    for (int f=0; f<FAC_COUNT; f++){
        out[f] = (s>0.0) ? (float)(acc[f]/s) : (f==FAC_COMMUNAUTAIRE?1.f:0.f);
        if (out[f] > best){ best=out[f]; dom=f; }
    }
    return (EthosFaction)dom;
}

EthosFaction faction_weights_of(const ProvincePop *provs, int n, float out[FAC_COUNT]){
    double acc[FAC_COUNT]={0};
    for (int p=0; p<n; p++) accumulate(&provs[p], acc);
    return finalize(acc, out);
}

EthosFaction country_faction_weights(const World *w, const WorldEconomy *econ, int cid,
                                     float out[FAC_COUNT]){
    double acc[FAC_COUNT]={0};
    if (cid>=0 && econ){
        for (int r=0; r<econ->n_regions; r++)
            if (econ->region[r].owner==cid && econ->region[r].culture.settled)
                accumulate(&econ->region[r].pop, acc);
    }
    (void)w;
    return finalize(acc, out);
}
