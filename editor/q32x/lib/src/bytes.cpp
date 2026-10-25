#include "q32x/bytes.h"

#include <algorithm>
#include <charconv>
#include <filesystem>
#include <fstream>

namespace q32x {

// Python's str.isspace() for a latin-1 character
static bool pyspace(char ch)
{
    unsigned char c = static_cast<unsigned char>(ch);
    return c == ' ' || (c >= 9 && c <= 13) || (c >= 0x1c && c <= 0x1f) || c == 0x85 || c == 0xa0;
}

void fail(const std::string &msg)
{
    throw Error(msg);
}

double pyround_digits(double x, int n)
{
    // Python rounds through the correctly rounded decimal string
    char buf[64];
    auto r = std::to_chars(buf, buf + sizeof buf, x, std::chars_format::fixed, n);
    check(r.ec == std::errc(), "number too large to round");
    double y = 0;
    std::from_chars(buf, r.ptr, y);
    if (y == 0) y = std::copysign(0.0, x);
    return y;
}

double pyfloat(std::string_view s)
{
    size_t a = 0, b = s.size();
    while (a < b && pyspace(s[a])) a++;
    while (b > a && pyspace(s[b - 1])) b--;
    std::string_view t = s.substr(a, b - a);
    bool neg = false;
    if (!t.empty() && (t[0] == '+' || t[0] == '-')) {
        neg = t[0] == '-';
        t.remove_prefix(1);
    }
    double v = 0;
    auto r = std::from_chars(t.data(), t.data() + t.size(), v);
    if (t.empty() || r.ec != std::errc() || r.ptr != t.data() + t.size())
        fail("could not convert string to float: '" + std::string(s) + "'");
    return neg ? -v : v;
}

std::vector<std::string> split_ws(std::string_view s)
{
    std::vector<std::string> out;
    size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && pyspace(s[i])) i++;
        size_t j = i;
        while (j < s.size() && !pyspace(s[j])) j++;
        if (j > i) out.emplace_back(s.substr(i, j - i));
        i = j;
    }
    return out;
}

std::string lower(std::string s)
{
    for (auto &c : s)
        if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    return s;
}

std::string strip(std::string_view s)
{
    size_t a = 0, b = s.size();
    while (a < b && pyspace(s[a])) a++;
    while (b > a && pyspace(s[b - 1])) b--;
    return std::string(s.substr(a, b - a));
}

std::string replace_all(std::string s, std::string_view from, std::string_view to)
{
    if (from.empty()) return s;
    size_t p = 0;
    while ((p = s.find(from, p)) != std::string::npos) {
        s.replace(p, from.size(), to);
        p += to.size();
    }
    return s;
}

static std::filesystem::path fspath(const std::string &utf8)
{
    return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
}

Bytes read_file(const std::string &path)
{
    std::ifstream f(fspath(path), std::ios::binary);
    if (!f) fail("cannot open " + path);
    Bytes b((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return b;
}

void write_file(const std::string &path, const Bytes &b)
{
    std::ofstream f(fspath(path), std::ios::binary | std::ios::trunc);
    if (!f) fail("cannot write " + path);
    f.write(reinterpret_cast<const char *>(b.data()), std::streamsize(b.size()));
    if (!f) fail("error writing " + path);
}

}  // namespace q32x
