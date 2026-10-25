#include "q32x/json.h"

#include <charconv>
#include <cmath>
#include <cstdio>

#include "q32x/bytes.h"

namespace q32x {

static const Json &null_json()
{
    static const Json n;
    return n;
}

const std::string &Json::as_string() const
{
    static const std::string empty;
    return t_ == String ? s_ : empty;
}

const Json &Json::operator[](size_t i) const { return t_ == Array && i < a_.size() ? a_[i] : null_json(); }

Json &Json::push(Json v)
{
    if (t_ != Array) {
        *this = array();
    }
    a_.push_back(std::move(v));
    return a_.back();
}

bool Json::has(const std::string &k) const
{
    if (t_ != Object) return false;
    for (auto &m : o_)
        if (m.first == k) return true;
    return false;
}

const Json &Json::operator[](const std::string &k) const
{
    if (t_ == Object)
        for (auto &m : o_)
            if (m.first == k) return m.second;
    return null_json();
}

Json &Json::set(const std::string &k, Json v)
{
    if (t_ != Object) *this = object();
    for (auto &m : o_)
        if (m.first == k) {
            m.second = std::move(v);
            return m.second;
        }
    o_.emplace_back(k, std::move(v));
    return o_.back().second;
}

Json &Json::at(const std::string &k)
{
    if (t_ != Object) *this = object();
    for (auto &m : o_)
        if (m.first == k) return m.second;
    o_.emplace_back(k, Json());
    return o_.back().second;
}

void Json::erase(const std::string &k)
{
    for (size_t i = 0; i < o_.size(); i++)
        if (o_[i].first == k) {
            o_.erase(o_.begin() + std::ptrdiff_t(i));
            return;
        }
}

// ------------------------------------------------------------ parser

namespace {
struct Parser {
    const std::string &s;
    size_t i = 0;
    int line = 1;
    [[noreturn]] void err(const std::string &m) { fail("JSON line " + std::to_string(line) + ": " + m); }
    void ws()
    {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n')) {
            if (s[i] == '\n') line++;
            i++;
        }
    }
    bool lit(const char *w)
    {
        size_t n = std::strlen(w);
        if (s.compare(i, n, w) == 0) {
            i += n;
            return true;
        }
        return false;
    }
    static void utf8(std::string &out, uint32_t c)
    {
        if (c < 0x80)
            out += char(c);
        else if (c < 0x800) {
            out += char(0xC0 | (c >> 6));
            out += char(0x80 | (c & 63));
        } else if (c < 0x10000) {
            out += char(0xE0 | (c >> 12));
            out += char(0x80 | ((c >> 6) & 63));
            out += char(0x80 | (c & 63));
        } else {
            out += char(0xF0 | (c >> 18));
            out += char(0x80 | ((c >> 12) & 63));
            out += char(0x80 | ((c >> 6) & 63));
            out += char(0x80 | (c & 63));
        }
    }
    uint32_t hex4()
    {
        if (i + 4 > s.size()) err("bad \\u escape");
        uint32_t v = 0;
        for (int k = 0; k < 4; k++) {
            char c = s[i++];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= uint32_t(c - '0');
            else if (c >= 'a' && c <= 'f') v |= uint32_t(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= uint32_t(c - 'A' + 10);
            else err("bad \\u escape");
        }
        return v;
    }
    std::string str()
    {
        std::string out;
        i++;   // "
        for (;;) {
            if (i >= s.size()) err("unterminated string");
            char c = s[i++];
            if (c == '"') break;
            if (c == '\\') {
                if (i >= s.size()) err("unterminated string");
                char e = s[i++];
                switch (e) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    uint32_t c1 = hex4();
                    if (c1 >= 0xD800 && c1 < 0xDC00 && i + 6 <= s.size() && s[i] == '\\' && s[i + 1] == 'u') {
                        i += 2;
                        uint32_t c2 = hex4();
                        c1 = 0x10000 + ((c1 - 0xD800) << 10) + (c2 - 0xDC00);
                    }
                    utf8(out, c1);
                    break;
                }
                default: err("bad escape");
                }
            } else
                out += c;
        }
        return out;
    }
    Json value(int depth)
    {
        if (depth > 200) err("nested too deeply");
        ws();
        if (i >= s.size()) err("unexpected end");
        char c = s[i];
        if (c == '{') {
            i++;
            Json o = Json::object();
            ws();
            if (i < s.size() && s[i] == '}') {
                i++;
                return o;
            }
            for (;;) {
                ws();
                if (i >= s.size() || s[i] != '"') err("expected a key");
                std::string k = str();
                ws();
                if (i >= s.size() || s[i] != ':') err("expected ':'");
                i++;
                o.set(k, value(depth + 1));
                ws();
                if (i < s.size() && s[i] == ',') {
                    i++;
                    continue;
                }
                if (i < s.size() && s[i] == '}') {
                    i++;
                    return o;
                }
                err("expected ',' or '}'");
            }
        }
        if (c == '[') {
            i++;
            Json a = Json::array();
            ws();
            if (i < s.size() && s[i] == ']') {
                i++;
                return a;
            }
            for (;;) {
                a.push(value(depth + 1));
                ws();
                if (i < s.size() && s[i] == ',') {
                    i++;
                    continue;
                }
                if (i < s.size() && s[i] == ']') {
                    i++;
                    return a;
                }
                err("expected ',' or ']'");
            }
        }
        if (c == '"') return Json(str());
        if (lit("true")) return Json(true);
        if (lit("false")) return Json(false);
        if (lit("null")) return Json();
        size_t j = i;
        if (j < s.size() && (s[j] == '-' || s[j] == '+')) j++;
        while (j < s.size() && (std::isdigit(static_cast<unsigned char>(s[j])) || s[j] == '.' || s[j] == 'e' ||
                                s[j] == 'E' || s[j] == '-' || s[j] == '+'))
            j++;
        if (j == i) err("unexpected character");
        double v = 0;
        const char *b = s.data() + i;
        if (*b == '+') b++;
        auto r = std::from_chars(b, s.data() + j, v);
        if (r.ec != std::errc() || r.ptr != s.data() + j) err("bad number");
        i = j;
        return Json(v);
    }
};
}  // namespace

Json Json::parse(const std::string &text)
{
    Parser p{text};
    Json v = p.value(0);
    p.ws();
    if (p.i != text.size()) p.err("text after the value");
    return v;
}

// ------------------------------------------------------------ writer

static void quote(std::string &out, const std::string &s)
{
    out += '"';
    for (unsigned char c : s) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20) {
                char b[8];
                std::snprintf(b, sizeof b, "\\u%04x", c);
                out += b;
            } else
                out += char(c);
        }
    }
    out += '"';
}

void Json::dump_to(std::string &out, int indent, int depth) const
{
    auto nl = [&](int d) {
        if (indent <= 0) return;
        out += '\n';
        out.append(size_t(indent * d), ' ');
    };
    switch (t_) {
    case Null: out += "null"; break;
    case Bool: out += b_ ? "true" : "false"; break;
    case Number: {
        if (!std::isfinite(n_)) {
            out += "null";
            break;
        }
        char b[40];
        if (n_ == std::trunc(n_) && std::fabs(n_) < 1e15)
            std::snprintf(b, sizeof b, "%lld", (long long)n_);
        else {
            auto r = std::to_chars(b, b + sizeof b, n_);
            *r.ptr = 0;
        }
        out += b;
        break;
    }
    case String: quote(out, s_); break;
    case Array: {
        if (a_.empty()) {
            out += "[]";
            break;
        }
        // short arrays of numbers on one line
        bool flat = a_.size() <= 64;
        for (auto &v : a_)
            if (v.t_ == Array || v.t_ == Object || v.t_ == String) flat = false;
        out += '[';
        for (size_t k = 0; k < a_.size(); k++) {
            if (k) out += flat ? ", " : ",";
            if (!flat) nl(depth + 1);
            a_[k].dump_to(out, indent, depth + 1);
        }
        if (!flat) nl(depth);
        out += ']';
        break;
    }
    case Object: {
        if (o_.empty()) {
            out += "{}";
            break;
        }
        out += '{';
        for (size_t k = 0; k < o_.size(); k++) {
            if (k) out += ',';
            nl(depth + 1);
            quote(out, o_[k].first);
            out += indent > 0 ? ": " : ":";
            o_[k].second.dump_to(out, indent, depth + 1);
        }
        nl(depth);
        out += '}';
        break;
    }
    }
}

std::string Json::dump(int indent) const
{
    std::string out;
    dump_to(out, indent, 0);
    if (indent > 0) out += '\n';
    return out;
}

}  // namespace q32x
