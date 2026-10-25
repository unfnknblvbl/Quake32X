#include "q32x/geom.h"

namespace q32x {

Winding base_winding(const Vec3 &n, double d, double size)
{
    // the axis the normal is most along, and a vector up the plane
    int x = 0;
    for (int k = 1; k < 3; k++)
        if (std::fabs(n[size_t(k)]) > std::fabs(n[size_t(x)])) x = k;
    Vec3 up = {0, 0, 0};
    if (x == 2)
        up[0] = 1;
    else
        up[2] = 1;
    double v = dot(up, n);
    up = normalize(sub(up, scale3(n, v)));
    Vec3 org = scale3(n, d);
    Vec3 right = cross(up, n);
    up = scale3(up, size);
    right = scale3(right, size);
    return {add3(sub(org, right), up), add3(add3(org, right), up), sub(add3(org, right), up),
            sub(sub(org, right), up)};
}

void split_winding(const Winding &w, const Vec3 &n, double d, Winding &front, Winding &back, double eps)
{
    front.clear();
    back.clear();
    size_t c = w.size();
    if (!c) return;
    std::vector<double> dist(c);
    std::vector<int> side(c);
    int nf = 0, nb = 0;
    for (size_t i = 0; i < c; i++) {
        dist[i] = dot(w[i], n) - d;
        side[i] = dist[i] > eps ? 1 : dist[i] < -eps ? -1 : 0;
        if (side[i] > 0) nf++;
        if (side[i] < 0) nb++;
    }
    if (!nb) {
        front = w;
        return;
    }
    if (!nf) {
        back = w;
        return;
    }
    for (size_t i = 0; i < c; i++) {
        const Vec3 &p = w[i];
        if (side[i] >= 0) front.push_back(p);
        if (side[i] <= 0) back.push_back(p);
        size_t j = (i + 1) % c;
        if (side[i] == 0 || side[j] == 0 || side[i] == side[j]) continue;
        double t = dist[i] / (dist[i] - dist[j]);
        Vec3 m;
        for (int k = 0; k < 3; k++) {
            // exact on axial planes
            if (n[size_t(k)] == 1)
                m[size_t(k)] = d;
            else if (n[size_t(k)] == -1)
                m[size_t(k)] = -d;
            else
                m[size_t(k)] = p[size_t(k)] + t * (w[j][size_t(k)] - p[size_t(k)]);
        }
        front.push_back(m);
        back.push_back(m);
    }
    if (front.size() < 3) front.clear();
    if (back.size() < 3) back.clear();
}

Winding clip_front(const Winding &w, const Vec3 &n, double d, double eps)
{
    Winding f, b;
    split_winding(w, n, d, f, b, eps);
    return f;
}

Vec3 winding_normal(const Winding &w)
{
    Vec3 s = {0, 0, 0};
    for (size_t i = 0; i < w.size(); i++) {
        const Vec3 &a = w[i], &b = w[(i + 1) % w.size()];
        s[0] += (a[1] - b[1]) * (a[2] + b[2]);
        s[1] += (a[2] - b[2]) * (a[0] + b[0]);
        s[2] += (a[0] - b[0]) * (a[1] + b[1]);
    }
    return s;
}

double winding_area(const Winding &w) { return length(winding_normal(w)) * 0.5; }

Vec3 winding_centre(const Winding &w)
{
    Vec3 c = {0, 0, 0};
    for (auto &p : w) c = add3(c, p);
    return w.empty() ? c : scale3(c, 1.0 / double(w.size()));
}

}  // namespace q32x
