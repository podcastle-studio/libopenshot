/*
 * gpu_effect_parity — is each GpuEffect's SkSL fragment the same function as its C++ twin?
 *
 * W19's per-effect gate: PSNR >= 48 dB against the CPU effect on eight images including
 * transparent and semi-transparent pixels, and <= 0.2 ms at 1080p. This measures both, and
 * also reports whether the two agree *exactly*, which is the stronger claim the fragments are
 * written for — see GpuEffect::GpuShaderPrelude() on why byte-for-byte parity is reachable at
 * all. A bit-exact shader lets the effects goldens keep Tolerance::Exact() on the GPU instead
 * of needing a tolerance band, so the distinction is worth printing separately.
 *
 * The images matter as much as the effects. Every CPU twin unpremultiplies, operates and
 * re-premultiplies, so the interesting pixels are the ones where that round trip loses
 * something: alpha 0 (undefined colour), alpha 1 (one part in 255), and alpha just under 255.
 *
 *   OPENSHOT_GPU=vulkan cmake-build-gpu/tests/gpu/openshot-gpu-effect-parity
 *   OPENSHOT_GPU=lavapipe cmake-build-gpu/tests/gpu/openshot-gpu-effect-parity
 */
#include "Frame.h"
#include "gpu/GpuFrame.h"
#include "KeyFrame.h"
#include "effects/Alpha.h"
#include "effects/Brightness.h"
#include "effects/ColorShift.h"
#include "effects/Exposure.h"
#include "gpu/GpuDevice.h"

#include <QImage>

#include "skia/include/core/SkBlendMode.h"
#include "skia/include/core/SkCanvas.h"
#include "skia/include/core/SkImage.h"
#include "skia/include/core/SkImageInfo.h"
#include "skia/include/core/SkPaint.h"
#include "skia/include/core/SkPixmap.h"
#include "skia/include/core/SkRect.h"
#include "skia/include/core/SkSamplingOptions.h"
#include "skia/include/core/SkShader.h"
#include "skia/include/core/SkString.h"
#include "skia/include/core/SkTileMode.h"
#include "skia/include/effects/SkRuntimeEffect.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <cstdint>
#include <functional>
#include <memory>
#include <limits>
#include <random>
#include <string>
#include <vector>

namespace {

constexpr int kW = 256, kH = 256;

// A premultiplied pixel, built so the invariant the CPU twins rely on always holds: no colour
// channel exceeds alpha. Feeding them anything else would test undefined behaviour rather than
// parity.
void setPremul(uchar* p, int r, int g, int b, int a) {
    p[0] = (uchar) std::min(r, a);
    p[1] = (uchar) std::min(g, a);
    p[2] = (uchar) std::min(b, a);
    p[3] = (uchar) a;
}

struct TestImage { std::string name; QImage image; };

std::vector<TestImage> makeImages() {
    std::vector<TestImage> images;
    auto blank = [] { return QImage(kW, kH, QImage::Format_RGBA8888_Premultiplied); };

    // 1. Every (r, g) pair at full alpha — the plain opaque case, and the one the goldens cover.
    {
        QImage img = blank();
        for (int y = 0; y < kH; ++y)
            for (int x = 0; x < kW; ++x)
                setPremul(img.scanLine(y) + x * 4, x, y, (x + y) / 2, 255);
        images.push_back({"opaque_ramp", img});
    }
    // 2. Colour ramp under an alpha ramp: the unpremultiply divides by every alpha there is.
    {
        QImage img = blank();
        for (int y = 0; y < kH; ++y)
            for (int x = 0; x < kW; ++x) {
                const int a = y;
                setPremul(img.scanLine(y) + x * 4, x * a / 255, (255 - x) * a / 255, a / 2, a);
            }
        images.push_back({"alpha_ramp", img});
    }
    // 3. Alpha 1 everywhere. One part in 255, so the unpremultiply divides by 1/255 and the
    //    re-premultiply throws almost everything away — the worst case for rounding, and the
    //    one where floor() vs round() shows up immediately.
    {
        QImage img = blank();
        for (int y = 0; y < kH; ++y)
            for (int x = 0; x < kW; ++x)
                setPremul(img.scanLine(y) + x * 4, x % 2, y % 2, 1, 1);
        images.push_back({"alpha_one", img});
    }
    // 4. Fully transparent. Colour is not observable, but the code still runs and must not
    //    produce a NaN out of a division by zero.
    {
        QImage img = blank();
        img.fill(QColor(0, 0, 0, 0));
        images.push_back({"transparent", img});
    }
    // 5. Alpha 254: opaque to the eye, but off the fast path every twin takes at 255.
    {
        QImage img = blank();
        for (int y = 0; y < kH; ++y)
            for (int x = 0; x < kW; ++x)
                setPremul(img.scanLine(y) + x * 4, x, y, 128, 254);
        images.push_back({"alpha_254", img});
    }
    // 6. The 0/255 corners of the cube, where a clamp either holds or does not.
    {
        QImage img = blank();
        const int corner[8][3] = {{0,0,0},{255,0,0},{0,255,0},{0,0,255},
                                  {255,255,0},{255,0,255},{0,255,255},{255,255,255}};
        for (int y = 0; y < kH; ++y)
            for (int x = 0; x < kW; ++x) {
                const int c = ((x / 32) + (y / 32) * 8) % 8;
                setPremul(img.scanLine(y) + x * 4, corner[c][0], corner[c][1], corner[c][2], 255);
            }
        images.push_back({"corners", img});
    }
    // 7. Flat mid-grey — the fixed point of a contrast curve about 128.
    {
        QImage img = blank();
        for (int y = 0; y < kH; ++y)
            for (int x = 0; x < kW; ++x)
                setPremul(img.scanLine(y) + x * 4, 128, 128, 128, 255);
        images.push_back({"mid_grey", img});
    }
    // 8. Noise in colour and alpha, fixed seed. Nothing structured left to hide behind.
    {
        QImage img = blank();
        std::mt19937 rng(20260922u);
        std::uniform_int_distribution<int> byte(0, 255);
        for (int y = 0; y < kH; ++y)
            for (int x = 0; x < kW; ++x) {
                const int a = byte(rng);
                setPremul(img.scanLine(y) + x * 4, byte(rng), byte(rng), byte(rng), a);
            }
        images.push_back({"noise", img});
    }
    return images;
}

std::shared_ptr<openshot::Frame> frameFrom(const QImage& img) {
    auto frame = std::make_shared<openshot::Frame>();
    frame->AddImage(std::make_shared<QImage>(img.copy()));
    return frame;
}

struct Delta { double psnr; int max_delta; long differing; long total; };

Delta compare(const QImage& a, const QImage& b) {
    Delta d{0.0, 0, 0, 0};
    double sum_squares = 0.0;
    for (int y = 0; y < a.height(); ++y) {
        const uchar* pa = a.scanLine(y);
        const uchar* pb = b.scanLine(y);
        for (int i = 0; i < a.width() * 4; ++i) {
            const int diff = std::abs((int) pa[i] - (int) pb[i]);
            if (diff > d.max_delta) d.max_delta = diff;
            if (diff) d.differing++;
            sum_squares += (double) diff * diff;
            d.total++;
        }
    }
    const double mse = sum_squares / (double) d.total;
    d.psnr = mse == 0.0 ? std::numeric_limits<double>::infinity()
                        : 10.0 * std::log10(255.0 * 255.0 / mse);
    return d;
}

using Factory = std::function<std::shared_ptr<openshot::EffectBase>()>;
struct Case { std::string name; Factory make; };

std::vector<Case> cases() {
    using openshot::Keyframe;
    return {
        {"brightness(0, 3)",      [] { return std::make_shared<openshot::Brightness>(Keyframe(0.0), Keyframe(3.0)); }},
        {"brightness(0.25, 10)",  [] { return std::make_shared<openshot::Brightness>(Keyframe(0.25), Keyframe(10.0)); }},
        {"brightness(-0.4, 20)",  [] { return std::make_shared<openshot::Brightness>(Keyframe(-0.4), Keyframe(20.0)); }},
        {"brightness(0.6, 100)",  [] { return std::make_shared<openshot::Brightness>(Keyframe(0.6), Keyframe(100.0)); }},

        // Alpha takes the CPU shortcuts at 0 and 1, so only the interior is a shader.
        {"alpha(0.5)",            [] { return std::make_shared<openshot::Alpha>(Keyframe(0.5)); }},
        {"alpha(0.13)",           [] { return std::make_shared<openshot::Alpha>(Keyframe(0.13)); }},

        {"exposure(1.0)",         [] { return std::make_shared<openshot::Exposure>(Keyframe(1.0)); }},
        {"exposure(1.7)",         [] { return std::make_shared<openshot::Exposure>(Keyframe(1.7)); }},
        {"exposure(4.2)",         [] { return std::make_shared<openshot::Exposure>(Keyframe(4.2)); }},

        // Shifts chosen to be non-integer fractions of 256, so the host's round()
        // actually rounds, and with a negative one because the sign is applied after
        // the magnitude. The last shifts alpha too, which is the case that can emit
        // colour above its own alpha -- invalid premultiplied data that the C++
        // produces and the fragment must not quietly clamp.
        {"colorshift(rgb)",       [] { return std::make_shared<openshot::ColorShift>(
                                           Keyframe(0.031), Keyframe(0.0), Keyframe(0.0), Keyframe(-0.017),
                                           Keyframe(0.0074), Keyframe(0.0), Keyframe(0.0), Keyframe(0.0)); }},
        {"colorshift(rgb+alpha)", [] { return std::make_shared<openshot::ColorShift>(
                                           Keyframe(0.05), Keyframe(0.02), Keyframe(-0.03), Keyframe(0.04),
                                           Keyframe(0.01), Keyframe(-0.06), Keyframe(0.02), Keyframe(0.03)); }},
    };
}

// The shader pass on its own. A single timed loop cannot give this: the run has to end in a
// readback or the recorded work never reaches the GPU, and at 1080p that readback is as large as
// the gate itself. So time two loop lengths that differ only in the number of shader passes and
// take the slope — the upload at the front and the readback at the end cancel exactly.
//
// Chained is the only way an effect is ever used: ApplyOnGpu leaves its result on the GPU, so the
// next effect reads a texture rather than uploading. The single-effect cost, upload and readback
// included, is reported beside it because that is what a lone GPU effect on a CPU frame pays, and
// it is the crossing W22-W25 exists to remove.
QImage timingImage(int width, int height) {
    QImage img(width, height, QImage::Format_RGBA8888_Premultiplied);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
            setPremul(img.scanLine(y) + x * 4, x & 255, y & 255, (x ^ y) & 255, 255);
    return img;
}

double msChainedPass(const Factory& make, int width, int height) {
    const QImage img = timingImage(width, height);
    auto effect = make();

    auto timeRun = [&](int passes) {
        auto frame = frameFrom(img);
        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < passes; ++i)
            effect->GetFrame(frame, 1);
        frame->GetImage();   // the one readback that makes all of it happen
        return std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count();
    };

    // A pass costs about as much as the gate allows, so a single slope over a short chain lands
    // on either side of it run to run — the first version of this measurement reported 0.143 and
    // 0.218 ms for the same case. Longer chains and the median of three make the number mean
    // something; the gate is not worth deciding by noise.
    constexpr int kShort = 30, kLong = 150;
    timeRun(kShort);   // warm up: shader compile, pool fill, first upload
    double slopes[3];
    for (double& slope : slopes)
        slope = (timeRun(kLong) - timeRun(kShort)) / (kLong - kShort);
    std::sort(std::begin(slopes), std::end(slopes));
    return slopes[1];
}

double msSingleWithTransfers(const Factory& make, int width, int height) {
    const QImage img = timingImage(width, height);
    auto effect = make();
    constexpr int kWarmup = 3, kRuns = 20;
    auto run = [&] {
        auto frame = frameFrom(img);
        effect->GetFrame(frame, 1);
        frame->GetImage();
    };
    for (int i = 0; i < kWarmup; ++i) run();
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < kRuns; ++i) run();
    return std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - start).count() / kRuns;
}

// Where a pass's time actually goes. ApplyOnGpu does three things per effect — snapshot the
// source surface, borrow a destination, run the fragment — and a pass measured well over its gate
// needs the breakdown before anything is redesigned around a guess.
//
// Every probe has to be a genuine dependency chain. The first attempt at this measured ~0 ms for
// all three because a run of full-coverage kSrc draws whose results nobody reads is work Graphite
// is entitled to throw away, and it does: each draw replaces the last, so only the final one
// survives. So each pass below reads what the previous pass wrote, which nothing can elide, and
// the variants differ by one ingredient at a time.
void probe(int width, int height) {
    using namespace openshot;
    const QImage img = timingImage(width, height);
    const SkPixmap pixels(
        SkImageInfo::Make(width, height, kRGBA_8888_SkColorType, kPremul_SkAlphaType),
        img.constBits(), img.bytesPerLine());

    // A trivial fragment, to separate "a shader pass costs this" from "this shader costs this".
    auto [passthrough, error] = SkRuntimeEffect::MakeForShader(SkString(
        "uniform shader osSrc;\nfloat4 main(float2 p) { return osSrc.eval(p); }\n"));
    if (!passthrough) { std::printf("  probe: %s\n", error.c_str()); return; }

    auto slope = [&](const std::function<void(int)>& body) {
        constexpr int kShort = 10, kLong = 30;
        auto run = [&](int n) {
            const auto start = std::chrono::steady_clock::now();
            body(n);
            GpuDevice::Instance().submit(true);
            return std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - start).count();
        };
        run(kShort);
        return (run(kLong) - run(kShort)) / (kLong - kShort);
    };

    auto drawChain = [&](sk_sp<SkRuntimeEffect> effect, bool snapshot_each, bool acquire_each) {
        auto a = GpuFrame::Create(width, height, kRGBA_8888_SkColorType);
        auto b = GpuFrame::Create(width, height, kRGBA_8888_SkColorType);
        if (!a || !b || !a->upload(pixels)) return -1.0;
        sk_sp<SkImage> fixed = a->snapshot();
        return slope([&](int n) {
            auto src = a, dst = b;
            for (int i = 0; i < n; ++i) {
                sk_sp<SkImage> child = snapshot_each ? src->snapshot() : fixed;
                if (acquire_each) {
                    auto fresh = GpuFrame::Create(width, height, kRGBA_8888_SkColorType);
                    if (fresh) dst = fresh;
                }
                SkRuntimeEffectBuilder builder(effect);
                builder.child("osSrc") = child->makeShader(SkTileMode::kClamp, SkTileMode::kClamp,
                                                            SkSamplingOptions());
                if (effect->findUniform("factor")) {
                    builder.uniform("factor") = 1.0f;
                    builder.uniform("shift") = 8.0f;
                }
                SkPaint paint;
                paint.setBlendMode(SkBlendMode::kSrc);
                paint.setShader(builder.makeShader());
                dst->canvas()->drawRect(SkRect::MakeIWH(width, height), paint);
                std::swap(src, dst);
            }
        });
    };

    // The brightness fragment in the same harness, so the only difference from the passthrough
    // line above is the body. Its text is duplicated here on purpose: this is a measurement of
    // the fragment, and reaching into GpuEffect for the real one would make the probe depend on
    // the thing it is measuring.
    auto [brightness, brightness_error] = SkRuntimeEffect::MakeForShader(SkString(
        "uniform shader osSrc;\n"
        "uniform float factor;\n"
        "uniform float shift;\n"
        "float4 main(float2 p) {\n"
        "  float4 bytes = floor(float4(osSrc.eval(p)) * 255.0 + 0.5);\n"
        "  float alpha_percent = bytes.a == 0.0 ? 1.0 : bytes.a / 255.0;\n"
        "  float3 c = floor(bytes.rgb / alpha_percent);\n"
        "  c = clamp(floor(factor * (c - 128.0) + 128.0), 0.0, 255.0);\n"
        "  c = clamp(floor(c + shift), 0.0, 255.0);\n"
        "  return float4(floor(c * alpha_percent), bytes.a) / 255.0;\n"
        "}\n"));
    if (!brightness) { std::printf("  probe: %s\n", brightness_error.c_str()); return; }

    std::printf("  passthrough, snapshot + acquire each pass  %6.3f ms\n",
                drawChain(passthrough, true, true));
    std::printf("  passthrough, snapshot each pass            %6.3f ms\n",
                drawChain(passthrough, true, false));
    std::printf("  brightness, snapshot + acquire each pass   %6.3f ms\n",
                drawChain(brightness, true, true));
}

// Exactly where the unpremultiply disagrees, over every (premultiplied byte, alpha) pair there
// is. This is the one step every CPU twin shares, so if it is not reproducible then no fragment
// can be bit-exact on a semi-transparent pixel and that is a property of the port, not of any one
// effect. Isolating it here says so once instead of once per effect.
int checkUnpremul() {
    using namespace openshot;
    auto [effect, error] = SkRuntimeEffect::MakeForShader(SkString(
        "uniform shader osSrc;\n"
        "float4 main(float2 p) {\n"
        "  float4 b = floor(float4(osSrc.eval(p)) * 255.0 + 0.5);\n"
        "  float ap = b.a == 0.0 ? 1.0 : b.a / 255.0;\n"
        "  float q = floor(b.r / ap);\n"
        // The quotient is 0..255 and has to survive an 8-bit surface, so it goes out as the
        // channel value itself rather than being scaled.
        "  return float4(q / 255.0, 0.0, 0.0, 1.0);\n"
        "}\n"));
    if (!effect) { std::printf("  unpremul probe: %s\n", error.c_str()); return 1; }

    // x is the premultiplied byte, y the alpha, and the image is premultiplied-valid, so only
    // x <= y is a legal pixel. The rest are skipped rather than compared.
    QImage img(256, 256, QImage::Format_RGBA8888_Premultiplied);
    for (int a = 0; a < 256; ++a)
        for (int r = 0; r < 256; ++r)
            setPremul(img.scanLine(a) + r * 4, r, 0, 0, a);

    auto source = GpuFrame::Create(256, 256, kRGBA_8888_SkColorType);
    auto target = GpuFrame::Create(256, 256, kRGBA_8888_SkColorType);
    const SkPixmap pixels(
        SkImageInfo::Make(256, 256, kRGBA_8888_SkColorType, kPremul_SkAlphaType),
        img.constBits(), img.bytesPerLine());
    if (!source || !target || !source->upload(pixels)) { std::printf("  unpremul: setup failed\n"); return 1; }

    SkRuntimeEffectBuilder builder(effect);
    builder.child("osSrc") = source->snapshot()->makeShader(
        SkTileMode::kClamp, SkTileMode::kClamp, SkSamplingOptions());
    SkPaint paint;
    paint.setBlendMode(SkBlendMode::kSrc);
    paint.setShader(builder.makeShader());
    target->canvas()->drawRect(SkRect::MakeIWH(256, 256), paint);

    QImage out(256, 256, QImage::Format_RGBA8888_Premultiplied);
    const SkPixmap dst(SkImageInfo::Make(256, 256, kRGBA_8888_SkColorType, kPremul_SkAlphaType),
                       out.bits(), out.bytesPerLine());
    if (!target->readback(dst)) { std::printf("  unpremul: readback failed\n"); return 1; }

    long compared = 0, differing = 0;
    int worst = 0, worst_r = 0, worst_a = 0;
    for (int a = 0; a < 256; ++a)
        for (int r = 0; r <= a && r < 256; ++r) {
            // The CPU twin, spelled exactly as image-processing-lib spells it.
            const float alpha_percent = a == 0 ? 1.0f : a / 255.0f;
            const int cpu = (int) (unsigned char) (r / alpha_percent);
            const int gpu = out.scanLine(a)[r * 4];
            compared++;
            if (cpu != gpu) {
                differing++;
                if (std::abs(cpu - gpu) > worst) { worst = std::abs(cpu - gpu); worst_r = r; worst_a = a; }
            }
        }
    std::printf("  unpremultiply: %ld of %ld (premul, alpha) pairs differ, worst %d LSB "
                "(byte %d over alpha %d)\n", differing, compared, worst, worst_r, worst_a);
    return 0;
}

// The whole exposure(1.0) chain -- floor(floor(v/a) * a), an identity in exact arithmetic -- over
// every legal (premultiplied byte, alpha) pair.
//
// This exists because the parity numbers did not add up. The unpremultiply above disagrees on
// 1.8 % of pairs, but exposure at 1.0 disagrees on 19 % of a noise image, and the composition
// cannot amplify a disagreement tenfold. Either the multiply back is also disagreeing, or the
// bytes the fragment reads are not the bytes the C++ reads -- and the second would matter to every
// fragment, not just this one. So the chain is measured whole, over the same exhaustive input.
int checkExposureChain() {
    using namespace openshot;
    auto [effect, error] = SkRuntimeEffect::MakeForShader(SkString(
        "uniform shader osSrc;\n"
        "float4 main(float2 p) {\n"
        "  float4 b = floor(float4(osSrc.eval(p)) * 255.0 + 0.5);\n"
        "  float ap = b.a == 0.0 ? 1.0 : b.a / 255.0;\n"
        "  float u = floor(b.r / ap);\n"
        "  float v = floor(u * ap);\n"
        // Two channels out: the chain's result, and the raw byte the fragment read, so a
        // disagreement in reading the texture is distinguishable from one in the arithmetic.
        "  return float4(v / 255.0, b.r / 255.0, 0.0, 1.0);\n"
        "}\n"));
    if (!effect) { std::printf("  exposure chain probe: %s\n", error.c_str()); return 1; }

    QImage img(256, 256, QImage::Format_RGBA8888_Premultiplied);
    for (int a = 0; a < 256; ++a)
        for (int r = 0; r < 256; ++r)
            setPremul(img.scanLine(a) + r * 4, r, 0, 0, a);

    auto source = GpuFrame::Create(256, 256, kRGBA_8888_SkColorType);
    auto target = GpuFrame::Create(256, 256, kRGBA_8888_SkColorType);
    const SkPixmap pixels(
        SkImageInfo::Make(256, 256, kRGBA_8888_SkColorType, kPremul_SkAlphaType),
        img.constBits(), img.bytesPerLine());
    if (!source || !target || !source->upload(pixels)) { std::printf("  chain: setup failed\n"); return 1; }

    SkRuntimeEffectBuilder builder(effect);
    builder.child("osSrc") = source->snapshot()->makeShader(
        SkTileMode::kClamp, SkTileMode::kClamp, SkSamplingOptions());
    SkPaint paint;
    paint.setBlendMode(SkBlendMode::kSrc);
    paint.setShader(builder.makeShader());
    target->canvas()->drawRect(SkRect::MakeIWH(256, 256), paint);

    QImage out(256, 256, QImage::Format_RGBA8888_Premultiplied);
    const SkPixmap dst(SkImageInfo::Make(256, 256, kRGBA_8888_SkColorType, kPremul_SkAlphaType),
                       out.bits(), out.bytesPerLine());
    if (!target->readback(dst)) { std::printf("  chain: readback failed\n"); return 1; }

    long compared = 0, chain_differ = 0, byte_differ = 0;
    int worst = 0, worst_r = 0, worst_a = 0;
    for (int a = 0; a < 256; ++a)
        for (int r = 0; r <= a && r < 256; ++r) {
            const float alpha_percent = a == 0 ? 1.0f : a / 255.0f;
            const int u = (int) (unsigned char) (r / alpha_percent);
            const int cpu = (int) (unsigned char) (u * alpha_percent);
            const int gpu = out.scanLine(a)[r * 4];
            const int gpu_byte = out.scanLine(a)[r * 4 + 1];
            compared++;
            if (gpu_byte != r) byte_differ++;
            if (cpu != gpu) {
                chain_differ++;
                if (std::abs(cpu - gpu) > worst) { worst = std::abs(cpu - gpu); worst_r = r; worst_a = a; }
            }
        }
    std::printf("  exposure(1.0) chain: %ld of %ld pairs differ, worst %d LSB (byte %d over alpha %d)\n",
                chain_differ, compared, worst, worst_r, worst_a);
    std::printf("  the byte the fragment reads: %ld of %ld wrong\n", byte_differ, compared);
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    // --sksl compiles whatever is on stdin and prints the compiler's answer. SkSL is not GLSL and
    // its intrinsic set is narrower than the documentation makes obvious, so a fragment that
    // "should" work is worth asking about directly rather than finding out through a silent
    // fallback to the CPU path.
    if (argc > 1 && std::string(argv[1]) == "--sksl") {
        std::string source, line;
        while (std::getline(std::cin, line)) source += line + "\n";
        auto [effect, error] = SkRuntimeEffect::MakeForShader(SkString(source.c_str()));
        std::printf("%s\n", effect ? "ok" : error.c_str());
        return effect ? 0 : 1;
    }

    if (!openshot::GpuDevice::Instance().available()) {
        std::printf("GPU not available (OPENSHOT_GPU unset or off) — nothing to compare.\n");
        return 0;
    }
    std::printf("device: %s\n\n", openshot::GpuDevice::Instance().deviceName().c_str());

    const std::vector<TestImage> images = makeImages();
    const std::vector<Case> all = cases();

    // Both arms of every case, GPU first, then the whole device torn down once for the CPU arm.
    // SetBackend is the single control and moves Generation(), so switching per case would drop
    // every pooled surface each time for no benefit.
    // ApplyOnGpu leaves its result as the frame's GPU backing and the CPU twin does not, so
    // IsGpuBacked() afterwards is the proof that the shader actually ran. Without this check a
    // fragment that fails to compile falls back to the CPU on both arms and every comparison
    // below reports a perfect match — which is exactly what the first run of this test did.
    std::vector<std::vector<QImage>> gpu_results(all.size());
    int not_on_gpu = 0;
    for (std::size_t c = 0; c < all.size(); ++c)
        for (const TestImage& t : images) {
            auto frame = frameFrom(t.image);
            all[c].make()->GetFrame(frame, 1);
            if (!frame->IsGpuBacked()) {
                std::printf("FAIL  %-22s %-12s did not run on the GPU (shader declined or "
                            "failed to compile)\n", all[c].name.c_str(), t.name.c_str());
                not_on_gpu++;
            }
            gpu_results[c].push_back(frame->GetImage()->copy());
        }
    if (not_on_gpu) {
        std::printf("\n%d case(s) never reached the GPU — comparing them would prove nothing.\n",
                    not_on_gpu);
        return 1;
    }

    const openshot::GpuDevice::Backend backend = openshot::GpuDevice::RequestedBackend();
    openshot::GpuDevice::SetBackend(openshot::GpuDevice::Backend::Off);

    int failures = 0;
    for (std::size_t c = 0; c < all.size(); ++c) {
        std::printf("%s\n", all[c].name.c_str());
        for (std::size_t i = 0; i < images.size(); ++i) {
            auto frame = frameFrom(images[i].image);
            all[c].make()->GetFrame(frame, 1);
            const QImage cpu = frame->GetImage()->copy();
            const Delta d = compare(cpu, gpu_results[c][i]);

            const bool exact = d.max_delta == 0;
            const bool pass = exact || d.psnr >= 48.0;
            if (!pass) failures++;
            std::printf("  %-6s %-12s psnr=%8.3f max=%3d differing=%6ld/%ld\n",
                        exact ? "EXACT" : (pass ? "PASS" : "FAIL"),
                        images[i].name.c_str(), d.psnr, d.max_delta, d.differing, d.total);
        }
    }

    // The CPU twin's own cost at 1080p, measured with the device still off. Printed beside the
    // shader figures so the two are comparable, and because it is the number that says whether a
    // change to the CPU path was worth making.
    std::printf("\n1080p cost per frame, CPU twin (device off)\n");
    for (const Case& c : all)
        std::printf("       %-22s %6.3f ms\n", c.name.c_str(),
                    msSingleWithTransfers(c.make, 1920, 1080));

    // Timing needs the GPU back.
    openshot::GpuDevice::SetBackend(backend);
    if (openshot::GpuDevice::Instance().available()) {
        std::printf("\nthe shared unpremultiply step, over every legal pair\n");
        checkUnpremul();
        checkExposureChain();
        std::printf("\n1080p breakdown of one pass\n");
        probe(1920, 1080);
        // The 0.2 ms gate is about a GPU. Lavapipe is Mesa's software rasteriser and exists here
        // to check that a result is not vendor-specific -- it measures roughly 25x slower, so
        // holding it to a hardware timing gate would only ever produce a failure that means
        // nothing. Parity still counts on lavapipe, and above it did.
        const bool software = backend == openshot::GpuDevice::Backend::Lavapipe;
        std::printf("\n1080p cost per frame, gate <= 0.200 ms for the shader pass%s\n",
                    software ? " (reported only: lavapipe is a software rasteriser)" : "");
        for (const Case& c : all) {
            const double chained = msChainedPass(c.make, 1920, 1080);
            const double alone = msSingleWithTransfers(c.make, 1920, 1080);
            const bool pass = chained <= 0.200;
            if (!pass && !software) failures++;
            std::printf("  %-4s %-22s shader=%6.3f ms   with upload+readback=%6.3f ms\n",
                        software ? "----" : (pass ? "PASS" : "FAIL"),
                        c.name.c_str(), chained, alone);
        }
    }

    std::printf("\n%d failure(s)\n", failures);
    return failures ? 1 : 0;
}
