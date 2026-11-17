// RetroCRT - CRT post-process for the duplicated desktop (Direct3D 11, SM 5.0).
//
// This file is embedded in RetroCRT.exe. To experiment without recompiling, copy it to
// "shaders\crt.hlsl" (or "crt.hlsl") next to the exe and use "Recargar shader" in the tray menu.
//
// Pipeline (single full-screen pass):
//   barrel warp -> composite signal (chroma aberration + NTSC bleed) -> contrast/saturation
//   -> luminance-dependent scanlines -> phosphor mask -> halation -> brightness, vignette,
//   grain -> output gamma -> black bezel.

cbuffer Constants : register(b0)
{
    float2 OutSize;     // overlay size in pixels
    float2 SrcSize;     // captured desktop size in pixels
    float  Time;        // seconds
    float  GlowMip;     // mip level used for halation (resolution dependent)

    // --- Same order as RETROCRT_PARAMS in src/Config.h ---
    float  Curvature;
    float  CornerRadius;
    float  Vignette;
    float  ScanStrength;
    float  ScanLines;
    float  ScanBloom;
    float  MaskStrength;
    float  MaskType;    // 0 aperture grille, 1 shadow mask, 2 slot mask
    float  MaskSize;
    float  Chroma;
    float  Bleed;
    float  Glow;
    float  Brightness;
    float  Gamma;
    float  Contrast;
    float  Saturation;
    float  Warmth;
    float  Noise;
};

Texture2D    Screen : register(t0);
SamplerState Linear : register(s0);

struct VSOut
{
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD0;
};

// Full-screen triangle, no vertex buffer.
VSOut VSMain(uint id : SV_VertexID)
{
    VSOut o;
    float2 uv = float2((id << 1) & 2, id & 2);
    o.uv  = uv;
    o.pos = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return o;
}

static const float3 LUMA   = float3(0.299, 0.587, 0.114);
static const float  TWO_PI = 6.2831853;

float3 ToLinear(float3 c) { return pow(max(c, 0.0), 2.2); }

// NTSC YIQ: luma and the two chroma axes that a composite signal carries separately.
float3 RgbToYiq(float3 c)
{
    return float3(dot(c, float3(0.299,  0.587,  0.114)),
                  dot(c, float3(0.596, -0.274, -0.322)),
                  dot(c, float3(0.211, -0.523,  0.312)));
}

float3 YiqToRgb(float3 y)
{
    return float3(dot(y, float3(1.0,  0.956,  0.621)),
                  dot(y, float3(1.0, -0.272, -0.647)),
                  dot(y, float3(1.0, -1.106,  1.703)));
}

// Barrel distortion of a curved CRT glass.
float2 Warp(float2 uv)
{
    float2 c = uv * 2.0 - 1.0;
    c *= 1.0 + Curvature * (c.yx * c.yx);
    return c * 0.5 + 0.5;
}

// 1 inside the visible tube, 0 on the black bezel, anti-aliased rounded corners.
float BezelMask(float2 uv)
{
    if (Curvature <= 0.0 && CornerRadius <= 0.0)
        return 1.0; // nothing is cropped: subtitles and HUDs stay intact

    float  aspect = OutSize.x / OutSize.y;
    float2 halfSize = float2(aspect, 1.0);
    float2 c = (uv * 2.0 - 1.0) * halfSize;
    float  r = CornerRadius * 2.0;
    float2 q = abs(c) - halfSize + r;
    float  d = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
    float  aa = 2.0 / OutSize.y; // ~1 output pixel
    return 1.0 - smoothstep(-aa, aa, d);
}

// Composite video: RGB convergence error + horizontally smeared chroma (gamma space).
float3 SampleSignal(float2 uv)
{
    float2 texel = 1.0 / SrcSize;

    // Radial chromatic aberration (stronger at the edges) plus a small constant misconvergence.
    float2 ca = ((uv - 0.5) * 2.0 * Chroma + float2(Chroma * 0.25, 0.0)) * texel;
    float3 c;
    c.r = Screen.SampleLevel(Linear, uv + ca, 0).r;
    c.g = Screen.SampleLevel(Linear, uv,      0).g;
    c.b = Screen.SampleLevel(Linear, uv - ca, 0).b;

    [branch]
    if (Bleed > 0.0)
    {
        // Chroma bandwidth is much lower than luma on NTSC: blur I/Q widely, Y only a little.
        // Weights lean left so colour trails to the right like a real composite signal.
        static const float OFFSETS[5] = { -2.0, -1.0, 0.0, 1.0, 2.0 };
        static const float WEIGHTS[5] = { 0.14, 0.24, 0.30, 0.20, 0.12 };
        float2 stepUv = float2(Bleed * 1.5 * texel.x, 0.0);
        float3 center = RgbToYiq(c);
        float3 sum = 0.0;
        [unroll]
        for (int i = 0; i < 5; ++i)
        {
            float3 s = (i == 2) ? c : Screen.SampleLevel(Linear, uv + stepUv * OFFSETS[i], 0).rgb;
            sum += RgbToYiq(s) * WEIGHTS[i];
        }
        float y = lerp(center.x, sum.x, 0.5);
        c = saturate(YiqToRgb(float3(y, sum.yz)));
    }
    return c;
}

// Halation: light scattered inside the glass, read from a low mip of the capture (linear).
float3 SampleGlow(float2 uv)
{
    float2 t = exp2(GlowMip) / SrcSize;
    float3 g = Screen.SampleLevel(Linear, uv, GlowMip).rgb * 0.36;
    g += Screen.SampleLevel(Linear, uv + float2( 1.5,  0.5) * t, GlowMip).rgb * 0.16;
    g += Screen.SampleLevel(Linear, uv + float2(-1.5, -0.5) * t, GlowMip).rgb * 0.16;
    g += Screen.SampleLevel(Linear, uv + float2(-0.5,  1.5) * t, GlowMip).rgb * 0.16;
    g += Screen.SampleLevel(Linear, uv + float2( 0.5, -1.5) * t, GlowMip).rgb * 0.16;
    return ToLinear(g);
}

// Electron beam profile. Bright pixels widen the beam (natural horizontal bloom of a tube),
// dark ones leave thin, well separated lines.
float ScanlineWeight(float y, float luma, float lineFw)
{
    float lines = max(ScanLines, 1.0);
    float f = frac(y * lines) - 0.5;
    float width = 0.20 + 0.32 * ScanBloom * sqrt(saturate(luma));
    float beam = exp(-0.5 * (f * f) / (width * width));
    // When a line is smaller than ~2 pixels, fade to its average to avoid moire.
    float average = saturate(width * 2.5066);
    beam = lerp(beam, average, saturate(lineFw * 2.0 - 0.5));
    return lerp(1.0, beam, ScanStrength);
}

// Phosphor triads in screen pixels. Cosine stripes average to 0.5 per channel, so any size works.
float3 PhosphorMask(float2 pix)
{
    float size = max(MaskSize, 1.0);
    float x = pix.x / size;
    float rowFactor = 1.0;

    if (MaskType > 0.5)
    {
        float row = floor(pix.y / size);
        x += fmod(row, 2.0) * 0.5; // staggered triads
        float yv = frac(pix.y / size);
        if (MaskType > 1.5)
            rowFactor = lerp(0.6, 1.0, smoothstep(0.0, 0.18, yv) * smoothstep(1.0, 0.82, yv)); // slot gaps
        else
            rowFactor = lerp(0.6, 1.0, 0.5 + 0.5 * cos(TWO_PI * (yv - 0.5)));                 // round dots
    }

    float3 m = 0.5 + 0.5 * cos(TWO_PI * (x - float3(0.0, 1.0 / 3.0, 2.0 / 3.0)));
    return m * rowFactor;
}

float Hash(float2 p)
{
    uint2 q = uint2(p);
    uint n = q.x * 1973u + q.y * 9277u + uint(Time * 60.0) * 26699u;
    n = (n << 13u) ^ n;
    n = n * (n * n * 15731u + 789221u) + 1376312589u;
    return float(n & 0x7fffffffu) / 2147483647.0;
}

float4 PSMain(VSOut i) : SV_Target
{
    float2 uv = (Curvature > 0.0) ? Warp(i.uv) : i.uv;
    float lineFw = fwidth(uv.y * ScanLines); // derivatives before any branching
    float bezel = BezelMask(uv);

    // Signal + picture controls (contrast in gamma space, the rest in linear light).
    float3 signal = SampleSignal(uv);
    signal = saturate((signal - 0.5) * Contrast + 0.5);
    float3 col = ToLinear(signal);
    float luma = dot(col, LUMA);
    col = max(lerp(luma.xxx, col, Saturation), 0.0);
    col *= float3(1.0 + 0.10 * Warmth, 1.0 + 0.02 * Warmth, 1.0 - 0.12 * Warmth);

    // Tube structure.
    col *= ScanlineWeight(uv.y, luma, lineFw);
    col *= lerp(1.0, 0.3 + 1.4 * PhosphorMask(i.pos.xy), MaskStrength);

    // Warm halo around highlights; it also fills the dark gaps between scanlines.
    [branch]
    if (Glow > 0.0)
    {
        float3 g = SampleGlow(uv);
        col += g * Glow * (0.35 + 0.65 * dot(g, LUMA)) * float3(1.0, 0.93, 0.84);
    }

    // Compensates the light lost to scanlines and the mask.
    col *= Brightness;

    if (Vignette > 0.0)
    {
        float2 v = saturate(uv * (1.0 - uv));
        col *= lerp(1.0, saturate(pow(16.0 * v.x * v.y, 0.35)), Vignette);
    }

    // Output gamma: 1.0 = neutral, >1 lifts the mid-tones.
    float3 outColor = pow(saturate(col), 1.0 / (2.2 * Gamma));

    if (Noise > 0.0)
        outColor += (Hash(i.pos.xy) - 0.5) * Noise;

    return float4(saturate(outColor) * bezel, 1.0);
}
