// Clip-level drop shadow and blur (podcastle additions to Clip).
#include "Recipes.h"

#include "Timeline.h"

#include <cmath>
#include <filesystem>
#include <string>

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

// EFFECT_SIZE_CANVAS, as the service sets it on every visual clip: the same square shown 80 px
// wide on the 640x360 canvas from a 32 px, a 120 px and an 800 px source (shown native, native
// and downscaled to 360 px by the reader). In canvas pixels all three must get the same shadow.
const int kSquareSources[] = {32, 120, 800};
constexpr int kSquareOnCanvas = 80;

std::string whiteSquare(int size) {
    const auto path = std::filesystem::temp_directory_path()
                      / ("golden_white_square_" + std::to_string(size) + ".png");
    savePng(path.string(), Image::blank(size, size, 255, 255, 255, 255));
    return path.string();
}

// A white square kSquareOnCanvas px wide centred at (cx, cy) of the canvas.
openshot::Clip* squareClip(Scene& s, int source, float cx, float cy, Transform t = {}) {
    MediaSpec m; m.path = whiteSquare(source); m.isImage = true; m.end = 3.0;
    t.bbox = BBox{cx, cy, float(kSquareOnCanvas) / s.width, float(kSquareOnCanvas) / s.height};
    m.transform = t;
    auto* c = mediaClip(s, m);
    c->effect_size_space = openshot::EFFECT_SIZE_CANVAS;
    return c;
}

void redShadow(openshot::Clip* c, double blur, double distance, double angle = 0.0) {
    c->Shadow(true);
    c->shadow_color = openshot::Color(255, 0, 0, 255);
    c->shadow_blur = openshot::Keyframe(blur);
    c->shadow_distance = openshot::Keyframe(distance);
    c->shadow_angle = openshot::Keyframe(angle);
}

bool isWhite(const uint8_t* p) { return p[0] > 200 && p[1] > 200 && p[2] > 200; }
bool isRed(const uint8_t* p)   { return p[0] > 128 && p[1] < 64 && p[2] < 64; }

// Along row y, from x0 rightwards: (last white column, last red column), -1 where none.
std::pair<int, int> rightEdges(const Image& img, int x0, int x1, int y) {
    int white = -1, red = -1;
    for (int x = std::max(0, x0); x < std::min(img.w, x1); ++x) {
        if (isWhite(img.px(x, y))) white = x;
        if (isRed(img.px(x, y))) red = x;
    }
    return {white, red};
}

// Along row y, the first column right of x0 where the red channel falls below half of `peak`.
int halfRedFalloff(const Image& img, int x0, int x1, int y) {
    int peak = 0;
    for (int x = x0; x < x1; ++x) if (!isWhite(img.px(x, y))) peak = std::max(peak, int(img.px(x, y)[0]));
    for (int x = x1 - 1; x >= x0; --x) if (img.px(x, y)[0] * 2 >= peak) return x;
    return -1;
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

    // Canvas-pixel sizes (the service's mode): the same blurred shadow and clip blur whatever the
    // source's resolution. Captured for the eye; the check holds the shadows' reach together.
    addCustom("clipfx.canvas_effect_size", {"clipfx", "shadow", "blur", "gpu-composite"},
        [](Scene& s) {
            auto& tl = s.makeTimeline();
            float cx = 0.2f;
            for (int source : kSquareSources) {
                auto* c = squareClip(s, source, cx, 0.4f);
                redShadow(c, 16.0, 30.0, 20.0);
                c->Blur(true);
                c->blur_amount = openshot::Keyframe(6.0);
                tl.AddClip(c);
                cx += 0.3f;
            }
            tl.Open();
        },
        [](Scene& s, std::vector<Captured>& out, std::vector<Check>& checks) {
            const Image img = fromFrame(s.timeline->GetFrame(30));
            out.push_back({"f000030", img});
            const int y = int(0.4f * s.height);
            std::string reach;
            int lo = img.w, hi = -1;
            for (int i = 0; i < 3; ++i) {
                const int cx = int((0.2f + 0.3f * i) * s.width);
                const int edge = halfRedFalloff(img, cx, cx + kSquareOnCanvas + 40, y) - cx;
                reach += std::to_string(kSquareSources[i]) + "px:" + std::to_string(edge) + " ";
                lo = std::min(lo, edge);
                hi = std::max(hi, edge);
            }
            checks.push_back({"same_shadow_reach", hi - lo <= 2,
                              "shadow half-falloff from the square's centre by source size: " + reach});
        });

    // The rule itself, measured with sharp shadows 30 px to the right: in canvas pixels the gap
    // between a square's edge and its shadow's is 30 whatever the source size (top row), after a
    // 90-degree rotation (the shadow keeps its canvas direction) and while the clip's scale
    // animates. The default, source pixels, scales it with the clip: 30 * 80 / 120 = 20.
    addCustom("unit.clip_effect_size_space", {"unit", "clipfx", "shadow"},
        [](Scene& s) {
            auto& tl = s.makeTimeline();
            float cx = 0.2f;
            for (int source : kSquareSources) {
                auto* c = squareClip(s, source, cx, 0.3f);
                redShadow(c, 0.0, 30.0);
                tl.AddClip(c);
                cx += 0.3f;
            }
            Transform rotated; rotated.rotation = 90.f;
            auto* r = squareClip(s, 120, 0.2f, 0.75f, rotated);
            redShadow(r, 0.0, 30.0);
            tl.AddClip(r);
            Transform growing; growing.hScale = growing.vScale = 0.5f;
            growing.scaleKfs.push_back(ScaleKf{2.0, 1.f, 1.f, Interp{openshot::LINEAR}});
            auto* g = squareClip(s, 120, 0.5f, 0.75f, growing);
            redShadow(g, 0.0, 30.0);
            tl.AddClip(g);
            auto* src = squareClip(s, 120, 0.8f, 0.75f);
            src->effect_size_space = openshot::EFFECT_SIZE_SOURCE;
            redShadow(src, 0.0, 30.0);
            tl.AddClip(src);
            tl.Open();
        },
        [](Scene& s, std::vector<Captured>&, std::vector<Check>& checks) {
            auto gap = [&](const Image& img, float cx, float cy) {
                const int x = int(cx * s.width);
                const auto [white, red] = rightEdges(img, x - kSquareOnCanvas, x + kSquareOnCanvas + 60,
                                                     int(cy * s.height));
                return (white < 0 || red < 0) ? -1 : red - white;
            };
            auto expect = [&](const std::string& name, int got, int want) {
                checks.push_back({name, std::abs(got - want) <= 1,
                                  "gap " + std::to_string(got) + " px, want " + std::to_string(want)});
            };
            const Image first = fromFrame(s.timeline->GetFrame(1));
            const Image late = fromFrame(s.timeline->GetFrame(75));
            expect("canvas_32px_source", gap(first, 0.2f, 0.3f), 30);
            expect("canvas_120px_source", gap(first, 0.5f, 0.3f), 30);
            expect("canvas_800px_source", gap(first, 0.8f, 0.3f), 30);
            expect("canvas_rotated_keeps_direction", gap(first, 0.2f, 0.75f), 30);
            expect("canvas_scale_half", gap(first, 0.5f, 0.75f), 30);
            expect("canvas_scale_full", gap(late, 0.5f, 0.75f), 30);
            expect("source_default_scales_with_clip", gap(first, 0.8f, 0.75f), 20);
        });
}
