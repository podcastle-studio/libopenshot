# Status

> **Resuming?** Run `/resume`. In short: branch `feature/gpu-rendering`, the golden suite must be
> green (`tools/golden.sh check`) before and after every change, performance is tracked with
> `openshot-bench` against `tests/bench/results/baseline-cpu.json`, and the work plan with its
> numeric gates is `doc/gpu-migration/GPU-RENDER-PLAN.md` section 3.
> **Read the standing constraint below before planning any step.**

## Standing constraint — the CPU path ships, the GPU path is an addition

Stated by the project owner, 2026-09-14. This overrides anything in
`GPU-RENDER-PLAN.md` that reads otherwise, and every remaining step is judged against it:

1. **The CPU path stays fully working, at full quality, forever.** It is the path production runs
   today (the runtime image has no GPU) and the path a no-GPU machine falls back to. A step is not
   done if it makes the CPU path slower, worse-looking, or dependent on a GPU being present.
2. **The GPU path is a configurable addition**, off by default, selected by `OPENSHOT_GPU`
   (`off` | `vulkan` | `lavapipe`) and by which Skia the build was configured against
   (`-DSkia_ROOT=/usr/local/skia-gpu`). `GpuDevice::available() == false` is a normal answer, not
   an error.
3. **Never delete CPU code because the GPU does not need it.** Gate it instead: keep the CPU branch
   and skip it when a GPU surface is in use. This directly rewrites plan step 2.5 — see the
   worklist below.
4. **Both configurations are validated on every change.** The four-way golden sweep (CPU Skia;
   GPU Skia with the GPU off; GPU Skia on Vulkan; GPU Skia on lavapipe) is the acceptance test,
   and all four must be 292/292. Commands are in `CLAUDE.md` under "GPU rendering (`src/gpu`)".

Last updated: 2026-09-14 · branch `feature/gpu-rendering`, working tree clean at `fae61407`.
**R2a complete** (2.1, 2.2, 2.3); **2.4 done bar its gate**; **2.0 still owed**.
Next: the Phase 2 worklist below, item **A** first.

## Where we are

Phase 0 of `doc/gpu-migration/GPU-RENDER-PLAN.md` is mostly done:

- Baseline measured (plan section 0.3): codecs are not the bottleneck; Qt raster compositing and
  single-threaded swscale are. GPU encode alone gives ~1.3x; hardware decode crashes in the fork.
- Plan reviewed a second time and rewritten around the measured data (`doc/gpu-migration/GPU-RENDER-PLAN.md`,
  draft 3): every step now has a numeric gate tied to the 1080p baseline, every phase a releasable
  stop, and Phase 2 is split so the single highest-value change ships on its own.
- Golden-frame regression suite built and baselined: `tests/golden`, 95 scenarios, 292 frames,
  green and bit-stable across thread counts; a deliberate 1 px composite shift fails 282 frames.
  Run with `tools/golden.sh check`.
- Per-process resource numbers measured for parallel-export sizing (plan section 4): a 1080p
  libx264 export uses ~8 cores / 0.75 GB; with nvenc 1.6 cores / 0.55 GB / 0.25 GB VRAM; 4K sources
  ~1.5 GB, 4K output ~2.2 GB. One NVENC engine ≈ 165 fps of 1080p.
- Performance benchmark built and **baseline complete**: `tests/bench/results/baseline-cpu.json`
  (195 cases, 89 min) plus `parallel-{1,2,4}-*.json`, written up in `doc/PERFORMANCE-BASELINE.md`.
  Headlines at 1080p: single video 81 fps x264, podcast layout 20 fps, animated glow text 1.4 fps
  (p95 952 ms, 1 core), everything-at-once 1.8 fps; 4K glow text needs 9.6 GB. Four concurrent
  1080p exports give 1.8x aggregate throughput, not 4x.
- All of the above is committed on `feature/gpu-rendering`.

**2026-09-14 — upstream merged.** `upstream/develop` (341 commits / 220 files ahead of the
2025-06-07 merge base) is merged and folded into `feature/gpu-rendering` (fast-forward; the
`merge/upstream-develop` branch has been deleted). It is a real merge commit, so the next sync has
a proper base. 24 conflicted paths, 82 hunks, ~2,322 lines. Golden suite green and stable
across 3 runs, with exactly one deliberate re-baseline (`compositing.layer_order`, see below).
Every per-file decision and the remaining checklist are in `doc/gpu-migration/UPSTREAM-MERGE.md`.

This brings in plan step 1.5's hardware-decode fix (better than the plan proposed), FFmpeg 8
support, opt-in Qt6, thread-budget settings that overlap step 1.2, and ~15 crash fixes.

## Open decisions (record in `doc/gpu-migration/GPU-DECISIONS.md` when taken)

- Timeline canvas precision for the GPU compositor: RGBA8 or RGBA16F (RGBA16F recommended).
- Reference for LUT rounding: native `ColorMap.cpp` or the WASM `LutApply.cpp` path.

## Key finding from the second pass

gdb sampling attributes the worst scenario precisely: `text_animated_glow_3` (1.4 fps at 1080p,
952 ms/frame, 1.0 core) spends ~72 % of its time in `TextGlowRenderer::paintGlowFromSilhouette`,
running the **already-SkSL** glow ray-march on Skia's CPU raster pipeline because Skia is built
without a GPU backend. `everything` spends 65 % there too. Moving that one pass to the GPU needs no
new algorithm — hence the new **R2a** stop (Skia Vulkan build + glow surfaces only, gate ≥ 4 fps).
`grid_3x3` and `heavy_effects` are instead dominated by Qt raster `drawImage`, which is R3.

## Blocking before the merge branch lands

1. **Full benchmark still owed, but the merge is performance-neutral.** An interleaved A/B against
   a pre-merge build (both run back to back, same conditions) shows no difference:
   `single_video` 1080p render 69.5–72.5 fps before vs 70.6–72.4 after; `podcast_pip` 12.6–13.1 vs
   12.7–13.4. Absolute numbers are unusable — the machine was throttled to 400 MHz and *both*
   builds landed ~40 % under `baseline-cpu.json`. Re-run the full `openshot-bench` + `compare` on a
   quiet machine before merging into `develop`. Gate: no scenario more than 5 % slower.
2. **`compositing.layer_order` was re-baselined.** Clip sort order is now insertion-stable rather
   than address-tie-broken (see `doc/gpu-migration/GPU-DECISIONS.md`). A clip sharing a layer *and*
   position with another now draws on top if it was added later; previously it could be hidden, and
   which happened varied run to run. Confirm this is the behaviour the service wants — or fix the
   layer collision so it cannot arise.
3. **libopenshot-audio stays at 0.6.0 — settled, not a risk.** Upstream's 1.0.0 requirement came
   from a release-tagging commit (`ffcff368`) that changes only version strings; they developed
   against 0.6.0 throughout. `ldd -r` on the merged library shows zero unresolved symbols. The open
   part is only that audio behaviour is thinly covered by the suite (one smoke test), which the
   merge does not change.

## Phase 2 so far

The plan was reordered on 2026-09-14 so Phase 2 runs before Phase 1; see
`doc/gpu-migration/GPU-RENDER-PLAN.md` section 3.1 for why. Phase and step numbers are stable
identifiers, not sequence. **What is still to do is the worklist further down, not this list.**

1. **2.0 GPU-capable image** — ⏳ **the only thing left in R2a, and now the blocker for shipping
   it.** (Was step 1.7, moved because Skia Vulkan cannot ship without it.)
   CUDA/Vulkan base image, `graphics` in `NVIDIA_DRIVER_CAPABILITIES`, `libvulkan1` +
   `vulkan-tools`, `ENCODER=libx264|h264_nvenc` with CPU fallback.
   *Verify:* container starts on both a CPU and a GPU node; `vulkaninfo --summary` shows the NVIDIA
   ICD; one export completes on each.
2. **2.1 `skia_build_script_gpu.sh`** — ✅ **done.** Graphite/Vulkan Skia m147 builds into
   `out/Release-GPU` beside the untouched CPU build; `install_skia_gpu.sh` installs to
   `/usr/local/skia-gpu`. `tests/gpu/openshot-gpu-smoke` (`-DENABLE_GPU_SMOKE=ON`, or configure
   `tests/gpu` on its own) proves the whole path — Vulkan device → Graphite `Context` → 64x64
   gradient → readback → PNG — on the NVIDIA A2000 and on lavapipe, byte-identical. Three
   corrections the plan did not have, all in `GPU-DECISIONS.md`: `-DSkia_ROOT` needed a
   `FindSkia.cmake` fix to beat pkg-config; Graphite needs a private-header memory allocator;
   Skia m147 needs Vulkan 1.4 headers, which the installer now ships. Installed into
   `/usr/local/skia-gpu`; `cmake-build-gpu` is configured against it.
3. **2.2 `src/gpu`** — ✅ **done.** `GpuDevice` (singleton: Vulkan device + one Graphite
   `Context`, recorder per thread, `available()`, `Generation()`), `GpuSurfacePool` (thread-local,
   recycles render targets) and `GpuFrame` (pooled surface + `upload()`/`readback()`). Off unless
   `OPENSHOT_GPU=vulkan|lavapipe`. `tests/gpu/openshot-gpu-checks` passes all five checks on the
   A2000 and on lavapipe: default-off, 200 device cycles with flat VRAM, 1000 bit-identical RGBA
   round trips, pool reuse, pool survives a device restart. Golden green on **both** the CPU-Skia
   and GPU-Skia builds (292/292 each), so the Skia swap moves no pixels.
4. **2.3 Glow pass on the GPU** — ✅ **done.** `paintGlowFromSilhouette` takes its working surface
   from `GpuSurfacePool` via `GpuFrame`, runs the unchanged SkSL, and reads back into the raster
   text image. The R/B swap in `SkiaRenderer::parseColorString` was **not** removed — it is a
   logical-colour convention that survives the round trip, and removing it would be a bug; see
   `GPU-DECISIONS.md`. The silhouette is still rasterised on the CPU and uploaded once per frame.

> **Gate R2a: met.** `text_animated_glow_3` 1.3 → **4.5 fps** (gate ≥ 4), `everything` 1.8 →
> **4.3 fps** (gate ≥ 3), measured back to back on one machine. Golden green with the GPU on at
> 292/292 **with no re-baseline at all** — the one allowed re-baseline was not needed. Four
> configurations green: CPU Skia; GPU Skia with the GPU off; GPU Skia on Vulkan; GPU Skia on
> lavapipe.

5. **2.4 Whole text engine on GPU surfaces** — ✅ **code done, gate blocked by a sizing bug.**
   `GpuOffscreen::Match(destination, w, h)` puts every remaining offscreen in the same memory as
   the canvas it will be drawn onto — the two glow silhouettes, the baked 3D block textures and
   the large-sigma shadow — and `TextClipReader::renderToQImage` renders the frame on a GPU
   surface with one readback at the boundary. The glow no longer round-trips at all when its
   destination is GPU-backed, and a per-frame render is handed to the `Frame` without the
   `image->copy()` the plan called out. Golden **292/292 in all four configurations with no
   re-baseline** (CPU Skia; GPU Skia off; Vulkan; lavapipe), plus a new `pool-canvas` check in
   `openshot-gpu-checks`. Two findings in `GPU-DECISIONS.md`: a pooled surface hands back the
   previous user's canvas transform, and Graphite has no synchronous `readPixels`.

> **Gate 2.4: not met — blocked on a pre-existing frame-sizing defect, not on the GPU work.**
> `text_animated_glow_3` 4.5 → **6.4 fps** (gate ≥ 8); `text_static_4` 62 → 56 fps at the bench's
> 150 frames, but p50 is unchanged (15.17 → 15.23 ms) and at 600 frames it is 135.5 vs 133.1 — the
> whole gap is one ~425 ms Graphite pipeline compile that amortises away over a real export.
>
> The glow gate misses for one reason: **`TextClipReader` sizes the "Rise and shine" clip's frame
> buffer at 2536 × 16969 = 164 MB** for 920 × 101 of content. `computeAnimatedExtent`
> (`TextAnimationRenderer.cpp`) bounds each sampled animation matrix with `SkMatrix::mapRect`,
> which blows up when a perspective corner approaches the vanishing point. That buffer is
> allocated, cleared and — now — read back over PCIe every frame; it is ~90 % of the scenario.
> Clamping the mapped extent to 4× the box (a throwaway probe, **not** a correct fix) takes the
> same build from 6.3 → **34.7 fps** and RSS 2.29 → 0.68 GB. So the gate is comfortably reachable;
> what it needs is a correct bound on the perspective mapping, which is its own change with its own
> re-baseline risk, not part of 2.4.

## Phase 2 worklist — what a resuming session picks up

Do these in order. Each one ends with the four-way golden sweep green at 292/292 and, where it
claims a speed-up, a back-to-back `openshot-bench` measurement on one machine. Nothing here may
regress the CPU path (see the standing constraint at the top).

**A. Bound the animated frame extent.** *Not a numbered plan step; do it first.* The largest
measured win available and a **pure CPU-path win** — it costs the raster path exactly as much as
the GPU one. `computeAnimatedExtent` (`src/text/TextAnimationRenderer.cpp`) bounds each sampled
animation matrix with `SkMatrix::mapRect`, which blows up as a perspective corner approaches the
vanishing point; `TextClipReader` then sizes one `text_animated_glow_3` clip's buffer at
2536 × 16969 = 164 MB for 920 × 101 of content, and allocates, clears and reads it back every
frame. The same `mapRect` pattern is in `TextClipReader`'s own tilt branch and should be fixed with
it. *Approach:* bound the mapping properly — clip the box against the near plane before mapping, or
map the four corners and reject/clamp any with a non-positive `w` — rather than clamping the
result, which is what the throwaway probe did. *Verify:* every `text.*` and `subtitles.*` golden
reviewed by eye (under-sizing clips the animation, which the suite will show as missing pixels at
the frame edge); `text_animated_glow_3` ≥ 8 fps, which also clears **gate 2.4**; RSS down from
2.29 GB. Probe measured 6.3 → 34.7 fps and 2.29 → 0.68 GB.

**B. 2.5 — skip the CPU-blur workaround on GPU surfaces.** *Rewritten by the standing constraint:
the plan says "delete", which would break the CPU path.* The σ > 120 downscale branch in
`TextClipRenderer::renderShadowLayer` exists because Skia's CPU mask blur clamps sigma at 128 px;
the GPU has no such clamp. **Keep the branch** and take it only when the offscreen is raster
(`GpuOffscreen::onGpu()` already answers this), so the GPU draws the true sigma directly and the
CPU keeps its downscale reconstruction. *Verify:* a 4K text shadow on GPU matches the 1080p shadow
scaled up (SSIM ≥ 0.97); `text_static_4` at 2160p ≥ 15 fps (11.8); CPU-path goldens **bit-identical**,
GPU-path text goldens reviewed if they move.

**C. 2.6 — long-lived `SkiaRenderer` and cross-frame caches.** One renderer per reader instead of
one per frame, so the font and paint caches survive; cache the glow silhouette and the 3D block
bake, keyed by the plan hash, the animation-independent style **and** `GpuDevice::Generation()`
(see `CLAUDE.md` — a GPU object cached across a device teardown crashes in the driver). Helps both
paths. *Verify:* `text_animated_glow_3` ≥ 12 fps; text goldens unchanged.

**D. 2.7 — subtitles on GPU surfaces.** `SubtitleManager::renderAtFrame` draws into a
`GpuOffscreen`; cache the per-word `buildCharRenderInfo` work per segment (a CPU-path win too).
*Verify:* `subtitles_words` ≥ 85 fps (56 CPU baseline; already 108.8 with the GPU on after 2.4, so
the real target here is the CPU number and the caching); `tools/golden.sh check --filter subtitles`
green.

> **Then re-measure gate R2b:** `text_animated_glow_3` ≥ 12 fps, `subtitles_words` ≥ 85 fps,
> `text_static_4` not slower, `everything` ≥ 4 fps, golden green in all four configurations.

**E. 2.0 — GPU-capable image.** Still owed, and still the thing that stops any of this shipping.
Lives in `../video-rendering-service` (branch `main`), not in this repo: CUDA/Vulkan base image,
`graphics` in `NVIDIA_DRIVER_CAPABILITIES`, `libvulkan1` + `vulkan-tools`, drop Google Chrome
(`Dockerfile` lines 86–89, ~130 MB), `ENCODER=libx264|h264_nvenc` with CPU fallback, pin the digest.
It must also ship Skia's Vulkan 1.4 headers (see `CLAUDE.md`). *Verify:* the container starts on a
CPU node **and** a GPU node; `ffmpeg -encoders` lists `h264_nvenc`; `vulkaninfo --summary` shows the
NVIDIA ICD; one export completes on each. Per the standing constraint the CPU node is not a
degraded mode — `OPENSHOT_GPU` stays `off` there and the same image must render identically.
*Needs the owner's go-ahead:* it changes the production service repo and needs a real GPU node to
verify.

Still open from Phase 0, not blocking Phase 2: **0.5** the six-payload production corpus and
**0.6** CI running `tools/golden.sh check` per PR.

**Phase 1 (R1, CPU quick wins) now runs after Phase 2** and is smaller than when written — the
upstream merge already delivered 1.5's hardware-decode fix and overlaps 1.2's thread budgets.
Remaining: 1.1 single `WriteFrame` call; 1.2 thread budgets; 1.3 RGBA straight into nvenc;
1.4 nvenc rate control; 1.5 reader copy removal + threaded swscale; 1.6 `GetImageCV` memoisation.

## Known oddities worth a look

- `effects.stack_crop_chroma_light_lut` golden shows harsh white blotches (ChromaKey + Light + LUT
  stacked). Baseline as-is; may be a real rendering quirk.
- Export round trip live-vs-decoded is ~28 dB on the noisy test pattern with no colour bias:
  x264 loss, not a matrix bug.

## Log

- 2026-09-14 — plan step 2.4: whole text engine on GPU surfaces (`GpuOffscreen`, one readback at
  the reader boundary, no `image->copy()` for per-frame renders). Golden 292/292 in all four
  configurations, no re-baseline. Gate blocked by the 164 MB animated-extent buffer, not by the
  GPU path; a probe clamp shows 6.3 → 34.7 fps once that is sized correctly.
- 2026-09-14 — plan step 2.3: glow ray-march on the GPU. R2a's two gates met, golden green with no
  re-baseline. Graphite needs explicit image uploads; the planned R/B swap removal was wrong.
- 2026-09-14 — plan step 2.2: `src/gpu` (GpuDevice, GpuSurfacePool, GpuFrame) +
  `tests/gpu/openshot-gpu-checks`. Two ownership crashes found and fixed by the checks; see
  `GPU-DECISIONS.md`. Golden green on both Skia builds.
- 2026-09-14 — plan step 2.1: `skia_build_script_gpu.sh` + `install_skia_gpu.sh`, `tests/gpu`
  smoke test, `FindSkia.cmake` honours `Skia_ROOT`. Golden green before and after.

- 2026-09-10 — analysis, plan, golden suite; first commit on `feature/gpu-rendering`.
- 2026-09-10 — CLAUDE.md + doc/gpu-migration/STATUS.md; plan section 4 (sizing for N parallel exports).
- 2026-09-10 — openshot-bench committed; full CPU baseline + concurrency measurements recorded in doc/PERFORMANCE-BASELINE.md.
- 2026-09-11 — second pass over the plan (draft 3): numeric gates per step, R2a split out after profiling showed the glow shader is 72 % of the worst scenario.
