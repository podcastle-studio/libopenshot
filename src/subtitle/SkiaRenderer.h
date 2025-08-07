#pragma once

#include <skia/include/core/SkCanvas.h>
#include <skia/include/core/SkFont.h>
#include <skia/include/core/SkPaint.h>
#include <skia/include/core/SkTypeface.h>
#include <skia/include/core/SkFontMgr.h>
#include <skia/include/core/SkRRect.h>

#include <map>
#include <memory>
#include <string>
#include <sstream>

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
    sk_sp<SkTypeface> getTypefaceForCharacter(const std::string& familyOrPath, const SkUnichar character);
    SkFont getFontForCharacter(const FontProps& fontProps, const SkUnichar character);

    SkPaint* getPaint(const PaintProps& paintProps);

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

    void drawPath(const SkPath& path, const SkPaint& paint) const {
        canvas->drawPath(path, paint);
    }

private:
    sk_sp<SkTypeface> getTypeface(const std::string& familyOrPath);

    static SkColor parseColorString(const std::string& colorStr, float opacity = 1.0f);

private:
    SkCanvas* canvas;
    sk_sp<SkFontMgr> fontMgr;
    std::map<std::string, SkFont> fontCache;
    std::map<std::string, std::unique_ptr<SkPaint>> paintCache;
    std::map<std::string, sk_sp<SkTypeface>> typefaceCache;

};

} // namespace subtitle
} // namespace openshot