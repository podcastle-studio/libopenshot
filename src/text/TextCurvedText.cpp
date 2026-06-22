#include "TextCurvedText.h"

#include "../subtitle/SkiaRenderer.h"
#include "TextDrawShared.h"
#include "TextGlowRenderer.h"
#include "TextGlowShader.h"

#include <skia/include/core/SkCanvas.h>
#include <skia/include/core/SkPaint.h>

#include <algorithm>
#include <cmath>
#include <limits>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace openshot {
namespace text {

namespace {

constexpr double DEG_TO_RAD = M_PI / 180.0;

// Below this absolute arc angle the radius is huge — treat the text as a straight line.
constexpr double MIN_CURVE_ANGLE = 0.5;

double sign(double v) { return v > 0.0 ? 1.0 : (v < 0.0 ? -1.0 : 0.0); }

std::vector<std::string> codepoints(const std::string& text) {
    std::vector<std::string> out;
    forEachUtf8(text, [&](const std::string& letter, SkUnichar) { out.push_back(letter); });
    return out;
}

CurvedTextGeometry buildStraightGeometry(
    const std::vector<std::string>& letters,
    const std::vector<double>& advances,
    double totalWidth, double ascent, double descent)
{
    CurvedTextGeometry geo;
    double cursor = 0.0;
    for (size_t i = 0; i < letters.size(); ++i) {
        const double advance = i < advances.size() ? advances[i] : 0.0;
        geo.placements.push_back({letters[i], advance, cursor + advance / 2.0, ascent, 0.0});
        cursor += advance;
    }
    geo.width = totalWidth;
    geo.height = ascent + descent;
    return geo;
}

} // namespace

bool isSpaceGlyph(const std::string& letter) { return letter == " "; }

CurvedTextGeometry computeCurvedGeometry(const TextClipLine& line, double curveAngleDeg) {
    const std::vector<std::string> letters = codepoints(line.text);
    const std::vector<double>& advances = line.letterAdvances;
    const double ascent = line.ascent;
    const double descent = line.descent;

    double totalWidth = 0.0;
    for (double a : advances) totalWidth += a;

    if (totalWidth <= 0.0 || std::abs(curveAngleDeg) < MIN_CURVE_ANGLE) {
        return buildStraightGeometry(letters, advances, totalWidth, ascent, descent);
    }

    const double arcAngle = std::abs(curveAngleDeg) * DEG_TO_RAD;
    const double direction = sign(curveAngleDeg);
    const double radius = totalWidth / arcAngle;

    CurvedTextGeometry geo;
    double minX = std::numeric_limits<double>::infinity();
    double minY = std::numeric_limits<double>::infinity();
    double maxX = -std::numeric_limits<double>::infinity();
    double maxY = -std::numeric_limits<double>::infinity();

    // Place each glyph's vertical CENTER on the arc (not its baseline). The baseline
    // sits below the center by centreToBaseline = (ascent − descent)/2; because the ink
    // then straddles the radius circle by ±halfHeight in both directions, +θ and −θ
    // render at the exact same size with letters upright (no flip / box-padding hack).
    const double centreToBaseline = (ascent - descent) / 2.0;
    const double halfHeight = (ascent + descent) / 2.0;

    double cursor = 0.0;
    for (size_t i = 0; i < letters.size(); ++i) {
        const double advance = i < advances.size() ? advances[i] : 0.0;
        const double angle = (cursor + advance / 2.0) / radius - arcAngle / 2.0;
        cursor += advance;

        const double s = std::sin(angle);
        const double c = std::cos(angle);
        const double rotation = direction * angle;
        const double rotSin = std::sin(rotation);
        const double rotCos = std::cos(rotation);

        // Glyph center on the arc, then recover the baseline point the painter draws from.
        const double centreX = radius * s;
        const double centreY = direction * radius * (1.0 - c);
        const double cx = centreX - centreToBaseline * rotSin;
        const double cy = centreY + centreToBaseline * rotCos;

        geo.placements.push_back({letters[i], advance, cx, cy, rotation});

        // Bounding box from a symmetric local box (±advance/2, ±halfHeight) about the arc
        // center — symmetric in Y, so the box dimensions are identical for ±θ.
        const double halfAdvance = advance / 2.0;
        for (double localX : {-halfAdvance, halfAdvance}) {
            for (double localY : {-halfHeight, halfHeight}) {
                const double x = localX * rotCos - localY * rotSin + centreX;
                const double y = localX * rotSin + localY * rotCos + centreY;
                minX = std::min(minX, x); maxX = std::max(maxX, x);
                minY = std::min(minY, y); maxY = std::max(maxY, y);
            }
        }
    }

    for (auto& p : geo.placements) {
        p.cx -= minX;
        p.cy -= minY;
    }
    geo.width = maxX - minX;
    geo.height = maxY - minY;
    return geo;
}

void forEachCurvedGlyph(
    subtitle::SkiaRenderer* renderer,
    const CurvedTextGeometry& geometry,
    double originX,
    double originY,
    const TextClipPaintStyle& style,
    const SkPaint* underPaint,
    const SkPaint& mainPaint)
{
    SkCanvas* canvas = renderer->getCanvas();
    if (!canvas) return;

    for (const auto& p : geometry.placements) {
        if (isSpaceGlyph(p.letter)) continue;
        canvas->save();
        canvas->translate(static_cast<float>(originX + p.cx), static_cast<float>(originY + p.cy));
        canvas->rotate(static_cast<float>(p.rotation * 180.0 / M_PI));
        if (underPaint) drawLetter(renderer, p.letter, -p.advance / 2.0, 0.0, *underPaint, style);
        drawLetter(renderer, p.letter, -p.advance / 2.0, 0.0, mainPaint, style);
        canvas->restore();
    }
}

double curvedBlockMargin(const TextClipPaintStyle& style, const CurvedTextGeometry& geometry, double blurSigma) {
    const double shadowMargin = style.dropShadow.has_value()
        ? style.dropShadow->distance + style.dropShadow->blur * 2.0 : 0.0;
    const double strokeMargin = style.stroke.has_value() ? style.stroke->width * 2.0 : 0.0;
    double glowMargin = 0.0;
    if (style.glow.has_value()) {
        const double offMax = std::max(std::abs(style.glow->sourceOffX), std::abs(style.glow->sourceOffY)) * style.fontSize;
        const double halfExtent = std::max(geometry.width, geometry.height) / 2.0;
        glowMargin = style.glow->rayLen * (halfExtent + offMax) + offMax + GLOW_BEAM_BLUR_RATIO * style.fontSize * 3.0;
    }
    return std::ceil(std::max({shadowMargin, strokeMargin, glowMargin}) + (blurSigma + style.blur) * 3.0 + 4.0);
}

void CurvedTextPainter::drawCurvedStatic(
    const CurvedTextGeometry& geometry,
    const TextClipLayout& layout,
    const TextClipPaintStyle& style,
    const std::optional<TextClipBackgroundStyle>& background,
    double originX,
    double originY,
    bool skipGlow)
{
    SkCanvas* canvas = renderer->getCanvas();
    if (!canvas) return;

    if (background.has_value()) {
        drawBackgroundRect(renderer, *background, originX, originY, geometry.width, geometry.height);
    }

    if (style.dropShadow.has_value()) {
        const auto& shadow = *style.dropShadow;
        const double radians = shadow.angle * M_PI / 180.0;
        const double dx = std::cos(radians) * shadow.distance;
        const double dy = std::sin(radians) * shadow.distance;
        const double shadowBlur = combineBlur(shadow.blur, style.blur) * SHADOW_BLUR_SIGMA_SCALE;
        const std::optional<double> blurOpt = shadowBlur > 0.0 ? std::optional<double>(shadowBlur) : std::nullopt;

        const SkPaint* shadowFill = renderer->getPaint(
            subtitle::PaintProps{shadow.color, shadow.opacity, std::nullopt, blurOpt});
        const SkPaint* shadowStroke = style.stroke.has_value()
            ? renderer->getPaint(subtitle::PaintProps{shadow.color, shadow.opacity, style.stroke->width, blurOpt})
            : nullptr;

        canvas->save();
        canvas->translate(static_cast<float>(dx), static_cast<float>(dy));
        forEachCurvedGlyph(renderer, geometry, originX, originY, style, shadowStroke, *shadowFill);
        canvas->restore();
    }

    if (style.glow.has_value() && !skipGlow) {
        glowRenderer->drawGlowLayer(layout, style, *style.glow, originX, originY, &geometry);
    }

    if (style.stroke.has_value()) {
        // Same CPU-vs-GPU mask-blur calibration as the shadow path so the stroke blur matches the front end.
        const double strokeBlur = style.blur * SHADOW_BLUR_SIGMA_SCALE;
        const std::optional<double> blurOpt = strokeBlur > 0.0 ? std::optional<double>(strokeBlur) : std::nullopt;
        const SkPaint* strokePaint = renderer->getPaint(
            subtitle::PaintProps{style.stroke->color, 1.0, style.stroke->width, blurOpt});
        forEachCurvedGlyph(renderer, geometry, originX, originY, style, nullptr, *strokePaint);
    }

    // Soften the topmost crisp text when glow is active (Layer 3).
    const double coreSoftBlur = style.glow.has_value() ? GLOW_CORE_TEXT_BLUR_RATIO * style.fontSize : 0.0;
    // Same CPU-vs-GPU mask-blur calibration as the shadow path so the fill blur matches the front end.
    const double fillBlur = combineBlur(style.blur, coreSoftBlur) * SHADOW_BLUR_SIGMA_SCALE;
    const std::optional<double> fillBlurOpt = fillBlur > 0.0 ? std::optional<double>(fillBlur) : std::nullopt;
    const SkPaint* fillPaint = renderer->getPaint(subtitle::PaintProps{
        style.color, style.glow.has_value() ? GLOW_CORE_TEXT_OPACITY : 1.0, std::nullopt, fillBlurOpt});
    forEachCurvedGlyph(renderer, geometry, originX, originY, style, nullptr, *fillPaint);
}

} // namespace text
} // namespace openshot
