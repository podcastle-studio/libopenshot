#pragma once

#include "TextClipTypes.h"
#include "TextCurvedText.h"

#include <optional>
#include <string>
#include <vector>

namespace openshot {
namespace subtitle {
    class SkiaRenderer;
}
namespace text {

// ---------------------------------------------------------------------------
// Style conversion helpers (text-rendering.helpers.ts)
// ---------------------------------------------------------------------------

std::string transformTextValue(const std::string& value, TextTransform transform);

/// Convert alignment-anchored x to center-based x.
/// LEFT:  +width/2,  RIGHT: -width/2,  CENTER: 0.
double alignmentOffsetX(TextAlignment alignment, double width);

/// Compute pixel-space paint properties for the given style/transformation.
TextClipPaintStyle convertTextStyleToPaintStyle(
    const TextClipStyle& style,
    const TextTransformation& transformation,
    double projectWidth);

/// Returns a background style or std::nullopt if disabled (no color / zero opacity).
std::optional<TextClipBackgroundStyle> convertBackgroundStyle(
    const TextClipStyle& style,
    const TextClipPaintStyle& paint);

// ---------------------------------------------------------------------------
// Layout (text-rendering.layout.ts)
// ---------------------------------------------------------------------------

/// Per-character advance widths for `text` using the given paint style.
/// `renderer` is used to access SkFont/typeface; it must be non-null.
std::vector<double> measureLetterWidths(
    const std::string& text,
    const TextClipPaintStyle& style,
    subtitle::SkiaRenderer* renderer);

/// Per-codepoint advances suitable for `TextClipLine::letterAdvances`.
/// advances[i] = glyphWidth + letterSpacing, except the last entry which is glyphWidth only.
std::vector<double> computeLetterAdvances(
    const std::string& text,
    const TextClipPaintStyle& style,
    subtitle::SkiaRenderer* renderer);

/// Visible vertical extent of `text` relative to its baseline.
/// `top` is NEGATIVE (above baseline; smaller = taller ascender).
/// `bottom` is POSITIVE (below baseline; larger = deeper descender).
struct VerticalBounds { double top = 0.0; double bottom = 0.0; };
VerticalBounds measureTextVerticalBounds(
    const std::string& text,
    const TextClipPaintStyle& style,
    subtitle::SkiaRenderer* renderer);

/// Lay out text into wrapped lines at the supplied paint's size.
/// For size-stable layout, prefer `layoutTextAtReferenceSize` which routes the
/// measurement pass through `LAYOUT_REFERENCE_SIZE` and then scales the result.
TextClipLayout layoutText(
    const std::string& text,
    const TextClipPaintStyle& style,
    double wrapWidth,
    double userMaxWidth,
    subtitle::SkiaRenderer* renderer);

/// Scale every numeric field of a layout produced at a reference paint up into
/// actual pixel space, with sizeScale = actualFontSize / referenceFontSize.
/// Line text and `letterAdvances` indices are unchanged; only geometry scales.
TextClipLayout scaleLayout(const TextClipLayout& layout, double sizeScale);

/// Convenience wrapper: run `layoutText` against `layoutPaint`, normalise
/// `wrapWidth` / `userMaxWidth` from actual into reference space, then scale
/// the result back to actual space. `paint` is used only to derive sizeScale.
TextClipLayout layoutTextAtReferenceSize(
    const std::string& text,
    const TextClipPaintStyle& paint,
    const TextClipPaintStyle& layoutPaint,
    double wrapWidth,
    double userMaxWidth,
    subtitle::SkiaRenderer* renderer);

/// Starting x offset within layout box for a single line, given the alignment.
/// `extraLetterSpacing` (animated word-mode spread) widens the line so centered/right
/// lines spread symmetrically around their anchor instead of growing to one side.
double getLineStartX(const TextClipLine& line, const TextClipLayout& layout, TextAlignment alignment,
                     double extraLetterSpacing = 0.0);

// ---------------------------------------------------------------------------
// Drawing (text-rendering.draw.ts)
// ---------------------------------------------------------------------------

/// Render a fully computed FLAT layout onto the renderer's canvas at the given top-left origin
/// of the text block (i.e. top-left of the layoutWidth × textHeight box; background extends
/// out by paddingX / paddingY beyond this). Draws in global passes
/// (background -> shadows -> glow -> strokes -> fills). `extraLetterSpacing` (animated word-mode
/// spread) widens the precomputed advances at draw time; `skipGlow` omits the glow layer (used
/// when compositing into an offscreen texture where the glow shader is drawn live instead).
void renderLayout(
    const TextClipLayout& layout,
    const TextClipPaintStyle& paint,
    const std::optional<TextClipBackgroundStyle>& background,
    double originX,
    double originY,
    subtitle::SkiaRenderer* renderer,
    double extraLetterSpacing = 0.0,
    bool skipGlow = false,
    bool skipShadow = false);

/// Draw ONLY the flat block's drop-shadow pass (no background / glow / stroke / fill). No-op when
/// the paint has no shadow. Used by the 3D-tilt word path to draw the shadow live under the same
/// transform (instead of baking it into the perspective-warped texture, which would foreshorten
/// and weaken the halo). Mirrors how the glow layer is drawn live in that path.
void renderShadowLayer(
    const TextClipLayout& layout,
    const TextClipPaintStyle& paint,
    double originX,
    double originY,
    subtitle::SkiaRenderer* renderer,
    double extraLetterSpacing = 0.0);

/// Compute the curved-arc geometry of the layout's first non-empty line (or an empty geometry).
CurvedTextGeometry curvedGeometryForLayout(const TextClipLayout& layout, const TextClipPaintStyle& paint);

/// Draw the static text content, dispatching flat vs curved based on paint.curveAngle.
void drawTextContent(
    const TextClipLayout& layout,
    const TextClipPaintStyle& paint,
    const std::optional<TextClipBackgroundStyle>& background,
    double originX,
    double originY,
    subtitle::SkiaRenderer* renderer);

// ---------------------------------------------------------------------------
// Top-level entry point (text-rendering.ts)
// ---------------------------------------------------------------------------

struct RenderResult {
    TextClipLayout layout;
    double boundingWidth = 0.0;
    double boundingHeight = 0.0;
};

/// Render a text clip onto the given Skia renderer's canvas.
/// `originX` / `originY` are the top-left of the text block (NOT the bounding box including
/// background padding). To draw centered at (cx, cy), pass:
///   originX = cx - layoutWidth/2,  originY = cy - textHeight/2
/// and the background (if any) will extend beyond that by paddingX / paddingY.
RenderResult renderTextClip(
    const TextClipData& clipData,
    double projectWidth,
    double projectHeight,
    subtitle::SkiaRenderer* renderer,
    double originX,
    double originY);

} // namespace text
} // namespace openshot
