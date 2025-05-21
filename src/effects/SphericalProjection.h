/**
* @file
 * @brief Header file for SphericalProjection effect class
 * @author Jonathan Thomas <jonathan@openshot.org>
 *
 * @ref License
 */

// Copyright (c) 2008-2025 OpenShot Studios, LLC
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#ifndef OPENSHOT_SPHERICAL_PROJECTION_EFFECT_H
#define OPENSHOT_SPHERICAL_PROJECTION_EFFECT_H

#include "../EffectBase.h"
#include "../Frame.h"
#include "../Json.h"
#include "../KeyFrame.h"

#include <memory>
#include <string>

namespace openshot
{

    /**
     * @brief Projects a 360° frame through a pinhole camera.
     * You can choose full sphere or hemisphere, and nearest-neighbor or bilinear sampling.
     */
    class SphericalProjection : public EffectBase
    {
    private:
        void init_effect_details();

    public:
        Keyframe yaw;      ///< Yaw around up-axis (degrees)
        Keyframe pitch;    ///< Pitch around right-axis (degrees)
        Keyframe roll;     ///< Roll around forward-axis (degrees)
        Keyframe fov;      ///< Field-of-view (horizontal degrees)

        int projection_mode; ///< 0 = full sphere, 1 = hemisphere
        int interpolation;   ///< 0 = nearest, 1 = bilinear

        SphericalProjection();
        SphericalProjection(Keyframe new_yaw,
                            Keyframe new_pitch,
                            Keyframe new_roll,
                            Keyframe new_fov);

        std::shared_ptr<Frame> GetFrame(int64_t frame_number) override
        { return GetFrame(std::make_shared<Frame>(), frame_number); }

        std::shared_ptr<Frame> GetFrame(std::shared_ptr<Frame> frame,
                                        int64_t frame_number) override;

        std::string Json() const override;
        void SetJson(const std::string value) override;
        Json::Value JsonValue() const override;
        void SetJsonValue(const Json::Value root) override;
        std::string PropertiesJSON(int64_t requested_frame) const override;
    };

} // namespace openshot

#endif // OPENSHOT_SPHERICAL_PROJECTION_EFFECT_H
