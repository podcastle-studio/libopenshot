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

constexpr char SPACE = ' ';
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

// Cap on the offscreen word-block texture (px). The block margin (uncapped glow/spread
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
    for (const auto& line : layout.lines) maxLineLen = std::max(maxLineLen, static_cast<double>(utf8Length(line.text)));
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

// ── Per-glyph primitives ─────────────────────────────────────────────────────

void applyCharTransform(subtitle::SkiaRenderer* renderer, SkCanvas* canvas,
                        const AnimatedCharItem& item, double fontSize, const AnimationTransformFlags& flags) {
    if (item.curve.has_value()) {
        canvas->translate(static_cast<float>(item.curve->arcX), static_cast<float>(item.curve->arcY));
        canvas->rotate(static_cast<float>(item.curve->rotationDeg));
        canvas->translate(0.0f, static_cast<float>(-item.curve->pivotOffsetY));
    } else {
        canvas->translate(static_cast<float>(item.centerX), static_cast<float>(item.centerY));
    }
    applyAnimationTransform(canvas, item.props, fontSize, item.boxWidth, item.boxHeight, flags);
    applyInsetClip(canvas, item.props, item.boxWidth, item.boxHeight);
    (void)renderer;
}

void drawAnimatedLetter(subtitle::SkiaRenderer* renderer, const AnimatedCharItem& item,
                        double dx, double dy, const SkPaint& paint, const TextClipPaintStyle& style) {
    drawLetter(renderer, item.letter, item.x - item.centerX + dx, item.baselineY - item.centerY + dy, paint, style);
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

// ── Word-mode renderer ───────────────────────────────────────────────────────

void WordAnimationRenderer::renderWordAnimatedBlock(
    const WordAnimationContent& content,
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
        drawWordBlockTexture(content, paddingX, paddingY, scale, props, fontSize);
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
        content.draw(originX, originY, false, false);
        canvas->restore();
    }

    if (needLayer) canvas->restore();
    canvas->restore();
}

void WordAnimationRenderer::drawWordBlockTexture(
    const WordAnimationContent& content,
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
    const int texWidth = std::max(2, static_cast<int>(std::ceil(dstWidth * renderScale)));
    const int texHeight = std::max(2, static_cast<int>(std::ceil(dstHeight * renderScale)));

    // Bake the shadow + stroke + fill into the flat texture, then let the 3D perspective warp the
    // whole thing together — the shadow is a flat drop shadow on the same plane as the glyphs, so it
    // must be tilted with them (matching the front end, which transforms one composited flat layer).
    // Only the glow is drawn live below (it is a screen-space beam effect, not part of the flat plane).
    sk_sp<SkSurface> surface = SkSurfaces::Raster(SkImageInfo::MakeN32Premul(texWidth, texHeight));
    if (!surface) return;
    SkCanvas* offscreen = surface->getCanvas();
    offscreen->clear(SK_ColorTRANSPARENT);
    offscreen->save();
    offscreen->scale(static_cast<float>(renderScale), static_cast<float>(renderScale));
    renderer->renderToCanvas(offscreen, [&] {
        // Skip only the glow here; it is drawn live on the destination canvas below. The shadow stays
        // baked so the perspective warp tilts it along with the glyphs.
        content.draw(margin + paddingX, margin + paddingY, true, false);
    });
    offscreen->restore();
    sk_sp<SkImage> image = surface->makeImageSnapshot();
    if (!image) return;

    // Glow live, beneath the composited block, faded with the block's animated opacity.
    content.drawGlow(-dstWidth / 2.0 + margin + paddingX, -dstHeight / 2.0 + margin + paddingY, clamp01(props.opacity()));

    SkPaint paint;
    paint.setAntiAlias(true);
    paint.setAlphaf(static_cast<float>(clamp01(props.opacity())));
    if (props.blur() > 0.0) {
        const double sigma = props.blur() * fontSize;
        paint.setImageFilter(SkImageFilters::Blur(sigma, sigma, SkTileMode::kClamp, nullptr));
    }
    canvas->drawImageRect(
        image.get(),
        SkRect::MakeWH(static_cast<float>(texWidth), static_cast<float>(texHeight)),
        SkRect::MakeXYWH(static_cast<float>(-dstWidth / 2.0), static_cast<float>(-dstHeight / 2.0),
                         static_cast<float>(dstWidth), static_cast<float>(dstHeight)),
        SkSamplingOptions(SkFilterMode::kLinear, SkMipmapMode::kLinear),
        &paint, SkCanvas::kStrict_SrcRectConstraint);
}

// ── Char-mode renderer ───────────────────────────────────────────────────────

namespace {

std::vector<AnimatedCharItem> collectAnimatedChars(
    const TextClipLayout& layout, const TextClipPaintStyle& style,
    double originX, double originY, const TextClipAnimationFrame& animation) {
    std::vector<AnimatedCharItem> items;
    const double firstBaselineY = originY + layout.firstLineAscent;
    int charIndex = 0;

    for (size_t li = 0; li < layout.lines.size(); ++li) {
        const TextClipLine& line = layout.lines[li];
        if (line.text.empty()) continue;
        const double baselineY = firstBaselineY + static_cast<double>(li) * layout.lineHeight;
        double cursor = originX + getLineStartX(line, layout, style.alignment, 0.0);

        size_t i = 0;
        forEachUtf8(line.text, [&](const std::string& letter, SkUnichar) {
            const double advance = i < line.letterAdvances.size() ? line.letterAdvances[i] : 0.0;
            const int index = charIndex++;
            if (letter != std::string(1, SPACE)) {
                const ResolvedAnimProps props = animation.getCharProps(index);
                const double opacity = clamp01(props.opacity());
                if (opacity > 0.0) {
                    AnimatedCharItem item;
                    item.letter = letter;
                    item.x = cursor;
                    item.baselineY = baselineY;
                    item.centerX = cursor + advance / 2.0;
                    item.centerY = baselineY - (line.ascent - line.descent) / 2.0;
                    item.boxWidth = advance;
                    item.boxHeight = line.ascent + line.descent;
                    item.props = props;
                    item.opacity = opacity;
                    items.push_back(std::move(item));
                }
            }
            cursor += advance;
            ++i;
        });
    }
    return items;
}

std::vector<AnimatedCharItem> collectCurvedAnimatedChars(
    const CurvedTextGeometry& geometry, const TextClipLine& line,
    double originX, double originY, const TextClipAnimationFrame& animation) {
    std::vector<AnimatedCharItem> items;
    const double pivotOffsetY = (line.ascent - line.descent) / 2.0;
    const double boxHeight = line.ascent + line.descent;
    int charIndex = 0;

    for (const auto& placement : geometry.placements) {
        const int index = charIndex++;
        if (isSpaceGlyph(placement.letter)) continue;
        const ResolvedAnimProps props = animation.getCharProps(index);
        const double opacity = clamp01(props.opacity());
        if (opacity <= 0.0) continue;

        AnimatedCharItem item;
        item.letter = placement.letter;
        item.x = -placement.advance / 2.0;
        item.baselineY = pivotOffsetY;
        item.centerX = 0.0;
        item.centerY = 0.0;
        item.boxWidth = placement.advance;
        item.boxHeight = boxHeight;
        item.props = props;
        item.opacity = opacity;
        item.curve = CurvedCharPlacement{
            originX + placement.cx,
            originY + placement.cy,
            placement.rotation * 180.0 / M_PI,
            pivotOffsetY,
        };
        items.push_back(std::move(item));
    }
    return items;
}

} // namespace

void CharAnimationRenderer::drawAnimatedCharItems(
    std::vector<AnimatedCharItem>& items, const TextClipPaintStyle& style,
    const AnimationTransformFlags& flags,
    double contentWidth, double contentHeight, double originX, double originY, bool skipGlow) {
    SkCanvas* canvas = renderer->getCanvas();
    const double fontSize = style.fontSize;
    if (items.empty()) return;

    if (style.dropShadow.has_value()) {
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
            applyCharTransform(renderer, canvas, item, fontSize, flags);
            if (style.stroke.has_value()) {
                withAnimatedPaint(renderer, {shadow.color, shadow.opacity, style.stroke->width}, item.opacity, combinedBlur,
                    [&](const SkPaint& paint) { drawAnimatedLetter(renderer, item, dx, dy, paint, style); });
            }
            withAnimatedPaint(renderer, {shadow.color, shadow.opacity, std::nullopt}, item.opacity, combinedBlur,
                [&](const SkPaint& paint) { drawAnimatedLetter(renderer, item, dx, dy, paint, style); });
            canvas->restore();
        }
    }

    if (style.glow.has_value() && !skipGlow) {
        glowRenderer->drawAnimatedGlowLayer(items, style, *style.glow, contentWidth, contentHeight, originX, originY, flags);
    }

    if (style.stroke.has_value()) {
        const auto& stroke = *style.stroke;
        auto drawStrokes = [&] {
            for (const auto& item : items) {
                const double animBlur = item.props.blur() > 0.0 ? item.props.blur() * fontSize : 0.0;
                canvas->save();
                applyCharTransform(renderer, canvas, item, fontSize, flags);
                withAnimatedPaint(renderer, {stroke.color, 1.0, stroke.width}, item.opacity, combineBlur(animBlur, calibratedTextBlur(style.blur)),
                    [&](const SkPaint& paint) { drawAnimatedLetter(renderer, item, 0.0, 0.0, paint, style); });
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
            applyCharTransform(renderer, canvas, item, fontSize, flags);
            withAnimatedPaint(renderer, {style.color, coreOpacity, std::nullopt}, item.opacity,
                combineBlur(combineBlur(animBlur, calibratedTextBlur(style.blur)), coreSoftBlur),
                [&](const SkPaint& paint) { drawAnimatedLetter(renderer, item, 0.0, 0.0, paint, style); });
            canvas->restore();
        }
    };
    if (style.colorGradient.has_value()) {
        const PaintGradient g = gradientFill(*style.colorGradient, originX, originY, contentWidth, contentHeight);
        withGradientCoverage(renderer, g, SkRect::MakeEmpty(), drawFills);
    } else {
        drawFills();
    }
}

void CharAnimationRenderer::renderCharAnimated(
    const TextClipLayout& layout, const TextClipPaintStyle& style,
    const std::optional<TextClipBackgroundStyle>& background,
    double originX, double originY, const TextClipAnimationFrame& animation, bool skipGlow) {
    if (background.has_value()) {
        drawBackgroundRect(renderer, *background, originX, originY, layout.layoutWidth, layout.textHeight);
    }
    std::vector<AnimatedCharItem> items = collectAnimatedChars(layout, style, originX, originY, animation);
    drawAnimatedCharItems(items, style, animation.flags, layout.layoutWidth, layout.textHeight, originX, originY, skipGlow);
}

void CharAnimationRenderer::renderCurvedCharAnimated(
    const CurvedTextGeometry& geometry, const TextClipLine& line, const TextClipPaintStyle& style,
    const std::optional<TextClipBackgroundStyle>& background,
    double originX, double originY, const TextClipAnimationFrame& animation, bool skipGlow) {
    if (background.has_value()) {
        drawBackgroundRect(renderer, *background, originX, originY, geometry.width, geometry.height);
    }
    std::vector<AnimatedCharItem> items = collectCurvedAnimatedChars(geometry, line, originX, originY, animation);
    drawAnimatedCharItems(items, style, animation.flags, geometry.width, geometry.height, originX, originY, skipGlow);
}

void CharAnimationRenderer::drawCharAnimatedGlowOnly(
    const TextClipLayout& layout, const TextClipPaintStyle& style,
    double originX, double originY, const TextClipAnimationFrame& animation) {
    if (!style.glow.has_value()) return;
    std::vector<AnimatedCharItem> items = collectAnimatedChars(layout, style, originX, originY, animation);
    if (items.empty()) return;
    glowRenderer->drawAnimatedGlowLayer(items, style, *style.glow, layout.layoutWidth, layout.textHeight,
                                        originX, originY, animation.flags);
}

void CharAnimationRenderer::drawCurvedCharAnimatedGlowOnly(
    const CurvedTextGeometry& geometry, const TextClipLine& line, const TextClipPaintStyle& style,
    double originX, double originY, const TextClipAnimationFrame& animation) {
    if (!style.glow.has_value()) return;
    std::vector<AnimatedCharItem> items = collectCurvedAnimatedChars(geometry, line, originX, originY, animation);
    if (items.empty()) return;
    glowRenderer->drawAnimatedGlowLayer(items, style, *style.glow, geometry.width, geometry.height,
                                        originX, originY, animation.flags);
}

// ── Top-level dispatch ───────────────────────────────────────────────────────

namespace {

void renderWordAnimatedFlat(
    subtitle::SkiaRenderer* renderer, const TextClipLayout& layout, const TextClipPaintStyle& style,
    const std::optional<TextClipBackgroundStyle>& background, double originX, double originY, double scale,
    const TextClipAnimationFrame& animation) {
    const double extraLetterSpacing = animation.props.letterSpacing() * style.fontSize;
    TextGlowRenderer glow(renderer);
    WordAnimationContent content;
    content.contentWidth = layout.layoutWidth;
    content.contentHeight = layout.textHeight;
    content.margin = [&style, &layout, extraLetterSpacing](double blurSigma) {
        return blockMargin(style, blurSigma, extraLetterSpacing, layout);
    };
    content.draw = [renderer, &layout, &style, &background, extraLetterSpacing](double ox, double oy, bool skipGlow, bool skipShadow) {
        renderLayout(layout, style, background, ox, oy, renderer, extraLetterSpacing, skipGlow, skipShadow);
    };
    content.drawGlow = [&glow, &layout, &style, extraLetterSpacing](double ox, double oy, double opacityMul) {
        if (style.glow.has_value()) {
            glow.drawGlowLayer(layout, style, *style.glow, ox, oy, nullptr, opacityMul, extraLetterSpacing);
        }
    };
    content.drawShadow = [renderer, &layout, &style, extraLetterSpacing](double ox, double oy) {
        renderShadowLayer(layout, style, ox, oy, renderer, extraLetterSpacing);
    };
    WordAnimationRenderer(renderer).renderWordAnimatedBlock(content, style, background, originX, originY, scale, animation);
}

void renderWordAnimatedCurved(
    subtitle::SkiaRenderer* renderer, const CurvedTextGeometry& geometry, const TextClipLayout& layout,
    const TextClipPaintStyle& style, const std::optional<TextClipBackgroundStyle>& background,
    double originX, double originY, double scale, const TextClipAnimationFrame& animation) {
    TextGlowRenderer glow(renderer);
    CurvedTextPainter painter(renderer, &glow);
    WordAnimationContent content;
    content.contentWidth = geometry.width;
    content.contentHeight = geometry.height;
    content.margin = [&style, &geometry](double blurSigma) {
        return curvedBlockMargin(style, geometry, blurSigma);
    };
    content.draw = [&painter, &geometry, &layout, &style, &background](double ox, double oy, bool skipGlow, bool skipShadow) {
        painter.drawCurvedStatic(geometry, layout, style, background, ox, oy, skipGlow, skipShadow);
    };
    content.drawGlow = [&glow, &layout, &style, &geometry](double ox, double oy, double opacityMul) {
        if (style.glow.has_value()) {
            glow.drawGlowLayer(layout, style, *style.glow, ox, oy, &geometry, opacityMul);
        }
    };
    content.drawShadow = [&painter, &geometry, &style](double ox, double oy) {
        painter.drawCurvedShadowOnly(geometry, style, ox, oy);
    };
    WordAnimationRenderer(renderer).renderWordAnimatedBlock(content, style, background, originX, originY, scale, animation);
}

// Char-mode animation composed with a static 3D tilt: paint the per-letter animation FLAT into
// the word-mode composited texture (skipGlow), then draw that whole texture under the block's 3D
// rotation — reusing the word texture+3D core with a buildStatic3DFrame frame. The glow is drawn
// live afterwards under the same transform (matches the word path).
void renderCharAnimated3D(
    subtitle::SkiaRenderer* renderer, const TextClipLayout& layout, const TextClipPaintStyle& style,
    const std::optional<TextClipBackgroundStyle>& background, double originX, double originY,
    double scale, const TextClipAnimationFrame& animation) {
    if (!animation.static3D.has_value()) return;
    TextGlowRenderer glow(renderer);
    WordAnimationContent content;
    content.contentWidth = layout.layoutWidth;
    content.contentHeight = layout.textHeight;
    content.margin = [&style, &layout](double blurSigma) {
        return blockMargin(style, blurSigma, 0.0, layout);
    };
    content.draw = [renderer, &glow, &layout, &style, &background, &animation](double ox, double oy, bool skipGlow, bool /*skipShadow*/) {
        // Char + 3D keeps the shadow baked into the texture (no live drawShadow set below).
        CharAnimationRenderer(renderer, &glow).renderCharAnimated(layout, style, background, ox, oy, animation, skipGlow);
    };
    content.drawGlow = [renderer, &glow, &layout, &style, &animation](double ox, double oy, double /*opacityMul*/) {
        CharAnimationRenderer(renderer, &glow).drawCharAnimatedGlowOnly(layout, style, ox, oy, animation);
    };
    const TextClipAnimationFrame tiltFrame = buildStatic3DFrame(animation.static3D->first, animation.static3D->second);
    WordAnimationRenderer(renderer).renderWordAnimatedBlock(content, style, background, originX, originY, scale, tiltFrame);
}

void renderCurvedCharAnimated3D(
    subtitle::SkiaRenderer* renderer, const CurvedTextGeometry& geometry, const TextClipLine& line,
    const TextClipLayout& layout, const TextClipPaintStyle& style,
    const std::optional<TextClipBackgroundStyle>& background, double originX, double originY,
    double scale, const TextClipAnimationFrame& animation) {
    if (!animation.static3D.has_value()) return;
    TextGlowRenderer glow(renderer);
    WordAnimationContent content;
    content.contentWidth = geometry.width;
    content.contentHeight = geometry.height;
    content.margin = [&style, &geometry](double blurSigma) {
        return curvedBlockMargin(style, geometry, blurSigma);
    };
    content.draw = [renderer, &glow, &geometry, &line, &style, &background, &animation](double ox, double oy, bool skipGlow, bool /*skipShadow*/) {
        CharAnimationRenderer(renderer, &glow).renderCurvedCharAnimated(geometry, line, style, background, ox, oy, animation, skipGlow);
    };
    content.drawGlow = [renderer, &glow, &geometry, &line, &style, &animation](double ox, double oy, double /*opacityMul*/) {
        CharAnimationRenderer(renderer, &glow).drawCurvedCharAnimatedGlowOnly(geometry, line, style, ox, oy, animation);
    };
    (void)layout;
    const TextClipAnimationFrame tiltFrame = buildStatic3DFrame(animation.static3D->first, animation.static3D->second);
    WordAnimationRenderer(renderer).renderWordAnimatedBlock(content, style, background, originX, originY, scale, tiltFrame);
}

} // namespace

TextClipAnimationFrame buildStatic3DFrame(double tiltX, double tiltY) {
    TextClipAnimationFrame frame;
    frame.mode = AnimationMode::WORD;
    frame.props = identityProps();
    frame.props[AnimProp::rotateX] = tiltX;
    frame.props[AnimProp::rotateY] = tiltY;
    frame.props[AnimProp::perspective] = STATIC_ROTATION_PERSPECTIVE;
    frame.scaleAnimated = false;
    return frame;
}

void composeStatic3DIntoWordFrame(TextClipAnimationFrame& frame, double tiltX, double tiltY) {
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

    const bool hasCharAnim = animation.has_value() && animation->mode == AnimationMode::CHAR;
    const bool hasWordAnim = animation.has_value() && animation->mode == AnimationMode::WORD;
    // Char mode has no block-level transform to hang a tilt on, so a char frame carrying a static
    // 3D tilt is baked flat then tilted as one unit (renderCharAnimated3D). Word/flat frames carry
    // the tilt folded into props and flow through the normal texture+3D word path.
    const bool charStatic3D = hasCharAnim && animation->static3D.has_value();

    if (paint.curveAngle.has_value()) {
        const CurvedTextGeometry geometry = curvedGeometryForLayout(layout, paint);
        TextGlowRenderer glow(renderer);
        if (hasCharAnim) {
            const TextClipLine* line = nullptr;
            for (const auto& l : layout.lines) { if (!l.text.empty()) { line = &l; break; } }
            if (!line) return;
            if (charStatic3D) {
                renderCurvedCharAnimated3D(renderer, geometry, *line, layout, paint, background, originX, originY, scale, *animation);
            } else {
                CharAnimationRenderer(renderer, &glow).renderCurvedCharAnimated(
                    geometry, *line, paint, background, originX, originY, *animation);
            }
        } else if (hasWordAnim) {
            renderWordAnimatedCurved(renderer, geometry, layout, paint, background, originX, originY, scale, *animation);
        } else {
            CurvedTextPainter(renderer, &glow).drawCurvedStatic(geometry, layout, paint, background, originX, originY);
        }
        return;
    }

    if (hasCharAnim) {
        if (charStatic3D) {
            renderCharAnimated3D(renderer, layout, paint, background, originX, originY, scale, *animation);
        } else {
            TextGlowRenderer glow(renderer);
            CharAnimationRenderer(renderer, &glow).renderCharAnimated(layout, paint, background, originX, originY, *animation);
        }
    } else if (hasWordAnim) {
        renderWordAnimatedFlat(renderer, layout, paint, background, originX, originY, scale, *animation);
    } else {
        renderLayout(layout, paint, background, originX, originY, renderer);
    }
}

// ── Animated frame extent ────────────────────────────────────────────────────

AnimatedExtent computeAnimatedExtent(
    const TextClipLayout& layout, const TextClipPaintStyle& paint,
    double contentWidth, double contentHeight,
    const AnimationTimeline& timeline, const AnimationPresetMap& presets, int charCount) {
    const double fontSize = paint.fontSize;
    double halfW = contentWidth / 2.0;
    double halfH = contentHeight / 2.0;

    // Nominal glyph box used to bound char-mode per-letter transforms.
    const double glyphBoxW = std::max(fontSize, contentHeight);
    const double glyphBoxH = layout.lines.empty() ? fontSize : std::max(fontSize, layout.lines.front().ascent + layout.lines.front().descent);

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
        const FramePlan plan = planFrame(elapsed, timeline, charCount, presets);
        if (!plan.presetId.has_value()) continue;
        const auto it = presets.find(*plan.presetId);
        if (it == presets.end()) continue;
        const auto frame = buildAnimationFrame(plan, it->second, elapsed);
        if (!frame.has_value()) continue;

        if (frame->mode == AnimationMode::WORD) {
            const SkMatrix m = animationMatrix(frame->props, fontSize, contentWidth, contentHeight, frame->flags);
            double hw, hh;
            mapBoxHalf(m, contentWidth, contentHeight, hw, hh);
            const double blurExtra = frame->props.blur() > 0.0 ? frame->props.blur() * fontSize * 3.0 : 0.0;
            halfW = std::max(halfW, hw + blurExtra);
            halfH = std::max(halfH, hh + blurExtra);
        } else if (frame->mode == AnimationMode::CHAR && charCount > 0) {
            const int idxs[3] = {0, charCount / 2, charCount - 1};
            for (int idx : idxs) {
                const ResolvedAnimProps props = frame->getCharProps(std::clamp(idx, 0, charCount - 1));
                const SkMatrix m = animationMatrix(props, fontSize, glyphBoxW, glyphBoxH, frame->flags);
                double hw, hh;
                mapBoxHalf(m, glyphBoxW, glyphBoxH, hw, hh);
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
