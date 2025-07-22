#include "Helpers.h"
#include "SubtitleTypes.h"
#include "../KeyFrame.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace {

std::string rgbToHex(const int r, const int g, const int b) {
    char buffer[8];
    snprintf(buffer, sizeof(buffer), "#%02X%02X%02X",
        std::clamp(r, 0, 255),
        std::clamp(g, 0, 255),
        std::clamp(b, 0, 255));
    return buffer;
}

std::tuple<int, int, int> hexToRgb(const std::string& hex) {
    if (hex.empty() || hex[0] != '#' || hex.length() < 7) {
        return {255, 255, 255};
    }

    try {
        int r = std::stoi(hex.substr(1, 2), nullptr, 16);
        int g = std::stoi(hex.substr(3, 2), nullptr, 16);
        int b = std::stoi(hex.substr(5, 2), nullptr, 16);
        return {r, g, b};
    } catch (...) {
        return {255, 255, 255};
    }
}

int64_t msToFrame(const float ms, const float fps) {
    // OpenShot uses 1-based frame indexing
    return static_cast<int64_t>((ms / 1000.0f) * fps) + 1;
}

// Get animated value using OpenShot's Keyframe at millisecond time
double getAnimatedValue(const openshot::Keyframe& keyframe, const float timeMs, const float fps) {
    // Convert milliseconds to frame number (OpenShot uses 1-based frame indexing)
    const int64_t frame = msToFrame(timeMs, fps);
    return keyframe.GetValue(frame);
}

}

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

SubtitleTextStyle applyAnimationParams(const std::vector<AnimationParam>& params,
    const float timeMs, const float fps, const SubtitleTextStyle& baseStyle) {
    SubtitleTextStyle result = baseStyle;

    // Store RGB components for color properties
    std::map<std::string, std::array<int, 3>> colorComponents;

    // Apply all animation parameters
    for (const auto& param : params) {
        double value = getAnimatedValue(param.keyframe, timeMs, fps);

        // Check if this is a color component (e.g., "color.r", "strokeColor.g", etc.)
        size_t dotPos = param.name.find('.');
        if (dotPos != std::string::npos) {
            std::string colorName = param.name.substr(0, dotPos);
            char component = param.name[dotPos + 1];

            int componentValue = static_cast<int>(std::clamp(value, 0.0, 255.0));

            if (component == 'r') {
                colorComponents[colorName][0] = componentValue;
            } else if (component == 'g') {
                colorComponents[colorName][1] = componentValue;
            } else if (component == 'b') {
                colorComponents[colorName][2] = componentValue;
            }
        } else {
            // Regular numeric properties
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
    }

    // Apply assembled color values
    for (const auto& [colorName, rgb] : colorComponents) {
        std::string hexColor = rgbToHex(rgb[0], rgb[1], rgb[2]);

        if (colorName == "color") result.color = hexColor;
        else if (colorName == "strokeColor") result.strokeColor = hexColor;
        else if (colorName == "shadowColor") result.shadowColor = hexColor;
        else if (colorName == "backgroundColor") result.backgroundColor = hexColor;
    }

    return result;
}

float frameToMs(const int64_t frame, const float fps) {
    // OpenShot uses 1-based frame indexing
    return ((frame - 1) * 1000.0f) / fps;
}

std::vector<WordAnimation> processSegmentAnimation(const std::vector<WordDetail>& wordDetails, const SegmentSettings& settings, float fps) {
    std::vector<WordAnimation> animations;

    const auto& animSettings = settings.animationSettings;
    const auto& defaultStyle = settings.defaultStyle;

    for (const auto& detail : wordDetails) {
        WordAnimation anim;
        anim.word = detail.word;

        // Word timing is relative to segment start
        float wordStartMs = detail.startMs;
        float wordEndMs = detail.endMs;

        // IN ANIMATION - Applied during word's active time
        if (animSettings.inDuration > 0) {
            // Create animations for numeric properties
            for (const auto& [key, targetValue] : animSettings.inStyles) {
                AnimationParam param;
                param.name = key;

                // Get base value from default style
                double baseValue = getBaseValue(key, defaultStyle);

                // Create keyframes for transition during word display
                // Animation happens from wordStart to wordStart + inDuration
                int64_t wordStartFrame = msToFrame(wordStartMs, fps);
                int64_t inEndFrame = msToFrame(wordStartMs + animSettings.inDuration, fps);

                // Add points to keyframe
                param.keyframe.AddPoint(wordStartFrame, baseValue, animSettings.inInterpolation);
                param.keyframe.AddPoint(inEndFrame, targetValue, animSettings.inInterpolation);

                // If there's an out animation for this property, add those keyframes
                if (animSettings.outDuration > 0 && animSettings.outStyles.count(key) > 0) {
                    float outStartMs = wordEndMs - animSettings.outDuration;
                    int64_t outStartFrame = msToFrame(outStartMs, fps);
                    int64_t outEndFrame = msToFrame(wordEndMs, fps);

                    // Maintain target value until out animation starts
                    if (outStartFrame > inEndFrame) {
                        param.keyframe.AddPoint(outStartFrame, targetValue, animSettings.inInterpolation);
                    }

                    // Add out animation
                    param.keyframe.AddPoint(outEndFrame, animSettings.outStyles.at(key), animSettings.outInterpolation);
                } else {
                    // No out animation - return to base value after inDuration
                    // This creates a spike effect: base -> target -> base
                    int64_t wordEndFrame = msToFrame(wordEndMs, fps);
                    if (wordEndFrame > inEndFrame) {
                        // Add a point to return to base value
                        param.keyframe.AddPoint(inEndFrame + 1, baseValue, animSettings.inInterpolation);
                        param.keyframe.AddPoint(wordEndFrame, baseValue, animSettings.inInterpolation);
                    }
                }

                anim.params.push_back(param);
            }

            // Create animations for color properties
            for (const auto& [key, targetColor] : animSettings.inStylesColor) {
                // Get base color from default style
                std::string baseColor = getBaseColor(key, defaultStyle);
                auto [baseR, baseG, baseB] = hexToRgb(baseColor);
                auto [targetR, targetG, targetB] = hexToRgb(targetColor);

                // Create separate params for R, G, B components
                AnimationParam paramR, paramG, paramB;
                paramR.name = key + ".r";
                paramG.name = key + ".g";
                paramB.name = key + ".b";

                // Create keyframes for color components
                int64_t wordStartFrame = msToFrame(wordStartMs, fps);
                int64_t inEndFrame = msToFrame(wordStartMs + animSettings.inDuration, fps);

                // Add points to RGB keyframes
                paramR.keyframe.AddPoint(wordStartFrame, baseR, animSettings.inInterpolation);
                paramR.keyframe.AddPoint(inEndFrame, targetR, animSettings.inInterpolation);

                paramG.keyframe.AddPoint(wordStartFrame, baseG, animSettings.inInterpolation);
                paramG.keyframe.AddPoint(inEndFrame, targetG, animSettings.inInterpolation);

                paramB.keyframe.AddPoint(wordStartFrame, baseB, animSettings.inInterpolation);
                paramB.keyframe.AddPoint(inEndFrame, targetB, animSettings.inInterpolation);

                // Handle out animation for colors
                if (animSettings.outDuration > 0 && animSettings.outStylesColor.count(key) > 0) {
                    float outStartMs = wordEndMs - animSettings.outDuration;
                    int64_t outStartFrame = msToFrame(outStartMs, fps);
                    int64_t outEndFrame = msToFrame(wordEndMs, fps);

                    auto [outR, outG, outB] = hexToRgb(animSettings.outStylesColor.at(key));

                    // Maintain target color until out animation starts
                    if (outStartFrame > inEndFrame) {
                        paramR.keyframe.AddPoint(outStartFrame, targetR, animSettings.inInterpolation);
                        paramG.keyframe.AddPoint(outStartFrame, targetG, animSettings.inInterpolation);
                        paramB.keyframe.AddPoint(outStartFrame, targetB, animSettings.inInterpolation);
                    }

                    // Add out animation points
                    paramR.keyframe.AddPoint(outEndFrame, outR, animSettings.outInterpolation);
                    paramG.keyframe.AddPoint(outEndFrame, outG, animSettings.outInterpolation);
                    paramB.keyframe.AddPoint(outEndFrame, outB, animSettings.outInterpolation);
                } else {
                    // No out animation - return to base color after inDuration
                    int64_t wordEndFrame = msToFrame(wordEndMs, fps);
                    if (wordEndFrame > inEndFrame) {
                        // Add points to return to base color
                        paramR.keyframe.AddPoint(inEndFrame + 1, baseR, animSettings.inInterpolation);
                        paramR.keyframe.AddPoint(wordEndFrame, baseR, animSettings.inInterpolation);

                        paramG.keyframe.AddPoint(inEndFrame + 1, baseG, animSettings.inInterpolation);
                        paramG.keyframe.AddPoint(wordEndFrame, baseG, animSettings.inInterpolation);

                        paramB.keyframe.AddPoint(inEndFrame + 1, baseB, animSettings.inInterpolation);
                        paramB.keyframe.AddPoint(wordEndFrame, baseB, animSettings.inInterpolation);
                    }
                }

                anim.params.push_back(paramR);
                anim.params.push_back(paramG);
                anim.params.push_back(paramB);
            }
        }

        // OUT ANIMATION - for properties not handled in IN animation
        if (animSettings.outDuration > 0) {
            float outStartMs = wordEndMs - animSettings.outDuration;

            // Handle numeric properties that only have out animation
            for (const auto& [key, targetValue] : animSettings.outStyles) {
                // Check if this property was already animated in the IN animation
                bool found = false;
                for (const auto& param : anim.params) {
                    if (param.name == key) {
                        found = true;
                        break;
                    }
                }

                if (!found) {
                    AnimationParam param;
                    param.name = key;

                    double baseValue = getBaseValue(key, defaultStyle);
                    int64_t wordStartFrame = msToFrame(wordStartMs, fps);
                    int64_t outStartFrame = msToFrame(outStartMs, fps);
                    int64_t outEndFrame = msToFrame(wordEndMs, fps);

                    // Stay at base value until out animation starts
                    param.keyframe.AddPoint(wordStartFrame, baseValue, animSettings.outInterpolation);
                    if (outStartFrame > wordStartFrame) {
                        param.keyframe.AddPoint(outStartFrame, baseValue, animSettings.outInterpolation);
                    }
                    param.keyframe.AddPoint(outEndFrame, targetValue, animSettings.outInterpolation);

                    anim.params.push_back(param);
                }
            }

            // Handle color properties that only have out animation
            for (const auto& [key, targetColor] : animSettings.outStylesColor) {
                // Check if this color was already animated (check for .r component)
                bool found = false;
                for (const auto& param : anim.params) {
                    if (param.name == key + ".r") {
                        found = true;
                        break;
                    }
                }

                if (!found) {
                    std::string baseColor = getBaseColor(key, defaultStyle);
                    auto [baseR, baseG, baseB] = hexToRgb(baseColor);
                    auto [targetR, targetG, targetB] = hexToRgb(targetColor);

                    AnimationParam paramR, paramG, paramB;
                    paramR.name = key + ".r";
                    paramG.name = key + ".g";
                    paramB.name = key + ".b";

                    int64_t wordStartFrame = msToFrame(wordStartMs, fps);
                    int64_t outStartFrame = msToFrame(outStartMs, fps);
                    int64_t outEndFrame = msToFrame(wordEndMs, fps);

                    // Stay at base color until out animation starts
                    paramR.keyframe.AddPoint(wordStartFrame, baseR, animSettings.outInterpolation);
                    paramG.keyframe.AddPoint(wordStartFrame, baseG, animSettings.outInterpolation);
                    paramB.keyframe.AddPoint(wordStartFrame, baseB, animSettings.outInterpolation);

                    if (outStartFrame > wordStartFrame) {
                        paramR.keyframe.AddPoint(outStartFrame, baseR, animSettings.outInterpolation);
                        paramG.keyframe.AddPoint(outStartFrame, baseG, animSettings.outInterpolation);
                        paramB.keyframe.AddPoint(outStartFrame, baseB, animSettings.outInterpolation);
                    }

                    paramR.keyframe.AddPoint(outEndFrame, targetR, animSettings.outInterpolation);
                    paramG.keyframe.AddPoint(outEndFrame, targetG, animSettings.outInterpolation);
                    paramB.keyframe.AddPoint(outEndFrame, targetB, animSettings.outInterpolation);

                    anim.params.push_back(paramR);
                    anim.params.push_back(paramG);
                    anim.params.push_back(paramB);
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