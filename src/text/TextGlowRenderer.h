#pragma once

// The volumetric glow layer: a silhouette of the glyphs in the glow colour is
// sampled by a runtime shader that marches rays out from the light source,
// producing god-ray beams (see TextGlowShader). A local-bloom Gaussian blur of
// the same silhouette is composited additively on top. Ported from
// text-glow-renderer.ts. Unlike the TS (which caches the silhouette across
// frames for live editing), the backend builds a fresh one-shot silhouette per
// render — the reader rasterizes each frame once.

#include "TextAnimationEngine.h"
#include "TextClipTypes.h"
#include "TextCurvedText.h"
#include "TextGlowShader.h"

#include <skia/include/core/SkImage.h>
#include <skia/include/core/SkRefCnt.h>

#include <vector>

namespace openshot {
namespace subtitle { class SkiaRenderer; }
namespace text {

struct AnimatedCharItem;

class TextGlowRenderer {
public:
    explicit TextGlowRenderer(subtitle::SkiaRenderer* renderer) : renderer(renderer) {}

    // Draw the volumetric glow layer beneath the crisp text. `curved` non-null routes the
    // silhouette through the arc geometry; otherwise the flat block is used. `opacityMul`
    // fades the glow with an animated block opacity. `extraLetterSpacing` (word-mode spread)
    // repositions the silhouette glyphs to track the live text.
    void drawGlowLayer(
        const TextClipLayout& layout,
        const TextClipPaintStyle& style,
        const TextClipGlowStyle& glow,
        double originX,
        double originY,
        const CurvedTextGeometry* curved = nullptr,
        double opacityMul = 1.0,
        double extraLetterSpacing = 0.0);

    // Char-mode glow: build a silhouette of the *animated* glyphs (each drawn in the glow
    // colour under its per-char transform) and run the shared god-ray shader over it, so the
    // beams emanate from the one shared block light source exactly like the resting glow.
    void drawAnimatedGlowLayer(
        const std::vector<AnimatedCharItem>& items,
        const TextClipPaintStyle& style,
        const TextClipGlowStyle& glow,
        double contentWidth, double contentHeight,
        double originX, double originY,
        const AnimationTransformFlags& animation);

private:
    struct GlowMargin { double margin; int width; int height; bool valid; };

    // Size the silhouette: pad for the outward beams, capped at GLOW_MAX_TEXTURE_DIM.
    GlowMargin glowMarginFor(
        double contentWidth, double contentHeight,
        const TextClipPaintStyle& style, const TextClipGlowStyle& glow,
        double spreadMargin = 0.0) const;

    // Composite the glow beneath the crisp text (ray-march shader + local bloom, both Screen).
    void paintGlowFromSilhouette(
        const sk_sp<SkImage>& image,
        const TextClipGlowStyle& glow,
        const TextClipPaintStyle& style,
        double contentWidth, double contentHeight,
        double margin, int width, int height,
        double originX, double originY,
        double opacityMul);

    // Rasterize the glyph silhouette (glow colour, transparent bg) into a new image.
    sk_sp<SkImage> renderGlowSilhouette(
        const TextClipLayout& layout,
        const TextClipPaintStyle& style,
        const std::string& glowColor,
        double margin, int width, int height,
        const CurvedTextGeometry* curved,
        double extraLetterSpacing);

    subtitle::SkiaRenderer* renderer;

    // Glow render quality for this pass. The animated path lowers these (motion hides the
    // difference); the static/resting path keeps full quality (and is cached, so paid once).
    double glowScale_ = GLOW_RENDER_SCALE;
    double glowStepCap_ = 32.0;
};

} // namespace text
} // namespace openshot
