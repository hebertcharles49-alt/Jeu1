/*
 * scps_modifier.c — pile de modificateurs persistants (voir scps_modifier.h)
 *
 * Autonome : ne dépend que de son en-tête. Agrégation linéaire O(n·pays) ;
 * suffisant pour SCPS_MAX_MODIFIERS=1024. Le retrait (expiration, rupture) se
 * fait par swap-remove côté appelant (sync_maintain).
 */
#include "scps_modifier.h"
#include <string.h>

bool modstack_push(ModifierStack *ms, Modifier m) {
    if (ms->n >= SCPS_MAX_MODIFIERS) return false;  /* pile pleine : à journaliser */
    ms->items[ms->n++] = m;
    return true;
}

ModAccum modstack_accumulate(const ModifierStack *ms, int country) {
    ModAccum a = {0};
    for (int i = 0; i < ms->n; i++) {
        const Modifier *m = &ms->items[i];
        if (m->country != country) continue;
        /* entrées */
        a.K += m->dK; a.L += m->dL; a.P += m->dP; a.F += m->dF;
        a.I += m->dI; a.H += m->dH;
        a.Mil += m->dMil; a.Mag += m->dMag; a.CF += m->dCF; a.Div += m->dDiv;
        /* sorties */
        a.PE += m->dPE; a.SI += m->dSI; a.PE_route += m->dPE_per_route;
    }
    return a;
}

/* ---- Dérive culturelle par groupe (§5) -------------------------------- */
GroupDrift modstack_group_drift(const ModifierStack *ms, int group_key) {
    GroupDrift d = {0,0,0,0,0};
    for (int i = 0; i < ms->n; i++) {
        const Modifier *m = &ms->items[i];
        if (m->group_key != group_key) continue;
        d.dCv += m->dCv; d.dCs += m->dCs; d.dCp += m->dCp; d.dCr += m->dCr; d.dAgit += m->dAgit;
    }
    return d;
}
void modstack_accumulate_drift(ModifierStack *ms, int group_key, GroupDrift step, bool reversible) {
    for (int i = 0; i < ms->n; i++) {
        Modifier *m = &ms->items[i];
        if (m->group_key == group_key && m->reversible == reversible) {
            m->dCv += step.dCv; m->dCs += step.dCs; m->dCp += step.dCp;
            m->dCr += step.dCr; m->dAgit += step.dAgit;
            return;
        }
    }
    Modifier m; memset(&m, 0, sizeof m);
    m.group_key = group_key; m.reversible = reversible; m.expires_tick = -1; m.country = -1;
    m.dCv = step.dCv; m.dCs = step.dCs; m.dCp = step.dCp; m.dCr = step.dCr; m.dAgit = step.dAgit;
    modstack_push(ms, m);
}
void modstack_drop_reversible(ModifierStack *ms, int group_key) {
    for (int i = ms->n - 1; i >= 0; i--) {
        if (ms->items[i].group_key == group_key && ms->items[i].reversible)
            ms->items[i] = ms->items[--ms->n];   /* swap-remove */
    }
}
