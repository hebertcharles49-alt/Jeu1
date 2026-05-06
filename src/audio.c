/*
 * audio.c - sons procéduraux générés au runtime, joués via SDL_QueueAudio
 */
#include "game.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define SR 22050

static int16_t *g_sfx_data[SFX_COUNT];
static int      g_sfx_len[SFX_COUNT];   /* in samples */

/* ---- generators ---- */
static int16_t *alloc_buf(int samples) {
    int16_t *b = (int16_t *)calloc(samples, sizeof(int16_t));
    return b;
}

static float frand(void) { return (rand() / (float)RAND_MAX) * 2.f - 1.f; }

static void env_apply(int16_t *buf, int n, float attack, float release) {
    int a = (int)(attack * SR);
    int r = (int)(release * SR);
    for (int i = 0; i < n; i++) {
        float e = 1.f;
        if (i < a) e = i / (float)(a + 1);
        if (i > n - r) e *= (n - i) / (float)(r + 1);
        if (e < 0) e = 0;
        int v = (int)(buf[i] * e);
        if (v >  32767) v =  32767;
        if (v < -32768) v = -32768;
        buf[i] = (int16_t)v;
    }
}

static void mix_square(int16_t *buf, int n, float freq, float amp, float pitch_decay) {
    float phase = 0.f;
    for (int i = 0; i < n; i++) {
        float t = i / (float)SR;
        float f = freq * (1.f - pitch_decay * t);
        if (f < 20.f) f = 20.f;
        phase += f / (float)SR;
        if (phase >= 1.f) phase -= 1.f;
        float s = (phase < 0.5f) ? 1.f : -1.f;
        int v = buf[i] + (int)(s * amp * 30000);
        if (v >  32767) v =  32767;
        if (v < -32768) v = -32768;
        buf[i] = (int16_t)v;
    }
}

static void mix_saw(int16_t *buf, int n, float freq, float amp, float pitch_decay) {
    float phase = 0.f;
    for (int i = 0; i < n; i++) {
        float t = i / (float)SR;
        float f = freq * (1.f - pitch_decay * t);
        if (f < 20.f) f = 20.f;
        phase += f / (float)SR;
        if (phase >= 1.f) phase -= 1.f;
        float s = phase * 2.f - 1.f;
        int v = buf[i] + (int)(s * amp * 30000);
        if (v >  32767) v =  32767;
        if (v < -32768) v = -32768;
        buf[i] = (int16_t)v;
    }
}

static void mix_sine(int16_t *buf, int n, float freq, float amp) {
    for (int i = 0; i < n; i++) {
        float t = i / (float)SR;
        float s = sinf(t * freq * 6.2831f);
        int v = buf[i] + (int)(s * amp * 30000);
        if (v >  32767) v =  32767;
        if (v < -32768) v = -32768;
        buf[i] = (int16_t)v;
    }
}

static void mix_noise(int16_t *buf, int n, float amp) {
    for (int i = 0; i < n; i++) {
        int v = buf[i] + (int)(frand() * amp * 30000);
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

/* ---- preset sounds ---- */
static void make_punch(int idx) {
    int n = (int)(0.10f * SR);
    int16_t *b = alloc_buf(n);
    mix_noise(b, n, 0.6f);
    mix_square(b, n, 200.f, 0.4f, 4.f);
    low_pass(b, n, 0.3f);
    env_apply(b, n, 0.002f, 0.07f);
    g_sfx_data[idx] = b; g_sfx_len[idx] = n;
}

static void make_hit(int idx) {
    int n = (int)(0.08f * SR);
    int16_t *b = alloc_buf(n);
    mix_square(b, n, 600.f, 0.5f, 5.f);
    mix_noise(b, n, 0.3f);
    env_apply(b, n, 0.001f, 0.06f);
    g_sfx_data[idx] = b; g_sfx_len[idx] = n;
}

static void make_heavy(int idx) {
    int n = (int)(0.18f * SR);
    int16_t *b = alloc_buf(n);
    mix_square(b, n, 90.f, 0.7f, 1.5f);
    mix_noise(b, n, 0.4f);
    low_pass(b, n, 0.2f);
    env_apply(b, n, 0.002f, 0.12f);
    g_sfx_data[idx] = b; g_sfx_len[idx] = n;
}

static void make_swing(int idx) {
    int n = (int)(0.07f * SR);
    int16_t *b = alloc_buf(n);
    mix_noise(b, n, 0.5f);
    low_pass(b, n, 0.15f);
    env_apply(b, n, 0.005f, 0.05f);
    g_sfx_data[idx] = b; g_sfx_len[idx] = n;
}

static void make_explode(int idx) {
    int n = (int)(0.30f * SR);
    int16_t *b = alloc_buf(n);
    mix_noise(b, n, 0.9f);
    mix_square(b, n, 60.f, 0.4f, 0.8f);
    low_pass(b, n, 0.1f);
    env_apply(b, n, 0.002f, 0.20f);
    g_sfx_data[idx] = b; g_sfx_len[idx] = n;
}

static void make_pickup(int idx) {
    int n = (int)(0.10f * SR);
    int16_t *b = alloc_buf(n);
    /* up sweep */
    for (int i = 0; i < n; i++) {
        float t = i / (float)SR;
        float f = 600.f + t * 1500.f;
        float s = sinf(t * f * 6.2831f);
        int v = (int)(s * 18000);
        b[i] = (int16_t)v;
    }
    env_apply(b, n, 0.002f, 0.06f);
    g_sfx_data[idx] = b; g_sfx_len[idx] = n;
}

static void make_coin(int idx) {
    int n = (int)(0.13f * SR);
    int16_t *b = alloc_buf(n);
    mix_sine(b, n / 3, 1320.f, 0.4f);
    mix_sine(b + n / 3, 2 * n / 3, 1760.f, 0.4f);
    env_apply(b, n, 0.001f, 0.10f);
    g_sfx_data[idx] = b; g_sfx_len[idx] = n;
}

static void make_levelup(int idx) {
    int n = (int)(0.45f * SR);
    int16_t *b = alloc_buf(n);
    int seg = n / 4;
    float notes[4] = { 523.f, 659.f, 784.f, 1046.f };
    for (int i = 0; i < 4; i++) mix_sine(b + i * seg, seg, notes[i], 0.4f);
    env_apply(b, n, 0.005f, 0.10f);
    g_sfx_data[idx] = b; g_sfx_len[idx] = n;
}

static void make_player_hurt(int idx) {
    int n = (int)(0.20f * SR);
    int16_t *b = alloc_buf(n);
    mix_saw(b, n, 220.f, 0.5f, 0.5f);
    mix_noise(b, n, 0.2f);
    low_pass(b, n, 0.25f);
    env_apply(b, n, 0.002f, 0.15f);
    g_sfx_data[idx] = b; g_sfx_len[idx] = n;
}

static void make_death(int idx) {
    int n = (int)(0.55f * SR);
    int16_t *b = alloc_buf(n);
    mix_saw(b, n, 320.f, 0.6f, 0.6f);
    mix_noise(b, n, 0.3f);
    low_pass(b, n, 0.15f);
    env_apply(b, n, 0.005f, 0.30f);
    g_sfx_data[idx] = b; g_sfx_len[idx] = n;
}

static void make_boss(int idx) {
    int n = (int)(0.80f * SR);
    int16_t *b = alloc_buf(n);
    mix_saw(b, n, 80.f, 0.7f, -0.4f);  /* rising */
    mix_square(b, n, 65.f, 0.4f, -0.2f);
    low_pass(b, n, 0.10f);
    env_apply(b, n, 0.05f, 0.30f);
    g_sfx_data[idx] = b; g_sfx_len[idx] = n;
}

static void make_portal(int idx) {
    int n = (int)(0.40f * SR);
    int16_t *b = alloc_buf(n);
    for (int i = 0; i < n; i++) {
        float t = i / (float)SR;
        float f = 400.f + sinf(t * 18.f) * 200.f;
        float s = sinf(t * f * 6.2831f);
        int v = (int)(s * 14000);
        b[i] = (int16_t)v;
    }
    env_apply(b, n, 0.05f, 0.20f);
    g_sfx_data[idx] = b; g_sfx_len[idx] = n;
}

static void make_shoot(int idx) {
    int n = (int)(0.08f * SR);
    int16_t *b = alloc_buf(n);
    mix_square(b, n, 900.f, 0.4f, 8.f);
    mix_noise(b, n, 0.15f);
    env_apply(b, n, 0.001f, 0.06f);
    g_sfx_data[idx] = b; g_sfx_len[idx] = n;
}

static void make_zap(int idx) {
    int n = (int)(0.12f * SR);
    int16_t *b = alloc_buf(n);
    mix_square(b, n, 1300.f, 0.45f, 6.f);
    mix_noise(b, n, 0.25f);
    env_apply(b, n, 0.001f, 0.08f);
    g_sfx_data[idx] = b; g_sfx_len[idx] = n;
}

void audio_init(Game *g) {
    SDL_AudioSpec want = {0}, got;
    want.freq     = SR;
    want.format   = AUDIO_S16SYS;
    want.channels = 1;
    want.samples  = 512;
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
    /* limit queue to avoid runaway */
    Uint32 queued = SDL_GetQueuedAudioSize(g->audio_dev);
    if (queued > (Uint32)(SR * 4)) return;
    if (vol >= 4) {
        SDL_QueueAudio(g->audio_dev, g_sfx_data[id], g_sfx_len[id] * sizeof(int16_t));
    } else {
        /* volume scaling : 1=25% 2=50% 3=75% */
        int n = g_sfx_len[id];
        int16_t *tmp = (int16_t *)malloc((size_t)n * sizeof(int16_t));
        if (!tmp) return;
        int num = vol, den = 4;
        for (int i = 0; i < n; i++) tmp[i] = (int16_t)((int)g_sfx_data[id][i] * num / den);
        SDL_QueueAudio(g->audio_dev, tmp, (Uint32)(n * sizeof(int16_t)));
        free(tmp);
    }
}
