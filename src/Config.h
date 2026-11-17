#pragma once

#include <cstddef>
#include <string>
#include <vector>

// Every tweakable CRT parameter, in the exact order the HLSL constant buffer expects
// (see the cbuffer in shaders/crt.hlsl). The JSON key is the field name.
//   X(field, panel label, min, max, slider steps)
#define RETROCRT_PARAMS(X)                                                           \
    X(curvature,    L"Curvatura de pantalla",            0.00f,   0.50f, 1000)      \
    X(cornerRadius, L"Bisel: esquinas redondeadas",      0.00f,   0.20f, 1000)      \
    X(vignette,     L"Viñeta",                           0.00f,   1.00f, 1000)      \
    X(scanStrength, L"Scanlines: intensidad",            0.00f,   1.00f, 1000)      \
    X(scanLines,    L"Scanlines: nº de líneas",        120.00f, 1080.00f, 960)      \
    X(scanBloom,    L"Scanlines: ensanche por brillo",   0.00f,   1.00f, 1000)      \
    X(maskStrength, L"Máscara de fósforo: intensidad",   0.00f,   1.00f, 1000)      \
    X(maskType,     L"Máscara: tipo",                    0.00f,   2.00f, 2)         \
    X(maskSize,     L"Máscara: tamaño tríada (px)",      2.00f,   9.00f, 700)       \
    X(chroma,       L"Aberración cromática (px)",        0.00f,   4.00f, 1000)      \
    X(bleed,        L"Sangrado NTSC / compuesto",        0.00f,   3.00f, 1000)      \
    X(glow,         L"Phosphor glow / halation",         0.00f,   1.00f, 1000)      \
    X(brightness,   L"Brillo (compensación)",            0.50f,   2.00f, 1000)      \
    X(gamma,        L"Gamma (medios tonos)",             0.60f,   1.60f, 1000)      \
    X(contrast,     L"Contraste",                        0.50f,   1.50f, 1000)      \
    X(saturation,   L"Saturación",                       0.00f,   2.00f, 1000)      \
    X(warmth,       L"Temperatura (frío / cálido)",     -1.00f,   1.00f, 1000)      \
    X(noise,        L"Granulado",                        0.00f,   0.25f, 1000)

struct Params {
#define RETROCRT_FIELD(name, label, mn, mx, steps) float name;
    RETROCRT_PARAMS(RETROCRT_FIELD)
#undef RETROCRT_FIELD
};

#define RETROCRT_COUNT(name, label, mn, mx, steps) +1
constexpr int kParamCount = 0 RETROCRT_PARAMS(RETROCRT_COUNT);
#undef RETROCRT_COUNT

struct ParamInfo {
    const char* key;
    const wchar_t* label;
    float minValue;
    float maxValue;
    int steps;
    size_t offset;
};

const ParamInfo& GetParamInfo(int index);
float GetParam(const Params& params, int index);
void SetParam(Params& params, int index, float value); // clamps to [min, max]

struct Profile {
    std::wstring name;
    Params params{};
};

int DefaultProfileCount();
Profile DefaultProfile(int index);

struct Config {
    bool enabled = true;
    int activeProfile = 0;
    std::vector<Profile> profiles;

    static Config Defaults();
    // Merges the file onto the current values. Returns false if missing or malformed.
    bool Load(const std::wstring& path, std::wstring* error);
    bool Save(const std::wstring& path) const;
};
