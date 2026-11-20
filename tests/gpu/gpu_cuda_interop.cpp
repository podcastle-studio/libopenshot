/*
 * openshot-gpu-cuda-interop — the acceptance gate for W22 (src/gpu/CudaInterop).
 *
 * Fill a CUDA NV12 buffer with a known pattern, copy it into the two exportable
 * Vulkan images device-to-device, sample both planes in a trivial SkSL shader,
 * read the result back and compare it **exactly** against the pattern. Then time
 * the copy at 4K.
 *
 *   1. copy      an NV12 pattern round-trips through CUDA -> Vulkan -> SkSL
 *                exactly, at 640x360 and at 3840x2160
 *   2. lifetime  a device teardown invalidates the images instead of crashing,
 *                and the interop comes back afterwards
 *   3. cost      the per-frame cost of the copy at 3840x2160, against 0.3 ms
 *
 * The interop declines — normally, not as a failure — with the GPU off, on
 * lavapipe (no external_semaphore_fd), and where there is no CUDA driver. That
 * is reported as SKIP and exits 0, which is what the lavapipe arm expects.
 *
 *   OPENSHOT_GPU=vulkan|lavapipe   which backend to test (default vulkan)
 *
 * Run it under compute-sanitizer for the second half of the gate:
 *   compute-sanitizer --tool memcheck --leak-check=full openshot-gpu-cuda-interop
 */

#include "gpu/CudaInterop.h"
#include "gpu/GpuDevice.h"
#include "gpu/GpuFrame.h"

#include "skia/include/core/SkAlphaType.h"
#include "skia/include/core/SkBlendMode.h"
#include "skia/include/core/SkCanvas.h"
#include "skia/include/core/SkColorType.h"
#include "skia/include/core/SkImage.h"
#include "skia/include/core/SkImageInfo.h"
#include "skia/include/core/SkPaint.h"
#include "skia/include/core/SkPixmap.h"
#include "skia/include/core/SkSamplingOptions.h"
#include "skia/include/core/SkShader.h"
#include "skia/include/core/SkTileMode.h"
#include "skia/include/effects/SkRuntimeEffect.h"

#include <cuda.h>

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
}

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <string>
#include <vector>

namespace {

int failures = 0;

void report(const char* name, bool ok, const std::string& detail) {
    std::printf("%-6s %-10s %s\n", ok ? "PASS" : "FAIL", name, detail.c_str());
    if (!ok) failures++;
}

std::string cudaError(const char* what, CUresult result) {
    const char* name = nullptr;
    cuGetErrorName(result, &name);
    return std::string(what) + ": " + (name ? name : "unknown");
}

// The pattern. Deliberately not smooth: neighbouring texels differ, so a copy
// that lands one row or one column out cannot pass, and a chroma plane sampled
// at the wrong scale cannot either.
uint8_t lumaAt(int x, int y)   { return static_cast<uint8_t>((x * 7 + y * 13) & 0xFF); }
uint8_t chromaU(int x, int y)  { return static_cast<uint8_t>((x * 3 + y * 5 + 17) & 0xFF); }
uint8_t chromaV(int x, int y)  { return static_cast<uint8_t>(255 - ((x * 11 + y * 2) & 0xFF)); }

// The shader under test: one luma plane at full resolution, one interleaved
// chroma plane at half, both sampled nearest so the comparison can be exact.
const char* kSampleBothPlanes = R"(
uniform shader luma;
uniform shader chroma;
half4 main(float2 p) {
    half  y  = luma.eval(p).r;
    half2 uv = chroma.eval(p * 0.5).rg;
    return half4(y, uv.r, uv.g, 1.0);
}
)";

/// An NV12 frame in CUDA device memory, with the AVFrame a decoder would hand us.
struct CudaNV12 {
    CUdeviceptr luma = 0;
    CUdeviceptr chroma = 0;
    size_t luma_pitch = 0;
    size_t chroma_pitch = 0;
    AVFrame frame{};

    bool allocate(int width, int height, std::string& error) {
        const int cw = (width + 1) / 2;
        const int ch = (height + 1) / 2;
        CUresult result = cuMemAllocPitch(&luma, &luma_pitch, width, height, 16);
        if (result != CUDA_SUCCESS) { error = cudaError("cuMemAllocPitch (luma)", result); return false; }
        result = cuMemAllocPitch(&chroma, &chroma_pitch, cw * 2, ch, 16);
        if (result != CUDA_SUCCESS) { error = cudaError("cuMemAllocPitch (chroma)", result); return false; }

        std::vector<uint8_t> plane(static_cast<size_t>(width) * height);
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x)
                plane[static_cast<size_t>(y) * width + x] = lumaAt(x, y);
        if (!upload(luma, luma_pitch, plane.data(), width, height, error)) return false;

        std::vector<uint8_t> interleaved(static_cast<size_t>(cw) * ch * 2);
        for (int y = 0; y < ch; ++y)
            for (int x = 0; x < cw; ++x) {
                interleaved[(static_cast<size_t>(y) * cw + x) * 2 + 0] = chromaU(x, y);
                interleaved[(static_cast<size_t>(y) * cw + x) * 2 + 1] = chromaV(x, y);
            }
        if (!upload(chroma, chroma_pitch, interleaved.data(), cw * 2, ch, error)) return false;

        frame.format = AV_PIX_FMT_CUDA;
        frame.width = width;
        frame.height = height;
        frame.data[0] = reinterpret_cast<uint8_t*>(luma);
        frame.data[1] = reinterpret_cast<uint8_t*>(chroma);
        frame.linesize[0] = static_cast<int>(luma_pitch);
        frame.linesize[1] = static_cast<int>(chroma_pitch);
        return true;
    }

    void release() {
        if (luma) cuMemFree(luma);
        if (chroma) cuMemFree(chroma);
        luma = chroma = 0;
    }

private:
    static bool upload(CUdeviceptr destination, size_t pitch, const uint8_t* source,
                       int width_in_bytes, int height, std::string& error) {
        CUDA_MEMCPY2D copy{};
        copy.srcMemoryType = CU_MEMORYTYPE_HOST;
        copy.srcHost = source;
        copy.srcPitch = static_cast<size_t>(width_in_bytes);
        copy.dstMemoryType = CU_MEMORYTYPE_DEVICE;
        copy.dstDevice = destination;
        copy.dstPitch = pitch;
        copy.WidthInBytes = static_cast<size_t>(width_in_bytes);
        copy.Height = static_cast<size_t>(height);
        const CUresult result = cuMemcpy2D(&copy);
        if (result != CUDA_SUCCESS) { error = cudaError("cuMemcpy2D (upload)", result); return false; }
        return true;
    }
};

/// Draw the two planes through the shader onto an RGBA frame and read it back.
bool renderAndRead(openshot::CudaInterop& interop, openshot::GpuImage& y,
                   openshot::GpuImage& uv, int width, int height,
                   std::vector<uint8_t>& pixels, std::string& error) {
    auto effect = SkRuntimeEffect::MakeForShader(SkString(kSampleBothPlanes));
    if (!effect.effect) { error = std::string("SkSL: ") + effect.errorText.c_str(); return false; }

    sk_sp<SkImage> luma = y.image();
    sk_sp<SkImage> chroma = uv.image();
    if (!luma || !chroma) { error = "GpuImage::image() returned null"; return false; }

    const SkSamplingOptions nearest(SkFilterMode::kNearest, SkMipmapMode::kNone);
    sk_sp<SkShader> children[2] = {
        luma->makeShader(SkTileMode::kClamp, SkTileMode::kClamp, nearest),
        chroma->makeShader(SkTileMode::kClamp, SkTileMode::kClamp, nearest),
    };
    SkRuntimeShaderBuilder builder(effect.effect);
    builder.child("luma") = children[0];
    builder.child("chroma") = children[1];
    sk_sp<SkShader> shader = builder.makeShader();
    if (!shader) { error = "the runtime effect made no shader"; return false; }

    std::shared_ptr<openshot::GpuFrame> target =
        openshot::GpuFrame::Create(width, height, kRGBA_8888_SkColorType);
    if (!target) { error = "GpuFrame::Create failed"; return false; }

    SkPaint paint;
    paint.setShader(shader);
    paint.setBlendMode(SkBlendMode::kSrc);   // replace, a pooled surface is dirty
    target->canvas()->drawPaint(paint);

    // The copy is still in flight on the CUDA stream; this is the submit that
    // waits for it, and the one that consumes the binary semaphore.
    const unsigned long long wait = interop.waitSemaphore(y);
    if (!openshot::GpuDevice::Instance().submit(false, &wait, 1)) {
        error = "submit with the interop's wait semaphore failed";
        return false;
    }

    pixels.assign(static_cast<size_t>(width) * height * 4, 0);
    const SkImageInfo info =
        SkImageInfo::Make(width, height, kRGBA_8888_SkColorType, kPremul_SkAlphaType);
    const SkPixmap destination(info, pixels.data(), static_cast<size_t>(width) * 4);
    if (!target->readback(destination)) { error = "readback failed"; return false; }
    return true;
}

void checkCopy(openshot::CudaInterop& interop, int width, int height, const char* label) {
    std::string error;
    CudaNV12 source;
    if (!source.allocate(width, height, error)) {
        source.release();
        report("copy", false, std::string(label) + ": " + error);
        return;
    }

    auto y = interop.createImage(width, height, openshot::GpuImage::Format::R8);
    auto uv = interop.createImage((width + 1) / 2, (height + 1) / 2,
                                  openshot::GpuImage::Format::R8G8);
    if (!y || !uv) {
        source.release();
        report("copy", false, std::string(label) + ": createImage failed: " + interop.lastError());
        return;
    }

    if (!interop.copyNV12(&source.frame, *y, *uv)) {
        source.release();
        report("copy", false, std::string(label) + ": copyNV12 failed: " + interop.lastError());
        return;
    }

    std::vector<uint8_t> pixels;
    if (!renderAndRead(interop, *y, *uv, width, height, pixels, error)) {
        source.release();
        report("copy", false, std::string(label) + ": " + error);
        return;
    }
    source.release();

    long long wrong = 0;
    std::string first;
    for (int py = 0; py < height; ++py) {
        for (int px = 0; px < width; ++px) {
            const uint8_t* got = &pixels[(static_cast<size_t>(py) * width + px) * 4];
            const uint8_t expected[4] = {lumaAt(px, py), chromaU(px / 2, py / 2),
                                         chromaV(px / 2, py / 2), 255};
            if (std::memcmp(got, expected, 4) == 0) continue;
            if (wrong == 0) {
                char buffer[160];
                std::snprintf(buffer, sizeof(buffer),
                              " first at (%d,%d): got %u,%u,%u,%u want %u,%u,%u,%u", px, py,
                              got[0], got[1], got[2], got[3], expected[0], expected[1],
                              expected[2], expected[3]);
                first = buffer;
            }
            ++wrong;
        }
    }

    char detail[256];
    std::snprintf(detail, sizeof(detail), "%s %dx%d: %lld of %lld pixels differ%s", label, width,
                  height, wrong, static_cast<long long>(width) * height, first.c_str());
    report("copy", wrong == 0, detail);
}

// Two image pairs, alternating, with nothing between them that waits on the CPU --
// what two NVDEC readers in one timeline do (a transition, a video matte). With one
// Vulkan->CUDA semaphore for the whole interop this hung inside a few frames: the
// second pair's barrier signalled it again before the first signal had been waited
// on, and the CUDA wait that lost its signal stalled the stream for good. A
// readback per frame hides it, which is why "copy" above never saw it.
void checkPairs(openshot::CudaInterop& interop) {
    std::string error;
    const int sizes[2][2] = {{640, 360}, {854, 480}};
    CudaNV12 sources[2];
    std::shared_ptr<openshot::GpuImage> ys[2], uvs[2];
    for (int i = 0; i < 2; ++i) {
        const int w = sizes[i][0], h = sizes[i][1];
        if (!sources[i].allocate(w, h, error)) {
            for (auto& s : sources) s.release();
            report("pairs", false, error);
            return;
        }
        ys[i] = interop.createImage(w, h, openshot::GpuImage::Format::R8);
        uvs[i] = interop.createImage((w + 1) / 2, (h + 1) / 2, openshot::GpuImage::Format::R8G8);
        if (!ys[i] || !uvs[i]) {
            for (auto& s : sources) s.release();
            report("pairs", false, "createImage failed: " + interop.lastError());
            return;
        }
    }

    // Watchdog: a regression here is a hang, not a wrong answer.
    std::atomic<bool> finished{false};
    std::thread watchdog([&] {
        for (int i = 0; i < 300 && !finished; ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (!finished) {
            std::printf("FAIL   pairs      hung: two alternating pairs deadlocked the stream\n");
            std::fflush(stdout);
            std::_Exit(1);
        }
    });

    const int rounds = 400;
    bool ok = true;
    for (int round = 0; round < rounds && ok; ++round) {
        const int i = round % 2;
        const int w = sizes[i][0], h = sizes[i][1];
        if (!interop.copyNV12(&sources[i].frame, *ys[i], *uvs[i])) {
            error = "copyNV12 failed: " + interop.lastError();
            ok = false;
            break;
        }
        std::shared_ptr<openshot::GpuFrame> sink = openshot::GpuFrame::Create(w, h);
        sk_sp<SkImage> luma = ys[i]->image();
        sk_sp<SkImage> chroma = uvs[i]->image();
        if (!sink || !luma || !chroma) { error = "no sink or no image"; ok = false; break; }
        sink->canvas()->drawImage(luma, 0, 0);
        sink->canvas()->drawImage(chroma, 0, 0);
        const unsigned long long wait = interop.waitSemaphore(*ys[i]);
        if (!openshot::GpuDevice::Instance().submit(false, &wait, 1)) {
            error = "submit failed";
            ok = false;
            break;
        }
        interop.prepareForCopy(*ys[i], *uvs[i]);
    }

    // And both pairs still hold the right pixels afterwards.
    long long wrong = 0;
    for (int i = 0; i < 2 && ok; ++i) {
        const int w = sizes[i][0], h = sizes[i][1];
        std::vector<uint8_t> pixels;
        if (!interop.copyNV12(&sources[i].frame, *ys[i], *uvs[i]) ||
            !renderAndRead(interop, *ys[i], *uvs[i], w, h, pixels, error)) {
            ok = false;
            break;
        }
        for (int py = 0; py < h; ++py)
            for (int px = 0; px < w; ++px) {
                const uint8_t* got = &pixels[(static_cast<size_t>(py) * w + px) * 4];
                if (got[0] != lumaAt(px, py) || got[1] != chromaU(px / 2, py / 2) ||
                    got[2] != chromaV(px / 2, py / 2))
                    ++wrong;
            }
    }
    finished = true;
    watchdog.join();
    for (auto& s : sources) s.release();

    char detail[192];
    std::snprintf(detail, sizeof(detail),
                  "%d alternating copies across two pairs, no CPU sync; %lld pixels wrong after%s%s",
                  rounds, wrong, error.empty() ? "" : "; ", error.c_str());
    report("pairs", ok && wrong == 0, detail);
}

// Nothing the interop hands out may outlive the device. Tear the device down
// while an image is still held and the image must go invalid rather than take
// the driver with it — and the interop must come back for the next frame.
void checkLifetime() {
    openshot::CudaInterop& interop = openshot::CudaInterop::Instance();
    auto image = interop.createImage(64, 64, openshot::GpuImage::Format::R8);
    if (!image) {
        report("lifetime", false, "createImage failed: " + interop.lastError());
        return;
    }

    const openshot::GpuDevice::Backend requested = openshot::GpuDevice::RequestedBackend();
    openshot::GpuDevice::SetBackend(openshot::GpuDevice::Backend::Off);
    const bool invalidated = !image->valid() && image->image() == nullptr;
    const bool declined = !openshot::CudaInterop::Instance().available();
    image.reset();   // must not touch the dead device

    openshot::GpuDevice::SetBackend(requested);
    const bool back = openshot::CudaInterop::Instance().available();
    auto again = openshot::CudaInterop::Instance().createImage(
        64, 64, openshot::GpuImage::Format::R8);
    const bool usable = back && again != nullptr && again->valid();

    const bool ok = invalidated && declined && usable;
    report("lifetime", ok,
           ok ? "teardown invalidates live images; the interop rebuilds afterwards"
              : std::string("invalidated=") + (invalidated ? "yes" : "no") +
                        " declined_while_off=" + (declined ? "yes" : "no") +
                        " rebuilt=" + (usable ? "yes" : "no"));
}

/// Mean milliseconds of one copyNV12 at this size, measured on the CUDA stream.
/// With @a pipelined the images are handed back to CUDA right after the drawing
/// that read them is submitted, which is how a real frame loop uses them; without
/// it, every copy pays the layout handshake on its own critical path.
bool measure(openshot::CudaInterop& interop, int width, int height, int iterations,
             bool pipelined, double& mean_ms, std::string& error) {
    CudaNV12 source;
    if (!source.allocate(width, height, error)) { source.release(); return false; }
    auto y = interop.createImage(width, height, openshot::GpuImage::Format::R8);
    auto uv = interop.createImage((width + 1) / 2, (height + 1) / 2,
                                  openshot::GpuImage::Format::R8G8);
    auto sink = openshot::GpuFrame::Create(64, 64, kRGBA_8888_SkColorType);
    if (!y || !uv || !sink) { source.release(); error = "setup failed"; return false; }

    CUstream stream = static_cast<CUstream>(interop.cudaStream());
    CUevent start = nullptr, stop = nullptr;
    cuEventCreate(&start, CU_EVENT_DEFAULT);
    cuEventCreate(&stop, CU_EVENT_DEFAULT);

    double total = 0;
    int counted = 0;
    for (int i = 0; i < iterations; ++i) {
        cuEventRecord(start, stream);
        if (!interop.copyNV12(&source.frame, *y, *uv)) {
            error = "copyNV12 failed: " + interop.lastError();
            break;
        }
        cuEventRecord(stop, stream);
        // Consume the binary semaphore with a real (tiny) recording, the way a
        // frame would, then wait for the stream so the events are readable.
        sink->canvas()->clear(SK_ColorBLACK);
        const unsigned long long wait = interop.waitSemaphore(*y);
        openshot::GpuDevice::Instance().submit(true, &wait, 1);
        if (pipelined) interop.prepareForCopy(*y, *uv);
        cuStreamSynchronize(stream);
        float elapsed = 0;
        if (cuEventElapsedTime(&elapsed, start, stop) != CUDA_SUCCESS) continue;
        if (i == 0) continue;   // first one pays for the pipeline warming up
        total += elapsed;
        ++counted;
    }
    cuEventDestroy(start);
    cuEventDestroy(stop);
    source.release();
    if (counted == 0) { if (error.empty()) error = "no timed iterations"; return false; }
    mean_ms = total / counted;
    return true;
}

void checkCost(openshot::CudaInterop& interop) {
    const int iterations = 60;
    double uhd = 0, uhd_serial = 0, tiny = 0;
    std::string error;
    if (!measure(interop, 3840, 2160, iterations, true, uhd, error) ||
        !measure(interop, 3840, 2160, iterations, false, uhd_serial, error) ||
        !measure(interop, 64, 64, iterations, true, tiny, error)) {
        report("cost", false, error);
        return;
    }
    // The 64x64 arm copies nothing worth measuring, so it is the fixed cost of
    // the semaphores; the difference is what moving 12.4 MB costs.
    char detail[256];
    std::snprintf(detail, sizeof(detail),
                  "3840x2160 %.3f ms/frame (gate 0.300); fixed %.3f, copy %.3f; "
                  "%.3f without prepareForCopy",
                  uhd, tiny, uhd - tiny, uhd_serial);
    report("cost", uhd <= 0.300, detail);
}

}   // namespace

int main() {
    const char* requested = std::getenv("OPENSHOT_GPU");
    std::printf("backend: %s\n", requested ? requested : "off (OPENSHOT_GPU unset)");

    openshot::GpuDevice& device = openshot::GpuDevice::Instance();
    if (!device.available()) {
        std::printf("SKIP   device     %s\n", device.lastError().c_str());
        return 0;
    }
    std::printf("device:  %s\n", device.deviceName().c_str());

    openshot::CudaInterop& interop = openshot::CudaInterop::Instance();
    if (!interop.available()) {
        // The expected answer on lavapipe and on any machine with no CUDA.
        std::printf("SKIP   interop    %s\n", interop.lastError().c_str());
        return 0;
    }
    std::printf("cuda:    %s\n", interop.deviceName().c_str());

    // This file's own CUDA calls — the pattern buffers and the timing events —
    // have to be in the interop's context, which is the one a decoder would be
    // using too. The interop pushes it for itself; this is for us.
    cuCtxSetCurrent(static_cast<CUcontext>(interop.cudaContext()));

    checkCopy(interop, 640, 360, "small");
    checkCopy(interop, 3840, 2160, "uhd");
    checkCost(interop);
    checkPairs(interop);

    // checkLifetime releases that context, so stop pointing at it first.
    cuCtxSetCurrent(nullptr);
    checkLifetime();

    openshot::GpuDevice::DestroyInstance();
    std::printf("%s (%d failure(s))\n", failures ? "FAILED" : "OK", failures);
    return failures ? 1 : 0;
}
