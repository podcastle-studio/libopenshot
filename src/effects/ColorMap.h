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

#include <memory>
#include <string>

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
        struct Impl;
        std::unique_ptr<Impl> pimpl;

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
        ~ColorMap();

        ColorMap(const std::string &path,
                 const Keyframe &i  = Keyframe(1.0),
                 const Keyframe &iR = Keyframe(1.0),
                 const Keyframe &iG = Keyframe(1.0),
                 const Keyframe &iB = Keyframe(1.0));

        void SetRefImagePath(const std::string& path);
        std::string GetRefImagePath() const;

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