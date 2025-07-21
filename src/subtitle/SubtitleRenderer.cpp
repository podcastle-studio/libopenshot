/**
 * @file
 * @brief Implementation of SubtitleRenderer class
 *
 * @ref License
 */

// Copyright (c) 2008-2024 OpenShot Studios, LLC
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "SubtitleRenderer.h"
#include "SkiaRenderer.h"
#include "TextRenderer.h"
#include "Helpers.h"

namespace openshot {
namespace subtitle {

SubtitleRenderer::SubtitleRenderer(SkiaRenderer* renderer, const float fps)
    : renderer(renderer), textRenderer(new TextRenderer(renderer)), fps(fps) {}

SubtitleRenderer::~SubtitleRenderer() {
    delete textRenderer;
}

std::vector<StyledWord> SubtitleRenderer::getAnimatedStyledWords(const std::vector<WordAnimation>& wordAnimations,
    const float timeMs, const SubtitleTextStyle& defaultStyle) const {

    std::vector<StyledWord> styledWords;

    for (const auto& wordAnim : wordAnimations) {
        StyledWord styledWord;
        styledWord.word = transformText(wordAnim.word, defaultStyle);
        styledWord.style = applyAnimationParams(
            wordAnim.params,
            wordAnim.colorParams,
            timeMs,
            fps,
            defaultStyle
        );
        styledWords.push_back(styledWord);
    }

    return styledWords;
}

double SubtitleRenderer::getStartX(const float textWidth, const float containerWidth, const SubtitleContainerStyle& containerStyles) {
    switch (containerStyles.textAlign) {
        case TextAlignment::LEFT:
            return containerStyles.paddingX;
        case TextAlignment::CENTER:
            return containerWidth / 2 - textWidth / 2;
        case TextAlignment::RIGHT:
            return containerWidth - textWidth - containerStyles.paddingX;
        default:
            return 0;
    }
}

double SubtitleRenderer::getStartY(const int currentLine, const SubtitleTextStyle& textStyle, const SubtitleContainerStyle& containerStyle) {
    const int line = (containerStyle.appearance == TextAppearance::ONE_WORD) ? 0 : currentLine;

    return containerStyle.paddingY + textStyle.fontSize * (line + 1) + line * (textStyle.lineHeight - 1) * textStyle.fontSize;
}

std::vector<std::vector<size_t>> SubtitleRenderer::getLines(const std::vector<StyledWord>& styledWords,
    const float maxWidth, const SubtitleContainerStyle& containerStyles) const {

    std::vector<std::vector<size_t>> lines;

    if (containerStyles.appearance == TextAppearance::ONE_WORD) {
        // Each word on its own line
        for (size_t i = 0; i < styledWords.size(); ++i) {
            lines.push_back({static_cast<size_t>(i)});
        }
        return lines;
    }

    // Normal line breaking
    std::vector<size_t> currentLine;
    size_t startIdx = 0;

    for (size_t i = 0; i < styledWords.size(); ++i) {
        // Get words for test line
        std::vector<StyledWord> testLine;
        for (size_t j = startIdx; j <= i; ++j) {
            testLine.push_back(styledWords[j]);
        }

        if (const float testLineWidth = textRenderer->measureTextWidth(testLine); testLineWidth > maxWidth && !currentLine.empty()) {
            lines.push_back(currentLine);
            startIdx = i;
            currentLine.clear();
        }
        currentLine.push_back(i);
    }

    if (!currentLine.empty()) {
        lines.push_back(currentLine);
    }

    return lines;
}

double SubtitleRenderer::getHeight(const SubtitleSegment& segment, const SegmentSettings& settings, const float maxWidth) const {
    const SegmentSettings& segmentSettings = segment.attached ? settings :
        (segment.settings.has_value() ? segment.settings.value() : settings);

    const std::vector<WordAnimation> wordAnimations = processSegmentAnimation(segment.wordDetails, segmentSettings, fps);

    const auto& defaultStyle = segmentSettings.defaultStyle;
    const auto& containerStyle = segmentSettings.containerStyle;

    // Get styled words at the last frame
    const std::vector<StyledWord> lastFrameStyledWords = getAnimatedStyledWords(wordAnimations, segment.endTimeMs, defaultStyle);

    const auto lines = getLines(lastFrameStyledWords, maxWidth, containerStyle);

    return defaultStyle.fontSize * lines.size() + (lines.size() - 1) * (defaultStyle.lineHeight - 1) * defaultStyle.fontSize;
}

void SubtitleRenderer::renderSegment(const SubtitleSegment& segment, const SegmentSettings& settings, const float segmentTimeMs) const {
    const SegmentSettings& segmentSettings = segment.attached ? settings :
        (segment.settings.has_value() ? segment.settings.value() : settings);

    const std::vector<WordAnimation> wordAnimations = processSegmentAnimation(segment.wordDetails, segmentSettings, fps);

    const auto& defaultStyle = segmentSettings.defaultStyle;
    const auto& transformation = segmentSettings.transformation;
    const auto& containerStyle = segmentSettings.containerStyle;
    const float maxWidth = transformation.maxWidth;

    // Get styled words for current time
    std::vector<StyledWord> frameStyledWords = getAnimatedStyledWords(wordAnimations, segmentTimeMs, defaultStyle);

    // Get styled words for last frame (for layout calculation)
    const std::vector<StyledWord> lastFrameStyledWords = getAnimatedStyledWords(wordAnimations, segment.endTimeMs, defaultStyle);

    const auto lines = getLines(lastFrameStyledWords, maxWidth, containerStyle);

    for (size_t lineIdx = 0; lineIdx < lines.size(); ++lineIdx) {
        const auto& line = lines[lineIdx];

        // Get styled words for this line
        std::vector<StyledWord> lineStyledWords;
        for (const auto wordIdx : line) {
            if (wordIdx < frameStyledWords.size()) {
                lineStyledWords.push_back(frameStyledWords[wordIdx]);
            }
        }

        const float totalLineWidth = textRenderer->measureTextWidth(lineStyledWords);
        const auto x = getStartX(totalLineWidth, maxWidth, containerStyle);
        const auto y = getStartY(lineIdx, defaultStyle, containerStyle);

        textRenderer->renderText(lineStyledWords, x, y);
    }
}

void SubtitleRenderer::renderSegmentAtFrame(const SubtitleSegment& segment, const SegmentSettings& settings, const int64_t frameNumber) {
    const float timeMs = frameToMs(frameNumber, fps);
    const float segmentTimeMs = timeMs - segment.startTimeMs;

    if (segmentTimeMs >= 0 && segmentTimeMs <= (segment.endTimeMs - segment.startTimeMs)) {
        renderSegment(segment, settings, segmentTimeMs);
    }
}

} // namespace subtitle
} // namespace openshot