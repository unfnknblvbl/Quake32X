// Byte buffers and small helpers shared by the converters.
//
// The converters were first written in Python (tools/*.py in the game's
// repository); these helpers reproduce Python's arithmetic where it matters
// for byte-identical output: round() rounds half to even, int() truncates,
// // and % floor.
#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace q32x {

using Bytes = std::vector<uint8_t>;

struct Error : std::runtime_error {
    using std::runtime_error::runtime_error;
};

[[noreturn]] void fail(const std::string &msg);
inline void check(bool ok, const std::string &msg)
{
    if (!ok) fail(msg);
}

// Python's round(x): to the nearest integer, ties to even
inline int64_t pyround(double x)
{
    double r = std::nearbyint(x);   // default rounding mode: ties to even
    return static_cast<int64_t>(r);
}
// Python's round(x, n) for n >= 0 (correctly rounded through the decimal form)
double pyround_digits(double x, int n);
// Python's math.floor / math.ceil (to integers)
inline int64_t pyfloor(double x) { return static_cast<int64_t>(std::floor(x)); }
inline int64_t pyceil(double x) { return static_cast<int64_t>(std::ceil(x)); }
// Python's a // b and a % b for integers
inline int64_t floordiv(int64_t a, int64_t b)
{
    int64_t q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0))) q--;
    return q;
}
inline int64_t floormod(int64_t a, int64_t b) { return a - floordiv(a, b) * b; }

// Python's float(s) (C locale, surrounding white space allowed)
double pyfloat(std::string_view s);
// int(float(s))
inline int pyintf(std::string_view s) { return static_cast<int>(std::trunc(pyfloat(s))); }
std::vector<std::string> split_ws(std::string_view s);
std::string lower(std::string s);
inline bool starts_with(std::string_view s, std::string_view p) { return s.substr(0, p.size()) == p; }
inline bool ends_with(std::string_view s, std::string_view p)
{
    return s.size() >= p.size() && s.substr(s.size() - p.size()) == p;
}
std::string strip(std::string_view s);
std::string replace_all(std::string s, std::string_view from, std::string_view to);

// a growing output buffer, big- or little-endian
class Buf {
public:
    explicit Buf(bool big = true) : big_(big) {}
    bool big() const { return big_; }
    Bytes &data() { return d_; }
    const Bytes &data() const { return d_; }
    size_t size() const { return d_.size(); }
    void align(size_t a)
    {
        while (d_.size() % a) d_.push_back(0);
    }
    // append b (after aligning), returning its offset
    size_t add(const Bytes &b, size_t a = 4)
    {
        align(a);
        size_t o = d_.size();
        d_.insert(d_.end(), b.begin(), b.end());
        return o;
    }
    size_t add(const Buf &b, size_t a = 4) { return add(b.d_, a); }
    void u8(int64_t v)
    {
        check(v >= 0 && v <= 255, "u8 out of range: " + std::to_string(v));
        d_.push_back(uint8_t(v));
    }
    void i8(int64_t v)
    {
        check(v >= -128 && v <= 127, "i8 out of range: " + std::to_string(v));
        d_.push_back(uint8_t(int8_t(v)));
    }
    void u16(int64_t v)
    {
        check(v >= 0 && v <= 0xFFFF, "u16 out of range: " + std::to_string(v));
        put(uint64_t(v), 2);
    }
    void i16(int64_t v)
    {
        check(v >= -32768 && v <= 32767, "i16 out of range: " + std::to_string(v));
        put(uint64_t(uint16_t(int16_t(v))), 2);
    }
    void u32(int64_t v)
    {
        check(v >= 0 && v <= 0xFFFFFFFFll, "u32 out of range: " + std::to_string(v));
        put(uint64_t(v), 4);
    }
    void i32(int64_t v)
    {
        check(v >= INT32_MIN && v <= INT32_MAX, "i32 out of range: " + std::to_string(v));
        put(uint64_t(uint32_t(int32_t(v))), 4);
    }
    void bytes(const Bytes &b) { d_.insert(d_.end(), b.begin(), b.end()); }
    void bytes(const uint8_t *p, size_t n) { d_.insert(d_.end(), p, p + n); }
    void zeros(size_t n) { d_.insert(d_.end(), n, 0); }
    // overwrite at an offset
    void put_at(size_t o, uint64_t v, int n)
    {
        for (int i = 0; i < n; i++) {
            int sh = big_ ? 8 * (n - 1 - i) : 8 * i;
            d_.at(o + i) = uint8_t(v >> sh);
        }
    }
    uint64_t get_at(size_t o, int n) const
    {
        uint64_t v = 0;
        for (int i = 0; i < n; i++) {
            int sh = big_ ? 8 * (n - 1 - i) : 8 * i;
            v |= uint64_t(d_.at(o + i)) << sh;
        }
        return v;
    }

private:
    void put(uint64_t v, int n)
    {
        for (int i = 0; i < n; i++) {
            int sh = big_ ? 8 * (n - 1 - i) : 8 * i;
            d_.push_back(uint8_t(v >> sh));
        }
    }
    bool big_;
    Bytes d_;
};

// little-endian reads from Quake's files
struct Reader {
    const uint8_t *p;
    size_t n;
    Reader(const Bytes &b) : p(b.data()), n(b.size()) {}
    Reader(const uint8_t *p_, size_t n_) : p(p_), n(n_) {}
    void need(size_t o, size_t k) const
    {
        if (o > n || k > n - o) fail("read past the end of the data");
    }
    uint8_t u8(size_t o) const { need(o, 1); return p[o]; }
    int8_t i8(size_t o) const { return int8_t(u8(o)); }
    uint16_t u16(size_t o) const { need(o, 2); return uint16_t(p[o] | p[o + 1] << 8); }
    int16_t i16(size_t o) const { return int16_t(u16(o)); }
    uint32_t u32(size_t o) const
    {
        need(o, 4);
        return uint32_t(p[o]) | uint32_t(p[o + 1]) << 8 | uint32_t(p[o + 2]) << 16 | uint32_t(p[o + 3]) << 24;
    }
    int32_t i32(size_t o) const { return int32_t(u32(o)); }
    float f32(size_t o) const
    {
        uint32_t v = u32(o);
        float f;
        std::memcpy(&f, &v, 4);
        return f;
    }
    // a NUL-terminated name in a fixed field (latin-1, kept as bytes)
    std::string name(size_t o, size_t len) const
    {
        need(o, len);
        size_t k = 0;
        while (k < len && p[o + k]) k++;
        return std::string(reinterpret_cast<const char *>(p + o), k);
    }
    Bytes slice(size_t o, size_t len) const
    {
        // like Python slicing: clipped to the data
        if (o >= n) return {};
        size_t k = std::min(len, n - o);
        return Bytes(p + o, p + o + k);
    }
};

Bytes read_file(const std::string &path);

// the cartridge's shared library: textures, models and the sound bank, each
// stored once however many levels use them. A level refers to an entry by a
// 32-bit field that holds the entry's offset in the library; the cartridge
// builder makes it an offset from the level's own start (the library lies
// after every level in the 4 MB layout, so it stays positive).
struct SharedLib {
    Bytes data;                              // big-endian, like the level blobs
    std::vector<std::pair<std::string, uint32_t>> index;   // contents -> offset
    size_t saved = 0;                        // bytes the sharing saved
    // the entry's offset (4-aligned); the same bytes again give the same entry
    uint32_t add(const Bytes &b, size_t align = 4)
    {
        std::string key(b.begin(), b.end());
        for (auto &kv : index)
            if (kv.first == key && kv.second % align == 0) { saved += b.size(); return kv.second; }
        while (data.size() % align) data.push_back(0);
        uint32_t o = uint32_t(data.size());
        data.insert(data.end(), b.begin(), b.end());
        index.push_back({std::move(key), o});
        return o;
    }
};
void write_file(const std::string &path, const Bytes &b);

}  // namespace q32x
