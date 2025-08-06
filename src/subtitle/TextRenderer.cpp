#include "TextRenderer.h"
#include "SkiaRenderer.h"
#include "WordRenderer.h"
#include <limits>

namespace openshot {
namespace subtitle {

TextRenderer::TextRenderer(SkiaRenderer* renderer) 
    : renderer(renderer), wordRenderer(new WordRenderer(renderer)) {}

TextRenderer::~TextRenderer() {
    delete wordRenderer;
}

TextBounds TextRenderer::getTextVerticalBounds(const std::vector<StyledWord>& styledWords) const {
    TextBounds bounds{std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()};
    for (const auto& styledWord : styledWords) {
        TextBounds wordBounds = wordRenderer->getTextHeight(styledWord.word, styledWord.style);
        
        bounds.top    = std::max(bounds.top,    wordBounds.top);    // least-negative
        bounds.bottom = std::min(bounds.bottom, wordBounds.bottom); // least-positive
    }

    return bounds;
}

float TextRenderer::measureTextWidth(const std::vector<StyledWord>& styledWords) const {
    if (styledWords.empty()) {
        return 0;
    }

    float totalWidth = 0;
    for (const auto& styledWord : styledWords) {
        totalWidth += wordRenderer->getTextWidth(styledWord.word, styledWord.style);
    }

    // Add space widths between words
    const float spaceWidth = wordRenderer->getSpaceWidth(styledWords[0].style);
    totalWidth += (styledWords.size() - 1) * spaceWidth;

    return totalWidth;
}

void TextRenderer::renderText(const std::vector<StyledWord>& styledWords, const double x, const double y) const {
    if (styledWords.empty()) {
        return;
    }

    // Calculate word widths
    std::vector<double> wordWidths;
    wordWidths.reserve(styledWords.size());
    for (const auto& styledWord : styledWords) {
        wordWidths.push_back(wordRenderer->getTextWidth(styledWord.word, styledWord.style));
    }

    const float spaceWidth = wordRenderer->getSpaceWidth(styledWords[0].style);
    const TextBounds bounds = getTextVerticalBounds(styledWords);

    auto currentX = x;
    for (size_t i = 0; i < styledWords.size(); ++i) {
        const auto& styledWord = styledWords[i];
        const auto& wordStyle = styledWord.style;
        const auto& wordWidth = wordWidths[i];

        // Calculate vertical offset
        const auto deltaY = (wordStyle.fontSize - (bounds.bottom - bounds.top)) / 2 + bounds.bottom;

        // Calculate translation offsets (as percentage of font size)
        const auto dx = (wordStyle.translateX.value_or(0) * wordStyle.fontSize) / 100;
        const auto dy = (wordStyle.translateY.value_or(0) * wordStyle.fontSize) / 100;

        wordRenderer->renderWord(styledWord.word, wordStyle, currentX + wordWidth / 2 + dx, y + dy, deltaY);

        currentX += wordWidth + spaceWidth;
    }
}

} // namespace subtitle
} // namespace openshot