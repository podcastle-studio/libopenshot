/**
 * @file
 * @brief Implementation of SkiaRenderer class
 *
 * @ref License
 */

// Copyright (c) 2008-2024 OpenShot Studios, LLC
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "SkiaRenderer.h"
#include "skia/include/core/SkFontMgr.h"
#include "skia/include/core/SkMaskFilter.h"
#include "skia/include/core/SkBlurTypes.h"
#include "skia/include/ports/SkFontMgr_fontconfig.h"

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

sk_sp<SkTypeface> SkiaRenderer::getTypeface(const std::string& fontFamily) {
    if (const auto it = typefaceCache.find(fontFamily); it != typefaceCache.end()) {
        return it->second;
    }

    constexpr SkFontStyle style(400, SkFontStyle::kNormal_Width, SkFontStyle::kUpright_Slant);
    sk_sp<SkTypeface> typeface = fontMgr->matchFamilyStyle(fontFamily.c_str(), style);

    if (!typeface) {
        typeface = fontMgr->matchFamilyStyle((fontFamily + " Bold").c_str(), style);
    }

    if (!typeface) {
        typeface = fontMgr->matchFamilyStyle(nullptr, style);
    }

    typefaceCache[fontFamily] = typeface;
    return typeface;
}

SkColor SkiaRenderer::parseColorString(const std::string& colorStr, const float opacity) {
    if (colorStr.empty() || colorStr[0] != '#') {
        return SkColorSetARGB(255 * opacity, 255, 255, 255);
    }

    const int r = std::stoi(colorStr.substr(1, 2), nullptr, 16);
    const int g = std::stoi(colorStr.substr(3, 2), nullptr, 16);
    const int b = std::stoi(colorStr.substr(5, 2), nullptr, 16);

    return SkColorSetARGB(255 * opacity, r, g, b);
}

} // namespace subtitle
} // namespace openshot