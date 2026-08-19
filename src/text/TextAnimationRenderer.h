#pragma once

// Animated text rendering: the stateless canvas transforms shared by block and unit
// modes (pivot / translate / rotate / skew / scale / 3D perspective + inset/polygon
// clips), the per-glyph primitives of unit-mode, and the two animation renderers
// (block mode = whole box under one transform, with a composited-texture path for
// 3D / scale presets; unit mode = staggered per-unit transforms in global passes,
// where a unit is one char, one word or one line of the laid-out block).
// Ported from text-animation-transforms.ts, text-animated-glyph-painter.ts,
// text-block-animation-renderer.ts and text-unit-animation-renderer.ts.

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

// ── Per-unit primitives (unit mode) ──────────────────────────────────────────

// The arc point a curved unit pivots about. For a single-glyph (char) unit this is the glyph's own
// arc point — identical to the pre-unit behaviour. For a multi-glyph unit it is the unit's middle
// placement (or the mean of the two middle ones); position and tangent both vary affinely along the
// arc, so that sits at the unit's arc midpoint to within half a glyph advance.
struct CurvedUnitAnchor {
    double arcX = 0.0;          // anchor baseline-centre on the arc, canvas coordinates
    double arcY = 0.0;
    double rotationDeg = 0.0;   // tangent rotation, degrees
    double pivotOffsetY = 0.0;  // baseline -> box-centre vertical offset
};

// Where one glyph of a multi-glyph curved unit sits relative to the unit anchor, measured in the
// anchor's ROTATED frame. Applied after the unit's animation transform so the group moves rigidly
// without flattening the curve. Unset for flat units and for single-glyph curved units.
struct CurvedGlyphLocal {
    double dx = 0.0;
    double dy = 0.0;
    double rotationDeg = 0.0;   // glyph tangent minus anchor tangent
};

// One glyph of a stagger unit. Draw offsets are stored relative to the unit box CENTRE so the
// shared letter-drawing code works unchanged inside a rotated frame.
struct AnimatedUnitGlyph {
    std::string letter;
    double offsetX = 0.0;       // draw origin (baseline-left) relative to the unit box centre
    double offsetY = 0.0;       // baseline Y relative to the unit box centre
    std::optional<CurvedGlyphLocal> local;
};

// One stagger unit: every glyph is drawn with the SAME evaluated props and the SAME pivot box,
// which is what makes a word or line move as one rigid piece while still being painted letter by
// letter through the existing global pass pipeline.
struct AnimatedUnitItem {
    std::vector<AnimatedUnitGlyph> glyphs;
    double centerX = 0.0;       // visual centre of the unit box — the per-unit transform pivot
    double centerY = 0.0;       // (0/0 for curved units; the anchor carries the placement)
    double boxWidth = 0.0;
    double boxHeight = 0.0;
    ResolvedAnimProps props;
    double opacity = 1.0;
    std::optional<CurvedUnitAnchor> curve;  // set for curved text
};

void applyUnitTransform(subtitle::SkiaRenderer* renderer, SkCanvas* canvas,
                        const AnimatedUnitItem& item, double fontSize, const AnimationTransformFlags& flags);

// Draw every glyph of the unit inside the already-applied unit transform. `dx`/`dy` shift the draw
// origin (used by the drop-shadow pass).
void drawAnimatedUnit(subtitle::SkiaRenderer* renderer, const AnimatedUnitItem& item,
                      double dx, double dy, const SkPaint& paint, const TextClipPaintStyle& style);

// Tally the stagger units over the laid-out lines. Feeds the timing planner.
UnitCounts countAnimationUnits(const TextClipLayout& layout);

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

// ── Block-mode renderer ──────────────────────────────────────────────────────

// Decouples the shared transform/texture core from what the block actually is (flat vs curved).
struct BlockAnimationContent {
    double contentWidth = 0.0;
    double contentHeight = 0.0;
    // True when the style has a glow. When set, the 3D texture path bakes the block in two z-layers
    // (BelowGlow then AboveGlow) with the live glow composited between them, so the glow keeps its flat
    // z-order (above background + shadow, below stroke + fill) instead of sinking under the whole block.
    bool hasGlow = false;
    std::function<double(double blurSigma)> margin;
    // Paint the block for a given z-band (see BlockDrawLayer): All = whole block with glow inline,
    // BelowGlow = background + shadow, AboveGlow = stroke + fill. skipShadow omits the drop shadow
    // (unused today; the shadow is always baked). glyph-box top-left sits at (originX, originY).
    std::function<void(double originX, double originY, BlockDrawLayer layer, bool skipShadow)> draw;
    // Paint just the glow layer with the glyph-box top-left at (originX, originY).
    std::function<void(double originX, double originY, double opacityMul)> drawGlow;
    // Paint just the drop-shadow layer at (originX, originY). Optional (unset = shadow stays baked
    // into the texture). When set, the 3D path draws the shadow live under the transform.
    std::function<void(double originX, double originY)> drawShadow;
};

class BlockAnimationRenderer {
public:
    explicit BlockAnimationRenderer(subtitle::SkiaRenderer* renderer) : renderer(renderer) {}

    void renderBlockAnimated(
        const BlockAnimationContent& content,
        const TextClipPaintStyle& style,
        const std::optional<TextClipBackgroundStyle>& background,
        double originX, double originY, double scale,
        const TextClipAnimationFrame& animation);

private:
    void drawBlockTexture(
        const BlockAnimationContent& content,
        double paddingX, double paddingY, double scale,
        const ResolvedAnimProps& props, double fontSize);

    subtitle::SkiaRenderer* renderer;
};

// ── Unit-mode renderer ───────────────────────────────────────────────────────

class UnitAnimationRenderer {
public:
    UnitAnimationRenderer(subtitle::SkiaRenderer* renderer, TextGlowRenderer* glowRenderer)
        : renderer(renderer), glowRenderer(glowRenderer) {}

    // `skipGlow` omits the glow layer (used when compositing into an offscreen texture for the
    // unit-mode + 3D-tilt path, where the glow is drawn live afterwards under the same transform).
    void renderUnitAnimated(
        const TextClipLayout& layout, const TextClipPaintStyle& style,
        const std::optional<TextClipBackgroundStyle>& background,
        double originX, double originY, const TextClipAnimationFrame& animation, bool skipGlow = false,
        BlockDrawLayer layer = BlockDrawLayer::All);

    void renderCurvedUnitAnimated(
        const CurvedTextGeometry& geometry, const TextClipLine& line, const TextClipPaintStyle& style,
        const std::optional<TextClipBackgroundStyle>& background,
        double originX, double originY, const TextClipAnimationFrame& animation, bool skipGlow = false,
        BlockDrawLayer layer = BlockDrawLayer::All);

    // Draw ONLY the glow layer of the unit animation (no fills / strokes / shadows). No-op when the
    // style has no glow. Used by the unit-mode + 3D path to draw the glow live over the tilted block.
    void drawUnitAnimatedGlowOnly(
        const TextClipLayout& layout, const TextClipPaintStyle& style,
        double originX, double originY, const TextClipAnimationFrame& animation);

    void drawCurvedUnitAnimatedGlowOnly(
        const CurvedTextGeometry& geometry, const TextClipLine& line, const TextClipPaintStyle& style,
        double originX, double originY, const TextClipAnimationFrame& animation);

private:
    void drawAnimatedUnitItems(
        std::vector<AnimatedUnitItem>& items, const TextClipPaintStyle& style,
        const AnimationTransformFlags& flags,
        double contentWidth, double contentHeight, double originX, double originY, bool skipGlow = false,
        BlockDrawLayer layer = BlockDrawLayer::All);

    subtitle::SkiaRenderer* renderer;
    TextGlowRenderer* glowRenderer;
};

// ── Static 3D tilt ─────────────────────────────────────────────────────────

// Build a resting BLOCK-mode frame that carries only a static 3D tilt (rotateX = tiltX,
// rotateY = tiltY, perspective = STATIC_ROTATION_PERSPECTIVE). Flows through the same
// texture+3D block path used by 3D animation presets.
TextClipAnimationFrame buildStatic3DFrame(double tiltX, double tiltY);

// Fold a static tilt into an existing BLOCK-mode animation frame: ADD the static angles onto
// whatever the preset animates, and supply STATIC_ROTATION_PERSPECTIVE only if the preset does
// not animate its own perspective (so flat presets still read as 3D under the tilt).
void composeStatic3DIntoBlockFrame(TextClipAnimationFrame& frame, double tiltX, double tiltY);

// ── Top-level per-frame dispatch ─────────────────────────────────────────────

// Draw one text frame: static when `animation` is nullopt, otherwise the same block content
// under the frame's block- or unit-level transforms. Dispatches flat vs curved (paint.curveAngle).
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
    const AnimationTimeline& timeline, const AnimationPresetMap& presets, const UnitCounts& counts);

} // namespace text
} // namespace openshot
