// Subtitles: Timeline::LoadSubtitlesFromJsonString with the JSON shape the service produces.
#include "Recipes.h"

#include "Timeline.h"
#include "subtitle/Helpers.h"

#include <map>
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
// Two production payloads (2026-10-02), each stacking words at boundaries before its fix:
//  - a 100 ms fade-in + slide, centred on the word's start, whose first half ran while the
//    previous word was still fully opaque;
//  - no in-animation at all (inDuration 0): the word's hold-hidden point and its in point fell
//    on the same frame, Keyframe::AddPoint overwrote the first with the second, and every word
//    faded in linearly from the segment's start, under all the words before it.
void checkOneWordNeverOverlaps(std::vector<Check>& checks) {
    using namespace openshot::subtitle;
    struct Case {
        const char* name;
        float inDuration;
        std::map<std::string, double> inStyles;
        std::vector<WordDetail> words;
        float segStart, segEnd;
        std::vector<std::pair<float, float>> gaps;  // real pauses between words, blank frames allowed
    };
    const std::vector<Case> cases = {
        {"one_word_overlap_", 100, {{"opacity", 1}, {"strokeOpacity", 1}, {"translateY", 0}},
         {{"Let's", 2560, 2800}, {"talk", 2800, 2920}, {"about", 2960, 3200}, {"creativity.", 3200, 4120}},
         2560, 4120, {{2920, 2960}}},
        {"one_word_no_in_animation_", 0, {},
         {{"People", 0, 400}, {"have", 400, 640}, {"strong", 640, 1000}, {"reactions", 1000, 1600},
          {"when", 1600, 1800}, {"they", 1800, 2000}, {"think,", 2000, 2400}, {"oh,", 2440, 2640},
          {"the", 2680, 2800}},
         0, 2800, {{2400, 2440}, {2640, 2680}}},
    };

    for (const Case& c : cases) {
        SegmentSettings set;
        set.containerStyle.appearance = TextAppearance::ONE_WORD;
        set.defaultStyle.opacity = c.inStyles.empty() ? 1 : 0;
        set.defaultStyle.translateY = c.inStyles.empty() ? 0 : 24;
        set.animationSettings.inDuration = c.inDuration;
        set.animationSettings.outDuration = 0;
        set.animationSettings.inStyles = c.inStyles;

        for (const float fps : {23.976f, 25.f, 30.f, 50.f, 60.f}) {
            const auto anim = processSegmentAnimation(c.words, set, fps);
            int overlapping = 0, gaps = 0;
            std::string first;
            for (int64_t f = 1;; ++f) {
                const float t = frameToMs(f, fps);
                if (t < c.segStart) continue;
                if (t >= c.segEnd) break;
                int visible = 0;
                for (const auto& a : anim)
                    if (applyAnimationParams(a.params, t - c.segStart, fps, set.defaultStyle).opacity > 0) ++visible;
                if (visible > 1 && overlapping++ == 0) first = "frame " + std::to_string(f);
                // Nor may a swap between contiguous words leave a blank frame. Exempt, one frame
                // either side for quantisation: the segment's first frame (its fade-in starts at
                // 0), its last (the last word ends there), and the real pauses.
                const float frame = 1000.f / fps;
                bool exempt = t < c.segStart + frame || t >= c.segEnd - frame;
                for (const auto& [g0, g1] : c.gaps) exempt |= t >= g0 - frame && t < g1 + frame;
                if (visible == 0 && !exempt && gaps++ == 0) first = "frame " + std::to_string(f);
            }
            const std::string name = c.name + std::to_string(static_cast<int>(fps * 1000 + 0.5f));
            checks.push_back({name, overlapping == 0 && gaps == 0,
                              overlapping ? std::to_string(overlapping) + " frames with two words, first " + first
                              : gaps      ? std::to_string(gaps) + " blank frames between contiguous words, first " + first
                                          : "one word per frame"});
        }
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
