#include "subtitle/WordRenderer.h"
#include "subtitle/SkiaRenderer.h"
#include <skia/include/core/SkPath.h>
#include <skia/include/core/SkRRect.h>
#include <skia/include/core/SkFont.h>
#include <skia/include/core/SkFontMetrics.h>
#include <algorithm>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace openshot {
namespace subtitle {

WordRenderer::WordRenderer(SkiaRenderer* renderer) : renderer(renderer) {}

void WordRenderer::bubblePath(SkPath* path, const float x, const float y, const float width, const float height, const float radius) {
    const float left = x;
    const float top = y;
    const float right = x + width;
    const  float bottom = y + height;
    const float r = std::max(0.0f, radius + 2);

    path->moveTo(left, bottom);
    path->lineTo(left, top + r);

    if (r > 0) {
        path->arcTo(SkRect::MakeXYWH(left, top, r * 2, r * 2), 180, 90, false);
    } else {
        path->lineTo(left, top);
        path->lineTo(left + r, top);
    }

    path->lineTo(right - r, top);

    if (r > 0) {
        path->arcTo(SkRect::MakeXYWH(right - r * 2, top, r * 2, r * 2), 270, 90, false);
    } else {
        path->lineTo(right, top);
        path->lineTo(right, top + r);
    }

    path->lineTo(right, bottom - r);

    if (r > 0) {
        path->arcTo(SkRect::MakeXYWH(right - r * 2, bottom - r * 2, r * 2, r * 2), 0, 90, false);
    } else {
        path->lineTo(right, bottom);
        path->lineTo(right - r, bottom);
    }

    path->lineTo(left, bottom);
    path->close();
}

void WordRenderer::renderWord(const std::string& word, const SubtitleTextStyle& style,
                             const double x, const double y, const double deltaY) {
    const float wordWidth = getTextWidth(word, style);

    renderer->save();
    renderer->translate(x, y);
    drawWordBackground(wordWidth, style, deltaY);
    drawWordShadow(word, wordWidth, style);
    drawWordStroke(word, wordWidth, style);
    drawWordText(word, wordWidth, style);
    renderer->restore();
}

float WordRenderer::getTextWidth(const std::string& text, const SubtitleTextStyle& style) const {
    const SkFont skFont = renderer->getFont({
        style.fontFamily,
        style.fontSize,
        style.bold,
        style.italic
    });

    return skFont.measureText(text.c_str(), text.length(), SkTextEncoding::kUTF8, nullptr);
}

TextBounds WordRenderer::getTextHeight(const std::string& text, const SubtitleTextStyle& style) const {
    const SkFont skFont = renderer->getFont({
        style.fontFamily,
        style.fontSize,
        style.bold,
        style.italic
    });

    SkFontMetrics metrics;
    skFont.getMetrics(&metrics);

    return {
        metrics.fAscent,  // top (negative value)
        metrics.fDescent  // bottom (positive value)
    };
}

float WordRenderer::getSpaceWidth(const SubtitleTextStyle& style) const {
    const SkFont skFont = renderer->getFont({
        style.fontFamily,
        style.fontSize,
        style.bold,
        style.italic
    });

    const float spaceWidth = skFont.measureText(" ", 1, SkTextEncoding::kUTF8, nullptr);
    return spaceWidth + style.letterSpacing;
}

void WordRenderer::drawBubbleBackground(const float wordWidth, const SubtitleTextStyle& style, const float deltaY) const {
    if (!style.backgroundColor.has_value()) return;

    const PaintProps bgPaintProps{
        style.backgroundColor.value(),
        style.backgroundOpacity.value_or(1.0f)
    };

    const SkPaint* bgPaint = renderer->getPaint(bgPaintProps);

    const PaintProps bgStrokePaintProps{
        "#000000",
        style.backgroundOpacity.value_or(1.0f)
    };

    const SkPaint* bgStrokePaint = renderer->getPaint(bgStrokePaintProps);

    const float paddingX = style.backgroundPaddingX.value_or(0);
    const float paddingY = style.backgroundPaddingY.value_or(0);
    const float radius = style.backgroundRadius.value_or(0);

    // Shadow path
    SkPath pathShadow;
    bubblePath(&pathShadow,
        -wordWidth / 2 - paddingX - 6,
        -style.fontSize - paddingY + deltaY + 6,
        2 * paddingX + wordWidth,
        2 * paddingY + style.fontSize,
        radius);
    renderer->drawPath(pathShadow, *bgStrokePaint);

    // Stroke path
    SkPath pathStroke;
    bubblePath(&pathStroke,
        -wordWidth / 2 - paddingX,
        -style.fontSize - paddingY + deltaY,
        2 * paddingX + wordWidth,
        2 * paddingY + style.fontSize,
        radius);
    renderer->drawPath(pathStroke, *bgStrokePaint);

    // Fill path
    SkPath path;
    bubblePath(&path,
        -wordWidth / 2 - paddingX + 3,
        -style.fontSize - paddingY + deltaY + 3,
        2 * paddingX + wordWidth - 6,
        2 * paddingY + style.fontSize - 6,
        radius);
    renderer->drawPath(path, *bgPaint);
}

void WordRenderer::drawWordBackground(const float wordWidth, const SubtitleTextStyle& style, const float deltaY) const {
    if (!style.backgroundColor.has_value()) return;

    if (style.bubble.has_value() && style.bubble.value()) {
        drawBubbleBackground(wordWidth, style, deltaY);
        return;
    }

    const PaintProps bgPaintProps{
        style.backgroundColor.value(),
        style.backgroundOpacity.value_or(1.0f)
    };
    const SkPaint* bgPaint = renderer->getPaint(bgPaintProps);

    const float paddingX = style.backgroundPaddingX.value_or(0);
    const float paddingY = style.backgroundPaddingY.value_or(0);
    const float radius = style.backgroundRadius.value_or(0);

    const SkRect rect = renderer->makeRect(
        -wordWidth / 2 - paddingX + 2,
        -style.fontSize - paddingY + deltaY,
        wordWidth / 2 + paddingX - 2,
        paddingY + deltaY
    );

    const SkRRect rrect = renderer->makeRRect(rect, radius, radius);
    renderer->drawRRect(rrect, *bgPaint);
}

void WordRenderer::drawWordText(const std::string& word, float wordWidth, const SubtitleTextStyle& style) const {
    const PaintProps textPaintProps{style.color, style.opacity };
    const SkPaint* textPaint = renderer->getPaint(textPaintProps);

    const SkFont skFont = renderer->getFont({
        style.fontFamily,
        style.fontSize,
        style.bold,
        style.italic
    });

    // Render letter by letter for letter spacing
    float currentX = -wordWidth / 2;
    for (size_t i = 0; i < word.length(); ++i) {
        std::string letter(1, word[i]);
        float letterWidth = getTextWidth(letter, style);

        renderer->drawText(letter, currentX, 0, *textPaint, skFont);
        currentX += letterWidth + style.letterSpacing;
    }
}

void WordRenderer::drawWordShadow(const std::string& word, float wordWidth, const SubtitleTextStyle& style) const {
    if (!style.shadowColor.has_value()) return;

    const SkFont skFont = renderer->getFont({
        style.fontFamily,
        style.fontSize,
        style.bold,
        style.italic
    });

    // Shadow with stroke (if any)
    const PaintProps strokePaintProps{
        style.shadowColor.value(),
        style.shadowOpacity.value_or(1.0f),
        style.strokeWidth,
        style.shadowBlur
    };
    const SkPaint* strokePaint = renderer->getPaint(strokePaintProps);

    // Shadow fill
    const PaintProps shadowPaintProps{
        style.shadowColor.value(),
        style.shadowOpacity.value_or(1.0f),
        std::nullopt,
        style.shadowBlur
    };
    const SkPaint* shadowPaint = renderer->getPaint(shadowPaintProps);

    const float angle = style.shadowAngle.value_or(0) * M_PI / 180;
    const float shadowX = cos(angle) * style.shadowDistance.value_or(0);
    const float shadowY = sin(angle) * style.shadowDistance.value_or(0);

    float currentX = -wordWidth / 2 + shadowX;

    for (size_t i = 0; i < word.length(); ++i) {
        std::string letter(1, word[i]);
        const float letterWidth = getTextWidth(letter, style);

        renderer->drawText(letter, currentX, shadowY, *strokePaint, skFont);
        renderer->drawText(letter, currentX, shadowY, *shadowPaint, skFont);

        currentX += letterWidth + style.letterSpacing;
    }
}

void WordRenderer::drawWordStroke(const std::string& word, float wordWidth, const SubtitleTextStyle& style) const {
    if (!style.strokeWidth.has_value() || style.strokeWidth.value() <= 0 ||
        !style.strokeColor.has_value()) return;

    const SkFont skFont = renderer->getFont({
        style.fontFamily,
        style.fontSize,
        style.bold,
        style.italic
    });

    const PaintProps strokePaintProps{
        style.strokeColor.value(),
        style.strokeOpacity.value_or(1.0f),
        style.strokeWidth
    };
    const SkPaint* strokePaint = renderer->getPaint(strokePaintProps);

    float currentX = -wordWidth / 2;

    for (size_t i = 0; i < word.length(); ++i) {
        std::string letter(1, word[i]);
        const float letterWidth = getTextWidth(letter, style);

        renderer->drawText(letter, currentX, 0, *strokePaint, skFont);
        currentX += letterWidth + style.letterSpacing;
    }
}

} // namespace subtitle
} // namespace openshot