# Vendored sources — MPC DJ FX (`DjFx`)

Files under `vst/third_party/` are byte-identical copies of upstream code, kept for provenance and
attribution. **Not all of them are compiled** — see “used as” per entry. Every claimed change is a real
diff against the pinned commit.

## `justrepeat/` — WrongPeople/JustRepeat (beat repeat)

- Upstream: <https://github.com/WrongPeople/JustRepeat> @ `0b09c50a08774c429164a407b5cd8d0092910e53`
  (branch `main`, HEAD at fetch time 2026-10-14). Source files `src/JustRepeat.{h,cpp}`.
- Licence: **MIT** (see `LICENSE`, © 2020 WrongPeople).
- sha256:
  - `JustRepeat.h`  `b932bdb589ae4bef39ee4328f8b139776537c1819b99de81088bfb282b2eb890`
  - `JustRepeat.cpp` `3e3abbb7435bd03332ff76763de3e46e4915391469c6ba73e1929421680b2eef`
  - `LICENSE`        `54f2704b3137e800508417ba8296dad8cac149dfc9406bb1654113e03c09e156`
- **Used as: REFERENCE ONLY, not compiled.** Both files are VST2-bound — `JustRepeat.h` includes
  `public.sdk/source/vst2.x/audioeffectx.h` and the class derives from `AudioEffectX`, so building them
  would need the Steinberg SDK. Our `vst/src/repeat.c` re-implements the algorithm and extends it
  (grid enum, gate, chance, repeats, mix). Attribution: © WrongPeople, MIT.
  - Upstream logic (for the port): 32 s stereo float buffer; on enable `barTime = 240 / bpm`,
    `recSamples = barTime * fs`; `DIVS = {1, 1.5, 2, 3, 4, 6, 8, 12, 16, 24, 32, 48, 64}`;
    `playSamples = barTime / DIVS[div] * fs`; state machine REC (record the first slice) → PLAY
    (keep recording to `recSamples`, loop `playSamples`); tempo from the host.
  - **Change we make at port time:** upstream allocates `BUF_LEN = 44100 * 32` floats/channel (~11 MB per
    instance); we size the buffer to the bar length (a few seconds) to stay light on the MPC.

## `mda/` — mda plugins (through-zero flanger)

- Upstream snapshot: `mda-vst-src-2015-06-18.zip`,
  sha256 `88c8c9924cac8fd6bca6e1e82e71c38acec46491a5e53d47b193637c11288885` (as vendored in `../DubStation`).
- Licence: **MIT or GPL-2.0-or-later** (dual). This port **elects the MIT branch** → the combined work stays
  **MIT** (superseded note: an earlier draft assumed GPL; see `../THIRD_PARTY_NOTICES.md`).
- **Used as:** the ThruZero DSP structure (2048-sample delay line, LFO-modulated tap) is adapted into our
  own `vst/src/flanger.c`, so no mda source file is compiled; the mda zip stays for provenance.

## DubStation modules — copied 2026-10-14 (task T2)

- Source: `../DubStation/vst/{src,include}` (our own MIT code). Copied **byte-identical**; the only files
  taken are `dub_delay`, `dub_filter`, `dub_reverb` (+ their headers and `dsp_common.h`).
  `dub_space.c` / `dub_motion.c` are deliberately not used.
- sha256 (as copied):
  - `include/dsp_common.h`  `6a521e0f1d329779b4082f467b8d6d2856fd6e4f4adb26f0aff47611c36174ce`
  - `src/dub_delay.c`        `1622bd8f8ca914e88a12e1cce4d62608f520e8983de8d5a1649c4c58297dffb5`
  - `src/dub_filter.c`       `36adbe1527b9fe62fae1eb9a3fef683c360c3c11719cee99b5e9e060695ab829`
  - `src/dub_reverb.c`       `1e206f3bab00b2926e38af5334f5b0cc42bbaf1ab15b3a00bda189765d11b750`
- **Changes we make:** none to `include/dsp_common.h` or `src/dub_reverb.c`.
  - `src/dub_filter.c` grew one **opt-in** mode in v2 (T24, 2026-10-16): `dub_filter_set_hp_only()` runs
  the HPF alone - no LPF biquad, no drive, no ceiling - for the VERB macro's wet-path high-pass. The
  default path (HPF -> LPF -> drive -> soft clip) is untouched, so the DJ filter behaves exactly as
  copied.
  - `src/dub_delay.c`: `dub_delay_set_feedback()` used to multiply its parameter by an internal 1.2
    ("so 1.0 can self-oscillate"), so every *measured* percent value was too hot - the VERB/FADE macros
    at 0.95 became 1.14 and the delay swelled to its limiter instead of decaying. `DUB_MAX_FEEDBACK` is
    now 1.0, i.e. the parameter is the real feedback gain; self-oscillation is still reachable at 1.0
    (the loop then sustains at the envelope limiter). Found by ear on the device, 2026-10-16.
  Consequently the sha256 of both files above no longer matches the working copy.
  `dub_filter` is driven as the DJ filter (bipolar LPF/HPF sweep) and *not* as a delay feedback filter
  (`dub_delay_set_filter(delay, NULL)`); the second instance (`vhf`) is the wet-path high-pass.

## Flanger — adapted from mda ThruZero (2026-10-14, task T7)

- Upstream: `mdaThruZero.{cpp,h}` inside `mda-vst-src-2015-06-18.zip` (see the DubStation archive),
  © mda / Paul Kellett. Licence **dual MIT / GPL-2.0-or-later**; **this port takes the MIT branch**.
- **Used as:** NOT vendored as a file (the mda source is VST2-bound). `vst/src/flanger.c` is our own
  rewrite of the DSP structure: the parabolic delay-tap modulation, linear interpolation, the bipolar
  feedback and the **dry-minus-wet** through-zero mix; our controls are rate/depth/feedback/mix (mda's
  fifth, depth-minimum, parameter is dropped so the delay reaches 0 at the modulation minimum).

## Own modules (no third-party code)

- `vst/src/iso.c` — 3-band shelving isolator (RBJ biquads).
- `vst/src/limiter.c` — look-ahead brickwall limiter.
- `vst/src/djfx_engine.c` — chain, macro, panic, VU, state.

## v2 (2026-10-15/16) — nothing new vendored

- `grain.c` (grain delay) and the true-peak detector inside `limiter.c` are **our own code**; no
  third-party file was added for v2.
- The candidates that were measured and rejected are listed in `../THIRD_PARTY_NOTICES.md` under
  "Evaluated and deliberately NOT used" (Clouds/MIT, boris/GPL-3.0, wumms/MIT C++).
- **GPL isolation policy:** if GPL code is ever taken, it goes to `vst/third_party/gpl/` and is compiled
  only with a `--private` flag, so the default build stays MIT. Currently that directory does not exist
  and `vst.json` lists no such source, so the default artifact is MIT end to end.
