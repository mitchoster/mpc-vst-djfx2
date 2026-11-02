/* dlclose_shim.cpp — the calls dlclose_test.cpp needs, exported unmangled so the host can
 * dlsym() them. It is the whole plugin lifecycle that matters: attach (start the listener), one
 * publish, detach (which must stop and join the thread), then the host dlclose()s this file.
 * If anything still runs in here after dlclose, the process dies - which is exactly what MPC did
 * on the device on 2026-11-01. */
#include "../src/dsp/companion.h"

extern "C" int tc_attach() { return djf2::attach(); }

extern "C" void tc_publish(int slot) {
  djf2::Snapshot s;
  s.vu = 0.5f;
  s.peak = 0.6f;
  s.delay_ms = 250.0f;
  s.bpm = 120.0f;
  s.mix = 0.35f;
  s.iso_lo = 0.0f;
  s.iso_mid = -3.0f;
  s.iso_hi = 2.0f;
  s.iso_lo_x = 250.0f;
  s.iso_hi_x = 2500.0f;
  s.dj_filter = 0.0f;
  s.dj_reso = 0.2f;
  s.rep_mode = 3;
  s.rep_gate = 0.25f;
  s.rep_chance = 1.0f;
  s.rep_count = 4;
  s.rep_mix = 0.0f;
  s.fill_mode = 0;
  s.dly_time = 400.0f;
  s.dly_sync = 0;
  s.dly_div = 3;
  s.dly_fbk = 0.4f;
  s.dly_tone = 0.5f;
  s.dly_mix = 1.0f;
  s.dly_pingpong = 0;
  s.verb_mode = 0;
  s.verb_amount = 0.0f;
  s.verb_decay = 0.5f;
  s.verb_damp = 0.5f;
  s.verb_predelay = 20.0f;
  s.hp_hz = 20.0f;
  s.hp_reso = 0.0f;
  s.fl_rate = 0.2f;
  s.fl_depth = 0.4f;
  s.fl_fbk = 0.3f;
  s.fl_mix = 0.0f;
  s.gr_rate = 20.0f;
  s.gr_time = 80.0f;
  s.gr_spray = 0.2f;
  s.gr_mix = 0.0f;
  s.mac_lo = 0.0f;
  s.mac_mid = 0.0f;
  s.mac_hi = 0.0f;
  s.mac_lpf = 0.0f;
  s.mac_glitch = 0.0f;
  s.mac_verb = 0.0f;
  s.mac_fade = 0.0f;
  s.mac_fill = 0.0f;
  s.output_gain = 0.0f;
  s.limiter = 1;
  s.rep_env = 0.3f;
  s.dly_env = 0.2f;
  s.verb_env = 0.1f;
  djf2::publish(slot, s);
}

extern "C" void tc_detach(int slot) { djf2::detach(slot); }
