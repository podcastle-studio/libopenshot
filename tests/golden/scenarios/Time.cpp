// Time: speed (time curve), trims, freeze frames, and the frame-rate mapping the timeline applies.
#include "Recipes.h"

#include "Timeline.h"

using namespace golden;
using namespace golden::recipes;

namespace {

openshot::Clip* speedScene(Scene& s, double speed, double end = 3.0, double trimStart = 0.0) {
    auto& tl = s.makeTimeline();
    tl.AddClip(backgroundClip(s, s.media("background_960x540.png")));
    MediaSpec m; m.path = s.media("clip_a_640x360_30.mp4"); m.end = end; m.speed = speed; m.trimStart = trimStart;
    m.transform = Transform{BBox{0.5f, 0.5f, 0.9f, 0.9f}};
    auto* c = mediaClip(s, m);
    tl.AddClip(c);
    return c;
}

} // namespace

void golden::registerTimeScenarios() {
    add("time.speed_2x", {"time", "speed", "exact"}, {1, 15, 30, 45}, [](Scene& s) {
        speedScene(s, 2.0);
        s.timeline->Open();
    });

    add("time.speed_half", {"time", "speed", "exact"}, {1, 30, 60, 89}, [](Scene& s) {
        speedScene(s, 0.5);
        s.timeline->Open();
    });

    add("time.trim_start_1s", {"time", "trim", "exact"}, {1, 30}, [](Scene& s) {
        speedScene(s, 1.0, 3.0, 1.0);
        s.timeline->Open();
    });

    add("time.freeze_head_tail", {"time", "freeze", "exact"}, {1, 15, 16, 30, 74, 75, 76, 89}, [](Scene& s) {
        // A 2 s clip at 0.5 s grown by 0.5 s holds on both sides (what an overlapping transition does).
        auto& tl = s.makeTimeline();
        tl.AddClip(backgroundClip(s, s.media("background_960x540.png")));
        MediaSpec m; m.path = s.media("clip_a_640x360_30.mp4"); m.start = 0.5; m.end = 2.5;
        m.transform = Transform{BBox{0.5f, 0.5f, 0.9f, 0.9f}};
        auto* c = mediaClip(s, m);
        const double fps = s.fps.ToDouble();
        const float head = 0.5f, tail = 0.5f;
        c->End(c->EndRaw() + head + tail);
        const auto before = std::llround(c->Position() * fps);
        c->Position(c->Position() - head);
        c->mFreezeFramesCountAtBeginning = std::max<int64_t>(0, before - std::llround(c->Position() * fps));
        c->mFreezeFramesCountAtEnd = std::max<int64_t>(0, std::llround((head + tail) * fps) - c->mFreezeFramesCountAtBeginning);
        tl.AddClip(c);
        tl.Open();
    });

    add("time.image_clip_long_duration", {"time", "exact"}, {1, 200}, [](Scene& s) {
        auto& tl = s.makeTimeline();
        MediaSpec m; m.path = s.media("image_rgb_400x300.jpg"); m.isImage = true; m.end = 10.0;
        m.transform = Transform{BBox{0.5f, 0.5f, 0.8f, 0.8f}};
        tl.AddClip(mediaClip(s, m));
        tl.Open();
    });
}
