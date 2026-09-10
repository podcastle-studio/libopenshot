// Per-clip effects the service attaches: crop, chroma key, colour/light/enhancement filters, LUT,
// masks (image + clip-synced video matte), camera movement, alpha.
#include "Recipes.h"

#include "Timeline.h"
#include "effects/Alpha.h"
#include "effects/ChromaKey.h"
#include "effects/ColorAdjustment.h"
#include "effects/ColorMap.h"
#include "effects/Enhancement.h"
#include "effects/LightAdjustment.h"

using namespace golden;
using namespace golden::recipes;

namespace {

openshot::Keyframe ramp(double a, double b, int f0 = 1, int f1 = 90) {
    openshot::Keyframe k;
    k.AddPoint(f0, a, openshot::LINEAR);
    k.AddPoint(f1, b, openshot::LINEAR);
    return k;
}

// background + full-frame clip_a; returns the clip so the scenario attaches its effect.
openshot::Clip* baseScene(Scene& s, const std::string& file = "clip_a_640x360_30.mp4", double speed = 1.0) {
    auto& tl = s.makeTimeline();
    tl.AddClip(backgroundClip(s, s.media("background_960x540.png")));
    MediaSpec m; m.path = s.media(file); m.end = 3.0; m.speed = speed;
    m.transform = Transform{BBox{0.5f, 0.5f, 0.9f, 0.9f}};
    auto* c = mediaClip(s, m);
    tl.AddClip(c);
    return c;
}

} // namespace

void golden::registerEffectScenarios() {
    const std::vector<int64_t> F = {1, 45, 89};

    add("effects.crop_radius", {"effects", "crop"}, F, [](Scene& s) {
        auto* c = baseScene(s);
        c->AddEffect(cropEffect(ramp(0, 0.25), openshot::Keyframe(0.1), openshot::Keyframe(0.1), ramp(0, 0.25), ramp(0.05, 0.45)));
        s.timeline->Open();
    });

    add("effects.chromakey_ycbcr", {"effects", "chromakey"}, F, [](Scene& s) {
        auto* c = baseScene(s, "clip_green_640x360_30.mp4");
        c->AddEffect(new openshot::ChromaKey(openshot::Color(0, 255, 0, 0), 70, 20, openshot::CHROMAKEY_YCBCR));
        s.timeline->Open();
    });

    add("effects.color_adjustment", {"effects", "filters"}, F, [](Scene& s) {
        auto* c = baseScene(s);
        c->AddEffect(new openshot::ColorAdjustment(ramp(-60, 60), openshot::Keyframe(20), ramp(0, 70), openshot::Keyframe(40)));
        s.timeline->Open();
    });

    add("effects.light_adjustment", {"effects", "filters"}, F, [](Scene& s) {
        auto* c = baseScene(s);
        c->AddEffect(new openshot::LightAdjustment(ramp(-30, 30), openshot::Keyframe(25), openshot::Keyframe(-30),
                                                   openshot::Keyframe(30), ramp(0, 40), openshot::Keyframe(-20)));
        s.timeline->Open();
    });

    add("effects.enhancement", {"effects", "filters"}, F, [](Scene& s) {
        auto* c = baseScene(s);
        c->AddEffect(new openshot::Enhancement(openshot::Keyframe(0.3), ramp(0, 0.9), ramp(-0.5, 0.8)));
        s.timeline->Open();
    }, Tolerance::Loose());

    add("effects.colormap_lut", {"effects", "lut"}, F, [](Scene& s) {
        auto* c = baseScene(s);
        c->AddEffect(new openshot::ColorMap(s.media("lut_example.cube"), ramp(0, 1)));
        s.timeline->Open();
    });

    add("effects.mask_image_radial", {"effects", "mask"}, {1, 45}, [](Scene& s) {
        auto* c = baseScene(s);
        c->AddEffect(imageMask(s, s.media("matte_radial_640x360.png")));
        s.timeline->Open();
    });

    add("effects.mask_video_clip_synced_2x", {"effects", "mask", "framemapper"}, {5, 20, 35, 45}, [](Scene& s) {
        auto* c = baseScene(s, "clip_a_640x360_30.mp4", 2.0);
        c->AddEffect(videoMask(s, s.media("matte_wipe_640x360_30.mp4"), *c, s.fps.ToDouble()));
        s.timeline->Open();
    });

    add("effects.camera_movement", {"effects", "animation"}, F, [](Scene& s) {
        auto* c = baseScene(s);
        addCameraMovement(*c, 0.0, 3.0, s.fps.ToDouble(),
                          {{0.0, 100}, {1.0, 170, kEaseInOut}}, {{0.0, 0}, {1.0, 20}},
                          {{0.0, 0.5}, {1.0, 0.65}}, {{0.0, 0.5}, {1.0, 0.4}});
        s.timeline->Open();
    });

    add("effects.alpha_effect", {"effects"}, F, [](Scene& s) {
        auto* c = baseScene(s);
        c->AddEffect(new openshot::Alpha(ramp(1.0, 0.15)));
        s.timeline->Open();
    });

    add("effects.stack_crop_chroma_light_lut", {"effects", "stack"}, {45}, [](Scene& s) {
        auto* c = baseScene(s, "clip_green_640x360_30.mp4");
        c->AddEffect(cropEffect(0.05, 0.05, 0.05, 0.05, 0.2));
        c->AddEffect(new openshot::ChromaKey(openshot::Color(0, 255, 0, 0), 70, 20, openshot::CHROMAKEY_YCBCR));
        c->AddEffect(new openshot::LightAdjustment(10, 15, 0, 0, 0, 0));
        c->AddEffect(new openshot::ColorMap(s.media("lut_example.cube")));
        s.timeline->Open();
    });
}
