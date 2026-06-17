#pragma once

// Shared low-level drawing/measurement helpers for the text-clip renderer family
// (static block, curved text, glow, and animation modules). These mirror the
// primitives the TypeScript reference exposes on its CanvasKitRenderer
// (toFontProps / drawLetter / getTextWidth) plus the small color/blur helpers
// from text-clip-style.helpers.ts, so every painter draws glyphs the same way:
// per-codepoint font fallback, letter-spacing-aware advances, mask-blur paints.

#include "TextClipTypes.h"
#include "../subtitle/SkiaRenderer.h"

#include <skia/include/core/SkFont.h>
#include <skia/include/core/SkPaint.h>
#include <skia/include/core/SkSpan.h>

#include <algorithm>
#include <cmath>
#include <string>

namespace openshot {
namespace text {

// ---------------------------------------------------------------------------
// UTF-8 iteration
// ---------------------------------------------------------------------------

// Byte length of the UTF-8 codepoint starting at s[i], or 1 if invalid.
inline size_t utf8CharLen(const std::string& s, size_t i) {
    if (i >= s.size()) return 0;
    const auto b = static_cast<unsigned char>(s[i]);
    if ((b & 0x80) == 0) return 1;
    if ((b & 0xE0) == 0xC0 && i + 1 < s.size()) return 2;
    if ((b & 0xF0) == 0xE0 && i + 2 < s.size()) return 3;
    if ((b & 0xF8) == 0xF0 && i + 3 < s.size()) return 4;
    return 1;
}

// Decode a UTF-8 codepoint at s[i] with already-known length `len`.
inline SkUnichar utf8Decode(const std::string& s, size_t i, size_t len) {
    if (len == 0) return 0;
    const char* p = s.c_str() + i;
    const auto b0 = static_cast<unsigned char>(p[0]);
    if (len == 1) return b0;
    if (len == 2) {
        return ((b0 & 0x1F) << 6) | (static_cast<unsigned char>(p[1]) & 0x3F);
    }
    if (len == 3) {
        return ((b0 & 0x0F) << 12) |
               ((static_cast<unsigned char>(p[1]) & 0x3F) << 6) |
               (static_cast<unsigned char>(p[2]) & 0x3F);
    }
    return ((b0 & 0x07) << 18) |
           ((static_cast<unsigned char>(p[1]) & 0x3F) << 12) |
           ((static_cast<unsigned char>(p[2]) & 0x3F) << 6) |
           (static_cast<unsigned char>(p[3]) & 0x3F);
}

// Iterate UTF-8 codepoints. `cb(letter, unichar)` is invoked once per codepoint.
template <typename Cb>
inline void forEachUtf8(const std::string& text, Cb&& cb) {
    size_t i = 0;
    while (i < text.size()) {
        const size_t len = utf8CharLen(text, i);
        if (len == 0) break;
        const std::string letter = text.substr(i, len);
        const SkUnichar uc = utf8Decode(text, i, len);
        cb(letter, uc);
        i += len;
    }
}

// Number of codepoints in a UTF-8 string (the unit the char-stagger counts in).
inline size_t utf8Length(const std::string& text) {
    size_t n = 0;
    forEachUtf8(text, [&](const std::string&, SkUnichar) { ++n; });
    return n;
}

// Walk a line's codepoints left to right, calling `draw(letter, letterX)` with each glyph's
// start X. `extraLetterSpacing` (animated word-mode spread) widens every gap except the last.
// Mirrors forEachLetter in text-layout-engine.ts.
template <typename Draw>
inline void forEachLetter(const TextClipLine& line, double x, double extraLetterSpacing, Draw&& draw) {
    if (line.text.empty()) return;
    const size_t n = line.letterAdvances.size();
    double cursor = x;
    size_t i = 0;
    forEachUtf8(line.text, [&](const std::string& letter, SkUnichar) {
        draw(letter, cursor);
        const double advance = i < n ? line.letterAdvances[i] : 0.0;
        cursor += advance + (i + 1 < n ? extraLetterSpacing : 0.0);
        ++i;
    });
}

// ---------------------------------------------------------------------------
// Font / glyph helpers
// ---------------------------------------------------------------------------

inline subtitle::FontProps toFontProps(const TextClipPaintStyle& style) {
    return subtitle::FontProps{
        style.fontFamily,
        style.fontSize,
        style.fontWeight > 0 ? style.fontWeight : 400,
        style.italic,
    };
}

// SkFont specialised for a Unicode character (with per-character fallback).
inline SkFont getFontForChar(subtitle::SkiaRenderer* renderer, const TextClipPaintStyle& style, SkUnichar uc) {
    return renderer->getFontForCharacter(toFontProps(style), uc);
}

// Sum of glyph widths for a single-codepoint `letter` in the given font.
inline double measureLetterAdvance(const SkFont& font, const std::string& letter) {
    SkGlyphID glyphs[8];
    const size_t count = font.textToGlyphs(
        letter.c_str(), letter.length(),
        SkTextEncoding::kUTF8, SkSpan<SkGlyphID>(glyphs));
    if (count == 0) {
        return font.measureText(letter.c_str(), letter.length(), SkTextEncoding::kUTF8, nullptr);
    }
    const size_t n = std::min<size_t>(count, 8);
    SkScalar widths[8] = {0};
    font.getWidths(SkSpan<const SkGlyphID>(glyphs, n), SkSpan<SkScalar>(widths, n));
    double advance = 0.0;
    for (size_t g = 0; g < n; ++g) advance += widths[g];
    return advance;
}

// Draw one codepoint `letter` (with per-character fallback font) at the given
// baseline-left position. Mirrors CanvasKitRenderer.drawLetter in the reference.
inline void drawLetter(
    subtitle::SkiaRenderer* renderer,
    const std::string& letter,
    double x,
    double baselineY,
    const SkPaint& paint,
    const TextClipPaintStyle& style)
{
    const size_t len = letter.empty() ? 0 : utf8CharLen(letter, 0);
    const SkUnichar uc = len ? utf8Decode(letter, 0, len) : 0;
    const SkFont font = getFontForChar(renderer, style, uc);
    renderer->drawText(letter, static_cast<float>(x), static_cast<float>(baselineY), paint, font);
}

// ---------------------------------------------------------------------------
// Misc numeric helpers (text-clip-style.helpers.ts)
// ---------------------------------------------------------------------------

inline double clamp01(double v) { return std::max(0.0, std::min(1.0, v)); }

// Two independent gaussian blurs compound in quadrature, not by simple addition.
inline double combineBlur(double a, double b) {
    return (a > 0.0 || b > 0.0) ? std::sqrt(a * a + b * b) : 0.0;
}

struct ParsedColor {
    std::string color;   // "#rrggbb" hex (or pass-through value)
    double opacity = 0.0;
};

// Mirrors parseColorOpacity in text-clip-style.helpers.ts: "rgba(...)" → hex + alpha,
// any other non-empty value passes through with opacity 1, empty → opacity 0.
ParsedColor parseColorOpacity(const std::string& colorValue);

// Draw the rounded background pill around a content box of the given size. The radius is
// (min(w,h)/2) * background.radius; padding extends the box outward by paddingX/Y.
void drawBackgroundRect(
    subtitle::SkiaRenderer* renderer,
    const TextClipBackgroundStyle& background,
    double originX,
    double originY,
    double contentWidth,
    double contentHeight);

} // namespace text
} // namespace openshot
