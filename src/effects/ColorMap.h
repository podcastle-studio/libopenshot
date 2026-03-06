/**
 * @file
 * @brief Header file for ColorMap (LUT + Color Match) effect
 * @author Jonathan Thomas <jonathan@openshot.org>
 *
 * @ref License
 */

// Copyright (c) 2008-2025 OpenShot Studios, LLC
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#ifndef OPENSHOT_COLORMAP_EFFECT_H
#define OPENSHOT_COLORMAP_EFFECT_H

#include "../EffectBase.h"
#include "../Json.h"
#include "../KeyFrame.h"
#include "image-processing-lib/src/ColorGrading/ColorGradingCore.h"

#include <vector>
#include <string>
#include <mutex>

namespace openshot
{

    /**
     * @brief Applies a 3D LUT (.cube) color transform or Reinhard color matching to each frame.
     *
     * Two modes (auto-detected from which path is set):
     *
     * **LUT mode** (`lut_path` set): Loads a .cube file, resamples to 17³,
     * applies trilinear interpolation per pixel with keyframable per-channel intensities.
     *
     * **Color Match mode** (`ref_image_path` set): Computes Lab statistics from a reference
     * image once, then per frame: computes source stats (downsampled), bakes Reinhard color
     * transfer into a 17³ 3D LUT, and applies via the same fast trilinear path.
     *
     * Core algorithms shared with WASM web build via color_grading_core.h/cpp.
     * This wrapper adds: OpenMP parallelized trilinear apply, QImage loading,
     * EffectBase integration, keyframe animation.
     */
    class ColorMap : public EffectBase
    {
    private:
        // ── LUT mode state ──────────────────────────────────────────────────
        std::string lut_path;
        int lut_size;
        std::vector<float> lut_data;       ///< Stride-3 [N³ × 3], resampled to 17³
        bool needs_lut_refresh;

        // ── Color match mode state ──────────────────────────────────────────
        std::string ref_image_path;
        bool needs_ref_refresh;

        ColorGrading::LabStats ref_stats;
        bool has_ref_stats;

        std::vector<float> baked_lut_data; ///< Stride-3 [17³ × 3]
        static constexpr int BAKED_LUT_SIZE = 17;

        ColorGrading::LabStats cached_src_stats;
        bool has_cached_stats;

        std::mutex bake_mutex;

        // ── Internal methods ────────────────────────────────────────────────
        void init_effect_details();
        void load_cube_file();
        void load_ref_image();

        /// OpenMP parallelized trilinear apply with coord table + fast paths
        static void applyTrilinearLut(const float* lut, int size,
                                      unsigned char* pixels, int pixel_count,
                                      float tR, float tG, float tB);

    public:
        // ── LUT mode keyframes ──────────────────────────────────────────────
        Keyframe intensity;
        Keyframe intensity_r;
        Keyframe intensity_g;
        Keyframe intensity_b;

        // ── Color match keyframes ───────────────────────────────────────────
        Keyframe cm_preserve;
        Keyframe cm_luminance_blend;
        Keyframe cm_saturation_boost;
        Keyframe cm_contrast_boost;

        ColorMap();

        ColorMap(const std::string &path,
                 const Keyframe &i  = Keyframe(1.0),
                 const Keyframe &iR = Keyframe(1.0),
                 const Keyframe &iG = Keyframe(1.0),
                 const Keyframe &iB = Keyframe(1.0));

        void SetRefImagePath(const std::string& path);
        std::string GetRefImagePath() const { return ref_image_path; }

        std::shared_ptr<openshot::Frame>
        GetFrame(int64_t frame_number) override
        { return GetFrame(std::make_shared<openshot::Frame>(), frame_number); }

        std::shared_ptr<openshot::Frame>
        GetFrame(std::shared_ptr<openshot::Frame> frame,
                 int64_t frame_number) override;

        std::string Json() const override;
        Json::Value JsonValue() const override;
        void SetJson(const std::string value) override;
        void SetJsonValue(const Json::Value root) override;
        std::string PropertiesJSON(int64_t requested_frame) const override;
    };

} // namespace openshot

#endif // OPENSHOT_COLORMAP_EFFECT_H