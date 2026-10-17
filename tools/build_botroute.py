#!/usr/bin/env python3
"""
build_botroute.py pak0.pak maps/e1m1.bsp legs.json out.json

Plans the demo bot's route leg by leg with plan_route.py (closed doors as
solid, lifts and bridges as floors), refines each leg with refine_route.py,
and writes one route whose 'waypoints' carry the bot's flags:
x, y, z, flags (WP_* in src/bot.h; the high byte is the mover for a wait,
or yaw / 2 for a look).
"""
import json
import os
import subprocess
import sys
import tempfile

WP_JUMP, WP_WAIT, WP_TOUCH, WP_LOOK = 1, 2, 4, 8
HERE = os.path.dirname(os.path.abspath(__file__))


def main():
    pak, bsp, legsf, outf = sys.argv[1:5]
    spec = json.load(open(legsf))
    blocks = spec['blocks']
    pos = None
    wps = []
    path = []
    tmp = tempfile.mkdtemp()
    for i, leg in enumerate(spec['legs']):
        start = leg.get('from', pos)
        args = [sys.executable, '-I', os.path.join(HERE, 'plan_route.py'), pak, bsp,
                os.path.join(tmp, 'leg.json')] + [str(v) for v in leg['to']]
        if start:
            args += ['--start'] + [str(v) for v in start]
        for name, box in blocks.items():
            if name not in leg.get('open', []):
                args += ['--block'] + [str(v) for v in box]
        for box in leg.get('floors', []):
            args += ['--floor'] + [str(v) for v in box]
        r = subprocess.run(args, capture_output=True, text=True)
        if r.returncode:
            raise SystemExit('leg %d: %s' % (i, r.stdout[-300:] + r.stderr[-300:]))
        subprocess.run([sys.executable, '-I', os.path.join(HERE, 'refine_route.py'), pak, bsp,
                        os.path.join(tmp, 'leg.json'), os.path.join(tmp, 'leg.json')],
                       check=True, capture_output=True)
        leg_route = json.load(open(os.path.join(tmp, 'leg.json')))
        pts = leg_route['waypoints']
        path += leg_route['path']
        # a leg's first point is where the last one ended
        for p in pts[1:] if wps else pts:
            wps.append([p[0], p[1], p[2], 0])
        end = wps[-1]
        for act in leg.get('then', []):
            if act[0] == 'touch':
                wps.append([act[1], act[2], end[2], WP_TOUCH])
            elif act[0] == 'wait':
                wps.append([wps[-1][0], wps[-1][1], end[2], WP_WAIT | (act[1] << 8)])
            elif act[0] == 'look':
                wps.append([wps[-1][0], wps[-1][1], end[2], WP_LOOK | ((act[1] // 2) << 8)])
        pos = leg.get('from_next') or leg['to']
        if i + 1 < len(spec['legs']) and 'from' in spec['legs'][i + 1]:
            pos = spec['legs'][i + 1]['from']
        print('leg %d: %d waypoints' % (i, len(pts)))
    json.dump({'start': wps[0][:3], 'path': path, 'waypoints': wps}, open(outf, 'w'))
    print('%d waypoints' % len(wps))


if __name__ == '__main__':
    main()
