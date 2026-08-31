#include "TextAnimationRenderer.h"

#include "../subtitle/SkiaRenderer.h"
#include "TextClipRenderer.h"
#include "TextDrawShared.h"
#include "TextGlowRenderer.h"
#include "TextGlowShader.h"

#include <skia/include/core/SkBlurTypes.h>
#include <skia/include/core/SkCanvas.h>
#include <skia/include/core/SkColor.h>
#include <skia/include/core/SkImage.h>
#include <skia/include/core/SkImageInfo.h>
#include <skia/include/core/SkMaskFilter.h>
#include <skia/include/core/SkPaint.h>
#include <skia/include/core/SkPath.h>
#include <skia/include/core/SkPathBuilder.h>
#include <skia/include/core/SkSamplingOptions.h>
#include <skia/include/core/SkSurface.h>
#include <skia/include/effects/SkImageFilters.h>

#include <algorithm>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace openshot {
namespace text {

namespace {

constexpr double DEG = M_PI / 180.0;

// CSS perspective(d) rotateX/rotateY translateZ(tz) as a 3x3 projective matrix (exact for a
// flat z=0 element). rotXFirst selects the 3D composition order. Identity if no 3D/tz.
SkMatrix concatRotation3D(const ResolvedAnimProps& props, double fontSize, bool rotXFirst) {
    const double perspectiveDistance = props.perspective() > 0.0 ? props.perspective() * fontSize : 0.0;
    const double tz = props.tz() * fontSize;
    if (props.rotateX() == 0.0 && props.rotateY() == 0.0 && tz == 0.0) return SkMatrix::I();

    const double cosRX = std::cos(props.rotateX() * DEG);
    const double cosRY = std::cos(props.rotateY() * DEG);
    const double sinRX = std::sin(props.rotateX() * DEG);
    const double sinRY = std::sin(props.rotateY() * DEG);
    const double invD = perspectiveDistance > 0.0 ? 1.0 / perspectiveDistance : 0.0;

    SkMatrix m;
    if (rotXFirst) {
        m.setAll(
            cosRY, 0.0f, static_cast<SkScalar>(sinRY * tz),
            static_cast<SkScalar>(sinRY * sinRX), static_cast<SkScalar>(cosRX), static_cast<SkScalar>(-sinRX * cosRY * tz),
            static_cast<SkScalar>(sinRY * cosRX * invD), static_cast<SkScalar>(-sinRX * invD),
            static_cast<SkScalar>(perspectiveDistance > 0.0 ? 1.0 - cosRY * cosRX * tz * invD : 1.0));
    } else {
        const double tzCosRX = tz * cosRX;
        m.setAll(
            static_cast<SkScalar>(cosRY), static_cast<SkScalar>(sinRY * sinRX), static_cast<SkScalar>(tzCosRX * sinRY),
            0.0f, static_cast<SkScalar>(cosRX), static_cast<SkScalar>(-tz * sinRX),
            static_cast<SkScalar>(sinRY * invD), static_cast<SkScalar>(-cosRY * sinRX * invD),
            static_cast<SkScalar>(perspectiveDistance > 0.0 ? 1.0 - tzCosRX * cosRY * invD : 1.0));
    }
    return m;
}

// Cap on the offscreen block texture (px). The block margin (uncapped glow/spread
// reservation) × the size scale could otherwise request a multi-thousand-px surface, which
// aborts the raster heap or makes SkSurfaces::Raster return null and silently drop the whole
// block+glow. Beyond this, the raster is downscaled (and drawn back into the same dst rect).
constexpr int WORD_BLOCK_MAX_TEXTURE_DIM = 4096;

// Flat block safety margin (shadow/stroke/glow/blur/animated-spread extent).
double blockMargin(const TextClipPaintStyle& style, double blurSigma, double extraLetterSpacing,
                   const TextClipLayout& layout) {
    const double shadowMargin = style.dropShadow.has_value()
        ? style.dropShadow->distance + style.dropShadow->blur * 2.0 : 0.0;
    const double strokeMargin = style.stroke.has_value() ? style.stroke->width * 2.0 : 0.0;
    double glowMargin = 0.0;
    if (style.glow.has_value()) {
        const double offMax = std::max(std::abs(style.glow->sourceOffX), std::abs(style.glow->sourceOffY)) * style.fontSize;
        const double halfExtent = std::max(layout.layoutWidth, layout.textHeight) / 2.0;
        glowMargin = style.glow->rayLen * (halfExtent + offMax) + offMax + GLOW_BEAM_BLUR_RATIO * style.fontSize * 3.0;
    }
    double maxLineLen = 0.0;
    for (const auto& line : layout.lines) maxLineLen = std::max(maxLineLen, static_cast<double>(clusterCount(line.text)));
    const double spreadMargin = std::abs(extraLetterSpacing) * maxLineLen;
    return std::ceil(std::max({shadowMargin, strokeMargin, glowMargin}) + (blurSigma + style.blur) * 3.0 + spreadMargin + 4.0);
}

} // namespace

// ── Transforms ───────────────────────────────────────────────────────────────

SkMatrix animationMatrix(const ResolvedAnimProps& props, double fontSize,
                         double boxWidth, double boxHeight, const AnimationTransformFlags& flags) {
    const double pivotOffsetX = flags.pivotX * boxWidth;
    const double pivotOffsetY = flags.pivotY * boxHeight;
    const double tx = props.tx() * fontSize;
    const double ty = props.ty() * fontSize;
    const double cosRX = std::cos(props.rotateX() * DEG);
    const double cosRY = std::cos(props.rotateY() * DEG);
    const double sx = props.sx();
    const double sy = props.sy();
    const double txFactor = flags.txScaled ? sx : 1.0;
    const double tyFactor = flags.txScaled ? sy : 1.0;

    SkMatrix m = SkMatrix::I();
    if (pivotOffsetX != 0.0 || pivotOffsetY != 0.0) m.preTranslate(pivotOffsetX, pivotOffsetY);

    if (flags.txInRotatedFrame) {
        if (props.rotate() != 0.0) m.preRotate(static_cast<SkScalar>(props.rotate()));
        m.preTranslate(static_cast<SkScalar>(tx * txFactor * cosRY), static_cast<SkScalar>(ty * tyFactor * cosRX));
    } else {
        if (tx != 0.0 || ty != 0.0) m.preTranslate(static_cast<SkScalar>(tx * txFactor), static_cast<SkScalar>(ty * tyFactor));
        if (props.rotate() != 0.0) m.preRotate(static_cast<SkScalar>(props.rotate()));
    }

    if (props.skewX() != 0.0 || props.skewY() != 0.0) {
        SkMatrix skew;
        skew.setAll(1.0f, static_cast<SkScalar>(std::tan(props.skewX() * DEG)), 0.0f,
                    static_cast<SkScalar>(std::tan(props.skewY() * DEG)), 1.0f, 0.0f,
                    0.0f, 0.0f, 1.0f);
        m.preConcat(skew);
    }

    if (sx != 1.0 || sy != 1.0) m.preScale(static_cast<SkScalar>(sx), static_cast<SkScalar>(sy));

    m.preConcat(concatRotation3D(props, fontSize, flags.rotXFirst));

    if (pivotOffsetX != 0.0 || pivotOffsetY != 0.0) m.preTranslate(-pivotOffsetX, -pivotOffsetY);
    return m;
}

void applyAnimationTransform(SkCanvas* canvas, const ResolvedAnimProps& props, double fontSize,
                             double boxWidth, double boxHeight, const AnimationTransformFlags& flags) {
    canvas->concat(animationMatrix(props, fontSize, boxWidth, boxHeight, flags));
}

void applyInsetClip(SkCanvas* canvas, const ResolvedAnimProps& props, double boxWidth, double boxHeight) {
    if (props.clipT() <= 0.001 && props.clipR() <= 0.001 && props.clipB() <= 0.001 && props.clipL() <= 0.001) return;
    const double clipW = boxWidth * 1.5;
    const double clipH = boxHeight * 1.5;
    canvas->clipRect(SkRect::MakeLTRB(
        static_cast<float>(-clipW / 2.0 + props.clipL() * clipW),
        static_cast<float>(-clipH / 2.0 + props.clipT() * clipH),
        static_cast<float>(clipW / 2.0 - props.clipR() * clipW),
        static_cast<float>(clipH / 2.0 - props.clipB() * clipH)),
        SkClipOp::kIntersect, true);
}

void applyPolygonClip(SkCanvas* canvas, const std::vector<std::pair<double, double>>& polygon,
                      double boxWidth, double boxHeight) {
    if (polygon.size() < 3) return;
    // M147: SkPath is immutable; build the contour with SkPathBuilder.
    SkPathBuilder builder;
    builder.moveTo(static_cast<float>((polygon[0].first - 0.5) * boxWidth),
                   static_cast<float>((polygon[0].second - 0.5) * boxHeight));
    for (size_t i = 1; i < polygon.size(); ++i) {
        builder.lineTo(static_cast<float>((polygon[i].first - 0.5) * boxWidth),
                       static_cast<float>((polygon[i].second - 0.5) * boxHeight));
    }
    builder.close();
    canvas->clipPath(builder.detach(), SkClipOp::kIntersect, true);
}

// ── Per-unit primitives ──────────────────────────────────────────────────────

void applyUnitTransform(subtitle::SkiaRenderer* renderer, SkCanvas* canvas,
                        const AnimatedUnitItem& item, double fontSize, const AnimationTransformFlags& flags) {
    // Curved: (1) translate to the arc anchor, rotate by its tangent, shift baseline -> box centre.
    if (item.curve.has_value()) {
        canvas->translate(static_cast<float>(item.curve->arcX), static_cast<float>(item.curve->arcY));
        canvas->rotate(static_cast<float>(item.curve->rotationDeg));
        canvas->translate(0.0f, static_cast<float>(-item.curve->pivotOffsetY));
    } else {
        canvas->translate(static_cast<float>(item.centerX), static_cast<float>(item.centerY));
    }
    // (2) the unit's animation transform, then (3) the inset clip — deliberately BEFORE the
    // per-glyph local placement (step 4, in drawAnimatedUnit) so it masks the unit as a whole
    // rather than each glyph separately.
    applyAnimationTransform(canvas, item.props, fontSize, item.boxWidth, item.boxHeight, flags);
    applyInsetClip(canvas, item.props, item.boxWidth, item.boxHeight);
    (void)renderer;
}

void drawAnimatedUnit(subtitle::SkiaRenderer* renderer, const AnimatedUnitItem& item,
                      double dx, double dy, const SkPaint& paint, const TextClipPaintStyle& style,
                      const EmojiPass& emoji) {
    SkCanvas* canvas = renderer->getCanvas();
    for (const AnimatedUnitGlyph& glyph : item.glyphs) {
        if (!glyph.local.has_value()) {
            drawLetter(renderer, glyph.letter, glyph.offsetX + dx, glyph.offsetY + dy, paint, style, emoji);
            continue;
        }
        // (4) multi-glyph curved unit: step this glyph back out to its own arc position inside the
        // (already animated) anchor frame, so the group moves rigidly without flattening the curve.
        const double pivotOffsetY = item.curve.has_value() ? item.curve->pivotOffsetY : 0.0;
        canvas->save();
        canvas->translate(static_cast<float>(glyph.local->dx),
                          static_cast<float>(glyph.local->dy + pivotOffsetY));
        canvas->rotate(static_cast<float>(glyph.local->rotationDeg));
        canvas->translate(0.0f, static_cast<float>(-pivotOffsetY));
        drawLetter(renderer, glyph.letter, glyph.offsetX + dx, glyph.offsetY + dy, paint, style, emoji);
        canvas->restore();
    }
}

// ── Unit model ───────────────────────────────────────────────────────────────

UnitCounts countAnimationUnits(const TextClipLayout& layout) {
    UnitCounts counts;
    for (const TextClipLine& line : layout.lines) {
        if (line.text.empty()) continue;
        bool lineHasInk = false;
        bool inWord = false;
        forEachCluster(line.text, [&](const std::string& letter, SkUnichar) {
            ++counts.chars;
            if (isSpaceGlyph(letter)) { inWord = false; return; }
            ++counts.charsDrawn;
            lineHasInk = true;
            if (!inWord) { ++counts.words; inWord = true; }   // a word never crosses a line
        });
        if (lineHasInk) ++counts.lines;
    }
    return counts;
}

void withAnimatedPaint(subtitle::SkiaRenderer* renderer, const AnimatedPaintBase& base,
                       double alphaMultiplier, double maskBlur,
                       const std::function<void(const SkPaint&)>& draw) {
    const subtitle::PaintProps props{base.color, base.opacity, base.strokeWidth, std::nullopt};
    if (alphaMultiplier >= 0.999 && maskBlur <= 0.0) {
        draw(*renderer->getPaint(props));
        return;
    }
    SkPaint paint = *renderer->getPaint(props);
    const float baseAlpha = SkColorGetA(paint.getColor()) / 255.0f;
    paint.setAlphaf(static_cast<float>(baseAlpha * alphaMultiplier));
    if (maskBlur > 0.0) {
        paint.setMaskFilter(SkMaskFilter::MakeBlur(kNormal_SkBlurStyle, static_cast<SkScalar>(maskBlur), false));
    }
    draw(paint);
}

// ── Block-mode renderer ──────────────────────────────────────────────────────

void BlockAnimationRenderer::renderBlockAnimated(
    const BlockAnimationContent& content,
    const TextClipPaintStyle& style,
    const std::optional<TextClipBackgroundStyle>& background,
    double originX, double originY, double scale,
    const TextClipAnimationFrame& animation) {
    SkCanvas* canvas = renderer->getCanvas();
    if (!canvas) return;
    ResolvedAnimProps props = animation.props;  // local copy: the perspective guard may raise it

    const double opacity = clamp01(props.opacity());
    if (opacity <= 0.0) return;

    const double fontSize = style.fontSize;
    const double paddingX = background.has_value() ? background->paddingX : 0.0;
    const double paddingY = background.has_value() ? background->paddingY : 0.0;
    const double contentWidth = content.contentWidth;
    const double contentHeight = content.contentHeight;
    const double blockWidth = contentWidth + 2.0 * paddingX;
    const double blockHeight = contentHeight + 2.0 * paddingY;
    const double centerX = originX + contentWidth / 2.0;
    const double centerY = originY + contentHeight / 2.0;

    const bool hasGlitch = (props.clipGT() < 0.999 || props.clipGB() < 0.999) && props.clipGT() < props.clipGB();
    const bool is3D = props.rotateX() != 0.0 || props.rotateY() != 0.0;

    // Perspective-singularity guard: under a steep rotateX/rotateY the far edge of the largest
    // primitive (the block texture — its margin already covers the live glow rect) can cross the
    // camera plane (projective w ≤ 0), which makes Skia project to infinity and abort. Enforce
    // perspectiveDistance ≳ that extent so it stays in front of the camera. Only ever INCREASE
    // perspective, so normal text / presets are unchanged; a very wide block just gets a flatter
    // but crash-free tilt.
    if (is3D && fontSize > 0.0) {
        const double margin = content.margin(0.0);
        const double fullExtent = std::max(blockWidth, blockHeight) + 2.0 * margin;
        const double minPerspective = fullExtent / fontSize;
        if (props.perspective() < minPerspective) props[AnimProp::perspective] = minPerspective;
    }

    canvas->save();
    canvas->translate(static_cast<float>(centerX), static_cast<float>(centerY));
    applyAnimationTransform(canvas, props, fontSize, blockWidth, blockHeight, animation.flags);
    applyInsetClip(canvas, props, blockWidth, blockHeight);
    if (animation.clipPolygon.has_value()) {
        applyPolygonClip(canvas, *animation.clipPolygon, contentWidth, contentHeight);
    }

    // 3D / scale-animating presets draw the composited block texture instead of live glyphs.
    // `forceBlockTexture` keeps a keyframed-tilt clip on this path even at the frame where tilt == 0,
    // so its (confined) shadow matches the neighbouring tilted frames instead of popping to the flat
    // full-buffer shadow.
    if (is3D || animation.forceBlockTexture || (animation.scaleAnimated && !hasGlitch)) {
        drawBlockTexture(content, paddingX, paddingY, scale, props, fontSize);
        canvas->restore();
        return;
    }

    const double blurSigma = props.blur() > 0.0 ? props.blur() * fontSize : 0.0;
    const bool needLayer = opacity < 0.999 || blurSigma > 0.0;
    if (needLayer) {
        SkPaint layerPaint;
        layerPaint.setAlphaf(static_cast<float>(opacity));
        if (blurSigma > 0.0) {
            layerPaint.setImageFilter(SkImageFilters::Blur(blurSigma, blurSigma, SkTileMode::kClamp, nullptr));
        }
        const double margin = content.margin(blurSigma);
        const SkRect bounds = SkRect::MakeLTRB(
            static_cast<float>(-blockWidth / 2.0 - margin), static_cast<float>(-blockHeight / 2.0 - margin),
            static_cast<float>(blockWidth / 2.0 + margin), static_cast<float>(blockHeight / 2.0 + margin));
        canvas->saveLayer(&bounds, &layerPaint);
    }

    // Glitch clip: a horizontal band is removed between clipGT and clipGB.
    struct Band { bool present; double lo; double hi; };
    std::vector<Band> bands;
    if (hasGlitch) {
        bands.push_back({true, -contentHeight, -contentHeight / 2.0 + props.clipGT() * contentHeight});
        bands.push_back({true, -contentHeight / 2.0 + props.clipGB() * contentHeight, contentHeight});
    } else {
        bands.push_back({false, 0.0, 0.0});
    }

    for (const Band& band : bands) {
        canvas->save();
        if (band.present) {
            canvas->clipRect(SkRect::MakeLTRB(
                static_cast<float>(-blockWidth * 2.0), static_cast<float>(band.lo),
                static_cast<float>(blockWidth * 2.0), static_cast<float>(band.hi)),
                SkClipOp::kIntersect, true);
        }
        canvas->translate(static_cast<float>(-centerX), static_cast<float>(-centerY));
        content.draw(originX, originY, BlockDrawLayer::All, false);   // live path: whole block, glow inline
        canvas->restore();
    }

    if (needLayer) canvas->restore();
    canvas->restore();
}

void BlockAnimationRenderer::drawBlockTexture(
    const BlockAnimationContent& content,
    double paddingX, double paddingY, double scale,
    const ResolvedAnimProps& props, double fontSize) {
    SkCanvas* canvas = renderer->getCanvas();
    const double blockWidth = content.contentWidth + 2.0 * paddingX;
    const double blockHeight = content.contentHeight + 2.0 * paddingY;
    const double margin = content.margin(0.0);

    const double dstWidth = blockWidth + 2.0 * margin;
    const double dstHeight = blockHeight + 2.0 * margin;
    // Downscale the raster (never the dst rect) when it would exceed the texture cap, so an
    // extreme size/margin can't abort the heap or silently drop the block. The dst rect below is
    // unchanged, so this only lowers the texture resolution, keeping geometry identical.
    double renderScale = scale;
    const double fullMaxDim = std::max(dstWidth, dstHeight) * scale;
    if (fullMaxDim > WORD_BLOCK_MAX_TEXTURE_DIM)
        renderScale = scale * (WORD_BLOCK_MAX_TEXTURE_DIM / fullMaxDim);

    // Sub-pixel-exact blit. The texture is an integer number of pixels but the dst rect is
    // fractional (a content box of 768 x 100.8 becomes a 776 x 108.8 rect drawn from a 776 x 109
    // raster, centred on a fractional offset), so drawImageRect resamples the block bilinearly —
    // it costs the glyph edges ~1px of softness for no geometric gain. When the block carries no
    // transform beyond a translation (a resting frame: identity animation props, no tilt, no
    // rotation — yet the canvas centre can still land on a half pixel) the round-trip can be made
    // an exact copy instead: fold the destination's sub-pixel offset into the BAKE, then blit on
    // whole device pixels with nearest sampling. The glyphs land at exactly the same device
    // position as before — the anti-aliasing simply happens once, while drawing them, instead of
    // being smeared afterwards. Any real scale/rotation/3D (i.e. every frame this texture path
    // exists for) keeps the resampling blit, where it is doing genuine work.
    double bakeOffsetX = 0.0, bakeOffsetY = 0.0;
    double blitLocalX = 0.0, blitLocalY = 0.0;
    bool exactBlit = false;
    if (canvas && renderScale == 1.0) {
        const SkMatrix ctm = canvas->getLocalToDeviceAs3x3();
        if (ctm.isTranslate()) {
            const double deviceLeft = -dstWidth / 2.0 + ctm.getTranslateX();
            const double deviceTop = -dstHeight / 2.0 + ctm.getTranslateY();
            bakeOffsetX = deviceLeft - std::floor(deviceLeft);
            bakeOffsetY = deviceTop - std::floor(deviceTop);
            // Places texture pixel (0,0) on the whole device pixel floor(deviceLeft/Top): the
            // bake offset above puts the content back at its exact sub-pixel place inside it.
            blitLocalX = -dstWidth / 2.0 - bakeOffsetX;
            blitLocalY = -dstHeight / 2.0 - bakeOffsetY;
            exactBlit = true;
        }
    }

    // The bake offset shifts the content within the texture, so the texture grows to cover it.
    const int texWidth = std::max(2, static_cast<int>(std::ceil(dstWidth * renderScale + bakeOffsetX)));
    const int texHeight = std::max(2, static_cast<int>(std::ceil(dstHeight * renderScale + bakeOffsetY)));

    const double opacity = clamp01(props.opacity());
    const double motionBlurSigma = props.blur() > 0.0 ? props.blur() * fontSize : 0.0;

    // Bake one z-band of the flat block into an offscreen image, then let the 3D perspective warp it.
    // The shadow stays baked so the warp tilts it with the glyphs (matching the front end, which
    // transforms one composited flat layer).
    auto bakeLayer = [&](BlockDrawLayer layer) -> sk_sp<SkImage> {
        sk_sp<SkSurface> surface = SkSurfaces::Raster(SkImageInfo::MakeN32Premul(texWidth, texHeight));
        if (!surface) return nullptr;
        SkCanvas* offscreen = surface->getCanvas();
        offscreen->clear(SK_ColorTRANSPARENT);
        offscreen->save();
        offscreen->scale(static_cast<float>(renderScale), static_cast<float>(renderScale));
        renderer->renderToCanvas(offscreen, [&] {
            content.draw(margin + paddingX + bakeOffsetX, margin + paddingY + bakeOffsetY, layer, false);
        });
        offscreen->restore();
        return surface->makeImageSnapshot();
    };
    auto drawLayerImage = [&](const sk_sp<SkImage>& image) {
        if (!image) return;
        SkPaint paint;
        paint.setAntiAlias(true);
        paint.setAlphaf(static_cast<float>(opacity));
        if (motionBlurSigma > 0.0) {
            paint.setImageFilter(SkImageFilters::Blur(motionBlurSigma, motionBlurSigma, SkTileMode::kClamp, nullptr));
        }
        if (exactBlit) {
            // Integer device translate + nearest sampling = a straight copy of the baked pixels.
            canvas->drawImage(image.get(), static_cast<float>(blitLocalX), static_cast<float>(blitLocalY),
                              SkSamplingOptions(), &paint);
            return;
        }
        // Source rect = the part of the raster the block was actually baked into, NOT the whole
        // (ceil-rounded) texture: the block occupies dstWidth x dstHeight of it, so mapping the
        // full texture onto the dst rect squeezed the content by the rounding slack (~0.2px here)
        // and biased its position. Sampling exactly what was baked makes the src->dst mapping 1:1,
        // leaving only the transform the frame actually asks for.
        canvas->drawImageRect(
            image.get(),
            SkRect::MakeWH(static_cast<float>(std::min<double>(dstWidth * renderScale, texWidth)),
                           static_cast<float>(std::min<double>(dstHeight * renderScale, texHeight))),
            SkRect::MakeXYWH(static_cast<float>(-dstWidth / 2.0), static_cast<float>(-dstHeight / 2.0),
                             static_cast<float>(dstWidth), static_cast<float>(dstHeight)),
            SkSamplingOptions(SkFilterMode::kLinear, SkMipmapMode::kLinear),
            &paint, SkCanvas::kStrict_SrcRectConstraint);
    };

    if (!content.hasGlow) {
        // Fast path (no glow): one texture holds the whole block. Byte-identical to before.
        drawLayerImage(bakeLayer(BlockDrawLayer::All));
        return;
    }

    // Glow present: bake the block in TWO z-layers and composite the LIVE glow between them, so the
    // flat order (background + shadow → glow → stroke + fill) is preserved under the tilt. Previously
    // the glow was drawn beneath the whole single texture, sinking it under the background/shadow.
    drawLayerImage(bakeLayer(BlockDrawLayer::BelowGlow));                 // background + shadow
    content.drawGlow(-dstWidth / 2.0 + margin + paddingX, -dstHeight / 2.0 + margin + paddingY, opacity);
    drawLayerImage(bakeLayer(BlockDrawLayer::AboveGlow));                 // stroke + fill
}

// ── Unit-mode renderer ───────────────────────────────────────────────────────

namespace {

// One glyph accumulated while grouping a unit, before the unit box is known.
struct PendingGlyph {
    std::string letter;
    double x = 0.0;         // draw origin (baseline-left), layout coordinates
    double advance = 0.0;
};

// Grouping granularity -> whether a unit is flushed on every glyph / on spaces / at end of line.
bool isPerGlyphGranularity(UnitGranularity g) { return g == UnitGranularity::CHAR; }

// Flat text: group the laid-out glyphs into stagger units and resolve each unit's props + box.
// Glyphs are laid out FIRST and grouped SECOND — the granularity only decides which glyphs share
// one stagger index and one pivot box, so a `word` preset is a `char` preset whose glyphs were
// bundled together. Spaces and empty units are never emitted, so a unit index always addresses a
// unit that actually draws.
std::vector<AnimatedUnitItem> collectAnimatedUnits(
    const TextClipLayout& layout, const TextClipPaintStyle& style,
    double originX, double originY, const TextClipAnimationFrame& animation) {
    std::vector<AnimatedUnitItem> items;
    const UnitGranularity granularity = animation.granularity;
    const double firstBaselineY = originY + layout.firstLineAscent;
    int slot = 0;          // char SLOT position — keeps counting through spaces
    int drawnIndex = 0;    // position among the glyphs that actually draw
    int unitOrdinal = 0;   // consecutive index over the emitted word / line units

    std::vector<PendingGlyph> pending;

    for (size_t li = 0; li < layout.lines.size(); ++li) {
        const TextClipLine& line = layout.lines[li];
        if (line.text.empty()) continue;
        const double baselineY = firstBaselineY + static_cast<double>(li) * layout.lineHeight;
        double cursor = originX + getLineStartX(line, layout, style.alignment, 0.0);

        // Emit the accumulated glyphs as one unit. `index` drives the default stagger order,
        // `ordinal` is the index a NAMED order is looked up by (see UnitCounts).
        auto flush = [&](int index, int ordinal) {
            if (pending.empty()) return;
            const ResolvedAnimProps props = animation.getUnitProps(index, ordinal);
            const double opacity = clamp01(props.opacity());
            if (opacity > 0.0) {   // skip the entire unit when its opacity clamps to 0
                // Interior spaces count towards the box; leading / trailing ones don't.
                const double left  = pending.front().x;
                const double right = pending.back().x + pending.back().advance;
                AnimatedUnitItem item;
                item.centerX = (left + right) / 2.0;
                item.centerY = baselineY - (line.ascent - line.descent) / 2.0;
                item.boxWidth = right - left;
                item.boxHeight = line.ascent + line.descent;   // ink box of the line the unit sits on
                item.props = props;
                item.opacity = opacity;
                item.glyphs.reserve(pending.size());
                for (const PendingGlyph& g : pending) {
                    item.glyphs.push_back(AnimatedUnitGlyph{
                        g.letter, g.x - item.centerX, baselineY - item.centerY, std::nullopt});
                }
                items.push_back(std::move(item));
            }
            pending.clear();
        };

        size_t i = 0;
        forEachCluster(line.text, [&](const std::string& letter, SkUnichar) {
            const double advance = i < line.letterAdvances.size() ? line.letterAdvances[i] : 0.0;
            const int slotIndex = slot++;
            if (isSpaceGlyph(letter)) {
                // A word never crosses a space (nor a line).
                if (granularity == UnitGranularity::WORD && !pending.empty()) {
                    const int o = unitOrdinal++;
                    flush(o, o);
                }
            } else {
                const int ordinal = drawnIndex++;
                pending.push_back(PendingGlyph{letter, cursor, advance});
                // char granularity: the unit index is the SLOT position, the ordinal the drawn one.
                if (isPerGlyphGranularity(granularity)) flush(slotIndex, ordinal);
            }
            cursor += advance;
            ++i;
        });
        // End of line closes any open word unit, and the line unit.
        if (!pending.empty()) { const int o = unitOrdinal++; flush(o, o); }
    }
    return items;
}

// Curved text: same grouping, but a multi-glyph unit pivots about a single ANCHOR point on the arc
// and each of its glyphs is stepped back out to its own arc position AFTER the animation transform.
std::vector<AnimatedUnitItem> collectCurvedAnimatedUnits(
    const CurvedTextGeometry& geometry, const TextClipLine& line,
    double originX, double originY, const TextClipAnimationFrame& animation) {
    std::vector<AnimatedUnitItem> items;
    const UnitGranularity granularity = animation.granularity;
    const double pivotOffsetY = (line.ascent - line.descent) / 2.0;
    const double boxHeight = line.ascent + line.descent;
    int slot = 0;
    int drawnIndex = 0;
    int unitOrdinal = 0;

    std::vector<const CurvedGlyphPlacement*> pending;

    auto flush = [&](int index, int ordinal) {
        if (pending.empty()) return;
        const ResolvedAnimProps props = animation.getUnitProps(index, ordinal);
        const double opacity = clamp01(props.opacity());
        if (opacity > 0.0) {
            // Anchor = the middle placement, or the mean of the two middle ones. Position and
            // tangent both vary affinely along the arc, so this sits at the unit's arc midpoint to
            // within half a glyph advance.
            const double middle = static_cast<double>(pending.size() - 1) / 2.0;
            const CurvedGlyphPlacement& low = *pending[static_cast<size_t>(std::floor(middle))];
            const CurvedGlyphPlacement& high = *pending[static_cast<size_t>(std::ceil(middle))];
            const double anchorCx = (&low == &high) ? low.cx : (low.cx + high.cx) / 2.0;
            const double anchorCy = (&low == &high) ? low.cy : (low.cy + high.cy) / 2.0;
            const double anchorRot = (&low == &high) ? low.rotation : (low.rotation + high.rotation) / 2.0;

            double arcLength = 0.0;
            for (const CurvedGlyphPlacement* p : pending) arcLength += p->advance;

            AnimatedUnitItem item;
            item.centerX = 0.0;
            item.centerY = 0.0;
            item.boxWidth = arcLength;   // the unit's ARC length (the chord it encloses is shorter)
            item.boxHeight = boxHeight;
            item.props = props;
            item.opacity = opacity;
            item.curve = CurvedUnitAnchor{
                originX + anchorCx, originY + anchorCy, anchorRot * 180.0 / M_PI, pivotOffsetY};

            const bool multi = pending.size() > 1;
            const double cosA = std::cos(anchorRot);
            const double sinA = std::sin(anchorRot);
            item.glyphs.reserve(pending.size());
            for (const CurvedGlyphPlacement* p : pending) {
                AnimatedUnitGlyph glyph;
                glyph.letter = p->letter;
                // Offsets relative to the (own) box centre, so the shared letter-drawing code
                // works unchanged inside the rotated frame.
                glyph.offsetX = -p->advance / 2.0;
                glyph.offsetY = pivotOffsetY;
                if (multi) {
                    // Local placement measured in the anchor's ROTATED frame.
                    const double ddx = p->cx - anchorCx;
                    const double ddy = p->cy - anchorCy;
                    glyph.local = CurvedGlyphLocal{
                        cosA * ddx + sinA * ddy,
                        -sinA * ddx + cosA * ddy,
                        (p->rotation - anchorRot) * 180.0 / M_PI};
                }
                item.glyphs.push_back(std::move(glyph));
            }
            items.push_back(std::move(item));
        }
        pending.clear();
    };

    for (const CurvedGlyphPlacement& placement : geometry.placements) {
        const int slotIndex = slot++;
        if (isSpaceGlyph(placement.letter)) {
            if (granularity == UnitGranularity::WORD && !pending.empty()) {
                const int o = unitOrdinal++;
                flush(o, o);
            }
            continue;
        }
        const int ordinal = drawnIndex++;
        pending.push_back(&placement);
        if (isPerGlyphGranularity(granularity)) flush(slotIndex, ordinal);
    }
    if (!pending.empty()) { const int o = unitOrdinal++; flush(o, o); }
    return items;
}

} // namespace

void UnitAnimationRenderer::drawAnimatedUnitItems(
    std::vector<AnimatedUnitItem>& items, const TextClipPaintStyle& style,
    const AnimationTransformFlags& flags,
    double contentWidth, double contentHeight, double originX, double originY, bool skipGlow,
    BlockDrawLayer layer) {
    SkCanvas* canvas = renderer->getCanvas();
    const double fontSize = style.fontSize;
    if (items.empty()) return;

    // z-band gating for the 3D two-texture split (see BlockDrawLayer).
    const bool drawBelow = layer != BlockDrawLayer::AboveGlow;   // shadow (background is in caller)
    const bool drawAbove = layer != BlockDrawLayer::BelowGlow;   // stroke + fill
    const bool drawGlow  = layer == BlockDrawLayer::All;         // glow only in the single/all pass

    if (drawBelow && style.dropShadow.has_value()) {
        const auto& shadow = *style.dropShadow;
        const double radians = shadow.angle * DEG;
        const double dx = std::cos(radians) * shadow.distance;
        const double dy = std::sin(radians) * shadow.distance;

        for (const auto& item : items) {
            const double animBlur = item.props.blur() > 0.0 ? item.props.blur() * fontSize : 0.0;
            // Match the flat/curved shadow paths: apply the CPU-vs-GPU blur calibration.
            const double combinedBlur =
                std::sqrt(shadow.blur * shadow.blur + animBlur * animBlur + style.blur * style.blur)
                * SHADOW_BLUR_SIGMA_SCALE;
            canvas->save();
            applyUnitTransform(renderer, canvas, item, fontSize, flags);
            if (style.stroke.has_value()) {
                withAnimatedPaint(renderer, {shadow.color, shadow.opacity, style.stroke->width}, item.opacity, combinedBlur,
                    [&](const SkPaint& paint) {
                        drawAnimatedUnit(renderer, item, dx, dy, paint, style, {EmojiPass::Kind::Skip});
                    });
            }
            withAnimatedPaint(renderer, {shadow.color, shadow.opacity, std::nullopt}, item.opacity, combinedBlur,
                [&](const SkPaint& paint) {
                    drawAnimatedUnit(renderer, item, dx, dy, paint, style,
                                     {EmojiPass::Kind::Silhouette, combinedBlur});
                });
            canvas->restore();
        }
    }

    if (drawGlow && style.glow.has_value() && !skipGlow) {
        glowRenderer->drawAnimatedGlowLayer(items, style, *style.glow, contentWidth, contentHeight, originX, originY, flags);
    }

    if (drawAbove && style.stroke.has_value()) {
        const auto& stroke = *style.stroke;
        auto drawStrokes = [&] {
            for (const auto& item : items) {
                const double animBlur = item.props.blur() > 0.0 ? item.props.blur() * fontSize : 0.0;
                canvas->save();
                applyUnitTransform(renderer, canvas, item, fontSize, flags);
                withAnimatedPaint(renderer, {stroke.color, 1.0, stroke.width}, item.opacity, combineBlur(animBlur, calibratedTextBlur(style.blur)),
                    [&](const SkPaint& paint) {
                        drawAnimatedUnit(renderer, item, 0.0, 0.0, paint, style, {EmojiPass::Kind::Skip});
                    });
                canvas->restore();
            }
        };
        // Gradient stroke: composite the per-glyph coverage then mask a block-space ramp with SrcIn.
        if (stroke.gradient.has_value()) {
            const PaintGradient g = gradientFill(*stroke.gradient, originX, originY, contentWidth, contentHeight);
            withGradientCoverage(renderer, g, SkRect::MakeEmpty(), drawStrokes);
        } else {
            drawStrokes();
        }
    }

    const double coreOpacity = style.glow.has_value() ? GLOW_CORE_TEXT_OPACITY : 1.0;
    const double coreSoftBlur = style.glow.has_value() ? GLOW_CORE_TEXT_BLUR_RATIO * fontSize : 0.0;
    auto drawFills = [&] {
        for (const auto& item : items) {
            const double animBlur = item.props.blur() > 0.0 ? item.props.blur() * fontSize : 0.0;
            canvas->save();
            applyUnitTransform(renderer, canvas, item, fontSize, flags);
            const double fillBlur = combineBlur(combineBlur(animBlur, calibratedTextBlur(style.blur)), coreSoftBlur);
            withAnimatedPaint(renderer, {style.color, coreOpacity, std::nullopt}, item.opacity, fillBlur,
                [&](const SkPaint& paint) {
                    drawAnimatedUnit(renderer, item, 0.0, 0.0, paint, style,
                                     {EmojiPass::Kind::Colour, fillBlur});
                });
            canvas->restore();
        }
    };
    if (!drawAbove) return;   // BelowGlow layer: shadow only, no stroke / fill
    if (style.colorGradient.has_value()) {
        const PaintGradient g = gradientFill(*style.colorGradient, originX, originY, contentWidth, contentHeight);
        withGradientCoverage(renderer, g, SkRect::MakeEmpty(), drawFills);
    } else {
        drawFills();
    }
}

void UnitAnimationRenderer::renderUnitAnimated(
    const TextClipLayout& layout, const TextClipPaintStyle& style,
    const std::optional<TextClipBackgroundStyle>& background,
    double originX, double originY, const TextClipAnimationFrame& animation, bool skipGlow,
    BlockDrawLayer layer) {
    if (layer != BlockDrawLayer::AboveGlow && background.has_value()) {
        drawBackgroundRect(renderer, *background, originX, originY, layout.layoutWidth, layout.textHeight);
    }
    std::vector<AnimatedUnitItem> items = collectAnimatedUnits(layout, style, originX, originY, animation);
    drawAnimatedUnitItems(items, style, animation.flags, layout.layoutWidth, layout.textHeight, originX, originY, skipGlow, layer);
}

void UnitAnimationRenderer::renderCurvedUnitAnimated(
    const CurvedTextGeometry& geometry, const TextClipLine& line, const TextClipPaintStyle& style,
    const std::optional<TextClipBackgroundStyle>& background,
    double originX, double originY, const TextClipAnimationFrame& animation, bool skipGlow,
    BlockDrawLayer layer) {
    if (layer != BlockDrawLayer::AboveGlow && background.has_value()) {
        drawBackgroundRect(renderer, *background, originX, originY, geometry.width, geometry.height);
    }
    std::vector<AnimatedUnitItem> items = collectCurvedAnimatedUnits(geometry, line, originX, originY, animation);
    drawAnimatedUnitItems(items, style, animation.flags, geometry.width, geometry.height, originX, originY, skipGlow, layer);
}

void UnitAnimationRenderer::drawUnitAnimatedGlowOnly(
    const TextClipLayout& layout, const TextClipPaintStyle& style,
    double originX, double originY, const TextClipAnimationFrame& animation) {
    if (!style.glow.has_value()) return;
    std::vector<AnimatedUnitItem> items = collectAnimatedUnits(layout, style, originX, originY, animation);
    if (items.empty()) return;
    glowRenderer->drawAnimatedGlowLayer(items, style, *style.glow, layout.layoutWidth, layout.textHeight,
                                        originX, originY, animation.flags);
}

void UnitAnimationRenderer::drawCurvedUnitAnimatedGlowOnly(
    const CurvedTextGeometry& geometry, const TextClipLine& line, const TextClipPaintStyle& style,
    double originX, double originY, const TextClipAnimationFrame& animation) {
    if (!style.glow.has_value()) return;
    std::vector<AnimatedUnitItem> items = collectCurvedAnimatedUnits(geometry, line, originX, originY, animation);
    if (items.empty()) return;
    glowRenderer->drawAnimatedGlowLayer(items, style, *style.glow, geometry.width, geometry.height,
                                        originX, originY, animation.flags);
}

// ── Top-level dispatch ───────────────────────────────────────────────────────

namespace {

void renderBlockAnimatedFlat(
    subtitle::SkiaRenderer* renderer, const TextClipLayout& layout, const TextClipPaintStyle& style,
    const std::optional<TextClipBackgroundStyle>& background, double originX, double originY, double scale,
    const TextClipAnimationFrame& animation) {
    const double extraLetterSpacing = animation.props.letterSpacing() * style.fontSize;
    TextGlowRenderer glow(renderer);
    BlockAnimationContent content;
    content.contentWidth = layout.layoutWidth;
    content.contentHeight = layout.textHeight;
    content.hasGlow = style.glow.has_value();
    content.margin = [&style, &layout, extraLetterSpacing](double blurSigma) {
        return blockMargin(style, blurSigma, extraLetterSpacing, layout);
    };
    content.draw = [renderer, &layout, &style, &background, extraLetterSpacing](double ox, double oy, BlockDrawLayer layer, bool skipShadow) {
        renderLayout(layout, style, background, ox, oy, renderer, extraLetterSpacing, /*skipGlow*/ false, skipShadow, layer);
    };
    content.drawGlow = [&glow, &layout, &style, extraLetterSpacing](double ox, double oy, double opacityMul) {
        if (style.glow.has_value()) {
            glow.drawGlowLayer(layout, style, *style.glow, ox, oy, nullptr, opacityMul, extraLetterSpacing);
        }
    };
    content.drawShadow = [renderer, &layout, &style, extraLetterSpacing](double ox, double oy) {
        renderShadowLayer(layout, style, ox, oy, renderer, extraLetterSpacing);
    };
    BlockAnimationRenderer(renderer).renderBlockAnimated(content, style, background, originX, originY, scale, animation);
}

void renderBlockAnimatedCurved(
    subtitle::SkiaRenderer* renderer, const CurvedTextGeometry& geometry, const TextClipLayout& layout,
    const TextClipPaintStyle& style, const std::optional<TextClipBackgroundStyle>& background,
    double originX, double originY, double scale, const TextClipAnimationFrame& animation) {
    TextGlowRenderer glow(renderer);
    CurvedTextPainter painter(renderer, &glow);
    BlockAnimationContent content;
    content.contentWidth = geometry.width;
    content.contentHeight = geometry.height;
    content.hasGlow = style.glow.has_value();
    content.margin = [&style, &geometry](double blurSigma) {
        return curvedBlockMargin(style, geometry, blurSigma);
    };
    content.draw = [&painter, &geometry, &layout, &style, &background](double ox, double oy, BlockDrawLayer layer, bool skipShadow) {
        painter.drawCurvedStatic(geometry, layout, style, background, ox, oy, /*skipGlow*/ false, skipShadow, layer);
    };
    content.drawGlow = [&glow, &layout, &style, &geometry](double ox, double oy, double opacityMul) {
        if (style.glow.has_value()) {
            glow.drawGlowLayer(layout, style, *style.glow, ox, oy, &geometry, opacityMul);
        }
    };
    content.drawShadow = [&painter, &geometry, &style](double ox, double oy) {
        painter.drawCurvedShadowOnly(geometry, style, ox, oy);
    };
    BlockAnimationRenderer(renderer).renderBlockAnimated(content, style, background, originX, originY, scale, animation);
}

// Unit-mode animation composed with a static 3D tilt: paint the per-unit animation FLAT into
// the block-mode composited texture (skipGlow), then draw that whole texture under the block's 3D
// rotation — reusing the block texture+3D core with a buildStatic3DFrame frame. The glow is drawn
// live afterwards under the same transform (matches the block path).
void renderUnitAnimated3D(
    subtitle::SkiaRenderer* renderer, const TextClipLayout& layout, const TextClipPaintStyle& style,
    const std::optional<TextClipBackgroundStyle>& background, double originX, double originY,
    double scale, const TextClipAnimationFrame& animation) {
    if (!animation.static3D.has_value()) return;
    TextGlowRenderer glow(renderer);
    BlockAnimationContent content;
    content.contentWidth = layout.layoutWidth;
    content.contentHeight = layout.textHeight;
    content.hasGlow = style.glow.has_value();
    content.margin = [&style, &layout](double blurSigma) {
        return blockMargin(style, blurSigma, 0.0, layout);
    };
    content.draw = [renderer, &glow, &layout, &style, &background, &animation](double ox, double oy, BlockDrawLayer layer, bool /*skipShadow*/) {
        // Unit + 3D keeps the shadow baked into the texture (no live drawShadow set below).
        UnitAnimationRenderer(renderer, &glow).renderUnitAnimated(layout, style, background, ox, oy, animation, /*skipGlow*/ false, layer);
    };
    content.drawGlow = [renderer, &glow, &layout, &style, &animation](double ox, double oy, double /*opacityMul*/) {
        UnitAnimationRenderer(renderer, &glow).drawUnitAnimatedGlowOnly(layout, style, ox, oy, animation);
    };
    const TextClipAnimationFrame tiltFrame = buildStatic3DFrame(animation.static3D->first, animation.static3D->second);
    BlockAnimationRenderer(renderer).renderBlockAnimated(content, style, background, originX, originY, scale, tiltFrame);
}

void renderCurvedUnitAnimated3D(
    subtitle::SkiaRenderer* renderer, const CurvedTextGeometry& geometry, const TextClipLine& line,
    const TextClipLayout& layout, const TextClipPaintStyle& style,
    const std::optional<TextClipBackgroundStyle>& background, double originX, double originY,
    double scale, const TextClipAnimationFrame& animation) {
    if (!animation.static3D.has_value()) return;
    TextGlowRenderer glow(renderer);
    BlockAnimationContent content;
    content.contentWidth = geometry.width;
    content.contentHeight = geometry.height;
    content.hasGlow = style.glow.has_value();
    content.margin = [&style, &geometry](double blurSigma) {
        return curvedBlockMargin(style, geometry, blurSigma);
    };
    content.draw = [renderer, &glow, &geometry, &line, &style, &background, &animation](double ox, double oy, BlockDrawLayer layer, bool /*skipShadow*/) {
        UnitAnimationRenderer(renderer, &glow).renderCurvedUnitAnimated(geometry, line, style, background, ox, oy, animation, /*skipGlow*/ false, layer);
    };
    content.drawGlow = [renderer, &glow, &geometry, &line, &style, &animation](double ox, double oy, double /*opacityMul*/) {
        UnitAnimationRenderer(renderer, &glow).drawCurvedUnitAnimatedGlowOnly(geometry, line, style, ox, oy, animation);
    };
    (void)layout;
    const TextClipAnimationFrame tiltFrame = buildStatic3DFrame(animation.static3D->first, animation.static3D->second);
    BlockAnimationRenderer(renderer).renderBlockAnimated(content, style, background, originX, originY, scale, tiltFrame);
}

} // namespace

TextClipAnimationFrame buildStatic3DFrame(double tiltX, double tiltY) {
    TextClipAnimationFrame frame;
    frame.mode = AnimationMode::BLOCK;
    frame.props = identityProps();
    frame.props[AnimProp::rotateX] = tiltX;
    frame.props[AnimProp::rotateY] = tiltY;
    frame.props[AnimProp::perspective] = STATIC_ROTATION_PERSPECTIVE;
    frame.scaleAnimated = false;
    return frame;
}

void composeStatic3DIntoBlockFrame(TextClipAnimationFrame& frame, double tiltX, double tiltY) {
    frame.props[AnimProp::rotateX] = frame.props.rotateX() + tiltX;
    frame.props[AnimProp::rotateY] = frame.props.rotateY() + tiltY;
    // Supply a perspective only if the preset doesn't animate its own, so flat presets read as 3D.
    if (frame.props.perspective() <= 0.0) {
        frame.props[AnimProp::perspective] = STATIC_ROTATION_PERSPECTIVE;
    }
}

void renderTextFrame(
    const TextClipLayout& layout,
    const TextClipPaintStyle& paint,
    const std::optional<TextClipBackgroundStyle>& background,
    double originX, double originY, double scale,
    const std::optional<TextClipAnimationFrame>& animation,
    subtitle::SkiaRenderer* renderer) {
    SkCanvas* canvas = renderer->getCanvas();
    if (!canvas) return;

    const bool hasUnitAnim = animation.has_value() && animation->mode == AnimationMode::UNIT;
    const bool hasBlockAnim = animation.has_value() && animation->mode == AnimationMode::BLOCK;
    // Unit mode has no block-level transform to hang a tilt on, so a unit frame carrying a static
    // 3D tilt is baked flat then tilted as one piece (renderUnitAnimated3D). Block/flat frames carry
    // the tilt folded into props and flow through the normal texture+3D block path.
    const bool unitStatic3D = hasUnitAnim && animation->static3D.has_value();

    if (paint.curveAngle.has_value()) {
        const CurvedTextGeometry geometry = curvedGeometryForLayout(layout, paint);
        TextGlowRenderer glow(renderer);
        if (hasUnitAnim) {
            const TextClipLine* line = nullptr;
            for (const auto& l : layout.lines) { if (!l.text.empty()) { line = &l; break; } }
            if (!line) return;
            if (unitStatic3D) {
                renderCurvedUnitAnimated3D(renderer, geometry, *line, layout, paint, background, originX, originY, scale, *animation);
            } else {
                UnitAnimationRenderer(renderer, &glow).renderCurvedUnitAnimated(
                    geometry, *line, paint, background, originX, originY, *animation);
            }
        } else if (hasBlockAnim) {
            renderBlockAnimatedCurved(renderer, geometry, layout, paint, background, originX, originY, scale, *animation);
        } else {
            CurvedTextPainter(renderer, &glow).drawCurvedStatic(geometry, layout, paint, background, originX, originY);
        }
        return;
    }

    if (hasUnitAnim) {
        if (unitStatic3D) {
            renderUnitAnimated3D(renderer, layout, paint, background, originX, originY, scale, *animation);
        } else {
            TextGlowRenderer glow(renderer);
            UnitAnimationRenderer(renderer, &glow).renderUnitAnimated(layout, paint, background, originX, originY, *animation);
        }
    } else if (hasBlockAnim) {
        renderBlockAnimatedFlat(renderer, layout, paint, background, originX, originY, scale, *animation);
    } else {
        renderLayout(layout, paint, background, originX, originY, renderer);
    }
}

// ── Animated frame extent ────────────────────────────────────────────────────

AnimatedExtent computeAnimatedExtent(
    const TextClipLayout& layout, const TextClipPaintStyle& paint,
    double contentWidth, double contentHeight,
    const AnimationTimeline& timeline, const AnimationPresetMap& presets, const UnitCounts& counts) {
    const double fontSize = paint.fontSize;
    double halfW = contentWidth / 2.0;
    double halfH = contentHeight / 2.0;

    // Nominal unit box used to bound unit-mode per-unit transforms. A word / line unit is far wider
    // than one glyph, so the box is sized from the widest unit of that granularity — otherwise the
    // frame buffer would be too small for a rotating/scaling word and clip it.
    const double glyphBoxW = std::max(fontSize, contentHeight);
    const double glyphBoxH = layout.lines.empty() ? fontSize : std::max(fontSize, layout.lines.front().ascent + layout.lines.front().descent);

    double widestWord = 0.0;
    double widestLine = 0.0;
    for (const TextClipLine& line : layout.lines) {
        if (line.text.empty()) continue;
        double lineRun = 0.0, wordRun = 0.0;
        size_t i = 0;
        forEachCluster(line.text, [&](const std::string& letter, SkUnichar) {
            const double advance = i < line.letterAdvances.size() ? line.letterAdvances[i] : 0.0;
            lineRun += advance;
            if (isSpaceGlyph(letter)) { widestWord = std::max(widestWord, wordRun); wordRun = 0.0; }
            else wordRun += advance;
            ++i;
        });
        widestWord = std::max(widestWord, wordRun);
        widestLine = std::max(widestLine, lineRun);
    }
    auto unitBoxW = [&](UnitGranularity g) {
        switch (g) {
            case UnitGranularity::WORD: return std::max(glyphBoxW, widestWord);
            case UnitGranularity::LINE: return std::max(glyphBoxW, widestLine);
            case UnitGranularity::CHAR: break;
        }
        return glyphBoxW;
    };

    auto mapBoxHalf = [](const SkMatrix& m, double boxW, double boxH, double& outHalfW, double& outHalfH) {
        SkRect r = SkRect::MakeLTRB(static_cast<float>(-boxW / 2.0), static_cast<float>(-boxH / 2.0),
                                    static_cast<float>(boxW / 2.0), static_cast<float>(boxH / 2.0));
        SkRect mapped;
        m.mapRect(&mapped, r);
        outHalfW = std::max(std::abs(static_cast<double>(mapped.fLeft)), std::abs(static_cast<double>(mapped.fRight)));
        outHalfH = std::max(std::abs(static_cast<double>(mapped.fTop)), std::abs(static_cast<double>(mapped.fBottom)));
    };

    constexpr int SAMPLES = 24;
    for (int s = 0; s <= SAMPLES; ++s) {
        const double elapsed = timeline.textDuration * (static_cast<double>(s) / SAMPLES);
        const FramePlan plan = planFrame(elapsed, timeline, counts, presets);
        if (!plan.presetId.has_value()) continue;
        const auto it = presets.find(*plan.presetId);
        if (it == presets.end()) continue;
        const auto frame = buildAnimationFrame(plan, it->second, elapsed);
        if (!frame.has_value()) continue;

        if (frame->mode == AnimationMode::BLOCK) {
            const SkMatrix m = animationMatrix(frame->props, fontSize, contentWidth, contentHeight, frame->flags);
            double hw, hh;
            mapBoxHalf(m, contentWidth, contentHeight, hw, hh);
            const double blurExtra = frame->props.blur() > 0.0 ? frame->props.blur() * fontSize * 3.0 : 0.0;
            halfW = std::max(halfW, hw + blurExtra);
            halfH = std::max(halfH, hh + blurExtra);
        } else if (frame->mode == AnimationMode::UNIT) {
            // Sample the first / middle / last unit, addressing each in BOTH index spaces so a
            // named order (looked up by ordinal, in drawn space) is probed correctly too.
            const int nIndex = counts.forGranularity(frame->granularity);
            if (nIndex <= 0) continue;
            const int nOrdinal = frame->granularity == UnitGranularity::CHAR ? counts.charsDrawn : nIndex;
            const double boxW = unitBoxW(frame->granularity);
            const int idxs[3] = {0, nIndex / 2, nIndex - 1};
            for (int idx : idxs) {
                const int i = std::clamp(idx, 0, nIndex - 1);
                const int ordinal = nOrdinal > 0 ? std::min(i, nOrdinal - 1) : 0;
                const ResolvedAnimProps props = frame->getUnitProps(i, ordinal);
                const SkMatrix m = animationMatrix(props, fontSize, boxW, glyphBoxH, frame->flags);
                double hw, hh;
                mapBoxHalf(m, boxW, glyphBoxH, hw, hh);
                const double blurExtra = props.blur() > 0.0 ? props.blur() * fontSize * 3.0 : 0.0;
                halfW = std::max(halfW, contentWidth / 2.0 + hw + blurExtra);
                halfH = std::max(halfH, contentHeight / 2.0 + hh + blurExtra);
            }
        }
    }

    return {halfW, halfH};
}

} // namespace text
} // namespace openshot
