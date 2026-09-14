#include "TextGlowRenderer.h"

#include "../gpu/GpuDevice.h"
#include "../gpu/GpuFrame.h"
#include "../gpu/GpuOffscreen.h"
#include "../subtitle/SkiaRenderer.h"
#include "TextAnimationRenderer.h"
#include "TextClipRenderer.h"
#include "TextDrawShared.h"
#include "TextGlowShader.h"

#include <skia/include/core/SkBitmap.h>
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
        for (const auto& line : layout.lines) maxLineLen = std::max(maxLineLen, static_cast<double>(utf8Length(line.text)));
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
        geom.imageMargin, geom.rectPadX, geom.rectPadY, geom.width, geom.height, geom.renderScale,
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

    // Stable silhouette padding: stroke overhang + fixed beam-blur softening + block-mode spread +
    // per-unit overshoot (extraPad). Independent of rayLen / light offset, so the image is stable.
    const double strokeOverhang = style.stroke.has_value() ? style.stroke->width : 0.0;
    const double imageMargin =
        std::ceil(strokeOverhang + beamBlurSigma * 3.0 + spreadMargin + extraPad + 4.0);

    const double width  = std::ceil(contentWidth  + 2.0 * imageMargin);
    const double height = std::ceil(contentHeight + 2.0 * imageMargin);

    // Beam reach — how far the god-rays extend past the glyphs. This ONLY widens the ray-march
    // draw surface (the shader samples the silhouette via Decal outside its bounds); it must NOT
    // size the silhouette image, or an animating rayLen / light-offset would re-rasterize the
    // silhouette at a slightly different size each frame and make the text visibly jump ~1px.
    //
    // The march is a homothety about the light source: a silhouette pixel d away from the light
    // on one axis lands (1 + rayLen) * d away on that SAME axis, so the reach is per-axis. Padding
    // both axes with the longer one wastes the short one badly — a 920 x 101 block was padded by
    // 608 px vertically where 158 px is the bound. The farthest silhouette pixel is the image
    // corner: imageMargin + half the content + the light offset from centre. The beam blur is
    // applied after the march, and the local bloom spreads from the image itself, so each needs
    // its own 3 sigma of room.
    //
    // Clamped to what padded both axes before, so whichever axis bound the old value keeps it
    // unchanged and no glow that fitted before can be truncated now.
    const double legacyPad =
        std::ceil(glow.rayLen * (std::max(contentWidth, contentHeight) / 2.0 + offMax) + offMax);
    const double bloomPad = GLOW_BLOOM_BLUR_RATIO * style.fontSize * 3.0;
    auto beamReach = [&](double contentExtent, double off) {
        const double reach =
            glow.rayLen * (imageMargin + contentExtent / 2.0 + std::abs(off)) + beamBlurSigma * 3.0;
        return std::min(legacyPad, std::ceil(std::max(reach, bloomPad)));
    };
    const double rectPadX = beamReach(contentWidth, offX);
    const double rectPadY = beamReach(contentHeight, offY);

    // Downscale (never skip) when the ray-march surface would exceed the texture cap, preserving
    // the full beam extent instead of truncating it. The front end's GLOW_MAX_TEXTURE_DIM cap is
    // in reference space (it renders at reference size and GPU-scales the sprite by sizeScale);
    // the backend renders at actual size, so scale the cap by sizeScale, bounded by a ceiling.
    const double texCap = std::clamp(GLOW_MAX_TEXTURE_DIM * style.sizeScale,
                                     static_cast<double>(GLOW_MAX_TEXTURE_DIM), 4096.0);
    const double fullMaxDim = std::max(width + 2.0 * rectPadX, height + 2.0 * rectPadY);
    const double pixelMaxDim = fullMaxDim * glowScale_;
    const double downscale = pixelMaxDim > texCap ? texCap / pixelMaxDim : 1.0;
    const double renderScale = glowScale_ * downscale;

    return {
        imageMargin, rectPadX, rectPadY,
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
    // Match the destination canvas: on the GPU this both rasterises the glyphs there and saves
    // paintGlowFromSilhouette the per-frame upload of the silhouette it uses as a shader child.
    GpuOffscreen offscreenSurface = GpuOffscreen::Match(renderer->getCanvas(), sw, sh);
    if (!offscreenSurface) return nullptr;
    SkCanvas* offscreen = offscreenSurface.canvas();
    offscreen->clear(SK_ColorTRANSPARENT);
    offscreen->scale(static_cast<float>(s), static_cast<float>(s));  // draw full-coord glyphs downscaled

    renderer->renderToCanvas(offscreen, [&] {
        const SkPaint* fillPaint = renderer->getPaint(subtitle::PaintProps{glowColor, 1.0, std::nullopt, std::nullopt});
        const SkPaint* strokePaint = style.stroke.has_value()
            ? renderer->getPaint(subtitle::PaintProps{glowColor, 1.0, style.stroke->width, std::nullopt})
            : nullptr;

        if (curved) {
            forEachCurvedGlyph(renderer, *curved, imageMargin, imageMargin, style, strokePaint, *fillPaint);
        } else {
            const double firstBaselineY = imageMargin + layout.firstLineAscent;
            for (size_t li = 0; li < layout.lines.size(); ++li) {
                const auto& line = layout.lines[li];
                if (line.text.empty()) continue;
                const double baselineY = firstBaselineY + static_cast<double>(li) * layout.lineHeight;
                const double x = imageMargin + getLineStartX(line, layout, style.alignment, extraLetterSpacing);
                forEachLetter(line, x, extraLetterSpacing, [&](const std::string& letter, double letterX) {
                    if (strokePaint) drawLetter(renderer, letter, letterX, baselineY, *strokePaint, style);
                    drawLetter(renderer, letter, letterX, baselineY, *fillPaint, style);
                });
            }
        }
    });

    return offscreenSurface.snapshot(SkIRect::MakeWH(sw, sh));
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
    // As in renderGlowSilhouette: the silhouette lives wherever the glow will be composited.
    GpuOffscreen offscreenSurface = GpuOffscreen::Match(renderer->getCanvas(), sw, sh);
    if (!offscreenSurface) return;
    SkCanvas* offscreen = offscreenSurface.canvas();
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
                    [&](const SkPaint& paint) { drawAnimatedUnit(renderer, item, 0.0, 0.0, paint, style); });
            }
            withAnimatedPaint(renderer, {glow.color, 1.0, std::nullopt}, item.opacity, 0.0,
                [&](const SkPaint& paint) { drawAnimatedUnit(renderer, item, 0.0, 0.0, paint, style); });
            target->restore();
        }
        target->restore();
    });

    sk_sp<SkImage> image = offscreenSurface.snapshot(SkIRect::MakeWH(sw, sh));
    if (!image) return;

    paintGlowFromSilhouette(image, glow, style, contentWidth, contentHeight,
                            geom.imageMargin, geom.rectPadX, geom.rectPadY, geom.width, geom.height, geom.renderScale,
                            originX, originY, 1.0);
}

void TextGlowRenderer::paintGlowFromSilhouette(
    const sk_sp<SkImage>& image,
    const TextClipGlowStyle& glow,
    const TextClipPaintStyle& style,
    double contentWidth, double contentHeight,
    double imageMargin, double rectPadX, double rectPadY, int width, int height, double renderScale,
    double originX, double originY,
    double opacityMul)
{
    SkCanvas* canvas = renderer->getCanvas();
    SkRuntimeEffect* effect = getGlowEffect();
    if (!canvas || !effect) return;

    // Work in the reduced glow resolution; the ray-march and blurs run on the small surface, and
    // light position / blur sigmas scale with it. The working surface is the silhouette image
    // padded by the beam reach on every side so the god-rays have room to extend past the glyphs —
    // the silhouette itself sits at (rectPadX, rectPadY) and is sampled via Decal, so everything
    // outside it reads transparent. The reach is per-axis (see glowMarginFor).
    const double s = renderScale;
    const double fullW = static_cast<double>(width)  + 2.0 * rectPadX;
    const double fullH = static_cast<double>(height) + 2.0 * rectPadY;
    const int gsw = std::max(1, static_cast<int>(std::ceil(fullW * s)));
    const int gsh = std::max(1, static_cast<int>(std::ceil(fullH * s)));
    const float offsetPxX = static_cast<float>(rectPadX * s);
    const float offsetPxY = static_cast<float>(rectPadY * s);

    const double beamBlurSigma = GLOW_BEAM_BLUR_RATIO * style.fontSize * s;
    const double bloomSigma    = GLOW_BLOOM_BLUR_RATIO * style.fontSize * s;
    const double offX = glow.sourceOffX * style.fontSize;
    const double offY = glow.sourceOffY * style.fontSize;
    // Light source in working-surface pixel space: the content-box top-left sits at
    // (rectPadX + imageMargin, rectPadY + imageMargin), so its centre is that plus half the
    // content, plus the light offset.
    const float lightX = static_cast<float>((rectPadX + imageMargin + contentWidth / 2.0 + offX) * s);
    const float lightY = static_cast<float>((rectPadY + imageMargin + contentHeight / 2.0 + offY) * s);
    const float steps = static_cast<float>(std::min(glowStepCap_, glowSteps(glow.rayLen)));

    // Uniforms in SkSL declaration order: float2 lightPos, rayLen, steps, gain, falloff.
    const float uniforms[6] = {
        lightX, lightY,
        static_cast<float>(glow.rayLen), steps,
        static_cast<float>(GLOW_GAIN), static_cast<float>(GLOW_FALLOFF),
    };

    // Choose the working surface BEFORE building the shader: on the GPU the shader's
    // child has to be a texture-backed image, so this decides which image it is built
    // from. The ray-march below is ~72 % of the worst benchmark scenario, and on the
    // GPU it is the same SkSL over the same silhouette. With OPENSHOT_GPU off,
    // GpuFrame::Create returns null and everything below runs exactly as it did
    // before — the golden suite depends on that staying true.
    //
    // The ray-march earns a GPU surface even when the destination is raster, because it
    // is worth a readback on its own. When the destination is GPU-backed too (step 2.4)
    // that readback goes away and the glow is composited straight from VRAM.
    const bool gpuDestination = GpuOffscreen::IsGpuBacked(canvas);
    std::shared_ptr<GpuFrame> gpuFrame;
    sk_sp<SkImage> source = image;
    if (GpuDevice::Instance().available())
        gpuFrame = GpuFrame::Create(gsw, gsh);
    if (gpuFrame) {
        // Graphite will not upload the raster silhouette on our behalf: a raster image
        // used as a shader is dropped with "Couldn't convert SkImage to a
        // Graphite-backed representation" and the draw vanishes. If the upload fails,
        // give up the GPU surface rather than the glow. With a GPU destination the
        // silhouette was drawn on the GPU already, and this is a no-op.
        source = GpuFrame::ToTexture(image);
        if (!source) {
            gpuFrame.reset();
            source = image;
        }
    }

    const SkSamplingOptions linear(SkFilterMode::kLinear);
    // Shift the silhouette shader so the image lands at (rectPadX, rectPadY) in the working surface.
    const SkMatrix childMat = SkMatrix::Translate(offsetPxX, offsetPxY);
    sk_sp<SkShader> child = source->makeShader(SkTileMode::kDecal, SkTileMode::kDecal, linear, &childMat);
    SkRuntimeEffect::ChildPtr children[1] = { SkRuntimeEffect::ChildPtr(child) };
    sk_sp<SkShader> shader = effect->makeShader(
        SkData::MakeWithCopy(uniforms, sizeof(uniforms)),
        SkSpan<const SkRuntimeEffect::ChildPtr>(children, 1));
    if (!shader) return;

    // Compose ray-march + local bloom into a small offscreen with their alphas baked.
    // Screen blending is associative, so screening this combined layer onto the canvas
    // matches drawing the ray then the bloom directly — but it lets us upscale only once.
    // (RGBA: this holds the COLOURED glow output, unlike the alpha-only silhouette.)
    sk_sp<SkSurface> glowSurface;
    SkCanvas* gc = nullptr;
    if (gpuFrame) {
        gc = gpuFrame->canvas();
    } else {
        glowSurface = SkSurfaces::Raster(SkImageInfo::MakeN32Premul(gsw, gsh));
        if (!glowSurface) return;
        gc = glowSurface->getCanvas();
    }
    if (!gc) return;
    // Pooled GPU surfaces are recycled, so this clear is load-bearing there, not
    // just tidiness as it is for a fresh raster surface.
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
        gc->drawImage(source.get(), offsetPxX, offsetPxY, SkSamplingOptions(), &bloomPaint);
    }

    // Collect the finished glow. Back to the CPU only when the destination canvas is
    // raster: reading into an N32 pixmap converts from the surface's kRGBA_8888, so the
    // platform BGR swap that SkiaRenderer::parseColorString bakes into every colour stays
    // consistent — it is a logical-colour convention, not a byte order, and survives the
    // round trip.
    sk_sp<SkImage> combined;
    if (gpuFrame && gpuDestination) {
        // Both sides are in VRAM: no round trip at all, just a texture draw below.
        combined = gpuFrame->snapshot();
    } else if (gpuFrame) {
        SkBitmap readback;
        if (!readback.tryAllocN32Pixels(gsw, gsh)) return;
        SkPixmap pixels;
        if (!readback.peekPixels(&pixels)) return;
        if (!gpuFrame->readback(pixels)) return;
        readback.setImmutable();
        combined = readback.asImage();
    } else {
        combined = glowSurface->makeImageSnapshot();
    }
    if (!combined) return;

    // Upscale the combined glow onto the canvas (screen-blended, beneath the text). Working-surface
    // (0,0) is (rectPadX, rectPadY) + imageMargin left/up of the content-box top-left, which maps
    // to origin.
    SkPaint up;
    up.setBlendMode(SkBlendMode::kScreen);
    canvas->save();
    canvas->translate(static_cast<float>(originX - rectPadX - imageMargin),
                      static_cast<float>(originY - rectPadY - imageMargin));
    canvas->scale(static_cast<float>(1.0 / s), static_cast<float>(1.0 / s));
    canvas->drawImage(combined.get(), 0, 0, linear, &up);
    canvas->restore();
}

} // namespace text
} // namespace openshot
