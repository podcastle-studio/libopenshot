#include "TextGlowShader.h"

#include <skia/include/core/SkString.h>

namespace openshot {
namespace text {

namespace {

// Identical SkSL to GLOW_SKSL in text-glow-shader.ts.
const char* kGlowSkSL = R"SKSL(
uniform shader silhouette;   // child: the letters rendered in the glow colour
uniform float2 lightPos;     // light source, in silhouette-image pixels
uniform float  rayLen;       // max extra contraction — how far the beams reach
uniform float  steps;        // number of samples to march (<= loop bound)
uniform float  gain;         // brightness multiplier of accumulated light
uniform float  falloff;      // distance-weight exponent — higher = tighter, brighter core

half4 main(float2 p) {
  half4 acc = half4(0.0);
  float wsum = 0.0;
  for (int i = 0; i < 64; i++) {        // loop bound MUST be a compile-time constant
    if (float(i) >= steps) { break; }   // dynamic count via early-out
    float t     = steps > 1.0 ? float(i) / (steps - 1.0) : 0.0;  // 0..1 along the ray
    float w     = pow(1.0 - t, falloff);                         // steep near-sample weight
    float scale = 1.0 + rayLen * t;                              // contract more as t grows
    float2 sp   = lightPos + (p - lightPos) / scale;             // sample toward the source
    acc  += silhouette.eval(sp) * w;                             // accumulate weighted light
    wsum += w;
  }
  return wsum > 0.0 ? acc * (gain / wsum) : half4(0.0);
}
)SKSL";

} // namespace

SkRuntimeEffect* getGlowEffect() {
    // RuntimeEffect compilation is expensive but the effect is immutable — compile once.
    static sk_sp<SkRuntimeEffect> cached = [] {
        auto [effect, err] = SkRuntimeEffect::MakeForShader(SkString(kGlowSkSL));
        return effect;
    }();
    return cached.get();
}

} // namespace text
} // namespace openshot
