// Clip-level drop shadow and blur (podcastle additions to Clip).
#include "Recipes.h"

#include "Timeline.h"

using namespace golden;
using namespace golden::recipes;

namespace {

openshot::Clip* imageClip(Scene& s, float rotation = 0.f) {
    MediaSpec m; m.path = s.media("image_alpha_320x200.png"); m.isImage = true; m.end = 3.0;
    m.transform = Transform{BBox{0.5f, 0.5f, 0.55f, 0.55f}, 1.f, 1.f, rotation};
    return mediaClip(s, m);
}

openshot::Keyframe ramp(double a, double b, int f0 = 1, int f1 = 90) {
    openshot::Keyframe k;
    k.AddPoint(f0, a, openshot::LINEAR);
    k.AddPoint(f1, b, openshot::LINEAR);
    return k;
}

} // namespace

void golden::registerClipFxScenarios() {
    add("clipfx.shadow", {"clipfx", "shadow", "exact", "gpu-composite"}, {1, 45, 89},
        [](Scene& s) {
            auto& tl = s.makeTimeline();
            tl.AddClip(backgroundClip(s, s.media("background_960x540.png")));
            auto* c = imageClip(s);
            c->Shadow(true);
            c->shadow_color = openshot::Color(0, 0, 0, 200);
            c->shadow_blur = openshot::Keyframe(12.0);
            c->shadow_distance = ramp(0, 40);
            c->shadow_angle = ramp(45, 135);
            tl.AddClip(c);
            tl.Open();
        });

    add("clipfx.shadow_colored_sharp", {"clipfx", "shadow", "exact", "gpu-composite"}, {30},
        [](Scene& s) {
            auto& tl = s.makeTimeline();
            tl.AddClip(backgroundClip(s, s.media("background_960x540.png")));
            auto* c = imageClip(s);
            c->Shadow(true);
            c->shadow_color = openshot::Color(255, 40, 40, 255);
            c->shadow_blur = openshot::Keyframe(0.0);
            c->shadow_distance = openshot::Keyframe(18.0);
            c->shadow_angle = openshot::Keyframe(90.0);
            tl.AddClip(c);
            tl.Open();
        });

    add("clipfx.blur", {"clipfx", "blur", "exact", "gpu-composite", "gpu-blur"}, {1, 45, 89},
        [](Scene& s) {
            auto& tl = s.makeTimeline();
            tl.AddClip(backgroundClip(s, s.media("background_960x540.png")));
            MediaSpec m; m.path = s.media("clip_a_640x360_30.mp4"); m.end = 3.0;
            m.transform = Transform{BBox{0.5f, 0.5f, 0.8f, 0.8f}};
            auto* c = mediaClip(s, m);
            c->Blur(true);
            c->blur_amount = ramp(0, 30);
            tl.AddClip(c);
            tl.Open();
        });

    add("clipfx.shadow_blur_rotated", {"clipfx", "shadow", "blur", "exact", "gpu-composite"}, {30},
        [](Scene& s) {
            auto& tl = s.makeTimeline();
            tl.AddClip(backgroundClip(s, s.media("background_960x540.png")));
            auto* c = imageClip(s, 25.f);
            c->Shadow(true);
            c->shadow_color = openshot::Color(0, 0, 0, 160);
            c->shadow_blur = openshot::Keyframe(20.0);
            c->shadow_distance = openshot::Keyframe(24.0);
            c->shadow_angle = openshot::Keyframe(60.0);
            c->Blur(true);
            c->blur_amount = openshot::Keyframe(6.0);
            tl.AddClip(c);
            tl.Open();
        });
}
