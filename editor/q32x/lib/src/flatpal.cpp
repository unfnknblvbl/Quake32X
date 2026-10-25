#include "q32x/flatpal.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <set>
#include <sstream>

#include "q32x/geom.h"
#include "q32x/image.h"

namespace q32x {

// ------------------------------------------------------------ palette files

static bool fail(std::string *err, const std::string &s)
{
    if (err) *err = s;
    return false;
}

static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool parse_palette(const Bytes &data, const std::string &name, std::array<uint8_t, 768> &out, std::string *err)
{
    out.fill(0);
    std::string ext = lower(name);
    size_t dot = ext.rfind('.');
    ext = dot == std::string::npos ? "" : ext.substr(dot + 1);
    std::vector<std::array<int, 3>> cols;
    auto finish = [&](const std::string &what) {
        if (cols.empty()) return fail(err, what + ": no colours found");
        if (cols.size() > 256) cols.resize(256);
        for (size_t i = 0; i < cols.size(); i++)
            for (size_t k = 0; k < 3; k++) out[3 * i + k] = uint8_t(std::clamp(cols[i][k], 0, 255));
        return true;
    };
    if (ext == "png") {
        RgbImage im;
        try {
            im = decode_png(data);
        } catch (std::exception &e) {
            return fail(err, std::string("not a PNG this can read: ") + e.what());
        }
        auto px = [&](int x, int y) {
            size_t o = (size_t(y) * size_t(im.w) + size_t(x)) * 3;
            return std::array<int, 3>{im.px[o], im.px[o + 1], im.px[o + 2]};
        };
        std::set<std::array<int, 3>> seen;
        std::vector<std::array<int, 3>> distinct;
        for (int y = 0; y < im.h; y++)
            for (int x = 0; x < im.w; x++) {
                auto c = px(x, y);
                if (seen.insert(c).second) distinct.push_back(c);
                if (distinct.size() > 4096) break;
            }
        // a 16x16 grid of plain swatches: in grid order (repeated colours kept)
        bool grid = im.w % 16 == 0 && im.h % 16 == 0 && im.w >= 16 && im.h >= 16;
        if (grid) {
            int cw = im.w / 16, ch = im.h / 16;
            for (int y = 0; y < 16 && grid; y++)
                for (int x = 0; x < 16 && grid; x++) {
                    auto c = px(x * cw + cw / 2, y * ch + ch / 2);
                    if (px(x * cw + cw / 4, y * ch + ch / 4) != c || px(x * cw + (3 * cw) / 4, y * ch + (3 * ch) / 4) != c) grid = false;
                }
        }
        if (grid) {
            int cw = im.w / 16, ch = im.h / 16;
            for (int y = 0; y < 16; y++)
                for (int x = 0; x < 16; x++) cols.push_back(px(x * cw + cw / 2, y * ch + ch / 2));
        } else if (distinct.size() <= 256) {
            cols = distinct;
        } else if (im.w % 16 == 0 && im.h % 16 == 0) {
            // a 16x16 grid of swatches (with labels or borders): their centres
            int cw = im.w / 16, ch = im.h / 16;
            for (int y = 0; y < 16; y++)
                for (int x = 0; x < 16; x++) cols.push_back(px(x * cw + cw / 2, y * ch + ch / 2));
        } else {
            return fail(err, "the picture has more than 256 colours and is not a 16x16 grid of swatches");
        }
        return finish("PNG");
    }
    std::string text(data.begin(), data.end());
    if (starts_with(text, "JASC-PAL")) {
        std::istringstream in(text);
        std::string line;
        int n = 0, ln = 0;
        while (std::getline(in, line)) {
            ln++;
            if (ln == 1 || ln == 2) continue;
            if (ln == 3) {
                n = std::atoi(line.c_str());
                continue;
            }
            auto w = split_ws(line);
            if (w.size() >= 3) cols.push_back({std::atoi(w[0].c_str()), std::atoi(w[1].c_str()), std::atoi(w[2].c_str())});
            if (n && int(cols.size()) >= n) break;
        }
        return finish("JASC palette");
    }
    if (starts_with(text, "GIMP Palette")) {
        std::istringstream in(text);
        std::string line;
        std::getline(in, line);
        while (std::getline(in, line)) {
            std::string s = strip(line);
            if (s.empty() || s[0] == '#' || starts_with(s, "Name:") || starts_with(s, "Columns:")) continue;
            auto w = split_ws(s);
            if (w.size() >= 3 && std::isdigit(uint8_t(w[0][0])))
                cols.push_back({std::atoi(w[0].c_str()), std::atoi(w[1].c_str()), std::atoi(w[2].c_str())});
        }
        return finish("GIMP palette");
    }
    if (ext == "hex" || ext == "txt") {
        // one RRGGBB a line (Lospec's .hex), '#' allowed
        std::istringstream in(text);
        std::string line;
        while (std::getline(in, line)) {
            std::string s = strip(line);
            if (!s.empty() && s[0] == '#') s = s.substr(1);
            if (s.size() < 6) continue;
            int v[6];
            bool ok = true;
            for (int k = 0; k < 6; k++)
                if ((v[k] = hexval(s[size_t(k)])) < 0) ok = false;
            if (ok) cols.push_back({v[0] * 16 + v[1], v[2] * 16 + v[3], v[4] * 16 + v[5]});
        }
        return finish("hex palette");
    }
    if (data.size() == 768 || data.size() == 772) {
        // Photoshop .act (with an optional count) or Quake's palette.lmp
        size_t n = 256;
        if (data.size() == 772) {
            n = size_t(data[768]) << 8 | data[769];
            if (n == 0 || n > 256) n = 256;
        }
        for (size_t i = 0; i < n; i++) cols.push_back({data[3 * i], data[3 * i + 1], data[3 * i + 2]});
        return finish("palette");
    }
    return fail(err, "not a palette this can read (JASC or GIMP .pal/.gpl, .hex, .act, .lmp or .png)");
}

bool builtin_palette(const std::string &name, std::array<uint8_t, 768> &out)
{
    std::string n = lower(name);
    if (n == "grey" || n == "gray") {
        for (int i = 0; i < 256; i++) {
            int k = i >> 3;                       // 0..31
            uint8_t v = uint8_t(k << 3 | k >> 2); // as the 32X shows it
            out[size_t(3 * i)] = out[size_t(3 * i + 1)] = out[size_t(3 * i + 2)] = v;
        }
        return true;
    }
    return false;
}

bool load_palette_file(const std::string &path, std::array<uint8_t, 768> &out, std::string *err)
{
    Bytes b;
    try {
        b = read_file(path);
    } catch (std::exception &e) {
        return fail(err, e.what());
    }
    return parse_palette(b, path, out, err);
}

// ------------------------------------------------------------ colour

// (only +, -, *, / and sqrt, so every machine gives the same colours)
static double cbrt_det(double x)
{
    if (x <= 0) return 0;
    double y = x > 1 ? x : 1;
    for (int i = 0; i < 80; i++) y = (2 * y + x / (y * y)) / 3;
    return y;
}

// OKLab, from gamma-2 linear light
static std::array<double, 3> to_lab(const Rgb &c)
{
    double r = c[0] / 255, g = c[1] / 255, b = c[2] / 255;
    r *= r;
    g *= g;
    b *= b;
    double l = cbrt_det(0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * b);
    double m = cbrt_det(0.2119034982 * r + 0.6806995451 * g + 0.1073969566 * b);
    double s = cbrt_det(0.0883024619 * r + 0.2817188376 * g + 0.6299787005 * b);
    return {0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s, 1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s,
            0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s};
}

static double cos_det(double x)
{
    // |x| up to pi: a Taylor series
    double x2 = x * x, term = 1, sum = 1;
    for (int k = 1; k < 20; k++) {
        term *= -x2 / double((2 * k - 1) * (2 * k));
        sum += term;
    }
    return sum;
}

ColourMatch::ColourMatch(const std::array<uint8_t, 768> &rgb) : rgb_(rgb)
{
    for (int i = 0; i < 256; i++) lab_.push_back(to_lab(this->rgb(i)));
}

int ColourMatch::nearest(const Rgb &c) const
{
    Rgb q = {std::clamp(c[0], 0.0, 255.0), std::clamp(c[1], 0.0, 255.0), std::clamp(c[2], 0.0, 255.0)};
    auto L = to_lab(q);
    double best = 1e300;
    int bi = 0;
    for (int i = 0; i < 256; i++) {
        double d0 = L[0] - lab_[size_t(i)][0], d1 = L[1] - lab_[size_t(i)][1], d2 = L[2] - lab_[size_t(i)][2];
        double d = d0 * d0 + d1 * d1 + d2 * d2;
        if (d < best) {
            best = d;
            bi = i;
        }
    }
    return bi;
}

struct Hsv {
    double h, s, v;   // h in 0..6
};

static Hsv to_hsv(const Rgb &c)
{
    double r = c[0] / 255, g = c[1] / 255, b = c[2] / 255;
    double mx = std::max({r, g, b}), mn = std::min({r, g, b});
    Hsv o{0, mx > 0 ? (mx - mn) / mx : 0, mx};
    if (mx > mn) {
        double d = mx - mn;
        if (mx == r)
            o.h = std::fmod((g - b) / d + 6, 6);
        else if (mx == g)
            o.h = (b - r) / d + 2;
        else
            o.h = (r - g) / d + 4;
    }
    return o;
}

static Rgb from_hsv(Hsv x)
{
    double h = std::fmod(std::fmod(x.h, 6) + 6, 6);
    double C = x.v * x.s, X = C * (1 - std::fabs(std::fmod(h, 2) - 1)), m = x.v - C;
    double rr = 0, gg = 0, bb = 0;
    switch (int(h) % 6) {
    case 0: rr = C, gg = X; break;
    case 1: rr = X, gg = C; break;
    case 2: gg = C, bb = X; break;
    case 3: gg = X, bb = C; break;
    case 4: rr = X, bb = C; break;
    default: rr = C, bb = X; break;
    }
    return {(rr + m) * 255, (gg + m) * 255, (bb + m) * 255};
}

Rgb vivid(const Rgb &c, double a)
{
    a = std::clamp(a, 0.0, 1.0);
    Hsv x = to_hsv(c);
    // greys stay grey; the rest go toward their full colour; dark colours
    // lifted, keeping their order
    x.s = x.s + (1 - x.s) * a * std::min(1.0, x.s * 4);
    x.v = std::min(1.0, x.v + a * x.v * (1 - x.v) * 1.2);
    return from_hsv(x);
}

std::vector<Rgb> spread_hues(const std::vector<Rgb> &cols, const std::vector<double> &weights, double amount)
{
    amount = std::clamp(amount, 0.0, 1.0);
    std::vector<Rgb> out = cols;
    if (amount <= 0) return out;
    std::vector<Hsv> hs;
    std::vector<size_t> chroma;
    double total = 0;
    for (size_t i = 0; i < cols.size(); i++) {
        hs.push_back(to_hsv(cols[i]));
        if (hs[i].s >= 0.2 && hs[i].v >= 0.08 && weights[i] > 0) {
            chroma.push_back(i);
            total += weights[i];
        }
    }
    if (chroma.size() < 2) return out;
    std::stable_sort(chroma.begin(), chroma.end(), [&](size_t x, size_t y) { return hs[x].h < hs[y].h; });
    double h0 = hs[chroma[0]].h, cum = 0;
    for (size_t i : chroma) {
        double pos = (cum + weights[i] / 2) / total;
        cum += weights[i];
        double target = h0 + pos * 6, d = std::fmod(target - hs[i].h + 9, 6) - 3;   // the short way round
        Hsv x = hs[i];
        x.h = hs[i].h + d * amount;
        out[i] = from_hsv(x);
    }
    return out;
}

Rgb texture_average(const BspTexture &t, const Palette &pal)
{
    const Bytes &mip = t.mips[3];
    double rs = 0, gs = 0, bs = 0;
    for (uint8_t c : mip) {
        rs += pal.rgb[3 * c];
        gs += pal.rgb[3 * c + 1];
        bs += pal.rgb[3 * c + 2];
    }
    double n = double(std::max<size_t>(1, mip.size()));
    return {rs / n, gs / n, bs / n};
}

std::vector<int> flatpal_texture_colours(const Bsp &m, const std::vector<Rgb> &tex_rgb, const FlatPalOptions &o,
                                         std::vector<Rgb> *chosen)
{
    const auto &textures = m.textures;
    ColourMatch cmatch(o.rgb);
    auto own = [&](size_t i) {   // skies and liquids keep their own colours
        return textures[i] && (starts_with(textures[i]->name, "sky") || starts_with(textures[i]->name, "*"));
    };
    // each texture's room on the colour wheel: by how much of the level it covers
    std::vector<double> weight(textures.size(), 0);
    if (!m.models.empty())
        for (int fi = 0; fi < m.models[0].numfaces && fi < int(m.faces.size()); fi++) {
            const BspFace &f = m.faces[size_t(fi)];
            size_t t = size_t(m.texinfo.at(size_t(f.texinfo)).miptex);
            if (t < weight.size()) weight[t] += winding_area(m.face_polygon(f));
        }
    // Quake's textures are dark (its light maps brighten them up to twice):
    // the level's textures brightened together, to an average of about two thirds
    double vsum = 0, wsum = 0;
    for (size_t i = 0; i < textures.size() && i < tex_rgb.size(); i++) {
        if (!textures[i] || own(i)) continue;
        vsum += std::max({tex_rgb[i][0], tex_rgb[i][1], tex_rgb[i][2]}) / 255 * weight[i];
        wsum += weight[i];
    }
    double gain = wsum > 0 && vsum > 0 ? std::clamp(0.65 / (vsum / wsum), 1.0, 4.0) : 1.0;
    std::vector<Rgb> viv;
    for (size_t i = 0; i < textures.size(); i++) {
        Rgb c = i < tex_rgb.size() ? tex_rgb[i] : Rgb{0, 0, 0};
        if (!own(i))
            for (auto &k : c) k = std::min(255.0, k * gain);
        viv.push_back(vivid(c, o.vividness));
        weight[i] = own(i) ? 0 : std::sqrt(weight[i]);
    }
    viv = spread_hues(viv, weight, o.variety);
    std::vector<int> out;
    if (o.special_kinds)
        for (size_t i = 0; i < textures.size(); i++) {
            if (!textures[i]) continue;
            const std::string &n = textures[i]->name;
            Rgb k{-1, 0, 0};
            if (starts_with(n, "sky"))
                k = {96, 88, 216};
            else if (starts_with(n, "*"))
                k = n.find("lava") != std::string::npos    ? Rgb{232, 64, 16}
                    : n.find("slime") != std::string::npos ? Rgb{160, 208, 32}
                    : n.find("tele") != std::string::npos  ? Rgb{152, 64, 216}
                                                           : Rgb{40, 152, 144};
            if (k[0] >= 0) viv[i] = k;
        }
    for (auto &c : viv) out.push_back(cmatch.nearest(c));
    if (chosen) *chosen = viv;
    return out;
}

Rgb shade_rgb(const Rgb &c, int row)
{
    double f = (64 - std::clamp(row, 0, 63)) / 32.0;
    Rgb o;
    for (size_t k = 0; k < 3; k++) {
        if (f <= 1)
            o[k] = c[k] * f;
        else {
            double e = f - 1, v = std::min(255.0, c[k] * f);
            o[k] = v + (255 - v) * e * 0.3;
        }
    }
    return o;
}

FlatPalTables build_flatpal_tables(const FlatPalOptions &o, const Palette &quake, const Bytes &quake_colormap)
{
    (void)quake_colormap;
    FlatPalTables t;
    ColourMatch cm(o.rgb);
    for (int i = 0; i < 256; i++) {
        int r = o.rgb[size_t(3 * i)], g = o.rgb[size_t(3 * i + 1)], b = o.rgb[size_t(3 * i + 2)];
        t.cram[size_t(i)] = uint16_t(((b >> 3) << 10) | ((g >> 3) << 5) | (r >> 3));
    }
    t.cmap.assign(64 * 256, 0);
    t.ucm.assign(64 * 256, 0);
    for (int q = 0; q < 256; q++) {
        Rgb c = vivid({double(quake.rgb[3 * q]), double(quake.rgb[3 * q + 1]), double(quake.rgb[3 * q + 2])}, o.vividness);
        t.remap[size_t(q)] = uint8_t(cm.nearest(c));
        for (int row = 0; row < 64; row++)
            // (Quake's last 32 colours are full bright: the same at every light level)
            t.cmap[size_t(row * 256 + q)] = uint8_t(q >= 224 ? t.remap[size_t(q)] : cm.nearest(shade_rgb(c, row)));
    }
    for (int u = 0; u < 256; u++)
        for (int row = 0; row < 64; row++)
            t.ucm[size_t(row * 256 + u)] = uint8_t(row == 32 ? u : cm.nearest(shade_rgb(cm.rgb(u), row)));
    return t;
}

// ------------------------------------------------------------ shading

static Vec3 add(const Vec3 &a, const Vec3 &b) { return {a[0] + b[0], a[1] + b[1], a[2] + b[2]}; }
static Vec3 mul(const Vec3 &a, double k) { return {a[0] * k, a[1] * k, a[2] * k}; }
static double len(const Vec3 &a) { return std::sqrt(dot(a, a)); }
static Vec3 norm(const Vec3 &a)
{
    double l = len(a);
    return l > 0 ? mul(a, 1 / l) : Vec3{0, 0, 1};
}

FlatShader::FlatShader(const Bsp &m, const std::vector<Entity> &ents, const FlatPalOptions &o) : m_(m), o_(o)
{
    std::map<std::string, Vec3> targets;
    for (auto &e : ents)
        if (e.has("targetname") && e.has("origin")) targets[e.get("targetname")] = e.vec("origin");
    for (auto &e : ents) {
        std::string cls = e.classname();
        if (starts_with(cls, "light") && e.has("origin")) {
            Light l;
            l.org = e.vec("origin");
            l.value = e.num("light", e.num("_light", 300));
            l.wait = e.num("wait", 1);
            if (l.wait <= 0) l.wait = 1;
            l.delay = int(e.num("delay", 0));
            if (e.has("target") && targets.count(e.get("target"))) {
                l.spot = true;
                l.dir = norm(sub(targets[e.get("target")], l.org));
                double ang = e.num("angle", 0);
                if (ang <= 0) ang = 20;
                l.cone = cos_det(std::min(ang, 360.0) / 2 * 3.14159265358979323846 / 180);
            }
            if (l.value > 0) lights_.push_back(l);
        }
        std::string mdl = e.get("model");
        if (starts_with(mdl, "*") && starts_with(cls, "func_") && cls != "func_illusionary") {
            int mi = std::atoi(mdl.c_str() + 1);
            if (mi > 0 && mi < int(m.models.size())) solid_models_.push_back(mi);
        }
    }
    int K = std::clamp(o.rays, 4, 256);
    // a Fibonacci spiral, turned by the golden angle (as a rotation, not cos and sin)
    const double gc = -0.73736887807831963, gs = 0.67549029426152365;
    double cx = 1, cy = 0;
    for (int i = 0; i < K; i++) {
        double u = (i + 0.5) / K, r = std::sqrt(u);
        dirs_.push_back({r * cx, r * cy, std::sqrt(1 - u)});
        double nx = cx * gc - cy * gs, ny = cx * gs + cy * gc;
        cx = nx;
        cy = ny;
    }
}

// the first solid (or sky) point on a to b, through the tree from `node`
static bool trace_node(const Bsp &m, int node, const Vec3 &a, const Vec3 &b, double ta, double tb, double &t, bool &sky)
{
    while (node >= 0) {
        const BspNode &n = m.nodes[size_t(node)];
        const BspPlane &pl = m.planes[size_t(n.plane)];
        double da = a[0] * pl.n[0] + a[1] * pl.n[1] + a[2] * pl.n[2] - pl.d;
        double db = b[0] * pl.n[0] + b[1] * pl.n[1] + b[2] * pl.n[2] - pl.d;
        if (da >= 0 && db >= 0) {
            node = n.child[0];
            continue;
        }
        if (da < 0 && db < 0) {
            node = n.child[1];
            continue;
        }
        double f = da / (da - db);
        Vec3 mid = {a[0] + (b[0] - a[0]) * f, a[1] + (b[1] - a[1]) * f, a[2] + (b[2] - a[2]) * f};
        double tm = ta + (tb - ta) * f;
        int side = da < 0;
        if (trace_node(m, n.child[side], a, mid, ta, tm, t, sky)) return true;
        return trace_node(m, n.child[side ^ 1], mid, b, tm, tb, t, sky);
    }
    int c = m.leafs[size_t(-1 - node)].contents;
    if (c == CONTENTS_SOLID || c == CONTENTS_SKY) {
        t = ta;
        sky = c == CONTENTS_SKY;
        return true;
    }
    return false;
}

bool FlatShader::blocked(const Vec3 &a, const Vec3 &b, double *frac, bool *sky) const
{
    double t = 1;
    bool s = false, hit = false;
    double tt;
    bool ss;
    if (trace_node(m_, m_.models[0].headnode[0], a, b, 0, 1, tt, ss)) {
        t = tt;
        s = ss;
        hit = true;
    }
    for (int mi : solid_models_) {
        const BspModel &mo = m_.models[size_t(mi)];
        bool out = false;
        for (int k = 0; k < 3; k++)
            if (std::max(a[size_t(k)], b[size_t(k)]) < mo.mins[k] - 1 || std::min(a[size_t(k)], b[size_t(k)]) > mo.maxs[k] + 1) out = true;
        if (out) continue;
        if (trace_node(m_, mo.headnode[0], a, b, 0, 1, tt, ss) && tt < t) {
            t = tt;
            s = false;
            hit = true;
        }
    }
    if (frac) *frac = t;
    if (sky) *sky = s;
    return hit;
}

std::vector<int> FlatShader::lights_near(const std::vector<Vec3> &poly, const Vec3 &n) const
{
    std::vector<int> out;
    if (poly.empty()) return out;
    Vec3 c{0, 0, 0};
    for (auto &p : poly) c = add(c, p);
    c = mul(c, 1.0 / double(poly.size()));
    double rad = 0;
    for (auto &p : poly) rad = std::max(rad, len(sub(p, c)));
    double pd = dot(n, poly[0]);
    for (size_t i = 0; i < lights_.size(); i++) {
        const Light &l = lights_[i];
        if (dot(n, l.org) - pd < 0.25) continue;   // behind the face
        double reach = l.delay == 0 ? l.value / l.wait : l.delay == 3 ? 1e9 : 4096;
        if (len(sub(l.org, c)) > reach + rad) continue;
        out.push_back(int(i));
    }
    return out;
}

double FlatShader::light_at(const Vec3 &p, const Vec3 &n, const std::vector<int> &lights, const Vec3 &tan) const
{
    Vec3 p0 = add(p, mul(n, 1.0));
    double value = 0;
    // occlusion: how much of the open space around, weighted toward up
    if (o_.ambient > 0) {
        Vec3 bt = cross(n, tan);
        double bias = std::clamp(o_.up_bias, 0.0, 1.0);
        double sum = 0;
        for (auto &d : dirs_) {
            Vec3 w = add(add(mul(tan, d[0]), mul(bt, d[1])), mul(n, d[2]));
            double f;
            bool sky;
            double vis = 1;
            if (blocked(p0, add(p0, mul(w, o_.reach)), &f, &sky) && !sky) vis = f;
            sum += (1 + bias * w[2]) * vis;
        }
        double ao = sum / (double(dirs_.size()) * (1 + bias * 2.0 / 3.0));
        double occ = 1 - std::clamp(o_.occlusion, 0.0, 1.0) * (1 - std::min(1.0, ao));
        if (o_.occlusion <= 0) occ = (1 + bias * n[2] * 2.0 / 3.0) / (1 + bias * 2.0 / 3.0);
        value += o_.ambient * occ;
    }
    if (o_.lights > 0) {
        double direct = 0;
        for (int li : lights) {
            const Light &l = lights_[size_t(li)];
            Vec3 dv = sub(l.org, p0);
            double dist = len(dv);
            if (dist < 1) dist = 1;
            Vec3 inc = mul(dv, 1 / dist);
            double cosn = dot(inc, n);
            if (cosn <= 0) continue;
            if (l.spot && -dot(inc, l.dir) < l.cone) continue;
            double add_ = l.delay == 0   ? l.value - dist * l.wait
                          : l.delay == 1 ? l.value * 128 / dist
                          : l.delay == 2 ? l.value * 16384 / (dist * dist)
                                         : l.value;
            add_ *= 0.5 + 0.5 * cosn;
            if (add_ <= 0) continue;
            double f;
            Vec3 end = sub(l.org, mul(inc, 1.0));
            if (blocked(p0, end, &f, nullptr)) continue;
            direct += add_;
        }
        value += o_.lights * direct;
    }
    return value;
}

// q (on the face's plane) moved inside the face if it lies outside
Vec3 FlatShader::inside(const std::vector<Vec3> &poly, const Vec3 &n, const Vec3 &c, const Vec3 &q) const
{
    size_t k = poly.size();
    bool in = true;
    for (size_t i = 0; i < k && in; i++) {
        const Vec3 &a = poly[i], &b = poly[(i + 1) % k];
        Vec3 e = sub(b, a);
        double sc = dot(cross(e, sub(c, a)), n), sq = dot(cross(e, sub(q, a)), n);
        if ((sc > 0) != (sq > 0) && std::fabs(sq) > 1e-9) in = false;
    }
    if (in) return q;
    // the nearest point of the outline, a little way in
    double best = 1e300;
    Vec3 bp = c;
    for (size_t i = 0; i < k; i++) {
        const Vec3 &a = poly[i], &b = poly[(i + 1) % k];
        Vec3 e = sub(b, a);
        double l2 = dot(e, e), t = l2 > 0 ? std::clamp(dot(sub(q, a), e) / l2, 0.0, 1.0) : 0;
        Vec3 p = add(a, mul(e, t));
        double d = len(sub(p, q));
        if (d < best) {
            best = d;
            bp = p;
        }
    }
    Vec3 to_c = sub(c, bp);
    double l = len(to_c);
    return l > 1 ? add(bp, mul(to_c, 1.0 / l)) : c;
}

// light (Quake units) as the converter's dark units, rolled off softly
// toward the brightest: full light is the colour itself (or, with
// highlight, up to twice as bright)
uint8_t FlatShader::to_dark(double v) const
{
    double vmax = 127 + std::clamp(o_.highlight, 0.0, 1.0) * 127, knee = 0.7 * vmax;
    if (v > knee) v = knee + (vmax - knee) * (1 - 1 / (1 + (v - knee) / (vmax - knee)));
    v = std::clamp(v, 0.0, vmax);
    return uint8_t(std::clamp(std::lround(255 - v), 1L, 255L));
}

static Vec3 tangent_of(const Vec3 &n)
{
    Vec3 a = std::fabs(n[2]) < 0.9 ? Vec3{0, 0, 1} : Vec3{1, 0, 0};
    return norm(cross(a, n));
}

std::vector<uint8_t> FlatShader::light_map(const std::vector<Vec3> &poly, const Vec3 &n, const double vecs[8],
                                           const int64_t tmins[2], int smax, int tmax, const std::vector<uint8_t> &quake) const
{
    std::vector<uint8_t> out(size_t(smax) * size_t(tmax), 128);
    if (poly.size() < 3) return out;
    auto lights = lights_near(poly, n);
    Vec3 c{0, 0, 0};
    for (auto &p : poly) c = add(c, p);
    c = mul(c, 1.0 / double(poly.size()));
    Vec3 tan = tangent_of(n);
    // a point from texture coordinates: s = vs.p + so, t = vt.p + to, n.p = d
    Vec3 vs = {vecs[0], vecs[1], vecs[2]}, vt = {vecs[4], vecs[5], vecs[6]};
    double pd = dot(n, poly[0]);
    double M[3][3] = {{vs[0], vs[1], vs[2]}, {vt[0], vt[1], vt[2]}, {n[0], n[1], n[2]}};
    double det = M[0][0] * (M[1][1] * M[2][2] - M[1][2] * M[2][1]) - M[0][1] * (M[1][0] * M[2][2] - M[1][2] * M[2][0]) +
                 M[0][2] * (M[1][0] * M[2][1] - M[1][1] * M[2][0]);
    std::vector<double> val(out.size(), 0);
    for (int t = 0; t < tmax; t++)
        for (int s = 0; s < smax; s++) {
            Vec3 q = c;
            if (std::fabs(det) > 1e-12) {
                double rhs[3] = {double(tmins[0] + 16 * s) - vecs[3], double(tmins[1] + 16 * t) - vecs[7], pd};
                double x[3];
                for (int k = 0; k < 3; k++) {
                    double A[3][3];
                    std::memcpy(A, M, sizeof A);
                    for (int r = 0; r < 3; r++) A[r][k] = rhs[r];
                    x[k] = (A[0][0] * (A[1][1] * A[2][2] - A[1][2] * A[2][1]) - A[0][1] * (A[1][0] * A[2][2] - A[1][2] * A[2][0]) +
                            A[0][2] * (A[1][0] * A[2][1] - A[1][1] * A[2][0])) /
                           det;
                }
                q = inside(poly, n, c, {x[0], x[1], x[2]});
            }
            val[size_t(t * smax + s)] = light_at(q, n, lights, tan);
        }
    // a little smoothing: occlusion is sampled, and bands want calm edges
    std::vector<double> sm(val.size());
    for (int t = 0; t < tmax; t++)
        for (int s = 0; s < smax; s++) {
            double acc = 0, w = 0;
            for (int dt = -1; dt <= 1; dt++)
                for (int ds = -1; ds <= 1; ds++) {
                    int ss = s + ds, tt = t + dt;
                    if (ss < 0 || tt < 0 || ss >= smax || tt >= tmax) continue;
                    double k = (ds == 0 && dt == 0) ? 4 : (ds == 0 || dt == 0) ? 2 : 1;
                    acc += k * val[size_t(tt * smax + ss)];
                    w += k;
                }
            sm[size_t(t * smax + s)] = acc / w;
        }
    double mix = quake.size() == out.size() ? std::clamp(o_.lightmaps, 0.0, 1.0) : 0;
    for (size_t i = 0; i < out.size(); i++) {
        double v = sm[i];
        if (mix > 0) v = (1 - mix) * v + mix * (255.0 - quake[i]);
        out[i] = to_dark(v);
    }
    return out;
}

int FlatShader::poly_light(const std::vector<Vec3> &poly, const Vec3 &n) const
{
    if (poly.size() < 3) return 128;
    auto lights = lights_near(poly, n);
    Vec3 c{0, 0, 0};
    for (auto &p : poly) c = add(c, p);
    c = mul(c, 1.0 / double(poly.size()));
    Vec3 tan = tangent_of(n);
    double acc = light_at(c, n, lights, tan);
    int cnt = 1;
    for (auto &p : poly) {
        acc += light_at(add(c, mul(sub(p, c), 0.6)), n, lights, tan);
        cnt++;
    }
    return to_dark(acc / cnt);
}

}  // namespace q32x
