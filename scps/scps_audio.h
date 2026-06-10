#ifndef SCPS_AUDIO_H
#define SCPS_AUDIO_H
/*
 * scps_audio.h — LA PRISE DE COURANT (brief build §5)
 *
 * On pose la PRISE, pas l'orchestre : un device de lecture (miniaudio vendoré),
 * un mixeur minimal de N voix procédurales (sinus amorti, générées côté jeu —
 * zéro fichier, zéro asset, conforme à la doctrine), et une preuve de vie.
 *
 * Le design sonore N'EST PAS ce module. Tout sample futur sera GÉNÉRÉ.
 *
 * Dégradation gracieuse : sans device (serveur, conteneur headless), audio_init
 * renvoie false et tout le reste devient muet SANS erreur — le release tourne.
 * N'est lié qu'au VIEWER (jamais au chronicle : l'instrumentation reste muette
 * et déterministe).
 */
#include <stdbool.h>

/* Ouvre le device. false si aucun (muet, pas une erreur). */
bool audio_init(void);
void audio_shutdown(void);

/* Joue une voix procédurale : un sinus de `freq` Hz, `dur` secondes, amplitude
 * `gain` [0..1], amorti. Sans device : sans effet. Thread-safe (le mixeur tourne
 * sur le thread audio). */
void audio_blip(float freq, float dur, float gain);

/* Une « alerte » discrète prête à l'emploi (deux tons brefs). */
void audio_alert(void);

/* VÉRIFICATION (§9.6) hors device : mixe l'état courant des voix dans `out`
 * (frames × 2 canaux entrelacés, à `rate` Hz) — sert au banc à prouver que le
 * mixeur SORT du son (RMS non nul) même sans carte. Avance les voix. */
void audio_render_test(float *out, int frames, int rate);

#endif /* SCPS_AUDIO_H */
