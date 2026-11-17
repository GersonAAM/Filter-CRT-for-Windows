#pragma once
// Tiny JSON reader/escaper, just enough for config.json (no external dependencies).

#include <string>
#include <vector>

struct JsonValue {
    enum class Type { Null, Bool, Number, String, Array, Object };

    Type type = Type::Null;
    bool boolean = false;
    double number = 0.0;
    std::string string;                 // UTF-8
    std::vector<JsonValue> items;       // Array elements, or Object values
    std::vector<std::string> keys;      // Object keys (parallel to items)

    const JsonValue* Find(const char* key) const;
};

bool ParseJson(const std::string& text, JsonValue& out);
std::string JsonEscape(const std::string& utf8);
