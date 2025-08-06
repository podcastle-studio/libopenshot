#pragma once

#include "subtitle/SubtitleTypes.h"
#include <vector>

namespace openshot {
namespace subtitle {

// Forward declarations
class SkiaRenderer;
class TextRenderer;

class SubtitleRenderer {
public:
    SubtitleRenderer(SkiaRenderer* renderer, float fps);
    ~SubtitleRenderer();

    void renderSegment(const SubtitleSegment& segment, const SegmentSettings& settings,
      const float segmentMs, const float canvasW, const float canvasH) const;

    void renderSegmentAtFrame(const SubtitleSegment& segment, const SegmentSettings& settings,
                            int64_t frameNumber, float canvasWidth, float canvasHeight) const;

private:
    static double getStartX(const float textWidth, const float containerWidth, const SubtitleContainerStyle& containerStyles);
    static double getStartY(const int currentLine, const SubtitleTextStyle& textStyle, const SubtitleContainerStyle& containerStyle);

    std::vector<std::vector<size_t>> getLines(const std::vector<StyledWord>& styledWords, float maxWidth, const SubtitleContainerStyle& containerStyles) const;

    SkiaRenderer* renderer;
    TextRenderer* textRenderer;
    float fps;
};

} // namespace subtitle
} // namespace openshot