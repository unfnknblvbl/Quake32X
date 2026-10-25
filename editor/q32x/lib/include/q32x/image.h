// Small image helpers: an RGB image and a PNG writer (stored deflate
// blocks, no compression library needed) for previews.
#pragma once

#include <string>

#include "q32x/bytes.h"

namespace q32x {

struct RgbImage {
    int w = 0, h = 0;
    Bytes px;   // w * h * 3
    RgbImage() = default;
    RgbImage(int w_, int h_, uint8_t r = 0, uint8_t g = 0, uint8_t b = 0) : w(w_), h(h_), px(size_t(w_) * size_t(h_) * 3)
    {
        for (size_t i = 0; i < px.size(); i += 3) {
            px[i] = r;
            px[i + 1] = g;
            px[i + 2] = b;
        }
    }
    void set(int x, int y, uint8_t r, uint8_t g, uint8_t b)
    {
        if (x < 0 || y < 0 || x >= w || y >= h) return;
        size_t o = (size_t(y) * size_t(w) + size_t(x)) * 3;
        px[o] = r;
        px[o + 1] = g;
        px[o + 2] = b;
    }
    RgbImage scaled(int k) const;
};

Bytes encode_png(const RgbImage &im);
// a PNG (8-bit grey, RGB, RGBA or palette; not interlaced) as RGB (alpha ignored)
RgbImage decode_png(const Bytes &png);
RgbImage load_png(const std::string &path);
Bytes inflate_zlib(const Bytes &z);
// an image as palette indices (the nearest of the first `limit` colours)
struct Palette;
Bytes image_to_palette(const RgbImage &im, const Palette &pal, int limit = 256);
// nearest-neighbour resize
RgbImage resize_nearest(const RgbImage &im, int w, int h);
void save_png(const std::string &path, const RgbImage &im);
uint32_t crc32(const uint8_t *p, size_t n, uint32_t crc = 0);

}  // namespace q32x
