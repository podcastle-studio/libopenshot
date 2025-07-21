#pragma once

#include <string>
#include <vector>
#include <map>
#include <optional>
#include <memory>
#include "../KeyFrame.h"

namespace openshot {
namespace subtitle {

enum class TextAlignment { LEFT, CENTER, RIGHT };
enum class TextTransform { NONE, UPPERCASE, LOWERCASE, CAPITALIZE };
enum class TextAppearance { ONE_WORD, PER_TIME };

// Style types
struct AnimatingTextStyle {
    double fontSize      = 64;
    double letterSpacing = 0;
    double lineHeight    = 1.2f;
    int bold            = 400;
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
};

struct SubtitleTextStyle : public AnimatingTextStyle {
    std::string fontFamily = "Arial";
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
    std::string color = "#000000";
};

struct AnimationParam {
    std::string name; // Property name to animate
    Keyframe keyframe; // Using OpenShot's Keyframe class directly
};

struct AnimationParamColor {
    std::string name;
    Keyframe rKeyframe; // Red component keyframe
    Keyframe gKeyframe; // Green component keyframe
    Keyframe bKeyframe; // Blue component keyframe
};

struct WordAnimation {
    std::string word;
    std::vector<AnimationParam> params;
    std::vector<AnimationParamColor> colorParams;
};

struct StyledWord {
    std::string word;
    SubtitleTextStyle style;
};

struct AnimationSettings {
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
        float x = 0.5f;  // Changed to match JSON (0.5 = 50%)
        float y = 0.9f;  // Changed to match JSON (0.9 = 90%)
    } center;

    float maxWidth = 900;  // Changed to match JSON default
};

struct SegmentSettings {
    std::string placeholder;
    SubtitleContainerStyle containerStyle;
    SubtitleTextStyle defaultStyle;
    AnimationSettings animationSettings;
    Transformation transformation;
};

// Word detail - timing in milliseconds
struct WordDetail {
    std::string word;
    float startMs;  // Start time in milliseconds
    float endMs;    // End time in milliseconds
    float confidence = 1.0f;
};

struct SubtitleSegment {
    std::string id;
    std::vector<WordDetail> wordDetails;
    bool attached = true;
    bool visible = true;
    float startTimeMs;  // Start time in milliseconds
    float endTimeMs;    // End time in milliseconds
    std::optional<SegmentSettings> settings;
};

struct SubtitleSettings : public SegmentSettings {
    bool locked = false;
    bool visible = true;
};

struct SubtitlePreset {
    std::string id;
    std::string placeholder;
    SubtitleContainerStyle containerStyle;
    SubtitleTextStyle defaultStyle;
    AnimationSettings animationSettings;
};

struct TextBounds {
    double top;
    double bottom;
};

// Helper function declarations
double getBaseValue(const std::string& key, const SubtitleTextStyle& style);
std::string getBaseColor(const std::string& key, const SubtitleTextStyle& style);

} // namespace subtitle
} // namespace openshot