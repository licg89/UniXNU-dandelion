#!/usr/bin/env python3
"""Print the nearest symbol at or below each address, from `nm -n` output on stdin.

nm lists the dyld shared cache's symbols in address order, so one pass over the stream is enough. The
symbols file next to the cache is a binary table and atos refuses the cache itself, which is why this
works on nm's text output instead.
"""
import sys

targets = [int(a, 16) for a in sys.argv[1:] if a]
if not targets:
    raise SystemExit('usage: symbolize-addresses.py <addr> [addr ...]')

pending = {t: True for t in targets}
below = {t: None for t in targets}

for line in sys.stdin:
    parts = line.split()
    if len(parts) < 2:
        continue
    try:
        addr = int(parts[0], 16)
    except ValueError:
        continue
    name = parts[-1]
    for t in targets:
        if not pending[t]:
            continue
        if addr <= t:
            below[t] = (addr, name)
        else:
            nxt = '0x%x %s' % (addr, name)
            if below[t] is None:
                print('0x%x -> (no symbol below; next is %s)' % (t, nxt))
            else:
                base, sym = below[t]
                print('0x%x -> %s + 0x%x   (next symbol %s)' % (t, sym, t - base, nxt))
            pending[t] = False
    if not any(pending.values()):
        break

for t in targets:
    if pending[t]:
        if below[t] is None:
            print('0x%x -> nothing above it in the table' % t)
        else:
            base, sym = below[t]
            print('0x%x -> %s + 0x%x' % (t, sym, t - base))
