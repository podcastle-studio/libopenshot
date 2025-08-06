#include "subtitle/WordRenderer.h"
#include "subtitle/SkiaRenderer.h"
#include <skia/include/core/SkPath.h>
#include <skia/include/core/SkRRect.h>
#include <skia/include/core/SkFont.h>
#include <skia/include/core/SkFontMetrics.h>
#include <skia/include/core/SkTypeface.h>
#include <algorithm>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace openshot {
namespace subtitle {

WordRenderer::WordRenderer(SkiaRenderer* renderer) : renderer(renderer) {}

void WordRenderer::bubblePath(SkPath* path, float x, float y, float width, float height, float radius) {
    const float left = x;
    const float top = y;
    const float right = x + width;
    const float bottom = y + height;
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
    const double x, const double y, const double deltaY) const {
    const float wordWidth = getTextWidth(word, style);

    renderer->save();
    renderer->translate(x, y);
    drawWordBackground(wordWidth, style, deltaY);
    drawWordShadow(word, wordWidth, style);
    drawWordStroke(word, wordWidth, style);
    drawWordText(word, wordWidth, style);
    renderer->restore();
}

SkFont WordRenderer::getFontForCharacter(const std::string& utf8Char, const SubtitleTextStyle& style) const {
    SkUnichar unichar = 0; // Will hold the Unicode codepoint (e.g., U+0531 for Ա)
    const char *ptr = utf8Char.c_str();
    const size_t len = utf8Char.length();

    if (len > 0) {
        const unsigned char firstByte = ptr[0];

        // 1-byte character (ASCII): 0xxxxxxx
        if ((firstByte & 0x80) == 0) {  // Check if first bit is 0
            unichar = firstByte;  // Just use the byte as-is
        }
        // 2-byte character: 110xxxxx 10xxxxxx
        else if ((firstByte & 0xE0) == 0xC0 && len >= 2) {  // Check if starts with 110
            // Extract 5 bits from first byte and 6 bits from second byte
            unichar = ((firstByte & 0x1F) << 6) | (ptr[1] & 0x3F);
            // Example: Armenian Ա (U+0531) = [0xD4, 0xB1]
            // = ((0xD4 & 0x1F) << 6) | (0xB1 & 0x3F)
            // = (0x14 << 6) | 0x31
            // = 0x500 | 0x31 = 0x531
        }
        // 3-byte character: 1110xxxx 10xxxxxx 10xxxxxx
        else if ((firstByte & 0xF0) == 0xE0 && len >= 3) {  // Check if starts with 1110
            // Extract 4 bits from first, 6 from second, 6 from third
            unichar = ((firstByte & 0x0F) << 12) |    // 4 bits shifted by 12
                      ((ptr[1] & 0x3F) << 6) |         // 6 bits shifted by 6
                      (ptr[2] & 0x3F);                 // 6 bits
        }
        // 4-byte character: 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
        else if ((firstByte & 0xF8) == 0xF0 && len >= 4) {  // Check if starts with 11110
            // Extract 3 bits from first, 6 from second, 6 from third, 6 from fourth
            unichar = ((firstByte & 0x07) << 18) |    // 3 bits shifted by 18
                      ((ptr[1] & 0x3F) << 12) |        // 6 bits shifted by 12
                      ((ptr[2] & 0x3F) << 6) |         // 6 bits shifted by 6
                      (ptr[3] & 0x3F);                 // 6 bits
        }
    }

    // Now we have the Unicode codepoint, get a font that can render it
    return renderer->getFontForCharacter(
        {style.fontFamily, style.fontSize, style.bold, style.italic},
        unichar  // Pass the Unicode codepoint (e.g., 0x0531 for Ա)
    );
}

float WordRenderer::getTextWidth(const std::string& text, const SubtitleTextStyle& style) const {
    // Calculate width letter by letter with proper font fallback
    float totalWidth = 0;
    size_t i = 0;

    while (i < text.length()) {
        // Extract UTF-8 character
        size_t charLen = 1;
        const unsigned char firstByte = text[i];
        if ((firstByte & 0x80) == 0)            charLen = 1;
        else if ((firstByte & 0xE0) == 0xC0)    charLen = 2;
        else if ((firstByte & 0xF0) == 0xE0)    charLen = 3;
        else if ((firstByte & 0xF8) == 0xF0)    charLen = 4;

        std::string utf8Char = text.substr(i, charLen);
        SkFont charFont = getFontForCharacter(utf8Char, style);

        const float letterWidth = charFont.measureText(utf8Char.c_str(), utf8Char.length(), SkTextEncoding::kUTF8, nullptr);
        totalWidth += letterWidth;

        // Add letter spacing except after last character
        if (i + charLen < text.length()) {
            totalWidth += style.letterSpacing;
        }

        i += charLen;
    }

    return totalWidth;
}

TextBounds WordRenderer::getTextHeight(const std::string& text, const SubtitleTextStyle& style) const {
    const SkFont skFont = renderer->getFont({style.fontFamily, style.fontSize, style.bold, style.italic});

    SkFontMetrics metrics{};
    skFont.getMetrics(&metrics);

    return {
        metrics.fAscent,   // negative
        metrics.fDescent   // positive
    };
}

float WordRenderer::getSpaceWidth(const SubtitleTextStyle& style) const {
    const SkFont skFont = renderer->getFont({ style.fontFamily, style.fontSize, style.bold, style.italic});

    const float spaceWidth = skFont.measureText(" ", 1, SkTextEncoding::kUTF8, nullptr);
    return spaceWidth + style.letterSpacing;
}

void WordRenderer::drawBubbleBackground(const float wordWidth, const SubtitleTextStyle& style, const float deltaY) const {
    if (!style.backgroundColor.has_value()) return;

    const PaintProps bgPaintProps{style.backgroundColor.value(), style.backgroundOpacity.value_or(1.0f)};
    const SkPaint* bgPaint = renderer->getPaint(bgPaintProps);

    const PaintProps bgStrokePaintProps{"#000000", style.backgroundOpacity.value_or(1.0f) };
    const SkPaint* bgStrokePaint = renderer->getPaint(bgStrokePaintProps);

    const auto paddingX = style.backgroundPaddingX.value_or(0);
    const auto paddingY = style.backgroundPaddingY.value_or(0);
    const auto radius = style.backgroundRadius.value_or(0);

    const auto strokeWidth = style.fontSize * 0.05;

    // Shadow path
    SkPath pathShadow;
    bubblePath(
        &pathShadow,
        -wordWidth / 2 - paddingX - (2 * strokeWidth),
        -style.fontSize - paddingY + deltaY + (2 * strokeWidth),
        2 * paddingX + wordWidth,
        2 * paddingY + style.fontSize,
        radius
    );
    renderer->drawPath(pathShadow, *bgStrokePaint);

    // Stroke path
    SkPath pathStroke;
    bubblePath(
        &pathStroke,
        -wordWidth / 2 - paddingX,
        -style.fontSize - paddingY + deltaY,
        2 * paddingX + wordWidth,
        2 * paddingY + style.fontSize,
        radius
    );
    renderer->drawPath(pathStroke, *bgStrokePaint);

    // Fill path
    SkPath path;
    bubblePath(
        &path,
        -wordWidth / 2 - paddingX + strokeWidth,
        -style.fontSize - paddingY + deltaY + strokeWidth,
        2 * paddingX + wordWidth - (2 * strokeWidth),
        2 * paddingY + style.fontSize - (2 * strokeWidth),
        radius
    );
    renderer->drawPath(path, *bgPaint);
}

void WordRenderer::drawWordBackground(const float wordWidth, const SubtitleTextStyle& style, const float deltaY) const {
    if (!style.backgroundColor.has_value() || style.backgroundOpacity.value_or(0.0) <= 0.0) {
        return;
    }

    if (style.bubble.has_value() && style.bubble.value()) {
        drawBubbleBackground(wordWidth, style, deltaY);
        return;
    }

    const PaintProps bgPaintProps{style.backgroundColor.value(), style.backgroundOpacity.value_or(1.0f)} ;
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
    const PaintProps textPaintProps{style.color, style.opacity};
    const SkPaint* textPaint = renderer->getPaint(textPaintProps);

    // Render character by character with font fallback
    float currentX = -wordWidth / 2;
    size_t i = 0;

    while (i < word.length()) {
        // Extract UTF-8 character
        size_t charLen = 1;
        const unsigned char firstByte = word[i];
        if ((firstByte & 0x80) == 0)         charLen = 1;
        else if ((firstByte & 0xE0) == 0xC0) charLen = 2;
        else if ((firstByte & 0xF0) == 0xE0) charLen = 3;
        else if ((firstByte & 0xF8) == 0xF0) charLen = 4;

        std::string utf8Char = word.substr(i, charLen);
        SkFont charFont = getFontForCharacter(utf8Char, style);

        const float letterWidth = charFont.measureText(utf8Char.c_str(), utf8Char.length(), SkTextEncoding::kUTF8, nullptr);
        renderer->drawText(utf8Char, currentX, 0, *textPaint, charFont);

        currentX += letterWidth + style.letterSpacing;
        i += charLen;
    }
}

void WordRenderer::drawWordShadow(const std::string& word, const float wordWidth, const SubtitleTextStyle& style) const {
    if (!style.shadowColor.has_value()) {
        return;
    }

    // Shadow with stroke (if any)
    const PaintProps strokePaintProps{ style.shadowColor.value(), style.shadowOpacity.value_or(1.0f), style.strokeWidth, style.shadowBlur };
    const SkPaint* strokePaint = renderer->getPaint(strokePaintProps);

    // Shadow fill
    const PaintProps shadowPaintProps{ style.shadowColor.value(), style.shadowOpacity.value_or(1.0f), std::nullopt, style.shadowBlur };
    const SkPaint* shadowPaint = renderer->getPaint(shadowPaintProps);

    const float angle = style.shadowAngle.value_or(0) * M_PI / 180;
    const float shadowX = cos(angle) * style.shadowDistance.value_or(0);
    const float shadowY = sin(angle) * style.shadowDistance.value_or(0);

    float currentX = -wordWidth / 2 + shadowX;
    size_t i = 0;

    while (i < word.length()) {
        // Extract UTF-8 character
        size_t charLen = 1;
        unsigned char firstByte = word[i];
        if ((firstByte & 0x80) == 0) charLen = 1;
        else if ((firstByte & 0xE0) == 0xC0) charLen = 2;
        else if ((firstByte & 0xF0) == 0xE0) charLen = 3;
        else if ((firstByte & 0xF8) == 0xF0) charLen = 4;

        std::string utf8Char = word.substr(i, charLen);
        SkFont charFont = getFontForCharacter(utf8Char, style);

        const float letterWidth = charFont.measureText(utf8Char.c_str(), utf8Char.length(), SkTextEncoding::kUTF8, nullptr);

        renderer->drawText(utf8Char, currentX, shadowY, *strokePaint, charFont);
        renderer->drawText(utf8Char, currentX, shadowY, *shadowPaint, charFont);

        currentX += letterWidth + style.letterSpacing;
        i += charLen;
    }
}

void WordRenderer::drawWordStroke(const std::string& word, float wordWidth, const SubtitleTextStyle& style) const {
    if (!style.strokeWidth.has_value() || style.strokeWidth.value() <= 0 ||
        !style.strokeColor.has_value()) return;

    const PaintProps strokePaintProps{ style.strokeColor.value(), style.strokeOpacity.value_or(1.0f), style.strokeWidth };
    const SkPaint* strokePaint = renderer->getPaint(strokePaintProps);

    float currentX = -wordWidth / 2;
    size_t i = 0;

    while (i < word.length()) {
        // Extract UTF-8 character
        size_t charLen = 1;
        unsigned char firstByte = word[i];
        if ((firstByte & 0x80) == 0) charLen = 1;
        else if ((firstByte & 0xE0) == 0xC0) charLen = 2;
        else if ((firstByte & 0xF0) == 0xE0) charLen = 3;
        else if ((firstByte & 0xF8) == 0xF0) charLen = 4;

        std::string utf8Char = word.substr(i, charLen);
        SkFont charFont = getFontForCharacter(utf8Char, style);

        float letterWidth = charFont.measureText(utf8Char.c_str(), utf8Char.length(), SkTextEncoding::kUTF8, nullptr);

        renderer->drawText(utf8Char, currentX, 0, *strokePaint, charFont);
        currentX += letterWidth + style.letterSpacing;
        i += charLen;
    }
}

} // namespace subtitle
} // namespace openshot