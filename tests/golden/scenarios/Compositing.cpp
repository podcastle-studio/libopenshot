// Compositing: all 16 blend modes, opacity curves (fade + ghost), layer order.
#include "Recipes.h"

#include "BlendModes.h"
#include "Timeline.h"

#include <algorithm>

using namespace golden;
using namespace golden::recipes;

namespace {

std::string slug(std::string s) {
    for (auto& c : s) { c = static_cast<char>(std::tolower(c)); if (c == '-' || c == ' ') c = '_'; }
    return s;
}

} // namespace

void golden::registerCompositingScenarios() {
    for (int mode = 0; mode < openshot::BLEND_MODE_COUNT; ++mode) {
        const auto bm = static_cast<openshot::BlendMode>(mode);
        add("compositing.blend_" + slug(openshot::BlendModeToString(bm)), {"compositing", "blend", "exact"}, {15},
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

    add("compositing.blend_with_png_alpha", {"compositing", "blend", "exact"}, {15},
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

    add("compositing.alpha_fade_ghost", {"compositing", "alpha", "exact"}, {1, 8, 16, 31, 40, 45, 75, 89},
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

    add("compositing.layer_order", {"compositing", "exact"}, {30},
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
