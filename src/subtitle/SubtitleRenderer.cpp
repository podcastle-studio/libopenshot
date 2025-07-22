#include "SubtitleRenderer.h"
#include "SkiaRenderer.h"
#include "TextRenderer.h"
#include "Helpers.h"

namespace openshot {
namespace subtitle {

SubtitleRenderer::SubtitleRenderer(SkiaRenderer* renderer, float fps)
    : renderer(renderer), textRenderer(new TextRenderer(renderer)), fps(fps) {}

SubtitleRenderer::~SubtitleRenderer() {
    delete textRenderer;
}

std::vector<StyledWord> SubtitleRenderer::getAnimatedStyledWords(
    const std::vector<WordAnimation>& wordAnimations,
    float segmentTimeMs, const SubtitleTextStyle& defaultStyle) const {

    std::vector<StyledWord> styledWords;

    for (size_t i = 0; i < wordAnimations.size(); ++i) {
        const auto& wordAnim = wordAnimations[i];
        StyledWord styledWord;
        styledWord.word = transformText(wordAnim.word, defaultStyle);

        // CRITICAL: JavaScript passes elapsed time relative to each word's start time
        // Calculate elapsed time for this specific word
        const auto& wordDetail = wordAnim.word; // We need to track word timings
        float wordStartMs = 0;
        float wordEndMs = 0;

        // Find the corresponding word detail to get timing
        // Note: This assumes wordAnimations are created in the same order as wordDetails
        // which is guaranteed by processSegmentAnimation
        if (i < wordAnimations.size()) {
            // Get word timing from the animation (we'll need to modify WordAnimation to include timing)
            // For now, we'll use segmentTimeMs directly, but this needs to be fixed
            // to match JS behavior where elapsed = segmentTimeMs - wordStartMs
        }

        // Apply animations at the current segment time
        styledWord.style = applyAnimationParams(
            wordAnim.params,
            segmentTimeMs, // This should be elapsed time relative to word start
            fps,
            defaultStyle
        );

        styledWords.push_back(styledWord);
    }

    return styledWords;
}

double SubtitleRenderer::getStartX(float textWidth, float containerWidth, const SubtitleContainerStyle& containerStyles) {
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

double SubtitleRenderer::getStartY(int currentLine, const SubtitleTextStyle& textStyle,
                                   const SubtitleContainerStyle& containerStyle) {
    int line = (containerStyle.appearance == TextAppearance::ONE_WORD) ? 0 : currentLine;

    // Same calculation as JS: fontSize * (line + 1) + line * (lineHeight - 1) * fontSize
    return containerStyle.paddingY +
           textStyle.fontSize * (line + 1) +
           line * (textStyle.lineHeight - 1) * textStyle.fontSize;
}

std::vector<std::vector<size_t>> SubtitleRenderer::getLines(
    const std::vector<StyledWord>& styledWords,
    float maxWidth, const SubtitleContainerStyle& containerStyles) const {

    std::vector<std::vector<size_t>> lines;

    if (containerStyles.appearance == TextAppearance::ONE_WORD) {
        // Each word on its own line
        for (size_t i = 0; i < styledWords.size(); ++i) {
            lines.push_back({i});
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

        float testLineWidth = textRenderer->measureTextWidth(testLine);

        if (testLineWidth > maxWidth && !currentLine.empty()) {
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

double SubtitleRenderer::getHeight(const SubtitleSegment& segment, const SegmentSettings& settings, float maxWidth) const {
    const SegmentSettings& segmentSettings = segment.attached ? settings :
        (segment.settings.has_value() ? segment.settings.value() : settings);

    std::vector<WordAnimation> wordAnimations = processSegmentAnimation(
        segment.wordDetails, segmentSettings, fps);

    const auto& defaultStyle = segmentSettings.defaultStyle;
    const auto& containerStyle = segmentSettings.containerStyle;

    // Get styled words at the last frame (end of segment)
    float segmentDuration = segment.endTimeMs - segment.startTimeMs;

    // For height calculation, we need words at their final state
    std::vector<StyledWord> lastFrameStyledWords;
    for (size_t i = 0; i < wordAnimations.size(); ++i) {
        const auto& wordAnim = wordAnimations[i];
        const auto& wordDetail = segment.wordDetails[i];

        StyledWord styledWord;
        styledWord.word = transformText(wordAnim.word, defaultStyle);

        // Apply animations at the end of this word's duration
        float wordElapsed = wordDetail.endMs - wordDetail.startMs;
        styledWord.style = applyAnimationParams(
            wordAnim.params,
            wordDetail.startMs + wordElapsed, // Use absolute time for keyframe lookup
            fps,
            defaultStyle
        );

        lastFrameStyledWords.push_back(styledWord);
    }

    auto lines = getLines(lastFrameStyledWords, maxWidth, containerStyle);

    return defaultStyle.fontSize * lines.size() +
           (lines.size() - 1) * (defaultStyle.lineHeight - 1) * defaultStyle.fontSize;
}

void SubtitleRenderer::renderSegment(const SubtitleSegment& segment, const SegmentSettings& settings,
                                   float segmentTimeMs, float canvasWidth, float canvasHeight) const {
    const SegmentSettings& segmentSettings = segment.attached ? settings :
        (segment.settings.has_value() ? segment.settings.value() : settings);

    std::vector<WordAnimation> wordAnimations = processSegmentAnimation(
        segment.wordDetails, segmentSettings, fps);

    const auto& defaultStyle = segmentSettings.defaultStyle;
    const auto& transformation = segmentSettings.transformation;
    const auto& containerStyle = segmentSettings.containerStyle;
    float maxWidth = transformation.maxWidth;

    // Get styled words for current time
    std::vector<StyledWord> frameStyledWords;
    for (size_t i = 0; i < wordAnimations.size(); ++i) {
        const auto& wordAnim = wordAnimations[i];
        const auto& wordDetail = segment.wordDetails[i];

        StyledWord styledWord;
        styledWord.word = transformText(wordAnim.word, defaultStyle);

        // Check if this word should be visible at current segment time
        if (segmentTimeMs >= wordDetail.startMs && segmentTimeMs <= wordDetail.endMs) {
            // Word is active - calculate elapsed time relative to word start
            float wordElapsed = segmentTimeMs - wordDetail.startMs;

            // Apply animations using absolute time (word start + elapsed)
            styledWord.style = applyAnimationParams(
                wordAnim.params,
                wordDetail.startMs + wordElapsed, // Absolute time for keyframe lookup
                fps,
                defaultStyle
            );
        } else if (segmentTimeMs > wordDetail.endMs) {
            // Word has finished - show at final state
            float wordDuration = wordDetail.endMs - wordDetail.startMs;
            styledWord.style = applyAnimationParams(
                wordAnim.params,
                wordDetail.startMs + wordDuration, // End time
                fps,
                defaultStyle
            );
        } else {
            // Word hasn't started yet - show at initial state
            styledWord.style = applyAnimationParams(
                wordAnim.params,
                wordDetail.startMs, // Start time
                fps,
                defaultStyle
            );
        }

        frameStyledWords.push_back(styledWord);
    }

    // Get styled words for last frame (for layout calculation)
    std::vector<StyledWord> lastFrameStyledWords;
    for (size_t i = 0; i < wordAnimations.size(); ++i) {
        const auto& wordAnim = wordAnimations[i];
        const auto& wordDetail = segment.wordDetails[i];

        StyledWord styledWord;
        styledWord.word = transformText(wordAnim.word, defaultStyle);

        // Apply animations at the end of this word's duration
        float wordDuration = wordDetail.endMs - wordDetail.startMs;
        styledWord.style = applyAnimationParams(
            wordAnim.params,
            wordDetail.startMs + wordDuration,
            fps,
            defaultStyle
        );

        lastFrameStyledWords.push_back(styledWord);
    }

    auto lines = getLines(lastFrameStyledWords, maxWidth, containerStyle);

    // Calculate total subtitle block dimensions
    float totalHeight = defaultStyle.fontSize * lines.size() +
                       (lines.size() - 1) * (defaultStyle.lineHeight - 1) * defaultStyle.fontSize;

    // Find the widest line
    float maxLineWidth = 0;
    for (const auto& line : lines) {
        std::vector<StyledWord> lineWords;
        for (size_t wordIdx : line) {
            if (wordIdx < lastFrameStyledWords.size()) {
                lineWords.push_back(lastFrameStyledWords[wordIdx]);
            }
        }
        float lineWidth = textRenderer->measureTextWidth(lineWords);
        maxLineWidth = std::max(maxLineWidth, lineWidth);
    }

    // Calculate the center position based on transformation settings
    // center.x and center.y are percentages (0.5 = 50%, 0.9 = 90%)
    float centerX = canvasWidth * transformation.center.x;
    float centerY = canvasHeight * transformation.center.y;

    // Apply transformations
    renderer->save();

    // Step 1: Translate to the desired center point
    renderer->translate(centerX, centerY);

    // Step 2: Apply rotation around this center point
    if (transformation.rotation != 0) {
        renderer->rotate(transformation.rotation);
    }

    // Step 3: Apply scale around this center point
    if (transformation.scale.horizontalScale != 1.0f || transformation.scale.verticalScale != 1.0f) {
        renderer->scale(transformation.scale.horizontalScale, transformation.scale.verticalScale);
    }

    // Step 4: Offset to position the content correctly
    // The content should be centered horizontally around the center point
    // For vertical, since center.y = 0.9 (90%), the text should be positioned
    // so that its vertical center is at 90% of canvas height
    float contentOffsetX = -maxLineWidth / 2;
    float contentOffsetY = -totalHeight / 2;

    renderer->translate(contentOffsetX, contentOffsetY);

    // Render each line
    for (size_t lineIdx = 0; lineIdx < lines.size(); ++lineIdx) {
        const auto& line = lines[lineIdx];

        // Get styled words for this line at current time
        std::vector<StyledWord> lineStyledWords;
        for (size_t wordIdx : line) {
            if (wordIdx < frameStyledWords.size()) {
                lineStyledWords.push_back(frameStyledWords[wordIdx]);
            }
        }

        float totalLineWidth = textRenderer->measureTextWidth(lineStyledWords);

        // Calculate x position for this line based on alignment
        double x = 0;
        switch (containerStyle.textAlign) {
            case TextAlignment::LEFT:
                x = containerStyle.paddingX;
                break;
            case TextAlignment::CENTER:
                x = (maxLineWidth - totalLineWidth) / 2;
                break;
            case TextAlignment::RIGHT:
                x = maxLineWidth - totalLineWidth - containerStyle.paddingX;
                break;
        }

        // Calculate y position for this line
        double y = getStartY(lineIdx, defaultStyle, containerStyle);

        textRenderer->renderText(lineStyledWords, x, y);
    }

    // Restore canvas state
    renderer->restore();
}

// Update the renderSegmentAtFrame to pass canvas dimensions
void SubtitleRenderer::renderSegmentAtFrame(const SubtitleSegment& segment, const SegmentSettings& settings,
                                          int64_t frameNumber, float canvasWidth, float canvasHeight) {
    float timeMs = frameToMs(frameNumber, fps);
    float segmentTimeMs = timeMs - segment.startTimeMs;

    if (segmentTimeMs >= 0 && segmentTimeMs <= (segment.endTimeMs - segment.startTimeMs)) {
        renderSegment(segment, settings, segmentTimeMs, canvasWidth, canvasHeight);
    }
}


} // namespace subtitle
} // namespace openshot