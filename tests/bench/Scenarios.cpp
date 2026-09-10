#include "Scenarios.h"

#include "Recipes.h"

#include "Timeline.h"
#include "effects/Blur.h"
#include "effects/ChromaKey.h"
#include "effects/ColorAdjustment.h"
#include "effects/ColorMap.h"
#include "effects/Enhancement.h"
#include "effects/LightAdjustment.h"

#include <sstream>

using namespace golden;
using namespace golden::recipes;
using namespace openshot::text;

namespace bench {

namespace {

std::string gBenchMedia;
std::string bm(const std::string& f) { return gBenchMedia + f; }

constexpr double kDur = 6.0;   // every scenario is at least 6 s long

openshot::Clip* video(Scene& s, const std::string& file, const BBox& box, int track, int priority,
                      double start = 0.0, double end = kDur, float rotation = 0.f) {
    MediaSpec m; m.path = bm(file); m.start = start; m.end = end; m.track = track; m.priority = priority;
    m.transform = Transform{box, 1.f, 1.f, rotation};
    return mediaClip(s, m);
}

void background(Scene& s) {
    s.makeTimeline().AddClip(backgroundClip(s, bm("bg_3840x2160.png")));
}

TextSpec text(Scene& s, const std::string& value, double size, float px, float py, bool bold = false) {
    TextSpec t;
    t.value = value;
    t.style = baseStyle(s.font(bold ? "NotoSans-Bold.ttf" : "NotoSans-Regular.ttf"));
    if (bold) t.style.fontWeight = 700;
    t.transformation.size = size;
    t.posX = px; t.posY = py;
    t.start = 0; t.end = kDur;
    return t;
}

// Subtitles covering the whole 6 s, animated words, one-word container (the heaviest variant).
std::string longSubtitles(const std::string& font, int width) {
    const int fs = width / 16;
    std::ostringstream o;
    o << "{\"settings\":{\"defaultStyle\":{\"fontFamily\":\"" << font << "\",\"fontSize\":" << fs
      << ",\"fontWeight\":700,\"color\":\"#FFFFFF\",\"strokeColor\":\"#000000\",\"strokeWidth\":" << fs / 12
      << ",\"shadowColor\":\"#000000\",\"shadowOpacity\":0.6,\"shadowBlur\":" << fs / 8 << ",\"shadowDistance\":" << fs / 16 << "},"
      << "\"transformation\":{\"maxWidth\":" << width * 3 / 4 << ",\"center\":{\"x\":0.5,\"y\":0.85}},"
      << "\"containerStyle\":{\"appearance\":\"ONE_WORD\",\"textAlign\":\"CENTER\",\"color\":\"#FFD040\",\"opacity\":0.9,"
         "\"paddingX\":" << fs / 3 << ",\"paddingY\":" << fs / 6 << ",\"radius\":" << fs / 4 << "},"
      << "\"animationSettings\":{\"level\":\"WORD\",\"inDuration\":200,\"inInterpolation\":{\"type\":\"BEZIER\"},"
         "\"inStyles\":{\"opacity\":0,\"translateY\":" << fs / 2 << "},\"outDuration\":120,"
         "\"outInterpolation\":{\"type\":\"LINEAR\"},\"outStyles\":{\"opacity\":0.4}}},\"segments\":[";
    const char* words[] = {"Every", "frame", "should", "stay", "on", "the", "GPU", "from", "decode", "to", "encode", "today"};
    int t = 0;
    for (int seg = 0; seg < 4; ++seg) {
        o << (seg ? "," : "") << "{\"id\":\"s" << seg << "\",\"startTime\":" << t << ",\"endTime\":" << t + 1500
          << ",\"visible\":true,\"attached\":true,\"wordDetails\":[";
        for (int w = 0; w < 3; ++w) {
            o << (w ? "," : "") << "{\"word\":\"" << words[seg * 3 + w] << "\",\"startTime\":" << t + w * 500
              << ",\"endTime\":" << t + (w + 1) * 500 << "}";
        }
        o << "]}";
        t += 1500;
    }
    o << "]}";
    return o.str();
}

} // namespace

void setBenchMediaDir(const std::string& dir) { gBenchMedia = dir; }

const std::vector<BenchScenario>& scenarios() {
    static const std::vector<BenchScenario> list = {

    {"single_video", "one 1080p video scaled to the canvas over a background image",
     [](Scene& s) {
         background(s);
         s.timeline->AddClip(video(s, "v1080_a.mp4", BBox{0.5f, 0.5f, 1.f, 1.f}, 1, 0));
         s.timeline->Open();
     }},

    {"grid_2x2", "four 1080p videos in a 2x2 grid (multi-decode + composite)",
     [](Scene& s) {
         background(s);
         const char* files[] = {"v1080_a.mp4", "v1080_b.mp4", "v1080_c.mp4", "v1080_green.mp4"};
         for (int i = 0; i < 4; ++i)
             s.timeline->AddClip(video(s, files[i], BBox{0.25f + 0.5f * (i % 2), 0.25f + 0.5f * (i / 2), 0.48f, 0.48f}, 1, i));
         s.timeline->Open();
     }},

    {"grid_3x3", "nine 1080p videos in a 3x3 grid",
     [](Scene& s) {
         background(s);
         const char* files[] = {"v1080_a.mp4", "v1080_b.mp4", "v1080_c.mp4"};
         for (int i = 0; i < 9; ++i)
             s.timeline->AddClip(video(s, files[i % 3], BBox{1.f / 6 + (i % 3) / 3.f, 1.f / 6 + (i / 3) / 3.f, 0.32f, 0.32f}, 1, i));
         s.timeline->Open();
     }},

    {"podcast_pip", "podcast layout: main video with colour filters + LUT, PiP with crop radius + shadow, logo, lower third",
     [](Scene& s) {
         background(s);
         auto* main = video(s, "v1080_a.mp4", BBox{0.5f, 0.5f, 0.96f, 0.96f}, 1, 0);
         main->AddEffect(new openshot::LightAdjustment(10, 15, -10, 10, 5, -5));
         main->AddEffect(new openshot::ColorAdjustment(10, -5, 20, 15));
         main->AddEffect(new openshot::ColorMap(s.media("lut_example.cube")));
         s.timeline->AddClip(main);
         auto* pip = video(s, "v1080_b.mp4", BBox{0.8f, 0.75f, 0.3f, 0.3f}, 2, 0);
         pip->AddEffect(cropEffect(0.0, 0.0, 0.0, 0.0, 0.25));
         pip->Shadow(true); pip->shadow_color = openshot::Color(0, 0, 0, 180);
         pip->shadow_blur = openshot::Keyframe(16); pip->shadow_distance = openshot::Keyframe(12); pip->shadow_angle = openshot::Keyframe(60);
         s.timeline->AddClip(pip);
         MediaSpec logo; logo.path = bm("logo_512.png"); logo.isImage = true; logo.end = kDur; logo.track = 3;
         logo.transform = Transform{BBox{0.1f, 0.12f, 0.12f, 0.12f}}; logo.opacity = 0.9f;
         s.timeline->AddClip(mediaClip(s, logo));
         auto t = text(s, "Episode 42 · The GPU Pipeline", 8.0, 0.5f, 0.9f, true);
         t.style.backgroundColor = "#000000AA"; t.style.backgroundPaddingXRatio = 0.5; t.style.backgroundPaddingYRatio = 0.3;
         t.style.backgroundRadiusRatio = 0.3; t.track = 4;
         s.timeline->AddClip(textClip(s, t, s.fps.ToDouble()));
         s.timeline->Open();
     }},

    {"subtitles_words", "video + animated one-word-container subtitles for the whole duration",
     [](Scene& s) {
         background(s);
         s.timeline->AddClip(video(s, "v1080_a.mp4", BBox{0.5f, 0.5f, 1.f, 1.f}, 1, 0));
         s.timeline->LoadSubtitlesFromJsonString(longSubtitles(s.font("NotoSans-Bold.ttf"), s.width));
         s.timeline->Open();
     }},

    {"text_static_4", "four static styled text clips (gradient+stroke+shadow, box, curved, plain) over video",
     [](Scene& s) {
         background(s);
         s.timeline->AddClip(video(s, "v1080_c.mp4", BBox{0.5f, 0.5f, 1.f, 1.f}, 1, 0));
         auto a = text(s, "THIS\nCONVERTS", 20.0, 0.3f, 0.35f, true);
         a.style.color = "linear-gradient(180deg, #FAC40A 0%, #C8271B 100%)";
         a.style.strokeColor = "linear-gradient(90deg, #DAF2E1 0%, #98C7C5 50%, #72888B 100%)";
         a.style.strokeWidthRatio = 0.18; a.style.shadowColor = "#FDE047"; a.style.shadowBlurRatio = 0.5;
         a.style.shadowDistanceRatio = 0.2; a.style.shadowAngle = 40; a.track = 2;
         s.timeline->AddClip(textClip(s, a, s.fps.ToDouble()));
         auto b = text(s, "caption in a box", 10.0, 0.72f, 0.3f);
         b.style.backgroundColor = "#2050FFCC"; b.style.backgroundPaddingXRatio = 0.4; b.style.backgroundPaddingYRatio = 0.25;
         b.style.backgroundRadiusRatio = 0.3; b.track = 2; b.priority = 1;
         s.timeline->AddClip(textClip(s, b, s.fps.ToDouble()));
         auto c = text(s, "curved along an arc", 10.0, 0.5f, 0.7f);
         c.style.curveAngle = 120.0; c.style.strokeColor = "#000000"; c.style.strokeWidthRatio = 0.08; c.track = 2; c.priority = 2;
         s.timeline->AddClip(textClip(s, c, s.fps.ToDouble()));
         auto d = text(s, "plain white label", 7.0, 0.5f, 0.92f);
         d.track = 2; d.priority = 3;
         s.timeline->AddClip(textClip(s, d, s.fps.ToDouble()));
         s.timeline->Open();
     }},

    {"text_animated_glow_3", "three animated text clips with glow, 3D tilt and style keyframes (per-frame Skia)",
     [](Scene& s) {
         background(s);
         auto a = text(s, "Rise and shine", 16.0, 0.5f, 0.25f, true);
         a.style.glowColor = "#40C0FF"; a.style.glowIntensityRatio = 0.9; a.style.glowRangeRatio = 0.6;
         TextAnimations aa; aa.inAnimationId = "rise-chars"; aa.inAnimationDuration = 2.0; aa.outAnimationId = "drop-words"; aa.outAnimationDuration = 1.5;
         a.animations = aa; a.track = 2;
         s.timeline->AddClip(textClip(s, a, s.fps.ToDouble()));
         auto b = text(s, "PULSE", 26.0, 0.5f, 0.5f, true);
         b.style.glowColor = "#FF8040"; b.style.glowIntensityRatio = 0.8; b.style.glowRangeRatio = 0.5;
         b.transformation.tiltX = 20; b.transformation.tiltY = 25;
         TextAnimations bb; bb.loopAnimationId = "pulse"; bb.loopAnimationDuration = 1.0;
         b.animations = bb; b.track = 2; b.priority = 1;
         s.timeline->AddClip(textClip(s, b, s.fps.ToDouble()));
         auto c = text(s, "KEYFRAMED", 20.0, 0.5f, 0.78f, true);
         c.style.glowColor = "#40FF80"; c.style.glowRangeRatio = 0.5;
         TextStyleKeyframes kf;
         openshot::Keyframe tilt; tilt.AddPoint(1, 0, openshot::LINEAR); tilt.AddPoint(180, 50, openshot::LINEAR); kf.tiltX = tilt;
         openshot::Keyframe glow; glow.AddPoint(1, 0, openshot::LINEAR); glow.AddPoint(120, 1, openshot::LINEAR); kf.glowIntensityRatio = glow;
         kf.color.points = {{0.0, "#FFFFFF", openshot::LINEAR}, {6.0, "#FF4040", openshot::LINEAR}};
         c.styleKeyframes = kf; c.track = 2; c.priority = 2;
         s.timeline->AddClip(textClip(s, c, s.fps.ToDouble()));
         s.timeline->Open();
     }},

    {"transitions_chain", "three videos with two overlapping transitions (zoom+blur+alpha, circle mask + additive overlay)",
     [](Scene& s) {
         background(s);
         auto* a = video(s, "v1080_a.mp4", BBox{0.5f, 0.5f, 0.96f, 0.96f}, 1, 0, 0.0, 2.5);
         auto* b = video(s, "v1080_b.mp4", BBox{0.5f, 0.5f, 0.96f, 0.96f}, 1, 1, 2.0, 4.5);
         auto* c = video(s, "v1080_c.mp4", BBox{0.5f, 0.5f, 0.96f, 0.96f}, 1, 2, 4.0, 6.5);
         const double fps = s.fps.ToDouble();
         applyOverlappingTransition(*a, *b, 1.0, fps, {TransitionEffect::Zoom, TransitionEffect::Blur, TransitionEffect::Alpha});
         applyOverlappingTransition(*b, *c, 1.0, fps, {TransitionEffect::CircleMask});
         addOverlayClip(s, bm("v1080_overlay.mp4"), 1.0, openshot::Clip::ADDITIVE_BLEND, b, c);
         s.timeline->AddClip(a); s.timeline->AddClip(b); s.timeline->AddClip(c);
         s.timeline->Open();
     }},

    {"blend_stack_5", "base video plus four layers composited with non-normal blend modes",
     [](Scene& s) {
         background(s);
         s.timeline->AddClip(video(s, "v1080_a.mp4", BBox{0.5f, 0.5f, 1.f, 1.f}, 1, 0));
         const openshot::BlendMode modes[] = {openshot::BLEND_MULTIPLY, openshot::BLEND_SCREEN, openshot::BLEND_OVERLAY, openshot::BLEND_SOFT_LIGHT};
         const char* files[] = {"v1080_overlay.mp4", "v1080_c.mp4", "v1080_overlay.mp4", "v1080_b.mp4"};
         for (int i = 0; i < 4; ++i) {
             auto* c = video(s, files[i], BBox{0.5f, 0.5f, 0.9f - 0.1f * i, 0.9f - 0.1f * i}, 2, i, 0.0, kDur, 5.f * i);
             c->Blend(modes[i]);
             s.timeline->AddClip(c);
         }
         s.timeline->Open();
     }},

    {"heavy_effects", "one video with blur, enhancement, colour + light filters, LUT, rounded crop, clip shadow and clip blur",
     [](Scene& s) {
         background(s);
         auto* c = video(s, "v1080_a.mp4", BBox{0.5f, 0.5f, 0.9f, 0.9f}, 1, 0, 0.0, kDur, 4.f);
         c->AddEffect(cropEffect(0.02, 0.02, 0.02, 0.02, 0.2));
         c->AddEffect(new openshot::Blur(12, 12, 0, 0, 0, 0, 0, 1, 1));
         c->AddEffect(new openshot::Enhancement(0.3, 0.6, 0.4));
         c->AddEffect(new openshot::ColorAdjustment(20, -10, 30, 20));
         c->AddEffect(new openshot::LightAdjustment(10, 20, -15, 15, 5, -5));
         c->AddEffect(new openshot::ColorMap(s.media("lut_example.cube")));
         c->Shadow(true); c->shadow_color = openshot::Color(0, 0, 0, 160);
         c->shadow_blur = openshot::Keyframe(20); c->shadow_distance = openshot::Keyframe(18); c->shadow_angle = openshot::Keyframe(60);
         c->Blur(true); c->blur_amount = openshot::Keyframe(6);
         s.timeline->AddClip(c);
         s.timeline->Open();
     }},

    {"chroma_key_green", "green-screen 1080p clip keyed (YCbCr) over a second video",
     [](Scene& s) {
         background(s);
         s.timeline->AddClip(video(s, "v1080_c.mp4", BBox{0.5f, 0.5f, 1.f, 1.f}, 1, 0));
         auto* g = video(s, "v1080_green.mp4", BBox{0.5f, 0.5f, 1.f, 1.f}, 2, 0);
         g->AddEffect(new openshot::ChromaKey(openshot::Color(0, 255, 0, 0), 70, 20, openshot::CHROMAKEY_YCBCR));
         s.timeline->AddClip(g);
         s.timeline->Open();
     }},

    {"source_4k", "one 4K (3840x2160) source scaled to the canvas (decode + pre-scale bound)",
     [](Scene& s) {
         background(s);
         s.timeline->AddClip(video(s, "v2160_a.mp4", BBox{0.5f, 0.5f, 1.f, 1.f}, 1, 0));
         s.timeline->Open();
     }},

    {"everything", "two videos side by side, chroma-keyed clip with LUT, animated glow text, subtitles, PiP with shadow",
     [](Scene& s) {
         background(s);
         s.timeline->AddClip(video(s, "v1080_a.mp4", BBox{0.26f, 0.45f, 0.5f, 0.6f}, 1, 0));
         s.timeline->AddClip(video(s, "v1080_b.mp4", BBox{0.74f, 0.45f, 0.5f, 0.6f}, 1, 1));
         auto* g = video(s, "v1080_green.mp4", BBox{0.5f, 0.5f, 0.6f, 0.6f}, 2, 0);
         g->AddEffect(new openshot::ChromaKey(openshot::Color(0, 255, 0, 0), 70, 20, openshot::CHROMAKEY_YCBCR));
         g->AddEffect(new openshot::ColorMap(s.media("lut_example.cube")));
         s.timeline->AddClip(g);
         auto* pip = video(s, "v1080_c.mp4", BBox{0.85f, 0.15f, 0.22f, 0.22f}, 3, 0);
         pip->AddEffect(cropEffect(0.0, 0.0, 0.0, 0.0, 0.3));
         pip->Shadow(true); pip->shadow_blur = openshot::Keyframe(14); pip->shadow_distance = openshot::Keyframe(10);
         s.timeline->AddClip(pip);
         auto t = text(s, "Everything at once", 14.0, 0.5f, 0.12f, true);
         t.style.glowColor = "#40C0FF"; t.style.glowIntensityRatio = 0.8; t.style.glowRangeRatio = 0.5;
         TextAnimations ta; ta.inAnimationId = "rise-chars"; ta.inAnimationDuration = 1.5; ta.loopAnimationId = "pulse"; ta.loopAnimationDuration = 1.0;
         t.animations = ta; t.track = 4;
         s.timeline->AddClip(textClip(s, t, s.fps.ToDouble()));
         s.timeline->LoadSubtitlesFromJsonString(longSubtitles(s.font("NotoSans-Bold.ttf"), s.width));
         s.timeline->Open();
     }},
    };
    return list;
}

} // namespace bench
