/* companion.h — the read/write feed behind the companion page (phone/tablet).
 *
 * Why this exists: MPC draws a plugin's screen from pre-rendered PNGs and polled values, and
 * every moving widget costs it a redraw. A dense, touch-shaped panel therefore belongs in a
 * browser on the same network, served by the plugin itself.
 *
 * Contract:
 *   - attach() claims one of kSlots static slots and starts the HTTP thread on the first call;
 *     detach() releases the slot and, when it was the last one, STOPS AND JOINS the listener
 *     (a thread parked in poll() after dlclose() takes MPC down).
 *   - publish() is called from the audio thread: stores only, no allocation, no locks.
 *   - The reader (HTTP thread) never dereferences an instance: all state lives in the static
 *     slots, so destroy() can never race the server into freed memory.
 *   - Writes arrive as Commands over a ring the audio thread drains; the DSP state belongs to
 *     the render loop, so set_param() is never called from the HTTP thread.
 */
#ifndef DJF2_COMPANION_H
#define DJF2_COMPANION_H

namespace djf2 {

const int kSlots = 4;

struct Snapshot {
  float delay_ms, bpm, mix;
  int   bpm_seen;  /* 0 = the host never reported a tempo; bpm is then not a real number */   /* the numbers that explain the picture */
  float vu;  /* meter: record/echo level 0..1 */
  float peak;  /* meter: peak since the last publish */
  float iso_lo;  /* isolator low band, dB */
  float iso_mid;  /* isolator mid band, dB */
  float iso_hi;  /* isolator high band, dB */
  float iso_lo_x;  /* low/mid crossover, Hz */
  float iso_hi_x;  /* mid/high crossover, Hz */
  float dj_filter;  /* DJ filter, -1 = HPF sweep .. +1 = LPF sweep (0 = off) */
  float dj_reso;  /* DJ filter resonance 0..1 */
  float rep_gate;  /* beat repeat gate 0..1 */
  float rep_chance;  /* beat repeat chance 0..1 */
  float rep_mix;  /* beat repeat glitch mix 0..1 */
  float dly_time;  /* delay time knob, ms */
  float dly_fbk;  /* delay feedback 0..1 */
  float dly_tone;  /* delay tone 0..1 */
  float dly_mix;  /* delay dry/wet inside the wet bus 0..1 */
  float verb_amount;  /* reverb amount 0..1 */
  float verb_decay;  /* reverb decay 0..1 */
  float verb_damp;  /* reverb damping 0..1 */
  float verb_predelay;  /* reverb predelay, ms */
  float hp_hz;  /* VERB high-pass, Hz */
  float hp_reso;  /* VERB high-pass resonance 0..1 */
  float fl_rate;  /* flanger rate 0..1 */
  float fl_depth;  /* flanger depth 0..1 */
  float fl_fbk;  /* flanger feedback 0..1 */
  float fl_mix;  /* flanger mix 0..1 */
  float gr_rate;  /* grain rate 1..200 */
  float gr_time;  /* grain time, ms */
  float gr_spray;  /* grain spray 0..1 */
  float gr_mix;  /* grain mix 0..1 */
  float mac_lo;  /* macro LOW-EQ 0..1 */
  float mac_mid;  /* macro MID-EQ 0..1 */
  float mac_hi;  /* macro HI-EQ 0..1 */
  float mac_lpf;  /* macro LPF 0..1 */
  float mac_glitch;  /* macro GLITCH 0..1 */
  float mac_verb;  /* macro VERB 0..1 */
  float mac_fade;  /* macro FADE 0..1 */
  float mac_fill;  /* macro AUTO-FILL 0..1 */
  float output_gain;  /* output trim, dB */
  float rep_env;  /* stage meter: repeat 0..1 */
  float dly_env;  /* stage meter: delay 0..1 */
  float verb_env;  /* stage meter: reverb 0..1 */
  int   rep_mode;  /* beat repeat grid index (0 = Off) */
  int   rep_count;  /* beat repeat count 0..8 */
  int   fill_mode;  /* 0 Fill, 1 Stutter */
  int   dly_sync;  /* 0 Free, 1 Sync */
  int   dly_div;  /* sync division index */
  int   dly_pingpong;  /* 0/1 */
  int   verb_mode;  /* reverb program index */
  int   limiter;  /* 0 Off, 1 On */
};

/* A parameter change asked for by the page. The HTTP thread only queues it; the audio thread
 * applies it, because that is where the DSP state lives. */
struct Command { int idx; float val; };

void register_param_keys(const char *const *keys, int count);
int  provide(int slot, const Command &c);
int  take_commands(int slot, Command *out, int max);
int  attach();
void detach(int slot);
void publish(int slot, const Snapshot &s);
int  port();
void stop_server();

}  // namespace djf2

#endif
