#include "TextGlowRenderer.h"

#include "../subtitle/SkiaRenderer.h"
#include "TextAnimationRenderer.h"
#include "TextClipRenderer.h"
#include "TextDrawShared.h"
#include "TextGlowShader.h"

#include <skia/include/core/SkCanvas.h>
#include <skia/include/core/SkData.h>
#include <skia/include/core/SkImageInfo.h>
#include <skia/include/core/SkPaint.h>
#include <skia/include/core/SkSamplingOptions.h>
#include <skia/include/core/SkShader.h>
#include <skia/include/core/SkSurface.h>
#include <skia/include/effects/SkImageFilters.h>

#include <algorithm>
#include <cmath>

namespace openshot {
namespace text {

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

    // The glow silhouette spans the curved content box when curving, otherwise the flat block.
    const double contentWidth = curved ? curved->width : layout.layoutWidth;
    const double contentHeight = curved ? curved->height : layout.textHeight;

    // Animated letter-spacing (word-mode merge/spread) pushes glyphs past the resting box;
    // reserve that spread so the silhouette captures them at the drawn positions.
    double maxLineLen = 0.0;
    if (!curved) {
        for (const auto& line : layout.lines) maxLineLen = std::max(maxLineLen, static_cast<double>(utf8Length(line.text)));
    }
    const double spreadMargin = std::abs(extraLetterSpacing) * std::max(0.0, maxLineLen - 1.0);

    const GlowMargin geom = glowMarginFor(contentWidth, contentHeight, style, glow, spreadMargin);
    if (!geom.valid) return; // bare text already exceeds the cap — skip glow rather than crash

    const sk_sp<SkImage> image = renderGlowSilhouette(
        layout, style, glow.color, geom.margin, geom.width, geom.height, curved, extraLetterSpacing);
    if (!image) return;

    paintGlowFromSilhouette(
        image, glow, style, contentWidth, contentHeight,
        geom.margin, geom.width, geom.height, originX, originY, opacityMul);
}

TextGlowRenderer::GlowMargin TextGlowRenderer::glowMarginFor(
    double contentWidth, double contentHeight,
    const TextClipPaintStyle& style, const TextClipGlowStyle& glow,
    double spreadMargin) const
{
    const double beamBlurSigma = GLOW_BEAM_BLUR_RATIO * style.fontSize;
    const double offX = glow.sourceOffX * style.fontSize;
    const double offY = glow.sourceOffY * style.fontSize;
    const double offMax = std::max(std::abs(offX), std::abs(offY));
    const double halfExtent = std::max(contentWidth, contentHeight) / 2.0;

    const double idealMargin =
        std::ceil(glow.rayLen * (halfExtent + offMax) + offMax + beamBlurSigma * 3.0 + 4.0 + spreadMargin);
    const double maxMargin =
        std::floor((GLOW_MAX_TEXTURE_DIM - std::max(contentWidth, contentHeight)) / 2.0);
    if (maxMargin <= 0.0) return {0.0, 0, 0, false};
    const double margin = std::min(idealMargin, maxMargin);
    return {
        margin,
        static_cast<int>(std::ceil(contentWidth + 2.0 * margin)),
        static_cast<int>(std::ceil(contentHeight + 2.0 * margin)),
        true,
    };
}

sk_sp<SkImage> TextGlowRenderer::renderGlowSilhouette(
    const TextClipLayout& layout,
    const TextClipPaintStyle& style,
    const std::string& glowColor,
    double margin, int width, int height,
    const CurvedTextGeometry* curved,
    double extraLetterSpacing)
{
    sk_sp<SkSurface> surface = SkSurfaces::Raster(SkImageInfo::MakeN32Premul(width, height));
    if (!surface) return nullptr;
    SkCanvas* offscreen = surface->getCanvas();
    offscreen->clear(SK_ColorTRANSPARENT);

    renderer->renderToCanvas(offscreen, [&] {
        const SkPaint* fillPaint = renderer->getPaint(subtitle::PaintProps{glowColor, 1.0, std::nullopt, std::nullopt});
        const SkPaint* strokePaint = style.stroke.has_value()
            ? renderer->getPaint(subtitle::PaintProps{glowColor, 1.0, style.stroke->width, std::nullopt})
            : nullptr;

        if (curved) {
            forEachCurvedGlyph(renderer, *curved, margin, margin, style, strokePaint, *fillPaint);
        } else {
            const double firstBaselineY = margin + layout.firstLineAscent;
            for (size_t li = 0; li < layout.lines.size(); ++li) {
                const auto& line = layout.lines[li];
                if (line.text.empty()) continue;
                const double baselineY = firstBaselineY + static_cast<double>(li) * layout.lineHeight;
                const double x = margin + getLineStartX(line, layout, style.alignment, extraLetterSpacing);
                forEachLetter(line, x, extraLetterSpacing, [&](const std::string& letter, double letterX) {
                    if (strokePaint) drawLetter(renderer, letter, letterX, baselineY, *strokePaint, style);
                    drawLetter(renderer, letter, letterX, baselineY, *fillPaint, style);
                });
            }
        }
    });

    return surface->makeImageSnapshot(SkIRect::MakeWH(width, height));
}

void TextGlowRenderer::drawAnimatedGlowLayer(
    const std::vector<AnimatedCharItem>& items,
    const TextClipPaintStyle& style,
    const TextClipGlowStyle& glow,
    double contentWidth, double contentHeight,
    double originX, double originY,
    const AnimationTransformFlags& animation)
{
    if (!renderer->getCanvas() || items.empty()) return;
    if (!getGlowEffect()) return;

    const GlowMargin geom = glowMarginFor(contentWidth, contentHeight, style, glow);
    if (!geom.valid) return;

    sk_sp<SkSurface> surface = SkSurfaces::Raster(SkImageInfo::MakeN32Premul(geom.width, geom.height));
    if (!surface) return;
    SkCanvas* offscreen = surface->getCanvas();
    offscreen->clear(SK_ColorTRANSPARENT);

    const double margin = geom.margin;
    renderer->renderToCanvas(offscreen, [&] {
        SkCanvas* target = renderer->getCanvas();
        target->save();
        // The items carry absolute layout coordinates (origin baked in); shift so the resting
        // content box top-left lands at (margin, margin) in the silhouette.
        target->translate(static_cast<float>(margin - originX), static_cast<float>(margin - originY));
        for (const auto& item : items) {
            target->save();
            applyCharTransform(renderer, target, item, style.fontSize, animation);
            if (style.stroke.has_value()) {
                withAnimatedPaint(renderer, {glow.color, 1.0, style.stroke->width}, item.opacity, 0.0,
                    [&](const SkPaint& paint) { drawAnimatedLetter(renderer, item, 0.0, 0.0, paint, style); });
            }
            withAnimatedPaint(renderer, {glow.color, 1.0, std::nullopt}, item.opacity, 0.0,
                [&](const SkPaint& paint) { drawAnimatedLetter(renderer, item, 0.0, 0.0, paint, style); });
            target->restore();
        }
        target->restore();
    });

    sk_sp<SkImage> image = surface->makeImageSnapshot(SkIRect::MakeWH(geom.width, geom.height));
    if (!image) return;

    paintGlowFromSilhouette(image, glow, style, contentWidth, contentHeight,
                            geom.margin, geom.width, geom.height, originX, originY, 1.0);
}

void TextGlowRenderer::paintGlowFromSilhouette(
    const sk_sp<SkImage>& image,
    const TextClipGlowStyle& glow,
    const TextClipPaintStyle& style,
    double contentWidth, double contentHeight,
    double margin, int width, int height,
    double originX, double originY,
    double opacityMul)
{
    SkCanvas* canvas = renderer->getCanvas();
    SkRuntimeEffect* effect = getGlowEffect();
    if (!canvas || !effect) return;

    const double beamBlurSigma = GLOW_BEAM_BLUR_RATIO * style.fontSize;
    const double offX = glow.sourceOffX * style.fontSize;
    const double offY = glow.sourceOffY * style.fontSize;
    const float lightX = static_cast<float>(margin + contentWidth / 2.0 + offX);
    const float lightY = static_cast<float>(margin + contentHeight / 2.0 + offY);
    const float steps = static_cast<float>(glowSteps(glow.rayLen));

    // Uniforms in SkSL declaration order: float2 lightPos, rayLen, steps, gain, falloff.
    const float uniforms[6] = {
        lightX, lightY,
        static_cast<float>(glow.rayLen), steps,
        static_cast<float>(GLOW_GAIN), static_cast<float>(GLOW_FALLOFF),
    };

    const SkSamplingOptions linear(SkFilterMode::kLinear);
    sk_sp<SkShader> child = image->makeShader(SkTileMode::kDecal, SkTileMode::kDecal, linear);
    SkRuntimeEffect::ChildPtr children[1] = { SkRuntimeEffect::ChildPtr(child) };
    sk_sp<SkShader> shader = effect->makeShader(
        SkData::MakeWithCopy(uniforms, sizeof(uniforms)),
        SkSpan<const SkRuntimeEffect::ChildPtr>(children, 1));
    if (!shader) return;

    SkPaint rayPaint;
    rayPaint.setShader(shader);
    rayPaint.setBlendMode(SkBlendMode::kScreen);
    rayPaint.setAlphaf(static_cast<float>(clamp01(glow.opacity * opacityMul)));
    if (beamBlurSigma > 0.0) {
        rayPaint.setImageFilter(SkImageFilters::Blur(
            beamBlurSigma, beamBlurSigma, SkTileMode::kDecal, nullptr));
    }

    const double bloomSigma = GLOW_BLOOM_BLUR_RATIO * style.fontSize;
    SkPaint bloomPaint;
    bool hasBloom = bloomSigma > 0.0;
    if (hasBloom) {
        bloomPaint.setBlendMode(SkBlendMode::kScreen);
        bloomPaint.setAlphaf(static_cast<float>(clamp01(glow.opacity * GLOW_BLOOM_ALPHA * opacityMul)));
        bloomPaint.setImageFilter(SkImageFilters::Blur(
            bloomSigma, bloomSigma, SkTileMode::kDecal, nullptr));
    }

    canvas->save();
    canvas->translate(static_cast<float>(originX - margin), static_cast<float>(originY - margin));
    canvas->drawRect(SkRect::MakeWH(static_cast<float>(width), static_cast<float>(height)), rayPaint);
    if (hasBloom) {
        canvas->drawImage(image.get(), 0, 0, SkSamplingOptions(), &bloomPaint);
    }
    canvas->restore();
}

} // namespace text
} // namespace openshot
