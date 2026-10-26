/* grain.c - MPC DJ FX 2 grain delay (own code, MIT). See include/grain.h for the model.
 *
 * Why this exists: the original DJ-FX rack's "Glitch" macro does not switch on a delay, it fades in
 * Ableton's Grain Delay (measured: grain rate 1 -> 150 Hz, read offset 22 -> 2 ms, dry/wet 0 -> 35 %).
 * v1 stood in for it with the through-zero flanger, which is the wrong texture. This module is the
 * spike candidate for the real thing: small, dependency-free, and it exposes exactly the spans the
 * macro needs.
 *
 * Structure per sample:
 *   - the input is written into a 1 s stereo ring buffer;
 *   - a phase accumulator fires `rate` grains per second;
 *   - every grain walks a Hann-windowed slice of the buffer, starting `time` ms behind the write head
 *     plus a random jitter (`spray`), with a small random detune and pan;
 *   - the grain sum is energy-normalised against the overlap (rate x grain length) and mixed against
 *     the dry signal.
 */
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "grain.h"

#define GR_MAX_GRAINS 12       /* 13 ms grains at 150 Hz need ~4; 12 caps the worst-case scan */
#define GR_BUF_SECONDS 0.75f    /* >= max time (500 ms) + spray span, and a smaller cache footprint */
#define GR_TABLE 1024
#define GR_MIN_LEN 16
#define GR_MAX_LEN_FRAC 0.25f        /* never longer than a quarter second */

typedef struct {
    int   active;
    float pos;        /* fractional read position, samples */
    float inc;        /* playback increment (1.0 +- detune) */
    int   age, len;
    float pan;        /* 0 = left, 1 = right */
} gr_voice_t;

struct grain {
    float fs;
    float *buf[2];
    int   buflen, w;

    float rate_hz, time_ms, spray, mix;
    double phase;              /* double: at 1 Hz a float accumulator never reaches 1.0 */
    unsigned long spawned;

    gr_voice_t v[GR_MAX_GRAINS];
    int   live[GR_MAX_GRAINS];   /* compact list of ringing voices: only these are scanned per sample */
    int   n_live;
    int   next;
    float env[GR_TABLE + 1];
    uint32_t rng;
};

static uint32_t gr_rand(uint32_t *s)
{
    uint32_t x = *s;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return (*s = x ? x : 0x9e3779b9u);
}

static float gr_rand01(uint32_t *s) { return (float)(gr_rand(s) >> 8) * (1.0f / 16777216.0f); }

static void gr_build_env(grain_t *g)
{
    for (int i = 0; i <= GR_TABLE; i++)
        g->env[i] = 0.5f - 0.5f * cosf(6.28318530718f * (float)i / (float)GR_TABLE);
}

grain_t *grain_create(float fs)
{
    grain_t *g = (grain_t *)calloc(1, sizeof *g);
    if (!g) return NULL;
    g->fs = fs > 0.0f ? fs : 44100.0f;
    g->buflen = (int)(g->fs * GR_BUF_SECONDS);
    if (g->buflen < 4096) g->buflen = 4096;
    g->buf[0] = (float *)calloc((size_t)g->buflen, sizeof(float));
    g->buf[1] = (float *)calloc((size_t)g->buflen, sizeof(float));
    if (!g->buf[0] || !g->buf[1]) { grain_destroy(g); return NULL; }
    gr_build_env(g);
    g->rate_hz = 20.0f;
    g->time_ms = 20.0f;
    g->spray = 0.35f;
    g->mix = 0.0f;
    grain_reset(g);
    return g;
}

void grain_destroy(grain_t *g)
{
    if (!g) return;
    free(g->buf[0]);
    free(g->buf[1]);
    free(g);
}

void grain_reset(grain_t *g)
{
    if (!g) return;
    memset(g->buf[0], 0, (size_t)g->buflen * sizeof(float));
    memset(g->buf[1], 0, (size_t)g->buflen * sizeof(float));
    memset(g->v, 0, sizeof g->v);
    g->n_live = 0;
    g->w = 0;
    g->phase = 0.0;
    g->spawned = 0;
    g->next = 0;
    g->rng = 0x1234567u;
}

void grain_set_sample_rate(grain_t *g, float fs)
{
    if (!g || fs <= 0.0f) return;
    g->fs = fs;
}

void grain_set_rate(grain_t *g, float hz)
{
    if (g) g->rate_hz = hz < 1.0f ? 1.0f : (hz > 200.0f ? 200.0f : hz);
}

void grain_set_time(grain_t *g, float ms)
{
    if (g) g->time_ms = ms < 0.5f ? 0.5f : (ms > 500.0f ? 500.0f : ms);
}

void grain_set_spray(grain_t *g, float s)
{
    if (g) g->spray = s < 0.0f ? 0.0f : (s > 1.0f ? 1.0f : s);
}

void grain_set_mix(grain_t *g, float m)
{
    if (g) g->mix = m < 0.0f ? 0.0f : (m > 1.0f ? 1.0f : m);
}

/* grain length = two periods, so consecutive grains overlap by half */
static int gr_len(const grain_t *g)
{
    float period = g->fs / g->rate_hz;
    float len = 2.0f * period;
    float maxlen = g->fs * GR_MAX_LEN_FRAC;
    if (len > maxlen) len = maxlen;
    if (len < (float)GR_MIN_LEN) len = (float)GR_MIN_LEN;
    return (int)len;
}

static void gr_spawn(grain_t *g)
{
    gr_voice_t *v = &g->v[g->next];
    const int slot = g->next;
    g->next = (g->next + 1) % GR_MAX_GRAINS;

    const int len = gr_len(g);
    const float behind = g->time_ms * 0.001f * g->fs;             /* base read offset, samples */
    const float span = g->spray * (float)(g->buflen - len - 2);    /* how far spray may reach back */
    float back = behind + gr_rand01(&g->rng) * span;
    if (back > (float)(g->buflen - len - 2)) back = (float)(g->buflen - len - 2);
    if (back < 1.0f) back = 1.0f;

    float pos = (float)g->w - back;
    while (pos < 0.0f) pos += (float)g->buflen;

    v->active = 1;
    v->pos = pos;
    v->inc = 0.995f + 0.010f * gr_rand01(&g->rng);   /* tiny detune, so grains cloud instead of chop */
    v->age = 0;
    v->len = len;
    v->pan = gr_rand01(&g->rng);
    g->spawned++;

    /* register the slot in the live list (the slot was dropped from it when it last finished) */
    int known = 0;
    for (int k = 0; k < g->n_live; k++) if (g->live[k] == slot) { known = 1; break; }
    if (!known && g->n_live < GR_MAX_GRAINS) g->live[g->n_live++] = slot;
}

void grain_process(grain_t *g, float *l, float *r, int n)
{
    if (!g || n <= 0) return;
    if (g->mix <= 0.0f) {
        /* mix 0 is a sample-exact passthrough (asserted by the tests), so skip the whole stage -
         * this is the default state, so the neutral plugin pays nothing for the grain */
        g->w = (g->w + n) % g->buflen;
        return;
    }
    const float dry = 1.0f - g->mix;
    const float wet = g->mix;
    const float len_s = (float)gr_len(g) / g->fs;
    float norm = 1.0f / (g->rate_hz * len_s);        /* overlap-aware energy normalisation */
    if (norm > 1.0f) norm = 1.0f;

    for (int i = 0; i < n; i++) {
        const float inl = l[i], inr = r[i];
        g->buf[0][g->w] = inl;
        g->buf[1][g->w] = inr;

        g->phase += (double)g->rate_hz / (double)g->fs;
        while (g->phase >= 1.0) { g->phase -= 1.0; gr_spawn(g); }

        float sl = 0.0f, sr = 0.0f;
        for (int k = 0; k < g->n_live; ) {
            gr_voice_t *v = &g->v[g->live[k]];
            const float e = g->env[(int)((float)v->age / (float)v->len * (float)GR_TABLE)];
            int idx = (int)v->pos;
            if (idx >= g->buflen) idx -= g->buflen;
            const float gl = g->buf[0][idx] * e;
            const float gr = g->buf[1][idx] * e;
            sl += gl * (1.0f - v->pan) + gr * v->pan;      /* cheap stereo spread */
            sr += gr * v->pan + gl * (1.0f - v->pan);
            v->pos += v->inc;
            if (v->pos >= (float)g->buflen) v->pos -= (float)g->buflen;
            if (++v->age >= v->len) {
                v->active = 0;
                g->live[k] = g->live[--g->n_live];         /* swap-remove: keeps the list compact */
            } else {
                k++;
            }
        }

        l[i] = inl * dry + sl * norm * wet;
        r[i] = inr * dry + sr * norm * wet;

        if (++g->w >= g->buflen) g->w = 0;
    }
}

unsigned long grain_spawned(const grain_t *g) { return g ? g->spawned : 0; }

int grain_active_voices(const grain_t *g) { return g ? g->n_live : 0; }
