#include "TextGlowRenderer.h"

#include "../subtitle/SkiaRenderer.h"
#include "TextAnimationRenderer.h"
#include "TextClipRenderer.h"
#include "TextDrawShared.h"
#include "TextGlowShader.h"

#include <skia/include/core/SkCanvas.h>
#include <skia/include/core/SkData.h>
#include <skia/include/core/SkImageInfo.h>
#include <skia/include/core/SkMatrix.h>
#include <skia/include/core/SkPaint.h>
#include <skia/include/core/SkSamplingOptions.h>
#include <skia/include/core/SkShader.h>
#include <skia/include/core/SkSurface.h>
#include <skia/include/effects/SkImageFilters.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace openshot {
namespace text {

namespace {
// Glow quality, read once from the environment for easy A/B tuning (no rebuild). Applied
// UNIFORMLY to resting and in-motion frames so the glow never changes quality mid-clip (no
// pop). Defaults are the chosen 0.40 / 24 balance; lowering trades softness for speed.
//   OPENSHOT_GLOW_SCALE  silhouette/ray-march render scale (0.05..1.0, default GLOW_RENDER_SCALE)
//   OPENSHOT_GLOW_STEPS  ray-march step cap (4..32, default 24)
double glowScaleSetting() {
    static const double v = [] {
        if (const char* e = std::getenv("OPENSHOT_GLOW_SCALE")) {
            try { double d = std::stod(e); if (d >= 0.05 && d <= 1.0) return d; } catch (...) {}
        }
        return GLOW_RENDER_SCALE;
    }();
    return v;
}
double glowStepCapSetting() {
    static const double v = [] {
        if (const char* e = std::getenv("OPENSHOT_GLOW_STEPS")) {
            try { double d = std::stod(e); if (d >= 4.0 && d <= 32.0) return d; } catch (...) {}
        }
        return 24.0;
    }();
    return v;
}
} // namespace

void TextGlowRenderer::drawGlowLayer(
    const TextClipLayout& layout,
    const TextClipPaintStyle& style,
    const TextClipGlowStyle& glow,
    double originX,
    double originY,
    const CurvedTextGeometry* curved,
    double opacityMul,
    double extraLetterSpacing)
{
    if (!renderer->getCanvas()) return;
    if (!getGlowEffect()) return;

    glowScale_ = glowScaleSetting();
    glowStepCap_ = glowStepCapSetting();

    // The glow silhouette spans the curved content box when curving, otherwise the flat block.
    const double contentWidth = curved ? curved->width : layout.layoutWidth;
    const double contentHeight = curved ? curved->height : layout.textHeight;

    // Animated letter-spacing (word-mode merge/spread) pushes glyphs past the resting box;
    // reserve that spread so the silhouette captures them at the drawn positions.
    double maxLineLen = 0.0;
    if (!curved) {
        for (const auto& line : layout.lines) maxLineLen = std::max(maxLineLen, static_cast<double>(clusterCount(line.text)));
    }
    const double spreadMargin = std::abs(extraLetterSpacing) * std::max(0.0, maxLineLen - 1.0);

    const GlowMargin geom = glowMarginFor(contentWidth, contentHeight, style, glow, spreadMargin);
    if (!geom.valid) return;

    const sk_sp<SkImage> image = renderGlowSilhouette(
        layout, style, glow.color, geom.imageMargin, geom.width, geom.height, geom.renderScale,
        curved, extraLetterSpacing);
    if (!image) return;

    paintGlowFromSilhouette(
        image, glow, style, contentWidth, contentHeight,
        geom.imageMargin, geom.rectPad, geom.width, geom.height, geom.renderScale,
        originX, originY, opacityMul);
}

TextGlowRenderer::GlowMargin TextGlowRenderer::glowMarginFor(
    double contentWidth, double contentHeight,
    const TextClipPaintStyle& style, const TextClipGlowStyle& glow,
    double spreadMargin, double extraPad) const
{
    const double beamBlurSigma = GLOW_BEAM_BLUR_RATIO * style.fontSize;
    const double offX = glow.sourceOffX * style.fontSize;
    const double offY = glow.sourceOffY * style.fontSize;
    const double offMax = std::max(std::abs(offX), std::abs(offY));
    const double halfExtent = std::max(contentWidth, contentHeight) / 2.0;

    // Beam reach — how far the god-rays extend past the glyphs. This ONLY widens the ray-march
    // draw surface (the shader samples the silhouette via Decal outside its bounds); it must NOT
    // size the silhouette image, or an animating rayLen / light-offset would re-rasterize the
    // silhouette at a slightly different size each frame and make the text visibly jump ~1px.
    const double rectPad = std::ceil(glow.rayLen * (halfExtent + offMax) + offMax);

    // Stable silhouette padding: stroke overhang + fixed beam-blur softening + block-mode spread +
    // per-unit overshoot (extraPad). Independent of rayLen / light offset, so the image is stable.
    const double strokeOverhang = style.stroke.has_value() ? style.stroke->width : 0.0;
    const double imageMargin =
        std::ceil(strokeOverhang + beamBlurSigma * 3.0 + spreadMargin + extraPad + 4.0);

    const double width  = std::ceil(contentWidth  + 2.0 * imageMargin);
    const double height = std::ceil(contentHeight + 2.0 * imageMargin);

    // Downscale (never skip) when the ray-march surface would exceed the texture cap, preserving
    // the full beam extent instead of truncating it. The front end's GLOW_MAX_TEXTURE_DIM cap is
    // in reference space (it renders at reference size and GPU-scales the sprite by sizeScale);
    // the backend renders at actual size, so scale the cap by sizeScale, bounded by a ceiling.
    const double texCap = std::clamp(GLOW_MAX_TEXTURE_DIM * style.sizeScale,
                                     static_cast<double>(GLOW_MAX_TEXTURE_DIM), 4096.0);
    const double fullMaxDim = std::max(width, height) + 2.0 * rectPad;
    const double pixelMaxDim = fullMaxDim * glowScale_;
    const double downscale = pixelMaxDim > texCap ? texCap / pixelMaxDim : 1.0;
    const double renderScale = glowScale_ * downscale;

    return {
        imageMargin, rectPad,
        static_cast<int>(width), static_cast<int>(height),
        renderScale, true,
    };
}

sk_sp<SkImage> TextGlowRenderer::renderGlowSilhouette(
    const TextClipLayout& layout,
    const TextClipPaintStyle& style,
    const std::string& glowColor,
    double imageMargin, int width, int height, double renderScale,
    const CurvedTextGeometry* curved,
    double extraLetterSpacing)
{
    // Render the silhouette at reduced resolution; the whole glow is upscaled later.
    const double s = renderScale;
    const int sw = std::max(1, static_cast<int>(std::ceil(width * s)));
    const int sh = std::max(1, static_cast<int>(std::ceil(height * s)));
    sk_sp<SkSurface> surface = SkSurfaces::Raster(SkImageInfo::MakeN32Premul(sw, sh));
    if (!surface) return nullptr;
    SkCanvas* offscreen = surface->getCanvas();
    offscreen->clear(SK_ColorTRANSPARENT);
    offscreen->scale(static_cast<float>(s), static_cast<float>(s));  // draw full-coord glyphs downscaled

    renderer->renderToCanvas(offscreen, [&] {
        const SkPaint* fillPaint = renderer->getPaint(subtitle::PaintProps{glowColor, 1.0, std::nullopt, std::nullopt});
        const SkPaint* strokePaint = style.stroke.has_value()
            ? renderer->getPaint(subtitle::PaintProps{glowColor, 1.0, style.stroke->width, std::nullopt})
            : nullptr;

        // The silhouette is what the glow shader smears, so an emoji has to contribute its shape
        // in the GLOW colour — drawn in its own colours it would leak the emoji artwork into the
        // bloom instead of lighting it.
        const EmojiPass emojiSilhouette{EmojiPass::Kind::Silhouette};
        if (curved) {
            forEachCurvedGlyph(renderer, *curved, imageMargin, imageMargin, style, strokePaint, *fillPaint,
                               emojiSilhouette);
        } else {
            const double firstBaselineY = imageMargin + layout.firstLineAscent;
            for (size_t li = 0; li < layout.lines.size(); ++li) {
                const auto& line = layout.lines[li];
                if (line.text.empty()) continue;
                const double baselineY = firstBaselineY + static_cast<double>(li) * layout.lineHeight;
                const double x = imageMargin + getLineStartX(line, layout, style.alignment, extraLetterSpacing);
                forEachLetter(line, x, extraLetterSpacing, [&](const std::string& letter, double letterX) {
                    if (strokePaint) {
                        drawLetter(renderer, letter, letterX, baselineY, *strokePaint, style, {EmojiPass::Kind::Skip});
                    }
                    drawLetter(renderer, letter, letterX, baselineY, *fillPaint, style, emojiSilhouette);
                });
            }
        }
    });

    return surface->makeImageSnapshot(SkIRect::MakeWH(sw, sh));
}

void TextGlowRenderer::drawAnimatedGlowLayer(
    const std::vector<AnimatedUnitItem>& items,
    const TextClipPaintStyle& style,
    const TextClipGlowStyle& glow,
    double contentWidth, double contentHeight,
    double originX, double originY,
    const AnimationTransformFlags& animation)
{
    if (!renderer->getCanvas() || items.empty()) return;
    if (!getGlowEffect()) return;

    // Same uniform glow quality as the static/resting path, so the glow does not change quality
    // when the animation ends. Tunable via OPENSHOT_GLOW_SCALE / OPENSHOT_GLOW_STEPS.
    glowScale_ = glowScaleSetting();
    glowStepCap_ = glowStepCapSetting();

    // Unit-mode glyphs pop/slide past the resting box; pad the silhouette by an extra fontSize to
    // absorb that overshoot (this silhouette is rebuilt every frame anyway, so a bigger stable
    // margin costs nothing and prevents the transformed glyphs from clipping).
    const GlowMargin geom = glowMarginFor(contentWidth, contentHeight, style, glow, 0.0, style.fontSize);
    if (!geom.valid) return;

    const double s = geom.renderScale;
    const int sw = std::max(1, static_cast<int>(std::ceil(geom.width * s)));
    const int sh = std::max(1, static_cast<int>(std::ceil(geom.height * s)));
    sk_sp<SkSurface> surface = SkSurfaces::Raster(SkImageInfo::MakeN32Premul(sw, sh));
    if (!surface) return;
    SkCanvas* offscreen = surface->getCanvas();
    offscreen->clear(SK_ColorTRANSPARENT);
    offscreen->scale(static_cast<float>(s), static_cast<float>(s));  // draw full-coord glyphs downscaled

    const double imageMargin = geom.imageMargin;
    renderer->renderToCanvas(offscreen, [&] {
        SkCanvas* target = renderer->getCanvas();
        target->save();
        // The items carry absolute layout coordinates (origin baked in); shift so the resting
        // content box top-left lands at (imageMargin, imageMargin) in the silhouette.
        target->translate(static_cast<float>(imageMargin - originX), static_cast<float>(imageMargin - originY));
        for (const auto& item : items) {
            target->save();
            applyUnitTransform(renderer, target, item, style.fontSize, animation);
            if (style.stroke.has_value()) {
                withAnimatedPaint(renderer, {glow.color, 1.0, style.stroke->width}, item.opacity, 0.0,
                    [&](const SkPaint& paint) {
                        drawAnimatedUnit(renderer, item, 0.0, 0.0, paint, style, {EmojiPass::Kind::Skip});
                    });
            }
            // As in the static silhouette: an emoji contributes its shape in the glow colour.
            withAnimatedPaint(renderer, {glow.color, 1.0, std::nullopt}, item.opacity, 0.0,
                [&](const SkPaint& paint) {
                    drawAnimatedUnit(renderer, item, 0.0, 0.0, paint, style, {EmojiPass::Kind::Silhouette});
                });
            target->restore();
        }
        target->restore();
    });

    sk_sp<SkImage> image = surface->makeImageSnapshot(SkIRect::MakeWH(sw, sh));
    if (!image) return;

    paintGlowFromSilhouette(image, glow, style, contentWidth, contentHeight,
                            geom.imageMargin, geom.rectPad, geom.width, geom.height, geom.renderScale,
                            originX, originY, 1.0);
}

void TextGlowRenderer::paintGlowFromSilhouette(
    const sk_sp<SkImage>& image,
    const TextClipGlowStyle& glow,
    const TextClipPaintStyle& style,
    double contentWidth, double contentHeight,
    double imageMargin, double rectPad, int width, int height, double renderScale,
    double originX, double originY,
    double opacityMul)
{
    SkCanvas* canvas = renderer->getCanvas();
    SkRuntimeEffect* effect = getGlowEffect();
    if (!canvas || !effect) return;

    // Work in the reduced glow resolution; the ray-march and blurs run on the small surface, and
    // light position / blur sigmas scale with it. The working surface is the silhouette image
    // padded by the beam reach (rectPad) on every side so the god-rays have room to extend past
    // the glyphs — the silhouette itself sits at (rectPad, rectPad) and is sampled via Decal, so
    // everything outside it reads transparent.
    const double s = renderScale;
    const double offset = rectPad;                    // full-coord offset of the silhouette
    const double fullW = static_cast<double>(width)  + 2.0 * rectPad;
    const double fullH = static_cast<double>(height) + 2.0 * rectPad;
    const int gsw = std::max(1, static_cast<int>(std::ceil(fullW * s)));
    const int gsh = std::max(1, static_cast<int>(std::ceil(fullH * s)));
    const float offsetPx = static_cast<float>(offset * s);

    const double beamBlurSigma = GLOW_BEAM_BLUR_RATIO * style.fontSize * s;
    const double bloomSigma    = GLOW_BLOOM_BLUR_RATIO * style.fontSize * s;
    const double offX = glow.sourceOffX * style.fontSize;
    const double offY = glow.sourceOffY * style.fontSize;
    // Light source in working-surface pixel space: the content-box top-left sits at
    // (rectPad + imageMargin), so its centre is that plus half the content, plus the light offset.
    const float lightX = static_cast<float>((offset + imageMargin + contentWidth / 2.0 + offX) * s);
    const float lightY = static_cast<float>((offset + imageMargin + contentHeight / 2.0 + offY) * s);
    const float steps = static_cast<float>(std::min(glowStepCap_, glowSteps(glow.rayLen)));

    // Uniforms in SkSL declaration order: float2 lightPos, rayLen, steps, gain, falloff.
    const float uniforms[6] = {
        lightX, lightY,
        static_cast<float>(glow.rayLen), steps,
        static_cast<float>(GLOW_GAIN), static_cast<float>(GLOW_FALLOFF),
    };

    const SkSamplingOptions linear(SkFilterMode::kLinear);
    // Shift the silhouette shader so the image lands at (rectPad, rectPad) in the working surface.
    const SkMatrix childMat = SkMatrix::Translate(offsetPx, offsetPx);
    sk_sp<SkShader> child = image->makeShader(SkTileMode::kDecal, SkTileMode::kDecal, linear, &childMat);
    SkRuntimeEffect::ChildPtr children[1] = { SkRuntimeEffect::ChildPtr(child) };
    sk_sp<SkShader> shader = effect->makeShader(
        SkData::MakeWithCopy(uniforms, sizeof(uniforms)),
        SkSpan<const SkRuntimeEffect::ChildPtr>(children, 1));
    if (!shader) return;

    // Compose ray-march + local bloom into a small offscreen with their alphas baked.
    // Screen blending is associative, so screening this combined layer onto the canvas
    // matches drawing the ray then the bloom directly — but it lets us upscale only once.
    // (RGBA: this holds the COLOURED glow output, unlike the alpha-only silhouette.)
    sk_sp<SkSurface> glowSurface = SkSurfaces::Raster(SkImageInfo::MakeN32Premul(gsw, gsh));
    if (!glowSurface) return;
    SkCanvas* gc = glowSurface->getCanvas();
    gc->clear(SK_ColorTRANSPARENT);

    SkPaint rayPaint;                                 // base layer (onto transparent)
    rayPaint.setShader(shader);
    rayPaint.setAlphaf(static_cast<float>(clamp01(glow.opacity * opacityMul)));
    if (beamBlurSigma > 0.0) {
        rayPaint.setImageFilter(SkImageFilters::Blur(
            beamBlurSigma, beamBlurSigma, SkTileMode::kDecal, nullptr));
    }
    gc->drawRect(SkRect::MakeWH(static_cast<float>(gsw), static_cast<float>(gsh)), rayPaint);

    if (bloomSigma > 0.0) {
        SkPaint bloomPaint;
        bloomPaint.setBlendMode(SkBlendMode::kScreen);
        bloomPaint.setAlphaf(static_cast<float>(clamp01(glow.opacity * GLOW_BLOOM_ALPHA * opacityMul)));
        bloomPaint.setImageFilter(SkImageFilters::Blur(
            bloomSigma, bloomSigma, SkTileMode::kDecal, nullptr));
        gc->drawImage(image.get(), offsetPx, offsetPx, SkSamplingOptions(), &bloomPaint);
    }

    sk_sp<SkImage> combined = glowSurface->makeImageSnapshot();
    if (!combined) return;

    // Upscale the combined glow onto the canvas (screen-blended, beneath the text). Working-surface
    // (0,0) is (rectPad + imageMargin) left/up of the content-box top-left, which maps to origin.
    SkPaint up;
    up.setBlendMode(SkBlendMode::kScreen);
    canvas->save();
    canvas->translate(static_cast<float>(originX - offset - imageMargin),
                      static_cast<float>(originY - offset - imageMargin));
    canvas->scale(static_cast<float>(1.0 / s), static_cast<float>(1.0 / s));
    canvas->drawImage(combined.get(), 0, 0, linear, &up);
    canvas->restore();
}

} // namespace text
} // namespace openshot
