/*
 * audio.c - sons procéduraux générés au runtime + mixer software a voix
 * multiples.
 *
 * Avant : SDL_QueueAudio empilait sequentiellement, donc les sons NE SE
 * SUPERPOSAIENT PAS (on entendait queue, pas un mix). Maintenant on a un
 * callback audio qui melange jusqu a MAX_VOICES voix simultanees, avec
 * variation de pitch (resampling lineaire) et volume par voix.
 */
#include "game.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define SR 22050
#define MAX_VOICES 24

static int16_t *g_sfx_data[SFX_COUNT];
static int      g_sfx_len[SFX_COUNT];

typedef struct {
    const int16_t *data;
    int     len;
    float   pos_f;        /* index fractionnaire pour pitch */
    float   pitch;        /* 1.0 = normal */
    float   volume;       /* 0..1 (apres ducking et settings) */
    int     priority;     /* 0 = bas, 9 = critique. utilise au steal */
    bool    active;
} Voice;

static Voice         g_voices[MAX_VOICES];
static SDL_mutex    *g_voice_mutex = NULL;
static float         g_master_vol  = 1.f;
/* ducking : un coup recu coupe les sons "sourds" pendant 80ms pour laisser
 * passer le grognement du joueur. */
static float         g_duck_t      = 0.f;
static float         g_duck_dur    = 0.08f;

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

/* hurt joueur : grognement court sourd. Plus de corps qu avant pour
 * bien marquer l impact (sub-bass + harmonique aigue + bruit). */
static void make_player_hurt(int idx) {
    int n = (int)(0.22f * SR);
    int16_t *b = alloc_buf(n);
    add_sine(b, n,  90.f, 0.35f, 3.f);     /* sub-bass */
    add_sine(b, n, 220.f, 0.40f, 5.f);     /* corps */
    add_sine(b, n, 440.f, 0.12f, 9.f);     /* harmonique */
    add_noise(b, n, 0.10f);
    low_pass(b, n, 0.20f);
    envelope(b, n, 0.003f, 10.f);
    g_sfx_data[idx] = b; g_sfx_len[idx] = n;
}

/* heartbeat : "thump" bas tres court, joue en boucle quand PV faibles */
static void make_heartbeat(int idx) {
    int n = (int)(0.10f * SR);
    int16_t *b = alloc_buf(n);
    add_sine(b, n,  55.f, 0.55f, 2.f);
    add_sine(b, n, 110.f, 0.20f, 4.f);
    low_pass(b, n, 0.12f);
    envelope(b, n, 0.005f, 22.f);
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

/* === MIXER CALLBACK ===
 * Appele par SDL dans un thread audio. On melange toutes les voix actives
 * avec resampling lineaire (pour le pitch). Ne pas faire d alloc ici. */
static void audio_mix_cb(void *udata, Uint8 *stream, int len_bytes) {
    (void)udata;
    int16_t *out = (int16_t *)stream;
    int n_samples = len_bytes / (int)sizeof(int16_t);
    memset(out, 0, (size_t)len_bytes);
    if (!g_voice_mutex) return;
    SDL_LockMutex(g_voice_mutex);
    /* ducking : on attenue les voix de priorite basse pendant g_duck_t.
     * On consomme le temps en fonction du buffer joue. */
    float duck_consume = (float)n_samples / (float)SR;
    float duck_amt = (g_duck_t > 0.f) ? 0.45f : 1.0f;
    for (int v = 0; v < MAX_VOICES; v++) {
        Voice *vo = &g_voices[v];
        if (!vo->active || !vo->data) continue;
        float vol = vo->volume * g_master_vol;
        if (vo->priority <= 2) vol *= duck_amt;
        for (int i = 0; i < n_samples; i++) {
            int idx = (int)vo->pos_f;
            if (idx >= vo->len - 1) { vo->active = false; break; }
            /* interpolation lineaire pour pitch non entier */
            float frac = vo->pos_f - (float)idx;
            int32_t s0 = vo->data[idx];
            int32_t s1 = vo->data[idx + 1];
            int32_t s = (int32_t)(((1.f - frac) * s0 + frac * s1) * vol);
            int32_t mix = (int32_t)out[i] + s;
            if (mix >  32767) mix =  32767;
            if (mix < -32768) mix = -32768;
            out[i] = (int16_t)mix;
            vo->pos_f += vo->pitch;
        }
    }
    if (g_duck_t > 0.f) {
        g_duck_t -= duck_consume;
        if (g_duck_t < 0.f) g_duck_t = 0.f;
    }
    SDL_UnlockMutex(g_voice_mutex);
}

void audio_init(Game *g) {
    SDL_AudioSpec want = {0}, got;
    want.freq     = SR;
    want.format   = AUDIO_S16SYS;
    want.channels = 1;
    want.samples  = 512;       /* ~23ms : latence faible pour le combat */
    want.callback = audio_mix_cb;
    want.userdata = g;
    g_voice_mutex = SDL_CreateMutex();
    memset(g_voices, 0, sizeof(g_voices));
    g_duck_t = 0.f;
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
    make_heartbeat(SFX_HEARTBEAT);
}

void audio_shutdown(Game *g) {
    if (g->audio_dev) SDL_CloseAudioDevice(g->audio_dev);
    if (g_voice_mutex) { SDL_DestroyMutex(g_voice_mutex); g_voice_mutex = NULL; }
    for (int i = 0; i < SFX_COUNT; i++) {
        if (g_sfx_data[i]) { free(g_sfx_data[i]); g_sfx_data[i] = NULL; }
    }
}

/* priorite par defaut. Le hurt joueur et la mort passent au-dessus du
 * brouhaha de combat (priorite 9). */
static int sfx_default_priority(SfxId id) {
    switch (id) {
        case SFX_PLAYER_HURT:
        case SFX_DEATH:
        case SFX_BOSS:
        case SFX_LEVELUP:
            return 9;
        case SFX_EXPLODE:
        case SFX_HEAVY_HIT:
        case SFX_PORTAL:
        case SFX_FUSE:
            return 6;
        default: return 3;
    }
}

/* alloue une voix libre, ou steal la plus avancee de priorite <= */
static int alloc_voice(int priority) {
    int free_idx = -1;
    int steal_idx = -1;
    float steal_score = -1.f;
    for (int v = 0; v < MAX_VOICES; v++) {
        if (!g_voices[v].active) { free_idx = v; break; }
        if (g_voices[v].priority <= priority) {
            float progress = g_voices[v].pos_f / (float)(g_voices[v].len + 1);
            /* preference au steal de la voix la plus terminee */
            if (progress > steal_score) {
                steal_score = progress;
                steal_idx = v;
            }
        }
    }
    if (free_idx >= 0) return free_idx;
    return steal_idx;
}

void sfx_play_ex(Game *g, SfxId id, float pitch, float vol_mul) {
    if (!g->audio_dev) return;
    if (id < 0 || id >= SFX_COUNT) return;
    if (!g_sfx_data[id]) return;
    if (g->settings.sfx_mute) return;
    int vol_set = g->settings.sfx_volume;
    if (vol_set <= 0) return;
    float vol = (vol_set / 4.f) * vol_mul;
    if (vol > 1.0f) vol = 1.0f;
    if (vol < 0.f) vol = 0.f;
    if (pitch < 0.25f) pitch = 0.25f;
    if (pitch > 4.0f)  pitch = 4.0f;

    int priority = sfx_default_priority(id);
    if (!g_voice_mutex) return;
    SDL_LockMutex(g_voice_mutex);
    int v = alloc_voice(priority);
    if (v >= 0) {
        g_voices[v].data     = g_sfx_data[id];
        g_voices[v].len      = g_sfx_len[id];
        g_voices[v].pos_f    = 0.f;
        g_voices[v].pitch    = pitch;
        g_voices[v].volume   = vol;
        g_voices[v].priority = priority;
        g_voices[v].active   = true;
    }
    /* sons "criants" (hurt/death/boss) declenchent un duck court */
    if (priority >= 9 && id != SFX_LEVELUP) g_duck_t = g_duck_dur;
    SDL_UnlockMutex(g_voice_mutex);
}

void sfx_play(Game *g, SfxId id) {
    /* Variation de pitch +/-8% sur les sons de combat percussifs pour eviter
     * la fatigue auditive sur les coups rapides. Les sons "musicaux"
     * (pickup, coin, levelup) restent stables. */
    float pitch = 1.f;
    switch (id) {
        case SFX_HIT:
        case SFX_PUNCH:
        case SFX_HEAVY_HIT:
        case SFX_SWING:
        case SFX_SHOOT:
        case SFX_ZAP:
        case SFX_EXPLODE:
            pitch = 1.f + ((rand() / (float)RAND_MAX) - 0.5f) * 0.16f;
            break;
        default: break;
    }
    sfx_play_ex(g, id, pitch, 1.f);
}
