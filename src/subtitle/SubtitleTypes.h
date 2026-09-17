#pragma once

#include <string>
#include <vector>
#include <map>
#include <optional>
#include <memory>
#include "../KeyFrame.h"

namespace openshot {
namespace subtitle {

/// Which colour convention a renderer's parsed colours follow.
///
/// SkiaRenderer::parseColorString swaps R and B on every colour it parses. That swap is
/// not a byte order and it is not a property of the canvas -- the glow already draws
/// parsed colours onto a kRGBA_8888 surface and reads them back through an N32 pixmap,
/// and Skia converts correctly at every such hop. It is a property of the *output
/// boundary*: every CPU path ends by handing an N32-declared pixmap's bytes to a QImage
/// that declares them Format_RGBA8888, and the swap is what cancels that one
/// reinterpretation.
///
/// A result that never gets reinterpreted -- drawn onto the Timeline's kRGBA_8888 canvas
/// and read back as kRGBA_8888 by Frame::FlattenGpuFrame -- must therefore NOT be
/// swapped, or red and blue come out exchanged. Callers compositing straight onto the
/// timeline canvas pass Logical; everything else keeps the default and behaves exactly
/// as it always has.
enum class ColorConvention {
    QImageBytes,   ///< legacy: the result is reinterpreted once at a QImage
    Logical        ///< no swap: the result stays in Skia's own colour space
};

enum class TextAlignment { LEFT, CENTER, RIGHT };
enum class TextTransform { NONE, UPPERCASE, LOWERCASE, CAPITALIZE };
enum class TextAppearance { ONE_WORD, PER_TIME };
enum class AnimationLevel { WORD, LINE };

struct AnimationParam {
    std::string name;
    Keyframe keyframe;
};

struct SubtitleTextStyle {
    double fontSize      = 64;
    double letterSpacing = 0;
    double lineHeight    = 1.2f;
    int fontWeight       = 400;
    std::string color   = "#FFFFFF";
    double opacity       = 1.0f;

    std::optional<std::string> strokeColor;
    std::optional<double> strokeOpacity;
    std::optional<double> strokeWidth;

    std::optional<std::string> shadowColor;
    std::optional<double> shadowOpacity;
    std::optional<double> shadowBlur;
    std::optional<double> shadowDistance;
    std::optional<double> shadowAngle;

    std::optional<std::string> backgroundColor;
    std::optional<double> backgroundOpacity;
    std::optional<double> backgroundRadius;
    std::optional<double> backgroundPaddingX;
    std::optional<double> backgroundPaddingY;

    std::optional<double> translateX;
    std::optional<double> translateY;

    std::string fontFamily;
    bool italic = false;
    std::optional<bool> bubble;
    TextTransform textTransform = TextTransform::NONE;
};

struct SubtitleContainerStyle {
    TextAppearance appearance = TextAppearance::PER_TIME;
    TextAlignment textAlign = TextAlignment::CENTER;
    double opacity = 0;
    double paddingX = 0;
    double paddingY = 0;
    double radius = 0;
    std::optional<std::string> color;
    bool punctuation = false;
};

struct WordAnimation {
    std::string word;
    std::vector<AnimationParam> params;
};

struct StyledWord {
    std::string word;
    SubtitleTextStyle style;
};

struct AnimationSettings {
    AnimationLevel level = AnimationLevel::WORD;

    InterpolationType inInterpolation = LINEAR;
    float inDuration = 100;  // Duration in milliseconds
    std::map<std::string, double> inStyles;
    std::map<std::string, std::string> inStylesColor;

    InterpolationType outInterpolation = LINEAR;
    float outDuration = 0;   // Duration in milliseconds
    std::map<std::string, double> outStyles;
    std::map<std::string, std::string> outStylesColor;
};

struct Transformation {
    struct Scale {
        float horizontalScale = 1.0f;
        float verticalScale = 1.0f;
    } scale;

    float rotation = 0;

    struct Center {
        float x = 0.5f;  // 0.5 = 50% (center)
        float y = 0.9f;  // 0.9 = 90% (near bottom)
    } center;

    float maxWidth = 900;
};

struct SegmentSettings {
    std::string placeholder;
    SubtitleContainerStyle containerStyle;
    SubtitleTextStyle defaultStyle;
    AnimationSettings animationSettings;
    Transformation transformation;
};

// Word detail - timing in milliseconds relative to segment
struct WordDetail {
    std::string word;
    float startMs;  // Start time in milliseconds (relative to segment)
    float endMs;    // End time in milliseconds (relative to segment)
    float confidence = 1.0f;
};

struct SubtitleSegment {
    std::string id;
    std::vector<WordDetail> wordDetails;
    bool attached = true;
    bool visible = true;
    float startTimeMs;  // Start time in milliseconds (absolute)
    float endTimeMs;    // End time in milliseconds (absolute)
    std::optional<SegmentSettings> settings;
};

struct TextBounds {
    double top;
    double bottom;
};

} // namespace subtitle
} // namespace openshot