#include "q32x/lightpoly.h"

#include <algorithm>
#include <cmath>
#include <map>

#include "q32x/geom.h"

namespace q32x {

BandSet choose_bands(const std::vector<const LightFace *> &faces, int bands, double contrast)
{
    bands = std::clamp(bands, 1, 8);
    std::vector<double> hist(64, 0);
    for (auto *f : faces)
        for (uint8_t l : f->L) hist[size_t(l >> 2)] += 1;
    double total = 0, mean = 0;
    for (int r = 0; r < 64; r++) {
        total += hist[size_t(r)];
        mean += hist[size_t(r)] * r;
    }
    BandSet bs;
    if (total <= 0) {
        bs.rows.assign(size_t(bands), 32);
        for (int b = 1; b < bands; b++) bs.thresholds.push_back(64.0 * b / bands);
        return bs;
    }
    mean /= total;
    // 1-D k-means on the histogram, from equal-population starting points
    std::vector<double> c(static_cast<size_t>(bands));
    {
        double acc = 0;
        int b = 0;
        for (int r = 0; r < 64 && b < bands; r++) {
            acc += hist[size_t(r)];
            while (b < bands && acc >= total * (b + 0.5) / bands) c[size_t(b++)] = r;
        }
        for (; b < bands; b++) c[size_t(b)] = 63;
    }
    for (int it = 0; it < 50; it++) {
        std::vector<double> s(static_cast<size_t>(bands), 0), w(size_t(bands), 0);
        for (int r = 0; r < 64; r++) {
            if (!hist[size_t(r)]) continue;
            int best = 0;
            for (int b = 1; b < bands; b++)
                if (std::fabs(r - c[size_t(b)]) < std::fabs(r - c[size_t(best)])) best = b;
            s[size_t(best)] += hist[size_t(r)] * r;
            w[size_t(best)] += hist[size_t(r)];
        }
        for (int b = 0; b < bands; b++)
            if (w[size_t(b)] > 0) c[size_t(b)] = s[size_t(b)] / w[size_t(b)];
        std::sort(c.begin(), c.end());
    }
    for (int b = 0; b + 1 < bands; b++) bs.thresholds.push_back((c[size_t(b)] + c[size_t(b) + 1]) / 2);
    for (int b = 0; b < bands; b++) {
        double r = mean + (c[size_t(b)] - mean) * contrast;
        bs.rows.push_back(int(std::clamp(std::lround(r), 0L, 63L)));
    }
    return bs;
}

Vec3 face_point(const LightFace &f, double s, double t)
{
    // solve S.p = s - S3, T.p = t - T3, n.p = d
    const double *v = f.vecs;
    double a[3][4] = {{v[0], v[1], v[2], s - v[3]}, {v[4], v[5], v[6], t - v[7]}, {f.n[0], f.n[1], f.n[2], f.d}};
    auto det3 = [](double m00, double m01, double m02, double m10, double m11, double m12, double m20, double m21,
                   double m22) {
        return m00 * (m11 * m22 - m12 * m21) - m01 * (m10 * m22 - m12 * m20) + m02 * (m10 * m21 - m11 * m20);
    };
    double D = det3(a[0][0], a[0][1], a[0][2], a[1][0], a[1][1], a[1][2], a[2][0], a[2][1], a[2][2]);
    if (std::fabs(D) < 1e-12) return winding_centre(f.poly);
    Vec3 p;
    p[0] = det3(a[0][3], a[0][1], a[0][2], a[1][3], a[1][1], a[1][2], a[2][3], a[2][1], a[2][2]) / D;
    p[1] = det3(a[0][0], a[0][3], a[0][2], a[1][0], a[1][3], a[1][2], a[2][0], a[2][3], a[2][2]) / D;
    p[2] = det3(a[0][0], a[0][1], a[0][3], a[1][0], a[1][1], a[1][3], a[2][0], a[2][1], a[2][3]) / D;
    return p;
}

namespace {
struct P2 {
    double s, t, v;   // texture coordinates and the light value there
};

bool less2(const P2 &a, const P2 &b) { return a.s < b.s || (a.s == b.s && a.t < b.t); }

// the point where the value crosses `th` on the edge a-b (computed the same
// way whichever way round the edge is)
P2 cross_value(P2 a, P2 b, double th)
{
    if (less2(b, a)) std::swap(a, b);
    double u = (th - a.v) / (b.v - a.v);
    return {a.s + u * (b.s - a.s), a.t + u * (b.t - a.t), th};
}

void split_value(const std::vector<P2> &w, double th, std::vector<P2> &lo, std::vector<P2> &hi)
{
    lo.clear();
    hi.clear();
    size_t n = w.size();
    for (size_t i = 0; i < n; i++) {
        const P2 &a = w[i], &b = w[(i + 1) % n];
        int sa = a.v < th ? -1 : a.v > th ? 1 : 0, sb = b.v < th ? -1 : b.v > th ? 1 : 0;
        if (sa <= 0) lo.push_back(a);
        if (sa >= 0) hi.push_back(a);
        if (sa * sb < 0) {
            P2 m = cross_value(a, b, th);
            lo.push_back(m);
            hi.push_back(m);
        }
    }
    if (lo.size() < 3) lo.clear();
    if (hi.size() < 3) hi.clear();
}

// clip by the half-plane on the left of edge a->b (in s, t) when `ccw`
std::vector<P2> clip_edge(const std::vector<P2> &w, P2 a, P2 b, bool ccw)
{
    std::vector<P2> out;
    double ex = b.s - a.s, ey = b.t - a.t;
    auto side = [&](const P2 &p) {
        double c = ex * (p.t - a.t) - ey * (p.s - a.s);
        return ccw ? c : -c;
    };
    size_t n = w.size();
    for (size_t i = 0; i < n; i++) {
        const P2 &p = w[i], &q = w[(i + 1) % n];
        double dp = side(p), dq = side(q);
        if (dp >= -1e-9) out.push_back(p);
        if ((dp > 1e-9 && dq < -1e-9) || (dp < -1e-9 && dq > 1e-9)) {
            P2 x = p, y = q;
            double dx = dp, dy = dq;
            if (less2(y, x)) {
                std::swap(x, y);
                std::swap(dx, dy);
            }
            double u = dx / (dx - dy);
            out.push_back({x.s + u * (y.s - x.s), x.t + u * (y.t - x.t), x.v + u * (y.v - x.v)});
        }
    }
    if (out.size() < 3) out.clear();
    return out;
}
}  // namespace

LightPieces light_pieces(const LightFace &f, const BandSet &bands, double min_area)
{
    LightPieces res;
    const int sm = f.smax, tm = f.tmax;
    if (sm <= 0 || tm <= 0 || f.L.size() != size_t(sm) * size_t(tm)) return res;
    std::vector<double> v(f.L.size());
    std::vector<uint8_t> band(f.L.size());
    for (size_t i = 0; i < f.L.size(); i++) {
        v[i] = f.L[i] / 4.0;
        band[i] = uint8_t(bands.band_of(v[i]));
    }
    if (f.forced_band >= 0) {
        res.uniform = std::min(f.forced_band, int(bands.rows.size()) - 1);
        res.bandmap.assign(f.L.size(), uint8_t(res.uniform));
        return res;
    }
    // small regions join their neighbours
    double sl = std::sqrt(f.vecs[0] * f.vecs[0] + f.vecs[1] * f.vecs[1] + f.vecs[2] * f.vecs[2]);
    double tl = std::sqrt(f.vecs[4] * f.vecs[4] + f.vecs[5] * f.vecs[5] + f.vecs[6] * f.vecs[6]);
    double cell_area = (sl > 0 && tl > 0) ? (16 / sl) * (16 / tl) : 256;
    for (int pass = 0; pass < 8; pass++) {
        std::vector<int> comp(band.size(), -1);
        bool changed = false;
        for (int start = 0; start < int(band.size()); start++) {
            if (comp[size_t(start)] >= 0) continue;
            std::vector<int> members = {start}, st = {start};
            comp[size_t(start)] = start;
            std::map<int, int> nb;
            while (!st.empty()) {
                int c = st.back();
                st.pop_back();
                int x = c % sm, y = c / sm;
                const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};
                for (int k = 0; k < 4; k++) {
                    int nx = x + dx[k], ny = y + dy[k];
                    if (nx < 0 || ny < 0 || nx >= sm || ny >= tm) continue;
                    int o = ny * sm + nx;
                    if (band[size_t(o)] != band[size_t(start)]) {
                        nb[band[size_t(o)]]++;
                        continue;
                    }
                    if (comp[size_t(o)] < 0) {
                        comp[size_t(o)] = start;
                        members.push_back(o);
                        st.push_back(o);
                    }
                }
            }
            if (!nb.empty() && double(members.size()) * cell_area < min_area) {
                int best = -1, bn = -1;
                for (auto &kv : nb)
                    if (kv.second > bn) {
                        bn = kv.second;
                        best = kv.first;
                    }
                for (int m : members) band[size_t(m)] = uint8_t(best);
                changed = true;
            }
        }
        if (!changed) break;
    }
    res.bandmap = band;
    bool uniform = true;
    for (auto b : band)
        if (b != band[0]) uniform = false;
    if (uniform) {
        res.uniform = band[0];
        return res;
    }
    // each sample's value inside its band's range, so the cuts follow the bands
    for (size_t i = 0; i < v.size(); i++) {
        int b = band[i];
        double lo = b > 0 ? bands.thresholds[size_t(b - 1)] : -1e9;
        double hi = b < int(bands.thresholds.size()) ? bands.thresholds[size_t(b)] : 1e9;
        if (v[i] < lo || v[i] >= hi) {
            double l = b > 0 ? lo : hi - 1, h = b < int(bands.thresholds.size()) ? hi : lo + 1;
            v[i] = (l + h) / 2;
        }
    }
    // the face in texture space, and its winding there
    std::vector<P2> face;
    for (auto &p : f.poly)
        face.push_back({p[0] * f.vecs[0] + p[1] * f.vecs[1] + p[2] * f.vecs[2] + f.vecs[3],
                        p[0] * f.vecs[4] + p[1] * f.vecs[5] + p[2] * f.vecs[6] + f.vecs[7], 0});
    double area = 0;
    for (size_t i = 0; i < face.size(); i++) {
        const P2 &a = face[i], &b = face[(i + 1) % face.size()];
        area += a.s * b.t - b.s * a.t;
    }
    bool ccw = area > 0;
    auto sample = [&](int i, int j) -> P2 {
        return {double(f.tmins[0] + 16 * i), double(f.tmins[1] + 16 * j), v[size_t(j * sm + i)]};
    };
    for (int j = 0; j + 1 < tm; j++)
        for (int i = 0; i + 1 < sm; i++) {
            P2 a = sample(i, j), b = sample(i + 1, j), c = sample(i + 1, j + 1), d = sample(i, j + 1);
            std::vector<std::vector<P2>> tris = {{a, b, c}, {a, c, d}};
            for (auto &tri : tris) {
                std::vector<std::vector<P2>> parts = {tri}, next;
                for (double th : bands.thresholds) {
                    next.clear();
                    for (auto &p : parts) {
                        std::vector<P2> lo, hi;
                        split_value(p, th, lo, hi);
                        if (!lo.empty()) next.push_back(lo);
                        if (!hi.empty()) next.push_back(hi);
                    }
                    parts.swap(next);
                }
                for (auto &p : parts) {
                    std::vector<P2> w = p;
                    for (size_t k = 0; k < face.size() && !w.empty(); k++)
                        w = clip_edge(w, face[k], face[(k + 1) % face.size()], ccw);
                    if (w.empty()) continue;
                    double cv = 0;
                    for (auto &q : w) cv += q.v;
                    cv /= double(w.size());
                    std::vector<Vec3> poly;
                    for (auto &q : w) poly.push_back(face_point(f, q.s, q.t));
                    if (!ccw) std::reverse(poly.begin(), poly.end());
                    res.pieces.push_back({poly, bands.band_of(cv)});
                }
            }
        }
    return res;
}

}  // namespace q32x
