#pragma once

// The volumetric glow layer: a silhouette of the glyphs in the glow colour is
// sampled by a runtime shader that marches rays out from the light source,
// producing god-ray beams (see TextGlowShader). A local-bloom Gaussian blur of
// the same silhouette is composited additively on top. Ported from
// text-glow-renderer.ts. Unlike the TS (which caches the silhouette across
// frames for live editing), the backend builds a fresh one-shot silhouette per
// render — the reader rasterizes each frame once.

#include "../subtitle/SubtitleTypes.h"
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

struct AnimatedUnitItem;

// One composited glow image reused across frames.
//
// The glow is ~99 % of an animated glow frame on the raster path (231 ms of a 231 ms frame for
// `text_animated_glow_3`'s worst clip), and ~91 % of that is the ray-march itself — measured by
// sweeping OPENSHOT_GLOW_STEPS, which changes only the step count: 4 -> 58 ms, 24 -> 243 ms,
// 32 -> 318 ms, linear at 9.3 ms/step.
//
// For a BLOCK-mode animation none of that work depends on the frame. The animation transform is
// concat'd onto the canvas before the block is drawn, so the glow is marched in block-local space
// and composited with a single drawImage under whatever transform the canvas carries. Caching the
// composited image and redrawing it is therefore **bit-identical** — the same draw call with the
// same image — rather than an approximation.
//
// The owner (TextClipReader) decides validity, which is why the key is this small: within one
// reader with no glow-affecting style keyframes, the layout, paint and glow style are fixed by
// construction, so the only per-frame inputs left are the block's animated opacity and its
// animated letter spacing. A miss costs exactly what the uncached path costs today.
//
// GPU images are deliberately NOT cached. `GpuFrame::snapshot()` comes off a pooled surface that
// is returned to the pool when the frame dies, and a cached texture would also have to be dropped
// before the Graphite context goes away (see CLAUDE.md). The raster path is where the win is —
// with a GPU the same clip is already 4.7 ms instead of 81 ms.
struct GlowFrameCache {
    sk_sp<SkImage> image;
    double opacityMul = 0.0;
    double extraLetterSpacing = 0.0;
    // The colours baked into the stored image follow one convention; serving it to a
    // renderer using the other exchanges red and blue. With a GPU present neither path
    // populates this cache at all (paintGlowFromSilhouette returns null and it resets),
    // so today only a GPU that comes and goes mid-clip could mix the two -- keying on it
    // costs a comparison and removes the possibility. See subtitle/SubtitleTypes.h.
    subtitle::ColorConvention convention = subtitle::ColorConvention::QImageBytes;
    bool valid = false;

    void reset() { image.reset(); valid = false; }

    bool matches(double opacity_mul, double extra_letter_spacing,
                 subtitle::ColorConvention conv) const {
        return valid && image && opacityMul == opacity_mul
               && extraLetterSpacing == extra_letter_spacing && convention == conv;
    }

    void store(sk_sp<SkImage> img, double opacity_mul, double extra_letter_spacing,
               subtitle::ColorConvention conv) {
        image = std::move(img);
        opacityMul = opacity_mul;
        extraLetterSpacing = extra_letter_spacing;
        convention = conv;
        valid = image != nullptr;
    }
};

class TextGlowRenderer {
public:
    explicit TextGlowRenderer(subtitle::SkiaRenderer* renderer, GlowFrameCache* cache = nullptr)
        : renderer(renderer), cache(cache) {}

    // Draw the volumetric glow layer beneath the crisp text. `curved` non-null routes the
    // silhouette through the arc geometry; otherwise the flat block is used. `opacityMul`
    // fades the glow with an animated block opacity. `extraLetterSpacing` (block-mode spread)
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
        const std::vector<AnimatedUnitItem>& items,
        const TextClipPaintStyle& style,
        const TextClipGlowStyle& glow,
        double contentWidth, double contentHeight,
        double originX, double originY,
        const AnimationTransformFlags& animation);

private:
    // The silhouette image is sized ONLY by glyph geometry (imageMargin: stroke overhang + fixed
    // beam-blur softening + block-mode spread + per-unit overshoot) so it stays a stable size while
    // an animating rayLen/light-offset changes only shader uniforms — no per-frame re-rasterize,
    // no ~1px jump. `rectPadX`/`rectPadY` carry the beam reach — per-axis, because the ray-march is
    // a homothety about the light source — and only widen the ray-march draw surface
    // (sampled beyond the image via Decal), never the image. `renderScale` folds the quality
    // downscale (GLOW_RENDER_SCALE) with an extra downscale when the surface would exceed the
    // texture cap — so large text keeps its full beam extent instead of being truncated/skipped.
    struct GlowMargin { double imageMargin; double rectPadX; double rectPadY; int width; int height; double renderScale; bool valid; };

    // Size the silhouette image + separate beam-reach draw padding. `extraPad` adds per-char
    // overshoot room for the char-mode animated silhouette (rebuilt every frame anyway).
    GlowMargin glowMarginFor(
        double contentWidth, double contentHeight,
        const TextClipPaintStyle& style, const TextClipGlowStyle& glow,
        double spreadMargin = 0.0, double extraPad = 0.0) const;

    // Draw a finished glow image beneath the crisp text (Screen), undoing the working surface's
    // offset and downscale. This is the whole of a cache hit.
    void compositeGlow(const sk_sp<SkImage>& combined,
                       double imageMargin, double rectPadX, double rectPadY, double renderScale,
                       double originX, double originY) const;

    // Build the glow (ray-march shader + local bloom, both Screen) and composite it. Returns the
    // composited image when it is raster-owned and so safe to keep across frames, and null when it
    // came off a pooled GPU surface — which is what stops a texture ending up in the cache.
    sk_sp<SkImage> paintGlowFromSilhouette(
        const sk_sp<SkImage>& image,
        const TextClipGlowStyle& glow,
        const TextClipPaintStyle& style,
        double contentWidth, double contentHeight,
        double imageMargin, double rectPadX, double rectPadY, int width, int height, double renderScale,
        double originX, double originY,
        double opacityMul);

    // Rasterize the glyph silhouette (glow colour, transparent bg) into a new image.
    sk_sp<SkImage> renderGlowSilhouette(
        const TextClipLayout& layout,
        const TextClipPaintStyle& style,
        const std::string& glowColor,
        double imageMargin, int width, int height, double renderScale,
        const CurvedTextGeometry* curved,
        double extraLetterSpacing);

    subtitle::SkiaRenderer* renderer;

    // Non-owning; null on every path whose glow is not frame-invariant (unit-mode animation, a
    // glow-affecting style keyframe). Owned by TextClipReader, which clears it when the plan
    // changes. See GlowFrameCache above.
    GlowFrameCache* cache = nullptr;

    // Glow render quality for this pass. The animated path lowers these (motion hides the
    // difference); the static/resting path keeps full quality (and is cached, so paid once).
    double glowScale_ = GLOW_RENDER_SCALE;
    double glowStepCap_ = 32.0;
};

} // namespace text
} // namespace openshot
