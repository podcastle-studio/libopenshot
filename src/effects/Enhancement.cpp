/**
 * @file
 * @brief Source file for Enhancement class ? CPU replica of the GLSL
 *        clarity / sharpness / grain shader
 *
 * @ref License
 */
// Copyright (c) 2025 OpenShot Studios, LLC
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "Enhancement.h"

#include "skia/include/core/SkM44.h"
#include "skia/include/effects/SkRuntimeEffect.h"
#include "Exceptions.h"

#include <QImage>
#include <algorithm>
#include <cmath>

using namespace openshot;

/* --------------------------------------------------------------- */
/*                     helpers / intrinsics                        */
/* --------------------------------------------------------------- */

namespace {

// fractional part
inline double fract(double v) { return v - std::floor(v); }

// clamp to 0-255 integer
inline int clamp255(double v)
{
    return static_cast<int>(std::max(0.0, std::min(255.0, std::round(v))));
}

// shader-style hash
inline double hash(double x, double y)
{
    double v = std::sin(x * 12.9898 + y * 78.233) * 43758.5453;
    return fract(v);
}

/* ---------- tiny image kernels (working in sRGB space) ---------- */

struct Pixel { double r, g, b; };              // sRGB 0-1

inline Pixel fetchSRGB(const QImage &img, int x, int y)
{
    const uchar *p = img.constScanLine(y) + x * 4; // RGBA
    return {
        p[0] / 255.0, // R
        p[1] / 255.0, // G
        p[2] / 255.0  // B
    };
}

inline int fetchAlpha(const QImage &img, int x, int y)
{
    const uchar *p = img.constScanLine(y) + x * 4; // RGBA
    return p[3]; // A
}

inline Pixel blur3x3(const QImage &orig, int x, int y)
{
    double sR = 0, sG = 0, sB = 0;
    for (int ky = -1; ky <= 1; ++ky) {
        const uchar *row = orig.constScanLine(y + ky);
        for (int kx = -1; kx <= 1; ++kx) {
            const uchar *p = row + (x + kx) * 4; // RGBA
            sR += p[0] / 255.0;
            sG += p[1] / 255.0;
            sB += p[2] / 255.0;
        }
    }
    return { sR / 9.0, sG / 9.0, sB / 9.0 };
}

inline Pixel highPass4(const QImage &orig, int x, int y)
{
    Pixel c = fetchSRGB(orig, x,     y);
    Pixel l = fetchSRGB(orig, x - 1, y);
    Pixel r = fetchSRGB(orig, x + 1, y);
    Pixel t = fetchSRGB(orig, x, y - 1);
    Pixel b = fetchSRGB(orig, x, y + 1);

    auto lap = [](double cc, double ll, double rr, double tt, double bb) {
        return (cc * 4.0 - (ll + rr + tt + bb)) * 0.5;   // shader's soft scale
    };
    return {
        lap(c.r, l.r, r.r, t.r, b.r),
        lap(c.g, l.g, r.g, t.g, b.g),
        lap(c.b, l.b, r.b, t.b, b.b)
    };
}

// mix function (GLSL-style linear interpolation)
inline double mix(double a, double b, double t) {
    return a * (1.0 - t) + b * t;
}

inline Pixel mix(const Pixel &a, const Pixel &b, double t) {
    return {
        mix(a.r, b.r, t),
        mix(a.g, b.g, t),
        mix(a.b, b.b, t)
    };
}

/* --------------------------------------------------------------- */
/*                             passes                              */
/* --------------------------------------------------------------- */

// ----- Local-contrast ("clarity") ? 0 ? 1 -----
void applyClarityPass(QImage &img, double strength)
{
    if (strength <= 0.0) return;
    const double k = strength * 3.0;          // shader multiplier (0-3)

    QImage orig = img.copy();
    const int w = img.width(), h = img.height();

    img.bits(); // <-- ensure detachment before multi-threaded writes

    #pragma omp parallel for schedule(static)
    for (int y = 1; y < h - 1; ++y) {
        uchar *out = img.scanLine(y);

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC ivdep
#endif
        for (int x = 1; x < w - 1; ++x) {
            Pixel base = fetchSRGB(orig, x, y);
            Pixel blr  = blur3x3(orig, x, y);

            Pixel color = {
                base.r + (base.r - blr.r) * k,
                base.g + (base.g - blr.g) * k,
                base.b + (base.b - blr.b) * k
            };

            color.r = std::max(0.0, std::min(1.0, color.r));
            color.g = std::max(0.0, std::min(1.0, color.g));
            color.b = std::max(0.0, std::min(1.0, color.b));

            const int a = fetchAlpha(orig, x, y);

            out[x * 4 + 0] = clamp255(color.r * 255.0); // R
            out[x * 4 + 1] = clamp255(color.g * 255.0); // G
            out[x * 4 + 2] = clamp255(color.b * 255.0); // B
            out[x * 4 + 3] = a;                         // A
        }
    }
}

// ----- Sharpen (pos) / blur (neg) ? ?1 ? 1 -----
void applySharpnessPass(QImage &img, double value)
{
    if (std::abs(value) < 1e-6) return;

    QImage orig = img.copy();
    const int w = img.width(), h = img.height();

    img.bits(); // <-- ensure detachment

    if (value > 0.0) {                       // sharpen
        const double k = value * 3.0;        // 0-3

        #pragma omp parallel for schedule(static)
        for (int y = 1; y < h - 1; ++y) {
            uchar *out = img.scanLine(y);

            #if defined(__GNUC__) || defined(__clang__)
            #pragma GCC ivdep
            #endif
            for (int x = 1; x < w - 1; ++x) {

                Pixel base = fetchSRGB(orig, x, y);
                Pixel edge = highPass4(orig, x, y);

                // Direct shader implementation: color += edge * uSharpness * 3.0
                Pixel color = {
                    base.r + edge.r * k,
                    base.g + edge.g * k,
                    base.b + edge.b * k
                };

                // Clamp to [0,1] as shader does
                color.r = std::max(0.0, std::min(1.0, color.r));
                color.g = std::max(0.0, std::min(1.0, color.g));
                color.b = std::max(0.0, std::min(1.0, color.b));

                const int a = fetchAlpha(orig, x, y);

                out[x * 4 + 0] = clamp255(color.r * 255.0); // R
                out[x * 4 + 1] = clamp255(color.g * 255.0); // G
                out[x * 4 + 2] = clamp255(color.b * 255.0); // B
                out[x * 4 + 3] = a;                         // A
            }
        }
    } else {                                 // blur (mix)
        const double t = -value;

        #pragma omp parallel for schedule(static)
        for (int y = 1; y < h - 1; ++y) {
            uchar *out = img.scanLine(y);

            #if defined(__GNUC__) || defined(__clang__)
            #pragma GCC ivdep
            #endif
            for (int x = 1; x < w - 1; ++x) {

                Pixel base = fetchSRGB(orig, x, y);
                Pixel blr  = blur3x3(orig, x, y);

                Pixel color = mix(base, blr, t);

                const int a = fetchAlpha(orig, x, y);

                out[x * 4 + 0] = clamp255(color.r * 255.0); // R
                out[x * 4 + 1] = clamp255(color.g * 255.0); // G
                out[x * 4 + 2] = clamp255(color.b * 255.0); // B
                out[x * 4 + 3] = a;                         // A
            }
        }
    }
}

// ----- Film-grain ? matches shader exactly -----
void applyNoisePass(QImage &img, double amount)
{
    if (amount <= 0.0) return;

    const int w = img.width(), h = img.height();

    img.bits(); // <-- ensure detachment

#pragma omp parallel for schedule(static)
    for (int y = 0; y < h; ++y) {
        uchar *out = img.scanLine(y);

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC ivdep
#endif
        for (int x = 0; x < w; ++x) {
            const int idx = x * 4;

            const int r8 = out[idx + 0];                        // R
            const int g8 = out[idx + 1];                        // G
            const int b8 = out[idx + 2];                        // B
            const int a8 = out[idx + 3];                        // A

            double seed = fract((r8 * 0.123 +
                                 g8 * 0.456 +
                                 b8 * 0.789) / 255.0);

            // hash with gl_FragCoord.xy equivalent
            double n = hash(x + seed * 437.0, y + seed * 437.0) - 0.5;

            // Luma calculation
            double luma = (0.299 * r8 +
                           0.587 * g8 +
                           0.114 * b8) / 255.0;

            // Amplitude: mix(1.4, 0.5, luma)
            double amp = 1.4 * (1.0 - luma) + 0.5 * luma;

            // Final noise delta
            double delta = n * amount * 0.4 * amp * 255.0;

            out[idx + 0] = clamp255(r8 + delta); // R
            out[idx + 1] = clamp255(g8 + delta); // G
            out[idx + 2] = clamp255(b8 + delta); // B
            out[idx + 3] = a8;                   // A
        }
    }
}

} // anonymous namespace



/* --------------------------------------------------------------- */
/*                       class implementation                      */
/* --------------------------------------------------------------- */

Enhancement::Enhancement()
    : noise(0.0), clarity(0.0), sharpness(0.0)
{
    init_effect_details();
}

Enhancement::Enhancement(Keyframe n, Keyframe c, Keyframe s)
    : noise(std::move(n)), clarity(std::move(c)), sharpness(std::move(s))
{
    init_effect_details();
}

/* ---------- EffectBase required ---------- */
std::shared_ptr<openshot::Frame>
Enhancement::GetFrame(std::shared_ptr<openshot::Frame> frame, int64_t frame_number)
{
    // Assume frames are already RGBA; do not convert format.

    const double noise_v     = std::clamp(noise    .GetValue(frame_number), 0.0, 1.0);
    const double clarity_v   = std::clamp(clarity  .GetValue(frame_number), 0.0, 1.0);
    const double sharpness_v = std::clamp(sharpness.GetValue(frame_number),-1.0, 1.0);

    if (noise_v == 0.0 && clarity_v == 0.0 && std::abs(sharpness_v) < 1e-6)
        return frame;                                // nothing to do

    // The shader when there is a GPU to run it on AND no grain is asked for. The grain pass is
    // deliberately not ported -- see GpuShaderSource() -- so a frame that wants it runs entirely
    // on the CPU rather than half on each, which would be two crossings for no gain.
    //
    // This is the first effect that is more than one pass: clarity and sharpness each read the
    // NEIGHBOURS of what the previous pass wrote, so they cannot be folded into one fragment.
    // Each ApplyOnGpu leaves its result as the frame's GPU backing, so the second pass reads the
    // first's output as a texture and only the last one is ever read back.
    const bool done_on_gpu = noise_v == 0.0 && [&] {
        bool ran = false;
        if (clarity_v > 0.0) {
            gpu_pass = GpuPass::Clarity;
            gpu_pass_strength = clarity_v;
            if (!ApplyOnGpu(frame, frame_number))
                return false;
            ran = true;
        }
        if (sharpness_v != 0.0) {
            gpu_pass = sharpness_v > 0.0 ? GpuPass::Sharpen : GpuPass::BlurMix;
            gpu_pass_strength = sharpness_v;
            if (!ApplyOnGpu(frame, frame_number))
                return false;
            ran = true;
        }
        return ran;
    }();
    if (done_on_gpu)
        return frame;

    // Falling back after a pass has already run on the GPU is still correct: that pass left its
    // result as the frame's pixels, so GetImage() reads it back and the remaining passes carry on
    // from there. It costs a crossing, which is why declining happens up front where it can.
    std::shared_ptr<QImage> img = frame->GetImage();

    /* order: clarity ? sharpen/blur ? grain */
    if (clarity_v   > 0.0) applyClarityPass (*img, clarity_v);
    if (sharpness_v != 0.0) applySharpnessPass(*img, sharpness_v);
    if (noise_v     > 0.0) applyNoisePass    (*img, noise_v);

    return frame;
}

/* ---------- GPU ---------- */

// The SkSL twin of the clarity and sharpness passes.
//
// **The grain pass is deliberately absent.** applyNoisePass is built on the classic GLSL hash
// fract(sin(x * 12.9898 + y * 78.233) * 43758.5453). At 1080p the argument to sin() reaches ~85,000,
// where the result depends entirely on how many bits the implementation carries: the C++ evaluates
// it in double, an SkSL fragment in float. The two do not differ by an LSB, they differ by an
// arbitrary amount in [0, 1), which the pass then scales to as much as ~140 LSB of grain. There is
// no way to make them agree short of changing the CPU's hash, so a frame that asks for grain runs
// entirely on the CPU -- SetGpuUniforms never sees it, because GetFrame checks first.
//
// Two details of the C++ that are easy to miss and are reproduced here: both passes SKIP the
// one-pixel border (their loops run 1..h-2 and 1..w-2), leaving it exactly as it was; and both
// work in 0..1 from the premultiplied bytes without unpremultiplying, then round rather than
// truncate on the way back out (clamp255 uses std::round).
const char* Enhancement::GpuShaderSource() const
{
	return R"SKSL(
uniform float2 size;   // frame size in pixels
uniform float  mode;   // 0 clarity, 1 sharpen, 2 blur-mix
uniform float  k;      // strength * 3 for clarity and sharpen; the mix amount for blur

// The C++ accumulates each neighbour already divided by 255, so the division happens nine times
// and not once at the end. That is not the same sum in floating point, and this is a parity
// fragment, so it accumulates the same way.
float3 osBlur3x3(float2 p) {
	float3 sum = float3(0.0);
	for (int dy = -1; dy <= 1; ++dy)
		for (int dx = -1; dx <= 1; ++dx)
			sum += osBytes(p + float2(float(dx), float(dy))).rgb / 255.0;
	return sum / 9.0;
}

float4 main(float2 p) {
	float4 bytes = osBytes(p);
	float2 q = floor(p);
	// The border the C++ never writes.
	if (q.x < 1.0 || q.y < 1.0 || q.x >= size.x - 1.0 || q.y >= size.y - 1.0)
		return bytes / 255.0;

	float3 base = bytes.rgb / 255.0;
	float3 c;

	if (mode < 0.5) {
		// Clarity: unsharp mask against a 3x3 box blur.
		c = base + (base - osBlur3x3(p)) * k;
	} else if (mode < 1.5) {
		// Sharpen: a 4-neighbour Laplacian at half scale, as highPass4 computes it.
		float3 l = osBytes(p + float2(-1.0,  0.0)).rgb / 255.0;
		float3 r = osBytes(p + float2( 1.0,  0.0)).rgb / 255.0;
		float3 t = osBytes(p + float2( 0.0, -1.0)).rgb / 255.0;
		float3 b = osBytes(p + float2( 0.0,  1.0)).rgb / 255.0;
		float3 edge = (base * 4.0 - (l + r + t + b)) * 0.5;
		c = base + edge * k;
	} else {
		// Negative sharpness: mix toward the blur. No clamp here, matching the C++, which
		// clamps only in the two branches above -- the mix cannot leave 0..1 anyway.
		c = base * (1.0 - k) + osBlur3x3(p) * k;
		return float4(clamp(floor(c * 255.0 + 0.5), 0.0, 255.0), bytes.a) / 255.0;
	}

	c = clamp(c, 0.0, 1.0);
	// clamp255 rounds; it does not truncate like the other effects' constrain().
	return float4(clamp(floor(c * 255.0 + 0.5), 0.0, 255.0), bytes.a) / 255.0;
}
)SKSL";
}

bool Enhancement::SetGpuUniforms(SkRuntimeEffectBuilder& builder, int64_t frame_number,
								 int width, int height) const
{
	builder.uniform("size") = SkV2{static_cast<float>(width), static_cast<float>(height)};
	switch (gpu_pass) {
	case GpuPass::Clarity:
		builder.uniform("mode") = 0.0f;
		builder.uniform("k") = static_cast<float>(gpu_pass_strength * 3.0);
		break;
	case GpuPass::Sharpen:
		builder.uniform("mode") = 1.0f;
		builder.uniform("k") = static_cast<float>(gpu_pass_strength * 3.0);
		break;
	case GpuPass::BlurMix:
		builder.uniform("mode") = 2.0f;
		builder.uniform("k") = static_cast<float>(-gpu_pass_strength);
		break;
	}
	return true;
}

/* ---------- serialisation ---------- */
std::string Enhancement::Json() const
{
    return JsonValue().toStyledString();
}

Json::Value Enhancement::JsonValue() const
{
    Json::Value root = EffectBase::JsonValue();
    root["type"]       = info.class_name;
    root["noise"]      = noise.JsonValue();
    root["clarity"]    = clarity.JsonValue();
    root["sharpness"]  = sharpness.JsonValue();
    return root;
}

void Enhancement::SetJson(const std::string value)
{
    try {
        SetJsonValue(stringToJson(value));
    }
    catch (const std::exception&) {
        throw InvalidJSON("JSON is invalid (missing keys or invalid data types)");
    }
}

void Enhancement::SetJsonValue(const Json::Value root)
{
    EffectBase::SetJsonValue(root);
    if (!root["noise"].isNull())     noise    .SetJsonValue(root["noise"]);
    if (!root["clarity"].isNull())   clarity  .SetJsonValue(root["clarity"]);
    if (!root["sharpness"].isNull()) sharpness.SetJsonValue(root["sharpness"]);
}

std::string Enhancement::PropertiesJSON(int64_t f) const
{
    Json::Value root = BasePropertiesJSON(f);

    root["noise"]     = add_property_json("Noise",     noise    .GetValue(f), "float", "", &noise,     0.0,  1.0, false, f);
    root["clarity"]   = add_property_json("Clarity",   clarity  .GetValue(f), "float", "", &clarity,   0.0,  1.0, false, f);
    root["sharpness"] = add_property_json("Sharpness", sharpness.GetValue(f), "float", "", &sharpness,-1.0,  1.0, false, f);

    return root.toStyledString();
}

/* ---------- meta ---------- */
void Enhancement::init_effect_details()
{
    InitEffectInfo();
    info.class_name  = "Enhancement";
    info.name        = "Enhancement";
    info.description = "Image enhancement (clarity, sharpen/blur, film grain).";
    info.has_audio   = false;
    info.has_video   = true;
}
