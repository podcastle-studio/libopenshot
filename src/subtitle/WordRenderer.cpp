#include "subtitle/WordRenderer.h"
#include "subtitle/SkiaRenderer.h"
#include <skia/include/core/SkPath.h>
#include <skia/include/core/SkPaint.h>
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

void WordRenderer::bubblePath(SkPath* path, float x, float y, float width, float height, float radius) {
    float left = x;
    float top = y;
    float right = x + width;
    float bottom = y + height;
    float r = std::max(0.0f, radius + 2);

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
                             double x, double y, double deltaY) {
    float wordWidth = getTextWidth(word, style);

    renderer->save();
    renderer->translate(x, y);
    drawWordBackground(wordWidth, style, deltaY);
    drawWordShadow(word, wordWidth, style);
    drawWordStroke(word, wordWidth, style);
    drawWordText(word, wordWidth, style);
    renderer->restore();
}

float WordRenderer::getTextWidth(const std::string& text, const SubtitleTextStyle& style) const {
    SkFont skFont = renderer->getFont({
        style.fontFamily,
        style.fontSize,
        style.bold,
        style.italic
    });

    // Calculate width letter by letter to include letter spacing
    float totalWidth = 0;
    for (size_t i = 0; i < text.length(); ++i) {
        std::string letter(1, text[i]);
        float letterWidth = skFont.measureText(letter.c_str(), letter.length(), SkTextEncoding::kUTF8, nullptr);
        totalWidth += letterWidth;

        // Add letter spacing except after last letter
        if (i < text.length() - 1) {
            totalWidth += style.letterSpacing;
        }
    }

    return totalWidth;
}

TextBounds WordRenderer::getTextHeight(const std::string& text, const SubtitleTextStyle& style) const {
    SkFont skFont = renderer->getFont({
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
    SkFont skFont = renderer->getFont({
        style.fontFamily,
        style.fontSize,
        style.bold,
        style.italic
    });

    float spaceWidth = skFont.measureText(" ", 1, SkTextEncoding::kUTF8, nullptr);
    return spaceWidth + style.letterSpacing;
}

void WordRenderer::drawBubbleBackground(float wordWidth, const SubtitleTextStyle& style, float deltaY) const {
    if (!style.backgroundColor.has_value()) return;

    PaintProps bgPaintProps{
        style.backgroundColor.value(),
        style.backgroundOpacity.value_or(1.0f)
    };
    SkPaint* bgPaint = renderer->getPaint(bgPaintProps);

    PaintProps bgStrokePaintProps{
        "#000000",
        style.backgroundOpacity.value_or(1.0f)
    };
    SkPaint* bgStrokePaint = renderer->getPaint(bgStrokePaintProps);

    float paddingX = style.backgroundPaddingX.value_or(0);
    float paddingY = style.backgroundPaddingY.value_or(0);
    float radius = style.backgroundRadius.value_or(0);

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

void WordRenderer::drawWordBackground(float wordWidth, const SubtitleTextStyle& style, float deltaY) const {
    if (!style.backgroundColor.has_value()) return;

    if (style.bubble.has_value() && style.bubble.value()) {
        drawBubbleBackground(wordWidth, style, deltaY);
        return;
    }

    PaintProps bgPaintProps{
        style.backgroundColor.value(),
        style.backgroundOpacity.value_or(1.0f)
    };
    SkPaint* bgPaint = renderer->getPaint(bgPaintProps);

    float paddingX = style.backgroundPaddingX.value_or(0);
    float paddingY = style.backgroundPaddingY.value_or(0);
    float radius = style.backgroundRadius.value_or(0);

    SkRect rect = renderer->makeRect(
        -wordWidth / 2 - paddingX + 2,
        -style.fontSize - paddingY + deltaY,
        wordWidth / 2 + paddingX - 2,
        paddingY + deltaY
    );

    SkRRect rrect = renderer->makeRRect(rect, radius, radius);
    renderer->drawRRect(rrect, *bgPaint);
}

void WordRenderer::drawWordText(const std::string& word, float wordWidth, const SubtitleTextStyle& style) const {
    PaintProps textPaintProps{style.color, style.opacity};
    SkPaint* textPaint = renderer->getPaint(textPaintProps);

    SkFont skFont = renderer->getFont({
        style.fontFamily,
        style.fontSize,
        style.bold,
        style.italic
    });

    // Render letter by letter for letter spacing
    float currentX = -wordWidth / 2;
    for (size_t i = 0; i < word.length(); ++i) {
        std::string letter(1, word[i]);
        float letterWidth = skFont.measureText(letter.c_str(), letter.length(), SkTextEncoding::kUTF8, nullptr);

        renderer->drawText(letter, currentX, 0, *textPaint, skFont);
        currentX += letterWidth + style.letterSpacing;
    }
}

void WordRenderer::drawWordShadow(const std::string& word, float wordWidth, const SubtitleTextStyle& style) const {
    if (!style.shadowColor.has_value()) return;

    SkFont skFont = renderer->getFont({
        style.fontFamily,
        style.fontSize,
        style.bold,
        style.italic
    });

    // Shadow with stroke (if any)
    PaintProps strokePaintProps{
        style.shadowColor.value(),
        style.shadowOpacity.value_or(1.0f),
        style.strokeWidth,
        style.shadowBlur
    };
    SkPaint* strokePaint = renderer->getPaint(strokePaintProps);

    // Shadow fill
    PaintProps shadowPaintProps{
        style.shadowColor.value(),
        style.shadowOpacity.value_or(1.0f),
        std::nullopt,
        style.shadowBlur
    };
    SkPaint* shadowPaint = renderer->getPaint(shadowPaintProps);

    float angle = style.shadowAngle.value_or(0) * M_PI / 180;
    float shadowX = cos(angle) * style.shadowDistance.value_or(0);
    float shadowY = sin(angle) * style.shadowDistance.value_or(0);

    float currentX = -wordWidth / 2 + shadowX;

    for (size_t i = 0; i < word.length(); ++i) {
        std::string letter(1, word[i]);
        float letterWidth = skFont.measureText(letter.c_str(), letter.length(), SkTextEncoding::kUTF8, nullptr);

        renderer->drawText(letter, currentX, shadowY, *strokePaint, skFont);
        renderer->drawText(letter, currentX, shadowY, *shadowPaint, skFont);

        currentX += letterWidth + style.letterSpacing;
    }
}

void WordRenderer::drawWordStroke(const std::string& word, float wordWidth, const SubtitleTextStyle& style) const {
    if (!style.strokeWidth.has_value() || style.strokeWidth.value() <= 0 ||
        !style.strokeColor.has_value()) return;

    SkFont skFont = renderer->getFont({
        style.fontFamily,
        style.fontSize,
        style.bold,
        style.italic
    });

    PaintProps strokePaintProps{
        style.strokeColor.value(),
        style.strokeOpacity.value_or(1.0f),
        style.strokeWidth
    };
    SkPaint* strokePaint = renderer->getPaint(strokePaintProps);

    float currentX = -wordWidth / 2;

    for (size_t i = 0; i < word.length(); ++i) {
        std::string letter(1, word[i]);
        float letterWidth = skFont.measureText(letter.c_str(), letter.length(), SkTextEncoding::kUTF8, nullptr);

        renderer->drawText(letter, currentX, 0, *strokePaint, skFont);
        currentX += letterWidth + style.letterSpacing;
    }
}

} // namespace subtitle
} // namespace openshot