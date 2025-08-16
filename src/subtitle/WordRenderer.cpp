#include "subtitle/WordRenderer.h"
#include "subtitle/SkiaRenderer.h"
#include <skia/include/core/SkPath.h>
#include <skia/include/core/SkRRect.h>
#include <skia/include/core/SkFont.h>
#include <skia/include/core/SkFontMetrics.h>
#include <skia/include/core/SkTypeface.h>
#include <algorithm>
#include <cmath>
#include <vector>

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
    SkUnichar unichar = 0;
    const char *ptr = utf8Char.c_str();
    const size_t len = utf8Char.length();

    if (len > 0) {
        const unsigned char firstByte = static_cast<unsigned char>(ptr[0]);

        // 1-byte character (ASCII): 0xxxxxxx
        if ((firstByte & 0x80) == 0) {
            unichar = firstByte;
        }
        // 2-byte character: 110xxxxx 10xxxxxx
        else if ((firstByte & 0xE0) == 0xC0 && len >= 2) {
            unichar = ((firstByte & 0x1F) << 6) | (static_cast<unsigned char>(ptr[1]) & 0x3F);
        }
        // 3-byte character: 1110xxxx 10xxxxxx 10xxxxxx
        else if ((firstByte & 0xF0) == 0xE0 && len >= 3) {
            unichar = ((firstByte & 0x0F) << 12) |
                      ((static_cast<unsigned char>(ptr[1]) & 0x3F) << 6) |
                      (static_cast<unsigned char>(ptr[2]) & 0x3F);
        }
        // 4-byte character: 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
        else if ((firstByte & 0xF8) == 0xF0 && len >= 4) {
            unichar = ((firstByte & 0x07) << 18) |
                      ((static_cast<unsigned char>(ptr[1]) & 0x3F) << 12) |
                      ((static_cast<unsigned char>(ptr[2]) & 0x3F) << 6) |
                      (static_cast<unsigned char>(ptr[3]) & 0x3F);
        }
    }

    return renderer->getFontForCharacter(
        {style.fontFamily, style.fontSize, style.fontWeight, style.italic},
        unichar
    );
}

// Helper structure to store character rendering information
struct CharRenderInfo {
    std::string utf8Char;
    SkFont font;
    float x;           // X position
    float advance;     // Advance width
};

// Build rendering info for all characters in the text
std::vector<WordRenderer::CharRenderInfo> WordRenderer::buildCharRenderInfo(
    const std::string& text, const SubtitleTextStyle& style) const {
    std::vector<CharRenderInfo> charInfos;

    float currentX = 0;
    size_t i = 0;

    while (i < text.length()) {
        // Extract UTF-8 character
        size_t charLen = 1;
        const unsigned char firstByte = static_cast<unsigned char>(text[i]);

        if ((firstByte & 0x80) == 0) {
            charLen = 1;
        } else if ((firstByte & 0xE0) == 0xC0) {
            charLen = 2;
        } else if ((firstByte & 0xF0) == 0xE0) {
            charLen = 3;
        } else if ((firstByte & 0xF8) == 0xF0) {
            charLen = 4;
        } else {
            // Invalid UTF-8 sequence
            i++;
            continue;
        }

        // Validate we have enough bytes
        if (i + charLen > text.length()) {
            break;
        }

        // Validate continuation bytes
        bool validUtf8 = true;
        for (size_t j = 1; j < charLen; j++) {
            unsigned char byte = static_cast<unsigned char>(text[i + j]);
            if ((byte & 0xC0) != 0x80) {
                validUtf8 = false;
                break;
            }
        }

        if (!validUtf8) {
            i++;
            continue;
        }

        CharRenderInfo info;
        info.utf8Char = text.substr(i, charLen);
        info.font = getFontForCharacter(info.utf8Char, style);
        info.x = currentX;

        // Get the glyph and its width
        SkGlyphID glyphs[10]; // Support for complex scripts that might produce multiple glyphs
        int glyphCount = info.font.textToGlyphs(info.utf8Char.c_str(), info.utf8Char.length(),
                                                SkTextEncoding::kUTF8, glyphs, 10);

        if (glyphCount > 0) {
            // Get widths for all glyphs
            std::vector<SkScalar> widths(glyphCount);
            info.font.getWidths(glyphs, glyphCount, widths.data());

            // Sum up all glyph widths (for complex scripts)
            info.advance = 0;
            for (int g = 0; g < glyphCount; g++) {
                info.advance += widths[g];
            }
        } else {
            // Fallback to measureText if glyph conversion fails
            info.advance = info.font.measureText(info.utf8Char.c_str(), info.utf8Char.length(),
                                                SkTextEncoding::kUTF8, nullptr);
        }

        charInfos.push_back(info);

        currentX += info.advance;

        // Add letter spacing except after last character
        if (i + charLen < text.length()) {
            currentX += style.letterSpacing;
        }

        i += charLen;
    }

    return charInfos;
}

float WordRenderer::getTextWidth(const std::string& text, const SubtitleTextStyle& style) const {
    if (text.empty()) return 0.0f;

    const auto charInfos = buildCharRenderInfo(text, style);
    if (charInfos.empty()) return 0.0f;

    // Total width is the x position of the last character plus its advance
    const auto& lastChar = charInfos.back();
    return lastChar.x + lastChar.advance;
}

TextBounds WordRenderer::getTextHeight(const std::string& text, const SubtitleTextStyle& style) const {
    const SkFont skFont = renderer->getFont({style.fontFamily, style.fontSize, style.fontWeight, style.italic});

    SkFontMetrics metrics{};
    skFont.getMetrics(&metrics);

    return {
        metrics.fAscent,   // negative
        metrics.fDescent   // positive
    };
}

float WordRenderer::getSpaceWidth(const SubtitleTextStyle& style) const {
    const SkFont skFont = renderer->getFont({ style.fontFamily, style.fontSize, style.fontWeight, style.italic});

    // Get the glyph for space
    SkGlyphID spaceGlyph;
    skFont.textToGlyphs(" ", 1, SkTextEncoding::kUTF8, &spaceGlyph, 1);

    // Get the width of the space glyph
    SkScalar spaceWidth;
    skFont.getWidths(&spaceGlyph, 1, &spaceWidth);

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

    // Use the same character rendering info that was used for measurement
    const auto charInfos = buildCharRenderInfo(word, style);
    const float startX = -wordWidth / 2;

    for (const auto& info : charInfos) {
        renderer->drawText(info.utf8Char, startX + info.x, 0, *textPaint, info.font);
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

    // Use the same character rendering info
    const auto charInfos = buildCharRenderInfo(word, style);
    const float startX = -wordWidth / 2 + shadowX;

    for (const auto& info : charInfos) {
        renderer->drawText(info.utf8Char, startX + info.x, shadowY, *strokePaint, info.font);
        renderer->drawText(info.utf8Char, startX + info.x, shadowY, *shadowPaint, info.font);
    }
}

void WordRenderer::drawWordStroke(const std::string& word, float wordWidth, const SubtitleTextStyle& style) const {
    if (!style.strokeWidth.has_value() || style.strokeWidth.value() <= 0 || !style.strokeColor.has_value()) return;

    const PaintProps strokePaintProps{ style.strokeColor.value(), style.strokeOpacity.value_or(1.0f), style.strokeWidth };
    const SkPaint* strokePaint = renderer->getPaint(strokePaintProps);

    // Use the same character rendering info
    const auto charInfos = buildCharRenderInfo(word, style);
    const float startX = -wordWidth / 2;

    for (const auto& info : charInfos) {
        renderer->drawText(info.utf8Char, startX + info.x, 0, *strokePaint, info.font);
    }
}

} // namespace subtitle
} // namespace openshot