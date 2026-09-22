# Zoom blur — a port that is exact everywhere except one `atan2`

**2026-09-22, W20.** `zoom_blur.sksl` here is a complete SkSL twin of
`Podcastle::Effects::applyZoomBlurEffect`. It is not in
`src/effects/image-processing-lib/shaders/` because it does not clear W20's 45 dB gate on
high-frequency content, and the reason is one specific thing rather than a general looseness. This
records what was measured so the next attempt starts from here rather than from the top.

## What the effect actually does, and the first surprise

Reflect the frame outward by the blur strength, `linearPolar` to polar, box-blur along rho,
`linearPolar` back, crop.

**Both polar conversions are nearest-neighbour**, and nothing in the code says so.
`cv::linearPolar(src, dst, centre, maxRadius, flags)` passes `flags & INTER_MAX` to `remap` as the
interpolation, and the effect passes only `WARP_FILL_OUTLIERS` on the way out and `WARP_INVERSE_MAP`
on the way back. Neither has a bit inside `INTER_MAX` (7), so the interpolation is `INTER_NEAREST`
(0) both times — almost certainly an omission rather than a choice, since nearest through two polar
conversions is what produces the spoke aliasing this effect has.

Measured, not assumed: warping a coordinate image through `cv::linearPolar` recovers the map, and
against OpenCV 4.6 it agrees with `round(formula)` on **9076 of 9076** forward positions.

That is good news for a port — there are no interpolation weights to reproduce, only a rounding.

## Where it stops

`stage_bisect.cpp` runs the three stages separately against the real `cv::linearPolar`, on a
256x256 noise image with the effect's own geometry (strength 9, padding 9, 274x274 polar):

| stage | result |
|---|---|
| forward polar, closed form vs `cv::linearPolar` | **0 of 300,304 channels differ** |
| box blur along rho | the same `cv::blur` on both sides |
| inverse polar, closed form vs `cv::linearPolar` | **1457 of 300,304 differ, worst 121 LSB** |

So the whole divergence is the inverse map, and within it the **angle**. `rho` is exact:
`cvRound(hypot(dx, dy) / Kmag)` matches on every position. `phi` does not, and
`angle_variants.cpp` says none of the obvious spellings does either:

| angle expression | positions wrong (of ~59,000) |
|---|---:|
| `std::atan2` | 449 |
| `cv::fastAtan2` | 273 |
| `cv::cartToPolar`, `/ Kangle` in double | 137 |
| `cv::cartToPolar`, `* (1/Kangle)` in float | **136** |

`warpPolar`'s inverse gets its angle from `cv::cartToPolar`, which is a float polynomial
approximation, and the exact float chain from there to the map is not reachable from this repo. At
0.23 % residual the map picks a **neighbouring polar column** on that fraction of pixels, and
because the remap is nearest that is a whole different source pixel rather than a fraction of one.

## What that costs, end to end

`openshot-gpu-effect-parity`, Vulkan, the standard eight images:

| case | opaque_ramp | alpha_ramp | corners | noise |
|---|---:|---:|---:|---:|
| `zoom_blur(40, centre)` | 69.9 dB | 69.7 dB | **30.1 dB** (max 255) | **40.7 dB** (max 118) |
| `zoom_blur(100, off-centre)` | 69.9 dB | 70.4 dB | 52.6 dB | 47.2 dB |

Smooth content is fine; a blocky or noisy image is not, and the centred case is worse because a
centred blur puts whole rays on the axes where the rounding is ambiguous. Two of sixteen fail a
45 dB gate.

**It is not floating-point precision.** The same algorithm computed entirely in `double` on the CPU
gives 39.837 dB on noise — the identical figure to the `float` version and to the shader. A
double-float arithmetic exercise inside the fragment would buy nothing.

## Two ways to unblock it, both cheap

1. **Pass `cv::INTER_LINEAR` to both `linearPolar` calls.** Then a thousandth-of-a-column
   difference in the angle costs a fraction of an LSB instead of a whole pixel, and the port is a
   normal resampling one like `BorderReflectedMove`. It also removes the spoke aliasing. It is a
   **product decision**: it moves existing projects' output and the front end compiles the same
   source to WASM — the same shape as the `cv::cvtColor` fix that took `Wipe` from 62–85 dB to
   bit-exact.
2. **Send the inverse map in as a texture**, per the standing rule that whatever a transcendental
   decides stays on the CPU. It works and it is not worth it here: the map is the padded frame's
   size and depends on the strength, which ramps every frame during a transition, so it is a
   full-frame upload per frame to save one.

## Files

| file | what |
|---|---|
| `zoom_blur.sksl` | the port. Complete, and correct apart from the angle |
| `stage_bisect.cpp` | forward / blur / inverse against `cv::linearPolar`, stage by stage |
| `angle_variants.cpp` | the table above |

Build either probe with
`g++ -O2 -std=c++17 <file>.cpp -o <file> $(pkg-config --cflags --libs opencv4)`;
`stage_bisect.cpp` also needs `image-processing-lib/src/Effects/effects.cpp`.
