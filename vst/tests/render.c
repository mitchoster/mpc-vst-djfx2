/* render.c - offline A/B renderer: run the plugin's own engine over a WAV file with a macro ride.
 *
 * Why: the only honest way to compare our plugin with the original rack is to put the SAME audio and the
 * SAME macro movement through both. Ableton gives us the original (recorded from the master, one WAV and
 * one ride log per macro); this renders the dry WAV through our engine with the same ride, so the two
 * files can be compared step by step.
 *
 *   render <in.wav> <out.wav> <param-key> <ride.csv>
 *     ride.csv: lines "<seconds>,<ableton-macro-value 0..127>" (the wall-clock offsets of the recording)
 *
 * The engine is linked in directly (src/*.c), so no wrapper and no .so are involved. Blocks are 128
 * frames of interleaved int16, exactly as the device calls it.
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "engine.h"

#define BLOCK 128

static void die(const char *m) { fprintf(stderr, "%s\n", m); exit(2); }

/* ---- minimal WAV: 16-bit, 24-bit or 32-bit float PCM, stereo ---- */
typedef struct { int bits; int is_float; long frames; float *l, *r; } wav_t;

static void rd_bytes(FILE *f, void *p, size_t n) { if (fread(p, 1, n, f) != n) die("read past end of file"); }
static uint32_t rd_u32(FILE *f) { uint8_t b[4]; rd_bytes(f, b, 4); return (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24); }
static uint16_t rd_u16(FILE *f) { uint8_t b[2]; rd_bytes(f, b, 2); return (uint16_t)(b[0] | (b[1] << 8)); }

static wav_t wav_read(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) die("cannot open input wav");
    char riff[4], wave[4];
    rd_bytes(f, riff, 4);
    rd_u32(f);              /* RIFF chunk size */
    rd_bytes(f, wave, 4);
    if (memcmp(riff, "RIFF", 4) || memcmp(wave, "WAVE", 4)) {
        fprintf(stderr, "not a RIFF/WAVE file: [%c%c%c%c][%c%c%c%c] path=%s\n",
                riff[0], riff[1], riff[2], riff[3], wave[0], wave[1], wave[2], wave[3], path);
        exit(2);
    }
    int fmt = 0, bits = 0, ch = 0, is_float = 0;
    long data_at = -1; uint32_t data_len = 0;
    for (;;) {
        char id[4];
        if (fread(id, 1, 4, f) != 4) break;
        uint32_t sz = rd_u32(f);
        if (!memcmp(id, "fmt ", 4)) {
            uint16_t tag = rd_u16(f); ch = rd_u16(f); rd_u32(f); rd_u32(f); rd_u16(f);
            bits = rd_u16(f);
            is_float = (tag == 3);
            for (uint32_t k = 18; k < sz; k++) (void)getc(f);
        } else if (!memcmp(id, "data", 4)) {
            data_at = ftell(f); data_len = sz; fseek(f, (long)sz, SEEK_CUR);
        } else {
            fseek(f, (long)sz + (sz & 1), SEEK_CUR);
        }
        if (data_at >= 0 && fmt) break;
        if (!fmt) fmt = 1;
    }
    if (data_at < 0 || bits == 0 || ch < 1) die("unsupported wav");
    const int bytes = bits / 8;
    const long frames = (long)(data_len / (uint32_t)(bytes * ch));
    wav_t w = { bits, is_float, frames, calloc(frames, sizeof(float)), calloc(frames, sizeof(float)) };
    if (!w.l || !w.r) die("out of memory");
    fseek(f, data_at, SEEK_SET);
    for (long i = 0; i < frames; i++) {
        for (int c = 0; c < ch && c < 2; c++) {
            float v = 0.0f;
            if (is_float) { float g; rd_bytes(f, &g, 4); v = g; }
            else if (bits == 16) { int16_t s; rd_bytes(f, &s, 2); v = (float)s / 32768.0f; }
            else if (bits == 24) {
                uint8_t b[3]; rd_bytes(f, b, 3);
                int32_t s = (int32_t)((uint32_t)b[0] << 8 | (uint32_t)b[1] << 16 | (uint32_t)b[2] << 24) >> 8;
                v = (float)s / 8388608.0f;
            } else die("unsupported bit depth");
            if (c == 0) w.l[i] = v; else w.r[i] = v;
        }
        for (int c = ch; c < 2 && c < 2; c++) { }              /* mono: leave r at zero */
        if (ch == 1) w.r[i] = w.l[i];
    }
    fclose(f);
    return w;
}

static void wr_u32(FILE *f, uint32_t v) { uint8_t b[4] = { (uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24) }; fwrite(b, 1, 4, f); }
static void wr_u16(FILE *f, uint16_t v) { uint8_t b[2] = { (uint8_t)v, (uint8_t)(v >> 8) }; fwrite(b, 1, 2, f); }

static void wav_write24(const char *path, const float *l, const float *r, long frames)
{
    FILE *f = fopen(path, "wb");
    if (!f) die("cannot write output wav");
    const uint32_t data = (uint32_t)frames * 6u;
    fwrite("RIFF", 1, 4, f); wr_u32(f, 36u + data); fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f); wr_u32(f, 16); wr_u16(f, 1); wr_u16(f, 2);
    wr_u32(f, 44100); wr_u32(f, 44100 * 6); wr_u16(f, 6); wr_u16(f, 24);
    fwrite("data", 1, 4, f); wr_u32(f, data);
    for (long i = 0; i < frames; i++) {
        for (int c = 0; c < 2; c++) {
            float v = (c == 0) ? l[i] : r[i];
            if (v > 1.0f) v = 1.0f; if (v < -1.0f) v = -1.0f;
            int32_t s = (int32_t)lrintf(v * 8388607.0f);
            uint8_t b[3] = { (uint8_t)(s & 0xff), (uint8_t)((s >> 8) & 0xff), (uint8_t)((s >> 16) & 0xff) };
            fwrite(b, 1, 3, f);
        }
    }
    fclose(f);
}

/* ---- the ride: "<seconds>,<value>" ---- */
typedef struct { double t; float v; } step_t;
static int rd_ride(const char *path, step_t **out)
{
    FILE *f = fopen(path, "r");
    if (!f) die("cannot open ride csv");
    int n = 0, cap = 64;
    step_t *s = malloc(sizeof(step_t) * cap);
    char line[128];
    while (fgets(line, sizeof line, f)) {
        double t; float v;
        if (sscanf(line, "%lf,%f", &t, &v) == 2) {
            if (n == cap) { cap *= 2; s = realloc(s, sizeof(step_t) * cap); }
            s[n].t = t; s[n].v = v; n++;
        }
    }
    fclose(f);
    *out = s;
    return n;
}

int main(int argc, char **argv)
{
    if (argc != 5) { fprintf(stderr, "usage: render <in.wav> <out.wav> <param-key> <ride.csv>\n"); return 2; }
    wav_t w = wav_read(argv[1]);
    step_t *ride = NULL;
    const int nride = rd_ride(argv[4], &ride);

    const mpc_engine_t *E = mpc_engine();
    void *inst = E->create(NULL);
    if (!inst) die("engine create failed");
    E->set_param(inst, "sample_rate", "44100");
    /* every macro at its neutral position, then the ride drives exactly one of them */
    E->set_param(inst, "mac_lo", "1");   E->set_param(inst, "mac_mid", "1");
    E->set_param(inst, "mac_hi", "1");   E->set_param(inst, "mac_lpf", "1");
    E->set_param(inst, "mac_glitch", "0"); E->set_param(inst, "mac_verb", "0");
    E->set_param(inst, "mac_fade", "0");  E->set_param(inst, "mac_fill", "0");

    float *ol = calloc((size_t)w.frames, sizeof(float));
    float *or_ = calloc((size_t)w.frames, sizeof(float));
    if (!ol || !or_) die("out of memory");

    int16_t in[BLOCK * 2], out[BLOCK * 2];
    int ri = 0;
    long i = 0;
    while (i < w.frames) {
        const double t = (double)i / 44100.0;
        while (ri + 1 < nride && t >= ride[ri + 1].t) ri++;      /* this step's value applies from its offset */
        char buf[32];
        /* a macro key takes an Ableton 0..127 ride value (-> our 0..1 macro); any other key takes the
         * CSV value as-is, so a static test can drive a parameter directly */
        if (!strncmp(argv[3], "mac_", 4))
            snprintf(buf, sizeof buf, "%g", (double)ride[ri].v / 127.0);
        else
            snprintf(buf, sizeof buf, "%g", (double)ride[ri].v);
        E->set_param(inst, argv[3], buf);

        int n = (w.frames - i < BLOCK) ? (int)(w.frames - i) : BLOCK;
        for (int k = 0; k < n; k++) {
            float a = w.l[i + k], b = w.r[i + k];
            if (a > 1.0f) a = 1.0f; if (a < -1.0f) a = -1.0f;
            if (b > 1.0f) b = 1.0f; if (b < -1.0f) b = -1.0f;
            in[2 * k] = (int16_t)lrintf(a * 32767.0f);
            in[2 * k + 1] = (int16_t)lrintf(b * 32767.0f);
        }
        memset(out, 0, sizeof out);      /* effects may accumulate into the output */
        E->process(inst, in, out, n);
        for (int k = 0; k < n; k++) {
            ol[i + k] = (float)out[2 * k] / 32768.0f;
            or_[i + k] = (float)out[2 * k + 1] / 32768.0f;
        }
        i += n;
    }
    wav_write24(argv[2], ol, or_, w.frames);
    E->destroy(inst);
    printf("rendered %ld frames (%.1f s), macro '%s', %d ride steps -> %s\n",
           w.frames, (double)w.frames / 44100.0, argv[3], nride, argv[2]);
    return 0;
}
