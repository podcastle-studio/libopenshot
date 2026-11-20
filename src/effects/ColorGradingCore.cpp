/**
 * @file ColorGradingCore.cpp
 * @brief Platform-agnostic color grading core implementation.
 */

#include "ColorGradingCore.h"
#include <cmath>
#include <cstring>
#include <algorithm>

namespace ColorGrading {

// ═══════════════════════════════════════════════════════════════════════════════
// Precomputed lookup tables
// ═══════════════════════════════════════════════════════════════════════════════

static constexpr int LABF_TABLE_SIZE = 4096;
static constexpr float LABF_TABLE_MAX = 1.2f;

static float g_srgb_to_linear[256] = {};
static float g_labf_lut[LABF_TABLE_SIZE + 1] = {};
static bool g_luts_initialized = false;

void initLookupTables() {
    if (g_luts_initialized) return;

    // sRGB byte [0..255] → linear float
    for (int i = 0; i < 256; i++) {
        float v = (float)i / 255.0f;
        g_srgb_to_linear[i] = v > 0.04045f
            ? powf((v + 0.055f) / 1.055f, 2.4f)
            : v / 12.92f;
    }

    // labF(t) for t in [0, LABF_TABLE_MAX], 4096 steps
    for (int i = 0; i <= LABF_TABLE_SIZE; i++) {
        float t = (float)i / (float)LABF_TABLE_SIZE * LABF_TABLE_MAX;
        g_labf_lut[i] = t > 0.008856f ? cbrtf(t) : (7.787f * t + 16.0f / 116.0f);
    }

    g_luts_initialized = true;
}


// ═══════════════════════════════════════════════════════════════════════════════
// Internal helpers
// ═══════════════════════════════════════════════════════════════════════════════

static inline float srgbToLinear(float v) {
    return v > 0.04045f ? powf((v + 0.055f) / 1.055f, 2.4f) : v / 12.92f;
}

static inline float linearToSrgb(float v) {
    v = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
    return v > 0.0031308f ? 1.055f * powf(v, 1.0f / 2.4f) - 0.055f : 12.92f * v;
}

static inline float labF(float t) {
    return t > 0.008856f ? cbrtf(t) : (7.787f * t + 16.0f / 116.0f);
}

static inline float labFInv(float t) {
    float t3 = t * t * t;
    return t3 > 0.008856f ? t3 : (t - 16.0f / 116.0f) / 7.787f;
}

/// Fast labF using precomputed table with linear interpolation
static inline float labfFast(float t) {
    if (t <= 0.0f) return g_labf_lut[0];
    if (t >= LABF_TABLE_MAX) return g_labf_lut[LABF_TABLE_SIZE];
    float idx_f = t * ((float)LABF_TABLE_SIZE / LABF_TABLE_MAX);
    int idx = (int)idx_f;
    float frac = idx_f - (float)idx;
    return g_labf_lut[idx] * (1.0f - frac) + g_labf_lut[idx + 1] * frac;
}

static inline float clamp01(float v) {
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}


// ═══════════════════════════════════════════════════════════════════════════════
// Color space conversions — standard (float input, uses powf/cbrtf)
// ═══════════════════════════════════════════════════════════════════════════════

void rgbToLab(float r, float g, float b, float& L, float& a, float& bOut) {
    float rLin = srgbToLinear(r);
    float gLin = srgbToLinear(g);
    float bLin = srgbToLinear(b);

    float x = rLin * 0.4124564f + gLin * 0.3575761f + bLin * 0.2126729f;
    float y = rLin * 0.2126729f + gLin * 0.7151522f + bLin * 0.0721750f;
    float z = rLin * 0.0193339f + gLin * 0.1191920f + bLin * 0.9503041f;

    x /= 0.95047f;
    z /= 1.08883f;

    L    = 116.0f * labF(y) - 16.0f;
    a    = 500.0f * (labF(x) - labF(y));
    bOut = 200.0f * (labF(y) - labF(z));
}

void labToRgb(float L, float a, float b, float& rOut, float& gOut, float& bOut) {
    float fy = (L + 16.0f) / 116.0f;
    float fx = a / 500.0f + fy;
    float fz = fy - b / 200.0f;

    float x = labFInv(fx) * 0.95047f;
    float y = labFInv(fy);
    float z = labFInv(fz) * 1.08883f;

    float rv = x *  3.2404542f + y * -1.5371385f + z * -0.4985314f;
    float gv = x * -0.9692660f + y *  1.8760108f + z *  0.0415560f;
    float bv = x *  0.0556434f + y * -0.2040259f + z *  1.0572252f;

    rOut = linearToSrgb(rv);
    gOut = linearToSrgb(gv);
    bOut = linearToSrgb(bv);
}


// ═══════════════════════════════════════════════════════════════════════════════
// Color space conversions — fast byte input (precomputed LUTs)
// ═══════════════════════════════════════════════════════════════════════════════

void rgbBytesToLab(int R, int G, int B, float& L, float& a, float& bOut) {
    float rLin = g_srgb_to_linear[R];
    float gLin = g_srgb_to_linear[G];
    float bLin = g_srgb_to_linear[B];

    float x = rLin * 0.4124564f + gLin * 0.3575761f + bLin * 0.2126729f;
    float y = rLin * 0.2126729f + gLin * 0.7151522f + bLin * 0.0721750f;
    float z = rLin * 0.0193339f + gLin * 0.1191920f + bLin * 0.9503041f;

    x /= 0.95047f;
    z /= 1.08883f;

    float fx = labfFast(x);
    float fy = labfFast(y);
    float fz = labfFast(z);

    L    = 116.0f * fy - 16.0f;
    a    = 500.0f * (fx - fy);
    bOut = 200.0f * (fy - fz);
}


// ═══════════════════════════════════════════════════════════════════════════════
// HSV conversions
// ═══════════════════════════════════════════════════════════════════════════════

void rgbToHsv(float r, float g, float b, float& h, float& s, float& v) {
    float mx = std::max({r, g, b});
    float mn = std::min({r, g, b});
    float d = mx - mn;

    v = mx;
    s = (mx > 0.0f) ? (d / mx) : 0.0f;

    if (d < 0.00001f) { h = 0.0f; return; }

    if      (mx == r) h = 60.0f * fmodf((g - b) / d + 6.0f, 6.0f);
    else if (mx == g) h = 60.0f * ((b - r) / d + 2.0f);
    else              h = 60.0f * ((r - g) / d + 4.0f);
}

void hsvToRgb(float h, float s, float v, float& r, float& g, float& b) {
    float c = v * s;
    float x = c * (1.0f - fabsf(fmodf(h / 60.0f, 2.0f) - 1.0f));
    float m = v - c;
    float r1, g1, b1;

    if      (h < 60)  { r1=c; g1=x; b1=0; }
    else if (h < 120) { r1=x; g1=c; b1=0; }
    else if (h < 180) { r1=0; g1=c; b1=x; }
    else if (h < 240) { r1=0; g1=x; b1=c; }
    else if (h < 300) { r1=x; g1=0; b1=c; }
    else              { r1=c; g1=0; b1=x; }

    r = r1 + m;
    g = g1 + m;
    b = b1 + m;
}


// ═══════════════════════════════════════════════════════════════════════════════
// Statistics — single-pass Welford with fast LUTs
// ═══════════════════════════════════════════════════════════════════════════════

LabStats computeLabStats(const uint8_t* rgba, int width, int height, int step) {
    const int totalPixels = width * height;

    double meanL = 0, meanA = 0, meanB = 0;
    double m2L = 0, m2A = 0, m2B = 0;
    int count = 0;

    for (int i = 0; i < totalPixels; i += step) {
        int idx = i * 4;
        float L, a, bv;
        rgbBytesToLab(rgba[idx], rgba[idx + 1], rgba[idx + 2], L, a, bv);

        count++;
        double dL = (double)L - meanL;
        double da = (double)a - meanA;
        double db = (double)bv - meanB;
        double invN = 1.0 / (double)count;
        meanL += dL * invN;
        meanA += da * invN;
        meanB += db * invN;
        m2L += dL * ((double)L - meanL);
        m2A += da * ((double)a - meanA);
        m2B += db * ((double)bv - meanB);
    }

    LabStats stats{};
    if (count < 2) return stats;

    double invN = 1.0 / (double)count;
    stats.L.mean = (float)meanL;
    stats.a.mean = (float)meanA;
    stats.b.mean = (float)meanB;
    stats.L.std = sqrtf((float)(m2L * invN));
    stats.a.std = sqrtf((float)(m2A * invN));
    stats.b.std = sqrtf((float)(m2B * invN));

    return stats;
}


bool statsAreSimilar(const LabStats& a, const LabStats& b, float threshold) {
    return fabsf(a.L.mean - b.L.mean) < threshold &&
           fabsf(a.a.mean - b.a.mean) < threshold &&
           fabsf(a.b.mean - b.b.mean) < threshold &&
           fabsf(a.L.std  - b.L.std)  < threshold &&
           fabsf(a.a.std  - b.a.std)  < threshold &&
           fabsf(a.b.std  - b.b.std)  < threshold;
}


// ═══════════════════════════════════════════════════════════════════════════════
// Bake color match to 3D LUT (stride-3 output)
// ═══════════════════════════════════════════════════════════════════════════════

void bakeColorMatchLut(float* outLut, int N,
                       const LabStats& srcStats,
                       const LabStats& refStats,
                       const ColorMatchParams& params) {

    const float preserve = params.preserve;
    const float lumBlend = params.luminanceBlend;
    const float satBoost = params.saturationBoost;
    const float conBoost = params.contrastBoost;

    // Pre-fuse Reinhard coefficients
    float scaleL = 1.0f, offL = 0.0f;
    float scaleA = 1.0f, offA = 0.0f;
    float scaleB = 1.0f, offB = 0.0f;

    if (srcStats.L.std > 0.001f) {
        float ratio = refStats.L.std / srcStats.L.std;
        scaleL = ratio;
        offL = refStats.L.mean - srcStats.L.mean * ratio;
    }
    if (srcStats.a.std > 0.001f) {
        float ratio = refStats.a.std / srcStats.a.std;
        scaleA = ratio;
        offA = refStats.a.mean - srcStats.a.mean * ratio;
    }
    if (srcStats.b.std > 0.001f) {
        float ratio = refStats.b.std / srcStats.b.std;
        scaleB = ratio;
        offB = refStats.b.mean - srcStats.b.mean * ratio;
    }

    // Blend with identity by preserve factor
    float keep = preserve;
    float xfer = 1.0f - preserve;
    scaleL = scaleL * xfer + 1.0f * keep;
    offL   = offL   * xfer;
    scaleA = scaleA * xfer + 1.0f * keep;
    offA   = offA   * xfer;
    scaleB = scaleB * xfer + 1.0f * keep;
    offB   = offB   * xfer;

    const float stepVal = 1.0f / (float)(N - 1);
    const bool needsHsv = (satBoost != 1.0f || conBoost != 1.0f);

    for (int ib = 0; ib < N; ib++) {
        for (int ig = 0; ig < N; ig++) {
            for (int ir = 0; ir < N; ir++) {
                float r = (float)ir * stepVal;
                float g = (float)ig * stepVal;
                float b = (float)ib * stepVal;

                float L, a, bv;
                rgbToLab(r, g, b, L, a, bv);

                float newL = L * scaleL + offL;
                float newA = a * scaleA + offA;
                float newB = bv * scaleB + offB;

                // Luminance blend
                newL = newL * (1.0f - lumBlend) + L * lumBlend;

                float outR, outG, outB;
                labToRgb(newL, newA, newB, outR, outG, outB);

                if (needsHsv) {
                    float h, s, v;
                    rgbToHsv(outR, outG, outB, h, s, v);
                    s = clamp01(s * satBoost);
                    v = clamp01(v * conBoost);
                    hsvToRgb(h, s, v, outR, outG, outB);
                }

                outR = clamp01(outR);
                outG = clamp01(outG);
                outB = clamp01(outB);

                int idx = (ib * N * N + ig * N + ir) * 3;
                outLut[idx + 0] = outR;
                outLut[idx + 1] = outG;
                outLut[idx + 2] = outB;
            }
        }
    }
}


// ═══════════════════════════════════════════════════════════════════════════════
// .cube parser (no Qt dependency)
// ═══════════════════════════════════════════════════════════════════════════════

bool parseCubeText(const char* text, int textLen,
                   std::vector<float>& outData, int& outSize,
                   float* outDomainMin, float* outDomainMax) {
    outData.clear();
    outSize = 0;

    // A cube that declares no domain is defined on 0..1, which is the overwhelming majority.
    float domainMin[3] = {0.0f, 0.0f, 0.0f};
    float domainMax[3] = {1.0f, 1.0f, 1.0f};
    auto publishDomain = [&]() {
        if (outDomainMin) for (int i = 0; i < 3; ++i) outDomainMin[i] = domainMin[i];
        if (outDomainMax) for (int i = 0; i < 3; ++i) outDomainMax[i] = domainMax[i];
    };

    if (!text) return false;

    const char* p = text;
    const char* end = (textLen >= 0) ? (text + textLen) : (text + strlen(text));

    int lutSize = 0;

    // Helper: skip to end of line
    auto skipLine = [&]() {
        while (p < end && *p != '\n' && *p != '\r') p++;
        while (p < end && (*p == '\n' || *p == '\r')) p++;
    };

    // Helper: skip whitespace (not newline)
    auto skipWS = [&]() {
        while (p < end && (*p == ' ' || *p == '\t')) p++;
    };

    // DOMAIN_MIN / DOMAIN_MAX, three floats each. Read rather than skipped since 2026-09-22:
    // the editor's shader normalises by them, so dropping them here was the export grading a
    // non-0..1 cube on the wrong input range. They are looked for in BOTH loops below because a
    // .cube may declare them either side of LUT_3D_SIZE, and in practice usually does after it.
    auto tryDomain = [&]() {
        float* target = nullptr;
        if (end - p >= 10 && memcmp(p, "DOMAIN_MIN", 10) == 0) target = domainMin;
        else if (end - p >= 10 && memcmp(p, "DOMAIN_MAX", 10) == 0) target = domainMax;
        if (!target) return false;
        p += 10;
        for (int i = 0; i < 3; ++i) {
            skipWS();
            char* endPtr = nullptr;
            const float v = strtof(p, &endPtr);
            if (endPtr == p) break;
            target[i] = v;
            p = endPtr;
        }
        skipLine();
        return true;
    };

    // Parse header to find LUT_3D_SIZE
    while (p < end) {
        skipWS();
        if (p >= end) break;

        // Comment
        if (*p == '#') { skipLine(); continue; }

        // Check for LUT_3D_SIZE
        if (end - p >= 12 && memcmp(p, "LUT_3D_SIZE", 11) == 0) {
            p += 11;
            skipWS();
            lutSize = 0;
            while (p < end && *p >= '0' && *p <= '9') {
                lutSize = lutSize * 10 + (*p - '0');
                p++;
            }
            skipLine();
            break;
        }

        if (tryDomain()) continue;

        // Skip other header lines (TITLE and the rest)
        if (*p < '0' || (*p > '9' && *p != '-' && *p != '+' && *p != '.')) {
            skipLine();
            continue;
        }

        // Hit numeric data before finding LUT_3D_SIZE — invalid
        return false;
    }

    if (lutSize < 2 || lutSize > 256) return false;

    int totalEntries = lutSize * lutSize * lutSize;
    outData.reserve(totalEntries * 3);

    // Parse data lines
    while (p < end && (int)outData.size() < totalEntries * 3) {
        skipWS();
        if (p >= end) break;

        // Skip empty lines and comments
        if (*p == '\n' || *p == '\r') { skipLine(); continue; }
        if (*p == '#') { skipLine(); continue; }
        // Skip any remaining header lines, reading the domain out of them if that is what
        // they are -- a .cube commonly declares DOMAIN_MIN/MAX after LUT_3D_SIZE, which is
        // past the point the header loop above stops at.
        if (tryDomain()) continue;
        if (*p < '0' && *p != '-' && *p != '+' && *p != '.') { skipLine(); continue; }

        // Parse 3 floats
        float vals[3];
        bool ok = true;
        for (int c = 0; c < 3 && ok; c++) {
            skipWS();
            if (p >= end) { ok = false; break; }

            char* endPtr = nullptr;
            vals[c] = strtof(p, &endPtr);
            if (endPtr == p) { ok = false; break; }
            p = endPtr;
        }

        if (ok) {
            outData.push_back(vals[0]);
            outData.push_back(vals[1]);
            outData.push_back(vals[2]);
        }
        skipLine();
    }

    if ((int)outData.size() != totalEntries * 3) {
        outData.clear();
        return false;
    }

    outSize = lutSize;
    publishDomain();
    return true;
}


// ═══════════════════════════════════════════════════════════════════════════════
// LUT resampling (stride-3 → stride-3)
// ═══════════════════════════════════════════════════════════════════════════════

void resampleLut3D(const float* srcLut, int srcSize,
                   float* dstLut, int dstSize) {
    const float srcSizeM1 = (float)(srcSize - 1);
    const float stepVal = 1.0f / (float)(dstSize - 1);

    for (int ib = 0; ib < dstSize; ib++) {
        for (int ig = 0; ig < dstSize; ig++) {
            for (int ir = 0; ir < dstSize; ir++) {
                float rf = (float)ir * stepVal * srcSizeM1;
                float gf = (float)ig * stepVal * srcSizeM1;
                float bf = (float)ib * stepVal * srcSizeM1;

                int r0 = (int)rf, r1 = std::min(r0 + 1, srcSize - 1);
                int g0 = (int)gf, g1 = std::min(g0 + 1, srcSize - 1);
                int b0 = (int)bf, b1 = std::min(b0 + 1, srcSize - 1);

                float dr = rf - r0, idr = 1.0f - dr;
                float dg = gf - g0, idg = 1.0f - dg;
                float db = bf - b0, idb = 1.0f - db;

                const float* p000 = srcLut + ((b0*srcSize+g0)*srcSize+r0)*3;
                const float* p100 = srcLut + ((b0*srcSize+g0)*srcSize+r1)*3;
                const float* p010 = srcLut + ((b0*srcSize+g1)*srcSize+r0)*3;
                const float* p110 = srcLut + ((b0*srcSize+g1)*srcSize+r1)*3;
                const float* p001 = srcLut + ((b1*srcSize+g0)*srcSize+r0)*3;
                const float* p101 = srcLut + ((b1*srcSize+g0)*srcSize+r1)*3;
                const float* p011 = srcLut + ((b1*srcSize+g1)*srcSize+r0)*3;
                const float* p111 = srcLut + ((b1*srcSize+g1)*srcSize+r1)*3;

                int dstIdx = (ib * dstSize * dstSize + ig * dstSize + ir) * 3;
                for (int ch = 0; ch < 3; ch++) {
                    float c00 = p000[ch]*idr + p100[ch]*dr;
                    float c01 = p001[ch]*idr + p101[ch]*dr;
                    float c10 = p010[ch]*idr + p110[ch]*dr;
                    float c11 = p011[ch]*idr + p111[ch]*dr;
                    float c0  = c00*idg + c10*dg;
                    float c1  = c01*idg + c11*dg;
                    dstLut[dstIdx + ch] = c0*idb + c1*db;
                }
            }
        }
    }
}


// ═══════════════════════════════════════════════════════════════════════════════
// Stride conversion
// ═══════════════════════════════════════════════════════════════════════════════

void lutStride3to4(const float* src3, float* dst4, int count) {
    for (int i = 0; i < count; i++) {
        dst4[i * 4 + 0] = src3[i * 3 + 0];
        dst4[i * 4 + 1] = src3[i * 3 + 1];
        dst4[i * 4 + 2] = src3[i * 3 + 2];
        dst4[i * 4 + 3] = 0.0f;
    }
}

void lutStride4to3(const float* src4, float* dst3, int count) {
    for (int i = 0; i < count; i++) {
        dst3[i * 3 + 0] = src4[i * 4 + 0];
        dst3[i * 3 + 1] = src4[i * 4 + 1];
        dst3[i * 3 + 2] = src4[i * 4 + 2];
    }
}

} // namespace ColorGrading
