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
            return 0;
        case TextAlignment::CENTER:
            return (containerWidth - textWidth) / 2;
        case TextAlignment::RIGHT:
            return containerWidth - textWidth;
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

void SubtitleRenderer::renderSegment(const SubtitleSegment& segment, const SegmentSettings& settings,
      const float segmentMs, const float canvasW, const float canvasH) const {
    const SegmentSettings& segSet = segment.attached ? settings
                              : (segment.settings ? *segment.settings : settings);

    // ── PASS 1 : animations without line info ──────────────────────────────
    auto animPass1 = processSegmentAnimation(segment.wordDetails, segSet, fps);

    // build last‑frame styles to discover line breaks
    std::vector<StyledWord> lastFrame;
    lastFrame.reserve(animPass1.size());
    for (size_t i=0;i<animPass1.size();++i){
        float dur = segment.wordDetails[i].endMs - segment.wordDetails[i].startMs;
        lastFrame.push_back({ animPass1[i].word,
            applyAnimationParams(animPass1[i].params,
                                 segment.wordDetails[i].startMs + dur,
                                 fps,
                                 segSet.defaultStyle) });
    }

    const auto& cont = segSet.containerStyle;
    const float maxWidth = segSet.transformation.maxWidth;
    auto lines = getLines(lastFrame, maxWidth, cont);

    // ── PASS 2 : real animations with line info ───────────────────────────
    auto anim = processSegmentAnimation(segment.wordDetails, segSet, fps, lines);

    // styled words for *current* frame
    std::vector<StyledWord> frameWords;
    frameWords.reserve(anim.size());
    for (size_t i=0;i<anim.size();++i){
        const auto& wa = anim[i];
        const auto& wd = segment.wordDetails[i];
        StyledWord sw; sw.word = transformText(wa.word, segSet.defaultStyle, segSet.containerStyle);

        if (segmentMs >= wd.startMs && segmentMs <= wd.endMs)
            sw.style = applyAnimationParams(wa.params,
                                            wd.startMs + (segmentMs - wd.startMs),
                                            fps,
                                            segSet.defaultStyle);
        else if (segmentMs > wd.endMs)
            sw.style = applyAnimationParams(wa.params,
                                            wd.endMs,
                                            fps,
                                            segSet.defaultStyle);
        else
            sw.style = applyAnimationParams(wa.params,
                                            segmentMs,
                                            fps,
                                            segSet.defaultStyle);

        frameWords.push_back(std::move(sw));
    }

    // width / height of the block
    float maxLineW = 0;
    for (const auto& ln:lines){
        std::vector<StyledWord> tmp;
        for(auto idx:ln) tmp.push_back(lastFrame[idx]);
        maxLineW = std::max(maxLineW, textRenderer->measureTextWidth(tmp));
    }
    const auto& s = segSet.defaultStyle;
    auto blockH = (cont.appearance == TextAppearance::ONE_WORD)
         ? s.fontSize  // just one visual line
         : s.fontSize * lines.size() + (lines.size()-1)*(s.lineHeight - s.fontSize);

    // ── transform canvas (unchanged logic) ────────────────────────────────
    const auto& tr = segSet.transformation;
    renderer->save();
    renderer->translate(canvasW*tr.center.x, canvasH*tr.center.y);
    if (tr.rotation) renderer->rotate(tr.rotation);
    if (tr.scale.horizontalScale!=1.0f || tr.scale.verticalScale!=1.0f)
        renderer->scale(tr.scale.horizontalScale, tr.scale.verticalScale);
    renderer->translate(-maxLineW/2, -blockH/2);

    // ── draw each visual line ─────────────────────────────────────────────
    for (size_t li=0; li<lines.size(); ++li) {
        std::vector<StyledWord> lineWords;
        for(auto idx:lines[li]) lineWords.push_back(frameWords[idx]);

        float lineW = textRenderer->measureTextWidth(lineWords);
        double x = (cont.textAlign == TextAlignment::LEFT)   ? cont.paddingX :
                   (cont.textAlign == TextAlignment::RIGHT)  ? maxLineW - lineW - cont.paddingX :
                                                              (maxLineW - lineW) / 2;
        double y = getStartY(li, segSet.defaultStyle, cont);

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