// Transitions as the service builds them: every effect from the Transition.cpp vocabulary applied to an
// outgoing + incoming pair with holds (freeze frames), the layer bump, and the shared ramp window;
// plus the two overlay-clip mechanisms.
#include "Recipes.h"

#include "Timeline.h"

using namespace golden;
using namespace golden::recipes;

namespace {

// out clip: clip_a 0–2 s; in clip: clip_b 1.5–3.5 s. After holds (1 s transition): out grows to 2.5 s,
// in moves to 1.0 s. Shared ramp window = frames 31..75.
struct Pair { openshot::Clip* out; openshot::Clip* in; };

Pair pairScene(Scene& s) {
    auto& tl = s.makeTimeline();
    tl.AddClip(backgroundClip(s, s.media("background_960x540.png")));
    MediaSpec a; a.path = s.media("clip_a_640x360_30.mp4"); a.start = 0.0; a.end = 2.0; a.track = 1; a.priority = 0;
    a.transform = Transform{BBox{0.5f, 0.5f, 0.9f, 0.9f}};
    MediaSpec b; b.path = s.media("clip_b_854x480_24.mp4"); b.start = 1.5; b.end = 3.5; b.track = 1; b.priority = 1;
    b.transform = Transform{BBox{0.5f, 0.5f, 0.9f, 0.9f}};
    Pair p{mediaClip(s, a), mediaClip(s, b)};
    return p;
}

void finish(Scene& s, const Pair& p) {
    s.timeline->AddClip(p.out);
    s.timeline->AddClip(p.in);
    s.timeline->Open();
}

const std::vector<int64_t> kRamp = {31, 42, 53, 64, 75};

} // namespace

void golden::registerTransitionScenarios() {
    const TransitionEffect all[] = {
        TransitionEffect::Alpha, TransitionEffect::Blur, TransitionEffect::DiagonalBlur, TransitionEffect::RotationalBlur,
        TransitionEffect::ZoomBlur, TransitionEffect::Zoom, TransitionEffect::BorderReflectedMove,
        TransitionEffect::BorderReflectedRotation, TransitionEffect::Bars, TransitionEffect::ThresholdWipeMask,
        TransitionEffect::SplitShift, TransitionEffect::CircleMask, TransitionEffect::Exposure,
        TransitionEffect::Brightness, TransitionEffect::ColorShift};

    for (auto e : all) {
        // Every one of these is bit-identical across the four-way sweep today (verified 2026-09-16),
        // so they are gated exact: the compositor must not move a transition pixel.
        std::vector<std::string> tags = {"transitions", name(e), "exact", "gpu-composite"};
        // The GPU circle's edge is analytic, not OpenCV's (owner, 2026-09-24): see GpuEdge.
        if (e == TransitionEffect::CircleMask) tags.push_back("gpu-edge");
        add(std::string("transitions.") + name(e), tags, kRamp, [e](Scene& s) {
            Pair p = pairScene(s);
            applyOverlappingTransition(*p.out, *p.in, 1.0, s.fps.ToDouble(), {e});
            finish(s, p);
        });
    }

    add("transitions.holds_freeze_layers", {"transitions", "freeze", "exact", "gpu-composite"}, {30, 31, 45, 60, 61, 75, 76},
        [](Scene& s) {
            Pair p = pairScene(s);
            applyOverlappingTransition(*p.out, *p.in, 1.0, s.fps.ToDouble(), {});
            finish(s, p);
        });

    add("transitions.overlay_additive_blend", {"transitions", "overlay", "exact", "gpu-composite"}, kRamp, [](Scene& s) {
        Pair p = pairScene(s);
        applyOverlappingTransition(*p.out, *p.in, 1.0, s.fps.ToDouble(), {});
        auto* overlay = addOverlayClip(s, s.media("overlay_gradients_640x360_30.mp4"), 1.0,
                                       openshot::Clip::ADDITIVE_BLEND, p.out, p.in);
        (void)overlay;
        finish(s, p);
    });

    add("transitions.overlay_displacement_map", {"transitions", "overlay", "exact", "gpu-composite"}, kRamp, [](Scene& s) {
        Pair p = pairScene(s);
        applyOverlappingTransition(*p.out, *p.in, 1.0, s.fps.ToDouble(), {});
        addOverlayClip(s, s.media("overlay_gradients_640x360_30.mp4"), 1.0,
                       openshot::Clip::DISPLACEMENT_MAP, p.out, p.in, 0.15, 0.1);
        finish(s, p);
    });

    // The service's overlays (light_leaks.mp4, glitch_map.mp4) are rarely the clip's size, and the
    // C++ resizes them with cv::resize(INTER_LINEAR) first. On the GPU that is the planner's
    // overlay pass (resample_linear) since 2026-09-24; before, a mismatched overlay put the whole
    // transition on the CPU. A 1280x720 25 fps overlay over 640x360 clips.
    add("transitions.overlay_additive_blend_scaled", {"transitions", "overlay", "exact", "gpu-composite"}, kRamp,
        [](Scene& s) {
            Pair p = pairScene(s);
            applyOverlappingTransition(*p.out, *p.in, 1.0, s.fps.ToDouble(), {});
            addOverlayClip(s, s.media("clip_c_1280x720_25.mp4"), 1.0, openshot::Clip::ADDITIVE_BLEND, p.out, p.in);
            finish(s, p);
        });

    add("transitions.overlay_displacement_map_scaled", {"transitions", "overlay", "exact", "gpu-composite"}, kRamp,
        [](Scene& s) {
            Pair p = pairScene(s);
            applyOverlappingTransition(*p.out, *p.in, 1.0, s.fps.ToDouble(), {});
            addOverlayClip(s, s.media("clip_c_1280x720_25.mp4"), 1.0, openshot::Clip::DISPLACEMENT_MAP,
                           p.out, p.in, 0.15, 0.1);
            finish(s, p);
        });

    add("transitions.stack_zoom_blur_alpha", {"transitions", "stack", "exact", "gpu-composite"}, kRamp, [](Scene& s) {
        Pair p = pairScene(s);
        applyOverlappingTransition(*p.out, *p.in, 1.0, s.fps.ToDouble(),
                                   {TransitionEffect::Zoom, TransitionEffect::Blur, TransitionEffect::Alpha});
        finish(s, p);
    });
}
