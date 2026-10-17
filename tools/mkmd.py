#!/usr/bin/env python3
"""
mkmd.py - the Mega Drive side graphics for Quake 32X (build/md_data.bin).

The 68000 draws text, the border around a reduced view and the view weapon
with the Mega Drive VDP, on top of the 32X picture. This tool makes what it
needs from pak0.pak:

  - fixed tiles: blank, black, a dither for shading, Quake's console font
    (conchars) and the border pattern (backtile), plain and shaded
  - the view weapons pre-rendered at every screen size as Mega Drive
    sprites, the way Quake's alias renderer draws them (near-plane clipped,
    back faces culled, textured and Gouraud-lit at one fixed light, 1.3x
    Quake's brightest), anti-aliased at the full 320-pixel width and
    ordered-dithered (a light 4x4 Bayer dither) to the Mega Drive's 3 bits
    per channel; each set (a weapon at a screen size) has three 15-colour
    palettes, and each sprite (a block of up to 4x4 tiles) uses whichever
    suits it
  - for each weapon frame, the rows of the 32X view it covers completely
    (in 32X pixels, two screen pixels by two lines): the 32X skips those

usage: mkmd.py pak0.pak out.bin [preview_dir]

Everything is big-endian, read by the 68000 and the SH-2s from cartridge
ROM. Layout (offsets from the start of the file):

  header   'MDQ1', static tile ofs, static tile count, static palette ofs,
           set count, set table ofs, size count, weapon count
  static   tiles 0..STATIC_TILES-1 (32 bytes each, 4 bits per pixel)
  palettes line 0 (the font and the border share it) and line 1 (unused:
           the weapon's), 16 words each
  sets     one per (weapon, screen size), 32 bytes each:
             tiles ofs, tile count (u16), frame count (u16),
             frame table ofs, palette ofs, cutout table ofs, pad
  frame    u16 sprite count, then per sprite four words, ready for the
           sprite table: y + 128, size << 8, tile word, x + 128
  cutout   u8 first row, u8 row count, then (x0, x1) per row (x1 = 0: none)
  palette  48 words: palette lines 1, 2 and 3
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
# the view weapons, in the order of MDW_ in src/mdlink.h
WEAPONS = ['progs/v_axe.mdl', 'progs/v_shot.mdl', 'progs/v_shot2.mdl', 'progs/v_nail.mdl',
           'progs/v_rock.mdl']
# frames kept (the axe's second swing, frames 5-8, would not fit in VRAM
# with the first: the game uses the first swing for both)
WEAPON_FRAMES = {'progs/v_axe.mdl': 5}

FONT_TILE = 16
BORDER_TILE = FONT_TILE + 256
SHADE_TILE = BORDER_TILE + 64
STATIC_TILES = SHADE_TILE + 64     # 400
GUN_TILE = STATIC_TILES
GUN_TILES_MAX = 1536 - GUN_TILE    # VRAM 0x0000-0xBFFF holds tiles
GUN_PAL = 1                        # palette lines for the weapon: 1, 2 and 3
GUN_NPAL = 3

SS = 3                             # supersampling for the weapon renders
REF_LIGHT = 128                    # the light the weapon is rendered at (Quake's brightest)
LIGHT_SCALE = 1.3                  # and a little brighter: 3 bits per channel lose dark detail
DITHER = 0.6                       # ordered dither strength (1 = full)
BAYER4 = np.array([[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]])
LUMA = np.array([0.299, 0.587, 0.114])


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
    # the font and the border share palette line 0 (the weapon has the
    # other three): their colours on the 3-bit grid are few
    merged = [0, 0]
    remap_f, remap_b = {}, {}
    for src, remap, used_i in ((font_pal, remap_f, fmap.values()), (border_pal, remap_b, bmap.values())):
        for i in sorted(set(used_i)):
            w = src[i]
            if w not in merged[2:]:
                merged.append(w)
            remap[i] = 2 + merged[2:].index(w)
    assert len(merged) <= 16, 'font and border need %d colours' % (len(merged) - 2)
    fmap = {k: remap_f[v] for k, v in fmap.items()}
    bmap = {k: remap_b[v] for k, v in bmap.items()}
    font_pal = merged + [0] * (16 - len(merged))

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


ALIAS_Z_CLIP = 5.0                 # Quake clips alias models 5 units in front of the eye
# V_CalcRefdef's fudge: the weapon is raised 2 units at viewsize 100, 1 at
# 90, 0.5 at 80 and not at all below
GUN_RAISE = {0: 2.0, 1: 1.0, 2: 0.5}


def clip_near(poly):
    """a polygon of (x, y, z, s, t, light) clipped to z >= ALIAS_Z_CLIP"""
    out = []
    for k in range(len(poly)):
        a, b = poly[k], poly[(k + 1) % len(poly)]
        ain, bin_ = a[2] >= ALIAS_Z_CLIP, b[2] >= ALIAS_Z_CLIP
        if ain:
            out.append(a)
        if ain != bin_:
            f = (ALIAS_Z_CLIP - a[2]) / (b[2] - a[2])
            out.append(tuple(a[i] + f * (b[i] - a[i]) for i in range(6)))
    return out


def render_gun(m, frame, vw, vh, pal, colormap, yaw_deg=180.0, size=0):
    """the weapon as the 32X would see it in a vw x vh view, at full
    resolution: returns (rgb float (H, W, 3), alpha float (H, W)). Like
    Quake's alias renderer: triangles clipped at the near plane, back faces
    culled, texture and light interpolated across the screen"""
    W, H = 2 * vw * SS, 2 * vh * SS
    focal = vw * SS
    fv = m['frames'][frame]
    sc, og = m['scale'], m['org']
    n = m['nverts']
    raise_ = GUN_RAISE.get(size, 0.0)
    P = np.zeros((n, 3))
    N = np.zeros((n, 3))
    for i in range(n):
        b = fv[4 * i:4 * i + 4]
        x = sc[0] * b[0] + og[0]
        y = sc[1] * b[1] + og[1]
        z = sc[2] * b[2] + og[2]
        P[i] = (-y, -(z + raise_), x)   # view right, down, forward
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

    zbuf = np.full((H, W), np.inf)
    rgb = np.zeros((H, W, 3))
    cov = np.zeros((H, W), bool)
    skin = m['skin']
    sw, sh = m['sw'], m['sh']
    prgb = np.frombuffer(pal, np.uint8).reshape(256, 3).astype(float)
    cmap = np.frombuffer(colormap, np.uint8).reshape(64, 256)

    def raster(xs, ys, zs, ss, ts, ls, area):
        x0, x1 = max(0, int(math.floor(xs.min()))), min(W - 1, int(math.ceil(xs.max())))
        y0, y1 = max(0, int(math.floor(ys.min()))), min(H - 1, int(math.ceil(ys.max())))
        if x0 > x1 or y0 > y1:
            return
        gx, gy = np.meshgrid(np.arange(x0, x1 + 1) + 0.5, np.arange(y0, y1 + 1) + 0.5)
        w0 = ((xs[1] - gx) * (ys[2] - gy) - (xs[2] - gx) * (ys[1] - gy)) / area
        w1 = ((xs[2] - gx) * (ys[0] - gy) - (xs[0] - gx) * (ys[2] - gy)) / area
        w2 = 1 - w0 - w1
        inside = (w0 >= 0) & (w1 >= 0) & (w2 >= 0)
        if not inside.any():
            return
        z = w0 * zs[0] + w1 * zs[1] + w2 * zs[2]
        sub = zbuf[y0:y1 + 1, x0:x1 + 1]
        draw = inside & (z < sub)
        if not draw.any():
            return
        s = w0 * ss[0] + w1 * ss[1] + w2 * ss[2]
        t = w0 * ts[0] + w1 * ts[1] + w2 * ts[2]
        li = w0 * ls[0] + w1 * ls[1] + w2 * ls[2]
        si = np.clip(s.astype(int), 0, sw - 1)
        ti = np.clip(t.astype(int), 0, sh - 1)
        tex = skin[ti, si]
        row = np.clip((li / 4).astype(int), 0, 63)
        col = prgb[cmap[row, tex]]
        sub[draw] = z[draw]
        rgb[y0:y1 + 1, x0:x1 + 1][draw] = col[draw]
        cov[y0:y1 + 1, x0:x1 + 1][draw] = True

    for facesfront, i0, i1, i2 in m['tris']:
        poly = []
        for i in (i0, i1, i2):
            onseam, s, t = m['st'][i]
            if onseam and not facesfront:
                s += sw // 2
            poly.append((P[i][0], P[i][1], P[i][2], s, t, light[i]))
        poly = clip_near(poly)
        if len(poly) < 3:
            continue
        v = np.array(poly)
        xs = W / 2 + v[:, 0] * focal / v[:, 2]
        ys = H / 2 + v[:, 1] * focal / v[:, 2]
        for k in range(1, len(poly) - 1):
            ix = [0, k, k + 1]
            tx, ty = xs[ix], ys[ix]
            # back faces are not drawn: Quake's test (d_polyse.c), as in r_model.c
            area = (tx[1] - tx[0]) * (ty[2] - ty[0]) - (tx[2] - tx[0]) * (ty[1] - ty[0])
            if area <= 0:
                continue
            raster(tx, ty, v[ix, 2], v[ix, 3], v[ix, 4], v[ix, 5], area)
    # box-filter the supersamples
    h, w = H // SS, W // SS
    c = cov.reshape(h, SS, w, SS).astype(float)
    alpha = c.mean(axis=(1, 3))
    acc = (rgb * cov[:, :, None]).reshape(h, SS, w, SS, 3).sum(axis=(1, 3))
    cnt = c.sum(axis=(1, 3))
    out = acc / np.maximum(cnt, 1)[:, :, None]
    return out, alpha


def frame_blocks(mask):
    """a frame's sprite blocks: up to 4x4 tiles each on a grid from the
    weapon's top left corner, trimmed to the tiles in use: (y, x, rows, cols)"""
    ys, xs = np.nonzero(mask)
    if len(xs) == 0:
        return []
    bx0, by0 = xs.min(), ys.min()
    tw = (xs.max() + 1 - bx0 + 7) // 8
    th = (ys.max() + 1 - by0 + 7) // 8
    grid = np.zeros((th * 8, tw * 8), bool)
    sub = mask[by0:by0 + th * 8, bx0:bx0 + tw * 8]
    grid[:sub.shape[0], :sub.shape[1]] = sub
    blocks = []
    for byk in range(0, th, 4):
        for bxk in range(0, tw, 4):
            used = [(r, c) for r in range(byk, min(th, byk + 4)) for c in range(bxk, min(tw, bxk + 4))
                    if grid[r * 8:r * 8 + 8, c * 8:c * 8 + 8].any()]
            if not used:
                continue
            r0, r1 = min(r for r, _ in used), max(r for r, _ in used) + 1
            c0, c1 = min(c for _, c in used), max(c for _, c in used) + 1
            blocks.append((by0 + r0 * 8, bx0 + c0 * 8, r1 - r0, c1 - c0))
    return blocks


def ccomp(a, b):
    """colour difference (Yliluoma's): brightness counts most, then hue"""
    d = a - b
    return ((d * d) @ LUMA) * 0.75 + (a @ LUMA - b @ LUMA) ** 2


def grid_dither(rgb):
    """each channel ordered-dithered to the Mega Drive's 8 levels with the
    same Bayer threshold, so a grey stays a grey; returns 9-bit codes
    (r << 6 | g << 3 | b)"""
    h, w = rgb.shape[:2]
    yy, xx = np.mgrid[0:h, 0:w]
    t = (BAYER4[yy % 4, xx % 4] + 0.5) / 16 - 0.5
    lv = np.clip(np.floor(rgb * 7 / 255.0 + DITHER * t[:, :, None] + 0.5), 0, 7).astype(int)
    return lv @ np.array([64, 8, 1])


def code_rgb(c):
    c = np.asarray(c)
    return np.stack([(c >> 6) & 7, (c >> 3) & 7, c & 7], -1) * 255.0 / 7


def code_cram(c):
    return (int(c) & 7) << 9 | ((int(c) >> 3) & 7) << 5 | ((int(c) >> 6) & 7) << 1


def palette_from(codes):
    """the 15 colours these pixels use most"""
    u, n = np.unique(codes, return_counts=True)
    keep = list(u[np.argsort(-n, kind='stable')][:15])
    while len(keep) < 15:
        keep.append(keep[0])
    return np.array(keep)


def palette_cost(codes, pal):
    if not len(codes):
        return 0.0
    return ccomp(code_rgb(codes)[:, None, :], code_rgb(pal)[None, :, :]).min(1).sum()


def build_sets(pak, pal, colormap, preview=None):
    """every weapon at every screen size: the frames as colour indices, each
    sprite block on one of three 15-colour palettes made for that set"""
    sets = []
    for wi, path in enumerate(WEAPONS):
        m = load_mdl(pak[path])
        nframes = min(len(m['frames']), WEAPON_FRAMES.get(path, 99))
        for si, (vw, vh) in enumerate(SIZES):
            renders = [render_gun(m, f, vw, vh, pal, colormap, size=si) for f in range(nframes)]
            masks = [al >= 0.5 for _, al in renders]
            codes = [grid_dither(np.clip(rgb * LIGHT_SCALE, 0, 255)) for rgb, _ in renders]
            blocks = []                 # (frame, y, x, rows, cols, the opaque pixels' codes)
            for f, mask in enumerate(masks):
                for (y, x, th, tw) in frame_blocks(mask):
                    sub = codes[f][y:y + th * 8, x:x + tw * 8][mask[y:y + th * 8, x:x + tw * 8]]
                    blocks.append([f, y, x, th, tw, sub])
            allc = np.concatenate([b[5] for b in blocks])
            # three palettes: the blocks split by brightness, then each block
            # takes the palette that fits it best, a few times over
            lum = np.array([code_rgb(b[5]).mean() if len(b[5]) else 0 for b in blocks])
            group = np.searchsorted(np.quantile(lum, np.linspace(0, 1, GUN_NPAL + 1)[1:-1]), lum)
            for _ in range(4):
                pals = []
                for g in range(GUN_NPAL):
                    c = [b[5] for b, k in zip(blocks, group) if k == g and len(b[5])]
                    pals.append(palette_from(np.concatenate(c) if c else allc))
                group = np.array([int(np.argmin([palette_cost(b[5], pals[g]) for g in range(GUN_NPAL)]))
                                  for b in blocks])
            frames = [np.zeros(mk.shape, int) for mk in masks]
            lines = [[] for _ in renders]
            for b, g in zip(blocks, group):
                f, y, x, th, tw, _ = b
                msk = masks[f][y:y + th * 8, x:x + tw * 8]
                reg = codes[f][y:y + th * 8, x:x + tw * 8]
                idx = ccomp(code_rgb(reg.reshape(-1))[:, None, :], code_rgb(pals[g])[None, :, :]).argmin(1)
                idx = (idx + 1).reshape(msk.shape) * msk
                frames[f][y:y + idx.shape[0], x:x + idx.shape[1]] = idx
                lines[f].append((y, x, th, tw, g))
            words = []
            for g in range(GUN_NPAL):
                words += [0] + [code_cram(c) for c in pals[g]]
            sets.append(dict(weapon=wi, size=si, vw=vw, vh=vh, frames=frames, blocks=lines, pal=words))
            if preview is not None:
                save_preview(preview, '%s_s%d' % (os.path.basename(path)[:-4], si), frames, lines, words)
    return sets


def cram_rgb(w):
    return tuple(((w >> s) & 7) * 255 // 7 for s in (1, 5, 9))


def save_preview(d, name, frames, lines, words):
    """the frames side by side, cropped to the weapon"""
    from PIL import Image
    ys, xs = np.nonzero(sum(f > 0 for f in frames))
    x0, x1, y0, y1 = xs.min(), xs.max() + 1, ys.min(), ys.max() + 1
    w, h = x1 - x0, y1 - y0
    im = Image.new('RGB', (w * len(frames), h), (40, 90, 40))
    rgbs = [cram_rgb(c) for c in words]
    for f, idx in enumerate(frames):
        for (by, bx, th, tw, g) in lines[f]:
            for y in range(by, min(by + th * 8, idx.shape[0])):
                for x in range(bx, min(bx + tw * 8, idx.shape[1])):
                    v = idx[y, x]
                    if v and y0 <= y < y1 and x0 <= x < x1:
                        im.putpixel((f * w + x - x0, y - y0), rgbs[16 * g + v])
    im.resize((im.width * 3, im.height * 3), Image.NEAREST).save(os.path.join(d, name + '.png'))


def sprites_for(idx, blocks, vw, vh, tile_base):
    """one frame's sprites (from frame_blocks, with their palette); returns
    (sprite words, tile bytes)"""
    ox = 160 - vw                       # view window in screen pixels / lines
    oy = VIEW_ROWS - vh
    pad = np.zeros((idx.shape[0] + 32, idx.shape[1] + 32), int)
    pad[:idx.shape[0], :idx.shape[1]] = idx
    words, tiles = [], bytearray()
    for (y, x, th, tw, g) in blocks:
        first = tile_base + len(tiles) // 32
        for c in range(tw):                 # sprite tiles go down the columns
            for r in range(th):
                tiles += tile_bytes(pad[y + r * 8:y + r * 8 + 8, x + c * 8:x + c * 8 + 8])
        size = ((tw - 1) << 2) | (th - 1)
        words += [oy + y + 128, size << 8, ((GUN_PAL + g) << 13) | first, ox + x + 128]
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
    hdr['pal'] = b.add(struct.pack('>32H', *(font_pal + [0] * 16)), 4)
    table = []
    total, worst = 0, 0
    for s in sets:
        ftab, ctab, alltiles = [], [], bytearray()
        for idx, blocks in zip(s['frames'], s['blocks']):
            words, t = sprites_for(idx, blocks, s['vw'], s['vh'], GUN_TILE + len(alltiles) // 32)
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
        po = b.add(struct.pack('>%dH' % (16 * GUN_NPAL), *s['pal']), 4)
        table.append(struct.pack('>IHHIIIIII', to, ntiles, len(s['frames']), fo, po, co, 0, 0, 0))
    hdr['sets'] = b.add(b''.join(table), 4)
    b.data[0:32] = struct.pack('>4sIIIIIII', b'MDQ1', hdr['tiles'], STATIC_TILES, hdr['pal'],
                               len(sets), hdr['sets'], len(SIZES), len(WEAPONS))
    open(out, 'wb').write(bytes(b.data))
    print('md data: %d bytes, %d weapon sets, weapon tiles %d KB (largest set %d tiles)' % (
        len(b.data), len(sets), total // 1024, worst))


if __name__ == '__main__':
    main()
