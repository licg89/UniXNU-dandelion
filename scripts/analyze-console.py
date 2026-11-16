#!/usr/bin/env python3
"""Summarize a dandelion kernel console log: who started, how far the display got, what the probes say.

The console is this port's only trace.  This turns a captured log into the handful of answers that
matter for bring-up, so a flash round is not spent re-reading raw text:

  processes   - every DANDELION-EXEC with its name (the image probe prints comm), and every exit with
                its signal, so a missing daemon is visible instead of inferred
  display     - the DANDELION-DISP stage lines in order: the panel used to be the only place they
                went, so their presence here is itself new
  ia probe    - DANDELION-AI tells whether all_image_info_addr is zero at exit (dyld cleared it) or
                copyin refused it, which decides how an app-region load base can be obtained
  traps       - the BRK records grouped by process, with the unslid address and, when present, the
                containing image base and offset

    python3 scripts/analyze-console.py out/console-sr14-....txt
"""
import argparse
import pathlib
import re
from collections import Counter, OrderedDict

EXEC = re.compile(r'DANDELION-EXEC: pid (\d+) comm=(\S+)')
EXIT = re.compile(r'DANDELION-EXIT: pid=(\d+) comm=(\S+) ppid=(\d+) rv=(\S+) sig=(\S+) status=(\S+)')
BRK = re.compile(r'DANDELION-BRK: pid=(-?\d+) comm=(\S+) pc=(0x[0-9a-fA-F]+) '
                 r'slide=(0x[0-9a-fA-F]+) unslid=(0x[0-9a-fA-F]+) lr=(0x[0-9a-fA-F]+) '
                 r'comment=(0x[0-9a-fA-F]+)')
SR = re.compile(r'DANDELION-SR: pid=(-?\d+) comm=(\S+) sr_slide=(0x[0-9a-fA-F]+) '
                r'sr_base=(0x[0-9a-fA-F]+) task_slide=(0x[0-9a-fA-F]+)')
TEXT = re.compile(r'DANDELION-TEXT: pid=(-?\d+) comm=(\S+) pc=(0x[0-9a-fA-F]+) '
                  r'base=(0x[0-9a-fA-F]+) off=(0x[0-9a-fA-F]+)')
AI = re.compile(r'DANDELION-AI: pid=(-?\d+) comm=(\S+) addr=(0x[0-9a-fA-F]+) copyin=(-?\d+)')
DISP = re.compile(r'DANDELION-DISP\((\d+)\): (.*)')

WATCHED = ('backboardd', 'IOMFB_FDR_Loader', 'watchdogd', 'SpringBoard', 'liveactivitiesd',
           'launchd', 'WindowServer')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('log')
    args = ap.parse_args()
    text = pathlib.Path(args.log).read_bytes().decode('latin-1', 'replace')
    lines = text.splitlines()

    print('log: %s  (%d lines)' % (args.log, len(lines)))

    # --- processes ------------------------------------------------------------
    execs = EXEC.findall(text)
    exits = EXIT.findall(text)
    print('\n== processes')
    print('   EXEC records with a name: %d   EXIT records: %d' % (len(execs), len(exits)))
    if execs:
        counts = Counter(name for _pid, name in execs)
        names = sorted(counts)
        print('   distinct executables: %d' % len(names))
        missing = [w for w in WATCHED if w not in counts]
        print('   watched daemons started here: %s'
              % ', '.join('%s x%d' % (w, counts[w]) for w in WATCHED if w in counts) or 'none')
        if missing:
            print('   watched daemons NOT started: %s' % ', '.join(missing))
        print('   by count: %s'
              % ', '.join('%s x%d' % (n, counts[n]) for n in sorted(counts, key=lambda k: -counts[k])[:12]))
    else:
        print('   (no names: this image predates the exec-name probe)')

    if exits:
        sig = Counter((comm, rv, s) for _pid, comm, _pp, rv, s, _st in exits)
        interesting = [(k, v) for k, v in sig.items() if k[0] in WATCHED]
        for (comm, rv, s), n in sorted(interesting)[:10]:
            print('   exit %-18s rv=%s sig=%s  x%d' % (comm, rv, s, n))

    # --- display -------------------------------------------------------------
    disp = DISP.findall(text)
    print('\n== display stages (DANDELION-DISP)')
    if not disp:
        print('   none: the shim either did not run, or the image predates the console mirror')
    else:
        seen = OrderedDict()
        for line, msg in disp:
            seen.setdefault(line, msg)
        for line, msg in sorted(seen.items(), key=lambda kv: int(kv[0])):
            print('   (%2s) %s' % (line, msg[:110]))

    # --- beat timeline -------------------------------------------------------
    BEAT = re.compile(r'D4K beat (\d+): procs=(\d+) pid1stat=(-?\d+)'
                      r'(?: sb=(-?\d+):(-?\d+) bb=(-?\d+):(-?\d+))?')
    beats = BEAT.findall(text)
    print('\n== beat timeline')
    if not beats:
        print('   none: the beat diagnostic is not in this image')
    else:
        print('   %-5s %6s %8s %10s %10s' % ('beat', 'procs', 'pid1stat', 'springboard', 'backboardd'))
        prev = None
        shown = 0
        for rec in beats:
            n = int(rec[0])
            procs = int(rec[1])
            pid1 = int(rec[2])
            sb = ('%s:%s' % (rec[3], rec[4])) if rec[3] else '-'
            bb = ('%s:%s' % (rec[5], rec[6])) if rec[5] else '-'
            interesting = (prev is None or n % 15 == 0 or sb != prev[2] or bb != prev[3]
                           or abs(procs - prev[1]) > 40)
            if interesting:
                print('   %-5d %6d %8d %10s %10s' % (n, procs, pid1, sb, bb))
                shown += 1
            prev = (n, procs, sb, bb)
        print('   (%d beats, %d shown)' % (len(beats), shown))
        alive = [r for r in beats if r[3] and r[3] != '-1']
        if alive:
            print('   springboard seen in %d of %d beats, pid %s..%s'
                  % (len(alive), len(beats), alive[0][3], alive[-1][3]))
        else:
            print('   springboard never appears in a beat')

    print('\n== all_image_info_addr probe')
    ais = AI.findall(text)
    if not ais:
        print('   none in this log')
    else:
        rc = Counter(r for _p, _c, _a, r in ais)
        zero = sum(1 for _p, _c, a, _r in ais if int(a, 16) == 0)
        print('   records: %d  addr==0: %d  copyin return codes: %s' % (len(ais), zero, dict(rc)))
        if zero == len(ais):
            print('   -> dyld has cleared the address by exit; the base has to be captured at exec')
        elif rc.get('0'):
            print('   -> copyin works in the exit path for this many; the base can come from the struct')
        else:
            print('   -> the address is set but copyin refuses; use a kernel-side source instead')

    # --- traps ---------------------------------------------------------------
    brks = BRK.findall(text)
    print('\n== traps')
    if not brks:
        print('   none')
    else:
        per = OrderedDict()
        for pid, comm, pc, slide, unslid, lr, comment in brks:
            per.setdefault(comm, set()).add((pc, unslid, comment))
        for comm, recs in sorted(per.items()):
            print('   %-18s %d distinct site(s)' % (comm, len(recs)))
            for pc, unslid, comment in sorted(recs)[:3]:
                print('      pc %s unslid %s comment %s' % (pc, unslid, comment))
    srs = SR.findall(text)
    if srs:
        vals = {(c, s) for _p, c, s, _b, _t in srs}
        print('   sr_slide values seen: %s' % ', '.join('%s=%s' % (c, s) for c, s in sorted(vals)))
    texts = TEXT.findall(text)
    if texts:
        print('\n== containing images (DANDELION-TEXT)')
        for pid, comm, pc, base, off in texts[:12]:
            print('   %-18s pc=%s base=%s off=%s' % (comm, pc, base, off))


if __name__ == '__main__':
    main()
