// Compositing: all 16 blend modes, opacity curves (fade + ghost), layer order.
#include "Recipes.h"

#include "BlendModes.h"
#include "Timeline.h"

#include <algorithm>
#include <string>
#include <vector>

using namespace golden;
using namespace golden::recipes;

namespace {

std::string slug(std::string s) {
    for (auto& c : s) { c = static_cast<char>(std::tolower(c)); if (c == '-' || c == ' ') c = '_'; }
    return s;
}

} // namespace

void golden::registerCompositingScenarios() {
    // Timeline background colour. Two paths produce it and they must agree: on the CPU
    // Timeline::GetFrame calls Frame::AddColor with GetColorHex(), and on the GPU it clears
    // the pooled canvas with the same colour. W18 took QColor out of the second one, and
    // nothing in the suite covered it until these two scenarios -- every other scenario
    // leaves the background black, which is the one case the code skips entirely.
    add("compositing.timeline_background_color", {"compositing", "exact", "gpu-composite"}, {15},
        [](Scene& s) {
            auto& tl = s.makeTimeline();
            tl.color = openshot::Color(std::string("#3c78d8"));
            MediaSpec m; m.path = s.media("clip_a_640x360_30.mp4");
            m.transform = Transform{BBox{0.5f, 0.5f, 0.5f, 0.5f}};
            tl.AddClip(mediaClip(s, m));
            tl.Open();
        });

    // An animated background exercises the other half of the has_background_color test,
    // the GetCount() > 1 branch, and proves the colour is sampled at the requested frame.
    add("compositing.timeline_background_animated", {"compositing", "exact", "gpu-composite"}, {1, 30},
        [](Scene& s) {
            auto& tl = s.makeTimeline();
            tl.color.red = openshot::Keyframe(0.0);
            tl.color.red.AddPoint(30.0, 255.0);
            tl.color.green = openshot::Keyframe(160.0);
            tl.color.blue = openshot::Keyframe(255.0);
            tl.color.blue.AddPoint(30.0, 0.0);
            MediaSpec m; m.path = s.media("clip_a_640x360_30.mp4");
            m.transform = Transform{BBox{0.5f, 0.5f, 0.5f, 0.5f}};
            tl.AddClip(mediaClip(s, m));
            tl.Open();
        });

    for (int mode = 0; mode < openshot::BLEND_MODE_COUNT; ++mode) {
        const auto bm = static_cast<openshot::BlendMode>(mode);
        // Since W13 every mode composites on the GPU via SkBlendMode, so none of them can
        // match the CPU goldens bit-for-bit: the clip is resampled onto the canvas with
        // Skia's bilinear rather than QPainter's smooth transform. They stay exact on the
        // CPU, where nothing has changed. That the *formula* mapping is right is checked
        // separately and far more sharply by openshot-gpu-blend-parity, which blends
        // identical pixels with no resampling at all.
        std::vector<std::string> tags{"compositing", "blend", "exact", "gpu-composite"};
        // Colour-burn divides by the source channel; hue and saturation renormalise chroma.
        // All three turn the ~1 LSB resampling difference into a large one, so on GPU they
        // need a band too wide to catch a real regression -- openshot-gpu-blend-parity is
        // what actually gates them. See Tolerance::GpuAmplified().
        if (bm == openshot::BLEND_COLOR_BURN || bm == openshot::BLEND_HUE ||
            bm == openshot::BLEND_SATURATION)
            tags.push_back("gpu-amplified");
        add("compositing.blend_" + slug(openshot::BlendModeToString(bm)), tags, {15},
            [bm](Scene& s) {
                auto& tl = s.makeTimeline();
                tl.AddClip(backgroundClip(s, s.media("background_960x540.png")));
                MediaSpec base; base.path = s.media("clip_a_640x360_30.mp4"); base.priority = 0;
                base.transform = Transform{BBox{0.5f, 0.5f, 0.85f, 0.85f}};
                tl.AddClip(mediaClip(s, base));
                MediaSpec top; top.path = s.media("overlay_gradients_640x360_30.mp4"); top.priority = 1;
                top.transform = Transform{BBox{0.5f, 0.5f, 0.7f, 0.7f}, 1.f, 1.f, 10.f};
                auto* topClip = mediaClip(s, top);
                topClip->Blend(bm);
                tl.AddClip(topClip);
                tl.Open();
            });
    }

    add("compositing.blend_with_png_alpha", {"compositing", "blend", "exact", "gpu-composite"}, {15},
        [](Scene& s) {
            auto& tl = s.makeTimeline();
            tl.AddClip(backgroundClip(s, s.media("background_960x540.png")));
            MediaSpec base; base.path = s.media("clip_a_640x360_30.mp4"); base.transform = Transform{};
            tl.AddClip(mediaClip(s, base));
            MediaSpec top; top.path = s.media("image_alpha_320x200.png"); top.isImage = true; top.priority = 1;
            top.transform = Transform{BBox{0.5f, 0.5f, 0.7f, 0.7f}}; top.opacity = 0.7f;
            auto* c = mediaClip(s, top);
            c->Blend(openshot::BLEND_SCREEN);
            tl.AddClip(c);
            tl.Open();
        });

    add("compositing.alpha_fade_ghost", {"compositing", "alpha", "exact", "gpu-composite"}, {1, 8, 16, 31, 40, 45, 75, 89},
        [](Scene& s) {
            auto& tl = s.makeTimeline();
            tl.AddClip(backgroundClip(s, s.media("background_960x540.png")));
            MediaSpec m; m.path = s.media("clip_a_640x360_30.mp4"); m.end = 3.0;
            m.transform = Transform{BBox{0.5f, 0.5f, 0.8f, 0.8f}};
            m.opacity = 0.8f;
            m.fade = Fade{0.5, 0.5, kEaseInOut};
            m.ghostIntervals = {{1.0, 1.4}};
            tl.AddClip(mediaClip(s, m));
            tl.Open();
        });

    add("compositing.opacity_keyframes_merge", {"compositing", "alpha", "exact"}, {1, 30, 60, 89},
        [](Scene& s) {
            auto& tl = s.makeTimeline();
            tl.AddClip(backgroundClip(s, s.media("background_960x540.png")));
            MediaSpec m; m.path = s.media("clip_a_640x360_30.mp4"); m.end = 3.0; m.transform = Transform{};
            m.opacityKfs = {{0.0, 1.f, kLinear}, {1.5, 0.2f, kEaseInOut}, {3.0, 1.f, kEaseInOut}};
            tl.AddClip(mediaClip(s, m));
            tl.Open();
        });

    add("compositing.layer_order", {"compositing", "exact", "gpu-composite"}, {30},
        [](Scene& s) {
            auto& tl = s.makeTimeline();
            tl.AddClip(backgroundClip(s, s.media("background_960x540.png")));
            // Same content on three layers: the service's layer formula decides who is on top.
            const struct { int track, priority; float cx; } L[] = {{0, 2, 0.4f}, {1, 0, 0.5f}, {0, 1, 0.6f}};
            for (const auto& l : L) {
                MediaSpec m; m.path = s.media("image_rgb_400x300.jpg"); m.isImage = true;
                m.track = l.track; m.priority = l.priority;
                m.transform = Transform{BBox{l.cx, 0.5f, 0.5f, 0.5f}};
                tl.AddClip(mediaClip(s, m));
            }
            tl.Open();
        });
}
