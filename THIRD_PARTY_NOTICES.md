# Third-party notices — MPC DJ FX (`DjFx`)

This port reuses code from other projects. Files are vendored **byte-identical** under
`vst/third_party/` (hashes + exact commits in `vst/src/VENDORED.md`).

## Vendored / reused

- **WrongPeople/JustRepeat** — beat repeat / stutter DSP.
  <https://github.com/WrongPeople/JustRepeat> @ `0b09c50a08774c429164a407b5cd8d0092910e53`.
  License: **MIT**. Files: `JustRepeat.h`, `JustRepeat.cpp` (VST2 host glue dropped).
  © WrongPeople.

- **mda plugins** — through-zero flanger (`mdaThruZero`).
  Snapshot `mda-vst-src-2015-06-18.zip` (sha256 `88c8c9924cac8fd6bca6e1e82e71c38acec46491a5e53d47b193637c11288885`),
  as already vendored in the sibling port `../DubStation`. © mda / Paul Kellett.
  License: **MIT or GPL-2.0-or-later** (dual).
  *This port elects the **MIT** branch: `vst/src/flanger.c` is a rewrite of mda ThruZero's DSP
  (structure, delay-modulation shape and the dry-minus-wet through-zero mix) with our own controls,
  so the combined work stays MIT.*

- **DubStation DSP modules** (delay, resonant filter, plate/hall reverb, motion) — our own code from the
  sibling port `../DubStation`; the project is MIT. © the port authors.

- **sd88me/mpc-vst-plugins** — the porting framework (build wrapper, VST2 glue, skin tooling).
  <https://github.com/sd88me/mpc-vst-plugins> — MIT. Build dependency, not vendored here.

## Named but NOT used

- **dblue / Illformed “Glitch” v1.3** and **Glitch²** — freeware but **closed source**; not vendored.
- Ableton **`.adg`** racks — presets for Ableton’s closed devices; no DSP is portable from them.

## Licence of this port

**MIT.** Everything used is MIT: our own DubStation modules, `WrongPeople/JustRepeat`, and the **MIT
branch** of mda's dual licence. (An earlier draft said GPL-3.0-only because the plan assumed the mda
GPL-2 branch; the mda source is dual, and we take MIT.)

## v2 additions (2026-10-15/16)

### Written by us (no third-party code added)

- **`vst/src/grain.c`** - the grain delay that reproduces the original rack's *Glitch* stage (rate
  1 -> 150 Hz, read offset 22 -> 2 ms, dry/wet 0 -> 35 %). Own implementation, MIT.
- **`vst/src/limiter.c`** - extended with a 4x polyphase **true-peak** detector (12 taps/phase, own
  code, MIT). See `PORTING_NOTES.md` for its measured accuracy.

### Evaluated and deliberately NOT used

- **Clouds granular** (Mutable Instruments, via `saustin2010/mpc-vst-verglas`) - **MIT**, but
  `src/clouds` (7,383 lines) + `src/stmlib` (2,216 lines) is disproportionate for three parameters, and
  its parameter model does not map onto the measured spans.
- **`sd88me/mpc-vst-boris`** `src/dsp/granular.c` (1,294 lines) - **GPL-3.0-only**, from
  `filliformes/boris-move` @ `f3b03b2cbe45eac90b2de2db82444a1c0be20afc` (a C rewrite of Alessandro
  Gaiba's Boris Granular Station). Feature-rich and already MPC-ported, but far beyond what the Glitch
  stage needs. Kept as the documented upgrade path for a private build.
- **`lunarscraper/mpc-vst-wumms`** (`vst/wumms_core.h`, `vst/wumms_master.h`, 688 lines) - **MIT**, but
  **C++** (`std::pow`, 15-sample latency); vendoring it would add a g++ compile unit for a problem ~40
  lines of our own C solves.

### GPL policy

v2 is used privately, so GPL code is allowed - but it must stay isolated under `vst/third_party/gpl/`
behind a `--private` build switch, leaving the default build MIT and publishable. **No such directory
exists yet**: none of the GPL candidates above was taken, so the default build contains no GPL path.

## Design references (no code, no assets copied)

- **MagicStino / Magic Tape Echo** — the look of the chassis (ivory plate, ink masthead) was modelled on
  this plugin's skin; only the visual idea was taken, no file from it is included.
  <https://github.com/MagicStino/Magic-Tape-Echo>
- **RAMS60 design system** — colour tokens (`#EFE8D8` paper, `#191511` shellac, `#1F7A3D` signal green,
  `#D9531E` orange) and the type/spacing rules come from the APC design library
  (`Noizefield/audio-plugin-coder`, **MIT**). <https://github.com/Noizefield/audio-plugin-coder>
- Ableton Live's built-in devices were used as a style reference for the control treatment only.
