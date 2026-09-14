# Status

> **Resuming?** Run `/resume`. In short: branch `feature/gpu-rendering`, the golden suite must be
> green (`tools/golden.sh check`) before and after every change, performance is tracked with
> `openshot-bench` against `tests/bench/results/baseline-cpu.json`, and the work plan with its
> numeric gates is `doc/gpu-migration/GPU-RENDER-PLAN.md` section 3.

Last updated: 2026-09-14 · branch `merge/upstream-develop` (from `feature/gpu-rendering`)

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
2025-06-07 merge base) is merged on `merge/upstream-develop`, as a real merge commit so the next
sync has a proper base. 24 conflicted paths, 82 hunks, ~2,322 lines. Golden suite green and stable
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
3. **libopenshot-audio is pinned at 0.6.0**, below upstream's 1.0.0 requirement. It compiles and
   links; it is not proven at runtime beyond the silent-audio smoke test.

## Next step

**Plan steps 0.5 and 0.6, then Phase 1 (R1, CPU quick wins).**

> Step numbers below are the *Phase 0* steps in plan section 3, not the background subsections
> 0.1–0.4 of plan section 0. The two share numbers; only the ones in section 3 are work items.

1. Step 0.5 — collect 6 production payloads + media into a corpus and add a service-level
   end-to-end check (the golden suite covers the library; the corpus covers the service's
   JSON → timeline code). Step 0.6 — run `tools/golden.sh check` in CI on every PR.
2. Phase 1 steps in order (see the plan for per-step gates), each validated with
   `tools/golden.sh check` and `openshot-bench --quick`, with a full run + `compare` against
   `baseline-cpu.json` at the R1 gate:
   1.1 service: one `WriteFrame` call instead of 8-frame chunks;
   1.2 service: thread budgets from the cgroup quota / process count;
   1.3 writer: RGBA straight into nvenc, no swscale/memcpy, BT.709 tags;
   1.4 writer: sane nvenc rate-control options;
   1.5 reader: drop memset + `av_image_copy`, threaded swscale, fix the hardware-decode crash;
   1.6 `Frame::GetImageCV` memoisation.

## Known oddities worth a look

- `effects.stack_crop_chroma_light_lut` golden shows harsh white blotches (ChromaKey + Light + LUT
  stacked). Baseline as-is; may be a real rendering quirk.
- Export round trip live-vs-decoded is ~28 dB on the noisy test pattern with no colour bias:
  x264 loss, not a matrix bug.

## Log

- 2026-09-10 — analysis, plan, golden suite; first commit on `feature/gpu-rendering`.
- 2026-09-10 — CLAUDE.md + doc/gpu-migration/STATUS.md; plan section 4 (sizing for N parallel exports).
- 2026-09-10 — openshot-bench committed; full CPU baseline + concurrency measurements recorded in doc/PERFORMANCE-BASELINE.md.
- 2026-09-11 — second pass over the plan (draft 3): numeric gates per step, R2a split out after profiling showed the glow shader is 72 % of the worst scenario.
