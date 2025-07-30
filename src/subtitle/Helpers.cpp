#include "Helpers.h"
#include "SubtitleTypes.h"
#include "../KeyFrame.h"

#include <algorithm>
#include <cctype>
#include <set>
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

double getBaseValue(const std::string& key, const openshot::subtitle::SubtitleTextStyle& style) {
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

std::string getBaseColor(const std::string& key, const openshot::subtitle::SubtitleTextStyle& style) {
    if (key == "color") return style.color;
    if (key == "strokeColor") return style.strokeColor.value_or("#000000");
    if (key == "shadowColor") return style.shadowColor.value_or("#000000");
    if (key == "backgroundColor") return style.backgroundColor.value_or("#000000");
    return "#FFFFFF";
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

std::vector<WordAnimation> processSegmentAnimation(
        const std::vector<WordDetail>& wordDetails,
        const SegmentSettings&         settings,
        float                          fps)
{
    std::vector<WordAnimation> animations;
    if (wordDetails.empty())
        return animations;

    const auto& animSet      = settings.animationSettings;
    const auto& defaultStyle = settings.defaultStyle;

    // ──────────────────────────────────────────────────────────────
    // 1.  Work on *copies* of the style maps – we will mutate them.
    // ──────────────────────────────────────────────────────────────
    auto inNumStyles   = animSet.inStyles;        // numeric (opacity, size, …)
    auto outNumStyles  = animSet.outStyles;
    auto inColStyles   = animSet.inStylesColor;   // colours  (#RRGGBB)
    auto outColStyles  = animSet.outStylesColor;

    // Animated‑key sets are frozen **before** we touch outNum/ColStyles.
    std::set<std::string> animatedNumKeys;
    for (auto& [k, _] : inNumStyles)  animatedNumKeys.insert(k);
    for (auto& [k, _] : outNumStyles) animatedNumKeys.insert(k);

    std::set<std::string> animatedColKeys;
    for (auto& [k, _] : inColStyles)  animatedColKeys.insert(k);
    for (auto& [k, _] : outColStyles) animatedColKeys.insert(k);

    // ──────────────────────────────────────────────────────────────
    // 2.  ONE_WORD appearance – force opacities to 0 in *out* maps,
    //     but DON'T add them to animatedNumKeys
    // ──────────────────────────────────────────────────────────────
    if (settings.containerStyle.appearance == TextAppearance::ONE_WORD) {
        outNumStyles["opacity"]            = 0.0;
        outNumStyles["backgroundOpacity"]  = 0.0;
        outNumStyles["shadowOpacity"]      = 0.0;
        outNumStyles["strokeOpacity"]      = 0.0;
    }

    // ──────────────────────────────────────────────────────────────
    float segmentStart = wordDetails.front().startMs;

    for (const WordDetail& wd : wordDetails) {
        WordAnimation anim;
        anim.word = wd.word;

        float relStart = wd.startMs - segmentStart;
        float relEnd   = wd.endMs   - segmentStart;
        float duration = relEnd - relStart;

        float inDur  = animSet.inDuration;
        float outDur = animSet.outDuration;
        if (duration < inDur + outDur) {
            inDur = outDur = duration * 0.5f;
        }

        // ----------------------------------------------------------
        // Numeric properties (opacity, fontSize, …)
        // ----------------------------------------------------------
        for (const std::string& key : animatedNumKeys) {

            double initial  = getBaseValue(key, defaultStyle);
            double animated = inNumStyles.count(key)  ? inNumStyles[key]  : initial;
            double final    = outNumStyles.count(key) ? outNumStyles[key] : initial;

            int64_t f0        = msToFrame(0, fps);
            int64_t fStart    = msToFrame(relStart, fps);
            int64_t fInEnd    = msToFrame(relStart + inDur, fps);
            int64_t fOutStart = msToFrame(relEnd   - outDur, fps);
            int64_t fEnd      = msToFrame(relEnd, fps);

            AnimationParam p;
            p.name = key;

            // 0) initial plateau until the word begins
            p.keyframe.AddPoint(f0,     initial, CONSTANT);
            p.keyframe.AddPoint(fStart, initial, CONSTANT);

            // 1) in‑animation
            p.keyframe.AddPoint(fInEnd, animated, animSet.inInterpolation);

            if (outDur > 0) {
                // 2a) constant plateau until out‑animation begins
                if (fOutStart > fInEnd)
                    p.keyframe.AddPoint(fOutStart, animated, CONSTANT);

                // 3) out‑animation to final value
                p.keyframe.AddPoint(fEnd, final, animSet.outInterpolation);
            }
            else {
                // 2b) keep animated value until the frame just before the end…
                if (fEnd - 1 > fInEnd)
                    p.keyframe.AddPoint(fEnd - 1, animated, CONSTANT);

                // 3) …then snap to final value on the very last frame
                p.keyframe.AddPoint(fEnd, final, CONSTANT);
            }

            anim.params.emplace_back(std::move(p));
        }

        // ----------------------------------------------------------
        // Colour properties (split into .r/.g/.b channels)
        // ----------------------------------------------------------
        for (const std::string& key : animatedColKeys) {

            std::string initHex  = getBaseColor(key, defaultStyle);
            std::string animHex  = inColStyles.count(key)  ? inColStyles[key]  : initHex;
            std::string finalHex = outColStyles.count(key) ? outColStyles[key] : initHex;

            auto [ir, ig, ib] = hexToRgb(initHex);
            auto [ar, ag, ab] = hexToRgb(animHex);
            auto [fr, fg, fb] = hexToRgb(finalHex);

            int64_t f0        = msToFrame(0, fps);
            int64_t fStart    = msToFrame(relStart, fps);
            int64_t fInEnd    = msToFrame(relStart + inDur, fps);
            int64_t fOutStart = msToFrame(relEnd   - outDur, fps);
            int64_t fEnd      = msToFrame(relEnd, fps);

            auto addRGB = [&](const std::string& suffix,
                              const double i, const double a, const double f)
            {
                AnimationParam p;
                p.name = key + suffix;

                p.keyframe.AddPoint(f0,     i, CONSTANT);
                p.keyframe.AddPoint(fStart, i, CONSTANT);
                p.keyframe.AddPoint(fInEnd, a, animSet.inInterpolation);

                if (outDur > 0) {
                    if (fOutStart > fInEnd)
                        p.keyframe.AddPoint(fOutStart, a, CONSTANT);
                    p.keyframe.AddPoint(fEnd, f, animSet.outInterpolation);
                }
                else {
                    if (fEnd - 1 > fInEnd)
                        p.keyframe.AddPoint(fEnd - 1, a, CONSTANT);
                    p.keyframe.AddPoint(fEnd, f, CONSTANT);
                }
                anim.params.emplace_back(std::move(p));
            };

            addRGB(".r", ir, ar, fr);
            addRGB(".g", ig, ag, fg);
            addRGB(".b", ib, ab, fb);
        }

        animations.emplace_back(std::move(anim));
    }

    return animations;
}

} // namespace subtitle
} // namespace openshot