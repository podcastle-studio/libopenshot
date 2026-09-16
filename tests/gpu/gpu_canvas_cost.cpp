/*
 * gpu_canvas_cost — what a GPU-backed timeline canvas costs before anything draws on it.
 *
 * W12 puts Timeline::GetFrame's output surface on the GPU, but the clip composite
 * (Clip::apply_keyframes + Clip::apply_background) stays on QPainter until W13. In
 * between, every frame pays: acquire a pooled surface, clear it, and read it back so
 * the QPainter path can touch the pixels. Nothing is drawn on the GPU in return.
 *
 * This measures that round trip against the per-frame budget of the scenarios W12's
 * gate names, so the decision to sequence W12 with W13 rests on a number rather than
 * on an estimate.
 *
 *   OPENSHOT_GPU=vulkan cmake-build-gpu/tests/gpu/openshot-gpu-canvas-cost
 */
#include "gpu/GpuDevice.h"
#include "gpu/GpuFrame.h"
#include "gpu/GpuSurfacePool.h"

#include "skia/include/core/SkBitmap.h"
#include "skia/include/core/SkCanvas.h"
#include "skia/include/core/SkColor.h"
#include "skia/include/core/SkColorType.h"
#include "skia/include/core/SkAlphaType.h"
#include "skia/include/core/SkImageInfo.h"
#include "skia/include/core/SkPixmap.h"
#include "skia/include/core/SkSurface.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <memory>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;
double ms(Clock::duration d) { return std::chrono::duration<double, std::milli>(d).count(); }

struct Summary { double median, p95, mean; };

Summary summarise(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    double sum = 0; for (double x : v) sum += x;
    return {v[v.size()/2], v[(size_t)(v.size()*0.95)], sum/v.size()};
}

// One frame's worth of the W12-without-W13 round trip: pooled surface, clear, readback.
Summary measure(int w, int h, int iters) {
    std::vector<double> samples;
    samples.reserve(iters);
    // One warm-up frame so pipeline creation is not charged to the first sample.
    for (int i = 0; i < iters + 1; ++i) {
        const auto t0 = Clock::now();
        auto frame = openshot::GpuFrame::Create(w, h, kN32_SkColorType);
        if (!frame) { std::printf("  GpuFrame::Create failed at %dx%d\n", w, h); return {0,0,0}; }
        frame->surface()->getCanvas()->clear(SK_ColorTRANSPARENT);
        SkBitmap dst;
        dst.allocPixels(SkImageInfo::Make(w, h, kN32_SkColorType, kPremul_SkAlphaType));
        SkPixmap map;
        if (!dst.peekPixels(&map) || !frame->readback(map)) {
            std::printf("  readback failed at %dx%d\n", w, h);
            return {0,0,0};
        }
        const double dt = ms(Clock::now() - t0);
        if (i > 0) samples.push_back(dt);
    }
    return summarise(std::move(samples));
}

} // namespace

int main() {
    auto& dev = openshot::GpuDevice::Instance();
    if (!dev.available()) {
        std::printf("GPU not available (OPENSHOT_GPU unset or off) — nothing to measure.\n");
        return 0;
    }

    struct Case { const char* label; int w, h; };
    const Case cases[] = {{"1080p", 1920, 1080}, {"2160p", 3840, 2160}};

    std::printf("Per-frame cost of a GPU timeline canvas with no GPU consumer\n");
    std::printf("(acquire pooled surface + clear + readback, 200 frames each)\n\n");
    std::printf("  %-8s %10s %10s %10s\n", "size", "median", "p95", "mean");
    for (const auto& c : cases) {
        const Summary s = measure(c.w, c.h, 200);
        std::printf("  %-8s %8.2f ms %8.2f ms %8.2f ms\n", c.label, s.median, s.p95, s.mean);
    }

    // The budgets W12's gate is written against (doc/PERFORMANCE-BASELINE.md, 1080p render).
    std::printf("\nAgainst the 1080p per-frame budget of the scenarios W12's gate names:\n");
    const Summary s = measure(1920, 1080, 200);
    struct Scen { const char* name; double fps; };
    const Scen scens[] = {{"single_video", 117.0}, {"source_4k", 61.0}, {"podcast_pip", 23.0}};
    for (const auto& sc : scens) {
        const double budget = 1000.0 / sc.fps;
        const double after  = budget + s.median;
        std::printf("  %-14s %6.1f fps = %5.2f ms/frame  ->  %5.2f ms = %5.1f fps  (%+.0f%%)\n",
                    sc.name, sc.fps, budget, after, 1000.0/after,
                    100.0 * ((1000.0/after) / sc.fps - 1.0));
    }
    return 0;
}
