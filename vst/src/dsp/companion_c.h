/* companion_c.h — the plain-C face of the companion feed, for src/djfx_engine.c.
 *
 * The engine is C and the server is C++ (atomics, a namespace), so the engine never includes
 * companion.h. This header carries the POD snapshot the engine fills and the four calls it
 * makes; companion.cpp implements them as extern "C" wrappers over the C++ namespace.
 * Fields are generated from the same list as the JSON, so the two sides cannot drift.
 */
#ifndef DJF2_COMPANION_C_H
#define DJF2_COMPANION_C_H

typedef struct {
    float vu;
    float peak;
    float delay_ms;
    float bpm;
    int   bpm_seen;
    float mix;
    float iso_lo;
    float iso_mid;
    float iso_hi;
    float iso_lo_x;
    float iso_hi_x;
    float dj_filter;
    float dj_reso;
    float rep_gate;
    float rep_chance;
    float rep_mix;
    float dly_time;
    float dly_fbk;
    float dly_tone;
    float dly_mix;
    float verb_amount;
    float verb_decay;
    float verb_damp;
    float verb_predelay;
    float hp_hz;
    float hp_reso;
    float fl_rate;
    float fl_depth;
    float fl_fbk;
    float fl_mix;
    float gr_rate;
    float gr_time;
    float gr_spray;
    float gr_mix;
    float mac_lo;
    float mac_mid;
    float mac_hi;
    float mac_lpf;
    float mac_glitch;
    float mac_verb;
    float mac_fade;
    float mac_fill;
    float output_gain;
    float rep_env;
    float dly_env;
    float verb_env;
    int   rep_mode;
    int   rep_count;
    int   fill_mode;
    int   dly_sync;
    int   dly_div;
    int   dly_pingpong;
    int   verb_mode;
    int   limiter;
} djfx_snap;

/* C linkage: the engine is C, the server is C++, and these five must meet in the middle. */
#ifdef __cplusplus
extern "C" {
#endif

void djfx_companion_register_keys(const char *const *keys, int count);
int  djfx_companion_attach(void);            /* slot, or -1 when off / all slots taken */
void djfx_companion_detach(int slot);
void djfx_companion_publish(int slot, const djfx_snap *s);
int  djfx_companion_take(int slot, int *idx, float *val, int max);

#ifdef __cplusplus
}
#endif

#endif
