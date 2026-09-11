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

1. `STATUS.md` — where the work is right now and what the next step is. **Read it before doing
   anything, and update it at the end of any session that changed code, plans or decisions.**
2. `doc/GPU-RENDER-PLAN.md` — the multi-phase plan to move rendering fully onto the GPU and remove Qt.
   Sections 0–2 are background (measurements, GPU primer, Qt inventory); section 3 is the step list;
   section 4 sizes CPU/RAM/VRAM/NVENC for N parallel export processes.
3. `tests/golden/README.md` — the regression suite that gates every change.
4. `doc/PERFORMANCE-BASELINE.md` — benchmark history (`tests/bench`, `openshot-bench`). Re-run at the
   end of every optimisation phase, commit the JSON under `tests/bench/results/`, append the table.
5. `doc/GPU-DECISIONS.md` — decisions already taken (do not re-litigate) and the ones still open.

## Repo map

| path | what |
|---|---|
| `src/` | the library. Upstream OpenShot plus this fork's additions |
| `src/text/` | **fork**: Skia text engine (layout, animation, glow, 3D tilt, curved text) |
| `src/subtitle/` | **fork**: Skia subtitle renderer driven by JSON |
| `src/effects/` | effect classes; the ones the service uses are listed in plan section 2.4 |
| `src/effects/image-processing-lib/` | **submodule**, shared with the web front end through WASM: transition algorithms and colour grading |
| `src/BlendModes.cpp`, `Clip.cpp` | **fork**: W3C blend modes, clip shadow/blur/flip, overlay clips |
| `src/FFmpegReader/Writer.cpp` | demux, decode, scale, encode, mux |
| `tests/golden/` | the regression suite and its committed reference frames |
| `tests/bench/` | the performance benchmark and its recorded results |
| `tools/golden.sh` | build + run + report wrapper |
| `doc/` | the plan, the baseline, the decisions |
| `skia_build_script.sh` | out-of-tree Skia build (see below) |
| `../video-rendering-service` | the only consumer: JSON payload in, MP4 out |

## How to communicate

- Keep replies short. Lead with the result, a few bullets at most, tables only when numbers matter.
  Details belong in the docs (`STATUS.md`, `doc/*.md`), not in chat.

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
| `skia_build_script_gpu.sh` *(added in plan step 2.1)* | Graphite + Vulkan | `~/skia-stable/out/Release-GPU/libskia.a` | `/usr/local/skia-gpu` via `install_skia_gpu.sh` |

Rules: **never edit `skia_build_script.sh` to add GPU support** — the CPU build must stay
reproducible as the no-GPU fallback. The two scripts share everything except the output directory,
the GPU GN args and the install prefix; keep the rest byte-identical so text rendering does not
drift. Both pin `SKIA_MILESTONE=m147` to match the front end's CanvasKit. Select a build at
configure time with `-DSkia_ROOT=/usr/local/skia-gpu` (or leave it unset for the CPU one) and record
which one a build used in `doc/GPU-DECISIONS.md`.

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
