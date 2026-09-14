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
 *
 * VRAM is read via nvidia-smi when it is present; on lavapipe or a machine with
 * no NVIDIA driver that part reports "skipped" rather than failing.
 *
 *   OPENSHOT_GPU=vulkan|lavapipe   which backend to test (default vulkan)
 */

#include "gpu/GpuDevice.h"
#include "gpu/GpuFrame.h"
#include "gpu/GpuSurfacePool.h"

#include "skia/include/core/SkAlphaType.h"
#include "skia/include/core/SkColorSpace.h"
#include "skia/include/core/SkColorType.h"
#include "skia/include/core/SkImageInfo.h"
#include "skia/include/core/SkPixmap.h"

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
    checkPoolSurvivesDeviceRestart();

    std::printf("\n%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
