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
public:
    explicit WordRenderer(SkiaRenderer* renderer);

    // Main rendering method
    void renderWord(const std::string& word, const SubtitleTextStyle& style,
                   const double x, const double y, const double deltaY = 0) const;

    SkFont getFontForCharacter(const std::string& utf8Char, const SubtitleTextStyle& style) const;

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

private:
    static void bubblePath(SkPath* path, const float x, const float y, const float width, const float height, const float radius);

    SkiaRenderer* renderer;
};

} // namespace subtitle
} // namespace openshot