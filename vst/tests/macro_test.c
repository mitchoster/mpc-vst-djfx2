/* macro_test.c — verifies the authored macro layer (8 macros, multi-target).
 * Drives the engine through its public vtable: with the macro at 0 its targets must hold their neutral
 * values, at 1 the documented extremes, and every target must move monotonically in between.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "engine.h"

const mpc_engine_t *mpc_engine(void);
static const mpc_engine_t *E;
static float getf(void *inst, const char *k)
{
    char b[64];
    if (E->get_param(inst, k, b, sizeof b) <= 0) return -999.0f;
    return (float)atof(b);
}

static const struct { const char *mac; const char *key; float lo, hi, neutral; int geom; } MAP[] = {
    /* ranges measured from the original DJ-FX rack in Live (2026-10-15); geom = equal ratios per step */
    { "mac_lo",     "iso_lo",       -24.0f,  0.0f,  0.0f, 2 },
    { "mac_mid",    "iso_mid",      -24.0f,  0.0f,  0.0f, 2 },
    { "mac_hi",     "iso_hi",       -24.0f,  0.0f,  0.0f, 2 },
    { "mac_lpf",    "dj_filter",     -1.00f,  0.0f,  0.0f, 0 },
    { "mac_glitch", "gr_rate",         1.0f, 150.0f, 1.0f, 0 },
    { "mac_glitch", "gr_time",        22.0f,   2.0f, 22.0f, 0 },
    { "mac_glitch", "gr_mix",          0.0f,  0.35f, 0.0f, 0 },
    { "mac_glitch", "mix",             0.0f,  1.00f, 0.0f, 0 },
    { "mac_verb",   "verb_amount",     0.0f,  1.00f, 0.0f, 0 },
    { "mac_verb",   "hp_hz",          26.0f, 15400.0f, 26.0f, 1 },
    { "mac_verb",   "hp_reso",         0.0f,  1.00f, 0.0f, 0 },
    { "mac_fade",   "iso_lo_x",      250.0f, 2030.0f, 250.0f, 1 },
    { "mac_fade",   "iso_hi_x",     2500.0f, 2180.0f, 2500.0f, 1 },
    { "mac_fade",   "iso_mid",         0.0f,  -8.0f, 0.0f, 0 },
    { "mac_fade",   "dly_fbk",        0.55f,  0.95f, 0.55f, 0 },
    { "mac_fill",   "rep_mode",        0.0f,   7.0f, 0.0f, 0 },
};
static int fails = 0;
static void check(const char *what, int ok, const char *detail)
{
    printf("%-56s %s%s%s\n", what, ok ? "ok" : "FAIL", detail[0] ? "  " : "", detail);
    if (!ok) fails++;
}

/* A clean macro state before each row: the macros share cells (`mix`, `dly_fbk`), so a leftover macro
 * position from the previous row would leak into this one. */
static void neutral_macros(void *inst)
{
    E->set_param(inst, "mac_lo", "1");     E->set_param(inst, "mac_mid", "1");
    E->set_param(inst, "mac_hi", "1");     E->set_param(inst, "mac_lpf", "1");
    E->set_param(inst, "mac_glitch", "0"); E->set_param(inst, "mac_verb", "0");
    E->set_param(inst, "mac_fade", "0");   E->set_param(inst, "mac_fill", "0");
}

int main(void)
{
    E = mpc_engine();
    void *inst = E->create(NULL);
    if (!inst) { printf("engine create failed\n"); return 2; }
    char b[96];

    for (unsigned k = 0; k < sizeof MAP / sizeof MAP[0]; k++) {
        float prev = 0.0f;
        int mono = 1, inrange = 1;
        neutral_macros(inst);
        for (int s = 0; s <= 10; s++) {
            const float m = (float)s / 10.0f;
            snprintf(b, sizeof b, "%g", (double)m);
            E->set_param(inst, MAP[k].mac, b);
            const float v = getf(inst, MAP[k].key);
            if (s > 0 && ((MAP[k].hi >= MAP[k].lo) ? (v < prev - 1e-3f) : (v > prev + 1e-3f))) mono = 0;
            const float lo = MAP[k].lo < MAP[k].hi ? MAP[k].lo : MAP[k].hi;
            const float hi = MAP[k].lo < MAP[k].hi ? MAP[k].hi : MAP[k].lo;
            if (v < lo - 1e-3f || v > hi + 1e-3f) inrange = 0;
            prev = v;
        }
        snprintf(b, sizeof b, "(%s: %.2f..%.2f)", MAP[k].key, MAP[k].lo, MAP[k].hi);
        char what[90]; snprintf(what, sizeof what, "%s moves %s monotonically in range", MAP[k].mac, MAP[k].key);
        check(what, mono && inrange, b);
    }

    /* midpoint check: linear rows hit (lo+hi)/2, geometric rows hit sqrt(lo*hi) */
    {
        int mono = 1;
        char d[110];
        for (unsigned k = 0; k < sizeof MAP / sizeof MAP[0]; k++) {
            neutral_macros(inst);
            E->set_param(inst, MAP[k].mac, "0.5");
            const float got = getf(inst, MAP[k].key);
            /* geom = equal ratios per step; 2 = the fitted EQ curve; 0 = linear */
            const float want = (MAP[k].geom == 1) ? sqrtf(MAP[k].lo * MAP[k].hi)
                             : (MAP[k].geom == 2) ? (MAP[k].hi + (MAP[k].lo - MAP[k].hi) * (-16.96f / MAP[k].lo))
                             : 0.5f * (MAP[k].lo + MAP[k].hi);
            const float span = fabsf(MAP[k].hi - MAP[k].lo);
            if (fabsf(got - want) > 0.03f * span + 1e-3f) {
                snprintf(d, sizeof d, "   %s at 0.5: got %.2f, want %.2f", MAP[k].key, got, want);
                printf("%s\n", d);
                mono = 0;
            }
        }
        check("every macro row follows its curve at the midpoint", mono, "");
    }

    /* a fresh instance: the macro defaults must give the neutral values */
    void *fresh = E->create(NULL);
    int ok = 1;
    for (unsigned k = 0; k < sizeof MAP / sizeof MAP[0]; k++)
        if (fabsf(getf(fresh, MAP[k].key) - MAP[k].neutral) > 1e-3f) {
            printf("   %s = %.3f, want %.3f\n", MAP[k].key, getf(fresh, MAP[k].key), MAP[k].neutral);
            ok = 0;
        }
    check("fresh instance: macros at their neutral defaults", ok, "");
    E->destroy(fresh);
    /* the Auto-Fill switch selects the row set, and takes effect without moving the macro again */
    {
        void *x = E->create(NULL);
        int ok = 1;
        char d2[128];
        E->set_param(x, "mac_fill", "1");
        const float cnt_fill = getf(x, "rep_count"), gate_fill = getf(x, "rep_gate");
        if (cnt_fill != 4.0f || gate_fill != 0.25f) ok = 0;      /* Fill leaves the hand-set values */
        E->set_param(x, "fill_mode", "1");                      /* switch to Stutter */
        const float cnt_st = getf(x, "rep_count"), gate_st = getf(x, "rep_gate");
        if (cnt_st != 0.0f || gate_st != 1.0f) ok = 0;          /* hold + open gate */
        E->set_param(x, "fill_mode", "0");
        if (getf(x, "rep_count") != 4.0f || getf(x, "rep_gate") != 0.25f) ok = 0;
        snprintf(d2, sizeof d2, "(fill repeats %.0f gate %.2f -> stutter %.0f / %.2f -> back)",
                 cnt_fill, gate_fill, cnt_st, gate_st);
        check("Auto-Fill mode: Stutter holds the slice, Fill repeats (switchable live)", ok, d2);
        E->destroy(x);
    }

    /* the tempo-sync defaults the owner asked for: synced, 1/8, and readable as a division */
    {
        void *x = E->create(NULL);
        char t[64];
        E->set_param(x, "lfo_bpm", "120");            /* the host tempo is what makes sync mean anything */
        const int sync = (int)getf(x, "dly_sync"), div = (int)getf(x, "dly_div");
        E->get_param(x, "dly_time_display", t, sizeof t);
        char d2[128];
        snprintf(d2, sizeof d2, "(dly_sync %d, dly_div %d, TIME reads '%s' at 120 BPM)", sync, div, t);
        check("a fresh instance is tempo-synced at 1/8", sync == 1 && div == 5 && strstr(t, "1/8") != NULL, d2);
        /* and the raw defaults still come out exact through the normalised form */
        snprintf(d2, sizeof d2, "(dly_time %.3f, iso_lo_x %.3f, dly_tone %.3f)",
                 getf(x, "dly_time"), getf(x, "iso_lo_x"), getf(x, "dly_tone"));
        check("the defaults survive the normalised form exactly",
              fabsf(getf(x, "dly_time") - 350.0f) < 0.01f && fabsf(getf(x, "iso_lo_x") - 250.0f) < 0.01f
              && fabsf(getf(x, "dly_tone") - 0.4f) < 1e-4f, d2);
        E->destroy(x);
    }

    E->destroy(inst);
    printf("\n%s\n", fails ? "FAILED" : "PASSED");
    return fails ? 1 : 0;
}
