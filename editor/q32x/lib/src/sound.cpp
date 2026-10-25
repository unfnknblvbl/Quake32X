// Sounds: every sound is stored 8-bit unsigned at 11025 Hz, the rate the
// game mixes at (src/snd.c), so the mixer reads each sample as it is.
#include <algorithm>
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

}  // namespace q32x
