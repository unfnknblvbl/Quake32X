#include "q32x/texreduce.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace q32x {

static double dist2(const Palette &pal, int c, double r, double g, double b)
{
    double dr = pal.rgb[3 * c] - r, dg = pal.rgb[3 * c + 1] - g, db = pal.rgb[3 * c + 2] - b;
    return dr * dr * 3 + dg * dg * 4 + db * db * 2;
}

static int nearest_of(const Palette &pal, const std::vector<int> &allowed, double r, double g, double b)
{
    int best = allowed.empty() ? 0 : allowed[0];
    double bd = 1e300;
    for (int c : allowed) {
        double d = dist2(pal, c, r, g, b);
        if (d < bd) {
            bd = d;
            best = c;
        }
    }
    return best;
}

ReducedTexture auto_reduce(const BspTexture &t, const Palette &pal, int min_mip, bool four_bit)
{
    ReducedTexture r;
    r.min_mip = std::clamp(min_mip, 0, 3);
    r.four_bit = four_bit;
    const Bytes &src = t.mips[r.min_mip];
    if (!four_bit) {
        r.pixels = src;
        return r;
    }
    // the colours used, by count
    std::map<int, int> cnt;
    for (uint8_t c : src) cnt[c]++;
    std::vector<int> pal16;
    if (cnt.size() <= 16) {
        for (auto &kv : cnt) pal16.push_back(kv.first);
    } else {
        // weighted k-means in RGB, starting from the most used colours
        std::vector<std::pair<int, int>> byc(cnt.begin(), cnt.end());
        std::stable_sort(byc.begin(), byc.end(), [](auto &a, auto &b) { return a.second > b.second; });
        std::vector<std::array<double, 3>> cent;
        for (size_t i = 0; i < 16; i++) {
            int c = byc[i].first;
            cent.push_back({double(pal.rgb[3 * c]), double(pal.rgb[3 * c + 1]), double(pal.rgb[3 * c + 2])});
        }
        for (int it = 0; it < 30; it++) {
            std::vector<std::array<double, 4>> acc(16, {0, 0, 0, 0});
            for (auto &kv : cnt) {
                double rr = pal.rgb[3 * kv.first], gg = pal.rgb[3 * kv.first + 1], bb = pal.rgb[3 * kv.first + 2];
                size_t best = 0;
                double bd = 1e300;
                for (size_t k = 0; k < 16; k++) {
                    double d = (rr - cent[k][0]) * (rr - cent[k][0]) * 3 + (gg - cent[k][1]) * (gg - cent[k][1]) * 4 +
                               (bb - cent[k][2]) * (bb - cent[k][2]) * 2;
                    if (d < bd) {
                        bd = d;
                        best = k;
                    }
                }
                acc[best][0] += rr * kv.second;
                acc[best][1] += gg * kv.second;
                acc[best][2] += bb * kv.second;
                acc[best][3] += kv.second;
            }
            for (size_t k = 0; k < 16; k++)
                if (acc[k][3] > 0)
                    for (int c = 0; c < 3; c++) cent[k][size_t(c)] = acc[k][size_t(c)] / acc[k][3];
        }
        // each centre becomes the nearest palette colour the texture uses
        std::vector<int> used;
        for (auto &kv : cnt) used.push_back(kv.first);
        for (auto &c : cent) {
            int p = nearest_of(pal, used, c[0], c[1], c[2]);
            if (std::find(pal16.begin(), pal16.end(), p) == pal16.end()) pal16.push_back(p);
        }
    }
    while (pal16.size() < 16) pal16.push_back(pal16.empty() ? 0 : pal16.back());
    for (int k = 0; k < 16; k++) r.pal[size_t(k)] = uint8_t(pal16[size_t(k)]);
    std::vector<int> allowed(pal16.begin(), pal16.end());
    r.pixels.resize(src.size());
    for (size_t i = 0; i < src.size(); i++) {
        int c = src[i];
        int p = nearest_of(pal, allowed, pal.rgb[3 * c], pal.rgb[3 * c + 1], pal.rgb[3 * c + 2]);
        r.pixels[i] = uint8_t(std::find(pal16.begin(), pal16.end(), p) - pal16.begin());
    }
    return r;
}

Bytes downsample(const Bytes &px, int w, int h, const Palette &pal, const std::vector<int> &allowed)
{
    int w2 = std::max(1, w / 2), h2 = std::max(1, h / 2);
    Bytes out(size_t(w2) * size_t(h2));
    for (int y = 0; y < h2; y++)
        for (int x = 0; x < w2; x++) {
            double r = 0, g = 0, b = 0;
            int n = 0;
            for (int dy = 0; dy < 2; dy++)
                for (int dx = 0; dx < 2; dx++) {
                    int sx = std::min(w - 1, 2 * x + dx), sy = std::min(h - 1, 2 * y + dy);
                    int c = px[size_t(sy) * size_t(w) + size_t(sx)];
                    r += pal.rgb[3 * c];
                    g += pal.rgb[3 * c + 1];
                    b += pal.rgb[3 * c + 2];
                    n++;
                }
            out[size_t(y) * size_t(w2) + size_t(x)] = uint8_t(nearest_of(pal, allowed, r / n, g / n, b / n));
        }
    return out;
}

std::vector<Bytes> reduced_levels(const BspTexture &t, const ReducedTexture &r, const Palette &pal)
{
    std::vector<Bytes> lv(4);
    int m = std::clamp(r.min_mip, 0, 3);
    int w = t.w >> m, h = t.h >> m;
    Bytes base = r.pixels;
    if (base.size() != size_t(w) * size_t(h)) base = t.mips[m];   // malformed: the original level
    std::vector<int> allowed;
    if (r.four_bit) {
        for (auto c : r.pal) allowed.push_back(c);
        for (auto &p : base) p = r.pal[p & 15];
    } else
        for (int c = 0; c < 256; c++) allowed.push_back(c);
    lv[size_t(m)] = base;
    for (int k = m + 1; k < 4; k++) {
        lv[size_t(k)] = downsample(lv[size_t(k) - 1], w, h, pal, allowed);
        w = std::max(1, w / 2);
        h = std::max(1, h / 2);
    }
    return lv;
}

}  // namespace q32x
