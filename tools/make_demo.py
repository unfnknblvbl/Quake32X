#!/usr/bin/env python3
"""
make_demo.py - turn a planned route (plan_route.py) into a scripted camera
demo for the ROM (DEMO=1 build): a per-tick camera table plus pause-menu
macros that fire at given camera ticks (the camera holds while one runs).

The script shows the screen size option while walking from the start to the
pool, then the same sweep over the pool with the four water settings
(textured / flat shaded x solid / see-through), then wades in, dips under
the surface to look up through it, and follows the channel.

Phases (written to COMM0 for captions) are listed in PHASES below.

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

BTN_UP, BTN_DOWN, BTN_LEFT, BTN_RIGHT, BTN_START = 0x0001, 0x0002, 0x0004, 0x0008, 0x0080
# pause menu items, in order (src/menu.c)
M_TEXTURES, M_LIGHTING, M_WATER, M_FAR, M_SIZE = 0, 1, 2, 3, 4
GAP = 15                     # vblanks between presses: longer than any frame

PHASES = {
    1: 'Full screen (100%)',
    2: 'Screen size 70%',
    3: 'Screen size 50%',
    4: 'Full screen again',
    5: 'Textured, solid water',
    6: 'Textured, see-through water',
    7: 'Flat shaded, see-through water',
    8: 'Flat shaded, solid water',
    9: 'Wading in (textured, see-through)',
    10: 'Under the surface, looking up through it',
    11: 'Along the channel',
}


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


def smoothstep(t):
    t = min(1.0, max(0.0, t))
    return t * t * (3 - 2 * t)


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


def heading(cam, a, b, yaw, ahead, pitch):
    """yaw from the direction of motion, looking a little ahead"""
    for i in range(a, b):
        j0, j1 = max(a, i - 6), min(b - 1, i + ahead)
        dx, dy = cam[j1][0] - cam[j0][0], cam[j1][1] - cam[j0][1]
        if math.hypot(dx, dy) > 4:
            yaw = unwrap(yaw, angle_to(dx, dy))
        cam[i][3] = yaw
        cam[i][4] = pitch
    return yaw


class Menu:
    """pause-menu macros; the menu remembers its cursor between visits"""

    def __init__(self):
        self.cursor = 0
        self.macros = []

    def macro(self, tick, changes, hold=30):
        """changes: [(item, button, count)]; opens the menu, makes them, closes it"""
        steps, t = [(0, BTN_START)], GAP
        for item, button, count in changes:
            while self.cursor != item:
                b = BTN_DOWN if item > self.cursor else BTN_UP
                self.cursor += 1 if b == BTN_DOWN else -1
                steps.append((t, b))
                t += GAP
            for _ in range(count):
                steps.append((t, button))
                t += GAP
        steps.append((t + hold, BTN_START))
        self.macros.append((tick, steps))


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
    menu = Menu()

    # 1-4: from the start to the pool, trying screen sizes on the way
    phases.append((0, 1))
    for _ in range(90):
        cam.append([route[0][0], route[0][1], route[0][2], 90.0, 0.0])
    a = len(cam)
    walk(walk1, WALK, cam)
    b = len(cam)
    smooth(cam, a, b, 0, 14)
    smooth(cam, a, b, 1, 14)
    smooth(cam, a, b, 2, 6)
    heading(cam, a, b, 90.0, 24, 0.0)
    smooth(cam, a, b, 3, 20)
    for i in range(a + 1, b):           # look down while riding the lift
        if cam[i][2] < cam[i - 1][2] - 0.5:
            cam[i][4] = 25.0
    smooth(cam, a, b, 4, 25)
    t70, t50, t100 = 90, 90 + 4 * 60, 90 + 8 * 60
    menu.macro(t70, [(M_SIZE, BTN_LEFT, 3)])
    phases.append((t70, 2))
    menu.macro(t50, [(M_SIZE, BTN_LEFT, 2)])
    phases.append((t50, 3))
    menu.macro(t100, [(M_SIZE, BTN_RIGHT, 5)])
    phases.append((t100, 4))

    def pan(seconds, base, amp, pitch):
        """sweep the view base-amp .. base+amp, easing in from the current view"""
        ex, ey, ez, y0, p0 = cam[-1]
        base = unwrap(y0, base)
        n = int(seconds * TICK)
        for k in range(n):
            t = k / n
            yy = base + amp * math.sin(t * 2 * math.pi)
            blend = smoothstep(k / 60.0)
            cam.append([ex, ey, ez, lerp(y0, yy, blend), lerp(p0, pitch, blend)])

    # 5-8: the same sweep over the pool with each water setting
    PAN = 6.0
    phases.append((len(cam), 5))
    pan(PAN, POOL_YAW, POOL_SWEEP, POOL_PITCH)
    menu.macro(len(cam), [(M_WATER, BTN_RIGHT, 1)])
    phases.append((len(cam), 6))
    pan(PAN, POOL_YAW, POOL_SWEEP, POOL_PITCH)
    menu.macro(len(cam), [(M_TEXTURES, BTN_RIGHT, 1)])
    phases.append((len(cam), 7))
    pan(PAN, POOL_YAW, POOL_SWEEP, POOL_PITCH)
    menu.macro(len(cam), [(M_WATER, BTN_RIGHT, 1)])
    phases.append((len(cam), 8))
    pan(PAN, POOL_YAW, POOL_SWEEP, POOL_PITCH)
    menu.macro(len(cam), [(M_TEXTURES, BTN_RIGHT, 1), (M_WATER, BTN_RIGHT, 1)])

    # 9: into the water, textured and see-through
    phases.append((len(cam), 9))
    a = len(cam)
    walk(walk2, SWIM, cam)
    b = len(cam)
    smooth(cam, a, b, 0, 14)
    smooth(cam, a, b, 1, 14)
    smooth(cam, a, b, 2, 8)
    heading(cam, a, b, cam[a - 1][3], 30, 8.0)
    for i in range(a, min(b, a + 60)):  # ease from the pan into the walk
        t = (i - a) / 60.0
        cam[i][3] = lerp(cam[a - 1][3], cam[i][3], t)
        cam[i][4] = lerp(cam[a - 1][4], cam[i][4], t)
    smooth(cam, a, b, 3, 20)
    for i in range(a, b):
        cam[i][2] = max(cam[i][2], MIN_Z)

    # 10: dip under the surface in the open pool and look up through it
    dip = next(i for i in range(a, b) if cam[i][0] >= 330)
    ex, ey, ez, y0, p0 = cam[dip]
    under = WATER_Z - EYE - 18          # eye 18 units below the surface
    seq = []
    n = 60                              # sink while tilting up
    for k in range(n):
        s = smoothstep(k / n)
        seq.append([ex, ey, lerp(ez, under, s), y0, lerp(p0, -32.0, s)])
    n = 300                             # look around
    for k in range(n):
        seq.append([ex, ey, under, y0 + 70.0 * math.sin(k / n * 2 * math.pi), -32.0])
    n = 60                              # come back up
    for k in range(n):
        s = smoothstep(k / n)
        seq.append([ex, ey, lerp(under, ez, s), y0, lerp(-32.0, p0, s)])
    cam[dip:dip] = seq
    phases.append((dip, 10))
    phases.append((dip + len(seq), 11))

    # end: turn round over the channel
    ex, ey, ez, y0, p0 = cam[-1]
    back = y0 + 165.0
    n = int(2.5 * TICK)
    for k in range(n):
        s = smoothstep(k / n)
        cam.append([ex, ey, ez, lerp(y0, back, s), lerp(p0, 22.0, s)])
    for k in range(120):
        cam.append([ex, ey, ez, back, 22.0])
    phases.append((len(cam), 12))
    for k in range(30):
        cam.append(list(cam[-1]))

    # macros are sorted by tick for the player
    macros = sorted(menu.macros, key=lambda m: m[0])
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
        for tick, ph in sorted(phases):
            f.write('    { %d, %d },\n' % (tick, ph))
        f.write('    { -1, 0 }\n};\n')
    print('camera ticks', len(cam), '(%.1f s)' % (len(cam) / TICK), 'macros at', [m[0] for m in macros])


if __name__ == '__main__':
    main()
