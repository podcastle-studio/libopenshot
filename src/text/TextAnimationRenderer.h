#pragma once

// Animated text rendering: the stateless canvas transforms shared by word and char
// modes (pivot / translate / rotate / skew / scale / 3D perspective + inset/polygon
// clips), the per-glyph primitives of char-mode, and the two animation renderers
// (word mode = whole block under one transform, with a composited-texture path for
// 3D / scale presets; char mode = per-letter staggered transforms in global passes).
// Ported from text-animation-transforms.ts, text-animated-glyph-painter.ts,
// text-word-animation-renderer.ts and text-char-animation-renderer.ts.

#include "TextAnimationEngine.h"
#include "TextClipTypes.h"
#include "TextCurvedText.h"

#include <skia/include/core/SkMatrix.h>

#include <functional>
#include <optional>
#include <string>
#include <vector>

class SkCanvas;
class SkPaint;

namespace openshot {
namespace subtitle { class SkiaRenderer; }
namespace text {

class TextGlowRenderer;

// ── Transforms ───────────────────────────────────────────────────────────────

// Build the local animation transform (relative to the already-positioned pivot) as an
// SkMatrix: pivot shift, translate/rotate (in the preset's CSS order), skew, scale and the
// 3D perspective projection. Used both to drive the canvas and to bound the animated extent.
SkMatrix animationMatrix(const ResolvedAnimProps& props, double fontSize,
                         double boxWidth, double boxHeight, const AnimationTransformFlags& flags);

void applyAnimationTransform(SkCanvas* canvas, const ResolvedAnimProps& props, double fontSize,
                             double boxWidth, double boxHeight, const AnimationTransformFlags& flags);

void applyInsetClip(SkCanvas* canvas, const ResolvedAnimProps& props, double boxWidth, double boxHeight);

void applyPolygonClip(SkCanvas* canvas, const std::vector<std::pair<double, double>>& polygon,
                      double boxWidth, double boxHeight);

// ── Per-glyph primitives (char mode) ─────────────────────────────────────────

struct CurvedCharPlacement {
    double arcX = 0.0;          // glyph baseline-centre on the arc, canvas coordinates
    double arcY = 0.0;
    double rotationDeg = 0.0;   // tangent rotation, degrees
    double pivotOffsetY = 0.0;  // baseline -> box-centre vertical offset
};

struct AnimatedCharItem {
    std::string letter;
    double x = 0.0;             // letter draw origin (baseline-left), layout coordinates
    double baselineY = 0.0;
    double centerX = 0.0;       // visual center of the letter box — the per-char transform pivot
    double centerY = 0.0;
    double boxWidth = 0.0;
    double boxHeight = 0.0;
    ResolvedAnimProps props;
    double opacity = 1.0;
    std::optional<CurvedCharPlacement> curve;  // set for curved text
};

void applyCharTransform(subtitle::SkiaRenderer* renderer, SkCanvas* canvas,
                        const AnimatedCharItem& item, double fontSize, const AnimationTransformFlags& flags);

void drawAnimatedLetter(subtitle::SkiaRenderer* renderer, const AnimatedCharItem& item,
                        double dx, double dy, const SkPaint& paint, const TextClipPaintStyle& style);

// Base paint description for an animated draw.
struct AnimatedPaintBase {
    std::string color;
    double opacity = 1.0;
    std::optional<double> strokeWidth;
};

// Get a paint for an animated draw (cached when fully opaque + unblurred, else a transient
// copy carrying the animated alpha / mask blur) and pass it to `draw`.
void withAnimatedPaint(subtitle::SkiaRenderer* renderer, const AnimatedPaintBase& base,
                       double alphaMultiplier, double maskBlur,
                       const std::function<void(const SkPaint&)>& draw);

// ── Word-mode renderer ───────────────────────────────────────────────────────

// Decouples the shared transform/texture core from what the block actually is (flat vs curved).
struct WordAnimationContent {
    double contentWidth = 0.0;
    double contentHeight = 0.0;
    std::function<double(double blurSigma)> margin;
    // Paint the block (background + glyphs) with its glyph-box top-left at (originX, originY).
    // skipGlow omits the glow layer (composited-texture path draws the glow live instead).
    std::function<void(double originX, double originY, bool skipGlow)> draw;
    // Paint just the glow layer with the glyph-box top-left at (originX, originY).
    std::function<void(double originX, double originY, double opacityMul)> drawGlow;
};

class WordAnimationRenderer {
public:
    explicit WordAnimationRenderer(subtitle::SkiaRenderer* renderer) : renderer(renderer) {}

    void renderWordAnimatedBlock(
        const WordAnimationContent& content,
        const TextClipPaintStyle& style,
        const std::optional<TextClipBackgroundStyle>& background,
        double originX, double originY, double scale,
        const TextClipAnimationFrame& animation);

private:
    void drawWordBlockTexture(
        const WordAnimationContent& content,
        double paddingX, double paddingY, double scale,
        const ResolvedAnimProps& props, double fontSize);

    subtitle::SkiaRenderer* renderer;
};

// ── Char-mode renderer ───────────────────────────────────────────────────────

class CharAnimationRenderer {
public:
    CharAnimationRenderer(subtitle::SkiaRenderer* renderer, TextGlowRenderer* glowRenderer)
        : renderer(renderer), glowRenderer(glowRenderer) {}

    void renderCharAnimated(
        const TextClipLayout& layout, const TextClipPaintStyle& style,
        const std::optional<TextClipBackgroundStyle>& background,
        double originX, double originY, const TextClipAnimationFrame& animation);

    void renderCurvedCharAnimated(
        const CurvedTextGeometry& geometry, const TextClipLine& line, const TextClipPaintStyle& style,
        const std::optional<TextClipBackgroundStyle>& background,
        double originX, double originY, const TextClipAnimationFrame& animation);

private:
    void drawAnimatedCharItems(
        std::vector<AnimatedCharItem>& items, const TextClipPaintStyle& style,
        const AnimationTransformFlags& flags,
        double contentWidth, double contentHeight, double originX, double originY);

    subtitle::SkiaRenderer* renderer;
    TextGlowRenderer* glowRenderer;
};

// ── Top-level per-frame dispatch ─────────────────────────────────────────────

// Draw one text frame: static when `animation` is nullopt, otherwise the same block content
// under the frame's word- or char-level transforms. Dispatches flat vs curved (paint.curveAngle).
void renderTextFrame(
    const TextClipLayout& layout,
    const TextClipPaintStyle& paint,
    const std::optional<TextClipBackgroundStyle>& background,
    double originX, double originY, double scale,
    const std::optional<TextClipAnimationFrame>& animation,
    subtitle::SkiaRenderer* renderer);

// Half-extents (from the content-box centre) the animation reaches across the whole timeline,
// so the reader can size a fixed frame buffer that never clips the animated glyphs.
struct AnimatedExtent { double halfWidth; double halfHeight; };
AnimatedExtent computeAnimatedExtent(
    const TextClipLayout& layout, const TextClipPaintStyle& paint,
    double contentWidth, double contentHeight,
    const AnimationTimeline& timeline, const AnimationPresetMap& presets, int charCount);

} // namespace text
} // namespace openshot
