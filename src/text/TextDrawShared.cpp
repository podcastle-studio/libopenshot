#include "TextDrawShared.h"

#include <skia/include/core/SkBlendMode.h>
#include <skia/include/core/SkCanvas.h>
#include <skia/include/core/SkPaint.h>
#include <skia/include/core/SkRRect.h>
#include <skia/include/core/SkRect.h>
#include <skia/include/core/SkShader.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>
#include <utility>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

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

PaintGradient gradientFill(const TextClipGradient& gradient,
                           double x, double y, double width, double height) {
    const double angleRad = gradient.angle * M_PI / 180.0;
    // Canvas y-axis points down: 0deg = bottom→top, 90deg = left→right.
    const double dx = std::sin(angleRad);
    const double dy = -std::cos(angleRad);
    const double cx = x + width / 2.0;
    const double cy = y + height / 2.0;
    // Half-length = projection of the box onto the gradient direction, so the ramp spans the block.
    const double half = (std::abs(width * dx) + std::abs(height * dy)) / 2.0;

    PaintGradient out;
    out.start = SkPoint::Make(static_cast<float>(cx - dx * half), static_cast<float>(cy - dy * half));
    out.end   = SkPoint::Make(static_cast<float>(cx + dx * half), static_cast<float>(cy + dy * half));
    out.stops = gradient.stops;
    return out;
}

sk_sp<SkShader> makeGradientShader(subtitle::SkiaRenderer* renderer, const PaintGradient& gradient) {
    std::vector<std::pair<std::string, double>> pairs;
    pairs.reserve(gradient.stops.size());
    for (const auto& s : gradient.stops) pairs.emplace_back(s.color, s.position);
    const SkPoint pts[2] = {gradient.start, gradient.end};
    return renderer->makeLinearGradientShader(pts, pairs);
}

void withGradientCoverage(
    subtitle::SkiaRenderer* renderer,
    const PaintGradient& gradient,
    const SkRect& box,
    const std::function<void()>& drawCoverage) {
    SkCanvas* canvas = renderer->getCanvas();
    if (!canvas) { drawCoverage(); return; }

    sk_sp<SkShader> shader = makeGradientShader(renderer, gradient);
    if (!shader) { drawCoverage(); return; }

    // An empty box means "bound the layer by the current clip" — used by the char-animation path
    // where animated glyphs can translate outside any tight content box. Otherwise the explicit
    // box bounds both the layer and the SrcIn rect. Either way the gradient endpoints are in block
    // space, so the ramp spans the block regardless of per-glyph transforms.
    const bool useClip = box.isEmpty();
    canvas->saveLayer(useClip ? nullptr : &box, nullptr);
    drawCoverage();

    SkPaint gradientPaint;
    gradientPaint.setAntiAlias(true);
    gradientPaint.setShader(shader);
    gradientPaint.setBlendMode(SkBlendMode::kSrcIn);
    canvas->drawRect(useClip ? canvas->getLocalClipBounds() : box, gradientPaint);

    canvas->restore();
}

} // namespace text
} // namespace openshot
