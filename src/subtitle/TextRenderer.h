#pragma once

#include "SubtitleTypes.h"
#include <vector>

namespace openshot {
namespace subtitle {

// Forward declarations
class SkiaRenderer;
class WordRenderer;

class TextRenderer {
public:
    explicit TextRenderer(SkiaRenderer* renderer);
    ~TextRenderer(); // Needed because we have a pointer to WordRenderer

    // Measurement methods
    TextBounds getTextVerticalBounds(const std::vector<StyledWord>& styledWords) const;
    float measureTextWidth(const std::vector<StyledWord>& styledWords) const;

    // Rendering method
    void renderText(const std::vector<StyledWord>& styledWords, double x, double y) const;

private:
    SkiaRenderer* renderer;
    WordRenderer* wordRenderer; // Pointer to avoid incomplete type issues
};

} // namespace subtitle
} // namespace openshot