# Status

> **Resuming?** Run `/resume`. In short: branch `feature/gpu-rendering`, the golden suite must be
> green (`tools/golden.sh check`) before and after every change, performance is tracked with
> `openshot-bench` against `tests/bench/results/baseline-cpu.json`, and the work plan with its
> numeric gates is `doc/gpu-migration/GPU-WORKLIST.md` (strict order, W01…W31; the plan holds the
> reasoning, not the steps).
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

Last updated: 2026-09-16 · branch `feature/gpu-rendering`.
**Phase 2 is complete.** 2.0 landed 2026-09-15 (the GPU-capable image, in
`../video-rendering-service` branch `feature/gpu-rendering`), which was the last thing in it and the
only thing stopping R2a and R2b from shipping. **R2a complete** (2.1, 2.2, 2.3); **2.4 done, gate
met**; worklist **A done**, **B rejected on
measurement**, **C done differently**, **D done**, **E done**. The Skia text and subtitle engines now both run
on the GPU under one control (`GpuDevice::SetBackend`), and the image that can run them exists.
**W11 is done** (2026-09-16) and **W12 — the timeline canvas on the GPU — is what a resuming session
picks up next**. W01 and W02 are **deferred to the end of the migration** by the project owner: no
image build, no release, no merge to `develop` until the GPU work is finished. See "Next step".

## Where we are

### What works today (verified 2026-09-15, all four configurations green)

- **The Skia text engine renders entirely on the GPU** when one is enabled, with one readback at
  the reader boundary (`TextClipReader::renderToQImage`). Steps 2.1–2.4 plus worklist items A and C.
- **The Skia subtitle engine is GPU-capable**: `SubtitleManager::renderAtFrame(SkCanvas*, w, h, n)`
  draws onto whatever surface the caller has, bit-identical on GPU and raster. The Timeline still
  passes a raster canvas on purpose — see step 2.7. Step D.
- **One control for all GPU use**: `GpuDevice::SetBackend(Backend::Off|Vulkan|Lavapipe)`, overriding
  `OPENSHOT_GPU` at runtime. Every GPU path asks `GpuDevice::Instance().available()` and none reads
  the environment itself; the `control` check in `openshot-gpu-checks` enforces that as more GPU
  logic lands.
- **The CPU path got faster too**, which matters because it is what production runs: the frame-extent
  fix (item A) and the block-mode glow cache (item C) are both pure CPU wins.
- **The service image is GPU-capable and still runs on a CPU node** — step 2.0, in
  `../video-rendering-service` branch `feature/gpu-rendering`. Two independent switches, both
  defaulting to the CPU: `ENCODER=libx264|h264_nvenc` (probed once, falls back with a logged reason)
  and `OPENSHOT_GPU=off|vulkan|lavapipe` (read by the library). See item E below.
- `tools/golden.sh check` **292/292** on CPU Skia and on GPU Skia with the GPU off, on Vulkan and on
  lavapipe. `openshot-gpu-checks` **7/7** on Vulkan and lavapipe, on the host *and inside a
  container* on both a GPU node and a CPU node.

Measured at 1080p, `render` mode, 150 frames, interleaved on one machine — now on the **NVIDIA RTX
A2000**, which every earlier phase-2 number was *not* (they were taken on an Intel Iris Xe iGPU).
Full table and the raw JSON in `doc/PERFORMANCE-BASELINE.md`, 2026-09-15 section.

| scenario | baseline | CPU path now (`off`) | Vulkan now | speed-up |
|---|---|---|---|---|
| `text_animated_glow_3` | 1.4 fps | **4.2–4.3** | **51.7–52.9** | **12.3×** |
| `text_animated_glow_3` 2160p | – | 1.3 | **11.4** | **9.1×** |
| `everything` | 1.8 fps | **5.4** | **8.8** | 1.6× |
| `subtitles_words` | 56 fps | **109** | 113 | 1.03× |
| `text_static_4` | 62 fps | 61.8–64.5 | 60.8–61.1 | 0.96× |

Two corrections to what this file said on 2026-09-14:

- **The A2000 is roughly twice the iGPU** on the glow scenario — 52 fps against 26.7 — so the
  earlier phase-2 numbers understate the production case.
- **`text_static_4` is ~5 % slower on the GPU, and it is not the pipeline compile.** Three
  interleaved 600-frame pairs gave 145.7 fps mean off against 139.0 on Vulkan, ranges
  non-overlapping — so the earlier "at 600 frames the GPU is ahead" no longer holds on this card.
  Static text comes from the resting-frame cache, so there is almost no per-frame Skia work for the
  GPU to take over and what is left is the fixed cost of going through a GPU surface. Not a
  regression to fix; a reason `OPENSHOT_GPU` is a per-deployment switch rather than a default.

### Phase 0 baseline (history)

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

## Key finding from the second pass (superseded in part — see the table above)

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
`doc/gpu-migration/GPU-RENDER-PLAN.md` section 3.1 for why. Those phase and step numbers are stable
identifiers, not sequence. **What is still to do is the worklist further down, not this list.**

1. **2.0 GPU-capable image** — ✅ **done, 2026-09-15.** (Was step 1.7, moved because Skia Vulkan
   cannot ship without it.) Details in item E of the worklist below and in `GPU-DECISIONS.md`.
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

## Phase 2 items A–E — all done (kept for the reasoning, not as work)

Historical. These were the ad-hoc A–E labels used while Phase 2 ran; the live, ordered work is
`GPU-WORKLIST.md` W01…W31. Kept because the measurements and the two rejected approaches below are
worth not re-discovering. Each ended with the four-way golden sweep green at 292/292.

Originally: do these in order. Each one ends with the four-way golden sweep green at 292/292 and, where it
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

**D. 2.7 — subtitles on GPU surfaces.** — ✅ **done, as a capability; deliberately not switched
on for the Timeline path.** Gate already met: `subtitles_words` 92–101 fps CPU vs ≥ 85.

`src/subtitle` builds **no offscreen of its own** — every renderer draws straight onto the canvas it
is handed — so the only thing keeping subtitles off the GPU was `SubtitleManager::renderAtFrame`
constructing its own raster canvas from the caller's `QImage`. There is now a canvas overload,
`renderAtFrame(SkCanvas*, w, h, frame)`, and the `QImage` one is a thin wrapper over it. A
GPU-backed canvas therefore keeps the whole subtitle pass on the GPU. The `subtitle-gpu` check in
`openshot-gpu-checks` renders the same frame both ways and gets **worst channel delta 0** — bit
identical — on Vulkan and on lavapipe.

*Why the Timeline still hands it a raster canvas.* Subtitles composite onto an existing video frame,
so a GPU pass there means uploading the frame and reading it back. Measured:

| | full-frame GPU round trip | the subtitle drawing it would replace |
|---|---|---|
| 1080p | 5.6 ms (1.1 up + 4.4 back) | **0.27 ms** |
| 2160p | 18.7 ms (3.8 up + 15.0 back) | **0.61 ms** |

21× and 31× more than it saves. It becomes free the moment the frame is *already* on the GPU — i.e.
when the compositor moves in phase 3 — and at that point the caller simply passes its canvas. The
plan's other half, caching `buildCharRenderInfo` per segment, is capped by the same 0.27 ms and was
not done.

**Single GPU control.** Asked for by the project owner so all GPU use can be switched on and off in
one place, including the logic still to come. `GpuDevice::SetBackend(Backend)` now overrides
`OPENSHOT_GPU` programmatically and takes effect at once — it tears down a device built for the old
choice, which moves `Generation()` and so drops every cache holding a GPU object.
`RequestedBackend()` answers without creating the device; `BackendFromName` / `BackendName` convert
to and from the `OPENSHOT_GPU` spelling. The invariant that makes this one control is that every GPU
path asks `GpuDevice::Instance().available()` (or `GpuOffscreen::Match` / `GpuFrame::Create`, which
ask it for you) and none reads the environment for itself — the new `control` check in
`openshot-gpu-checks` fails if that stops being true.

> **Then re-measure gate R2b:** `text_animated_glow_3` ≥ 12 fps, `subtitles_words` ≥ 85 fps,
> `text_static_4` not slower, `everything` ≥ 4 fps, golden green in all four configurations.
>
> **R2b met 2026-09-14** on Vulkan at 1080p: `text_animated_glow_3` **26.7–33.7**,
> `subtitles_words` **94.6**, `everything` **7.7–8.4**, `text_static_4` not slower (its 150-frame
> deficit is a one-off pipeline compile; at 600 frames the GPU leads 116.3 to 113.0), golden
> 292/292 four ways. **R2b is code-complete and waits only on 2.0 below.**

## Next step

**Phase 2 is finished.** The remaining work is `doc/gpu-migration/GPU-WORKLIST.md`. One item to a
session; the worklist opens with the protocol.

> **2026-09-16, project owner — no release work until the GPU migration is done.** **W01** (build and
> push the service image) and **W02** (the full post-merge benchmark, the `develop` merge gate) are
> **deferred to the very end**. Develop and test locally, without container images, and keep moving
> the render path onto the GPU. Both items stay in the worklist, marked DEFERRED; nothing depends on
> either.

**W11 is done** (2026-09-16), taken out of order because W12 depends on it and nothing else does.
The four decisions the compositor bakes in are recorded in `GPU-DECISIONS.md`:

- **Canvas precision → `kRGBA_8888`**, overriding the F16 recommendation that was on file. Output is
  8-bit H.264 throughout; pooled surfaces are null-colour-space, so F16 would buy precision between
  stages but not gamma-correct blending; and 8888 keeps the GPU canvas bit-identical to the CPU path.
- **Graphite only**, no Ganesh fallback — there is no Ganesh code in `src/` to keep alive.
- **LUT → match the front end at the native cube size.** Measured: the `ColorMap.cpp` 17³ resample,
  not the interpolation kind, is essentially the whole editor-vs-export gap (17.05 LSB max / 0.404
  mean, against 4.34 / 0.060 for trilinear-vs-tetrahedral).
- **Nearest-neighbour sampling stays nearest** — both sites are in the submodule the front end runs
  through WASM.

**Next is W12 — the timeline canvas on the GPU**, the first item of Stage 5 and where the remaining
CPU time actually is. It now opens with a sub-task that did not exist before: tag the bit-exact
golden scenarios `"exact"` *before* touching the render path, because the suite currently gates
"close" and not "exact" (see below) and the 8888 decision is only enforceable if it does.

W03/W04 (CI, production corpus) remain open and are the safety net for Stage 5; W05–W10 are the CPU
quick wins. Then W13–W18 (the rest of the compositor), W19–W21 (effects as shaders), W22–W25 (frames
stay on the GPU — last, because it only pays after W12), W26–W28 (remove Qt), W29–W31 (frames in
flight, density, observability).

## Known oddities worth a look

- **Text is visibly different on the GPU.** The `text.*` scenarios are not bit-exact between the CPU
  and GPU paths — worst case 106 LSB (`text.curved`, PSNR 39.99) — and pass only because `Text.cpp`
  puts them all on `Tolerance::Loose()` (PSNR ≥ 38) for glyph anti-aliasing. Expected rather than
  broken, but "292/292 four ways" reads as a stronger claim than it is: for text the two paths agree
  only to PSNR ≥ 38. Everything that is *not* text is now gated bit-exact (see the log entry below).
- `effects.stack_crop_chroma_light_lut` golden shows harsh white blotches (ChromaKey + Light + LUT
  stacked). Baseline as-is; may be a real rendering quirk.
- Export round trip live-vs-decoded is ~28 dB on the noisy test pattern with no colour bias:
  x264 loss, not a matrix bug.

## Log

- 2026-09-16 — **W11 done: the four decisions the compositor bakes in**, taken out of order because
  W12 depends on W11 and nothing else does. Canvas `kRGBA_8888` (overriding the F16 recommendation
  on file), Graphite only, LUT matched to the front end at native cube size, nearest sampling kept.
  Each decided from a measurement or from the tree rather than the plan's recommendation: the LUT
  gap was quantified by replicating `resampleLut3D` verbatim over 636,056 colours (the 17³ resample
  is 17.05 LSB max / 0.404 mean; trilinear-vs-tetrahedral only 4.34 / 0.060 — but up to **98 LSB**
  on a coarse 2³ LUT), and the Ganesh question was settled by finding no Ganesh code in `src/` at
  all. Two consequences carried into W19 rather than left to be discovered there. Separately, found
  that the golden suite gates "close" and not "exact" — no scenario uses `Tolerance::Exact()` —
  which made tagging the bit-exact scenarios the first sub-task of W12. **That claim was wrong and
  is corrected in the 2026-09-16 W12 entry below** — 25 scenarios were already exact-gated; the grep
  behind it missed `tests/golden/scenarios/`. W01 and W02 deferred to the end of the migration by
  the project owner; no code changed, suite green 292/292 before and after.

- 2026-09-16 — **W12 sub-task: the golden suite now gates 76 of 95 scenarios bit-exact.** Measured
  every scenario across the full four-way sweep and took the worst result per scenario: 77 are
  bit-exact in all four configurations, 25 of which were already gated `Tolerance::Exact()`, so 51
  were newly tagged. `export.roundtrip_x264` deliberately left out (lossy round trip; its
  `Codec()` tolerance also governs its checks). All four configurations stay **292/292** under the
  tighter gate, so the compositor rewrite starting now cannot move a compositing, transform,
  transition, effect or reader pixel without failing. The 17 `text.*` scenarios are **not**
  bit-exact GPU-vs-CPU (up to 106 LSB) and stay on `Loose()`; `clipfx.*` blur/shadow and
  `subtitles.*` are expected to lose their exact gate at W14 and W17 respectively, by design.

- 2026-09-15 — **Phase 2 finished: worklist item E / plan step 2.0, the GPU-capable image.**
  `../video-rendering-service` branch `feature/gpu-rendering`: CUDA/FFmpeg runtime base with
  `graphics` capability and the Vulkan packages, Chrome dropped, `ENCODER` probed with fallback,
  `OPENSHOT_GPU` reported at startup, GPU-Skia `libopenshot.so` vendored, helm overlay and README.
  Two library fixes fell out — codec-agnostic colour tagging in `FFmpegWriter::SetOption`, and NVENC
  no longer inheriting VAAPI's H.264 overrides (it was emitting Main profile at `preset=slow`).
  Golden 292/292 four ways; `openshot-gpu-checks` 7/7 inside a container on a GPU node and a CPU
  node. First measurement on the **A2000**: animated glow text **4.3 → 52 fps at 1080p (12.3×)** and
  1.3 → 11.4 at 2160p (9.1×); static text is ~5 % *slower* on the GPU, reproducibly, and that is
  expected rather than a regression. The full service image is still unbuilt — the build-stage base
  is in a private registry this machine cannot authenticate to.
- 2026-09-14 — session wrap-up: plan steps 2.5 / 2.6 / 2.7 annotated with what actually happened,
  release gate R2b recorded as met, `GPU-DECISIONS.md` given the three decisions this session took.
  Golden green, tree clean, four commits on `feature/gpu-rendering`.
- 2026-09-14 — worklist item D (plan step 2.7) + the single GPU control. Subtitles gained a canvas
  entry point, so the whole pass follows its destination (bit-identical on GPU and raster); the
  Timeline keeps a raster canvas because a full-frame round trip is 5.6 ms at 1080p against 0.27 ms
  of drawing. `GpuDevice::SetBackend` is now the one switch for all GPU use, guarded by a `control`
  check. Golden 292/292 four ways; `subtitles_words` unchanged (98.7/99.8/100.5 -> 98.0/96.8/101.2).
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
