// Per-clip effects the service attaches: crop, chroma key, colour/light/enhancement filters, LUT,
// masks (image + clip-synced video matte), camera movement, alpha.
#include "Recipes.h"

#include "Timeline.h"
#include "effects/Alpha.h"
#include "effects/Bars.h"
#include "effects/Brightness.h"
#include "effects/ChromaKey.h"
#include "effects/ColorAdjustment.h"
#include "effects/ColorMap.h"
#include "effects/ColorShift.h"
#include "effects/Enhancement.h"
#include "effects/Exposure.h"
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

// A 1:1 clip with UNIFORM partial alpha, for the four effects below.
//
// Three properties, each of which one earlier attempt at this scenario got wrong.
//
// Partial alpha is the point: every CPU effect unpremultiplies, operates and re-premultiplies,
// and that round trip is where a fragment and its C++ twin can disagree (GPU-DECISIONS.md, W19).
// Opaque pixels exercise none of it, which is why transitions.{brightness,exposure,colorshift,bars}
// -- which drive all four over opaque video -- do not cover this.
//
// The clip is 640x360, exactly the golden timeline size, with no transform, so it lands 1:1 and
// the GPU composite is a translate-only draw that can be bit-exact. baseScene's 0.9x box would
// make Skia's bilinear resample where QPainter uses its smooth transform, forcing the whole
// scenario into the wide gpu-composite band and hiding the small differences it exists to catch.
//
// And the alpha comes from an Alpha effect rather than from a source PNG's alpha channel, so it
// is uniform. An earlier version used image_alpha_320x200.png, whose alphas are 0/217/255: scaled
// to fit, its alpha *boundaries* interpolate, and those boundary pixels turned out to depend on
// what else had run in the same process -- 1280 of them moved between an isolated run and a full
// suite run, and between two 4-thread full runs. Only brightness and colorshift showed it,
// because the unpremultiply amplifies a 1 LSB alpha difference at low alpha and the other two
// effects do not. That instability is real and pre-existing; it is written up in STATUS.md rather
// than papered over with a tolerance here, and these scenarios simply do not depend on it.
openshot::Clip* uniformAlphaScene(Scene& s) {
    auto& tl = s.makeTimeline();
    tl.AddClip(backgroundClip(s, s.media("background_960x540.png")));
    MediaSpec m; m.path = s.media("clip_a_640x360_30.mp4"); m.end = 3.0;
    auto* c = mediaClip(s, m);
    tl.AddClip(c);
    return c;
}

} // namespace

void golden::registerEffectScenarios() {
    const std::vector<int64_t> F = {1, 45, 89};

    add("effects.crop_radius", {"effects", "crop", "exact", "gpu-composite"}, F, [](Scene& s) {
        auto* c = baseScene(s);
        c->AddEffect(cropEffect(ramp(0, 0.25), openshot::Keyframe(0.1), openshot::Keyframe(0.1), ramp(0, 0.25), ramp(0.05, 0.45)));
        s.timeline->Open();
    });

    add("effects.chromakey_ycbcr", {"effects", "chromakey", "exact", "gpu-composite"}, F, [](Scene& s) {
        auto* c = baseScene(s, "clip_green_640x360_30.mp4");
        c->AddEffect(new openshot::ChromaKey(openshot::Color(0, 255, 0, 0), 70, 20, openshot::CHROMAKEY_YCBCR));
        s.timeline->Open();
    });

    add("effects.color_adjustment", {"effects", "filters", "exact", "gpu-composite"}, F, [](Scene& s) {
        auto* c = baseScene(s);
        c->AddEffect(new openshot::ColorAdjustment(ramp(-60, 60), openshot::Keyframe(20), ramp(0, 70), openshot::Keyframe(40)));
        s.timeline->Open();
    });

    add("effects.light_adjustment", {"effects", "filters", "exact"}, F, [](Scene& s) {
        auto* c = baseScene(s);
        c->AddEffect(new openshot::LightAdjustment(ramp(-30, 30), openshot::Keyframe(25), openshot::Keyframe(-30),
                                                   openshot::Keyframe(30), ramp(0, 40), openshot::Keyframe(-20)));
        s.timeline->Open();
    });

    // With grain. On a GPU the grain is the same noise at a different random phase (float hash,
    // owner decision 2026-09-24), so this frame cannot match its CPU golden there and the GPU arms
    // hold it only to Tolerance::GpuGrain(), which is not a gate. unit.gpu_grain is the gate, and
    // effects.enhancement_no_grain below holds the GPU's clarity and sharpness to GpuClose.
    add("effects.enhancement", {"effects", "filters", "exact", "gpu-composite", "gpu-grain"}, F, [](Scene& s) {
        auto* c = baseScene(s);
        c->AddEffect(new openshot::Enhancement(openshot::Keyframe(0.3), ramp(0, 0.9), ramp(-0.5, 0.8)));
        s.timeline->Open();
    }, Tolerance::Loose());

    add("effects.enhancement_no_grain", {"effects", "filters", "exact", "gpu-composite"}, F, [](Scene& s) {
        auto* c = baseScene(s);
        c->AddEffect(new openshot::Enhancement(openshot::Keyframe(0.0), ramp(0, 0.9), ramp(-0.5, 0.8)));
        s.timeline->Open();
    });

    add("effects.colormap_lut", {"effects", "lut", "exact", "gpu-composite"}, F, [](Scene& s) {
        auto* c = baseScene(s);
        c->AddEffect(new openshot::ColorMap(s.media("lut_example.cube"), ramp(0, 1)));
        s.timeline->Open();
    });

    add("effects.mask_image_radial", {"effects", "mask", "exact", "gpu-composite"}, {1, 45}, [](Scene& s) {
        auto* c = baseScene(s);
        c->AddEffect(imageMask(s, s.media("matte_radial_640x360.png")));
        s.timeline->Open();
    });

    add("effects.mask_video_clip_synced_2x", {"effects", "mask", "framemapper", "exact", "gpu-composite"}, {5, 20, 35, 45}, [](Scene& s) {
        auto* c = baseScene(s, "clip_a_640x360_30.mp4", 2.0);
        c->AddEffect(videoMask(s, s.media("matte_wipe_640x360_30.mp4"), *c, s.fps.ToDouble()));
        s.timeline->Open();
    });

    add("effects.camera_movement", {"effects", "animation", "exact", "gpu-composite"}, F, [](Scene& s) {
        auto* c = baseScene(s);
        addCameraMovement(*c, 0.0, 3.0, s.fps.ToDouble(),
                          {{0.0, 100}, {1.0, 170, kEaseInOut}}, {{0.0, 0}, {1.0, 20}},
                          {{0.0, 0.5}, {1.0, 0.65}}, {{0.0, 0.5}, {1.0, 0.4}});
        s.timeline->Open();
    });

    add("effects.alpha_effect", {"effects", "exact", "gpu-composite"}, F, [](Scene& s) {
        auto* c = baseScene(s);
        c->AddEffect(new openshot::Alpha(ramp(1.0, 0.15)));
        s.timeline->Open();
    });

    // The four effects the service builds only in Transition.cpp, applied to semi-transparent
    // pixels. They are constructed exactly as addBrightnessEffect / addExposureEffect /
    // addColorShiftEffect / addBarsEffect construct them -- one keyframe for brightness, so
    // contrast keeps its 3.0 default, and zero alpha shift for ColorShift -- because measuring a
    // configuration production never builds is what tests/golden/Recipes.cpp was doing before W09.
    //
    // transitions.{brightness,exposure,colorshift,bars} already drive all four, but through a
    // scaled clip and therefore under the wide gpu-composite band, and over opaque video. These
    // hold them to the exact class on partial alpha instead.
    add("effects.brightness_alpha", {"effects", "filters", "alpha", "exact"}, F, [](Scene& s) {
        auto* c = uniformAlphaScene(s);
        c->AddEffect(new openshot::Alpha(ramp(1.0, 0.35)));
        c->AddEffect(new openshot::Brightness(ramp(-0.3, 0.4)));
        s.timeline->Open();
    });

    add("effects.exposure_alpha", {"effects", "filters", "alpha", "exact"}, F, [](Scene& s) {
        auto* c = uniformAlphaScene(s);
        c->AddEffect(new openshot::Alpha(ramp(1.0, 0.35)));
        // Exposure clamps to >= 1.0, so a ramp below it is a ramp to the clamp.
        c->AddEffect(new openshot::Exposure(ramp(1.0, 2.5)));
        s.timeline->Open();
    });

    add("effects.colorshift_alpha", {"effects", "filters", "alpha", "exact"}, F, [](Scene& s) {
        auto* c = uniformAlphaScene(s);
        c->AddEffect(new openshot::Alpha(ramp(1.0, 0.35)));
        c->AddEffect(new openshot::ColorShift(ramp(0, 0.04), ramp(0, -0.02),
                                              ramp(0, -0.03), ramp(0, 0.03),
                                              ramp(0, 0.02), ramp(0, 0.05),
                                              0, 0));
        s.timeline->Open();
    });

    add("effects.bars_alpha", {"effects", "filters", "alpha", "exact"}, F, [](Scene& s) {
        auto* c = uniformAlphaScene(s);
        c->AddEffect(new openshot::Alpha(ramp(1.0, 0.35)));
        c->AddEffect(new openshot::Bars(openshot::Color("#000000"),
                                        ramp(0, 0.2), ramp(0, 0.15),
                                        ramp(0, 0.1), ramp(0, 0.25)));
        s.timeline->Open();
    });

    add("effects.stack_crop_chroma_light_lut", {"effects", "stack", "exact", "gpu-composite"}, {45}, [](Scene& s) {
        auto* c = baseScene(s, "clip_green_640x360_30.mp4");
        c->AddEffect(cropEffect(0.05, 0.05, 0.05, 0.05, 0.2));
        c->AddEffect(new openshot::ChromaKey(openshot::Color(0, 255, 0, 0), 70, 20, openshot::CHROMAKEY_YCBCR));
        c->AddEffect(new openshot::LightAdjustment(10, 15, 0, 0, 0, 0));
        c->AddEffect(new openshot::ColorMap(s.media("lut_example.cube")));
        s.timeline->Open();
    });
}
