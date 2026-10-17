#!/usr/bin/env python3
"""
dem.py - read a Quake (protocol 15) demo into a list of frames.

    frames = parse(data)

Each frame is the server message of one demo block, decoded: a dict with
the view angles the block carries and the list of messages, each a tuple
(name, fields...). Entity updates arrive as ('update', num, fields) with
only the fields the message holds (the rest come from the baseline).

    python3 -I tools/dem.py pak0.pak demo1.dem    prints a summary
"""
import struct
import sys

SVC = ['bad', 'nop', 'disconnect', 'updatestat', 'version', 'setview', 'sound', 'time',
       'print', 'stufftext', 'setangle', 'serverinfo', 'lightstyle', 'updatename',
       'updatefrags', 'clientdata', 'stopsound', 'updatecolors', 'particle', 'damage',
       'spawnstatic', 'spawnbinary', 'spawnbaseline', 'temp_entity', 'setpause',
       'signonnum', 'centerprint', 'killedmonster', 'foundsecret', 'spawnstaticsound',
       'intermission', 'finale', 'cdtrack', 'sellscreen']

TE_NAMES = {0: 'spike', 1: 'superspike', 2: 'gunshot', 3: 'explosion', 4: 'tarexplosion',
            5: 'lightning1', 6: 'lightning2', 7: 'wizspike', 8: 'knightspike',
            9: 'lightning3', 10: 'lavasplash', 11: 'teleport'}

U_MOREBITS, U_ORIGIN1, U_ORIGIN2, U_ORIGIN3 = 1, 2, 4, 8
U_ANGLE2, U_NOLERP, U_FRAME, U_SIGNAL = 16, 32, 64, 128
U_ANGLE1, U_ANGLE3, U_MODEL, U_COLORMAP = 256, 512, 1024, 2048
U_SKIN, U_EFFECTS, U_LONGENTITY = 4096, 8192, 16384

SU_VIEWHEIGHT, SU_IDEALPITCH, SU_PUNCH1 = 1, 2, 4
SU_VELOCITY1 = 32
SU_ITEMS, SU_ONGROUND, SU_INWATER = 512, 1024, 2048
SU_WEAPONFRAME, SU_ARMOR, SU_WEAPON = 4096, 8192, 16384


class Msg:
    def __init__(self, d):
        self.d = d
        self.o = 0

    def more(self):
        return self.o < len(self.d)

    def byte(self):
        v = self.d[self.o]
        self.o += 1
        return v

    def char(self):
        v = self.byte()
        return v - 256 if v >= 128 else v

    def short(self):
        v = struct.unpack_from('<h', self.d, self.o)[0]
        self.o += 2
        return v

    def long(self):
        v = struct.unpack_from('<i', self.d, self.o)[0]
        self.o += 4
        return v

    def float(self):
        v = struct.unpack_from('<f', self.d, self.o)[0]
        self.o += 4
        return v

    def coord(self):
        return self.short() / 8.0

    def angle(self):
        return self.char() * 360.0 / 256

    def string(self):
        e = self.d.index(b'\0', self.o)
        s = self.d[self.o:e].decode('latin1')
        self.o = e + 1
        return s


def parse_message(m):
    out = []
    while m.more():
        cmd = m.byte()
        if cmd & U_SIGNAL:
            bits = cmd & 127
            if bits & U_MOREBITS:
                bits |= m.byte() << 8
            num = m.short() if bits & U_LONGENTITY else m.byte()
            f = {}
            if bits & U_MODEL: f['model'] = m.byte()
            if bits & U_FRAME: f['frame'] = m.byte()
            if bits & U_COLORMAP: f['colormap'] = m.byte()
            if bits & U_SKIN: f['skin'] = m.byte()
            if bits & U_EFFECTS: f['effects'] = m.byte()
            if bits & U_ORIGIN1: f['o0'] = m.coord()
            if bits & U_ANGLE1: f['a0'] = m.angle()
            if bits & U_ORIGIN2: f['o1'] = m.coord()
            if bits & U_ANGLE2: f['a1'] = m.angle()
            if bits & U_ORIGIN3: f['o2'] = m.coord()
            if bits & U_ANGLE3: f['a2'] = m.angle()
            if bits & U_NOLERP: f['nolerp'] = 1
            out.append(('update', num, f))
            continue
        name = SVC[cmd] if cmd < len(SVC) else 'cmd%d' % cmd
        if name == 'nop':
            out.append((name,))
        elif name == 'disconnect':
            out.append((name,))
        elif name == 'updatestat':
            out.append((name, m.byte(), m.long()))
        elif name == 'version':
            out.append((name, m.long()))
        elif name == 'setview':
            out.append((name, m.short()))
        elif name == 'sound':
            mask = m.byte()
            vol = m.byte() if mask & 1 else 255
            att = m.byte() / 64.0 if mask & 2 else 1.0
            ch = m.short()
            snd = m.byte()
            pos = (m.coord(), m.coord(), m.coord())
            out.append((name, ch >> 3, ch & 7, snd, vol, att, pos))
        elif name == 'time':
            out.append((name, m.float()))
        elif name in ('print', 'stufftext', 'centerprint', 'finale'):
            out.append((name, m.string()))
        elif name == 'setangle':
            out.append((name, m.angle(), m.angle(), m.angle()))
        elif name == 'serverinfo':
            proto = m.long()
            maxc = m.byte()
            gt = m.byte()
            level = m.string()
            models = ['']
            while True:
                s = m.string()
                if not s:
                    break
                models.append(s)
            sounds = ['']
            while True:
                s = m.string()
                if not s:
                    break
                sounds.append(s)
            out.append((name, proto, maxc, gt, level, models, sounds))
        elif name == 'lightstyle':
            out.append((name, m.byte(), m.string()))
        elif name == 'updatename':
            out.append((name, m.byte(), m.string()))
        elif name == 'updatefrags':
            out.append((name, m.byte(), m.short()))
        elif name == 'clientdata':
            bits = m.short()
            c = {'bits': bits}
            c['viewheight'] = m.char() if bits & SU_VIEWHEIGHT else 22
            c['idealpitch'] = m.char() if bits & SU_IDEALPITCH else 0
            punch, vel = [0, 0, 0], [0, 0, 0]
            for i in range(3):
                if bits & (SU_PUNCH1 << i):
                    punch[i] = m.char()
                if bits & (SU_VELOCITY1 << i):
                    vel[i] = m.char() * 16
            c['punch'], c['vel'] = punch, vel
            c['items'] = m.long()
            c['onground'] = bool(bits & SU_ONGROUND)
            c['inwater'] = bool(bits & SU_INWATER)
            c['weaponframe'] = m.byte() if bits & SU_WEAPONFRAME else 0
            c['armor'] = m.byte() if bits & SU_ARMOR else 0
            c['weapon'] = m.byte() if bits & SU_WEAPON else 0
            c['health'] = m.short()
            c['ammo'] = m.byte()
            c['shells'], c['nails'], c['rockets'], c['cells'] = m.byte(), m.byte(), m.byte(), m.byte()
            c['activeweapon'] = m.byte()
            out.append((name, c))
        elif name == 'stopsound':
            v = m.short()
            out.append((name, v >> 3, v & 7))
        elif name == 'updatecolors':
            out.append((name, m.byte(), m.byte()))
        elif name == 'particle':
            pos = (m.coord(), m.coord(), m.coord())
            d = (m.char() / 16.0, m.char() / 16.0, m.char() / 16.0)
            count = m.byte()
            colour = m.byte()
            out.append((name, pos, d, count, colour))
        elif name == 'damage':
            armor = m.byte()
            blood = m.byte()
            pos = (m.coord(), m.coord(), m.coord())
            out.append((name, armor, blood, pos))
        elif name in ('spawnstatic', 'spawnbaseline'):
            num = m.short() if name == 'spawnbaseline' else None
            model, frame, colormap, skin = m.byte(), m.byte(), m.byte(), m.byte()
            org, ang = [0, 0, 0], [0, 0, 0]
            for i in range(3):
                org[i] = m.coord()
                ang[i] = m.angle()
            out.append((name, num, dict(model=model, frame=frame, colormap=colormap, skin=skin,
                                        org=org, ang=ang)))
        elif name == 'temp_entity':
            t = m.byte()
            if t in (5, 6, 9, 13):
                ent = m.short()
                s = (m.coord(), m.coord(), m.coord())
                e = (m.coord(), m.coord(), m.coord())
                out.append((name, TE_NAMES.get(t, t), ent, s, e))
            else:
                p = (m.coord(), m.coord(), m.coord())
                out.append((name, TE_NAMES.get(t, t), p))
        elif name == 'setpause':
            out.append((name, m.byte()))
        elif name == 'signonnum':
            out.append((name, m.byte()))
        elif name in ('killedmonster', 'foundsecret', 'intermission', 'sellscreen'):
            out.append((name,))
        elif name == 'spawnstaticsound':
            pos = (m.coord(), m.coord(), m.coord())
            out.append((name, pos, m.byte(), m.byte(), m.byte() / 64.0))
        elif name == 'cdtrack':
            out.append((name, m.byte(), m.byte()))
        else:
            raise ValueError('unknown svc %d at %d' % (cmd, m.o))
    return out


def parse(data):
    nl = data.index(b'\n')
    o = nl + 1
    frames = []
    while o + 16 <= len(data):
        size = struct.unpack_from('<i', data, o)[0]
        ang = struct.unpack_from('<3f', data, o + 4)
        body = data[o + 16:o + 16 + size]
        o += 16 + size
        frames.append(dict(angles=ang, msgs=parse_message(Msg(body))))
    return data[:nl].decode('latin1'), frames


def main():
    sys.path.insert(0, __file__.rsplit('/', 1)[0])
    import mkmap
    pak = mkmap.read_pak(sys.argv[1])
    track, frames = parse(pak[sys.argv[2]])
    import collections
    counts = collections.Counter()
    times = []
    models = sounds = None
    used_models = collections.Counter()
    used_sounds = collections.Counter()
    tes = collections.Counter()
    weapons = collections.Counter()
    items = set()
    for f in frames:
        for msg in f['msgs']:
            counts[msg[0]] += 1
            if msg[0] == 'serverinfo':
                models, sounds = msg[5], msg[6]
                print('level', msg[4], 'protocol', msg[1])
            elif msg[0] == 'time':
                times.append(msg[1])
            elif msg[0] == 'update' and 'model' in msg[2]:
                used_models[msg[2]['model']] += 1
            elif msg[0] in ('spawnbaseline', 'spawnstatic'):
                used_models[msg[2]['model']] += 1
            elif msg[0] in ('sound', ):
                used_sounds[msg[3]] += 1
            elif msg[0] == 'spawnstaticsound':
                used_sounds[msg[2]] += 1
            elif msg[0] == 'temp_entity':
                tes[msg[1]] += 1
            elif msg[0] == 'clientdata':
                weapons[msg[1]['weapon']] += 1
                items.add(msg[1]['items'])
    print('track', track, 'blocks', len(frames), 'time %.2f .. %.2f' % (times[0], times[-1]))
    for k, v in counts.most_common():
        print('  %-18s %d' % (k, v))
    print('models used:')
    for k, v in sorted(used_models.items()):
        print('  %3d %-28s %d' % (k, models[k] if k < len(models) else '?', v))
    print('sounds used:')
    for k, v in sorted(used_sounds.items()):
        print('  %3d %-28s %d' % (k, sounds[k] if k < len(sounds) else '?', v))
    print('temp entities:', dict(tes))
    print('view weapons:', {models[k] if k else 'none': v for k, v in weapons.items()})
    print('items:', sorted(hex(i) for i in items))


if __name__ == '__main__':
    main()
