// Subtitles: Timeline::LoadSubtitlesFromJsonString with the JSON shape the service produces.
#include "Recipes.h"

#include "Timeline.h"
#include "subtitle/Helpers.h"

#include <string>

using namespace golden;
using namespace golden::recipes;

namespace {

void subtitleScene(Scene& s, SubtitleVariant v) {
    auto& tl = s.makeTimeline();
    tl.AddClip(backgroundClip(s, s.media("background_960x540.png")));
    MediaSpec m; m.path = s.media("clip_a_640x360_30.mp4"); m.end = 3.0; m.transform = Transform{};
    tl.AddClip(mediaClip(s, m));
    tl.LoadSubtitlesFromJsonString(subtitlesJson(s.font("NotoSans-Bold.ttf"), s.width, v));
    tl.Open();
}

// ONE_WORD draws every word at the same spot, so at most one word may be visible on any frame.
// The in-animation is centred on the word's start; before 2026-10-02 its first half ran while the
// previous word was still fully opaque, stacking two words at every boundary. Word timings are
// the production payload's from that report (contiguous words, a gap, a 100 ms fade-in + slide).
void checkOneWordNeverOverlaps(std::vector<Check>& checks) {
    using namespace openshot::subtitle;
    SegmentSettings set;
    set.containerStyle.appearance = TextAppearance::ONE_WORD;
    set.defaultStyle.opacity = 0;
    set.defaultStyle.translateY = 24;
    set.animationSettings.inDuration = 100;
    set.animationSettings.outDuration = 0;
    set.animationSettings.inStyles = {{"opacity", 1}, {"strokeOpacity", 1}, {"translateY", 0}};
    const std::vector<WordDetail> words = {{"Let's", 2560, 2800}, {"talk", 2800, 2920},
        {"about", 2960, 3200}, {"creativity.", 3200, 4120}};
    const float segStart = 2560, segEnd = 4120;

    for (const float fps : {23.976f, 25.f, 30.f, 50.f, 60.f}) {
        const auto anim = processSegmentAnimation(words, set, fps);
        int overlapping = 0, gaps = 0;
        std::string first;
        for (int64_t f = 1;; ++f) {
            const float t = frameToMs(f, fps);
            if (t < segStart) continue;
            if (t >= segEnd) break;
            int visible = 0;
            for (const auto& a : anim)
                if (applyAnimationParams(a.params, t - segStart, fps, set.defaultStyle).opacity > 0) ++visible;
            if (visible > 1 && overlapping++ == 0) first = "frame " + std::to_string(f);
            // Nor may a swap between contiguous words leave a blank frame. Exempt, one frame
            // either side for quantisation: the segment's first frame (its fade-in starts at
            // 0), its last (the last word ends there), and the one real gap, talk -> about.
            const float frame = 1000.f / fps;
            const bool exempt = t < segStart + frame || t >= segEnd - frame ||
                                (t >= 2920 - frame && t < 2960 + frame);
            if (visible == 0 && !exempt && gaps++ == 0) first = "frame " + std::to_string(f);
        }
        const std::string name = "one_word_overlap_" + std::to_string(static_cast<int>(fps * 1000 + 0.5f));
        checks.push_back({name, overlapping == 0 && gaps == 0,
                          overlapping ? std::to_string(overlapping) + " frames with two words, first " + first
                          : gaps      ? std::to_string(gaps) + " blank frames between contiguous words, first " + first
                                      : "one word per frame"});
    }
}

} // namespace

void golden::registerSubtitleScenarios() {
    const Tolerance text = Tolerance::Loose();
    const std::vector<int64_t> F = {5, 20, 40, 55, 75};

    // Since W17 the subtitle pass draws onto the Timeline's GPU canvas when there is one,
    // so the glyphs and the container rounded rect are antialiased by Skia's GPU rasteriser
    // rather than its CPU one. Sub-pixel edge differences only -- worst measured 55.4 dB /
    // SSIM 0.9999, confined to the subtitle band -- so these stay bit-exact on the CPU path,
    // which is untouched, and take the "close" class only when a GPU is actually compositing.
    add("subtitles.per_time", {"subtitles", "exact", "gpu-composite"}, F, [](Scene& s) { subtitleScene(s, SubtitleVariant::PerTime); }, text);
    add("subtitles.one_word_container", {"subtitles", "exact", "gpu-composite"}, F, [](Scene& s) { subtitleScene(s, SubtitleVariant::OneWordContainer); }, text);
    add("subtitles.animated_in_out", {"subtitles", "animation", "exact", "gpu-composite"}, {1, 3, 6, 16, 18, 46, 68},
        [](Scene& s) { subtitleScene(s, SubtitleVariant::AnimatedInOut); }, text);
    addCustom("unit.subtitle_one_word", {"subtitles", "unit"}, [](Scene& s) { s.makeTimeline().Open(); },
        [](Scene&, std::vector<Captured>&, std::vector<Check>& checks) { checkOneWordNeverOverlaps(checks); });
}
