// Export: FFmpegWriter configured exactly like the service (libx264, silent audio, pipeline mode),
// decoded back with FFmpegReader and compared to the live timeline frames; plus an audio smoke test.
#include "Recipes.h"
#include <QImage>
#include "Settings.h"
#include "gpu/GpuDevice.h"
#include "gpu/CudaInterop.h"
#include "gpu/GpuYuv.h"

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

    // NVENC fed straight from the GPU (W25, Settings::GPU_ENCODE) must be as faithful an export
    // as the readback path: every frame there, none shifted, none black, and on every frame no
    // further from the live timeline than the readback export is (0.5 dB of slack). The two
    // exports are not compared to each other directly: they use different chroma filters (box
    // against swscale's bicubic) under lossy encoding, measured at ~37.5 dB apart with the GPU
    // export marginally the *closer* of the two to the timeline. The counter proves the GPU arm
    // did not quietly read back. Declines -- and passes -- where there is no NVENC, no GPU
    // compositor or no CUDA interop: every one of those is a supported configuration.
    addCustom("export.nvenc_on_device", {"export", "gpu"}, exportScene,
        [](Scene& s, std::vector<Captured>&, std::vector<Check>& checks) {
            const int64_t last = 30;
            openshot::Settings* settings = openshot::Settings::Instance();
            const bool previous = settings->GPU_ENCODE;
            const auto encode = [&](bool on_device, const std::string& path) {
                settings->GPU_ENCODE = on_device;
                openshot::FFmpegWriter w(path);
                configureWriter(w, s.width, s.height, s.fps, 4000000, "h264_nvenc");
                w.SetPipelineMode(true);   // as the service runs it: the frame crosses threads
                w.SetPipelineQueueCapacity(16);
                w.Open();
                w.WriteFrame(s.timeline.get(), 1, last);
                w.Close();
            };
            unsigned long long ran = 0;
            try {
                encode(false, s.workDir + "nvenc_readback.mp4");
                s.timeline->ClearAllCache();   // or its last frame comes back from the first arm
                const unsigned long long before = openshot::GpuYuv::Encodes();
                encode(true, s.workDir + "nvenc_device.mp4");
                ran = openshot::GpuYuv::Encodes() - before;
            } catch (const std::exception& e) {
                settings->GPU_ENCODE = previous;
                checks.push_back({"nvenc_on_device", true, std::string("no NVENC here, declined: ") + e.what()});
                return;
            }
            settings->GPU_ENCODE = previous;

            const bool expected = openshot::GpuDevice::Instance().available() &&
                                  openshot::CudaInterop::Instance().available();
            openshot::FFmpegReader a(s.workDir + "nvenc_readback.mp4"), b(s.workDir + "nvenc_device.mp4");
            a.Open(); b.Open();
            double worst = 1e9;   // (live vs device) - (live vs readback), worst frame
            for (int64_t k = 1; k <= last; ++k) {
                const Image live = fromFrame(s.timeline->GetFrame(k));
                const double readback = compare(live, fromFrame(a.GetFrame(k))).psnr;
                const double device = compare(live, fromFrame(b.GetFrame(k))).psnr;
                worst = std::min(worst, device - readback);
            }
            const bool same_length = a.info.video_length == b.info.video_length;
            a.Close(); b.Close();
            const bool ok = same_length && worst >= -0.5 &&
                            (expected ? ran >= static_cast<unsigned long long>(last) : ran == 0);
            checks.push_back({"nvenc_on_device", ok,
                              std::to_string(ran) + " frames encoded on the device (" +
                                  (expected ? "expected" : "declined") + "); worst frame " + fmt(worst) +
                                  " dB closer to the timeline than the readback export (gate -0.5); lengths " +
                                  (same_length ? "match" : "differ")});
        });

    // Exports are BT.709 (2026-09-23): the writer encodes RGB with the matrix the file is tagged
    // with. Known sRGB bars go through the writer the way the service configures it -- x264 tagged
    // through x264-params, NVENC through the codec context -- and must come back within 3 code
    // values (8-bit limited-range YUV plus the encoder) when decoded as the tag says. Until this
    // change they came back ~30 off, because the pixels were BT.601 under a BT.709 label.
    addCustom("export.bt709_bars", {"export"}, exportScene,
        [](Scene& s, std::vector<Captured>&, std::vector<Check>& checks) {
            static const int kBars[8][3] = {{191, 191, 191}, {191, 191, 0}, {0, 191, 191}, {0, 191, 0},
                                            {191, 0, 191},   {191, 0, 0},   {0, 0, 191},   {200, 150, 120}};
            const int W = 640, H = 360;
            for (const std::string codec : {std::string("libx264"), std::string("h264_nvenc")}) {
                const std::string path = s.workDir + "bars_" + codec + ".mp4";
                try {
                    openshot::FFmpegWriter w(path);
                    configureWriter(w, W, H, s.fps, 8000000, codec);
                    w.SetOption(openshot::VIDEO_STREAM, "crf", "8");   // the matrix, not the encoder
                    w.Open();
                    for (int n = 1; n <= 10; ++n) {
                        auto image = std::make_shared<QImage>(W, H, QImage::Format_RGBA8888_Premultiplied);
                        for (int y = 0; y < H; ++y)
                            for (int x = 0; x < W; ++x) {
                                uchar* p = image->scanLine(y) + x * 4;
                                const int* c = kBars[x / 80];
                                p[0] = uchar(c[0]); p[1] = uchar(c[1]); p[2] = uchar(c[2]); p[3] = 255;
                            }
                        auto frame = std::make_shared<openshot::Frame>(n, W, H, "#000000");
                        frame->AddImage(image);
                        w.WriteFrame(frame);
                    }
                    w.Close();
                } catch (const std::exception& e) {
                    if (codec != "libx264") {   // no NVENC here: a supported configuration
                        checks.push_back({"bt709_bars_" + codec, true, std::string("declined: ") + e.what()});
                        continue;
                    }
                    checks.push_back({"bt709_bars_" + codec, false, std::string("threw: ") + e.what()});
                    continue;
                }
                openshot::FFmpegReader r(path);
                r.Open();
                auto image = r.GetFrame(5)->GetImage();
                int worst = 0;
                for (int bar = 0; bar < 8; ++bar) {
                    const uchar* p = image->constScanLine(180) + (40 + 80 * bar) * 4;
                    for (int c = 0; c < 3; ++c) worst = std::max(worst, std::abs(int(p[c]) - kBars[bar][c]));
                }
                r.Close();
                checks.push_back({"bt709_bars_" + codec, worst <= 3,
                                  "worst channel error " + std::to_string(worst) + " (gate 3), decoded as tagged"});
            }
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
