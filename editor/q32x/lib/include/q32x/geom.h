// Polygon helpers: windings on planes, clipping, areas.
#pragma once

#include <cmath>
#include <vector>

#include "q32x/quake.h"

namespace q32x {

using Winding = std::vector<Vec3>;

inline Vec3 sub(const Vec3 &a, const Vec3 &b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
inline Vec3 add3(const Vec3 &a, const Vec3 &b) { return {a[0] + b[0], a[1] + b[1], a[2] + b[2]}; }
inline Vec3 scale3(const Vec3 &a, double s) { return {a[0] * s, a[1] * s, a[2] * s}; }
inline Vec3 cross(const Vec3 &a, const Vec3 &b)
{
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
inline double length(const Vec3 &a) { return std::sqrt(dot(a, a)); }
inline Vec3 normalize(const Vec3 &a)
{
    double l = length(a);
    return l > 0 ? scale3(a, 1 / l) : a;
}

// a square on the plane, `size` units from its centre each way
Winding base_winding(const Vec3 &n, double d, double size = 65536);
// the part of w in front of the plane n.x = d (epsilon: on-plane points kept)
Winding clip_front(const Winding &w, const Vec3 &n, double d, double eps = 0.01);
void split_winding(const Winding &w, const Vec3 &n, double d, Winding &front, Winding &back, double eps = 0.01);
double winding_area(const Winding &w);
Vec3 winding_centre(const Winding &w);
Vec3 winding_normal(const Winding &w);

}  // namespace q32x
