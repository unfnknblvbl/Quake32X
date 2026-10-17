#!/usr/bin/env python3
"""
refine_route.py - turn a plan_route.py grid path into bot waypoints.

usage: refine_route.py pak0.pak maps/e1m1.bsp route.json out.json

Each grid point is nudged (up to 12 units) to where it is furthest from
any wall at its height, then the path is shortened greedily: a waypoint is
dropped when the straight walk between its neighbours keeps a 10-unit
margin from solid (player clip hull) all the way. Output keeps the input's
'start' and 'path' and adds 'waypoints'.
"""
import json
import math
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mkmap  # noqa: E402
from plan_route import hull_contents  # noqa: E402

SOLID = mkmap.CONTENTS_SOLID


def main():
    pak = mkmap.read_pak(sys.argv[1])
    m = mkmap.parse_bsp(pak[sys.argv[2].lower()])
    route = json.load(open(sys.argv[3]))
    path = [tuple(p) for p in route['path']]

    def solid(pts):
        return hull_contents(m, np.asarray(pts, dtype=np.float64)) == SOLID

    # clearance of candidate spots: nearest solid sample on rings up to 32
    angs = [2 * math.pi * k / 16 for k in range(16)]
    ring = [(r * math.cos(a), r * math.sin(a)) for r in range(4, 36, 4) for a in angs]
    offs = [(dx, dy) for dx in range(-12, 13, 4) for dy in range(-12, 13, 4)]
    out = []
    for i, (x, y, z) in enumerate(path):
        if i == 0 or i == len(path) - 1:
            out.append((x, y, z))
            continue
        cand = [(x + dx, y + dy) for dx, dy in offs]
        pts = [(cx + rx, cy + ry, z) for cx, cy in cand for rx, ry in ring]
        s = solid(pts).reshape(len(cand), len(ring))
        here = solid([(cx, cy, z) for cx, cy in cand])
        best, bp = -1, (x, y)
        for c in range(len(cand)):
            if here[c]:
                continue
            hit = np.nonzero(s[c])[0]
            clear = 36 if len(hit) == 0 else min(math.hypot(*ring[h]) for h in hit)
            score = clear - 0.05 * math.hypot(cand[c][0] - x, cand[c][1] - y)
            if score > best:
                best, bp = score, cand[c]
        out.append((bp[0], bp[1], z))

    def segment_clear(a, b):
        n = max(2, int(math.dist(a[:2], b[:2]) / 4))
        dx, dy = b[0] - a[0], b[1] - a[1]
        ln = math.hypot(dx, dy) or 1.0
        px, py = -dy / ln * 10, dx / ln * 10
        pts = []
        for k in range(n + 1):
            t = k / n
            x, y = a[0] + dx * t, a[1] + dy * t
            z = max(a[2], b[2]) if abs(a[2] - b[2]) > 24 else a[2] + (b[2] - a[2]) * t
            pts += [(x, y, z), (x + px, y + py, z), (x - px, y - py, z)]
        return not solid(pts).any()

    wps = [out[0]]
    i = 0
    while i < len(out) - 1:
        j = i + 1
        while j + 1 < len(out) and abs(out[j + 1][2] - out[i][2]) <= 24 and segment_clear(out[i], out[j + 1]):
            j += 1
        wps.append(out[j])
        i = j
    route['waypoints'] = [[round(v, 1) for v in p] for p in wps]
    json.dump(route, open(sys.argv[4], 'w'))
    print('%d points -> %d waypoints' % (len(path), len(wps)))


if __name__ == '__main__':
    main()
