#!/usr/bin/env python3
"""
mkents.py - game data for a level blob: entities, flat-coloured models,
sounds, movers and triggers (used by mkmap.py).

The class, model and sound lists below are the numbering the engine uses
for every level; `mkents.py --header src/g_ids.h src/g_frames.h pak0.pak`
writes the matching C headers (ids, and the first frame and length of
each animation of the monster models). A level blob holds only the models
and sounds that level can use (level_needs): the tables have an entry for
every id, 0 where it is left out.

Formats (all big-endian unless mkmap runs with --le):

  entity record, 16 bytes:
    u8 cls, u8 variant, i16 angle (degrees), i16 org[3], i16 half[3],
    u8 targetname, u8 target              (names: indices shared with movers)
    info_intermission: angle = yaw, half[0] = pitch

  model (offsets relative to the model's start):
    u16 numverts, numtris, numframes, numskins
    i32 scale[3], origin[3]       16.16: vertex = byte * scale + origin
    i16 radius, flags             Quake's model flags (8 = rotates, trails
                                  1 2 4 16 32 64 128), 0x4000 = full bright
    u32 tris_ofs, colors_ofs, frames_ofs, bbox_ofs
    tris:   numtris x { u8 v0, v1, v2, pad }
    colors: numskins x numtris bytes: one palette colour per triangle,
            the average of the skin texels the triangle covers
    frames: numframes x numverts x { u8 x, y, z, normal index }
    bbox:   numframes x { u8 min[3], max[3] }

  model table: MDL_NUM x u32 offset (from the blob start), 0 = not here

  sound table: SND_NUM x { u32 offset, u32 length, i32 loop start (-1 none) };
    data is 8-bit unsigned PCM at 11025 Hz; offset 0 = not here
"""
import math
import struct
import sys

CLASSES = [
    'none',
    'monster_army', 'monster_dog', 'monster_ogre', 'monster_knight',
    'monster_demon1', 'monster_wizard', 'monster_zombie',
    'item_health', 'item_shells', 'item_spikes', 'item_rockets',
    'item_armor1', 'item_armor2', 'item_armorInv',
    'weapon_supershotgun', 'weapon_nailgun', 'weapon_grenadelauncher',
    'item_artifact_super_damage', 'item_artifact_envirosuit', 'item_artifact_invisibility',
    'item_key1', 'item_key2',
    'trigger_changelevel', 'backpack', 'info_teleport_destination',
    'misc_explobox', 'trap_spikeshooter',
    'light_torch_small_walltorch', 'light_flame_large_yellow', 'light_flame_small_yellow',
    'ambient_comp_hum', 'ambient_drone', 'ambient_drip', 'ambient_swamp1', 'ambient_swamp2',
    'light_fluoro', 'light_fluorospark',
    'info_intermission',
]

MONSTERS = ['monster_army', 'monster_dog', 'monster_ogre', 'monster_knight',
            'monster_demon1', 'monster_wizard', 'monster_zombie']

MODELS = [
    ('soldier', 'progs/soldier.mdl'),
    ('dog', 'progs/dog.mdl'),
    ('ogre', 'progs/ogre.mdl'),
    ('knight', 'progs/knight.mdl'),
    ('demon', 'progs/demon.mdl'),
    ('wizard', 'progs/wizard.mdl'),
    ('zombie', 'progs/zombie.mdl'),
    ('h_guard', 'progs/h_guard.mdl'),
    ('h_dog', 'progs/h_dog.mdl'),
    ('h_ogre', 'progs/h_ogre.mdl'),
    ('h_knight', 'progs/h_knight.mdl'),
    ('h_demon', 'progs/h_demon.mdl'),
    ('h_wizard', 'progs/h_wizard.mdl'),
    ('h_zombie', 'progs/h_zombie.mdl'),
    ('gib1', 'progs/gib1.mdl'),
    ('gib2', 'progs/gib2.mdl'),
    ('gib3', 'progs/gib3.mdl'),
    ('spike', 'progs/spike.mdl'),
    ('grenade', 'progs/grenade.mdl'),
    ('zom_gib', 'progs/zom_gib.mdl'),
    ('w_spike', 'progs/w_spike.mdl'),
    ('armor', 'progs/armor.mdl'),
    ('g_shot', 'progs/g_shot.mdl'),
    ('g_nail', 'progs/g_nail.mdl'),
    ('g_rock', 'progs/g_rock.mdl'),
    ('backpack', 'progs/backpack.mdl'),
    ('quaddama', 'progs/quaddama.mdl'),
    ('suit', 'progs/suit.mdl'),
    ('invisibl', 'progs/invisibl.mdl'),
    ('w_s_key', 'progs/w_s_key.mdl'),
    ('w_g_key', 'progs/w_g_key.mdl'),
    ('flame', 'progs/flame.mdl'),
    ('flame2', 'progs/flame2.mdl'),
    ('b_bh10', 'maps/b_bh10.bsp'),
    ('b_bh25', 'maps/b_bh25.bsp'),
    ('b_bh100', 'maps/b_bh100.bsp'),
    ('b_shell0', 'maps/b_shell0.bsp'),
    ('b_shell1', 'maps/b_shell1.bsp'),
    ('b_nail0', 'maps/b_nail0.bsp'),
    ('b_nail1', 'maps/b_nail1.bsp'),
    ('b_rock0', 'maps/b_rock0.bsp'),
    ('b_rock1', 'maps/b_rock1.bsp'),
    ('b_explob', 'maps/b_explob.bsp'),
]
MODEL_NAMES = [n for n, _ in MODELS]
FULLBRIGHT = ('flame', 'flame2')

# the monster models whose animations g_frames.h lists
FRAME_MODELS = ['soldier', 'dog', 'ogre', 'knight', 'demon', 'wizard', 'zombie']

SOUNDS = [
    # teleports, the player's weapons and items
    'misc/r_tele1', 'misc/r_tele2', 'misc/r_tele3', 'misc/r_tele4', 'misc/r_tele5',
    'weapons/guncock', 'weapons/shotgn2', 'weapons/rocket1i', 'weapons/spike2',
    'weapons/grenade', 'weapons/bounce', 'weapons/r_exp3', 'weapons/ax1',
    'weapons/pkup', 'weapons/lock4',
    'weapons/tink1', 'weapons/ric1', 'weapons/ric2', 'weapons/ric3',
    'items/health1', 'items/r_item1', 'items/r_item2', 'items/armor1',
    'items/damage', 'items/damage2', 'items/damage3', 'items/suit', 'items/suit2',
    'items/inv1', 'items/inv2', 'items/inv3',
    'misc/medkey', 'misc/runekey',
    # the player
    'player/pain1', 'player/pain2', 'player/pain3', 'player/death1', 'player/death2',
    'player/gib', 'player/udeath', 'player/plyrjmp8', 'player/land',
    'player/axhit2', 'player/h2ojump', 'player/inh2o', 'misc/outwater',
    'player/gasp2', 'player/drown1', 'player/slimbrn2', 'player/lburn1',
    # monsters
    'soldier/sight1', 'soldier/sattck1', 'soldier/pain1', 'soldier/pain2',
    'soldier/death1', 'soldier/idle',
    'dog/dsight', 'dog/dattack1', 'dog/dpain1', 'dog/ddeath', 'dog/idle',
    'ogre/ogwake', 'ogre/ogidle', 'ogre/ogidle2', 'ogre/ogdrag', 'ogre/ogpain1',
    'ogre/ogdth', 'ogre/ogsawatk',
    'knight/ksight', 'knight/idle', 'knight/khurt', 'knight/kdeath', 'knight/sword1',
    'knight/sword2',
    'demon/sight2', 'demon/idle1', 'demon/dpain1', 'demon/ddeath', 'demon/djump',
    'demon/dland2', 'demon/dhit2',
    'wizard/wsight', 'wizard/widle1', 'wizard/widle2', 'wizard/wpain', 'wizard/wdeath',
    'wizard/wattack', 'wizard/hit',
    'zombie/z_idle', 'zombie/z_idle1', 'zombie/z_pain', 'zombie/z_pain1', 'zombie/z_fall',
    'zombie/z_gib', 'zombie/z_shot1', 'zombie/z_hit', 'zombie/z_miss',
    # doors, secret doors, plats, trains, buttons, triggers
    'doors/drclos4', 'doors/doormv1', 'doors/hydro1', 'doors/hydro2',
    'doors/stndr1', 'doors/stndr2', 'doors/ddoor1', 'doors/ddoor2',
    'doors/basesec1', 'doors/basesec2', 'doors/latch2', 'doors/winch2',
    'doors/airdoor1', 'doors/airdoor2', 'doors/medtry', 'doors/meduse',
    'doors/runetry', 'doors/runeuse', 'doors/basetry', 'doors/baseuse',
    'plats/plat1', 'plats/plat2', 'plats/medplat1', 'plats/medplat2',
    'plats/train1', 'plats/train2',
    'buttons/airbut1', 'buttons/switch21', 'buttons/switch02', 'buttons/switch04',
    'misc/talk', 'misc/secret', 'misc/trigger1',
    # static sounds
    'ambience/fire1', 'ambience/comp1', 'ambience/drone6', 'ambience/drip1',
    'ambience/swamp1', 'ambience/swamp2', 'ambience/fl_hum1', 'ambience/buzz1',
]

# creature folders keep their name in the C identifiers (pain1, death1, idle ...)
CREATURES = ('soldier', 'dog', 'player', 'ogre', 'knight', 'demon', 'wizard', 'zombie')

MOVER_IDS = ['func_door', 'func_door_secret', 'func_plat', 'func_button', 'func_train']
TRIGGER_IDS = ['trigger_once', 'trigger_multiple', 'trigger_secret', 'trigger_counter',
               'trigger_teleport', 'door_field', 'plat_field']

SPAWNFLAG_NOT_MEDIUM = 512


def cname(s):
    return s.upper().replace('/', '_').replace('.', '_')


def class_cname(c):
    return cname(c.replace('monster_', '').replace('item_', 'i_').replace('weapon_', 'w_')
                 .replace('trigger_', 't_').replace('info_', '').replace('light_', 'l_')
                 .replace('ambient_', 'amb_'))


def sound_cname(s):
    folder, name = s.split('/')
    if folder in CREATURES:
        name = folder + '_' + name
    return cname(name)


def write_header(path):
    with open(path, 'w') as f:
        f.write('/* generated by tools/mkents.py --header: do not edit */\n')
        f.write('#ifndef G_IDS_H\n#define G_IDS_H\n\n')
        f.write('enum {\n')
        for i, c in enumerate(CLASSES):
            f.write('    CL_%s = %d,\n' % (class_cname(c), i))
        f.write('    CL_NUM\n};\n\nenum {\n')
        for i, (n, _) in enumerate(MODELS):
            f.write('    MDL_%s = %d,\n' % (cname(n), i))
        f.write('    MDL_NUM\n};\n\nenum {\n')
        for i, s in enumerate(SOUNDS):
            f.write('    SND_%s = %d,\n' % (sound_cname(s), i))
        f.write('    SND_NUM\n};\n\nenum {\n')
        for i, c in enumerate(MOVER_IDS):
            f.write('    MV_%s = %d,\n' % (cname(c.replace('func_', '')), i))
        f.write('    MV_NUM\n};\n\nenum {\n')
        for i, c in enumerate(TRIGGER_IDS):
            f.write('    TR_%s = %d,\n' % (cname(c.replace('trigger_', '')), i))
        f.write('    TR_NUM\n};\n\n#endif\n')


def frame_groups(d):
    """the animations of an .mdl: [(name, first, count)] by frame name"""
    names = mdl_frame_names(d)
    groups = []
    for i, n in enumerate(names):
        base = n.rstrip('0123456789')
        if groups and groups[-1][0] == base and groups[-1][1] + groups[-1][2] == i:
            groups[-1][2] += 1
        else:
            groups.append([base, i, 1])
    return groups


def write_frames(path, pak):
    with open(path, 'w') as f:
        f.write('/* generated by tools/mkents.py --header: the monsters\' animations\n')
        f.write(' * (first frame and length, from the frame names in the .mdl files) */\n')
        f.write('#ifndef G_FRAMES_H\n#define G_FRAMES_H\n\n')
        for name in FRAME_MODELS:
            path_ = dict(MODELS)[name]
            seen = set()
            for g, first, n in frame_groups(pak[path_]):
                key = cname(name + '_' + g)
                if key in seen:
                    continue
                seen.add(key)
                f.write('#define FR_%s %d\n#define FN_%s %d\n' % (key, first, key, n))
            f.write('\n')
        f.write('#endif\n')


# ---------------------------------------------------------------- names

class Names:
    """targetname/target strings -> small numbers, shared by entities,
    movers and triggers"""
    def __init__(self):
        self.ids = {}

    def __call__(self, n):
        if not n:
            return 0
        if n not in self.ids:
            self.ids[n] = len(self.ids) + 1
        assert self.ids[n] < 256
        return self.ids[n]


# ---------------------------------------------------------------- what a level needs

ITEM_DEPS = {
    'item_health': (['b_bh10', 'b_bh25', 'b_bh100'], ['items/health1', 'items/r_item1', 'items/r_item2']),
    'item_shells': (['b_shell0', 'b_shell1'], []),
    'item_spikes': (['b_nail0', 'b_nail1'], []),
    'item_rockets': (['b_rock0', 'b_rock1'], []),
    'item_armor1': (['armor'], ['items/armor1']),
    'item_armor2': (['armor'], ['items/armor1']),
    'item_armorInv': (['armor'], ['items/armor1']),
    'weapon_supershotgun': (['g_shot'], []),
    'weapon_nailgun': (['g_nail'], []),
    'weapon_grenadelauncher': (['g_rock'], []),
    'item_artifact_super_damage': (['quaddama'], ['items/damage', 'items/damage2', 'items/damage3']),
    'item_artifact_envirosuit': (['suit'], ['items/suit', 'items/suit2']),
    'item_artifact_invisibility': (['invisibl'], ['items/inv1', 'items/inv2', 'items/inv3']),
    'misc_explobox': (['b_explob'], ['weapons/r_exp3']),
    'trap_spikeshooter': (['spike'], ['weapons/spike2']),
    'light_torch_small_walltorch': (['flame'], ['ambience/fire1']),
    'light_flame_large_yellow': (['flame2'], ['ambience/fire1']),
    'light_flame_small_yellow': (['flame2'], ['ambience/fire1']),
    'ambient_comp_hum': ([], ['ambience/comp1']),
    'ambient_drone': ([], ['ambience/drone6']),
    'ambient_drip': ([], ['ambience/drip1']),
    'ambient_swamp1': ([], ['ambience/swamp1']),
    'ambient_swamp2': ([], ['ambience/swamp2']),
    'light_fluoro': ([], ['ambience/fl_hum1']),
    'light_fluorospark': ([], ['ambience/buzz1']),
    'trigger_teleport': ([], []),
}

MONSTER_DEPS = {
    'monster_army': (['soldier', 'h_guard', 'backpack'], 'soldier'),
    'monster_dog': (['dog', 'h_dog'], 'dog'),
    'monster_ogre': (['ogre', 'h_ogre', 'grenade', 'backpack'], 'ogre'),
    'monster_knight': (['knight', 'h_knight'], 'knight'),
    'monster_demon1': (['demon', 'h_demon'], 'demon'),
    'monster_wizard': (['wizard', 'h_wizard', 'w_spike'], 'wizard'),
    'monster_zombie': (['zombie', 'h_zombie', 'zom_gib'], 'zombie'),
}

# every level: the player, the weapons, gibs, teleports, messages
BASE_MODELS = ['gib1', 'gib2', 'gib3', 'spike', 'grenade', 'backpack']
BASE_SOUNDS = [s for s in SOUNDS if s.split('/')[0] in ('player', 'weapons')
               or s in ('misc/r_tele1', 'misc/r_tele2', 'misc/r_tele3', 'misc/r_tele4',
                        'misc/r_tele5', 'misc/outwater', 'misc/h2ohit1', 'misc/talk',
                        'misc/secret', 'misc/trigger1')]


def key_names(world):
    wt = int(float(world.get('worldtype', '0')))
    return {0: ('misc/medkey', 'doors/medtry', 'doors/meduse'),
            1: ('misc/runekey', 'doors/runetry', 'doors/runeuse'),
            2: ('misc/medkey', 'doors/basetry', 'doors/baseuse')}[wt]


def mover_sounds(m, world):
    """the sounds a mover makes (doors.qc, plats.qc, buttons.qc)"""
    s = m['sounds']
    if m['cls'] == 'func_door':
        out = {1: ['doors/drclos4', 'doors/doormv1'], 2: ['doors/hydro1', 'doors/hydro2'],
               3: ['doors/stndr1', 'doors/stndr2'], 4: ['doors/ddoor1', 'doors/ddoor2']}.get(s, [])
        if m.get('items'):
            out += list(key_names(world)[1:])
        return out
    if m['cls'] == 'func_door_secret':
        return {1: ['doors/latch2', 'doors/winch2', 'doors/drclos4'],
                2: ['doors/airdoor1', 'doors/airdoor2'],
                3: ['doors/basesec1', 'doors/basesec2']}.get(s, [])
    if m['cls'] == 'func_plat':
        return ['plats/plat1', 'plats/plat2'] if s == 1 else ['plats/medplat1', 'plats/medplat2']
    if m['cls'] == 'func_train':
        return ['plats/train1', 'plats/train2'] if s == 1 else []
    return [{0: 'buttons/airbut1', 1: 'buttons/switch21', 2: 'buttons/switch02',
             3: 'buttons/switch04'}[s & 3]]


def present(e):
    return not (int(float(e.get('spawnflags', '0'))) & SPAWNFLAG_NOT_MEDIUM)


def level_needs(ents, movers, world, extra_models=(), extra_sounds=()):
    """the models and sounds a level can use"""
    models, sounds = set(BASE_MODELS), set(BASE_SOUNDS)
    for e in ents:
        cn = e.get('classname', '')
        if not present(e):
            continue
        if cn in MONSTER_DEPS:
            ms, folder = MONSTER_DEPS[cn]
            models.update(ms)
            sounds.update(s for s in SOUNDS if s.startswith(folder + '/'))
            if cn == 'monster_ogre':
                sounds.update(['weapons/grenade', 'weapons/bounce', 'weapons/r_exp3'])
        elif cn in ITEM_DEPS:
            ms, ss = ITEM_DEPS[cn]
            models.update(ms)
            sounds.update(ss)
        elif cn in ('item_key1', 'item_key2'):
            models.add('w_s_key' if cn == 'item_key1' else 'w_g_key')
            sounds.add(key_names(world)[0])
    for m in movers:
        sounds.update(mover_sounds(m, world))
    sounds.update(['misc/talk', 'misc/secret', 'misc/trigger1'])
    models.update(extra_models)
    sounds.update(extra_sounds)
    return models, sounds


# ---------------------------------------------------------------- entities

def entities(ents, models, E, names, world):
    """entity records for the classes the engine knows, normal skill"""
    out = bytearray()
    n = 0
    for e in ents:
        cn = e.get('classname', '')
        sf = int(float(e.get('spawnflags', '0')))
        if sf & SPAWNFLAG_NOT_MEDIUM:
            continue
        if cn not in CLASSES:
            continue
        variant = 0
        if cn in ('item_health', 'item_shells', 'item_spikes', 'item_rockets'):
            variant = sf & 3            # health: 1 rotten, 2 mega; ammo: 1 big
        elif cn in ('item_key1', 'item_key2'):
            variant = int(float(world.get('worldtype', '0')))
        elif cn == 'trap_spikeshooter':
            variant = sf & 3            # 1 super spike, 2 laser
        elif cn == 'monster_zombie':
            variant = sf & 1            # crucified
        org = [0, 0, 0]
        half = [0, 0, 0]
        if 'origin' in e:
            org = [int(round(float(v))) for v in e['origin'].split()]
        angle = int(float(e.get('angle', '0')))
        if cn == 'trigger_changelevel':
            mi = int(e['model'][1:])
            mn, mx = models[mi][0:3], models[mi][3:6]
            org = [int(round((mn[k] + mx[k]) / 2)) for k in range(3)]
            half = [int(round((mx[k] - mn[k]) / 2)) for k in range(3)]
        elif cn == 'info_intermission':
            ma = [float(v) for v in e.get('mangle', '0 0 0').split()]
            angle = int(round(ma[1]))
            half = [int(round(ma[0])), 0, 0]
        out += struct.pack(E + 'BBh3h3hBB', CLASSES.index(cn), variant, angle, *org, *half,
                           names(e.get('targetname')), names(e.get('target')))
        n += 1
    return out, n


# ---------------------------------------------------------------- models

def palette_nearest(pal, rgb, limit=224):
    best, bi = 1e18, 0
    for i in range(limit):
        d = (pal[3 * i] - rgb[0]) ** 2 + (pal[3 * i + 1] - rgb[1]) ** 2 + (pal[3 * i + 2] - rgb[2]) ** 2
        if d < best:
            best, bi = d, i
    return bi


def tri_colour(skin, sw, sh, pts, pal, limit=224):
    """average colour of the skin texels inside a triangle (in skin space)"""
    xs = [p[0] for p in pts]
    ys = [p[1] for p in pts]
    x0, x1 = max(0, int(min(xs))), min(sw - 1, int(math.ceil(max(xs))))
    y0, y1 = max(0, int(min(ys))), min(sh - 1, int(math.ceil(max(ys))))
    (ax, ay), (bx, by), (cx, cy) = pts
    area = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax)
    acc = [0, 0, 0]
    n = 0
    if abs(area) > 1e-6:
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                px, py = x + 0.5, y + 0.5
                w0 = ((bx - px) * (cy - py) - (by - py) * (cx - px)) / area
                w1 = ((cx - px) * (ay - py) - (cy - py) * (ax - px)) / area
                w2 = 1 - w0 - w1
                if w0 >= -0.01 and w1 >= -0.01 and w2 >= -0.01:
                    c = skin[y * sw + x]
                    acc[0] += pal[3 * c]
                    acc[1] += pal[3 * c + 1]
                    acc[2] += pal[3 * c + 2]
                    n += 1
    if n == 0:   # tiny triangle: the texel under its centre
        x = min(sw - 1, max(0, int(sum(xs) / 3)))
        y = min(sh - 1, max(0, int(sum(ys) / 3)))
        c = skin[y * sw + x]
        return palette_nearest(pal, (pal[3 * c], pal[3 * c + 1], pal[3 * c + 2]), limit)
    return palette_nearest(pal, (acc[0] / n, acc[1] / n, acc[2] / n), limit)


def mdl_frame_names(d):
    """the frame names of an .mdl (the first of each group)"""
    nskins, sw, sh, nverts, ntris, nframes = struct.unpack('<6i', d[48:72])
    o = 84
    for _ in range(nskins):
        t = struct.unpack('<i', d[o:o + 4])[0]
        o += 4
        if t == 0:
            o += sw * sh
        else:
            n = struct.unpack('<i', d[o:o + 4])[0]
            o += 4 + 4 * n + n * sw * sh
    o += 12 * nverts + 16 * ntris
    names = []
    for _ in range(nframes):
        t = struct.unpack('<i', d[o:o + 4])[0]
        o += 4
        if t == 0:
            names.append(d[o + 8:o + 24].split(b'\0')[0].decode('latin1'))
            o += 24 + 4 * nverts
        else:
            n = struct.unpack('<i', d[o:o + 4])[0]
            o += 4 + 8 + 4 * n
            names.append(d[o + 8:o + 24].split(b'\0')[0].decode('latin1'))
            o += n * (24 + 4 * nverts)
    return names


def load_mdl(d, pal, fullbright=False):
    ident, ver = struct.unpack('<4si', d[:8])
    assert ident == b'IDPO' and ver == 6
    scale = struct.unpack('<3f', d[8:20])
    sorg = struct.unpack('<3f', d[20:32])
    radius = struct.unpack('<f', d[32:36])[0]
    nskins, sw, sh, nverts, ntris, nframes, sync, flags = struct.unpack('<8i', d[48:80])
    o = 84
    skins = []
    for _ in range(nskins):
        t = struct.unpack('<i', d[o:o + 4])[0]
        o += 4
        if t == 0:
            skins.append(d[o:o + sw * sh])
            o += sw * sh
        else:
            n = struct.unpack('<i', d[o:o + 4])[0]
            o += 4 + 4 * n
            skins.append(d[o:o + sw * sh])
            o += n * sw * sh
    st = []
    for _ in range(nverts):
        st.append(struct.unpack('<3i', d[o:o + 12]))
        o += 12
    tris = []
    for _ in range(ntris):
        tris.append(struct.unpack('<4i', d[o:o + 16]))
        o += 16
    frames = []
    for _ in range(nframes):
        t = struct.unpack('<i', d[o:o + 4])[0]
        o += 4
        if t == 0:
            o += 8 + 16
            frames.append(d[o:o + 4 * nverts])
            o += 4 * nverts
        else:
            n = struct.unpack('<i', d[o:o + 4])[0]
            o += 4 + 8 + 4 * n
            first = None
            for k in range(n):
                o += 8 + 16
                if first is None:
                    first = d[o:o + 4 * nverts]
                o += 4 * nverts
            frames.append(first)
    colours = []
    for skin in skins:
        cl = bytearray()
        for facesfront, a, b, c in tris:
            pts = []
            for v in (a, b, c):
                onseam, s, t = st[v]
                if onseam and not facesfront:
                    s += sw // 2
                pts.append((s, t))
            # full bright models (flames) may use the bright end of the palette
            cl.append(tri_colour(skin, sw, sh, pts, pal, 256 if fullbright else 224))
        colours.append(cl)
    if fullbright:
        flags |= 0x4000
    return dict(scale=scale, origin=sorg, radius=radius, flags=flags, nverts=nverts,
                tris=[(a, b, c) for _, a, b, c in tris], frames=frames, colours=colours)


def nearest_normal(n, anorms):
    best, bi = -2, 0
    for i, a in enumerate(anorms):
        d = a[0] * n[0] + a[1] * n[1] + a[2] * n[2]
        if d > best:
            best, bi = d, i
    return bi


def load_bsp_model(d, pal, mkmap, anorms):
    """an item box (maps/b_*.bsp) as a one-frame model, one vertex set per face"""
    m = mkmap.parse_bsp(d)
    texinfo, textures = m['texinfo'], m['textures']
    pts_all, tris, cols = [], [], []
    for f in m['faces'][m['models'][0][14]:m['models'][0][14] + m['models'][0][15]]:
        pts = mkmap.face_polygon(m, f)
        pl = m['planes'][f[0]]
        n = pl[0:3] if not f[1] else (-pl[0], -pl[1], -pl[2])
        t = textures[texinfo[f[4]][8]]
        mip = t['mips'][0]
        acc = [0, 0, 0]
        for c in mip:
            acc[0] += pal[3 * c]
            acc[1] += pal[3 * c + 1]
            acc[2] += pal[3 * c + 2]
        col = palette_nearest(pal, [a / len(mip) for a in acc])
        ni = nearest_normal(n, anorms)
        base = len(pts_all)
        for p in pts:
            pts_all.append((p, ni))
        for k in range(1, len(pts) - 1):
            tris.append((base, base + k, base + k + 1))
            cols.append(col)
    mins = [min(p[0][k] for p in pts_all) for k in range(3)]
    maxs = [max(p[0][k] for p in pts_all) for k in range(3)]
    scale = [max(1e-3, (maxs[k] - mins[k]) / 255.0) for k in range(3)]
    frame = bytearray()
    for p, ni in pts_all:
        for k in range(3):
            frame.append(int(round((p[k] - mins[k]) / scale[k])))
        frame.append(ni)
    radius = math.sqrt(sum(max(abs(mins[k]), abs(maxs[k])) ** 2 for k in range(3)))
    return dict(scale=scale, origin=mins, radius=radius, flags=0, nverts=len(pts_all),
                tris=tris, frames=[bytes(frame)], colours=[bytes(cols)])


def build_model(md, fx, E):
    nv, nt, nf, ns = md['nverts'], len(md['tris']), len(md['frames']), len(md['colours'])
    assert nv <= 256, nv
    hdr_size = 4 * 2 + 6 * 4 + 2 * 2 + 4 * 4
    tris = bytearray()
    for a, b, c in md['tris']:
        tris += bytes((a, b, c, 0))
    cols = bytearray()
    for cl in md['colours']:
        cols += cl
    while len(cols) % 4:
        cols.append(0)
    frames = b''.join(md['frames'])
    # per-frame bounding box of the byte vertices: tight screen rectangles
    bbox = bytearray()
    for fr in md['frames']:
        for k in range(3):
            bbox.append(min(fr[4 * i + k] for i in range(nv)))
        for k in range(3):
            bbox.append(max(fr[4 * i + k] for i in range(nv)))
    while len(bbox) % 4:
        bbox.append(0)
    t_ofs = hdr_size
    c_ofs = t_ofs + len(tris)
    f_ofs = c_ofs + len(cols)
    b_ofs = f_ofs + len(frames)
    h = struct.pack(E + '4H', nv, nt, nf, ns)
    h += struct.pack(E + '6i', *[fx(v) for v in md['scale']], *[fx(v) for v in md['origin']])
    h += struct.pack(E + 'hh', int(math.ceil(md['radius'])), md['flags'])
    h += struct.pack(E + '4I', t_ofs, c_ofs, f_ofs, b_ofs)
    assert len(h) == hdr_size
    return bytes(h + tris + cols + frames + bbox)


_model_cache = {}


def model_blob(name, pak, pal, fx, E, mkmap, anorms):
    key = (name, E)
    if key not in _model_cache:
        path = dict(MODELS)[name]
        d = pak[path]
        md = load_mdl(d, pal, name in FULLBRIGHT) if path.endswith('.mdl') \
            else load_bsp_model(d, pal, mkmap, anorms)
        _model_cache[key] = build_model(md, fx, E)
    return _model_cache[key]


# ---------------------------------------------------------------- sounds

def load_wav(d):
    """8-bit 11025 Hz samples and the loop start (-1: none), as Quake's
    GetWavinfo finds it (the first cue point)"""
    assert d[:4] == b'RIFF' and d[8:12] == b'WAVE'
    o = 12
    fmt = None
    data = None
    loop = -1
    loop_len = None
    while o + 8 <= len(d):
        cid, size = d[o:o + 4], struct.unpack('<I', d[o + 4:o + 8])[0]
        body = d[o + 8:o + 8 + size]
        if cid == b'fmt ':
            fmt = struct.unpack('<HHIIHH', body[:16])
        elif cid == b'data':
            data = body
        elif cid == b'cue ' and len(body) >= 28:
            loop = struct.unpack('<i', body[24:28])[0]
        elif cid == b'LIST' and loop >= 0 and body[20:24] == b'mark':
            loop_len = struct.unpack('<i', body[16:20])[0]
        o += 8 + size + (size & 1)
    tag, ch, rate, _, _, bits = fmt
    assert tag == 1 and ch == 1, fmt
    if bits == 16:
        data = bytes(((struct.unpack('<h', data[i:i + 2])[0] >> 8) + 128) & 255 for i in range(0, len(data), 2))
    if loop >= 0 and loop_len:
        data = data[:loop + loop_len]
    if rate != 11025:   # nearest-sample resample
        n = int(len(data) * 11025 / rate)
        data = bytes(data[int(i * rate / 11025)] for i in range(n))
        if loop >= 0:
            loop = int(loop * 11025 / rate)
    return bytes(data), loop


def add_movers(blob, ents, models, movers, E, names, world):
    """doors, plats, buttons, trains, their keyframes, triggers and messages"""
    import movers as mv
    trigs = mv.build_triggers(ents, models, movers)
    # string 1 is always the level's name (worldspawn's message)
    msgs = ['', (world.get('message', '') or ' ').replace('\n', ' ').strip()]

    def msg(t):
        if not t:
            return 0
        t = t.replace('\n', ' ').strip()
        if t not in msgs:
            msgs.append(t)
        return msgs.index(t)

    def i16(v):
        v = int(round(v))
        assert -32768 <= v < 32768, v
        return v

    def wait10(w):
        return -1 if w < 0 else i16(w * 10)

    out = {}
    keys = bytearray()
    recs = bytearray()
    nkeys = 0
    for m in movers:
        startpos = 0
        if m['cls'] == 'func_plat' and not m['targetname']:
            startpos = 1
        pos = list(m['pos']) + [(0, 0, 0)] * (3 - len(m['pos']))
        assert len(m['pos']) <= 3, m['pos']
        # qbsp stores model bounds 1 unit inside the brush; SV_LinkEdict
        # adds the unit back, which gives the brush's own bounds
        mins = [i16(m['mins'][k] - 1) for k in range(3)]
        maxs = [i16(m['maxs'][k] + 1) for k in range(3)]
        flags = m['flags'] & 0xFFFF
        if m['cls'] == 'func_door' and m.get('items'):
            flags = (flags & ~0x18) | (0x8000 if m['items'] == 2 else 0x4000)   # key door
        recs += struct.pack(E + 'BBBBBBBBHHhhhhhh3h3h9hBB',
                            MOVER_IDS.index(m['cls']), m['sounds'], names(m['targetname']),
                            names(m['target']), msg(m['message']), m['group'], len(m['keys']), startpos,
                            flags, nkeys, i16(m['speed']), wait10(m['wait']), i16(m['dmg']),
                            i16(m['health']), m['hull1'], m['hull2'], *mins, *maxs,
                            *[i16(c) for p in pos for c in p], len(m['pos']), 0)
        for k in m['keys']:
            keys += struct.pack(E + '3h', *[i16(c) for c in k])
            nkeys += 1
    out['movers'] = (blob.add(bytes(recs)), len(movers))
    out['mkeys'] = (blob.add(bytes(keys)), nkeys)
    recs = bytearray()
    for t in trigs:
        facing = -1000
        if t['facing']:
            facing = i16(math.degrees(math.atan2(t['facing'][1], t['facing'][0])))
        dest = t.get('dest', (0, 0, 0))
        recs += struct.pack(E + 'BBBBBBHhhhh3h3hh3hhH',
                            TRIGGER_IDS.index(t['cls']), t['sounds'], names(t['targetname']),
                            names(t['target']), names(t['killtarget']), msg(t['message']),
                            t['flags'] & 0xFFFF, wait10(t['wait']), i16(t['health']), i16(t['count']),
                            t['owner'], *[i16(c - 1) for c in t['mins']], *[i16(c + 1) for c in t['maxs']],
                            facing, *[i16(c) for c in dest], i16(t.get('dest_yaw', 0)), 0)
    out['triggers'] = (blob.add(bytes(recs)), len(trigs))
    # messages: a table of offsets (from the blob start), then the text
    table = bytearray(4 * len(msgs))
    base = blob.add(bytes(table))
    offs = [blob.add(t.encode('latin1') + b'\0') for t in msgs]
    blob.data[base:base + 4 * len(msgs)] = b''.join(struct.pack(E + 'I', o) for o in offs)
    out['strings'] = (base, len(msgs))
    print('movers: %d (%d keyframes), triggers: %d, names: %d, messages: %d' % (
        len(movers), nkeys, len(trigs), len(names.ids), len(msgs) - 1))
    return out, msgs


def add_game_data(blob, pak, pal, ents, models, fx, E, mkmap, anorms, movers=(), world=None,
                  extra_models=(), extra_sounds=()):
    """append entities, models and sounds; returns {lump: (offset, count)}"""
    world = world or {}
    out = {}
    names = Names()
    movers = list(movers)
    need_m, need_s = level_needs(ents, movers, world, extra_models, extra_sounds)
    eb, n = entities(ents, models, E, names, world)
    out['ents'] = (blob.add(eb), n)

    table = bytearray(4 * len(MODELS))
    base = blob.add(bytes(table))
    offs = []
    msize = 0
    for name, _ in MODELS:
        if name not in need_m:
            offs.append(0)
            continue
        b = model_blob(name, pak, pal, fx, E, mkmap, anorms)
        blob.align(4)
        offs.append(blob.add(b))
        msize += len(b)
    blob.data[base:base + 4 * len(MODELS)] = b''.join(struct.pack(E + 'I', o) for o in offs)
    out['models'] = (base, len(MODELS))

    an = bytearray()
    for a in anorms:
        an += struct.pack(E + '3h', *[int(round(v * 16384)) for v in a])
    out['anorms'] = (blob.add(bytes(an)), len(anorms))

    table = bytearray(12 * len(SOUNDS))
    base = blob.add(bytes(table))
    ent = bytearray()
    ssize = 0
    for s in SOUNDS:
        if s not in need_s:
            ent += struct.pack(E + 'IIi', 0, 0, -1)
            continue
        data, loop = load_wav(pak['sound/%s.wav' % s])
        o = blob.add(data)
        ssize += len(data)
        ent += struct.pack(E + 'IIi', o, len(data), loop)
    blob.data[base:base + len(ent)] = ent
    out['sounds'] = (base, len(SOUNDS))
    mv_out, msgs = add_movers(blob, ents, models, movers, E, names, world)
    out.update(mv_out)
    print('game data: %d entities, %d models %d KB, %d sounds %d KB' % (
        n, len(need_m), msize // 1024, len(need_s), ssize // 1024))
    return out


if __name__ == '__main__':
    if len(sys.argv) == 5 and sys.argv[1] == '--header':
        sys.path.insert(0, __file__.rsplit('/', 1)[0])
        import mkmap
        write_header(sys.argv[2])
        write_frames(sys.argv[3], mkmap.read_pak(sys.argv[4]))
    else:
        print(__doc__)
