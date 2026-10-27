#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
"""Print size, pack sha256 and per-entry sha256 for catalog entries.
Usage: pack_hash.py entries.txt   (one url|dest|note|vel|keytrack line per entry; downloads each URL)"""
import hashlib, sys, urllib.request
pack, total, out = hashlib.sha256(), 0, []
for line in open(sys.argv[1]):
    line = line.strip()
    if not line or line.startswith('#'): continue
    f = line.split('|')[:5]
    data = urllib.request.urlopen(f[0]).read()
    pack.update(data); total += len(data)
    out.append('|'.join(f + [hashlib.sha256(data).hexdigest()]))
print('"size":', total, ', "sha256": "%s",' % pack.hexdigest())
print('"entries": [\n  ' + ',\n  '.join('"%s"' % o for o in out) + '\n]')
