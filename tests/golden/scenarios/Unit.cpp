// Unit checks that need no rendering. They live in the golden suite because it is this
// project's test suite -- ENABLE_TESTS (Catch2) is off and Catch2 is not installed.
//
// unit.color guards openshot::Color's string parsing, which stopped going through QColor in
// W18 so that Color.h no longer drags Qt into every consumer and the Timeline's background
// clear does not parse a colour through Qt on every frame. Every expectation below was
// RECORDED FROM THE PRE-W18 IMPLEMENTATION (Qt 5.15.13), so the check is a parity test
// against the behaviour that shipped, quirks and all:
//
//   - an unparseable string is opaque black (0,0,0,255), never an error;
//   - "rgbx(1,2,3)" parses as CSS rgb(), because the prefix test is startsWith("rgb");
//   - GetColorHex() is "#rrggbb" and drops alpha, which is why Timeline's GPU background
//     clear forces alpha to 255 rather than reading the alpha curve.
//
// To regenerate after an intentional change, see W18 in `git show 628a53d5:doc/gpu-migration/GPU-WORKLIST.md`: build
// a small program that links Qt, run the recorded inputs through the old parser, and paste
// the results back here.

#include "Recipes.h"
#include <fstream>
#include "gpu/GpuTelemetry.h"
extern "C" {
#include <libswscale/swscale.h>
}
#include "gpu/GpuFrame.h"
#include "skia/include/core/SkPixmap.h"

#include "Color.h"
#include "FFmpegReader.h"
#include "GpuEffect.h"
#include "Settings.h"
#include "Timeline.h"
#include "effects/Blur.h"
#include "effects/Brightness.h"
#include "effects/Crop.h"
#include "effects/Enhancement.h"
#include "effects/ColorMap.h"
#include "effects/Mask.h"
#include "gpu/CudaInterop.h"
#include "gpu/GpuDevice.h"
#include "gpu/GpuYuv.h"
#include "gpu/GpuOverlay.h"

#include "effects/image-processing-lib/src/Effects/effects.h"
#include "effects/image-processing-lib/src/Planner/EffectPlan.h"
#include "Json.h"

#include "Frame.h"

#include <QImage>
#include <QPainter>

#include <memory>
#include <string>
#include <cmath>
#include <algorithm>
#include <chrono>
#include <thread>
#include <vector>

using namespace golden;

namespace {

struct ColorCase {
    const char* input;
    int r, g, b, a;
};

// The 147 SVG colour keywords plus "transparent", as QColor::colorNames() reported them.
const ColorCase kNamedCases[] = {
    {"aliceblue", 240, 248, 255, 255},
    {"antiquewhite", 250, 235, 215, 255},
    {"aqua", 0, 255, 255, 255},
    {"aquamarine", 127, 255, 212, 255},
    {"azure", 240, 255, 255, 255},
    {"beige", 245, 245, 220, 255},
    {"bisque", 255, 228, 196, 255},
    {"black", 0, 0, 0, 255},
    {"blanchedalmond", 255, 235, 205, 255},
    {"blue", 0, 0, 255, 255},
    {"blueviolet", 138, 43, 226, 255},
    {"brown", 165, 42, 42, 255},
    {"burlywood", 222, 184, 135, 255},
    {"cadetblue", 95, 158, 160, 255},
    {"chartreuse", 127, 255, 0, 255},
    {"chocolate", 210, 105, 30, 255},
    {"coral", 255, 127, 80, 255},
    {"cornflowerblue", 100, 149, 237, 255},
    {"cornsilk", 255, 248, 220, 255},
    {"crimson", 220, 20, 60, 255},
    {"cyan", 0, 255, 255, 255},
    {"darkblue", 0, 0, 139, 255},
    {"darkcyan", 0, 139, 139, 255},
    {"darkgoldenrod", 184, 134, 11, 255},
    {"darkgray", 169, 169, 169, 255},
    {"darkgreen", 0, 100, 0, 255},
    {"darkgrey", 169, 169, 169, 255},
    {"darkkhaki", 189, 183, 107, 255},
    {"darkmagenta", 139, 0, 139, 255},
    {"darkolivegreen", 85, 107, 47, 255},
    {"darkorange", 255, 140, 0, 255},
    {"darkorchid", 153, 50, 204, 255},
    {"darkred", 139, 0, 0, 255},
    {"darksalmon", 233, 150, 122, 255},
    {"darkseagreen", 143, 188, 143, 255},
    {"darkslateblue", 72, 61, 139, 255},
    {"darkslategray", 47, 79, 79, 255},
    {"darkslategrey", 47, 79, 79, 255},
    {"darkturquoise", 0, 206, 209, 255},
    {"darkviolet", 148, 0, 211, 255},
    {"deeppink", 255, 20, 147, 255},
    {"deepskyblue", 0, 191, 255, 255},
    {"dimgray", 105, 105, 105, 255},
    {"dimgrey", 105, 105, 105, 255},
    {"dodgerblue", 30, 144, 255, 255},
    {"firebrick", 178, 34, 34, 255},
    {"floralwhite", 255, 250, 240, 255},
    {"forestgreen", 34, 139, 34, 255},
    {"fuchsia", 255, 0, 255, 255},
    {"gainsboro", 220, 220, 220, 255},
    {"ghostwhite", 248, 248, 255, 255},
    {"gold", 255, 215, 0, 255},
    {"goldenrod", 218, 165, 32, 255},
    {"gray", 128, 128, 128, 255},
    {"green", 0, 128, 0, 255},
    {"greenyellow", 173, 255, 47, 255},
    {"grey", 128, 128, 128, 255},
    {"honeydew", 240, 255, 240, 255},
    {"hotpink", 255, 105, 180, 255},
    {"indianred", 205, 92, 92, 255},
    {"indigo", 75, 0, 130, 255},
    {"ivory", 255, 255, 240, 255},
    {"khaki", 240, 230, 140, 255},
    {"lavender", 230, 230, 250, 255},
    {"lavenderblush", 255, 240, 245, 255},
    {"lawngreen", 124, 252, 0, 255},
    {"lemonchiffon", 255, 250, 205, 255},
    {"lightblue", 173, 216, 230, 255},
    {"lightcoral", 240, 128, 128, 255},
    {"lightcyan", 224, 255, 255, 255},
    {"lightgoldenrodyellow", 250, 250, 210, 255},
    {"lightgray", 211, 211, 211, 255},
    {"lightgreen", 144, 238, 144, 255},
    {"lightgrey", 211, 211, 211, 255},
    {"lightpink", 255, 182, 193, 255},
    {"lightsalmon", 255, 160, 122, 255},
    {"lightseagreen", 32, 178, 170, 255},
    {"lightskyblue", 135, 206, 250, 255},
    {"lightslategray", 119, 136, 153, 255},
    {"lightslategrey", 119, 136, 153, 255},
    {"lightsteelblue", 176, 196, 222, 255},
    {"lightyellow", 255, 255, 224, 255},
    {"lime", 0, 255, 0, 255},
    {"limegreen", 50, 205, 50, 255},
    {"linen", 250, 240, 230, 255},
    {"magenta", 255, 0, 255, 255},
    {"maroon", 128, 0, 0, 255},
    {"mediumaquamarine", 102, 205, 170, 255},
    {"mediumblue", 0, 0, 205, 255},
    {"mediumorchid", 186, 85, 211, 255},
    {"mediumpurple", 147, 112, 219, 255},
    {"mediumseagreen", 60, 179, 113, 255},
    {"mediumslateblue", 123, 104, 238, 255},
    {"mediumspringgreen", 0, 250, 154, 255},
    {"mediumturquoise", 72, 209, 204, 255},
    {"mediumvioletred", 199, 21, 133, 255},
    {"midnightblue", 25, 25, 112, 255},
    {"mintcream", 245, 255, 250, 255},
    {"mistyrose", 255, 228, 225, 255},
    {"moccasin", 255, 228, 181, 255},
    {"navajowhite", 255, 222, 173, 255},
    {"navy", 0, 0, 128, 255},
    {"oldlace", 253, 245, 230, 255},
    {"olive", 128, 128, 0, 255},
    {"olivedrab", 107, 142, 35, 255},
    {"orange", 255, 165, 0, 255},
    {"orangered", 255, 69, 0, 255},
    {"orchid", 218, 112, 214, 255},
    {"palegoldenrod", 238, 232, 170, 255},
    {"palegreen", 152, 251, 152, 255},
    {"paleturquoise", 175, 238, 238, 255},
    {"palevioletred", 219, 112, 147, 255},
    {"papayawhip", 255, 239, 213, 255},
    {"peachpuff", 255, 218, 185, 255},
    {"peru", 205, 133, 63, 255},
    {"pink", 255, 192, 203, 255},
    {"plum", 221, 160, 221, 255},
    {"powderblue", 176, 224, 230, 255},
    {"purple", 128, 0, 128, 255},
    {"red", 255, 0, 0, 255},
    {"rosybrown", 188, 143, 143, 255},
    {"royalblue", 65, 105, 225, 255},
    {"saddlebrown", 139, 69, 19, 255},
    {"salmon", 250, 128, 114, 255},
    {"sandybrown", 244, 164, 96, 255},
    {"seagreen", 46, 139, 87, 255},
    {"seashell", 255, 245, 238, 255},
    {"sienna", 160, 82, 45, 255},
    {"silver", 192, 192, 192, 255},
    {"skyblue", 135, 206, 235, 255},
    {"slateblue", 106, 90, 205, 255},
    {"slategray", 112, 128, 144, 255},
    {"slategrey", 112, 128, 144, 255},
    {"snow", 255, 250, 250, 255},
    {"springgreen", 0, 255, 127, 255},
    {"steelblue", 70, 130, 180, 255},
    {"tan", 210, 180, 140, 255},
    {"teal", 0, 128, 128, 255},
    {"thistle", 216, 191, 216, 255},
    {"tomato", 255, 99, 71, 255},
    {"transparent", 0, 0, 0, 0},
    {"turquoise", 64, 224, 208, 255},
    {"violet", 238, 130, 238, 255},
    {"wheat", 245, 222, 179, 255},
    {"white", 255, 255, 255, 255},
    {"whitesmoke", 245, 245, 245, 255},
    {"yellow", 255, 255, 0, 255},
    {"yellowgreen", 154, 205, 50, 255},
};

// Hex forms, whitespace handling, the CSS rgb()/rgba() branch, and malformed input.
const ColorCase kFormCases[] = {
    {"#fff", 255, 255, 255, 255},
    {"#abc", 170, 187, 204, 255},
    {"#000", 0, 0, 0, 255},
    {"#ffffff", 255, 255, 255, 255},
    {"#ff0000", 255, 0, 0, 255},
    {"#abc123", 171, 193, 35, 255},
    {"#00ff80", 0, 255, 128, 255},
    {"#80ff0000", 255, 0, 0, 128},
    {"#00000000", 0, 0, 0, 0},
    {"#ffffffff", 255, 255, 255, 255},
    {"#aabbccdd", 187, 204, 221, 170},
    {"#123456789", 18, 69, 120, 255},
    {"#fff000000", 255, 0, 0, 255},
    {"#abcdef012345", 171, 238, 35, 255},
    {"#112233445566", 17, 51, 85, 255},
    {"#ffff00000000", 255, 0, 0, 255},
    {"red", 255, 0, 0, 255},
    {"RED", 255, 0, 0, 255},
    {"Red", 255, 0, 0, 255},
    {"transparent", 0, 0, 0, 0},
    {"TRANSPARENT", 0, 0, 0, 0},
    {"cornflowerblue", 100, 149, 237, 255},
    {"darkslategrey", 47, 79, 79, 255},
    {"  #ff0000  ", 255, 0, 0, 255},
    {"\tred\n", 255, 0, 0, 255},
    {" transparent ", 0, 0, 0, 0},
    {"rgb(1,2,3)", 1, 2, 3, 255},
    {"rgb( 10 , 20 , 30 )", 10, 20, 30, 255},
    {"RGB(1,2,3)", 1, 2, 3, 255},
    {"rgb(1.4,2.6,3.5)", 1, 3, 4, 255},
    {"rgba(1,2,3,0.5)", 1, 2, 3, 128},
    {"rgba(1,2,3,1)", 1, 2, 3, 255},
    {"rgba(1,2,3,2)", 1, 2, 3, 2},
    {"rgba(1,2,3,255)", 1, 2, 3, 255},
    {"rgba(1,2,3,0)", 1, 2, 3, 0},
    {"RGBA(1,2,3,0.25)", 1, 2, 3, 64},
    {"rgb(-5,-5,-5)", 0, 0, 0, 255},
    {"rgb(300,300,300)", 255, 255, 255, 255},
    {"rgb(1,2,3,4,5)", 1, 2, 3, 4},
    {"rgba(1,2,3,)", 1, 2, 3, 0},
    {"", 0, 0, 0, 255},
    {" ", 0, 0, 0, 255},
    {"garbage", 0, 0, 0, 255},
    {"#", 0, 0, 0, 255},
    {"#f", 0, 0, 0, 255},
    {"#ff", 0, 0, 0, 255},
    {"#ffff", 0, 0, 0, 255},
    {"#fffff", 0, 0, 0, 255},
    {"#fffffff", 0, 0, 0, 255},
    {"#ffffffff0", 255, 255, 254, 255},
    {"#gggggg", 0, 0, 0, 255},
    {"#12345g", 0, 0, 0, 255},
    {"REDD", 0, 0, 0, 255},
    {"rgb(1,2)", 0, 0, 0, 255},
    {"rgb()", 0, 0, 0, 255},
    {"rgb(", 0, 0, 0, 255},
    {"rgb", 0, 0, 0, 255},
    {"rgbx(1,2,3)", 1, 2, 3, 255},
    {"rgb(a,b,c)", 0, 0, 0, 255},
};

std::string rgbaText(const std::vector<int>& v) {
    return "(" + std::to_string(v[0]) + "," + std::to_string(v[1]) + "," +
           std::to_string(v[2]) + "," + std::to_string(v[3]) + ")";
}

// Run one table, reporting a single check for it and naming the first input that disagrees.
void checkTable(const char* name, const ColorCase* cases, std::size_t count,
                std::vector<Check>& checks) {
    std::size_t failed = 0;
    std::string first_failure;
    for (std::size_t i = 0; i < count; ++i) {
        const ColorCase& c = cases[i];
        const std::vector<int> got = openshot::Color(std::string(c.input)).GetColorRGBA(0);
        if (got[0] == c.r && got[1] == c.g && got[2] == c.b && got[3] == c.a) continue;
        if (failed++ == 0)
            first_failure = std::string("\"") + c.input + "\" expected (" +
                std::to_string(c.r) + "," + std::to_string(c.g) + "," + std::to_string(c.b) +
                "," + std::to_string(c.a) + ") got " + rgbaText(got);
    }
    checks.push_back({name, failed == 0,
                      failed == 0 ? std::to_string(count) + " inputs match"
                                  : std::to_string(failed) + " of " + std::to_string(count) +
                                    " differ, first: " + first_failure});
}

void unitScene(Scene& s) {
    // The harness requires a timeline even when a scenario renders nothing.
    s.makeTimeline().Open();
}

} // namespace

void golden::registerUnitScenarios() {
    // Does a GPU effect actually run as a shader in the real render path?
    //
    // This is not a pixel check and cannot be one. GpuEffect::ApplyOnGpu declining is a normal
    // answer -- no GPU, OPENSHOT_GPU=off, a fragment that will not compile -- and the CPU twin
    // then produces the correct frame. So a comparison of pixels passes identically whether the
    // shader ran or never existed, which is how the first openshot-gpu-effect-parity reported 32
    // bit-exact results for a fragment that had failed to compile (GPU-DECISIONS.md, W19).
    //
    // GpuEffect's counters are the only way to tell the two apart, so this asserts on them: with
    // a GPU up, at least one frame must have gone through a shader; with the GPU off, none may.
    // Does an overlay composite actually run as a shader? Same reasoning as unit.gpu_effect_path
    // below: GpuOverlay declines silently and OpenCV then produces the correct frame, so comparing
    // pixels cannot tell the two apart. The overlay scenarios in Transitions.cpp are bit-exact on
    // Vulkan, which is only meaningful if the shader is what produced them.
    addCustom("unit.gpu_overlay_path", {"unit", "gpu"},
        [](Scene& s) {
            using namespace golden::recipes;
            auto& tl = s.makeTimeline();
            tl.AddClip(backgroundClip(s, s.media("background_960x540.png")));
            MediaSpec a; a.path = s.media("clip_a_640x360_30.mp4"); a.start = 0.0; a.end = 2.0;
            a.track = 1; a.transform = Transform{BBox{0.5f, 0.5f, 0.9f, 0.9f}};
            MediaSpec b; b.path = s.media("clip_b_854x480_24.mp4"); b.start = 1.5; b.end = 3.5;
            b.track = 1; b.priority = 1; b.transform = Transform{BBox{0.5f, 0.5f, 0.9f, 0.9f}};
            auto* out_clip = mediaClip(s, a);
            auto* in_clip = mediaClip(s, b);
            applyOverlappingTransition(*out_clip, *in_clip, 1.0, s.fps.ToDouble(), {});
            addOverlayClip(s, s.media("overlay_gradients_640x360_30.mp4"), 1.0,
                           openshot::Clip::ADDITIVE_BLEND, out_clip, in_clip);
            tl.AddClip(out_clip);
            tl.AddClip(in_clip);
            tl.Open();
        },
        [](Scene& s, std::vector<Captured>&, std::vector<Check>& checks) {
            const bool gpu = openshot::GpuDevice::Instance().available();
            openshot::GpuOverlay::ResetCounters();
            s.timeline->GetFrame(42);   // inside the transition's ramp, where the overlay applies
            const long long passes = openshot::GpuOverlay::GpuPasses();
            const long long fallbacks = openshot::GpuOverlay::CpuFallbacks();
            const std::string counts = "gpu_passes=" + std::to_string(passes) +
                                       " cpu_fallbacks=" + std::to_string(fallbacks);
            if (gpu)
                checks.push_back({"overlay_ran_as_shader", passes > 0,
                                  passes > 0 ? counts
                                             : "a GPU is available but no overlay composite ran as "
                                               "a shader: " + counts});
            else
                checks.push_back({"overlay_ran_on_cpu", passes == 0,
                                  passes == 0 ? counts
                                              : "no GPU, so nothing should have run as a shader: "
                                                + counts});
        });

    addCustom("unit.gpu_effect_path", {"unit", "gpu"},
        [](Scene& s) {
            auto& tl = s.makeTimeline();
            using namespace golden::recipes;
            tl.AddClip(backgroundClip(s, s.media("background_960x540.png")));
            MediaSpec m; m.path = s.media("image_alpha_320x200.png"); m.isImage = true; m.end = 1.0;
            auto* c = mediaClip(s, m);
            c->AddEffect(new openshot::Brightness(openshot::Keyframe(0.2)));
            tl.AddClip(c);
            tl.Open();
        },
        [](Scene& s, std::vector<Captured>&, std::vector<Check>& checks) {
            const bool gpu = openshot::GpuDevice::Instance().available();
            openshot::GpuEffect::ResetCounters();
            s.timeline->GetFrame(1);
            const long long passes = openshot::GpuEffect::GpuPasses();
            const long long fallbacks = openshot::GpuEffect::CpuFallbacks();

            const std::string counts = "gpu_passes=" + std::to_string(passes) +
                                       " cpu_fallbacks=" + std::to_string(fallbacks);
            if (gpu)
                checks.push_back({"effect_ran_as_shader", passes > 0,
                                  passes > 0 ? counts
                                             : "a GPU is available but no effect ran as a shader: "
                                               + counts});
            else
                checks.push_back({"effect_ran_on_cpu", passes == 0 && fallbacks > 0,
                                  passes == 0 && fallbacks > 0
                                      ? counts
                                      : "no GPU, so every effect should have fallen back: " + counts});
        });

    // A still image hands draw_to_canvas the same QImage every frame, and it is uploaded once
    // (Clip::HostTextureCache). Uploading it every frame was 1.65 ms a frame at 1080p for the
    // background PNG every export carries, and looks exactly the same in the goldens -- so this
    // asserts the count. Two stills over five frames: two uploads, eight reuses.
    addCustom("unit.host_texture_cache", {"unit", "gpu"},
        [](Scene& s) {
            auto& tl = s.makeTimeline();
            using namespace golden::recipes;
            tl.AddClip(backgroundClip(s, s.media("background_960x540.png")));
            MediaSpec m; m.path = s.media("image_alpha_320x200.png"); m.isImage = true; m.end = 1.0;
            tl.AddClip(mediaClip(s, m));
            tl.Open();
        },
        [](Scene& s, std::vector<Captured>&, std::vector<Check>& checks) {
            using openshot::GpuCounters;
            if (!openshot::GpuDevice::Instance().available()) {
                checks.push_back({"no_gpu", true, "nothing is uploaded without a GPU"});
                return;
            }
            s.timeline->GetFrame(1);   // opens the readers and fills the cache
            const auto uploads0 = GpuCounters::Get(GpuCounters::Upload);
            const auto cached0 = GpuCounters::Get(GpuCounters::UploadCached);
            for (int f = 2; f <= 5; ++f)
                s.timeline->GetFrame(f);
            const auto uploads = GpuCounters::Get(GpuCounters::Upload) - uploads0;
            const auto cached = GpuCounters::Get(GpuCounters::UploadCached) - cached0;
            const std::string counts = "uploads=" + std::to_string(uploads) +
                                       " cached=" + std::to_string(cached);
            checks.push_back({"stills_uploaded_once", uploads == 0 && cached == 8, counts});
        });

    // Above the reference width the rotational blur works on a resized copy, and above a megapixel
    // the diagonal blur on a half-size one. Both were CPU-only there -- the 1080p frames every
    // ROTATE and PAN_DIAGONAL transition hits -- and are now a resample, the blur and a resample
    // back, as three GPU passes (owner's decision 2026-09-24: the resamples are OpenCV's fixed
    // point to within a code value, and the rotational blur's intermediate is 8-bit where the
    // C++'s is float). Asserts the passes ran, and the result against the CPU.
    addCustom("unit.gpu_blur_large", {"unit", "gpu"}, unitScene,
        [](Scene& s, std::vector<Captured>&, std::vector<Check>& checks) {
            if (!openshot::GpuDevice::Instance().available()) {
                checks.push_back({"no_gpu", true, "the OpenCV blurs run without a GPU"});
                return;
            }
            const QImage background(QString::fromStdString(s.media("background_960x540.png")));
            const QImage pattern(QString::fromStdString(s.media("image_rgb_400x300.jpg")));
            struct Case { const char* label; int w, h; int diagonal; double rotational; };
            const Case cases[] = {{"rotational(10) 1080p", 1920, 1080, 0, 10.0},
                                  {"rotational(28) 1080p", 1920, 1080, 0, 28.0},
                                  {"rotational(20) 2160p", 3840, 2160, 0, 20.0},
                                  {"diagonal(100) 1080p", 1920, 1080, 100, 0.0},
                                  {"diagonal(57) 2160p", 3840, 2160, 57, 0.0}};
            for (const Case& c : cases) {
                // Detail to blur: the background with the test pattern over its middle.
                QImage source = background.scaled(c.w, c.h).convertToFormat(QImage::Format_RGBA8888_Premultiplied);
                {
                    QPainter painter(&source);
                    painter.drawImage(QRect(c.w / 4, c.h / 4, c.w / 2, c.h / 2), pattern);
                }
                auto run = [&]() {
                    openshot::Blur blur(openshot::Keyframe(0), openshot::Keyframe(0),
                                        openshot::Keyframe(c.diagonal), openshot::Keyframe(c.rotational),
                                        openshot::Keyframe(0), openshot::Keyframe(0), openshot::Keyframe(0),
                                        openshot::Keyframe(0), openshot::Keyframe(1));
                    auto frame = std::make_shared<openshot::Frame>(1, c.w, c.h, "#000000");
                    frame->AddImage(std::make_shared<QImage>(source.copy()));
                    return golden::fromFrame(blur.GetFrame(frame, 1));
                };
                openshot::GpuEffect::ResetCounters();
                const golden::Image gpu = run();
                const long long passes = openshot::GpuEffect::GpuPasses();
                const openshot::GpuDevice::Backend backend = openshot::GpuDevice::RequestedBackend();
                openshot::GpuDevice::SetBackend(openshot::GpuDevice::Backend::Off);
                const golden::Image cpu = run();
                openshot::GpuDevice::SetBackend(backend);
                const golden::Metrics m = golden::compare(cpu, gpu);
                char buf[160];
                std::snprintf(buf, sizeof buf, "%s: gpu_passes=%lld psnr vs CPU %.2f max %d", c.label, passes, m.psnr, m.maxAbs);
                checks.push_back({c.label, passes == 3 && m.psnr >= 45.0, buf});
            }
        });

    // A GPU-decoded video matte is used on the GPU (Mask::PrepareGpuMask): its own texture when it
    // is the frame's size -- bit-exact -- and a bilinear resample otherwise, where Qt's
    // SmoothTransformation box-averages a shrink (owner's decision, 2026-09-24). Before, the matte
    // was read back, scaled by Qt and uploaded every frame. Asserts no readback happens inside the
    // effect, and the result against the CPU at both sizes.
    addCustom("unit.gpu_mask_matte", {"unit", "gpu"}, unitScene,
        [](Scene& s, std::vector<Captured>&, std::vector<Check>& checks) {
            using openshot::GpuCounters;
            if (!openshot::GpuDevice::Instance().available()) {
                checks.push_back({"no_gpu", true, "the matte is prepared by Qt without a GPU"});
                return;
            }
            openshot::Settings* settings = openshot::Settings::Instance();
            const QImage background(QString::fromStdString(s.media("background_960x540.png")));
            auto run = [&](int w, int h, unsigned long long* readbacks) {
                const bool previous = settings->GPU_DECODE;
                settings->GPU_DECODE = true;
                openshot::FFmpegReader matte(s.media("matte_wipe_640x360_30.mp4"));
                matte.Open();
                openshot::Mask mask(&matte, openshot::Keyframe(0.0), openshot::Keyframe(3.0));
                mask.invert = true;
                auto frame = std::make_shared<openshot::Frame>(1, w, h, "#000000");
                frame->AddImage(std::make_shared<QImage>(
                    background.scaled(w, h).convertToFormat(QImage::Format_RGBA8888_Premultiplied)));
                const auto before = GpuCounters::Get(GpuCounters::Readback);
                auto out = mask.GetFrame(frame, 60);
                if (readbacks) *readbacks = GpuCounters::Get(GpuCounters::Readback) - before;
                const golden::Image result = golden::fromFrame(out);
                matte.Close();
                settings->GPU_DECODE = previous;
                return result;
            };
            for (const auto& [w, h] : std::vector<std::pair<int, int>>{{640, 360}, {480, 270}}) {
                unsigned long long readbacks = 0;
                const golden::Image gpu = run(w, h, &readbacks);
                const openshot::GpuDevice::Backend backend = openshot::GpuDevice::RequestedBackend();
                openshot::GpuDevice::SetBackend(openshot::GpuDevice::Backend::Off);
                const golden::Image cpu = run(w, h, nullptr);
                openshot::GpuDevice::SetBackend(backend);
                const golden::Metrics m = golden::compare(cpu, gpu);
                const bool same_size = w == 640;
                char buf[160];
                std::snprintf(buf, sizeof buf, "%dx%d: readbacks in the effect %llu, psnr vs CPU %.2f, max %d",
                              w, h, readbacks, m.psnr, m.maxAbs);
                // Same size: the only difference left is GPU decode's own rounding, a few LSB.
                const bool ok = readbacks == 0 && (same_size ? m.maxAbs <= 4 : m.psnr >= 40.0);
                checks.push_back({same_size ? "matte_same_size" : "matte_resampled", ok, buf});
            }
        });

    // Blur is four effects in one class and three of them are now shaders, each declining
    // silently -- and a decline produces exactly the golden frame, so transitions.blur passing on
    // Vulkan says nothing about which path drew it. This asserts the path, per mode.
    //
    // The box blur's pass count is checked as well as its being non-zero: it is separable and runs
    // one draw per axis per box, so a half silently skipped would still look like "the shader
    // ran". The expected count comes from the same function the effect resolves its kernels with.
    addCustom("unit.gpu_blur_path", {"unit", "gpu"},
        [](Scene& s) {
            auto& tl = s.makeTimeline();
            using namespace golden::recipes;
            tl.AddClip(backgroundClip(s, s.media("background_960x540.png")));
            MediaSpec m; m.path = s.media("image_alpha_320x200.png"); m.isImage = true; m.end = 1.0;
            tl.AddClip(mediaClip(s, m));
            tl.Open();
        },
        [](Scene& s, std::vector<Captured>&, std::vector<Check>& checks) {
            const bool gpu = openshot::GpuDevice::Instance().available();

            // A private copy of a real composited frame for each mode, so the three do not blur
            // each other's output, and applied straight to it so this measures the effect rather
            // than the compositor around it.
            const std::shared_ptr<QImage> source = s.timeline->GetFrame(1)->GetImage();
            auto count = [&](openshot::Blur& blur) {
                auto frame = std::make_shared<openshot::Frame>();
                frame->AddImage(std::make_shared<QImage>(source->copy()));
                openshot::GpuEffect::ResetCounters();
                blur.GetFrame(frame, 1);
                return openshot::GpuEffect::GpuPasses();
            };

            constexpr int kRadius = 40;
            openshot::Blur box{openshot::Keyframe(kRadius), openshot::Keyframe(kRadius)};
            openshot::Blur diagonal{0, 0, openshot::Keyframe(kRadius)};
            openshot::Blur rotational{0, 0, 0, openshot::Keyframe(25.0)};

            const long long box_passes = count(box);
            const long long diagonal_passes = count(diagonal);
            const long long rotational_passes = count(rotational);

            // One draw per axis per box, minus any whose kernel is a single tap.
            const Podcastle::Effects::BlurBoxes boxes =
                Podcastle::Effects::blurBoxSizes(s.width, kRadius, kRadius);
            long long expected_box = 0;
            for (int pass = 0; pass < 3; ++pass) {
                if (boxes.x[pass] > 1) expected_box++;
                if (boxes.y[pass] > 1) expected_box++;
            }

            auto report = [](const char* name, long long passes, long long expected, bool gpu_on,
                             std::vector<Check>& out) {
                const long long want = gpu_on ? expected : 0;
                const std::string got = "gpu_passes=" + std::to_string(passes);
                out.push_back({name, passes == want,
                               passes == want ? got
                                              : got + ", expected " + std::to_string(want)});
            };
            report("box_blur_pass_count", box_passes, expected_box, gpu, checks);
            report("diagonal_blur_pass_count", diagonal_passes, 1, gpu, checks);
            report("rotational_blur_pass_count", rotational_passes, 1, gpu, checks);

            // The zoom blur is three draws -- forward polar, the box blur along rho, inverse
            // polar -- and the count is what proves the middle one is not being skipped.
            openshot::Blur zoom{0, 0, 0, 0, openshot::Keyframe(40.0),
                                openshot::Keyframe(0.5), openshot::Keyframe(0.5)};
            report("zoom_blur_pass_count", count(zoom), 3, gpu, checks);
        });

    // ColorMap's fragment declines silently and a decline produces the golden frame, so
    // effects.colormap_lut passing on Vulkan says nothing about which path drew it.
    addCustom("unit.gpu_colormap_path", {"unit", "gpu"},
        [](Scene& s) {
            auto& tl = s.makeTimeline();
            using namespace golden::recipes;
            tl.AddClip(backgroundClip(s, s.media("background_960x540.png")));
            tl.Open();
        },
        [](Scene& s, std::vector<Captured>&, std::vector<Check>& checks) {
            const bool gpu = openshot::GpuDevice::Instance().available();
            const std::shared_ptr<QImage> source = s.timeline->GetFrame(1)->GetImage();

            auto count = [&](openshot::EffectBase& effect) {
                auto frame = std::make_shared<openshot::Frame>();
                frame->AddImage(std::make_shared<QImage>(source->copy()));
                openshot::GpuEffect::ResetCounters();
                effect.GetFrame(frame, 1);
                return openshot::GpuEffect::GpuPasses();
            };

            openshot::ColorMap lut{s.media("lut_example.cube")};
            const long long lut_passes = count(lut);
            checks.push_back({"colormap_lut_path", lut_passes == (gpu ? 1 : 0),
                              "gpu_passes=" + std::to_string(lut_passes)});

            // Colour-match mode is deliberately not ported: its cube is re-baked from the
            // frame's own statistics, which is the readback this pass exists to avoid. It must
            // decline rather than silently produce something else.
            openshot::ColorMap match{""};
            match.SetRefImagePath(s.media("background_960x540.png"));
            const long long match_passes = count(match);
            checks.push_back({"colormap_match_declines", match_passes == 0,
                              "gpu_passes=" + std::to_string(match_passes)});
        });

    // Is the reader's GPU YUV->RGBA pass the same function as swscale's?
    //
    // This is the sharp gate on that path, and the reason the golden tolerances for a
    // GPU-decoded scenario can afford to be loose: it compares the two conversions directly,
    // on an unscaled clip, with nothing else in the frame. It also asserts the GPU actually
    // ran -- GpuYuv::Convert declining is a normal answer and the swscale result is then
    // correct, so a comparison that cannot tell the two apart would pass on a shader that
    // never compiled.
    addCustom("unit.gpu_decode", {"unit", "gpu"}, unitScene,
        [](Scene& s, std::vector<Captured>&, std::vector<Check>& checks) {
            const int wanted = 4;
            const std::string clip = s.media("clip_a_640x360_30.mp4");
            const auto decode = [&](bool on_gpu) {
                openshot::Settings::Instance()->GPU_DECODE = on_gpu;
                std::vector<std::vector<uint8_t>> frames;
                openshot::FFmpegReader reader(clip);
                reader.Open();
                for (int i = 1; i <= wanted; ++i) {
                    auto image = reader.GetFrame(i)->GetImage();
                    frames.emplace_back(image->bits(), image->bits() + image->sizeInBytes());
                }
                reader.Close();
                return frames;
            };

            openshot::Settings* settings = openshot::Settings::Instance();
            const bool previous = settings->GPU_DECODE;
            // Software decode for both arms, whatever the harness asked for: this compares the
            // two conversions on identical YUV420P planes, and NVDEC's NV12 would put swscale's
            // own 40 dB NV12-vs-YUV420P inconsistency into the CPU arm (unit.nvdec_on_device
            // covers NVDEC).
            const int previous_hw = settings->HARDWARE_DECODER;
            settings->HARDWARE_DECODER = 0;
            const std::vector<std::vector<uint8_t>> cpu = decode(false);
            const unsigned long long before = openshot::GpuYuv::Conversions();
            const std::vector<std::vector<uint8_t>> gpu = decode(true);
            const unsigned long long ran = openshot::GpuYuv::Conversions() - before;
            settings->GPU_DECODE = previous;
            settings->HARDWARE_DECODER = previous_hw;

            const bool have_gpu = openshot::GpuDevice::Instance().available();
            if (!have_gpu) {
                // No GPU is a supported configuration: the pass must decline, and declining
                // must leave the swscale result untouched.
                bool same = true;
                for (int i = 0; i < wanted && same; ++i) same = cpu[i] == gpu[i];
                checks.push_back({"gpu_decode_declines", ran == 0 && same,
                                  ran == 0 && same
                                      ? "no GPU: the pass declined and the frames are identical"
                                      : "the pass ran or changed pixels with no GPU"});
                return;
            }

            double worst = 1e9;
            int worst_delta = 0;
            for (int i = 0; i < wanted; ++i) {
                if (cpu[i].size() != gpu[i].size()) { worst = 0; break; }
                double sum = 0;
                for (std::size_t p = 0; p < cpu[i].size(); ++p) {
                    const int d = int(cpu[i][p]) - int(gpu[i][p]);
                    if (std::abs(d) > worst_delta) worst_delta = std::abs(d);
                    sum += double(d) * d;
                }
                const double mse = sum / cpu[i].size();
                const double psnr = mse == 0 ? 1e9 : 10.0 * std::log10(255.0 * 255.0 / mse);
                worst = std::min(worst, psnr);
            }
            // >=, not ==: the reader decodes ahead, so more frames go through the pass than
            // were asked for. What matters is that it ran at all.
            const bool ok = ran >= static_cast<unsigned long long>(wanted) && worst >= 44.0 &&
                            worst_delta <= 4;
            char detail[192];
            std::snprintf(detail, sizeof(detail),
                          "%llu frames on the GPU (%d asked for), worst PSNR %.2f dB (gate 44), max "
                          "delta %d (gate 4)",
                          ran, wanted, worst, worst_delta);
            checks.push_back({"gpu_decode_matches_swscale", ok, detail});
        });

    // NVDEC's frames converted where they are (W23): the zero-copy path must be the same picture
    // as software decode through the same conversion, and it must actually have run.
    //
    // Exact, not "close". H.264 decoding is normative -- NVDEC and libavcodec agree to the byte
    // in YUV (measured on the suite's and the bench's media, 640x360 to 4K) -- and both arms go
    // through the same GpuYuv shader, so any difference at all is this path's bug: a plane
    // copied at the wrong pitch, chroma sampled at the wrong size, a missed semaphore showing
    // the previous frame. DeviceConversions() is what tells the two arms apart; without it a
    // path that silently downloaded would pass. With no interop (GPU off, lavapipe, no NVIDIA
    // driver) the path must decline and hardware decode must still produce the same frames.
    addCustom("unit.nvdec_on_device", {"unit", "gpu"}, unitScene,
        [](Scene& s, std::vector<Captured>&, std::vector<Check>& checks) {
          // H.264, and the webm codecs and HEVC/AV1 NVDEC has been asked for since 2026-09-24.
          const std::pair<const char*, const char*> clips[] = {
              {"h264", "clip_a_640x360_30.mp4"}, {"vp9", "clip_vp9_640x360_30.webm"},
              {"vp8", "clip_vp8_640x360_30.webm"}, {"hevc", "clip_hevc_640x360_30.mp4"},
              {"av1", "clip_av1_640x360_30.mp4"}};
          for (const auto& [codec, file] : clips) {
            const int wanted = 8;
            const std::string clip = s.media(file);
            openshot::Settings* settings = openshot::Settings::Instance();
            const bool previous_gpu = settings->GPU_DECODE;
            const int previous_hw = settings->HARDWARE_DECODER;
            const auto decode = [&](int hardware) {
                settings->HARDWARE_DECODER = hardware;
                std::vector<std::vector<uint8_t>> frames;
                openshot::FFmpegReader reader(clip);
                reader.Open();
                for (int i = 1; i <= wanted; ++i) {
                    auto image = reader.GetFrame(i)->GetImage();
                    frames.emplace_back(image->bits(), image->bits() + image->sizeInBytes());
                }
                reader.Close();
                return frames;
            };

            std::vector<std::vector<uint8_t>> software, nvdec;
            unsigned long long ran = 0;
            std::string failure;
            try {
                settings->GPU_DECODE = true;
                software = decode(0);
                const unsigned long long before = openshot::GpuYuv::DeviceConversions();
                nvdec = decode(2);
                ran = openshot::GpuYuv::DeviceConversions() - before;
            } catch (const std::exception& e) {
                failure = std::string("threw: ") + e.what();
            }
            // Settings is process-wide: restore before anything else can run.
            settings->GPU_DECODE = previous_gpu;
            settings->HARDWARE_DECODER = previous_hw;
            const std::string label = std::string(codec);
            if (!failure.empty()) {
                checks.push_back({"nvdec_on_device(" + label + ")", false, failure});
                continue;
            }

            int differing = 0, worst = 0;
            for (int i = 0; i < wanted; ++i) {
                if (software[i].size() != nvdec[i].size()) { ++differing; worst = 255; continue; }
                bool frame_differs = false;
                for (std::size_t p = 0; p < software[i].size(); ++p) {
                    const int d = std::abs(int(software[i][p]) - int(nvdec[i][p]));
                    if (d) { frame_differs = true; worst = std::max(worst, d); }
                }
                differing += frame_differs;
            }

            const bool interop = openshot::CudaInterop::Instance().available();
            // With no GPU at all both arms go through swscale, which converts NV12 and YUV420P
            // holding the same samples 40 dB apart (GPU-WORKLIST W23). Nothing to hold exact
            // there; what matters is that the path declined and hardware decode still decoded.
            const bool same_conversion = openshot::GpuDevice::Instance().available();
            char detail[224];
            std::snprintf(detail, sizeof(detail),
                          "%llu frames converted on the device (%d asked for, interop %s); %d of "
                          "%d frames differ from software decode, max delta %d%s",
                          ran, wanted, interop ? "on" : "off", differing, wanted, worst,
                          same_conversion ? "" : " (swscale both arms: not compared)");
            // >=, as in unit.gpu_decode: the reader decodes ahead.
            const bool ok = (differing == 0 || !same_conversion) &&
                            (interop ? ran >= static_cast<unsigned long long>(wanted) : ran == 0);
            checks.push_back({std::string(interop ? "nvdec_on_device_exact(" : "nvdec_on_device_declines(") +
                                  label + ")", ok, detail});
          }
        });

    // A BT.709-tagged chart must decode to the sRGB values it was made from (W23's gate).
    //
    // The GPU conversion honours the stream's declared matrix; swscale as this reader configures
    // it never has, and decodes everything as BT.601. That is why GPU_DECODE changes pixels and
    // why turning it on is the owner's decision -- so the swscale error is measured and reported
    // here, not gated. The GPU arms are gated at 3 code values: an exact BT.709 decode of this file
    // is within 2 of the bars (8-bit YUV), while decoding it as BT.601 is off by ~28.
    addCustom("unit.bt709_chart", {"unit", "gpu"}, unitScene,
        [](Scene& s, std::vector<Captured>&, std::vector<Check>& checks) {
            static const int kBars[8][3] = {{191, 191, 191}, {191, 191, 0}, {0, 191, 191},
                                            {0, 191, 0},     {191, 0, 191}, {191, 0, 0},
                                            {0, 0, 191},     {200, 150, 120}};
            openshot::Settings* settings = openshot::Settings::Instance();
            const bool previous_gpu = settings->GPU_DECODE;
            const int previous_hw = settings->HARDWARE_DECODER;
            // Worst channel error over the bar centres, or -1 if the decode failed.
            const auto worst_error = [&](bool gpu_decode, int hardware,
                                         unsigned long long* device_conversions) {
                settings->GPU_DECODE = gpu_decode;
                settings->HARDWARE_DECODER = hardware;
                int worst = -1;
                try {
                    const unsigned long long before = openshot::GpuYuv::DeviceConversions();
                    openshot::FFmpegReader reader(s.media("chart_bt709_640x360_30.mp4"));
                    reader.Open();
                    auto image = reader.GetFrame(1)->GetImage();
                    reader.Close();
                    if (device_conversions)
                        *device_conversions = openshot::GpuYuv::DeviceConversions() - before;
                    worst = 0;
                    for (int bar = 0; bar < 8; ++bar) {
                        const uint8_t* px = image->constScanLine(180) + (40 + 80 * bar) * 4;
                        for (int c = 0; c < 3; ++c)
                            worst = std::max(worst, std::abs(int(px[c]) - kBars[bar][c]));
                    }
                } catch (const std::exception&) {
                }
                return worst;
            };

            const int cpu = worst_error(false, 0, nullptr);
            const bool have_gpu = openshot::GpuDevice::Instance().available();
            const int gpu = have_gpu ? worst_error(true, 0, nullptr) : -1;
            const bool interop = openshot::CudaInterop::Instance().available();
            unsigned long long device = 0;
            const int nvdec = interop ? worst_error(true, 2, &device) : -1;
            settings->GPU_DECODE = previous_gpu;
            settings->HARDWARE_DECODER = previous_hw;

            const bool ok = cpu >= 0 && (!have_gpu || (gpu >= 0 && gpu <= 3)) &&
                            (!interop || (nvdec >= 0 && nvdec <= 3 && device >= 1));
            char detail[224];
            std::snprintf(detail, sizeof(detail),
                          "worst channel error: GPU %s, NVDEC on device %s (gate 3); swscale %d "
                          "(BT.601 by design, not gated)",
                          have_gpu ? std::to_string(gpu).c_str() : "n/a",
                          interop ? std::to_string(nvdec).c_str() : "n/a", cpu);
            checks.push_back({"bt709_chart_srgb", ok, detail});
        });

    // Read-ahead (W24) is a speed change: the frames must be the ones decoding on the caller's
    // thread gives, through forward walks, seeks back and seeks forward, and the worker must
    // actually have decoded ahead -- a read-ahead that never ran would pass the first half.
    addCustom("unit.read_ahead", {"unit"}, unitScene,
        [](Scene& s, std::vector<Captured>&, std::vector<Check>& checks) {
            openshot::Settings* settings = openshot::Settings::Instance();
            const int previous = settings->READ_AHEAD_FRAMES;
            const std::vector<int> order = {1, 2, 3, 4, 5, 6, 40, 41, 42, 10, 11, 12, 13, 150, 151, 2};
            const auto decode = [&](int depth, bool* went_ahead) {
                settings->READ_AHEAD_FRAMES = depth;
                std::vector<std::vector<uint8_t>> frames;
                openshot::FFmpegReader reader(s.media("clip_a_640x360_30.mp4"));
                reader.Open();
                for (int n : order) {
                    auto image = reader.GetFrame(n)->GetImage();
                    frames.emplace_back(image->bits(), image->bits() + image->sizeInBytes());
                }
                if (went_ahead) {
                    // After frame 20, the worker should put 21 in the cache on its own.
                    reader.GetFrame(20);
                    bool seen = false;
                    for (int i = 0; i < 200 && !seen; ++i) {
                        seen = reader.final_cache.GetFrame(21) != nullptr;
                        if (!seen) std::this_thread::sleep_for(std::chrono::milliseconds(5));
                    }
                    *went_ahead = seen;
                }
                reader.Close();
                return frames;
            };
            bool went_ahead = false;
            const auto plain = decode(0, nullptr);
            const auto ahead = decode(2, &went_ahead);
            settings->READ_AHEAD_FRAMES = previous;

            int differ = 0;
            for (std::size_t i = 0; i < plain.size(); ++i) differ += plain[i] != ahead[i];
            // With GPU decode on and a GPU present, read-ahead is off by design.
            const bool applies = !(settings->GPU_DECODE && openshot::GpuDevice::Instance().available());
            const bool ok = differ == 0 && went_ahead == applies;
            checks.push_back({"read_ahead_same_frames", ok,
                              std::to_string(differ) + " of " + std::to_string(plain.size()) +
                                  " frames differ across walks and seeks; worker decoded ahead: " +
                                  (went_ahead ? "yes" : "no") + (applies ? "" : " (off: GPU decode)")});

            // A stream that can never reach the GPU -- ProRes 4444, which NVDEC is not asked to
            // decode and GpuYuv does not convert, like the service's watermark -- decodes ahead
            // even with GPU decode on, and its frames stay in host memory.
            const bool previous_decode = settings->GPU_DECODE;
            settings->GPU_DECODE = true;
            settings->READ_AHEAD_FRAMES = 2;
            bool host_ahead = false, host_frames = true;
            {
                openshot::FFmpegReader reader(s.media("watermark_prores4444_160x120_30.mov"));
                reader.Open();
                host_frames = !reader.GetFrame(1)->IsGpuBacked() && !reader.GetFrame(5)->IsGpuBacked();
                for (int i = 0; i < 200 && !host_ahead; ++i) {
                    host_ahead = reader.final_cache.GetFrame(6) != nullptr;
                    if (!host_ahead) std::this_thread::sleep_for(std::chrono::milliseconds(5));
                }
                reader.Close();
            }
            settings->GPU_DECODE = previous_decode;
            settings->READ_AHEAD_FRAMES = previous;
            checks.push_back({"read_ahead_host_stream_with_gpu_decode", host_ahead && host_frames,
                              std::string("ProRes 4444 with GPU decode on: worker decoded ahead: ") +
                                  (host_ahead ? "yes" : "no") + ", frames in host memory: " +
                                  (host_frames ? "yes" : "no")});
        });

    // The encoder's RGBA -> NV12 pass (W25) against swscale, and back again.
    //
    // Gated against exact BT.601 limited-range maths, within a code value (the GPU's float
    // rounding at ties). swscale is reported, not gated: its FAST_BILINEAR chroma is the same
    // 2x2 box but is itself a code value off exact, and the writer's BICUBIC is a different
    // filter. And the round trip through GpuYuv on flat 2x2 blocks, where subsampling loses
    // nothing: gated at 2, because 8-bit limited range cannot do better -- an exact
    // RGB -> Y'CbCr -> RGB round trip is already 2 off on e.g. (247, 36, 40). W25's "within
    // 1 LSB" is unreachable as written.
    addCustom("unit.gpu_encode_nv12", {"unit", "gpu"}, unitScene,
        [](Scene&, std::vector<Captured>&, std::vector<Check>& checks) {
            if (!openshot::GpuDevice::Instance().available()) {
                checks.push_back({"gpu_encode_nv12", true, "no GPU: nothing to compare (declines)"});
                return;
            }
            const int W = 64, H = 48;
            std::vector<uint8_t> rgba(W * H * 4);
            uint32_t seed = 12345;
            const auto next = [&] { seed = seed * 1664525u + 1013904223u; return uint8_t(seed >> 24); };
            for (int by = 0; by < H; by += 2)
                for (int bx = 0; bx < W; bx += 2) {
                    const uint8_t c[3] = {next(), next(), next()};
                    for (int dy = 0; dy < 2; ++dy)
                        for (int dx = 0; dx < 2; ++dx) {
                            uint8_t* p = &rgba[((by + dy) * W + bx + dx) * 4];
                            p[0] = c[0]; p[1] = c[1]; p[2] = c[2]; p[3] = 255;
                        }
                }

            // GPU: upload, encode into a packed R8 target, read the packed NV12 back.
            const SkImageInfo rgba_info = SkImageInfo::Make(W, H, kRGBA_8888_SkColorType, kPremul_SkAlphaType);
            auto source = openshot::GpuFrame::Create(W, H);
            auto packed = openshot::GpuFrame::Create(W, H + H / 2, kR8_unorm_SkColorType);
            std::vector<uint8_t> nv12(W * (H + H / 2));
            const bool drawn = source && packed &&
                source->upload(SkPixmap(rgba_info, rgba.data(), W * 4)) &&
                openshot::GpuYuv::EncodeNV12(source->snapshot(), packed->canvas(), W, H,
                                             openshot::GpuYuv::Matrix::BT601) &&
                packed->readback(SkPixmap(SkImageInfo::Make(W, H + H / 2, kR8_unorm_SkColorType,
                                                            kOpaque_SkAlphaType), nv12.data(), W));
            if (!drawn) {
                checks.push_back({"gpu_encode_nv12", false, "the GPU pass did not run"});
                return;
            }

            // swscale, both ways the writer can be configured.
            const auto sws = [&](int flags) {
                std::vector<uint8_t> out(W * (H + H / 2));
                SwsContext* ctx = sws_getContext(W, H, AV_PIX_FMT_RGBA, W, H, AV_PIX_FMT_NV12,
                                                 flags, nullptr, nullptr, nullptr);
                const uint8_t* src[1] = {rgba.data()};
                const int src_stride[1] = {W * 4};
                uint8_t* dst[2] = {out.data(), out.data() + W * H};
                const int dst_stride[2] = {W, W};
                sws_scale(ctx, src, src_stride, 0, H, dst, dst_stride);
                sws_freeContext(ctx);
                return out;
            };
            const auto worst = [&](const std::vector<uint8_t>& a, int from, int to) {
                int m = 0;
                for (int i = from; i < to; ++i) m = std::max(m, std::abs(int(a[i]) - int(nv12[i])));
                return m;
            };
            // Exact reference: BT.601 limited, 2x2 box chroma, rounded.
            std::vector<uint8_t> exact(W * (H + H / 2));
            const double kr = 0.299, kb = 0.114, kg = 1.0 - kr - kb;
            const auto px = [&](int x, int y, int c) { return rgba[(y * W + x) * 4 + c] / 255.0; };
            for (int y = 0; y < H; ++y)
                for (int x = 0; x < W; ++x)
                    exact[y * W + x] = uint8_t(std::lround(16 + 219 * (kr * px(x, y, 0) + kg * px(x, y, 1) + kb * px(x, y, 2))));
            for (int cy = 0; cy < H / 2; ++cy)
                for (int cx = 0; cx < W / 2; ++cx) {
                    double r = 0, g = 0, b = 0;
                    for (int d = 0; d < 4; ++d) {
                        r += px(2 * cx + d % 2, 2 * cy + d / 2, 0) / 4;
                        g += px(2 * cx + d % 2, 2 * cy + d / 2, 1) / 4;
                        b += px(2 * cx + d % 2, 2 * cy + d / 2, 2) / 4;
                    }
                    const double yy = kr * r + kg * g + kb * b;
                    exact[W * H + cy * W + 2 * cx] = uint8_t(std::lround(128 + 224 * (b - yy) / (2 * (1 - kb))));
                    exact[W * H + cy * W + 2 * cx + 1] = uint8_t(std::lround(128 + 224 * (r - yy) / (2 * (1 - kr))));
                }
            const auto bicubic = sws(SWS_BICUBIC), fast = sws(SWS_FAST_BILINEAR);
            const int vs_exact = worst(exact, 0, W * (H + H / 2));
            const int luma = worst(bicubic, 0, W * H);
            const int chroma_box = worst(fast, W * H, W * (H + H / 2));
            const int chroma_bicubic = worst(bicubic, W * H, W * (H + H / 2));

            // Round trip through the decoder's conversion.
            openshot::GpuYuvPlane planes[2];
            planes[0] = {nv12.data(), W, W, H};
            planes[1] = {nv12.data() + W * H, W, W / 2, H / 2};
            auto back = openshot::GpuYuv::Convert(openshot::GpuYuv::Layout::NV12,
                                                  openshot::GpuYuv::Matrix::BT601, false, planes, 2, W, H);
            std::vector<uint8_t> again(W * H * 4);
            int round_trip = 255;
            if (back && back->readback(SkPixmap(rgba_info, again.data(), W * 4))) {
                round_trip = 0;
                for (int i = 0; i < W * H * 4; ++i)
                    if (i % 4 != 3) round_trip = std::max(round_trip, std::abs(int(again[i]) - int(rgba[i])));
            }

            char detail[256];
            std::snprintf(detail, sizeof(detail),
                          "vs exact BT.601 %d (gate 1); RGBA->NV12->RGBA %d (gate 2, the 8-bit "
                          "ideal); swscale: luma %d, chroma FAST_BILINEAR %d, BICUBIC %d",
                          vs_exact, round_trip, luma, chroma_box, chroma_bicubic);
            checks.push_back({"gpu_encode_nv12", vs_exact <= 1 && round_trip <= 2, detail});
        });

    // Crop on the GPU (W29, Settings::GPU_CROP) against the QPainter path, on the same frame.
    //
    // The two rasterisers antialias the rounded corners and the fractional edges differently --
    // that is why the flag is off by default -- so the comparison is split: pixels within 2 px
    // of the crop's outline are reported, pixels further inside or outside are gated. There the
    // two must agree: the interior is the same bilinear copy, the outside is transparent.
    // The frame must also stay on the GPU: a crop that read it back would pass on pixels.
    addCustom("unit.gpu_crop", {"unit", "gpu"}, unitScene,
        [](Scene&, std::vector<Captured>&, std::vector<Check>& checks) {
            if (!openshot::GpuDevice::Instance().available()) {
                checks.push_back({"gpu_crop", true, "no GPU: the QPainter path runs (declines)"});
                return;
            }
            const int W = 320, H = 200;
            auto image = std::make_shared<QImage>(W, H, QImage::Format_RGBA8888_Premultiplied);
            for (int y = 0; y < H; ++y)
                for (int x = 0; x < W; ++x) {
                    uint8_t* p = image->scanLine(y) + x * 4;
                    p[0] = uint8_t(x * 255 / W); p[1] = uint8_t(y * 255 / H);
                    p[2] = uint8_t((x ^ y) & 255); p[3] = 255;
                }
            struct Case { double l, t, r, b, radius, sx, sy; const char* name; };
            const Case cases[] = {
                {0.0, 0.0, 0.0, 0.0, 0.3, 0.0, 0.0, "radius only"},
                {0.1, 0.05, 0.2, 0.1, 0.0, 0.0, 0.0, "fractional rect"},
                {0.13, 0.07, 0.11, 0.21, 0.5, 0.0, 0.0, "fractional + radius"},
                {0.1, 0.1, 0.1, 0.1, 0.2, 0.05, -0.03, "shifted"},
            };
            openshot::Settings* settings = openshot::Settings::Instance();
            const bool previous = settings->GPU_CROP;
            int worst_inside = 0, band_worst = 0;
            bool stayed_on_gpu = true;
            for (const Case& c : cases) {
                const auto run = [&](bool on_gpu) {
                    settings->GPU_CROP = on_gpu;
                    openshot::Crop crop(openshot::Keyframe(c.l), openshot::Keyframe(c.t),
                                        openshot::Keyframe(c.r), openshot::Keyframe(c.b),
                                        openshot::Keyframe(c.radius));
                    crop.resize = false;
                    crop.x = openshot::Keyframe(c.sx);
                    crop.y = openshot::Keyframe(c.sy);
                    auto frame = std::make_shared<openshot::Frame>(1, W, H, "#000000");
                    auto gpu = openshot::GpuFrame::Create(W, H);
                    gpu->upload(SkPixmap(SkImageInfo::Make(W, H, kRGBA_8888_SkColorType, kPremul_SkAlphaType),
                                         image->constBits(), image->bytesPerLine()));
                    frame->AttachGpuFrame(gpu);
                    crop.GetFrame(frame, 1);
                    if (on_gpu && !frame->IsGpuBacked()) stayed_on_gpu = false;
                    return std::make_shared<QImage>(frame->GetImage()->copy());
                };
                const auto cpu = run(false), gpu = run(true);
                // The outline, in pixels, as both paths compute it.
                const double pl = c.l * W, pt = c.t * H;
                const double pr = pl + (1 - c.l - c.r) * W, pb = pt + (1 - c.t - c.b) * H;
                const double r = std::clamp(c.radius, 0.0, 1.0) * std::min(pr - pl, pb - pt) * 0.5;
                const auto near_outline = [&](int x, int y) {
                    const double fx = x + 0.5, fy = y + 0.5;
                    const double cx = std::clamp(fx, pl + r, pr - r), cy = std::clamp(fy, pt + r, pb - r);
                    const double d = std::hypot(fx - cx, fy - cy) - r;          // outside the rounded rect
                    const double inside = std::min({fx - pl, pr - fx, fy - pt, pb - fy});
                    return std::abs(d) <= 2.0 || (d <= 0 && inside <= 2.0);
                };
                for (int y = 0; y < H; ++y)
                    for (int x = 0; x < W; ++x) {
                        const uint8_t* a = cpu->constScanLine(y) + x * 4;
                        const uint8_t* b = gpu->constScanLine(y) + x * 4;
                        int d = 0;
                        for (int k = 0; k < 4; ++k) d = std::max(d, std::abs(int(a[k]) - int(b[k])));
                        (near_outline(x, y) ? band_worst : worst_inside) =
                            std::max(near_outline(x, y) ? band_worst : worst_inside, d);
                    }
            }
            settings->GPU_CROP = previous;
            char detail[192];
            std::snprintf(detail, sizeof(detail),
                          "away from the outline: max delta %d (gate 2); within 2 px of it: %d "
                          "(rasteriser, not gated); stayed on the GPU: %s",
                          worst_inside, band_worst, stayed_on_gpu ? "yes" : "no");
            checks.push_back({"gpu_crop", worst_inside <= 2 && stayed_on_gpu, detail});
        });

    // Per-export telemetry (W31): the counters move with the work, and the report carries the
    // GPU figures where there is an NVIDIA driver and says so where there is not.
    addCustom("unit.telemetry", {"unit"}, unitScene,
        [](Scene& s, std::vector<Captured>&, std::vector<Check>& checks) {
            openshot::ExportTelemetry telemetry;
            telemetry.Start();
            openshot::FFmpegReader reader(s.media("clip_a_640x360_30.mp4"));
            reader.Open();
            for (int i = 1; i <= 10; ++i) reader.GetFrame(i)->GetImage();
            reader.Close();
            std::this_thread::sleep_for(std::chrono::milliseconds(600));   // a couple of samples
            telemetry.SetFrames(10);
            const openshot::ExportTelemetry::Report r = telemetry.Stop();
            const unsigned long long decoded = r.counters[openshot::GpuCounters::DecodedOnCpu] +
                                               r.counters[openshot::GpuCounters::DecodedOnGpu] +
                                               r.counters[openshot::GpuCounters::DecodedOnDevice];
            const bool driver = openshot::ExportTelemetry::NvmlBuiltIn() &&
                                std::ifstream("/proc/driver/nvidia/version").good();
            const bool ok = decoded >= 10 && r.frames == 10 && r.seconds > 0.5 &&
                            (!driver || (r.nvml && r.samples >= 2));
            checks.push_back({"telemetry_report", ok, r.Summary()});
        });

    // Hardware decode must not take the process with it.
    //
    // With HARDWARE_DECODER set, FFmpegReader used to throw on the first frame
    // (OutOfMemory: "Failed to initialize sws context", because swscale was configured from
    // pCodecCtx->pix_fmt, which is AV_PIX_FMT_CUDA once NVDEC is on) -- and then abort the whole
    // process, because Close() drains the decoder through the same call and ~FFmpegReader let the
    // throw escape a destructor. Both are fixed; this is the guard.
    //
    // It passes either way on purpose. On a machine with no NVDEC the reader falls back to
    // software and still decodes, which is the supported answer; what is being asserted is that
    // asking for hardware decode never throws and never aborts.
    addCustom("unit.hardware_decode", {"unit"}, unitScene,
        [](Scene& s, std::vector<Captured>&, std::vector<Check>& checks) {
            openshot::Settings* settings = openshot::Settings::Instance();
            const int previous = settings->HARDWARE_DECODER;
            settings->HARDWARE_DECODER = 2;   // CUDA / NVDEC

            const int wanted = 8;
            int decoded = 0;
            std::string detail;
            bool threw = false;
            try {
                openshot::FFmpegReader reader(s.media("clip_a_640x360_30.mp4"));
                reader.Open();
                for (int i = 1; i <= wanted; ++i)
                    if (reader.GetFrame(i)) ++decoded;
                reader.Close();
            } catch (const std::exception& e) {
                threw = true;
                detail = std::string("threw: ") + e.what();
            } catch (...) {
                threw = true;
                detail = "threw a non-std exception";
            }
            // Restore before anything else runs: Settings is process-wide and every later
            // scenario would otherwise decode with hardware acceleration asked for.
            settings->HARDWARE_DECODER = previous;

            const bool ok = !threw && decoded == wanted;
            if (!threw)
                detail = std::to_string(decoded) + " of " + std::to_string(wanted) +
                         " frames decoded with HARDWARE_DECODER=2";
            checks.push_back({"hardware_decode_no_throw", ok, detail});
        });

    // unit.gpu_grain gates Enhancement's grain on the GPU. It cannot be gated byte for byte: the
    // hash is float in the fragment and double in the C++, so the GPU grain is the same noise at a
    // different random phase (owner decision 2026-09-24; src/shaders/enhancement.sksl). What is
    // held instead is what the grain looks like -- its spread and mean against the CPU twin's on
    // the same frame -- that it has no spatial structure (float sin() at large arguments is where
    // a hash like this goes wrong), that alpha is untouched, and that the service's whole ADJUSTMENT
    // configuration (grain + clarity + blur-mix) ran as three shader passes with no readback.
    addCustom("unit.gpu_grain", {"unit", "gpu"}, unitScene,
        [](Scene&, std::vector<Captured>&, std::vector<Check>& checks) {
            constexpr int kW = 640, kH = 360;
            QImage src(kW, kH, QImage::Format_RGBA8888_Premultiplied);
            for (int y = 0; y < kH; ++y) {
                uchar* row = src.scanLine(y);
                for (int x = 0; x < kW; ++x) {
                    const int a = y < kH / 2 ? 255 : 160;          // an opaque and a translucent band
                    const int v = x * 255 / (kW - 1);               // shadows to highlights
                    row[x * 4 + 0] = static_cast<uchar>(v * a / 255);
                    row[x * 4 + 1] = static_cast<uchar>((255 - v) * a / 255 / 2);
                    row[x * 4 + 2] = static_cast<uchar>(128 * a / 255);
                    row[x * 4 + 3] = static_cast<uchar>(a);
                }
            }
            const auto run = [&](double noise, double clarity, double sharpness) {
                auto frame = std::make_shared<openshot::Frame>();
                frame->AddImage(std::make_shared<QImage>(src.copy()));
                openshot::Enhancement fx{openshot::Keyframe(noise), openshot::Keyframe(clarity),
                                         openshot::Keyframe(sharpness)};
                fx.GetFrame(frame, 1);
                return frame->GetImage()->copy();
            };
            struct Stats { double mean, sd, corr; };
            const auto stats = [&](const QImage& out) {
                std::vector<double> d(static_cast<std::size_t>(kW) * kH);
                double sum = 0, sum2 = 0; long n = 0;
                for (int y = 0; y < kH; ++y)
                    for (int x = 0; x < kW; ++x) {
                        double px = 0;
                        for (int c = 0; c < 3; ++c) {
                            const double v = double(out.scanLine(y)[x * 4 + c]) - double(src.scanLine(y)[x * 4 + c]);
                            sum += v; sum2 += v * v; n++; px += v;
                        }
                        d[static_cast<std::size_t>(y) * kW + x] = px / 3;
                    }
                const double mean = sum / n;
                double pm = 0; for (double v : d) pm += v; pm /= double(d.size());
                double num = 0, den = 0;
                for (int y = 0; y < kH; ++y)
                    for (int x = 0; x + 1 < kW; ++x) {
                        const double a = d[static_cast<std::size_t>(y) * kW + x] - pm;
                        const double b = d[static_cast<std::size_t>(y) * kW + x + 1] - pm;
                        num += a * b; den += a * a;
                    }
                return Stats{mean, std::sqrt(std::max(0.0, sum2 / n - mean * mean)), den > 0 ? num / den : 0};
            };
            const auto alphaKept = [&](const QImage& out) {
                for (int y = 0; y < kH; ++y)
                    for (int x = 0; x < kW; ++x)
                        if (out.scanLine(y)[x * 4 + 3] != src.scanLine(y)[x * 4 + 3]) return false;
                return true;
            };
            char buf[256];

            if (!openshot::GpuDevice::Instance().available()) {
                // The CPU path, which ships: it must still run and still be deterministic.
                openshot::GpuEffect::ResetCounters();
                const QImage a = run(0.67, 0.0, 0.0), b = run(0.67, 0.0, 0.0);
                const bool same = a == b;
                checks.push_back({"grain_on_cpu_deterministic",
                                  same && openshot::GpuEffect::GpuPasses() == 0 && alphaKept(a),
                                  same ? "no GPU: CPU grain, identical across runs" : "CPU grain differs between runs"});
                return;
            }

            // The service's configuration, on the GPU: three passes, no fallback.
            openshot::GpuEffect::ResetCounters();
            const QImage gpu_all = run(0.67, 0.68, -0.58);
            const long long passes = openshot::GpuEffect::GpuPasses();
            const long long fallbacks = openshot::GpuEffect::CpuFallbacks();
            std::snprintf(buf, sizeof buf, "gpu_passes=%lld cpu_fallbacks=%lld", passes, fallbacks);
            checks.push_back({"adjustment_ran_as_shaders", passes == 3 && fallbacks == 0, buf});

            const QImage gpu = run(0.67, 0.0, 0.0);
            const openshot::GpuDevice::Backend backend = openshot::GpuDevice::RequestedBackend();
            openshot::GpuDevice::SetBackend(openshot::GpuDevice::Backend::Off);
            const QImage cpu = run(0.67, 0.0, 0.0);
            openshot::GpuDevice::SetBackend(backend);

            const Stats sg = stats(gpu), sc = stats(cpu);
            const bool spread = std::abs(sg.sd / sc.sd - 1.0) <= 0.05 && std::abs(sg.mean - sc.mean) <= 0.5;
            std::snprintf(buf, sizeof buf, "sd gpu %.2f cpu %.2f, mean gpu %+.2f cpu %+.2f", sg.sd, sc.sd, sg.mean, sc.mean);
            checks.push_back({"grain_matches_cpu_statistically", spread, buf});
            std::snprintf(buf, sizeof buf, "neighbour correlation gpu %+.3f cpu %+.3f", sg.corr, sc.corr);
            checks.push_back({"grain_has_no_structure", std::abs(sg.corr) <= 0.1, buf});
            checks.push_back({"grain_keeps_alpha", alphaKept(gpu) && alphaKept(gpu_all),
                              alphaKept(gpu) && alphaKept(gpu_all) ? "alpha untouched" : "alpha changed"});
        });

    // unit.effect_plan guards the shared effect planner (image-processing-lib/src/Planner), which
    // the editor calls through the WASM (planEffect) and every GPU effect here calls natively. The
    // pixels it leads to are held by the effect goldens and openshot-gpu-effect-parity; this holds
    // what they cannot see: that every name resolves, that the JSON the editor parses is JSON, and
    // that the three short-circuits -- identity, clear, cpu -- are taken where the C++ takes them.
    addCustom("unit.effect_plan", {"unit"}, unitScene,
        [](Scene&, std::vector<Captured>&, std::vector<Check>& checks) {
            namespace fx = Podcastle::Effects;
            // Every parameter the planner reads, at a value that takes most effects off their
            // identity branch, so the JSON below carries uniforms, children and textures.
            const fx::EffectParams busy = {
                {"alpha", 0.5}, {"alphaX", 0.1}, {"alphaY", -0.1}, {"anchorX", 0.3}, {"anchorY", 0.6},
                {"angle", 20}, {"blueX", -0.05}, {"blueY", 0.05}, {"bottom", 0.1}, {"brightness", 0.2},
                {"circleRadius", 0.5}, {"contrast", 4}, {"diagonalRadius", 12}, {"dx", 0.2}, {"dy", -0.1},
                {"enabled", 1}, {"exposure", 1.5}, {"greenX", 0.02}, {"greenY", -0.02},
                {"highPercent", 60}, {"horizontalDisplacement", 0.1}, {"horizontalRadius", 10},
                {"isHorizontal", 1}, {"left", 0.1}, {"lowPercent", 40}, {"redX", 0.05}, {"redY", 0.0},
                {"right", 0.1}, {"rotationalRadius", 8}, {"shiftAmount", 0.3}, {"shiftPoint", 0.5},
                {"top", 0.1}, {"verticalDisplacement", 0.1}, {"verticalRadius", 6},
                {"zoomBlurRadius", 20}, {"zoomPercent", 130}};
            constexpr int kW = 320, kH = 180;

            std::string unresolved, malformed;
            std::size_t plans = 0;
            for (const std::string& name : fx::plannedEffects()) {
                for (const fx::EffectParams* params : {&busy, static_cast<const fx::EffectParams*>(nullptr)}) {
                    const fx::EffectPlan plan =
                        fx::planEffect(name, params ? *params : fx::EffectParams{}, kW, kH, kW, kH);
                    ++plans;
                    if (!plan.known || plan.steps.empty())
                        unresolved += " " + name;
                    // Strict: the editor runs JSON.parse, which forgives nothing jsoncpp's
                    // default reader would.
                    Json::CharReaderBuilder builder;
                    Json::CharReaderBuilder::strictMode(&builder.settings_);
                    std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
                    const std::string text = plan.toJson();
                    Json::Value root;
                    std::string errors;
                    const bool parsed = reader->parse(text.data(), text.data() + text.size(), &root, &errors);
                    if (!parsed || root["effect"].asString() != name ||
                        root["steps"].size() != plan.steps.size() ||
                        root["textures"].size() != plan.textures.size())
                        malformed += " " + name + (params ? "(busy)" : "(defaults)");
                }
            }
            checks.push_back({"every_effect_resolves", unresolved.empty(),
                              unresolved.empty()
                                  ? std::to_string(fx::plannedEffects().size()) + " effects"
                                  : "unresolved:" + unresolved});
            checks.push_back({"json_well_formed", malformed.empty(),
                              malformed.empty() ? std::to_string(plans) + " plans parse strictly"
                                                : "malformed:" + malformed});

            // The short-circuits. Each is where the C++ twin returns early or hands off, and a plan
            // that missed one would draw a pass the export never drew.
            struct Expect { const char* label; const char* effect; fx::EffectParams params;
                            fx::PlanStep::Kind kind; };
            const Expect expects[] = {
                {"ZOOM 80 -> cpu", "ZOOM", {{"zoomPercent", 80}, {"anchorX", 0.5}, {"anchorY", 0.5}},
                 fx::PlanStep::Kind::Cpu},
                {"ZOOM 99.9 -> cpu", "ZOOM", {{"zoomPercent", 99.9}}, fx::PlanStep::Kind::Cpu},
                {"ALPHA 0 -> clear", "ALPHA", {{"alpha", 0}}, fx::PlanStep::Kind::Clear},
                {"ALPHA -1 -> clear", "ALPHA", {{"alpha", -1}}, fx::PlanStep::Kind::Clear},
                {"BLUR zero radii -> identity", "BLUR",
                 {{"horizontalRadius", 0}, {"verticalRadius", 0}, {"diagonalRadius", 0},
                  {"zoomBlurRadius", 0}, {"rotationalRadius", 0}},
                 fx::PlanStep::Kind::Identity},
                {"BLUR no params -> identity", "BLUR", {}, fx::PlanStep::Kind::Identity},
                // and the other side of each, so a planner that always short-circuits fails too
                {"ZOOM 130 -> gpu", "ZOOM", {{"zoomPercent", 130}}, fx::PlanStep::Kind::Gpu},
                {"ALPHA 0.5 -> gpu", "ALPHA", {{"alpha", 0.5}}, fx::PlanStep::Kind::Gpu},
                {"BLUR 10 -> gpu", "BLUR", {{"horizontalRadius", 10}}, fx::PlanStep::Kind::Gpu},
            };
            std::string wrong;
            for (const Expect& e : expects) {
                const fx::EffectPlan plan = fx::planEffect(e.effect, e.params, kW, kH);
                if (plan.steps.size() != 1 || plan.steps.front().kind != e.kind)
                    wrong += std::string(wrong.empty() ? "" : "; ") + e.label;
            }
            const fx::EffectPlan cpu = fx::planEffect("ZOOM", {{"zoomPercent", 80}}, kW, kH);
            if (cpu.steps.size() == 1 && cpu.steps.front().cpu.function != "applyZoomEffect")
                wrong += std::string(wrong.empty() ? "" : "; ") + "ZOOM 80 names " +
                         cpu.steps.front().cpu.function;
            checks.push_back({"identity_clear_cpu", wrong.empty(),
                              wrong.empty() ? std::to_string(sizeof(expects) / sizeof(expects[0])) +
                                                  " cases as expected"
                                            : wrong});
        });

    addCustom("unit.color", {"unit"}, unitScene,
        [](Scene&, std::vector<Captured>&, std::vector<Check>& checks) {
            checkTable("named_colors", kNamedCases,
                       sizeof(kNamedCases) / sizeof(kNamedCases[0]), checks);
            checkTable("hex_rgb_and_malformed", kFormCases,
                       sizeof(kFormCases) / sizeof(kFormCases[0]), checks);

            // Named lookup is case-insensitive, as QColor's was.
            std::size_t case_failures = 0;
            for (const ColorCase& c : kNamedCases) {
                std::string upper(c.input);
                for (char& ch : upper) ch = static_cast<char>(toupper(static_cast<unsigned char>(ch)));
                const std::vector<int> got = openshot::Color(upper).GetColorRGBA(0);
                if (got[0] != c.r || got[1] != c.g || got[2] != c.b || got[3] != c.a) ++case_failures;
            }
            checks.push_back({"named_colors_uppercase", case_failures == 0,
                              case_failures == 0 ? "all match"
                                                 : std::to_string(case_failures) + " differ"});

            // GetColorHex is "#rrggbb", lowercase, alpha dropped -- exactly QColor::name().
            std::size_t hex_failures = 0;
            std::string first_hex_failure;
            for (int v = 0; v < 256; ++v) {
                openshot::Color c(static_cast<unsigned char>(v),
                                  static_cast<unsigned char>(255 - v),
                                  static_cast<unsigned char>((v * 7) % 256),
                                  static_cast<unsigned char>((v * 13) % 256));
                char want[8];
                std::snprintf(want, sizeof(want), "#%02x%02x%02x", v, 255 - v, (v * 7) % 256);
                const std::string got = c.GetColorHex(0);
                if (got != want && hex_failures++ == 0)
                    first_hex_failure = std::string("expected ") + want + " got " + got;
            }
            checks.push_back({"hex_round_trip", hex_failures == 0,
                              hex_failures == 0 ? "256 colours match"
                                                : first_hex_failure});

            // Parsing a colour and formatting it back must be a fixed point for "#rrggbb".
            std::size_t round_trip_failures = 0;
            for (const ColorCase& c : kNamedCases) {
                openshot::Color parsed{std::string(c.input)};
                const std::string hex = parsed.GetColorHex(0);
                const std::vector<int> again = openshot::Color(hex).GetColorRGBA(0);
                if (again[0] != c.r || again[1] != c.g || again[2] != c.b) ++round_trip_failures;
            }
            checks.push_back({"name_to_hex_to_rgb", round_trip_failures == 0,
                              round_trip_failures == 0 ? "all match"
                                                       : std::to_string(round_trip_failures) + " differ"});
        });
}
