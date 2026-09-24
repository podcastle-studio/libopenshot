# GPU rendering in the podcastle fork

How libopenshot renders on the GPU, what is switched by what, how it is validated, the decisions
that constrain the code, and — at the end — what is still owed before it ships. This file replaces
the migration's working folder (`doc/gpu-migration/`, retired 2026-09-24); the full history of that
work is in git (`git show 628a53d5:doc/gpu-migration/STATUS.md`, and `GPU-WORKLIST.md`,
`GPU-DECISIONS.md` beside it). The W01–W31 item numbers in commit messages and code comments refer to that worklist, and a
comment citing `GPU-DECISIONS.md`, `GPU-WORKLIST.md`, `STAGE6-EFFECTS.md`, `TRANSITION-PARITY.md`
or `GPU-RENDER-PLAN.md` means that file at `628a53d5`.

Working rules and the sharp edges of the GPU code are in `CLAUDE.md`; benchmark history is in
`doc/PERFORMANCE-BASELINE.md`.

## The one constraint

**The CPU path ships; the GPU path is an addition** (project owner, 2026-09-14). Since 2026-09-24 the
service turns every GPU path on by default (below, "The switches"), but a machine with no GPU is still a
supported configuration and runs exactly the CPU pipeline, so:

- no change may make the CPU path slower, worse-looking, or dependent on a GPU;
- CPU code is never deleted because the GPU does not need it — it is gated on
  `GpuDevice::Instance().available()` / `GpuOffscreen::onGpu()` and kept;
- every change is accepted on the **four-way golden sweep** (below), all four arms green.

## What runs where

In the library everything below is off unless switched on; the service switches all of it on by
default (2026-09-24). Every GPU path falls back to the CPU by itself when its requirement is missing. `false` from `available()` is a normal answer, not an error.

| stage | on the GPU | CPU fallback / notes |
|---|---|---|
| decode | NVDEC for H.264, HEVC, VP8, VP9, AV1 (and MPEG-2/VC-1); 8-bit 4:2:0 frames stay on the device through `CudaInterop` into `GpuYuv` (YUV→RGBA on the GPU), bit-exact against software decode (`unit.nvdec_on_device`, all five codecs) | software decode + swscale: ProRes (the watermark), alpha VP9 (libvpx), and a stream NVDEC refuses (> 4096 wide, 4:4:4) reopens in software. 10-bit (P010) is decoded by NVDEC but downloaded and converted by swscale |
| read-ahead | one decode worker per `FFmpegReader` (`READ_AHEAD_FRAMES`, default 2) | host-memory frames only: with `GPU_DECODE` on, only for a stream that can never reach the GPU (no NVDEC, a layout `GpuYuv` does not convert — the ProRes 4444 watermark), since 2026-09-24 |
| compositing | `Timeline::GetFrame` on a pooled Graphite surface; `Clip::draw_to_canvas` in one transformed draw; all 16 blend modes; clip shadow, blur, flip on the paint; the opacity curve as an exact pre-pass; overlay-clip transitions (the overlay composited onto the source frame, the overlay clip transformed on the GPU); a host-memory source (still image, held last frame) uploaded once (`Clip::HostTextureCache`) | a clip that cannot draw on the GPU — a frame-number overlay, a waveform, an effect after keyframes, none of which the service uses — puts the whole frame on QPainter only if its blend mode is not `NORMAL` (`Timeline.cpp:1099`); a `NORMAL` one reads the canvas back mid-frame in `apply_background` |
| text, subtitles | the whole Skia text engine incl. the glow ray-march; the resting text frame is kept on the GPU; subtitles draw on whatever canvas they are given | raster Skia |
| effects | every one of the 20 the service constructs, at every frame size and every parameter the production presets reach: `GpuEffect` + SkSL twins for the per-clip effects and the transitions (the rotational and diagonal blurs above the reference size, zoom-out, and box blurs up to 1023 taps as planned multi-pass chains; wide boxes two taps a fetch), the overlay composites with the overlay resized on the GPU, CameraMovement as a GPU draw, the planner's identity and clear; a GPU-decoded mask matte is prepared on the GPU | the C++ twin runs whenever a fragment declines, which with a GPU is now only past a loop bound no preset reaches (a box > 1023 taps) |
| crop | `Crop` behind `GPU_CROP`, on GPU frames and on host frames (a still, a shape: uploaded once) | QPainter |
| encode | NVENC takes the composited frame from the GPU (`GPU_ENCODE`) | readback + swscale + libx264 / NVENC |
| telemetry | `GpuCounters` on every GPU path and fallback; `ExportTelemetry` (NVML) — the service logs one line per export | — |

**The effects in scope** are the 20 classes `../video-rendering-service` constructs — per clip
(`VideoRenderingImpl.cpp`): ChromaKey, ColorAdjustment, ColorMap, Crop, Enhancement,
LightAdjustment, Mask; transitions (`Transition.cpp`): Alpha, Bars, Blur, BorderReflectedMove,
BorderReflectedRotation, Brightness, CircleMask, ColorShift, Exposure, SplitShift, Wipe, Zoom;
animation (`Animation.cpp`): CameraMovement. The other effect classes are upstream OpenShot's and
untouched.

**Not on the GPU, deliberately:** `Crop` without `GPU_CROP` (the switch exists because the GPU's rounded
corners antialias differently; the service turns it on); decoding what NVDEC cannot — ProRes (the
watermark: 144 small frames per export, read ahead on their own thread and uploaded, the held last
frame once), still images and SVG shapes (decoded once, uploaded once), alpha VP9 (libvpx); 10-bit
video's conversion (P010, see "What is left", 6); ChromaKey's non-YCbCr methods (babl colour science; the service only uses YCbCr); ColorMap's
colour-match mode (re-bakes the cube from the frame, i.e. a readback). Enhancement's grain *is* on
the GPU (since 2026-09-24), but not as a parity fragment — see "Parity". Also CPU by nature and
out of scope: **audio** — the service mutes every clip (`volume = 0`, `SetSilentAudioMode`), mixes
the real audio outside libopenshot (`AudioExportHelper`, then an `ffmpeg amix` mux), so libopenshot
only decodes the clips' audio streams and encodes a silent AAC track. The 2026-09-24 audit (see
"What is left", 5 and 6) found everything else the service reaches, and all of it now runs on the
GPU: `unit.gpu_resident` holds the whole production feature set to no readback, no CPU decode and
no CPU fallback.

### The switches

The service's defaults are all "on" since 2026-09-24 (`RenderBackend`, the image's `ENV`): `ENCODER=h264_nvenc`,
`OPENSHOT_GPU=vulkan`, `OPENSHOT_GPU_DECODE/_ENCODE/_CROP=1`, the last three applied only when a GPU came up.
The "default" column below is the library's, which the golden suite and the bench rely on.

| switch | set by | default | what it does | changes pixels? |
|---|---|---|---|---|
| `OPENSHOT_GPU` | env, or `GpuDevice::SetBackend()` (overrides) | `off` | `vulkan` / `lavapipe` enable every GPU path at all | slightly — the four-way sweep is bit-exact except text (PSNR ≥ 38), three blend modes, film grain (same grain, different random phase), the circle mask's edge ring (analytic, not OpenCV's; ≥ 40 dB), a rotational blur above 1280 px (≤ 1 LSB), a mask matte of another size (bilinear, not Qt's box; ~58 dB), zoom-out (≤ 1 LSB), CameraMovement (Skia's bilinear and edge rule for QPainter's; ≥ 44 dB) and the overlay transitions (now GPU-composited like every clip) |
| `ENCODER` | service env | `libx264` | `h264_nvenc` (probed once; falls back) | encoder output only |
| `GPU_DECODE` (+ `HARDWARE_DECODER=2`) | service env `OPENSHOT_GPU_DECODE` | off | NVDEC + GPU YUV→RGBA, frames never leave the device | **yes**: GPU rounding; honours a `bt709` tag swscale never did (12 frames + 3 checks of 307 move on Vulkan) |
| `GPU_ENCODE` | service env `OPENSHOT_GPU_ENCODE` (needs `ENCODER=h264_nvenc`) | off | NVENC reads the GPU frame | **yes**: box vs bicubic chroma |
| `GPU_CROP` | service env `OPENSHOT_GPU_CROP` | off | `Crop` draws GPU frames on the GPU | **yes**: rounded corners antialias differently from QPainter |
| `READ_AHEAD_FRAMES` | `Settings` | 2 | decode-ahead depth; 0 = off | no (~8 MB per open reader per frame) |
| `RENDER_SINGLE_WRITEFRAME` | service env | off | one `WriteFrame` call per export instead of 8-frame chunks | no (byte-identical); ~6 % on the corpus payload |

All the GPU paths need `OPENSHOT_GPU=vulkan`; the NVDEC/NVENC interop also needs the NVIDIA driver's
`libcuda` in the container (it is `dlopen`ed; only `cuda.h` is a build dependency). The service
reports what it resolved in its first log line (`RenderBackend: encoder=… | OPENSHOT_GPU=… |
gpu_decode=… gpu_encode=… gpu_crop=…`).

## Building

```bash
# CPU Skia (the shipping library)
cmake -S . -B cmake-build-release -DCMAKE_BUILD_TYPE=Release
# GPU-capable Skia (Graphite + Vulkan), from skia_build_script_gpu.sh
cmake -S . -B cmake-build-gpu -DCMAKE_BUILD_TYPE=Release -DSkia_ROOT=/usr/local/skia-gpu
```

The two Skia builds, what the GPU installer adds and why, and the Vulkan 1.4 header workaround are in
`CLAUDE.md` ("Skia"). Both pin milestone **m147** to match the editor's CanvasKit.

## Validating a change

| gate | command | what it proves |
|---|---|---|
| four-way golden sweep | `tools/golden.sh check` (CPU Skia); `BUILD_DIR=$PWD/cmake-build-gpu tools/golden.sh check` with `OPENSHOT_GPU` unset, `=vulkan`, `=lavapipe` (give the GPU arms their own `GOLDEN_OUT`/`GOLDEN_REPORT`) | 117 scenarios, 307 frames, 39 checks, all four green |
| GPU unit checks | `OPENSHOT_GPU=vulkan cmake-build-gpu/tests/gpu/openshot-gpu-checks` (and `=lavapipe`) | device lifetime, pools, the single control, readback |
| effect parity | `OPENSHOT_GPU=vulkan cmake-build-gpu/tests/gpu/openshot-gpu-effect-parity` (and `=lavapipe`, ~45 min) | every fragment against its C++ twin over eight alpha-edge images; refuses a case that never reached the GPU. Exits non-zero today on the per-pass cost gate only (see "What is left", 4) |
| CUDA interop | `cmake-build-gpu/tests/gpu/openshot-gpu-cuda-interop` | NVDEC/NVENC hand-off, per-pair semaphores |
| payload corpus | `tests/payloads/run-corpus.sh` | a real production payload through `render-payload`, two rounds hash-stable, against a recorded hash |
| benchmark | `openshot-bench` — see `doc/PERFORMANCE-BASELINE.md` | performance against `baseline-cpu.json` |

Harness switches for the flagged paths: `OPENSHOT_GOLDEN_GPU_DECODE=1`, `OPENSHOT_GOLDEN_HW_DECODE=1`,
`OPENSHOT_GOLDEN_GPU_CROP=1`; bench: `OPENSHOT_BENCH_GPU_DECODE=1`, `OPENSHOT_BENCH_HW_DECODE=2`,
`OPENSHOT_BENCH_GPU_ENCODE=1`, `OPENSHOT_BENCH_GPU_CROP=1`, `OPENSHOT_BENCH_PIPELINE=1` (the
service's threading), `OPENSHOT_BENCH_READ_AHEAD`, `OPENSHOT_BENCH_REPEAT=N`.

**Benchmarking this laptop:** mains power only (battery is ~1/3 speed), never straight after a
build, and interleave the arms — a sequential A/B once read +55 % where the truth was 0 %.

## Parity

**Server, CPU vs GPU.** Everything that is not text is gated bit-exact in the golden suite; text
passes at PSNR ≥ 38 (glyph antialiasing), and three blend modes — colour-burn, hue, saturation —
pass at `Tolerance::GpuAmplified()` (PSNR ≥ 25) by owner decision: Skia's bilinear resample of the
clip differs from QPainter's by ~1 LSB and those three formulas amplify it. Effect fragments are
bit-exact wherever reachable; where not, it is one of exactly two causes: a division by alpha
(Vulkan allows 2.5 ULP — always 1 LSB, amplified by a contrast/exposure factor to 3–5), or a
`double` parameter carried as a `float` uniform.

**Film grain is the one deliberate exception** (owner, 2026-09-24). Enhancement's grain hash,
`fract(sin(x·12.9898 + y·78.233)·43758.5453)`, is evaluated in `double` by the C++ and in `float` by
the fragment, and at 1080p arguments that is a different random value per pixel, not an LSB. The
fragment is the same formula, seed, amplitude and rounding, so it is the same grain at a different
phase — and the CPU grain could not survive a GPU path anyway, because it is seeded from the pixel's
own colour and `GPU_DECODE` moves that by an LSB. It is gated statistically instead: spread within
5 % and mean within 0.5 LSB of the CPU twin's, no neighbour correlation (float `sin()` at large
arguments is where such a hash turns into stripes), alpha untouched — `unit.gpu_grain` and the
parity tool's `enhance(grain)` / `enhance(all, prod)` cases. `effects.enhancement` carries grain and
is held on the GPU arms only to `Tolerance::GpuGrain()` (not a gate); `effects.enhancement_no_grain`
holds the GPU's clarity and sharpness to `GpuClose`.

**Also not bit-exact, by decision (owner, 2026-09-24):** the circle mask's edge (analytic on the
GPU, OpenCV's rasterised polygon in the C++: interior and exterior exact, the one-pixel ring
37–50 dB, `Tolerance::GpuEdge` / the parity tool's `kCircleEdgeGate`); the rotational blur above
the reference width (62 dB, 1 LSB: the chain's intermediate is 8-bit where the C++'s is float);
a video mask matte resampled to another size (bilinear where Qt box-averages a shrink, ~58 dB,
2 LSB); zoom-out (76–80 dB, 1 LSB: resample_linear's rare fixed-point miss); CameraMovement drawn by
Skia (bilinear and the rotated edge's aliasing where QPainter smooth-transforms; 47 dB at worst in
`effects.camera_movement`). Bit-exact: the diagonal blur's chain above a megapixel (odd sizes within
1 LSB), the wide box blurs read in pairs (`unit.gpu_blur_pairs`), the opacity curve's pre-pass, NVDEC
of every codec (`unit.nvdec_on_device`).

**Editor vs export.** The transitions are one source on both sides: the C++ in
`src/effects/image-processing-lib` (compiled natively here, to WASM for the editor), the SkSL in its
`shaders/`, and the **planner** (`src/Planner/EffectPlan`) that turns a preset's parameters into
what to run — every GPU effect here binds the planner's uniforms, and the editor calls the same
planner through the WASM. How the editor integrates it:
`src/effects/image-processing-lib/doc/FRONTEND-INTEGRATION.md`. The target across stacks is "no
visible difference, measured" (PSNR ≥ 48 dB per-pixel, ≥ 45 dB resampling), not bit-equality —
two GPUs through two APIs never agree to the bit.

Per-clip effects the editor does not share (chroma key, colour/light adjustment, enhancement, mask,
LUT) have their fragments in `src/shaders/`; LUT parsing and colour match are `src/effects/
ColorGradingCore`.

## Decisions that constrain the code

Each of these was decided once, with a measurement or an owner call behind it; do not re-open one
without the "revisit if" condition being true.

- **Skia Graphite on Vulkan**, not OpenCV CUDA, hand-written Vulkan or Ganesh. Skia already owned
  text, subtitles, blend modes, filters and SkSL, and matches the editor's CanvasKit. There is no
  Ganesh code; keeping it "alive" would mean writing a second backend.
- **One device, one `Context`, recorders and pools owned by the device**; nothing Graphite hands out
  outlives the `Context`; caches are keyed on `GpuDevice::Generation()`; `GpuSurfacePool` is per
  thread. Details in `CLAUDE.md`.
- **Timeline canvas is `kRGBA_8888` with a null colour space**, not F16 / sRGB: it keeps the GPU
  canvas bit-identical to the CPU path, and everything shipped is 8-bit H.264. *Revisit if* 10-bit
  or HDR output reaches the roadmap, or stacked blends show banding.
- **A frame composites on one path**, whole frame or none, because mixing QPainter and Skia within a
  frame costs a readback per switch. QPainter rounds a translate-only transform, so the GPU path
  rounds it too.
- **SkSL is the one shader language**, run by both hosts (owner, 2026-09-18); **the planner is the
  one implementation of the parameter arithmetic** (owner, 2026-09-23). A host never computes a
  uniform.
- **Length-valued effect parameters use a 1280 px-wide reference**, unversioned (owner,
  2026-09-22): exports match the editor's 720p preview. The blur is a normalised Gaussian (three
  boxes), not a single box; the zoom blur's polar conversions are bilinear.
- **Nearest stays nearest** where the C++ chose it (border-reflected rotation, displacement map).
- **Film grain on the GPU is the same grain at a different random phase** (owner, 2026-09-24),
  gated statistically. It was the one per-pixel pass that kept a whole effect on the CPU: with
  grain on, the service's ADJUSTMENT filter read the frame back and ran clarity, sharpen and grain
  in OpenMP at source resolution. *Revisit if* grain must be reproducible across paths — then the
  hash has to change on both sides to one exact in `float`.
- **The circle mask's edge is analytic on the GPU** (owner, 2026-09-24): a ramp on the distance to
  the centre fitted to OpenCV's coverage (0.4 px inside, 1.2 px wide), with OpenCV's own centre and
  1/8-px radius, instead of OpenCV's circle rasterised on the CPU and uploaded every animated frame
  (+7.4 ms at 1080p). The C++ is unchanged, so CPU and GPU differ on the ring. *Revisit if* the edge
  must match across paths — then both sides take one analytic circle.
- **Resizes inside an effect are passes** (2026-09-24): `resample_linear` (INTER_LINEAR) and
  `resample_area2` (INTER_AREA at half size) reproduce OpenCV's fixed point — the SIMD vertical
  kernel, 11-bit weights — so the diagonal blur's chain is bit-exact and the rotational one within a
  code value. The planner, not a host, decides the chain. *Revisit if* OpenCV's resize arithmetic
  changes (a new OpenCV, or an IPP-enabled build): re-measure against `cv::resize`.
- **A GPU-decoded mask matte is resampled on the GPU with Skia's bilinear** (owner, 2026-09-24),
  not Qt's `SmoothTransformation`; bit-exact when the matte is the frame's size. *Revisit if* mattes
  are routinely much larger than the clip (a box filter would then be the right one).
- **Every GPU path is on by default in the service** (owner, 2026-09-24): `ENCODER=h264_nvenc`,
  `OPENSHOT_GPU=vulkan`, GPU decode/encode/crop — accepting their measured pixel changes. The
  library keeps defaulting off (the suite and the bench name their arms explicitly). Two guards
  make "on by default" safe on a CPU node: the decode/encode/crop switches are applied only when
  the device came up, and `vulkan` never picks a CPU-type Vulkan device, so the image's lavapipe is
  used only by name. Verified: a simulated CPU node (only lavapipe visible, no CUDA device) renders
  the corpus payload to its recorded CPU hash. *Revisit if* a GPU node's output must match a CPU
  node's byte for byte — then the pixel-changing three go back to opt-in.
- **Nothing the service constructs runs on the CPU when a GPU is present** (owner, 2026-09-24): the
  frame goes decode → every effect, transition, overlay, animation, text and subtitle → encode
  without leaving the device, gated by `unit.gpu_resident` (no readback but the one the harness
  makes for lack of an encoder, no CPU decode, no CPU fallback, no uploads after warm-up). What
  stays on the CPU is only what cannot be on a GPU (ProRes and still-image decode, audio) or is
  outside the service's use. Where porting moved pixels it was accepted, and each case is listed
  under "Parity". *Revisit if* a feature is added to the service — it joins `unit.gpu_resident`.
- **CameraMovement is a Skia draw on the GPU** (owner, 2026-09-24), not QPainter: the same transform,
  bilinear where QPainter smooth-transforms and snapped to whole pixels for a translate-only move,
  as the compositor does. *Revisit if* animations must match the CPU byte for byte.
- **Wide box blurs read two taps per fetch through the linear filter, where the device allows it**
  (2026-09-24): `blur_pairs.sksl` is byte-identical to `blur.sksl` on a device whose filter keeps
  the pair sum exact at a texel midpoint, which `linearMidpointIsExact()` measures once per device
  (NVIDIA yes, llvmpipe no → plain `blur`). 2.4–2.7× faster on the production presets' blurs.
  *Revisit if* a device passes the probe and a golden moves — the probe then misses a case.
- **The opacity curve on the GPU is a pre-pass, not paint alpha** (2026-09-24): `floor(byte * alpha)`
  before the transform, exactly the CPU loop, so a fade is bit-exact on the GPU where paint alpha
  was an LSB off.
- **Glow quality is fixed**: in-motion glow matches resting glow; speed comes from the GPU and the
  composited-glow cache, never from fewer steps or a lower resolution.
- **No CPU frame-level parallelism**: Graphite parallelises through pipeline depth (decode-ahead,
  encode on the device); width is the process manager's job. *Revisit if* the CPU fallback becomes
  a product requirement at scale.
- **Qt stays** (owner, 2026-09-23): QPainter is the CPU path that ships; removing it (old Stage 8)
  is void.
- **Exports are BT.709 and the reader honours declared matrices** (owner, 2026-09-23), on swscale
  and `GPU_ENCODE` alike; untagged stays BT.601 on both sides. Never fix one side alone.
- **NVENC**: one quality knob for both encoders (`crf`; NVENC `cq = crf + 10`, calibrated), spatial
  AQ on, preset **p4** (same VMAF as p5, twice the throughput). B-frames left to the caller.
- **Clip sort is insertion-stable**: equal layer and position keep insertion order (the old order
  was undefined behaviour, upstream's was random per run). Awaiting confirmation — see below.
- **Upstream merges: upstream wins on plumbing, the fork wins on pixels.** The fork wins wherever it
  delegates to image-processing-lib, is a deliberate fork feature, or the golden suite encodes the
  behaviour; upstream wins on decode correctness, new APIs and build files. The 2026-09-14 merge is
  done; `USE_QT6` stays `OFF` until moved deliberately.

## Known issues and divergences

- **Resampled alpha boundaries vary with what else ran in the process** (golden suite, not fixed):
  1,280 pixels on the interpolated alpha edges of a scaled PNG move between an isolated and a full
  run, and between two 4-thread runs. Nothing is red — the affected scenarios were rebuilt on 1:1
  clips — but the instability is real. Start with whether anything caches a scaled image per path
  rather than per reader.
- **Diagonal blur still halves above one megapixel** (on the GPU too since 2026-09-24, as the same chain) — the one downscale threshold the reference
  decision did not retire, so the same radius renders at full scale from 720p and at half from 1080p.
- **The editor leaves some LUTs ungraded** (1-D-only cubes, a 3-D cube with a 1-D shaper, any
  non-WebGL2 renderer) where the export grades them.
- **Film grain never matches across paths, by construction** (see "Parity"): a grain clip measures
  ~23 dB CPU vs full-GPU where everything around it is ~42. Exclude grain clips, or compare them by
  statistics, when checking parity.
- `effects.stack_crop_chroma_light_lut` shows harsh white blotches (ChromaKey + Light + LUT
  stacked); baselined as-is.
- **`render-payload` prints a garbled Pub/Sub topic name** in its progress messages once rendering
  starts (it reads like a string outliving its owner in the offline tool's publisher; not yet
  investigated). The video is unaffected, but on 2026-09-24 every run also **stopped logging
  mid-render** — no `ExportTelemetry` line, no summary, exit 0, all 750 frames written — which
  looks like the same corruption leaving `std::cout` failed. Until fixed, time a run by the export
  file's birth → last write.
- **Heap corruption under gdb** (2026-09-24): bench `podcast_pip`, `everything` and
  `transitions_chain`, every flag on, aborted in glibc (`corrupted size vs. prev_size`,
  `free(): invalid size`) under a gdb breakpoint tracer; they run cleanly without it. A
  timing-sensitive memory bug is the likeliest reading; chase it before the soak (11). ASan on the
  GPU build is the first thing to try.

## Status (2026-09-24) and what is left

**Done.** Every render stage the service uses can run on the GPU, under one control, with the CPU
path intact and bit-exact to what shipped: decode (NVDEC → GPU YUV), read-ahead, compositing, text
and subtitles, all effects and transitions the service constructs except the two QPainter ones,
crop (flagged), NVENC from the GPU (flagged), BT.709 end to end, and per-export telemetry. The
editor has a shared planner and a published WASM (`dist/image_processing_lib_v2.1.0`) to reach
parity. Headline numbers (RTX A2000, 1080p) are in `doc/PERFORMANCE-BASELINE.md`, "End of the
migration".

**Also done 2026-09-24: the CPU work the audit found** (5 below; A1–A9 moved to the GPU, A10 left).
Consumers must rebuild against the new headers — `Clip`, `Mask`, `CircleMask`, `GpuEffect`,
`TextClipReader` and `GpuCounters` changed layout; `../video-rendering-service`'s `cpp-third-party`
was refreshed.

**What is left is decisions, infrastructure and final checks.** In the order they should happen:

### 1. Owner decisions

1. ~~**Defaults for `GPU_DECODE`, `GPU_ENCODE`, `GPU_CROP`.**~~ **Decided 2026-09-24: on**, with every
   other GPU path, in the service (see "Decisions"). Owed: the corpus's recorded hash is the CPU
   render, so a GPU node's output has no recorded reference yet — record one per GPU (10).
2. **`compositing.layer_order`** — accept insertion-stable ordering for clips sharing a layer and
   position, or fix the collision in the service (the background clip shares layer 1 with content,
   which is arguably the real bug).
3. **`RENDER_SINGLE_WRITEFRAME`** — off until the 1/s progress cadence is watched once where Pub/Sub
   and Redis answer (`render-payload` points both at dead addresses). ~6 % on the one corpus payload;
   re-measure on an encode-bound payload before deciding.
4. **The release gates as written are stale** (R3 `grid_3x3` ≥ 60, R4 `heavy_effects` ≥ 60 at
   1080p; the ≤ 0.2 ms per-pass effect gate). They were written before the flagged paths existed and
   measure the default flags; the blur family fails the per-pass gate by design (206 fetches a pixel)
   while beating its CPU twin. Restate them against the full-GPU configuration (check 6 below) or
   drop them.

### 2. Infrastructure

5. **Build and pin the real service image** (W01): the build-stage base is in a private registry
   (`gcloud auth login`, then `docker build`); pin both bases by digest; confirm the build stage has
   the Skia headers; deploy to a CPU node and a GPU node (`helm/values_gpu_example.yaml`). The GPU
   node needs `libcuda` and the Vulkan ICD (`NVIDIA_DRIVER_CAPABILITIES` including `graphics`).
   Gate: one export on each; the CPU node's output identical to today's; `tools/gpu-preflight.sh`
   shows NVENC and the NVIDIA ICD on the GPU node.
6. **Density on the target GPU** (W30, L4 owed): `openshot-bench --parallel 1,2,4` with every flag
   on; set `SERVICE_NUM_INSTANCES_PARALLEL`, GPU time-slicing and pod requests from it. The laptop
   says a GPU-bound export already keeps the GPU ~85 % busy, so the unit of density is the GPU, and
   **2 exports per GPU** is the indicative start.
7. **CI** (W03): `.github/workflows/golden.yml` is written and has never run. It needs the pinned
   FFmpeg (run it `container:`-ed on the base image, which is the same registry blocker as 5) and a
   deploy key for the private submodule (`secrets.SUBMODULE_SSH_KEY`). Gate: a PR shifting one
   pixel fails.

### 3. Final checks before merging to `develop`

8. **Full CPU benchmark on a quiet machine** (W02): `openshot-bench --label release` (~90 min)
   against `baseline-cpu.json`; no scenario more than 5 % slower. This is the standing constraint's
   proof and has not been run since the upstream merge.
9. **Full-GPU benchmark**: the same matrix with every flag on (`OPENSHOT_GPU=vulkan`,
   `ENCODER=h264_nvenc`, `OPENSHOT_BENCH_GPU_DECODE=1 OPENSHOT_BENCH_HW_DECODE=2
   OPENSHOT_BENCH_GPU_ENCODE=1 OPENSHOT_BENCH_GPU_CROP=1 OPENSHOT_BENCH_PIPELINE=1`), recorded in
   `doc/PERFORMANCE-BASELINE.md` as the GPU baseline, and the restated gates (4) checked against it.
10. **Payload corpus 1 → 6** (W04): text animations, subtitles, a transition-heavy timeline, chroma
    key, a 4K source — fresh captures, media archived the same day (signed URLs last 24 h). Each
    rendered on the CPU and full-GPU: hash-stable across two rounds, the CPU render matching its
    recorded hash, and **CPU vs full-GPU compared** frame by frame with the *same* encoder
    (NVENC both sides — against x264 the comparison mostly measures the encoders). The one
    existing payload passed on 2026-09-24: CPU render identical to its recorded hash; CPU vs
    full-GPU ~42 dB on every frame except the grain clip (see "Known issues"), and **2.9× faster**
    (23.2 → 8.1 s) once grain moved to the GPU. Compressed output
    caps what this can see at the encoder's noise (~42 dB); a pre-encode frame dump from
    `render-payload` would make it exact.
11. **Soak and concurrency on the GPU node**: a long export and N concurrent exports with every flag
    on — VRAM and RSS flat (VRAM was flat over 10,080 frames on the laptop), no NVDEC/NVENC session
    exhaustion, telemetry line per export, clean fallback when `libcuda` or the ICD is absent.
12. **Editor parity**: the front end moves to `dist/v2.1.0` (their current v2.0.4, built from the
    submodule's `main`, lacks the 2026-09-22 blur and zoom-blur changes, so preview and export
    already disagree on blurs), then integrates the shaders per `FRONTEND-INTEGRATION.md` and runs
    its checks. The submodule's `main` should take `feature/gpu-rendering` when this ships, or the
    editor's line and the export's keep diverging. **Since 2026-09-24 the planner emits two new
    fragments (`resample_linear`, `resample_area2`) and `CIRCLE_MASK` no longer has a texture**, so
    the editor needs a WASM built after `bb7ad18` (`dist/` has not been republished) and the new
    shaders; `wasm/test/plan-check.mjs` passes against it.

### 4. Worth doing, not blocking

- **Look for the next grain.** The production payload's GPU gain was capped at ~1.5× by one CPU
  effect on one clip for a third of the export (`doc/PERFORMANCE-BASELINE.md`, 2026-09-24). The
  telemetry's `readbacks` counter is how to find the next one: every new payload in the corpus
  should be run full-GPU and any readback count much above its clip-transition count explained.
  The corpus payload's remaining 11 readbacks are the WHOOSH out-clip's `Alpha` at 0 (5, row A5).
  `readbacks` does not see uploads (`GpuFrame::ToTexture`/`upload` have no counter), and uploads are
  the biggest item in 5.
- Overlay clips on the GPU (`isOverlay` frames composite on QPainter; `transitions_chain`).
- Thread budgets from the cgroup (`FF_THREADS`/`OMP_THREADS` from `cpu.max` ÷ instances; W06) —
  needs a container to validate; check what the upstream merge's thread settings already do first.
- Audio is covered by one smoke scenario; upstream's new effects (AnalogTape, Glow, Shadow, …) are
  built but untested here, and unused by the service.
- From the upstream merge: port `apply_keyframes`' corner radius and painter opacity if wanted;
  collapse `BlendMode` and `CompositeType`; send the `lerp` rename upstream.

### 5. CPU work left with every GPU path on (audit, 2026-09-24) — A1–A9 done the same day

**Outcome.** Every row but A10 was moved to the GPU and committed (`98c182e9` A1 + upload counters,
`95d6af64` A2/A5, `e869e5b4` A7, `37ea01d9` A9, `181689e3` A8, `5646ee9e` + submodule `bb7ad18`
A3/A4/A6 and CircleMask's clear). The same A/B afterwards — every per-frame readback gone, cost at
1080p / 720p in ms a frame (JSON: `tests/bench/results/20260924-cpu-work-audit-ab.json`):

| # | before | after | pixels |
|---|---|---|---|
| A1 background still | +1.65 / +0.28 | +0.02 / 0 | unchanged |
| A2 SplitShift at rest | +6.33 / +2.88 | 0 / 0 | unchanged |
| A3 rotational blur | +52.4 / (GPU) +1.15 | +2.03 / +1.24 | ≤ 1 LSB (owner) |
| A4 diagonal blur | +14.7 / (GPU) +0.05 | +0.75 / +0.14 | unchanged (bit-exact chain) |
| A5 Alpha at 0 | +3.17 / +0.73 | 0 / 0 | unchanged |
| A6 CircleMask animating | +7.38 / +0.47 | +0.04 / 0 | edge ring differs (owner) |
| A7 watermark decode | +0.35 / ~0 | ~0 / ~0 | unchanged |
| A8 video mask matte | +6.26 / +3.78 | +0.60 / +0.38 | resampled matte ~58 dB (owner) |
| A9 resting text | ~0 | ~0 (300 → 1 uploads per 60 frames in `text_static_4`) | resting frame under a fade: paint alpha, 46.75 → 46.64 dB |

The production payload and its variants, full-GPU through the rebuilt service (render+encode, one
run each; `doc/PERFORMANCE-BASELINE.md`): split@1080 10.8 → 7.7 s, rotate@1080 11.8 → 7.6 s,
pan-diagonal@1080 9.6 → 8.2 s, whoosh@1080 10.4 → 9.1 s, the corpus payload (720p) 10.6 → 9.9 s.
New gates: `unit.host_texture_cache`, `unit.gpu_blur_large`, `unit.gpu_mask_matte`, a second
`unit.read_ahead` check, `readers.watermark_prores4444` (new ProRes 4444 golden media).

**Left from this audit:** see 6 — A10's codecs, A11 and A12 were done the same day too. **Found
afterwards (2026-09-24, same A/B, 1080p / 720p), both in production presets:**

- **A11 (done, see 6) — Zoom below 100 %** (ZOOM_IN's in-clip 34 → 100, ZOOM_OUT's out-clip 100 → 66): the planner
  returns cpu below 100 (`EffectPlan.cpp`, `planZoom`: the C++ shrinks into a border), a readback
  and OpenCV every frame of the window: **+11.0 / +4.3 ms**. (b); a port would need the C++'s
  shrink-and-reflect as a pass — probably bit-exact like the other resamples, unmeasured.
- **A12 (done, see 6) — box blurs past the fragment's 255-tap loop**: BLUR_VERTICAL's `verticalRadius` 360 goes
  to the CPU at 1080p (**+26.2 ms**, 2 readbacks); at 720p it fits and is on the GPU but still
  +13.3 ms. Big blurs that stay on the GPU are expensive too (PAN_*'s 230: +27.6 / +8.7) — the blur
  family is 206 fetches a pixel per box ("What is left", 4). (c); a raised loop bound fixes the
  CPU case without changing pixels; the cost wants a different algorithm (e.g. a downscaled
  Gaussian), which would change pixels.

The audit as it was written:

**Scope.** Everything `../video-rendering-service` constructs, traced with `OPENSHOT_GPU=vulkan`,
`ENCODER=h264_nvenc`, `GPU_DECODE` + NVDEC, `GPU_ENCODE` and `GPU_CROP` on. Nothing was changed to
measure it.

**How it was measured:**

- **Attribution.** A gdb script (no code changes) counts every `GpuFrame::readback`,
  `GpuFrame::upload`, `SkImages::TextureFromImage`, `sws_scale` and `QPainter::begin` by call
  stack, and puts a hardware watchpoint on `GpuEffect`'s CPU-fallback counter. It was run over:
  - all 119 golden scenarios;
  - the 13 bench scenarios, run in-process with `--case`;
  - the corpus payload through `render-payload`;
  - five payload variants: the corpus payload with its transition swapped for a production preset
    from `firebase-configs`, and/or rendered at 1080p.
- **Cost.** A scratch A/B program times `Timeline::GetFrame` for one feature added to one 1080p
  H.264 clip (NVDEC, on the device). It uses 120 frames, three interleaved rounds and median ms per
  frame, on mains power. The base arm is 2.22 ms at 1080p and 1.77 ms at 720p, and includes one
  final readback that the `GPU_ENCODE` export does not pay.

**The production payload, full-GPU (750 frames, 720p):**

| counter | count | what it is |
|---|---:|---|
| readbacks | 11 | row A5 |
| CPU swscale | 144 | the watermark, row A6 |
| CPU → GPU uploads in `Clip::draw_to_canvas` | 1,511 | two per frame, row A1 |
| encoder-side `sws_scale` | 0 | every frame reached NVENC on the device |

(a) = deliberate and already recorded; (b) = CPU for a reason in the code but not in this doc;
(c) = CPU for no recorded reason, and probably portable. Ranked by production impact, which is how
often the service hits the feature times its cost:

| # | feature | service → libopenshot | where it goes CPU | class | measured | cost (+ms per frame, 1080p / 720p) |
|---|---|---|---|---|---|---|
| A1 | **still-image sources: the background, image clips, shapes, the watermark's held frame** | background PNG on **every export** (`VideoRenderingImpl.cpp:412`), `QtImageReader` | `Clip::draw_to_canvas` uploads every host-memory source **on every frame**, with no texture cache (`Clip.cpp:1782-1796`) | (c) | 60/60 frames in every bench scenario; 1,511 of 750 frames in the payload | **+1.65 / +0.28**, every frame of every export |
| A2 | **SplitShift at rest** (SPLIT_HORIZONTAL/VERTICAL) | `Transition.cpp:365` | no early-out at shift 0. The planner says identity, `BindPlan` declines it, and `GetImageCV()` reads the frame back and converts it for a no-op (`SplitShift.cpp:64-71`, `EffectPlan.cpp:203`). The in-clip ends at 0 and the out-clip starts at 0, so this hits every frame of both clips outside the window | (c) | split variant: **482 readbacks + 482 fallbacks + 471 extra uploads in 750 frames** | **+6.33 / +2.88** on ~⅔ of the export |
| A3 | **rotational blur above 1280 px wide** (ROTATE_LEFT/RIGHT) | `Blur.cpp:150` | the planner sends it to the CPU whenever `referenceWorkingScale(width) != 1`, i.e. at 1080p and 4K (`EffectPlan.cpp:372`) | (b) | rotate@1080 variant: 25 readbacks in `Blur::GetFrame` | **+52.4** (on the GPU at 720p: +1.15) over the window |
| A4 | **diagonal blur above 1 MP** (PAN_DIAGONAL) | `Blur.cpp:141` | the C++ blurs a half-size copy, so the planner puts it on the CPU (`EffectPlan.cpp:343`). "Known issues" records the halving, but not that it is CPU | (b) | 2 readbacks per frame in the A/B | **+14.7** (on the GPU at 720p: +0.05) over the window |
| A5 | **Alpha at 0 / CircleMask radius 0** (the ALPHA out-effect of almost every preset; CIRCLE_MASK) | `Transition.cpp:186, 344` | the plan is a *clear*, `BindPlan` declines, and the C++ memsets after a readback (`EffectPlan.cpp:91, 263`; `Alpha.cpp:115-119`) | (c) | payload: **all 11 readbacks**; 11 frames per transition | +3.17 / +0.73 |
| A6 | **CircleMask while animating** | `Transition.cpp:344` | its coverage is OpenCV's antialiased circle, drawn on the CPU and uploaded whenever the radius changes, i.e. every frame of the window (`CircleMask.cpp:111-143`) | (b) | no readback; A/B only | +7.38 / +0.47 (static radius: +0.37 / 0) |
| A7 | **the ProRes 4444 watermark** (`yuva444p12le`, 400x300; on every export that has `watermarkUrl`) | `ExportData.cpp:133` → `FFmpegReader` | NVDEC only takes H.264/MPEG-2/VC-1/WMV (`FFmpegReader.cpp:367`), and `GpuYuv` only 4:2:0/NV12 (`GpuYuv.cpp:230`), so it goes through swscale. It decodes synchronously, because `GPU_DECODE` turns read-ahead off for **every** reader, host-frame ones included (`FFmpegReader.cpp:1404`). After 4.8 s the held frame is re-uploaded per A1 | (c) | 144 `sws_scale` calls = its 144 frames | +0.35 / ~0 |
| A8 | **video mask matte** | `VideoRenderingImpl.cpp:300` | the matte frame (GPU-decoded) is read back, rescaled with Qt `SmoothTransformation` and re-uploaded on every frame (`Mask.cpp:116`) | (c) | golden `mask_video_clip_synced_2x`: 1 readback + 1 upload per frame | +6.26 / +3.78 (no mask in any sample payload) |
| A9 | **resting text frames** | `TextClipReader` | the resting frame is rendered on the GPU and read back once, then copied (`TextClipReader.cpp:767`) and uploaded on every frame | (c) | `text_static_4`: 4 uploads per frame | **~0**: the frame is the text's bounding box, not the canvas (4 captions at 1080p: +0.1) |
| A10 | **codecs NVDEC is not given** (HEVC, VP9, AV1, ProRes, MJPEG) and 4:2:2/4:4:4/10-bit | `FFmpegReader` | software decode (whitelist above); `GpuYuv` still converts 8-bit 4:2:0 on the GPU after a host upload, and anything else goes through swscale | (b)/(c) | not measured: the corpus has only H.264 | not measured |

Portable, with pixels unchanged (the uploaded or cleared bytes stay the same):

- **A1**: a per-thread texture cache for host sources in `draw_to_canvas`, keyed on the QImage's
  `cacheKey()` and `GpuDevice::Generation()`. `QtImageReader` already returns the same cached
  image every frame.
- **A2**: treat an identity plan as "done" in the GPU path (the frame untouched) instead of
  declining. That leaves the CPU branch as it is. An early return in `SplitShift::GetFrame`, as
  the other transition effects have, would also speed up the CPU path, but it is bit-exact only if
  `GetImageCV`→`SetImageCV` is a lossless round trip. The golden suite answers that.
- **A5**: run the clear on the GPU, i.e. clear the surface to transparent (all zeros, which is
  what the memset writes).
- **A7**: allow read-ahead for readers whose frames stay in host memory, and add counters for
  uploads.
- **A9**: keep the resting image as a texture.

**Owner's decision — porting would change pixels:**

- **A3, A4**: a down/blur/up chain of GPU passes; the resize would be Skia's, not `cv::resize`'s.
- **A6**: the coverage as an SkSL distance function instead of OpenCV's antialiased circle.
- **A8**: the matte rescaled on the GPU instead of by Qt's `SmoothTransformation`.
- **A7/A10**: 4:4:4 and alpha conversion in `GpuYuv`; HEVC through NVDEC. The latter is probably
  bit-exact, as `unit.nvdec_on_device` shows for H.264, but needs the same gate.

**Other observations:**

- **Heap corruption.** Under gdb, `podcast_pip`, `everything` and `transitions_chain` (bench,
  every flag on) aborted with glibc heap-corruption errors (`corrupted size vs. prev_size`,
  `free(): invalid size`). Without gdb they run cleanly. A timing-sensitive memory bug is a lead
  to chase before the soak (3, 11).
- **`render-payload` stops logging mid-render.** It renders all 750 frames and exits 0, but prints
  neither `ExportTelemetry` nor its summary. This is probably the garbled topic string under "Known
  issues" corrupting `std::cout`. Every run of the variants did this, so their export times were
  not usable and the costs above come from the A/B program.

### 6. The frame never leaves the GPU (2026-09-24)

**Done.** With a GPU the service's frame goes decode → every effect, transition, overlay, animation,
text and subtitle → encode on the device. What moved, beyond 5:

| item | before | now |
|---|---|---|
| webm (VP8, VP9), HEVC, AV1 decode | software + host upload | NVDEC, kept on the device, bit-exact vs software (`unit.nvdec_on_device`); AV1 takes the native decoder (libdav1d has no hwaccel) |
| overlay transitions (LIGHT_FOOTAGE, GLITCH) | QPainter compositing, overlay read back, mismatched overlay → CPU | GPU: overlay clip drawn on the GPU, host clip GPU-composited, `overlayPasses` resize the overlay |
| zoom-out (ZOOM_IN / ZOOM_OUT) | readback + OpenCV, +11 ms at 1080p | resample_linear + zoom_out_pad, +0.2 ms, ≤ 1 LSB, sizes identical |
| box blurs > 255 taps (BLUR_VERTICAL) | readback + OpenCV, +26 ms | up to 1023 taps on the GPU, wide ones two taps a fetch (`blur_pairs`): +16.7 ms, byte-identical; radius 230 +28 → +10.4 |
| CameraMovement | QPainter, readback | Skia draw on the GPU (owner: 47 dB at worst) |
| Crop on a host frame (image, shape) | QPainter | uploaded once, cropped on the GPU |
| diagonal blur, odd sizes > 1 MP; rotation at ±360 | CPU | GPU (odd sizes ≤ 1 LSB; the full turn is an identity) |
| fade on a host source | CPU loop (and a re-upload every frame) | exact GPU pre-pass |
| background colour on a GPU canvas | a CPU fill discarded every frame | not done |
| a missing frame (VFR webm) | readback + copy of the previous one | shares its surface |
| SVG shapes / large stills (`QtImageReader`) | re-rasterised / rescaled every frame (a cache bug; CPU path too) | once |
| a still under a GPU effect | uploaded every frame | once |

**Measured through the service** (`render-payload`, the corpus payload with its transition swapped
for each production preset, 1080p, the service's defaults, gdb-counted; `doc/PERFORMANCE-BASELINE.md`):
all 15 variants — FADE, DISSOLVE_BLUR, LIGHT_LEAK, LIGHT_FOOTAGE, BLUR_VERTICAL, ROTATE_LEFT,
SPLIT_HORIZONTAL, ZOOM_IN, ZOOM_OUT, WHOOSH (+ a CAMERA_MOVEMENT), GLITCH, CONTRAST, CIRCLE_MASK,
BARN_DOORS, PAN_DIAGONAL — 750 frames with **0 readbacks** (1 in the three with a wide blur: the
once-per-process filter probe), **0 QPainter draws, 0 CPU fallbacks**; the only CPU video work is the
ProRes watermark's 144 frames (decoded and uploaded; NVDEC has no ProRes) and one upload each for
the background and the LUT/tone tables. The gate for all of it in the suite is `unit.gpu_resident`.

**What is left on the CPU, and why:**

- **The ProRes 4444 watermark** — no GPU decodes ProRes. 144 small frames an export, read ahead on
  their own thread. Re-encoding the asset would not help: NVDEC has no alpha-capable codec either.
- **10-bit video** (VP9 profile 2, HEVC Main 10): NVDEC decodes it, as P010, which `CudaInterop` /
  `GpuYuv` do not take (R8/RG8 only), so it is downloaded and converted by swscale. The service's
  inputs are 8-bit (H.264, browser webm); porting means R16/RG16 interop images and a 10-bit
  `GpuYuv` conversion, and changes pixels like `GPU_DECODE` does.
- **Alpha VP9** (libvpx, no hwaccel; NVDEC has no alpha plane) and **4:4:4 / 4:2:2** streams:
  software decode, then `GpuYuv` for 4:2:0 or swscale otherwise.
- **Still images and SVG shapes**: decoded / rasterised once on the CPU, uploaded once.
- **Audio** (see "What runs where"), and the service's ffmpeg audio mux.
- Past loop bounds no production preset reaches: a box blur over 1023 taps, a diagonal kernel over
  513, a rotational blur over 30 iterations.

**Owed:** the service's per-export telemetry line is not printed by `render-payload` (see "Known
issues"), so the numbers above are gdb counts, not `ExportTelemetry`; and the editor needs a WASM
built from the submodule's current `feature/gpu-rendering` for the new fragments (`zoom_out_pad`,
`blur_pairs` with `linearSource`, `resample_*`) and `overlayPasses`.

