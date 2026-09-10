// Export: FFmpegWriter configured exactly like the service (libx264, silent audio, pipeline mode),
// decoded back with FFmpegReader and compared to the live timeline frames; plus an audio smoke test.
#include "Recipes.h"

#include "FFmpegReader.h"
#include "Frame.h"
#include "Timeline.h"

#include <cmath>
#include <sstream>

using namespace golden;
using namespace golden::recipes;

namespace {

void exportScene(Scene& s) {
    auto& tl = s.makeTimeline();
    tl.AddClip(backgroundClip(s, s.media("background_960x540.png")));
    MediaSpec m; m.path = s.media("clip_a_640x360_30.mp4"); m.end = 3.0;
    m.transform = Transform{BBox{0.5f, 0.5f, 0.85f, 0.85f}, 1.f, 1.f, 8.f};
    m.fade = Fade{0.3, 0.3, kEaseInOut};
    tl.AddClip(mediaClip(s, m));
    MediaSpec png; png.path = s.media("image_alpha_320x200.png"); png.isImage = true; png.priority = 1; png.end = 3.0;
    png.transform = Transform{BBox{0.7f, 0.3f, 0.4f, 0.4f}};
    tl.AddClip(mediaClip(s, png));
    tl.Open();
}

std::string fmt(double v) { std::ostringstream o; o.precision(3); o << std::fixed << v; return o.str(); }

} // namespace

void golden::registerExportScenarios() {
    addCustom("export.roundtrip_x264", {"export"}, exportScene,
        [](Scene& s, std::vector<Captured>& out, std::vector<Check>& checks) {
            const int64_t last = 45;
            const std::string path = s.workDir + "export.mp4";
            {
                openshot::FFmpegWriter w(path);
                configureWriter(w, s.width, s.height, s.fps);
                w.Open();
                w.WriteFrame(s.timeline.get(), 1, last);
                w.Close();
            }
            openshot::FFmpegReader r(path);
            r.Open();
            checks.push_back({"video_length", std::llabs(r.info.video_length - last) <= 1,
                              "video_length=" + std::to_string(r.info.video_length) + " expected " + std::to_string(last)});
            checks.push_back({"fps", r.info.fps.num == 30 && r.info.fps.den == 1,
                              std::to_string(r.info.fps.num) + "/" + std::to_string(r.info.fps.den)});
            checks.push_back({"size", r.info.width == s.width && r.info.height == s.height,
                              std::to_string(r.info.width) + "x" + std::to_string(r.info.height)});
            for (int64_t k : {int64_t(1), int64_t(15), int64_t(30), last}) {
                const Image decoded = fromFrame(r.GetFrame(k));
                const Image live = fromFrame(s.timeline->GetFrame(k));
                const Metrics m = compare(live, decoded);
                // Mean signed difference per channel: a bias here points at a colour matrix / range
                // mismatch between the writer's RGB->YUV and the reader's YUV->RGB, not at x264 loss.
                double bias[3] = {0, 0, 0};
                if (!m.sizeMismatch) {
                    const size_t n = static_cast<size_t>(live.w) * live.h;
                    for (size_t p = 0, i = 0; p < n; ++p, i += 4)
                        for (int c = 0; c < 3; ++c) bias[c] += double(decoded.rgba[i + c]) - double(live.rgba[i + c]);
                    for (auto& b : bias) b /= double(n);
                }
                char label[32];
                std::snprintf(label, sizeof label, "f%06lld", static_cast<long long>(k));
                out.push_back({std::string(label) + "_live", live});
                out.push_back({std::string(label) + "_decoded", decoded});
                checks.push_back({std::string("live_vs_decoded_") + label, m.psnr >= 25.0 && !m.sizeMismatch,
                                  "psnr=" + fmt(m.psnr) + " ssim=" + fmt(m.ssim) + " bias(r,g,b)=" + fmt(bias[0]) + "," + fmt(bias[1]) + "," + fmt(bias[2])});
            }
            r.Close();
        }, Tolerance::Codec());

    addCustom("export.silent_audio_smoke", {"export", "audio"}, exportScene,
        [](Scene& s, std::vector<Captured>&, std::vector<Check>& checks) {
            const int64_t last = 30;
            const std::string path = s.workDir + "export_audio.mp4";
            {
                openshot::FFmpegWriter w(path);
                configureWriter(w, s.width, s.height, s.fps);
                w.Open();
                w.WriteFrame(s.timeline.get(), 1, last);
                w.Close();
            }
            openshot::FFmpegReader r(path);
            r.Open();
            checks.push_back({"has_audio", r.info.has_audio, r.info.has_audio ? "yes" : "no"});
            checks.push_back({"sample_rate", r.info.sample_rate == 48000, std::to_string(r.info.sample_rate)});
            checks.push_back({"channels", r.info.channels == 2, std::to_string(r.info.channels)});
            float peak = 0.f;
            for (int64_t k : {int64_t(1), int64_t(15), int64_t(30)}) {
                auto f = r.GetFrame(k);
                for (int ch = 0; ch < f->GetAudioChannelsCount(); ++ch) {
                    const float* smp = f->GetAudioSamples(ch);
                    for (int i = 0; i < f->GetAudioSamplesCount(); ++i) peak = std::max(peak, std::fabs(smp[i]));
                }
            }
            checks.push_back({"audio_is_silent", peak < 1e-3f, "peak=" + fmt(peak)});
            checks.push_back({"video_length", std::llabs(r.info.video_length - last) <= 1, std::to_string(r.info.video_length)});
            r.Close();
        });
}
