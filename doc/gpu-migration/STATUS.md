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

Last updated: 2026-09-14 · branch `feature/gpu-rendering`.
**R2a complete** (2.1, 2.2, 2.3); **2.4 done, gate met**; worklist **A done**, **B rejected on
measurement**, **C done differently**; **2.0 still owed**. Next: the Phase 2 worklist below, item
**D** (plan step 2.7), or item **E** (2.0), which is what actually blocks shipping.

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

> **Gate 2.4: met**, once worklist item A below sized the frame buffer correctly.
> `text_animated_glow_3` 7.3 → **26.7 fps** (gate ≥ 8); `text_static_4` unchanged within noise
> (60.2/59.9/58.9 before vs 60.2/58.8/58.7 after, GPU off, same RSS). At the time 2.4 landed the
> gate read 6.4 fps, blocked by a 164 MB frame buffer for 920 × 101 of content — see item A for
> what that actually was.

## Phase 2 worklist — what a resuming session picks up

Do these in order. Each one ends with the four-way golden sweep green at 292/292 and, where it
claims a speed-up, a back-to-back `openshot-bench` measurement on one machine. Nothing here may
regress the CPU path (see the standing constraint at the top).

**A. Bound the animated frame extent.** — ✅ **done, 2026-09-14.**

> **The diagnosis this item was written around was wrong, and the correction is worth keeping.**
> It claimed `SkMatrix::mapRect` blew up as a perspective corner approached the vanishing point.
> It does not. The 164 MB clip ("Rise and shine") uses the `rise-chars` / `drop-words` presets,
> which carry only `opacity`, `ty` and 2D `rotate` — no `rotateX`, `rotateY` or `perspective`, so
> `mapRect` never sees a projective matrix at all. Sweeping tilt from 0° to 89.9° over the clip
> that *does* tilt holds its frame at 958 × 116 throughout; the perspective mapping never blew up.
> There was no near-plane bug to fix.

The 164 MB was two independent things, measured by decomposing the clip:

| "Rise and shine" (content 920 × 101) | frame | size |
|---|---|---|
| no glow, no animation | 920 × 101 | 0.4 MB |
| glow only | 2166 × 1346 | 11.1 MB |
| animation only | 1291 × 15724 | 77.4 MB |
| both (as benchmarked) | 2536 × 16969 | 164.2 MB |

1. **The recipe, not the library.** `rise-chars` had `ty: 40` and `drop-words` `ty: 60`. `ty` is in
   fontSize units and the service passes it straight through (`TextClipData.cpp:112`), so that was
   12,288 px and 18,432 px of travel per glyph. Real payloads (`../text-metrics/examples`) use
   `ty` ∈ [−0.27, 0.5] and `tx` ∈ [−2, 0]; the recipe was ~100× off, and the committed golden showed
   it — at frame 15 only "R", "i", "s" were on screen, scattered, the rest thousands of pixels away.
   Fixed to 0.4 / 0.6, which is what CLAUDE.md's "recipes mirror production" rule requires.
   `tests/golden/Recipes.cpp` now says so at the preset, so the next value that lands there is
   sanity-checked against the range.
2. **A real library over-estimate.** `TextGlowRenderer::glowMarginFor` and `effectsMargin`
   (`TextClipReader.cpp`) padded **both** axes with `max(contentW, contentH) / 2`. The ray-march is
   a homothety about the light source — a silhouette pixel `d` from the light on one axis lands
   `(1 + rayLen) · d` away **on that same axis** — so the reach is per-axis, and the short axis was
   padded from the long one. Both now compute per axis, clamped to the old value, so whichever axis
   bound the old margin keeps it to the bit. Widths came out byte-identical (2536 / 1478 / 2068 for
   the scenario's three clips) and no glow golden moved, which is the proof the clamp holds.

Combined, the scenario's three clips go 178.9 → **13.5 MB** of frame buffer.

*Result,* back to back on one machine, GPU Skia build, 1080p render, 150 frames:

| scenario | GPU off | Vulkan |
|---|---|---|
| `text_animated_glow_3` | 1.2 → **2.9 fps**, 1.88 → **0.41 GB** | 7.3 → **26.7 fps**, 1.91 → **0.51 GB** |
| `everything` | 1.8 → **3.4 fps**, 1.71 → **1.02 GB** | 4.1 → **8.4 fps**, 1.92 → **1.16 GB** |
| `text_static_4` | 60.1 → 56.2–60.2 fps (noise, RSS equal) | 45.5 → 46.2 fps |

A pure CPU-path win as predicted: **GPU off it is 2.4× on the glow scenario and 1.9× on
`everything`**, with RSS down 4.6× and 1.7×. Golden 292/292 in all four configurations; the only
re-baseline was the two animation scenarios the recipe fix intentionally changed
(`text.anim_in_rise_chars`, `text.anim_out_drop_words`, 5 PNGs), each triptych reviewed by eye with
nothing clipped at the frame edge.

**Note the headline the old diagnosis implied was inflated.** The "6.3 → 34.7 fps" probe number
came from clamping an extent that was only that large because of the bad recipe; the honest figure
for the library change is the table above.

**B. 2.5 — skip the CPU-blur workaround on GPU surfaces.** — ❌ **measured and rejected,
2026-09-14. Do not redo it.** Full reasoning in `GPU-DECISIONS.md`; the short version:

The plan's premise about the clamp is correct — Graphite never sees a mask filter, so `SkCanvas`
converts it through `asImageFilter` and the sigma reaches `SkImageFilters::Blur` unclamped; drawing
directly on a GPU destination does give the full blur (**83.7 dB PSNR** against the CPU
reconstruction at 4K). But the σ > 120 downscale is **an optimisation in its own right**, not only a
clamp workaround: at σ = 384 it blurs ~10× fewer pixels. Skipping it gains nothing on Vulkan
(27.2/28.8 → 32.0/28.1 ms over two A/B rounds) and costs **~30 % on lavapipe** (145.9/141.2 →
171.5/186.6 ms), which is a supported no-GPU configuration. The CPU path was bit-identical, as
required, and all four golden configurations stayed at 292/292 — the change was correct, just not
worth making. Reverted.

**Its gate belongs to R3, not here.** `text_static_4` at 2160p ≥ 15 fps cannot be moved by this
step. Static text is served from the resting-frame cache, so the shadow renders **once** (~120 ms,
amortised to ~0.8 ms over 150 frames), and timing the scenario's four clips at 2160p gives
0.98 / 0.04 / 0.10 / 0.01 ms per steady-state frame — about **1 % of its ~90 ms frame**. The rest is
decode and Qt compositing.

**C. 2.6 — long-lived `SkiaRenderer` and cross-frame caches.** — ⚠️ **measured; done differently.**
The two halves as written were worth ~0.5 % and ~4 %. What the measurement pointed at instead is
committed: a cross-frame cache for the **composited glow image** on block-mode animations, worth
**+19 % / +22 %** on the CPU path.

*Why the plan's version is not worth doing.* At 1080p on the raster path, per frame:

| clip of `text_animated_glow_3` | with glow | glow removed |
|---|---|---|
| a "Rise and shine" (glyphs move) | 231 ms | **0.15 ms** |
| b "PULSE" (block scale + static tilt) | 81 ms | **1.08 ms** |
| c "KEYFRAMED" (glyphs still, style keyframes) | 103 ms | **1.04 ms** |

The glow is ~99 % of every frame. Sweeping `OPENSHOT_GLOW_STEPS`, which changes only the march step
count: 4 → 58 ms, 8 → 104, 16 → 158, 24 → 243, 32 → 318 — linear at 9.3 ms/step with a ~21 ms
intercept, so **the ray-march is ~91 %** and everything else is ~9 %. That caps a long-lived
`SkiaRenderer` (the whole non-glow frame is ≤ 1.1 ms, and `SkiaRenderer.h` already documents that
the expensive half — fontconfig `SkFontMgr`, typeface resolution — lives in the `SkiaFontResources`
singleton) and caps a silhouette/bake cache at the ~9 % non-march share.

*What was done instead.* A block-mode animation concats its transform onto the **canvas** before
the block is drawn, so the glow is marched in block-local space and composited with a single
`drawImage`. Caching that composited image and redrawing it is therefore **bit-identical** — the
same draw call with the same image — not an approximation, and it skips the march entirely.
`text::GlowFrameCache` lives on `TextClipReader`, which is what makes the key three fields: within
one reader with no glow-affecting style keyframe the layout, paint and glow style are fixed by
construction, so only the block's animated opacity and letter spacing remain. A miss costs exactly
what the uncached path cost before.

GPU images are deliberately **not** cached: `GpuFrame::snapshot()` comes off a pooled surface that
returns to the pool when the frame dies, and a cached texture would also have to be dropped before
the Graphite context goes away. `paintGlowFromSilhouette` returns null on the GPU path so this
cannot be got wrong by accident.

*Result,* back to back on one machine, 1080p render, GPU off:

| scenario | before | after |
|---|---|---|
| `text_animated_glow_3` | 3.1 fps | **3.7 fps** (+19 %) |
| `everything` | 3.6 fps | **4.4 fps** (+22 %) |
| `subtitles_words` | 96.7 fps | 96.6 fps (untouched) |

Vulkan is unchanged at 26.7 fps, by design. Golden **292/292 in all four configurations with no
re-baseline** — `text.anim_loop_pulse_with_glow` is exactly the cache-hit case, so the suite is
what proves the bit-identity.

*Left on the table:* the cache misses whenever the block's opacity animates (a fade), because the
glow's alpha is folded into the ray and bloom paints before they are Screen-composited together.
Caching the ray layer at alpha 1 and applying the alpha at composite time would cover fades too,
but it needs an extra surface per frame, which would cost every cache **miss** — and the standing
constraint says the CPU path must not get slower. Clips with a glow-affecting style keyframe
(clip c above) also never hit; decoupling colour from the silhouette would fix that.

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

- 2026-09-14 — worklist item C (plan step 2.6): the plan's two halves measured at ~0.5 % and ~4 %
  (the glow is ~99 % of an animated glow frame and the ray-march ~91 % of that). Shipped instead a
  cross-frame cache for the composited glow image on block-mode animations, where the march is
  frame-invariant: CPU path `text_animated_glow_3` +19 %, `everything` +22 %, Vulkan unchanged,
  golden 292/292 four ways with no re-baseline.
- 2026-09-14 — worklist item B (plan step 2.5): written, verified four-way green, measured,
  **reverted**. The large-sigma shadow downscale is an optimisation, not just a CPU-clamp
  workaround — skipping it on GPU surfaces gains nothing on Vulkan and costs ~30 % on lavapipe. Its
  gate (`text_static_4` 2160p) is unreachable from this step: text is ~1 % of that scenario.
- 2026-09-14 — worklist item A: frame-extent sizing. No perspective bug existed; the 164 MB buffer
  was a ~100×-too-large `ty` in `tests/golden/Recipes.cpp` plus a glow margin that padded both axes
  from the longer one. Per-axis glow margin (clamped, pixel-identical) + recipe fix. Glow scenario
  2.4× faster with the GPU **off**, 3.7× with Vulkan; RSS 1.88 → 0.41 GB. Gate 2.4 now met.
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
