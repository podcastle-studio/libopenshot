#pragma once

// Curved text: arc geometry (per-glyph position + tangent rotation) and the
// static painter that draws the glyphs along the arc. Ported from
// text-curve-geometry.ts and text-curved-text-painter.ts.

#include "TextClipTypes.h"

#include <optional>
#include <string>
#include <vector>

class SkPaint;

namespace openshot {
namespace subtitle { class SkiaRenderer; }
namespace text {

class TextGlowRenderer;

// One laid-out glyph resolved to its position and rotation on the arc.
struct CurvedGlyphPlacement {
    std::string letter;
    double advance = 0.0;   // advance width (includes letter spacing); glyph centred on it
    double cx = 0.0;        // baseline-centre of the glyph, relative to the box top-left
    double cy = 0.0;
    double rotation = 0.0;  // tangent rotation, radians
};

// The single curved line resolved to per-glyph placements plus the tight content box.
struct CurvedTextGeometry {
    std::vector<CurvedGlyphPlacement> placements;
    double width = 0.0;
    double height = 0.0;
};

bool isSpaceGlyph(const std::string& letter);

// Lay a single line of text along a circular arc subtending `curveAngleDeg` degrees.
CurvedTextGeometry computeCurvedGeometry(const TextClipLine& line, double curveAngleDeg);

// Walk the curved glyphs, drawing each one rotated about its baseline-centre. `underPaint`
// (if non-null) is drawn first (used by the shadow/glow passes that stroke + fill in one go),
// then `mainPaint`. Skips space glyphs.
void forEachCurvedGlyph(
    subtitle::SkiaRenderer* renderer,
    const CurvedTextGeometry& geometry,
    double originX,
    double originY,
    const TextClipPaintStyle& style,
    const SkPaint* underPaint,
    const SkPaint& mainPaint);

// Block safety margin for curved text (shadow/stroke/glow/blur extent).
double curvedBlockMargin(const TextClipPaintStyle& style, const CurvedTextGeometry& geometry, double blurSigma);

class CurvedTextPainter {
public:
    CurvedTextPainter(subtitle::SkiaRenderer* renderer, TextGlowRenderer* glowRenderer)
        : renderer(renderer), glowRenderer(glowRenderer) {}

    // Paint the static curved block: background, then global passes (shadow -> glow ->
    // stroke -> fill), every glyph positioned and rotated along the arc. `skipGlow` / `skipShadow`
    // omit those layers (used by the 3D-tilt path, which draws them live under the transform).
    void drawCurvedStatic(
        const CurvedTextGeometry& geometry,
        const TextClipLayout& layout,
        const TextClipPaintStyle& style,
        const std::optional<TextClipBackgroundStyle>& background,
        double originX,
        double originY,
        bool skipGlow = false,
        bool skipShadow = false);

    // Draw ONLY the curved drop-shadow pass (no background / glow / stroke / fill). No-op when the
    // style has no shadow. Used by the 3D-tilt path to draw the shadow live under the transform.
    void drawCurvedShadowOnly(
        const CurvedTextGeometry& geometry,
        const TextClipPaintStyle& style,
        double originX,
        double originY);

private:
    subtitle::SkiaRenderer* renderer;
    TextGlowRenderer* glowRenderer;
};

} // namespace text
} // namespace openshot
