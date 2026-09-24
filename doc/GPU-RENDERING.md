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

**The CPU path ships; the GPU path is an opt-in addition** (project owner, 2026-09-14). Production
runs CPU-only today and a machine with no GPU is a supported configuration, so:

- no change may make the CPU path slower, worse-looking, or dependent on a GPU;
- CPU code is never deleted because the GPU does not need it — it is gated on
  `GpuDevice::Instance().available()` / `GpuOffscreen::onGpu()` and kept;
- every change is accepted on the **four-way golden sweep** (below), all four arms green.

## What runs where

Everything below is off unless switched on, and every GPU path falls back to the CPU by itself when
its requirement is missing. `false` from `available()` is a normal answer, not an error.

| stage | on the GPU | CPU fallback / notes |
|---|---|---|
| decode | NVDEC, frames stay on the device through `CudaInterop` into `GpuYuv` (YUV→RGBA on the GPU) | software decode + swscale; a stream NVDEC refuses (10-bit, > 4096 wide) reopens in software |
| read-ahead | one decode worker per `FFmpegReader` (`READ_AHEAD_FRAMES`, default 2) | host-memory frames only; off for GPU frames (bound to their thread's recorder) |
| compositing | `Timeline::GetFrame` on a pooled Graphite surface; `Clip::draw_to_canvas` in one transformed draw; all 16 blend modes; clip shadow, blur, flip on the paint | a frame composites on **one** path: any clip that reads the backdrop on the CPU (overlay clip, frame-number overlay, waveform, an effect after keyframes) puts the whole frame on QPainter |
| text, subtitles | the whole Skia text engine incl. the glow ray-march; subtitles draw on whatever canvas they are given | raster Skia |
| effects | `GpuEffect` + SkSL twins: 11 per-clip/per-pixel effects and all 10 transition variants, overlay composites (additive, displacement) | the C++ twin runs whenever a fragment declines |
| crop | `Crop` on GPU frames behind `GPU_CROP` | QPainter (reads the frame back) |
| encode | NVENC takes the composited frame from the GPU (`GPU_ENCODE`) | readback + swscale + libx264 / NVENC |
| telemetry | `GpuCounters` on every GPU path and fallback; `ExportTelemetry` (NVML) — the service logs one line per export | — |

**The effects in scope** are the 20 classes `../video-rendering-service` constructs — per clip
(`VideoRenderingImpl.cpp`): ChromaKey, ColorAdjustment, ColorMap, Crop, Enhancement,
LightAdjustment, Mask; transitions (`Transition.cpp`): Alpha, Bars, Blur, BorderReflectedMove,
BorderReflectedRotation, Brightness, CircleMask, ColorShift, Exposure, SplitShift, Wipe, Zoom;
animation (`Animation.cpp`): CameraMovement. The other effect classes are upstream OpenShot's and
untouched.

**Not on the GPU, deliberately:** `Crop` without `GPU_CROP` and `CameraMovement` (both QPainter
resampling — a rasteriser difference, not an arithmetic one, so porting them redefines pixels);
ChromaKey's non-YCbCr methods (babl colour science; the service only uses YCbCr); Enhancement's
grain pass (a `sin()` hash that differs by up to ~140 LSB between `double` and `float`); ColorMap's
colour-match mode (re-bakes the cube from the frame, i.e. a readback).

### The switches

| switch | set by | default | what it does | changes pixels? |
|---|---|---|---|---|
| `OPENSHOT_GPU` | env, or `GpuDevice::SetBackend()` (overrides) | `off` | `vulkan` / `lavapipe` enable every GPU path at all | no — the four-way sweep is bit-exact except text (PSNR ≥ 38) and three blend modes |
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

- **Overlay clips composite on the CPU** (`Clip::draw_to_canvas` is skipped for `isOverlay`): in
  `transitions_chain` only 45 of 180 frames take the GPU encode path, with 370 readbacks. The next
  lever for transition-heavy exports.
- **The production payload still does ~251 readbacks per 750 frames** with every flag on and the GPU
  ~10 % busy (W31 telemetry); which clips cause them is not measured (see "Worth doing").
- **Resampled alpha boundaries vary with what else ran in the process** (golden suite, not fixed):
  1,280 pixels on the interpolated alpha edges of a scaled PNG move between an isolated and a full
  run, and between two 4-thread runs. Nothing is red — the affected scenarios were rebuilt on 1:1
  clips — but the instability is real. Start with whether anything caches a scaled image per path
  rather than per reader.
- **Diagonal blur still halves above one megapixel** — the one downscale threshold the reference
  decision did not retire, so the same radius renders at full scale from 720p and at half from 1080p.
- **The editor leaves some LUTs ungraded** (1-D-only cubes, a 3-D cube with a 1-D shaper, any
  non-WebGL2 renderer) where the export grades them.
- **Film grain is chaotic in its input, so it never matches across paths.** Enhancement's grain
  (always on the CPU) seeds its hash from the pixel's own colour, so a 1 LSB difference upstream —
  which `GPU_DECODE` produces by design — gives the pixel entirely different grain. Same look, a
  different random phase: a grain clip measures ~23 dB CPU vs full-GPU where everything around it
  is ~42. Expected; exclude grain clips, or compare them by statistics, when checking parity.
- `effects.stack_crop_chroma_light_lut` shows harsh white blotches (ChromaKey + Light + LUT
  stacked); baselined as-is.
- **`render-payload` prints a garbled Pub/Sub topic name** in its progress messages once rendering
  starts (it reads like a string outliving its owner in the offline tool's publisher; not yet
  investigated). The video is unaffected.

## Status (2026-09-24) and what is left

**Done.** Every render stage the service uses can run on the GPU, under one control, with the CPU
path intact and bit-exact to what shipped: decode (NVDEC → GPU YUV), read-ahead, compositing, text
and subtitles, all effects and transitions the service constructs except the two QPainter ones,
crop (flagged), NVENC from the GPU (flagged), BT.709 end to end, and per-export telemetry. The
editor has a shared planner and a published WASM (`dist/image_processing_lib_v2.1.0`) to reach
parity. Headline numbers (RTX A2000, 1080p) are in `doc/PERFORMANCE-BASELINE.md`, "End of the
migration".

**What is left is decisions, infrastructure and final checks — no planned code.** In the order
they should happen:

### 1. Owner decisions

1. **Defaults for `GPU_DECODE`, `GPU_ENCODE`, `GPU_CROP`.** Each changes pixels (measured, gated,
   listed under "The switches"); each is off. Decide per flag whether a GPU node runs with it on.
   Without them a GPU node still gets GPU compositing, text, effects and NVENC, but pays a readback
   and upload per clip per frame.
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
    full-GPU ~42 dB on every frame except the grain clip (see "Known issues"), and only ~15 %
    faster, for reasons not yet measured (see "Worth doing"). Compressed output
    caps what this can see at the encoder's noise (~42 dB); a pre-encode frame dump from
    `render-payload` would make it exact.
11. **Soak and concurrency on the GPU node**: a long export and N concurrent exports with every flag
    on — VRAM and RSS flat (VRAM was flat over 10,080 frames on the laptop), no NVDEC/NVENC session
    exhaustion, telemetry line per export, clean fallback when `libcuda` or the ICD is absent.
12. **Editor parity**: the front end moves to `dist/v2.1.0` (their current v2.0.4, built from the
    submodule's `main`, lacks the 2026-09-22 blur and zoom-blur changes, so preview and export
    already disagree on blurs), then integrates the shaders per `FRONTEND-INTEGRATION.md` and runs
    its checks. The submodule's `main` should take `feature/gpu-rendering` when this ships, or the
    editor's line and the export's keep diverging.

### 4. Worth doing, not blocking

- **Find out why the production payload gains only ~15 %** — the most important open question for
  whether the GPU pays in production. With every GPU path on it renders in ~26 s against the CPU's
  ~31 s (25 s of 720p), the GPU ~10 % busy (`doc/PERFORMANCE-BASELINE.md`, 2026-09-24). Unmeasured
  candidates: its grain clip (Enhancement grain is CPU-only — a readback per frame), the WHOOSH
  transition's overlay clip (CPU compositing path), and fixed per-export costs. Profile it with
  `GpuCounters` per frame before building anything.
- Overlay clips on the GPU (`isOverlay` frames composite on QPainter; `transitions_chain`).
- Thread budgets from the cgroup (`FF_THREADS`/`OMP_THREADS` from `cpu.max` ÷ instances; W06) —
  needs a container to validate; check what the upstream merge's thread settings already do first.
- Audio is covered by one smoke scenario; upstream's new effects (AnalogTape, Glow, Shadow, …) are
  built but untested here, and unused by the service.
- From the upstream merge: port `apply_keyframes`' corner radius and painter opacity if wanted;
  collapse `BlendMode` and `CompositeType`; send the `lerp` rename upstream.
