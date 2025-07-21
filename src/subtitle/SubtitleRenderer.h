#pragma once

#include "subtitle/SubtitleTypes.h"
#include <vector>

namespace openshot {
namespace subtitle {

// Forward declarations
class SkiaRenderer;
class TextRenderer;

class SubtitleRenderer {
private:
    SkiaRenderer* renderer;
    TextRenderer* textRenderer;
    float fps;

    // Private helper methods
    std::vector<StyledWord> getAnimatedStyledWords(const std::vector<WordAnimation>& wordAnimations, float timeMs, const SubtitleTextStyle& defaultStyle) const;

    static double getStartX(float textWidth, float containerWidth, const SubtitleContainerStyle& containerStyles);
    static double getStartY(int currentLine, const SubtitleTextStyle& textStyle, const SubtitleContainerStyle& containerStyle);

    std::vector<std::vector<size_t>> getLines(const std::vector<StyledWord>& styledWords, float maxWidth, const SubtitleContainerStyle& containerStyles) const;

public:
    SubtitleRenderer(SkiaRenderer* renderer, float fps);
    ~SubtitleRenderer();

    // Height calculation for layout
    double getHeight(const SubtitleSegment& segment, const SegmentSettings& settings, float maxWidth) const;

    // Main rendering methods
    void renderSegment(const SubtitleSegment& segment, const SegmentSettings& settings, float segmentTimeMs) const;
    void renderSegmentAtFrame(const SubtitleSegment& segment, const SegmentSettings& settings, int64_t frameNumber);
};

} // namespace subtitle
} // namespace openshot