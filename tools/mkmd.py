#!/usr/bin/env python3
"""
mkmd.py - the Mega Drive side graphics for Quake 32X (build/md_data.bin).

The 68000 draws text, the border around a reduced view and the view weapon
with the Mega Drive VDP, on top of the 32X picture. This tool makes what it
needs from pak0.pak:

  - fixed tiles: blank, black, a dither for shading, Quake's console font
    (conchars) and the border pattern (backtile), plain and shaded
  - the view weapons pre-rendered at every screen size as Mega Drive
    sprites: textured, Gouraud-lit and anti-aliased at the full 320-pixel
    width, in one 15-colour palette per weapon, with palettes for 16 light
    levels (the 68000 swaps them as the light changes)
  - for each weapon frame, the rows of the 32X view it covers completely
    (in 32X pixels, two screen pixels by two lines): the 32X skips those

usage: mkmd.py pak0.pak out.bin [preview_dir]

Everything is big-endian, read by the 68000 and the SH-2s from cartridge
ROM. Layout (offsets from the start of the file):

  header   'MDQ1', static tile ofs, static tile count, static palette ofs,
           set count, set table ofs, size count, weapon count, level count
  static   tiles 0..STATIC_TILES-1 (32 bytes each, 4 bits per pixel)
  palettes line 0 (font) and line 1 (border), 16 words each
  sets     one per (weapon, screen size), 32 bytes each:
             tiles ofs, tile count (u16), frame count (u16),
             frame table ofs, palette ofs, cutout table ofs, pad
  frame    u16 sprite count, then per sprite four words, ready for the
           sprite table: y + 128, size << 8, tile word, x + 128
  cutout   u8 first row, u8 row count, then (x0, x1) per row (x1 = 0: none)
  palette  NLEVELS x 16 words (palette line 2)
"""
import math
import os
import struct
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mkmap                       # noqa: E402
from anorms import ANORMS          # noqa: E402

# screen sizes: (view width in 32X pixels = 2 screen pixels, height in 32X
# rows = 2 lines); must match view_sizes in src/game.c
SIZES = [(160, 104), (144, 96), (128, 80), (112, 72), (96, 64), (80, 56)]
VIEW_ROWS = 104                    # 32X rows above the status bar
WEAPONS = ['progs/v_shot.mdl', 'progs/v_shot2.mdl']
NLEVELS = 16

FONT_TILE = 16
BORDER_TILE = FONT_TILE + 256
SHADE_TILE = BORDER_TILE + 64
STATIC_TILES = SHADE_TILE + 64     # 400
GUN_TILE = STATIC_TILES
GUN_TILES_MAX = 1536 - GUN_TILE    # VRAM 0x0000-0xBFFF holds tiles
GUN_PAL = 2                        # palette line for the weapon

SS = 3                             # supersampling for the weapon renders
REF_LIGHT = 128
GAMMA = 0.8                        # a little lift: 3 bits per channel lose dark detail                    # the light the weapon is rendered at


def md_level(v):
    return max(0, min(7, int(round(v * 7 / 255.0))))


def cram(rgb):
    r, g, b = (md_level(c) for c in rgb)
    return (b << 9) | (g << 5) | (r << 1)


def md_rgb(rgb):
    return tuple(md_level(c) * 255 // 7 for c in rgb)


WEIGHT = np.array([3.0, 4.0, 2.0])     # perceptual weights for r, g, b


def snap(c):
    return np.array([md_level(v) * 255 // 7 for v in c], float)


def kmeans(pts, w, k, iters=40, snapped=False):
    """weighted k-means in RGB; pts (n, 3) float, w (n,). With snapped, the
    centres are kept on the Mega Drive's 3-bit-per-channel grid, so the
    colours assigned are the ones that will be shown"""
    order = np.argsort(-w)
    cent = pts[order[:k]].copy()
    if len(cent) < k:
        cent = np.vstack([cent, np.zeros((k - len(cent), 3))])
    for it in range(iters):
        ref = np.array([snap(c) for c in cent]) if snapped and it >= iters // 2 else cent
        d = (((pts[:, None, :] - ref[None, :, :]) ** 2) * WEIGHT).sum(2)
        a = d.argmin(1)
        for j in range(k):
            m = a == j
            if m.any():
                cent[j] = (pts[m] * w[m, None]).sum(0) / w[m].sum()
    return cent


def tile_bytes(px):
    """8x8 array of colour indices 0..15 -> 32 bytes"""
    out = bytearray()
    for y in range(8):
        for x in range(0, 8, 2):
            out.append((int(px[y][x]) << 4) | int(px[y][x + 1]))
    return bytes(out)


# ------------------------------------------------------------ fixed tiles

def static_tiles(pak, pal):
    wad = mkmap.read_wad(pak['gfx.wad'])
    conchars = wad['conchars'][:128 * 128]
    backtile = wad['backtile'][8:8 + 64 * 64]
    prgb = [tuple(pal[3 * i:3 * i + 3]) for i in range(256)]

    # font: the colours of the glyphs in use, cut to 14 (entries 2..15)
    used = [c for c in range(256) if 32 <= (c & 127) < 127 or (c & 127) == 13]
    cnt = {}
    for c in used:
        for y in range(8):
            for x in range(8):
                p = conchars[((c >> 4) * 8 + y) * 128 + (c & 15) * 8 + x]
                if p:
                    cnt[p] = cnt.get(p, 0) + 1
    keys = sorted(cnt)
    pts = np.array([prgb[k] for k in keys], float)
    cent = kmeans(pts, np.array([cnt[k] for k in keys], float), 14)
    fmap = {}
    for k in keys:
        fmap[k] = 2 + int(((cent - np.array(prgb[k], float)) ** 2).sum(1).argmin())
    font_pal = [0, 0] + [cram(c) for c in cent]

    # border: backtile has a handful of colours
    bcnt = {}
    for p in backtile:
        bcnt[p] = bcnt.get(p, 0) + 1
    bkeys = sorted(bcnt)
    if len(bkeys) > 14:
        bpts = np.array([prgb[k] for k in bkeys], float)
        bcent = kmeans(bpts, np.array([bcnt[k] for k in bkeys], float), 14)
        bmap = {k: 2 + int(((bcent - np.array(prgb[k], float)) ** 2).sum(1).argmin()) for k in bkeys}
        border_pal = [0, 0] + [cram(c) for c in bcent]
    else:
        bmap = {k: 2 + i for i, k in enumerate(bkeys)}
        border_pal = [0, 0] + [cram(prgb[k]) for k in bkeys]
    border_pal += [0] * (16 - len(border_pal))

    tiles = []
    blank = [[0] * 8 for _ in range(8)]
    for t in range(FONT_TILE):
        if t == 1:
            tiles.append(tile_bytes([[1] * 8 for _ in range(8)]))
        elif t == 2:
            tiles.append(tile_bytes([[1 if (x + y) & 1 == 0 else 0 for x in range(8)] for y in range(8)]))
        else:
            tiles.append(tile_bytes(blank))
    for c in range(256):
        px = [[0] * 8 for _ in range(8)]
        if c in used:
            for y in range(8):
                for x in range(8):
                    p = conchars[((c >> 4) * 8 + y) * 128 + (c & 15) * 8 + x]
                    px[y][x] = fmap[p] if p else 0
        tiles.append(tile_bytes(px))
    for shade in (0, 1):
        for ty in range(8):
            for tx in range(8):
                px = [[bmap[backtile[(ty * 8 + y) * 64 + tx * 8 + x]] for x in range(8)] for y in range(8)]
                if shade:
                    px = [[1 if (x + y) & 1 == 0 else px[y][x] for x in range(8)] for y in range(8)]
                tiles.append(tile_bytes(px))
    assert len(tiles) == STATIC_TILES
    return b''.join(tiles), font_pal, border_pal


# ------------------------------------------------------------ the weapon

def load_mdl(d):
    ident, ver = struct.unpack('<4si', d[:8])
    assert ident == b'IDPO' and ver == 6
    scale = struct.unpack('<3f', d[8:20])
    org = struct.unpack('<3f', d[20:32])
    nskins, sw, sh, nverts, ntris, nframes, sync, flags = struct.unpack('<8i', d[48:80])
    o = 84
    skin = None
    for _ in range(nskins):
        t = struct.unpack('<i', d[o:o + 4])[0]
        o += 4
        if t == 0:
            if skin is None:
                skin = d[o:o + sw * sh]
            o += sw * sh
        else:
            n = struct.unpack('<i', d[o:o + 4])[0]
            o += 4 + 4 * n
            if skin is None:
                skin = d[o:o + sw * sh]
            o += n * sw * sh
    st = [struct.unpack('<3i', d[o + 12 * i:o + 12 * i + 12]) for i in range(nverts)]
    o += 12 * nverts
    tris = [struct.unpack('<4i', d[o + 16 * i:o + 16 * i + 16]) for i in range(ntris)]
    o += 16 * ntris
    frames = []
    for _ in range(nframes):
        t = struct.unpack('<i', d[o:o + 4])[0]
        o += 4
        if t == 0:
            o += 24
            frames.append(d[o:o + 4 * nverts])
            o += 4 * nverts
        else:
            n = struct.unpack('<i', d[o:o + 4])[0]
            o += 4 + 8 + 4 * n
            first = None
            for _ in range(n):
                o += 24
                if first is None:
                    first = d[o:o + 4 * nverts]
                o += 4 * nverts
            frames.append(first)
    return dict(scale=scale, org=org, sw=sw, sh=sh, skin=np.frombuffer(skin, np.uint8).reshape(sh, sw),
                st=st, tris=tris, frames=frames, nverts=nverts)


def render_gun(m, frame, vw, vh, pal, colormap, yaw_deg=180.0):
    """the weapon as the 32X would see it in a vw x vh view, at full
    resolution: returns (rgb float (H, W, 3), alpha float (H, W))"""
    W, H = 2 * vw * SS, 2 * vh * SS
    focal = vw * SS
    fv = m['frames'][frame]
    sc, og = m['scale'], m['org']
    n = m['nverts']
    P = np.zeros((n, 3))
    N = np.zeros((n, 3))
    for i in range(n):
        b = fv[4 * i:4 * i + 4]
        x = sc[0] * b[0] + og[0]
        y = sc[1] * b[1] + og[1]
        z = sc[2] * b[2] + og[2]
        P[i] = (-y, -z - 1.0, x)        # view right, down, forward (r_model.c)
        N[i] = ANORMS[b[3]]
    # Quake's alias lighting: ambient, plus shade on the side away from the
    # light vector (-1, 0, 0) turned into the model's frame
    a = math.radians(yaw_deg)
    lv = np.array([-math.cos(a), math.sin(a), 0.0])
    amb = min(REF_LIGHT, 128)
    shade = REF_LIGHT
    if amb + shade > 192:
        shade = 192 - amb
    lc = N @ lv
    light = (255 - amb) + np.where(lc < 0, shade * lc, 0.0)
    light = np.clip(light, 0, 255)
    sx = W / 2 + P[:, 0] * focal / P[:, 2]
    sy = H / 2 + P[:, 1] * focal / P[:, 2]

    zbuf = np.full((H, W), np.inf)
    rgb = np.zeros((H, W, 3))
    cov = np.zeros((H, W), bool)
    skin = m['skin']
    sw, sh = m['sw'], m['sh']
    prgb = np.frombuffer(pal, np.uint8).reshape(256, 3).astype(float)
    cmap = np.frombuffer(colormap, np.uint8).reshape(64, 256)
    for facesfront, i0, i1, i2 in m['tris']:
        idx = (i0, i1, i2)
        xs = np.array([sx[i] for i in idx])
        ys = np.array([sy[i] for i in idx])
        # back faces (screen winding) are not drawn
        area = (xs[1] - xs[0]) * (ys[2] - ys[0]) - (xs[2] - xs[0]) * (ys[1] - ys[0])
        if area >= 0 or min(P[i][2] for i in idx) < 1:
            continue
        x0, x1 = max(0, int(math.floor(xs.min()))), min(W - 1, int(math.ceil(xs.max())))
        y0, y1 = max(0, int(math.floor(ys.min()))), min(H - 1, int(math.ceil(ys.max())))
        if x0 > x1 or y0 > y1:
            continue
        gx, gy = np.meshgrid(np.arange(x0, x1 + 1) + 0.5, np.arange(y0, y1 + 1) + 0.5)
        w0 = ((xs[1] - gx) * (ys[2] - gy) - (xs[2] - gx) * (ys[1] - gy)) / area
        w1 = ((xs[2] - gx) * (ys[0] - gy) - (xs[0] - gx) * (ys[2] - gy)) / area
        w2 = 1 - w0 - w1
        inside = (w0 >= 0) & (w1 >= 0) & (w2 >= 0)
        if not inside.any():
            continue
        zs = np.array([P[i][2] for i in idx])
        z = w0 * zs[0] + w1 * zs[1] + w2 * zs[2]
        sub = zbuf[y0:y1 + 1, x0:x1 + 1]
        draw = inside & (z < sub)
        if not draw.any():
            continue
        stc = []
        for i in idx:
            onseam, s, t = m['st'][i]
            if onseam and not facesfront:
                s += sw // 2
            stc.append((s, t))
        s = w0 * stc[0][0] + w1 * stc[1][0] + w2 * stc[2][0]
        t = w0 * stc[0][1] + w1 * stc[1][1] + w2 * stc[2][1]
        li = w0 * light[i0] + w1 * light[i1] + w2 * light[i2]
        si = np.clip(s.astype(int), 0, sw - 1)
        ti = np.clip(t.astype(int), 0, sh - 1)
        tex = skin[ti, si]
        row = np.clip((li / 4).astype(int), 0, 63)
        col = prgb[cmap[row, tex]]
        sub[draw] = z[draw]
        rgb[y0:y1 + 1, x0:x1 + 1][draw] = col[draw]
        cov[y0:y1 + 1, x0:x1 + 1][draw] = True
    # box-filter the supersamples
    h, w = H // SS, W // SS
    c = cov.reshape(h, SS, w, SS).astype(float)
    alpha = c.mean(axis=(1, 3))
    acc = (rgb * cov[:, :, None]).reshape(h, SS, w, SS, 3).sum(axis=(1, 3))
    cnt = c.sum(axis=(1, 3))
    out = acc / np.maximum(cnt, 1)[:, :, None]
    out = 255.0 * (out / 255.0) ** GAMMA
    return out, alpha


def light_scale(j):
    """average brightness of the weapon at light j, as setup_light works it out"""
    j = max(j, 24)
    amb = min(j, 128)
    shade = j if amb + j <= 192 else 192 - amb
    return amb + 0.5 * shade


def level_light(k):
    """the light (0..255) at the middle of level k, as src/mdlink.c picks levels"""
    return 24 + 15 * k + 7


def build_sets(pak, pal, colormap, preview=None):
    sets = []
    for wi, path in enumerate(WEAPONS):
        m = load_mdl(pak[path])
        nframes = len(m['frames'])
        renders = {}
        for si, (vw, vh) in enumerate(SIZES):
            for f in range(nframes):
                renders[si, f] = render_gun(m, f, vw, vh, pal, colormap)
        # one palette for the weapon: weighted by area
        pts, wts = [], []
        for (si, f), (rgb, al) in renders.items():
            msk = al >= 0.5
            pts.append(rgb[msk])
            wts.append(np.full(msk.sum(), 1.0 / (1 + si)))
        pts = np.vstack(pts)
        wts = np.concatenate(wts)
        q = np.round(pts / 4).astype(int)
        keys, inv = np.unique(q, axis=0, return_inverse=True)
        inv = inv.reshape(-1)
        kw = np.bincount(inv, weights=wts)
        kp = np.zeros((len(keys), 3))
        for c in range(3):
            kp[:, c] = np.bincount(inv, weights=pts[:, c] * wts) / kw
        cent = kmeans(kp, kw, 15, snapped=True)
        ref = light_scale(REF_LIGHT)
        pals = []
        for k in range(NLEVELS):
            s = light_scale(level_light(k)) / ref
            pals.append([0] + [cram(tuple(min(255.0, c * s) for c in cc)) for cc in cent])
        for si, (vw, vh) in enumerate(SIZES):
            frames = []
            for f in range(nframes):
                rgb, al = renders[si, f]
                shown = np.array([snap(c) for c in cent])
                d = (((rgb[:, :, None, :] - shown[None, None, :, :]) ** 2) * WEIGHT).sum(3)
                idx = d.argmin(2) + 1
                idx[al < 0.5] = 0
                frames.append(idx)
            sets.append(dict(weapon=wi, size=si, vw=vw, vh=vh, frames=frames, pals=pals, cent=cent))
            if preview is not None:
                save_preview(preview, '%s_s%d' % (os.path.basename(path)[:-4], si), frames, pals)
    return sets


def cram_rgb(w):
    return tuple(((w >> s) & 7) * 255 // 7 for s in (1, 5, 9))


def save_preview(d, name, frames, pals):
    """frames side by side, cropped to the weapon, at four light levels"""
    from PIL import Image
    ys, xs = np.nonzero(sum(f > 0 for f in frames))
    x0, x1, y0, y1 = xs.min(), xs.max() + 1, ys.min(), ys.max() + 1
    w, h = x1 - x0, y1 - y0
    levels = (2, 6, 10, 14)
    im = Image.new('RGB', (w * len(frames), h * len(levels)), (40, 90, 40))
    for li, k in enumerate(levels):
        rgbs = [cram_rgb(c) for c in pals[k]]
        for f, idx in enumerate(frames):
            for y in range(h):
                for x in range(w):
                    v = idx[y0 + y, x0 + x]
                    if v:
                        im.putpixel((f * w + x, li * h + y), rgbs[v])
    im.resize((im.width * 3, im.height * 3), Image.NEAREST).save(os.path.join(d, name + '.png'))


def sprites_for(idx, vw, vh, tile_base):
    """split one frame into sprites; returns (sprite words, tile bytes)"""
    ys, xs = np.nonzero(idx)
    if len(xs) == 0:
        return [], b''
    ox = 160 - vw                       # view window in screen pixels / lines
    oy = VIEW_ROWS - vh
    bx0, by0 = xs.min(), ys.min()
    bx1, by1 = xs.max() + 1, ys.max() + 1
    tw = (bx1 - bx0 + 7) // 8
    th = (by1 - by0 + 7) // 8
    grid = np.zeros((th * 8, tw * 8), int)
    grid[:by1 - by0, :bx1 - bx0] = idx[by0:by1, bx0:bx1]
    words, tiles = [], bytearray()
    for byk in range(0, th, 4):
        for bxk in range(0, tw, 4):
            used = [(r, c) for r in range(byk, min(th, byk + 4)) for c in range(bxk, min(tw, bxk + 4))
                    if grid[r * 8:r * 8 + 8, c * 8:c * 8 + 8].any()]
            if not used:
                continue
            r0, r1 = min(r for r, _ in used), max(r for r, _ in used) + 1
            c0, c1 = min(c for _, c in used), max(c for _, c in used) + 1
            first = tile_base + len(tiles) // 32
            for c in range(c0, c1):            # sprite tiles go down the columns
                for r in range(r0, r1):
                    tiles += tile_bytes(grid[r * 8:r * 8 + 8, c * 8:c * 8 + 8])
            size = ((c1 - c0 - 1) << 2) | (r1 - r0 - 1)
            words += [oy + by0 + r0 * 8 + 128, size << 8, (GUN_PAL << 13) | first, ox + bx0 + c0 * 8 + 128]
    return words, bytes(tiles)


def cutout(idx, vw, vh):
    """32X rows of the view the weapon covers completely: (row0, [(x0, x1)])"""
    op = idx > 0
    rows = []
    for r in range(vh):
        c = op[2 * r] & op[2 * r + 1]
        cells = c[0::2] & c[1::2]
        best, run, start = (0, 0), 0, 0
        for x in range(vw + 1):
            if x < vw and cells[x]:
                if run == 0:
                    start = x
                run += 1
            else:
                if run > best[1] - best[0]:
                    best = (start, start + run)
                run = 0
        rows.append(best)
    first = next((r for r in range(vh) if rows[r][1]), vh)
    last = max((r for r in range(vh) if rows[r][1]), default=-1)
    if last < first:
        return 0, []
    return first, rows[first:last + 1]


class Blob:
    def __init__(self):
        self.data = bytearray()

    def add(self, b, align=2):
        while len(self.data) % align:
            self.data.append(0)
        o = len(self.data)
        self.data += b
        return o


def main():
    pak = mkmap.read_pak(sys.argv[1])
    out = sys.argv[2]
    preview = sys.argv[3] if len(sys.argv) > 3 else None
    pal = pak['gfx/palette.lmp']
    colormap = pak['gfx/colormap.lmp'][:64 * 256]
    tiles, font_pal, border_pal = static_tiles(pak, pal)
    sets = build_sets(pak, pal, colormap, preview)

    b = Blob()
    b.add(bytes(64))
    hdr = {}
    hdr['tiles'] = b.add(tiles, 4)
    hdr['pal'] = b.add(struct.pack('>32H', *(font_pal + border_pal)), 4)
    table = []
    total, worst = 0, 0
    for s in sets:
        ftab, ctab, alltiles = [], [], bytearray()
        for idx in s['frames']:
            words, t = sprites_for(idx, s['vw'], s['vh'], GUN_TILE + len(alltiles) // 32)
            alltiles += t
            ftab.append(b.add(struct.pack('>H%dH' % len(words), len(words) // 4, *words)))
            row0, rows = cutout(idx, s['vw'], s['vh'])
            ctab.append(b.add(struct.pack('>BB', row0, len(rows)) + b''.join(struct.pack('>BB', *r) for r in rows)))
        ntiles = len(alltiles) // 32
        assert ntiles <= GUN_TILES_MAX, (s['weapon'], s['size'], ntiles)
        worst = max(worst, ntiles)
        total += len(alltiles)
        to = b.add(bytes(alltiles), 4)
        fo = b.add(struct.pack('>%dI' % len(ftab), *ftab), 4)
        co = b.add(struct.pack('>%dI' % len(ctab), *ctab), 4)
        po = b.add(struct.pack('>%dH' % (16 * NLEVELS), *[w for p in s['pals'] for w in p]), 4)
        table.append(struct.pack('>IHHIIIIII', to, ntiles, len(s['frames']), fo, po, co, 0, 0, 0))
    hdr['sets'] = b.add(b''.join(table), 4)
    b.data[0:36] = struct.pack('>4sIIIIIIII', b'MDQ1', hdr['tiles'], STATIC_TILES, hdr['pal'],
                               len(sets), hdr['sets'], len(SIZES), len(WEAPONS), NLEVELS)
    open(out, 'wb').write(bytes(b.data))
    print('md data: %d bytes, %d weapon sets, weapon tiles %d KB (largest set %d tiles)' % (
        len(b.data), len(sets), total // 1024, worst))


if __name__ == '__main__':
    main()
