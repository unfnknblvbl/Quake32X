// Flat mode in a palette of the user's own: the colours each face gets,
// shading by occlusion (less open sky under arches and bridges, in corners
// and alcoves) and by the level's light entities with shadows, and the
// tables the game swaps in while textures are off (the FLATPAL lump).
#pragma once

#include <array>
#include <string>
#include <vector>

#include "q32x/bytes.h"
#include "q32x/quake.h"

namespace q32x {

struct FlatPalOptions {
    bool enabled = false;
    std::array<uint8_t, 768> rgb{};   // the 256 colours
    std::string source;               // the file they came from (shown in the editor)
    // the faces' colours: the nearest of the palette to their texture's
    // average, made more saturated and brighter first (0: as it is)
    double vividness = 0.6;
    // the textures' colours spread round the colour wheel in their order
    // (a level of browns gets reds, greens and blues; 0: their own hues)
    double variety = 0.5;
    // liquids and skies by kind instead of their (murky) averages: water
    // blue-green, slime green-yellow, lava red, teleporters purple, sky
    // blue-purple, each the nearest of the palette to that colour
    bool special_kinds = true;
    // ordinary surfaces, and the duller colours of models, from the
    // palette's greys only (by lightness); the colourful ones (blood, fire,
    // the full brights) and liquids and skies keep their colours
    bool neutral_surfaces = false;
    // shading: by occlusion and the light entities (false: Quake's light maps)
    bool shade = true;
    double occlusion = 0.8;           // how much enclosed places darken (0..1)
    double reach = 192;               // how far occlusion looks, world units
    int rays = 32;                    // occlusion rays per light-map sample
    double up_bias = 0.6;             // light comes from above: faces turned down darker (0..1)
    double ambient = 48;             // light everywhere, Quake light units (127: the full colour)
    double lights = 0.7;              // strength of the light entities
    double lightmaps = 0.0;           // blend in Quake's own light maps (0..1)
    double highlight = 0.0;           // brighter than the colour near lights (0: never; 1: up to twice)
    // light levels a face is split into (when light to polygons is off; 1:
    // faces whole, each at its average: no extra polygons to draw)
    int levels = 4;
};

using Rgb = std::array<double, 3>;

// a palette file: JASC .pal, GIMP .gpl, Photoshop .act, Quake .lmp (768
// bytes) or a PNG (a 16x16 grid of swatches, an indexed PNG's palette, or
// its first 256 different colours). Fewer than 256 colours: the rest black.
bool parse_palette(const Bytes &data, const std::string &name, std::array<uint8_t, 768> &out, std::string *err = nullptr);
bool load_palette_file(const std::string &path, std::array<uint8_t, 768> &out, std::string *err = nullptr);
// a palette that comes with the editor: "grey" (the 32 greys the 32X can
// show, from black to white, each eight times so they span the 256)
bool builtin_palette(const std::string &name, std::array<uint8_t, 768> &out);

// the nearest palette colour (in OKLab); the first of equals
class ColourMatch {
public:
    explicit ColourMatch(const std::array<uint8_t, 768> &rgb);
    int nearest(const Rgb &c) const;
    // the nearest of the palette's greys by lightness (-1: it has none)
    int nearest_grey(const Rgb &c) const;
    static double chroma(const Rgb &c);
    Rgb rgb(int i) const { return {double(rgb_[size_t(3 * i)]), double(rgb_[size_t(3 * i + 1)]), double(rgb_[size_t(3 * i + 2)])}; }

private:
    std::array<uint8_t, 768> rgb_;
    std::vector<std::array<double, 3>> lab_;
};

// more saturated and brighter by `amount` (0..1)
Rgb vivid(const Rgb &c, double amount);
// the hues of a set of colours (greys left alone) spread round the colour
// wheel in their order, each taking room by its weight (0: as they are)
std::vector<Rgb> spread_hues(const std::vector<Rgb> &cols, const std::vector<double> &weights, double amount);
// a texture's average colour (from its smallest mip, as the converter takes it)
Rgb texture_average(const BspTexture &t, const Palette &pal);
// every texture's flat colour in the user's palette (an index): its average
// brightened with the level's others, made vivid, hues spread, matched
std::vector<int> flatpal_texture_colours(const Bsp &m, const std::vector<Rgb> &tex_rgb, const FlatPalOptions &o,
                                         std::vector<Rgb> *chosen = nullptr);
// a colour at a colormap row (0 twice as bright .. 32 itself .. 63 black)
Rgb shade_rgb(const Rgb &c, int row);

struct FlatPalTables {
    std::array<uint16_t, 256> cram{};  // the palette as 32X colours (BGR555)
    std::array<uint8_t, 256> remap{};  // a Quake colour as one of these (particles)
    Bytes cmap;                        // 64 x 256: Quake colour at a row -> one of these (models, kept textures)
    Bytes ucm;                         // 64 x 256: one of these at a row (the faces)
};
FlatPalTables build_flatpal_tables(const FlatPalOptions &o, const Palette &quake, const Bytes &quake_colormap);

// the light at points of a level, from occlusion and its light entities
class FlatShader {
public:
    FlatShader(const Bsp &m, const std::vector<Entity> &ents, const FlatPalOptions &o);
    // a face's light-map grid (smax x tmax samples from tmins, 16 units
    // apart in texture space) as light values (0 bright .. 255 dark, as
    // the converter's light maps); quake: Quake's own map to blend in, or empty
    std::vector<uint8_t> light_map(const std::vector<Vec3> &poly, const Vec3 &normal, const double vecs[8],
                                   const int64_t tmins[2], int smax, int tmax, const std::vector<uint8_t> &quake) const;
    // the average over a polygon (dark units)
    int poly_light(const std::vector<Vec3> &poly, const Vec3 &normal) const;
    int nlights() const { return int(lights_.size()); }

    struct Light {
        Vec3 org{};
        double value = 300, wait = 1;
        int delay = 0;
        bool spot = false;
        Vec3 dir{};
        double cone = 0;              // cos of the spot's half angle
    };

private:
    double light_at(const Vec3 &p, const Vec3 &n, const std::vector<int> &lights, const Vec3 &jit) const;
    uint8_t to_dark(double v) const;
    bool blocked(const Vec3 &a, const Vec3 &b, double *frac, bool *sky) const;
    std::vector<int> lights_near(const std::vector<Vec3> &poly, const Vec3 &n) const;
    Vec3 inside(const std::vector<Vec3> &poly, const Vec3 &n, const Vec3 &c, const Vec3 &q) const;
    const Bsp &m_;
    FlatPalOptions o_;
    std::vector<Light> lights_;
    std::vector<int> solid_models_;   // brush models that cast shadows (walls, doors)
    std::vector<Vec3> dirs_;          // cosine-weighted hemisphere directions around +z
};

}  // namespace q32x
