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

> Everything under `doc/gpu-migration/` is scaffolding for the GPU migration and is deleted when
> that work lands. Permanent documentation stays directly in `doc/`.

1. `doc/gpu-migration/STATUS.md` — where the work is right now and what the next step is. **Read it
   before doing anything, and update it at the end of any session that changed code, plans or
   decisions.**
2. `doc/gpu-migration/GPU-RENDER-PLAN.md` — the multi-phase plan to move rendering fully onto the
   GPU and remove Qt. Sections 0–2 are background (measurements, GPU primer, Qt inventory);
   section 3 is the step list; section 4 sizes CPU/RAM/VRAM/NVENC for N parallel export processes.
3. `tests/golden/README.md` — the regression suite that gates every change.
4. `doc/PERFORMANCE-BASELINE.md` — benchmark history (`tests/bench`, `openshot-bench`). Re-run at the
   end of every optimisation phase, commit the JSON under `tests/bench/results/`, append the table.
5. `doc/gpu-migration/GPU-DECISIONS.md` — decisions already taken (do not re-litigate) and the ones
   still open.

## Repo map

| path | what |
|---|---|
| `src/` | the library. Upstream OpenShot plus this fork's additions |
| `src/gpu/` | **fork**: Vulkan device + Skia Graphite context, surface pool, GPU frame (off unless `OPENSHOT_GPU` is set) |
| `src/text/` | **fork**: Skia text engine (layout, animation, glow, 3D tilt, curved text) |
| `src/subtitle/` | **fork**: Skia subtitle renderer driven by JSON |
| `src/effects/` | effect classes; the ones the service uses are listed in plan section 2.4 |
| `src/effects/image-processing-lib/` | **submodule**, shared with the web front end through WASM: transition algorithms and colour grading |
| `src/BlendModes.cpp`, `Clip.cpp` | **fork**: W3C blend modes, clip shadow/blur/flip, overlay clips |
| `src/FFmpegReader/Writer.cpp` | demux, decode, scale, encode, mux |
| `tests/golden/` | the regression suite and its committed reference frames |
| `tests/bench/` | the performance benchmark and its recorded results |
| `tools/golden.sh` | build + run + report wrapper |
| `doc/` | permanent documentation: install guides, hardware acceleration, the benchmark history |
| `doc/gpu-migration/` | **temporary**: the plan, the decisions log, session status — deleted when the migration lands |
| `skia_build_script.sh` | out-of-tree Skia build (see below) |
| `../video-rendering-service` | the only consumer: JSON payload in, MP4 out |

## How to communicate

- Keep replies short. Lead with the result, a few bullets at most, tables only when numbers matter.
  Details belong in the docs (`doc/gpu-migration/STATUS.md`, `doc/*.md`), not in chat.

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
the golden suite is the test suite.

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
configure time with `-DSkia_ROOT=/usr/local/skia-gpu` (or leave it unset for the CPU one) and record
which one a build used in `doc/gpu-migration/GPU-DECISIONS.md`.

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

Off unless `OPENSHOT_GPU` says otherwise — `off` (default), `vulkan`, or `lavapipe` (Mesa's software
rasteriser, for machines with no GPU and for checking a result is not vendor-specific). Everything
goes through `GpuDevice::Instance().available()`, and `false` is a normal answer: fall back to
raster, never treat it as an error.

```bash
cmake -S . -B cmake-build-gpu -DCMAKE_BUILD_TYPE=Release -DSkia_ROOT=/usr/local/skia-gpu
cmake --build cmake-build-gpu --target openshot openshot-gpu-checks
OPENSHOT_GPU=vulkan cmake-build-gpu/tests/gpu/openshot-gpu-checks     # and =lavapipe
BUILD_DIR=$PWD/cmake-build-gpu tools/golden.sh check                  # must stay 292/292
```

Rules that are easy to get wrong and crash in the NVIDIA driver rather than anywhere useful:

- **Nothing Graphite hands out may outlive the `Context`.** `GpuDevice::DestroyInstance()` empties
  every pool and destroys every `Recorder` before the context, in that order. Do not put a
  `Recorder`, `SkSurface` or `SkImage` in a `thread_local` or a static — the device owns them.
- Cache a GPU object across calls only alongside `GpuDevice::Generation()`, and drop the cache when
  it changes.
- `GpuSurfacePool` is **per thread**, because a Graphite surface belongs to the recorder that made
  it. Never move a surface between threads.
- `GpuFrame` is `kRGBA_8888`; raster N32 is BGRA on x86. Anything moving bytes between the two swaps
  R and B.

## Facts that are easy to get wrong

- The service never touches `openshot::Settings`; `HARDWARE_DECODER` is 0 and the codec is
  hard-coded to libx264 in `ExportData.h`. The runtime image has no GPU today.
- Hardware decode (`HARDWARE_DECODER != 0`) throws on the first frame in this fork; the fix is
  plan step 1.5. Upstream 1.0.0 fixed it with `sw_pix_fmt`.
- Three time→frame conventions and two bezier-handle conventions coexist in the service; see
  `tests/golden/Recipes.h`.
- `Scene` in the golden harness must delete readers in reverse creation order (a FrameMapper before
  the reader it wraps) or teardown segfaults.
- `BorderReflectedMove` dx/dy are fractions of width/height; `Zoom` 100 = no zoom; `Exposure`
  clamps to ≥ 1.0; `CameraMovement` zoom is a percentage.
- Qt PNG "quality" 100 means no compression; the harness saves with 10.
