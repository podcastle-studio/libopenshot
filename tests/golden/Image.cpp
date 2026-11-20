#include "Image.h"

#include "Frame.h"

#include <QImage>
#include <QString>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <limits>

namespace golden {

Image Image::blank(int w, int h, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    Image img;
    img.w = w;
    img.h = h;
    img.rgba.resize(static_cast<size_t>(w) * h * 4);
    for (size_t i = 0; i < img.rgba.size(); i += 4) {
        img.rgba[i] = r; img.rgba[i + 1] = g; img.rgba[i + 2] = b; img.rgba[i + 3] = a;
    }
    return img;
}

Image fromFrame(const std::shared_ptr<openshot::Frame>& frame) {
    Image img;
    img.w = frame->GetWidth();
    img.h = frame->GetHeight();
    img.rgba.resize(static_cast<size_t>(img.w) * img.h * 4);
    for (int y = 0; y < img.h; ++y) {
        const unsigned char* row = frame->GetPixels(y);
        std::memcpy(&img.rgba[static_cast<size_t>(y) * img.w * 4], row, static_cast<size_t>(img.w) * 4);
    }
    return img;
}

bool savePng(const std::string& path, const Image& img) {
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
    // Raw bytes, no premultiplication handling: what the renderer produced is what is stored.
    QImage q(img.w, img.h, QImage::Format_RGBA8888);
    for (int y = 0; y < img.h; ++y)
        std::memcpy(q.scanLine(y), img.px(0, y), static_cast<size_t>(img.w) * 4);
    return q.save(QString::fromStdString(path), "PNG", 10);   // Qt: low "quality" = high zlib compression
}

std::optional<Image> loadPng(const std::string& path) {
    QImage q(QString::fromStdString(path));
    if (q.isNull()) return std::nullopt;
    if (q.format() != QImage::Format_RGBA8888) q = q.convertToFormat(QImage::Format_RGBA8888);
    Image img;
    img.w = q.width();
    img.h = q.height();
    img.rgba.resize(static_cast<size_t>(img.w) * img.h * 4);
    for (int y = 0; y < img.h; ++y)
        std::memcpy(img.px(0, y), q.constScanLine(y), static_cast<size_t>(img.w) * 4);
    return img;
}

namespace {

std::vector<float> luma(const Image& img) {
    std::vector<float> out(static_cast<size_t>(img.w) * img.h);
    for (size_t i = 0, p = 0; p < out.size(); ++p, i += 4)
        out[p] = 0.299f * img.rgba[i] + 0.587f * img.rgba[i + 1] + 0.114f * img.rgba[i + 2];
    return out;
}

// Mean SSIM over non-overlapping-stride-4 8x8 windows on luma (Wang et al. constants).
double ssimLuma(const Image& a, const Image& b) {
    const auto la = luma(a);
    const auto lb = luma(b);
    constexpr int win = 8, stride = 4;
    constexpr double C1 = 6.5025, C2 = 58.5225;
    if (a.w < win || a.h < win) return 1.0;
    double sum = 0.0;
    long count = 0;
    for (int y = 0; y + win <= a.h; y += stride) {
        for (int x = 0; x + win <= a.w; x += stride) {
            double ma = 0, mb = 0;
            for (int j = 0; j < win; ++j)
                for (int i = 0; i < win; ++i) {
                    ma += la[static_cast<size_t>(y + j) * a.w + x + i];
                    mb += lb[static_cast<size_t>(y + j) * a.w + x + i];
                }
            ma /= win * win; mb /= win * win;
            double va = 0, vb = 0, cov = 0;
            for (int j = 0; j < win; ++j)
                for (int i = 0; i < win; ++i) {
                    const double da = la[static_cast<size_t>(y + j) * a.w + x + i] - ma;
                    const double db = lb[static_cast<size_t>(y + j) * a.w + x + i] - mb;
                    va += da * da; vb += db * db; cov += da * db;
                }
            va /= win * win - 1; vb /= win * win - 1; cov /= win * win - 1;
            sum += ((2 * ma * mb + C1) * (2 * cov + C2)) / ((ma * ma + mb * mb + C1) * (va + vb + C2));
            ++count;
        }
    }
    return count ? sum / count : 1.0;
}

} // namespace

Metrics compare(const Image& golden, const Image& actual) {
    Metrics m;
    if (golden.w != actual.w || golden.h != actual.h || golden.empty() || actual.empty()) {
        m.sizeMismatch = true;
        return m;
    }
    double sq = 0.0;
    long over2 = 0;
    const size_t n = static_cast<size_t>(golden.w) * golden.h;
    for (size_t p = 0, i = 0; p < n; ++p, i += 4) {
        int pixMax = 0;
        for (int c = 0; c < 3; ++c) {
            const int d = std::abs(int(golden.rgba[i + c]) - int(actual.rgba[i + c]));
            sq += double(d) * d;
            pixMax = std::max(pixMax, d);
        }
        m.maxAbs = std::max(m.maxAbs, pixMax);
        if (pixMax > 2) ++over2;
    }
    const double mse = sq / (3.0 * n);
    m.psnr = mse <= 0.0 ? std::numeric_limits<double>::infinity() : 10.0 * std::log10(255.0 * 255.0 / mse);
    m.pctOver2 = 100.0 * over2 / n;
    m.ssim = ssimLuma(golden, actual);
    return m;
}

Image diffHeatmap(const Image& a, const Image& b, int gain) {
    Image out = Image::blank(a.w, a.h);
    if (a.w != b.w || a.h != b.h) return out;
    const size_t n = static_cast<size_t>(a.w) * a.h;
    for (size_t p = 0, i = 0; p < n; ++p, i += 4) {
        int d = 0;
        for (int c = 0; c < 3; ++c) d = std::max(d, std::abs(int(a.rgba[i + c]) - int(b.rgba[i + c])));
        const int v = std::min(255, d * gain);
        // black -> blue -> red -> yellow -> white
        uint8_t r, g, bl;
        if (v < 64)       { r = 0;                 g = 0;                     bl = uint8_t(v * 4); }
        else if (v < 128) { r = uint8_t((v - 64) * 4); g = 0;                 bl = uint8_t(255 - (v - 64) * 4); }
        else if (v < 192) { r = 255;               g = uint8_t((v - 128) * 4); bl = 0; }
        else              { r = 255;               g = 255;                    bl = uint8_t((v - 192) * 4); }
        out.rgba[i] = r; out.rgba[i + 1] = g; out.rgba[i + 2] = bl; out.rgba[i + 3] = 255;
    }
    return out;
}

Image triptych(const Image& golden, const Image& actual, const Image& diff, int gutter) {
    const int w = std::max({golden.w, actual.w, diff.w});
    const int h = std::max({golden.h, actual.h, diff.h});
    Image out = Image::blank(w * 3 + gutter * 2, h, 40, 40, 40);
    auto blit = [&](const Image& src, int ox) {
        for (int y = 0; y < src.h; ++y)
            std::memcpy(out.px(ox, y), src.px(0, y), static_cast<size_t>(src.w) * 4);
    };
    if (!golden.empty()) blit(golden, 0);
    if (!actual.empty()) blit(actual, w + gutter);
    if (!diff.empty())   blit(diff, 2 * (w + gutter));
    return out;
}

} // namespace golden
