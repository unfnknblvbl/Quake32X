// A small JSON value with a parser and a writer, for project files and the
// bot route. Objects keep their keys in insertion order, so files written
// by the editor diff cleanly.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace q32x {

class Json {
public:
    enum Type { Null, Bool, Number, String, Array, Object };

    Json() = default;
    Json(std::nullptr_t) {}
    Json(bool b) : t_(Bool), b_(b) {}
    Json(int v) : t_(Number), n_(v) {}
    Json(int64_t v) : t_(Number), n_(double(v)) {}
    Json(size_t v) : t_(Number), n_(double(v)) {}
    Json(double v) : t_(Number), n_(v) {}
    Json(const char *s) : t_(String), s_(s) {}
    Json(std::string s) : t_(String), s_(std::move(s)) {}
    static Json array() { Json j; j.t_ = Array; return j; }
    static Json object() { Json j; j.t_ = Object; return j; }

    Type type() const { return t_; }
    bool is_null() const { return t_ == Null; }
    bool is_object() const { return t_ == Object; }
    bool is_array() const { return t_ == Array; }
    bool is_number() const { return t_ == Number; }
    bool is_string() const { return t_ == String; }

    bool as_bool(bool def = false) const { return t_ == Bool ? b_ : t_ == Number ? n_ != 0 : def; }
    double as_number(double def = 0) const { return t_ == Number ? n_ : t_ == Bool ? double(b_) : def; }
    int as_int(int def = 0) const { return t_ == Number ? int(n_) : t_ == Bool ? int(b_) : def; }
    const std::string &as_string() const;
    std::string as_string(const std::string &def) const { return t_ == String ? s_ : def; }

    // arrays
    size_t size() const { return t_ == Array ? a_.size() : t_ == Object ? o_.size() : 0; }
    const Json &operator[](size_t i) const;
    Json &push(Json v);
    const std::vector<Json> &items() const { return a_; }

    // objects
    bool has(const std::string &k) const;
    const Json &operator[](const std::string &k) const;   // a null Json if absent
    const Json &operator[](const char *k) const { return (*this)[std::string(k)]; }
    Json &set(const std::string &k, Json v);
    Json &at(const std::string &k);                       // created if absent
    void erase(const std::string &k);
    const std::vector<std::pair<std::string, Json>> &members() const { return o_; }

    static Json parse(const std::string &text);   // throws q32x::Error
    std::string dump(int indent = 2) const;

private:
    void dump_to(std::string &out, int indent, int depth) const;
    Type t_ = Null;
    bool b_ = false;
    double n_ = 0;
    std::string s_;
    std::vector<Json> a_;
    std::vector<std::pair<std::string, Json>> o_;
};

}  // namespace q32x
