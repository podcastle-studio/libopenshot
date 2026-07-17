#include "SubtitleRenderer.h"
#include "SkiaRenderer.h"
#include "TextRenderer.h"
#include "Helpers.h"

#include <skia/include/core/SkFontMetrics.h>

namespace openshot {
namespace subtitle {

SubtitleRenderer::SubtitleRenderer(SkiaRenderer* renderer, const float fps)
    : renderer(renderer), textRenderer(new TextRenderer(renderer)), fps(fps) {}

SubtitleRenderer::~SubtitleRenderer() {
    delete textRenderer;
}

double SubtitleRenderer::getStartY(const int currentLine, const SubtitleTextStyle& textStyle, const SubtitleContainerStyle& containerStyle) {
    const int line = (containerStyle.appearance == TextAppearance::ONE_WORD) ? 0 : currentLine;
    return textStyle.fontSize * (line + 1) + line * (textStyle.lineHeight - textStyle.fontSize);
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

void SubtitleRenderer::drawContainer(const float blockW, const float blockH, const SubtitleContainerStyle& style,
                                     const double verticalOffset) const {
    if (!style.color.has_value() || style.opacity <= 0) return;

    const PaintProps paintProps{ *style.color, style.opacity };
    const SkPaint* paint = renderer->getPaint(paintProps);

    // NB: we are already translated so that the text block’s origin is (0,0). The line-box model
    // places baselines at getStartY = fontSize*(line+1), so the glyphs' true visual centre is
    // `verticalOffset` below the naive block centre (blockH/2). Shift the box by that offset so the
    // text is vertically CENTRED in the rectangle — the same correction the per-word background
    // (drawWordBackground's deltaY) already applies. Without it the text sits low in the box.
    const auto left   = -style.paddingX;
    const auto top    = -style.paddingY + verticalOffset;
    const auto right  =  blockW + style.paddingX;
    const auto bottom =  blockH + style.paddingY + verticalOffset;

    const SkRect rect = renderer->makeRect(left, top, right, bottom);
    const SkRRect rr  = renderer->makeRRect(rect, style.radius, style.radius);

    renderer->drawRRect(rr, *paint);
}

void SubtitleRenderer::renderSegment(const SubtitleSegment& segment, const SegmentSettings& settings,
      const float segmentMs, const float canvasW, const float canvasH) const {
    const SegmentSettings& segSet = segment.attached ? settings : (segment.settings ? *segment.settings : settings);

    // ── PASS 1 : animations without line info (used to discover line breaks) ─────
    auto animPass1 = processSegmentAnimation(segment.wordDetails, segSet, fps);

    // build last-frame styles + TRANSFORMED text to discover line breaks correctly
    std::vector<StyledWord> lastFrame;
    lastFrame.reserve(animPass1.size());
    const float segDuration = segment.endTimeMs - segment.startTimeMs;
    for (const auto& i : animPass1) {
        // style at the end of segment (what we used before for line discovery)
        auto styleAtEnd = applyAnimationParams(i.params, segDuration, fps, segSet.defaultStyle);

        // IMPORTANT: transform BEFORE measuring/line breaking
        // If transformText doesn't need the style, passing it is still harmless.
        const auto transformedWord = transformText(i.word, styleAtEnd, segSet.containerStyle);

        lastFrame.push_back({ transformedWord, std::move(styleAtEnd) });
    }

    const auto& contStyle = segSet.containerStyle;

    // compute line breaks using TRANSFORMED last-frame words
    auto lines = getLines(lastFrame, segSet.transformation.maxWidth, contStyle);

    // ── PASS 2 : real animations WITH line info ───────────────────────────
    auto anim = processSegmentAnimation(segment.wordDetails, segSet, fps, lines);

    // styled + TRANSFORMED words for *current* frame
    std::vector<StyledWord> frameWords;
    frameWords.reserve(anim.size());
    for (size_t i = 0; i < anim.size(); ++i) {
        const auto& wa = anim[i];
        StyledWord sw;

        // style for this frame
        auto styleNow = applyAnimationParams(wa.params, segmentMs, fps, segSet.defaultStyle);

        // transform BEFORE any width/height calculations
        sw.word  = transformText(wa.word, styleNow, segSet.containerStyle);
        sw.style = std::move(styleNow);

        frameWords.push_back(std::move(sw));
    }

    // width / height of the block (all sizing now based on transformed content)
    float maxLineW = 0.0f;
    for (const auto& ln : lines) {
        std::vector<StyledWord> tmp;
        tmp.reserve(ln.size());
        for (auto idx : ln) {
            tmp.push_back(lastFrame[idx]); // already transformed
        }
        maxLineW = std::max(maxLineW, textRenderer->measureTextWidth(tmp));
    }

    // block height: based on number of visual lines (transform affects width/lines;
    // lineHeight is style-driven, not letter case-driven)
    const auto& s = segSet.defaultStyle;
    const auto  blockH = (contStyle.appearance == TextAppearance::ONE_WORD)
        ? s.fontSize
        : s.fontSize * lines.size() + (lines.size() - 1) * (s.lineHeight - s.fontSize);

    // ── transform canvas ────────────────────────────────────────────────
    const float viewportW = segSet.transformation.maxWidth;
    const auto& tr = segSet.transformation;

    renderer->save();
    renderer->translate(canvasW * tr.center.x, canvasH * tr.center.y);
    if (tr.rotation) {
        renderer->rotate(tr.rotation);
    }
    if (tr.scale.horizontalScale != 1.0f || tr.scale.verticalScale != 1.0f) {
        renderer->scale(tr.scale.horizontalScale, tr.scale.verticalScale);
    }
    renderer->translate(-viewportW / 2, -blockH / 2);

    // ── background box  ───────────────────────────────────────────────────
    // Vertical centring offset from the default-style font metrics (matches drawWordBackground's
    // deltaY): the baseline model puts glyphs `deltaY` below the naive block centre, so the
    // container box must shift by the same amount to keep the text centred inside it.
    const SkFont contFont = renderer->getFont({ s.fontFamily, s.fontSize, s.fontWeight, s.italic });
    SkFontMetrics contMetrics{};
    contFont.getMetrics(&contMetrics);
    const double containerDeltaY =
        (s.fontSize - (contMetrics.fDescent - contMetrics.fAscent)) / 2.0 + contMetrics.fDescent;

    drawContainer(viewportW, blockH, contStyle, containerDeltaY);

    // ── draw each visual line ─────────────────────────────────────────────
    for (size_t li = 0; li < lines.size(); ++li) {
        std::vector<StyledWord> lineWords;
        lineWords.reserve(lines[li].size());
        for (auto idx : lines[li]) {
            lineWords.push_back(frameWords[idx]); // transformed current-frame words
        }

        const float lineW = textRenderer->measureTextWidth(lineWords);

        double x;
        if (contStyle.textAlign == TextAlignment::LEFT) {
            x = 0;
        } else if (contStyle.textAlign == TextAlignment::RIGHT) {
            x = viewportW - lineW;
        } else { // CENTER
            x = (viewportW - lineW) / 2.0;
        }

        const double y = getStartY(li, segSet.defaultStyle, contStyle);

        textRenderer->renderText(lineWords, x, y);
    }
    renderer->restore();
}

void SubtitleRenderer::renderSegmentAtFrame(const SubtitleSegment& segment, const SegmentSettings& settings,
                                          const int64_t frameNumber, const float canvasWidth, const float canvasHeight) const {
    const float timeMs = frameToMs(frameNumber, fps);
    const float segmentTimeMs = timeMs - segment.startTimeMs;

    // Half-open [0, duration) so a frame landing exactly on the segment's end boundary is NOT drawn
    // here (it belongs to the next segment) — consistent with SubtitleManager::renderAtFrame.
    if (segmentTimeMs >= 0 && segmentTimeMs < (segment.endTimeMs - segment.startTimeMs)) {
        renderSegment(segment, settings, segmentTimeMs, canvasWidth, canvasHeight);
    }
}


} // namespace subtitle
} // namespace openshot