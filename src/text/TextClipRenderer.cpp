#include "TextClipRenderer.h"

#include "../subtitle/SkiaRenderer.h"
#include "TextCurvedText.h"
#include "TextDrawShared.h"
#include "TextGlowRenderer.h"
#include "TextGlowShader.h"

#include <skia/include/core/SkCanvas.h>
#include <skia/include/core/SkFont.h>
#include <skia/include/core/SkImageInfo.h>
#include <skia/include/core/SkPaint.h>
#include <skia/include/core/SkRRect.h>
#include <skia/include/core/SkRect.h>
#include <skia/include/core/SkSamplingOptions.h>
#include <skia/include/core/SkShader.h>
#include <skia/include/core/SkSpan.h>
#include <skia/include/core/SkSurface.h>

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

std::string trimWs(const std::string& s) {
    const auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    const auto e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

std::string toLowerAscii(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    return out;
}

// Split on commas that are NOT inside parentheses (so rgb()/rgba() stay intact).
std::vector<std::string> splitTopLevelCommas(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    int depth = 0;
    for (char c : s) {
        if (c == '(') ++depth;
        else if (c == ')') depth = std::max(0, depth - 1);
        if (c == ',' && depth == 0) {
            out.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    out.push_back(cur);
    return out;
}

} // namespace

// Parse a CSS linear-gradient(...) string into a TextClipGradient, or nullopt for a solid colour
// / anything not a parseable linear gradient (so the solid-colour path is used unchanged). Exposed
// via the header so the keyframe colour sampler (TextColorKeyframes) reuses the exact same parse.
std::optional<TextClipGradient> parseTextGradient(const std::string& value) {
    const std::string trimmed = trimWs(value);
    const std::string lower = toLowerAscii(trimmed);
    if (lower.rfind("linear-gradient", 0) != 0) return std::nullopt;

    const auto open = trimmed.find('(');
    const auto close = trimmed.rfind(')');
    if (open == std::string::npos || close == std::string::npos || close <= open) return std::nullopt;

    const std::string inner = trimmed.substr(open + 1, close - open - 1);
    std::vector<std::string> parts = splitTopLevelCommas(inner);

    TextClipGradient grad;
    grad.angle = DEFAULT_GRADIENT_ANGLE;

    // A leading "<number>deg" token sets the angle; otherwise it's a colour stop.
    size_t firstStop = 0;
    if (!parts.empty()) {
        const std::string p0 = trimWs(parts[0]);
        const std::string p0l = toLowerAscii(p0);
        const auto degPos = p0l.find("deg");
        if (degPos != std::string::npos) {
            try {
                grad.angle = std::stod(p0.substr(0, degPos));
                firstStop = 1;
            } catch (...) {}
        }
    }

    // Each remaining part: colour + optional trailing "<number>%" position.
    std::vector<std::string> colors;
    std::vector<std::optional<double>> positions;
    for (size_t i = firstStop; i < parts.size(); ++i) {
        const std::string part = trimWs(parts[i]);
        if (part.empty()) continue;
        std::string color = part;
        std::optional<double> pos;
        if (part.back() == '%') {
            const auto sp = part.find_last_of(" \t");
            if (sp != std::string::npos) {
                color = trimWs(part.substr(0, sp));
                try {
                    pos = std::stod(trimWs(part.substr(sp + 1, part.size() - sp - 2))) / 100.0;
                } catch (...) { pos.reset(); }
            }
        }
        colors.push_back(color);
        positions.push_back(pos);
    }

    if (colors.size() < 2) return std::nullopt;

    // Distribute any missing positions evenly across the stops, clamped 0..1.
    const size_t n = colors.size();
    for (size_t i = 0; i < n; ++i) {
        double p = positions[i].has_value()
            ? *positions[i]
            : static_cast<double>(i) / static_cast<double>(n - 1);
        grad.stops.push_back({colors[i], clamp01(p)});
    }
    return grad;
}

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
    paint.sizeScale = transformation.size / LAYOUT_REFERENCE_SIZE;
    paint.fontWeight = style.fontWeight;
    paint.italic = style.italic;
    paint.letterSpacing = style.letterSpacing * paint.fontSize;
    paint.lineHeight = style.lineHeight * paint.fontSize;
    paint.alignment = style.textAlign;

    // Fill colour: a CSS linear-gradient becomes paint.colorGradient, and the solid `color` is
    // set to the OPAQUE first-stop colour (glyph coverage only) so the paint alpha / SrcIn mask
    // doesn't globally dim the gradient. A solid colour passes straight through.
    if (auto grad = parseTextGradient(style.color); grad.has_value() && !grad->stops.empty()) {
        paint.colorGradient = grad;
        paint.color = parseColorOpacity(grad->stops.front().color).color;
    } else {
        paint.color = style.color;
    }

    if (style.strokeColor.has_value() && style.strokeWidthRatio > 0.0) {
        TextClipStrokeStyle stroke;
        stroke.width = style.strokeWidthRatio * paint.fontSize;
        if (auto grad = parseTextGradient(*style.strokeColor); grad.has_value() && !grad->stops.empty()) {
            stroke.gradient = grad;
            stroke.color = parseColorOpacity(grad->stops.front().color).color;
        } else {
            stroke.color = *style.strokeColor;
        }
        paint.stroke = std::move(stroke);
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

    // Gaussian blur. Ratio-based so the blur stays visually constant across font sizes.
    paint.blur = style.blurRatio.has_value() && *style.blurRatio > 0.0
        ? *style.blurRatio * paint.fontSize
        : 0.0;

    // Glow: spread -> beam reach, direction -> light-source offset (fontSize units, size-invariant).
    if (style.glowColor.has_value()) {
        const auto parsed = parseColorOpacity(*style.glowColor);
        const double intensity = style.glowIntensityRatio;
        if (parsed.opacity > 0.0 && intensity > 0.0) {
            paint.glow = TextClipGlowStyle{
                parsed.color,
                parsed.opacity * intensity,
                style.glowRangeRatio * GLOW_RAY_LEN_SCALE,
                (style.glowDirectionX / GLOW_DIRECTION_RANGE) * GLOW_MAX_SOURCE_OFFSET,
                (style.glowDirectionY / GLOW_DIRECTION_RANGE) * GLOW_MAX_SOURCE_OFFSET,
            };
        }
    }

    // Size-invariant (it is an angle), so it passes straight through. nullopt = curving off.
    paint.curveAngle = style.curveAngle;

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
        // M147: textToGlyphs/getBounds take SkSpan; textToGlyphs returns size_t.
        const size_t count = font.textToGlyphs(
            letter.c_str(), letter.length(),
            SkTextEncoding::kUTF8, SkSpan<SkGlyphID>(glyphs));
        if (count == 0) return;
        const size_t n = std::min<size_t>(count, 8);
        SkRect bounds[8];
        font.getBounds(SkSpan<const SkGlyphID>(glyphs, n), SkSpan<SkRect>(bounds, n), nullptr);
        for (size_t g = 0; g < n; ++g) {
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
        const bool isLastParagraph = (pi + 1 == paragraphs.size());

        if (paragraph.empty()) {
            TextClipLine empty;
            empty.isHardBreak = !isLastParagraph;
            lines.push_back(std::move(empty));
            continue;
        }

        const auto pLines = layoutParagraph(paragraph, style, wrapWidth, renderer);
        for (const auto& line : pLines) lines.push_back(line);

        if (!isLastParagraph && pLines.empty()) {
            TextClipLine empty;
            empty.isHardBreak = true;
            lines.push_back(std::move(empty));
        }

        // Mark the last line of this paragraph as a hard break (unless it's the last paragraph).
        if (!isLastParagraph && !lines.empty()) {
            lines.back().isHardBreak = true;
        }
    }

    if (lines.empty()) {
        lines.push_back({});
    }

    // Whitespace-only lines collapse to zero glyph bounds (spaces carry no ink), so fall back to
    // the "Hg" probe for those so they still get a font-nominal ascent / descent.
    auto hasInk = [](const std::string& s) {
        for (char c : s) {
            if (!std::isspace(static_cast<unsigned char>(c))) return true;
        }
        return false;
    };

    const std::string VERTICAL_METRICS_PROBE = "Hg";
    const VerticalBounds probeBounds = measureTextVerticalBounds(VERTICAL_METRICS_PROBE, style, renderer);

    // Populate per-codepoint advances and per-line vertical metrics for each line. When the
    // caller is `layoutTextAtReferenceSize`, these are reference-space values which scaleLayout
    // converts back into actual pixel space.
    for (auto& line : lines) {
        line.letterAdvances = computeLetterAdvances(line.text, style, renderer);
        if (hasInk(line.text)) {
            const VerticalBounds bounds = measureTextVerticalBounds(line.text, style, renderer);
            line.ascent = -bounds.top;
            line.descent = bounds.bottom;
        } else {
            line.ascent = -probeBounds.top;
            line.descent = probeBounds.bottom;
        }
    }

    double textWidth = 0.0;
    for (const auto& l : lines) textWidth = std::max(textWidth, l.width);

    // First inked line's ascent and last inked line's descent bound the visible block.
    double firstLineAscent = -probeBounds.top;
    for (const auto& l : lines) {
        if (hasInk(l.text)) { firstLineAscent = l.ascent; break; }
    }
    double lastLineDescent = probeBounds.bottom;
    for (auto it = lines.rbegin(); it != lines.rend(); ++it) {
        if (hasInk(it->text)) { lastLineDescent = it->descent; break; }
    }

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
        scaled.isHardBreak = line.isHardBreak;
        scaled.ascent = line.ascent * sizeScale;
        scaled.descent = line.descent * sizeScale;
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
    // sizeScale maps the reference layout (measured at layoutPaint's fixed size AND fixed Full HD
    // project width) up to the real render size. Because layoutPaint uses the fixed reference
    // project width while wrapWidth is computed at the real project_width, dividing wrapWidth by
    // sizeScale cancels the real project_width out: refWrapWidth reduces to a resolution-independent
    // LAYOUT_REFERENCE_SIZE-scaled width, so the wrap is measured identically at every resolution.
    const double sizeScale = layoutPaint.fontSize > 0.0
        ? paint.fontSize / layoutPaint.fontSize
        : 1.0;
    const double refWrapWidth    = wrapWidth    / sizeScale;
    const double refUserMaxWidth = userMaxWidth / sizeScale;
    const auto referenceLayout = layoutText(text, layoutPaint, refWrapWidth, refUserMaxWidth, renderer);
    return scaleLayout(referenceLayout, sizeScale);
}

double getLineStartX(const TextClipLine& line, const TextClipLayout& layout, TextAlignment alignment,
                     double extraLetterSpacing) {
    const size_t n = utf8Length(line.text);
    const double width = line.width + (n > 0 ? static_cast<double>(n - 1) : 0.0) * extraLetterSpacing;
    switch (alignment) {
    case TextAlignment::LEFT:  return 0.0;
    case TextAlignment::RIGHT: return layout.layoutWidth - width;
    case TextAlignment::CENTER:
    default:                   return (layout.layoutWidth - width) / 2.0;
    }
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

namespace {

// SHADOW_BLUR_SIGMA_SCALE (CPU-vs-GPU shadow blur match) lives in TextDrawShared.h so the
// flat, curved, and animated shadow paths all apply the same correction.

// Topmost crisp fill of the flat block. When glow is active the fill is softened (Layer 3):
// lower opacity + a sub-pixel mask blur so the underlying bloom/ray light dominates the edges.
void drawTextLine(
    const TextClipLine& line,
    const TextClipPaintStyle& style,
    double x,
    double baselineY,
    subtitle::SkiaRenderer* renderer,
    double extraLetterSpacing = 0.0,
    const PaintGradient* gradient = nullptr)
{
    const double coreSoftBlur = style.glow.has_value() ? GLOW_CORE_TEXT_BLUR_RATIO * style.fontSize : 0.0;
    const double fillBlur = combineBlur(calibratedTextBlur(style.blur), coreSoftBlur);
    const std::optional<double> blurOpt = fillBlur > 0.0 ? std::optional<double>(fillBlur) : std::nullopt;
    const SkPaint* base = renderer->getPaint(subtitle::PaintProps{
        style.color, style.glow.has_value() ? GLOW_CORE_TEXT_OPACITY : 1.0, std::nullopt, blurOpt});

    // Gradient fill: same paint (AA / core-soft blur / opacity) with a linear-gradient shader in
    // block space so the ramp spans the whole block. The shader colour dominates `style.color`.
    SkPaint gradPaint;
    const SkPaint* paint = base;
    if (gradient) {
        if (sk_sp<SkShader> shader = makeGradientShader(renderer, *gradient)) {
            gradPaint = *base;
            gradPaint.setShader(shader);
            paint = &gradPaint;
        }
    }
    forEachLetter(line, x, extraLetterSpacing, [&](const std::string& letter, double letterX) {
        drawLetter(renderer, letter, letterX, baselineY, *paint, style);
    });
}

void drawStrokeLine(
    const TextClipLine& line,
    const TextClipPaintStyle& style,
    const TextClipStrokeStyle& stroke,
    double x,
    double baselineY,
    subtitle::SkiaRenderer* renderer,
    double extraLetterSpacing = 0.0,
    const PaintGradient* gradient = nullptr)
{
    const double textBlur = calibratedTextBlur(style.blur);
    const std::optional<double> blurOpt = textBlur > 0.0 ? std::optional<double>(textBlur) : std::nullopt;
    const SkPaint* base = renderer->getPaint(subtitle::PaintProps{stroke.color, 1.0, stroke.width, blurOpt});

    SkPaint gradPaint;
    const SkPaint* paint = base;
    if (gradient) {
        if (sk_sp<SkShader> shader = makeGradientShader(renderer, *gradient)) {
            gradPaint = *base;
            gradPaint.setShader(shader);
            paint = &gradPaint;
        }
    }
    forEachLetter(line, x, extraLetterSpacing, [&](const std::string& letter, double letterX) {
        drawLetter(renderer, letter, letterX, baselineY, *paint, style);
    });
}

void drawShadowLine(
    const TextClipLine& line,
    const TextClipPaintStyle& style,
    const TextClipShadowStyle& shadow,
    const std::optional<TextClipStrokeStyle>& stroke,
    double x,
    double baselineY,
    subtitle::SkiaRenderer* renderer,
    double extraLetterSpacing,
    double shadowBlur)
{
    const double radians = shadow.angle * M_PI / 180.0;
    const double dx = std::cos(radians) * shadow.distance;
    const double dy = std::sin(radians) * shadow.distance;

    // `shadowBlur` is the already-combined, already-calibrated mask sigma (device px) chosen by the
    // caller — the direct path passes the full sigma; the downscale path passes the reduced offscreen
    // sigma (see renderShadowLayer). Kept as a parameter so both share one glyph-drawing routine.
    const std::optional<double> blur = shadowBlur > 0.0 ? std::optional<double>(shadowBlur) : std::nullopt;

    // Stroke-expanded shadow pass: trace the stroked outer edge so the blur halo matches the
    // full stroked+filled glyph. Skipped when there is no stroke.
    const SkPaint* strokePaint = stroke.has_value()
        ? renderer->getPaint(subtitle::PaintProps{shadow.color, shadow.opacity, stroke->width, blur})
        : nullptr;
    const SkPaint* fillPaint = renderer->getPaint(subtitle::PaintProps{shadow.color, shadow.opacity, std::nullopt, blur});

    forEachLetter(line, x, extraLetterSpacing, [&](const std::string& letter, double letterX) {
        if (strokePaint) drawLetter(renderer, letter, letterX + dx, baselineY + dy, *strokePaint, style);
        drawLetter(renderer, letter, letterX + dx, baselineY + dy, *fillPaint, style);
    });
}

} // namespace

void renderShadowLayer(
    const TextClipLayout& layout,
    const TextClipPaintStyle& paint,
    double originX,
    double originY,
    subtitle::SkiaRenderer* renderer,
    double extraLetterSpacing)
{
    if (!paint.dropShadow.has_value()) return;
    const TextClipShadowStyle& shadow = *paint.dropShadow;

    // Combined text+shadow blur, calibrated (see TextDrawShared.h). This is the true device-space
    // sigma we want to reproduce at any resolution.
    const double shadowBlur = combineBlur(shadow.blur, paint.blur) * SHADOW_BLUR_SIGMA_SCALE;

    // Draw every non-empty line's shadow at content-box origin (ox, oy) with mask sigma `blurSigma`.
    auto drawLines = [&](double ox, double oy, double blurSigma) {
        const double firstBaselineY = oy + layout.firstLineAscent;
        for (size_t li = 0; li < layout.lines.size(); ++li) {
            const auto& line = layout.lines[li];
            if (line.text.empty()) continue;
            const double lineX = ox + getLineStartX(line, layout, paint.alignment, extraLetterSpacing);
            drawShadowLine(line, paint, shadow, paint.stroke, lineX,
                           firstBaselineY + static_cast<double>(li) * layout.lineHeight,
                           renderer, extraLetterSpacing, blurSigma);
        }
    };

    // Fast path: sigma within Skia's CPU mask-blur capacity -> draw directly onto the canvas. Byte-
    // for-byte identical to the previous behaviour for every case that didn't hit the clamp (e.g.
    // 720p / 1080p), so nothing changes there.
    if (shadowBlur <= MAX_CPU_MASK_BLUR_SIGMA) {
        drawLines(originX, originY, shadowBlur);
        return;
    }

    // Large-sigma path (mirrors the front-end GPU GaussianBlur). Skia's CPU mask blur hard-clamps
    // sigma at 128 px, so at high resolutions a big shadow (sigma > 128) renders too tight. Instead,
    // render the shadow into a downscaled offscreen, blur there with a sigma under the cap, then
    // upscale — which reconstructs the true Gaussian at the full sigma and matches CanvasKit.
    const double s = MAX_CPU_MASK_BLUR_SIGMA / shadowBlur;   // downscale factor (< 1)
    const double radians = shadow.angle * M_PI / 180.0;
    const double offsetReach = std::max(std::abs(std::cos(radians)), std::abs(std::sin(radians))) * shadow.distance;
    const double margin = std::ceil(3.0 * shadowBlur + offsetReach + 4.0);   // gaussian reach + offset
    const double blockW = layout.layoutWidth + 2.0 * margin;
    const double blockH = layout.textHeight  + 2.0 * margin;
    const int sw = std::max(2, static_cast<int>(std::ceil(blockW * s)));
    const int sh = std::max(2, static_cast<int>(std::ceil(blockH * s)));

    sk_sp<SkSurface> surface = SkSurfaces::Raster(SkImageInfo::MakeN32Premul(sw, sh));
    if (!surface) { drawLines(originX, originY, shadowBlur); return; }   // fallback: clamped, but drawn
    SkCanvas* offscreen = surface->getCanvas();
    offscreen->clear(SK_ColorTRANSPARENT);
    offscreen->scale(static_cast<float>(s), static_cast<float>(s));   // draw full-coord glyphs downscaled
    renderer->renderToCanvas(offscreen, [&] {
        drawLines(margin, margin, shadowBlur * s);   // offscreen sigma == MAX_CPU_MASK_BLUR_SIGMA, under cap
    });
    sk_sp<SkImage> image = surface->makeImageSnapshot();
    if (!image) return;

    SkCanvas* canvas = renderer->getCanvas();
    canvas->save();
    canvas->translate(static_cast<float>(originX - margin), static_cast<float>(originY - margin));
    canvas->scale(static_cast<float>(1.0 / s), static_cast<float>(1.0 / s));
    canvas->drawImage(image.get(), 0, 0, SkSamplingOptions(SkFilterMode::kLinear), nullptr);
    canvas->restore();
}

void renderLayout(
    const TextClipLayout& layout,
    const TextClipPaintStyle& paint,
    const std::optional<TextClipBackgroundStyle>& background,
    double originX,
    double originY,
    subtitle::SkiaRenderer* renderer,
    double extraLetterSpacing,
    bool skipGlow,
    bool skipShadow,
    BlockDrawLayer layer)
{
    // z-band gating for the 3D two-texture split (see BlockDrawLayer): BelowGlow paints only
    // background + shadow, AboveGlow paints only stroke + fill, All paints everything (glow inline).
    const bool drawBelow = layer != BlockDrawLayer::AboveGlow;   // background + shadow
    const bool drawAbove = layer != BlockDrawLayer::BelowGlow;   // stroke + fill
    const bool drawGlow  = layer == BlockDrawLayer::All;         // glow only in the single/all pass

    if (drawBelow && background.has_value()) {
        drawBackgroundRect(renderer, *background, originX, originY, layout.layoutWidth, layout.textHeight);
    }

    // First baseline sits `firstLineAscent` below the top of the text block, so the visible
    // top of line 0's glyphs lines up exactly with originY. Subsequent baselines step down
    // by lineHeight (the inter-baseline advance).
    const double firstBaselineY = originY + layout.firstLineAscent;
    auto lineStart = [&](const TextClipLine& line) {
        return originX + getLineStartX(line, layout, paint.alignment, extraLetterSpacing);
    };

    // Gradient fill / stroke endpoints span the whole block (all lines share them), computed over
    // the content box in absolute canvas coords so the flat shader-paint maps correctly.
    std::optional<PaintGradient> fillGradient;
    if (paint.colorGradient.has_value()) {
        fillGradient = gradientFill(*paint.colorGradient, originX, originY, layout.layoutWidth, layout.textHeight);
    }
    std::optional<PaintGradient> strokeGradient;
    if (paint.stroke.has_value() && paint.stroke->gradient.has_value()) {
        strokeGradient = gradientFill(*paint.stroke->gradient, originX, originY, layout.layoutWidth, layout.textHeight);
    }

    // Global passes (background -> all shadows -> glow -> all strokes -> all fills) so a line's
    // shadow/stroke can never land on top of another line's fill, and the glow sits beneath all
    // crisp glyphs. For non-overlapping lines this is identical to per-line draw order.
    if (drawBelow && !skipShadow) {
        renderShadowLayer(layout, paint, originX, originY, renderer, extraLetterSpacing);
    }

    if (drawGlow && paint.glow.has_value() && !skipGlow) {
        TextGlowRenderer(renderer).drawGlowLayer(layout, paint, *paint.glow, originX, originY, nullptr, 1.0, extraLetterSpacing);
    }

    if (drawAbove && paint.stroke.has_value()) {
        for (size_t li = 0; li < layout.lines.size(); ++li) {
            const auto& line = layout.lines[li];
            if (line.text.empty()) continue;
            drawStrokeLine(line, paint, *paint.stroke, lineStart(line),
                           firstBaselineY + static_cast<double>(li) * layout.lineHeight, renderer, extraLetterSpacing,
                           strokeGradient.has_value() ? &*strokeGradient : nullptr);
        }
    }

    if (drawAbove) {
        for (size_t li = 0; li < layout.lines.size(); ++li) {
            const auto& line = layout.lines[li];
            if (line.text.empty()) continue;
            drawTextLine(line, paint, lineStart(line),
                         firstBaselineY + static_cast<double>(li) * layout.lineHeight, renderer, extraLetterSpacing,
                         fillGradient.has_value() ? &*fillGradient : nullptr);
        }
    }
}

CurvedTextGeometry curvedGeometryForLayout(const TextClipLayout& layout, const TextClipPaintStyle& paint) {
    const double angle = paint.curveAngle.value_or(0.0);
    for (const auto& line : layout.lines) {
        if (!line.text.empty()) return computeCurvedGeometry(line, angle);
    }
    return CurvedTextGeometry{};
}

void drawTextContent(
    const TextClipLayout& layout,
    const TextClipPaintStyle& paint,
    const std::optional<TextClipBackgroundStyle>& background,
    double originX,
    double originY,
    subtitle::SkiaRenderer* renderer)
{
    if (paint.curveAngle.has_value()) {
        const CurvedTextGeometry geometry = curvedGeometryForLayout(layout, paint);
        TextGlowRenderer glowRenderer(renderer);
        CurvedTextPainter painter(renderer, &glowRenderer);
        painter.drawCurvedStatic(geometry, layout, paint, background, originX, originY);
    } else {
        renderLayout(layout, paint, background, originX, originY, renderer);
    }
}

// ---------------------------------------------------------------------------
// renderTextClip (top-level)
// ---------------------------------------------------------------------------

RenderResult renderTextClip(
    const TextClipData& clipData,
    double projectWidth,
    double projectHeight,
    subtitle::SkiaRenderer* renderer,
    double originX,
    double originY)
{
    const std::string text = transformTextValue(clipData.value, clipData.style.textTransform);

    // `paint` rasterizes glyphs at the live size; `layoutPaint` drives line breaks at the fixed
    // LAYOUT_REFERENCE_SIZE and the aspect-ratio-correct reference width (Full HD long side) so wrap
    // decisions stop depending on Skia's per-Font glyph-advance quantization AND on export resolution.
    // The sizeScale in layoutTextAtReferenceSize divides the reference width back out to the real size.
    const TextClipPaintStyle paint = convertTextStyleToPaintStyle(
        clipData.style, clipData.transformation, projectWidth);

    TextTransformation refTransformation = clipData.transformation;
    refTransformation.size = LAYOUT_REFERENCE_SIZE;
    const TextClipPaintStyle layoutPaint = convertTextStyleToPaintStyle(
        clipData.style, refTransformation,
        layoutReferenceProjectWidth(static_cast<int>(projectWidth), static_cast<int>(projectHeight)));

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

    // Curved text bounds the arc's AABB, not the flat line box.
    double contentWidth = layout.layoutWidth;
    double contentHeight = layout.textHeight;
    if (paint.curveAngle.has_value()) {
        const CurvedTextGeometry geometry = curvedGeometryForLayout(layout, paint);
        contentWidth = geometry.width;
        contentHeight = geometry.height;
    }

    RenderResult result;
    result.layout = layout;
    result.boundingWidth = contentWidth + 2.0 * paddingX;
    result.boundingHeight = contentHeight + 2.0 * paddingY;

    drawTextContent(layout, paint, background, originX, originY, renderer);
    return result;
}

} // namespace text
} // namespace openshot
