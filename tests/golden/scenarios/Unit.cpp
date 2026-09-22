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
// To regenerate after an intentional change, see doc/gpu-migration/GPU-WORKLIST.md W18: build
// a small program that links Qt, run the recorded inputs through the old parser, and paste
// the results back here.

#include "Recipes.h"

#include "Color.h"
#include "FFmpegReader.h"
#include "GpuEffect.h"
#include "Settings.h"
#include "Timeline.h"
#include "effects/Blur.h"
#include "effects/Brightness.h"
#include "effects/ColorMap.h"
#include "gpu/GpuDevice.h"
#include "gpu/GpuOverlay.h"

#include "effects/image-processing-lib/src/Effects/effects.h"

#include "Frame.h"

#include <QImage>

#include <memory>
#include <string>
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
