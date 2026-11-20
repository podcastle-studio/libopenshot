// Readers: FFmpegReader (several fps/sizes), QtImageReader (PNG alpha, JPEG, SVG), background crop.
#include "Recipes.h"

#include "Timeline.h"

using namespace golden;
using namespace golden::recipes;

namespace {

void videoScene(Scene& scene, const std::string& file, double end) {
    auto& tl = scene.makeTimeline();
    tl.AddClip(backgroundClip(scene, scene.media("background_960x540.png")));
    MediaSpec m; m.path = scene.media(file); m.end = end; m.transform = Transform{};
    tl.AddClip(mediaClip(scene, m));
    tl.Open();
}

} // namespace

void golden::registerReaderScenarios() {
    add("readers.video_a_30fps", {"readers", "exact"}, {1, 30, 75, 150},
        [](Scene& s) { videoScene(s, "clip_a_640x360_30.mp4", 6.0); });

    add("readers.video_b_24fps_prescale", {"readers", "framemapper", "exact"}, {1, 2, 5, 30, 75, 150},
        [](Scene& s) { videoScene(s, "clip_b_854x480_24.mp4", 6.0); });

    add("readers.video_c_25fps_prescale_720p", {"readers", "framemapper", "exact"}, {1, 5, 6, 30, 60, 89},
        [](Scene& s) { videoScene(s, "clip_c_1280x720_25.mp4", 3.0); });

    add("readers.image_png_alpha", {"readers", "alpha", "exact", "gpu-composite"}, {1},
        [](Scene& s) {
            auto& tl = s.makeTimeline();
            tl.AddClip(backgroundClip(s, s.media("background_960x540.png")));
            MediaSpec m; m.path = s.media("image_alpha_320x200.png"); m.isImage = true;
            m.transform = Transform{BBox{0.5f, 0.5f, 0.6f, 0.6f}};
            tl.AddClip(mediaClip(s, m));
            tl.Open();
        });

    // The service's watermark: a ProRes 4444 .mov with alpha on its own top track for the whole
    // export (ExportData.cpp builds it as an IMAGE clip; QtImageReader cannot open it, so it falls
    // back to FFmpegReader). The source is 1 s long and the clip 2 s, so frame 45 is the held last
    // frame -- the case the host texture cache and read-ahead with GPU decode (A7) are about.
    add("readers.watermark_prores4444", {"readers", "alpha", "exact", "gpu-composite"}, {1, 15, 45},
        [](Scene& s) {
            auto& tl = s.makeTimeline();
            MediaSpec v; v.path = s.media("clip_a_640x360_30.mp4"); v.end = 2.0;
            v.transform = Transform{BBox{0.5f, 0.5f, 1.f, 1.f}};
            tl.AddClip(mediaClip(s, v));
            MediaSpec w; w.path = s.media("watermark_prores4444_160x120_30.mov"); w.end = 2.0; w.track = 9;
            w.transform = Transform{BBox{0.82f, 0.18f, 0.25f, 0.25f}};
            tl.AddClip(mediaClip(s, w));
            tl.Open();
        });

    add("readers.image_jpg", {"readers", "exact", "gpu-composite"}, {1},
        [](Scene& s) {
            auto& tl = s.makeTimeline();
            MediaSpec m; m.path = s.media("image_rgb_400x300.jpg"); m.isImage = true;
            m.transform = Transform{BBox{0.5f, 0.5f, 1.f, 1.f}};
            tl.AddClip(mediaClip(s, m));
            tl.Open();
        });

    add("readers.svg_shapes", {"readers", "shapes", "exact", "gpu-composite"}, {1},
        [](Scene& s) {
            auto& tl = s.makeTimeline();
            tl.AddClip(backgroundClip(s, s.media("background_960x540.png")));
            MediaSpec arrow; arrow.path = s.media("shape_arrow.svg"); arrow.isImage = true; arrow.priority = 0;
            arrow.transform = Transform{BBox{0.35f, 0.4f, 0.5f, 0.35f}, 1.f, 1.f, -20.f};
            tl.AddClip(mediaClip(s, arrow));
            MediaSpec line; line.path = s.media("shape_dashed_line.svg"); line.isImage = true; line.priority = 1;
            line.transform = Transform{BBox{0.5f, 0.8f, 0.8f, 0.06f}};
            tl.AddClip(mediaClip(s, line));
            tl.Open();
        });

    add("readers.background_scale_crop", {"readers", "exact"}, {1},
        [](Scene& s) {
            auto& tl = s.makeTimeline();
            tl.AddClip(backgroundClip(s, s.media("background_960x540.png")));
            tl.Open();
        });
}
