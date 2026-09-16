// Subtitles: Timeline::LoadSubtitlesFromJsonString with the JSON shape the service produces.
#include "Recipes.h"

#include "Timeline.h"

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

} // namespace

void golden::registerSubtitleScenarios() {
    const Tolerance text = Tolerance::Loose();
    const std::vector<int64_t> F = {5, 20, 40, 55, 75};

    add("subtitles.per_time", {"subtitles", "exact"}, F, [](Scene& s) { subtitleScene(s, SubtitleVariant::PerTime); }, text);
    add("subtitles.one_word_container", {"subtitles", "exact"}, F, [](Scene& s) { subtitleScene(s, SubtitleVariant::OneWordContainer); }, text);
    add("subtitles.animated_in_out", {"subtitles", "animation", "exact"}, {1, 3, 6, 16, 18, 46, 68},
        [](Scene& s) { subtitleScene(s, SubtitleVariant::AnimatedInOut); }, text);
}
