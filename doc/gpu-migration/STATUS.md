# Status

> **Resuming?** Run `/resume`. In short: branch `feature/gpu-rendering`, the golden suite must be
> green (`tools/golden.sh check`) before and after every change, performance is tracked with
> `openshot-bench` against `tests/bench/results/baseline-cpu.json`, and the work plan with its
> numeric gates is `doc/gpu-migration/GPU-RENDER-PLAN.md` section 3.

Last updated: 2026-09-14 · branch `feature/gpu-rendering` (plan steps 2.1 and 2.2 done)

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

- Base branch: stay on the fork (recommended) or merge upstream 1.0.0 first (plan step 0.1).
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

## Next step

**Phase 2 (R2a) — Skia on the GPU, glow pass first.** The plan was reordered on 2026-09-14 so
Phase 2 runs before Phase 1; see `doc/gpu-migration/GPU-RENDER-PLAN.md` section 3.1 for why. Phase
and step numbers are stable identifiers, not sequence.

1. **2.0 GPU-capable image** (was step 1.7, moved because Skia Vulkan cannot ship without it):
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
   Skia m147 needs Vulkan 1.4 headers, which the installer now ships. **Not yet done:** the
   `sudo ./install_skia_gpu.sh` into `/usr/local/skia-gpu` (verification used a scratch prefix).
3. **2.2 `src/gpu`** — ✅ **done.** `GpuDevice` (singleton: Vulkan device + one Graphite
   `Context`, recorder per thread, `available()`, `Generation()`), `GpuSurfacePool` (thread-local,
   recycles render targets) and `GpuFrame` (pooled surface + `upload()`/`readback()`). Off unless
   `OPENSHOT_GPU=vulkan|lavapipe`. `tests/gpu/openshot-gpu-checks` passes all five checks on the
   A2000 and on lavapipe: default-off, 200 device cycles with flat VRAM, 1000 bit-identical RGBA
   round trips, pool reuse, pool survives a device restart. Golden green on **both** the CPU-Skia
   and GPU-Skia builds (292/292 each), so the Skia swap moves no pixels.
4. **2.3 Glow pass on the GPU** — the actual prize, and **the next step**. `TextGlowRenderer`
   allocates its silhouette, ray-march and bloom surfaces from `GpuSurfacePool` via `GpuFrame` when
   `GpuDevice::Instance().available()`, runs the existing SkSL unchanged, and reads the result back
   into the CPU text image. Nothing else in the text engine changes. Watch the channel order:
   `GpuFrame` is `kRGBA_8888` while raster N32 is BGRA on x86, so the R/B swap in
   `SkiaRenderer::parseColorString` must be removed on the GPU path — a red glyph staying red is the
   check. Build with `-DSkia_ROOT=/usr/local/skia-gpu`; `cmake-build-gpu` is already configured.

> **Gate R2a:** `text_animated_glow_3` ≥ 4 fps (1.4 today) and `everything` ≥ 3 fps (1.8), golden
> green. Text scenarios may be re-baselined once, after reviewing every triptych.

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

- 2026-09-14 — plan step 2.2: `src/gpu` (GpuDevice, GpuSurfacePool, GpuFrame) +
  `tests/gpu/openshot-gpu-checks`. Two ownership crashes found and fixed by the checks; see
  `GPU-DECISIONS.md`. Golden green on both Skia builds.
- 2026-09-14 — plan step 2.1: `skia_build_script_gpu.sh` + `install_skia_gpu.sh`, `tests/gpu`
  smoke test, `FindSkia.cmake` honours `Skia_ROOT`. Golden green before and after.

- 2026-09-10 — analysis, plan, golden suite; first commit on `feature/gpu-rendering`.
- 2026-09-10 — CLAUDE.md + doc/gpu-migration/STATUS.md; plan section 4 (sizing for N parallel exports).
- 2026-09-10 — openshot-bench committed; full CPU baseline + concurrency measurements recorded in doc/PERFORMANCE-BASELINE.md.
- 2026-09-11 — second pass over the plan (draft 3): numeric gates per step, R2a split out after profiling showed the glow shader is 72 % of the worst scenario.
