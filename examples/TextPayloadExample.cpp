// Standalone render harness for tmp/payload.json: exercises the new libopenshot text features
// (CSS linear-gradient fill + static 3D tilt) end to end. Builds the payload's three TEXT clips
// on a 1280x720 timeline with a solid background and writes a PNG frame (and a short mp4).
//
// Fonts are the payload's woff files, downloaded to FONTS_DIR (override at compile time with
// -DFONTS_DIR="..."). Positioning mirrors video-rendering-service: the reader renders a centred
// frame and the Clip's GRAVITY_CENTER + location curve places it (location = position - 0.5 for
// CENTER-aligned text).

#include <iostream>
#include <string>

#include "Timeline.h"
#include "Clip.h"
#include "Color.h"
#include "Frame.h"
#include "KeyFrame.h"
#include "FFmpegWriter.h"
#include "text/TextClipReader.h"
#include "text/TextClipTypes.h"
#include "text/TextStyleKeyframes.h"

#ifndef FONTS_DIR
#define FONTS_DIR "/tmp/claude-1000/-home-seno-Desktop-projects-libopenshot/2cc099d5-1e7f-4060-8363-3a92bd367d5b/scratchpad/fonts/"
#endif

using namespace openshot;

namespace {

// Build a positioned text Clip. Mirrors video-rendering-service addTextClip: the reader is placed
// at canvas centre (position/rotation zeroed) while keeping size + tiltX/tiltY; the Clip carries
// the on-canvas position via its location curve.
Clip* makeTextClip(int canvasW, int canvasH,
                   const std::string& fontPath,
                   const std::string& value,
                   text::TextClipStyle style,
                   text::TextTransformation t,
                   double posX, double posY) {
    style.fontFamily = fontPath;

    text::TextClipData data;
    data.value = value;
    data.style = style;
    data.transformation = t;
    data.transformation.positionX = 0.0;
    data.transformation.positionY = 0.0;
    data.transformation.rotation  = 0.0;   // 2D rotation would move to the Clip; tiltX/Y stay.

    auto* reader = new TextClipReader(canvasW, canvasH, data);
    auto* clip = new Clip(reader);
    clip->Position(0.0);
    clip->Start(0.0);
    clip->End(5.0);
    clip->gravity = GRAVITY_CENTER;
    clip->scale   = SCALE_NONE;
    clip->location_x = Keyframe(posX - 0.5);   // CENTER alignment → no bbox-anchor shift
    clip->location_y = Keyframe(posY - 0.5);
    return clip;
}

// ── Payload 2 (gradient fill + gradient STROKE + shadow + 3D tilt) ──────────
void buildPayload2(Timeline& timeline, int W, int H, const std::string& fontsDir) {
    // Clip A: gold→red gradient fill, 3-stop horizontal gradient stroke, yellow drop shadow,
    // static 3D tilt.
    {
        text::TextClipStyle s;
        s.textAlign = text::TextAlignment::CENTER;
        s.textTransform = text::TextTransform::NONE;
        s.color = "linear-gradient(180deg, #FAC40A 0%, #C8271B 100%)";
        s.strokeColor = "linear-gradient(90deg, #DAF2E1 0%, #98C7C5 50%, #72888B 100%)";
        s.strokeWidthRatio = 0.21;
        s.shadowColor = "#FDE047";
        s.shadowBlurRatio = 0.56;
        s.shadowDistanceRatio = 0.2;
        s.shadowAngle = 40.0;
        s.lineHeight = 0.86;
        s.letterSpacing = -0.03;
        s.fontWeight = 900;

        text::TextTransformation t;
        t.size = 25.72415852566017;
        t.maxWidth = 5.86025;
        t.tiltX = 38;
        t.tiltY = 50;

        timeline.AddClip(makeTextClip(W, H,fontsDir + "poppins-900.woff",
                                      "THIS\nCONVERTS", s, t,
                                      0.46458333333333335, 0.3824074074074074));
    }

    // Clip B: solid grey label.
    {
        text::TextClipStyle s;
        s.textAlign = text::TextAlignment::CENTER;
        s.textTransform = text::TextTransform::UPPERCASE;
        s.color = "#444444";
        s.lineHeight = 1.2;
        s.letterSpacing = 0.42;
        s.fontWeight = 400;

        text::TextTransformation t;
        t.size = 2.41980670942495;
        t.maxWidth = 12.036;

        timeline.AddClip(makeTextClip(W, H,fontsDir + "roboto-400.woff",
                                      "B2B GROWTH", s, t,
                                      0.44531249999999994, 0.8105186616865886));
    }
}

// ── Payload 3 (payload 2 + a text blurRatio on the tilted clip) ─────────────
void buildPayload3(Timeline& timeline, int W, int H, const std::string& fontsDir) {
    {
        text::TextClipStyle s;
        s.textAlign = text::TextAlignment::CENTER;
        s.textTransform = text::TextTransform::NONE;
        s.color = "linear-gradient(180deg, #FAC40A 0%, #C8271B 100%)";
        s.strokeColor = "linear-gradient(90deg, #DAF2E1 0%, #98C7C5 50%, #72888B 100%)";
        s.strokeWidthRatio = 0.21;
        s.shadowColor = "#FDE047";
        s.shadowBlurRatio = 0.56;
        s.shadowDistanceRatio = 0.2;
        s.shadowAngle = 40.0;
        s.blurRatio = 0.06614999999999999;   // NEW: gaussian text blur on the tilted clip
        s.lineHeight = 0.86;
        s.letterSpacing = -0.03;
        s.fontWeight = 900;

        text::TextTransformation t;
        t.size = 25.72415852566017;
        t.maxWidth = 5.86025;
        t.tiltX = 38;
        t.tiltY = 50;

        timeline.AddClip(makeTextClip(W, H,fontsDir + "poppins-900.woff",
                                      "THIS\nCONVERTS", s, t,
                                      0.5010416666666667, 0.262037037037037));
    }
    {
        text::TextClipStyle s;
        s.textAlign = text::TextAlignment::CENTER;
        s.textTransform = text::TextTransform::UPPERCASE;
        s.color = "#444444";
        s.lineHeight = 1.2;
        s.letterSpacing = 0.42;
        s.fontWeight = 400;

        text::TextTransformation t;
        t.size = 2.41980670942495;
        t.maxWidth = 12.036;

        timeline.AddClip(makeTextClip(W, H,fontsDir + "roboto-400.woff",
                                      "B2B GROWTH", s, t,
                                      0.44531249999999994, 0.8105186616865886));
    }
}

// ── Payload 4 (payload 3 flat: gradient fill + stroke + shadow + blur, NO tilt) ─
void buildPayload4(Timeline& timeline, int W, int H, const std::string& fontsDir) {
    {
        text::TextClipStyle s;
        s.textAlign = text::TextAlignment::CENTER;
        s.textTransform = text::TextTransform::NONE;
        s.color = "linear-gradient(180deg, #FAC40A 0%, #C8271B 100%)";
        s.strokeColor = "linear-gradient(90deg, #DAF2E1 0%, #98C7C5 50%, #72888B 100%)";
        s.strokeWidthRatio = 0.21;
        s.shadowColor = "#FDE047";
        s.shadowBlurRatio = 0.56;
        s.shadowDistanceRatio = 0.2;
        s.shadowAngle = 40.0;
        s.blurRatio = 0.06614999999999999;
        s.lineHeight = 0.86;
        s.letterSpacing = -0.03;
        s.fontWeight = 900;

        text::TextTransformation t;
        t.size = 25.72415852566017;
        t.maxWidth = 5.86025;
        t.tiltX = 0;
        t.tiltY = 0;

        timeline.AddClip(makeTextClip(W, H,fontsDir + "poppins-900.woff",
                                      "THIS\nCONVERTS", s, t,
                                      0.5010416666666667, 0.262037037037037));
    }
    {
        text::TextClipStyle s;
        s.textAlign = text::TextAlignment::CENTER;
        s.textTransform = text::TextTransform::UPPERCASE;
        s.color = "#444444";
        s.lineHeight = 1.2;
        s.letterSpacing = 0.42;
        s.fontWeight = 400;

        text::TextTransformation t;
        t.size = 2.41980670942495;
        t.maxWidth = 12.036;

        timeline.AddClip(makeTextClip(W, H,fontsDir + "roboto-400.woff",
                                      "B2B GROWTH", s, t,
                                      0.44531249999999994, 0.8105186616865886));
    }
}

// ── Payload 5 (real video-rendering-service export: portrait, two TEXT tracks) ──
// From an exportId a2ba2448 project (settings 2160x3840). Reproduces addTextClip preprocessing:
// project_width = settings.width (the portrait W passed in), size/maxWidth passed through, position
// carried by the Clip (GRAVITY_CENTER, location = pos-0.5 for CENTER text), fontId -> local woff.
// The headline's in/out char animations (Zoom Shake / Fly Away) are OMITTED: we render the resting
// frame, and wrapping/layout (the thing under test across resolutions) is animation-independent.
void buildPayload5(Timeline& timeline, int W, int H, const std::string& /*fontsDir*/) {
    const std::string kRoboto =
        "/tmp/claude-1000/-home-seno-Desktop-projects-libopenshot/2eab89e1-cae0-4bfa-825b-15d9e5e1ac19/scratchpad/fonts/roboto-400.woff";
    const std::string kPlayfair =
        "/tmp/claude-1000/-home-seno-Desktop-projects-libopenshot/2eab89e1-cae0-4bfa-825b-15d9e5e1ac19/scratchpad/fonts/playfair-400-italic.woff";

    // Track zindex 1 (below): big italic Playfair headline with 3D tilt.
    {
        text::TextClipStyle s;
        s.italic = true;
        s.textAlign = text::TextAlignment::CENTER;
        s.textTransform = text::TextTransform::NONE;
        s.color = "#f0ead8";
        s.lineHeight = 0.8;
        s.letterSpacing = -0.02;
        s.fontWeight = 900;

        text::TextTransformation t;
        t.size = 26.285062962373054;
        t.maxWidth = 6.940993244171142;
        t.tiltX = -40;
        t.tiltY = 18;

        Clip* c = makeTextClip(W, H,kPlayfair, "TO DO\nGREAT WORK", s, t,
                               0.4027777777777778, 0.0765625);
        c->Layer(1);
        timeline.AddClip(c);
    }

    // Track zindex 2 (on top): small uppercase Roboto label with a faint background.
    {
        text::TextClipStyle s;
        s.textAlign = text::TextAlignment::CENTER;
        s.textTransform = text::TextTransform::UPPERCASE;
        s.color = "#4a4038";
        s.lineHeight = 1.2;
        s.letterSpacing = 0.5;
        s.fontWeight = 400;
        s.backgroundColor = "rgba(0, 0, 0, 0.01)";
        s.backgroundRadiusRatio = 0.3;
        s.backgroundPaddingXRatio = 0.1;
        s.backgroundPaddingYRatio = 1.0;

        text::TextTransformation t;
        t.size = 2.7574316377769614;
        t.maxWidth = 26.97;

        Clip* c = makeTextClip(W, H,kRoboto, "IS TO LOVE WHAT YOU DO", s, t,
                               0.4796296296296295, 0.2948591540510747);
        c->Layer(2);
        timeline.AddClip(c);
    }
}

// ── Payload 6 (volumetric glow, flat/no-tilt — exercises the glow silhouette path) ──
// Centred light (directionX/Y = 0) so the god-rays radiate symmetrically: any asymmetry in the
// rendered glow would reveal a light-position / rectPad bug in the silhouette rewrite. Rendered
// at 4k too, to confirm large text keeps its full beam extent (downscale, not truncation).
void buildPayload6(Timeline& timeline, int W, int H, const std::string& /*fontsDir*/) {
    const std::string kFont = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf";
    text::TextClipStyle s;
    s.textAlign = text::TextAlignment::CENTER;
    s.textTransform = text::TextTransform::UPPERCASE;
    s.color = "#FFFFFF";
    s.glowColor = "#37F0C8";
    s.glowIntensityRatio = 0.75;
    s.glowRangeRatio = 0.6;
    s.glowDirectionX = 0.0;
    s.glowDirectionY = 0.0;
    s.lineHeight = 1.0;
    s.letterSpacing = 0.0;
    s.fontWeight = 700;

    text::TextTransformation t;
    t.size = 10.0;
    t.maxWidth = 12.0;

    timeline.AddClip(makeTextClip(W, H, kFont, "GLOW", s, t, 0.5, 0.5));
}

// ── Payload 7 (keyframed STYLE + TILT — exercises the SetStyleKeyframes overlay) ──
// A 2s clip that keyframes, over its duration: tiltY 0→50°, glowRangeRatio 0.2→0.9,
// glowDirectionX −40→40, blurRatio 0→0.3, and the FILL colour solid-white → red→blue gradient
// (exercises the §4 union-of-stops colour path). Rendered as multiple frames (see main) so the
// interpolation is visible; the worst-case frame buffer must hold every frame without clipping.
void buildPayload7(Timeline& timeline, int W, int H, const std::string& /*fontsDir*/) {
    const std::string kFont = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf";
    const double fps = 30.0;
    const int64_t frames = 60;   // 2s @ 30fps

    text::TextClipStyle s;
    s.fontFamily = kFont;
    s.textAlign = text::TextAlignment::CENTER;
    s.textTransform = text::TextTransform::UPPERCASE;
    s.color = "#FFFFFF";
    s.glowColor = "#37F0C8";      // enables glow; range/direction keyframed below
    s.glowIntensityRatio = 0.85;
    s.glowRangeRatio = 0.2;
    s.glowDirectionX = -40.0;
    s.glowDirectionY = 0.0;
    s.blurRatio = 0.0;            // enables blur; keyframed below
    s.lineHeight = 1.0;
    s.fontWeight = 700;

    text::TextClipData data;
    data.value = "KEYFRAME";
    data.style = s;
    data.transformation.size = 8.0;
    data.transformation.maxWidth = 18.0;

    auto* reader = new TextClipReader(W, H, data);

    text::TextStyleKeyframes kf;
    { openshot::Keyframe k; k.AddPoint(1, 0.0,   LINEAR); k.AddPoint(frames, 50.0, LINEAR); kf.tiltY = k; }
    { openshot::Keyframe k; k.AddPoint(1, 0.2,   LINEAR); k.AddPoint(frames, 0.9,  LINEAR); kf.glowRangeRatio = k; }
    { openshot::Keyframe k; k.AddPoint(1, -40.0, LINEAR); k.AddPoint(frames, 40.0, LINEAR); kf.glowDirectionX = k; }
    { openshot::Keyframe k; k.AddPoint(1, 0.0,   LINEAR); k.AddPoint(frames, 0.3,  LINEAR); kf.blurRatio = k; }
    {
        text::ColorKeyPoint p0; p0.timeSec = 0.0; p0.interp = LINEAR; p0.value = "#FFFFFF";
        text::ColorKeyPoint p1; p1.timeSec = 2.0; p1.interp = LINEAR;
        p1.value = "linear-gradient(90deg, #FF3B30 0%, #0A84FF 100%)";
        kf.color.points = {p0, p1};
    }
    reader->SetStyleKeyframes(kf, fps);

    auto* clip = new Clip(reader);
    clip->Position(0.0);
    clip->Start(0.0);
    clip->End(2.0);
    clip->gravity = GRAVITY_CENTER;
    clip->scale   = SCALE_NONE;
    clip->location_x = Keyframe(0.0);   // centred
    clip->location_y = Keyframe(0.0);
    timeline.AddClip(clip);
}

// ── Payload 8 (isolated FILL colour keyframe: solid → gradient, no glow/blur/tilt) ──
// Large legible text so the §4 colour interpolation is directly visible: fill goes white →
// (red→blue linear-gradient) over 2s. No other effects, so nothing obscures the fill.
void buildPayload8(Timeline& timeline, int W, int H, const std::string& /*fontsDir*/) {
    const std::string kFont = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf";
    const double fps = 30.0;

    text::TextClipStyle s;
    s.fontFamily = kFont;
    s.textAlign = text::TextAlignment::CENTER;
    s.textTransform = text::TextTransform::UPPERCASE;
    s.color = "#FFFFFF";
    s.lineHeight = 1.0;
    s.fontWeight = 700;

    text::TextClipData data;
    data.value = "COLOR";
    data.style = s;
    data.transformation.size = 14.0;
    data.transformation.maxWidth = 18.0;

    auto* reader = new TextClipReader(W, H, data);

    text::TextStyleKeyframes kf;
    text::ColorKeyPoint p0; p0.timeSec = 0.0; p0.interp = LINEAR; p0.value = "#FFFFFF";
    text::ColorKeyPoint p1; p1.timeSec = 2.0; p1.interp = LINEAR;
    p1.value = "linear-gradient(90deg, #FF3B30 0%, #0A84FF 100%)";
    kf.color.points = {p0, p1};
    reader->SetStyleKeyframes(kf, fps);

    auto* clip = new Clip(reader);
    clip->Position(0.0);
    clip->Start(0.0);
    clip->End(2.0);
    clip->gravity = GRAVITY_CENTER;
    clip->scale   = SCALE_NONE;
    clip->location_x = Keyframe(0.0);
    clip->location_y = Keyframe(0.0);
    timeline.AddClip(clip);
}

// ── Payload 9 (the user's provided payload: full style + shadow/glow/tilt keyframes + Tumble Out) ──
Clip* buildPayload9(int W, int H, const std::string& fontsDir) {
    const std::string kFont = fontsDir + "irish_grover.woff";
    const double fps = 30.0;

    text::TextClipStyle s;
    s.fontFamily = kFont;
    s.textAlign = text::TextAlignment::CENTER;
    s.color = "linear-gradient(270deg, #FFC6EB 0%, #F9FCDA 28%, #BEFF9E 50.2%, #70D4FF 75.1%, #5479FF 100%)";
    s.lineHeight = 0.86;
    s.letterSpacing = -0.03;
    s.fontWeight = 900;
    s.strokeColor = "linear-gradient(135deg, #E1DCAE 0%, #58A8C3 44.3%, #F48E80 100%)";
    s.strokeWidthRatio = 0.04;
    s.shadowColor = "#EC4899";
    s.shadowBlurRatio = 0.3;
    s.shadowDistanceRatio = 0.2;
    s.shadowAngle = 40.0;
    s.backgroundColor = "rgba(234, 88, 12, 1)";
    s.backgroundRadiusRatio = 0.3;
    s.backgroundPaddingXRatio = 0.1;
    s.backgroundPaddingYRatio = 0.1;
    s.glowColor = "#FAC40A";
    s.glowIntensityRatio = 0.0;
    s.glowRangeRatio = 0.0;

    text::TextClipData data;
    data.value = "THIS\nCONVERTS";
    data.style = s;
    data.transformation.size = 39.474993956458064;
    data.transformation.maxWidth = 5.86025;

    auto* reader = new TextClipReader(W, H, data);

    text::AnimationPreset tumble;
    tumble.level = text::AnimationPresetLevel::CHAR;
    auto& ks = tumble.keyframes;
    ks.txInRotatedFrame = true;
    ks.easing = text::CubicBezier{0.22, 0.61, 0.36, 1.0};
    auto track = [](std::initializer_list<std::pair<double,double>> pts) {
        std::vector<text::PresetKeyframe> t;
        for (auto [pct, val] : pts) { text::PresetKeyframe k; k.pct = pct; k.value = val; t.push_back(k); }
        return t;
    };
    ks.tracks[(size_t)text::AnimProp::opacity] = track({{0,1},{30,1},{80,0},{100,0}});
    ks.tracks[(size_t)text::AnimProp::rotate]  = track({{0,0},{30,0},{80,360},{100,360}});
    ks.tracks[(size_t)text::AnimProp::sx]      = track({{0,1},{30,1},{80,0.3},{100,0.3}});
    ks.tracks[(size_t)text::AnimProp::sy]      = track({{0,1},{30,1},{80,0.3},{100,0.3}});
    ks.tracks[(size_t)text::AnimProp::ty]      = track({{0,0},{30,0},{80,1.666667},{100,1.666667}});

    text::TextAnimations anims;
    anims.outAnimationId = "tumble";
    anims.outAnimationDuration = 1.5;   // seconds (service converts payload's 1500 ms → 1.5 s)
    text::AnimationPresetMap presets{{"tumble", tumble}};
    reader->SetAnimations(anims, presets, fps, 5.196);

    // Style keyframes built like KeyframeApplier: frame = lround(fps*timeSec)+1, LINEAR.
    // (3390 ms → f103, 3420 ms → f104, 3480 ms → f105, 3600 ms → f109, 4770 ms → f144)
    text::TextStyleKeyframes kf;
    auto num2 = [](double v1, int f2, double v2) {
        openshot::Keyframe k; k.AddPoint(1, v1, LINEAR); k.AddPoint(f2, v2, LINEAR); return k;
    };
    kf.backgroundPaddingXRatio = num2(0.1, 103, 0.24);
    kf.backgroundPaddingYRatio = num2(0.1, 103, 0.3);
    kf.backgroundRadiusRatio   = num2(0.3, 103, 0.7);
    kf.glowIntensityRatio      = num2(0.0, 103, 0.91);
    kf.glowRangeRatio          = num2(0.0, 103, 0.9);
    kf.glowDirectionX          = num2(0.0, 103, -23.0);
    kf.glowDirectionY          = num2(0.0, 103, -24.0);
    kf.shadowAngle             = num2(40.0, 103, -136.0);
    kf.shadowBlurRatio         = num2(0.3, 103, 0.63);
    kf.shadowDistanceRatio     = num2(0.2, 103, 0.84);
    kf.strokeWidthRatio        = num2(0.04, 103, 0.35);
    kf.tiltX                   = num2(0.0, 109, 25.0);
    kf.tiltY                   = num2(0.0, 109, 60.0);
    {   // fill colour: solid-ish gradient at f1 → gradient at f109 (3600 ms)
        text::ColorKeyPoint a; a.timeSec = 0.0;   a.interp = LINEAR;
        a.value = "linear-gradient(270deg, #FFC6EB 0%, #F9FCDA 28%, #BEFF9E 50.2%, #70D4FF 75.1%, #5479FF 100%)";
        text::ColorKeyPoint b; b.timeSec = 3.6; b.interp = LINEAR;
        b.value = "linear-gradient(45deg, #F4F400 0%, #6A556E 50%, #BB0007 100%)";
        kf.color.points = {a, b};
    }
    {   // stroke colour f1 → f103
        text::ColorKeyPoint a; a.timeSec = 0.0;   a.interp = LINEAR;
        a.value = "linear-gradient(135deg, #E1DCAE 0%, #58A8C3 44.3%, #F48E80 100%)";
        text::ColorKeyPoint b; b.timeSec = 3.39; b.interp = LINEAR;
        b.value = "linear-gradient(270deg, #FFCBDD 0%, #F9CFFF 36.1%, #FFFA9C 100%)";
        kf.strokeColor.points = {a, b};
    }
    {   // shadow colour f1 → f104 (3420 ms)
        text::ColorKeyPoint a; a.timeSec = 0.0;   a.interp = LINEAR; a.value = "#EC4899";
        text::ColorKeyPoint b; b.timeSec = 3.42; b.interp = LINEAR; b.value = "#4F46E5";
        kf.shadowColor.points = {a, b};
    }
    {   // background colour: orange → orange (f105) → near-transparent (f144, 4770 ms)
        text::ColorKeyPoint a; a.timeSec = 0.0;   a.interp = LINEAR; a.value = "rgba(234, 88, 12, 1)";
        text::ColorKeyPoint b; b.timeSec = 3.48; b.interp = LINEAR; b.value = "#ea580c";
        text::ColorKeyPoint c; c.timeSec = 4.77; c.interp = LINEAR; c.value = "rgba(0, 0, 0, 0.01)";
        kf.backgroundColor.points = {a, b, c};
    }
    {   // glow colour f1 → f103
        text::ColorKeyPoint a; a.timeSec = 0.0;   a.interp = LINEAR; a.value = "#FAC40A";
        text::ColorKeyPoint b; b.timeSec = 3.39; b.interp = LINEAR; b.value = "#9333EA";
        kf.glowColor.points = {a, b};
    }
    reader->SetStyleKeyframes(kf, fps);

    auto* clip = new Clip(reader);
    clip->Position(0.0);
    clip->Start(0.0);
    clip->End(5.196);
    clip->Layer(1000);
    clip->gravity = GRAVITY_CENTER;
    clip->scale   = SCALE_NONE;
    clip->location_x = Keyframe(0.0);
    clip->location_y = Keyframe(0.0);
    return clip;
}

} // namespace

int main(int argc, char** argv) {
    constexpr int fps = 30;
    const std::string fontsDir = FONTS_DIR;
    const std::string which = argc > 1 ? argv[1] : "1";
    const std::string res   = argc > 2 ? argv[2] : "720";

    // Resolution presets. Payloads 1-4 are 16:9 landscape; payload 5 is a 9:16 portrait project
    // (settings 2160x3840), so HD/FHD/4K there mean 720x1280 / 1080x1920 / 2160x3840.
    int W = 1280, H = 720;
    const bool portrait = (which == "5" || which == "9");
    if (res == "1080" || res == "fhd" || res == "fullhd") { W = portrait ? 1080 : 1920; H = portrait ? 1920 : 1080; }
    else if (res == "4k" || res == "2160")                { W = portrait ? 2160 : 3840; H = portrait ? 3840 : 2160; }
    else                                                  { W = portrait ? 720  : 1280; H = portrait ? 1280 : 720;  }

    const std::string suffix = (which == "1") ? "" : which;
    const std::string base    = "text-payload" + suffix + "-" + res;
    const std::string pngPath = base + ".png";
    const std::string mp4Path = base + ".mp4";

    Timeline timeline(W, H, Fraction(fps, 1), 48000, 2, ChannelLayout::LAYOUT_STEREO);
    timeline.color = Color(std::string("#222326"));   // payload background (solid COLOR)
    timeline.Open();

    if (which == "9") {
        timeline.AddClip(buildPayload9(W, H, fontsDir));
    } else if (which == "8") {
        buildPayload8(timeline, W, H, fontsDir);
    } else if (which == "7") {
        buildPayload7(timeline, W, H, fontsDir);
    } else if (which == "6") {
        buildPayload6(timeline, W, H, fontsDir);
    } else if (which == "5") {
        buildPayload5(timeline, W, H, fontsDir);
    } else if (which == "4") {
        buildPayload4(timeline, W, H, fontsDir);
    } else if (which == "3") {
        buildPayload3(timeline, W, H, fontsDir);
    } else if (which == "2") {
        buildPayload2(timeline, W, H, fontsDir);
    } else {

    // ── Clip 1: gradient fill + static 3D tilt ──────────────────────────────
    {
        text::TextClipStyle s;
        s.textAlign = text::TextAlignment::CENTER;
        s.textTransform = text::TextTransform::NONE;
        s.color = "linear-gradient(180deg, #FAC40A 0%, #C8271B 100%)";
        s.lineHeight = 0.86;
        s.letterSpacing = -0.03;
        s.fontWeight = 900;

        text::TextTransformation t;
        t.size = 25.70282837762894;
        t.maxWidth = 5.86025;
        t.tiltX = 38;
        t.tiltY = 50;

        timeline.AddClip(makeTextClip(W, H,fontsDir + "poppins-900.woff",
                                      "THIS\nCONVERTS", s, t,
                                      0.4322916666666667, 0.6703703703703704));
    }

    // ── Clip 2: solid yellow rule ────────────────────────────────────────────
    {
        text::TextClipStyle s;
        s.textAlign = text::TextAlignment::CENTER;
        s.textTransform = text::TextTransform::NONE;
        s.color = "#f5d000";
        s.lineHeight = 1.2;
        s.letterSpacing = -0.04;
        s.fontWeight = 400;
        s.backgroundColor = "rgba(0, 0, 0, 0.001)";
        s.backgroundPaddingXRatio = 0.5;
        s.backgroundPaddingYRatio = 0.5;

        text::TextTransformation t;
        t.size = 3.0778597006824575;
        t.maxWidth = 6.680701754385964;

        timeline.AddClip(makeTextClip(W, H,fontsDir + "ibmplexmono-400.woff",
                                      "\xE2\x94\x81\xE2\x94\x81\xE2\x94\x81\xE2\x94\x81\xE2\x94\x81\xE2\x94\x81\xE2\x94\x81",
                                      s, t, 0.4322916666666667, 0.768320641777177));
    }

    // ── Clip 3: solid grey label ─────────────────────────────────────────────
    {
        text::TextClipStyle s;
        s.textAlign = text::TextAlignment::CENTER;
        s.textTransform = text::TextTransform::UPPERCASE;
        s.color = "#444444";
        s.lineHeight = 1.2;
        s.letterSpacing = 0.42;
        s.fontWeight = 400;

        text::TextTransformation t;
        t.size = 2.59188185320628;
        t.maxWidth = 12.036;

        timeline.AddClip(makeTextClip(W, H,fontsDir + "roboto-400.woff",
                                      "B2B GROWTH", s, t,
                                      0.4322916666666667, 0.7822089979841851));
    }

    } // end payload 1

    // Payload 9: the user's real payload. Write a full MP4 (service-style, start frame 1 per the fix)
    // plus key PNG frames, so the whole animation and the early-shadow behaviour can be inspected.
    if (which == "9") {
        const int64_t fullLast = static_cast<int64_t>(std::llround(5.196 * fps));   // ~156
        // Optional 3rd arg caps the number of frames (e.g. "30" to render just the 1st second).
        const int64_t lastFrame = argc > 3 ? std::min<int64_t>(fullLast, std::atoi(argv[3])) : fullLast;
        FFmpegWriter w(mp4Path);
        w.SetVideoOptions("libx264", W, H, Fraction(fps, 1), 24000000);
        w.PrepareStreams();
        w.SetOption(VIDEO_STREAM, "crf", "18");
        w.SetOption(VIDEO_STREAM, "preset", "medium");
        w.SetOption(VIDEO_STREAM, "g", "30");
        w.Open();
        w.WriteFrame(&timeline, 1, lastFrame);   // 1-indexed (matches the service fix)
        w.Close();
        std::cout << "Wrote " << mp4Path << " (" << W << "x" << H << ", frames 1.." << lastFrame << ")\n";
        std::vector<int> keyFrames;
        for (int fn : {1, 5, 10, 15, 20, 25, 30, 45, 103, 130})
            if (fn <= lastFrame) keyFrames.push_back(fn);
        for (int fn : keyFrames) {
            auto fr = timeline.GetFrame(fn);
            const std::string p = base + "-f" + std::to_string(fn) + ".png";
            fr->Save(p, 1.0, "PNG", 100);
            std::cout << "Wrote " << p << "\n";
        }
        timeline.Close();
        return 0;
    }

    // Keyframed payload: render several frames across the clip so the interpolation is visible.
    if (which == "7" || which == "8") {
        for (int fn : {1, 30, 60}) {
            auto fr = timeline.GetFrame(fn);
            const std::string p = base + "-f" + std::to_string(fn) + ".png";
            fr->Save(p, 1.0, "PNG", 100);
            std::cout << "Wrote " << p << " (" << fr->GetWidth() << "x" << fr->GetHeight() << ")\n";
        }
        timeline.Close();
        return 0;
    }

    // Single-frame PNG (no animation → every frame identical).
    auto frame = timeline.GetFrame(1);
    frame->Save(pngPath, 1.0, "PNG", 100);
    std::cout << "Wrote " << pngPath << " (" << frame->GetWidth() << "x" << frame->GetHeight() << ")\n";

    // Short mp4 (video-only) proving the full encode pipeline.
    FFmpegWriter w(mp4Path);
    w.SetAudioOptions(false, "aac", 48000, 2, ChannelLayout::LAYOUT_STEREO, 128000);
    w.SetVideoOptions(true, "libx264", Fraction(fps, 1), W, H, Fraction(1, 1), false, false, 4000000);
    w.PrepareStreams();
    w.SetOption(VIDEO_STREAM, "crf", "18");
    w.SetOption(VIDEO_STREAM, "preset", "fast");
    w.Open();
    w.WriteFrame(&timeline, 1, 1);   // single static frame
    w.Close();
    std::cout << "Wrote " << mp4Path << "\n";

    timeline.Close();
    return 0;
}
