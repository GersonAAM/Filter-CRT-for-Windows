#include "Json.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

const JsonValue* JsonValue::Find(const char* key) const {
    if (type != Type::Object) return nullptr;
    for (size_t i = 0; i < keys.size(); ++i)
        if (keys[i] == key) return &items[i];
    return nullptr;
}

namespace {

class Parser {
public:
    Parser(const char* begin, const char* end) : p_(begin), end_(end) {}

    bool ParseDocument(JsonValue& out) {
        if (!Value(out, 0)) return false;
        SkipSpace();
        return p_ == end_;
    }

private:
    const char* p_;
    const char* end_;

    void SkipSpace() {
        while (p_ < end_ && (*p_ == ' ' || *p_ == '\t' || *p_ == '\n' || *p_ == '\r')) ++p_;
    }

    bool Literal(const char* text) {
        size_t n = strlen(text);
        if (static_cast<size_t>(end_ - p_) < n || strncmp(p_, text, n) != 0) return false;
        p_ += n;
        return true;
    }

    bool Value(JsonValue& v, int depth) {
        if (depth > 64) return false;
        SkipSpace();
        if (p_ >= end_) return false;
        switch (*p_) {
        case '{': return Object(v, depth);
        case '[': return Array(v, depth);
        case '"': v.type = JsonValue::Type::String; return String(v.string);
        case 't': v.type = JsonValue::Type::Bool; v.boolean = true; return Literal("true");
        case 'f': v.type = JsonValue::Type::Bool; v.boolean = false; return Literal("false");
        case 'n': v.type = JsonValue::Type::Null; return Literal("null");
        default: return Number(v);
        }
    }

    bool Number(JsonValue& v) {
        std::string text;
        while (p_ < end_ && *p_ != '\0' && strchr("+-0123456789.eE", *p_)) text += *p_++;
        if (text.empty()) return false;
        char* stop = nullptr;
        v.number = strtod(text.c_str(), &stop);
        if (*stop != '\0') return false;
        v.type = JsonValue::Type::Number;
        return true;
    }

    static void AppendUtf8(std::string& out, unsigned cp) {
        if (cp < 0x80) {
            out += static_cast<char>(cp);
        } else if (cp < 0x800) {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }

    bool Hex4(unsigned& out) {
        if (end_ - p_ < 4) return false;
        out = 0;
        for (int i = 0; i < 4; ++i) {
            char c = *p_++;
            out <<= 4;
            if (c >= '0' && c <= '9') out |= static_cast<unsigned>(c - '0');
            else if (c >= 'a' && c <= 'f') out |= static_cast<unsigned>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') out |= static_cast<unsigned>(c - 'A' + 10);
            else return false;
        }
        return true;
    }

    bool String(std::string& out) {
        ++p_; // opening quote
        out.clear();
        while (p_ < end_) {
            char c = *p_++;
            if (c == '"') return true;
            if (c != '\\') { out += c; continue; }
            if (p_ >= end_) return false;
            char e = *p_++;
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
                unsigned cp;
                if (!Hex4(cp)) return false;
                if (cp >= 0xD800 && cp <= 0xDBFF && end_ - p_ >= 6 && p_[0] == '\\' && p_[1] == 'u') {
                    p_ += 2;
                    unsigned low;
                    if (!Hex4(low)) return false;
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                }
                AppendUtf8(out, cp);
                break;
            }
            default: return false;
            }
        }
        return false;
    }

    bool Array(JsonValue& v, int depth) {
        ++p_;
        v.type = JsonValue::Type::Array;
        SkipSpace();
        if (p_ < end_ && *p_ == ']') { ++p_; return true; }
        for (;;) {
            JsonValue item;
            if (!Value(item, depth + 1)) return false;
            v.items.push_back(std::move(item));
            SkipSpace();
            if (p_ >= end_) return false;
            if (*p_ == ',') { ++p_; continue; }
            if (*p_ == ']') { ++p_; return true; }
            return false;
        }
    }

    bool Object(JsonValue& v, int depth) {
        ++p_;
        v.type = JsonValue::Type::Object;
        SkipSpace();
        if (p_ < end_ && *p_ == '}') { ++p_; return true; }
        for (;;) {
            SkipSpace();
            if (p_ >= end_ || *p_ != '"') return false;
            std::string key;
            if (!String(key)) return false;
            SkipSpace();
            if (p_ >= end_ || *p_ != ':') return false;
            ++p_;
            JsonValue item;
            if (!Value(item, depth + 1)) return false;
            v.keys.push_back(std::move(key));
            v.items.push_back(std::move(item));
            SkipSpace();
            if (p_ >= end_) return false;
            if (*p_ == ',') { ++p_; continue; }
            if (*p_ == '}') { ++p_; return true; }
            return false;
        }
    }
};

} // namespace

bool ParseJson(const std::string& text, JsonValue& out) {
    const char* begin = text.data();
    const char* end = begin + text.size();
    // Skip a UTF-8 BOM (Notepad likes to add one).
    if (text.size() >= 3 && static_cast<unsigned char>(begin[0]) == 0xEF &&
        static_cast<unsigned char>(begin[1]) == 0xBB && static_cast<unsigned char>(begin[2]) == 0xBF)
        begin += 3;
    out = JsonValue();
    return Parser(begin, end).ParseDocument(out);
}

std::string JsonEscape(const std::string& utf8) {
    std::string out;
    for (char c : utf8) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                char buf[8];
                snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned>(c));
                out += buf;
            } else {
                out += c;
            }
        }
    }
    return out;
}
