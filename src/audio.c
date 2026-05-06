/*
 * audio.c - sons procéduraux générés au runtime, joués via SDL_QueueAudio
 * Pass de polish : bcp plus de sinus/triangles, moins de carre/bruit,
 * envelopes adoucies pour limiter la fatigue auditive.
 */
#include "game.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define SR 22050

static int16_t *g_sfx_data[SFX_COUNT];
static int      g_sfx_len[SFX_COUNT];

static int16_t *alloc_buf(int samples) {
    return (int16_t *)calloc(samples, sizeof(int16_t));
}

static float frand(void) { return (rand() / (float)RAND_MAX) * 2.f - 1.f; }

/* envelope ADSR-light, attack et release smooth */
static void env_apply(int16_t *buf, int n, float attack, float release) {
    int a = (int)(attack * SR);
    int r = (int)(release * SR);
    if (a < 1) a = 1;
    if (r < 1) r = 1;
    for (int i = 0; i < n; i++) {
        float e = 1.f;
        if (i < a) {
            float t = i / (float)a;
            e = t * t * (3.f - 2.f * t);   /* smoothstep attack */
        }
        if (i > n - r) {
            float t = (n - i) / (float)r;
            e *= t * t;                     /* quadratic release */
        }
        if (e < 0) e = 0;
        int v = (int)(buf[i] * e);
        if (v >  32767) v =  32767;
        if (v < -32768) v = -32768;
        buf[i] = (int16_t)v;
    }
}

static void mix_sine(int16_t *buf, int n, float freq, float amp, float pitch_decay) {
    for (int i = 0; i < n; i++) {
        float t = i / (float)SR;
        float f = freq * (1.f - pitch_decay * t);
        if (f < 20.f) f = 20.f;
        float s = sinf(t * f * 6.2831f);
        int v = buf[i] + (int)(s * amp * 30000);
        if (v >  32767) v =  32767;
        if (v < -32768) v = -32768;
        buf[i] = (int16_t)v;
    }
}

static void mix_tri(int16_t *buf, int n, float freq, float amp, float pitch_decay) {
    float phase = 0.f;
    for (int i = 0; i < n; i++) {
        float t = i / (float)SR;
        float f = freq * (1.f - pitch_decay * t);
        if (f < 20.f) f = 20.f;
        phase += f / (float)SR;
        if (phase >= 1.f) phase -= 1.f;
        float s = phase < 0.5f ? (phase * 4.f - 1.f) : (3.f - phase * 4.f);
        int v = buf[i] + (int)(s * amp * 28000);
        if (v >  32767) v =  32767;
        if (v < -32768) v = -32768;
        buf[i] = (int16_t)v;
    }
}

static void mix_noise(int16_t *buf, int n, float amp) {
    for (int i = 0; i < n; i++) {
        int v = buf[i] + (int)(frand() * amp * 20000);
        if (v >  32767) v =  32767;
        if (v < -32768) v = -32768;
        buf[i] = (int16_t)v;
    }
}

static void low_pass(int16_t *buf, int n, float k) {
    float prev = 0;
    for (int i = 0; i < n; i++) {
        prev += k * (buf[i] - prev);
        buf[i] = (int16_t)prev;
    }
}

static void make_punch(int idx) {
    int n = (int)(0.13f * SR);
    int16_t *b = alloc_buf(n);
    mix_tri(b, n, 110.f, 0.55f, 1.5f);
    mix_noise(b, n, 0.18f);
    low_pass(b, n, 0.20f);
    env_apply(b, n, 0.005f, 0.10f);
    g_sfx_data[idx] = b; g_sfx_len[idx] = n;
}

static void make_hit(int idx) {
    int n = (int)(0.10f * SR);
    int16_t *b = alloc_buf(n);
    /* corps : sinus de mid-low decroissant en pitch */
    mix_sine(b, n, 480.f, 0.55f, 2.5f);
    mix_tri (b, n, 240.f, 0.30f, 2.0f);
    mix_noise(b, n, 0.06f);
    low_pass(b, n, 0.40f);
    env_apply(b, n, 0.003f, 0.08f);
    g_sfx_data[idx] = b; g_sfx_len[idx] = n;
}

static void make_heavy(int idx) {
    int n = (int)(0.22f * SR);
    int16_t *b = alloc_buf(n);
    mix_tri (b, n,  80.f, 0.65f, 0.8f);
    mix_sine(b, n, 160.f, 0.30f, 1.0f);
    mix_noise(b, n, 0.10f);
    low_pass(b, n, 0.18f);
    env_apply(b, n, 0.004f, 0.16f);
    g_sfx_data[idx] = b; g_sfx_len[idx] = n;
}

static void make_swing(int idx) {
    /* whoosh : bruit lowpass tres bref */
    int n = (int)(0.08f * SR);
    int16_t *b = alloc_buf(n);
    mix_noise(b, n, 0.30f);
    /* sweep filter manually */
    float k = 0.05f;
    float prev = 0;
    for (int i = 0; i < n; i++) {
        k = 0.05f + (i / (float)n) * 0.20f;
        prev += k * (b[i] - prev);
        b[i] = (int16_t)prev;
    }
    env_apply(b, n, 0.010f, 0.05f);
    g_sfx_data[idx] = b; g_sfx_len[idx] = n;
}

static void make_explode(int idx) {
    int n = (int)(0.32f * SR);
    int16_t *b = alloc_buf(n);
    /* basse profonde + bruit filtré */
    mix_tri (b, n,  60.f, 0.60f, 0.5f);
    mix_sine(b, n, 100.f, 0.25f, 0.8f);
    mix_noise(b, n, 0.30f);
    low_pass(b, n, 0.10f);
    env_apply(b, n, 0.004f, 0.22f);
    g_sfx_data[idx] = b; g_sfx_len[idx] = n;
}

static void make_pickup(int idx) {
    int n = (int)(0.12f * SR);
    int16_t *b = alloc_buf(n);
    /* sinus monte doucement */
    for (int i = 0; i < n; i++) {
        float t = i / (float)SR;
        float f = 700.f + t * 900.f;
        float s = sinf(t * f * 6.2831f);
        int v = (int)(s * 14000);
        b[i] = (int16_t)v;
    }
    env_apply(b, n, 0.005f, 0.08f);
    g_sfx_data[idx] = b; g_sfx_len[idx] = n;
}

static void make_coin(int idx) {
    int n = (int)(0.18f * SR);
    int16_t *b = alloc_buf(n);
    /* deux notes sinus, harmoniques cristallines */
    mix_sine(b, n / 3, 1320.f, 0.30f, 0.f);
    mix_sine(b + n / 3, 2 * n / 3, 1980.f, 0.30f, 0.f);
    /* leger overtone */
    mix_sine(b, n / 3, 2640.f, 0.10f, 0.f);
    env_apply(b, n, 0.004f, 0.14f);
    g_sfx_data[idx] = b; g_sfx_len[idx] = n;
}

static void make_levelup(int idx) {
    int n = (int)(0.55f * SR);
    int16_t *b = alloc_buf(n);
    int seg = n / 4;
    float notes[4] = { 523.f, 659.f, 784.f, 1046.f };
    for (int i = 0; i < 4; i++) {
        mix_sine(b + i * seg, seg, notes[i], 0.32f, 0.f);
        mix_sine(b + i * seg, seg, notes[i] * 2.f, 0.10f, 0.f);
    }
    env_apply(b, n, 0.010f, 0.18f);
    g_sfx_data[idx] = b; g_sfx_len[idx] = n;
}

static void make_player_hurt(int idx) {
    int n = (int)(0.22f * SR);
    int16_t *b = alloc_buf(n);
    /* triangle descendant, plus chaud que saw */
    mix_tri (b, n, 230.f, 0.45f, 0.6f);
    mix_sine(b, n, 460.f, 0.18f, 0.7f);
    mix_noise(b, n, 0.07f);
    low_pass(b, n, 0.30f);
    env_apply(b, n, 0.005f, 0.16f);
    g_sfx_data[idx] = b; g_sfx_len[idx] = n;
}

static void make_death(int idx) {
    int n = (int)(0.70f * SR);
    int16_t *b = alloc_buf(n);
    mix_tri (b, n, 280.f, 0.55f, 0.7f);
    mix_sine(b, n, 140.f, 0.40f, 0.5f);
    mix_noise(b, n, 0.10f);
    low_pass(b, n, 0.18f);
    env_apply(b, n, 0.010f, 0.40f);
    g_sfx_data[idx] = b; g_sfx_len[idx] = n;
}

static void make_boss(int idx) {
    int n = (int)(0.95f * SR);
    int16_t *b = alloc_buf(n);
    /* drone bas montant, rappelle Hades */
    mix_sine(b, n,  70.f, 0.55f, -0.4f);
    mix_tri (b, n,  55.f, 0.30f, -0.2f);
    mix_sine(b, n, 110.f, 0.20f, -0.4f);
    low_pass(b, n, 0.10f);
    env_apply(b, n, 0.06f, 0.40f);
    g_sfx_data[idx] = b; g_sfx_len[idx] = n;
}

static void make_portal(int idx) {
    int n = (int)(0.50f * SR);
    int16_t *b = alloc_buf(n);
    /* sinus modulé par sinus lent (glissando ethere) */
    for (int i = 0; i < n; i++) {
        float t = i / (float)SR;
        float f = 540.f + sinf(t * 8.f) * 180.f;
        float s = sinf(t * f * 6.2831f);
        s += 0.3f * sinf(t * f * 2.f * 6.2831f);
        int v = (int)(s * 9000);
        b[i] = (int16_t)v;
    }
    env_apply(b, n, 0.05f, 0.25f);
    g_sfx_data[idx] = b; g_sfx_len[idx] = n;
}

static void make_shoot(int idx) {
    int n = (int)(0.10f * SR);
    int16_t *b = alloc_buf(n);
    /* ton clair chute pitch, sinus pas square */
    mix_sine(b, n, 980.f, 0.40f, 5.f);
    mix_tri (b, n, 490.f, 0.20f, 4.f);
    mix_noise(b, n, 0.06f);
    env_apply(b, n, 0.003f, 0.08f);
    g_sfx_data[idx] = b; g_sfx_len[idx] = n;
}

static void make_zap(int idx) {
    int n = (int)(0.14f * SR);
    int16_t *b = alloc_buf(n);
    /* eclair : sinus haut + harmonique, modulation fast */
    for (int i = 0; i < n; i++) {
        float t = i / (float)SR;
        float f = 1300.f * (1.f - t * 5.f);
        if (f < 200.f) f = 200.f;
        float s = sinf(t * f * 6.2831f);
        s += 0.4f * sinf(t * f * 2.f * 6.2831f);
        int v = (int)(s * 12000);
        b[i] = (int16_t)v;
    }
    mix_noise(b, n, 0.04f);
    env_apply(b, n, 0.002f, 0.10f);
    g_sfx_data[idx] = b; g_sfx_len[idx] = n;
}

static void make_fuse(int idx) {
    /* fusion : long arpege harmonique */
    int n = (int)(0.85f * SR);
    int16_t *b = alloc_buf(n);
    int seg = n / 5;
    float notes[5] = { 392.f, 523.f, 659.f, 880.f, 1175.f };
    for (int i = 0; i < 5; i++) {
        mix_sine(b + i * seg, seg, notes[i], 0.28f, 0.f);
        mix_sine(b + i * seg, seg, notes[i] * 1.5f, 0.10f, 0.f);
    }
    env_apply(b, n, 0.008f, 0.25f);
    g_sfx_data[idx] = b; g_sfx_len[idx] = n;
}

void audio_init(Game *g) {
    SDL_AudioSpec want = {0}, got;
    want.freq     = SR;
    want.format   = AUDIO_S16SYS;
    want.channels = 1;
    want.samples  = 1024;
    want.callback = NULL;
    g->audio_dev = SDL_OpenAudioDevice(NULL, 0, &want, &got, 0);
    if (g->audio_dev == 0) return;
    g->audio_sample_rate = got.freq;
    SDL_PauseAudioDevice(g->audio_dev, 0);

    make_punch(SFX_PUNCH);
    make_hit(SFX_HIT);
    make_heavy(SFX_HEAVY_HIT);
    make_swing(SFX_SWING);
    make_explode(SFX_EXPLODE);
    make_pickup(SFX_PICKUP);
    make_coin(SFX_COIN);
    make_levelup(SFX_LEVELUP);
    make_player_hurt(SFX_PLAYER_HURT);
    make_death(SFX_DEATH);
    make_boss(SFX_BOSS);
    make_portal(SFX_PORTAL);
    make_shoot(SFX_SHOOT);
    make_zap(SFX_ZAP);
    make_fuse(SFX_FUSE);
}

void audio_shutdown(Game *g) {
    if (g->audio_dev) SDL_CloseAudioDevice(g->audio_dev);
    for (int i = 0; i < SFX_COUNT; i++) {
        if (g_sfx_data[i]) { free(g_sfx_data[i]); g_sfx_data[i] = NULL; }
    }
}

void sfx_play(Game *g, SfxId id) {
    if (!g->audio_dev) return;
    if (id < 0 || id >= SFX_COUNT) return;
    if (!g_sfx_data[id]) return;
    if (g->settings.sfx_mute) return;
    int vol = g->settings.sfx_volume;
    if (vol <= 0) return;
    Uint32 queued = SDL_GetQueuedAudioSize(g->audio_dev);
    if (queued > (Uint32)(SR * 4)) return;
    if (vol >= 4) {
        SDL_QueueAudio(g->audio_dev, g_sfx_data[id], g_sfx_len[id] * sizeof(int16_t));
    } else {
        int n = g_sfx_len[id];
        int16_t *tmp = (int16_t *)malloc((size_t)n * sizeof(int16_t));
        if (!tmp) return;
        int num = vol, den = 4;
        for (int i = 0; i < n; i++) tmp[i] = (int16_t)((int)g_sfx_data[id][i] * num / den);
        SDL_QueueAudio(g->audio_dev, tmp, (Uint32)(n * sizeof(int16_t)));
        free(tmp);
    }
}
