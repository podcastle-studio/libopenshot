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
#include "ColorGradingCore.h"

#include <omp.h>
#include "EffectShaders.h"
#include "../gpu/GpuDevice.h"
#include "../gpu/GpuFrame.h"

#include "skia/include/core/SkImage.h"
#include "skia/include/core/SkImageInfo.h"
#include "skia/include/core/SkPixmap.h"
#include "skia/include/effects/SkRuntimeEffect.h"

#include <cmath>
#include <algorithm>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <vector>
#include <mutex>
#include <QImage>

using namespace openshot;


// ═══════════════════════════════════════════════════════════════════════════════
// Impl — private implementation hidden from the header
// ═══════════════════════════════════════════════════════════════════════════════

struct ColorMap::Impl {
    // ── LUT mode state ──────────────────────────────────────────────────
    std::string lut_path;
    int lut_size = 0;
    std::vector<float> lut_data;       ///< Stride-3 [N³ × 3], at the cube's own size
    /// The cube's declared input range. The editor normalises by it, so the export has to as
    /// well or any non-0..1 cube grades differently in the two -- see GPU-DECISIONS.md,
    /// "the front end never calls the WASM for LUTs".
    float lut_domain_min[3] = {0.0f, 0.0f, 0.0f};
    float lut_domain_span[3] = {1.0f, 1.0f, 1.0f};
    bool needs_lut_refresh = true;
    /// Bumped whenever lut_data changes, so the GPU atlas below knows to be rebuilt. A pointer
    /// comparison would not do: a vector can reallocate onto the same address.
    unsigned long long lut_version = 0;

    /// The cube as a texture, in the editor's packing: lut_size wide (the R axis), lut_size²
    /// high (row = b * size + g). Rebuilt only when the cube changes or the device has been torn
    /// down since it was made -- a Graphite texture does not survive that, hence the generation.
    struct LutAtlas {
        std::shared_ptr<openshot::GpuFrame> owner;
        sk_sp<SkImage> texture;
        unsigned long long version = 0;
        unsigned long long generation = 0;
    };
    mutable std::shared_ptr<LutAtlas> atlas;

    // ── Color match mode state ──────────────────────────────────────────
    std::string ref_image_path;
    bool needs_ref_refresh = false;

    ColorGrading::LabStats ref_stats;
    bool has_ref_stats = false;

    std::vector<float> baked_lut_data; ///< Stride-3 [17³ × 3]
    static constexpr int BAKED_LUT_SIZE = 17;

    ColorGrading::LabStats cached_src_stats;
    bool has_cached_stats = false;

    std::mutex bake_mutex;

    // ── Internal methods ────────────────────────────────────────────────
    void load_cube_file();
    void load_ref_image();

    /// OpenMP parallelized trilinear apply with coord table + fast paths
    static void applyTrilinearLut(const float* lut, int size,
                                  unsigned char* pixels, int pixel_count,
                                  float tR, float tG, float tB,
                                  const float* domain_min, const float* domain_span);
};


// ═══════════════════════════════════════════════════════════════════════════════
// OpenMP trilinear LUT apply (platform-specific optimization)
// ═══════════════════════════════════════════════════════════════════════════════

void ColorMap::Impl::applyTrilinearLut(const float* lut, int size,
                                  unsigned char* pixels, int pixel_count,
                                  float tR, float tG, float tB,
                                  const float* domain_min, const float* domain_span) {

    // Precompute byte→LUT coordinate table (256 entries, ~1μs)
    struct LutCoord {
        int i0, i1;
        float frac, ifrac;
    };
    // One table per channel, because DOMAIN_MIN/MAX are per channel. The normalisation is the
    // editor's, spelled the same way: clamp((v - min) / span, 0, 1) and then onto the cube.
    LutCoord coord3[3][256];
    {
        const float sizeM1 = (float)(size - 1);
        const float inv255 = 1.0f / 255.0f;
        for (int c = 0; c < 3; c++) {
            const float dmin = domain_min ? domain_min[c] : 0.0f;
            const float dspan = domain_span ? domain_span[c] : 1.0f;
            for (int i = 0; i < 256; i++) {
                float t = ((float)i * inv255 - dmin) / dspan;
                t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
                float f = t * sizeM1;
                int i0 = (int)f;
                if (i0 > size - 1) i0 = size - 1;
                coord3[c][i].i0    = i0;
                coord3[c][i].i1    = std::min(i0 + 1, size - 1);
                coord3[c][i].frac  = f - (float)i0;
                coord3[c][i].ifrac = 1.0f - (f - (float)i0);
            }
        }
    }
    const LutCoord* coordR = coord3[0];
    const LutCoord* coordG = coord3[1];
    const LutCoord* coordB = coord3[2];

    const int strideR = 3;
    const int strideG = size * 3;
    const int strideB = size * size * 3;
    const bool full_intensity = (tR >= 0.999f && tG >= 0.999f && tB >= 0.999f);

    #pragma omp parallel for schedule(static)
    for (int i = 0; i < pixel_count; ++i) {
        const int idx = i * 4;
        const int A = pixels[idx + 3];
        if (A == 0) continue;

        const LutCoord& rc = coordR[pixels[idx + 0]];
        const LutCoord& gc = coordG[pixels[idx + 1]];
        const LutCoord& bc = coordB[pixels[idx + 2]];

        if (A != 255) {
            // Slow path: semi-transparent pixel — demultiply first
            const float inv255 = 1.0f / 255.0f;
            const float alpha = A * inv255;
            const float invAlpha = 1.0f / alpha;

            int trueR = std::min((int)(pixels[idx + 0] * invAlpha + 0.5f), 255);
            int trueG = std::min((int)(pixels[idx + 1] * invAlpha + 0.5f), 255);
            int trueB = std::min((int)(pixels[idx + 2] * invAlpha + 0.5f), 255);

            const LutCoord& rc2 = coordR[trueR];
            const LutCoord& gc2 = coordG[trueG];
            const LutCoord& bc2 = coordB[trueB];

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

void ColorMap::Impl::load_cube_file()
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

    float parsed_domain_min[3] = {0.0f, 0.0f, 0.0f};
    float parsed_domain_max[3] = {1.0f, 1.0f, 1.0f};
    if (!ColorGrading::parseCubeText(content.c_str(), (int)content.size(), parsed_data, parsed_size,
                                     parsed_domain_min, parsed_domain_max)) {
        lut_data.clear();
        lut_size = 0;
        needs_lut_refresh = false;
        return;
    }

    // The cube is kept at its own size. It used to be resampled to 17³ "for L1 cache
    // friendliness", and that resample was the largest measured editor/export divergence in the
    // colour path: on the production 25³ LUT it costs max 9.95 LSB / mean 0.657, where the
    // interpolation choice either side of it costs max 6.32 / mean 0.269. The editor samples the
    // cube at its native size and so does this now -- decided in W11, and confirmed against the
    // editor's own shader on 2026-09-22. It costs LUT throughput: 33³ is 431 KB against 17³'s
    // 59 KB, so the table no longer lives in L2.
    lut_size = parsed_size;
    lut_data.swap(parsed_data);
    ++lut_version;

    for (int c = 0; c < 3; ++c) {
        lut_domain_min[c] = parsed_domain_min[c];
        const float span = parsed_domain_max[c] - parsed_domain_min[c];
        // The editor collapses a degenerate span to 1 rather than dividing by zero; so does this.
        lut_domain_span[c] = std::abs(span) < 1e-12f ? 1.0f : span;
    }

    needs_lut_refresh = false;
}


// ═══════════════════════════════════════════════════════════════════════════════
// Reference image loading — uses shared core for stats
// ═══════════════════════════════════════════════════════════════════════════════

void ColorMap::Impl::load_ref_image()
{
    has_ref_stats = false;
    has_cached_stats = false;
    needs_ref_refresh = false;

    if (ref_image_path.empty()) return;

    ColorGrading::initLookupTables();

    QImage img(QString::fromStdString(ref_image_path));
    if (img.isNull()) return;

    QImage rgba = img.convertToFormat(QImage::Format_RGBA8888);
    int w = rgba.width();
    int h = rgba.height();
    const unsigned char* pixels = rgba.constBits();

    int step = (w * h > 500000) ? 2 : 1;
    ref_stats = ColorGrading::computeLabStats(pixels, w, h, step);
    has_ref_stats = true;
}


// ═══════════════════════════════════════════════════════════════════════════════
// Constructors / Destructor
// ═══════════════════════════════════════════════════════════════════════════════

static void init_effect_details(EffectBase& self)
{
    self.InitEffectInfo();
    self.info.class_name  = "ColorMap";
    self.info.name        = "Color Map / Lookup";
    self.info.description = "Adjust colors using 3D LUT (.cube) or reference image color matching";
    self.info.has_video   = true;
    self.info.has_audio   = false;
}

ColorMap::ColorMap()
    : pimpl(std::make_unique<Impl>()),
      intensity(1.0), intensity_r(1.0), intensity_g(1.0), intensity_b(1.0),
      cm_preserve(0.3), cm_luminance_blend(0.5),
      cm_saturation_boost(1.1), cm_contrast_boost(1.1)
{
    init_effect_details(*this);
    ColorGrading::initLookupTables();
}

ColorMap::~ColorMap() = default;

ColorMap::ColorMap(const std::string &path,
                   const Keyframe &i,
                   const Keyframe &iR,
                   const Keyframe &iG,
                   const Keyframe &iB)
    : pimpl(std::make_unique<Impl>()),
      intensity(i), intensity_r(iR), intensity_g(iG), intensity_b(iB),
      cm_preserve(0.3), cm_luminance_blend(0.5),
      cm_saturation_boost(1.1), cm_contrast_boost(1.1)
{
    pimpl->lut_path = path;
    init_effect_details(*this);
    ColorGrading::initLookupTables();
    pimpl->load_cube_file();
}

void ColorMap::SetRefImagePath(const std::string& path) {
    pimpl->ref_image_path = path;
    pimpl->needs_ref_refresh = true;
    pimpl->has_cached_stats = false;
    if (!path.empty()) {
        pimpl->lut_path.clear();
        pimpl->lut_data.clear();
        pimpl->lut_size = 0;
    }
}

std::string ColorMap::GetRefImagePath() const {
    return pimpl->ref_image_path;
}


// ═══════════════════════════════════════════════════════════════════════════════
// GetFrame — main entry point
// ═══════════════════════════════════════════════════════════════════════════════

std::shared_ptr<openshot::Frame>
ColorMap::GetFrame(std::shared_ptr<openshot::Frame> frame, int64_t frame_number)
{
    auto& d = *pimpl;

    if (d.needs_lut_refresh && !d.lut_path.empty()) {
        d.load_cube_file();
    }
    if (d.needs_ref_refresh && !d.ref_image_path.empty()) {
        d.load_ref_image();
    }

    bool is_lut_mode = (!d.lut_data.empty() && d.lut_size > 0);
    bool is_cm_mode  = (d.has_ref_stats && !d.ref_image_path.empty());

    if (!is_lut_mode && !is_cm_mode)
        return frame;

    // The shader when there is a GPU to run it on, before GetImage() -- which on a GPU-backed
    // frame is a readback. Colour-match mode is not ported (its cube is re-baked from the frame's
    // own statistics every few frames), so it declines in SetGpuUniforms and runs the C++.
    if (is_lut_mode && ApplyOnGpu(frame, frame_number))
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

        Impl::applyTrilinearLut(d.lut_data.data(), d.lut_size, pixels, pixel_count, tR, tG, tB,
                                d.lut_domain_min, d.lut_domain_span);

    } else {
        // ── Color match mode ────────────────────────────────────────────

        // Only recompute stats every N frames for speed
        constexpr int STATS_INTERVAL = 4;
        bool should_check_stats = !d.has_cached_stats
                                  || (frame_number % STATS_INTERVAL == 0);

        if (should_check_stats) {
            int step = std::max(1, (w * h) / 10000);
            ColorGrading::LabStats srcStats = ColorGrading::computeLabStats(pixels, w, h, step);

            std::lock_guard<std::mutex> lock(d.bake_mutex);
            if (!d.has_cached_stats || !ColorGrading::statsAreSimilar(srcStats, d.cached_src_stats)) {
                // Bake via shared core
                d.baked_lut_data.resize(Impl::BAKED_LUT_SIZE * Impl::BAKED_LUT_SIZE * Impl::BAKED_LUT_SIZE * 3);
                ColorGrading::ColorMatchParams params;
                params.preserve        = (float)cm_preserve.GetValue(frame_number);
                params.luminanceBlend  = (float)cm_luminance_blend.GetValue(frame_number);
                params.saturationBoost = (float)cm_saturation_boost.GetValue(frame_number);
                params.contrastBoost   = (float)cm_contrast_boost.GetValue(frame_number);

                ColorGrading::bakeColorMatchLut(d.baked_lut_data.data(), Impl::BAKED_LUT_SIZE,
                                      srcStats, d.ref_stats, params);
                d.cached_src_stats = srcStats;
                d.has_cached_stats = true;
            }
        }

        // The baked colour-match LUT is defined on 0..1 by construction, so it takes the unit
        // domain rather than the .cube's.
        static constexpr float kUnitMin[3] = {0.0f, 0.0f, 0.0f};
        static constexpr float kUnitSpan[3] = {1.0f, 1.0f, 1.0f};
        Impl::applyTrilinearLut(d.baked_lut_data.data(), Impl::BAKED_LUT_SIZE, pixels, pixel_count,
                                1.0f, 1.0f, 1.0f, kUnitMin, kUnitSpan);
    }

    return frame;
}


// ═══════════════════════════════════════════════════════════════════════════════
// GPU
// ═══════════════════════════════════════════════════════════════════════════════

// The SkSL source lives in src/shaders/color_map.sksl; it is embedded here at build time so
// the export has no runtime data-path dependency.
const char* ColorMap::GpuShaderSource() const
{
    return openshot::shaders::kColorMap;
}

bool ColorMap::SetGpuUniforms(SkRuntimeEffectBuilder& builder, int64_t frame_number,
                              int width, int height) const
{
    (void)width;
    (void)height;
    Impl& d = *pimpl;

    // Only LUT mode. Colour-match bakes its cube from the frame's own Lab statistics every few
    // frames, which is a readback of the very frame this pass exists to keep on the GPU.
    if (d.lut_data.empty() || d.lut_size <= 1)
        return false;

    const unsigned long long generation = GpuDevice::Generation();
    if (!d.atlas || d.atlas->version != d.lut_version ||
        d.atlas->generation != generation || !d.atlas->texture) {
        auto cache = std::make_shared<Impl::LutAtlas>();
        cache->version = d.lut_version;
        cache->generation = generation;

        // The editor's packing, so both stacks read the same table the same way: width is the
        // R axis, row is b * size + g. F16 rather than 8-bit because a cube entry is a float and
        // quantising it to a byte would put the error in the table rather than in the result --
        // half carries eleven bits of mantissa, a tenth of an LSB on a 0..1 value.
        const int size = d.lut_size;
        const int atlas_w = size;
        const int atlas_h = size * size;
        std::vector<float> rgba(static_cast<size_t>(atlas_w) * atlas_h * 4, 0.0f);
        for (int b = 0; b < size; ++b)
            for (int g = 0; g < size; ++g)
                for (int r = 0; r < size; ++r) {
                    // .cube order is R fastest, then G, then B -- the same entry the CPU's
                    // strides spell as r*3 + g*size*3 + b*size*size*3.
                    const size_t src = (static_cast<size_t>(b) * size * size +
                                        static_cast<size_t>(g) * size + r) * 3;
                    const size_t dst = ((static_cast<size_t>(b) * size + g) *
                                        static_cast<size_t>(atlas_w) + r) * 4;
                    rgba[dst + 0] = d.lut_data[src + 0];
                    rgba[dst + 1] = d.lut_data[src + 1];
                    rgba[dst + 2] = d.lut_data[src + 2];
                    rgba[dst + 3] = 1.0f;
                }

        const SkPixmap source(
            SkImageInfo::Make(atlas_w, atlas_h, kRGBA_F32_SkColorType, kUnpremul_SkAlphaType),
            rgba.data(), static_cast<size_t>(atlas_w) * 4 * sizeof(float));

        // Converted to half on the CPU rather than handed over as F32: Graphite declines to make
        // a texture out of an F32 raster image, and the upload silently fails if it is asked to.
        std::vector<uint16_t> halves(static_cast<size_t>(atlas_w) * atlas_h * 4, 0);
        const SkPixmap pixels(
            SkImageInfo::Make(atlas_w, atlas_h, kRGBA_F16_SkColorType, kUnpremul_SkAlphaType),
            halves.data(), static_cast<size_t>(atlas_w) * 4 * sizeof(uint16_t));
        if (!source.readPixels(pixels))
            return false;

        cache->owner = GpuFrame::Create(atlas_w, atlas_h, kRGBA_F16_SkColorType);
        if (!cache->owner || !cache->owner->upload(pixels))
            return false;
        cache->texture = cache->owner->snapshot();
        if (!cache->texture)
            return false;
        d.atlas = std::move(cache);
    }

    // NEAREST is mandatory: the atlas stacks the cube's blue slices one above the next, so a
    // bilinear sampler would blend across a tile boundary. All three interpolations are done by
    // hand in the fragment, which is what the editor does and for the same reason.
    builder.child("lut") = d.atlas->texture->makeShader(SkTileMode::kClamp, SkTileMode::kClamp,
                                                        SkSamplingOptions());

    const float overall = static_cast<float>(intensity.GetValue(frame_number));
    const float tR = static_cast<float>(intensity_r.GetValue(frame_number)) * overall;
    const float tG = static_cast<float>(intensity_g.GetValue(frame_number)) * overall;
    const float tB = static_cast<float>(intensity_b.GetValue(frame_number)) * overall;

    builder.uniform("lutSize") = static_cast<float>(d.lut_size);
    builder.uniform("domainMin") = SkV3{d.lut_domain_min[0], d.lut_domain_min[1],
                                        d.lut_domain_min[2]};
    builder.uniform("domainSpan") = SkV3{d.lut_domain_span[0], d.lut_domain_span[1],
                                         d.lut_domain_span[2]};
    builder.uniform("intensity") = SkV3{tR, tG, tB};
    // The C++ takes a separate branch when every channel is at full strength; so does the
    // fragment, because that branch skips the blend rather than blending by one.
    builder.uniform("fullIntensity") =
        (tR >= 0.999f && tG >= 0.999f && tB >= 0.999f) ? 1.0f : 0.0f;
    return true;
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
    root["lut_path"]       = pimpl->lut_path;
    root["ref_image_path"] = pimpl->ref_image_path;
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
        pimpl->lut_path = root["lut_path"].asString();
        pimpl->needs_lut_refresh = true;
    }
    if (!root["ref_image_path"].isNull()) {
        pimpl->ref_image_path = root["ref_image_path"].asString();
        pimpl->needs_ref_refresh = true;
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
        "LUT File", 0.0, "string", pimpl->lut_path, nullptr, 0, 0, false, requested_frame);

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
        "Reference Image", 0.0, "string", pimpl->ref_image_path, nullptr, 0, 0, false, requested_frame);
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
