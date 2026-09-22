#include "Harness.h"

#include "gpu/GpuYuv.h"

#include "gpu/GpuDevice.h"

#include "Clip.h"
#include "Frame.h"
#include "ReaderBase.h"
#include "Settings.h"
#include "Timeline.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <iostream>

namespace golden {

openshot::Timeline& Scene::makeTimeline() {
    timeline = std::make_unique<openshot::Timeline>(width, height, fps, 48000, 2, openshot::LAYOUT_STEREO);
    return *timeline;
}

Scene::~Scene() {
    if (timeline) {
        try { timeline->Close(); } catch (...) {}
        timeline.reset();
    }
    // Reverse order of creation: a FrameMapper must go before the reader it wraps.
    for (auto it = clips.rbegin(); it != clips.rend(); ++it) {
        try { (*it)->Close(); } catch (...) {}
        delete *it;
    }
    for (auto it = readers.rbegin(); it != readers.rend(); ++it) {
        try { (*it)->Close(); } catch (...) {}
        delete *it;
    }
}

std::vector<Scenario>& registry() {
    static std::vector<Scenario> reg;
    return reg;
}

void add(const std::string& name, std::vector<std::string> tags, std::vector<int64_t> frames,
         std::function<void(Scene&)> build, Tolerance tol) {
    for (const auto& s : registry())
        if (s.name == name) { std::cerr << "duplicate scenario " << name << "\n"; std::abort(); }
    if (std::find(tags.begin(), tags.end(), "exact") != tags.end()) tol = Tolerance::Exact();
    // A scenario the GPU compositor draws cannot match CPU goldens bit-for-bit -- Skia's
    // bilinear is not QPainter's smooth transform. Such a scenario keeps its strict CPU
    // tolerance and is held to the parity policy's "close" class only when a GPU is in use.
    Tolerance gpu_tol = tol;
    if (std::find(tags.begin(), tags.end(), "gpu-composite") != tags.end())
        gpu_tol = Tolerance::GpuClose();
    if (std::find(tags.begin(), tags.end(), "gpu-blur") != tags.end())
        gpu_tol = Tolerance::GpuBlur();
    if (std::find(tags.begin(), tags.end(), "gpu-amplified") != tags.end())
        gpu_tol = Tolerance::GpuAmplified();
    registry().push_back(Scenario{name, std::move(tags), std::move(frames), tol, gpu_tol,
                                  std::move(build), nullptr});
}

void addCustom(const std::string& name, std::vector<std::string> tags, std::function<void(Scene&)> build,
               std::function<void(Scene&, std::vector<Captured>&, std::vector<Check>&)> capture, Tolerance tol) {
    add(name, std::move(tags), {}, std::move(build), tol);
    registry().back().capture = std::move(capture);
}

namespace {

std::string frameLabel(int64_t n) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "f%06lld", static_cast<long long>(n));
    return buf;
}

bool selected(const Scenario& s, const std::string& filter) {
    if (filter.empty()) return true;
    if (s.name.find(filter) != std::string::npos) return true;
    return std::find(s.tags.begin(), s.tags.end(), filter) != s.tags.end();
}

bool passes(const Metrics& m, const Tolerance& t) {
    if (m.sizeMismatch) return false;
    return m.psnr >= t.psnrMin && m.ssim >= t.ssimMin && m.maxAbs <= t.maxAbs && m.pctOver2 <= t.pctOver2Max;
}

void defaultCapture(const Scenario& s, Scene& scene, std::vector<Captured>& out) {
    for (const int64_t n : s.frames) {
        auto frame = scene.timeline->GetFrame(n);
        out.push_back({frameLabel(n), fromFrame(frame)});
    }
}

} // namespace

RunSummary runAll(const Options& opts) {
    const auto t0 = std::chrono::steady_clock::now();
    RunSummary summary;

    // Which tolerance applies is a property of the run, not of the scenario: the same
    // scenario is held bit-exact on the CPU and "close" when a GPU composites it.
    const bool gpu_active = openshot::GpuDevice::Instance().available();
    if (gpu_active)
        std::printf("GPU active — scenarios tagged gpu-composite use the close tolerance\n");

    auto* settings = openshot::Settings::Instance();
    settings->OMP_THREADS = opts.threads;
    settings->FF_THREADS = opts.threads;
    settings->DISABLE_CACHING = true;
    settings->HIGH_QUALITY_SCALING = true;
    settings->DEBUG_TO_STDERR = false;

    for (const Scenario& s : registry()) {
        if (!selected(s, opts.filter)) continue;
        ++summary.scenariosRun;
        std::vector<Captured> captured;
        std::vector<Check> checks;
        std::string error;
        // A scenario whose frames the reader decoded on the GPU cannot be held to CPU goldens
        // bit-for-bit, and asking the scenario's tags would not know: whether a GPU decoded
        // depends on the build, the backend and the pixel format, not on the scenario. So ask
        // the converter instead, the same way unit.gpu_effect_path asks GpuEffect -- a check
        // that cannot tell whether the GPU ran proves nothing.
        const unsigned long long decodes_before = openshot::GpuYuv::Conversions();
        const unsigned long long scaled_before = openshot::GpuYuv::ScaledConversions();
        try {
            Scene scene;
            scene.mediaDir = opts.mediaDir;
            scene.workDir = opts.outDir + "/" + s.name + "/work/";
            std::filesystem::create_directories(scene.workDir);
            s.build(scene);
            if (!scene.timeline) throw std::runtime_error("scenario did not create a timeline");
            if (s.capture) s.capture(scene, captured, checks);
            else defaultCapture(s, scene, captured);
        } catch (const std::exception& e) {
            error = e.what();
        } catch (...) {
            error = "unknown exception";
        }

        for (const auto& c : checks) {
            summary.checks.emplace_back(s.name, c);
            if (!c.ok) ++summary.failures;
            std::printf("%-6s %-40s check %-28s %s\n", c.ok ? "PASS" : "FAIL", s.name.c_str(), c.name.c_str(), c.message.c_str());
        }

        if (!error.empty()) {
            FrameResult r;
            r.scenario = s.name;
            r.label = "(build)";
            r.message = error;
            summary.frames.push_back(r);
            ++summary.failures;
            std::printf("FAIL   %-40s exception: %s\n", s.name.c_str(), error.c_str());
            continue;
        }

        // Relax the scenario's own GPU band by the decode band, never tighten it: a scenario
        // that already allows more (a rotation the compositor resamples, say) keeps what it
        // allows, and one held exact gets just enough room for the conversion.
        const auto relax = [](Tolerance band, const Tolerance& by) {
            band.psnrMin = std::min(band.psnrMin, by.psnrMin);
            band.ssimMin = std::min(band.ssimMin, by.ssimMin);
            band.maxAbs = std::max(band.maxAbs, by.maxAbs);
            band.pctOver2Max = std::max(band.pctOver2Max, by.pctOver2Max);
            return band;
        };
        Tolerance gpu_tol = s.gpuTol;
        if (openshot::GpuYuv::ScaledConversions() > scaled_before)
            gpu_tol = relax(gpu_tol, Tolerance::GpuDecodeScaled());
        else if (openshot::GpuYuv::Conversions() > decodes_before)
            gpu_tol = relax(gpu_tol, Tolerance::GpuDecode());

        for (const auto& cap : captured) {
            FrameResult r;
            r.scenario = s.name;
            r.label = cap.label;
            r.actualPath = opts.outDir + "/" + s.name + "/" + cap.label + ".png";
            r.goldenPath = opts.expectedDir + "/" + s.name + "/" + cap.label + ".png";
            savePng(r.actualPath, cap.image);

            if (opts.update) {
                savePng(r.goldenPath, cap.image);
                r.pass = true;
                r.message = "updated";
                summary.frames.push_back(r);
                std::printf("UPDATE %-40s %s\n", s.name.c_str(), cap.label.c_str());
                continue;
            }

            auto golden = loadPng(r.goldenPath);
            if (!golden) {
                r.pass = false;
                r.message = "missing golden (run with --update)";
                ++summary.failures;
                summary.frames.push_back(r);
                std::printf("FAIL   %-40s %s  %s\n", s.name.c_str(), cap.label.c_str(), r.message.c_str());
                continue;
            }
            r.metrics = compare(*golden, cap.image);
            r.pass = passes(r.metrics, gpu_active ? gpu_tol : s.tol);
            if (!r.pass) ++summary.failures;
            if (!r.pass || opts.allImages) {
                const std::string base = (opts.reportDir.empty() ? opts.outDir : opts.reportDir) + "/" + s.name + "/" + cap.label;
                const Image diff = diffHeatmap(*golden, cap.image);
                r.diffPath = base + "_diff.png";
                r.triptychPath = base + "_triptych.png";
                savePng(r.diffPath, diff);
                savePng(r.triptychPath, triptych(*golden, cap.image, diff));
            }
            std::printf("%-6s %-40s %s  psnr=%6.2f ssim=%.4f max=%3d over2=%6.2f%%%s\n",
                        r.pass ? "PASS" : "FAIL", s.name.c_str(), cap.label.c_str(),
                        r.metrics.psnr, r.metrics.ssim, r.metrics.maxAbs, r.metrics.pctOver2,
                        r.metrics.sizeMismatch ? "  SIZE MISMATCH" : "");
            summary.frames.push_back(r);
        }
    }

    summary.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    return summary;
}

} // namespace golden
