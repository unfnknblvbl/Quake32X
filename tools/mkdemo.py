#!/usr/bin/env python3
"""
mkdemo.py - one of Quake's demos (a recording of what the server sent the
client, tools/dem.py) as a compact stream the 32X plays back (src/demo.c).

Everything the playback needs is worked out here: the demo's model and
sound numbers become the level blob's (MDL_, SND_: tools/mkents.py), the
brush models that move become movers (tools/movers.py), the view entity's
updates become the view, and the messages it does not use are left out.

Stream (always big-endian; src/playdemo.c reads it byte by byte):
  header: u32 nframes, u32 nbaselines, u32 baselines ofs, u32 frames ofs,
          u32 strings ofs, u32 max entities in a frame
  baselines: nbaselines x 12 bytes { u8 model, frame, skin, pitch,
             i16 org[3] (1/8 units), u8 yaw, pad }  (angles in 1/256 turn)
             model: MDL_ id; 255 = not drawn
  frames, each: u16 time (1/100 s), u16 pitch, u16 yaw (1/65536 turn),
          then records until END:
    0 END
    1 ENT  u16 num, u8 bits, the fields bits says differ from the baseline:
           1 model u8, 2 frame u8, 4 skin u8, 8 origin i16 x3,
           16 yaw u8, 32 pitch u8, 64 effects u8
    2 MOVER u8 mover, i16 offset[3] (1/8 units)
    3 VIEW i16 origin[3] (1/8 units): the player
    4 SOUND u8 sound, u8 volume, u8 attenuation x 64, i16 origin[3]
    5 PARTICLE i16 origin[3], i8 dir[3] (x16), u8 count, u8 colour
    6 TEMP u8 kind, i16 origin[3]       kinds as Quake's TE_
    7 CLIENT i16 health, u8 armour, u8 ammo, u8 weapon (MDW_, 255 none),
           u8 weapon frame, u8 items (1 silver key, 2 gold key, 4 ring,
           8 quad, 16 suit), i8 view height
    8 DAMAGE u8 armour + blood
    9 BONUS (the gold flash of a pickup)
   10 PRINT u16 offset into the strings
   11 STAT u8 which (0 kills, 1 total monsters, 2 secrets, 3 total secrets), u16 value
"""
import struct
import sys
import os

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dem          # noqa: E402
import mkents       # noqa: E402

TE_KEEP = {'spike': 0, 'superspike': 1, 'gunshot': 2, 'explosion': 3, 'tarexplosion': 4,
           'wizspike': 7, 'knightspike': 8, 'lavasplash': 10, 'teleport': 11}
VIEW_WEAPONS = {'progs/v_axe.mdl': 0, 'progs/v_shot.mdl': 1, 'progs/v_shot2.mdl': 2,
                'progs/v_nail.mdl': 3, 'progs/v_rock.mdl': 4}
# Quake's items bits -> the stream's
ITEM_BITS = {131072: 1, 262144: 2, 524288: 4, 4194304: 8, 2097152: 16}
NOT_DRAWN = 255


def convert(data, ents, models, movers, E='>'):
    track, frames = dem.parse(data)
    mover_of = {m['model']: i for i, m in enumerate(movers)}
    names = dict((p, n) for n, p in mkents.MODELS)
    snd_ids = {s: i for i, s in enumerate(mkents.SOUNDS)}
    mdl_map, snd_map = {}, {}
    view_ent = 1
    need_m, need_s = set(), set()
    baselines = {}
    out = bytearray()
    strings = bytearray()
    nframes = 0
    max_ents = 0
    pending_print = ''
    printed = {}

    def map_model(i):
        if i in mdl_map:
            return mdl_map[i]
        return NOT_DRAWN

    def i16(v):
        v = int(round(v * 8))
        return max(-32768, min(32767, v))

    def ang8(a):
        return int(round(a * 256 / 360)) & 255

    def string(t):
        if t not in printed:
            printed[t] = len(strings)
            strings.extend(t.encode('latin1') + b'\0')
        return printed[t]

    started = False
    last_time = 0.0
    pending_rec = bytearray()
    for f in frames:
        msgs = f['msgs']
        rec = bytearray()
        time = None
        nents = 0
        for m in msgs:
            k = m[0]
            if k == 'serverinfo':
                models_list, sounds_list = m[5], m[6]
                for i, p in enumerate(models_list):
                    if p.startswith('*'):
                        mi = int(p[1:])
                        if mi in mover_of:
                            mdl_map[i] = ('mover', mover_of[mi])
                    else:
                        n = names.get(p)
                        if n is not None:
                            mdl_map[i] = mkents.MODEL_NAMES.index(n)
                    if p in VIEW_WEAPONS:
                        mdl_map[i] = ('view', VIEW_WEAPONS[p])
                for i, p in enumerate(sounds_list):
                    s = p[:-4] if p.endswith('.wav') else p
                    if s in snd_ids:
                        snd_map[i] = snd_ids[s]
            elif k == 'setview':
                view_ent = m[1]
            elif k == 'spawnbaseline':
                baselines[m[1]] = m[2]
            elif k == 'time':
                time = m[1]
            elif k == 'update':
                num, fl = m[1], m[2]
                b = baselines.get(num, dict(model=0, frame=0, skin=0, org=[0, 0, 0], ang=[0, 0, 0]))
                model = fl.get('model', b['model'])
                org = [fl.get('o%d' % i, b['org'][i]) for i in range(3)]
                mm = mdl_map.get(model)
                if num == view_ent:
                    rec += struct.pack(E + 'B3h', 3, *[i16(c) for c in org])
                elif isinstance(mm, tuple) and mm[0] == 'mover':
                    rec += struct.pack(E + 'BB3h', 2, mm[1], *[i16(c) for c in org])
                elif isinstance(mm, int):
                    bits = 0
                    body = bytearray()
                    bm = map_model(b['model'])
                    if map_model(model) != bm:
                        bits |= 1
                        body += bytes([map_model(model)])
                    if fl.get('frame', b['frame']) != b['frame']:
                        bits |= 2
                        body += bytes([fl['frame']])
                    if fl.get('skin', b['skin']) != b['skin']:
                        bits |= 4
                        body += bytes([fl['skin']])
                    if org != list(b['org']):
                        bits |= 8
                        body += struct.pack(E + '3h', *[i16(c) for c in org])
                    yaw = fl.get('a1', b['ang'][1])
                    pitch = fl.get('a0', b['ang'][0])
                    if ang8(yaw) != ang8(b['ang'][1]):
                        bits |= 16
                        body += bytes([ang8(yaw)])
                    if ang8(pitch) != ang8(b['ang'][0]):
                        bits |= 32
                        body += bytes([ang8(pitch)])
                    if fl.get('effects', 0):
                        bits |= 64
                        body += bytes([fl['effects']])
                    rec += struct.pack(E + 'BHB', 1, num, bits) + body
                    need_m.add(mkents.MODEL_NAMES[mm])
                    nents += 1
            elif k == 'sound':
                ent, ch, snd, vol, att, pos = m[1:]
                if snd in snd_map:
                    rec += struct.pack(E + 'BBBB3h', 4, snd_map[snd], vol, int(att * 64),
                                       *[i16(c) for c in pos])
                    need_s.add(mkents.SOUNDS[snd_map[snd]])
            elif k == 'particle':
                pos, d, count, colour = m[1:]
                rec += struct.pack(E + 'B3h3bBB', 5, *[i16(c) for c in pos],
                                   *[max(-128, min(127, int(round(c * 16)))) for c in d], count, colour)
            elif k == 'temp_entity':
                kind = m[1]
                if kind in TE_KEEP:
                    rec += struct.pack(E + 'BB3h', 6, TE_KEEP[kind], *[i16(c) for c in m[2]])
            elif k == 'clientdata':
                c = m[1]
                wm = mdl_map.get(c['weapon'])
                w = wm[1] if isinstance(wm, tuple) and wm[0] == 'view' else 255
                items = 0
                for qb, b in ITEM_BITS.items():
                    if c['items'] & qb:
                        items |= b
                rec += struct.pack(E + 'BhBBBBBb', 7, c['health'], c['armor'], c['ammo'], w,
                                   c['weaponframe'], items, c['viewheight'])
            elif k == 'damage':
                rec += struct.pack(E + 'BB', 8, min(255, m[1] + m[2]))
            elif k == 'stufftext':
                if m[1].strip() == 'bf':
                    rec += bytes([9])
            elif k in ('print', 'centerprint'):
                pending_print += m[1]
                while '\n' in pending_print:
                    line, pending_print = pending_print.split('\n', 1)
                    line = line.strip()
                    if line:
                        rec += struct.pack(E + 'BH', 10, string(line[:38]))
            elif k == 'updatestat':
                which = {14: 0, 12: 1, 13: 2, 11: 3}.get(m[1])
                if which is not None:
                    rec += struct.pack(E + 'BBH', 11, which, m[2])
            elif k == 'killedmonster':
                rec += struct.pack(E + 'BBH', 11, 4, 0)        # one more kill
            elif k == 'foundsecret':
                rec += struct.pack(E + 'BBH', 11, 5, 0)
        if time is None:
            # a block with no time of its own (reliable messages: kills,
            # prints): its records go with the next frame
            pending_rec += rec
            continue
        rec = pending_rec + rec
        pending_rec = bytearray()
        started = True
        pitch, yaw = f['angles'][0], f['angles'][1]
        out += struct.pack(E + 'HHH', int(round(time * 100)), int(round(pitch * 65536 / 360)) & 0xFFFF,
                           int(round(yaw * 65536 / 360)) & 0xFFFF)
        out += rec + bytes([0])
        nframes += 1
        last_time = time
        max_ents = max(max_ents, nents)
    nb = max(baselines) + 1 if baselines else 0
    base = bytearray()
    for i in range(nb):
        b = baselines.get(i)
        if not b or i == view_ent:
            base += struct.pack(E + 'BBBB3hBB', NOT_DRAWN, 0, 0, 0, 0, 0, 0, 0, 0)
            continue
        mm = mdl_map.get(b['model'])
        model = mm if isinstance(mm, int) else NOT_DRAWN
        if isinstance(mm, int):
            need_m.add(mkents.MODEL_NAMES[mm])
        base += struct.pack(E + 'BBBB3hBB', model, b['frame'], b['skin'], ang8(b['ang'][0]),
                            *[i16(c) for c in b['org']], ang8(b['ang'][1]), 0)
    hdr = 6 * 4
    body = bytes(base) + bytes(out)
    while len(body) % 4:
        body += b'\0'
    blob = struct.pack(E + '6I', nframes, nb, hdr, hdr + len(base), hdr + len(body), max_ents) + body + strings
    print('demo: %d frames, %.1f s, %d baselines, %d KB (frames %d KB), up to %d entities a frame' % (
        nframes, last_time, nb, len(blob) // 1024, len(out) // 1024, max_ents))
    return dict(data=blob, count=nframes, needs=(sorted(need_m), sorted(need_s)))


if __name__ == '__main__':
    import mkmap
    import movers as mv
    pak = mkmap.read_pak(sys.argv[1])
    m = mkmap.parse_bsp(pak[sys.argv[2]])
    ents = mkmap.parse_entities(m['entities'])
    d = convert(pak[sys.argv[3]], ents, m['models'], mv.build_movers(ents, m['models']))
    print('needs', d['needs'])
