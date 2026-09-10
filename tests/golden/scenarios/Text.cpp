// Text clips (Skia engine): static styles, gradients, stroke, shadow, background box, blur, glow,
// 3D tilt, curved text, wrapping/alignment, animations (in/out/loop) and keyframed styles.
#include "Recipes.h"

#include "Timeline.h"

using namespace golden;
using namespace golden::recipes;
using namespace openshot::text;

namespace {

void withBackground(Scene& s) {
    auto& tl = s.makeTimeline();
    tl.AddClip(backgroundClip(s, s.media("background_960x540.png")));
}

TextSpec spec(Scene& s, const std::string& value, double size = 14.0) {
    TextSpec t;
    t.value = value;
    t.style = baseStyle(s.font("NotoSans-Regular.ttf"));
    t.transformation.size = size;
    return t;
}

void one(Scene& s, const TextSpec& t) {
    withBackground(s);
    s.timeline->AddClip(textClip(s, t, s.fps.ToDouble()));
    s.timeline->Open();
}

} // namespace

void golden::registerTextScenarios() {
    const Tolerance text = Tolerance::Loose();   // glyph anti-aliasing differs across Skia/FreeType builds
    const std::vector<int64_t> S = {15};
    const std::vector<int64_t> A = {1, 15, 30, 45, 60};

    add("text.static_solid", {"text"}, S, [](Scene& s) { one(s, spec(s, "Golden frames")); }, text);

    add("text.bold_uppercase_letterspacing", {"text"}, S, [](Scene& s) {
        auto t = spec(s, "render pipeline", 12.0);
        t.style.fontFamily = s.font("NotoSans-Bold.ttf");
        t.style.fontWeight = 700;
        t.style.textTransform = TextTransform::UPPERCASE;
        t.style.letterSpacing = 0.25;
        one(s, t);
    }, text);

    add("text.wrap_maxwidth_align_left", {"text", "layout"}, S, [](Scene& s) {
        auto t = spec(s, "The quick brown fox jumps over the lazy dog and keeps running", 8.0);
        t.style.textAlign = TextAlignment::LEFT;
        t.style.lineHeight = 1.35;
        t.transformation.maxWidth = 12.0;
        t.posX = 0.15f; t.posY = 0.5f;
        one(s, t);
    }, text);

    add("text.wrap_maxwidth_align_right", {"text", "layout"}, S, [](Scene& s) {
        auto t = spec(s, "The quick brown fox jumps over the lazy dog and keeps running", 8.0);
        t.style.textAlign = TextAlignment::RIGHT;
        t.transformation.maxWidth = 12.0;
        t.posX = 0.85f; t.posY = 0.5f;
        one(s, t);
    }, text);

    add("text.gradient_fill_stroke_shadow", {"text", "style"}, S, [](Scene& s) {
        auto t = spec(s, "THIS\nCONVERTS", 22.0);
        t.style.fontFamily = s.font("NotoSans-Bold.ttf");
        t.style.fontWeight = 700;
        t.style.color = "linear-gradient(180deg, #FAC40A 0%, #C8271B 100%)";
        t.style.strokeColor = "linear-gradient(90deg, #DAF2E1 0%, #98C7C5 50%, #72888B 100%)";
        t.style.strokeWidthRatio = 0.18;
        t.style.shadowColor = "#FDE047";
        t.style.shadowBlurRatio = 0.5;
        t.style.shadowDistanceRatio = 0.2;
        t.style.shadowAngle = 40.0;
        t.style.lineHeight = 0.9;
        one(s, t);
    }, text);

    add("text.background_box", {"text", "style"}, S, [](Scene& s) {
        auto t = spec(s, "caption box", 12.0);
        t.style.backgroundColor = "#2050FFCC";
        t.style.backgroundPaddingXRatio = 0.4;
        t.style.backgroundPaddingYRatio = 0.25;
        t.style.backgroundRadiusRatio = 0.3;
        one(s, t);
    }, text);

    add("text.blur", {"text", "style"}, S, [](Scene& s) {
        auto t = spec(s, "soft focus", 18.0);
        t.style.blurRatio = 0.08;
        one(s, t);
    }, text);

    add("text.glow", {"text", "glow"}, S, [](Scene& s) {
        auto t = spec(s, "GLOW", 26.0);
        t.style.fontFamily = s.font("NotoSans-Bold.ttf");
        t.style.fontWeight = 700;
        t.style.color = "#FFFFFF";
        t.style.glowColor = "#40C0FF";
        t.style.glowIntensityRatio = 0.9;
        t.style.glowRangeRatio = 0.6;
        t.style.glowDirectionX = 10;
        t.style.glowDirectionY = -15;
        one(s, t);
    }, text);

    add("text.tilt_3d", {"text", "tilt"}, S, [](Scene& s) {
        auto t = spec(s, "TILTED", 24.0);
        t.style.fontFamily = s.font("NotoSans-Bold.ttf");
        t.style.fontWeight = 700;
        t.style.shadowColor = "#000000";
        t.style.shadowBlurRatio = 0.3;
        t.style.shadowDistanceRatio = 0.15;
        t.transformation.tiltX = 35;
        t.transformation.tiltY = 40;
        one(s, t);
    }, text);

    add("text.curved", {"text", "curved"}, S, [](Scene& s) {
        auto t = spec(s, "curved along an arc", 12.0);
        t.style.curveAngle = 120.0;
        t.style.strokeColor = "#000000";
        t.style.strokeWidthRatio = 0.08;
        one(s, t);
    }, text);

    add("text.rotated_clip_positioned", {"text", "transform"}, S, [](Scene& s) {
        auto t = spec(s, "corner label", 10.0);
        t.posX = 0.25f; t.posY = 0.3f; t.rotation = -20.f;
        one(s, t);
    }, text);

    add("text.anim_in_rise_chars", {"text", "animation"}, A, [](Scene& s) {
        auto t = spec(s, "Rise and shine", 16.0);
        TextAnimations a; a.inAnimationId = "rise-chars"; a.inAnimationDuration = 1.5;
        t.animations = a;
        one(s, t);
    }, text);

    add("text.anim_out_drop_words", {"text", "animation"}, {30, 60, 70, 80, 89}, [](Scene& s) {
        auto t = spec(s, "Words drop away", 16.0);
        TextAnimations a; a.outAnimationId = "drop-words"; a.outAnimationDuration = 1.0;
        t.animations = a;
        one(s, t);
    }, text);

    add("text.anim_loop_pulse_with_glow", {"text", "animation", "glow"}, A, [](Scene& s) {
        auto t = spec(s, "PULSE", 24.0);
        t.style.fontFamily = s.font("NotoSans-Bold.ttf");
        t.style.fontWeight = 700;
        t.style.glowColor = "#FF8040";
        t.style.glowIntensityRatio = 0.8;
        t.style.glowRangeRatio = 0.5;
        TextAnimations a; a.loopAnimationId = "pulse"; a.loopAnimationDuration = 1.0;
        t.animations = a;
        one(s, t);
    }, text);

    add("text.anim_in_out_fade_tilt", {"text", "animation", "tilt"}, A, [](Scene& s) {
        auto t = spec(s, "Fade & tilt", 20.0);
        t.transformation.tiltX = 20; t.transformation.tiltY = 25;
        TextAnimations a;
        a.inAnimationId = "fade-in"; a.inAnimationDuration = 1.0;
        a.outAnimationId = "fade-in"; a.outAnimationDuration = 1.0;   // reused preset, played out
        t.animations = a;
        one(s, t);
    }, text);

    add("text.style_keyframes_glow_tilt_color", {"text", "style-keyframes", "glow"}, A, [](Scene& s) {
        auto t = spec(s, "KEYFRAMED", 22.0);
        t.style.fontFamily = s.font("NotoSans-Bold.ttf");
        t.style.fontWeight = 700;
        t.style.glowColor = "#40FF80";
        t.style.glowIntensityRatio = 0.0;
        t.style.glowRangeRatio = 0.5;
        TextStyleKeyframes kf;
        openshot::Keyframe tilt; tilt.AddPoint(1, 0, openshot::LINEAR); tilt.AddPoint(90, 50, openshot::LINEAR);
        kf.tiltX = tilt;
        openshot::Keyframe glow; glow.AddPoint(1, 0, openshot::LINEAR); glow.AddPoint(60, 1, openshot::LINEAR);
        kf.glowIntensityRatio = glow;
        openshot::Keyframe blur; blur.AddPoint(1, 0.1, openshot::LINEAR); blur.AddPoint(90, 0, openshot::LINEAR);
        kf.blurRatio = blur;
        kf.color.points = {{0.0, "#FFFFFF", openshot::LINEAR}, {3.0, "#FF4040", openshot::LINEAR}};
        t.styleKeyframes = kf;
        one(s, t);
    }, text);

    add("text.over_video_with_fade", {"text", "compositing"}, {1, 10, 45, 80, 89}, [](Scene& s) {
        auto& tl = s.makeTimeline();
        tl.AddClip(backgroundClip(s, s.media("background_960x540.png")));
        MediaSpec m; m.path = s.media("clip_a_640x360_30.mp4"); m.end = 3.0; m.transform = Transform{};
        tl.AddClip(mediaClip(s, m));
        auto t = spec(s, "Lower third", 12.0);
        t.style.backgroundColor = "#000000AA";
        t.style.backgroundPaddingXRatio = 0.5;
        t.style.backgroundPaddingYRatio = 0.3;
        t.posX = 0.5f; t.posY = 0.85f;
        t.fade = Fade{0.4, 0.4, kEaseInOut};
        tl.AddClip(textClip(s, t, s.fps.ToDouble()));
        tl.Open();
    }, text);
}
