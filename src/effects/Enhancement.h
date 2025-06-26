// Enhancement.h
/**
 * @file
 * @brief Header file for Enhancement class
 *
 * @ref License
 */

// Copyright (c) 2024 OpenShot Studios, LLC
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#ifndef OPENSHOT_ENHANCEMENT_H
#define OPENSHOT_ENHANCEMENT_H

#include "../EffectBase.h"
#include "../Frame.h"
#include "../Json.h"
#include "../KeyFrame.h"

#include <memory>
#include <string>
#include <cmath>

namespace openshot
{
    /**
     * @brief This class provides image enhancement controls
     *
     * The Enhancement effect allows for image quality improvements with
     * controls for noise reduction, clarity, and sharpness.
     * All parameters can be animated with keyframes.
     */
    class Enhancement : public EffectBase
    {
    private:
        /// Init effect settings
        void init_effect_details();

        /// Helper functions for enhancement
        static int clamp(int value, int min = 0, int max = 255) {
            return std::max(min, std::min(max, value));
        }

        static double clampDouble(double value, double min = 0.0, double max = 255.0) {
            return std::max(min, std::min(max, value));
        }

    public:
        // Enhancement keyframes
        Keyframe noise;   ///< Noise reduction keyframe (-100 to 100, 0 is default)
        Keyframe clarity;           ///< Clarity keyframe (-100 to 100, 0 is default)
        Keyframe sharpness;         ///< Sharpness keyframe (-100 to 100, 0 is default)

        /// Blank constructor, useful when using Json to load the effect properties
        Enhancement();

        /// Default constructor with all parameters
        Enhancement(Keyframe noise_reduction, Keyframe clarity, Keyframe sharpness);

        /// @brief This method is required for all derived classes of ClipBase, and returns a
        /// new openshot::Frame object. All Clip keyframes and effects are resolved into
        /// pixels.
        ///
        /// @returns A new openshot::Frame object
        /// @param frame_number The frame number (starting at 1) of the clip or effect on the timeline.
        std::shared_ptr<openshot::Frame> GetFrame(int64_t frame_number) override {
            return GetFrame(std::make_shared<openshot::Frame>(), frame_number);
        }

        /// @brief This method is required for all derived classes of ClipBase, and returns a
        /// modified openshot::Frame object
        ///
        /// The frame object is passed into this method and used as a starting point (pixels and audio).
        /// All Clip keyframes and effects are resolved into pixels.
        ///
        /// @returns The modified openshot::Frame object
        /// @param frame The frame object that needs the clip or effect applied to it
        /// @param frame_number The frame number (starting at 1) of the clip or effect on the timeline.
        std::shared_ptr<openshot::Frame> GetFrame(std::shared_ptr<openshot::Frame> frame, int64_t frame_number) override;

        // Get and Set JSON methods
        std::string Json() const override; ///< Generate JSON string of this object
        void SetJson(const std::string value) override; ///< Load JSON string into this object
        Json::Value JsonValue() const override; ///< Generate Json::Value for this object
        void SetJsonValue(const Json::Value root) override; ///< Load Json::Value into this object

        /// Get all properties for a specific frame (perfect for a UI to display the current state
        /// of all properties at any time)
        std::string PropertiesJSON(int64_t requested_frame) const override;
    };
}

#endif