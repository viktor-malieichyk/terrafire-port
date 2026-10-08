#!/usr/bin/env python3
"""Compare a port trace (--trace) with a reference trace from the original
(trace_original.py). Rows are aligned by mission_frames once the level's
objects exist; reports the first frame and field that differ.

Usage: trace_compare.py ORIGINAL.csv PORT.csv [fields...]
"""
import csv
import sys

DEFAULT_FIELDS = ('queries', 'events', 'x', 'y', 'vx', 'vy', 'angle', 'shields', 'stage',
                  'tow_angle', 'rand_a', 'rand_b', 'objects', 'p_frozen', 'p_timer0', 'objhash')


def by_frame(path):
    rows = {}
    for r in csv.DictReader(open(path)):
        if int(r['objects']) > 2:        # skip briefing-screen steps
            rows[int(r['mission_frames'])] = r
    return rows


def main(orig_path, port_path, fields):
    orig, port = by_frame(orig_path), by_frame(port_path)
    common = sorted(set(orig) & set(port))
    if not common:
        sys.exit('no overlapping mission_frames')
    for mf in common:
        diffs = [(f, orig[mf][f], port[mf][f]) for f in fields
                 if f in orig[mf] and f in port[mf] and orig[mf][f] != port[mf][f]]
        if diffs:
            print(f'first divergence at mission_frames {mf} (original step {orig[mf]["step"]}, '
                  f'port step {port[mf]["step"]}):')
            for f, a, b in diffs:
                print(f'  {f:10} original {a:>14}   port {b:>14}')
            print(f'matched mission_frames {common[0]}..{mf - 1} ({mf - common[0]} frames)')
            return 1
    print(f'all {len(common)} frames match (mission_frames {common[0]}..{common[-1]})')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1], sys.argv[2], tuple(sys.argv[3:]) or DEFAULT_FIELDS))
