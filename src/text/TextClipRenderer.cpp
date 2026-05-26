#include "TextClipRenderer.h"

#include "../subtitle/SkiaRenderer.h"

#include <skia/include/core/SkCanvas.h>
#include <skia/include/core/SkFont.h>
#include <skia/include/core/SkPaint.h>
#include <skia/include/core/SkRRect.h>
#include <skia/include/core/SkRect.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <sstream>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace openshot {
namespace text {

namespace {

constexpr char SPACE = ' ';

// ---------------------------------------------------------------------------
// UTF-8 helpers
// ---------------------------------------------------------------------------

// Returns the byte length of the UTF-8 codepoint starting at `s[i]`,
// or 1 if invalid (caller advances past one byte to keep going).
size_t utf8CharLen(const std::string& s, size_t i) {
    if (i >= s.size()) return 0;
    const auto b = static_cast<unsigned char>(s[i]);
    if ((b & 0x80) == 0) return 1;
    if ((b & 0xE0) == 0xC0 && i + 1 < s.size()) return 2;
    if ((b & 0xF0) == 0xE0 && i + 2 < s.size()) return 3;
    if ((b & 0xF8) == 0xF0 && i + 3 < s.size()) return 4;
    return 1;
}

// Decode a UTF-8 codepoint at `s[i]` (with already-known length `len`).
SkUnichar utf8Decode(const std::string& s, size_t i, size_t len) {
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
void forEachUtf8(const std::string& text, Cb&& cb) {
    size_t i = 0;
    while (i < text.size()) {
        size_t len = utf8CharLen(text, i);
        if (len == 0) break;
        const std::string letter = text.substr(i, len);
        const SkUnichar uc = utf8Decode(text, i, len);
        cb(letter, uc);
        i += len;
    }
}

// Convert a paint style into FontProps consumable by SkiaRenderer.
subtitle::FontProps toFontProps(const TextClipPaintStyle& style) {
    return subtitle::FontProps{
        style.fontFamily,
        style.fontSize,
        style.fontWeight > 0 ? style.fontWeight : 400,
        style.italic,
    };
}

// Get a SkFont specialised for a given Unicode character (with fallback support).
SkFont getFontForChar(subtitle::SkiaRenderer* renderer, const TextClipPaintStyle& style, SkUnichar uc) {
    return renderer->getFontForCharacter(toFontProps(style), uc);
}

// Sum of glyph widths for `letter` (single codepoint) in the given font.
double measureLetterAdvance(const SkFont& font, const std::string& letter) {
    SkGlyphID glyphs[8];
    const int count = font.textToGlyphs(
        letter.c_str(), letter.length(),
        SkTextEncoding::kUTF8, glyphs, 8);
    if (count <= 0) {
        // Fallback to measureText
        return font.measureText(letter.c_str(), letter.length(), SkTextEncoding::kUTF8, nullptr);
    }
    SkScalar widths[8] = {0};
    font.getWidths(glyphs, count, widths);
    double advance = 0.0;
    for (int g = 0; g < count; ++g) advance += widths[g];
    return advance;
}

// ---------------------------------------------------------------------------
// Color parsing: TS parseColorOpacity
// ---------------------------------------------------------------------------

struct ParsedColor {
    std::string color;   // "#rrggbb" hex
    double opacity = 0.0;
};

std::string trimCopy(const std::string& s) {
    auto begin = s.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return "";
    auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(begin, end - begin + 1);
}

ParsedColor parseColorOpacity(const std::string& colorValue) {
    if (colorValue.empty()) return {std::string(), 0.0};

    if (colorValue.rfind("rgba", 0) == 0 || colorValue.rfind("RGBA", 0) == 0) {
        // Strip "rgba(" and ")"
        const auto open = colorValue.find('(');
        const auto close = colorValue.find(')');
        if (open == std::string::npos || close == std::string::npos || close <= open) {
            return {std::string(), 0.0};
        }
        const std::string inner = colorValue.substr(open + 1, close - open - 1);
        std::stringstream ss(inner);
        std::string part;
        int idx = 0;
        int r = 0, g = 0, b = 0;
        double a = 1.0;
        while (std::getline(ss, part, ',')) {
            const auto v = trimCopy(part);
            try {
                if (idx == 0) r = std::stoi(v);
                else if (idx == 1) g = std::stoi(v);
                else if (idx == 2) b = std::stoi(v);
                else if (idx == 3) a = std::stod(v);
            } catch (...) {}
            ++idx;
        }
        char buf[8];
        std::snprintf(buf, sizeof(buf), "#%02x%02x%02x",
            std::clamp(r, 0, 255), std::clamp(g, 0, 255), std::clamp(b, 0, 255));
        return {std::string(buf), a};
    }

    return {colorValue, 1.0};
}

} // namespace

// ---------------------------------------------------------------------------
// transformTextValue
// ---------------------------------------------------------------------------

std::string transformTextValue(const std::string& value, TextTransform transform) {
    switch (transform) {
    case TextTransform::UPPERCASE: {
        std::string out;
        out.reserve(value.size());
        for (char c : value) out.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
        return out;
    }
    case TextTransform::LOWERCASE: {
        std::string out;
        out.reserve(value.size());
        for (char c : value) out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        return out;
    }
    case TextTransform::CAPITALIZE: {
        // TS: value.toLowerCase().replace(/\b\w/g, c => c.toUpperCase())
        std::string out;
        out.reserve(value.size());
        bool prevWordChar = false;
        for (char c : value) {
            const auto uc = static_cast<unsigned char>(c);
            const bool isWord = std::isalnum(uc) || uc == '_';
            char lower = static_cast<char>(std::tolower(uc));
            if (isWord && !prevWordChar) {
                lower = static_cast<char>(std::toupper(uc));
            }
            out.push_back(lower);
            prevWordChar = isWord;
        }
        return out;
    }
    default:
        return value;
    }
}

// ---------------------------------------------------------------------------
// alignmentOffsetX
// ---------------------------------------------------------------------------

double alignmentOffsetX(TextAlignment alignment, double width) {
    switch (alignment) {
    case TextAlignment::LEFT:  return  width / 2.0;
    case TextAlignment::RIGHT: return -width / 2.0;
    case TextAlignment::CENTER:
    default:                   return 0.0;
    }
}

// ---------------------------------------------------------------------------
// convertTextStyleToPaintStyle / convertBackgroundStyle
// ---------------------------------------------------------------------------

TextClipPaintStyle convertTextStyleToPaintStyle(
    const TextClipStyle& style,
    const TextTransformation& transformation,
    double projectWidth)
{
    TextClipPaintStyle paint;
    paint.fontFamily = style.fontFamily;
    paint.fontSize = projectWidth * SIZE_BASE_COEFFICIENT * transformation.size;
    paint.fontWeight = style.fontWeight;
    paint.italic = style.italic;
    paint.color = style.color;
    paint.letterSpacing = style.letterSpacing * paint.fontSize;
    paint.lineHeight = style.lineHeight * paint.fontSize;
    paint.alignment = style.textAlign;

    if (style.strokeColor.has_value() && style.strokeWidthRatio > 0.0) {
        paint.stroke = TextClipStrokeStyle{
            *style.strokeColor,
            style.strokeWidthRatio * paint.fontSize,
        };
    }

    if (style.shadowColor.has_value()) {
        const auto parsed = parseColorOpacity(*style.shadowColor);
        if (parsed.opacity > 0.0) {
            paint.dropShadow = TextClipShadowStyle{
                parsed.color,
                parsed.opacity,
                style.shadowAngle,
                style.shadowDistanceRatio * paint.fontSize,
                style.shadowBlurRatio * paint.fontSize,
            };
        }
    }

    return paint;
}

std::optional<TextClipBackgroundStyle> convertBackgroundStyle(
    const TextClipStyle& style,
    const TextClipPaintStyle& paint)
{
    if (!style.backgroundColor.has_value()) return std::nullopt;
    const auto parsed = parseColorOpacity(*style.backgroundColor);
    if (parsed.opacity <= 0.0) return std::nullopt;

    TextClipBackgroundStyle bg;
    bg.color = parsed.color;
    bg.opacity = parsed.opacity;
    bg.paddingX = style.backgroundPaddingXRatio * paint.fontSize;
    bg.paddingY = style.backgroundPaddingYRatio * paint.fontSize;
    bg.radius = style.backgroundRadiusRatio;
    return bg;
}

// ---------------------------------------------------------------------------
// measureLetterWidths
// ---------------------------------------------------------------------------

std::vector<double> measureLetterWidths(
    const std::string& text,
    const TextClipPaintStyle& style,
    subtitle::SkiaRenderer* renderer)
{
    std::vector<double> widths;
    if (text.empty() || !renderer) return widths;

    forEachUtf8(text, [&](const std::string& letter, SkUnichar uc) {
        const SkFont font = getFontForChar(renderer, style, uc);
        widths.push_back(measureLetterAdvance(font, letter));
    });

    return widths;
}

std::vector<double> computeLetterAdvances(
    const std::string& text,
    const TextClipPaintStyle& style,
    subtitle::SkiaRenderer* renderer)
{
    const auto widths = measureLetterWidths(text, style, renderer);
    if (widths.empty()) return {};
    std::vector<double> out;
    out.reserve(widths.size());
    for (size_t i = 0; i < widths.size(); ++i) {
        // Advance after letter i = glyphWidth + letterSpacing, except the final letter
        // which has no trailing letter-spacing — matches the measureWidth invariant.
        const bool isLast = (i + 1 == widths.size());
        out.push_back(isLast ? widths[i] : widths[i] + style.letterSpacing);
    }
    return out;
}

VerticalBounds measureTextVerticalBounds(
    const std::string& text,
    const TextClipPaintStyle& style,
    subtitle::SkiaRenderer* renderer)
{
    VerticalBounds out;
    if (text.empty() || !renderer) return out;

    bool found = false;
    forEachUtf8(text, [&](const std::string& letter, SkUnichar uc) {
        const SkFont font = getFontForChar(renderer, style, uc);
        SkGlyphID glyphs[8];
        const int count = font.textToGlyphs(
            letter.c_str(), letter.length(),
            SkTextEncoding::kUTF8, glyphs, 8);
        if (count <= 0) return;
        SkRect bounds[8];
        font.getBounds(glyphs, count, bounds, nullptr);
        for (int g = 0; g < count; ++g) {
            const double t = static_cast<double>(bounds[g].fTop);
            const double b = static_cast<double>(bounds[g].fBottom);
            if (!found) {
                out.top = t;
                out.bottom = b;
                found = true;
            } else {
                out.top = std::min(out.top, t);
                out.bottom = std::max(out.bottom, b);
            }
        }
    });
    return out;
}

// ---------------------------------------------------------------------------
// Layout engine
// ---------------------------------------------------------------------------

namespace {

double measureWidth(const std::string& text, const TextClipPaintStyle& style, subtitle::SkiaRenderer* renderer) {
    if (text.empty()) return 0.0;
    const auto widths = measureLetterWidths(text, style, renderer);
    if (widths.empty()) return 0.0;
    double total = 0.0;
    for (double w : widths) total += w;
    if (widths.size() <= 1) return total;
    return total + static_cast<double>(widths.size() - 1) * style.letterSpacing;
}

// Split paragraph by ASCII space (matching TS .split(' ')).
std::vector<std::string> splitBySpace(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == ' ') {
            out.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    out.push_back(cur);
    return out;
}

std::vector<std::string> splitByNewline(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == '\n') {
            out.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    out.push_back(cur);
    return out;
}

// Append a chunk to the running line, breaking before the chunk if it would overflow.
// Used for whole-token placements.
void appendChunk(
    const std::string& chunk,
    double chunkWidth,
    bool withSeparator,
    double spaceWidth,
    double maxWidth,
    double letterSpacing,
    std::vector<TextClipLine>& lines,
    std::string& currentText,
    double& currentWidth)
{
    const double sepW = withSeparator ? spaceWidth : 0.0;
    const std::string sep = withSeparator ? std::string(1, SPACE) : std::string();

    // letterSpacing is the gap between every adjacent pair of characters — including
    // across the joining space. Gluing currentText + separator + chunk introduces up
    // to two new gaps not present in currentWidth / separatorWidth / chunkWidth:
    //   leadingGap : between tail of currentText and the joining space
    //   trailingGap: between the joining space and head of chunk
    const double leadingGap  = (withSeparator && !currentText.empty()) ? letterSpacing : 0.0;
    const double trailingGap = (withSeparator && !chunk.empty())       ? letterSpacing : 0.0;
    const double total = currentWidth + leadingGap + sepW + trailingGap + chunkWidth;

    if (!currentText.empty() && total > maxWidth) {
        lines.push_back({currentText, currentWidth, {}});
        currentText = chunk;
        currentWidth = chunkWidth;
        return;
    }

    currentText += sep + chunk;
    currentWidth += leadingGap + sepW + trailingGap + chunkWidth;
}

// Character-level breaking for tokens wider than maxWidth.
void appendBrokenToken(
    const std::string& token,
    const TextClipPaintStyle& style,
    double maxWidth,
    bool withSeparator,
    double spaceWidth,
    subtitle::SkiaRenderer* renderer,
    std::vector<TextClipLine>& lines,
    std::string& currentText,
    double& currentWidth)
{
    // Build per-codepoint advance widths and the codepoint letters.
    std::vector<std::string> letters;
    std::vector<double> letterWidths;
    forEachUtf8(token, [&](const std::string& letter, SkUnichar uc) {
        const SkFont font = getFontForChar(renderer, style, uc);
        letters.push_back(letter);
        letterWidths.push_back(measureLetterAdvance(font, letter));
    });

    std::string workingText = currentText;
    double workingWidth = currentWidth;

    auto pushWorking = [&]() {
        lines.push_back({workingText, workingWidth, {}});
        workingText.clear();
        workingWidth = 0.0;
    };

    bool separatorPending = withSeparator && !workingText.empty();

    for (size_t i = 0; i < letters.size(); ++i) {
        const std::string& letter = letters[i];
        const double letterWidth = letterWidths[i];
        const double additionalSpacing = !workingText.empty() ? style.letterSpacing : 0.0;
        const double separatorWidth = separatorPending ? spaceWidth : 0.0;
        const double projectedWidth = workingWidth + separatorWidth + additionalSpacing + letterWidth;

        if (!workingText.empty() && projectedWidth > maxWidth) {
            pushWorking();
            separatorPending = false;
            workingText = letter;
            workingWidth = letterWidth;
            continue;
        }

        if (separatorPending) {
            workingText += SPACE;
            workingWidth += spaceWidth;
            separatorPending = false;
        }

        if (!workingText.empty()) {
            workingWidth += style.letterSpacing;
        }
        workingText += letter;
        workingWidth += letterWidth;
    }

    currentText = workingText;
    currentWidth = workingWidth;
}

std::vector<TextClipLine> layoutParagraph(
    const std::string& paragraph,
    const TextClipPaintStyle& style,
    double maxWidth,
    subtitle::SkiaRenderer* renderer)
{
    const auto tokens = splitBySpace(paragraph);
    const double spaceWidth = measureWidth(std::string(1, SPACE), style, renderer);

    std::vector<TextClipLine> lines;
    std::string currentText;
    double currentWidth = 0.0;

    auto pushCurrent = [&]() {
        if (currentText.empty() && currentWidth == 0.0) return;
        lines.push_back({currentText, currentWidth, {}});
        currentText.clear();
        currentWidth = 0.0;
    };

    for (size_t ti = 0; ti < tokens.size(); ++ti) {
        const auto& token = tokens[ti];
        const bool withSeparator = ti > 0;

        if (token.empty()) {
            // Preserve consecutive spaces.
            appendChunk("", 0.0, true, spaceWidth, maxWidth, style.letterSpacing,
                        lines, currentText, currentWidth);
            continue;
        }

        const double tokenWidth = measureWidth(token, style, renderer);

        if (tokenWidth <= maxWidth) {
            appendChunk(token, tokenWidth, withSeparator, spaceWidth, maxWidth, style.letterSpacing,
                        lines, currentText, currentWidth);
            continue;
        }

        // Token is wider than maxWidth → break by characters.
        if (!currentText.empty()) {
            pushCurrent();
        }

        appendBrokenToken(token, style, maxWidth, /*withSeparator=*/false, spaceWidth,
                          renderer, lines, currentText, currentWidth);
    }

    pushCurrent();
    return lines;
}

} // namespace

TextClipLayout layoutText(
    const std::string& text,
    const TextClipPaintStyle& style,
    double wrapWidth,
    double userMaxWidth,
    subtitle::SkiaRenderer* renderer)
{
    const auto paragraphs = splitByNewline(text);
    std::vector<TextClipLine> lines;

    for (size_t pi = 0; pi < paragraphs.size(); ++pi) {
        const auto& paragraph = paragraphs[pi];

        if (paragraph.empty()) {
            lines.push_back({"", 0.0, {}});
            continue;
        }

        const auto pLines = layoutParagraph(paragraph, style, wrapWidth, renderer);
        for (const auto& line : pLines) lines.push_back(line);

        if (pi + 1 < paragraphs.size() && pLines.empty()) {
            lines.push_back({"", 0.0, {}});
        }
    }

    if (lines.empty()) {
        lines.push_back({"", 0.0, {}});
    }

    // Populate per-codepoint letter advances for each line at the supplied paint.
    // When the caller is `layoutTextAtReferenceSize`, these are reference-space advances
    // which scaleLayout converts back into actual pixel space.
    for (auto& line : lines) {
        line.letterAdvances = computeLetterAdvances(line.text, style, renderer);
    }

    // Vertical metrics: use real glyph bounds from a line that actually has ink. Whitespace-
    // only lines would collapse to zero bounds because spaces carry no glyph extents; in that
    // case fall back to the "Hg" probe so we still get a font-nominal ascent / descent.
    auto hasInk = [](const std::string& s) {
        for (char c : s) {
            if (!std::isspace(static_cast<unsigned char>(c))) return true;
        }
        return false;
    };

    const std::string VERTICAL_METRICS_PROBE = "Hg";
    std::string firstNonEmpty = VERTICAL_METRICS_PROBE;
    for (const auto& l : lines) {
        if (hasInk(l.text)) { firstNonEmpty = l.text; break; }
    }
    std::string lastNonEmpty = VERTICAL_METRICS_PROBE;
    for (auto it = lines.rbegin(); it != lines.rend(); ++it) {
        if (hasInk(it->text)) { lastNonEmpty = it->text; break; }
    }

    const VerticalBounds firstBounds = measureTextVerticalBounds(firstNonEmpty, style, renderer);
    const VerticalBounds lastBounds  = (firstNonEmpty == lastNonEmpty)
        ? firstBounds
        : measureTextVerticalBounds(lastNonEmpty, style, renderer);

    // top is negative (above baseline); bottom is positive (below baseline). Convert to
    // positive distances.
    const double firstLineAscent = -firstBounds.top;
    const double lastLineDescent = lastBounds.bottom;

    double textWidth = 0.0;
    for (const auto& l : lines) textWidth = std::max(textWidth, l.width);

    // Single line collapses to (ascent + descent) — the exact ink height.
    const double textHeight = firstLineAscent
        + static_cast<double>(lines.size() - 1) * style.lineHeight
        + lastLineDescent;

    const double layoutWidth = userMaxWidth > 0.0 ? std::max(textWidth, userMaxWidth) : textWidth;

    return TextClipLayout{lines, style.lineHeight, textWidth, textHeight, layoutWidth, firstLineAscent};
}

TextClipLayout scaleLayout(const TextClipLayout& layout, double sizeScale) {
    if (sizeScale == 1.0) return layout;

    TextClipLayout out;
    out.lines.reserve(layout.lines.size());
    for (const auto& line : layout.lines) {
        TextClipLine scaled;
        scaled.text = line.text;
        scaled.width = line.width * sizeScale;
        scaled.letterAdvances.reserve(line.letterAdvances.size());
        for (double a : line.letterAdvances) {
            scaled.letterAdvances.push_back(a * sizeScale);
        }
        out.lines.push_back(std::move(scaled));
    }
    out.lineHeight      = layout.lineHeight      * sizeScale;
    out.textWidth       = layout.textWidth       * sizeScale;
    out.textHeight      = layout.textHeight      * sizeScale;
    out.layoutWidth     = layout.layoutWidth     * sizeScale;
    out.firstLineAscent = layout.firstLineAscent * sizeScale;
    return out;
}

TextClipLayout layoutTextAtReferenceSize(
    const std::string& text,
    const TextClipPaintStyle& paint,
    const TextClipPaintStyle& layoutPaint,
    double wrapWidth,
    double userMaxWidth,
    subtitle::SkiaRenderer* renderer)
{
    // paint.fontSize / layoutPaint.fontSize == transformation.size / LAYOUT_REFERENCE_SIZE,
    // since both paints come from the same convertTextStyleToPaintStyle call with the same
    // projectWidth and only differ in transformation.size.
    const double sizeScale = layoutPaint.fontSize > 0.0
        ? paint.fontSize / layoutPaint.fontSize
        : 1.0;
    const double refWrapWidth    = wrapWidth    / sizeScale;
    const double refUserMaxWidth = userMaxWidth / sizeScale;
    const auto referenceLayout = layoutText(text, layoutPaint, refWrapWidth, refUserMaxWidth, renderer);
    return scaleLayout(referenceLayout, sizeScale);
}

double getLineStartX(const TextClipLine& line, const TextClipLayout& layout, TextAlignment alignment) {
    switch (alignment) {
    case TextAlignment::LEFT:  return 0.0;
    case TextAlignment::RIGHT: return layout.layoutWidth - line.width;
    case TextAlignment::CENTER:
    default:                   return (layout.layoutWidth - line.width) / 2.0;
    }
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

namespace {

// Walk the codepoints of `line.text` left-to-right, using the precomputed
// `line.letterAdvances` as cursor advances. The advances were measured at the
// layout (reference-size) paint and scaled to actual pixel space, so they MUST
// be consumed as-is — re-measuring at the live paint would reintroduce the
// per-`Font`-instance quantization drift the line-calculation patch eliminates.
void drawLetterRun(
    const TextClipLine& line,
    const TextClipPaintStyle& style,
    double startX,
    double baselineY,
    const SkPaint& paint,
    subtitle::SkiaRenderer* renderer,
    double deltaX = 0.0,
    double deltaY = 0.0)
{
    if (line.text.empty()) return;
    double cursor = startX;
    size_t idx = 0;
    forEachUtf8(line.text, [&](const std::string& letter, SkUnichar uc) {
        const SkFont font = getFontForChar(renderer, style, uc);
        renderer->drawText(letter, static_cast<float>(cursor + deltaX),
                           static_cast<float>(baselineY + deltaY), paint, font);
        cursor += idx < line.letterAdvances.size() ? line.letterAdvances[idx] : 0.0;
        ++idx;
    });
}

void drawTextLine(
    const TextClipLine& line,
    const TextClipPaintStyle& style,
    double x,
    double baselineY,
    subtitle::SkiaRenderer* renderer)
{
    const subtitle::PaintProps props{style.color, 1.0, std::nullopt, std::nullopt};
    const SkPaint* paint = renderer->getPaint(props);
    drawLetterRun(line, style, x, baselineY, *paint, renderer);
}

void drawStrokeLine(
    const TextClipLine& line,
    const TextClipPaintStyle& style,
    const TextClipStrokeStyle& stroke,
    double x,
    double baselineY,
    subtitle::SkiaRenderer* renderer)
{
    const subtitle::PaintProps props{stroke.color, 1.0, stroke.width, std::nullopt};
    const SkPaint* paint = renderer->getPaint(props);
    drawLetterRun(line, style, x, baselineY, *paint, renderer);
}

void drawShadowLine(
    const TextClipLine& line,
    const TextClipPaintStyle& style,
    const TextClipShadowStyle& shadow,
    double x,
    double baselineY,
    subtitle::SkiaRenderer* renderer)
{
    const double radians = shadow.angle * M_PI / 180.0;
    const double dx = std::cos(radians) * shadow.distance;
    const double dy = std::sin(radians) * shadow.distance;

    const subtitle::PaintProps props{
        shadow.color,
        shadow.opacity,
        std::nullopt,
        shadow.blur > 0.0 ? std::optional<double>(shadow.blur) : std::nullopt,
    };
    const SkPaint* paint = renderer->getPaint(props);
    drawLetterRun(line, style, x, baselineY, *paint, renderer, dx, dy);
}

void drawBackgroundRect(
    const TextClipLayout& layout,
    const TextClipBackgroundStyle& background,
    double originX,
    double originY,
    subtitle::SkiaRenderer* renderer)
{
    const double width = layout.layoutWidth + 2.0 * background.paddingX;
    const double height = layout.textHeight + 2.0 * background.paddingY;
    const double radius = (std::min(width, height) / 2.0) * background.radius;

    const subtitle::PaintProps props{background.color, background.opacity, std::nullopt, std::nullopt};
    const SkPaint* paint = renderer->getPaint(props);

    const SkRect rect = SkRect::MakeLTRB(
        static_cast<float>(originX - background.paddingX),
        static_cast<float>(originY - background.paddingY),
        static_cast<float>(originX + layout.layoutWidth + background.paddingX),
        static_cast<float>(originY + layout.textHeight + background.paddingY));
    const SkRRect rrect = SkRRect::MakeRectXY(rect, static_cast<float>(radius), static_cast<float>(radius));
    renderer->drawRRect(rrect, *paint);
}

void drawLine(
    const TextClipLine& line,
    const TextClipPaintStyle& style,
    double x,
    double baselineY,
    subtitle::SkiaRenderer* renderer)
{
    if (style.dropShadow.has_value()) {
        drawShadowLine(line, style, *style.dropShadow, x, baselineY, renderer);
    }
    if (style.stroke.has_value()) {
        drawStrokeLine(line, style, *style.stroke, x, baselineY, renderer);
    }
    drawTextLine(line, style, x, baselineY, renderer);
}

} // namespace

void renderLayout(
    const TextClipLayout& layout,
    const TextClipPaintStyle& paint,
    const std::optional<TextClipBackgroundStyle>& background,
    double originX,
    double originY,
    subtitle::SkiaRenderer* renderer)
{
    if (background.has_value()) {
        drawBackgroundRect(layout, *background, originX, originY, renderer);
    }

    // First baseline sits `firstLineAscent` below the top of the text block, so the visible
    // top of line 0's glyphs lines up exactly with originY. Subsequent baselines step down
    // by lineHeight (the inter-baseline advance).
    const double firstBaselineY = originY + layout.firstLineAscent;

    for (size_t li = 0; li < layout.lines.size(); ++li) {
        const auto& line = layout.lines[li];
        if (line.text.empty()) continue;

        const double x = originX + getLineStartX(line, layout, paint.alignment);
        const double baselineY = firstBaselineY + static_cast<double>(li) * layout.lineHeight;
        drawLine(line, paint, x, baselineY, renderer);
    }
}

// ---------------------------------------------------------------------------
// renderTextClip (top-level)
// ---------------------------------------------------------------------------

RenderResult renderTextClip(
    const TextClipData& clipData,
    double projectWidth,
    subtitle::SkiaRenderer* renderer,
    double originX,
    double originY)
{
    const std::string text = transformTextValue(clipData.value, clipData.style.textTransform);

    // `paint` rasterizes glyphs at the live size; `layoutPaint` drives line breaks at the
    // fixed LAYOUT_REFERENCE_SIZE so wrap decisions stop depending on Skia's per-Font
    // glyph-advance quantization. Both come from the same helper with the same projectWidth
    // and only differ in transformation.size.
    const TextClipPaintStyle paint = convertTextStyleToPaintStyle(
        clipData.style, clipData.transformation, projectWidth);

    TextTransformation refTransformation = clipData.transformation;
    refTransformation.size = LAYOUT_REFERENCE_SIZE;
    const TextClipPaintStyle layoutPaint = convertTextStyleToPaintStyle(
        clipData.style, refTransformation, projectWidth);

    const auto background = convertBackgroundStyle(clipData.style, paint);

    // maxWidth is a dimensionless multiplier of the canvas-and-size scale; convert to pixels.
    //   wrapWidthPx = projectWidth * SIZE_BASE_COEFFICIENT * size * maxWidth
    // 0 is the explicit "no wrap" sentinel.
    const double maxWidthPx = clipData.transformation.maxWidth > 0.0
        ? projectWidth * SIZE_BASE_COEFFICIENT * clipData.transformation.size * clipData.transformation.maxWidth
        : 0.0;
    const double wrapWidth    = maxWidthPx > 0.0 ? maxWidthPx : 1e9;
    const double userMaxWidth = maxWidthPx;

    TextClipLayout layout = layoutTextAtReferenceSize(
        text, paint, layoutPaint, wrapWidth, userMaxWidth, renderer);

    const double paddingX = background.has_value() ? background->paddingX : 0.0;
    const double paddingY = background.has_value() ? background->paddingY : 0.0;

    RenderResult result;
    result.layout = layout;
    result.boundingWidth = layout.layoutWidth + 2.0 * paddingX;
    result.boundingHeight = layout.textHeight + 2.0 * paddingY;

    renderLayout(layout, paint, background, originX, originY, renderer);
    return result;
}

} // namespace text
} // namespace openshot
