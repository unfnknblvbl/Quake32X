#!/usr/bin/env python3
"""
mkmap.py - convert a Quake BSP29 map from pak0.pak into a compact,
big-endian, fixed-point level blob for the Sega 32X renderer.

usage: mkmap.py pak0.pak maps/e1m1.bsp out.lvl

All multi-byte values are big-endian (the SH-2 is big-endian).
Layout is described by the header (see LVL_* in src/level.h).
"""
import math
import struct
import sys

# ----------------------------------------------------------------- pak / wad

def read_pak(path):
    d = open(path, 'rb').read()
    if d[:4] != b'PACK':
        raise SystemExit('not a pak file')
    off, ln = struct.unpack('<ii', d[4:12])
    files = {}
    for i in range(ln // 64):
        e = d[off + i * 64: off + i * 64 + 64]
        name = e[:56].split(b'\0')[0].decode('latin1').lower()
        fp, fl = struct.unpack('<ii', e[56:])
        files[name] = d[fp:fp + fl]
    return files


def read_wad(d):
    assert d[:4] == b'WAD2'
    n, ofs = struct.unpack('<ii', d[4:12])
    lumps = {}
    for i in range(n):
        e = d[ofs + i * 32: ofs + i * 32 + 32]
        fp, dsz, sz, typ, comp, pad = struct.unpack('<iiibbh', e[:16])
        name = e[16:32].split(b'\0')[0].decode('latin1').lower()
        lumps[name] = d[fp:fp + dsz]
    return lumps

# ----------------------------------------------------------------- bsp parse

LUMPS = ['entities', 'planes', 'textures', 'vertexes', 'visibility', 'nodes',
         'texinfo', 'faces', 'lighting', 'clipnodes', 'leafs', 'marksurfaces',
         'edges', 'surfedges', 'models']

CONTENTS_EMPTY = -1
CONTENTS_SOLID = -2
CONTENTS_SKY = -6


def parse_bsp(b):
    ver = struct.unpack('<i', b[:4])[0]
    assert ver == 29, 'BSP version %d' % ver
    L = {}
    for i, n in enumerate(LUMPS):
        o, l = struct.unpack('<ii', b[4 + i * 8: 12 + i * 8])
        L[n] = b[o:o + l]
    m = {}
    P = L['planes']
    m['planes'] = [struct.unpack('<4fi', P[i:i + 20]) for i in range(0, len(P), 20)]
    V = L['vertexes']
    m['verts'] = [struct.unpack('<3f', V[i:i + 12]) for i in range(0, len(V), 12)]
    N = L['nodes']
    m['nodes'] = [struct.unpack('<ihh3h3hHH', N[i:i + 24]) for i in range(0, len(N), 24)]
    T = L['texinfo']
    m['texinfo'] = [struct.unpack('<8fii', T[i:i + 40]) for i in range(0, len(T), 40)]
    F = L['faces']
    m['faces'] = [struct.unpack('<hhihh4Bi', F[i:i + 20]) for i in range(0, len(F), 20)]
    m['lighting'] = L['lighting']
    C = L['clipnodes']
    m['clipnodes'] = [struct.unpack('<ihh', C[i:i + 8]) for i in range(0, len(C), 8)]
    LF = L['leafs']
    m['leafs'] = [struct.unpack('<ii3h3hHH4B', LF[i:i + 28]) for i in range(0, len(LF), 28)]
    MS = L['marksurfaces']
    m['marksurfaces'] = list(struct.unpack('<%dH' % (len(MS) // 2), MS))
    E = L['edges']
    m['edges'] = list(struct.unpack('<%dH' % (len(E) // 2), E))
    SE = L['surfedges']
    m['surfedges'] = list(struct.unpack('<%di' % (len(SE) // 4), SE))
    MO = L['models']
    m['models'] = [struct.unpack('<9f4i3i', MO[i:i + 64]) for i in range(0, len(MO), 64)]
    m['vis'] = L['visibility']
    m['entities'] = L['entities'].split(b'\0')[0].decode('latin1')
    # textures
    TX = L['textures']
    n = struct.unpack('<i', TX[:4])[0]
    offs = struct.unpack('<%di' % n, TX[4:4 + 4 * n])
    texs = []
    for o in offs:
        if o < 0:
            texs.append(None)
            continue
        name = TX[o:o + 16].split(b'\0')[0].decode('latin1').lower()
        w, h = struct.unpack('<II', TX[o + 16:o + 24])
        mo = struct.unpack('<4I', TX[o + 24:o + 40])
        mips = []
        for k in range(4):
            mips.append(TX[o + mo[k]: o + mo[k] + (w >> k) * (h >> k)])
        texs.append({'name': name, 'w': w, 'h': h, 'mips': mips})
    m['textures'] = texs
    return m


def parse_entities(s):
    ents = []
    cur = None
    i = 0
    toks = []
    # simple tokenizer
    while i < len(s):
        c = s[i]
        if c in ' \t\r\n':
            i += 1
        elif c in '{}':
            toks.append(c)
            i += 1
        elif c == '"':
            j = s.index('"', i + 1)
            toks.append(s[i + 1:j])
            i = j + 1
        else:
            j = i
            while j < len(s) and s[j] not in ' \t\r\n{}"':
                j += 1
            toks.append(s[i:j])
            i = j
    k = 0
    while k < len(toks):
        t = toks[k]
        if t == '{':
            cur = {}
            k += 1
        elif t == '}':
            ents.append(cur)
            k += 1
        else:
            cur[t] = toks[k + 1]
            k += 2
    return ents

# ----------------------------------------------------------------- helpers


def face_polygon(m, f):
    planenum, side, firstedge, numedges = f[0], f[1], f[2], f[3]
    pts = []
    for k in range(numedges):
        e = m['surfedges'][firstedge + k]
        if e >= 0:
            vi = m['edges'][2 * e]
        else:
            vi = m['edges'][2 * (-e) + 1]
        pts.append(m['verts'][vi])
    return pts


def dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def split_poly(pts, n, d, eps=0.01):
    """split polygon by plane n.x = d -> (front, back)"""
    dists = [dot(p, n) - d for p in pts]
    sides = [1 if x > eps else (-1 if x < -eps else 0) for x in dists]
    if all(s >= 0 for s in sides):
        if all(s == 0 for s in sides):
            return pts, None  # coplanar - keep on front
        return pts, None
    if all(s <= 0 for s in sides):
        return None, pts
    front, back = [], []
    cnt = len(pts)
    for i in range(cnt):
        p, s, dp = pts[i], sides[i], dists[i]
        if s >= 0:
            front.append(p)
        if s <= 0:
            back.append(p)
        q, sq, dq = pts[(i + 1) % cnt], sides[(i + 1) % cnt], dists[(i + 1) % cnt]
        if s == 0 or sq == 0 or s == sq:
            continue
        t = dp / (dp - dq)
        mid = tuple(p[j] + t * (q[j] - p[j]) for j in range(3))
        front.append(mid)
        back.append(mid)
    return (front if len(front) >= 3 else None), (back if len(back) >= 3 else None)


def palette_nearest(pal, rgb, limit=224):
    best, bi = 1e18, 0
    for i in range(limit):
        r, g, b = pal[3 * i], pal[3 * i + 1], pal[3 * i + 2]
        dd = (r - rgb[0]) ** 2 + (g - rgb[1]) ** 2 + (b - rgb[2]) ** 2
        if dd < best:
            best, bi = dd, i
    return bi

# ----------------------------------------------------------------- writer


class Blob:
    def __init__(self):
        self.data = bytearray()

    def align(self, a=4):
        while len(self.data) % a:
            self.data.append(0)

    def tell(self):
        return len(self.data)

    def add(self, b, align=4):
        self.align(align)
        o = len(self.data)
        self.data += b
        return o


def fx(v):  # float -> 16.16
    return int(round(v * 65536.0))


def v13_3(v):
    r = int(round(v * 8.0))
    assert -32768 <= r <= 32767, v
    return r


E = '>'


def main():
    global E
    if '--le' in sys.argv:
        E = '<'
        sys.argv.remove('--le')
    if len(sys.argv) < 4:
        print(__doc__)
        sys.exit(1)
    pak = read_pak(sys.argv[1])
    m = parse_bsp(pak[sys.argv[2].lower()])
    pal = pak['gfx/palette.lmp']
    colormap = pak['gfx/colormap.lmp'][:64 * 256]
    wad = read_wad(pak['gfx.wad'])
    conchars = wad['conchars']
    assert len(conchars) >= 128 * 128
    ents = parse_entities(m['entities'])

    planes = m['planes']
    verts = list(m['verts'])
    texinfo = m['texinfo']
    textures = m['textures']
    bspfaces = m['faces']
    nodes = m['nodes']
    leafs = m['leafs']
    models = m['models']

    # ------------------------------------------------ texture info / flags
    tex_avg = []
    for t in textures:
        if t is None:
            tex_avg.append(0)
            continue
        mip = t['mips'][3]
        rs = gs = bs = 0
        for c in mip:
            rs += pal[3 * c]
            gs += pal[3 * c + 1]
            bs += pal[3 * c + 2]
        n = max(1, len(mip))
        tex_avg.append(palette_nearest(pal, (rs / n, gs / n, bs / n)))

    def tex_flags(ti):
        t = textures[texinfo[ti][8]]
        name = t['name'] if t else ''
        fl = 0
        if name.startswith('sky'):
            fl |= 2
        if name.startswith('*'):
            fl |= 4
        return fl

    # ------------------------------------------------ face records
    # Each face (world faces + bmodel fragments) gets a polygon (vertex index
    # list), texture extents, lightmap and flat colours.
    vert_index = {}
    out_verts = []

    def vindex(p):
        key = tuple(v13_3(c) for c in p)
        if key not in vert_index:
            vert_index[key] = len(out_verts)
            out_verts.append(key)
        return vert_index[key]

    lightdata = bytearray()
    face_light_cache = {}

    def face_extents(pts, ti):
        tv = texinfo[ti]
        mins = [1e18, 1e18]
        maxs = [-1e18, -1e18]
        for p in pts:
            for j in range(2):
                val = p[0] * tv[j * 4] + p[1] * tv[j * 4 + 1] + p[2] * tv[j * 4 + 2] + tv[j * 4 + 3]
                mins[j] = min(mins[j], val)
                maxs[j] = max(maxs[j], val)
        bmin = [math.floor(mins[j] / 16) for j in range(2)]
        bmax = [math.ceil(maxs[j] / 16) for j in range(2)]
        return [bmin[j] * 16 for j in range(2)], [(bmax[j] - bmin[j]) * 16 for j in range(2)]

    def build_light(fi, orig_pts):
        """returns (offset into lightdata or -1, avg L)"""
        if fi in face_light_cache:
            return face_light_cache[fi]
        f = bspfaces[fi]
        styles = f[5:9]
        lofs = f[9]
        ti = f[4]
        tmins, ext = face_extents(orig_pts, ti)
        smax = (ext[0] >> 4) + 1
        tmax = (ext[1] >> 4) + 1
        if lofs < 0 or styles[0] == 255 or (tex_flags(ti) & 6):
            res = (-1, 255)
            face_light_cache[fi] = res
            return res
        nmaps = sum(1 for s in styles if s != 255)
        size = smax * tmax
        out = bytearray(size)
        tot = 0
        for i in range(size):
            bl = 0
            for k in range(nmaps):
                bl += m['lighting'][lofs + k * size + i] * 264
            t = (255 * 256 - bl) >> 2
            if t < 64:
                t = 64
            L = t >> 6
            if L > 255:
                L = 255
            out[i] = L
            tot += L
        o = len(lightdata)
        lightdata.extend(out)
        while len(lightdata) % 2:
            lightdata.append(0)
        res = (o, tot // size)
        face_light_cache[fi] = res
        return res

    face_recs = []   # dict per output face
    face_vlists = []

    def add_face(fi, pts, orig_pts, side_flag, allow_empty=False):
        f = bspfaces[fi]
        ti = f[4]
        flags = tex_flags(ti) | side_flag
        tmins, ext = face_extents(orig_pts, ti)
        lofs, avgL = build_light(fi, orig_pts)
        if lofs < 0 and not (flags & 6):
            flags |= 8  # no lightmap -> dark
        tidx = texinfo[ti][8]
        avgc = tex_avg[tidx]
        row = (avgL << 6) >> 8 if not (flags & 6) else 32
        if flags & 8:
            row = 63
        lit = colormap[row * 256 + avgc]
        unlit = colormap[32 * 256 + avgc]
        if flags & 2:   # sky: use the darker back layer average
            lit = unlit
        # remove collinear duplicates in quantized space
        vl = []
        for p in pts:
            vi = vindex(p)
            if not vl or vl[-1] != vi:
                vl.append(vi)
        if len(vl) > 1 and vl[0] == vl[-1]:
            vl.pop()
        if len(vl) < 3:
            if not allow_empty:
                return -1
            vl = []
        face_recs.append(dict(plane=f[0], flags=flags, texinfo=ti,
                              tmins=tmins, ext=ext, lofs=lofs, lit=lit, unlit=unlit,
                              vl=vl))
        return len(face_recs) - 1

    # world faces: keep BSP face numbering for model 0 faces
    nworld = models[0][15]  # numfaces
    first0 = models[0][14]
    assert first0 == 0
    face_map = {}
    for fi in range(nworld):
        f = bspfaces[fi]
        pts = face_polygon(m, f)
        side = 1 if f[1] else 0
        idx = add_face(fi, pts, pts, side, allow_empty=True)
        assert idx == fi
        face_map[fi] = idx

    # ------------------------------------------------ bmodels -> leaf fragments
    VISIBLE_BMODELS = ('func_door', 'func_door_secret', 'func_plat', 'func_button',
                       'func_wall', 'func_illusionary', 'func_episodegate',
                       'func_bossgate', 'func_train')
    leaf_frags = {}
    nfrag = 0
    for e in ents:
        mdl = e.get('model', '')
        cls = e.get('classname', '')
        if not mdl.startswith('*') or cls not in VISIBLE_BMODELS:
            continue
        mi = int(mdl[1:])
        mo = models[mi]
        mins = mo[0:3]
        maxs = mo[3:6]
        org = [0.0, 0.0, 0.0]
        if cls == 'func_plat':
            h = float(e['height']) if 'height' in e else (maxs[2] - mins[2] - 8)
            if 'targetname' not in e:
                org[2] -= h
        if 'origin' in e:
            o = [float(x) for x in e['origin'].split()]
            org = [org[i] + o[i] for i in range(3)]
        firstface, numfaces = mo[14], mo[15]
        for fi in range(firstface, firstface + numfaces):
            f = bspfaces[fi]
            if textures[texinfo[f[4]][8]]['name'] in ('trigger', 'clip'):
                continue
            pts0 = face_polygon(m, f)
            pts = [tuple(p[j] + org[j] for j in range(3)) for p in pts0]
            side = 1 if f[1] else 0
            # push down the world tree
            stack = [(models[0][9], pts)]
            while stack:
                node, poly = stack.pop()
                if node < 0:
                    leaf = -1 - node
                    if leafs[leaf][0] == CONTENTS_SOLID:
                        continue
                    idx = add_face(fi, poly, pts0, side)
                    if idx >= 0:
                        # bmodel offsets do not move texture space: adjust by
                        # shifting the polygon back for extents was done using
                        # pts0; texture coords use world pos -> fold org into
                        # texinfo is not possible, so store org per fragment.
                        face_recs[idx]['org'] = org
                        leaf_frags.setdefault(leaf, []).append(idx)
                        nfrag += 1
                    continue
                n = nodes[node]
                pl = planes[n[0]]
                fr, bk = split_poly(poly, pl[0:3], pl[3])
                if fr:
                    stack.append((n[1], fr))
                if bk:
                    stack.append((n[2], bk))
    print('bmodel fragments:', nfrag)

    # texinfo for fragments: texture coords are relative to the model's own
    # coordinates; for moved models (plats) we create adjusted texinfos.
    out_texinfo = [list(t) for t in texinfo]
    ti_cache = {}
    for rec in face_recs:
        org = rec.get('org')
        if not org or org == [0.0, 0.0, 0.0]:
            continue
        key = (rec['texinfo'], tuple(org))
        if key not in ti_cache:
            t = list(texinfo[rec['texinfo']])
            for j in range(2):
                t[j * 4 + 3] -= t[j * 4] * org[0] + t[j * 4 + 1] * org[1] + t[j * 4 + 2] * org[2]
            ti_cache[key] = len(out_texinfo)
            out_texinfo.append(t)
        rec['texinfo'] = ti_cache[key]
        # extents / tmins were computed from pts0 (model space) - consistent
        # with the adjusted texinfo evaluated in world space.
        # the plane moves with the model as well
        pkey = ('plane', rec['plane'], tuple(org))
        if pkey not in ti_cache:
            p = planes[rec['plane']]
            planes.append((p[0], p[1], p[2], p[3] + dot(p[0:3], org), p[4]))
            ti_cache[pkey] = len(planes) - 1
        rec['plane'] = ti_cache[pkey]

    # ------------------------------------------------ node / leaf parents
    nparent = [-1] * len(nodes)
    lparent = [-1] * len(leafs)
    for i, n in enumerate(nodes):
        for c in (n[1], n[2]):
            if c >= 0:
                nparent[c] = i
            else:
                lparent[-1 - c] = i

    # node faces -> output face indices (world faces map 1:1)
    # ------------------------------------------------ build blob
    blob = Blob()
    hdr_fields = ['planes', 'nodes', 'leafs', 'marksurf', 'faces', 'fverts', 'verts',
                  'texinfo', 'textures', 'light', 'vis', 'leaffrags', 'clipnodes',
                  'palette', 'colormap', 'conchars', 'skytex', 'facesph', 'nodeparent']
    HDR_SIZE = 4 + 4 + len(hdr_fields) * 8 + 64
    blob.data += b'\0' * HDR_SIZE
    offs = {}
    counts = {}

    # planes: nx ny nz dist (16.16)
    b = bytearray()
    for p in planes:
        b += struct.pack(E + '4i', fx(p[0]), fx(p[1]), fx(p[2]), fx(p[3]))
    offs['planes'] = blob.add(b)
    counts['planes'] = len(planes)

    # nodes (32 bytes, 16-byte aligned so a node is exactly two cache lines):
    #   child0, child1, firstface, numfaces, mins[3], maxs[3],
    #   plane type (0-2 axial), pad, normal[3] (2.14), dist (16.16)
    b = bytearray()
    for i, n in enumerate(nodes):
        pl = planes[n[0]]
        b += struct.pack(E + 'hhHH3h3hBB3hi', n[1], n[2], n[9], n[10],
                         n[3], n[4], n[5], n[6], n[7], n[8], pl[4] if pl[4] < 3 else 3, 0,
                         *[int(round(pl[k] * 16384)) for k in range(3)], fx(pl[3]))
    offs['nodes'] = blob.add(b, 16)
    counts['nodes'] = len(nodes)
    b = bytearray()
    for i in range(len(nodes)):
        b += struct.pack(E + 'h', nparent[i])
    offs['nodeparent'] = blob.add(b)
    counts['nodeparent'] = len(nodes)

    # leafs: contents(h), parent(h), visofs(i), firstmark(H), nummark(H),
    #        firstfrag(H), numfrag(H), mins[3], maxs[3] -> 28 bytes
    fraglist = []
    b = bytearray()
    for i, lf in enumerate(leafs):
        frs = leaf_frags.get(i, [])
        first = len(fraglist)
        fraglist += frs
        b += struct.pack(E + 'hhiHHHH3h3h', lf[0], lparent[i], lf[1], lf[8], lf[9],
                         first, len(frs), lf[2], lf[3], lf[4], lf[5], lf[6], lf[7])
    offs['leafs'] = blob.add(b)
    counts['leafs'] = len(leafs)

    b = bytearray()
    for ms in m['marksurfaces']:
        b += struct.pack(E + 'H', face_map[ms] if ms in face_map else 0xFFFF)
    offs['marksurf'] = blob.add(b)
    counts['marksurf'] = len(m['marksurfaces'])

    b = bytearray()
    for fi in fraglist:
        b += struct.pack(E + 'H', fi)
    offs['leaffrags'] = blob.add(b)
    counts['leaffrags'] = len(fraglist)

    # faces (24 bytes):
    #  firstvert(H) numverts(B) flags(B) plane(H) texinfo(H)
    #  tmins[2](h) ext[2](H) lightofs(i) lit(B) unlit(B) pad(H)
    fverts = []
    b = bytearray()
    for rec in face_recs:
        first = len(fverts)
        fverts += rec['vl']
        assert len(rec['vl']) < 256
        assert -32768 <= rec['tmins'][0] < 32768 and -32768 <= rec['tmins'][1] < 32768
        b += struct.pack(E + 'HBBHHhhHHiBBH', first, len(rec['vl']), rec['flags'], rec['plane'],
                         rec['texinfo'], rec['tmins'][0], rec['tmins'][1],
                         rec['ext'][0], rec['ext'][1], rec['lofs'], rec['lit'], rec['unlit'], 0)
    offs['faces'] = blob.add(b)
    counts['faces'] = len(face_recs)
    print('faces:', len(face_recs), 'world:', nworld)

    # bounding sphere per face (world units): x, y, z, radius
    b = bytearray()
    for rec in face_recs:
        if not rec['vl']:
            b += struct.pack(E + '4h', 0, 0, 0, -1)
            continue
        pts = [out_verts[v] for v in rec['vl']]
        c = [(min(p[k] for p in pts) + max(p[k] for p in pts)) / 2.0 / 8.0 for k in range(3)]
        ci = [int(round(x)) for x in c]
        r = max(math.sqrt(sum((p[k] / 8.0 - ci[k]) ** 2 for k in range(3))) for p in pts)
        b += struct.pack(E + '4h', ci[0], ci[1], ci[2], int(math.ceil(r)) + 1)
    offs['facesph'] = blob.add(b)
    counts['facesph'] = len(face_recs)

    b = bytearray()
    for v in fverts:
        b += struct.pack(E + 'H', v)
    offs['fverts'] = blob.add(b)
    counts['fverts'] = len(fverts)

    # verts: x,y,z 13.3 + pad -> 8 bytes
    b = bytearray()
    for v in out_verts:
        b += struct.pack(E + '4h', v[0], v[1], v[2], 0)
    offs['verts'] = blob.add(b)
    counts['verts'] = len(out_verts)
    assert len(out_verts) < 65536

    # texinfo: s[4], t[4] (16.16), texture(H), flags(H) -> 36 bytes
    b = bytearray()
    for t in out_texinfo:
        b += struct.pack(E + '8iHH', *[fx(x) for x in t[0:8]], t[8], t[9] & 0xFFFF)
    offs['texinfo'] = blob.add(b)
    counts['texinfo'] = len(out_texinfo)

    # textures: table of (w(H), h(H), avg(B), flags(B), pad(H), mipofs[4](i)) = 24 bytes
    texdata = bytearray()
    table = bytearray()
    for i, t in enumerate(textures):
        if t is None:
            table += struct.pack(E + 'HHBBH4i', 0, 0, 0, 0, 0, 0, 0, 0, 0)
            continue
        mo = []
        for k in range(4):
            while len(texdata) % 4:
                texdata.append(0)
            mo.append(len(texdata))
            texdata += t['mips'][k]
        table += struct.pack(E + 'HHBBH4i', t['w'], t['h'], tex_avg[i], 0, 0, *mo)
    offs['textures'] = blob.add(table)
    counts['textures'] = len(textures)
    texbase = blob.add(bytes(texdata))
    # patch mip offsets to be absolute within blob
    for i in range(len(textures)):
        base = offs['textures'] + i * 24 + 8
        mo = struct.unpack(E + '4i', blob.data[base:base + 16])
        if textures[i] is not None:
            blob.data[base:base + 16] = struct.pack(E + '4i', *[x + texbase for x in mo])

    offs['light'] = blob.add(bytes(lightdata))
    counts['light'] = len(lightdata)
    offs['vis'] = blob.add(m['vis'])
    counts['vis'] = len(m['vis'])

    b = bytearray()
    for c in m['clipnodes']:
        b += struct.pack(E + 'hhhh', c[0], c[1], c[2], 0)
    offs['clipnodes'] = blob.add(b)
    counts['clipnodes'] = len(m['clipnodes'])

    # palette as 32X CRAM words (BGR555)
    b = bytearray()
    for i in range(256):
        r, g, bb = pal[3 * i], pal[3 * i + 1], pal[3 * i + 2]
        b += struct.pack(E + 'H', ((bb >> 3) << 10) | ((g >> 3) << 5) | (r >> 3))
    offs['palette'] = blob.add(b)
    counts['palette'] = 256
    offs['colormap'] = blob.add(colormap)
    counts['colormap'] = len(colormap)
    offs['conchars'] = blob.add(conchars[:128 * 128])
    counts['conchars'] = 128 * 128

    # sky: use the back (right) half of the sky texture, 128x128, as a single layer
    sky = None
    for t in textures:
        if t and t['name'].startswith('sky'):
            sky = t
    b = bytearray(128 * 128)
    if sky:
        for y in range(128):
            row = sky['mips'][0][y * 256:(y + 1) * 256]
            for x in range(128):
                c = row[x]           # front layer (clouds, has 0 = transparent)
                if c == 0:
                    c = row[128 + x]  # back layer
                b[y * 128 + x] = c
    offs['skytex'] = blob.add(b)
    counts['skytex'] = 128 * 128

    # ------------------------------------------------ spawn point
    spawn = (0.0, 0.0, 0.0)
    yaw = 0.0
    for e in ents:
        if e.get('classname') == 'info_player_start':
            spawn = tuple(float(x) for x in e['origin'].split())
            yaw = float(e.get('angle', '0'))
    # header
    h = struct.pack(E + '4si', b'Q32L', len(hdr_fields))
    for k in hdr_fields:
        h += struct.pack(E + 'ii', offs[k], counts[k])
    h += struct.pack(E + '3ii', fx(spawn[0]), fx(spawn[1]), fx(spawn[2]), int(yaw))
    h += struct.pack(E + 'hhh', models[0][9], models[0][10], len(nodes))
    h += struct.pack(E + 'hh', nworld, models[0][13])
    h = h.ljust(HDR_SIZE, b'\0')
    blob.data[0:HDR_SIZE] = h
    blob.align(16)
    open(sys.argv[3], 'wb').write(blob.data)
    print('wrote', sys.argv[3], len(blob.data), 'bytes')
    for k in hdr_fields:
        print('  %-10s ofs %8d count %6d' % (k, offs[k], counts[k]))
    print('spawn', spawn, 'yaw', yaw)


if __name__ == '__main__':
    main()
