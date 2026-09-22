# Transition parity between the editor and the export

**Status: a proposal to react to, not a decision.** Written 2026-09-18, before W19–W21, because
those items move the server's transitions onto the GPU and that changes what the editor and the
export share. Nothing here is implemented.

Sections marked **MEASURED** were run on this machine and the numbers are reproducible. Sections
marked **PROPOSED** are design. Sections marked **UNKNOWN** need an answer from the front end.

> If any of this is adopted, the "Contract" section should graduate out of `doc/gpu-migration/`
> into permanent `doc/`, because it is a cross-repo contract that outlives the migration.

## The goal

Transitions should look the same in the editor preview and in the exported video. Today they do,
because both run **the same C++ compiled twice**: `image-processing-lib` is built natively for
libopenshot and to WASM for the front end. That is identity by construction, and it is the thing
worth protecting.

W20 proposes porting the transition vocabulary to SkSL so the server can run it on the GPU. If the
front end stays on the OpenCV WASM, there would be **two independent implementations** of every
transition for the first time, and every difference becomes its own investigation.

## What "identical" can actually mean

**Bit-identical across two stacks is not achievable, and this repo already contains the proof.**
`openshot-gpu-blend-parity` runs the *same Skia code* on NVIDIA and on lavapipe: 13 of 16 blend
modes agree within 1 LSB, but colour-dodge reaches **28 LSB** at its division singularity, and
colour-burn 7. Same source, same API, different float behaviour. A user's Intel/AMD/Apple GPU
through WebGL against an NVIDIA server through Vulkan will never be bit-equal.

So the target is **"no visible difference, and measured"** — a number on a fixed corpus, not an
aspiration. Something like PSNR ≥ 50 dB with max deviation in the low single-digit LSBs, per effect,
in CI. Designing for bit-equality will waste time; designing for a measured bound will not.

## MEASURED — a divergence that already exists, unrelated to any of this

`src/effects/Blur.cpp` passes the authored keyframe value straight into OpenCV as a **pixel count**:

```cpp
applyDiagonalBlurEffect(imageCv, diagonal_radius_value);
applyZoomBlurEffect(imageCv, zoom_blur_radius_value, centerPoint);
applyBlurEffect(imageCv, horizontal_radius_value, vertical_radius_value);
```

`../video-rendering-service`'s `Transition.cpp::addBlurEffect` also passes the payload value
verbatim — there is no resolution scaling anywhere in the chain.

**Which pixels, though?** `EffectBase.cpp:67` defaults `apply_before_clip = true` and `Blur` does
not override it (only `Glow` and `Shadow` do), so `Clip::apply_effects(..., before_keyframes=true)`
runs the blur on the frame straight from `GetOrCreateFrame` → `reader->GetFrame()` — that is the
**source media's native resolution**, before `apply_keyframes` scales anything to the timeline.

The front end applies the same number at its **preview canvas** resolution (HD), or at a lower
proxy resolution for effects it considers slow — which a blur certainly is.

Applying `applyBlurEffect(img, 10, 10)` to a step edge and measuring the 10–90 % transition width as
a fraction of frame width:

| frame | width | blur width (% of frame) | vs 720p preview |
|---|---:|---:|---:|
| 4K source (server) | 3840 | 0.208 % | **0.33×** |
| 1080p source (server) | 1920 | 0.417 % | 0.67× |
| 720p preview (front end) | 1280 | 0.625 % | 1.00× |
| 540p slow-effect proxy (front end) | 960 | 0.833 % | **1.33×** |

Two separate problems, both visible:

1. **Preview vs export.** A blur authored against a 720p preview comes out a third as wide when
   exported from a 4K source. The error is a function of the *source media resolution*, so it
   differs per clip in the same timeline.
2. **The preview is not self-consistent with itself.** The same blur is 1.33× wider when it takes
   the proxy path than when it does not.

Affected: horizontal/vertical blur, diagonal blur, zoom blur.

> **Correction, 2026-09-22: rotational blur is affected too, by a different mechanism, and this
> note previously said it was fine.** Its *parameter* is in degrees and is indeed
> resolution-independent — but `applyRotationalBlur` downscales before it works, and the threshold
> keys on the image: `absBlur > 15 && minDim > 400` halves it, `absBlur > 45 && minDim > 800`
> quarters it. So a 20° rotational blur runs at full resolution on a 640x360 source and at half
> resolution, upscaled, on a 1080p one. Same authored angle, visibly different result, varying per
> clip in the same timeline — which is exactly the problem described here for the radii. It belongs
> with them in the decision, and it is **not** portable until that decision is taken. `BorderReflectedMove` (dx/dy as fractions), `Zoom` (percent), `SplitShift` (ratios),
`ColorShift` (fractions) and `DisplacementMap` ([0,1]) are all already resolution-independent.

**This is not caused by the GPU work and is not fixed by sharing shader source.** It is a missing
definition: *the radius has no declared reference resolution*. It should be fixed on its own terms,
and the fix is a cross-repo behaviour change, so it needs a decision — see "Open questions".

## DECIDED (2026-09-18) — one algorithm, one language

**SkSL on both sides.** Put the effect bodies in `image-processing-lib/shaders/`, one `.sksl` file
per effect, and run them through Skia on both stacks: `SkRuntimeEffect` on the server (W19–W21),
`CanvasKit.RuntimeEffect` in the browser. The front end already ships CanvasKit — our Skia is pinned
to m147 to match it — so this is not a new dependency there, and the same source through the same
compiler makes parity a property of the build rather than something a test has to keep catching.

The C++ stays as the CPU oracle and the server's no-GPU fallback, unchanged.

> **Superseded.** This section previously proposed a restricted common subset with *two* emitters,
> SkSL for the server and GLSL ES for a PixiJS filter. That design is dropped; the reasoning and the
> one dependency it rests on are in `GPU-DECISIONS.md`, "SkSL is the one shader language". **The
> dependency: the front end must be able to put video frames through CanvasKit, not only text.**
> Confirm that before W19 writes its first shader — if it cannot, the two-emitter design comes back.

Two side benefits. The front end currently reads pixels out, mutates them in the OpenCV WASM on the
CPU, and re-uploads (`HEAPU8.set` → mutate → `subarray` → upload); run as a runtime effect the work
happens on a texture already on the GPU, so the round trip disappears — likely a larger preview win
than the parity work itself. And a backend change underneath CanvasKit (WebGPU) is Skia's problem,
not a third hand-port: the SkSL source does not move.

### The risk is concentrated, which makes this tractable

All 17 functions in `effects.cpp`, classified by whether they resample:

| | effects |
|---|---|
| **Pure per-pixel (8)** | Alpha, Bars, Brightness, Exposure, ThresholdWipe, CircleMask, SplitShift, ColorShift |
| **Resample (9)** | BorderReflectedMove, BorderReflectedRotation, Zoom, Blur, DiagonalBlur, ZoomBlur, RotationalBlur, DisplacementMap, additiveBlend |

The 8 per-pixel ones will agree to ~1 LSB on any stack with no special care — same arithmetic, no
filtering. **Every bit of the divergence risk is in the 9 that resample**, and it is not the
algorithm, it is the filter kernel and the border rule.

Worth recording because it was checked rather than assumed: `applyThresholdWipeMaskEffect` looked
like it might compute a percentile over the image, which would need a reduction pass and could not
be a single shader. It does not — it is a straight `percentage * 255 / 100` threshold, so it is
per-pixel like the rest. Nothing in this set needs a reduction.

### The lever that actually buys cross-stack parity

**Do not rely on hardware sampling or wrap modes.**

Neither GL nor Vulkan specifies bilinear filtering weights to bit precision — implementations may
use reduced precision for the interpolation — and boundary behaviour for mirror/clamp differs
between them. Two stacks running byte-identical shader source will still diverge in exactly the 9
resamplers.

The fix is to make the shader self-contained:

- **explicit `texelFetch` + manual lerp** instead of a `LINEAR` sampler;
- **explicit mirror arithmetic** on the coordinate instead of `MIRRORED_REPEAT` / `SkTileMode::kMirror`.

This costs performance but makes the resamplers deterministic across stacks — and because the weights
are then ours, they can be made to match OpenCV's `INTER_LINEAR` pixel-centre convention, so the C++
oracle, the server and the browser all agree rather than just two of the three.

**Measure this on one effect before committing to it.** W20's gate is `transitions_chain` ≥ 70 fps;
explicit sampling is the one part of this design that could threaten it.

## PROPOSED — the contract the front end must satisfy

Shared source is not enough on its own. These six will diverge even with byte-identical shader code,
so each needs one written answer that both sides implement:

1. **Alpha.** Premultiplied or straight. `effects.h` already carries a `premultipliedAlpha` flag
   precisely because libopenshot Frames are premultiplied and canvas `ImageData` is not; Pixi
   premultiplies by default. Getting this wrong shows up as haloing on semi-transparent edges.
2. **Colour space / gamma.** Whether blending is done in sRGB or linear. `CLAUDE.md` already warns
   that attaching sRGB to the pooled GPU surfaces "makes every blend gamma-correct and changes
   output". This is a *visible* difference, not an LSB one.
3. **Sampling.** Filter and pixel-centre convention. The single biggest source of difference in the
   9 resamplers; see the lever above.
4. **Border rule.** `BORDER_REFLECT` → mirror, done in-shader. This is the area that already needed
   the `BorderReflectedMove` edge-seam fix, so it deserves its own targeted test rather than relying
   on the generic gate.
5. **Precision.** Force `highp` in GLSL, or mobile devices will band visibly.
6. **Parameter reference resolution.** Every length-valued parameter normalised to a fraction of
   frame dimension, with the reference frame stated. This is the "MEASURED" bug above; it has to be
   settled here or shared shaders will still disagree.

Plus, per effect: exact uniform names, types and ranges. That list is the deliverable the front end
works from, since it is what makes a wiring job possible without reading this repo.

## PROPOSED — how it gets verified

A **three-way parity harness**: a fixed corpus (hard edges, gradients, semi-transparent regions,
fully transparent regions), each effect at three parameter values, rendered by

- the OpenCV C++ oracle,
- the server SkSL (Vulkan *and* lavapipe, as the existing GPU checks already do),
- a headless-browser GLSL run,

compared with the same PSNR/SSIM metric `tests/golden` already uses, and failing a build on
regression.

Without this, "near-identical" is something nobody can check. With it, it is a number.

## PROPOSED — phasing

1. **Fix the parameter reference resolution** (the MEASURED bug). Independent of everything else,
   and it is a live visible difference today.
2. **The 8 per-pixel effects.** Parity is nearly free, so this proves the generator, the contract
   and the harness on easy ground.
3. **The 9 resamplers**, with explicit sampling, measuring parity and cost per effect.

If the approach does not pay off, that is visible after step 2 rather than after all of W20.

## The honest alternative

If exact identity matters more than server transition throughput: **do not GPU-port the transitions
at all.** Keep both sides on the shared C++/WASM, drop W20's shader work, and forfeit the
`transitions_chain` ≥ 70 fps gate. That is the only option that keeps identity by construction.
Everything else trades some parity for speed, and it is better to choose that knowingly.

## UNKNOWN — open questions

- **Pixi version and renderer.** Now only matters for how a CanvasKit-rendered result is composited
  into the existing preview, not for the shader language.
- **The reference resolution decision.** Fixing the blur parameters is a behaviour change visible to
  existing projects: old payloads carry radii authored against the old, undefined behaviour. Either
  the fix is versioned in the payload, or existing projects shift. This needs a product decision,
  not a technical one.
- **Which resolution the front end's slow-effect proxy uses**, and whether it varies. If it varies,
  no fixed compensation factor exists — normalisation is the only fix.
- ~~**Whether the front end adopts the shared shaders at all.**~~ **Decided 2026-09-18: yes, as
  SkSL through CanvasKit.** What replaces it is narrower and is a question for the front-end team,
  not a decision: **can they route video frames through CanvasKit, or only text?** Everything above
  assumes they can.

## Reproducing the measurement

The blur table above came from a short program linking the submodule directly: build a step-edge
`cv::Mat` at each resolution, call `Podcastle::Effects::applyBlurEffect(img, 10, 10)`, and measure
the 10–90 % luminance transition width as a fraction of frame width. No libopenshot pipeline is
involved, so it isolates the parameter question from everything else.
