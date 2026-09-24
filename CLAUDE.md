# libopenshot (podcastle fork) — working notes for Claude

This is podcastle-studio's fork of libopenshot: a headless, JSON-driven video renderer used only by
`../video-rendering-service`. It is **not** used with any UI. The fork adds a Skia text engine
(`src/text`), subtitles (`src/subtitle`), W3C blend modes, clip shadow/blur/flip, overlay clips, and
the transition effects in `src/effects/image-processing-lib` (shared with the web front end via WASM).

## Resume in one command

Run `/resume`. It reads the state, builds, runs the regression suite and tells you what is next.
Other project commands: `/check` (golden suite + visual diff), `/bench` (performance vs baseline),
`/wrap-up` (close the session so the next one can continue).

## Read first

1. `doc/GPU-RENDERING.md` — **the GPU work in one place**: what runs where, every switch and
   whether it changes pixels, how to validate, the decisions that constrain the code (do not
   re-litigate them), known issues, and — at the end — **what is left**. Update its "Status and
   what is left" section at the end of any session that changed code, plans or decisions.
2. `tests/golden/README.md` — the regression suite that gates every change.
3. `doc/PERFORMANCE-BASELINE.md` — benchmark history (`tests/bench`, `openshot-bench`). Re-run at the
   end of every optimisation step, commit the JSON under `tests/bench/results/`, append the table.
4. `src/effects/image-processing-lib/shaders/README.md` — the shader and planner contract; and
   `src/effects/image-processing-lib/doc/FRONTEND-INTEGRATION.md` — how the editor uses it.

The migration's working folder (`doc/gpu-migration/`: status log, W01–W31 worklist, plan, decisions
log, spikes) was retired on 2026-09-24; `git show 628a53d5:doc/gpu-migration/<file>` recovers any of
it when a commit message's W-number needs its context.

## Repo map

| path | what |
|---|---|
| `src/` | the library. Upstream OpenShot plus this fork's additions |
| `src/gpu/` | **fork**: Vulkan device + Skia Graphite context, surface pool, GPU frame, CUDA interop (off unless `OPENSHOT_GPU` is set) |
| `src/text/` | **fork**: Skia text engine (layout, animation, glow, 3D tilt, curved text) |
| `src/subtitle/` | **fork**: Skia subtitle renderer driven by JSON |
| `src/effects/` | effect classes; the 20 the service constructs are named in `doc/GPU-RENDERING.md` |
| `src/effects/image-processing-lib/` | **submodule**, shared with the web front end through WASM: the transition effects (C++), their SkSL, and the planner |
| `src/shaders/` | **fork**: SkSL of the per-clip effects only the export runs on the GPU (embedded with the submodule's) |
| `src/BlendModes.cpp`, `Clip.cpp` | **fork**: W3C blend modes, clip shadow/blur/flip, overlay clips |
| `src/FFmpegReader/Writer.cpp` | demux, decode, scale, encode, mux |
| `tests/golden/` | the regression suite and its committed reference frames |
| `tests/bench/` | the performance benchmark and its recorded results |
| `tools/golden.sh` | build + run + report wrapper |
| `doc/` | `GPU-RENDERING.md`, the benchmark history, upstream's install and hardware-acceleration guides |
| `skia_build_script.sh` | out-of-tree Skia build (see below) |
| `../video-rendering-service` | the only consumer: JSON payload in, MP4 out |

## How to communicate

- Keep replies short. Lead with the result, a few bullets at most, tables only when numbers matter.
  Details belong in the docs (`doc/GPU-RENDERING.md`, `doc/*.md`), not in chat.

## Non-negotiable workflow

- Run `tools/golden.sh check` before and after any change to the render path (Clip, Timeline, Frame,
  FFmpegReader/Writer, effects, text, subtitle). It must be green before a commit.
- When a rendering change is intentional, look at every failing triptych in the report, then
  `tools/golden.sh update <filter>` for exactly those scenarios and commit the PNGs with the code.
  Never blanket-update goldens.
- Keep `tests/golden/Recipes.*` in sync with how `../video-rendering-service` constructs clips
  (`VideoRenderingImpl.cpp`, `KeyframeApplier.cpp`, `Transition.cpp`, `Animation.cpp`, `Subtitles.cpp`).
  The suite is only meaningful if it drives the library the way production does.
- Work in small steps, each with its validation from the plan, on branch `feature/gpu-rendering`
  (based on the fork's `develop`, not upstream).

## Build

```bash
cmake -S . -B cmake-build-release -DCMAKE_BUILD_TYPE=Release   # Skia in /usr/local/include/skia, FFmpeg in /usr/local
ninja -C cmake-build-release openshot openshot-golden openshot-bench
tools/golden.sh check
tests/bench/media/generate.sh && cmake-build-release/tests/bench/openshot-bench --quick   # 1080p perf smoke
```
Submodules `src/effects/image-processing-lib` and `external/godot-cpp` must be initialised
(`git submodule update --init`). `ENABLE_TESTS` (Catch2) is off and Catch2 is not installed;
the golden suite is the test suite — including for things that render nothing, which go in
`tests/golden/scenarios/Unit.cpp` as `unit.*` scenarios with checks and no captured frames.

`ENABLE_PLAYER` (default ON) builds the Qt video player. **OFF is the headless configuration**: it
drops `QtPlayer`, `src/Qt/*`, `Frame::Display`/`DisplayWaveform` and the Qt **Widgets** component,
and the suite must stay green in that build too. Two things to know before touching it —
`Qt/VideoCacheThread.cpp` is filed under `Qt/` but is Widgets-free and `AudioReaderSource` links
against it, so it is always built; and `libQt5Svg` pulls Widgets back in transitively, so the option
removes *our* dependency on Widgets, not the library from the image. `USE_QT_PLAYER` is the
corresponding header guard, following `USE_IMAGEMAGICK`.

### Skia

Skia is built out of tree by a script at the repository root and installed as a static library.

| script | backend | output | installs to |
|---|---|---|---|
| `skia_build_script.sh` | CPU raster only (all GPU backends off) | `~/skia-stable/out/Release-CPU/libskia.a` | `/usr/local` via the `install_skia.sh` it emits |
| `skia_build_script_gpu.sh` | Graphite + Vulkan | `~/skia-stable/out/Release-GPU/libskia.a` | `/usr/local/skia-gpu` via `install_skia_gpu.sh` |

Rules: **never edit `skia_build_script.sh` to add GPU support** — the CPU build must stay
reproducible as the no-GPU fallback. The two scripts share everything except the output directory,
the GPU GN args and the install prefix; keep the rest byte-identical so text rendering does not
drift. Both pin `SKIA_MILESTONE=m147` to match the front end's CanvasKit. Select a build at
configure time with `-DSkia_ROOT=/usr/local/skia-gpu` (or leave it unset for the CPU one) and say
which one a measurement used wherever it is recorded.

The GPU script differs from the CPU one in four ways only, and nothing else may diverge: output
directory, the GPU GN args, the install prefix, and that it **reuses** `~/skia-stable/skia` instead
of wiping it — both builds share one checkout (`out/Release-CPU` and `out/Release-GPU`), so a wipe
would destroy the CPU fallback. `SKIA_FORCE_CLONE=1` opts back into the wipe;
`SKIA_ENABLE_GANESH=true` adds the Ganesh Vulkan backend beside Graphite.

`install_skia_gpu.sh` installs three things beyond the CPU installer's library + `include/`, all of
them load-bearing:

- `modules/skcms` — `SkRuntimeEffect` (the text glow) includes `modules/skcms/skcms.h`, which in
  turn includes `src/skcms_public.h` relative to itself, so the whole module directory has to land
  under the same include root. Without it `FindSkia.cmake` hunts for a Skia source tree.
- `src/gpu/GpuTypesPriv.h` and `src/gpu/vk/vulkanmemoryallocator/VulkanMemoryAllocatorPriv.h` —
  Graphite makes the caller supply a `VulkanMemoryAllocator` and Skia's VMA-backed one is reachable
  only through `skgpu::VulkanMemoryAllocators::Make`, declared in a private header. Both headers are
  self-contained and are installed at their source-tree paths so their relative includes resolve.
- `include/skia-vulkan/` — Skia m147's `include/gpu/vk/VulkanPreferredFeatures.h` uses Vulkan **1.4**
  types; Ubuntu 24.04's `libvulkan-dev` is 1.3.275, so the system headers do not compile against it.
  Consumers must put this directory **ahead of** `/usr/include` (see `tests/gpu/CMakeLists.txt`,
  which links the loader by path rather than through `Vulkan::Vulkan` for exactly this reason).

`cmake/Modules/FindSkia.cmake` prefers pkg-config, which knows nothing about `Skia_ROOT`; when a
root is given it is now ignored, or `-DSkia_ROOT` would pick the GPU library and the CPU headers.

Check a GPU prefix with the smoke test (Vulkan device → Graphite `Context` → 64x64 gradient →
readback → PNG, and it checks the channel order):

```bash
cmake -S tests/gpu -B cmake-build-gpu-smoke -DSkia_ROOT=/usr/local/skia-gpu
cmake --build cmake-build-gpu-smoke && cmake-build-gpu-smoke/openshot-gpu-smoke out.png
VK_DRIVER_FILES=/usr/share/vulkan/icd.d/lvp_icd.json cmake-build-gpu-smoke/openshot-gpu-smoke sw.png  # no-GPU path
```
Inside the main tree the same target is `-DENABLE_GPU_SMOKE=ON` (off by default).

### GPU rendering (`src/gpu`)

**The CPU path ships; the GPU path is a configurable addition.** Production runs CPU-only today
and a no-GPU machine is a supported configuration, so no change may make the CPU path slower, worse
looking, or dependent on a GPU. Never delete CPU code because the GPU makes it unnecessary — gate
it on `GpuOffscreen::onGpu()` / `GpuDevice::available()` and keep the CPU branch. Every change is
accepted on the four-way golden sweep below (CPU Skia; GPU Skia with the GPU off; GPU Skia on
Vulkan; GPU Skia on lavapipe), all four at 295/295.

**One control.** `GpuDevice::SetBackend(Backend::Off|Vulkan|Lavapipe)` is the single switch and
overrides `OPENSHOT_GPU` at runtime (it tears the device down, moving `Generation()`, so caches
drop). It only stays a single switch because every GPU path asks `GpuDevice::Instance().available()`
— or `GpuOffscreen::Match` / `GpuFrame::Create`, which ask for you — and none reads the environment
itself. Add new GPU logic the same way; the `control` check in `openshot-gpu-checks` enforces it.

Off unless `OPENSHOT_GPU` says otherwise — `off` (default), `vulkan`, or `lavapipe` (Mesa's software
rasteriser, for machines with no GPU and for checking a result is not vendor-specific). Everything
goes through `GpuDevice::Instance().available()`, and `false` is a normal answer: fall back to
raster, never treat it as an error.

```bash
cmake -S . -B cmake-build-gpu -DCMAKE_BUILD_TYPE=Release -DSkia_ROOT=/usr/local/skia-gpu
cmake --build cmake-build-gpu --target openshot openshot-gpu-checks
OPENSHOT_GPU=vulkan cmake-build-gpu/tests/gpu/openshot-gpu-checks     # and =lavapipe
BUILD_DIR=$PWD/cmake-build-gpu tools/golden.sh check                  # must stay 295/295
```

Effect shaders (`src/GpuEffect.h`) are gated by `openshot-gpu-effect-parity`: PSNR and max LSB
against each effect's C++ twin over eight images built around the alpha edge cases, plus the 1080p
cost per pass. It refuses to compare a case that did not actually reach the GPU — `ApplyOnGpu`
declining looks like a perfect match otherwise, and once did. `--sksl` compiles a fragment from
stdin, which is the quick way to find out what SkSL supports: it is the **GLSL ES 1.00** intrinsic
set, so there is no `round` and no `trunc`, only `floor`. That is CanvasKit's ceiling too, so the
prelude's helpers are written within it on purpose.

**Effect parameters are resolved by the shared planner**, not in the effect classes:
`image-processing-lib/src/Planner/EffectPlan` (`planEffect(name, params, w, h[, ow, oh])` →
identity / clear / gpu passes / cpu call), which the editor calls through the WASM (`planEffect`,
`planTexture`) so both sides bind the same uniforms. An effect's `SetGpuUniforms` is
`BindPlan(builder, planEffect(...))`; a multi-pass effect runs `RunPlannedStep`, which attaches
nothing unless every pass ran. Change the arithmetic in the planner, never in a host — the
contract is `shaders/README.md`, the gate is `unit.effect_plan` plus the parity tool.

Rules that are easy to get wrong and crash in the NVIDIA driver rather than anywhere useful:

- **Nothing Graphite hands out may outlive the `Context`.** `GpuDevice::DestroyInstance()` empties
  every pool and destroys every `Recorder` before the context, in that order. Do not put a
  `Recorder`, `SkSurface` or `SkImage` in a `thread_local` or a static — the device owns them.
- Cache a GPU object across calls only alongside `GpuDevice::Generation()`, and drop the cache when
  it changes.
- `GpuSurfacePool` is **per thread**, because a Graphite surface belongs to the recorder that made
  it. Never move a surface between threads.
- A recycled surface hands back the **previous user's canvas transform, clip and save stack** —
  clearing the pixels does not touch them. `acquire()` resets it (`pool-canvas` in
  `openshot-gpu-checks` guards this), so code written against `SkSurfaces::Raster`, which is fresh
  every time, keeps working. Do not reintroduce a path that skips the reset.
- Graphite has **no synchronous `SkSurface::readPixels`** — it returns false immediately, so a
  caller silently falls back and merely looks slow. `GpuFrame::readback` is the only route.
- **Graphite never uploads a raster image for you.** A raster `SkImage` used as a shader — a
  runtime-effect child included — is dropped with `Couldn't convert SkImage to a Graphite-backed
  representation` and the draw silently disappears; Ganesh did upload automatically. Put every image
  crossing onto a GPU surface through `GpuFrame::ToTexture` first.
- Pooled surfaces default to a **null** colour space, matching the `SkImageInfo::MakeN32Premul`
  raster surfaces they replace. Attaching sRGB makes every blend gamma-correct and changes output.
- **`CudaInterop` (W22) allocates its own images and nothing else may.** Graphite cannot export its
  own allocations, so the images CUDA writes are ours (`vkAllocateMemory`, dedicated, exportable);
  `GpuSurfacePool` stays VMA-backed and is never one of them. CUDA needs them in
  `VK_IMAGE_LAYOUT_GENERAL` and Skia leaves them in `SHADER_READ_ONLY_OPTIMAL`, so two things hold:
  `GpuImage::image()` hands back a **fresh** wrapper every call (one kept across frames would
  barrier from a layout the image has left), and the layout barrier goes through
  `prepareForCopy(y, uv)` **right after the submit of the draw that read them** — inside
  `copyNV12` it is a cross-API round trip on the critical path and costs 0.25 ms a frame for
  nothing. The driver is dlopen'd: only `cuda.h` is a build dependency, `available()` is false
  without it, and lavapipe declines (no `external_semaphore_fd`). Gate:
  `openshot-gpu-cuda-interop`.
- **The interop's two binary semaphores are per image pair** (they live on the luma image;
  `waitSemaphore(y)`), never process-wide. One pair's cycle is self-ordered; two pairs sharing
  one semaphore re-signal it before it is waited on, and the CUDA wait that loses its signal
  stalls the stream NVDEC decodes on — every transition hung that way (W23). A readback between
  frames hides it; `pairs` in `openshot-gpu-cuda-interop` does not. Callers serialise
  copy → submit → `prepareForCopy` themselves (`FFmpegReader::ConvertOnDevice` holds one lock).
- `GpuFrame` is `kRGBA_8888` and raster N32 is BGRA on x86, but do **not** "fix" the R/B swap in
  `SkiaRenderer::parseColorString` for the GPU path. It is a logical `SkColor` convention, not a
  byte order; Skia converts correctly in both directions on readback, so it survives the round trip.

Subtitles build **no offscreen of their own**, so the whole pass follows the canvas it is given:
`SubtitleManager::renderAtFrame(SkCanvas*, w, h, frame)` is the real entry point and the `QImage`
overload just wraps a raster canvas around the caller's pixels. Do **not** route the Timeline's call
through a GPU surface — subtitles composite onto an existing frame, so that costs an upload plus a
readback (5.6 ms at 1080p, 18.7 ms at 2160p) to save 0.27 ms / 0.61 ms of drawing. It pays only once
the frame is already on the GPU, and then the caller just passes its canvas.

The whole text engine renders on the GPU when one is available. `TextClipReader::renderToQImage`
picks GPU or raster once per frame and reads back once at the end; every offscreen below it goes
through `GpuOffscreen::Match(destination, w, h)`, which puts it in the same memory as the canvas it
will be drawn onto. At 1080p on an RTX A2000, `text_animated_glow_3` goes 4.3 → **52 fps** with the
GPU on and `everything` 5.4 → 8.8 (2026-09-15, `doc/PERFORMANCE-BASELINE.md`). Static text is ~5 %
*slower* on the GPU — it comes from the resting-frame cache, so there is nothing per frame for the
GPU to take over. Earlier, lower figures in the docs were measured on an Intel iGPU.

## Facts that are easy to get wrong

- The service never touches `openshot::Settings`; `HARDWARE_DECODER` is 0. The codec is **no longer
  hard-coded**: since plan step 2.0 (2026-09-15) `ExportSettings::vCodec` comes from
  `RenderBackend::videoCodec()`, which reads `ENCODER` (`libx264` default, or `h264_nvenc`), probes
  it once and falls back to libx264 if no device answers. `OPENSHOT_GPU` is likewise passed through
  to the library. Both default to the CPU, and the runtime image is GPU-*capable*, not GPU-requiring.
- **The reader can convert YUV→RGBA on the GPU** (`src/gpu/GpuYuv`, `Settings::GPU_DECODE`,
  2026-09-22): the decoded frame is born on a GPU surface and stays there for the compositor.
  **Off by default because it changes pixels** — rounding, plus it honours the stream's declared
  colour space where swscale here never did, so a `bt709`-tagged file decodes differently; the
  default is the owner's decision. With `HARDWARE_DECODER=2` as well, NVDEC's frames never leave
  the device (W23): `source_4k` 78 → ~130 fps at 0.6 cores. Gates: `unit.gpu_decode`,
  `unit.nvdec_on_device` (bit-exact vs software decode), `unit.bt709_chart`. The harness runs
  the paths with `OPENSHOT_GOLDEN_GPU_DECODE=1` / `OPENSHOT_GOLDEN_HW_DECODE=1`, the bench with
  `OPENSHOT_BENCH_GPU_DECODE=1` / `OPENSHOT_BENCH_HW_DECODE=2`. Neutral chroma is **128/255, not
  0.5** — taking 0.5 was a ~1-code bias on R and B that the chart found.
- **Every `FFmpegReader` decodes ahead on a thread of its own** (W24, `READ_AHEAD_FRAMES`,
  default 2, 0 = off), into `final_cache`, through the same `getFrameMutex` path as the caller.
  **Host-memory frames only** — a GPU frame is bound to its thread's recorder, so with
  `GPU_DECODE` on and a GPU present it is off. `Close()` stops the worker *before* taking the
  mutex, and the worker only ever `try_lock`s it: the hardware-decode fallback calls `Close()`
  with the mutex held. The bench switch is `OPENSHOT_BENCH_READ_AHEAD`.
- **NVENC can take the frame from the GPU** (W25, `Settings::GPU_ENCODE`, **off**: box vs
  bicubic chroma). The conversion runs in `Timeline::GetFrame` through `SetGpuEncodeHook`,
  **on the compositing thread**, because the service's writer encodes on another thread and a
  Graphite surface cannot follow it; the CUDA frame can. A frame carrying an encoder payload has
  no picture for anyone else and is never cached. Copies run on the interop's *encode* stream and
  NVENC's stream waits on a per-frame event — never queue them on the stream NVENC reads.
  Graphite render targets need `VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT`. Bench:
  `OPENSHOT_BENCH_GPU_ENCODE=1`, `OPENSHOT_BENCH_PIPELINE=1` (the service's mode),
  `OPENSHOT_BENCH_NVENC_PRESET`; read the `LOOP` line, since `fps=` includes NVENC init.
- **`Crop` on a GPU-backed frame reads it back** unless `Settings::GPU_CROP` (W29, off: the
  rounded corners antialias differently from QPainter). That one readback was half of
  `everything`'s frame and kept the GPU ~35 % busy; with it on, ~135 fps at ~80 %. The service
  puts a `Crop` on every clip with a crop or rounded corners. Bench `OPENSHOT_BENCH_GPU_CROP=1`,
  `OPENSHOT_BENCH_REPEAT=N` for long runs; harness `OPENSHOT_GOLDEN_GPU_CROP=1`.
- **Exports are BT.709 and the reader honours declared matrices** (owner, 2026-09-23). The writer
  encodes with the matrix the output is tagged with (codec `colorspace`, or `colormatrix=` in
  `x264-params`), on swscale and `GPU_ENCODE` alike; the reader's CPU path passes the stream's
  matrix/range to swscale. Untagged stays BT.601 on both sides. Never fix one side alone: until
  this, BT.601-in/BT.601-out under a BT.709 tag cancelled for video and shifted everything drawn.
  Gate: `export.bt709_bars`. NVENC preset is **p4** (same VMAF as p5, twice the speed).
- **Per-export telemetry** (W31, `src/gpu/GpuTelemetry`): `GpuCounters` are bumped on the render
  path (add one when you add a GPU path or a fallback), `ExportTelemetry` samples NVML (dlopen'd,
  `_v2` memory info — v1 counts the driver's reservation) and the service logs one line per export.
  Utilisation is device-wide. NVML needs the CUDA headers at build time (`NvmlBuiltIn()`).
- **`FrameMapper` must not `GetImage()` a GPU-backed frame.** It did, for every frame it rebuilt
  (any clip whose audio mapping differs, i.e. most video), which read every GPU-decoded frame back
  and made GPU decode look worthless. It shares the surface now, as `Frame`'s copy constructor
  does. Anything new that copies frames must do the same.
- Hardware decode (`HARDWARE_DECODER != 0`) **worked again as of 2026-09-22** (plan step 1.5):
  `ProcessVideoPacket` takes swscale's source format from the frame it converts, not from
  `pCodecCtx->pix_fmt`, which is `AV_PIX_FMT_CUDA` once NVDEC is on. `~FFmpegReader` also no
  longer lets `Close()` throw out of a destructor — `Close()` drains the decoder through that same
  call, so any decode failure used to `terminate()` the process. `unit.hardware_decode` guards
  both. With the download + swscale it is still *slower* than software decode (58 against 78 fps
  at 4K → 1080p); only with `GPU_DECODE` on too does it pay. `HARDWARE_DECODER` stays 0 by
  default, and `DE_LIMIT_*` (a 1950x1100 cap that silently sent 4K to software) is gone: a
  stream NVDEC refuses fails before its first frame and the reader reopens it in software.
- Three time→frame conventions and two bezier-handle conventions coexist in the service; see
  `tests/golden/Recipes.h`.
- `Scene` in the golden harness must delete readers in reverse creation order (a FrameMapper before
  the reader it wraps) or teardown segfaults.
- `BorderReflectedMove` dx/dy are fractions of width/height; `Zoom` 100 = no zoom; `Exposure`
  clamps to ≥ 1.0; `CameraMovement` zoom is a percentage.
- Animation preset `tx`/`ty`/`tz` tracks are in **fontSize units**, passed through unscaled by the
  service. Production values are fractions (`ty` ∈ [−0.27, 0.5], `tx` ∈ [−2, 0]); a value of 40 is
  40 font sizes, which silently sizes the text frame buffer in the hundreds of MB.
- **Benchmarking this laptop: never measure straight after a build, and interleave the arms.** A
  compile loads every core and the machine stays drifting for minutes afterwards — a sequential A/B
  once read +55 % where the truth was 0 %, and re-running the *unchanged* arm gave 3x its own
  earlier figure. Build both artefacts up front and swap them in place if that is what interleaving
  takes. Check AC power too: on battery this machine runs at ~1/3 speed.
- The glow is ~99 % of an animated glow frame on the raster path, and the ray-march is ~91 % of
  that (sweep `OPENSHOT_GLOW_STEPS` to measure — it changes only the step count). Optimise the
  march or skip it; nothing else in the text engine is worth measuring against it.
- A **block-mode** animation concats its transform onto the canvas, so the glow is marched in
  block-local space and is frame-invariant. `text::GlowFrameCache` on `TextClipReader` reuses the
  composited image — bit-identical, raster only (a pooled GPU snapshot must not outlive its frame).
- The glow's beam reach is **per axis** — the ray-march is a homothety about the light source, so
  the x reach depends only on the content width and the y reach only on the height. Do not go back
  to padding both from `max(w, h)`.
- `../text-metrics/vendor/text/` vendors this engine; `TextGlowRenderer.{h,cpp}` there is now behind
  `src/text/`.
- Qt PNG "quality" 100 means no compression; the harness saves with 10.
