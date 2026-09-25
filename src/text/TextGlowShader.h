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

// The resolution the silhouette, ray-march and blurs run at, as a fraction of the frame's; the
// result is upscaled bilinearly. Full resolution since 2026-09-25 (owner: the best glow, on both
// paths): it was 0.40 with a 24-step cap, chosen when the export was CPU-only, which showed as
// banded beams and stair-stepped edges. Measured at 1080p on text_animated_glow_3: the GPU path
// ~56 -> ~49 fps, the CPU path 2.4 -> 0.1 fps -- accepted by the owner. Applied UNIFORMLY to resting
// and in-motion frames (no mid-clip quality pop). Override at runtime -- no rebuild -- with env
// OPENSHOT_GLOW_SCALE (0.05..1.0) and OPENSHOT_GLOW_STEPS (a step cap, 4..GLOW_MAX_STEPS);
// OPENSHOT_GLOW_SCALE=0.4 OPENSHOT_GLOW_STEPS=24 reproduces the old glow exactly.
constexpr double GLOW_RENDER_SCALE = 1.0;

// Compile (once) and return the glow runtime effect, or null if unsupported.
SkRuntimeEffect* getGlowEffect();

// The ray-march's loop bound (kGlowSkSL), and so the most samples a ray can take.
constexpr int GLOW_MAX_STEPS = 512;

// Ray sample count: the front end's schedule (rayLen * 40 + 14, 12..64) as a floor, raised until
// consecutive samples are at most @a spacing apart where they are farthest apart -- on the
// silhouette pixel farthest from the light, @a reach away. A sample there moves by
// reach * rayLen / (steps - 1) per step, so that many steps and the discrete samples are no
// longer visible as bands in the beams: the beam blur fuses gaps up to about its sigma, which is
// what @a spacing is. More steps than that converge on the same continuous integral, so this is
// the smooth limit of the same shader, not a different look (2026-09-25; it was capped at 24).
inline double glowSteps(double rayLen, double reach, double spacing) {
    const double schedule = std::min(64.0, std::max(12.0, std::round(rayLen * 40.0) + 14.0));
    const double smooth = spacing > 0.0 ? std::ceil(reach * rayLen / spacing) + 1.0 : 0.0;
    return std::min(static_cast<double>(GLOW_MAX_STEPS), std::max(schedule, smooth));
}

} // namespace text
} // namespace openshot
