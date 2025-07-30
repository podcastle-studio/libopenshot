#include "SkiaRenderer.h"

#include "skia/include/core/SkFontMgr.h"
#include "skia/include/core/SkMaskFilter.h"
#include "skia/include/core/SkBlurTypes.h"
#include "skia/include/ports/SkFontMgr_fontconfig.h"

#include <filesystem>

namespace openshot {
namespace subtitle {

SkiaRenderer::SkiaRenderer(SkCanvas* canvas) : canvas(canvas) {
    fontMgr = SkFontMgr_New_FontConfig(nullptr);
    if (!fontMgr) {
        fontMgr = SkFontMgr::RefEmpty();
    }
}

SkFont SkiaRenderer::getFont(const FontProps& fontProps) {
    const std::string key = fontProps.getKey();

    if (const auto it = fontCache.find(key); it != fontCache.end()) {
        return it->second;
    }

    const sk_sp<SkTypeface> typeface = getTypeface(fontProps.fontFamily);
    SkFont skFont(typeface, fontProps.fontSize);

    if (fontProps.italic) {
        skFont.setSkewX(-0.25f);
    }

    if (fontProps.fontWeight >= 500) {
        skFont.setEmbolden(true);
    }

    skFont.setEdging(SkFont::Edging::kAntiAlias);

    fontCache[key] = skFont;
    return skFont;
}

sk_sp<SkTypeface> SkiaRenderer::getTypefaceForCharacter(const std::string& familyOrPath, const SkUnichar character) {
    // Create a key for caching
    const std::string cacheKey = familyOrPath + "_char_" + std::to_string(character);

    if (const auto it = typefaceCache.find(cacheKey); it != typefaceCache.end()) {
        return it->second;
    }

    sk_sp<SkTypeface> typeface;

    // First try the requested font
    constexpr SkFontStyle style(400, SkFontStyle::kNormal_Width, SkFontStyle::kUpright_Slant);
    if (std::filesystem::is_regular_file(familyOrPath)) {
        typeface = fontMgr->makeFromFile(familyOrPath.c_str());
    } else {
        typeface = fontMgr->matchFamilyStyle(familyOrPath.c_str(), style);
    }

    // Check if this typeface can render the character
    if (typeface) {
        const SkFont testFont(typeface);
        const SkGlyphID glyphID = testFont.unicharToGlyph(character);
        if (glyphID != 0) {
            typefaceCache[cacheKey] = typeface;
            return typeface;
        }
    }

    // Fallback to DejaVu Serif for any unsupported character
    typeface = fontMgr->matchFamilyStyle("DejaVu Serif", style);
    if (typeface) {
        // Verify it actually has the character
        const SkFont testFont(typeface);
        if (testFont.unicharToGlyph(character) != 0) {
            typefaceCache[cacheKey] = typeface;
            return typeface;
        }
    }

    // Ultimate fallback to any available font
    if (!typeface) {
        typeface = fontMgr->matchFamilyStyle(nullptr, style);
    }

    typefaceCache[cacheKey] = typeface;
    return typeface;
}

SkFont SkiaRenderer::getFontForCharacter(const FontProps& fontProps, const SkUnichar character) {
    const std::string key = fontProps.getKey() + "_char_" + std::to_string(character);

    if (const auto it = fontCache.find(key); it != fontCache.end()) {
        return it->second;
    }

    const sk_sp<SkTypeface> typeface = getTypefaceForCharacter(fontProps.fontFamily, character);
    SkFont skFont(typeface, fontProps.fontSize);

    if (fontProps.italic) {
        skFont.setSkewX(-0.25f);
    }

    if (fontProps.fontWeight >= 500) {
        skFont.setEmbolden(true);
    }

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
        paint->setMaskFilter(SkMaskFilter::MakeBlur(
            kNormal_SkBlurStyle,
            paintProps.maskBlur.value(),
            false
        ));
    }

    SkPaint* paintPtr = paint.get();
    paintCache[key] = std::move(paint);
    return paintPtr;
}

sk_sp<SkTypeface> SkiaRenderer::getTypeface(const std::string& familyOrPath) {
    if (const auto it = typefaceCache.find(familyOrPath); it != typefaceCache.end()) {
        return it->second;
    }

    sk_sp<SkTypeface> typeface;
    constexpr SkFontStyle style(400, SkFontStyle::kNormal_Width, SkFontStyle::kUpright_Slant);
    if (std::filesystem::is_regular_file(familyOrPath)) {
        typeface = fontMgr->makeFromFile(familyOrPath.c_str());
    } else {
        typeface = fontMgr->matchFamilyStyle(familyOrPath.c_str(), style);
    }

    if (!typeface) { // last‑chance fallback
        typeface = fontMgr->matchFamilyStyle(nullptr, style);
    }

    typefaceCache[familyOrPath] = typeface;
    return typeface;
}

SkColor SkiaRenderer::parseColorString(const std::string& colorStr, float opacity) {
    if (colorStr.empty() || colorStr[0] != '#') {
        return SkColorSetARGB(255 * opacity, 255, 255, 255);
    }

    const int r = std::stoi(colorStr.substr(1, 2), nullptr, 16);
    const int g = std::stoi(colorStr.substr(3, 2), nullptr, 16);
    const int b = std::stoi(colorStr.substr(5, 2), nullptr, 16);

    // Platform-specific: This build/platform expects BGR order instead of RGB
    // Despite the function name suggesting RGB order, we need to swap R and B
    return SkColorSetARGB(255 * opacity, b, g, r);

}

} // namespace subtitle
} // namespace openshot