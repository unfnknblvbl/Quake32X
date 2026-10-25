// Sounds: every sound is stored 8-bit unsigned at 11025 Hz, the rate the
// game mixes at (src/snd.c), so the mixer reads each sample as it is.
#include <algorithm>
#include <array>
#include <climits>
#include <cmath>

#include "q32x/gamedata.h"

namespace q32x {

Sound apply_sound_edit(const Sound &s, const SoundEdit &e)
{
    Sound o = s;
    if (e.gain != 1.0)
        for (auto &b : o.data)
            b = uint8_t(std::clamp(128 + int(std::lround((int(b) - 128) * e.gain)), 0, 255));
    if (e.trim && !o.data.empty()) {
        auto loud = [&](uint8_t b) { return std::abs(int(b) - 128) > e.threshold; };
        size_t a = 0, z = o.data.size();
        // a looping sound keeps its loop: only the lead-in before it may go
        size_t keep_from = o.loop >= 0 ? size_t(o.loop) : z;
        while (a < z && a < keep_from && !loud(o.data[a])) a++;
        if (o.loop < 0)
            while (z > a + 1 && !loud(o.data[z - 1])) z--;
        o.data = Bytes(o.data.begin() + std::ptrdiff_t(a), o.data.begin() + std::ptrdiff_t(z));
        if (o.loop >= 0) o.loop -= int(a);
        if (o.data.empty()) o.data.push_back(128);
    }
    return o;
}

Sound convert_wav(const Bytes &d)
{
    check(d.size() >= 12 && std::memcmp(d.data(), "RIFF", 4) == 0 && std::memcmp(d.data() + 8, "WAVE", 4) == 0,
          "not a .wav file");
    Reader r(d);
    size_t o = 12;
    int tag = 0, ch = 0, rate = 0, bits = 0;
    Bytes data;
    int64_t loop = -1;
    while (o + 8 <= d.size()) {
        std::string cid(reinterpret_cast<const char *>(d.data() + o), 4);
        uint32_t size = r.u32(o + 4);
        Bytes body = r.slice(o + 8, size);
        if (cid == "fmt " && body.size() >= 16) {
            Reader b(body);
            tag = b.u16(0);
            ch = b.u16(2);
            rate = int(b.u32(4));
            bits = b.u16(14);
        } else if (cid == "data")
            data = body;
        else if (cid == "cue " && body.size() >= 28)
            loop = Reader(body).i32(24);
        o += 8 + size_t(size) + (size & 1);
    }
    check(tag == 1 && ch >= 1 && (bits == 8 || bits == 16) && rate > 0, "only 8 or 16-bit PCM .wav files are supported");
    // to mono floats
    size_t bps = size_t(bits / 8) * size_t(ch), n = data.size() / bps;
    std::vector<double> in(n);
    for (size_t i = 0; i < n; i++) {
        double acc = 0;
        for (int c = 0; c < ch; c++) {
            size_t p = i * bps + size_t(c) * size_t(bits / 8);
            acc += bits == 8 ? (int(data[p]) - 128) / 128.0 : int16_t(data[p] | data[p + 1] << 8) / 32768.0;
        }
        in[i] = acc / ch;
    }
    // to 11025 Hz: each output sample the average of what it covers
    Sound s;
    double ratio = double(rate) / 11025.0;
    size_t m = size_t(std::floor(n / ratio));
    for (size_t k = 0; k < m; k++) {
        double a = k * ratio, b = std::max(a + 1e-9, (k + 1) * ratio);
        size_t i0 = size_t(a), i1 = std::min(n, size_t(std::ceil(b)));
        double acc = 0, w = 0;
        for (size_t i = i0; i < i1; i++) {
            double lo = std::max(a, double(i)), hi = std::min(b, double(i + 1));
            if (hi > lo) {
                acc += in[i] * (hi - lo);
                w += hi - lo;
            }
        }
        double v = w > 0 ? acc / w : 0;
        s.data.push_back(uint8_t(std::clamp(128 + int(std::lround(v * 127)), 0, 255)));
    }
    if (s.data.empty()) s.data.push_back(128);
    s.loop = loop >= 0 ? int(std::min<int64_t>(int64_t(s.data.size()) - 1, int64_t(std::llround(loop / ratio)))) : -1;
    return s;
}

// ------------------------------------------------------------ smaller formats
// (the decoder is src/snd.c's; decode_sound below is the same arithmetic)

static const int PK_BLOCK = 256, PK_BYTES = 1 + 16 + 256 / 2;

// the samples as they are stored before packing: 11 kHz, or every other one
// (each the middle of a small low-pass over its neighbours; played twice)
static Bytes stored_pcm(const Bytes &d, int format)
{
    if (!(format & SF_HALF)) return d;
    Bytes o((d.size() + 1) / 2);
    for (size_t k = 0; k < o.size(); k++) {
        size_t i = 2 * k;
        int a = i > 0 ? d[i - 1] : d[i], b = d[i], c = i + 1 < d.size() ? d[i + 1] : d[i];
        o[k] = uint8_t((a + 2 * b + c + 2) / 4);
    }
    return o;
}

// one block in 4 bits: each sample the one before plus one of 16 steps.
// The steps are fitted to the block (k-means over the steps it needs, then
// again over those the encoding met), and each sample takes the step that
// lands nearest, so the errors do not add up.
static void pack_block(const int *x, int n, uint8_t out[PK_BYTES])
{
    auto encode = [&](const std::array<int, 16> &tab, std::vector<int> &nib, std::vector<int> &want) {
        int v = x[0], err = 0;
        nib.assign(size_t(n), 0);
        want.assign(size_t(n), 0);
        for (int i = 0; i < n; i++) {
            int best = -1, bd = INT_MAX;
            for (int k = 0; k < 16; k++) {
                int w = v + tab[size_t(k)];
                if (w < 0 || w > 255) continue;
                int d = std::abs(w - x[i]);
                if (d < bd) {
                    bd = d;
                    best = k;
                }
            }
            want[size_t(i)] = x[i] - v;
            nib[size_t(i)] = best;
            v += tab[size_t(best)];
            err += bd;
        }
        return err;
    };
    auto fit = [](std::vector<int> d) {
        std::sort(d.begin(), d.end());
        std::array<double, 16> c{};
        for (int k = 0; k < 16; k++) c[size_t(k)] = d[std::min(d.size() - 1, size_t((k + 0.5) * double(d.size()) / 16))];
        for (int it = 0; it < 12; it++) {
            std::array<double, 16> sum{};
            std::array<int, 16> cnt{};
            for (int v : d) {
                int b = 0;
                for (int k = 1; k < 16; k++)
                    if (std::fabs(v - c[size_t(k)]) < std::fabs(v - c[size_t(b)])) b = k;
                sum[size_t(b)] += v;
                cnt[size_t(b)]++;
            }
            for (int k = 0; k < 16; k++)
                if (cnt[size_t(k)]) c[size_t(k)] = sum[size_t(k)] / cnt[size_t(k)];
        }
        std::array<int, 16> t{};
        for (int k = 0; k < 16; k++) t[size_t(k)] = int(std::clamp(std::lround(c[size_t(k)]), -128L, 127L));
        // a zero step (silence stays silent) in place of the smallest
        int z = 0;
        for (int k = 1; k < 16; k++)
            if (std::abs(t[size_t(k)]) < std::abs(t[size_t(z)])) z = k;
        t[size_t(z)] = 0;
        // and the largest moves both ways, so any sample can be reached
        return t;
    };
    std::vector<int> d(static_cast<size_t>(n));
    for (int i = 0; i < n; i++) d[size_t(i)] = x[i] - (i ? x[i - 1] : x[0]);
    std::array<int, 16> tab = fit(d), best_tab = tab;
    std::vector<int> nib, want, best_nib;
    int best_err = encode(tab, nib, want);
    best_nib = nib;
    for (int round = 0; round < 3; round++) {
        tab = fit(want);
        int e = encode(tab, nib, want);
        if (e < best_err) {
            best_err = e;
            best_tab = tab;
            best_nib = nib;
        }
    }
    out[0] = uint8_t(x[0]);
    for (int k = 0; k < 16; k++) out[1 + k] = uint8_t(int8_t(best_tab[size_t(k)]));
    for (int i = 0; i < PK_BLOCK; i += 2) {
        int a = i < n ? best_nib[size_t(i)] : 0, b = i + 1 < n ? best_nib[size_t(i + 1)] : 0;
        out[17 + i / 2] = uint8_t(a << 4 | b);
    }
}

Bytes encode_sound(const Sound &s, int format)
{
    Bytes pcm = stored_pcm(s.data, format);
    if (!(format & SF_PACK)) return pcm;
    Bytes out;
    for (size_t b0 = 0; b0 < pcm.size(); b0 += size_t(PK_BLOCK)) {
        int n = int(std::min(pcm.size() - b0, size_t(PK_BLOCK)));
        int x[256];
        for (int i = 0; i < n; i++) x[i] = pcm[b0 + size_t(i)];
        uint8_t blk[PK_BYTES];
        pack_block(x, n, blk);
        out.insert(out.end(), blk, blk + PK_BYTES);
    }
    return out;
}

uint32_t sound_length_field(const Sound &s, int format)
{
    return uint32_t(s.data.size()) | (uint32_t(format & 3) << 28);
}

Bytes decode_sound(const Bytes &st, uint32_t field)
{
    int format = int(field >> 28);
    size_t len = field & 0x0FFFFFFF, srclen = format & SF_HALF ? (len + 1) / 2 : len;
    std::vector<int> src(srclen);
    if (format & SF_PACK) {
        int v = 0;
        for (size_t i = 0; i < srclen; i++) {
            size_t b = i / size_t(PK_BLOCK) * size_t(PK_BYTES);
            int j = int(i % size_t(PK_BLOCK));
            if (j == 0) v = st.at(b);
            int nb = st.at(b + 17 + size_t(j >> 1));
            v += int8_t(st.at(b + 1 + size_t(j & 1 ? nb & 15 : nb >> 4)));
            src[i] = v;
        }
    } else
        for (size_t i = 0; i < srclen; i++) src[i] = st.at(i);
    Bytes o(len);
    for (size_t i = 0; i < len; i++) {
        if (!(format & SF_HALF)) {
            o[i] = uint8_t(src[i]);
            continue;
        }
        o[i] = uint8_t(src[i >> 1]);   // (each stored sample twice, as the mixer reads it)
    }
    return o;
}

}  // namespace q32x
