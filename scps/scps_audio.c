/*
 * scps_audio.c — mixeur minimal de voix procédurales sur miniaudio (voir .h).
 * La logique de mixage (audio_mix) est PARTAGÉE entre le callback du device et
 * le banc hors-device → la preuve de vie se vérifie sans carte son.
 */
#include "scps_audio.h"
#define MA_NO_DECODING
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_NODE_GRAPH
#include "miniaudio.h"
#include <math.h>
#include <string.h>

#define AUD_RATE   48000
#define AUD_VOICES 16
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

typedef struct {
    bool   active;
    float  phase;       /* radians */
    float  dphase;      /* radians/sample */
    float  gain;
    int    left;        /* samples restants */
    int    total;       /* pour l'enveloppe */
} Voice;

static Voice       g_voice[AUD_VOICES];
static ma_device   g_device;
static ma_mutex    g_mutex;
static bool        g_have_device = false;
static bool        g_have_mutex  = false;

/* Mixe `frames` (stéréo entrelacé) dans `out` à `rate` Hz. Le cœur partagé. */
static void audio_mix(float *out, ma_uint32 frames, int rate){
    memset(out, 0, sizeof(float)*frames*2);
    for (int v=0; v<AUD_VOICES; v++){
        Voice *vo=&g_voice[v];
        if (!vo->active) continue;
        float dphase = vo->dphase * (48000.f/(float)rate);   /* re-cale si rate ≠ 48k */
        for (ma_uint32 i=0; i<frames; i++){
            if (vo->left<=0){ vo->active=false; break; }
            float env = (vo->total>0) ? (float)vo->left/(float)vo->total : 0.f;  /* décroissance linéaire */
            float s = sinf(vo->phase) * vo->gain * env;
            out[i*2+0] += s;
            out[i*2+1] += s;
            vo->phase += dphase;
            if (vo->phase > 2.f*(float)M_PI) vo->phase -= 2.f*(float)M_PI;
            vo->left--;
        }
    }
    /* garde-fou anti-saturation (somme de voix) : clamp doux. */
    for (ma_uint32 i=0; i<frames*2; i++){
        if (out[i] >  1.f) out[i]= 1.f;
        else if (out[i] < -1.f) out[i]=-1.f;
    }
}

static void data_callback(ma_device *dev, void *out, const void *in, ma_uint32 frames){
    (void)dev; (void)in;
    if (g_have_mutex) ma_mutex_lock(&g_mutex);
    audio_mix((float*)out, frames, AUD_RATE);
    if (g_have_mutex) ma_mutex_unlock(&g_mutex);
}

bool audio_init(void){
    memset(g_voice, 0, sizeof g_voice);
    ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
    cfg.playback.format   = ma_format_f32;
    cfg.playback.channels = 2;
    cfg.sampleRate        = AUD_RATE;
    cfg.dataCallback      = data_callback;
    if (ma_device_init(NULL, &cfg, &g_device) != MA_SUCCESS)
        return false;                                  /* pas de device → muet, pas d'erreur */
    if (ma_mutex_init(&g_mutex) == MA_SUCCESS) g_have_mutex = true;
    if (ma_device_start(&g_device) != MA_SUCCESS){
        ma_device_uninit(&g_device);
        if (g_have_mutex){ ma_mutex_uninit(&g_mutex); g_have_mutex=false; }
        return false;
    }
    g_have_device = true;
    return true;
}

void audio_shutdown(void){
    if (g_have_device){ ma_device_uninit(&g_device); g_have_device=false; }
    if (g_have_mutex){ ma_mutex_uninit(&g_mutex); g_have_mutex=false; }
}

/* Pose une voix dans un slot libre (le plus avancé sinon). Verrou si dispo. */
static void voice_set(float freq, float dur, float gain){
    if (g_have_mutex) ma_mutex_lock(&g_mutex);
    int slot=-1;
    for (int v=0; v<AUD_VOICES; v++) if (!g_voice[v].active){ slot=v; break; }
    if (slot<0){ int best=0; for (int v=1;v<AUD_VOICES;v++) if (g_voice[v].left<g_voice[best].left) best=v; slot=best; }
    Voice *vo=&g_voice[slot];
    vo->phase=0.f;
    vo->dphase=2.f*(float)M_PI*freq/(float)AUD_RATE;
    vo->gain=(gain<0.f)?0.f:(gain>1.f?1.f:gain);
    vo->total=vo->left=(int)(dur*(float)AUD_RATE);
    if (vo->left<1) vo->left=1;
    vo->active=true;
    if (g_have_mutex) ma_mutex_unlock(&g_mutex);
}

void audio_blip(float freq, float dur, float gain){
    voice_set(freq, dur, gain);
}

void audio_alert(void){
    /* deux tons brefs, discrets (quinte descendante). */
    voice_set(660.f, 0.12f, 0.22f);
    voice_set(440.f, 0.18f, 0.20f);
}

void audio_render_test(float *out, int frames, int rate){
    if (g_have_mutex) ma_mutex_lock(&g_mutex);
    audio_mix(out, (ma_uint32)frames, rate);
    if (g_have_mutex) ma_mutex_unlock(&g_mutex);
}
