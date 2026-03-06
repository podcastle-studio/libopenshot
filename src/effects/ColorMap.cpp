/**
 * @file
 * @brief Source file for ColorMap (LUT + Color Match) effect
 * @author Jonathan Thomas <jonathan@openshot.org>
 *
 * @ref License
 */

// Copyright (c) 2008-2025 OpenShot Studios, LLC
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "ColorMap.h"
#include "Exceptions.h"

#include <omp.h>
#include <cmath>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <QImage>

using namespace openshot;


// ═══════════════════════════════════════════════════════════════════════════════
// OpenMP trilinear LUT apply (platform-specific optimization)
// ═══════════════════════════════════════════════════════════════════════════════

void ColorMap::applyTrilinearLut(const float* lut, int size,
                                  unsigned char* pixels, int pixel_count,
                                  float tR, float tG, float tB) {

    // Precompute byte→LUT coordinate table (256 entries, ~1μs)
    struct LutCoord {
        int i0, i1;
        float frac, ifrac;
    };
    LutCoord coord[256];
    {
        const float sizeM1 = (float)(size - 1);
        const float inv255 = 1.0f / 255.0f;
        for (int i = 0; i < 256; i++) {
            float f = (float)i * inv255 * sizeM1;
            int i0 = (int)f;
            coord[i].i0    = i0;
            coord[i].i1    = std::min(i0 + 1, size - 1);
            coord[i].frac  = f - (float)i0;
            coord[i].ifrac = 1.0f - (f - (float)i0);
        }
    }

    const int strideR = 3;
    const int strideG = size * 3;
    const int strideB = size * size * 3;
    const bool full_intensity = (tR >= 0.999f && tG >= 0.999f && tB >= 0.999f);

    #pragma omp parallel for schedule(static)
    for (int i = 0; i < pixel_count; ++i) {
        const int idx = i * 4;
        const int A = pixels[idx + 3];
        if (A == 0) continue;

        const LutCoord& rc = coord[pixels[idx + 0]];
        const LutCoord& gc = coord[pixels[idx + 1]];
        const LutCoord& bc = coord[pixels[idx + 2]];

        if (A != 255) {
            // Slow path: semi-transparent pixel — demultiply first
            const float inv255 = 1.0f / 255.0f;
            const float alpha = A * inv255;
            const float invAlpha = 1.0f / alpha;

            int trueR = std::min((int)(pixels[idx + 0] * invAlpha + 0.5f), 255);
            int trueG = std::min((int)(pixels[idx + 1] * invAlpha + 0.5f), 255);
            int trueB = std::min((int)(pixels[idx + 2] * invAlpha + 0.5f), 255);

            const LutCoord& rc2 = coord[trueR];
            const LutCoord& gc2 = coord[trueG];
            const LutCoord& bc2 = coord[trueB];

            const int base = bc2.i0 * strideB + gc2.i0 * strideG + rc2.i0 * strideR;
            const float* p000 = lut + base;
            const float* p100 = p000 + (rc2.i1 - rc2.i0) * strideR;
            const float* p010 = p000 + (gc2.i1 - gc2.i0) * strideG;
            const float* p110 = p010 + (rc2.i1 - rc2.i0) * strideR;
            const float* p001 = p000 + (bc2.i1 - bc2.i0) * strideB;
            const float* p101 = p001 + (rc2.i1 - rc2.i0) * strideR;
            const float* p011 = p001 + (gc2.i1 - gc2.i0) * strideG;
            const float* p111 = p011 + (rc2.i1 - rc2.i0) * strideR;

            const float dr = rc2.frac, idr = rc2.ifrac;
            const float dg = gc2.frac, idg = gc2.ifrac;
            const float db = bc2.frac, idb = bc2.ifrac;

            float c0, c1;
            c0 = (p000[0]*idr+p100[0]*dr)*idg+(p010[0]*idr+p110[0]*dr)*dg;
            c1 = (p001[0]*idr+p101[0]*dr)*idg+(p011[0]*idr+p111[0]*dr)*dg;
            float lr = c0*idb + c1*db;
            c0 = (p000[1]*idr+p100[1]*dr)*idg+(p010[1]*idr+p110[1]*dr)*dg;
            c1 = (p001[1]*idr+p101[1]*dr)*idg+(p011[1]*idr+p111[1]*dr)*dg;
            float lg = c0*idb + c1*db;
            c0 = (p000[2]*idr+p100[2]*dr)*idg+(p010[2]*idr+p110[2]*dr)*dg;
            c1 = (p001[2]*idr+p101[2]*dr)*idg+(p011[2]*idr+p111[2]*dr)*dg;
            float lb = c0*idb + c1*db;

            float Rn = trueR * (1.0f / 255.0f);
            float Gn = trueG * (1.0f / 255.0f);
            float Bn = trueB * (1.0f / 255.0f);
            float outR = (lr * tR + Rn * (1.0f - tR)) * alpha;
            float outG = (lg * tG + Gn * (1.0f - tG)) * alpha;
            float outB = (lb * tB + Bn * (1.0f - tB)) * alpha;

            pixels[idx + 0] = (unsigned char)std::clamp((int)(outR * 255.0f + 0.5f), 0, 255);
            pixels[idx + 1] = (unsigned char)std::clamp((int)(outG * 255.0f + 0.5f), 0, 255);
            pixels[idx + 2] = (unsigned char)std::clamp((int)(outB * 255.0f + 0.5f), 0, 255);
            continue;
        }

        // Fast path: opaque pixel (A == 255)
        const int base = bc.i0 * strideB + gc.i0 * strideG + rc.i0 * strideR;
        const float* p000 = lut + base;
        const float* p100 = p000 + (rc.i1 - rc.i0) * strideR;
        const float* p010 = p000 + (gc.i1 - gc.i0) * strideG;
        const float* p110 = p010 + (rc.i1 - rc.i0) * strideR;
        const float* p001 = p000 + (bc.i1 - bc.i0) * strideB;
        const float* p101 = p001 + (rc.i1 - rc.i0) * strideR;
        const float* p011 = p001 + (gc.i1 - gc.i0) * strideG;
        const float* p111 = p011 + (rc.i1 - rc.i0) * strideR;

        const float dr = rc.frac, idr = rc.ifrac;
        const float dg = gc.frac, idg = gc.ifrac;
        const float db = bc.frac, idb = bc.ifrac;

        float c0, c1;
        c0 = (p000[0]*idr+p100[0]*dr)*idg+(p010[0]*idr+p110[0]*dr)*dg;
        c1 = (p001[0]*idr+p101[0]*dr)*idg+(p011[0]*idr+p111[0]*dr)*dg;
        float lr = c0*idb + c1*db;
        c0 = (p000[1]*idr+p100[1]*dr)*idg+(p010[1]*idr+p110[1]*dr)*dg;
        c1 = (p001[1]*idr+p101[1]*dr)*idg+(p011[1]*idr+p111[1]*dr)*dg;
        float lg = c0*idb + c1*db;
        c0 = (p000[2]*idr+p100[2]*dr)*idg+(p010[2]*idr+p110[2]*dr)*dg;
        c1 = (p001[2]*idr+p101[2]*dr)*idg+(p011[2]*idr+p111[2]*dr)*dg;
        float lb = c0*idb + c1*db;

        if (full_intensity) {
            pixels[idx + 0] = (unsigned char)std::clamp((int)(lr * 255.0f + 0.5f), 0, 255);
            pixels[idx + 1] = (unsigned char)std::clamp((int)(lg * 255.0f + 0.5f), 0, 255);
            pixels[idx + 2] = (unsigned char)std::clamp((int)(lb * 255.0f + 0.5f), 0, 255);
        } else {
            const float inv255 = 1.0f / 255.0f;
            float Rn = pixels[idx + 0] * inv255;
            float Gn = pixels[idx + 1] * inv255;
            float Bn = pixels[idx + 2] * inv255;
            float outR = lr * tR + Rn * (1.0f - tR);
            float outG = lg * tG + Gn * (1.0f - tG);
            float outB = lb * tB + Bn * (1.0f - tB);
            pixels[idx + 0] = (unsigned char)std::clamp((int)(outR * 255.0f + 0.5f), 0, 255);
            pixels[idx + 1] = (unsigned char)std::clamp((int)(outG * 255.0f + 0.5f), 0, 255);
            pixels[idx + 2] = (unsigned char)std::clamp((int)(outB * 255.0f + 0.5f), 0, 255);
        }
    }
}


// ═══════════════════════════════════════════════════════════════════════════════
// .cube file loading — uses shared core parser + resampler
// ═══════════════════════════════════════════════════════════════════════════════

void ColorMap::load_cube_file()
{
    if (lut_path.empty()) {
        lut_data.clear();
        lut_size = 0;
        needs_lut_refresh = false;
        return;
    }

    // Read file into string
    std::ifstream file(lut_path);
    if (!file.is_open()) {
        lut_data.clear();
        lut_size = 0;
        needs_lut_refresh = false;
        return;
    }

    std::ostringstream ss;
    ss << file.rdbuf();
    std::string content = ss.str();

    // Parse using shared core
    std::vector<float> parsed_data;
    int parsed_size = 0;

    if (!cg::parseCubeText(content.c_str(), (int)content.size(), parsed_data, parsed_size)) {
        lut_data.clear();
        lut_size = 0;
        needs_lut_refresh = false;
        return;
    }

    // Resample large LUTs to 17³ for L1 cache friendliness
    constexpr int TARGET_LUT_SIZE = 17;
    if (parsed_size > TARGET_LUT_SIZE) {
        int total = TARGET_LUT_SIZE * TARGET_LUT_SIZE * TARGET_LUT_SIZE;
        lut_data.resize(total * 3);
        cg::resampleLut3D(parsed_data.data(), parsed_size,
                          lut_data.data(), TARGET_LUT_SIZE);
        lut_size = TARGET_LUT_SIZE;
    } else {
        lut_size = parsed_size;
        lut_data.swap(parsed_data);
    }

    needs_lut_refresh = false;
}


// ═══════════════════════════════════════════════════════════════════════════════
// Reference image loading — uses shared core for stats
// ═══════════════════════════════════════════════════════════════════════════════

void ColorMap::load_ref_image()
{
    has_ref_stats = false;
    has_cached_stats = false;
    needs_ref_refresh = false;

    if (ref_image_path.empty()) return;

    cg::initLookupTables();

    QImage img(QString::fromStdString(ref_image_path));
    if (img.isNull()) return;

    QImage rgba = img.convertToFormat(QImage::Format_RGBA8888);
    int w = rgba.width();
    int h = rgba.height();
    const unsigned char* pixels = rgba.constBits();

    int step = (w * h > 500000) ? 2 : 1;
    ref_stats = cg::computeLabStats(pixels, w, h, step);
    has_ref_stats = true;
}


// ═══════════════════════════════════════════════════════════════════════════════
// Constructors
// ═══════════════════════════════════════════════════════════════════════════════

void ColorMap::init_effect_details()
{
    InitEffectInfo();
    info.class_name  = "ColorMap";
    info.name        = "Color Map / Lookup";
    info.description = "Adjust colors using 3D LUT (.cube) or reference image color matching";
    info.has_video   = true;
    info.has_audio   = false;
}

ColorMap::ColorMap()
    : lut_path(""), lut_size(0), needs_lut_refresh(true),
      ref_image_path(""), needs_ref_refresh(false), has_ref_stats(false),
      has_cached_stats(false),
      intensity(1.0), intensity_r(1.0), intensity_g(1.0), intensity_b(1.0),
      cm_preserve(0.3), cm_luminance_blend(0.5),
      cm_saturation_boost(1.1), cm_contrast_boost(1.1)
{
    init_effect_details();
    cg::initLookupTables();
}

ColorMap::ColorMap(const std::string &path,
                   const Keyframe &i,
                   const Keyframe &iR,
                   const Keyframe &iG,
                   const Keyframe &iB)
    : lut_path(path), lut_size(0), needs_lut_refresh(true),
      ref_image_path(""), needs_ref_refresh(false), has_ref_stats(false),
      has_cached_stats(false),
      intensity(i), intensity_r(iR), intensity_g(iG), intensity_b(iB),
      cm_preserve(0.3), cm_luminance_blend(0.5),
      cm_saturation_boost(1.1), cm_contrast_boost(1.1)
{
    init_effect_details();
    cg::initLookupTables();
    load_cube_file();
}

void ColorMap::SetRefImagePath(const std::string& path) {
    ref_image_path = path;
    needs_ref_refresh = true;
    has_cached_stats = false;
    if (!path.empty()) {
        lut_path.clear();
        lut_data.clear();
        lut_size = 0;
    }
}


// ═══════════════════════════════════════════════════════════════════════════════
// GetFrame — main entry point
// ═══════════════════════════════════════════════════════════════════════════════

std::shared_ptr<openshot::Frame>
ColorMap::GetFrame(std::shared_ptr<openshot::Frame> frame, int64_t frame_number)
{
    if (needs_lut_refresh && !lut_path.empty()) {
        load_cube_file();
    }
    if (needs_ref_refresh && !ref_image_path.empty()) {
        load_ref_image();
    }

    bool is_lut_mode = (!lut_data.empty() && lut_size > 0);
    bool is_cm_mode  = (has_ref_stats && !ref_image_path.empty());

    if (!is_lut_mode && !is_cm_mode)
        return frame;

    auto image = frame->GetImage();
    int w = image->width(), h = image->height();
    unsigned char* pixels = image->bits();
    int pixel_count = w * h;

    if (is_lut_mode) {
        // ── LUT mode ────────────────────────────────────────────────────
        float overall = (float)intensity.GetValue(frame_number);
        float tR = (float)intensity_r.GetValue(frame_number) * overall;
        float tG = (float)intensity_g.GetValue(frame_number) * overall;
        float tB = (float)intensity_b.GetValue(frame_number) * overall;

        applyTrilinearLut(lut_data.data(), lut_size, pixels, pixel_count, tR, tG, tB);

    } else {
        // ── Color match mode ────────────────────────────────────────────

        // Only recompute stats every N frames for speed
        constexpr int STATS_INTERVAL = 4;
        bool should_check_stats = !has_cached_stats
                                  || (frame_number % STATS_INTERVAL == 0);

        if (should_check_stats) {
            int step = std::max(1, (w * h) / 10000);
            cg::LabStats srcStats = cg::computeLabStats(pixels, w, h, step);

            std::lock_guard<std::mutex> lock(bake_mutex);
            if (!has_cached_stats || !cg::statsAreSimilar(srcStats, cached_src_stats)) {
                // Bake via shared core
                baked_lut_data.resize(BAKED_LUT_SIZE * BAKED_LUT_SIZE * BAKED_LUT_SIZE * 3);
                cg::ColorMatchParams params;
                params.preserve        = (float)cm_preserve.GetValue(frame_number);
                params.luminanceBlend  = (float)cm_luminance_blend.GetValue(frame_number);
                params.saturationBoost = (float)cm_saturation_boost.GetValue(frame_number);
                params.contrastBoost   = (float)cm_contrast_boost.GetValue(frame_number);

                cg::bakeColorMatchLut(baked_lut_data.data(), BAKED_LUT_SIZE,
                                      srcStats, ref_stats, params);
                cached_src_stats = srcStats;
                has_cached_stats = true;
            }
        }

        applyTrilinearLut(baked_lut_data.data(), BAKED_LUT_SIZE, pixels, pixel_count, 1.0f, 1.0f, 1.0f);
    }

    return frame;
}


// ═══════════════════════════════════════════════════════════════════════════════
// JSON serialization
// ═══════════════════════════════════════════════════════════════════════════════

std::string ColorMap::Json() const {
    return JsonValue().toStyledString();
}

Json::Value ColorMap::JsonValue() const {
    Json::Value root = EffectBase::JsonValue();
    root["type"]           = info.class_name;
    root["lut_path"]       = lut_path;
    root["ref_image_path"] = ref_image_path;
    root["intensity"]   = intensity.JsonValue();
    root["intensity_r"] = intensity_r.JsonValue();
    root["intensity_g"] = intensity_g.JsonValue();
    root["intensity_b"] = intensity_b.JsonValue();
    root["cm_preserve"]         = cm_preserve.JsonValue();
    root["cm_luminance_blend"]  = cm_luminance_blend.JsonValue();
    root["cm_saturation_boost"] = cm_saturation_boost.JsonValue();
    root["cm_contrast_boost"]   = cm_contrast_boost.JsonValue();
    return root;
}

void ColorMap::SetJson(const std::string value) {
    try {
        const Json::Value root = openshot::stringToJson(value);
        SetJsonValue(root);
    }
    catch (...) {
        throw InvalidJSON("Invalid JSON for ColorMap effect");
    }
}

void ColorMap::SetJsonValue(const Json::Value root) {
    EffectBase::SetJsonValue(root);

    if (!root["lut_path"].isNull()) {
        lut_path = root["lut_path"].asString();
        needs_lut_refresh = true;
    }
    if (!root["ref_image_path"].isNull()) {
        ref_image_path = root["ref_image_path"].asString();
        needs_ref_refresh = true;
    }
    if (!root["intensity"].isNull())
        intensity.SetJsonValue(root["intensity"]);
    if (!root["intensity_r"].isNull())
        intensity_r.SetJsonValue(root["intensity_r"]);
    if (!root["intensity_g"].isNull())
        intensity_g.SetJsonValue(root["intensity_g"]);
    if (!root["intensity_b"].isNull())
        intensity_b.SetJsonValue(root["intensity_b"]);
    if (!root["cm_preserve"].isNull())
        cm_preserve.SetJsonValue(root["cm_preserve"]);
    if (!root["cm_luminance_blend"].isNull())
        cm_luminance_blend.SetJsonValue(root["cm_luminance_blend"]);
    if (!root["cm_saturation_boost"].isNull())
        cm_saturation_boost.SetJsonValue(root["cm_saturation_boost"]);
    if (!root["cm_contrast_boost"].isNull())
        cm_contrast_boost.SetJsonValue(root["cm_contrast_boost"]);
}

std::string ColorMap::PropertiesJSON(int64_t requested_frame) const {
    Json::Value root = BasePropertiesJSON(requested_frame);

    root["lut_path"] = add_property_json(
        "LUT File", 0.0, "string", lut_path, nullptr, 0, 0, false, requested_frame);

    root["intensity"] = add_property_json(
        "Overall Intensity", intensity.GetValue(requested_frame),
        "float", "", &intensity, 0.0, 1.0, false, requested_frame);
    root["intensity_r"] = add_property_json(
        "Red Intensity", intensity_r.GetValue(requested_frame),
        "float", "", &intensity_r, 0.0, 1.0, false, requested_frame);
    root["intensity_g"] = add_property_json(
        "Green Intensity", intensity_g.GetValue(requested_frame),
        "float", "", &intensity_g, 0.0, 1.0, false, requested_frame);
    root["intensity_b"] = add_property_json(
        "Blue Intensity", intensity_b.GetValue(requested_frame),
        "float", "", &intensity_b, 0.0, 1.0, false, requested_frame);

    root["ref_image_path"] = add_property_json(
        "Reference Image", 0.0, "string", ref_image_path, nullptr, 0, 0, false, requested_frame);
    root["cm_preserve"] = add_property_json(
        "Preservation", cm_preserve.GetValue(requested_frame),
        "float", "", &cm_preserve, 0.0, 1.0, false, requested_frame);
    root["cm_luminance_blend"] = add_property_json(
        "Luminance Blend", cm_luminance_blend.GetValue(requested_frame),
        "float", "", &cm_luminance_blend, 0.0, 1.0, false, requested_frame);
    root["cm_saturation_boost"] = add_property_json(
        "Saturation Boost", cm_saturation_boost.GetValue(requested_frame),
        "float", "", &cm_saturation_boost, 0.5, 2.0, false, requested_frame);
    root["cm_contrast_boost"] = add_property_json(
        "Contrast Boost", cm_contrast_boost.GetValue(requested_frame),
        "float", "", &cm_contrast_boost, 0.5, 2.0, false, requested_frame);

    return root.toStyledString();
}