#!/usr/bin/env python3
"""Measure what a macro ride does to the signal, for the original and for our plugin.

The recordings come from Ableton: one track records the master while one macro is ridden from its
neutral value to the far end and back, one bar per step. The loop keeps running throughout, so a segment
of a recording is *not* aligned with the dry reference. Each segment is therefore aligned by
cross-correlation against the dry reference first, and only then compared, band by band: the result is a
transfer function per macro value - "at this knob position the original does this to the signal".

    uv run --with numpy python3 ab_analyze.py <ride.json> --dry dry_ref.wav [--ours ours.wav]
"""
import argparse
import json
import math
import os
import struct
import sys

import numpy as np

BANDS = [(20, 200), (200, 800), (800, 3000), (3000, 8000), (8000, 16000)]
SR = 44100
SETTLE = 0.30          # seconds skipped after each step change (the transition belongs to neither value)
WIN = 8192


def read_wav(path):
    data = open(path, "rb").read()
    if data[:4] != b"RIFF" or data[8:12] != b"WAVE":
        raise SystemExit(f"{path}: not a WAV")
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
    if body is None:
        raise SystemExit(f"{path}: no data chunk")
    if bits == 24 and not isf:
        raw = np.frombuffer(body, dtype=np.uint8)[: (len(body) // 3) * 3].reshape(-1, 3)
        v = (raw[:, 0].astype(np.int32) | (raw[:, 1].astype(np.int32) << 8) | (raw[:, 2].astype(np.int32) << 16))
        x = ((v << 8) >> 8).astype(np.float64) / 8388608.0
    elif bits == 16:
        x = np.frombuffer(body, dtype="<i2").astype(np.float64) / 32768.0
    elif bits == 32 and isf:
        x = np.frombuffer(body, dtype="<f4").astype(np.float64)
    else:
        raise SystemExit(f"{path}: unsupported bits={bits} float={isf}")
    x = x.reshape(-1, ch)
    return x.mean(axis=1) if ch > 1 else x


def best_lag(ref, seg):
    """Sample offset where `seg` best matches `ref` (FFT cross-correlation, normalised peak)."""
    n = 1 << int(math.ceil(math.log2(len(ref) + len(seg))))
    a = np.fft.rfft(ref, n)
    b = np.fft.rfft(seg, n)
    corr = np.fft.irfft(a * np.conj(b), n)
    k = int(np.argmax(np.abs(corr)))
    return k if k < len(ref) else k - n


def spec_db(x):
    """Per-band level after FFT band-limiting (no window leakage) plus the centroid and RMS."""
    n = len(x)
    X = np.fft.rfft(x)
    f = np.fft.rfftfreq(n, 1.0 / SR)
    mag2 = np.abs(X) ** 2
    bands = []
    for lo, hi in BANDS:
        m = (f >= lo) & (f < hi)
        y = np.fft.irfft(X * m, n)
        bands.append(10.0 * math.log10(float(np.mean(y ** 2)) + 1e-24))
    centroid = float(np.sum(f * mag2) / (np.sum(mag2) + 1e-20))
    rms = 10.0 * math.log10(float(np.mean(x ** 2)) + 1e-24)
    return bands, centroid, rms



def time_metrics(x):
    """Onset density, tail ratio and loop periodicity - the metrics that see grains, echoes and stutter,
    where the spectrum stays flat."""
    if len(x) < SR:
        return None
    env = np.abs(x)
    k = int(0.01 * SR)                                   # 10 ms envelope
    env = np.convolve(env, np.ones(k) / k, mode="same")
    thr = 1.6 * float(np.mean(env))
    onsets = []
    i, refractory = int(0.02 * SR), int(0.06 * SR)
    while i < len(env):
        if env[i] > thr:
            j = i + int(np.argmax(env[i:i + int(0.15 * SR)]))
            onsets.append(j)
            i = j + refractory
        else:
            i += 1
    dense = len(onsets) / (len(x) / SR)
    tails = []
    for o in onsets:
        a0, a1 = o, o + int(0.12 * SR)
        b0, b1 = o + int(0.18 * SR), o + int(0.50 * SR)
        if b1 >= len(x):
            continue
        e0 = float(np.mean(x[a0:a1] ** 2))
        e1 = float(np.mean(x[b0:b1] ** 2))
        if e0 > 1e-9:
            tails.append(10.0 * math.log10(e1 / e0 + 1e-12))
    tail = float(np.mean(tails)) if tails else float("nan")
    # loop periodicity: strongest autocorrelation between 0.3 s and 6 s
    n = len(x)
    ac = np.fft.irfft(np.abs(np.fft.rfft(x, 2 * n)) ** 2, 2 * n)[:n]
    ac /= (ac[0] + 1e-20)
    lo, hi = int(0.3 * SR), min(int(6.0 * SR), n - 1)
    peak = float(np.max(ac[lo:hi])) if hi > lo else float("nan")
    return {"onsets_per_s": dense, "tail_db": tail, "loop_corr": peak}


def transfer(ref, ride_wav, log):
    """Per step: the band transfer relative to the dry reference, aligned by correlation."""
    x = read_wav(ride_wav)
    t0 = log[0]["wall"]
    out = []
    for i, e in enumerate(log):
        a = int((e["wall"] - t0 + SETTLE) * SR)
        b = int(((log[i + 1]["wall"] - t0) if i + 1 < len(log) else len(x) / SR) * SR)
        if b - a < WIN * 3:
            out.append({"value": e["value"], "skip": True})
            continue
        seg = x[a:b]
        lag = best_lag(ref, seg)
        lag = max(0, min(lag, len(ref) - len(seg)))
        rs = spec_db(ref[lag:lag + len(seg)])
        ss = spec_db(seg)
        if rs is None or ss is None:
            out.append({"value": e["value"], "skip": True})
            continue
        tm = time_metrics(seg)
        # residual: how much the macro changes the signal, and where (aligned, so the loop cancels)
        rs_ref = ref[lag:lag + len(seg)]
        m = min(len(rs_ref), len(seg))
        res = seg[:m] - rs_ref[:m]
        e_ref = float(np.mean(rs_ref[:m] ** 2)) + 1e-20
        tm["res_db"] = 10.0 * math.log10(float(np.mean(res ** 2)) / e_ref + 1e-20)
        rb = spec_db(res)[0]
        rb_ref = spec_db(rs_ref[:m])[0]
        tm["res_bands"] = [a - b for a, b in zip(rb, rb_ref)]
        out.append({"value": e["value"], "lag_ms": 1000.0 * lag / SR,
                    "band_db": [s - r for s, r in zip(ss[0], rs[0])],
                    "centroid": ss[1], "rms_db": ss[2], "time": tm})
    return out


def show(name, rows, indent=""):
    print(f"{indent}{name}")
    print(indent + "value | " + " | ".join("%5d-%-5d" % b for b in BANDS) + " | centroid |   lag")
    for r in rows:
        if r.get("skip"):
            print(indent + "%5d | step too short" % r["value"])
            continue
        tm = r.get("time") or {}
        print(indent + "%5d | %s | %9.0f | %6.1f/s | %+6.1f dB tail | %.2f corr" % (
            r["value"], " | ".join("%+6.1f" % v for v in r["band_db"]), r["centroid"],
            tm.get("onsets_per_s", float("nan")), tm.get("tail_db", float("nan")),
            tm.get("loop_corr", float("nan"))))
        print(indent + "      residual %+6.1f dB, bands %s" % (
            tm.get("res_db", float("nan")), " ".join("%+5.1f" % v for v in tm.get("res_bands", []))))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("ride_json")
    ap.add_argument("--dry", required=True, help="the dry reference recording")
    ap.add_argument("--orig", required=True, help="the original ride recording")
    ap.add_argument("--ours", default=None, help="our plugin's render over the same dry file")
    a = ap.parse_args()

    ride = json.load(open(a.ride_json))
    log = ride["log"]
    ref = read_wav(a.dry)
    macro = ride["macro"]
    print(f"== {macro}   (original: {ride['file']})")

    orig = transfer(ref, a.orig, log)
    show("original (band transfer vs dry, dB):", orig)
    if a.ours:
        ours = transfer(ref, a.ours, log)
        print("  -- ours (same ride):")
        show("", ours, indent="  ")
        print("  -- difference (ours - original), positive = ours louder in that band:")
        print("  value | " + " | ".join("%5d-%-5d" % b for b in BANDS))
        for o, u in zip(orig, ours):
            if o.get("skip") or u.get("skip"):
                continue
            print("  %5d | %s" % (o["value"], " | ".join("%+6.1f" % (uv - ov) for uv, ov in zip(u["band_db"], o["band_db"]))))


if __name__ == "__main__":
    main()
