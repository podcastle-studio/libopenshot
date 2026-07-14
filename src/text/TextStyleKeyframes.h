#pragma once

// Per-frame keyframe overlay for TEXT-clip STYLE and TILT (glow, blur, curve, colors, tiltX/Y).
//
// Design: TextClipReader stays value-based (TextClipStyle / TextTransformation are plain scalars);
// this overlay carries the time-varying curves and is SAMPLED per frame in GetFrame to produce a
// resolved style/transformation that the existing render pipeline consumes unchanged. Mirrors how
// the reader is already time-aware for text ANIMATIONS. See tmp/BACKEND_TEXT_STYLE_KEYFRAMES_DESIGN.md.
//
// Numeric channels use openshot::Keyframe (frame-indexed) so interpolation matches every other
// keyframed clip property the service drives (position/scale/rotation/opacity). Colour channels are
// gradient-aware and sampled by sampleColorChannel() (§ BACKEND_KEYFRAME_SPEC.md §4).
//
// This header pulls in openshot core (KeyFrame.h), so it is intentionally NOT part of the leaf
// TextClipTypes.h (which text-metrics vendors verbatim); it is included only by TextClipReader.

#include "TextClipTypes.h"
#include "../KeyFrame.h"          // openshot::Keyframe, openshot::InterpolationType

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace openshot {
namespace text {

// One keyframe on a colour channel. `value` is the raw CSS colour — a solid ("#rgb" / "#rrggbb[aa]"
// / "rgb[a](...)") OR a "linear-gradient(...)" string; the sampler parses it (via parseTextGradient),
// so callers just pass the string straight from the payload. `interp` governs the segment ENDING at
// this point (matching openshot::Keyframe, which interpolates a segment using its right point's
// method). `bezier` holds the cubic-bezier easing handles (x1,y1,x2,y2) when interp == BEZIER.
struct ColorKeyPoint {
    double timeSec = 0.0;
    std::string value;
    openshot::InterpolationType interp = openshot::LINEAR;
    std::array<double, 4> bezier{{0.0, 0.0, 1.0, 1.0}};
};

// A colour channel over time. Empty = not keyframed (the static style colour is used).
struct ColorKeyframeChannel {
    std::vector<ColorKeyPoint> points;
    bool empty() const { return points.empty(); }
};

// The keyframe overlay attached to a TextClipReader. Any absent numeric channel / empty colour
// channel means that property is NOT keyframed and its static style value is used.
struct TextStyleKeyframes {
    // Transformation
    std::optional<openshot::Keyframe> tiltX, tiltY;                 // degrees, -90..90
    // Glow (numeric)
    std::optional<openshot::Keyframe> glowIntensityRatio;           // 0..1
    std::optional<openshot::Keyframe> glowRangeRatio;               // 0..1
    std::optional<openshot::Keyframe> glowDirectionX, glowDirectionY; // -50..50
    // Blur
    std::optional<openshot::Keyframe> blurRatio;                    // 0..1.5
    // Curve
    std::optional<openshot::Keyframe> curveAngle;                   // -360..360
    // Stroke (numeric)
    std::optional<openshot::Keyframe> strokeWidthRatio;
    // Shadow (numeric)
    std::optional<openshot::Keyframe> shadowBlurRatio, shadowDistanceRatio, shadowAngle;
    // Background (numeric)
    std::optional<openshot::Keyframe> backgroundPaddingXRatio, backgroundPaddingYRatio, backgroundRadiusRatio;
    // Colours (gradient-aware)
    ColorKeyframeChannel color, strokeColor, shadowColor, backgroundColor, glowColor;

    bool empty() const {
        return !tiltX && !tiltY && !glowIntensityRatio && !glowRangeRatio &&
               !glowDirectionX && !glowDirectionY && !blurRatio && !curveAngle &&
               !strokeWidthRatio && !shadowBlurRatio && !shadowDistanceRatio && !shadowAngle &&
               !backgroundPaddingXRatio && !backgroundPaddingYRatio && !backgroundRadiusRatio &&
               color.empty() && strokeColor.empty() && shadowColor.empty() &&
               backgroundColor.empty() && glowColor.empty();
    }

    // True for the channels that can change the output frame buffer size: glow reach/offset, blur,
    // tilt, curve, stroke width, shadow blur/distance, and background padding. The stroke / shadow /
    // background / glow COLOUR channels count too, because a colour keyframed from absent→present
    // switches its effect (and thus its margin) on. Fill colour / glow-intensity / shadow-angle /
    // background-radius never change the frame size.
    bool affectsFrameSize() const {
        return tiltX || tiltY || glowRangeRatio || glowDirectionX || glowDirectionY ||
               blurRatio || curveAngle || strokeWidthRatio || shadowBlurRatio || shadowDistanceRatio ||
               backgroundPaddingXRatio || backgroundPaddingYRatio ||
               !strokeColor.empty() || !shadowColor.empty() ||
               !backgroundColor.empty() || !glowColor.empty();
    }
};

// Sample a colour channel at time `t` (seconds) and return a CSS colour string — a solid
// "rgba(r, g, b, a)" or a "linear-gradient(<deg>deg, rgba(...) p%, ...)" — ready to feed straight
// into the existing convertTextStyleToPaintStyle path. Empty channel returns `fallback` unchanged.
std::string sampleColorChannel(const ColorKeyframeChannel& channel, double t,
                               const std::string& fallback);

} // namespace text
} // namespace openshot
