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
    TextBounds bounds{ -std::numeric_limits<double>::infinity(),  std::numeric_limits<double>::infinity() };
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
    for (size_t i = 0; i < styledWords.size(); ++i) {
        totalWidth += wordRenderer->getTextWidth(styledWords[i].word, styledWords[i].style);

        // Add space width between words (not after the last word)
        if (i + 1 < styledWords.size()) {
            totalWidth += wordRenderer->getSpaceWidth(styledWords[i].style);
        }
    }
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

    const TextBounds bounds = getTextVerticalBounds(styledWords);

    // Precompute per-word positions and vertical offsets
    std::vector<double> posX;
    std::vector<double> posY;
    std::vector<double> deltaYs;
    posX.reserve(styledWords.size());
    posY.reserve(styledWords.size());
    deltaYs.reserve(styledWords.size());

    auto currentX = x;
    for (size_t i = 0; i < styledWords.size(); ++i) {
        const auto& styledWord = styledWords[i];
        const auto& wordStyle = styledWord.style;
        const auto& wordWidth = wordWidths[i];

        const auto deltaY = (wordStyle.fontSize - (bounds.bottom - bounds.top)) / 2 + bounds.bottom;

        const auto dx = (wordStyle.translateX.value_or(0) * wordStyle.fontSize) / 100;
        const auto dy = (wordStyle.translateY.value_or(0) * wordStyle.fontSize) / 100;

        posX.push_back(currentX + wordWidth / 2 + dx);
        posY.push_back(y + dy);
        deltaYs.push_back(deltaY);

        currentX += wordWidth;

        // Add space width between words (not after the last word)
        if (i + 1 < styledWords.size()) {
            currentX += wordRenderer->getSpaceWidth(styledWord.style);
        }
    }

    // Pass 1: backgrounds
    for (size_t i = 0; i < styledWords.size(); ++i) {
        const auto& styledWord = styledWords[i];
        renderer->save();
        renderer->translate(posX[i], posY[i]);
        wordRenderer->drawWordBackground(static_cast<float>(wordWidths[i]), styledWord.style, static_cast<float>(deltaYs[i]));
        renderer->restore();
    }

    // Pass 2: shadows
    for (size_t i = 0; i < styledWords.size(); ++i) {
        const auto& styledWord = styledWords[i];
        renderer->save();
        renderer->translate(posX[i], posY[i]);
        wordRenderer->drawWordShadow(styledWord.word, static_cast<float>(wordWidths[i]), styledWord.style);
        renderer->restore();
    }

    // Pass 3: strokes
    for (size_t i = 0; i < styledWords.size(); ++i) {
        const auto& styledWord = styledWords[i];
        renderer->save();
        renderer->translate(posX[i], posY[i]);
        wordRenderer->drawWordStroke(styledWord.word, static_cast<float>(wordWidths[i]), styledWord.style);
        renderer->restore();
    }

    // Pass 4: text
    for (size_t i = 0; i < styledWords.size(); ++i) {
        const auto& styledWord = styledWords[i];
        renderer->save();
        renderer->translate(posX[i], posY[i]);
        wordRenderer->drawWordText(styledWord.word, static_cast<float>(wordWidths[i]), styledWord.style);
        renderer->restore();
    }
}

} // namespace subtitle
} // namespace openshot