# Spike — one SkSL source, a generated GLSL ES twin, and what they actually agree to

**Question.** Can a `.frag` for the front end be *generated* from the SkSL the server runs, so the
two stacks are the same algorithm rather than two ports of it?

**Answer: yes**, with one caveat about the tool. `rotational_blur.rts` is the source; `sksl2glsl.py`
emits `rotational_blur.frag`, WebGL 1 / GLSL ES 1.00, validated with `glslang`. On the hardest
transition effect we have, the generated shader agrees with the existing C++ to **57–61 dB PSNR,
2–3 LSB max**, against W20's 45 dB gate.

Specimen chosen on purpose: `applyRotationalBlur` is the most complex effect in
`image-processing-lib` — up to 30 rotated taps, two different angle schedules, bilinear resampling,
two border modes, and parameter-dependent behaviour.

## The caveat: `skslc -> .glsl` does not work for runtime effects

The obvious route is a dead end and it fails *silently*, which is worth knowing before someone
spends a day on it. Skia's standalone GLSL and WGSL back ends do not lower runtime-effect children.
A shader containing `child.eval(coord)` compiles without error and emits:

```glsl
vec4 c = ;        // <- the sample is simply gone
```

The child-sampling logic lives in `SkSLPipelineStageCodeGenerator`, which Skia uses at draw time to
inline a runtime effect into a real program, and it is reachable from `skslc` only through the
**`.stage`** output. That output is a normalised SkSL body with the child call preserved as
`child_0.eval(_coords)` — a clean placeholder.

So the pipeline is `.rts` --`skslc`--> `.stage` --`sksl2glsl.py`--> `.frag`, and `sksl2glsl.py` is a
type rename plus a host prologue, about 120 lines. **Skia does the parsing, inlining and constant
folding; the script never touches an expression.** That distinction is the whole value: the SkSL
`... * kPi / 180.0` is folded by Skia into `* 3.14159274) * 0.00555555569`, and both stacks inherit
*that*, rather than each rounding `/180.0` in its own way.

## Measured

`parity_check` renders the SkSL through `SkRuntimeEffect` and the same input through the C++
oracle, then reports PSNR and per-channel max delta.

| input | blur | PSNR | max delta |
|---|---:|---:|---:|
| 256x256, alpha edge | 0.5 | 61.34 dB | 2 |
| " | 3 | 59.61 dB | 2 |
| " | 6 | 58.27 dB | 3 |
| " | 8 | 58.06 dB | 3 |
| " | 12 | 58.13 dB | 2 |
| " | 15 | 57.29 dB | 3 |
| " | 20 | 57.31 dB | 3 |
| " | 40 | 58.66 dB | 2 |
| " | −20 | 57.32 dB | 3 |
| 512x512, alpha edge | 12 | 56.01 dB | 4 |
| 512x512, alpha edge | 20 | **32.86 dB** | **85** |

The last row is not a defect — see "What the shader deliberately leaves out".

## The alpha convention is worth more than every other detail combined

The first run measured **41.39 dB, 60 LSB**. The only cause was premultiplied versus straight alpha:
Skia hands a shader premultiplied colour and expects premultiplied colour back, while the C++ blurs
a `cv::Mat` of RGBA bytes channel by channel with no alpha weighting anywhere. Unpremultiplying each
tap and re-premultiplying the result moved it to **58.27 dB, 3 LSB** — a 17 dB swing from one
convention, on an effect where everything else was already right.

Two things follow. This is the single convention to pin first, and a parity harness that only ever
tests opaque images will report success and prove nothing.

## What the shader deliberately leaves out, and why the 512/20 row is 33 dB

`applyRotationalBlur` is not purely per-pixel. Around its core it wraps two passes that are
performance shortcuts, not part of the algorithm:

- **An adaptive downscale.** `absBlur > 15 && min(w,h) > 400` renders at 0.5 scale; `absBlur > 45 &&
  min(w,h) > 800` at 0.25. That is what the 512x512 / blur 20 row measures: the C++ blurred a
  256x256 image and scaled back up, the shader did not.
- **A mild Gaussian** for `absBlur < 15`, `sigma = absBlur / 60`. Measurably irrelevant — at
  sigma 0.13–0.2 OpenCV's derived kernel is effectively an identity, which is why the blur-8 and
  blur-12 rows are as clean as the rest.

**This needs a decision before W20, and it is not only a shader question.** The downscale means the
C++ oracle's *output quality depends on image size and blur strength* — a 4K export and a 720p
preview of the same project are not the same effect today. On the GPU the shortcut buys nothing.
Dropping it is the right answer technically and changes existing projects' output, which makes it
the same class of decision as the blur reference-resolution item already open in
`TRANSITION-PARITY.md`.

## Conventions the front end must satisfy

All of these are in the shader comments too; they are repeated here because each one silently
degrades the result rather than failing.

1. **Straight alpha in, premultiplied out** — as above.
2. **NEAREST sampling, no mipmaps.** The shader computes its own bilinear weights on purpose:
   neither GL nor Vulkan pins interpolation to bit precision. A LINEAR sampler replaces our weights
   with the driver's.
3. **`highp float`.** `mediump` is fp16 on mobile. The accumulator alone sums up to 30 taps.
4. **Pixel-space coordinates, fragment centres at integer + 0.5**, OpenCV's convention where pixel
   `(x, y)` has its centre *at* integer `(x, y)`. The shader subtracts the 0.5 itself.
   `gl_FragCoord.xy` is exactly right when the filter target is 1:1 with the texture.
5. **Uniforms**: `uSize` (pixels), `uBlurAmount` (degrees, signed), `uInvSize` (`1.0 / uSize`,
   GLSL only — Skia's child shader is already in pixel units). Tap count and every tap angle are
   derived *inside* the shader from `uBlurAmount`, so the two hosts cannot disagree about them.

## Files

| file | what |
|---|---|
| `rotational_blur.rts` | **the source of truth.** SkSL runtime effect |
| `rotational_blur.frag` | generated. WebGL 1 / GLSL ES 1.00. Do not edit |
| `sksl2glsl.py` | `.rts` -> `.stage` (via `skslc`) -> `.frag` |
| `parity_check.cpp` | renders SkSL through `SkRuntimeEffect` and the C++ oracle, reports PSNR |
| `testdata/` | 256x256 and 512x512 checker + radial ramp + hard alpha edge |
| `out/` | `input.png`, `skia_sksl.png`, `cpp_oracle.png` from the last run |

## Reproducing

`skslc` is not part of a normal Skia build. Build it once:

```bash
cd ~/skia-stable/skia
bin/gn gen out/skslc --args='is_debug=false skia_compile_sksl_tests=true is_official_build=false'
ninja -C out/skslc skslc
cp src/sksl/*.sksl out/skslc/          # skslc loads its modules from beside the binary
```

Then, from this directory:

```bash
./sksl2glsl.py rotational_blur.rts rotational_blur.frag     # SKSLC= to override the path

# The spike links Skia and OpenCV directly, not libopenshot. It needs the GPU prefix only
# because that is the one whose installer ships modules/skcms, which SkColorSpace.h includes.
cmake -S . -B build -DSkia_ROOT=/usr/local/skia-gpu
cmake --build build
./build/parity_check testdata/checker_256.png 6 out
```

Validating the generated GLSL as WebGL 1 needs `glslang` (vendored at
`~/skia-stable/skia/third_party/externals/glslang`):

```bash
cmake -S ~/skia-stable/skia/third_party/externals/glslang -B /tmp/glslang-build \
      -DCMAKE_BUILD_TYPE=Release -DENABLE_OPT=OFF -DGLSLANG_TESTS=OFF -DENABLE_HLSL=OFF
cmake --build /tmp/glslang-build -j8 --target glslang-standalone
(echo '#version 100'; cat rotational_blur.frag) > /tmp/v100.frag
/tmp/glslang-build/StandAlone/glslang -S frag /tmp/v100.frag
```

## What this does not answer

- **Browser numbers.** Everything above is Skia's CPU raster backend against OpenCV. The front end
  running the generated `.frag` on a real GL driver is the point of handing this over.
- **Whether the front end can put video frames through CanvasKit at all**, which is the open
  dependency behind the SkSL decision in `GPU-DECISIONS.md`. This spike makes that question less
  urgent, not moot: if CanvasKit cannot take video, the generated `.frag` is the fallback, and it is
  now a measured fallback rather than a hope.
- **Performance.** Explicit bilinear and explicit border arithmetic cost more than letting the
  sampler do it. W20's gate is `transitions_chain` >= 70 fps and this is the design decision most
  likely to threaten it.
