#include "q32x/mdgfx.h"

#include <tuple>
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

#include "q32x/gamedata.h"
#include "q32x/image.h"

namespace q32x {

const std::array<std::array<int, 2>, 6> MD_SIZES = {{{160, 104}, {144, 96}, {128, 80}, {112, 72}, {96, 64}, {80, 56}}};
const std::vector<std::string> MD_WEAPONS = {"progs/v_axe.mdl", "progs/v_shot.mdl", "progs/v_shot2.mdl",
                                             "progs/v_nail.mdl", "progs/v_rock.mdl"};
// frames kept (the axe's second swing, frames 5-8, would not fit in VRAM
// with the first: the game uses the first swing for both)
static int weapon_frames_max(const std::string &path) { return path == "progs/v_axe.mdl" ? 5 : 99; }

static const int SS = 3;               // supersampling for the weapon renders
static const double REF_LIGHT = 128;   // the light the weapon is rendered at (Quake's brightest)
static const int GUN_PAL = 1;          // palette lines for the weapon: 1, 2 and 3
static const int BAYER4[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
static const double LUMA[3] = {0.299, 0.587, 0.114};
static const double WEIGHT[3] = {3.0, 4.0, 2.0};   // perceptual weights for r, g, b

static int md_level(double v)
{
    return int(std::max<int64_t>(0, std::min<int64_t>(7, pyround(v * 7 / 255.0))));
}
static uint16_t cram(const double rgb[3])
{
    int r = md_level(rgb[0]), g = md_level(rgb[1]), b = md_level(rgb[2]);
    return uint16_t((b << 9) | (g << 5) | (r << 1));
}

// numpy's pairwise summation (np.sum of a contiguous float64 array)
static double pairwise_sum(const double *a, size_t n)
{
    if (n < 8) {
        double res = -0.0;
        for (size_t i = 0; i < n; i++) res += a[i];
        return res;
    }
    if (n <= 128) {
        double r[8];
        for (int j = 0; j < 8; j++) r[j] = a[j];
        size_t i;
        for (i = 8; i < n - (n % 8); i += 8)
            for (int j = 0; j < 8; j++) r[j] += a[i + size_t(j)];
        double res = ((r[0] + r[1]) + (r[2] + r[3])) + ((r[4] + r[5]) + (r[6] + r[7]));
        for (; i < n; i++) res += a[i];
        return res;
    }
    size_t n2 = n / 2;
    n2 -= n2 % 8;
    return pairwise_sum(a, n2) + pairwise_sum(a + n2, n - n2);
}
static double np_sum(const std::vector<double> &v) { return 0.0 + pairwise_sum(v.data(), v.size()); }

// weighted k-means in RGB (pts n x 3, weights n)
static std::vector<std::array<double, 3>> kmeans(const std::vector<std::array<double, 3>> &pts,
                                                 const std::vector<double> &w, size_t k, int iters = 40)
{
    std::vector<size_t> order(pts.size());
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return -w[a] < -w[b]; });
    std::vector<std::array<double, 3>> cent;
    for (size_t i = 0; i < k && i < order.size(); i++) cent.push_back(pts[order[i]]);
    while (cent.size() < k) cent.push_back({0, 0, 0});
    std::vector<size_t> a(pts.size());
    for (int it = 0; it < iters; it++) {
        for (size_t i = 0; i < pts.size(); i++) {
            double best = 0;
            size_t bj = 0;
            for (size_t j = 0; j < k; j++) {
                double d = 0;
                for (int c = 0; c < 3; c++) {
                    double e = pts[i][size_t(c)] - cent[j][size_t(c)];
                    d += (e * e) * WEIGHT[c];
                }
                if (j == 0 || d < best) {
                    best = d;
                    bj = j;
                }
            }
            a[i] = bj;
        }
        for (size_t j = 0; j < k; j++) {
            double s[3] = {0, 0, 0}, ws = 0;
            bool any = false;
            for (size_t i = 0; i < pts.size(); i++)
                if (a[i] == j) {
                    for (int c = 0; c < 3; c++) s[c] += pts[i][size_t(c)] * w[i];
                    ws += w[i];
                    any = true;
                }
            if (any)
                for (int c = 0; c < 3; c++) cent[j][size_t(c)] = s[c] / ws;
        }
    }
    return cent;
}

static size_t nearest_plain(const std::vector<std::array<double, 3>> &cent, const double p[3])
{
    double best = 0;
    size_t bj = 0;
    for (size_t j = 0; j < cent.size(); j++) {
        double d = 0;
        for (int c = 0; c < 3; c++) {
            double e = cent[j][size_t(c)] - p[c];
            d += e * e;
        }
        if (j == 0 || d < best) {
            best = d;
            bj = j;
        }
    }
    return bj;
}

static void tile_bytes(Bytes &out, const uint8_t px[8][8])
{
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x += 2) out.push_back(uint8_t((px[y][x] << 4) | px[y][x + 1]));
}

// ------------------------------------------------------------ fixed tiles

static void static_tiles(const Pak &pak, const Palette &pal, const MdGfxOptions &opt, Bytes &tiles,
                         std::array<uint16_t, 16> &font_pal_out)
{
    auto wad = read_wad(pak.get("gfx.wad"));
    Bytes conchars = opt.font ? *opt.font : Reader(wad.at("conchars")).slice(0, 128 * 128);
    Bytes backtile = opt.border ? *opt.border : Reader(wad.at("backtile")).slice(8, 64 * 64);
    check(conchars.size() == 128 * 128 && backtile.size() == 64 * 64, "font or border tile of the wrong size");
    auto prgb = [&](int k, double out[3]) {
        for (int c = 0; c < 3; c++) out[c] = pal.rgb[3 * k + c];
    };
    // font: the colours of the glyphs in use, cut to 14 (entries 2..15)
    std::vector<bool> used(256);
    for (int c = 0; c < 256; c++) used[size_t(c)] = (32 <= (c & 127) && (c & 127) < 127) || (c & 127) == 13;
    std::map<int, int> cnt;
    for (int c = 0; c < 256; c++) {
        if (!used[size_t(c)]) continue;
        for (int y = 0; y < 8; y++)
            for (int x = 0; x < 8; x++) {
                int p = conchars[size_t(((c >> 4) * 8 + y) * 128 + (c & 15) * 8 + x)];
                if (p) cnt[p]++;
            }
    }
    std::vector<std::array<double, 3>> pts;
    std::vector<double> w;
    std::vector<int> keys;
    for (auto &kv : cnt) {
        double p[3];
        prgb(kv.first, p);
        pts.push_back({p[0], p[1], p[2]});
        w.push_back(kv.second);
        keys.push_back(kv.first);
    }
    auto cent = kmeans(pts, w, 14);
    std::map<int, int> fmap;
    for (int k : keys) {
        double p[3];
        prgb(k, p);
        fmap[k] = 2 + int(nearest_plain(cent, p));
    }
    std::vector<uint16_t> font_pal = {0, 0};
    for (auto &c : cent) font_pal.push_back(cram(c.data()));

    // border: backtile has a handful of colours
    std::map<int, int> bcnt;
    for (uint8_t p : backtile) bcnt[p]++;
    std::map<int, int> bmap;
    std::vector<uint16_t> border_pal = {0, 0};
    if (bcnt.size() > 14) {
        std::vector<std::array<double, 3>> bpts;
        std::vector<double> bw;
        for (auto &kv : bcnt) {
            double p[3];
            prgb(kv.first, p);
            bpts.push_back({p[0], p[1], p[2]});
            bw.push_back(kv.second);
        }
        auto bcent = kmeans(bpts, bw, 14);
        for (auto &kv : bcnt) {
            double p[3];
            prgb(kv.first, p);
            bmap[kv.first] = 2 + int(nearest_plain(bcent, p));
        }
        for (auto &c : bcent) border_pal.push_back(cram(c.data()));
    } else {
        int i = 0;
        for (auto &kv : bcnt) {
            bmap[kv.first] = 2 + i++;
            double p[3];
            prgb(kv.first, p);
            border_pal.push_back(cram(p));
        }
    }
    border_pal.resize(16, 0);
    // the font and the border share palette line 0 (the weapon has the
    // other three): their colours on the 3-bit grid are few
    std::vector<uint16_t> merged = {0, 0};
    std::map<int, int> remap_f, remap_b;
    auto merge = [&](const std::vector<uint16_t> &src, std::map<int, int> &remap, const std::map<int, int> &m) {
        std::set<int> idx;
        for (auto &kv : m) idx.insert(kv.second);
        for (int i : idx) {
            uint16_t wv = src[size_t(i)];
            auto it = std::find(merged.begin() + 2, merged.end(), wv);
            if (it == merged.end()) {
                merged.push_back(wv);
                it = merged.end() - 1;
            }
            remap[i] = int(it - merged.begin());
        }
    };
    merge(font_pal, remap_f, fmap);
    merge(border_pal, remap_b, bmap);
    check(merged.size() <= 16, "the font and border need " + std::to_string(merged.size() - 2) +
                                   " colours, more than the 14 a palette line holds");
    for (auto &kv : fmap) kv.second = remap_f[kv.second];
    for (auto &kv : bmap) kv.second = remap_b[kv.second];
    merged.resize(16, 0);
    for (int i = 0; i < 16; i++) font_pal_out[size_t(i)] = merged[size_t(i)];

    tiles.clear();
    uint8_t px[8][8];
    for (int t = 0; t < MD_FONT_TILE; t++) {
        for (int y = 0; y < 8; y++)
            for (int x = 0; x < 8; x++) px[y][x] = t == 1 ? 1 : t == 2 ? (((x + y) & 1) == 0 ? 1 : 0) : 0;
        tile_bytes(tiles, px);
    }
    for (int c = 0; c < 256; c++) {
        for (int y = 0; y < 8; y++)
            for (int x = 0; x < 8; x++) {
                px[y][x] = 0;
                if (used[size_t(c)]) {
                    int p = conchars[size_t(((c >> 4) * 8 + y) * 128 + (c & 15) * 8 + x)];
                    px[y][x] = p ? uint8_t(fmap[p]) : 0;
                }
            }
        tile_bytes(tiles, px);
    }
    for (int shade = 0; shade < 2; shade++)
        for (int ty = 0; ty < 8; ty++)
            for (int tx = 0; tx < 8; tx++) {
                for (int y = 0; y < 8; y++)
                    for (int x = 0; x < 8; x++) {
                        px[y][x] = uint8_t(bmap[backtile[size_t((ty * 8 + y) * 64 + tx * 8 + x)]]);
                        if (shade && ((x + y) & 1) == 0) px[y][x] = 1;
                    }
                tile_bytes(tiles, px);
            }
    check(tiles.size() == size_t(MD_STATIC_TILES) * 32, "static tile count");
}

// ------------------------------------------------------------ the weapon

namespace {
struct Mdl {
    double scale[3], org[3];
    int sw, sh, nverts;
    Bytes skin;
    std::vector<std::array<int, 3>> st;
    std::vector<std::array<int, 4>> tris;
    std::vector<Bytes> frames;
};

Mdl load_mdl(const Bytes &d)
{
    Reader r(d);
    check(d.size() >= 84 && std::memcmp(d.data(), "IDPO", 4) == 0 && r.i32(4) == 6, "not a Quake model");
    Mdl m;
    for (int k = 0; k < 3; k++) {
        m.scale[k] = r.f32(8 + 4 * size_t(k));
        m.org[k] = r.f32(20 + 4 * size_t(k));
    }
    int nskins = r.i32(48);
    m.sw = r.i32(52);
    m.sh = r.i32(56);
    m.nverts = r.i32(60);
    int ntris = r.i32(64), nframes = r.i32(68);
    size_t o = 84, ss = size_t(m.sw) * size_t(m.sh);
    bool have = false;
    for (int i = 0; i < nskins; i++) {
        int t = r.i32(o);
        o += 4;
        if (t == 0) {
            if (!have) m.skin = r.slice(o, ss);
            have = true;
            o += ss;
        } else {
            int n = r.i32(o);
            o += 4 + 4 * size_t(n);
            if (!have) m.skin = r.slice(o, ss);
            have = true;
            o += size_t(n) * ss;
        }
    }
    for (int i = 0; i < m.nverts; i++, o += 12) m.st.push_back({r.i32(o), r.i32(o + 4), r.i32(o + 8)});
    for (int i = 0; i < ntris; i++, o += 16) m.tris.push_back({r.i32(o), r.i32(o + 4), r.i32(o + 8), r.i32(o + 12)});
    for (int i = 0; i < nframes; i++) {
        int t = r.i32(o);
        o += 4;
        if (t == 0) {
            o += 24;
            m.frames.push_back(r.slice(o, 4 * size_t(m.nverts)));
            o += 4 * size_t(m.nverts);
        } else {
            int n = r.i32(o);
            o += 4 + 8 + 4 * size_t(n);
            Bytes first;
            for (int k = 0; k < n; k++) {
                o += 24;
                if (k == 0) first = r.slice(o, 4 * size_t(m.nverts));
                o += 4 * size_t(m.nverts);
            }
            m.frames.push_back(first);
        }
    }
    return m;
}

constexpr double ALIAS_Z_CLIP = 5.0;   // Quake clips alias models 5 units in front of the eye

using V6 = std::array<double, 6>;   // x, y, z, s, t, light

std::vector<V6> clip_near(const std::vector<V6> &poly)
{
    std::vector<V6> out;
    for (size_t k = 0; k < poly.size(); k++) {
        const V6 &a = poly[k], &b = poly[(k + 1) % poly.size()];
        bool ain = a[2] >= ALIAS_Z_CLIP, bin = b[2] >= ALIAS_Z_CLIP;
        if (ain) out.push_back(a);
        if (ain != bin) {
            double f = (ALIAS_Z_CLIP - a[2]) / (b[2] - a[2]);
            V6 m;
            for (int i = 0; i < 6; i++) m[size_t(i)] = a[size_t(i)] + f * (b[size_t(i)] - a[size_t(i)]);
            out.push_back(m);
        }
    }
    return out;
}

struct Render {
    int w, h;
    std::vector<double> rgb;    // h * w * 3
    std::vector<int> cov;       // samples covered (of SS * SS)
};

// the weapon as the 32X would see it in a vw x vh view: like Quake's alias
// renderer, triangles clipped at the near plane, back faces culled, texture
// and light interpolated across the screen; box-filtered supersamples
Render render_gun(const Mdl &m, int frame, int vw, int vh, const Palette &pal, const Bytes &colormap, int size, bool flat)
{
    int flat_c = -1;   // flat shading: the triangle's colour (and one light for it)
    const int W = 2 * vw * SS, H = 2 * vh * SS;
    const double focal = vw * SS;
    const Bytes &fv = m.frames.at(size_t(frame));
    // V_CalcRefdef's fudge: the weapon is raised 2 units at viewsize 100, 1
    // at 90, 0.5 at 80 and not at all below
    double raise = size == 0 ? 2.0 : size == 1 ? 1.0 : size == 2 ? 0.5 : 0.0;
    std::vector<std::array<double, 3>> P(static_cast<size_t>(m.nverts)), N(size_t(m.nverts));
    for (int i = 0; i < m.nverts; i++) {
        const uint8_t *b = fv.data() + 4 * i;
        double x = m.scale[0] * b[0] + m.org[0];
        double y = m.scale[1] * b[1] + m.org[1];
        double z = m.scale[2] * b[2] + m.org[2];
        P[size_t(i)] = {-y, -(z + raise), x};   // view right, down, forward
        N[size_t(i)] = {ANORMS[b[3]][0], ANORMS[b[3]][1], ANORMS[b[3]][2]};
    }
    // Quake's alias lighting: ambient, plus shade on the side away from the
    // light vector (-1, 0, 0) turned into the model's frame
    const double a = 180.0 * (3.141592653589793 / 180.0);
    const double lv[3] = {-std::cos(a), std::sin(a), 0.0};
    double amb = std::min(REF_LIGHT, 128.0), shade = REF_LIGHT;
    if (amb + shade > 192) shade = 192 - amb;
    std::vector<double> light(static_cast<size_t>(m.nverts));
    for (int i = 0; i < m.nverts; i++) {
        double lc = (N[size_t(i)][0] * lv[0] + N[size_t(i)][1] * lv[1]) + N[size_t(i)][2] * lv[2];
        double l = (255 - amb) + (lc < 0 ? shade * lc : 0.0);
        light[size_t(i)] = std::min(255.0, std::max(0.0, l));
    }
    std::vector<double> zbuf(static_cast<size_t>(W) * size_t(H), std::numeric_limits<double>::infinity());
    std::vector<int> rgb(static_cast<size_t>(W) * size_t(H) * 3, 0);
    std::vector<uint8_t> cov(static_cast<size_t>(W) * size_t(H), 0);
    const int sw = m.sw, sh = m.sh;

    auto raster = [&](const double xs[3], const double ys[3], const double zs[3], const double ss[3],
                      const double ts[3], const double ls[3], double area) {
        int x0 = int(std::max<int64_t>(0, pyfloor(std::min({xs[0], xs[1], xs[2]}))));
        int x1 = int(std::min<int64_t>(W - 1, pyceil(std::max({xs[0], xs[1], xs[2]}))));
        int y0 = int(std::max<int64_t>(0, pyfloor(std::min({ys[0], ys[1], ys[2]}))));
        int y1 = int(std::min<int64_t>(H - 1, pyceil(std::max({ys[0], ys[1], ys[2]}))));
        if (x0 > x1 || y0 > y1) return;
        for (int py = y0; py <= y1; py++) {
            double gy = py + 0.5;
            for (int px = x0; px <= x1; px++) {
                double gx = px + 0.5;
                double w0 = ((xs[1] - gx) * (ys[2] - gy) - (xs[2] - gx) * (ys[1] - gy)) / area;
                double w1 = ((xs[2] - gx) * (ys[0] - gy) - (xs[0] - gx) * (ys[2] - gy)) / area;
                double w2 = 1 - w0 - w1;
                if (!(w0 >= 0 && w1 >= 0 && w2 >= 0)) continue;
                double z = w0 * zs[0] + w1 * zs[1] + w2 * zs[2];
                size_t o = size_t(py) * size_t(W) + size_t(px);
                if (!(z < zbuf[o])) continue;
                double s = w0 * ss[0] + w1 * ss[1] + w2 * ss[2];
                double t = w0 * ts[0] + w1 * ts[1] + w2 * ts[2];
                double li = w0 * ls[0] + w1 * ls[1] + w2 * ls[2];
                int si = std::clamp(int(std::trunc(s)), 0, sw - 1);
                int ti = std::clamp(int(std::trunc(t)), 0, sh - 1);
                int tex = flat_c >= 0 ? flat_c : m.skin.at(size_t(ti) * size_t(sw) + size_t(si));
                int row = std::clamp(int(std::trunc(li / 4)), 0, 63);
                int c = colormap.at(size_t(row) * 256 + size_t(tex));
                zbuf[o] = z;
                for (int k = 0; k < 3; k++) rgb[o * 3 + size_t(k)] = pal.rgb[3 * c + k];
                cov[o] = 1;
            }
        }
    };

    for (auto &tr : m.tris) {
        std::vector<V6> poly;
        for (int k = 1; k <= 3; k++) {
            int i = tr[size_t(k)];
            auto &st = m.st.at(size_t(i));
            int s = st[1], t = st[2];
            if (st[0] && !tr[0]) s += sw / 2;
            poly.push_back({P[size_t(i)][0], P[size_t(i)][1], P[size_t(i)][2], double(s), double(t), light[size_t(i)]});
        }
        if (flat) {
            // the colour of the skin under the triangle (averaged), and one
            // light for the whole triangle, as the game draws flat models
            double acc[3] = {0, 0, 0};
            int n = 0;
            for (int a = 0; a <= 6; a++)
                for (int b2 = 0; b2 <= 6 - a; b2++) {
                    double wa = (a + 0.33) / 7.33, wb = (b2 + 0.33) / 7.33, wc = 1 - wa - wb;
                    if (wc < 0) continue;
                    double ss = wa * poly[0][3] + wb * poly[1][3] + wc * poly[2][3];
                    double tt = wa * poly[0][4] + wb * poly[1][4] + wc * poly[2][4];
                    int si = std::clamp(int(ss), 0, sw - 1), ti = std::clamp(int(tt), 0, sh - 1);
                    int c = m.skin.at(size_t(ti) * size_t(sw) + size_t(si));
                    for (int k = 0; k < 3; k++) acc[k] += pal.rgb[3 * c + k];
                    n++;
                }
            flat_c = pal.nearest(acc[0] / n, acc[1] / n, acc[2] / n, 224);
            double l = (poly[0][5] + poly[1][5] + poly[2][5]) / 3;
            for (auto &v : poly) v[5] = l;
        }
        poly = clip_near(poly);
        if (poly.size() < 3) continue;
        std::vector<double> xs(poly.size()), ys(poly.size());
        for (size_t k = 0; k < poly.size(); k++) {
            xs[k] = W / 2.0 + poly[k][0] * focal / poly[k][2];
            ys[k] = H / 2.0 + poly[k][1] * focal / poly[k][2];
        }
        for (size_t k = 1; k + 1 < poly.size(); k++) {
            size_t ix[3] = {0, k, k + 1};
            double tx[3], ty[3], tz[3], ts_[3], tt[3], tl[3];
            for (int j = 0; j < 3; j++) {
                tx[j] = xs[ix[j]];
                ty[j] = ys[ix[j]];
                tz[j] = poly[ix[j]][2];
                ts_[j] = poly[ix[j]][3];
                tt[j] = poly[ix[j]][4];
                tl[j] = poly[ix[j]][5];
            }
            // back faces are not drawn: Quake's test (d_polyse.c), as in r_model.c
            double area = (tx[1] - tx[0]) * (ty[2] - ty[0]) - (tx[2] - tx[0]) * (ty[1] - ty[0]);
            if (area <= 0) continue;
            raster(tx, ty, tz, ts_, tt, tl, area);
        }
    }
    // box-filter the supersamples
    Render r;
    r.h = H / SS;
    r.w = W / SS;
    r.rgb.assign(size_t(r.w) * size_t(r.h) * 3, 0.0);
    r.cov.assign(size_t(r.w) * size_t(r.h), 0);
    for (int y = 0; y < r.h; y++)
        for (int x = 0; x < r.w; x++) {
            int cnt = 0;
            double acc[3] = {0, 0, 0};
            for (int sy = 0; sy < SS; sy++)
                for (int sx = 0; sx < SS; sx++) {
                    size_t o = size_t(y * SS + sy) * size_t(W) + size_t(x * SS + sx);
                    if (cov[o]) {
                        cnt++;
                        for (int k = 0; k < 3; k++) acc[k] += rgb[o * 3 + size_t(k)];
                    }
                }
            size_t o = size_t(y) * size_t(r.w) + size_t(x);
            r.cov[o] = cnt;
            for (int k = 0; k < 3; k++) r.rgb[o * 3 + size_t(k)] = acc[k] / std::max(cnt, 1);
        }
    return r;
}

// colour difference (Yliluoma's): brightness counts most, then hue
double ccomp(const double a[3], const double b[3])
{
    double d[3] = {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
    double dd = (d[0] * d[0] * LUMA[0] + d[1] * d[1] * LUMA[1]) + d[2] * d[2] * LUMA[2];
    double la = (a[0] * LUMA[0] + a[1] * LUMA[1]) + a[2] * LUMA[2];
    double lb = (b[0] * LUMA[0] + b[1] * LUMA[1]) + b[2] * LUMA[2];
    double e = la - lb;
    return dd * 0.75 + e * e;
}

void code_rgb(int c, double out[3])
{
    out[0] = ((c >> 6) & 7) * 255.0 / 7;
    out[1] = ((c >> 3) & 7) * 255.0 / 7;
    out[2] = (c & 7) * 255.0 / 7;
}

uint16_t code_cram(int c) { return uint16_t((c & 7) << 9 | ((c >> 3) & 7) << 5 | ((c >> 6) & 7) << 1); }

// the 15 colours these pixels use most
std::array<int, 15> palette_from(const std::vector<int> &codes)
{
    std::map<int, int> cnt;
    for (int c : codes) cnt[c]++;
    std::vector<std::pair<int, int>> u(cnt.begin(), cnt.end());   // sorted by code
    std::stable_sort(u.begin(), u.end(), [](auto &a, auto &b) { return -a.second < -b.second; });
    std::array<int, 15> keep{};
    size_t n = std::min<size_t>(15, u.size());
    for (size_t i = 0; i < n; i++) keep[i] = u[i].first;
    for (size_t i = n; i < 15; i++) keep[i] = keep[0];
    return keep;
}

double palette_cost(const std::vector<int> &codes, const std::array<int, 15> &pal)
{
    if (codes.empty()) return 0.0;
    double prgb[15][3];
    for (int j = 0; j < 15; j++) code_rgb(pal[size_t(j)], prgb[j]);
    std::vector<double> best(codes.size());
    for (size_t i = 0; i < codes.size(); i++) {
        double c[3];
        code_rgb(codes[i], c);
        double b = 0;
        for (int j = 0; j < 15; j++) {
            double d = ccomp(c, prgb[j]);
            if (j == 0 || d < b) b = d;
        }
        best[i] = b;
    }
    return np_sum(best);
}

struct Block {
    int f, y, x, th, tw;
    std::vector<int> codes;   // the opaque pixels' codes, row by row
};

// a frame's sprite blocks: up to 4x4 tiles each on a grid from the weapon's
// top left corner, trimmed to the tiles in use: (y, x, rows, cols)
std::vector<std::array<int, 4>> frame_blocks(const std::vector<uint8_t> &mask, int w, int h)
{
    int minx = w, maxx = -1, miny = h, maxy = -1;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            if (mask[size_t(y) * size_t(w) + size_t(x)]) {
                minx = std::min(minx, x);
                maxx = std::max(maxx, x);
                miny = std::min(miny, y);
                maxy = std::max(maxy, y);
            }
    if (maxx < 0) return {};
    int bx0 = minx, by0 = miny;
    int tw = (maxx + 1 - bx0 + 7) / 8, th = (maxy + 1 - by0 + 7) / 8;
    auto at = [&](int gy, int gx) {
        int y = by0 + gy, x = bx0 + gx;
        return y < h && x < w && mask[size_t(y) * size_t(w) + size_t(x)];
    };
    std::vector<std::array<int, 4>> blocks;
    for (int byk = 0; byk < th; byk += 4)
        for (int bxk = 0; bxk < tw; bxk += 4) {
            int r0 = 1 << 30, r1 = -1, c0 = 1 << 30, c1 = -1;
            for (int r = byk; r < std::min(th, byk + 4); r++)
                for (int c = bxk; c < std::min(tw, bxk + 4); c++) {
                    bool any = false;
                    for (int y = 0; y < 8 && !any; y++)
                        for (int x = 0; x < 8 && !any; x++) any = at(r * 8 + y, c * 8 + x);
                    if (any) {
                        r0 = std::min(r0, r);
                        r1 = std::max(r1, r + 1);
                        c0 = std::min(c0, c);
                        c1 = std::max(c1, c + 1);
                    }
                }
            if (r1 < 0) continue;
            blocks.push_back({by0 + r0 * 8, bx0 + c0 * 8, r1 - r0, c1 - c0});
        }
    return blocks;
}

}  // namespace

MdWeaponSet render_weapon_set(const Pak &pak, int wi, int si, double light_scale, double dither, bool flat)
{
    Palette pal = Palette::from(pak.get("gfx/palette.lmp"));
    Bytes colormap = Reader(pak.get("gfx/colormap.lmp")).slice(0, 64 * 256);
    const std::string &path = MD_WEAPONS.at(size_t(wi));
    Mdl m = load_mdl(pak.get(path));
    int nframes = std::min(int(m.frames.size()), weapon_frames_max(path));
    int vw = MD_SIZES[size_t(si)][0], vh = MD_SIZES[size_t(si)][1];
    std::vector<std::vector<uint8_t>> masks;
    std::vector<std::vector<int>> codes;
    int w = 0, h = 0;
    for (int f = 0; f < nframes; f++) {
        Render r = render_gun(m, f, vw, vh, pal, colormap, si, flat);
        w = r.w;
        h = r.h;
        std::vector<uint8_t> mask(static_cast<size_t>(w) * size_t(h));
        std::vector<int> code(static_cast<size_t>(w) * size_t(h));
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) {
                size_t o = size_t(y) * size_t(w) + size_t(x);
                mask[o] = r.cov[o] / 9.0 >= 0.5;
                // each channel ordered-dithered to the Mega Drive's 8 levels
                // with the same Bayer threshold, so a grey stays a grey
                double t = (BAYER4[y % 4][x % 4] + 0.5) / 16 - 0.5;
                int c = 0;
                for (int k = 0; k < 3; k++) {
                    double v = std::min(255.0, std::max(0.0, r.rgb[o * 3 + size_t(k)] * light_scale));
                    double lv = std::floor(v * 7 / 255.0 + dither * t + 0.5);
                    int l = int(std::min(7.0, std::max(0.0, lv)));
                    c = c * 8 + l;
                }
                code[o] = c;
            }
        masks.push_back(std::move(mask));
        codes.push_back(std::move(code));
    }
    std::vector<Block> blocks;
    for (int f = 0; f < nframes; f++)
        for (auto &b : frame_blocks(masks[size_t(f)], w, h)) {
            Block bl{f, b[0], b[1], b[2], b[3], {}};
            for (int y = b[0]; y < std::min(h, b[0] + b[2] * 8); y++)
                for (int x = b[1]; x < std::min(w, b[1] + b[3] * 8); x++) {
                    size_t o = size_t(y) * size_t(w) + size_t(x);
                    if (masks[size_t(f)][o]) bl.codes.push_back(codes[size_t(f)][o]);
                }
            blocks.push_back(std::move(bl));
        }
    std::vector<int> allc;
    for (auto &b : blocks) allc.insert(allc.end(), b.codes.begin(), b.codes.end());
    // three palettes: the blocks split by brightness, then each block takes
    // the palette that fits it best, a few times over
    std::vector<double> lum;
    for (auto &b : blocks) {
        if (b.codes.empty()) {
            lum.push_back(0);
            continue;
        }
        std::vector<double> v;
        for (int c : b.codes) {
            double rgb[3];
            code_rgb(c, rgb);
            v.insert(v.end(), rgb, rgb + 3);
        }
        lum.push_back(np_sum(v) / double(v.size()));
    }
    std::vector<size_t> group(blocks.size(), 0);
    if (!lum.empty()) {
        std::vector<double> sorted = lum;
        std::sort(sorted.begin(), sorted.end());
        double n1 = double(sorted.size() - 1);
        const double step = 1.0 / 3.0;
        double qs[2];
        for (int qi = 0; qi < 2; qi++) {
            double q = (qi + 1) * step;
            double vi = n1 * q;
            double prev_d = std::floor(vi);
            size_t prev = size_t(prev_d), next = prev + 1;
            if (vi >= n1) prev = next = sorted.size() - 1;
            if (next >= sorted.size()) next = sorted.size() - 1;
            double gamma = vi - prev_d;
            double a = sorted[prev], b = sorted[next];
            double diff = b - a;
            qs[qi] = gamma >= 0.5 ? b - diff * (1 - gamma) : a + diff * gamma;
        }
        for (size_t i = 0; i < lum.size(); i++) group[i] = size_t(qs[0] < lum[i]) + size_t(qs[1] < lum[i]);
    }
    std::vector<std::array<int, 15>> pals(MD_GUN_NPAL);
    for (int it = 0; it < 4; it++) {
        for (int g = 0; g < MD_GUN_NPAL; g++) {
            std::vector<int> c;
            for (size_t i = 0; i < blocks.size(); i++)
                if (group[i] == size_t(g)) c.insert(c.end(), blocks[i].codes.begin(), blocks[i].codes.end());
            pals[size_t(g)] = palette_from(c.empty() ? allc : c);
        }
        for (size_t i = 0; i < blocks.size(); i++) {
            double best = 0;
            size_t bg = 0;
            for (int g = 0; g < MD_GUN_NPAL; g++) {
                double cost = palette_cost(blocks[i].codes, pals[size_t(g)]);
                if (g == 0 || cost < best) {
                    best = cost;
                    bg = size_t(g);
                }
            }
            group[i] = bg;
        }
    }
    MdWeaponSet set;
    set.weapon = wi;
    set.size = si;
    set.vw = vw;
    set.vh = vh;
    set.frames.assign(size_t(nframes), std::vector<uint8_t>(size_t(w) * size_t(h), 0));
    set.blocks.resize(size_t(nframes));
    for (size_t i = 0; i < blocks.size(); i++) {
        const Block &b = blocks[i];
        size_t g = group[i];
        double prgb[15][3];
        for (int j = 0; j < 15; j++) code_rgb(pals[g][size_t(j)], prgb[j]);
        for (int y = b.y; y < std::min(h, b.y + b.th * 8); y++)
            for (int x = b.x; x < std::min(w, b.x + b.tw * 8); x++) {
                size_t o = size_t(y) * size_t(w) + size_t(x);
                double c[3];
                code_rgb(codes[size_t(b.f)][o], c);
                double bd = 0;
                int bj = 0;
                for (int j = 0; j < 15; j++) {
                    double d = ccomp(c, prgb[j]);
                    if (j == 0 || d < bd) {
                        bd = d;
                        bj = j;
                    }
                }
                set.frames[size_t(b.f)][o] = masks[size_t(b.f)][o] ? uint8_t(bj + 1) : 0;
            }
        set.blocks[size_t(b.f)].push_back({b.y, b.x, b.th, b.tw, int(g)});
    }
    for (int g = 0; g < MD_GUN_NPAL; g++) {
        set.pal[size_t(16 * g)] = 0;
        for (int j = 0; j < 15; j++) set.pal[size_t(16 * g + 1 + j)] = code_cram(pals[size_t(g)][size_t(j)]);
    }
    return set;
}

static void cram_rgb(uint16_t w, uint8_t out[3])
{
    for (int k = 0; k < 3; k++) out[k] = uint8_t(((w >> (1 + 4 * k)) & 7) * 255 / 7);
}

static void save_preview(const std::string &dir, const std::string &name, const MdWeaponSet &s)
{
    int w = 2 * s.vw, h = 2 * s.vh;
    int x0 = w, x1 = -1, y0 = h, y1 = -1;
    for (auto &f : s.frames)
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++)
                if (f[size_t(y) * size_t(w) + size_t(x)]) {
                    x0 = std::min(x0, x);
                    x1 = std::max(x1, x + 1);
                    y0 = std::min(y0, y);
                    y1 = std::max(y1, y + 1);
                }
    if (x1 < 0) return;
    int cw = x1 - x0, ch = y1 - y0;
    RgbImage im(cw * int(s.frames.size()), ch, 40, 90, 40);
    for (size_t f = 0; f < s.frames.size(); f++)
        for (auto &b : s.blocks[f])
            for (int y = b.y; y < std::min(b.y + b.rows * 8, h); y++)
                for (int x = b.x; x < std::min(b.x + b.cols * 8, w); x++) {
                    int v = s.frames[f][size_t(y) * size_t(w) + size_t(x)];
                    if (v && y >= y0 && y < y1 && x >= x0 && x < x1) {
                        uint8_t c[3];
                        cram_rgb(s.pal[size_t(16 * b.pal + v)], c);
                        im.set(int(f) * cw + x - x0, y - y0, c[0], c[1], c[2]);
                    }
                }
    save_png(dir + "/" + name + ".png", im.scaled(3));
}

// one frame's sprites (with their palette), placed in a view: sprite words
// and tile bytes
static void sprites_for(const MdWeaponSet &s, size_t f, int ox, int oy, int tile_base, std::vector<int> &words,
                        Bytes &tiles)
{
    int w = 2 * s.vw, h = 2 * s.vh;
    auto px = [&](int y, int x) -> uint8_t {
        if (y < 0 || x < 0 || y >= h || x >= w) return 0;
        return s.frames[f][size_t(y) * size_t(w) + size_t(x)];
    };
    for (auto &b : s.blocks[f]) {
        int first = tile_base + int(tiles.size() / 32);
        for (int c = 0; c < b.cols; c++)   // sprite tiles go down the columns
            for (int r = 0; r < b.rows; r++) {
                uint8_t t[8][8];
                for (int y = 0; y < 8; y++)
                    for (int x = 0; x < 8; x++) t[y][x] = px(b.y + r * 8 + y, b.x + c * 8 + x);
                tile_bytes(tiles, t);
            }
        int size = ((b.cols - 1) << 2) | (b.rows - 1);
        words.insert(words.end(), {oy + b.y + 128, size << 8, ((GUN_PAL + b.pal) << 13) | first, ox + b.x + 128});
    }
}

// 32X rows of the view the weapon covers completely: (row0, [(x0, x1)])
static void cutout(const std::vector<uint8_t> &canvas, int vw, int vh, int &row0, std::vector<std::pair<int, int>> &rows_out)
{
    int w = 2 * vw;
    std::vector<std::pair<int, int>> rows;
    for (int r = 0; r < vh; r++) {
        std::pair<int, int> best = {0, 0};
        int run = 0, start = 0;
        for (int x = 0; x <= vw; x++) {
            bool cell = false;
            if (x < vw) {
                auto op = [&](int y, int xx) { return canvas[size_t(y) * size_t(w) + size_t(xx)] > 0; };
                cell = op(2 * r, 2 * x) && op(2 * r + 1, 2 * x) && op(2 * r, 2 * x + 1) && op(2 * r + 1, 2 * x + 1);
            }
            if (cell) {
                if (run == 0) start = x;
                run++;
            } else {
                if (run > best.second - best.first) best = {start, start + run};
                run = 0;
            }
        }
        rows.push_back(best);
    }
    int first = vh, last = -1;
    for (int r = 0; r < vh; r++)
        if (rows[size_t(r)].second) {
            first = std::min(first, r);
            last = r;
        }
    rows_out.clear();
    if (last < first) {
        row0 = 0;
        return;
    }
    row0 = first;
    rows_out.assign(rows.begin() + first, rows.begin() + last + 1);
}

MdGfx build_mdgfx(const Pak &pak, const MdGfxOptions &opt, std::string *log)
{
    MdGfx out;
    Palette pal = Palette::from(pak.get("gfx/palette.lmp"));
    static_tiles(pak, pal, opt, out.static_tiles, out.font_pal);
    for (int s = 0; s < 6; s++)
        check(opt.weapon_size[size_t(s)] >= s && opt.weapon_size[size_t(s)] < 6,
              "a screen size can only use a weapon render of its own size or smaller");
    // the renders in use
    // (style 1: the flat-shaded renders the game shows with textures off)
    const int styles = opt.flat_weapons ? 2 : 1;
    std::map<std::tuple<int, int, int>, size_t> set_of;
    for (int g = 0; g < styles; g++)
        for (int wi = 0; wi < int(MD_WEAPONS.size()); wi++)
            for (int s = 0; s < 6; s++) {
                int r = opt.weapon_size[size_t(s)];
                if (set_of.count({g, wi, r})) continue;
                auto ed = opt.edited.find({wi, r});
                MdWeaponSet set = g == 0 && ed != opt.edited.end()
                                      ? ed->second
                                      : render_weapon_set(pak, wi, r, opt.light_scale, opt.dither, g == 1);
                set_of[{g, wi, r}] = out.sets.size();
                out.sets.push_back(std::move(set));
                if (!opt.preview_dir.empty()) {
                    std::string base = MD_WEAPONS[size_t(wi)];
                    base = base.substr(base.find('/') + 1);
                    base = base.substr(0, base.size() - 4);
                    save_preview(opt.preview_dir, base + (g ? "_flat" : "") + "_s" + std::to_string(r), out.sets.back());
                }
            }

    Buf b(true);
    b.zeros(64);
    size_t hdr_tiles = b.add(out.static_tiles, 4);
    Buf pw(true);
    for (auto v : out.font_pal) pw.u16(v);
    pw.zeros(32);
    size_t hdr_pal = b.add(pw, 4);
    Buf table(true);
    size_t total = 0;
    int worst = 0;
    std::map<size_t, std::pair<size_t, int>> tiles_at;   // render -> (tiles ofs, tile count)
    for (int g = 0; g < styles; g++)
    for (int wi = 0; wi < int(MD_WEAPONS.size()); wi++)
        for (int s = 0; s < 6; s++) {
            size_t ri = set_of[{g, wi, opt.weapon_size[size_t(s)]}];
            const MdWeaponSet &set = out.sets[ri];
            int vw = MD_SIZES[size_t(s)][0], vh = MD_SIZES[size_t(s)][1];
            // the render placed in this view: centred, on the view's bottom edge
            int dx = vw - set.vw, dy = 2 * (vh - set.vh);
            int ox = 160 - vw + dx, oy = MD_VIEW_ROWS - vh + dy;
            std::vector<size_t> ftab, ctab;
            Bytes alltiles;
            for (size_t f = 0; f < set.frames.size(); f++) {
                std::vector<int> words;
                Bytes t;
                sprites_for(set, f, ox, oy, MD_GUN_TILE + int(alltiles.size() / 32), words, t);
                alltiles.insert(alltiles.end(), t.begin(), t.end());
                Buf fb(true);
                fb.u16(int64_t(words.size() / 4));
                for (int v : words) fb.u16(v);
                ftab.push_back(b.add(fb, 2));
                std::vector<uint8_t> canvas(static_cast<size_t>(4) * size_t(vw) * size_t(vh), 0);
                int sw2 = 2 * set.vw, sh2 = 2 * set.vh;
                for (int y = 0; y < sh2; y++)
                    for (int x = 0; x < sw2; x++) {
                        int cy = y + dy, cx = x + dx;
                        if (cy >= 0 && cx >= 0 && cy < 2 * vh && cx < 2 * vw)
                            canvas[size_t(cy) * size_t(2 * vw) + size_t(cx)] = set.frames[f][size_t(y) * size_t(sw2) + size_t(x)];
                    }
                int row0;
                std::vector<std::pair<int, int>> rows;
                cutout(canvas, vw, vh, row0, rows);
                Buf cb(true);
                cb.u8(row0);
                cb.u8(int64_t(rows.size()));
                for (auto &r : rows) {
                    cb.u8(r.first);
                    cb.u8(r.second);
                }
                ctab.push_back(b.add(cb, 2));
            }
            int ntiles = int(alltiles.size() / 32);
            check(ntiles <= MD_GUN_TILES_MAX, "weapon " + std::to_string(wi) + " size " + std::to_string(s) + ": " +
                                                  std::to_string(ntiles) + " tiles, more than the video memory holds");
            worst = std::max(worst, ntiles);
            size_t to;
            auto shared = tiles_at.find(ri);
            if (shared != tiles_at.end())
                to = shared->second.first;   // the same render: the same tiles
            else {
                if (opt.compress) {
                    // one packed stream (src-md/md.s unpacks it into video memory)
                    Bytes pk = md_pack(alltiles);
                    check(md_unpack(pk, alltiles.size()) == alltiles, "tile packing failed");
                    to = b.add(pk, 4) | 0x80000000u;
                    total += pk.size();
                } else {
                    to = b.add(alltiles, 4);
                    total += alltiles.size();
                }
                tiles_at[ri] = {to, ntiles};
            }
            Buf ft(true), ct(true), pt(true);
            for (auto o : ftab) ft.u32(int64_t(o));
            for (auto o : ctab) ct.u32(int64_t(o));
            for (auto v : set.pal) pt.u16(v);
            size_t fo = b.add(ft, 4), co = b.add(ct, 4), po = b.add(pt, 4);
            table.u32(int64_t(to));
            table.u16(ntiles);
            table.u16(int64_t(set.frames.size()));
            table.u32(int64_t(fo));
            table.u32(int64_t(po));
            table.u32(int64_t(co));
            table.zeros(12);
        }
    size_t hdr_sets = b.add(table, 4);
    Buf h(true);
    h.bytes(reinterpret_cast<const uint8_t *>("MDQ1"), 4);
    h.u32(int64_t(hdr_tiles));
    h.u32(MD_STATIC_TILES);
    h.u32(int64_t(hdr_pal));
    h.u32(int64_t(MD_WEAPONS.size() * 6 * size_t(styles)));
    h.u32(int64_t(hdr_sets));
    h.u32(6);
    h.u32(int64_t(MD_WEAPONS.size()));
    std::copy(h.data().begin(), h.data().end(), b.data().begin());
    out.data = b.data();
    // the 68000 sees the program's first 512 KB, and the data starts at 0x30000
    check(out.data.size() <= 0x50000, "the Mega Drive data is " + std::to_string(out.data.size() / 1024) +
                                          " KB, more than the 320 KB it has: pack the weapon tiles or share renders");
    out.weapon_bytes = total;
    out.largest_set = worst;
    if (log)
        *log += "md data: " + std::to_string(out.data.size()) + " bytes, " + std::to_string(MD_WEAPONS.size() * 6 * size_t(styles)) +
                " weapon sets, weapon tiles " + std::to_string(total / 1024) + " KB (largest set " +
                std::to_string(worst) + " tiles)\n";
    return out;
}

// ------------------------------------------------------------ tile packing
//
// A stream of tokens: a byte c < 0x80 is followed by c + 1 literal bytes; a
// byte c >= 0x80 copies (c & 0x7F) + 3 bytes from d bytes back, d the next
// big-endian word (1..4096). The unpacked size is known to the reader.

Bytes md_pack(const Bytes &in)
{
    Bytes out;
    size_t i = 0, lit_start = 0;
    auto flush = [&](size_t end) {
        while (lit_start < end) {
            size_t n = std::min<size_t>(128, end - lit_start);
            out.push_back(uint8_t(n - 1));
            out.insert(out.end(), in.begin() + std::ptrdiff_t(lit_start), in.begin() + std::ptrdiff_t(lit_start + n));
            lit_start += n;
        }
    };
    while (i < in.size()) {
        size_t best_len = 0, best_d = 0;
        size_t lo = i > 4096 ? i - 4096 : 0;   // (the 68000 keeps 4 KB of history)
        for (size_t j = i; j-- > lo;) {
            size_t l = 0;
            while (l < 130 && i + l < in.size() && in[j + l] == in[i + l]) l++;
            if (l > best_len) {
                best_len = l;
                best_d = i - j;
                if (l == 130) break;
            }
        }
        if (best_len >= 3) {
            flush(i);
            out.push_back(uint8_t(0x80 | (best_len - 3)));
            out.push_back(uint8_t(best_d >> 8));
            out.push_back(uint8_t(best_d));
            i += best_len;
            lit_start = i;
        } else
            i++;
    }
    flush(in.size());
    return out;
}

Bytes md_unpack(const Bytes &p, size_t size)
{
    Bytes out;
    size_t i = 0;
    while (out.size() < size) {
        check(i < p.size(), "packed tiles end early");
        int c = p[i++];
        if (c < 0x80) {
            size_t n = size_t(c) + 1;
            check(i + n <= p.size(), "packed tiles end early");
            out.insert(out.end(), p.begin() + std::ptrdiff_t(i), p.begin() + std::ptrdiff_t(i + n));
            i += n;
        } else {
            check(i + 2 <= p.size(), "packed tiles end early");
            size_t n = size_t(c & 0x7F) + 3, d = size_t(p[i]) << 8 | p[i + 1];
            i += 2;
            check(d >= 1 && d <= out.size(), "bad back-reference in packed tiles");
            for (size_t k = 0; k < n; k++) out.push_back(out[out.size() - d]);
        }
    }
    check(out.size() == size, "packed tiles unpack to the wrong size");
    return out;
}

}  // namespace q32x
