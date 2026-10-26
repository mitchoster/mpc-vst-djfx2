#!/usr/bin/env python3
"""Evaluate the deterministic-pattern rides: exact, noise-free, and it can see the time-structure macros.

The test pattern (`make_testpattern.py`) puts a click, a log sweep and a noise burst into every bar, and
the ride log records the song's beat position at every step - so a step maps to its bars exactly, no
cross-correlation and no phase noise (the loop-based measurement had a +-6 dB floor).

Per step it reports
  * the band transfer of the sweep against the same bars of the dry pattern (what the macro does to the
    spectrum),
  * the click tail: energy in the 50-300 ms window after each click over the click's own energy, which is
    where delay, echo, grain and stutter show up (in dB),
  * the energy of the noise burst against the dry burst (grains/stutter change it).

  uv run --with numpy python3 ab_pattern.py <ride2_*.json> --dry testpattern.wav --orig <macro>.wav [--ours ours.wav]
"""
import argparse
import json
import math
import struct
import sys

import numpy as np

SR = 44100
BANDS = [(40, 200), (200, 800), (800, 3000), (3000, 8000), (8000, 16000)]


def read_wav(path):
    data = open(path, "rb").read()
    pos, bits, ch, isf, body = 12, None, None, False, None
    while pos + 8 <= len(data):
        cid, sz = data[pos:pos + 4], struct.unpack("<I", data[pos + 4:pos + 8])[0]
        chunk = data[pos + 8:pos + 8 + sz]
        if cid == b"fmt ":
            tag, ch, _sr, _br, _ba, bits = struct.unpack("<HHIIHH", chunk[:16])
            isf = (tag == 3)
        elif cid == b"data":
            body = chunk
        pos += 8 + sz + (sz & 1)
    if bits == 24 and not isf:
        raw = np.frombuffer(body, dtype=np.uint8)[: (len(body) // 3) * 3].reshape(-1, 3)
        v = (raw[:, 0].astype(np.int32) | (raw[:, 1].astype(np.int32) << 8) | (raw[:, 2].astype(np.int32) << 16))
        x = ((v << 8) >> 8).astype(np.float64) / 8388608.0
    elif bits == 16:
        x = np.frombuffer(body, dtype="<i2").astype(np.float64) / 32768.0
    elif bits == 32 and isf:
        x = np.frombuffer(body, dtype="<f4").astype(np.float64)
    else:
        raise SystemExit(f"{path}: unsupported bits={bits}")
    x = x.reshape(-1, ch)
    return x.mean(axis=1) if ch > 1 else x


def bands_db(x):
    n = len(x)
    X = np.fft.rfft(x)
    f = np.fft.rfftfreq(n, 1.0 / SR)
    out = []
    for lo, hi in BANDS:
        y = np.fft.irfft(X * ((f >= lo) & (f < hi)), n)
        out.append(10.0 * math.log10(float(np.mean(y ** 2)) + 1e-24))
    return out


def first_click(x, thr_frac=0.35, short_ms=15.0, quiet_frac=0.2):
    """The pattern's bar-0 click, found by its SHORTNESS: the generated file normalises on its loudest
    part, which is the 350 ms noise burst (not the 2 ms click), so a plain peak detector anchors on the
    burst - 1.85 s into the bar - and every sweep window then lands one element late. A click condition:
    above the threshold and back below 20 % of the peak within `short_ms`."""
    mag = np.abs(x)
    if len(mag) == 0:
        return 0
    peak = float(np.max(mag))
    thr, quiet = thr_frac * peak, quiet_frac * peak
    look = int(short_ms * 0.001 * SR)
    idx = np.nonzero(mag > thr)[0]
    for i in idx:
        j = i + int(np.argmax(mag[i:i + look])) if i + look < len(mag) else i
        k = j + int(0.002 * SR)
        if k + look >= len(mag):
            return int(i)
        if float(np.max(mag[k:k + look])) < quiet:
            return j
    return int(idx[0]) if len(idx) else 0


def clicks(x, thr_frac=0.45):
    """Positions of the bar clicks: envelope peaks above a fraction of the file peak."""
    env = np.abs(x)
    k = int(0.002 * SR)
    env = np.convolve(env, np.ones(k) / k, mode="same")
    thr = thr_frac * float(np.max(env))
    out, i, refr = [], int(0.05 * SR), int(0.5 * SR)
    while i < len(env) - k:
        if env[i] > thr:
            j = i + int(np.argmax(env[i:i + int(0.02 * SR)]))
            out.append(j)
            i = j + refr
        else:
            i += 1
    return out


def step_metrics(rec, dry, a, b, bar_n, loop_bars=16, bar_samples=None):
    """Metrics for the recording window [a, b) against the matching dry bars (wrapping at loop_bars:
    the clip in Live loops after 16 bars, the generated file holds 20)."""
    n = min(b - a, len(dry))
    r = rec[a:a + n]
    off = ((bar_n % loop_bars) * bar_samples) if bar_samples else 0
    d = dry[off:off + n]
    n = min(n, len(d))
    r = r[:n]
    out = {"band_db": [x - y for x, y in zip(bands_db(r), bands_db(d))], "bar_n": bar_n}
    # clicks: absolute positions in both, then tail energy per click
    rc, dc = clicks(r), clicks(d)
    tails, burst = [], []
    for c in rc:
        c0, c1 = c, min(len(r), c + int(0.05 * SR))
        t0, t1 = min(len(r), c + int(0.05 * SR)), min(len(r), c + int(0.30 * SR))
        if t1 - t0 < 1000 or c1 - c0 < 100:
            continue
        e0 = float(np.mean(r[c0:c1] ** 2))
        e1 = float(np.mean(r[t0:t1] ** 2))
        if e0 > 1e-12:
            tails.append(10.0 * math.log10(e1 / e0 + 1e-15))
    # noise burst sits at 1.85 s into the bar in the pattern
    for bstart in range(0, n - int(2.6 * SR), int(2.6087 * SR)):
        s0 = bstart + int(1.85 * SR)
        s1 = s0 + int(0.30 * SR)
        if s1 >= n:
            continue
        eb = float(np.mean(r[s0:s1] ** 2))
        db = float(np.mean(d[s0:s1] ** 2))
        if db > 1e-12:
            burst.append(10.0 * math.log10(eb / db + 1e-15))
    out["click_tail_db"] = float(np.mean(tails)) if tails else float("nan")
    out["burst_db"] = float(np.mean(burst)) if burst else float("nan")
    out["clicks"] = len(rc)
    return out



FR_BANDS = [(40, 80), (80, 160), (160, 315), (315, 630), (630, 1250), (1250, 2500),
            (2500, 5000), (5000, 10000), (10000, 16000)]


def sweep_window(x, bar_start, bar_samples):
    """The sweep sits 0.35 s .. 1.60 s into every bar (see make_testpattern.py); return the samples."""
    a = bar_start + int(0.35 * SR)
    b = bar_start + int(1.60 * SR)
    if b > len(x):
        return None
    w = x[a:b]
    fade = int(0.005 * SR)
    w = w.copy()
    w[:fade] *= np.linspace(0, 1, fade)
    w[-fade:] *= np.linspace(1, 0, fade)
    return w


def bar_anchor(rec, approx, win=int(0.12 * SR)):
    """Sample-exact position of a bar's click: the loudest sample within +-120 ms of where the bar is
    expected. Correlating the sweep windows instead does not work - a log sweep is not shift-invariant, so
    its self-correlation has several near-equal peaks (measured: lags scattered over +-130 ms)."""
    a = max(0, approx - win)
    b = min(len(rec), approx + win)
    if b - a < 32:
        return approx
    return a + int(np.argmax(np.abs(rec[a:b])))


def transfer(dry, rec, first_bar, base, bar_samples, nbars, nfft=1 << 18):
    """Farina-style transfer: FR = median over bars of FFT(recorded sweep) / FFT(dry sweep), each bar
    aligned against its own dry bar."""
    ratios, lags = [], []
    for k in range(nbars):
        db = (first_bar + k) % 16
        rs = base + (first_bar + k) * bar_samples
        # one anchor for the whole take, then the nominal bar grid: a per-bar peak search picks the wrong
        # element whenever the rack masks the click, and the drift over 16 bars is 1 sample per bar.
        lag = 0
        anchor = rs
        lags.append(lag)
        x = sweep_window(dry, db * bar_samples, bar_samples)
        y = sweep_window(rec, anchor, bar_samples)
        if x is None or y is None or len(x) != len(y):
            continue
        X = np.fft.rfft(x, nfft)
        Y = np.fft.rfft(y, nfft)
        m = np.abs(X) > 1e-3 * float(np.max(np.abs(X)))
        r = np.abs(Y) / np.maximum(np.abs(X), 1e-12)
        ratios.append((m, r, float(np.median(lag))))
    if not ratios:
        return None
    nf = len(ratios[0][1])
    acc = np.zeros(nf)
    cnt = np.zeros(nf)
    for m, r, _ in ratios:
        acc[m] += r[m]
        cnt[m] += 1.0
    out = acc / np.maximum(cnt, 1.0)
    return out, float(np.median([l for _, _, l in ratios]))


def fr_bands(fr, nfft=1 << 18):
    f = np.fft.rfftfreq(nfft, 1.0 / SR)
    out = []
    for lo, hi in FR_BANDS:
        m = (f >= lo) & (f < hi)
        v = fr[m]
        out.append(20.0 * math.log10(float(np.mean(v)) + 1e-12) if np.any(m) else float("nan"))
    return out



def all_clicks(x, thr_frac=0.35, short_ms=15.0, quiet_frac=0.25, maxn=64):
    """Every bar click in a take, by the same shortness test as first_click. The recording's bar k is the
    k-th click, which is exact - mapping the ride's beat timestamps to samples is not, because
    start_pattern() returns its beat ~50 ms after the clip started."""
    mag = np.abs(x)
    if len(mag) == 0:
        return []
    peak = float(np.max(mag[: 2 * SR])) if len(mag) > SR else float(np.max(mag))
    thr, quiet = thr_frac * peak, quiet_frac * peak
    look = int(short_ms * 0.001 * SR)
    out, i = [], 0
    while i < len(mag) - 2 * look and len(out) < maxn:
        if mag[i] > thr:
            j = i + (int(np.argmax(mag[i:i + look])) if i + look < len(mag) else 0)
            k = j + int(0.002 * SR)
            if k + look < len(mag) and float(np.max(mag[k:k + look])) < quiet:
                out.append(j)
                i = j + look
                continue
            i = j + look
        else:
            i += 1
    return out


def click_grid(x, bar_samples, maxn=40, tol=int(0.015 * SR)):
    """Click positions snapped to the bar grid: one click per bar, within +-15 ms of the expected
    position. Raw detection is polluted by echoes of the click (each short transient passes the shortness
    test), which shifts every later bar index."""
    first = first_click(x)
    if first <= 0 and len(x) < bar_samples:
        return []
    out = []
    for k in range(maxn):
        want = first + k * bar_samples
        a, b = max(0, want - tol), min(len(x), want + tol)
        if b - a < 32:
            break
        j = a + int(np.argmax(np.abs(x[a:b])))
        out.append(j)
    return out


def transfer_bars(dry, rec, clicks, bars, bar_samples, nfft=1 << 18):
    """FR from FFT(recorded bar) / FFT(dry bar) for the given bar indices, using the click positions."""
    acc = cnt = None
    used = 0
    for k in bars:
        if k < 0 or k + 1 >= len(clicks):
            continue
        rs = clicks[k]
        x = sweep_window(dry, (k % 16) * bar_samples, bar_samples)
        y = sweep_window(rec, rs, bar_samples)
        if x is None or y is None or len(x) != len(y):
            continue
        X = np.fft.rfft(x, nfft)
        Y = np.fft.rfft(y, nfft)
        m = np.abs(X) > 1e-3 * float(np.max(np.abs(X)))
        r = np.abs(Y) / np.maximum(np.abs(X), 1e-12)
        if acc is None:
            acc = np.zeros(len(r)); cnt = np.zeros(len(r))
        acc[m] += r[m]; cnt[m] += 1.0
        used += 1
    if acc is None or used == 0:
        return None
    return acc / np.maximum(cnt, 1.0)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("ride_json")
    ap.add_argument("--dry", required=True)
    ap.add_argument("--orig", required=True)
    ap.add_argument("--ours", default=None)
    a = ap.parse_args()

    ride = json.load(open(a.ride_json))
    dry = read_wav(a.dry)
    bar_s = ride["bar_seconds"]
    log = ride["log"]
    bpb = 4.0                                   # beats per bar (4/4)
    loop_bars = 16                              # the test clip loops after 16 bars
    bar_samples = int(round(bar_s * SR))
    orig = read_wav(a.orig)
    ours = read_wav(a.ours) if a.ours else None
    base_o = first_click(orig)       # the recorder's pre-roll ends where the pattern's first click is
    base_u = first_click(ours) if ours is not None else 0
    ck_o = click_grid(orig, bar_samples)
    ck_u = click_grid(ours, bar_samples) if ours is not None else []
    print(f"  clicks in take: original {len(ck_o)}, ours {len(ck_u)}")
    base = base_o
    fb = ride.get("t_fire_beat") or {}
    fire_beat = fb.get("beat", 0.0) if isinstance(fb, dict) else float(fb)
    print(f"  anchor at {1000.0*base/SR:.1f} ms, pattern fired at beat {fire_beat:.2f}")

    print(f"== {ride['macro']}   ({ride['file']})")
    hdr = "value | " + " | ".join("%5d-%-5d" % b for b in BANDS) + " | click tail | burst | clicks"
    print("original:")
    print("  " + hdr)
    rows_o, rows_u = [], []
    for i, e in enumerate(log):
        b0 = e["beat"]
        b1 = log[i + 1]["beat"] if i + 1 < len(log) else b0 + bpb
        pb0, pb1 = b0 - fire_beat, b1 - fire_beat          # position inside the pattern, in beats
        if pb1 - pb0 < bpb * 0.9 or pb0 < 0:
            continue
        a0 = base + int(pb0 / bpb * bar_s * SR)
        a1 = base + int(pb1 / bpb * bar_s * SR)
        if a1 - a0 < SR:
            continue
        mo = step_metrics(orig, dry, base_o + (a0 - base), min(a1, len(orig)) if base_o == base else base_o + int(pb1 / bpb * bar_s * SR),
                          int(pb0 // bpb), loop_bars, bar_samples)
        rows_o.append((e["value"], mo))
        if ours is not None:
            mu = step_metrics(ours, dry, a0, min(a1, len(ours)), int(pb0 // bpb))
            rows_u.append((e["value"], mu))
        print("  %5d | %s | %+8.1f dB | %+5.1f dB | %3d" % (
            e["value"], " | ".join("%+6.1f" % v for v in mo["band_db"]),
            mo["click_tail_db"], mo["burst_db"], mo["clicks"]))
    print("frequency response (sweep deconvolution, 1/3-octave bands):")
    print("  value | " + " | ".join("%5d-%-5d" % b for b in FR_BANDS))
    for i, e in enumerate(log):
        b0 = e["beat"]
        pb = b0 - (ride.get("t_fire_beat") or {}).get("beat", 0.0) if isinstance(ride.get("t_fire_beat"), dict) else b0
        if pb < 0:
            continue
        first_bar = int(pb // bpb)
        nbars = max(1, int(((log[i + 1]["beat"] - b0) if i + 1 < len(log) else bpb) // bpb))
        bars = list(range(first_bar, first_bar + nbars))
        fo = transfer_bars(dry, orig, ck_o, bars, bar_samples) if ck_o else None
        if fo is None:
            continue
        lag_o = 0.0
        line_o = fr_bands(fo)
        if ours is not None:
            fu = transfer_bars(dry, ours, ck_u, bars, bar_samples) if ck_u else None
            line_u = fr_bands(fu) if fu is not None else [float("nan")] * len(FR_BANDS)
            lag_u = 0.0
            print("  %5d | %s |  ours: %s   (lag %+.0f/%+.0f)" % (
                e["value"], " | ".join("%+6.1f" % v for v in line_o),
                " ".join("%+6.1f" % v for v in line_u), lag_o, lag_u))
        else:
            print("  %5d | %s" % (e["value"], " | ".join("%+6.1f" % v for v in line_o)))

    if ours is not None:
        print("ours:")
        print("  " + hdr)
        for v, m in rows_u:
            print("  %5d | %s | %+8.1f dB | %+5.1f dB | %3d" % (
                v, " | ".join("%+6.1f" % x for x in m["band_db"]),
                m["click_tail_db"], m["burst_db"], m["clicks"]))
        print("difference (ours - original) per band:")
        for (v, mo), (_, mu) in zip(rows_o, rows_u):
            print("  %5d | %s | %+6.1f | %+6.1f" % (
                v, " | ".join("%+6.1f" % (x - y) for x, y in zip(mu["band_db"], mo["band_db"])),
                mu["click_tail_db"] - mo["click_tail_db"], mu["burst_db"] - mo["burst_db"]))


if __name__ == "__main__":
    main()
