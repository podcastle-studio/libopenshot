/**
 * @file
 * @brief Source file for Enhancement class – CPU replica of the GLSL
 *        clarity / sharpness / grain shader
 *
 * @ref License
 */
// Copyright (c) 2025 OpenShot Studios, LLC
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "Enhancement.h"
#include "Exceptions.h"

#include <QImage>
#include <QRgb>
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

inline Pixel fetchSRGB(const QRgb &p)
{
    return {
        qRed(p) / 255.0,
        qGreen(p) / 255.0,
        qBlue(p) / 255.0
    };
}

inline Pixel blur3x3(const QImage &orig, int x, int y)
{
    double sR = 0, sG = 0, sB = 0;
    for (int ky = -1; ky <= 1; ++ky)
        for (int kx = -1; kx <= 1; ++kx) {
            Pixel p = fetchSRGB(orig.pixel(x + kx, y + ky));
            sR += p.r;  sG += p.g;  sB += p.b;
        }
    return { sR / 9.0, sG / 9.0, sB / 9.0 };
}

inline Pixel highPass4(const QImage &orig, int x, int y)
{
    Pixel c = fetchSRGB(orig.pixel(x,     y));
    Pixel l = fetchSRGB(orig.pixel(x - 1, y));
    Pixel r = fetchSRGB(orig.pixel(x + 1, y));
    Pixel t = fetchSRGB(orig.pixel(x, y - 1));
    Pixel b = fetchSRGB(orig.pixel(x, y + 1));

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

// ----- Local-contrast ("clarity") – 0 … 1 -----
void applyClarityPass(QImage &img, double strength)
{
    if (strength <= 0.0) return;
    const double k = strength * 3.0;          // shader multiplier (0-3)

    QImage orig = img.copy();
    const int w = img.width(), h = img.height();

    for (int y = 1; y < h - 1; ++y) {
        QRgb *out = reinterpret_cast<QRgb *>(img.scanLine(y));
        for (int x = 1; x < w - 1; ++x) {
            Pixel base = fetchSRGB(orig.pixel(x, y));
            Pixel blur = blur3x3(orig, x, y);

            // Direct shader implementation: color += (baseColor - blurColor) * (uClarity * 3.0)
            Pixel color = {
                base.r + (base.r - blur.r) * k,
                base.g + (base.g - blur.g) * k,
                base.b + (base.b - blur.b) * k
            };

            // Clamp to [0,1] as shader does
            color.r = std::max(0.0, std::min(1.0, color.r));
            color.g = std::max(0.0, std::min(1.0, color.g));
            color.b = std::max(0.0, std::min(1.0, color.b));

            out[x] = qRgba(clamp255(color.r * 255.0),
                           clamp255(color.g * 255.0),
                           clamp255(color.b * 255.0),
                           qAlpha(orig.pixel(x, y)));
        }
    }
}

// ----- Sharpen (pos) / blur (neg) – −1 … 1 -----
void applySharpnessPass(QImage &img, double value)
{
    if (std::abs(value) < 1e-6) return;

    QImage orig = img.copy();
    const int w = img.width(), h = img.height();

    if (value > 0.0) {                       // sharpen
        const double k = value * 3.0;        // 0-3
        for (int y = 1; y < h - 1; ++y) {
            QRgb *out = reinterpret_cast<QRgb *>(img.scanLine(y));
            for (int x = 1; x < w - 1; ++x) {
                Pixel base = fetchSRGB(orig.pixel(x, y));
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

                out[x] = qRgba(clamp255(color.r * 255.0),
                               clamp255(color.g * 255.0),
                               clamp255(color.b * 255.0),
                               qAlpha(orig.pixel(x, y)));
            }
        }
    }
    else {                                   // blur (mix)
        const double t = -value;             // 0 … 1
        for (int y = 1; y < h - 1; ++y) {
            QRgb *out = reinterpret_cast<QRgb *>(img.scanLine(y));
            for (int x = 1; x < w - 1; ++x) {
                Pixel base = fetchSRGB(orig.pixel(x, y));
                Pixel blur = blur3x3(orig, x, y);

                // Shader: color = mix(color, blurColor, -uSharpness)
                Pixel color = mix(base, blur, t);

                out[x] = qRgba(clamp255(color.r * 255.0),
                               clamp255(color.g * 255.0),
                               clamp255(color.b * 255.0),
                               qAlpha(orig.pixel(x, y)));
            }
        }
    }
}

// ----- Film-grain – matches shader exactly -----
void applyNoisePass(QImage &img, double amount)
{
    if (amount <= 0.0) return;

    const int w = img.width(), h = img.height();

    for (int y = 0; y < h; ++y) {
        QRgb *out = reinterpret_cast<QRgb *>(img.scanLine(y));
        for (int x = 0; x < w; ++x) {
            QRgb base = out[x];

            // frameSeed function from shader
            double seed = fract((qRed(base) * 0.123 +
                                 qGreen(base) * 0.456 +
                                 qBlue(base)  * 0.789) / 255.0);

            // hash with gl_FragCoord.xy equivalent
            double n = hash(x + seed * 437.0, y + seed * 437.0) - 0.5;

            // Luma calculation
            double luma = (0.299 * qRed(base) +
                           0.587 * qGreen(base) +
                           0.114 * qBlue(base)) / 255.0;

            // Amplitude: mix(1.4, 0.5, luma)
            double amp = 1.4 * (1.0 - luma) + 0.5 * luma;

            // Final noise delta
            double delta = n * amount * 0.4 * amp * 255.0;

            out[x] = qRgba(clamp255(qRed(base)   + delta),
                           clamp255(qGreen(base) + delta),
                           clamp255(qBlue(base)  + delta),
                           qAlpha(base));
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
    std::shared_ptr<QImage> img = frame->GetImage();

    if (img->format() != QImage::Format_ARGB32)
        *img = img->convertToFormat(QImage::Format_ARGB32);

    const double noise_v     = std::clamp(noise    .GetValue(frame_number), 0.0, 1.0);
    const double clarity_v   = std::clamp(clarity  .GetValue(frame_number), 0.0, 1.0);
    const double sharpness_v = std::clamp(sharpness.GetValue(frame_number),-1.0, 1.0);

    if (noise_v == 0.0 && clarity_v == 0.0 && std::abs(sharpness_v) < 1e-6)
        return frame;                                // nothing to do

    /* order: clarity → sharpen/blur → grain */
    if (clarity_v   > 0.0) applyClarityPass (*img, clarity_v);
    if (sharpness_v != 0.0) applySharpnessPass(*img, sharpness_v);
    if (noise_v     > 0.0) applyNoisePass    (*img, noise_v);

    return frame;
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