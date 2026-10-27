/* djfx_engine.c — MPC DJ FX engine.
 *
 * One insert effect: isolator -> DJ filter -> beat repeat -> delay -> VERB high-pass -> reverb ->
 * flanger -> limiter, plus an authored macro, a global wet/dry mix and an output trim.
 *
 * Implemented: T1 skeleton, T2 reused DubStation DSP, T3 own isolator + DJ filter, T5/T6 beat repeat,
 * T7 through-zero flanger, T8 limiter/output/panic + live VU, T9 macro. The skin is T10.
 *
 * Contract: wrapper/engine.h — interleaved int16 stereo, 128-frame blocks, 44.1 kHz. No allocation in
 * process(). KEYS must stay in step with vst/params.json (same order = VST parameter index).
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

#include "engine.h"
#include "params.h"      /* generated: defines + PARAMS[] (NPARAMS, HAS_LFO_BPM, ...) */
#include "dsp_common.h"
#include "iso.h"
#include "dub_delay.h"
#include "dub_filter.h"
#include "dub_reverb.h"
#include "repeat.h"
#include "flanger.h"
#include "grain.h"
#include "limiter.h"

#define MAXFRAMES 128
#define VALLEN 40
#define VU_STEPS 81   /* 81 frames = 0.6 dB per step on a 48 dB scale (was 24 = 2.09 dB) */
#define MET_STEPS 41   /* per-page stage meters: 1.2 dB per step */
#define VU_FLOOR_DB (-48.0f)

static const char *const KEYS[] = {
    "iso_lo", "iso_mid", "iso_hi", "iso_lo_x", "iso_hi_x",
    "dj_filter", "dj_reso",
    "rep_mode", "rep_gate", "rep_chance", "rep_count", "rep_mix", "fill_mode",
    "dly_time", "dly_sync", "dly_div", "dly_fbk", "dly_tone", "dly_mix", "dly_pingpong",
    "verb_mode", "verb_amount", "verb_decay", "verb_damp", "verb_predelay",
    "hp_hz", "hp_reso",
    "fl_rate", "fl_depth", "fl_fbk", "fl_mix",
    "gr_rate", "gr_time", "gr_spray", "gr_mix",
    "mac_lo", "mac_mid", "mac_hi", "mac_lpf", "mac_glitch", "mac_verb", "mac_fade", "mac_fill",
    "output_gain", "limiter", "panic", "vu", "mix",
    "dly_tail", "verb_tail", "rep_act", "dly_show", "out_show"
};
#define NKEYS ((int)(sizeof KEYS / sizeof KEYS[0]))
_Static_assert(sizeof KEYS / sizeof KEYS[0] == NPARAMS,
               "KEYS must list every parameter in params.json, in the same order");

/* Defaults come from params.json alone: the generated PARAMS[] carries them, and the hand-written copy
 * that used to sit here drifted from it once (dly_sync/dly_div, 2026-10-16) - a whole class of bug that
 * is gone with the duplicate. `defaults must match vst/params.json` is now structural, not a promise. */

enum {
    P_ISO_LO = 0, P_ISO_MID, P_ISO_HI, P_ISO_LO_X, P_ISO_HI_X,
    P_DJ_FILTER, P_DJ_RESO,
    P_REP_MODE, P_REP_GATE, P_REP_CHANCE, P_REP_COUNT, P_REP_MIX, P_FILL_MODE,
    P_DLY_TIME, P_DLY_SYNC, P_DLY_DIV, P_DLY_FBK, P_DLY_TONE, P_DLY_MIX, P_DLY_PINGPONG,
    P_VERB_MODE, P_VERB_AMOUNT, P_VERB_DECAY, P_VERB_DAMP, P_VERB_PREDELAY,
    P_VHF_HZ, P_VHF_RESO,
    P_FL_RATE, P_FL_DEPTH, P_FL_FBK, P_FL_MIX,
    P_GR_RATE, P_GR_TIME, P_GR_SPRAY, P_GR_MIX,
    P_MAC_LO, P_MAC_MID, P_MAC_HI, P_MAC_LPF, P_MAC_GLITCH, P_MAC_VERB, P_MAC_FADE, P_MAC_FILL,
    P_OUTPUT_GAIN, P_LIMITER, P_PANIC, P_VU, P_MIX,
    P_DLY_TAIL, P_VERB_TAIL, P_REP_ACT, P_DLY_SHOW, P_OUT_SHOW
};

typedef struct {
    iso_t        *iso;
    dub_filter_t *djf;      /* the DJ filter (bipolar LPF/HPF sweep) */
    dub_filter_t *vhf;      /* the VERB macro's wet-path high-pass (hp only; driven by mac_verb) */
    repeat_t     *rep;
    dub_delay_t  *delay;
    dub_reverb_t *rv;
    flanger_t    *fl;
    grain_t      *gr;       /* grain delay: the original Glitch stage */
    limiter_t    *lim;
    float fs;
    float bpm;              /* host tempo (HAS_LFO_BPM) */
    float mix;
    float out_gain;         /* linear */
    unsigned display_rev;   /* HAS_DISPLAY_REV */
    int   vu_step;          /* last reported VU step (-1 = never) */
    int   met_step[3];      /* last reported stage steps: delay, verb, repeat (-1 = never) */
    int   met_tick;         /* samples since the last 20 Hz meter tick */
    unsigned long long pending;  /* meter keys waiting for mpc_engine_changed() */
    float vu_env;
    float dly_env, verb_env, rep_env;
    int   panic_cd;         /* panic debounce, in samples */
    int   panic_state;      /* panic flush staged across blocks (0 = idle, 1..3 = stage) */
    char  val[NKEYS][VALLEN];
    float l[MAXFRAMES], r[MAXFRAMES], w1[MAXFRAMES], w2[MAXFRAMES];
} ctx_t;

/* 12 sync divisions of a beat, in beats */
static const float DIV_BEATS[12] = { 4.0f, 2.0f, 3.0f, 1.0f, 1.5f, 0.5f, 0.75f, 1.0f / 3.0f,
                                     0.25f, 0.375f, 1.0f / 6.0f, 0.125f };
#define N_DIV 12

/* The macro layer is OUR design: the Ableton rack's macro->parameter mapping is not stored in the
 * .adg (every ModulationTarget Id is 0 - verified 2026-10-15), so the targets below were read from the
 * rack's Map view in Live and rebuilt here with the same devices. Each macro drives 1..N of our
 * parameters; values are written back into the parameter cells so the controls follow on screen. */
/* curve types: linear in the parameter's own unit, or geometric (equal ratios per macro step), which
 * is what the original's moving EQ crossovers follow (50 -> 324 -> 2030 Hz, 18 k -> 6.2 k -> 2.18 kHz). */
#define MC_LIN  0
#define MC_GEOM 1
/* MC_EQ: the fitted EQ curve. The original's kill knob is not linear in dB - measured 2026-10-16, its low
 * band is already 6.6 dB down after 12 % of the travel and 17 dB at half, which dB = lo * (1-m)^0.6
 * reproduces (12 % -> -6.9 dB, half -> -16.3 dB). The exponent ends exactly at the bottom of the range,
 * where the isolator's kill takes over. */
#define MC_EQ   2
/* The EQ kill curve, read out of Live's own EQ Three on 2026-10-16 (macro -> GainLo -> dB, no audio in
 * the loop): 127 -> 0.00, 111 -> -4.28, 95 -> -8.56, 79 -> -12.8, 63 -> -17.1, 47 -> -23.0, 31 -> -33.5,
 * 15 -> -48.5, 0 -> -inf dB. It is much steeper at the bottom than any dB power law fitted to audio
 * measurements (those saturate and disagreed with each other); this table is the original's own curve. */
static const float MC_EQ_DB[9] = { -60.0f, -48.5f, -33.5f, -23.0f, -17.1f, -12.8f, -8.56f, -4.28f, 0.0f };
static const float MC_EQ_M[9]  = { 0.0f, 15.0f/127.0f, 31.0f/127.0f, 47.0f/127.0f, 63.0f/127.0f,
                                   79.0f/127.0f, 95.0f/127.0f, 111.0f/127.0f, 1.0f };
typedef struct { int param; float lo, hi; int curve; } macro_map_t;

static const macro_map_t MAP_LO[]     = { { P_ISO_LO,  -24.0f, 0.0f, MC_EQ } };  /* fitted: -24*(1-m)^0.6 */
static const macro_map_t MAP_MID[]    = { { P_ISO_MID, -24.0f, 0.0f, MC_EQ } };
static const macro_map_t MAP_HI[]     = { { P_ISO_HI,  -24.0f, 0.0f, MC_EQ } };
/* LPF: measured 2026-10-15 in Live - macro 0 = 26.0 Hz (closed), 64 = 738 Hz, 127 = 19.9 kHz
 * (open). Frequency only; the original maps no resonance. Neutral default is therefore 1.0. */
static const macro_map_t MAP_LPF[]    = { { P_DJ_FILTER, -1.0f, 0.0f } };
/* GLITCH: the original's Glitch-Dub rack is a **serial insert** - "a fully wet Delay into a Grain Delay"
 * (PORTING_NOTES, chain dump 2026-10-15) - so at the top of the macro the direct sound is REPLACED, not
 * doubled. That is what `mix` is for here: without it the grain lives in a bus nothing can hear, which is
 * why this macro did nothing at all until 2026-10-16 (proved: output bit-identical at macro 0 and 1).
 * The `mix` span is authored (the sub-rack's rack-level dry/wet was not measured); everything else is
 * measured: Delay 100 % wet, Grain on, 1 -> 150 Hz, wet 0 -> 35 %, time 22 -> 2 ms. `dly_mix` is NOT a
 * macro target: our wet bus is parallel (the original's sub-racks are serial), and once `mix` replaces the
 * direct signal the delay's own dry/wet and the send amount are the same control - mapping both would
 * apply it twice. It stays a manual knob on the DELAY page (default 1.0 = the fully wet bus every macro
 * was fitted against, so a fresh instance reads 100 %, not 0 %). */
static const macro_map_t MAP_GLITCH[] = { { P_MIX, 0.0f, 1.0f },          /* the serial insert of the rack */
                                          { P_GR_RATE, 1.0f, 150.0f },    /* measured: 1 -> 150 Hz */
                                          { P_GR_TIME, 22.0f, 2.0f },     /* measured: 22 -> 2 ms */
                                          { P_GR_MIX, 0.0f, 0.35f } };    /* measured: 0 -> 35 % */
/* VERB: the original brings in a Delay (on, fb 95 %, wet 40 %), an HPF sweep (26 Hz..15.4 kHz, reso up)
 * and the Reverb (dry/wet 0..100 %). Read out of Live on 2026-10-16: reverb dry/wet 0 -> 100 %,
 * delay feedback 0 -> 95 %, HPF 26 Hz -> 648 Hz -> 15.4 kHz (geometric), Q 0.51 -> 2.50. The HPF is a
 * second dub_filter (`vhf`, hp-only) on the wet path between the delay and the reverb - the position the
 * original's delay filter sits in; it is not the DJ filter, which belongs to mac_lpf. The measured
 * resonance is a Q, and our hp_reso 0..1 == Q 0.50..2.50, so the read-out's 0.51 is our 0.0 (below the
 * measurement's precision). */
static const macro_map_t MAP_VERB[]   = { { P_VERB_AMOUNT, 0.0f, 1.00f }, { P_VERB_PREDELAY, 20.0f, 35.0f },
                                          { P_DLY_FBK, 0.55f, 0.95f },
                                          { P_VHF_HZ, 26.0f, 15400.0f, MC_GEOM }, { P_VHF_RESO, 0.0f, 1.0f },
                                          { P_MIX, 0.0f, 1.00f } };
/* FADE ("Fade to Grey"): corrected 2026-10-15 after the T15 spike. The original does NOT band-pass -
 * GainLo/GainHi stay at 0 dB and only the EQ crossovers travel together (50 Hz/18 kHz -> 2.03 kHz/
 * 2.18 kHz) while GainMid dips to -6.2 dB; the audible weight is the delay throw (95 % feedback,
 * 95 % wet). v1's iso_lo/iso_hi kill was wrong (far too filtered). We move the isolator's own
 * crossovers geometrically. The ride starts at OUR defaults (250 Hz / 2.5 kHz), not at the
 * original's 50 Hz / 18 kHz: the measured ride start would otherwise move the isolator's crossovers at
 * macro 0 and colour the neutral state (found by the A/B measurement on 2026-10-16). and calibrate the
 * mid dip to -8 dB because our mid band is the difference of two 4-pole low-passes and therefore dips
 * shallower than the original's EQ Three for the same parameter value. Both ends stay open. */
static const macro_map_t MAP_FADE[]   = { { P_ISO_LO_X, 250.0f, 2030.0f, MC_GEOM },
                                          { P_ISO_HI_X, 2500.0f, 2180.0f, MC_GEOM },
                                          { P_ISO_MID, 0.0f, -8.0f },
                                          { P_DLY_FBK, 0.55f, 0.95f },
                                          { P_MIX, 0.0f, 0.95f } };
/* AUTO-FILL: measured - only the Beat Repeat's Device On and Grid are macro-driven (Grid 1/6 -> 1/32 in
 * the original, ours Off .. 1/32). Gate/Chance keep their hand-set values (4/16 = 25 %, 100 %) - not mapped. */
/* The measured Auto-Fill: it walks the grid and leaves the hand-set gate/repeats alone. The stage is
 * restored by the `fill_mode` switch, not by this table, so the measured behaviour stays exactly what it
 * was before the stutter existed. */
static const macro_map_t MAP_FILL[]   = { { P_REP_MODE, 0.0f, 7.0f } };

/* AUTO-FILL, the owner's stutter experiment (2026-10-16): same grid walk, but the slices play in full
 * and hold for the whole bar instead of stopping after `count` repeats - a tempo-locked roll, the way
 * the Roland grooveboxes' stutter behaves. `rep_count` 0 is the hold (repeat.c). It is a separate row
 * set behind the `fill_mode` switch, so the measured Fill behaviour stays one click away - the owner
 * asked for a fallback rather than a replacement. */
static const macro_map_t MAP_FILL_STUTTER[] = { { P_REP_MODE, 0.0f, 7.0f },
                                                { P_REP_GATE, 0.25f, 1.00f },
                                                { P_REP_COUNT, 4.0f, 0.0f } };   /* 4 repeats -> hold */

typedef struct { const macro_map_t *m; int n; } macro_t;
#define N_MACROS 8
static const macro_t MACROS[N_MACROS] = {
    { MAP_LO, 1 }, { MAP_MID, 1 }, { MAP_HI, 1 }, { MAP_LPF, 1 },
    { MAP_GLITCH, 4 }, { MAP_VERB, 6 }, { MAP_FADE, 5 }, { MAP_FILL, 1 },
};
static const macro_t FILL_STUTTER_MACRO = { MAP_FILL_STUTTER, 3 };

/* which row set a macro uses right now (only Auto-Fill depends on a switch) */
static const macro_t *macro_for(const ctx_t *c, int idx)
{
    if (idx == (P_MAC_FILL - P_MAC_LO) && atoi(c->val[P_FILL_MODE]) == 1) return &FILL_STUTTER_MACRO;
    return &MACROS[idx];
}

static int key_idx(const char *k)
{
    for (int i = 0; i < NKEYS; i++)
        if (!strcmp(k, KEYS[i])) return i;
    return -1;
}

static float effective_time_ms(ctx_t *c)
{
    float ms = (float)atof(c->val[P_DLY_TIME]);
    if (atoi(c->val[P_DLY_SYNC]) == 1 && c->bpm > 1.0f) {
        int d = atoi(c->val[P_DLY_DIV]);
        if (d < 0 || d >= N_DIV) d = 0;
        ms = DIV_BEATS[d] * 60000.0f / c->bpm;
    }
    return ms;
}

static void flush_tails(ctx_t *c)
{
    dub_delay_reset(c->delay);
    dub_reverb_reset(c->rv);
    repeat_reset(c->rep);
    flanger_reset(c->fl);
    iso_reset(c->iso);
    dub_filter_reset(c->djf);
    dub_filter_reset(c->vhf);
}

/* the numeric part of a parameter change; the macro uses it too */
static void apply_macro(ctx_t *c, int idx, float m);   /* defined below; fill_mode re-runs it */

/* A parameter's default in its OWN unit. vst/params.json is the single source (the generated PARAMS[]
 * carries it, normalised 0..1: an option index for an enum, a position in [min,max] otherwise), so this
 * is the wrapper's own norm_to_str() conversion - not a second table that can drift from it. */
static float param_default(int i)
{
    const param_t *p = &PARAMS[i];
    return (p->nopts > 0) ? (float)lroundf(p->def * (float)(p->nopts - 1))
                          : p->min + p->def * (p->max - p->min);
}

static void apply_scalar(ctx_t *c, int i, float f)
{
    switch (i) {
        case P_ISO_LO:     iso_set_lo_db(c->iso, f); break;
        case P_ISO_MID:    iso_set_mid_db(c->iso, f); break;
        case P_ISO_HI:     iso_set_hi_db(c->iso, f); break;
        case P_ISO_LO_X:   iso_set_lo_x(c->iso, f); break;
        case P_ISO_HI_X:   iso_set_hi_x(c->iso, f); break;

        case P_DJ_RESO:    dub_filter_set_resonance(c->djf, f); break;
        case P_DJ_FILTER: {
            const int on = (fabsf(f) > 0.001f);
            dub_filter_set_enable(c->djf, on);
            if (on) {
                if (f < 0.0f) {
                    const float t = -f;
                    dub_filter_set_hpf(c->djf, 20.0f);
                    dub_filter_set_lpf(c->djf, 20000.0f * powf(26.0f / 20000.0f, t));   /* 20k .. 26 Hz (as the original) */
                } else {
                    const float t = f;
                    dub_filter_set_lpf(c->djf, 20000.0f);
                    dub_filter_set_hpf(c->djf, 20.0f * powf(400.0f, t));     /* 20 .. 8000 Hz */
                }
            }
            break;
        }

        case P_REP_MODE:   repeat_set_mode(c->rep, (int)lrintf(f)); break;
        case P_REP_GATE:   repeat_set_gate(c->rep, f); break;
        case P_REP_CHANCE: repeat_set_chance(c->rep, f); break;
        case P_REP_COUNT:  repeat_set_count(c->rep, (int)lrintf(f)); break;
        case P_REP_MIX:    repeat_set_mix(c->rep, f); break;
        case P_FILL_MODE: {
            /* The switch selects a different Auto-Fill row set. It also has to put the repeat stage back
             * into its defaults first: the stutter opens the gate and holds the repeats, so switching
             * back would otherwise leave that behind - and a fallback that leaves the gate open is not a
             * fallback. The defaults come from params.json (PARAMS[]), like every other default now. */
            const int stage[2] = { P_REP_GATE, P_REP_COUNT };
            for (int k = 0; k < 2; k++) {
                const float d = param_default(stage[k]);
                snprintf(c->val[stage[k]], VALLEN, "%g", (double)d);
                apply_scalar(c, stage[k], d);
            }
            apply_macro(c, P_MAC_FILL - P_MAC_LO, (float)atof(c->val[P_MAC_FILL]));
            break;
        }

        case P_DLY_TIME: case P_DLY_SYNC: case P_DLY_DIV:
            dub_delay_set_time_ms(c->delay, effective_time_ms(c)); break;
        case P_DLY_FBK:      dub_delay_set_feedback(c->delay, f); break;
        case P_DLY_TONE:     dub_delay_set_tone(c->delay, f); break;
        case P_DLY_MIX: {    /* the delay's own dry/wet inside the wet bus (measured: VERB 0->40 %,
                              * Glitch 50->100 %, Fade 0->95 %). Default 1.0 = the bus is a fully wet
                              * delay, which is what VERB/FADE were fitted against before. */
            const float w = db_clampf(f, 0.0f, 1.0f);
            dub_delay_set_wet(c->delay, w);
            dub_delay_set_dry(c->delay, 1.0f - w);
            break;
        }
        case P_DLY_PINGPONG: dub_delay_set_ping_pong(c->delay, (int)lrintf(f)); break;

        case P_VERB_MODE:      dub_reverb_set_mode(c->rv, (int)lrintf(f)); break;
        case P_VERB_AMOUNT:    dub_reverb_set_amount(c->rv, f); break;
        case P_VERB_DECAY:     dub_reverb_set_decay(c->rv, f); break;
        case P_VERB_DAMP:      dub_reverb_set_damping(c->rv, f); break;
        case P_VERB_PREDELAY:  dub_reverb_set_predelay(c->rv, db_clampf(f / 200.0f, 0.0f, 1.0f)); break;

        case P_VHF_HZ:   dub_filter_set_hpf(c->vhf, f); break;
        case P_VHF_RESO: dub_filter_set_resonance(c->vhf, f); break;

        case P_FL_RATE:  flanger_set_rate(c->fl, f); break;
        case P_FL_DEPTH: flanger_set_depth(c->fl, f); break;
        case P_FL_FBK:   flanger_set_fbk(c->fl, f); break;
        case P_FL_MIX:   flanger_set_mix(c->fl, f); break;

        case P_GR_RATE:  grain_set_rate(c->gr, f); break;
        case P_GR_TIME:  grain_set_time(c->gr, f); break;
        case P_GR_SPRAY: grain_set_spray(c->gr, f); break;
        case P_GR_MIX:   grain_set_mix(c->gr, f); break;

        case P_OUTPUT_GAIN: c->out_gain = powf(10.0f, f / 20.0f); break;
        case P_LIMITER:     limiter_set_on(c->lim, (int)lrintf(f)); break;
        case P_PANIC:
            if (f > 0.5f && c->panic_cd <= 0) {
                c->panic_state = 1;                   /* the flush itself is staged over the next blocks */
                c->panic_cd = (int)(0.20f * c->fs);   /* debounce: one flush per 200 ms */
            }
            break;

        case P_MIX: c->mix = db_clampf(f, 0.0f, 1.0f); break;
        default: break;
    }
}

/* Evaluate one macro row at macro position m (0..1). Kept separate from apply_macro because the shared
 * cells need a row's value without writing it. */
static float map_eval(const macro_map_t *r, float m)
{
    const float lo = r->lo, hi = r->hi;
    if (r->curve == MC_GEOM && lo > 0.0f && hi > 0.0f) return lo * powf(hi / lo, m);
    if (r->curve == MC_EQ) {
        /* table lookup with linear interpolation between the measured points */
        float db = MC_EQ_DB[8];
        for (int t = 1; t < 9; t++) {
            if (m <= MC_EQ_M[t]) {
                const float f = (m - MC_EQ_M[t - 1]) / (MC_EQ_M[t] - MC_EQ_M[t - 1]);
                db = MC_EQ_DB[t - 1] + f * (MC_EQ_DB[t] - MC_EQ_DB[t - 1]);
                break;
            }
        }
        /* below -23.9 the isolator kills anyway, and the parameter range ends at lo (-24) */
        return (db <= -23.9f) ? lo : db;
    }
    return lo + (hi - lo) * m;
}

/* Cells that MORE THAN ONE macro writes. Each macro used to write its own value, so the last one used
 * won the cell: with Verb up, moving Fade to 0 muted the wet bus the reverb lives in, and moving Verb
 * did the same to Fade ("im moment springt Verb wenn ich Fade nutze", owner on the device, 2026-10-16).
 * They are now derived from all eight macro positions at once - the row values still come from the
 * measured tables above, only the combination changed:
 *   - `mix` is a set of parallel sends, so it combines like one: 1 - product(1 - a_i).
 *   - `dly_fbk` takes the largest demand (Verb and Fade map it identically, 0.55 -> 0.95). */
static int is_shared_param(int p) { return p == P_MIX || p == P_DLY_FBK; }

static void apply_shared(ctx_t *c)
{
    float mix = 0.0f, fbk = -1.0f;
    for (int i = 0; i < N_MACROS; i++) {
        const macro_t *M = macro_for(c, i);
        const float m = db_clampf((float)atof(c->val[P_MAC_LO + i]), 0.0f, 1.0f);
        for (int k = 0; k < M->n; k++) {
            const macro_map_t *r = &M->m[k];
            if (r->param == P_MIX) {
                const float a = db_clampf(map_eval(r, m), 0.0f, 1.0f);
                mix = 1.0f - (1.0f - mix) * (1.0f - a);
            } else if (r->param == P_DLY_FBK) {
                const float a = map_eval(r, m);
                if (a > fbk) fbk = a;
            }
        }
    }
    snprintf(c->val[P_MIX], VALLEN, "%g", (double)mix);
    apply_scalar(c, P_MIX, mix);
    if (fbk >= 0.0f) {
        snprintf(c->val[P_DLY_FBK], VALLEN, "%g", (double)fbk);
        apply_scalar(c, P_DLY_FBK, fbk);
    }
}

/* write one macro's targets and apply them */
static void apply_macro(ctx_t *c, int idx, float m)
{
    if (idx < 0 || idx >= N_MACROS) return;
    m = db_clampf(m, 0.0f, 1.0f);
    const macro_t *M = macro_for(c, idx);
    int shared = 0;
    for (int k = 0; k < M->n; k++) {
        const int p = M->m[k].param;
        if (is_shared_param(p)) { shared = 1; continue; }   /* derived from all macros, see apply_shared */
        const float v = map_eval(&M->m[k], m);
        snprintf(c->val[p], VALLEN, "%g", (double)v);
        apply_scalar(c, p, v);
    }
    if (shared) apply_shared(c);
}

static void apply(ctx_t *c, int i)
{
    const float f = (float)atof(c->val[i]);
    if (i >= P_MAC_LO && i <= P_MAC_FILL) apply_macro(c, i - P_MAC_LO, f);
    else                                  apply_scalar(c, i, f);
}

static void set_state(ctx_t *c, const char *s)
{
    const char *p = s;
    while (*p) {
        const char *nl = strchr(p, '\n');
        const int linelen = nl ? (int)(nl - p) : (int)strlen(p);
        const char *eq = (const char *)memchr(p, '=', (size_t)linelen);
        if (eq) {
            const int kl = (int)(eq - p);
            if (kl > 0 && kl < 64) {
                char key[64];
                memcpy(key, p, (size_t)kl);
                key[kl] = 0;
                const int i = key_idx(key);
                if (i >= 0) {
                    int vl = linelen - (kl + 1);
                    if (vl < 0) vl = 0;
                    if (vl > VALLEN - 1) vl = VALLEN - 1;
                    memcpy(c->val[i], eq + 1, (size_t)vl);
                    c->val[i][vl] = 0;
                    /* A macro is stored for the panel, but NOT re-applied on load: it owns its
                     * targets, and re-running it would overwrite the target values that were saved
                     * beside it (same reasoning as DubStation's stored `preset` selection). */
                    if (!(i >= P_MAC_LO && i <= P_MAC_FILL)) apply(c, i);
                }
            }
        }
        if (!nl) break;
        p = nl + 1;
    }
}

static void build_dsp(ctx_t *c)
{
    c->iso   = iso_create(c->fs);
    c->djf   = dub_filter_create(c->fs);
    c->vhf   = dub_filter_create(c->fs);
    c->rep   = repeat_create(c->fs);
    c->delay = dub_delay_create(c->fs);
    c->rv    = dub_reverb_create(c->fs);
    c->fl    = flanger_create(c->fs);
    c->gr    = grain_create(c->fs);
    c->lim   = limiter_create(c->fs);
    if (c->delay) {
        dub_delay_set_filter(c->delay, NULL);
        dub_delay_set_wet(c->delay, 1.0f);
        dub_delay_set_dry(c->delay, 0.0f);
    }
    if (c->rv) dub_reverb_set_enable(c->rv, 1);
    if (c->vhf) {
        dub_filter_set_enable(c->vhf, 1);
        dub_filter_set_hp_only(c->vhf, 1);   /* pure high-pass: no LPF, no drive (see dub_filter.h) */
    }
    if (c->lim) limiter_set_ceiling(c->lim, -0.3f);
}

static void free_dsp(ctx_t *c)
{
    iso_destroy(c->iso);
    dub_filter_destroy(c->djf);
    dub_filter_destroy(c->vhf);
    repeat_destroy(c->rep);
    dub_delay_destroy(c->delay);
    dub_reverb_destroy(c->rv);
    flanger_destroy(c->fl);
    grain_destroy(c->gr);
    limiter_destroy(c->lim);
    c->iso = NULL; c->djf = NULL; c->vhf = NULL; c->rep = NULL; c->delay = NULL;
    c->rv = NULL; c->fl = NULL; c->lim = NULL;
}

static int dsp_ok(const ctx_t *c)
{
    return c->iso && c->djf && c->vhf && c->rep && c->delay && c->rv && c->fl && c->gr && c->lim;
}

static void *create(const char *data_dir)
{
    (void)data_dir;
    ctx_t *c = (ctx_t *)calloc(1, sizeof *c);
    if (!c) return NULL;
    c->fs = 44100.0f;
    c->mix = 0.35f;
    c->out_gain = 1.0f;
    c->vu_step = -1;
    for (int k = 0; k < 3; k++) c->met_step[k] = -1;
    build_dsp(c);
    if (!dsp_ok(c)) { free_dsp(c); free(c); return NULL; }
    for (int i = 0; i < NKEYS; i++) {
        const float raw = param_default(i);
        snprintf(c->val[i], VALLEN, "%g", (double)raw);
        apply(c, i);
    }
    return c;
}

static void destroy(void *inst)
{
    ctx_t *c = inst;
    if (!c) return;
    free_dsp(c);
    free(c);
}

static void midi(void *inst, const uint8_t *msg, int len) { (void)inst; (void)msg; (void)len; }

static void engine_set_sample_rate(void *inst, float fs);
static void engine_reset(void *inst);

static void set_param(void *inst, const char *key, const char *val)
{
    ctx_t *c = inst;
    if (!c) return;
    if (!strcmp(key, "state")) { set_state(c, val); return; }
    if (!strcmp(key, "sample_rate")) { engine_set_sample_rate(c, (float)atof(val)); return; }
    if (!strcmp(key, "reset")) { engine_reset(c); return; }
    if (!strcmp(key, "lfo_bpm")) { c->bpm = (float)atof(val); repeat_set_bpm(c->rep, c->bpm); apply(c, P_DLY_TIME); return; }
    const int i = key_idx(key);
    if (i < 0) return;
    snprintf(c->val[i], VALLEN, "%s", val);
    apply(c, i);
}

static int get_param(void *inst, const char *key, char *buf, int buf_len)
{
    ctx_t *c = inst;
    if (!c || buf_len <= 0) return 0;
    if (!strcmp(key, "state")) {
        int p = 0;
        for (int i = 0; i < NKEYS; i++) {
            const int n = snprintf(buf + p, (size_t)(buf_len - p), "%s=%s\n", KEYS[i], c->val[i]);
            if (n < 0 || n >= buf_len - p) break;
            p += n;
        }
        return p;
    }
    /* live text (params.json "display": "string"): polled every 100 ms, no numeric value */
    if (!strcmp(key, "dly_show")) return get_param(c, "dly_time_display", buf, buf_len);
    if (!strcmp(key, "out_show")) {
        const float db = 20.0f * log10f(c->vu_env + 1e-6f);
        snprintf(buf, (size_t)buf_len, "%+.1f dBFS", db);
        return 1;
    }
    const int i = key_idx(key);
    if (i < 0) {
        if (!strcmp(key, "display_rev")) {
            /* the meters are published from the 20 Hz tick in process() (2026-10-26): deriving them
             * here capped the needle at the host's 100 ms readout poll and coupled the number of
             * frames to the poll. display_rev just counts what changed since the last poll. */
            snprintf(buf, (size_t)buf_len, "%u", c->display_rev);
            return 1;
        }
        const size_t klen = strlen(key);
        if (klen > 8 && !strcmp(key + klen - 8, "_display")) {
            char base[64];
            const size_t bl = klen - 8;
            if (bl >= sizeof base) return 0;
            memcpy(base, key, bl);
            base[bl] = 0;
            if (!strcmp(base, "dly_time")) {
                const float ms = effective_time_ms(c);
                char t[24];
                if (ms < 1000.0f) snprintf(t, sizeof t, "%.0f ms", ms);
                else              snprintf(t, sizeof t, "%.2f s", ms / 1000.0f);
                if (atoi(c->val[P_DLY_SYNC]) == 1 && c->bpm > 1.0f) {
                    const int d = atoi(c->val[P_DLY_DIV]);
                    const char *lbl = (d >= 0 && d < PARAMS[P_DLY_DIV].nopts) ? PARAMS[P_DLY_DIV].opts[d] : "";
                    snprintf(buf, (size_t)buf_len, "%s (%s)", t, lbl);
                } else {
                    snprintf(buf, (size_t)buf_len, "%s", t);
                }
                return 1;
            }
            const int bi = key_idx(base);
            if (bi < 0) return 0;
            if (!strcmp(base, "hp_reso")) {   /* the knob is 0..1; the original's read-out is a Q */
                snprintf(buf, (size_t)buf_len, "Q %.2f", 0.5f + 2.0f * (float)atof(c->val[bi]));
                return 1;
            }
            if (!strcmp(base, "rep_count")) {   /* 0 is not "no repeats", it is the stutter's hold */
                const int n = atoi(c->val[bi]);
                snprintf(buf, (size_t)buf_len, n == 0 ? "HOLD" : "%d", n);
                return 1;
            }
            snprintf(buf, (size_t)buf_len, "%.0f %%", (float)atof(c->val[bi]) * 100.0f);
            return 1;
        }
        return 0;
    }
    snprintf(buf, (size_t)buf_len, "%s", c->val[i]);
    return 1;
}

static void render(void *inst, int16_t *out_lr, int frames)
{
    (void)inst;
    if (frames < 0) frames = 0;
    if (frames > MAXFRAMES) frames = MAXFRAMES;
    memset(out_lr, 0, (size_t)frames * 2 * sizeof(int16_t));
}

static void process(void *inst, const int16_t *in_lr, int16_t *out_lr, int frames)
{
    ctx_t *c = inst;
    if (!c || frames <= 0) return;
    if (frames > MAXFRAMES) frames = MAXFRAMES;

    for (int i = 0; i < frames; i++) {
        c->l[i] = (float)in_lr[2 * i]     * (1.0f / 32768.0f);
        c->r[i] = (float)in_lr[2 * i + 1] * (1.0f / 32768.0f);
    }

    /* full-signal section: isolator -> DJ filter -> beat repeat */
    iso_process(c->iso, c->l, c->r, frames);
    dub_filter_process(c->djf, c->l, c->r, frames);
    repeat_process(c->rep, c->l, c->r, frames);
    float rep_pk = 0.0f;
    for (int i = 0; i < frames; i++) {
        const float a = fabsf(c->l[i]), b = fabsf(c->r[i]);
        if (a > rep_pk) rep_pk = a;
        if (b > rep_pk) rep_pk = b;
    }

    /* wet path: delay (wet only) -> VERB high-pass -> reverb -> flanger */
    dub_delay_process(c->delay, c->l, c->r, c->w1, c->w2, frames);
    float dly_pk = 0.0f;
    for (int i = 0; i < frames; i++) {
        const float a = fabsf(c->w1[i]), b = fabsf(c->w2[i]);
        if (a > dly_pk) dly_pk = a;
        if (b > dly_pk) dly_pk = b;
    }
    dub_filter_process(c->vhf, c->w1, c->w2, frames);
    dub_reverb_process(c->rv, c->w1, c->w2, frames);
    float verb_pk = 0.0f;
    for (int i = 0; i < frames; i++) {
        const float a = fabsf(c->w1[i]), b = fabsf(c->w2[i]);
        if (a > verb_pk) verb_pk = a;
        if (b > verb_pk) verb_pk = b;
    }
    flanger_process(c->fl, c->w1, c->w2, frames);
    grain_process(c->gr, c->w1, c->w2, frames);

    const float wet = c->mix, dry = 1.0f - c->mix;
    float pk = 0.0f;
    for (int i = 0; i < frames; i++) {
        c->l[i] = db_sanitize(c->l[i] * dry + c->w1[i] * wet) * c->out_gain;
        c->r[i] = db_sanitize(c->r[i] * dry + c->w2[i] * wet) * c->out_gain;
        const float al = fabsf(c->l[i]), ar = fabsf(c->r[i]);
        if (al > pk) pk = al;
        if (ar > pk) pk = ar;
    }
    limiter_process(c->lim, c->l, c->r, frames);

    /* panic: clear the module buffers in stages, one module group per block, so a single block never
     * memsets several MB (delay + repeat buffers are ~2.8 + ~1.8 MB, which would blow the block budget
     * on ARM). The output is silenced while flushing - that is what panic means. */
    if (c->panic_state) {
        switch (c->panic_state) {
            case 1: iso_reset(c->iso); dub_filter_reset(c->djf); dub_filter_reset(c->vhf); flanger_reset(c->fl); dub_reverb_reset(c->rv); break;
            case 2: repeat_reset(c->rep); grain_reset(c->gr); break;
            default: dub_delay_reset(c->delay); break;
        }
        c->panic_state = (c->panic_state >= 3) ? 0 : c->panic_state + 1;
        memset(out_lr, 0, (size_t)frames * 2 * sizeof(int16_t));
        return;
    }

    for (int i = 0; i < frames; i++) {
        long a = lrintf(db_hard_clip(c->l[i], 1.0f) * 32768.0f);
        long b = lrintf(db_hard_clip(c->r[i], 1.0f) * 32768.0f);
        if (a > 32767) a = 32767; else if (a < -32768) a = -32768;
        if (b > 32767) b = 32767; else if (b < -32768) b = -32768;
        out_lr[2 * i]     = (int16_t)a;
        out_lr[2 * i + 1] = (int16_t)b;
    }

    /* master VU + stage meters: peak with a slow release */
    c->vu_env   = pk      > c->vu_env   ? pk      : c->vu_env   * 0.9995f;
    c->dly_env  = dly_pk  > c->dly_env  ? dly_pk  : c->dly_env  * 0.9995f;
    c->verb_env = verb_pk > c->verb_env ? verb_pk : c->verb_env * 0.9995f;
    c->rep_env  = rep_pk  > c->rep_env  ? rep_pk  : c->rep_env  * 0.9995f;

    /* 20 Hz: publish VU + the three stage meters. The host re-reads a value when display_rev changed
     * (DISPLAY_REV_NO_UPDATE), so one counter serves all four. */
    c->met_tick += frames;
    if (c->met_tick >= (int)(c->fs / 20.0f)) {
        c->met_tick = 0;
        struct { float env; int *step; int idx; int top; } m[4] = {
            { c->vu_env,   &c->vu_step,      P_VU,        VU_STEPS  },
            { c->dly_env,  &c->met_step[0],  P_DLY_TAIL,  MET_STEPS },
            { c->verb_env, &c->met_step[1],  P_VERB_TAIL, MET_STEPS },
            { c->rep_env,  &c->met_step[2],  P_REP_ACT,   MET_STEPS },
        };
        for (int k = 0; k < 4; k++) {
            const int top = m[k].top;
            int st = (int)lrintf((20.0f * log10f(m[k].env + 1e-6f) - VU_FLOOR_DB)
                                 / (0.0f - VU_FLOOR_DB) * (float)(top - 1));
            if (st < 0) st = 0;
            if (st >= top) st = top - 1;
            if (st != *m[k].step) {
                *m[k].step = st;
                snprintf(c->val[m[k].idx], VALLEN, "%d", st);
                c->pending |= 1ull << m[k].idx;   /* wrapper reports it (engine.h hook), no 100 ms poll */
            }
        /* live text is pushed too: nothing bumps display_rev any more, so a text param would otherwise
         * never be re-read (out_show follows the meter, dly_show the delay time/division/tempo) */
        c->pending |= (1ull << P_OUT_SHOW) | (1ull << P_DLY_SHOW);
        }
    }
    if (c->panic_cd > 0) { c->panic_cd -= frames; if (c->panic_cd < 0) c->panic_cd = 0; }
}

static void engine_set_sample_rate(void *inst, float fs)
{
    ctx_t *c = inst;
    if (!c || !(fs > 8000.0f && fs < 192000.0f) || fabsf(fs - c->fs) < 0.5f) return;
    free_dsp(c);
    c->fs = fs;
    build_dsp(c);
    if (!dsp_ok(c)) return;
    for (int i = 0; i < NKEYS; i++) apply(c, i);
}

static void engine_reset(void *inst)
{
    ctx_t *c = inst;
    if (!c) return;
    flush_tails(c);
    limiter_reset(c->lim);
}

/* engine.h's push hook: the wrapper asks once per block which keys the engine changed by itself and tells the
 * host as it does for a MIDI CC (at most every 1024 frames per control = ~23 ms), which is the documented way to
 * drive a filmstrip meter. Visibility matters: the build uses -fvisibility=hidden. */
__attribute__((visibility("default")))
int mpc_engine_changed(void *inst, char *key, int len)
{
    ctx_t *c = inst;
    if (!c || !key || len <= 0) return 0;
    for (int i = 0; i < NKEYS; i++) {
        if (c->pending & (1ull << i)) {
            c->pending &= ~(1ull << i);
            snprintf(key, (size_t)len, "%s", KEYS[i]);
            return 1;
        }
    }
    return 0;
}

static const mpc_engine_t ENGINE = { create, destroy, midi, set_param, get_param, render, process,
                                     NULL /* song_pos: HAS_SONG_POS unset */ };
const mpc_engine_t *mpc_engine(void) { return &ENGINE; }
