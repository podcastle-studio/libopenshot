// Golden-frame suite: minimal image container, PNG I/O and comparison metrics.
//
// This is the ONLY place in the suite that knows how pixels are stored by the library or on
// disk. Today the implementation uses QImage for PNG encode/decode and copies the bytes out of
// openshot::Frame; when Qt leaves the library, replace Image.cpp with a Skia (or libpng) backed
// implementation and nothing else in tests/golden changes.
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace openshot { class Frame; }

namespace golden {

// Straight 8-bit RGBA, row-major, stride = w * 4.
struct Image {
    int w = 0;
    int h = 0;
    std::vector<uint8_t> rgba;

    bool empty() const { return rgba.empty(); }
    const uint8_t* px(int x, int y) const { return &rgba[(static_cast<size_t>(y) * w + x) * 4]; }
    uint8_t* px(int x, int y) { return &rgba[(static_cast<size_t>(y) * w + x) * 4]; }
    static Image blank(int w, int h, uint8_t r = 0, uint8_t g = 0, uint8_t b = 0, uint8_t a = 255);
};

// Copy the frame's RGBA8888 (premultiplied) buffer as-is. Alpha is kept verbatim so a
// transparent timeline would be visible in the goldens; the timelines here are opaque.
Image fromFrame(const std::shared_ptr<openshot::Frame>& frame);

bool savePng(const std::string& path, const Image& img);           // creates parent directories
std::optional<Image> loadPng(const std::string& path);

struct Metrics {
    double psnr = 0.0;        // dB over RGB; +inf when identical
    double ssim = 0.0;        // mean SSIM on luma, 8x8 windows
    int maxAbs = 0;           // max |diff| over RGB channels
    double pctOver2 = 0.0;    // % of pixels where any RGB channel differs by more than 2
    bool sizeMismatch = false;
};

Metrics compare(const Image& golden, const Image& actual);

// Visual aids for the report.
Image diffHeatmap(const Image& a, const Image& b, int gain = 8);
Image triptych(const Image& golden, const Image& actual, const Image& diff, int gutter = 4);

} // namespace golden
