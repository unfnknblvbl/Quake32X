#!/usr/bin/env python3
"""
plan_route.py - find a walkable route through a Quake map using the player
clip hull (hull 1), for scripted walkthrough demos.

usage: plan_route.py pak0.pak maps/e1m1.bsp out.json goal_x goal_y goal_z
                     [--start x y z] [--wet] [--append route.json]

--start starts somewhere other than info_player_start, --wet lets the route
wade through water freely, and --append adds the new leg to the end of an
existing route file (its last point should be the new start).

Samples the hull on a 16-unit grid (4-unit vertical steps), finds standing
spots (origin empty, solid just below), connects neighbours that a player
could walk between (step up <= 18 units, any drop), and runs Dijkstra from
info_player_start to the standing spot closest to the goal. Output: list of
origin points (world units).
"""
import heapq
import json
import math
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mkmap  # noqa: E402

STEP = 16
ZSTEP = 4


def hull_contents(m, pts):
    """vectorized SV_HullPointContents for hull 1; pts: (N,3) float array"""
    planes = np.array([p[:4] for p in m['planes']], dtype=np.float64)
    cn = np.array(m['clipnodes'], dtype=np.int64)          # plane, c0, c1
    node = np.full(len(pts), m['models'][0][10], dtype=np.int64)   # headnode[1]
    for _ in range(200):
        act = node >= 0
        if not act.any():
            break
        idx = node[act]
        pl = planes[cn[idx, 0]]
        d = (pl[:, 0] * pts[act, 0] + pl[:, 1] * pts[act, 1] + pl[:, 2] * pts[act, 2]) - pl[:, 3]
        node[act] = np.where(d < 0, cn[idx, 2], cn[idx, 1])
    return node     # contents (negative)


def main():
    import argparse
    ap = argparse.ArgumentParser()
    ap.add_argument('pak')
    ap.add_argument('bsp')
    ap.add_argument('out')
    ap.add_argument('goal', type=float, nargs=3)
    ap.add_argument('--start', type=float, nargs=3)
    ap.add_argument('--wet', action='store_true')
    ap.add_argument('--append')
    args = ap.parse_args()
    pak = mkmap.read_pak(args.pak)
    m = mkmap.parse_bsp(pak[args.bsp.lower()])
    goal = list(args.goal)
    mins = m['models'][0][0:3]
    maxs = m['models'][0][3:6]
    xs = np.arange(math.floor(mins[0] / STEP) * STEP, maxs[0], STEP)
    ys = np.arange(math.floor(mins[1] / STEP) * STEP, maxs[1], STEP)
    zs = np.arange(math.floor(mins[2] / ZSTEP) * ZSTEP, maxs[2], ZSTEP)
    X, Y, Z = np.meshgrid(xs, ys, zs, indexing='ij')
    pts = np.stack([X.ravel(), Y.ravel(), Z.ravel()], axis=1) + 0.5
    solid = (hull_contents(m, pts) == mkmap.CONTENTS_SOLID).reshape(X.shape)
    empty = ~solid
    stand = np.zeros_like(empty)
    stand[:, :, 1:] = empty[:, :, 1:] & solid[:, :, :-1]
    print('grid', X.shape, 'standing spots', int(stand.sum()))

    ents = mkmap.parse_entities(m['entities'])
    start = None
    for e in ents:
        if e.get('classname') == 'info_player_start':
            start = [float(v) for v in e['origin'].split()]
    first = start
    if args.start:
        start = list(args.start)

    def cell(p):
        return (int(round((p[0] - xs[0]) / STEP)), int(round((p[1] - ys[0]) / STEP)))

    def nearest_stand(p, zrange=64):
        i0, j0 = cell(p)
        best = None
        for di in range(-3, 4):
            for dj in range(-3, 4):
                i, j = i0 + di, j0 + dj
                if not (0 <= i < len(xs) and 0 <= j < len(ys)):
                    continue
                for k in np.nonzero(stand[i, j])[0]:
                    dz = abs(zs[k] - p[2])
                    if dz > zrange:
                        continue
                    d = (xs[i] - p[0]) ** 2 + (ys[j] - p[1]) ** 2 + dz ** 2
                    if best is None or d < best[0]:
                        best = (d, (i, j, int(k)))
        return best[1] if best else None

    s = nearest_stand(start, 80)
    g = nearest_stand(goal, 200)
    print('start cell', s, 'goal cell', g)
    nx, ny, nz = stand.shape
    maxup = 18 // ZSTEP + 1

    def neighbours(c):
        i, j, k = c
        for di in (-1, 0, 1):
            for dj in (-1, 0, 1):
                if not di and not dj:
                    continue
                ii, jj = i + di, j + dj
                if not (0 <= ii < nx and 0 <= jj < ny):
                    continue
                # step up or level
                for kk in range(k, min(nz, k + maxup + 1)):
                    if stand[ii, jj, kk]:
                        # headroom along the way
                        if empty[ii, jj, kk] and empty[i, j, kk] and (not (di and dj) or
                                                                     (empty[ii, j, kk] and empty[i, jj, kk])):
                            yield (ii, jj, kk), math.hypot(di, dj) * STEP + (kk - k) * ZSTEP
                        break
                # walk off a ledge and drop
                if empty[ii, jj, k] and (not (di and dj) or (empty[ii, j, k] and empty[i, jj, k])):
                    kk = k
                    while kk > 0 and empty[ii, jj, kk - 1]:
                        kk -= 1
                    if kk < k and stand[ii, jj, kk] and k - kk < 320 // ZSTEP:
                        yield (ii, jj, kk), math.hypot(di, dj) * STEP + (k - kk) * ZSTEP * 0.5

    # water volumes: hull 0 contents of the origin point (cheap: only used as a cost)
    water_z = -296.0
    wrects = [(-96, 360, 1088, 1328), (440, 640, 896, 1296), (704, 896, 704, 1000), (896, 1008, 480, 928)]

    def inwater(x, y):
        return any(a - 16 <= x <= b + 16 and c - 16 <= y <= d + 16 for a, b, c, d in wrects)

    dist = {s: 0.0}
    prev = {}
    pq = [(0.0, s)]
    while pq:
        d, c = heapq.heappop(pq)
        if c == g:
            break
        if d > dist.get(c, 1e18):
            continue
        # prefer staying away from walls: small penalty next to solid
        for n, w in neighbours(c):
            i, j, k = n
            wall = 0
            for di, dj in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                ii, jj = i + 2 * di, j + 2 * dj
                if 0 <= ii < nx and 0 <= jj < ny and solid[ii, jj, k]:
                    wall += 1
            # avoid wading through water (hull 0 water under the surface level)
            wet = 0
            if not args.wet and zs[k] < water_z + 24 and inwater(xs[i], ys[j]):
                wet = 1
            nd = d + w + wall * 6 + wet * 400
            if nd < dist.get(n, 1e18):
                dist[n] = nd
                prev[n] = c
                heapq.heappush(pq, (nd, n))
    if g not in prev:
        reach = np.array([[xs[c[0]], ys[c[1]], zs[c[2]]] for c in dist])
        print('reachable spots', len(reach), 'bbox', reach.min(0), reach.max(0))
        np.save(args.out + '.reach.npy', reach)
        raise SystemExit('goal not reachable')
    path = [g]
    while path[-1] != s:
        path.append(prev[path[-1]])
    path.reverse()
    out = [[float(xs[i]), float(ys[j]), float(zs[k]) + 2.0] for i, j, k in path]
    if args.append:
        prev = json.load(open(args.append))
        first = prev['start']
        out = prev['path'] + out[1:]
    json.dump({'start': first, 'path': out}, open(args.out, 'w'))
    print('path cells', len(out), 'length %.0f units' % dist[g])


if __name__ == '__main__':
    main()
