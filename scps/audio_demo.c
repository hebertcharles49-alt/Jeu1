/*
 * audio_demo.c — banc auto-vérifiant : le mixeur SORT du son (brief §9.6),
 * sans dépendre d'une carte. On NE démarre PAS le device (headless) : on pose
 * des voix et on rend des frames hors-device, puis on mesure le RMS.
 */
#include "scps_audio.h"
#include <stdio.h>
#include <math.h>

static int pass=0, fail=0;
static void check(const char *w, int ok){ printf("   %s %s\n", ok?"✓":"✗", w); if(ok)pass++; else fail++; }

static double rms(const float *b, int n){ double s=0; for(int i=0;i<n;i++) s+=(double)b[i]*b[i]; return sqrt(s/n); }

int main(void){
    printf("══ audio : le mixeur procédural sort du son (hors device) ══\n");
    enum { F=2048 };
    float buf[F*2];

    /* Silence au départ : aucune voix. */
    audio_render_test(buf, F, 48000);
    check("silence quand aucune voix n'est posée", rms(buf,F*2) < 1e-6);

    /* Une voix → du son (RMS franc). */
    audio_blip(440.f, 0.5f, 0.5f);
    audio_render_test(buf, F, 48000);
    double r1 = rms(buf, F*2);
    check("une voix posée → le mixeur SORT du son (RMS > 0)", r1 > 0.01);
    check("le signal reste borné [-1,1]", buf[0]>=-1.f && buf[0]<=1.f);

    /* L'alerte (deux tons) sort aussi. */
    audio_alert();
    audio_render_test(buf, F, 48000);
    check("l'alerte discrète produit du son", rms(buf,F*2) > 0.01);

    /* Les voix s'éteignent : après assez de frames, retour au silence. */
    for (int k=0;k<40;k++) audio_render_test(buf, F, 48000);   /* draine ~1.7 s */
    audio_render_test(buf, F, 48000);
    check("les voix s'amortissent jusqu'au silence", rms(buf,F*2) < 1e-6);

    printf("══ BILAN : %d réussis, %d échoués ══\n", pass, fail);
    return fail ? 1 : 0;
}
