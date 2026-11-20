/**
 * @file ColorGradingCore.h
 * @brief Platform-agnostic color grading core.
 *
 * No Qt, no Emscripten, no OpenMP — pure C++ with standard library only.
 *
 * Used only by `ColorMap.cpp` (OpenMP trilinear, stride-3), to parse `.cube` files and to compute
 * and bake the colour-match statistics. It lived in the shared image-processing-lib until
 * 2026-09-24; the editor grades LUTs in a PixiJS GLSL pass of its own and never used it, so it
 * moved here, next to its only caller.
 */

#ifndef COLOR_GRADING_CORE_H
#define COLOR_GRADING_CORE_H

#include <cstdint>
#include <vector>

namespace ColorGrading {

// ═══════════════════════════════════════════════════════════════════════════════
// Types
// ═══════════════════════════════════════════════════════════════════════════════

struct ChannelStats {
    float mean = 0.0f;
    float std  = 0.0f;
};

struct LabStats {
    ChannelStats L, a, b;
};

struct ColorMatchParams {
    float preserve        = 0.3f;   ///< 0=full transfer, 1=keep original
    float luminanceBlend  = 0.5f;   ///< 0=transfer brightness, 1=keep original
    float saturationBoost = 1.1f;   ///< 1.0=no change
    float contrastBoost   = 1.1f;   ///< 1.0=no change
};

// ═══════════════════════════════════════════════════════════════════════════════
// Initialization (call once, thread-safe)
// ═══════════════════════════════════════════════════════════════════════════════

/// Initialize precomputed sRGB→Linear (256 entries) and labF (4096 entries) tables.
/// Safe to call multiple times; only initializes once.
void initLookupTables();

// ═══════════════════════════════════════════════════════════════════════════════
// Color space conversions
// ═══════════════════════════════════════════════════════════════════════════════

/// Standard RGB (float 0–1) ↔ Lab. Uses powf/cbrtf — suitable for bake paths, not hot loops.
void rgbToLab(float r, float g, float b, float& L, float& a, float& bOut);
void labToRgb(float L, float a, float b, float& rOut, float& gOut, float& bOut);

/// Fast RGB (byte 0–255) → Lab using precomputed tables. No powf/cbrtf. Hot-path safe.
/// Requires initLookupTables() called first.
void rgbBytesToLab(int R, int G, int B, float& L, float& a, float& bOut);

/// RGB (float 0–1) ↔ HSV. H in [0,360), S/V in [0,1].
void rgbToHsv(float r, float g, float b, float& h, float& s, float& v);
void hsvToRgb(float h, float s, float v, float& r, float& g, float& b);

// ═══════════════════════════════════════════════════════════════════════════════
// Statistics
// ═══════════════════════════════════════════════════════════════════════════════

/// Single-pass Welford mean+variance from RGBA buffer using fast LUT-based conversion.
/// @param step  Process every Nth pixel (for downsampling). 1 = all pixels.
/// Requires initLookupTables() called first.
LabStats computeLabStats(const uint8_t* rgba, int width, int height, int step = 1);

/// Check if two stats are similar enough to skip rebaking.
/// Default threshold of 1.5 Lab units covers typical frame-to-frame variation.
bool statsAreSimilar(const LabStats& a, const LabStats& b, float threshold = 1.5f);

// ═══════════════════════════════════════════════════════════════════════════════
// Bake color match to 3D LUT
// ═══════════════════════════════════════════════════════════════════════════════

/// Bake Reinhard color transfer into a 3D LUT.
/// @param outLut   Output buffer, must be at least N*N*N*3 floats (stride-3, RGB).
/// @param N        LUT edge size (typically 17).
/// @param srcStats Source frame Lab statistics.
/// @param refStats Reference image Lab statistics.
/// @param params   Color match parameters.
void bakeColorMatchLut(float* outLut, int N,
                       const LabStats& srcStats,
                       const LabStats& refStats,
                       const ColorMatchParams& params);

// ═══════════════════════════════════════════════════════════════════════════════
// .cube LUT parsing (platform-agnostic, no Qt)
// ═══════════════════════════════════════════════════════════════════════════════

/// Parse .cube text into a flat stride-3 float array.
/// @param text          Raw .cube file content (null-terminated or textLen chars).
/// @param textLen       Length of text (-1 for null-terminated).
/// @param outData       Filled with N³×3 float values (R,G,B per entry).
/// @param outSize       Set to N (the LUT_3D_SIZE).
/// @param outDomainMin  Optional: the cube's DOMAIN_MIN, or {0,0,0} when it declares none.
/// @param outDomainMax  Optional: the cube's DOMAIN_MAX, or {1,1,1} when it declares none.
/// @returns true on success, false on parse failure.
///
/// The domain is what a cube's input range is declared to be, and it was dropped here until
/// 2026-09-22 while the editor's shader normalised by it -- so any cube outside 0..1 graded
/// differently in the preview and in the export. Both out-params are optional so the callers
/// that predate this keep compiling; a caller that ignores them is accepting the old bug.
bool parseCubeText(const char* text, int textLen,
                   std::vector<float>& outData, int& outSize,
                   float* outDomainMin = nullptr, float* outDomainMax = nullptr);

// ═══════════════════════════════════════════════════════════════════════════════
// LUT resampling
// ═══════════════════════════════════════════════════════════════════════════════

/// Resample a stride-3 LUT from srcSize³ → dstSize³ via trilinear interpolation.
/// @param srcLut  Source LUT data (srcSize³ × 3 floats).
/// @param srcSize Source edge size.
/// @param dstLut  Output buffer (must be at least dstSize³ × 3 floats).
/// @param dstSize Target edge size (typically 17).
void resampleLut3D(const float* srcLut, int srcSize,
                   float* dstLut, int dstSize);

// ═══════════════════════════════════════════════════════════════════════════════
// Stride conversion helpers
// ═══════════════════════════════════════════════════════════════════════════════

/// Convert stride-3 LUT to stride-4 (padded for SIMD). Output needs count*4 floats.
void lutStride3to4(const float* src3, float* dst4, int count);

/// Convert stride-4 LUT to stride-3. Output needs count*3 floats.
void lutStride4to3(const float* src4, float* dst3, int count);

} // namespace ColorGrading

#endif // COLOR_GRADING_CORE_H
