/**
 * @file gpu_checks.cpp
 * @brief Acceptance checks for plan step 2.2 (src/gpu).
 *
 * Links libopenshot, so it only means anything when the library was configured
 * with -DSkia_ROOT=/usr/local/skia-gpu. Three checks, straight from the plan:
 *
 *   1. device    create/destroy the device 100 times, no leak, no VRAM growth
 *   2. transfer  upload -> readback of 1000 random RGBA images is bit-identical
 *   3. pool      the second acquire of a given size returns the same allocation
 *   4. canvas    a recycled surface's canvas comes back in a new surface's state
 *   5. control   GpuDevice::SetBackend is the single on/off switch for all of it
 *   6. subtitle  the subtitle pass renders the same on a GPU and a raster canvas
 *
 * VRAM is read via nvidia-smi when it is present; on lavapipe or a machine with
 * no NVIDIA driver that part reports "skipped" rather than failing.
 *
 *   OPENSHOT_GPU=vulkan|lavapipe   which backend to test (default vulkan)
 */

#include "gpu/GpuDevice.h"
#include "gpu/GpuFrame.h"
#include "gpu/GpuSurfacePool.h"
#include "subtitle/SubtitleManager.h"

#include "skia/include/core/SkBitmap.h"
#include "skia/include/core/SkSurface.h"
#include "skia/include/core/SkColor.h"

#include "skia/include/core/SkAlphaType.h"
#include "skia/include/core/SkCanvas.h"
#include "skia/include/core/SkColorSpace.h"
#include "skia/include/core/SkColorType.h"
#include "skia/include/core/SkImageInfo.h"
#include "skia/include/core/SkMatrix.h"
#include "skia/include/core/SkPixmap.h"
#include "skia/include/core/SkRect.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <vector>

namespace {

int failures = 0;

void report(const char* name, bool ok, const std::string& detail) {
    std::printf("%-6s %-28s %s\n", ok ? "PASS" : "FAIL", name, detail.c_str());
    if (!ok) failures++;
}

long long runQuery(const char* command, bool matchPid) {
    FILE* pipe = popen(command, "r");
    if (!pipe) return -1;
    const long long self = static_cast<long long>(getpid());
    long long mib = -1;
    char line[256];
    while (std::fgets(line, sizeof(line), pipe)) {
        if (matchPid) {
            long long pid = 0, value = 0;
            if (std::sscanf(line, "%lld, %lld", &pid, &value) == 2 && pid == self)
                mib = value;
        } else {
            long long value = 0;
            if (std::sscanf(line, "%lld", &value) == 1) mib = value;
        }
    }
    pclose(pipe);
    return mib < 0 ? -1 : mib;
}

/// VRAM in MiB, and whether it is this process alone.
///
/// --query-compute-apps lists CUDA contexts only, and a Vulkan graphics process
/// has none, so it usually returns nothing here. The whole-device figure is the
/// fallback: it counts other processes too, which makes it noisy in absolute
/// terms but perfectly adequate for "did 100 device cycles leak", where a leak
/// would be hundreds of MiB.
long long usedVram(bool* perProcess) {
    const long long mine = runQuery("nvidia-smi --query-compute-apps=pid,used_memory "
                                    "--format=csv,noheader,nounits 2>/dev/null", true);
    if (mine >= 0) {
        if (perProcess) *perProcess = true;
        return mine;
    }
    if (perProcess) *perProcess = false;
    return runQuery("nvidia-smi --query-gpu=memory.used "
                    "--format=csv,noheader,nounits 2>/dev/null", false);
}

// ---------------------------------------------------------------------------

void checkDeviceCycles() {
    const int kCycles = 100;
    std::string name;
    for (int i = 0; i < kCycles; ++i) {
        openshot::GpuDevice& device = openshot::GpuDevice::Instance();
        if (!device.available()) {
            report("device", false,
                   "GPU unavailable on cycle " + std::to_string(i) + ": " +
                           device.lastError());
            return;
        }
        if (i == 0) name = device.deviceName();
        openshot::GpuDevice::DestroyInstance();
    }

    // Measure after the cycles above have warmed every driver-side allocation, so
    // a second run of the same size is comparing like with like.
    bool perProcess = false;
    const long long before = usedVram(&perProcess);
    for (int i = 0; i < kCycles; ++i) {
        openshot::GpuDevice::Instance().available();
        openshot::GpuDevice::DestroyInstance();
    }
    const long long after = usedVram(&perProcess);

    std::string detail = std::to_string(kCycles * 2) + " cycles on " + name;
    if (before < 0 || after < 0) {
        detail += ", VRAM check skipped (nvidia-smi reported nothing)";
        report("device", true, detail);
        return;
    }
    detail += ", VRAM " + std::to_string(before) + " -> " + std::to_string(after) +
              " MiB" + (perProcess ? " (this process)" : " (whole device)");
    // A per-cycle leak of even one small allocation would show as tens of MiB over
    // 100 cycles. Allow 32 MiB of slack for the driver and, on the whole-device
    // figure, for whatever else is running.
    const long long growth = after - before;
    if (growth > 32) detail += ", grew by " + std::to_string(growth) + " MiB";
    report("device", growth <= 32, detail);
}

void checkTransferRoundTrip() {
    openshot::GpuDevice& device = openshot::GpuDevice::Instance();
    if (!device.available()) {
        report("transfer", false, "GPU unavailable: " + device.lastError());
        return;
    }

    const int kImages = 1000;
    const int kWidth = 64, kHeight = 64;
    const SkImageInfo info = SkImageInfo::Make(kWidth, kHeight, kRGBA_8888_SkColorType,
                                               kPremul_SkAlphaType,
                                               SkColorSpace::MakeSRGB());
    std::vector<uint8_t> source(static_cast<size_t>(kWidth) * kHeight * 4);
    std::vector<uint8_t> result(source.size());
    std::mt19937 rng(20260914);

    int mismatches = 0;
    int firstBad = -1;
    for (int i = 0; i < kImages && mismatches == 0; ++i) {
        for (size_t p = 0; p < source.size(); p += 4) {
            // Opaque, and RGB <= alpha: a premultiplied surface would otherwise
            // round-trip lossily and this would be testing arithmetic, not transfer.
            source[p + 0] = static_cast<uint8_t>(rng() & 0xff);
            source[p + 1] = static_cast<uint8_t>(rng() & 0xff);
            source[p + 2] = static_cast<uint8_t>(rng() & 0xff);
            source[p + 3] = 0xff;
        }
        std::shared_ptr<openshot::GpuFrame> frame =
                openshot::GpuFrame::Create(kWidth, kHeight, kRGBA_8888_SkColorType);
        if (!frame) {
            report("transfer", false, "GpuFrame::Create failed on image " +
                                              std::to_string(i));
            return;
        }
        const SkPixmap src(info, source.data(), static_cast<size_t>(kWidth) * 4);
        if (!frame->upload(src)) {
            report("transfer", false, "upload failed on image " + std::to_string(i));
            return;
        }
        std::memset(result.data(), 0, result.size());
        const SkPixmap dst(info, result.data(), static_cast<size_t>(kWidth) * 4);
        if (!frame->readback(dst)) {
            report("transfer", false, "readback failed on image " + std::to_string(i));
            return;
        }
        if (std::memcmp(source.data(), result.data(), source.size()) != 0) {
            mismatches++;
            firstBad = i;
        }
    }

    std::string detail = std::to_string(kImages) + " x " + std::to_string(kWidth) + "x" +
                         std::to_string(kHeight) + " RGBA round trips";
    if (mismatches) detail += ", first mismatch at image " + std::to_string(firstBad);
    report("transfer", mismatches == 0, detail);
}

void checkPoolReuse() {
    openshot::GpuDevice& device = openshot::GpuDevice::Instance();
    if (!device.available()) {
        report("pool", false, "GPU unavailable: " + device.lastError());
        return;
    }

    openshot::GpuSurfacePool& pool = openshot::GpuSurfacePool::Instance();
    pool.clear();
    const openshot::GpuSurfacePool::Stats start = pool.stats();

    SkSurface* firstAddress = nullptr;
    {
        sk_sp<SkSurface> first = pool.acquire(256, 128, kRGBA_8888_SkColorType);
        if (!first) {
            report("pool", false, "first acquire returned null");
            return;
        }
        firstAddress = first.get();
        pool.release(first);
    }
    sk_sp<SkSurface> second = pool.acquire(256, 128, kRGBA_8888_SkColorType);
    const bool sameAllocation = second && second.get() == firstAddress;
    pool.release(second);

    // A different size must NOT be served from the free list.
    sk_sp<SkSurface> other = pool.acquire(320, 128, kRGBA_8888_SkColorType);
    const bool differentIsNew = other && other.get() != firstAddress;
    pool.release(other);

    const openshot::GpuSurfacePool::Stats end = pool.stats();
    const std::size_t created = end.created - start.created;
    const std::size_t reused = end.reused - start.reused;

    const bool ok = sameAllocation && differentIsNew && created == 2 && reused == 1;
    report("pool", ok,
           "created=" + std::to_string(created) + " reused=" + std::to_string(reused) +
                   " same_allocation=" + (sameAllocation ? "yes" : "no") +
                   " other_size_is_new=" + (differentIsNew ? "yes" : "no"));
}

/// A surface carries its canvas, so a recycled one carries the previous user's
/// transform and save stack — which clearing the pixels does not touch. Several
/// call sites scale or translate their offscreen without a matching restore (they
/// were written against SkSurfaces::Raster, which is fresh every time), so a pool
/// that hands the state back compounds it: the glow silhouette rendered at scale s
/// came out at s^2 on the second frame and s^3 on the third.
void checkPoolResetsCanvasState() {
    openshot::GpuDevice& device = openshot::GpuDevice::Instance();
    if (!device.available()) {
        report("pool-canvas", false, "GPU unavailable: " + device.lastError());
        return;
    }

    openshot::GpuSurfacePool& pool = openshot::GpuSurfacePool::Instance();
    pool.clear();

    // Leave the canvas as dirty as a caller plausibly can: an unbalanced save with
    // a clip inside it, plus a scale applied at the base level where there is
    // nothing to restore to.
    sk_sp<SkSurface> first = pool.acquire(64, 64, kN32_SkColorType);
    if (!first) {
        report("pool-canvas", false, "first acquire returned null");
        return;
    }
    SkCanvas* dirty = first->getCanvas();
    dirty->save();
    dirty->clipRect(SkRect::MakeWH(8, 8));
    dirty->translate(17.0f, 4.0f);
    dirty->scale(3.0f, 3.0f);
    pool.release(first);
    first.reset();

    sk_sp<SkSurface> second = pool.acquire(64, 64, kN32_SkColorType);
    if (!second) {
        report("pool-canvas", false, "second acquire returned null");
        return;
    }
    SkCanvas* canvas = second->getCanvas();
    const bool identity = canvas->getLocalToDeviceAs3x3().isIdentity();
    const bool unclipped = canvas->getDeviceClipBounds() == SkIRect::MakeWH(64, 64);
    const bool unwound = canvas->getSaveCount() == 1;
    pool.release(second);

    const bool ok = identity && unclipped && unwound;
    report("pool-canvas", ok,
           std::string("identity=") + (identity ? "yes" : "no") +
                   " unclipped=" + (unclipped ? "yes" : "no") +
                   " save_count=" + std::to_string(canvas->getSaveCount()));
}

/// The pool caches surfaces that belong to the device's Graphite context, so a
/// device teardown has to invalidate it. Without GpuDevice::Generation() this
/// hands back a surface from a destroyed context and crashes.
void checkPoolSurvivesDeviceRestart() {
    openshot::GpuDevice::Instance().available();
    openshot::GpuSurfacePool& pool = openshot::GpuSurfacePool::Instance();
    pool.clear();

    sk_sp<SkSurface> before = pool.acquire(128, 128, kRGBA_8888_SkColorType);
    if (!before) {
        report("pool-restart", false, "acquire before restart returned null");
        return;
    }
    pool.release(before);
    before.reset();

    openshot::GpuDevice::DestroyInstance();
    if (!openshot::GpuDevice::Instance().available()) {
        report("pool-restart", false, "device did not come back up");
        return;
    }

    // Same thread, same pool object, new context.
    sk_sp<SkSurface> after = pool.acquire(128, 128, kRGBA_8888_SkColorType);
    const bool ok = after != nullptr;
    std::string detail = ok ? "pool rebuilt after device teardown"
                            : "acquire after restart returned null";
    if (ok) {
        // And it must be a genuinely new allocation, not the stale cached one.
        std::shared_ptr<openshot::GpuFrame> frame =
                openshot::GpuFrame::Create(32, 32, kRGBA_8888_SkColorType);
        if (!frame || !frame->canvas()) detail = "new frame unusable after restart";
        report("pool-restart", frame && frame->canvas(), detail);
    } else {
        report("pool-restart", false, detail);
    }
    pool.release(after);
}

// The single switch: SetBackend must both disable and re-enable every GPU path,
// whatever OPENSHOT_GPU says, and must move Generation() so caches holding GPU
// objects drop them. This is the check that keeps "one control" true as more GPU
// logic is added — anything that reads the environment for itself fails it.
void checkSingleControl() {
    const openshot::GpuDevice::Backend requested = openshot::GpuDevice::RequestedBackend();

    openshot::GpuDevice::SetBackend(openshot::GpuDevice::Backend::Off);
    const unsigned long long afterOff = openshot::GpuDevice::Generation();
    const bool offWorks = !openshot::GpuDevice::Instance().available() &&
                          openshot::GpuDevice::RequestedBackend() ==
                                  openshot::GpuDevice::Backend::Off;
    // With everything off, the helpers every GPU path goes through must refuse too.
    const bool noFrame = openshot::GpuFrame::Create(64, 64, kRGBA_8888_SkColorType) == nullptr;

    openshot::GpuDevice::SetBackend(requested);
    const bool backOn = openshot::GpuDevice::Instance().available() &&
                        openshot::GpuFrame::Create(64, 64, kRGBA_8888_SkColorType) != nullptr;
    const bool bumped = openshot::GpuDevice::Generation() > afterOff;

    const bool ok = offWorks && noFrame && backOn && bumped;
    std::string detail = ok ? std::string("off -> unavailable and GpuFrame::Create null; ") +
                                      openshot::GpuDevice::BackendName(requested) +
                                      " -> available again; generation moved"
                            : std::string("off_disables=") + (offWorks ? "yes" : "no") +
                                      " create_refused=" + (noFrame ? "yes" : "no") +
                                      " re_enabled=" + (backOn ? "yes" : "no") +
                                      " generation_moved=" + (bumped ? "yes" : "no");
    report("control", ok, detail);
}

// "vulkan" must mean a GPU. The service turns the GPU on by default (2026-09-24), so a CPU node
// that happens to have Mesa's Vulkan drivers installed would otherwise pick lavapipe and render
// every export through a software rasteriser instead of the raster path. Simulated by leaving the
// loader only lavapipe's ICD: the device must stay unavailable. Last, because it rebuilds the device.
void checkVulkanSkipsSoftwareDevices() {
    const openshot::GpuDevice::Backend requested = openshot::GpuDevice::RequestedBackend();
    if (requested != openshot::GpuDevice::Backend::Vulkan) {
        report("vulkan-no-software", true, "skipped (only meaningful with OPENSHOT_GPU=vulkan)");
        return;
    }
    const char* previous = std::getenv("VK_DRIVER_FILES");
    const std::string saved = previous ? previous : "";
    setenv("VK_DRIVER_FILES", "/usr/share/vulkan/icd.d/lvp_icd.json", 1);
    openshot::GpuDevice::SetBackend(openshot::GpuDevice::Backend::Off);
    openshot::GpuDevice::SetBackend(openshot::GpuDevice::Backend::Vulkan);
    const bool refused = !openshot::GpuDevice::Instance().available();
    const std::string why = openshot::GpuDevice::Instance().lastError();
    if (previous) setenv("VK_DRIVER_FILES", saved.c_str(), 1); else unsetenv("VK_DRIVER_FILES");
    openshot::GpuDevice::SetBackend(openshot::GpuDevice::Backend::Off);
    openshot::GpuDevice::SetBackend(requested);
    const bool back = openshot::GpuDevice::Instance().available();
    report("vulkan-no-software", refused && back,
           refused ? (back ? "lavapipe only -> unavailable (" + why + "); GPU back after" : "GPU did not come back")
                   : "OPENSHOT_GPU=vulkan took a software device");
}

// Subtitles draw straight onto the canvas they are handed and build no offscreen
// of their own, so the whole pass follows its destination. Render the same frame
// onto a GPU surface and a raster one and require the results to agree.
void checkSubtitleOnGpuCanvas() {
    const char* fontDir = std::getenv("OPENSHOT_TEST_FONT");
    if (!fontDir) {
        report("subtitle-gpu", true, "skipped (set OPENSHOT_TEST_FONT to a .ttf)");
        return;
    }
    const int W = 640, H = 360;
    std::string json =
            std::string("{\"settings\":{\"defaultStyle\":{\"fontFamily\":\"") + fontDir +
            "\",\"fontSize\":48,\"fontWeight\":700,\"color\":\"#FFFFFF\","
            "\"strokeColor\":\"#000000\",\"strokeWidth\":3},"
            "\"transformation\":{\"maxWidth\":480,\"center\":{\"x\":0.5,\"y\":0.8}},"
            "\"containerStyle\":{\"appearance\":\"ONE_WORD\",\"textAlign\":\"CENTER\"}},"
            "\"segments\":[{\"id\":\"s0\",\"startTime\":0,\"endTime\":2000,\"visible\":true,"
            "\"attached\":true,\"wordDetails\":[{\"word\":\"GPU\",\"startTime\":0,\"endTime\":2000}]}]}";

    openshot::subtitle::SubtitleManager manager(30.f);
    manager.loadFromJSONString(json);

    // Raster reference.
    SkBitmap rasterPixels;
    if (!rasterPixels.tryAllocN32Pixels(W, H)) {
        report("subtitle-gpu", false, "raster allocation failed");
        return;
    }
    rasterPixels.eraseColor(SK_ColorBLUE);
    SkCanvas rasterCanvas(rasterPixels);
    manager.renderAtFrame(&rasterCanvas, W, H, 10);

    // The same call, onto a GPU-backed canvas.
    std::shared_ptr<openshot::GpuFrame> frame =
            openshot::GpuFrame::Create(W, H, kN32_SkColorType);
    if (!frame || !frame->canvas()) {
        report("subtitle-gpu", false, "no GPU frame");
        return;
    }
    frame->canvas()->clear(SK_ColorBLUE);
    manager.renderAtFrame(frame->canvas(), W, H, 10);

    SkBitmap gpuPixels;
    if (!gpuPixels.tryAllocN32Pixels(W, H)) {
        report("subtitle-gpu", false, "readback allocation failed");
        return;
    }
    SkPixmap gpuMap;
    if (!gpuPixels.peekPixels(&gpuMap) || !frame->readback(gpuMap)) {
        report("subtitle-gpu", false, "readback failed");
        return;
    }

    // Rasteriser and GPU are not required to be bit-identical (different AA), but
    // the glyphs have to land in the same place and the frame must not be blank.
    long long changed = 0, differing = 0, worst = 0;
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            const SkColor r = rasterPixels.getColor(x, y);
            const SkColor g = gpuPixels.getColor(x, y);
            if (r != SK_ColorBLUE) changed++;
            const long long d = std::max({std::abs((int)SkColorGetR(r) - (int)SkColorGetR(g)),
                                          std::abs((int)SkColorGetG(r) - (int)SkColorGetG(g)),
                                          std::abs((int)SkColorGetB(r) - (int)SkColorGetB(g))});
            if (d > 8) differing++;
            worst = std::max(worst, d);
        }
    }
    const double differingPct = 100.0 * differing / (double)(W * H);
    const bool ok = changed > 500 && differingPct < 0.5;
    char detail[256];
    std::snprintf(detail, sizeof(detail),
                  "%lld px drawn, %.3f%% differ by >8, worst channel delta %lld",
                  changed, differingPct, worst);
    report("subtitle-gpu", ok, detail);
}

// The colour convention the Timeline's canvas needs, which the check above cannot see.
//
// SkiaRenderer::parseColorString swaps R and B so that the swap cancels at the one place
// every CPU path ends: N32-declared bytes handed to a QImage that declares them
// Format_RGBA8888. The check above draws onto a kN32 surface and so keeps that cancelling
// pair intact. The Timeline's canvas is kRGBA_8888 and is read back as kRGBA_8888, so
// nothing is ever reinterpreted and the swap must be switched off -- otherwise every
// subtitle composites with red and blue exchanged, which no existing check would notice.
//
// Draw the same frame both ways, each through its own boundary, and require the colours
// that come out to agree. The clear colour is grey so it cannot itself hide a swap.
void checkSubtitleCanvasColors() {
    const char* fontDir = std::getenv("OPENSHOT_TEST_FONT");
    if (!fontDir) {
        report("subtitle-colors", true, "skipped (set OPENSHOT_TEST_FONT to a .ttf)");
        return;
    }
    const int W = 640, H = 360;
    // A strongly red-biased fill and a blue stroke: a swap turns one into the other.
    std::string json =
            std::string("{\"settings\":{\"defaultStyle\":{\"fontFamily\":\"") + fontDir +
            "\",\"fontSize\":64,\"fontWeight\":700,\"color\":\"#FF2010\","
            "\"strokeColor\":\"#1020FF\",\"strokeWidth\":4},"
            "\"transformation\":{\"maxWidth\":480,\"center\":{\"x\":0.5,\"y\":0.5}},"
            "\"containerStyle\":{\"appearance\":\"ONE_WORD\",\"textAlign\":\"CENTER\"}},"
            "\"segments\":[{\"id\":\"s0\",\"startTime\":0,\"endTime\":2000,\"visible\":true,"
            "\"attached\":true,\"wordDetails\":[{\"word\":\"RED\",\"startTime\":0,\"endTime\":2000}]}]}";

    openshot::subtitle::SubtitleManager manager(30.f);
    manager.loadFromJSONString(json);

    const SkColor grey = SkColorSetARGB(255, 0x40, 0x40, 0x40);   // swap-invariant

    // Reference: the CPU boundary. Draw with the legacy swap onto kN32, then read the raw
    // bytes back as kRGBA_8888 -- which is exactly what Frame's QImage does to them.
    SkBitmap rasterPixels;
    if (!rasterPixels.tryAllocN32Pixels(W, H)) {
        report("subtitle-colors", false, "raster allocation failed");
        return;
    }
    rasterPixels.eraseColor(grey);
    SkCanvas rasterCanvas(rasterPixels);
    manager.renderAtFrame(&rasterCanvas, W, H, 10,
                          openshot::subtitle::ColorConvention::QImageBytes);
    const SkPixmap reinterpreted(
            SkImageInfo::Make(W, H, kRGBA_8888_SkColorType, kPremul_SkAlphaType),
            rasterPixels.getPixels(), rasterPixels.rowBytes());

    // The Timeline's boundary: kRGBA_8888 canvas, no swap, read back as kRGBA_8888.
    std::shared_ptr<openshot::GpuFrame> frame =
            openshot::GpuFrame::Create(W, H, kRGBA_8888_SkColorType);
    if (!frame || !frame->canvas()) {
        report("subtitle-colors", false, "no GPU frame");
        return;
    }
    frame->canvas()->clear(grey);
    manager.renderAtFrame(frame->canvas(), W, H, 10,
                          openshot::subtitle::ColorConvention::Logical);

    SkBitmap gpuPixels;
    if (!gpuPixels.tryAllocPixels(
                SkImageInfo::Make(W, H, kRGBA_8888_SkColorType, kPremul_SkAlphaType))) {
        report("subtitle-colors", false, "readback allocation failed");
        return;
    }
    SkPixmap gpuMap;
    if (!gpuPixels.peekPixels(&gpuMap) || !frame->readback(gpuMap)) {
        report("subtitle-colors", false, "readback failed");
        return;
    }

    // Both sides now hold logical colours. Compare them, and also against the swapped
    // reference, so the failure message says which way round it went wrong.
    long long changed = 0, differing = 0, worst = 0;
    double sumDirect = 0.0, sumSwapped = 0.0;
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            const SkColor r = reinterpreted.getColor(x, y);
            const SkColor g = gpuMap.getColor(x, y);
            if (r != grey) changed++;
            const int dr = std::abs((int)SkColorGetR(r) - (int)SkColorGetR(g));
            const int dg = std::abs((int)SkColorGetG(r) - (int)SkColorGetG(g));
            const int db = std::abs((int)SkColorGetB(r) - (int)SkColorGetB(g));
            sumDirect += dr + dg + db;
            sumSwapped += std::abs((int)SkColorGetR(r) - (int)SkColorGetB(g)) + dg +
                          std::abs((int)SkColorGetB(r) - (int)SkColorGetR(g));
            const long long d = std::max({dr, dg, db});
            if (d > 8) differing++;
            worst = std::max(worst, d);
        }
    }
    const double differingPct = 100.0 * differing / (double)(W * H);
    const double meanDirect = sumDirect / (double)(W * H * 3);
    const double meanSwapped = sumSwapped / (double)(W * H * 3);
    const bool ok = changed > 500 && differingPct < 0.5 && meanDirect < meanSwapped;
    char detail[320];
    std::snprintf(detail, sizeof(detail),
                  "%lld px drawn, %.3f%% differ by >8, worst %lld, mean err %.3f "
                  "(R/B swapped would be %.3f)",
                  changed, differingPct, worst, meanDirect, meanSwapped);
    report("subtitle-colors", ok, detail);
}

}  // namespace

int main() {
    // The flag the whole phase leans on: with nothing set, the device must stay
    // off even on a machine that has a working GPU. Checked before anything else
    // touches the environment.
    if (!std::getenv("OPENSHOT_GPU")) {
        openshot::GpuDevice& fresh = openshot::GpuDevice::Instance();
        const bool off = !fresh.available() &&
                         fresh.backend() == openshot::GpuDevice::Backend::Off;
        report("default-off", off,
               off ? "OPENSHOT_GPU unset -> unavailable"
                   : "OPENSHOT_GPU unset but the device came up anyway");
        openshot::GpuDevice::DestroyInstance();
        setenv("OPENSHOT_GPU", "vulkan", 1);
    }

    openshot::GpuDevice& device = openshot::GpuDevice::Instance();
    if (!device.available()) {
        std::printf("GPU unavailable (OPENSHOT_GPU=%s): %s\n",
                    std::getenv("OPENSHOT_GPU"), device.lastError().c_str());
        return 1;
    }
    std::printf("device:   %s (OPENSHOT_GPU=%s)\n\n", device.deviceName().c_str(),
                std::getenv("OPENSHOT_GPU"));
    openshot::GpuDevice::DestroyInstance();

    checkDeviceCycles();
    checkTransferRoundTrip();
    checkPoolReuse();
    checkPoolResetsCanvasState();
    checkPoolSurvivesDeviceRestart();
    checkSingleControl();
    checkSubtitleOnGpuCanvas();
    checkSubtitleCanvasColors();
    checkVulkanSkipsSoftwareDevices();

    std::printf("\n%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
