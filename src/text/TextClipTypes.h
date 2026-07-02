#pragma once

#include <optional>
#include <string>
#include <vector>

namespace openshot {
namespace text {

// Base coefficient: fontSize = projectWidth * SIZE_BASE_COEFFICIENT * transformation.size
constexpr double SIZE_BASE_COEFFICIENT = 1.0 / 240.0;

// Reference transformation.size used to compute line wrapping. Wrapping decisions
// are made at this size and then scaled up to the actual transformation.size so
// they stop depending on Skia's per-Font-instance glyph-advance quantization.
// See BACKEND_PATCH_LINE_CALCULATION.md. Must agree with the frontend value.
constexpr double LAYOUT_REFERENCE_SIZE = 10.0;

// Long side (px) of the Full-HD layout reference canvas. Wrap/line-break measurement runs on a
// reference canvas at the project's aspect ratio whose LONGER side is pinned to this value, so the
// measuring font size is resolution-independent (otherwise it would be projectWidth/240*10 —
// 80px@1080p vs 160px@4K — and Skia's non-linear glyph metrics would reflow the text, e.g. 2 lines
// at 1080p but 3 at 4K). See layoutReferenceProjectWidth().
constexpr double LAYOUT_REFERENCE_LONG_SIDE = 1920.0;

// Reference project WIDTH used ONLY for the wrap/line-break measurement (resolution-independent).
// The reference canvas keeps the project's aspect ratio with its longer side = LAYOUT_REFERENCE_LONG_SIDE,
// so the width is that scaled to the project's orientation:
//   refWidth = round( projectWidth * LONG_SIDE / max(projectWidth, projectHeight) )
// 16:9 -> 1920, 9:16 -> 1080, 1:1 -> 1920, 3:4 portrait -> 1440, 4:3 landscape -> 1920.
// projectHeight <= 0 (unknown) falls back to treating the project as landscape (refWidth = LONG_SIDE).
// The sizeScale in layoutTextAtReferenceSize divides this reference width back out to the real
// render size, so geometry stays correct while the wrap decision is identical at every resolution.
inline double layoutReferenceProjectWidth(int projectWidth, int projectHeight) {
    if (projectWidth <= 0) return LAYOUT_REFERENCE_LONG_SIDE;
    const int longSide = projectWidth >= projectHeight ? projectWidth : projectHeight;
    if (longSide <= 0) return LAYOUT_REFERENCE_LONG_SIDE;
    const double w = static_cast<double>(projectWidth) * LAYOUT_REFERENCE_LONG_SIDE
                     / static_cast<double>(longSide);
    return static_cast<double>(static_cast<long>(w + 0.5));   // round to nearest px
}

enum class TextAlignment { LEFT, CENTER, RIGHT };
enum class TextTransform { NONE, UPPERCASE, LOWERCASE, CAPITALIZE };

// Default CSS linear-gradient angle when none is specified in the string (0 = bottom→top,
// 90 = left→right). Matches the frontend DEFAULT_GRADIENT_ANGLE.
constexpr double DEFAULT_GRADIENT_ANGLE = 90.0;

// Static 3D tilt maps onto the animation transform's rotateX/rotateY/perspective. This fixed
// perspective (× fontSize) makes the same tilt read consistently at every font size. Mirrors
// the frontend STATIC_ROTATION_PERSPECTIVE.
constexpr double STATIC_ROTATION_PERSPECTIVE = 16.0;

// A parsed CSS linear-gradient: an angle (degrees) plus colour stops. Attached to a paint style
// (fill) or stroke style when the corresponding CSS colour is a linear-gradient, else absent.
struct TextClipGradient {
    struct Stop {
        std::string color;      // raw CSS colour string (consumed by parseColorString)
        double position = 0.0;  // normalised 0..1
    };
    double angle = DEFAULT_GRADIENT_ANGLE;  // 0 = bottom→top, 90 = left→right
    std::vector<Stop> stops;
};

struct TextClipStyle {
    std::string fontFamily;                          // font family name OR explicit path
    bool italic = false;
    TextAlignment textAlign = TextAlignment::CENTER;
    TextTransform textTransform = TextTransform::NONE;
    std::string color = "#FFFFFF";
    double lineHeight = 1.2;                          // multiplier
    double letterSpacing = 0.0;                       // em ratio
    int fontWeight = 400;

    // Stroke
    std::optional<std::string> strokeColor;
    double strokeWidthRatio = 0.0;

    // Shadow
    std::optional<std::string> shadowColor;
    double shadowBlurRatio = 0.0;
    double shadowDistanceRatio = 0.0;
    double shadowAngle = 0.0;

    // Background
    std::optional<std::string> backgroundColor;
    double backgroundRadiusRatio = 0.0;
    double backgroundPaddingXRatio = 0.0;
    double backgroundPaddingYRatio = 0.0;

    // Gaussian blur. nullopt = blur off. rendered sigma = blurRatio * fontSize (size-invariant).
    std::optional<double> blurRatio;

    // Glow (volumetric "sunbeam" halo). nullopt color = glow off.
    std::optional<std::string> glowColor;
    double glowIntensityRatio = 0.0;                  // 0..1 brightness — alpha multiplier on the glow colour
    double glowRangeRatio = 0.0;                      // 0..1 beam reach — rayLen = glowRangeRatio * GLOW_RAY_LEN_SCALE
    double glowDirectionX = 0.0;                      // -50..50 horizontal light-source offset from block centre
    double glowDirectionY = 0.0;                      // -50..50 vertical light-source offset from block centre

    // Curved text. nullopt = curving off. -360..360 degrees = the arc the single line spans
    // (sign chooses the bend direction); 0 = enabled but straight, 360 = full circle.
    std::optional<double> curveAngle;
};

struct TextTransformation {
    double size = 1.0;                                // factor; fontSize = projectWidth * (1/240) * size
    double rotation = 0.0;                            // degrees
    double positionX = 0.0;                           // pixel coord of CENTER of bounding box
    double positionY = 0.0;                           // pixel coord of CENTER of bounding box
    // Wrap width as a dimensionless multiplier of the same canvas-and-size scale that drives
    // fontSize:
    //   wrapWidthPx = projectWidth * SIZE_BASE_COEFFICIENT * size * maxWidth
    // This composition makes the wrap box invariant under both canvas resize and `size` change.
    // 0 = no wrapping (single line). See BACKEND_PATCH_MAX_WIDTH_SIZE_RELATIVE.md.
    double maxWidth = 0.0;
    // Static 3D tilt of the whole block about its own centre, each in the range −90…90°.
    // Expressed through the animation transform system (rotateX/rotateY + STATIC_ROTATION_PERSPECTIVE).
    // 0/0 = no tilt.
    double tiltX = 0.0;                               // vertical tilt (maps to rotateX)
    double tiltY = 0.0;                               // horizontal tilt (maps to rotateY)
};

struct TextClipData {
    std::string value;
    TextClipStyle style;
    TextTransformation transformation;
};

// ---------------------------------------------------------------------------
// Pixel-space paint styles (computed)
// ---------------------------------------------------------------------------

struct TextClipStrokeStyle {
    std::string color;
    double width = 0.0;
    // Set when the stroke colour is a CSS linear-gradient; else nullopt (solid stroke).
    std::optional<TextClipGradient> gradient;
};

struct TextClipShadowStyle {
    std::string color;
    double opacity = 1.0;
    double angle = 0.0;
    double distance = 0.0;
    double blur = 0.0;
};

struct TextClipGlowStyle {
    std::string color;                                // opaque glow colour — the silhouette fill
    double opacity = 1.0;                             // paint alpha (intensity * colour alpha)
    double rayLen = 0.0;                              // beam reach — how far the rays extend (0 = none)
    double sourceOffX = 0.0;                          // light-source offset from block centre, in fontSize units
    double sourceOffY = 0.0;
};

struct TextClipPaintStyle {
    std::string fontFamily;
    double fontSize = 0.0;
    int fontWeight = 400;
    bool italic = false;
    std::string color;
    // Set when the fill colour (style.color) is a CSS linear-gradient; else nullopt (solid fill).
    // When set, `color` holds only the opaque coverage colour (the first stop, alpha stripped).
    std::optional<TextClipGradient> colorGradient;
    double letterSpacing = 0.0;
    double lineHeight = 0.0;
    TextAlignment alignment = TextAlignment::CENTER;
    std::optional<TextClipStrokeStyle> stroke;
    std::optional<TextClipShadowStyle> dropShadow;
    // Gaussian blur sigma (px) applied to fill + stroke, and folded into the shadow blur. 0 = off.
    double blur = 0.0;
    // Coloured halo around the glyphs (on the stroke silhouette if present, else the fill). nullopt = off.
    std::optional<TextClipGlowStyle> glow;
    // Curved-text arc angle in degrees, or nullopt when curving is off. When set the text is
    // forced onto a single line bent along a circular arc.
    std::optional<double> curveAngle;
    // transformation.size / LAYOUT_REFERENCE_SIZE. The front end renders the glow at the
    // reference size and GPU-scales the sprite, so its GLOW_MAX_TEXTURE_DIM cap is in reference
    // space; the backend renders at actual size, so it scales that cap by sizeScale to match.
    double sizeScale = 1.0;
};

struct TextClipBackgroundStyle {
    std::string color;
    double opacity = 1.0;
    double paddingX = 0.0;
    double paddingY = 0.0;
    double radius = 0.0;                              // ratio 0..1
};

// ---------------------------------------------------------------------------
// Layout result
// ---------------------------------------------------------------------------

struct TextClipLine {
    std::string text;
    double width = 0.0;
    // Per-codepoint advance: glyphWidth + letterSpacing for every codepoint except the
    // last (which carries no trailing letter-spacing). Populated by layoutText from
    // measurements taken at the layout (reference-size) paint, then scaled into actual
    // pixel space by scaleLayout. Renderer consumes these instead of re-measuring.
    std::vector<double> letterAdvances;
    // True when this line ends a hard paragraph break ('\n') rather than a soft wrap.
    bool isHardBreak = false;
    // Distance (positive) from this line's baseline up to its glyph top.
    double ascent = 0.0;
    // Distance (positive) from this line's baseline down to its glyph bottom.
    double descent = 0.0;
};

struct TextClipLayout {
    std::vector<TextClipLine> lines;
    double lineHeight = 0.0;
    double textWidth = 0.0;
    // Total VISIBLE text height in pixels — NOT lines.size() * lineHeight.
    //   textHeight = firstLineAscent + (lines.size() - 1) * lineHeight + lastLineDescent
    double textHeight = 0.0;
    double layoutWidth = 0.0;
    // Distance (positive) from the top of the bounding box down to the baseline of the
    // first visible line. Renderer places the first baseline so the visible top of line 0
    // sits flush against originY.
    double firstLineAscent = 0.0;
};

} // namespace text
} // namespace openshot
