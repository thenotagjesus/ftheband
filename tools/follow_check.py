#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
# Copyright (C) 2026 FtheBand contributors
"""Objective follow check: band chord log vs the played truth of a --render-demo."""
import re, sys
NOTES = ['C','C#','D','D#','E','F','F#','G','G#','A','A#','B']
def parse(n):
    if n == 'N': return None
    m = re.match(r'([A-G]#?)(.*)', n); r = NOTES.index(m.group(1)); q = m.group(2).split('/')[0]
    return (r, q.startswith('m') and not q.startswith('maj'))
def main(base, bpm):
    beat = 60.0 / bpm
    truth = [(float(t[:-1]), parse(c)) for t, c in re.findall(r'(\d+\.\d+s) (\S+)', open(base + '.truth.txt').read())]
    band = [(float(l.split('s')[0]), parse(l.split()[-1])) for l in open(base + '.log.txt') if 'BAND CHORD' in l]
    end = truth[-1][0] + 2 / beat * beat * 1.0 + 4 * beat
    def band_at(t):
        c = None
        for bt, bc in band:
            if bt <= t: c = bc
        return c
    ok = tot = 0; t = truth[0][0]
    while t < end:
        tc = None
        for tt, c in truth:
            if tt <= t: tc = c
        if tc is not None:
            tot += 1; bc = band_at(t); ok += bc is not None and bc[0] == tc[0] and bc[1] == tc[1]
        t += 0.05
    lats = []; miss = 0
    for i, (tt, c) in enumerate(truth):
        if c is None or i == 0: continue
        nxt = truth[i + 1][0] if i + 1 < len(truth) else end
        hit = [bt for bt, bc in band if tt <= bt < nxt and bc and bc[0] == c[0] and bc[1] == c[1]]
        if hit: lats.append((hit[0] - tt) / beat)
        else: miss += 1
    pre = sum(1 for bt, _ in band if bt < truth[0][0])
    lats.sort()
    med = lats[len(lats)//2] if lats else float('nan')
    print(f"{base}: band-chord correct {100*ok/max(1,tot):.1f}% of played time, change latency median {med:.2f} beats max {max(lats) if lats else 0:.2f}, missed {miss}/{miss+len(lats)}, band chord changes during count-in {pre}")
if __name__ == '__main__':
    main(sys.argv[1], float(sys.argv[2]))
