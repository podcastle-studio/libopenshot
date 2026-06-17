#pragma once

// Volumetric "sunbeam" text glow via a Skia runtime shader. The text silhouette
// (glyphs in the glow colour on transparent) is sampled while marching toward a
// light point: each sample reads a copy of the silhouette contracted a little
// more toward the source, accumulated with a distance fade. Bright letters smear
// outward into rays radiating from the source. Ported from text-glow-shader.ts.

#include <skia/include/effects/SkRuntimeEffect.h>

#include <algorithm>
#include <cmath>

namespace openshot {
namespace text {

// Accumulation brightness — > 1 lets the near halo blow out into light.
constexpr double GLOW_GAIN = 3.0;

// Distance-weight exponent — higher concentrates light at the letters and fades faster.
constexpr double GLOW_FALLOFF = 1.4;

// Fixed Gaussian softening that fuses the discrete ray samples (* fontSize).
constexpr double GLOW_BEAM_BLUR_RATIO = 0.04;

// Local-bloom Gaussian blur sigma (* fontSize) — the tight rim halo hugging the glyphs.
constexpr double GLOW_BLOOM_BLUR_RATIO = 0.06;

// Brightness of the local-bloom layer relative to the glow opacity.
constexpr double GLOW_BLOOM_ALPHA = 1.5;

// Core-text softening when glow is active: opacity of the topmost crisp text.
constexpr double GLOW_CORE_TEXT_OPACITY = 0.9;

// Sub-pixel mask blur (* fontSize) softening the crisp top text when glow is active.
constexpr double GLOW_CORE_TEXT_BLUR_RATIO = 0.012;

// glowRangeRatio (0..1) -> rayLen (beam reach).
constexpr double GLOW_RAY_LEN_SCALE = 2.2;

// glowDirection (-50..50) -> light-source offset from block centre, in fontSize units.
constexpr double GLOW_MAX_SOURCE_OFFSET = 3.0;
constexpr double GLOW_DIRECTION_RANGE = 50.0;

// Hard cap on the glow silhouette texture (px, reference space) so an extreme
// spread/offset can't ask for an enormous surface.
constexpr int GLOW_MAX_TEXTURE_DIM = 1024;

// Compile (once) and return the glow runtime effect, or null if unsupported.
SkRuntimeEffect* getGlowEffect();

// Ray sample count, scaled with beam length and capped at the shader's loop bound (64).
inline double glowSteps(double rayLen) {
    return std::min(64.0, std::max(12.0, std::round(rayLen * 40.0) + 14.0));
}

} // namespace text
} // namespace openshot
