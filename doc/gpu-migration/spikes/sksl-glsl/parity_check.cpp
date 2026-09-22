// Renders rotational_blur.rts through SkRuntimeEffect and, for the same input and parameters,
// through the C++ oracle (Podcastle::Effects::applyRotationalBlur), then reports PSNR and writes
// both results as PNG.
//
// The point is not that Skia and OpenCV agree to the bit — they cannot, one resamples in float
// through explicit weights and the other through OpenCV's. The point is to establish, once, what
// the achievable agreement IS, so the front end has a number to compare its own port against.
//
//   ./parity_check <input.png> <blurAmountDegrees> <outdir>

#include "include/core/SkBitmap.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkColorSpace.h"
#include "include/core/SkData.h"
#include "include/core/SkImage.h"
#include "include/core/SkPaint.h"
#include "include/core/SkStream.h"
#include "include/core/SkSurface.h"
#include "include/effects/SkRuntimeEffect.h"
#include "include/encode/SkPngEncoder.h"

#include <opencv2/opencv.hpp>

#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace Podcastle { namespace Effects {
void applyRotationalBlur(cv::Mat& src, double blurAmount);
}}

static std::string readFile(const char* path) {
    std::ifstream in(path);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

static bool writePng(const SkBitmap& bm, const std::string& path) {
    SkFILEWStream out(path.c_str());
    return SkPngEncoder::Encode(&out, bm.pixmap(), {});
}

// cv::Mat(CV_8UC4, RGBA) <-> SkBitmap(kRGBA_8888, unpremul)
static cv::Mat toMat(const SkBitmap& bm) {
    cv::Mat m(bm.height(), bm.width(), CV_8UC4);
    for (int y = 0; y < bm.height(); ++y)
        std::memcpy(m.ptr(y), bm.getAddr32(0, y), size_t(bm.width()) * 4);
    return m;
}

static double psnr(const cv::Mat& a, const cv::Mat& b) {
    cv::Mat d;
    cv::absdiff(a, b, d);
    d.convertTo(d, CV_64F);
    d = d.mul(d);
    const cv::Scalar s = cv::sum(d);
    const double se = s[0] + s[1] + s[2] + s[3];
    if (se <= 1e-12) return 1e9;
    const double mse = se / (double(a.total()) * 4.0);
    return 10.0 * std::log10(255.0 * 255.0 / mse);
}

int main(int argc, char** argv) {
    if (argc != 4) {
        std::fprintf(stderr, "usage: %s <input.png> <blurAmountDegrees> <outdir>\n", argv[0]);
        return 2;
    }
    const std::string inPath = argv[1];
    const double blurAmount = std::atof(argv[2]);
    const std::string outDir = argv[3];

    // --- load, as unpremultiplied RGBA8 so both paths see identical bytes -------------------
    sk_sp<SkData> data = SkData::MakeFromFileName(inPath.c_str());
    if (!data) { std::fprintf(stderr, "cannot read %s\n", inPath.c_str()); return 1; }
    sk_sp<SkImage> src = SkImages::DeferredFromEncodedData(data);
    if (!src) { std::fprintf(stderr, "cannot decode %s\n", inPath.c_str()); return 1; }

    const int W = src->width(), H = src->height();
    const SkImageInfo info = SkImageInfo::Make(W, H, kRGBA_8888_SkColorType, kUnpremul_SkAlphaType);

    SkBitmap srcBm;
    srcBm.allocPixels(info);
    if (!src->readPixels(nullptr, srcBm.pixmap(), 0, 0)) {
        std::fprintf(stderr, "readPixels failed\n"); return 1;
    }

    // --- Skia: the shared SkSL ---------------------------------------------------------------
    const std::string sksl = readFile("rotational_blur.rts");
    SkRuntimeEffect::Result res = SkRuntimeEffect::MakeForShader(SkString(sksl.c_str()));
    if (!res.effect) {
        std::fprintf(stderr, "SkRuntimeEffect rejected the shader:\n%s\n", res.errorText.c_str());
        return 1;
    }

    sk_sp<SkImage> srcImage = srcBm.asImage();
    SkRuntimeShaderBuilder builder(res.effect);
    builder.child("uImage") = srcImage->makeShader(SkSamplingOptions{});   // NEAREST, no mips
    builder.uniform("uSize") = SkV2{float(W), float(H)};
    builder.uniform("uBlurAmount") = float(blurAmount);

    SkBitmap skiaBm;
    skiaBm.allocPixels(info);
    {
        SkCanvas canvas(skiaBm);
        canvas.clear(SK_ColorTRANSPARENT);
        SkPaint paint;
        paint.setShader(builder.makeShader());
        paint.setBlendMode(SkBlendMode::kSrc);
        canvas.drawRect(SkRect::MakeWH(float(W), float(H)), paint);
    }

    // --- the C++ oracle ----------------------------------------------------------------------
    cv::Mat oracle = toMat(srcBm).clone();
    Podcastle::Effects::applyRotationalBlur(oracle, blurAmount);

    const cv::Mat skiaMat = toMat(skiaBm);
    const double p = psnr(skiaMat, oracle);

    SkBitmap oracleBm;
    oracleBm.allocPixels(info);
    for (int y = 0; y < H; ++y)
        std::memcpy(oracleBm.getAddr32(0, y), oracle.ptr(y), size_t(W) * 4);

    writePng(srcBm,    outDir + "/input.png");
    writePng(skiaBm,   outDir + "/skia_sksl.png");
    writePng(oracleBm, outDir + "/cpp_oracle.png");

    // Per-channel max absolute difference, which is what a parity gate actually argues about.
    cv::Mat diff;
    cv::absdiff(skiaMat, oracle, diff);
    double maxAll = 0;
    std::vector<cv::Mat> ch;
    cv::split(diff, ch);
    std::printf("blurAmount=%.3f  size=%dx%d\n", blurAmount, W, H);
    const char* names[4] = {"R", "G", "B", "A"};
    for (int i = 0; i < 4; ++i) {
        double mn, mx;
        cv::minMaxLoc(ch[i], &mn, &mx);
        maxAll = std::max(maxAll, mx);
        std::printf("  max |delta| %s = %3.0f\n", names[i], mx);
    }
    std::printf("  PSNR(skia_sksl, cpp_oracle) = %.2f dB   max |delta| = %.0f LSB\n", p, maxAll);
    return 0;
}
