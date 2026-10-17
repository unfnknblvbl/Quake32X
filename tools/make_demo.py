#!/usr/bin/env python3
"""
make_demo.py - turn a planned route (plan_route.py) into a scripted camera
demo for the ROM (DEMO=1 build): a per-tick camera table plus pause-menu
macros that fire at given camera ticks.

usage: make_demo.py route.json out.c
"""
import json
import math
import sys

TICK = 60.0
WALK = 210.0 / TICK          # units per tick
SWIM = 140.0 / TICK
LIFT = 110.0 / TICK          # descending a lift / falling into water
POOL_YAW, POOL_SWEEP, POOL_PITCH = 125.0, 35.0, 36.0   # view from the pool's ledge
WATER_Z = -296.0
EYE = 22.0
MIN_Z = WATER_Z - EYE + 10   # wading/swimming: keep the eye 10 units above the water

BTN_RIGHT, BTN_START = 0x0008, 0x0080


def lerp(a, b, t):
    return a + (b - a) * t


def angle_to(dx, dy):
    return math.degrees(math.atan2(dy, dx))


def unwrap(prev, a):
    while a - prev > 180:
        a -= 360
    while a - prev < -180:
        a += 360
    return a


def walk(points, speed, cam):
    """append ticks walking along points (x,y,z origin) to cam"""
    x, y, z = points[0]
    for p in points[1:]:
        dx, dy = p[0] - x, p[1] - y
        dz = p[2] - z
        if dz < -24:                    # drop: ride/fall straight down first
            while z > p[2]:
                z = max(p[2], z - LIFT)
                cam.append([x, y, z, None, None])
        d = math.hypot(dx, dy)
        n = max(1, int(round(d / speed)))
        z0 = z
        for k in range(1, n + 1):
            t = k / n
            cam.append([lerp(x, p[0], t), lerp(y, p[1], t), lerp(z0, p[2], t), None, None])
        x, y, z = p


def smooth(cam, a, b, idx, radius):
    vals = [c[idx] for c in cam[a:b]]
    out = []
    for i in range(len(vals)):
        lo, hi = max(0, i - radius), min(len(vals), i + radius + 1)
        out.append(sum(vals[lo:hi]) / (hi - lo))
    for i, v in enumerate(out):
        cam[a + i][idx] = v


def main():
    route = json.load(open(sys.argv[1]))['path']
    # split where the route drops into the pool
    edge = None
    for i in range(1, len(route)):
        if route[i - 1][2] > -250 and route[i][2] < -280:
            edge = i - 1
            break
    walk1, walk2 = route[:edge + 1], route[edge:]
    cam = []
    phases = []                         # (tick, phase id) for captions

    # 0: stand at the start for 2 s
    phases.append((0, 1))
    for _ in range(120):
        cam.append([route[0][0], route[0][1], route[0][2], 90.0, 0.0])

    # 1: walk to the edge of the pool (textured)
    a = len(cam)
    walk(walk1, WALK, cam)
    b = len(cam)
    smooth(cam, a, b, 0, 14)
    smooth(cam, a, b, 1, 14)
    smooth(cam, a, b, 2, 6)
    # heading from motion, looking a little ahead
    yaw = 90.0
    for i in range(a, b):
        j0, j1 = max(a, i - 6), min(b - 1, i + 24)
        dx, dy = cam[j1][0] - cam[j0][0], cam[j1][1] - cam[j0][1]
        if math.hypot(dx, dy) > 4:
            yaw = unwrap(yaw, angle_to(dx, dy))
        cam[i][3] = yaw
        cam[i][4] = 0.0
    smooth(cam, a, b, 3, 20)
    # look down while riding the lift
    for i in range(a + 1, b):
        if cam[i][2] < cam[i - 1][2] - 0.5:
            cam[i][4] = 25.0
    smooth(cam, a, b, 4, 25)

    TOGGLE = [(0, BTN_START), (45, BTN_RIGHT), (100, BTN_START)]   # pause, Textures, resume
    macros = []

    def pan(seconds, base, amp, pitch):
        """sweep the view base-amp .. base+amp, easing in from the current view"""
        ex, ey, ez, y0, p0 = cam[-1]
        base = unwrap(y0, base)
        n = int(seconds * TICK)
        for k in range(n):
            t = k / n
            yy = base + amp * math.sin(t * 2 * math.pi)
            blend = min(1.0, k / 60.0)
            blend = blend * blend * (3 - 2 * blend)
            cam.append([ex, ey, ez, lerp(y0, yy, blend), lerp(p0, pitch, blend)])

    # 2/3: the pool, textured then flat shaded: the same sweep over the
    # water from the ledge at its east end, so the two passes match
    phases.append((len(cam), 2))
    pan(8.0, POOL_YAW, POOL_SWEEP, POOL_PITCH)
    macros.append((len(cam), TOGGLE))
    phases.append((len(cam), 3))
    pan(8.0, POOL_YAW, POOL_SWEEP, POOL_PITCH)
    macros.append((len(cam), TOGGLE))

    # 4: into the water and along the channel, textured
    phases.append((len(cam), 4))
    a = len(cam)
    walk(walk2, SWIM, cam)
    b = len(cam)
    for i in range(a, b):
        cam[i][2] = max(cam[i][2], MIN_Z)
    smooth(cam, a, b, 0, 14)
    smooth(cam, a, b, 1, 14)
    smooth(cam, a, b, 2, 8)
    yaw = cam[a - 1][3]
    for i in range(a, b):
        j0, j1 = max(a, i - 6), min(b - 1, i + 30)
        dx, dy = cam[j1][0] - cam[j0][0], cam[j1][1] - cam[j0][1]
        if math.hypot(dx, dy) > 4:
            yaw = unwrap(yaw, angle_to(dx, dy))
        cam[i][3] = yaw
        cam[i][4] = 8.0
    # ease from the pan into the walk
    for i in range(a, min(b, a + 60)):
        t = (i - a) / 60.0
        cam[i][3] = lerp(cam[a - 1][3], cam[i][3], t)
        cam[i][4] = lerp(cam[a - 1][4], cam[i][4], t)
    smooth(cam, a, b, 3, 20)

    # 5/6: turn round and look back over the channel at water level,
    # textured then flat shaded
    phases.append((len(cam), 5))
    ex, ey, ez, y0, p0 = cam[-1]
    back = y0 + 165.0
    n = int(2.5 * TICK)
    for k in range(n):
        t = k / n
        s = t * t * (3 - 2 * t)
        cam.append([ex, ey, ez, lerp(y0, back, s), lerp(p0, 22.0, s)])
    seg = int(4.0 * TICK)
    for k in range(seg):
        cam.append([ex, ey, ez, back + 18.0 * math.sin(k / seg * 2 * math.pi), 22.0])
    macros.append((len(cam), TOGGLE))
    phases.append((len(cam), 6))
    for k in range(seg):
        cam.append([ex, ey, ez, back + 18.0 * math.sin(k / seg * 2 * math.pi), 22.0])
    phases.append((len(cam), 7))
    for k in range(30):
        cam.append(list(cam[-1]))

    with open(sys.argv[2], 'w') as f:
        f.write('/* generated by tools/make_demo.py */\n#include "demo.h"\n\n')
        f.write('const demo_cam_t demo_cam[%d] ROMDATA = {\n' % len(cam))
        for c in cam:
            f.write('    { %d, %d, %d, %d, %d },\n' % (
                int(round(c[0] * 65536)), int(round(c[1] * 65536)), int(round(c[2] * 65536)),
                int(round(c[3] * 65536 / 360.0)) & 0xFFFF, int(round(c[4] * 65536 / 360.0))))
        f.write('};\nconst int demo_ncam = %d;\n\n' % len(cam))
        for mi, (tick, steps) in enumerate(macros):
            f.write('static const demo_step_t macro%d[] = { %s, { -1, 0 } };\n' % (
                mi, ', '.join('{ %d, 0x%04x }' % s for s in steps)))
        f.write('const demo_macro_t demo_macros[] = {\n')
        for mi, (tick, steps) in enumerate(macros):
            f.write('    { %d, macro%d },\n' % (tick, mi))
        f.write('    { -1, 0 }\n};\n')
        f.write('const demo_phase_t demo_phases[] = {\n')
        for tick, ph in phases:
            f.write('    { %d, %d },\n' % (tick, ph))
        f.write('    { -1, 0 }\n};\n')
    print('camera ticks', len(cam), '(%.1f s)' % (len(cam) / TICK), 'pool edge', walk1[-1], 'macros at',
          [m[0] for m in macros])


if __name__ == '__main__':
    main()
