#pragma once

#include <skia/include/core/SkCanvas.h>
#include <skia/include/core/SkFont.h>
#include <skia/include/core/SkPaint.h>
#include <skia/include/core/SkPath.h>
#include <skia/include/core/SkPoint.h>
#include <skia/include/core/SkShader.h>
#include <skia/include/core/SkSpan.h>
#include <skia/include/core/SkTypeface.h>
#include <skia/include/core/SkFontMgr.h>
#include <skia/include/core/SkRRect.h>

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <sstream>
#include <utility>
#include <vector>

namespace openshot {
namespace subtitle {

struct FontProps {
    std::string fontFamily;
    double fontSize;
    int fontWeight = 400;
    bool italic = false;

    std::string getKey() const {
        std::stringstream ss;
        ss << fontFamily << "_" << fontSize << "_" << fontWeight << "_" << italic;
        return ss.str();
    }
};

struct PaintProps {
    std::string color;
    double opacity = 1.0f;
    std::optional<double> strokeWidth;
    std::optional<double> maskBlur;

    std::string getKey() const {
        std::stringstream ss;
        ss << color << "_" << opacity << "_" << strokeWidth.value_or(0) << "_" << maskBlur.value_or(0);
        return ss.str();
    }
};

class SkiaRenderer {
public:
    explicit SkiaRenderer(SkCanvas* canvas);

    SkCanvas* getCanvas() const { return canvas; }

    void save() const { canvas->save(); }
    void restore() const { canvas->restore(); }
    void translate(const double x, const double y) const { canvas->translate(x, y); }
    void rotate(const float degrees) const { canvas->rotate(degrees); }
    void scale(const float sx, const float sy) const { canvas->scale(sx, sy); }

    SkFont getFont(const FontProps& fontProps);
    sk_sp<SkTypeface> getTypefaceForCharacter(const std::string& familyOrPath, const SkUnichar character, const SkFontStyle& style);
    SkFont getFontForCharacter(const FontProps& fontProps, const SkUnichar character);

    SkPaint* getPaint(const PaintProps& paintProps);

    // Build a linear-gradient shader between two points. `stops` are (CSS colour, position 0..1)
    // pairs, parsed with the same parseColorString used everywhere else (so the platform BGR
    // swap stays consistent). Returns null if fewer than two stops. Clamps at both ends.
    sk_sp<SkShader> makeLinearGradientShader(
        const SkPoint pts[2],
        const std::vector<std::pair<std::string, double>>& stops);

    // Skia-specific helpers
    static SkRect makeRect(const float left, const float top, const float right, const float bottom) {
        return SkRect::MakeLTRB(left, top, right, bottom);
    }

    static SkRRect makeRRect(const SkRect& rect, const float rx, const float ry) {
        return SkRRect::MakeRectXY(rect, rx, ry);
    }

    void drawRRect(const SkRRect& rect, const SkPaint& paint) const {
        canvas->drawRRect(rect, paint);
    }

    void drawText(const std::string& text, const float x, const float y, const SkPaint& paint, const SkFont& font) const {
        canvas->drawString(text.c_str(), x, y, font, paint);
    }

    // Draw a SINGLE glyph as its outline path instead of through Skia's glyph-mask cache.
    //
    // Mask drawing rounds a glyph's device origin to a whole pixel (SkFont::isSubpixel() is off,
    // and even with it on the y axis still rounds) and re-grid-fits the outline through the
    // hinter at every device scale. Under an animated transform that turns continuous motion into
    // 1px steps with the stems shimmering between them — the visible jitter of char-level text
    // animations, worst during the slow settle at the end of an ease, where the true per-frame
    // motion is smaller than the rounding.
    //
    // A path is filled at the exact subpixel position from an unhinted, scale-invariant outline
    // (SkFont::getPath bakes it at Skia's canonical path size with hinting off), so a glyph moves
    // and scales smoothly. This is the same route Skia takes on its own above a 256px device text
    // size (SkStrikeSpec::ShouldDrawAsPath), so it makes small text behave the way large text
    // already did rather than introducing a new look. Synthetic bold/italic (embolden / skewX)
    // are preserved — they live on the font, not the rasterizer.
    //
    // Returns false when the text isn't exactly one glyph or that glyph has no outline
    // (bitmap-only faces, e.g. colour emoji), so the caller can fall back to drawText.
    bool drawTextAsPath(const std::string& text, const float x, const float y,
                        const SkPaint& paint, const SkFont& font) const {
        if (!canvas || text.empty()) return false;
        SkGlyphID glyphs[2] = {0, 0};
        const size_t count = font.textToGlyphs(
            text.c_str(), text.length(), SkTextEncoding::kUTF8, SkSpan<SkGlyphID>(glyphs));
        if (count != 1) return false;
        const std::optional<SkPath> path = font.getPath(glyphs[0]);
        if (!path.has_value()) return false;
        canvas->save();
        canvas->translate(x, y);
        canvas->drawPath(*path, paint);
        canvas->restore();
        return true;
    }

    void drawPath(const SkPath& path, const SkPaint& paint) const {
        canvas->drawPath(path, paint);
    }

    // Temporarily redirect drawing to an offscreen canvas (e.g. a raster surface for the
    // glow silhouette or the word-mode composited block), run `draw`, then restore the
    // previous canvas. Mirrors CanvasKitRenderer.renderToCanvas in the TS reference.
    template <typename Fn>
    void renderToCanvas(SkCanvas* target, Fn&& draw) {
        SkCanvas* previous = canvas;
        canvas = target;
        draw();
        canvas = previous;
    }

private:
    sk_sp<SkTypeface> getTypeface(const std::string& familyOrPath, const SkFontStyle& style);

    // Resolve a single family name or font-file path to the typeface whose design most
    // closely matches `style`. For an installed family this returns the real bold / italic
    // cut when the family ships one; for a variable-font file it pins the weight axis to
    // the requested weight. No synthetic styling happens here.
    sk_sp<SkTypeface> matchTypeface(const std::string& familyOrPath, const SkFontStyle& style);

    SkColor parseColorString(const std::string& colorStr, const float opacity = 1.0f);

private:
    SkCanvas* canvas;
    sk_sp<SkFontMgr> fontMgr;
    std::map<std::string, SkFont> fontCache;
    std::map<std::string, std::unique_ptr<SkPaint>> paintCache;
    std::map<std::string, sk_sp<SkTypeface>> typefaceCache;

};

} // namespace subtitle
} // namespace openshot