#include "SkiaRenderer.h"

#include "skia/include/core/SkFontMgr.h"
#include "skia/include/core/SkFontArguments.h"
#include "skia/include/core/SkFontParameters.h"
#include "skia/include/core/SkFourByteTag.h"
#include "skia/include/core/SkMaskFilter.h"
#include "skia/include/core/SkBlurTypes.h"
#include "skia/include/core/SkSpan.h"
#include "skia/include/ports/SkFontMgr_fontconfig.h"
#include "skia/include/ports/SkFontScanner_FreeType.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <vector>

namespace openshot {
namespace subtitle {

namespace {

// OpenType variation axis tag for weight ("wght").
constexpr SkFourByteTag kWeightAxisTag = SkSetFourByteTag('w', 'g', 'h', 't');

// Build the OpenType style (weight + slant) the caller is asking for.
SkFontStyle makeFontStyle(const FontProps& fontProps) {
    const int weight = fontProps.fontWeight > 0 ? fontProps.fontWeight : SkFontStyle::kNormal_Weight;
    const SkFontStyle::Slant slant =
        fontProps.italic ? SkFontStyle::kItalic_Slant : SkFontStyle::kUpright_Slant;
    return SkFontStyle(weight, SkFontStyle::kNormal_Width, slant);
}

// If `typeface` is a variable font exposing a weight ("wght") axis, return a clone pinned
// to `weight` (clamped to the axis range) so we render a genuine heavier cut. Returns the
// original typeface unchanged when it is not variable / has no weight axis / cloning fails.
sk_sp<SkTypeface> applyWeightVariation(sk_sp<SkTypeface> typeface, int weight) {
    if (!typeface) return typeface;

    // M147: getVariationDesignParameters takes an SkSpan; an empty span returns the axis count.
    const int axisCount = typeface->getVariationDesignParameters(
        SkSpan<SkFontParameters::Variation::Axis>{});
    if (axisCount <= 0) return typeface;

    std::vector<SkFontParameters::Variation::Axis> axes(axisCount);
    if (typeface->getVariationDesignParameters(SkSpan<SkFontParameters::Variation::Axis>(axes))
            != axisCount) {
        return typeface;
    }

    for (const auto& axis : axes) {
        if (axis.tag != kWeightAxisTag) continue;

        const float clamped = std::clamp(static_cast<float>(weight), axis.min, axis.max);
        const SkFontArguments::VariationPosition::Coordinate coord{kWeightAxisTag, clamped};
        SkFontArguments args;
        args.setVariationDesignPosition({&coord, 1});
        if (sk_sp<SkTypeface> cloned = typeface->makeClone(args)) {
            return cloned;
        }
        break;
    }
    return typeface;
}

// The weight this typeface actually renders at: the pinned "wght" variation coordinate for
// a variable instance, otherwise its static design weight. Used to decide whether a real
// bold cut was obtained before falling back to synthetic emboldening.
int effectiveWeight(const sk_sp<SkTypeface>& typeface) {
    if (!typeface) return SkFontStyle::kNormal_Weight;

    // M147: getVariationDesignPosition takes an SkSpan; an empty span returns the axis count.
    const int count = typeface->getVariationDesignPosition(
        SkSpan<SkFontArguments::VariationPosition::Coordinate>{});
    if (count > 0) {
        std::vector<SkFontArguments::VariationPosition::Coordinate> coords(count);
        if (typeface->getVariationDesignPosition(
                SkSpan<SkFontArguments::VariationPosition::Coordinate>(coords)) == count) {
            for (const auto& c : coords) {
                if (c.axis == kWeightAxisTag) return static_cast<int>(c.value);
            }
        }
    }
    return typeface->fontStyle().weight();
}

// Apply faux bold / faux italic to `font` ONLY where the resolved `typeface` cannot supply
// the requested style for real. When a genuine bold (or italic/oblique) face was matched we
// leave the glyphs untouched so the designed cut is rendered instead of a synthetic one.
void applySyntheticStyle(SkFont& font, const sk_sp<SkTypeface>& typeface, const SkFontStyle& requested) {
    const bool wantsBold = requested.weight() >= SkFontStyle::kMedium_Weight;
    if (wantsBold && effectiveWeight(typeface) < SkFontStyle::kMedium_Weight) {
        font.setEmbolden(true);
    }

    const bool wantsItalic = requested.slant() != SkFontStyle::kUpright_Slant;
    const SkFontStyle::Slant actualSlant =
        typeface ? typeface->fontStyle().slant() : SkFontStyle::kUpright_Slant;
    if (wantsItalic && actualSlant == SkFontStyle::kUpright_Slant) {
        font.setSkewX(-0.10f);
    }
}

} // namespace

SkiaRenderer::SkiaRenderer(SkCanvas* canvas) : canvas(canvas) {
    // M147: SkFontMgr_New_FontConfig now requires an explicit font scanner.
    fontMgr = SkFontMgr_New_FontConfig(nullptr, SkFontScanner_Make_FreeType());
    if (!fontMgr) {
        fontMgr = SkFontMgr::RefEmpty();
    }
}

SkFont SkiaRenderer::getFont(const FontProps& fontProps) {
    const std::string key = fontProps.getKey();

    if (const auto it = fontCache.find(key); it != fontCache.end()) {
        return it->second;
    }

    const SkFontStyle style = makeFontStyle(fontProps);
    const sk_sp<SkTypeface> typeface = getTypeface(fontProps.fontFamily, style);
    SkFont skFont(typeface, fontProps.fontSize);

    applySyntheticStyle(skFont, typeface, style);

    skFont.setEdging(SkFont::Edging::kAntiAlias);

    fontCache[key] = skFont;
    return skFont;
}

sk_sp<SkTypeface> SkiaRenderer::getTypefaceForCharacter(const std::string& familyOrPath, const SkUnichar character, const SkFontStyle& style)
{
    // ---- cache key (style-aware, so a bold/italic request can't return a cached regular face) --
    const std::string cacheKey = familyOrPath + "_char_" + std::to_string(character)
        + "_" + std::to_string(style.weight()) + "_" + std::to_string(style.slant());
    if (const auto it = typefaceCache.find(cacheKey); it != typefaceCache.end()) {
        return it->second;
    }

    auto covers = [character](const sk_sp<SkTypeface>& typeface) {
        return typeface && SkFont(typeface).unicharToGlyph(character) != 0;
    };

    // ------------------------------------------------------------------
    // 1) The requested family name or explicit file-path, at the requested style. When the
    //    family ships a real bold / italic cut (or the file is a variable font) this is it.
    // ------------------------------------------------------------------
    sk_sp<SkTypeface> typeface = matchTypeface(familyOrPath, style);
    if (covers(typeface)) {
        typefaceCache[cacheKey] = typeface;
        return typeface;
    }

    // ------------------------------------------------------------------
    // 2) Preferred fallback: Noto Sans Arabic, 3) Secondary fallback: FreeSans.
    //    Matched at the same style so fallback glyphs keep the requested weight / slant.
    // ------------------------------------------------------------------
    for (const char* fallback : {"Noto Sans Arabic", "FreeSans"}) {
        typeface = fontMgr->matchFamilyStyle(fallback, style);
        if (covers(typeface)) {
            typefaceCache[cacheKey] = typeface;
            return typeface;
        }
    }

    // ------------------------------------------------------------------
    // 4) Last-chance fallback: whatever FontConfig thinks best for this style
    // ------------------------------------------------------------------
    typeface = fontMgr->matchFamilyStyle(nullptr, style);

    // Cache even if null so we don’t repeat the work every call
    typefaceCache[cacheKey] = typeface;
    return typeface;
}

SkFont SkiaRenderer::getFontForCharacter(const FontProps& fontProps, const SkUnichar character) {
    const std::string key = fontProps.getKey() + "_char_" + std::to_string(character);

    if (const auto it = fontCache.find(key); it != fontCache.end()) {
        return it->second;
    }

    const SkFontStyle style = makeFontStyle(fontProps);
    const sk_sp<SkTypeface> typeface = getTypefaceForCharacter(fontProps.fontFamily, character, style);
    SkFont skFont(typeface, fontProps.fontSize);

    applySyntheticStyle(skFont, typeface, style);

    skFont.setEdging(SkFont::Edging::kAntiAlias);

    fontCache[key] = skFont;
    return skFont;
}

SkPaint* SkiaRenderer::getPaint(const PaintProps& paintProps) {
    const std::string key = paintProps.getKey();
    if (const auto it = paintCache.find(key); it != paintCache.end()) {
        return it->second.get();
    }

    auto paint = std::make_unique<SkPaint>();
    paint->setColor(parseColorString(paintProps.color, paintProps.opacity));
    paint->setAntiAlias(true);

    if (paintProps.strokeWidth.has_value()) {
        paint->setStyle(SkPaint::kStroke_Style);
        paint->setStrokeWidth(paintProps.strokeWidth.value());
        paint->setStrokeCap(SkPaint::kRound_Cap);
        paint->setStrokeJoin(SkPaint::kRound_Join);
    }

    if (paintProps.maskBlur.has_value()) {
        paint->setMaskFilter(SkMaskFilter::MakeBlur(kNormal_SkBlurStyle, paintProps.maskBlur.value(), false));
    }

    SkPaint* paintPtr = paint.get();
    paintCache[key] = std::move(paint);
    return paintPtr;
}

sk_sp<SkTypeface> SkiaRenderer::matchTypeface(const std::string& familyOrPath, const SkFontStyle& style) {
    if (std::filesystem::is_regular_file(familyOrPath)) {
        // A font *file* is a fixed face. The exception is a variable font that exposes a
        // weight axis — pin it to the requested weight so we get a genuine heavier cut
        // instead of falling back to synthetic emboldening later.
        return applyWeightVariation(fontMgr->makeFromFile(familyOrPath.c_str()), style.weight());
    }
    // matchFamilyStyle returns the installed face closest to `style`; when a real bold (or
    // italic) cut exists in the family it is returned here.
    return fontMgr->matchFamilyStyle(familyOrPath.c_str(), style);
}

sk_sp<SkTypeface> SkiaRenderer::getTypeface(const std::string& familyOrPath, const SkFontStyle& style) {
    // Style-aware cache key: a later bold/italic request must not reuse a cached regular face.
    const std::string cacheKey = familyOrPath
        + "_" + std::to_string(style.weight()) + "_" + std::to_string(style.slant());
    if (const auto it = typefaceCache.find(cacheKey); it != typefaceCache.end()) {
        return it->second;
    }

    sk_sp<SkTypeface> typeface = matchTypeface(familyOrPath, style);
    if (!typeface) { // last‑chance fallback
        typeface = fontMgr->matchFamilyStyle(nullptr, style);
    }

    typefaceCache[cacheKey] = typeface;
    return typeface;
}

SkColor SkiaRenderer::parseColorString(const std::string& colorStr, const float opacity) {
    int r = 255, g = 255, b = 255;
    double a = 1.0;            // colour's own alpha (0..1)
    bool parsed = false;

    if (!colorStr.empty() && colorStr[0] == '#') {
        // #rrggbb or #rrggbbaa
        try {
            if (colorStr.size() >= 7) {
                r = std::stoi(colorStr.substr(1, 2), nullptr, 16);
                g = std::stoi(colorStr.substr(3, 2), nullptr, 16);
                b = std::stoi(colorStr.substr(5, 2), nullptr, 16);
                if (colorStr.size() >= 9) {
                    a = std::stoi(colorStr.substr(7, 2), nullptr, 16) / 255.0;
                }
                parsed = true;
            }
        } catch (...) {}
    } else if (colorStr.rfind("rgba", 0) == 0 || colorStr.rfind("RGBA", 0) == 0 ||
               colorStr.rfind("rgb", 0) == 0  || colorStr.rfind("RGB", 0) == 0) {
        // rgba(r, g, b, a) or rgb(r, g, b). %d skips leading whitespace, so the
        // spaces after commas are handled.
        int rr = 0, gg = 0, bb = 0;
        double aa = 1.0;
        if (std::sscanf(colorStr.c_str(), "rgba(%d,%d,%d,%lf)", &rr, &gg, &bb, &aa) >= 3 ||
            std::sscanf(colorStr.c_str(), "RGBA(%d,%d,%d,%lf)", &rr, &gg, &bb, &aa) >= 3 ||
            std::sscanf(colorStr.c_str(), "rgb(%d,%d,%d)",      &rr, &gg, &bb)      == 3 ||
            std::sscanf(colorStr.c_str(), "RGB(%d,%d,%d)",      &rr, &gg, &bb)      == 3) {
            r = rr; g = gg; b = bb; a = aa;
            parsed = true;
        }
    }

    if (!parsed) {
        // Unknown format → opaque white fallback (legacy behaviour).
        return SkColorSetARGB(static_cast<U8CPU>(std::lround(255 * opacity)), 255, 255, 255);
    }

    const int alpha = std::clamp(static_cast<int>(std::lround(a * opacity * 255.0)), 0, 255);
    r = std::clamp(r, 0, 255);
    g = std::clamp(g, 0, 255);
    b = std::clamp(b, 0, 255);

    // Platform-specific: This build/platform expects BGR order instead of RGB
    // Despite the function name suggesting RGB order, we need to swap R and B
    return SkColorSetARGB(static_cast<U8CPU>(alpha), b, g, r);
}

} // namespace subtitle
} // namespace openshot