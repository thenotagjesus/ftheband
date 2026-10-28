#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
# Copyright (C) 2026 FtheBand contributors
"""Objective checks on a rendered jam (no listening needed).

  check_mix.py <mix.wav> <stems_dir> <log.txt> [--key "F minor"]
  check_mix.py --clicks <a.wav> [b.wav ...]     click check only (any length; streamed in 10 s chunks)

  1. clicks     : sample-level discontinuities in the mix and every stem (2nd difference >> local level)
  2. timing grid: drum-stem onsets vs the band's 16th grid (bpm + first STATE Playing from the log)
  3. bass in key: bass chair notes from the log vs the band chord sounding at the time (hard check), and vs the\n                 whole-take key (--key, a hard check; otherwise the log's last 'key' line, report only)
Exit code 1 if a check fails its threshold."""
import re, sys, glob, os
import numpy as np
from scipy.io import wavfile
from scipy.signal import find_peaks

NOTES = ['C','C#','D','D#','E','F','F#','G','G#','A','A#','B']
MAJOR = [0,2,4,5,7,9,11]; MINOR = [0,2,3,5,7,8,10]; MINOR_EXTRA = [11]   # harmonic-minor leading tone allowed

def load(path):
    sr, x = wavfile.read(path)
    x = x.astype(np.float64)
    if x.dtype.kind == 'i' or np.abs(x).max() > 2: x /= {2: 32768.0, 4: 2147483648.0}.get(wavfile.read(path)[1].dtype.itemsize, 1.0)
    return sr, (x.mean(axis=1) if x.ndim == 2 else x)

def clicks(x, sr):
    d2 = np.abs(np.diff(x, 2))
    win = int(0.01 * sr)
    env = np.convolve(np.abs(x[1:-1]), np.ones(win) / win, mode='same') + 1e-4
    # a click: curvature spike far above what the local level could produce smoothly
    bad = np.where((d2 > 0.25) & (d2 > 8 * env))[0]
    if len(bad) == 0: return 0
    return int(1 + np.sum(np.diff(bad) > int(0.005 * sr)))

def onsets(x, sr, min_gap):
    hop = 128
    e = np.array([np.sum(x[i:i + hop] ** 2) for i in range(0, len(x) - hop, hop)])
    le = np.log10(e + 1e-9); flux = np.maximum(0, np.diff(le, prepend=le[0]))
    # a hit: a big jump in log energy landing within 20 dB of the loudest hit. Sampled drums (crash swells, layered
    # kit pieces) have later transients inside one hit, so keep the EARLIEST peak of each cluster, not the highest.
    loud = le > le.max() - 2.0
    cand, _ = find_peaks(flux, height=1.0)
    gap = max(1, int(min_gap * sr / hop)); pk = []
    for p in cand:
        if not loud[min(p + 2, len(le) - 1)]: continue
        if pk and p - pk[-1] < gap: continue
        pk.append(p)
    out = []
    for p in pk:   # refine to the sample where the hit starts
        s0 = max(0, (p - 1) * hop); seg = np.abs(x[s0:s0 + 2 * hop]); thr = 0.2 * seg.max()
        out.append((s0 + int(np.argmax(seg > thr))) / sr)
    return np.array(out)

def clicks_file(path):
    """Streamed click count for long files (30 min drift tests). Same detector as clicks(), with 1 s overlap."""
    try:
        sr, x = wavfile.read(path, mmap=True)
    except ValueError:            # 24-bit containers can't be mmapped
        sr, x = wavfile.read(path)
    scale = {2: 32768.0, 4: 2147483648.0}.get(x.dtype.itemsize, 1.0) if x.dtype.kind == 'i' else 1.0
    n = len(x); step = 10 * sr; ov = sr; total = 0; last = -10**12; worst = 0.0
    for s0 in range(0, n, step):
        a0 = max(0, s0 - ov); seg = np.asarray(x[a0:min(n, s0 + step + 2)], dtype=np.float64) / scale
        if seg.ndim == 2: seg = seg.mean(axis=1)
        if len(seg) < 3: break
        d2 = np.abs(np.diff(seg, 2)); win = int(0.01 * sr)
        env = np.convolve(np.abs(seg[1:-1]), np.ones(win) / win, mode='same') + 1e-4
        r = d2 / (8 * env); worst = max(worst, float(r[(s0 - a0):].max()) if len(r) > s0 - a0 else 0.0)
        bad = np.where((d2 > 0.25) & (d2 > 8 * env))[0] + a0
        for b in bad[bad >= s0]:
            if b - last > int(0.005 * sr): total += 1
            last = b
    return total, n / sr, worst

def main():
    a = sys.argv[1:]
    if a and a[0] == '--clicks':
        bad = 0
        for f in a[1:]:
            c, dur, worst = clicks_file(f); bad += c
            print(f"clicks: {os.path.basename(f)} {c}  ({dur/60:.1f} min, worst d2/(8*env) = {worst:.3f}; click needs > 1 and d2 > 0.25)")
        return 1 if bad else 0
    if len(a) < 3: print(__doc__); return 2
    mix, stems, logp = a[0], a[1], a[2]
    key = a[a.index('--key') + 1] if '--key' in a else None
    log = open(logp).read()
    bpm = float(re.search(r'bpm ([\d.]+)', log).group(1))
    fails = []

    # 1. clicks
    sr, x = load(mix); n = clicks(x, sr); print(f"clicks: mix {n}")
    if n > 0: fails.append('clicks in mix')
    for f in sorted(glob.glob(os.path.join(stems, '*.wav'))):
        if 'GuitarDI' in f or 'GuitarFX' in f: continue        # the player's own signal is not ours to judge
        s, y = load(f); c = clicks(y, s); print(f"clicks: {os.path.basename(f)} {c}")
        if c > 0: fails.append('clicks in ' + os.path.basename(f))

    # 2. timing grid (drums vs the band grid)
    d = [f for f in glob.glob(os.path.join(stems, '*Drums*.wav'))]
    if d:
        s, y = load(d[0]); on = onsets(y, s, 0.6 * 60.0 / bpm / 4)
        beat = 60.0 / bpm
        # the band plays straight 16ths or swung/shuffled 8th-note triplets: a hit is on the grid if it sits on either
        def devs(p):
            g16 = beat / 4; g12 = beat / 3; r = on - p
            return np.minimum(np.abs(((r + g16 / 2) % g16) - g16 / 2), np.abs(((r + g12 / 2) % g12) - g12 / 2))
        # grid phase: fitted per section, because the band re-phases to the player on every (re-)entry
        # ("STATE Playing" in the log) — judge steadiness within a section, not one global phase
        starts = [float(v) for v in re.findall(r'^([\d.]+)s STATE Playing', log, re.M)] or [0.0]
        bounds = starts[1:] + [1e9]; phases = np.linspace(0, beat, 400, endpoint=False); dev = []
        for a0, a1 in zip(starts, bounds):
            sel = (on >= a0 - 0.05) & (on < a1 - 0.05)
            if not sel.any(): continue
            full = on; on = full[sel]
            p = phases[int(np.argmin([np.median(devs(q)) for q in phases]))]
            dev.extend(devs(p) * 1000); on = full
        dev = np.array(dev)
        within = np.mean(dev < 20) if len(dev) else 0
        print(f"timing: {len(on)} drum onsets vs 16th/triplet grid at {bpm:.2f} BPM: median |dev| {np.median(dev):.1f} ms, "
              f"{100 * within:.1f}% within 20 ms")
        if within < 0.75 or np.median(dev) > 10: fails.append('drums off grid')

    # 3. bass in key. The demos change key on purpose (to test following), so the hard check is per chord: each bass
    #    note must fit the band chord sounding when it was played (chord tones or that chord's scale, i.e. passing
    #    notes). The whole-take key is reported too, and is a hard check only when --key is given.
    ev = re.findall(r'^([\d.]+)s (BAND CHORD (\S+)|bass (\d+))$', log, re.M)
    cur = None; inch = []; notes = []
    for t, _, ch, b in ev:
        if ch: cur = None if ch == 'N' else ch; continue
        n = int(b); notes.append(n)
        if cur is None: continue
        m = re.match(r'([A-G]#?)(.*)', cur); r = NOTES.index(m.group(1)); q = m.group(2).split('/')[0]
        minor = q.startswith('m') and not q.startswith('maj'); open5 = q.startswith('5') or q.startswith('sus')
        if minor: sc = MINOR + [9]                       # natural minor + dorian 6th
        elif open5: sc = [0, 2, 3, 4, 5, 7, 9, 10]      # no third given: either
        else: sc = MAJOR + [10]                          # major + mixolydian 7th
        inch.append((n - r) % 12 in sc)
    if inch:
        f = np.mean(inch); print(f"bass vs sounding chord: {100 * f:.1f}% of {len(inch)} notes fit")
        if f < 0.9: fails.append('bass outside its chord')
    hard = key is not None
    if key is None:
        m = re.findall(r'key ([A-G]#?) (major|minor)', log)
        key = ' '.join(m[-1]) if m else None
    if key and notes:
        t, q = key.split(); t = NOTES.index(t)
        sc = set((t + i) % 12 for i in (MINOR + MINOR_EXTRA if q == 'minor' else MAJOR))
        inkey = np.mean([n % 12 in sc for n in notes])
        print(f"bass in key ({key}): {100 * inkey:.1f}% of {len(notes)} notes")
        if hard and inkey < 0.85: fails.append('bass out of key')
    elif not inch: print("bass: skipped (no bass notes in log)")

    print("RESULT:", "PASS" if not fails else "FAIL (" + ", ".join(fails) + ")")
    return 1 if fails else 0

if __name__ == '__main__': sys.exit(main())
