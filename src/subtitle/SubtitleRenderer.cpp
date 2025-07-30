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
    return containerStyle.paddingY + textStyle.fontSize * (line + 1) + line * (textStyle.lineHeight - textStyle.fontSize); // absolute, not ratio
}

std::vector<std::vector<size_t>> SubtitleRenderer::getLines(const std::vector<StyledWord>& styledWords,
    const float maxWidth, const SubtitleContainerStyle& containerStyles) const {

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

        const float testLineWidth = textRenderer->measureTextWidth(testLine);
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

double SubtitleRenderer::getHeight(const SubtitleSegment& segment, const SegmentSettings& settings, const float maxWidth) const {
    const SegmentSettings &segSet = segment.attached ? settings : (segment.settings ? *segment.settings : settings);

    const auto wordAnims = processSegmentAnimation(segment.wordDetails, segSet, fps);
    const auto& defStyle   = segSet.defaultStyle;
    const auto& contStyle  = segSet.containerStyle;

    std::vector<StyledWord> lastFrame;
    lastFrame.reserve(wordAnims.size());

    for (size_t i = 0; i < wordAnims.size(); ++i) {
      const auto &wa = wordAnims[i];
      const auto &wd = segment.wordDetails[i];

      float dur = wd.endMs - wd.startMs;
      StyledWord sw{wa.word, applyAnimationParams(wa.params, wd.startMs + dur,
                                                  fps, defStyle)};
      lastFrame.push_back(std::move(sw));
    }

    const auto lines = getLines(lastFrame, maxWidth, contStyle);
    return defStyle.fontSize * lines.size() + (lines.size() - 1) * (defStyle.lineHeight - defStyle.fontSize);
}

void SubtitleRenderer::renderSegment(const SubtitleSegment& segment, const SegmentSettings& settings,
    const float segmentTimeMs, const float canvasW, const float canvasH) const {
    const SegmentSettings& segSet = segment.attached ? settings : (segment.settings ? *segment.settings : settings);

    auto wordAnims = processSegmentAnimation(segment.wordDetails, segSet, fps);

    const auto& defStyle   = segSet.defaultStyle;
    const auto& trans      = segSet.transformation;
    const auto& contStyle  = segSet.containerStyle;
    const float maxWidth   = trans.maxWidth;

    // ── build frameStyledWords ───────────────────────────────────────────────
    std::vector<StyledWord> frameWords;
    frameWords.reserve(wordAnims.size());

    for (size_t i = 0; i < wordAnims.size(); ++i) {
        const auto& wa = wordAnims[i];
        const auto& wd = segment.wordDetails[i];

        StyledWord sw; sw.word = transformText(wa.word, defStyle);

        if (segmentTimeMs >= wd.startMs && segmentTimeMs <= wd.endMs) {
            sw.style = applyAnimationParams(wa.params,
                                            wd.startMs + (segmentTimeMs - wd.startMs),
                                            fps, defStyle);
        } else if (segmentTimeMs > wd.endMs) {
            sw.style = applyAnimationParams(wa.params, wd.endMs, fps, defStyle);
        } else { // not started yet – evaluate at current segment time
            sw.style = applyAnimationParams(wa.params, segmentTimeMs, fps, defStyle);
        }
        frameWords.push_back(std::move(sw));
    }

    // layout uses last‑frame styles
    std::vector<StyledWord> lfWords;
    lfWords.reserve(wordAnims.size());
    for (size_t i = 0; i < wordAnims.size(); ++i) {
        const auto& wa = wordAnims[i];
        const auto& wd = segment.wordDetails[i];

        float dur = wd.endMs - wd.startMs;
        lfWords.push_back({ wa.word,
            applyAnimationParams(wa.params, wd.startMs + dur, fps, defStyle) });
    }

    auto lines = getLines(lfWords, maxWidth, contStyle);

    float totalH = defStyle.fontSize * lines.size()
                 + (lines.size() - 1) * (defStyle.lineHeight - defStyle.fontSize);

    float maxLineW = 0;
    for (const auto& ln : lines) {
        std::vector<StyledWord> tmp;
        for (auto idx : ln) tmp.push_back(lfWords[idx]);
        maxLineW = std::max(maxLineW, textRenderer->measureTextWidth(tmp));
    }

    // ── canvas transform ────────────────────────────────────────────────────
    const float cx = canvasW * trans.center.x;
    const float cy = canvasH * trans.center.y;

    renderer->save();
    renderer->translate(cx, cy);
    if (trans.rotation) renderer->rotate(trans.rotation);
    if (trans.scale.horizontalScale != 1.0f || trans.scale.verticalScale != 1.0f)
        renderer->scale(trans.scale.horizontalScale, trans.scale.verticalScale);
    renderer->translate(-maxLineW / 2, -totalH / 2);

    // ── draw lines ──────────────────────────────────────────────────────────
    for (size_t li = 0; li < lines.size(); ++li) {
        std::vector<StyledWord> lineWords;
        for (auto idx : lines[li]) lineWords.push_back(frameWords[idx]);

        float lineW = textRenderer->measureTextWidth(lineWords);

        double x = 0;
        switch (contStyle.textAlign) {
            case TextAlignment::LEFT:   x = contStyle.paddingX; break;
            case TextAlignment::CENTER: x = (maxLineW - lineW) / 2; break;
            case TextAlignment::RIGHT:  x = maxLineW - lineW - contStyle.paddingX; break;
        }
        double y = getStartY(li, defStyle, contStyle);

        textRenderer->renderText(lineWords, x, y);
    }
    renderer->restore();
}

void SubtitleRenderer::renderSegmentAtFrame(const SubtitleSegment& segment, const SegmentSettings& settings,
                                          const int64_t frameNumber, const float canvasWidth, const float canvasHeight) const {
    const float timeMs = frameToMs(frameNumber, fps);
    const float segmentTimeMs = timeMs - segment.startTimeMs;

    if (segmentTimeMs >= 0 && segmentTimeMs <= (segment.endTimeMs - segment.startTimeMs)) {
        renderSegment(segment, settings, segmentTimeMs, canvasWidth, canvasHeight);
    }
}


} // namespace subtitle
} // namespace openshot