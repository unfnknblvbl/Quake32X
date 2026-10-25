// libq32x self-tests (no game data needed)
#include <cmath>
#include <cstdint>
#include <cstdio>

#include "q32x/bytes.h"
#include "q32x/flatpal.h"
#include "q32x/gamedata.h"
#include "q32x/image.h"
#include "q32x/json.h"
#include "q32x/mdgfx.h"
#include "q32x/project.h"
#include "q32x/rom.h"

using namespace q32x;

// the game's own decoder (src/snd.c plays the smaller sound formats with it)
#if __has_include("../../../src/snd_dec.h")
#define HAVE_GAME_DECODER 1
#include "../../../src/snd_dec.h"
#endif

static int failures = 0;
#define CHECK(c)                                                         \
    do {                                                                 \
        if (!(c)) {                                                      \
            std::fprintf(stderr, "%s:%d: failed: %s\n", __FILE__, __LINE__, #c); \
            failures++;                                                  \
        }                                                                \
    } while (0)

int main()
{
    CHECK(pyround(2.5) == 2 && pyround(3.5) == 4 && pyround(-2.5) == -2);
    CHECK(pyround_digits(0.1234565, 6) == 0.123457 || pyround_digits(0.1234565, 6) == 0.123456);
    CHECK(floordiv(-7, 2) == -4 && floormod(-7, 2) == 1);
    CHECK(pyfloat(" -12.5 ") == -12.5);
    Json j = Json::parse("{\"a\": [1, 2, {\"b\": \"x\\u00e9\"}], \"c\": true}");
    CHECK(j["a"][1].as_int() == 2 && j["c"].as_bool() && j["a"][2]["b"].as_string() == "x\xc3\xa9");
    CHECK(Json::parse(j.dump()).dump() == j.dump());
    // tile packing round trip
    Bytes t;
    for (int i = 0; i < 5000; i++) t.push_back(uint8_t((i * 7) % 13 == 0 ? i : (i / 64) & 3));
    CHECK(md_unpack(md_pack(t), t.size()) == t);
    // PNG round trip
    RgbImage im(13, 7);
    for (int y = 0; y < 7; y++)
        for (int x = 0; x < 13; x++) im.set(x, y, uint8_t(x * 19), uint8_t(y * 31), uint8_t(x ^ y));
    RgbImage back = decode_png(encode_png(im));
    CHECK(back.w == 13 && back.h == 7 && back.px == im.px);
    CHECK(glob_match("+*button", "+0button") && !glob_match("+*button", "+0shoot") && glob_match("*", "x") && glob_match("*", "*04water1") && glob_match("*water*", "*04water1"));
    CHECK(from_hex(to_hex({0, 1, 255})) == Bytes({0, 1, 255}));
    std::printf(failures ? "%d failures\n" : "all tests passed\n", failures);
    {   // palette files
        std::array<uint8_t, 768> p{};
        std::string jasc = "JASC-PAL\r\n0100\r\n3\r\n255 0 0\r\n0 255 0\r\n0 0 255\r\n";
        CHECK(parse_palette(Bytes(jasc.begin(), jasc.end()), "a.pal", p) && p[0] == 255 && p[4] == 255 && p[8] == 255 && p[9] == 0);
        std::string gpl = "GIMP Palette\nName: x\nColumns: 4\n# c\n 10  20  30\tone\n200 100 50 two\n";
        CHECK(parse_palette(Bytes(gpl.begin(), gpl.end()), "a.gpl", p) && p[0] == 10 && p[5] == 50);
        std::string hex = "ff8000\n#0080FF\n";
        CHECK(parse_palette(Bytes(hex.begin(), hex.end()), "a.hex", p) && p[0] == 255 && p[1] == 128 && p[5] == 255);
        Bytes act(768);
        for (int i = 0; i < 768; i++) act[size_t(i)] = uint8_t(i / 3);
        CHECK(parse_palette(act, "a.act", p) && p[3 * 77] == 77);
        RgbImage sw(32, 32);
        for (int y = 0; y < 32; y++)
            for (int x = 0; x < 32; x++) sw.set(x, y, uint8_t(x / 2 * 16), uint8_t(y / 2 * 16), 7);
        CHECK(parse_palette(encode_png(sw), "a.png", p) && p[3 * 17] == 16 && p[3 * 17 + 1] == 16);
        CHECK(!parse_palette(Bytes{1, 2, 3}, "a.xyz", p));
        CHECK(builtin_palette("grey", p) && p[0] == 0 && p[765] == 255 && p[3 * 128] == p[3 * 128 + 2] && !builtin_palette("x", p));
        // every colour is its own nearest; shades of a colour stay that colour's
        std::array<uint8_t, 768> ramp{};
        for (int h = 0; h < 4; h++)
            for (int k = 0; k < 32; k++) {
                int v = 8 * k + 7;
                ramp[size_t(3 * (h * 64 + k))] = uint8_t(h == 0 || h == 3 ? v : 0);
                ramp[size_t(3 * (h * 64 + k) + 1)] = uint8_t(h == 1 || h == 3 ? v : 0);
                ramp[size_t(3 * (h * 64 + k) + 2)] = uint8_t(h == 2 || h == 3 ? v : 0);
            }
        ColourMatch cm(ramp);
        bool own = true;
        for (int h = 0; h < 4; h++)
            for (int k = 0; k < 32; k++)
                if (cm.nearest(cm.rgb(h * 64 + k)) != h * 64 + k) {
                    own = false;
                    std::fprintf(stderr, "colour %d -> %d\n", h * 64 + k, cm.nearest(cm.rgb(h * 64 + k)));
                }
        CHECK(own);
        CHECK(cm.nearest(shade_rgb(cm.rgb(64 + 30), 48)) / 64 == 1);   // a darker green is a green
        FlatPalOptions o;
        o.rgb = ramp;
        Palette q;
        for (int i = 0; i < 768; i++) q.rgb[i] = uint8_t(i / 3);
        FlatPalTables t = build_flatpal_tables(o, q, Bytes(64 * 256, 0));
        CHECK(t.cmap.size() == 64 * 256 && t.ucm.size() == 64 * 256);
        CHECK(t.ucm[32 * 256 + 100] == 100);   // row 32: the colour itself
        CHECK(t.cram[192 + 31] == (31 << 10 | 31 << 5 | 31));
        // brighter rows are not darker
        CHECK(cm.rgb(t.ucm[10 * 256 + 64 + 30])[1] >= cm.rgb(64 + 30)[1] && cm.rgb(t.ucm[50 * 256 + 64 + 30])[1] < cm.rgb(64 + 30)[1]);
        // hues spread round the wheel in their order, greys left alone
        std::vector<Rgb> c3 = {{200, 100, 50}, {200, 120, 50}, {200, 140, 50}, {128, 128, 128}};
        auto sp = spread_hues(c3, {1, 1, 1, 1}, 1.0);
        double far = std::fabs(sp[0][0] - sp[2][0]) + std::fabs(sp[0][1] - sp[2][1]) + std::fabs(sp[0][2] - sp[2][2]);
        CHECK(sp[3] == c3[3] && far > 150 && spread_hues(c3, {1, 1, 1, 1}, 0) == c3);
    }
    {   // cartridges: 4 MB or less without the mapper, bigger with it
        Bytes prog(0x400, 0);
        const char *tag = "LEVELDIR";
        std::copy(tag, tag + 8, prog.begin() + 0x300);
        auto lv = [](size_t n) { return RomLevel{"m", Bytes(n, 0x11)}; };
        RomInfo i1, i2;
        Bytes small = build_rom(prog, {lv(1000000), lv(1500000)}, "", &i1, Bytes(300000, 0x22));
        CHECK(i1.linear && small.size() == (4u << 20) && i1.levels[0].first == 13 && i1.bank_page == 8 && i1.bank_pages == 5);
        CHECK(small[0x300 + 12 + 128 + 3] == 1 && small[size_t(i1.levels[1].first) * 65536] == 0x11);
        Bytes big = build_rom(prog, {lv(2000000), lv(2000000)}, "", &i2, Bytes(300000, 0x22));
        CHECK(!i2.linear && big.size() >= (8u << 20) && big[0x300 + 12 + 128 + 3] == 0);
    }
    {   // smaller sounds: sizes, and what plays back stays close to the original
        Sound snd;
        for (int i = 0; i < 3001; i++)
            snd.data.push_back(uint8_t(128 + std::lround(90 * std::sin(i * 0.05) + 20 * std::sin(i * 0.31))));
        for (int f = 0; f < 4; f++) {
            Bytes st = encode_sound(snd, f);
            uint32_t field = sound_length_field(snd, f);
            Bytes back = decode_sound(st, field);
            size_t want = f == 0 ? 3001 : f == SF_HALF ? 1501 : f == SF_PACK ? 12 * 145 : 6 * 145;
            double err = 0;
            for (size_t i = 0; i < back.size(); i++) err += std::abs(int(back[i]) - int(snd.data[i]));
            err /= double(back.size());
            CHECK(st.size() == want && back.size() == 3001 && (field >> 28) == uint32_t(f));
            CHECK(err < (f == 0 ? 0.001 : 4.0));
#ifdef HAVE_GAME_DECODER
            // the game's decoder gives the same samples, in the runs the mixer asks for
            // (128 at a time, a loop back to sample 1000, and a jump forward)
            sdec_t d;
            sdec_start(&d, st.data(), f, 3001);
            Bytes game;
            uint8_t buf[128];
            for (int32_t pos = 0; pos < 3001; pos += 128) {
                int n = int(std::min<int32_t>(128, 3001 - pos));
                sdec_run(&d, pos, n, buf);
                game.insert(game.end(), buf, buf + n);
            }
            CHECK(game == back);
            sdec_run(&d, 1000, 100, buf);
            CHECK(std::equal(buf, buf + 100, back.begin() + 1000));
            sdec_run(&d, 2500, 77, buf);
            CHECK(std::equal(buf, buf + 77, back.begin() + 2500));
#endif
        }
    }
    return failures ? 1 : 0;
}
