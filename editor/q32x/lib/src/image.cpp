#include "q32x/image.h"

#include <cstdlib>
#include <map>

namespace q32x {

RgbImage RgbImage::scaled(int k) const
{
    RgbImage o(w * k, h * k);
    for (int y = 0; y < o.h; y++)
        for (int x = 0; x < o.w; x++) {
            size_t s = (size_t(y / k) * size_t(w) + size_t(x / k)) * 3;
            o.set(x, y, px[s], px[s + 1], px[s + 2]);
        }
    return o;
}

uint32_t crc32(const uint8_t *p, size_t n, uint32_t crc)
{
    static uint32_t table[256];
    static bool init = false;
    if (!init) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        init = true;
    }
    crc = ~crc;
    for (size_t i = 0; i < n; i++) crc = table[(crc ^ p[i]) & 255] ^ (crc >> 8);
    return ~crc;
}

static void be32(Bytes &b, uint32_t v)
{
    for (int i = 3; i >= 0; i--) b.push_back(uint8_t(v >> (8 * i)));
}

static void chunk(Bytes &out, const char *type, const Bytes &data)
{
    be32(out, uint32_t(data.size()));
    Bytes td(type, type + 4);
    td.insert(td.end(), data.begin(), data.end());
    out.insert(out.end(), td.begin(), td.end());
    be32(out, crc32(td.data(), td.size()));
}

Bytes encode_png(const RgbImage &im)
{
    Bytes out = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    Bytes ihdr;
    be32(ihdr, uint32_t(im.w));
    be32(ihdr, uint32_t(im.h));
    ihdr.insert(ihdr.end(), {8, 2, 0, 0, 0});
    chunk(out, "IHDR", ihdr);
    Bytes raw;
    for (int y = 0; y < im.h; y++) {
        raw.push_back(0);
        raw.insert(raw.end(), im.px.begin() + std::ptrdiff_t(size_t(y) * size_t(im.w) * 3),
                   im.px.begin() + std::ptrdiff_t(size_t(y + 1) * size_t(im.w) * 3));
    }
    Bytes z = {0x78, 0x01};
    size_t o = 0;
    do {
        size_t n = std::min<size_t>(65535, raw.size() - o);
        bool last = o + n == raw.size();
        z.push_back(last ? 1 : 0);
        z.push_back(uint8_t(n));
        z.push_back(uint8_t(n >> 8));
        z.push_back(uint8_t(~n));
        z.push_back(uint8_t(~n >> 8));
        z.insert(z.end(), raw.begin() + std::ptrdiff_t(o), raw.begin() + std::ptrdiff_t(o + n));
        o += n;
    } while (o < raw.size());
    uint32_t a = 1, b = 0;
    for (uint8_t c : raw) {
        a = (a + c) % 65521;
        b = (b + a) % 65521;
    }
    be32(z, (b << 16) | a);
    chunk(out, "IDAT", z);
    chunk(out, "IEND", {});
    return out;
}

void save_png(const std::string &path, const RgbImage &im) { write_file(path, encode_png(im)); }

}  // namespace q32x

// ------------------------------------------------------------ PNG reading

#include "q32x/quake.h"

namespace q32x {

namespace {
struct BitIn {
    const Bytes &d;
    size_t pos = 0;
    uint32_t buf = 0;
    int cnt = 0;
    int bits(int n)
    {
        while (cnt < n) {
            check(pos < d.size(), "compressed data ends early");
            buf |= uint32_t(d[pos++]) << cnt;
            cnt += 8;
        }
        int v = int(buf & ((1u << n) - 1));
        buf >>= n;
        cnt -= n;
        return v;
    }
};

struct Huff {
    std::vector<short> count, symbol;
    void build(const std::vector<int> &lens)
    {
        count.assign(16, 0);
        symbol.assign(lens.size(), 0);
        for (int l : lens) count[size_t(l)]++;
        count[0] = 0;
        std::vector<short> offs(16, 0);
        for (int i = 1; i < 16; i++) offs[size_t(i)] = short(offs[size_t(i) - 1] + count[size_t(i) - 1]);
        for (size_t s = 0; s < lens.size(); s++)
            if (lens[s]) symbol[size_t(offs[size_t(lens[s])]++)] = short(s);
    }
    int decode(BitIn &in) const
    {
        int code = 0, first = 0, index = 0;
        for (int len = 1; len < 16; len++) {
            code |= in.bits(1);
            int c = count[size_t(len)];
            if (code - c < first) return symbol[size_t(index + (code - first))];
            index += c;
            first += c;
            first <<= 1;
            code <<= 1;
        }
        fail("bad compressed data");
    }
};
}  // namespace

Bytes inflate_zlib(const Bytes &z)
{
    check(z.size() >= 2, "bad zlib data");
    BitIn in{z};
    in.pos = 2;
    Bytes out;
    static const short lbase[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
                                    35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
    static const short lext[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
    static const short dbase[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129,
                                    193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
    static const short dext[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
    int last;
    do {
        last = in.bits(1);
        int type = in.bits(2);
        if (type == 0) {
            in.buf = 0;
            in.cnt = 0;
            check(in.pos + 4 <= z.size(), "bad stored block");
            int len = z[in.pos] | z[in.pos + 1] << 8;
            in.pos += 4;
            check(in.pos + size_t(len) <= z.size(), "bad stored block");
            out.insert(out.end(), z.begin() + std::ptrdiff_t(in.pos), z.begin() + std::ptrdiff_t(in.pos + size_t(len)));
            in.pos += size_t(len);
            continue;
        }
        check(type != 3, "bad block type");
        Huff lit, dist;
        if (type == 1) {
            std::vector<int> l(288);
            for (int i = 0; i < 288; i++) l[size_t(i)] = i < 144 ? 8 : i < 256 ? 9 : i < 280 ? 7 : 8;
            lit.build(l);
            dist.build(std::vector<int>(30, 5));
        } else {
            int nlen = in.bits(5) + 257, ndist = in.bits(5) + 1, ncode = in.bits(4) + 4;
            static const int order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
            std::vector<int> cl(19, 0);
            for (int i = 0; i < ncode; i++) cl[size_t(order[i])] = in.bits(3);
            Huff ch;
            ch.build(cl);
            std::vector<int> lens;
            while (int(lens.size()) < nlen + ndist) {
                int sym = ch.decode(in);
                if (sym < 16)
                    lens.push_back(sym);
                else if (sym == 16) {
                    check(!lens.empty(), "bad code lengths");
                    int r = 3 + in.bits(2), v = lens.back();
                    while (r--) lens.push_back(v);
                } else {
                    int r = sym == 17 ? 3 + in.bits(3) : 11 + in.bits(7);
                    while (r--) lens.push_back(0);
                }
            }
            lit.build(std::vector<int>(lens.begin(), lens.begin() + nlen));
            dist.build(std::vector<int>(lens.begin() + nlen, lens.begin() + nlen + ndist));
        }
        for (;;) {
            int sym = lit.decode(in);
            if (sym < 256)
                out.push_back(uint8_t(sym));
            else if (sym == 256)
                break;
            else {
                sym -= 257;
                check(sym < 29, "bad length code");
                int len = lbase[sym] + in.bits(lext[sym]);
                int ds = dist.decode(in);
                check(ds < 30, "bad distance code");
                size_t d = size_t(dbase[ds] + in.bits(dext[ds]));
                check(d <= out.size(), "bad distance");
                for (int k = 0; k < len; k++) out.push_back(out[out.size() - d]);
            }
        }
    } while (!last);
    return out;
}

RgbImage decode_png(const Bytes &png)
{
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    check(png.size() > 8 && std::memcmp(png.data(), sig, 8) == 0, "not a PNG file");
    size_t o = 8;
    int w = 0, h = 0, depth = 0, ctype = 0, interlace = 0;
    Bytes idat, plte;
    auto be = [&](size_t p) { return uint32_t(png[p]) << 24 | uint32_t(png[p + 1]) << 16 | uint32_t(png[p + 2]) << 8 | png[p + 3]; };
    while (o + 8 <= png.size()) {
        uint32_t len = be(o);
        std::string type(reinterpret_cast<const char *>(png.data() + o + 4), 4);
        check(o + 12 + len <= png.size(), "PNG chunk past the end");
        const uint8_t *d = png.data() + o + 8;
        if (type == "IHDR") {
            w = int(be(o + 8));
            h = int(be(o + 12));
            depth = d[8];
            ctype = d[9];
            interlace = d[12];
        } else if (type == "PLTE")
            plte.assign(d, d + len);
        else if (type == "IDAT")
            idat.insert(idat.end(), d, d + len);
        else if (type == "IEND")
            break;
        o += 12 + len;
    }
    check(w > 0 && h > 0 && w < 16384 && h < 16384, "bad PNG size");
    check(depth == 8 && interlace == 0 && (ctype == 0 || ctype == 2 || ctype == 3 || ctype == 4 || ctype == 6),
          "only 8-bit, non-interlaced PNG files are supported");
    int ch = ctype == 0 ? 1 : ctype == 2 ? 3 : ctype == 3 ? 1 : ctype == 4 ? 2 : 4;
    Bytes raw = inflate_zlib(idat);
    size_t stride = size_t(w) * size_t(ch);
    check(raw.size() >= (stride + 1) * size_t(h), "PNG data too short");
    Bytes px(stride * size_t(h));
    for (int y = 0; y < h; y++) {
        int f = raw[size_t(y) * (stride + 1)];
        const uint8_t *src = raw.data() + size_t(y) * (stride + 1) + 1;
        uint8_t *dst = px.data() + size_t(y) * stride;
        const uint8_t *up = y ? dst - stride : nullptr;
        for (size_t i = 0; i < stride; i++) {
            int a = i >= size_t(ch) ? dst[i - size_t(ch)] : 0, b = up ? up[i] : 0,
                c = (up && i >= size_t(ch)) ? up[i - size_t(ch)] : 0, v = src[i];
            switch (f) {
            case 1: v += a; break;
            case 2: v += b; break;
            case 3: v += (a + b) / 2; break;
            case 4: {
                int p = a + b - c, pa = std::abs(p - a), pb = std::abs(p - b), pc = std::abs(p - c);
                v += (pa <= pb && pa <= pc) ? a : pb <= pc ? b : c;
                break;
            }
            }
            dst[i] = uint8_t(v);
        }
    }
    RgbImage im(w, h);
    for (int i = 0; i < w * h; i++) {
        const uint8_t *p = px.data() + size_t(i) * size_t(ch);
        uint8_t r, g, b;
        if (ctype == 3) {
            size_t k = size_t(p[0]) * 3;
            check(k + 2 < plte.size(), "PNG palette index out of range");
            r = plte[k];
            g = plte[k + 1];
            b = plte[k + 2];
        } else if (ch <= 2)
            r = g = b = p[0];
        else {
            r = p[0];
            g = p[1];
            b = p[2];
        }
        im.set(i % w, i / w, r, g, b);
    }
    return im;
}

RgbImage load_png(const std::string &path) { return decode_png(read_file(path)); }

Bytes image_to_palette(const RgbImage &im, const Palette &pal, int limit)
{
    Bytes out(size_t(im.w) * size_t(im.h));
    std::map<uint32_t, uint8_t> cache;
    for (size_t i = 0; i < out.size(); i++) {
        uint32_t key = uint32_t(im.px[i * 3]) << 16 | uint32_t(im.px[i * 3 + 1]) << 8 | im.px[i * 3 + 2];
        auto it = cache.find(key);
        if (it == cache.end())
            it = cache.emplace(key, uint8_t(pal.nearest(im.px[i * 3], im.px[i * 3 + 1], im.px[i * 3 + 2], limit))).first;
        out[i] = it->second;
    }
    return out;
}

RgbImage resize_nearest(const RgbImage &im, int w, int h)
{
    RgbImage o(w, h);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            int sx = x * im.w / w, sy = y * im.h / h;
            size_t s = (size_t(sy) * size_t(im.w) + size_t(sx)) * 3;
            o.set(x, y, im.px[s], im.px[s + 1], im.px[s + 2]);
        }
    return o;
}

}  // namespace q32x
