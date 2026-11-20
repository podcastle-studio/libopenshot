/*
 * gpu_blend_parity — does SkBlendMode agree with BlendModes.cpp on identical inputs?
 *
 * W13 maps the fork's 16 W3C blend modes onto Skia's. Both claim to implement "Compositing and
 * Blending Level 1", so on the SAME pixels they should agree to rounding. The golden suite cannot
 * answer this on its own: there the clip is also resampled onto the canvas, and a steep blend
 * curve (colour-burn divides by the source) magnifies a sub-LSB resampling difference into a large
 * one. This isolates the formula from the resampling by blending with no transform at all.
 *
 *   OPENSHOT_GPU=vulkan cmake-build-gpu/tests/gpu/openshot-gpu-blend-parity
 */
#include "BlendModes.h"
#include "Enums.h"
#include "gpu/GpuDevice.h"
#include "gpu/GpuFrame.h"

#include "skia/include/core/SkAlphaType.h"
#include "skia/include/core/SkBitmap.h"
#include "skia/include/core/SkBlendMode.h"
#include "skia/include/core/SkCanvas.h"
#include "skia/include/core/SkColorType.h"
#include "skia/include/core/SkImage.h"
#include "skia/include/core/SkImageInfo.h"
#include "skia/include/core/SkPaint.h"
#include "skia/include/core/SkPixmap.h"
#include "skia/include/core/SkSamplingOptions.h"
#include "skia/include/core/SkSurface.h"

#include <QImage>
#include <cmath>
#include <cstdio>
#include <vector>

namespace {

SkBlendMode ToSk(openshot::BlendMode m) {
    switch (m) {
        case openshot::BLEND_NORMAL:      return SkBlendMode::kSrcOver;
        case openshot::BLEND_MULTIPLY:    return SkBlendMode::kMultiply;
        case openshot::BLEND_SCREEN:      return SkBlendMode::kScreen;
        case openshot::BLEND_OVERLAY:     return SkBlendMode::kOverlay;
        case openshot::BLEND_DARKEN:      return SkBlendMode::kDarken;
        case openshot::BLEND_LIGHTEN:     return SkBlendMode::kLighten;
        case openshot::BLEND_COLOR_DODGE: return SkBlendMode::kColorDodge;
        case openshot::BLEND_COLOR_BURN:  return SkBlendMode::kColorBurn;
        case openshot::BLEND_HARD_LIGHT:  return SkBlendMode::kHardLight;
        case openshot::BLEND_SOFT_LIGHT:  return SkBlendMode::kSoftLight;
        case openshot::BLEND_DIFFERENCE:  return SkBlendMode::kDifference;
        case openshot::BLEND_EXCLUSION:   return SkBlendMode::kExclusion;
        case openshot::BLEND_HUE:         return SkBlendMode::kHue;
        case openshot::BLEND_SATURATION:  return SkBlendMode::kSaturation;
        case openshot::BLEND_COLOR:       return SkBlendMode::kColor;
        case openshot::BLEND_LUMINOSITY:  return SkBlendMode::kLuminosity;
    }
    return SkBlendMode::kSrcOver;
}

constexpr int kW = 256, kH = 256;

// Backdrop sweeps red/green across the full 8-bit range; source sweeps blue and alpha. Between
// them every (Cb, Cs) pair a blend can see is covered, including the 0 and 1 edges the spec
// special-cases.
QImage makeBackdrop() {
    QImage img(kW, kH, QImage::Format_RGBA8888_Premultiplied);
    for (int y = 0; y < kH; ++y)
        for (int x = 0; x < kW; ++x) {
            uchar* p = img.scanLine(y) + x * 4;
            p[0] = (uchar) x; p[1] = (uchar) y; p[2] = (uchar) ((x + y) / 2); p[3] = 255;
        }
    return img;
}

QImage makeSource() {
    QImage img(kW, kH, QImage::Format_RGBA8888_Premultiplied);
    for (int y = 0; y < kH; ++y)
        for (int x = 0; x < kW; ++x) {
            uchar* p = img.scanLine(y) + x * 4;
            // Premultiplied: colour may not exceed alpha.
            const int a = 255 - (y / 2);
            const int r = (x * a) / 255, g = ((255 - x) * a) / 255, b = ((x ^ y) * a) / 255;
            p[0] = (uchar) r; p[1] = (uchar) g; p[2] = (uchar) b; p[3] = (uchar) a;
        }
    return img;
}

} // namespace

int main() {
    if (!openshot::GpuDevice::Instance().available()) {
        std::printf("GPU not available (OPENSHOT_GPU unset or off) — nothing to compare.\n");
        return 0;
    }

    const QImage backdrop = makeBackdrop();
    const QImage source   = makeSource();

    std::printf("SkBlendMode vs BlendModes.cpp on identical pixels, no resampling (%dx%d)\n\n", kW, kH);
    std::printf("  %-14s %8s %8s %10s %10s\n", "mode", "max|d|", "mean|d|", ">1 LSB", ">2 LSB");

    int failures = 0;
    for (const auto mode : openshot::BlendModeList()) {
        // CPU reference
        QImage cpu = backdrop.copy();
        openshot::BlendImages(cpu, source, mode);

        // GPU: same two images, drawn at the origin with no transform and no filtering.
        auto frame = openshot::GpuFrame::Create(kW, kH, kRGBA_8888_SkColorType);
        if (!frame) { std::printf("  GpuFrame::Create failed\n"); return 1; }
        const SkPixmap bd(SkImageInfo::Make(kW, kH, kRGBA_8888_SkColorType, kPremul_SkAlphaType),
                          backdrop.constBits(), backdrop.bytesPerLine());
        if (!frame->upload(bd)) { std::printf("  upload failed\n"); return 1; }

        const SkPixmap srcpm(SkImageInfo::Make(kW, kH, kRGBA_8888_SkColorType, kPremul_SkAlphaType),
                             source.constBits(), source.bytesPerLine());
        sk_sp<SkImage> tex = openshot::GpuFrame::ToTexture(SkImages::RasterFromPixmap(srcpm, nullptr, nullptr));
        if (!tex) { std::printf("  ToTexture failed\n"); return 1; }

        SkPaint paint;
        paint.setBlendMode(ToSk(mode));
        frame->canvas()->drawImage(tex, 0, 0, SkSamplingOptions(), &paint);

        QImage gpu(kW, kH, QImage::Format_RGBA8888_Premultiplied);
        const SkPixmap dst(SkImageInfo::Make(kW, kH, kRGBA_8888_SkColorType, kPremul_SkAlphaType),
                           gpu.bits(), gpu.bytesPerLine());
        if (!frame->readback(dst)) { std::printf("  readback failed\n"); return 1; }

        long over1 = 0, over2 = 0, n = 0;
        double sum = 0; int worst = 0;
        for (int y = 0; y < kH; ++y) {
            const uchar* pc = cpu.constScanLine(y);
            const uchar* pg = gpu.constScanLine(y);
            for (int x = 0; x < kW; ++x) {
                for (int c = 0; c < 4; ++c) {
                    const int d = std::abs((int) pc[x*4+c] - (int) pg[x*4+c]);
                    worst = std::max(worst, d); sum += d; ++n;
                    if (d > 1) ++over1;
                    if (d > 2) ++over2;
                }
            }
        }
        // Judge on the bulk, not on the single worst channel. A wrong mapping is wrong
        // everywhere and blows both of these out by orders of magnitude; what the worst
        // channel catches instead is the driver's float behaviour at a formula's
        // singularity -- colour-dodge divides by (1 - Cs), colour-burn by Cs -- where the
        // two sides land on opposite sides of a clamp. Those are a handful of channels in
        // 262,144, and they appear only on the NVIDIA driver: lavapipe, which is strict
        // software IEEE, matches the CPU on all 16 modes. The max is still printed, so a
        // real divergence is visible rather than hidden by the threshold.
        const double mean = sum/n, pct2 = 100.0*over2/n;
        const bool ok = mean <= 0.2 && pct2 <= 0.05;
        if (!ok) ++failures;
        std::printf("  %-14s %8d %8.4f %9.3f%% %9.3f%%   %s\n",
                    openshot::BlendModeToString(mode).c_str(), worst, mean,
                    100.0*over1/n, pct2, ok ? "ok" : "DIFFERS");
    }
    std::printf("\n%d mode(s) disagree with BlendModes.cpp beyond rounding "
                "(mean > 0.2 LSB or > 0.05%% of channels over 2 LSB)\n", failures);
    return failures ? 1 : 0;
}
