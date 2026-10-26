#!/usr/bin/env python3
"""Generate the measurement signal for the A/B runs: one bar of deterministic content, repeated.

Why not the running loop: while measuring the original rack against our plugin, the loop's own noise floor
was +-6 dB (two neutral steps of the same ride differed by up to 5.9 dB per band), which hides every effect
that is not a big spectral change. A known signal removes that: every bar contains

  * a short click at the bar start - the alignment anchor. It must be the LOUDEST element in the
    file: an earlier version normalised on the 350 ms noise burst, so a peak detector anchored on the
    burst (1.85 s into the bar) and every measurement window landed one element late,
  * a log sweep 40 Hz ... 16 kHz - the frequency response of whatever is in the chain,
  * a noise burst - the tails (echoes, grains, stutter, reverb),
  * silence around both, so the tails are visible where they should be.

  uv run --with numpy python3 make_testpattern.py [--bars 20] [--bpm 92] --out testpattern.wav
"""
import argparse
import math

import numpy as np

SR = 44100


def click(n, amp=0.9):
    """2 ms windowed burst: broadband, short enough to localise the bar exactly."""
    x = np.zeros(n)
    k = int(0.002 * SR)
    t = np.arange(k) / SR
    env = np.exp(-t / 0.0004)
    x[:k] = amp * env * np.sin(2 * np.pi * 2000.0 * t)
    return x


def sweep(n, f0=40.0, f1=16000.0, amp=0.20):
    """Log sweep over the given length, faded in and out."""
    dur = n / SR
    k = np.arange(n) / SR
    K = dur / math.log(f1 / f0)
    ph = 2 * math.pi * f0 * K * (np.exp(k / K) - 1.0)
    x = amp * np.sin(ph)
    fade = int(0.01 * SR)
    x[:fade] *= np.linspace(0, 1, fade)
    x[-fade:] *= np.linspace(1, 0, fade)
    return x


def noise(n, amp=0.12, seed=1):   # gaussian peaks reach ~4 sigma: keep the burst below the click
    rng = np.random.default_rng(seed)
    x = amp * rng.standard_normal(n)
    k = int(0.005 * SR)
    x[:k] *= np.linspace(0, 1, k)
    x[-k:] *= np.linspace(1, 0, k)
    return x


def bar(bpm):
    n = int(round(4.0 * 60.0 / bpm * SR))          # 4/4
    x = np.zeros(n)
    x[:] += click(n)
    a = int(0.35 * SR)
    b = a + int(1.25 * SR)
    x[a:b] += sweep(b - a)
    c = int(1.85 * SR)
    d = c + int(0.35 * SR)
    x[c:d] += noise(d - c)
    return x


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--bars", type=int, default=20)
    ap.add_argument("--bpm", type=float, default=92.0)
    ap.add_argument("--out", default="testpattern.wav")
    a = ap.parse_args()

    one = bar(a.bpm)
    x = np.tile(one, a.bars)
    peak = float(np.max(np.abs(x)))
    if peak > 0.98:
        x *= 0.98 / peak
    stereo = np.repeat(x, 2)
    v = (np.clip(stereo, -1, 1) * (2 ** 23 - 1)).astype(np.int32)
    b24 = v.astype("<i4").view(np.uint8).reshape(-1, 4)[:, :3].tobytes()
    import wave
    w = wave.open(a.out, "wb")
    w.setnchannels(2)
    w.setsampwidth(3)
    w.setframerate(SR)
    w.writeframes(b24)
    w.close()
    print("wrote %s: %d bars, bar = %.4f s (%.2f BPM), total %.1f s, peak %.3f"
          % (a.out, a.bars, len(one) / SR, a.bpm, len(x) / SR, float(np.max(np.abs(x)))))


if __name__ == "__main__":
    main()
