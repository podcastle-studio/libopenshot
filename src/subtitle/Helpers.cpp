#include "Helpers.h"
#include "SubtitleTypes.h"
#include "../KeyFrame.h"

#include <algorithm>
#include <cctype>
#include <cmath>

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
                for (size_t i = 1; i < result.length(); ++i) {
                    if (std::isspace(result[i-1])) {
                        result[i] = std::toupper(result[i]);
                    }
                }
            }
            break;
        default:
            break;
    }

    return result;
}

double getAnimatedValue(const Keyframe& keyframe, const float timeMs, const float fps) {
    // Convert milliseconds to frame number (OpenShot frames are 1-indexed)
    const int64_t frame = static_cast<int64_t>((timeMs / 1000.0f) * fps) + 1;
    return keyframe.GetValue(frame);
}

// Helper functions for color conversion
std::tuple<int, int, int> hexToRgb(const std::string& hex) {
    if (hex.empty() || hex[0] != '#' || hex.length() < 7) return {255, 255, 255};
    try {
        int r = std::stoi(hex.substr(1, 2), nullptr, 16);
        int g = std::stoi(hex.substr(3, 2), nullptr, 16);
        int b = std::stoi(hex.substr(5, 2), nullptr, 16);
        return {r, g, b};
    } catch (...) {
        return {255, 255, 255};
    }
}

std::string rgbToHex(int r, int g, int b) {
    char buffer[8];
    snprintf(buffer, sizeof(buffer), "#%02X%02X%02X",
        std::clamp(r, 0, 255),
        std::clamp(g, 0, 255),
        std::clamp(b, 0, 255));
    return buffer;
}

std::string getAnimatedColor(const Keyframe& rKeyframe,
                            const Keyframe& gKeyframe,
                            const Keyframe& bKeyframe,
                            const float timeMs, const float fps) {
    const int r = static_cast<int>(getAnimatedValue(rKeyframe, timeMs, fps));
    const int g = static_cast<int>(getAnimatedValue(gKeyframe, timeMs, fps));
    const int b = static_cast<int>(getAnimatedValue(bKeyframe, timeMs, fps));

    return rgbToHex(r, g, b);
}

SubtitleTextStyle applyAnimationParams(const std::vector<AnimationParam>& params,
                                     const std::vector<AnimationParamColor>& colorParams,
                                     const float timeMs, const float fps,
                                     const SubtitleTextStyle& baseStyle) {
    SubtitleTextStyle result = baseStyle;

    // Apply numeric parameters using OpenShot's Keyframe
    for (const auto& param : params) {
        const auto value = getAnimatedValue(param.keyframe, timeMs, fps);

        // Match property names with JavaScript implementation
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

    // Apply color parameters using separate RGB keyframes
    for (const auto& param : colorParams) {
        const std::string value = getAnimatedColor(param.rKeyframe, param.gKeyframe, param.bKeyframe, timeMs, fps);

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

std::vector<WordAnimation> processSegmentAnimation(const std::vector<WordDetail>& wordDetails,
                                                 const SegmentSettings& settings, const float fps) {
    std::vector<WordAnimation> animations;

    const auto& animSettings = settings.animationSettings;
    const auto& defaultStyle = settings.defaultStyle;

    for (const auto& detail : wordDetails) {
        WordAnimation anim;
        anim.word = detail.word;

        // Calculate absolute timing
        const float wordStartMs = detail.startMs;
        const float wordEndMs = detail.endMs;

        // IN ANIMATION - Applied during word's active time
        if (animSettings.inDuration > 0) {
            // Create animations for numeric properties
            for (const auto& [key, targetValue] : animSettings.inStyles) {
                AnimationParam param;
                param.name = key;

                // Get base value from default style
                double baseValue = getBaseValue(key, defaultStyle);

                // Create keyframes for transition during word display
                const int64_t wordStartFrame = msToFrame(wordStartMs, fps);
                const int64_t inEndFrame = msToFrame(wordStartMs + animSettings.inDuration, fps);

                // Create keyframe and add points
                param.keyframe.AddPoint(wordStartFrame, baseValue, animSettings.inInterpolation);
                param.keyframe.AddPoint(inEndFrame, targetValue, animSettings.inInterpolation);

                anim.params.push_back(param);
            }

            // Create animations for color properties
            for (const auto& [key, targetColor] : animSettings.inStylesColor) {
                AnimationParamColor param;
                param.name = key;

                // Get base color from default style
                const std::string baseColor = getBaseColor(key, defaultStyle);

                // Convert both colors to RGB
                auto [baseR, baseG, baseB] = hexToRgb(baseColor);
                auto [targetR, targetG, targetB] = hexToRgb(targetColor);

                // Create separate keyframes for R, G, B components
                const int64_t wordStartFrame = msToFrame(wordStartMs, fps);
                const int64_t inEndFrame = msToFrame(wordStartMs + animSettings.inDuration, fps);

                // Add points to RGB keyframes
                param.rKeyframe.AddPoint(wordStartFrame, baseR, animSettings.inInterpolation);
                param.rKeyframe.AddPoint(inEndFrame, targetR, animSettings.inInterpolation);

                param.gKeyframe.AddPoint(wordStartFrame, baseG, animSettings.inInterpolation);
                param.gKeyframe.AddPoint(inEndFrame, targetG, animSettings.inInterpolation);

                param.bKeyframe.AddPoint(wordStartFrame, baseB, animSettings.inInterpolation);
                param.bKeyframe.AddPoint(inEndFrame, targetB, animSettings.inInterpolation);

                anim.colorParams.push_back(param);
            }
        }

        // OUT ANIMATION - Applied after highlight phase
        if (animSettings.outDuration > 0) {
            const float outStartMs = wordEndMs - animSettings.outDuration;

            // Create animations for numeric properties
            for (const auto& [key, targetValue] : animSettings.outStyles) {
                // Find existing param or create new one
                auto paramIt = std::find_if(anim.params.begin(), anim.params.end(),
                    [&key](const AnimationParam& p) { return p.name == key; });

                if (paramIt != anim.params.end()) {
                    // Add out keyframes to existing keyframe
                    const int64_t outStartFrame = msToFrame(outStartMs, fps);
                    const int64_t outEndFrame = msToFrame(wordEndMs, fps);

                    // Get current end value from existing keyframe
                    double currentValue = getAnimatedValue(paramIt->keyframe, outStartMs, fps);

                    // Add new points to existing keyframe
                    paramIt->keyframe.AddPoint(outStartFrame, currentValue, animSettings.outInterpolation);
                    paramIt->keyframe.AddPoint(outEndFrame, targetValue, animSettings.outInterpolation);
                } else {
                    // Create new param for out animation
                    AnimationParam param;
                    param.name = key;

                    const double baseValue = getBaseValue(key, defaultStyle);
                    const int64_t outStartFrame = msToFrame(outStartMs, fps);
                    const int64_t outEndFrame = msToFrame(wordEndMs, fps);

                    param.keyframe.AddPoint(outStartFrame, baseValue, animSettings.outInterpolation);
                    param.keyframe.AddPoint(outEndFrame, targetValue, animSettings.outInterpolation);

                    anim.params.push_back(param);
                }
            }

            // Handle out color animations similarly
            for (const auto& [key, targetColor] : animSettings.outStylesColor) {
                auto paramIt = std::find_if(anim.colorParams.begin(), anim.colorParams.end(),
                    [&key](const AnimationParamColor& p) { return p.name == key; });

                if (paramIt != anim.colorParams.end()) {
                    // Add out keyframes to existing RGB components
                    const int64_t outStartFrame = msToFrame(outStartMs, fps);
                    const int64_t outEndFrame = msToFrame(wordEndMs, fps);

                    // Get current RGB values at out start time
                    const int currentR = static_cast<int>(getAnimatedValue(paramIt->rKeyframe, outStartMs, fps));
                    const int currentG = static_cast<int>(getAnimatedValue(paramIt->gKeyframe, outStartMs, fps));
                    const int currentB = static_cast<int>(getAnimatedValue(paramIt->bKeyframe, outStartMs, fps));

                    // Get target RGB values
                    auto [targetR, targetG, targetB] = hexToRgb(targetColor);

                    // Add out points to existing keyframes
                    paramIt->rKeyframe.AddPoint(outStartFrame, currentR, animSettings.outInterpolation);
                    paramIt->rKeyframe.AddPoint(outEndFrame, targetR, animSettings.outInterpolation);

                    paramIt->gKeyframe.AddPoint(outStartFrame, currentG, animSettings.outInterpolation);
                    paramIt->gKeyframe.AddPoint(outEndFrame, targetG, animSettings.outInterpolation);

                    paramIt->bKeyframe.AddPoint(outStartFrame, currentB, animSettings.outInterpolation);
                    paramIt->bKeyframe.AddPoint(outEndFrame, targetB, animSettings.outInterpolation);
                } else {
                    // Create new color param for out animation
                    AnimationParamColor param;
                    param.name = key;

                    const std::string baseColor = getBaseColor(key, defaultStyle);
                    auto [baseR, baseG, baseB] = hexToRgb(baseColor);
                    auto [targetR, targetG, targetB] = hexToRgb(targetColor);

                    const int64_t outStartFrame = msToFrame(outStartMs, fps);
                    const int64_t outEndFrame = msToFrame(wordEndMs, fps);

                    param.rKeyframe.AddPoint(outStartFrame, baseR, animSettings.outInterpolation);
                    param.rKeyframe.AddPoint(outEndFrame, targetR, animSettings.outInterpolation);

                    param.gKeyframe.AddPoint(outStartFrame, baseG, animSettings.outInterpolation);
                    param.gKeyframe.AddPoint(outEndFrame, targetG, animSettings.outInterpolation);

                    param.bKeyframe.AddPoint(outStartFrame, baseB, animSettings.outInterpolation);
                    param.bKeyframe.AddPoint(outEndFrame, targetB, animSettings.outInterpolation);

                    anim.colorParams.push_back(param);
                }
            }
        }

        animations.push_back(anim);
    }

    return animations;
}

// Helper functions to get base values from default style
double getBaseValue(const std::string& key, const SubtitleTextStyle& style) {
    if (key == "fontSize") return style.fontSize;
    if (key == "opacity") return style.opacity;
    if (key == "letterSpacing") return style.letterSpacing;
    if (key == "lineHeight") return style.lineHeight;
    if (key == "bold") return style.bold;
    if (key == "translateX") return style.translateX.value_or(0);
    if (key == "translateY") return style.translateY.value_or(0);
    if (key == "strokeWidth") return style.strokeWidth.value_or(0);
    if (key == "strokeOpacity") return style.strokeOpacity.value_or(1);
    if (key == "shadowBlur") return style.shadowBlur.value_or(0);
    if (key == "shadowDistance") return style.shadowDistance.value_or(0);
    if (key == "shadowAngle") return style.shadowAngle.value_or(0);
    if (key == "shadowOpacity") return style.shadowOpacity.value_or(1);
    if (key == "backgroundOpacity") return style.backgroundOpacity.value_or(0);
    if (key == "backgroundRadius") return style.backgroundRadius.value_or(0);
    if (key == "backgroundPaddingX") return style.backgroundPaddingX.value_or(0);
    if (key == "backgroundPaddingY") return style.backgroundPaddingY.value_or(0);
    return 0;
}

std::string getBaseColor(const std::string& key, const SubtitleTextStyle& style) {
    if (key == "color") return style.color;
    if (key == "strokeColor") return style.strokeColor.value_or("#000000");
    if (key == "shadowColor") return style.shadowColor.value_or("#000000");
    if (key == "backgroundColor") return style.backgroundColor.value_or("#000000");
    return "#FFFFFF";
}

} // namespace subtitle
} // namespace openshot