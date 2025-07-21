#pragma once

#include "SubtitleTypes.h"
#include <string>

// Forward declarations
class SkPath;
class SkPaint;
class SkFont;
class SkRRect;

namespace openshot {
namespace subtitle {

// Forward declaration
class SkiaRenderer;

class WordRenderer {
private:
    SkiaRenderer* renderer;

    // Private helper method
    static void bubblePath(SkPath* path, float x, float y, float width, float height, float radius);

public:
    explicit WordRenderer(SkiaRenderer* renderer);

    // Main rendering method
    void renderWord(const std::string& word, const SubtitleTextStyle& style,
                   const double x, const double y, const double deltaY = 0);

    // Measurement methods
    float getTextWidth(const std::string& text, const SubtitleTextStyle& style) const;
    TextBounds getTextHeight(const std::string& text, const SubtitleTextStyle& style) const;
    float getSpaceWidth(const SubtitleTextStyle& style) const;

    // Drawing methods
    void drawBubbleBackground(float wordWidth, const SubtitleTextStyle& style, float deltaY = 0) const;
    void drawWordBackground(float wordWidth, const SubtitleTextStyle& style, float deltaY = 0) const;
    void drawWordText(const std::string& word, float wordWidth, const SubtitleTextStyle& style) const;
    void drawWordShadow(const std::string& word, float wordWidth, const SubtitleTextStyle& style) const;
    void drawWordStroke(const std::string& word, float wordWidth, const SubtitleTextStyle& style) const;
};

} // namespace subtitle
} // namespace openshot