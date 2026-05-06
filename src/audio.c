/*
 * audio.c - sons procéduraux générés au runtime.
 * Refait : amplitudes plus basses, sinus + harmoniques propres,
 * envelopes longues exponentielles. Beaucoup moins agressif.
 */
#include "game.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define SR 22050

static int16_t *g_sfx_data[SFX_COUNT];
static int      g_sfx_len[SFX_COUNT];

static int16_t *alloc_buf(int n) { return (int16_t *)calloc(n, sizeof(int16_t)); }
static float frand(void) { return (rand() / (float)RAND_MAX) * 2.f - 1.f; }

/* envelope douce : attack lineaire courte, release exponentielle */
static void envelope(int16_t *buf, int n, float attack, float release_decay) {
    int a = (int)(attack * SR);
    if (a < 1) a = 1;
    for (int i = 0; i < n; i++) {
        float e;
        if (i < a)        e = (float)i / (float)a;
        else              e = expf(-release_decay * (i - a) / (float)SR);
        int v = (int)(buf[i] * e);
        if (v >  32767) v =  32767;
        if (v < -32768) v = -32768;
        buf[i] = (int16_t)v;
    }
}

static void add_sine(int16_t *buf, int n, float freq, float amp, float pitch_decay) {
    for (int i = 0; i < n; i++) {
        float t = i / (float)SR;
        float f = freq * expf(-pitch_decay * t);
        if (f < 20.f) f = 20.f;
        float s = sinf(t * f * 6.2831f);
        int v = buf[i] + (int)(s * amp * 22000);
        if (v >  32767) v =  32767;
        if (v < -32768) v = -32768;
        buf[i] = (int16_t)v;
    }
}

static void add_noise(int16_t *buf, int n, float amp) {
    for (int i = 0; i < n; i++) {
        int v = buf[i] + (int)(frand() * amp * 14000);
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

/* === SONS === */

/* hit standard : un coup mat, pas un cri */
static void make_hit(int idx) {
    int n = (int)(0.13f * SR);
    int16_t *b = alloc_buf(n);
    add_sine(b, n, 320.f, 0.45f, 6.f);   /* corps thump */
    add_sine(b, n, 640.f, 0.18f, 8.f);   /* harmonique */
    low_pass(b, n, 0.30f);
    envelope(b, n, 0.002f, 18.f);
    g_sfx_data[idx] = b; g_sfx_len[idx] = n;
}

/* heavy : un grand boum sourd */
static void make_heavy(int idx) {
    int n = (int)(0.30f * SR);
    int16_t *b = alloc_buf(n);
    add_sine(b, n,  90.f, 0.55f, 3.f);
    add_sine(b, n, 180.f, 0.25f, 5.f);
    low_pass(b, n, 0.18f);
    envelope(b, n, 0.003f, 9.f);
    g_sfx_data[idx] = b; g_sfx_len[idx] = n;
}

/* punch : coup de poing mou */
static void make_punch(int idx) {
    int n = (int)(0.10f * SR);
    int16_t *b = alloc_buf(n);
    add_sine(b, n, 140.f, 0.40f, 8.f);
    add_noise(b, n, 0.10f);
    low_pass(b, n, 0.18f);
    envelope(b, n, 0.005f, 24.f);
    g_sfx_data[idx] = b; g_sfx_len[idx] = n;
}

/* swing : whoosh discret */
static void make_swing(int idx) {
    int n = (int)(0.08f * SR);
    int16_t *b = alloc_buf(n);
    add_noise(b, n, 0.20f);
    low_pass(b, n, 0.10f);
    envelope(b, n, 0.010f, 30.f);
    g_sfx_data[idx] = b; g_sfx_len[idx] = n;
}

/* explose : pas de cymbale, juste un boum bas filtree */
static void make_explode(int idx) {
    int n = (int)(0.30f * SR);
    int16_t *b = alloc_buf(n);
    add_sine(b, n,  60.f, 0.50f, 2.f);
    add_sine(b, n, 110.f, 0.20f, 4.f);
    add_noise(b, n, 0.15f);
    low_pass(b, n, 0.10f);
    envelope(b, n, 0.005f, 8.f);
    g_sfx_data[idx] = b; g_sfx_len[idx] = n;
}

/* pickup : "ding" doux a deux notes */
static void make_pickup(int idx) {
    int n = (int)(0.13f * SR);
    int16_t *b = alloc_buf(n);
    add_sine(b, n / 2,             880.f, 0.30f, 0.f);
    add_sine(b + n / 2, n - n / 2, 1320.f, 0.30f, 0.f);
    envelope(b, n, 0.005f, 20.f);
    g_sfx_data[idx] = b; g_sfx_len[idx] = n;
}

/* coin : sinus haut clair */
static void make_coin(int idx) {
    int n = (int)(0.16f * SR);
    int16_t *b = alloc_buf(n);
    add_sine(b, n / 3,             1318.f, 0.28f, 0.f);  /* mi 6 */
    add_sine(b + n / 3, 2 * n / 3, 1976.f, 0.28f, 0.f);  /* si 6 */
    add_sine(b, n,                 3951.f, 0.06f, 0.f);  /* harmonique */
    envelope(b, n, 0.004f, 18.f);
    g_sfx_data[idx] = b; g_sfx_len[idx] = n;
}

/* level-up : arpege ascendant majeur */
static void make_levelup(int idx) {
    int n = (int)(0.55f * SR);
    int16_t *b = alloc_buf(n);
    int seg = n / 4;
    float notes[4] = { 523.f, 659.f, 784.f, 1046.f }; /* C-E-G-C */
    for (int i = 0; i < 4; i++) {
        add_sine(b + i * seg, seg, notes[i], 0.30f, 0.f);
        add_sine(b + i * seg, seg, notes[i] * 2.f, 0.07f, 0.f);
    }
    envelope(b, n, 0.005f, 5.f);
    g_sfx_data[idx] = b; g_sfx_len[idx] = n;
}

/* fuse : long arpege harmonique pour la fusion */
static void make_fuse(int idx) {
    int n = (int)(0.85f * SR);
    int16_t *b = alloc_buf(n);
    int seg = n / 5;
    float notes[5] = { 392.f, 523.f, 659.f, 880.f, 1175.f };
    for (int i = 0; i < 5; i++) {
        add_sine(b + i * seg, seg, notes[i], 0.28f, 0.f);
        add_sine(b + i * seg, seg, notes[i] * 1.5f, 0.08f, 0.f);
    }
    envelope(b, n, 0.005f, 4.f);
    g_sfx_data[idx] = b; g_sfx_len[idx] = n;
}

/* hurt joueur : grognement court sourd */
static void make_player_hurt(int idx) {
    int n = (int)(0.18f * SR);
    int16_t *b = alloc_buf(n);
    add_sine(b, n, 220.f, 0.40f, 5.f);
    add_noise(b, n, 0.06f);
    low_pass(b, n, 0.20f);
    envelope(b, n, 0.005f, 14.f);
    g_sfx_data[idx] = b; g_sfx_len[idx] = n;
}

/* death joueur : descente longue */
static void make_death(int idx) {
    int n = (int)(0.65f * SR);
    int16_t *b = alloc_buf(n);
    add_sine(b, n, 280.f, 0.45f, 3.5f);
    add_sine(b, n, 140.f, 0.30f, 2.5f);
    low_pass(b, n, 0.15f);
    envelope(b, n, 0.010f, 4.f);
    g_sfx_data[idx] = b; g_sfx_len[idx] = n;
}

/* boss : drone bas montant en pression */
static void make_boss(int idx) {
    int n = (int)(0.90f * SR);
    int16_t *b = alloc_buf(n);
    /* glissando montant : pitch_decay negatif = montee */
    add_sine(b, n,  80.f, 0.50f, -0.6f);
    add_sine(b, n,  60.f, 0.30f, -0.4f);
    add_sine(b, n, 160.f, 0.18f, -0.6f);
    low_pass(b, n, 0.10f);
    envelope(b, n, 0.080f, 2.5f);
    g_sfx_data[idx] = b; g_sfx_len[idx] = n;
}

/* portal : sinus modulee douce */
static void make_portal(int idx) {
    int n = (int)(0.45f * SR);
    int16_t *b = alloc_buf(n);
    for (int i = 0; i < n; i++) {
        float t = i / (float)SR;
        float f = 540.f + sinf(t * 7.f) * 120.f;
        float s = sinf(t * f * 6.2831f);
        s += 0.30f * sinf(t * f * 2.f * 6.2831f);
        b[i] = (int16_t)(s * 8000);
    }
    envelope(b, n, 0.05f, 4.f);
    g_sfx_data[idx] = b; g_sfx_len[idx] = n;
}

/* shoot : ton sec */
static void make_shoot(int idx) {
    int n = (int)(0.08f * SR);
    int16_t *b = alloc_buf(n);
    add_sine(b, n,  900.f, 0.32f, 8.f);
    add_sine(b, n, 1800.f, 0.10f, 12.f);
    envelope(b, n, 0.002f, 28.f);
    g_sfx_data[idx] = b; g_sfx_len[idx] = n;
}

/* zap : eclair haut, harmonique courte */
static void make_zap(int idx) {
    int n = (int)(0.10f * SR);
    int16_t *b = alloc_buf(n);
    for (int i = 0; i < n; i++) {
        float t = i / (float)SR;
        float f = 1200.f * expf(-7.f * t);
        if (f < 200.f) f = 200.f;
        float s = sinf(t * f * 6.2831f);
        s += 0.30f * sinf(t * f * 2.f * 6.2831f);
        b[i] = (int16_t)(s * 9000);
    }
    envelope(b, n, 0.002f, 24.f);
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
    if (queued > (Uint32)(SR * 4)) return;   /* anti-runaway */
    if (vol >= 4) {
        SDL_QueueAudio(g->audio_dev, g_sfx_data[id], g_sfx_len[id] * sizeof(int16_t));
    } else {
        int n = g_sfx_len[id];
        int16_t *tmp = (int16_t *)malloc((size_t)n * sizeof(int16_t));
        if (!tmp) return;
        for (int i = 0; i < n; i++) tmp[i] = (int16_t)((int)g_sfx_data[id][i] * vol / 4);
        SDL_QueueAudio(g->audio_dev, tmp, (Uint32)(n * sizeof(int16_t)));
        free(tmp);
    }
}
