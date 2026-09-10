# libopenshot (podcastle fork) — working notes for Claude

This is podcastle-studio's fork of libopenshot: a headless, JSON-driven video renderer used only by
`../video-rendering-service`. It is **not** used with any UI. The fork adds a Skia text engine
(`src/text`), subtitles (`src/subtitle`), W3C blend modes, clip shadow/blur/flip, overlay clips, and
the transition effects in `src/effects/image-processing-lib` (shared with the web front end via WASM).

## Read first

1. `STATUS.md` — where the work is right now and what the next step is. **Read it before doing
   anything, and update it at the end of any session that changed code, plans or decisions.**
2. `doc/GPU-RENDER-PLAN.md` — the multi-phase plan to move rendering fully onto the GPU and remove Qt.
   Sections 0–2 are background (measurements, GPU primer, Qt inventory); section 3 is the step list;
   section 4 sizes CPU/RAM/VRAM/NVENC for N parallel export processes.
3. `tests/golden/README.md` — the regression suite that gates every change.
4. `doc/PERFORMANCE-BASELINE.md` — benchmark history (`tests/bench`, `openshot-bench`). Re-run at the
   end of every optimisation phase, commit the JSON under `tests/bench/results/`, append the table.

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
