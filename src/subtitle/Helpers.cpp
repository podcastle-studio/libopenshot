/**
 * @file
 * @brief Implementation of subtitle helper functions
 *
 * @ref License
 */

// Copyright (c) 2008-2024 OpenShot Studios, LLC
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "Helpers.h"
#include "SubtitleTypes.h"
#include "../KeyFrame.h"

#include <algorithm>
#include <cctype>

namespace openshot {
namespace subtitle {

std::string transformText(const std::string& text, const SubtitleTextStyle& style) {
    std::string result = text;

    switch (style.textTransform) {
        case TextTransform::UPPERCASE:
            std::transform(result.begin(), result.end(), result.begin(), ::toupper);
            break;
        case TextTransform::LOWERCASE:
            std::transform(result.begin(), result.end(), result.begin(), ::tolower);
            break;
        case TextTransform::CAPITALIZE:
            if (!result.empty()) {
                result[0] = std::toupper(result[0]);
            }
            break;
        default:
            break;
    }

    return result;
}

double getAnimatedValue(const std::vector<Point>& keyframes, const float timeMs, const float fps) {
    if (keyframes.empty()) return 0;

    // Convert milliseconds to frame number
    const int64_t frame = static_cast<int64_t>((timeMs / 1000.0f) * fps) + 1;

    // Create a temporary Keyframe object
    const Keyframe kf(keyframes);
    return kf.GetValue(frame);
}

std::string getAnimatedColor(const std::vector<std::pair<double, std::string>>& keyframes,
                             const float timeMs, const InterpolationType interpolation) {
    if (keyframes.empty()) {
        return "#FFFFFF";
    }
    if (keyframes.size() == 1) {
        return keyframes[0].second;
    }

    // Find surrounding keyframes
    for (size_t i = 0; i < keyframes.size() - 1; ++i) {
        if (timeMs >= keyframes[i].first && timeMs <= keyframes[i + 1].first) {
            if (interpolation == CONSTANT) {
                return keyframes[i].second;
            }

            // For LINEAR interpolation of colors
            float t = (timeMs - keyframes[i].first) / (keyframes[i + 1].first - keyframes[i].first);

            // Parse colors
            auto parseColor = [](const std::string& hex) -> std::tuple<int, int, int> {
                if (hex.empty() || hex[0] != '#') return {255, 255, 255};
                int r = std::stoi(hex.substr(1, 2), nullptr, 16);
                int g = std::stoi(hex.substr(3, 2), nullptr, 16);
                int b = std::stoi(hex.substr(5, 2), nullptr, 16);
                return {r, g, b};
            };

            auto [r1, g1, b1] = parseColor(keyframes[i].second);
            auto [r2, g2, b2] = parseColor(keyframes[i + 1].second);

            // Interpolate
            const int r = r1 + (r2 - r1) * t;
            const int g = g1 + (g2 - g1) * t;
            const int b = b1 + (b2 - b1) * t;

            // Convert back to hex
            char buffer[8];
            snprintf(buffer, sizeof(buffer), "#%02X%02X%02X", r, g, b);
            return buffer;
        }
    }

    // Before first or after last
    if (timeMs < keyframes[0].first) return keyframes[0].second;
    return keyframes.back().second;
}

SubtitleTextStyle applyAnimationParams(const std::vector<AnimationParam>& params, const std::vector<AnimationParamColor>& colorParams,
    const float timeMs, const float fps, const SubtitleTextStyle& baseStyle) {
    SubtitleTextStyle result = baseStyle;

    // Apply numeric parameters using OpenShot's Keyframe
    for (const auto& param : params) {
        const auto value = getAnimatedValue(param.keyframes, timeMs, fps);

        if (param.name == "fontSize") result.fontSize = value;
        else if (param.name == "opacity") result.opacity = value;
        else if (param.name == "letterSpacing") result.letterSpacing = value;
        else if (param.name == "lineHeight") result.lineHeight = value;
        else if (param.name == "bold") result.bold = static_cast<int>(value);
        else if (param.name == "translateX") result.translateX = value;
        else if (param.name == "translateY") result.translateY = value;
        else if (param.name == "strokeWidth") result.strokeWidth = value;
        else if (param.name == "strokeOpacity") result.strokeOpacity = value;
        else if (param.name == "shadowBlur") result.shadowBlur = value;
        else if (param.name == "shadowDistance") result.shadowDistance = value;
        else if (param.name == "shadowAngle") result.shadowAngle = value;
        else if (param.name == "shadowOpacity") result.shadowOpacity = value;
        else if (param.name == "backgroundOpacity") result.backgroundOpacity = value;
        else if (param.name == "backgroundRadius") result.backgroundRadius = value;
        else if (param.name == "backgroundPaddingX") result.backgroundPaddingX = value;
        else if (param.name == "backgroundPaddingY") result.backgroundPaddingY = value;
    }

    // Apply color parameters
    for (const auto& param : colorParams) {
        std::string value = getAnimatedColor(param.keyframes, timeMs, param.interpolation);

        if (param.name == "color") result.color = value;
        else if (param.name == "strokeColor") result.strokeColor = value;
        else if (param.name == "shadowColor") result.shadowColor = value;
        else if (param.name == "backgroundColor") result.backgroundColor = value;
    }

    return result;
}

int64_t msToFrame(const float ms, const float fps) {
    return static_cast<int64_t>((ms / 1000.0f) * fps) + 1;
}

float frameToMs(const int64_t frame, const float fps) {
    return (frame - 1) / fps * 1000.0f;
}

std::vector<WordAnimation> processSegmentAnimation(const std::vector<WordDetail>& wordDetails, const SegmentSettings& settings, const float fps) {
    std::vector<WordAnimation> animations;

    for (const auto& detail : wordDetails) {
        WordAnimation anim;
        anim.word = detail.word;

        // Apply in/out animations
        const auto& animSettings = settings.animationSettings;
        // In animation
        if (animSettings.inSpeed > 0) {
            const float inDuration = animSettings.inSpeed;

            // Apply numeric in styles
            for (const auto& [key, value] : animSettings.inStyles) {
                AnimationParam param;
                param.name = key;

                // Get base value from default style
                double baseValue = 0;
                if (key == "fontSize") baseValue = settings.defaultStyle.fontSize;
                else if (key == "opacity") baseValue = settings.defaultStyle.opacity;
                else if (key == "backgroundOpacity") baseValue = settings.defaultStyle.backgroundOpacity.value_or(0);
                else if (key == "backgroundRadius") baseValue = settings.defaultStyle.backgroundRadius.value_or(0);
                else if (key == "backgroundPaddingX") baseValue = settings.defaultStyle.backgroundPaddingX.value_or(0);
                else if (key == "backgroundPaddingY") baseValue = settings.defaultStyle.backgroundPaddingY.value_or(0);

                // Create keyframes as Points
                const int64_t startFrame = msToFrame(detail.startMs, fps);
                const int64_t endFrame = msToFrame(detail.startMs + inDuration, fps);

                param.keyframes.emplace_back(startFrame, baseValue, animSettings.inInterpolation);
                param.keyframes.emplace_back(endFrame, value, animSettings.inInterpolation);

                anim.params.push_back(param);
            }

            // Apply color in styles
            for (const auto& [key, value] : animSettings.inStylesColor) {
                AnimationParamColor param;
                param.name = key;
                param.interpolation = animSettings.inInterpolation;

                // Get base color from default style
                std::string baseColor = "#FFFFFF";
                if (key == "color") baseColor = settings.defaultStyle.color;
                else if (key == "backgroundColor") baseColor = settings.defaultStyle.backgroundColor.value_or("#FFFFFF");

                param.keyframes.emplace_back(detail.startMs, baseColor);
                param.keyframes.emplace_back(detail.startMs + inDuration, value);

                anim.colorParams.push_back(param);
            }
        }

        animations.push_back(anim);
    }

    return animations;
}

} // namespace subtitle
} // namespace openshot