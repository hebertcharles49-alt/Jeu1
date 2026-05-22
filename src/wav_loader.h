/* wav_loader.h - parser RIFF WAV minimal.
 *
 * Usage :
 *   #define WAV_LOADER_IMPLEMENTATION
 *   #include "wav_loader.h"
 *
 *   WavData w;
 *   if (wav_load_file("sounds/hit.wav", &w)) {
 *       w.samples       (float buffer, mono, -1..1)
 *       w.sample_count  (nb floats)
 *       w.sample_rate   (Hz)
 *       wav_free(&w);
 *   }
 *
 * Supporte : PCM 8-bit / 16-bit / 32-bit float, mono ou stereo
 * (downmix automatique en mono pour le mixer du jeu).
 * Resample non implemente : le caller doit accepter le sample_rate
 * tel quel (le mixer du jeu tourne a 44100, donc des WAV 44100 Hz
 * sont ideaux). */

#ifndef WAV_LOADER_H
#define WAV_LOADER_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    float    *samples;       /* mono, -1..1 */
    int       sample_count;
    int       sample_rate;
} WavData;

bool wav_load_file(const char *path, WavData *out);
bool wav_load_buffer(const uint8_t *buf, int n, WavData *out);
void wav_free(WavData *w);

#endif

#ifdef WAV_LOADER_IMPLEMENTATION

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t wav__u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint16_t wav__u16(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

bool wav_load_buffer(const uint8_t *buf, int n, WavData *out) {
    if (!buf || n < 44 || !out) return false;
    if (memcmp(buf, "RIFF", 4) != 0 || memcmp(buf + 8, "WAVE", 4) != 0) return false;
    /* parse chunks fmt + data */
    int format = 0;       /* 1 = PCM, 3 = float */
    int channels = 0, sample_rate = 0, bits = 0;
    const uint8_t *data = NULL;
    int data_sz = 0;
    int p = 12;
    while (p + 8 <= n) {
        const uint8_t *id = buf + p;
        uint32_t sz = wav__u32(buf + p + 4);
        const uint8_t *body = buf + p + 8;
        if (memcmp(id, "fmt ", 4) == 0 && sz >= 16) {
            format      = wav__u16(body);
            channels    = wav__u16(body + 2);
            sample_rate = (int)wav__u32(body + 4);
            bits        = wav__u16(body + 14);
        } else if (memcmp(id, "data", 4) == 0) {
            data = body;
            data_sz = (int)sz;
            break;
        }
        p += 8 + (int)sz + (sz & 1);   /* chunks alignes sur 2 bytes */
    }
    if (!data || channels < 1 || sample_rate <= 0) return false;
    int bytes_per_sample = bits / 8;
    if (bytes_per_sample <= 0) return false;
    int total_samples = data_sz / (bytes_per_sample * channels);
    if (total_samples <= 0) return false;
    out->samples = (float *)malloc(sizeof(float) * total_samples);
    if (!out->samples) return false;
    out->sample_count = total_samples;
    out->sample_rate  = sample_rate;
    /* Decode + downmix vers mono */
    for (int i = 0; i < total_samples; i++) {
        float sum = 0.f;
        for (int c = 0; c < channels; c++) {
            const uint8_t *s = data + (i * channels + c) * bytes_per_sample;
            float v = 0.f;
            if (format == 1) {
                if (bits == 8) {
                    /* 8-bit PCM is UNSIGNED 0..255, centre sur 128 */
                    v = ((float)*s - 128.f) / 128.f;
                } else if (bits == 16) {
                    int16_t x = (int16_t)((uint16_t)s[0] | ((uint16_t)s[1] << 8));
                    v = (float)x / 32768.f;
                } else if (bits == 24) {
                    int32_t x = (int32_t)((uint32_t)s[0] |
                                          ((uint32_t)s[1] << 8) |
                                          ((uint32_t)s[2] << 16));
                    if (x & 0x800000) x |= 0xFF000000;
                    v = (float)x / 8388608.f;
                } else if (bits == 32) {
                    int32_t x = (int32_t)wav__u32(s);
                    v = (float)x / 2147483648.f;
                }
            } else if (format == 3 && bits == 32) {
                uint32_t u = wav__u32(s);
                float f; memcpy(&f, &u, 4);
                v = f;
            }
            sum += v;
        }
        out->samples[i] = sum / (float)channels;
    }
    return true;
}

bool wav_load_file(const char *path, WavData *out) {
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0) { fclose(f); return false; }
    uint8_t *buf = (uint8_t *)malloc((size_t)n);
    if (!buf) { fclose(f); return false; }
    size_t rd = fread(buf, 1, (size_t)n, f);
    fclose(f);
    bool ok = wav_load_buffer(buf, (int)rd, out);
    free(buf);
    return ok;
}

void wav_free(WavData *w) {
    if (!w) return;
    free(w->samples);
    w->samples = NULL;
    w->sample_count = 0;
}

#endif
