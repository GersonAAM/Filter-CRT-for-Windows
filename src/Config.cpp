#include "Common.h"
#include "Config.h"
#include "Json.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {

const ParamInfo kParamInfos[kParamCount] = {
#define RETROCRT_INFO(name, label, mn, mx, steps) {#name, label, mn, mx, steps, offsetof(Params, name)},
    RETROCRT_PARAMS(RETROCRT_INFO)
#undef RETROCRT_INFO
};

Profile MakeTrinitron() {
    // Series y películas: curvatura media, scanlines suaves, sangrado ligero, tono cálido.
    Profile pr;
    pr.name = L"TV Sony Trinitron (Series y Películas)";
    Params& p = pr.params;
    p.curvature = 0.08f;
    p.cornerRadius = 0.04f;
    p.vignette = 0.25f;
    p.scanStrength = 0.40f;
    p.scanLines = 480.0f;
    p.scanBloom = 0.65f;
    p.maskStrength = 0.22f;
    p.maskType = 0.0f;
    p.maskSize = 3.0f;
    p.chroma = 0.6f;
    p.bleed = 1.0f;
    p.glow = 0.28f;
    p.brightness = 1.22f;
    p.gamma = 1.05f;
    p.contrast = 1.05f;
    p.saturation = 1.08f;
    p.warmth = 0.35f;
    p.noise = 0.0f;
    return pr;
}

Profile MakePvm() {
    // Juegos: scanlines nítidas, máscara RGB definida, curvatura mínima (HUD intacto), sin ruido.
    Profile pr;
    pr.name = L"Monitor PVM / Arcade (Juegos)";
    Params& p = pr.params;
    p.curvature = 0.02f;
    p.cornerRadius = 0.01f;
    p.vignette = 0.08f;
    p.scanStrength = 0.70f;
    p.scanLines = 480.0f;
    p.scanBloom = 0.45f;
    p.maskStrength = 0.45f;
    p.maskType = 0.0f;
    p.maskSize = 3.0f;
    p.chroma = 0.0f;
    p.bleed = 0.3f;
    p.glow = 0.15f;
    p.brightness = 1.40f;
    p.gamma = 1.10f;
    p.contrast = 1.10f;
    p.saturation = 1.10f;
    p.warmth = 0.0f;
    p.noise = 0.0f;
    return pr;
}

Profile MakeVhs() {
    // TV antigua / VHS: curvatura pronunciada, esquinas redondeadas, aberración marcada, grano.
    Profile pr;
    pr.name = L"VHS / TV Antigua";
    Params& p = pr.params;
    p.curvature = 0.22f;
    p.cornerRadius = 0.10f;
    p.vignette = 0.55f;
    p.scanStrength = 0.35f;
    p.scanLines = 360.0f;
    p.scanBloom = 0.70f;
    p.maskStrength = 0.15f;
    p.maskType = 1.0f;
    p.maskSize = 4.0f;
    p.chroma = 2.5f;
    p.bleed = 2.5f;
    p.glow = 0.35f;
    p.brightness = 1.20f;
    p.gamma = 1.00f;
    p.contrast = 0.95f;
    p.saturation = 0.85f;
    p.warmth = 0.40f;
    p.noise = 0.08f;
    return pr;
}

Profile MakeSubtle() {
    // Escritorio y juegos actuales con mucho texto: sin recorte, scanlines muy ligeras, algo de contraste.
    Profile pr;
    pr.name = L"Sutil / Todo Terreno";
    Params& p = pr.params;
    p.curvature = 0.0f;
    p.cornerRadius = 0.0f;
    p.vignette = 0.05f;
    p.scanStrength = 0.18f;
    p.scanLines = 720.0f;
    p.scanBloom = 0.30f;
    p.maskStrength = 0.08f;
    p.maskType = 0.0f;
    p.maskSize = 3.0f;
    p.chroma = 0.0f;
    p.bleed = 0.0f;
    p.glow = 0.08f;
    p.brightness = 1.08f;
    p.gamma = 1.00f;
    p.contrast = 1.08f;
    p.saturation = 1.05f;
    p.warmth = 0.0f;
    p.noise = 0.0f;
    return pr;
}

bool ReadWholeFile(const std::wstring& path, std::string& out) {
    FILE* f = _wfopen(path.c_str(), L"rb");
    if (!f) return false;
    char buffer[4096];
    size_t n;
    out.clear();
    while ((n = fread(buffer, 1, sizeof(buffer), f)) > 0) out.append(buffer, n);
    fclose(f);
    return true;
}

} // namespace

const ParamInfo& GetParamInfo(int index) { return kParamInfos[index]; }

float GetParam(const Params& params, int index) {
    return *reinterpret_cast<const float*>(reinterpret_cast<const char*>(&params) + kParamInfos[index].offset);
}

void SetParam(Params& params, int index, float value) {
    const ParamInfo& info = kParamInfos[index];
    value = std::min(std::max(value, info.minValue), info.maxValue);
    *reinterpret_cast<float*>(reinterpret_cast<char*>(&params) + info.offset) = value;
}

int DefaultProfileCount() { return 4; }

Profile DefaultProfile(int index) {
    switch (index) {
    case 0: return MakeTrinitron();
    case 1: return MakePvm();
    case 2: return MakeVhs();
    default: return MakeSubtle();
    }
}

Config Config::Defaults() {
    Config cfg;
    for (int i = 0; i < DefaultProfileCount(); ++i) cfg.profiles.push_back(DefaultProfile(i));
    return cfg;
}

bool Config::Load(const std::wstring& path, std::wstring* error) {
    std::string text;
    if (!ReadWholeFile(path, text)) {
        if (error) *error = L"No existe " + path;
        return false;
    }
    JsonValue root;
    if (!ParseJson(text, root) || root.type != JsonValue::Type::Object) {
        if (error) *error = L"config.json no es JSON válido";
        return false;
    }

    if (const JsonValue* v = root.Find("enabled"); v && v->type == JsonValue::Type::Bool) enabled = v->boolean;
    if (const JsonValue* v = root.Find("activeProfile"); v && v->type == JsonValue::Type::Number)
        activeProfile = static_cast<int>(v->number);

    if (const JsonValue* list = root.Find("profiles"); list && list->type == JsonValue::Type::Array) {
        std::vector<Profile> loaded;
        for (size_t i = 0; i < list->items.size(); ++i) {
            const JsonValue& obj = list->items[i];
            if (obj.type != JsonValue::Type::Object) continue;
            // Missing keys fall back to the matching built-in profile (or the subtle one for extras).
            Profile pr = DefaultProfile(static_cast<int>(std::min<size_t>(i, DefaultProfileCount() - 1)));
            if (const JsonValue* name = obj.Find("name"); name && name->type == JsonValue::Type::String)
                pr.name = FromUtf8(name->string);
            for (int k = 0; k < kParamCount; ++k) {
                const JsonValue* v = obj.Find(kParamInfos[k].key);
                if (v && v->type == JsonValue::Type::Number && std::isfinite(v->number))
                    SetParam(pr.params, k, static_cast<float>(v->number));
            }
            loaded.push_back(pr);
        }
        if (!loaded.empty()) profiles = loaded;
    }

    if (profiles.empty()) profiles = Defaults().profiles;
    activeProfile = std::min(std::max(activeProfile, 0), static_cast<int>(profiles.size()) - 1);
    return true;
}

bool Config::Save(const std::wstring& path) const {
    std::string out = "{\n";
    out += std::string("  \"enabled\": ") + (enabled ? "true" : "false") + ",\n";
    out += "  \"activeProfile\": " + std::to_string(activeProfile) + ",\n";
    out += "  \"profiles\": [\n";
    for (size_t i = 0; i < profiles.size(); ++i) {
        out += "    {\n      \"name\": \"" + JsonEscape(ToUtf8(profiles[i].name)) + "\"";
        for (int k = 0; k < kParamCount; ++k) {
            char line[96];
            snprintf(line, sizeof(line), ",\n      \"%s\": %.4g", kParamInfos[k].key,
                     static_cast<double>(GetParam(profiles[i].params, k)));
            out += line;
        }
        out += "\n    }";
        if (i + 1 < profiles.size()) out += ",";
        out += "\n";
    }
    out += "  ]\n}\n";

    // Write to a temp file first so a crash never leaves a truncated config behind.
    std::wstring temp = path + L".tmp";
    FILE* f = _wfopen(temp.c_str(), L"wb");
    if (!f) return false;
    bool ok = fwrite(out.data(), 1, out.size(), f) == out.size();
    ok = (fclose(f) == 0) && ok;
    if (!ok) {
        DeleteFileW(temp.c_str());
        return false;
    }
    return MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
}
