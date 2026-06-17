#include "TextDrawShared.h"

#include <skia/include/core/SkPaint.h>
#include <skia/include/core/SkRRect.h>
#include <skia/include/core/SkRect.h>

#include <algorithm>
#include <cstdio>
#include <sstream>

namespace openshot {
namespace text {

namespace {

std::string trimCopy(const std::string& s) {
    auto begin = s.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return "";
    auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(begin, end - begin + 1);
}

} // namespace

ParsedColor parseColorOpacity(const std::string& colorValue) {
    if (colorValue.empty()) return {std::string(), 0.0};

    if (colorValue.rfind("rgba", 0) == 0 || colorValue.rfind("RGBA", 0) == 0) {
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

void drawBackgroundRect(
    subtitle::SkiaRenderer* renderer,
    const TextClipBackgroundStyle& background,
    double originX,
    double originY,
    double contentWidth,
    double contentHeight)
{
    const double width = contentWidth + 2.0 * background.paddingX;
    const double height = contentHeight + 2.0 * background.paddingY;
    const double radius = (std::min(width, height) / 2.0) * background.radius;

    const subtitle::PaintProps props{background.color, background.opacity, std::nullopt, std::nullopt};
    const SkPaint* paint = renderer->getPaint(props);

    const SkRect rect = SkRect::MakeLTRB(
        static_cast<float>(originX - background.paddingX),
        static_cast<float>(originY - background.paddingY),
        static_cast<float>(originX + contentWidth + background.paddingX),
        static_cast<float>(originY + contentHeight + background.paddingY));
    const SkRRect rrect = SkRRect::MakeRectXY(rect, static_cast<float>(radius), static_cast<float>(radius));
    renderer->drawRRect(rrect, *paint);
}

} // namespace text
} // namespace openshot
