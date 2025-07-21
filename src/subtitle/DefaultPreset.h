#pragma once

#pragma once

#include "SubtitleTypes.h"
#include "../Point.h"

namespace openshot {
namespace subtitle {

inline SubtitlePreset getDefaultPreset() {
    SubtitlePreset preset;

    preset.id = "preset-0";
    preset.placeholder = "Perfect for highlighting viral podcast moments";

    // Container style
    preset.containerStyle.appearance = TextAppearance::PER_TIME;
    preset.containerStyle.textAlign = TextAlignment::CENTER;
    preset.containerStyle.opacity = 0;
    preset.containerStyle.paddingX = 0;
    preset.containerStyle.paddingY = 0;
    preset.containerStyle.radius = 0;
    preset.containerStyle.color = "#000000";

    // Default text style
    preset.defaultStyle.fontFamily = "Poppins Bold";
    preset.defaultStyle.fontSize = 64;
    preset.defaultStyle.letterSpacing = 0;
    preset.defaultStyle.lineHeight = 1.2f;
    preset.defaultStyle.textTransform = TextTransform::UPPERCASE;
    preset.defaultStyle.bold = 400;
    preset.defaultStyle.italic = false;
    preset.defaultStyle.color = "#ece8e8";
    preset.defaultStyle.opacity = 1;
    preset.defaultStyle.translateX = 0;
    preset.defaultStyle.translateY = 0;
    preset.defaultStyle.backgroundColor = "#f8f1f1";
    preset.defaultStyle.backgroundOpacity = 0;
    preset.defaultStyle.backgroundRadius = 4;
    preset.defaultStyle.backgroundPaddingX = 1;
    preset.defaultStyle.backgroundPaddingY = 1;
    preset.defaultStyle.bubble = true;
    preset.defaultStyle.strokeColor = "#000000";
    preset.defaultStyle.strokeOpacity = 1;
    preset.defaultStyle.strokeWidth = 10;

    // Animation settings
    preset.animationSettings.inInterpolation = LINEAR;
    preset.animationSettings.inSpeed = 100;

    // In styles
    preset.animationSettings.inStyles["backgroundOpacity"] = 1;
    preset.animationSettings.inStyles["backgroundPaddingX"] = 10;
    preset.animationSettings.inStyles["backgroundPaddingY"] = 10;
    preset.animationSettings.inStyles["backgroundRadius"] = 13;

    preset.animationSettings.inStylesColor["backgroundColor"] = "#beee50";

    preset.animationSettings.outInterpolation = LINEAR;
    preset.animationSettings.outSpeed = 0;

    return preset;
}

// Example of creating a subtitle segment
inline SubtitleSegment createExampleSegment() {
    SubtitleSegment segment;

    segment.id = "segment-1";
    segment.attached = true;
    segment.visible = true;
    segment.startTimeMs = 0;
    segment.endTimeMs = 3000; // 3 seconds

    // Add some word details (times in milliseconds)
    segment.wordDetails = {
        {"HELLO", 0, 500, 1.0f},
        {"WORLD", 600, 1100, 1.0f},
        {"ANIMATION", 1200, 2000, 1.0f}
    };

    return segment;
}

// Helper to create segment settings from preset
inline SegmentSettings createSegmentSettings(const SubtitlePreset& preset) {
    SegmentSettings settings;

    settings.placeholder = preset.placeholder;
    settings.containerStyle = preset.containerStyle;
    settings.defaultStyle = preset.defaultStyle;
    settings.animationSettings = preset.animationSettings;

    // Default transformation
    settings.transformation.scale.horizontalScale = 1.0f;
    settings.transformation.scale.verticalScale = 1.0f;
    settings.transformation.rotation = 0;
    settings.transformation.center.x = 960;  // Center of 1920
    settings.transformation.center.y = 540;  // Center of 1080
    settings.transformation.maxWidth = 1920;

    return settings;
}

} // namespace subtitle
} // namespace openshot