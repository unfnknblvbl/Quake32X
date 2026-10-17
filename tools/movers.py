"""
movers.py - Quake's moving brush entities and triggers for the 32X build,
worked out offline from the BSP entity lump (shared by mkmap.py and
mkents.py, so both see the same movers in the same order).

A mover is a func_door, func_door_secret, func_plat, func_button or
func_train. Each gets its path as Quake's QuakeC computes it (doors.qc,
plats.qc, buttons.qc) and a list of keyframes along it, KEY_STEP units
apart: mkmap
clips the mover's faces into the world BSP leaves once per keyframe, and
the renderer draws the keyframe nearest the mover's position, shifted by
the small remainder.

Triggers are trigger_once, trigger_multiple, trigger_secret,
trigger_counter and trigger_teleport, plus the touch fields Quake spawns
for doors and plats.
"""
import math

KEY_STEP = 16

MOVER_CLASSES = ('func_door', 'func_door_secret', 'func_plat', 'func_button', 'func_train')
TRIGGER_CLASSES = ('trigger_once', 'trigger_multiple', 'trigger_secret',
                   'trigger_counter', 'trigger_teleport')
STATIC_CLASSES = ('func_wall', 'func_illusionary', 'func_episodegate', 'func_bossgate')

NOT_MEDIUM = 512


def present(e):
    """single player, normal skill"""
    return not (int(float(e.get('spawnflags', '0'))) & NOT_MEDIUM)


def fnum(e, k, default):
    return float(e[k]) if k in e else default


def movedir(e):
    """SetMovedir: angle -1 up, -2 down, else a yaw"""
    ang = fnum(e, 'angle', 0.0)
    if ang == -1:
        return (0.0, 0.0, 1.0)
    if ang == -2:
        return (0.0, 0.0, -1.0)
    a = math.radians(ang)
    return (round(math.cos(a), 6), round(math.sin(a), 6), 0.0)


def dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def add(a, b, s=1.0):
    return tuple(a[k] + b[k] * s for k in range(3))


def keys_along(points, step=KEY_STEP):
    """keyframes every `step` units along a polyline of offsets"""
    out = [tuple(points[0])]
    for a, b in zip(points, points[1:]):
        d = [b[k] - a[k] for k in range(3)]
        ln = math.sqrt(dot(d, d))
        n = max(1, int(math.ceil(ln / step)))
        for i in range(1, n + 1):
            out.append(tuple(a[k] + d[k] * i / n for k in range(3)))
    return [tuple(round(c) for c in p) for p in out]


def build_movers(ents, models):
    """movers in entity order, each a dict"""
    movers = []
    for e in ents:
        cls = e.get('classname', '')
        mdl = e.get('model', '')
        if cls not in MOVER_CLASSES or not mdl.startswith('*') or not present(e):
            continue
        mi = int(mdl[1:])
        mo = models[mi]
        mins, maxs = tuple(mo[0:3]), tuple(mo[3:6])
        size = tuple(maxs[k] - mins[k] for k in range(3))
        sf = int(float(e.get('spawnflags', '0')))
        m = dict(ent=e, cls=cls, model=mi, mins=mins, maxs=maxs, flags=sf,
                 hull1=mo[10], hull2=mo[11], sounds=int(fnum(e, 'sounds', 0)),
                 targetname=e.get('targetname'), target=e.get('target'),
                 killtarget=e.get('killtarget'), message=e.get('message'),
                 health=int(fnum(e, 'health', 0)))
        zero = (0.0, 0.0, 0.0)
        if cls == 'func_door':
            md = movedir(e)
            lip = fnum(e, 'lip', 8)
            pos1, pos2 = zero, add(zero, md, abs(dot(md, size)) - lip)
            if sf & 1:                          # DOOR_START_OPEN
                pos1, pos2 = pos2, pos1
            m.update(speed=fnum(e, 'speed', 100) or 100, wait=fnum(e, 'wait', 3) or 3,
                     dmg=fnum(e, 'dmg', 2) or 2,
                     pos=[pos1, pos2], start=pos1)
            # key doors (DOOR_GOLD_KEY 8, DOOR_SILVER_KEY 16) stay open
            m['items'] = 2 if sf & 8 else 1 if sf & 16 else 0
            if m['items']:
                m['wait'] = -1
            m['keys'] = keys_along([pos1, pos2])
        elif cls == 'func_door_secret':
            a = math.radians(fnum(e, 'angle', 0))
            fwd = (math.cos(a), math.sin(a), 0.0)
            right = (math.sin(a), -math.cos(a), 0.0)
            up = (0.0, 0.0, 1.0)
            if sf & 4:                          # SECRET_1ST_DOWN
                width = abs(dot(up, size))
                dest1 = add(zero, up, -width)
            else:
                width = abs(dot(right, size))
                temp = 1 - (sf & 2)             # SECRET_1ST_LEFT
                dest1 = add(zero, right, width * temp)
            length = abs(dot(fwd, size))
            dest2 = add(dest1, fwd, length)
            if not m['sounds']:
                m['sounds'] = 3
            shoot = (not m['targetname']) or bool(sf & 16)
            m.update(speed=50, wait=fnum(e, 'wait', 5) or 5, dmg=fnum(e, 'dmg', 2),
                     pos=[zero, dest1, dest2], start=zero, health=10000 if shoot else 0)
            m['keys'] = keys_along([zero, dest1, dest2])
        elif cls == 'func_plat':
            height = fnum(e, 'height', size[2] - 8)
            pos1, pos2 = zero, (0.0, 0.0, -height)
            if not m['sounds']:
                m['sounds'] = 2
            m.update(speed=fnum(e, 'speed', 150) or 150, wait=3, dmg=1, pos=[pos1, pos2],
                     start=pos1 if m['targetname'] else pos2)
            m['keys'] = keys_along([pos1, pos2])
        elif cls == 'func_train':
            # the path_corners from its target, as offsets that put the
            # model's mins on each corner; it stops at a corner with wait -1
            byname = {c.get('targetname'): c for c in ents if c.get('classname') == 'path_corner'}
            pts, seen, t = [], set(), e.get('target')
            while t and t in byname and t not in seen:
                seen.add(t)
                c = byname[t]
                o = [float(v) for v in c['origin'].split()]
                pts.append(tuple(o[k] - mins[k] for k in range(3)))
                if fnum(c, 'wait', 0) == -1 and len(pts) > 1:
                    break
                t = c.get('target')
            assert 2 <= len(pts) <= 3, (mi, pts)
            m.update(speed=fnum(e, 'speed', 100) or 100, wait=-1, dmg=fnum(e, 'dmg', 2) or 2,
                     pos=pts, start=pts[0])
            m['keys'] = keys_along(pts)
        else:                                   # func_button
            md = movedir(e)
            lip = fnum(e, 'lip', 4)
            pos2 = add(zero, md, abs(dot(md, size)) - lip)
            m.update(speed=fnum(e, 'speed', 40) or 40, wait=fnum(e, 'wait', 1) or 1, dmg=0,
                     pos=[zero, pos2], start=zero)
            m['keys'] = keys_along([zero, pos2])
        movers.append(m)
    link_doors(movers)
    return movers


def absbox(m, off):
    return ([m['mins'][k] + off[k] for k in range(3)], [m['maxs'][k] + off[k] for k in range(3)])


def touching(a, b):
    (amin, amax), (bmin, bmax) = a, b
    return all(amin[k] <= bmax[k] and amax[k] >= bmin[k] for k in range(3))


def link_doors(movers):
    """LinkDoors: touching func_doors move together; a group without a
    targetname or health gets a touch field 60 units around it"""
    doors = [i for i, m in enumerate(movers) if m['cls'] == 'func_door' and not (m['flags'] & 4)]
    seen = set()
    for i in doors:
        if i in seen:
            continue
        group, todo = [i], [i]
        seen.add(i)
        while todo:
            j = todo.pop()
            for k in doors:
                if k not in seen and touching(absbox(movers[j], movers[j]['start']),
                                              absbox(movers[k], movers[k]['start'])):
                    seen.add(k)
                    group.append(k)
                    todo.append(k)
        group.sort()
        for k in group:
            movers[k]['group'] = group[0]
        lead = movers[group[0]]
        for k in group:
            for f in ('health', 'targetname', 'message', 'items'):
                if movers[k].get(f) and not lead.get(f):
                    lead[f] = movers[k][f]
        # shootable, fired and key doors spawn no touch field
        if not lead.get('health') and not lead.get('targetname') and not lead.get('items'):
            mins = [min(absbox(movers[k], movers[k]['start'])[0][a] for k in group) for a in range(3)]
            maxs = [max(absbox(movers[k], movers[k]['start'])[1][a] for k in group) for a in range(3)]
            lead['field'] = ([mins[0] - 60, mins[1] - 60, mins[2] - 8],
                             [maxs[0] + 60, maxs[1] + 60, maxs[2] + 8])
    for i, m in enumerate(movers):
        m.setdefault('group', i)


def plat_field(m):
    """plat_spawn_inside_trigger (with the plat at its top position)"""
    mins, maxs = list(m['mins']), list(m['maxs'])
    tmin = [mins[0] + 25, mins[1] + 25, 0]
    tmax = [maxs[0] - 25, maxs[1] - 25, maxs[2] + 8]
    tmin[2] = tmax[2] - (m['pos'][0][2] - m['pos'][1][2] + 8)
    if m['flags'] & 1:                          # PLAT_LOW_TRIGGER
        tmax[2] = tmin[2] + 8
    for k in range(2):
        if maxs[k] - mins[k] <= 50:
            tmin[k] = (mins[k] + maxs[k]) * 0.5
            tmax[k] = tmin[k] + 1
    return tmin, tmax


def build_triggers(ents, models, movers):
    """trigger entities in entity order, then door and plat touch fields"""
    trigs = []
    for e in ents:
        cls = e.get('classname', '')
        mdl = e.get('model', '')
        if cls not in TRIGGER_CLASSES or not mdl.startswith('*') or not present(e):
            continue
        mo = models[int(mdl[1:])]
        t = dict(cls=cls, mins=list(mo[0:3]), maxs=list(mo[3:6]),
                 flags=int(float(e.get('spawnflags', '0'))), sounds=int(fnum(e, 'sounds', 0)),
                 targetname=e.get('targetname'), target=e.get('target'),
                 killtarget=e.get('killtarget'), message=e.get('message'),
                 health=int(fnum(e, 'health', 0)), wait=fnum(e, 'wait', 0.2),
                 count=int(fnum(e, 'count', 2)), facing=None, owner=-1)
        if 'angle' in e and fnum(e, 'angle', 0) != 0:
            t['facing'] = movedir(e)
        if cls == 'trigger_once':
            t['wait'] = -1
        elif cls == 'trigger_secret':
            t['wait'] = -1
            t['message'] = t['message'] or 'You found a secret area!'
            t['sounds'] = t['sounds'] or 1
        elif cls == 'trigger_counter':
            t['wait'] = -1
        elif cls == 'trigger_teleport':
            dest = None
            for d in ents:
                if d.get('classname') == 'info_teleport_destination' and d.get('targetname') == e.get('target'):
                    dest = d
            o = [float(v) for v in dest['origin'].split()]
            t['dest'] = (o[0], o[1], o[2] + 27)
            t['dest_yaw'] = fnum(dest, 'angle', 0)
        trigs.append(t)
    for i, m in enumerate(movers):
        if 'field' in m:
            trigs.append(dict(cls='door_field', mins=m['field'][0], maxs=m['field'][1], owner=i, flags=0,
                              sounds=0, targetname=None, target=None, killtarget=None, message=None,
                              health=0, wait=0, count=0, facing=None))
        if m['cls'] == 'func_plat':
            tmin, tmax = plat_field(m)
            trigs.append(dict(cls='plat_field', mins=tmin, maxs=tmax, owner=i, flags=0, sounds=0,
                              targetname=None, target=None, killtarget=None, message=None,
                              health=0, wait=0, count=0, facing=None))
    return trigs
