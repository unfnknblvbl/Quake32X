// Reduced textures: a texture drawn from a coarser mip level (16x16 for a
// 64x64 texture), with 16 colours at 4 bits per texel.
#pragma once

#include "q32x/level.h"

namespace q32x {

// the default reduced texture: the texture's own mip level `min_mip`, cut
// to 16 colours of the palette (when four_bit)
ReducedTexture auto_reduce(const BspTexture &t, const Palette &pal, int min_mip, bool four_bit);

// the reduced texture's levels as palette indices: level k (k >= min_mip) is
// (w >> k) x (h >> k); levels below min_mip are empty
std::vector<Bytes> reduced_levels(const BspTexture &t, const ReducedTexture &r, const Palette &pal);

// 2x2 box downsampling of a palette-indexed image, to the nearest of `allowed` colours
Bytes downsample(const Bytes &px, int w, int h, const Palette &pal, const std::vector<int> &allowed);

}  // namespace q32x
