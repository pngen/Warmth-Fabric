#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>
#include <variant>
#include <stdexcept>
#include <cstdio>

namespace warmth::json {

// Minimal, self-contained JSON value. Objects are backed by std::map so that
// serialization is deterministic (keys are never reordered by the writer).
class Value {
public:
    using Array = std::vector<Value>;
    using Object = std::map<std::string, Value>;

    enum class Kind { Null, Bool, Number, String, Array, Object };

    Value() : data_(nullptr) {}
    Value(std::nullptr_t) : data_(nullptr) {}
    Value(bool b) : data_(b) {}
    Value(double d) : data_(d) {}
    Value(std::int64_t i) : data_(static_cast<double>(i)) {}
    Value(int i) : data_(static_cast<double>(i)) {}
    Value(std::string s) : data_(std::move(s)) {}
    Value(const char* s) : data_(std::string(s)) {}
    Value(Array a) : data_(std::move(a)) {}
    Value(Object o) : data_(std::move(o)) {}

    static Value object() { return Value(Object{}); }
    static Value array() { return Value(Array{}); }

    [[nodiscard]] Kind kind() const noexcept { return static_cast<Kind>(data_.index()); }

    [[nodiscard]] bool is_null() const noexcept { return kind() == Kind::Null; }
    [[nodiscard]] bool is_bool() const noexcept { return kind() == Kind::Bool; }
    [[nodiscard]] bool is_number() const noexcept { return kind() == Kind::Number; }
    [[nodiscard]] bool is_string() const noexcept { return kind() == Kind::String; }
    [[nodiscard]] bool is_array() const noexcept { return kind() == Kind::Array; }
    [[nodiscard]] bool is_object() const noexcept { return kind() == Kind::Object; }

    [[nodiscard]] bool as_bool() const { return std::get<bool>(data_); }
    [[nodiscard]] double as_number() const { return std::get<double>(data_); }
    [[nodiscard]] const std::string& as_string() const { return std::get<std::string>(data_); }
    [[nodiscard]] const Array& as_array() const { return std::get<Array>(data_); }
    [[nodiscard]] Array& as_array() { return std::get<Array>(data_); }
    [[nodiscard]] const Object& as_object() const { return std::get<Object>(data_); }
    [[nodiscard]] Object& as_object() { return std::get<Object>(data_); }

    // Object helpers.
    Value& set(std::string key, Value v) {
        as_object()[std::move(key)] = std::move(v);
        return *this;
    }
    Value& set(std::string key, const char* v) { return set(std::move(key), Value(v)); }
    Value& set(std::string key, bool v) { return set(std::move(key), Value(v)); }
    Value& set(std::string key, std::int64_t v) { return set(std::move(key), Value(v)); }
    Value& set(std::string key, double v) { return set(std::move(key), Value(v)); }

    [[nodiscard]] const Value* find(std::string_view key) const {
        if (!is_object()) return nullptr;
        const auto& o = as_object();
        const auto it = o.find(std::string(key));
        return it == o.end() ? nullptr : &it->second;
    }

    // Array helpers.
    void push(Value v) { as_array().push_back(std::move(v)); }

    // Serialize (deterministic, compact). An optional pretty flag provides
    // indented output for human consumption.
    std::string dump(int indent = -1) const;

    // Parse a JSON document. Throws std::runtime_error on any malformed input.
    static Value parse(std::string_view text);

private:
    std::variant<std::nullptr_t, bool, double, std::string, Array, Object> data_;
};

namespace detail {

inline void write_escaped(std::string& out, std::string_view s) {
    out.push_back('"');
    for (const char c : s) {
        switch (c) {
            case '"':  out += "\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned>(c));
                    out += buf;
                } else {
                    out.push_back(c);
                }
        }
    }
    out.push_back('"');
}

inline void write_value(std::string& out, const Value& v, int indent, int depth) {
    const bool pretty = indent >= 0;
    switch (v.kind()) {
        case Value::Kind::Null:   out += "null"; break;
        case Value::Kind::Bool:   out += v.as_bool() ? "true" : "false"; break;
        case Value::Kind::Number: {
            const double d = v.as_number();
            // Print integers without decimal point where exact.
            if (d == static_cast<double>(static_cast<std::int64_t>(d)) && (d > -1e15 && d < 1e15)) {
                char buf[32];
                std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(d));
                out += buf;
            } else {
                char buf[64];
                std::snprintf(buf, sizeof(buf), "%.17g", d);
                out += buf;
            }
            break;
        }
        case Value::Kind::String: write_escaped(out, v.as_string()); break;
        case Value::Kind::Array: {
            const auto& arr = v.as_array();
            out.push_back('[');
            for (std::size_t i = 0; i < arr.size(); ++i) {
                if (i) out.push_back(',');
                if (pretty) { out.push_back('\n'); out.append(static_cast<std::size_t>((depth + 1) * indent), ' '); }
                write_value(out, arr[i], indent, depth + 1);
            }
            if (pretty && !arr.empty()) { out.push_back('\n'); out.append(static_cast<std::size_t>(depth * indent), ' '); }
            out.push_back(']');
            break;
        }
        case Value::Kind::Object: {
            const auto& obj = v.as_object();
            out.push_back('{');
            bool first = true;
            for (const auto& kv : obj) {
                if (!first) out.push_back(',');
                first = false;
                if (pretty) { out.push_back('\n'); out.append(static_cast<std::size_t>((depth + 1) * indent), ' '); }
                write_escaped(out, kv.first);
                out.push_back(':');
                if (pretty) out.push_back(' ');
                write_value(out, kv.second, indent, depth + 1);
            }
            if (pretty && !obj.empty()) { out.push_back('\n'); out.append(static_cast<std::size_t>(depth * indent), ' '); }
            out.push_back('}');
            break;
        }
    }
}

} // namespace detail

inline std::string Value::dump(int indent) const {
    std::string out;
    detail::write_value(out, *this, indent, 0);
    return out;
}

namespace detail {

class Parser {
public:
    explicit Parser(std::string_view s) : s_(s), i_(0) {}

    Value parse() {
        skip_ws();
        Value v = parse_value();
        skip_ws();
        if (i_ != s_.size()) fail("trailing characters");
        return v;
    }

private:
    [[noreturn]] void fail(const std::string& msg) {
        throw std::runtime_error("JSON parse error at offset " + std::to_string(i_) + ": " + msg);
    }
    void skip_ws() {
        while (i_ < s_.size() && (s_[i_] == ' ' || s_[i_] == '\t' || s_[i_] == '\n' || s_[i_] == '\r')) ++i_;
    }
    char peek() const { return i_ < s_.size() ? s_[i_] : '\0'; }
    char take() { if (i_ >= s_.size()) fail("unexpected end"); return s_[i_++]; }

    Value parse_value() {
        skip_ws();
        const char c = peek();
        switch (c) {
            case '{': return parse_object();
            case '[': return parse_array();
            case '"': return Value(parse_string());
            case 't': expect("true"); return Value(true);
            case 'f': expect("false"); return Value(false);
            case 'n': expect("null"); return Value(nullptr);
            default:  return parse_number();
        }
    }

    void expect(std::string_view lit) {
        for (const char c : lit) {
            if (take() != c) fail("unexpected token");
        }
    }

    Value parse_object() {
        take(); // {
        Value o = Value::object();
        skip_ws();
        if (peek() == '}') { take(); return o; }
        for (;;) {
            skip_ws();
            if (peek() != '"') fail("expected object key");
            std::string key = parse_string();
            skip_ws();
            if (take() != ':') fail("expected colon");
            o.set(std::move(key), parse_value());
            skip_ws();
            const char c = take();
            if (c == '}') break;
            if (c != ',') fail("expected comma or closing brace");
        }
        return o;
    }

    Value parse_array() {
        take(); // [
        Value a = Value::array();
        skip_ws();
        if (peek() == ']') { take(); return a; }
        for (;;) {
            a.push(parse_value());
            skip_ws();
            const char c = take();
            if (c == ']') break;
            if (c != ',') fail("expected comma or closing bracket");
        }
        return a;
    }

    std::string parse_string() {
        take(); // opening quote
        std::string out;
        for (;;) {
            const char c = take();
            if (c == '"') break;
            if (c == '\\') {
                const char e = take();
                switch (e) {
                    case '"':  out.push_back('"'); break;
                    case '\\': out.push_back('\\'); break;
                    case '/':  out.push_back('/'); break;
                    case 'b':  out.push_back('\b'); break;
                    case 'f':  out.push_back('\f'); break;
                    case 'n':  out.push_back('\n'); break;
                    case 'r':  out.push_back('\r'); break;
                    case 't':  out.push_back('\t'); break;
                    case 'u': {
                        const unsigned cp = parse_hex4();
                        // Encode as UTF-8 (handles surrogate pairs).
                        unsigned u = cp;
                        if (cp >= 0xD800 && cp <= 0xDBFF) {
                            // A high surrogate must be followed by a low surrogate.
                            if (peek() == '\\' && i_ + 1 < s_.size() && s_[i_ + 1] == 'u') {
                                i_ += 2; // skip backslash and 'u'
                                const unsigned lo = parse_hex4();
                                u = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                            } else {
                                fail("lone high surrogate");
                            }
                        }
                        append_utf8(out, u);
                        break;
                    }
                    default: fail("bad escape");
                }
            } else {
                if (static_cast<unsigned char>(c) < 0x20) fail("control char in string");
                out.push_back(c);
            }
        }
        return out;
    }

    unsigned parse_hex4() {
        unsigned v = 0;
        for (int k = 0; k < 4; ++k) {
            const char c = take();
            int nv = 0;
            if (c >= '0' && c <= '9') nv = c - '0';
            else if (c >= 'a' && c <= 'f') nv = c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') nv = c - 'A' + 10;
            else fail("bad hex");
            v = (v << 4) | static_cast<unsigned>(nv);
        }
        return v;
    }

    static void append_utf8(std::string& out, unsigned u) {
        if (u < 0x80) { out.push_back(static_cast<char>(u)); }
        else if (u < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (u >> 6)));
            out.push_back(static_cast<char>(0x80 | (u & 0x3F)));
        } else if (u < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (u >> 12)));
            out.push_back(static_cast<char>(0x80 | ((u >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (u & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (u >> 18)));
            out.push_back(static_cast<char>(0x80 | ((u >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((u >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (u & 0x3F)));
        }
    }

    Value parse_number() {
        const std::size_t start = i_;
        if (peek() == '-') ++i_;
        if (peek() == '0') ++i_;
        else if (peek() >= '1' && peek() <= '9') { while (peek() >= '0' && peek() <= '9') ++i_; }
        else fail("bad number");
        if (peek() == '.') { ++i_; if (!(peek() >= '0' && peek() <= '9')) fail("bad fraction"); while (peek() >= '0' && peek() <= '9') ++i_; }
        if (peek() == 'e' || peek() == 'E') {
            ++i_;
            if (peek() == '+' || peek() == '-') ++i_;
            if (!(peek() >= '0' && peek() <= '9')) fail("bad exponent");
            while (peek() >= '0' && peek() <= '9') ++i_;
        }
        const std::string tok(s_.substr(start, i_ - start));
        try {
            return Value(std::stod(tok));
        } catch (...) { fail("bad number value"); }
    }

    std::string_view s_;
    std::size_t i_ = 0;
};

} // namespace detail

inline Value Value::parse(std::string_view text) {
    detail::Parser p(text);
    return p.parse();
}

// Convenience: JSON object literal builder.
inline Value obj() { return Value::object(); }

} // namespace warmth::json
