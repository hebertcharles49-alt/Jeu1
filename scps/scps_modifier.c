/*
 * scps_modifier.c — pile de modificateurs persistants (voir scps_modifier.h)
 *
 * Autonome : ne dépend que de son en-tête. Agrégation linéaire O(n·pays) ;
 * suffisant pour SCPS_MAX_MODIFIERS=1024. Le retrait (expiration, rupture) se
 * fait par swap-remove côté appelant (sync_maintain).
 */
#include "scps_modifier.h"

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
