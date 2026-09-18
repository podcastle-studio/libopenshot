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
   and all four must be **295/295** (292 until W18 added three frames of background-colour
   coverage). Commands are in `CLAUDE.md` under "GPU rendering (`src/gpu`)".

Last updated: 2026-09-18 · branch `feature/gpu-rendering`.
**Phase 2 is complete.** 2.0 landed 2026-09-15 (the GPU-capable image, in
`../video-rendering-service` branch `feature/gpu-rendering`), which was the last thing in it and the
only thing stopping R2a and R2b from shipping. **R2a complete** (2.1, 2.2, 2.3); **2.4 done, gate
met**; worklist **A done**, **B rejected on
measurement**, **C done differently**, **D done**, **E done**. The Skia text and subtitle engines now both run
on the GPU under one control (`GpuDevice::SetBackend`), and the image that can run them exists.
**W11–W14 are done** (2026-09-16). **W15 and W16 are both void** — examined 2026-09-17 and closed
with no code change, because everything they ask for is already done, unnecessary, or forbidden by
the standing constraint (see the worklist items, and the note below on why Stage 5 reads this way).
**W17 is done** (2026-09-17; its fps gate closed on mains power 2026-09-18) and **W18 is done**
(2026-09-18), the latter scoped to the render path because its written gate names a build flag that
does not exist. **Stage 5 is finished.**
**A resuming session starts Stage 2 — W03 and W04** (2026-09-18, project owner): Stage 6 (effects)
is the obvious next thing, but Stage 2 (the safety net) and Stage 3 (the CPU wins) were skipped past
and are to be closed first, so everything up to the effects work is complete.
W01 and W02 have **moved to the end as Stage 10** — no image build, no release, no merge to
`develop` until the GPU work is finished, so they now sit where they actually run. See "Next step".

## Where we are

### What works today (verified 2026-09-16, all four configurations green)

- **The timeline composites on the GPU.** `Timeline::GetFrame` takes its canvas from
  `GpuSurfacePool`, and `Clip::draw_to_canvas` composites a clip onto it in one transformed draw —
  `apply_keyframes`' timeline-sized intermediate and `apply_background`'s full-frame composite both
  disappear. **All 16 blend modes** go through `SkBlendMode`. W12 and W13.
  - **Shadow and blur composite on the GPU too** (W14): both are one `SkImageFilter` chain on
    the paint, so a clip carrying either still draws in the single transformed draw. A clip now
    falls back to the QPainter path only for an overlay clip, the frame-number overlay, a
    waveform, or an effect that runs *after* the keyframes. Flip has been on the GPU since W12
    (`get_transform` applies it); crop never was a `Clip` concern at all — it is the `Crop`
    effect, so it belongs to W19–W21.
  - A frame composites on **one** path: the canvas is attached only when no clip would read the
    backdrop on the CPU. Why, and what it cost to learn, is in `GPU-DECISIONS.md`.
  - `Frame` can be GPU-backed; `GetImage()` does one cached readback and detaches, so every
    unported path keeps working. The Timeline flattens before returning, because a Graphite surface
    belongs to the thread that made it.
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
- `tools/golden.sh check` **295/295** (98 scenarios, 17 non-image checks) on CPU Skia and on GPU
  Skia with the GPU off, on Vulkan and on lavapipe — and in an `ENABLE_PLAYER=OFF` build, which
  carries no direct `Qt5Widgets` dependency. `openshot-gpu-checks` **8/8** on Vulkan and lavapipe,
  on the host *and inside a container* on both a GPU node and a CPU node; set `OPENSHOT_TEST_FONT`
  to a `.ttf` or its two subtitle checks skip. `openshot-gpu-blend-parity` clean on both backends.
- **The suite now gates what it can prove.** 76 of 95 scenarios are held bit-exact
  (`Tolerance::Exact()`); a scenario the GPU compositor moves is held bit-exact on the CPU and to
  the parity policy's "close" class only when a GPU is actually compositing (`gpu-composite` tag).
  Three blend modes carry a deliberately wide GPU band — read the caveat in `GPU-DECISIONS.md`
  before trusting them.

Compositor gains, measured interleaved at 1080p `render` on the A2000, GPU off against Vulkan:

| scenario | GPU off | Vulkan | |
|---|---|---|---|
| `blend_stack_5` | 15.9–17.4 | **38.5–43.2** | **2.5×** |
| `grid_3x3` | 17.5–20.5 | 24.6–27.3 | +34 % |
| `single_video` | 96.9–101.6 | 101.4–104.3 | +4 % |
| `podcast_pip` (W14) | 20.5–20.7 | **27.4–28.9** | **+40 %** |
| `heavy_effects` (W14) | 9.9 | **10.6** (was 9.0) | **+18 %** |

W14 turned `podcast_pip`'s −5 % into +40 % and `heavy_effects` from a GPU *loss* into a win, by
putting the clip shadow and blur on the paint instead of falling back. Neither reaches its gate, and
the remaining shortfall has one cause and one fix: every source image still crosses PCIe once per
clip per frame. That is **W22–W25**, and `grid_3x3`'s and `podcast_pip`'s 45 fps gates are both
carried there. The ceiling for W14 alone was measured at 33.5–35.0 fps on `podcast_pip` (guard
dropped, shadow not drawn), so 45 was never reachable in this item.

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

**Phase 2 and Stage 5 are both finished.** The remaining work is
`doc/gpu-migration/GPU-WORKLIST.md`. One item to a session; the worklist opens with the protocol.
**The order is now Stage 2 → Stage 3 → Stage 6**, by owner decision on 2026-09-18.

> **2026-09-16, project owner — no release work until the GPU migration is done.** **W01** (build and
> push the service image) and **W02** (the full post-merge benchmark, the `develop` merge gate)
> happen at the very end. Develop and test locally, without container images. **2026-09-18: they are
> no longer Stage 1 — both moved to Stage 10, at the end of the worklist**, so the file reads in
> execution order. The item IDs are unchanged; every cross-reference still says W01 and W02.

**W11, W12 and W13 are done** (2026-09-16). The four decisions the compositor bakes in are in
`GPU-DECISIONS.md` — canvas `kRGBA_8888` (overriding the F16 recommendation that was on file),
Graphite only, LUT matched to the front end at native cube size, nearest sampling kept.

### Next: Stage 2 — W03 (CI) and W04 (production payload corpus)

> **2026-09-18, project owner.** Stage 5 is finished and Stage 6 (effects as shaders) is the obvious
> next thing, but **Stage 2 and Stage 3 were skipped past and are to be closed first**, so
> everything up to the effects work is complete. Stage 1 has been **moved to the end as Stage 10** —
> nothing ships until the whole migration is done, so the release items now sit where they run.

**W04 is blocked, and the fix is small.** The corpus has **zero runnable payloads today**, not one:
the capture's signed URLs expired 2026-09-17, and although the media *is* archived in
`tmp/payloads/prod-2026-09-16/media/` with a `urls.txt` fileId→file map, the service's
`HTTPFileTransfer::download` opens the destination `"wb"` — truncate — on every attempt, with no
skip-if-exists. So pre-seeding the archive does not help and `render-payload` re-downloads into a
403. A **local-media mode in the service** (~half a day) fixes it, and it is what makes any payload
corpus durable rather than a 24-hour asset. It also unblocks **W05's gate**, which is measured
through `render-payload`.

**W03 is bigger than "add a golden step".** `.github/workflows/ci.yml` and `.gitlab-ci.yml` are
**upstream OpenShot's, unmodified** — no podcastle references, and they build upstream libopenshot,
not this fork (no Skia, no submodules, no pinned FFmpeg). The fork is on GitHub
(`podcastle-studio/libopenshot`), so this is GitHub Actions, but it means standing CI up rather than
extending it — and a runner that builds Skia from source per PR is not viable, so it needs a
prebuilt image or a cache.

**On generating scenarios from the payload.** Worth knowing before starting: W04 exists to catch
**JSON→timeline** regressions, which `tests/golden` structurally cannot see because it drives the
library directly through `Recipes.h` and never parses a payload. Decomposing the payload into golden
scenarios adds rendering coverage but loses exactly that property — the two are complementary.
Checked against the 98 existing scenarios, the payload's genuinely uncovered shapes are three: a
clip rotated 90° at opacity 0.25 **split across a trim boundary**; an **overlapping** transition
(`isOverlapping`); and a **`.mov` watermark with alpha** across the whole timeline. Everything else
it exercises — crop + corner radius, LUT, stacked filters, layer order, background colour — is
already covered.

### After Stage 2: Stage 3 — W05–W10, the CPU wins

Two are unblocked and worth taking first:

- **W07** (`Frame::GetImageCV` memoisation) — pure library, bench-gated (`transitions_chain` ≥ 33,
  `heavy_effects` ≥ 13). Its note says "W15 and W21 delete most callers"; **W15 is void**, so those
  callers are staying and the item is worth *more* than when it was written.
- **W08** (reader copies + threaded swscale) — pure library, bench-gated (`source_4k` ≥ 70,
  `single_video` ≥ 125). Carries a real ASan requirement.

W05's code is straightforward but its **gate** needs the payload fix above. W06 wants a
cgroup-limited container to validate honestly. W09 needs VMAF tooling that has never been run.
**W10 should be skipped** — it is explicitly optional and W25 replaces the code entirely.

### Then: Stage 6 — W19–W21, effects and transitions as shaders

**Read `TRANSITION-PARITY.md` first.** W20 is the item that ends editor/export
identity-by-construction, and it carries an open product decision (parameter reference resolution)
plus a measured bug that exists today. `GPU-DECISIONS.md` has both in its open list.

### Recently finished — W17 and W18 (kept for the reasoning)

**W17 and W18 are both done, and nothing is half-written.**

**W17's owed fps number was measured on mains power** (2026-09-18) and the gate is met, with a
caveat worth carrying: `subtitles_words` on Vulkan read a median **118.8 fps** over 12 runs against
a 120 fps gate, best 123.2, with the `OPENSHOT_GPU=off` arm at ~98. The gate clears in the better
half of the runs and misses by ~1 % at the median, and the spread is the host (a browser and two
IDEs were running; the unchanged `off` arm is just as noisy). Two things make the pass credible: the
pre-W17 quiet-window reading was 120.0 / 121.3 / 125.0, and W17 measured +3.5 % interleaved.

**A finding that applies to every gate in the worklist, not just this one.** The 150-frame
measurement window charges ~0.6–0.8 s of one-time warm-up (first decode, font load, cache fill), and
it is **not** GPU-specific — both arms show it. Solving the 150- against the 300-frame window gives
`subtitles_words` a steady-state **~227 fps on Vulkan against ~195 with the GPU off**. A real export
runs thousands of frames and sees the steady rate, so the recorded fps understates throughput —
here by about 90 % — while overstating the GPU's advantage (16 % steady-state against 22 % at 150
frames). Details in `doc/PERFORMANCE-BASELINE.md`, 2026-09-18.

**W18 was scoped to the render path** (2026-09-18) after its written gate turned out to be
unsatisfiable: it requires `QPainter` to survive only behind `ENABLE_LEGACY_EFFECTS`, and **that
flag exists nowhere in the tree**. All 20 `QPainter` files under `src/` are the CPU fallback the
standing constraint requires be kept — W15's wall for the third time. Three more of its sub-tasks
had no subject: `ENABLE_MAGICK` already existed, `ColorMap` is an effect that loads a `QImage`
rather than parsing `.cube` text, and `Timeline`'s `QDir`/`QRegularExpression` block is the
project-file path rewriter, which the service never calls. What was delivered:

- **`ENABLE_PLAYER`** (default ON). OFF drops the player, `Frame::Display`, and the **Widgets** Qt
  component; the golden suite is green in that build and `libopenshot.so` has no direct
  `Qt5Widgets` dependency. It still arrives transitively through `libQt5Svg`, so this removes our
  dependency on it, not the .so from the image.
- **`Color` parses without `QColor`**, and `Color.h` no longer includes Qt at all. Proven identical
  before being trusted: 144,559 hex/named inputs against `QColor`, 60,020 `rgb()`/`rgba()` inputs
  against the old code, 200,000 `GetColorHex` against `QColor::name()` — zero mismatches.
- **The Timeline's GPU background clear builds no Qt type** and no longer round-trips a hex string
  every frame.
- **Two coverage gaps closed.** `unit.color` in the golden suite (proven to have teeth by injecting
  a fault), and two new scenarios for the timeline background colour — **nothing in the 292-frame
  suite had ever set one**, so the branch W18 changed was entirely uncovered. They also pin
  CPU/GPU parity for it: background pixels are bit-identical on both paths.

The `QString`/`QDir`/`QFile` rewrites in `Profiles`, `ChunkReader/Writer`, `effects/ColorMap` and
Timeline's path rewriter were **deliberately left**: none is on the render path, none is reached by
the service, and the diff is large with no measurable win. They are ordinary housekeeping now, not
GPU-migration work.

### W17 — subtitles and text into the timeline canvas · legacy `3.6` (done 2026-09-17)

Read the item in the worklist. W14 is closed, W15 and W16 are void; nothing is half-finished and
there is no in-progress state to resume or revert.

**Why W17 is real when the two before it were not.** Its own note explains it: the capability has
existed since Phase 2 (`SubtitleManager::renderAtFrame(SkCanvas*, w, h, frame)`, bit-identical on
GPU and raster), and the Timeline passed a raster canvas on purpose because a full-frame round trip
cost 5.6 ms at 1080p against 0.27 ms of drawing. **W12 landed, so the frame is already on the GPU
and that arithmetic inverts.** The item is now mostly deleting the raster wrapper and letting the
Timeline hand over its own canvas, for both subtitles and the text reader. Gate:
`subtitles_words` ≥ 120 fps, against ~113 measured at W13. Nothing blocks it.

**Stage 5's Qt items were written before the standing constraint and before W13**, which is why
two in a row came out void: they assume Qt code gets rewritten and deleted, and the constraint
requires the CPU path be kept and gated instead. **W18 is mixed** — its build options and the
`QString`/`QDir`/`QColor` work in `Timeline`, `Profiles`, `ColorMap` and `ChunkReader/Writer` are
genuine (none of those is the CPU render fallback), but its "no `QPainter` on the render path" gate
hits W15's wall unless the answer is gating rather than deletion. Read it accordingly.

**Three things carried forward, all recorded rather than forgotten:**

1. **Three 45–50 fps gates now sit on W22–W25**, all with the same single cause — every source
   image crosses PCIe once per clip per frame. `grid_3x3` (45, at ~26), `podcast_pip` (45, at
   27.4–28.9 after W14, with W14's own ceiling measured at 33.5–35.0) and `blend_stack_5` (50, at
   ~41–42, unreachable from W15 by construction). W22–W25 is the item that has to clear all three,
   and it should be sized accordingly.
2. **Taking Qt off the render path is W16–W18, and it is gating, not deletion.** `src/Timeline.cpp`
   is already QPainter-free; `src/Clip.cpp`'s 15 references are all the CPU fallback. The GPU path
   still uses Qt as the image container (`QImage` from `frame->GetImage()`) and `QTransform` to
   build the matrix — that is the part W16–W18 can actually remove.
3. **Crop's `QPainterPath` → `clipRRect` is an effects port**, not a `Clip` one —
   `src/effects/Crop.cpp`, W19–W21. The Qt inventory table in `GPU-RENDER-PLAN.md` §2 lists it
   against `Clip.cpp` in error.

**After W17:** W18 (readers on
Skia, subtitles into the timeline canvas, Qt off the render path), W19–W21 (effects as shaders),
**W22–W25 (frames stay on the GPU — this is where `grid_3x3`'s 45 fps gate and the `podcast_pip`
regression are settled, because it removes the per-clip upload)**, W26–W28, W29–W31. W03/W04 (CI,
production corpus) remain open; W05–W10 are the CPU quick wins. W01/W02 stay deferred.

## Known oddities worth a look

- **Text is visibly different on the GPU.** The `text.*` scenarios are not bit-exact between the CPU
  and GPU paths — worst case 106 LSB (`text.curved`, PSNR 39.99) — and pass only because `Text.cpp`
  puts them all on `Tolerance::Loose()` (PSNR ≥ 38) for glyph anti-aliasing. Expected rather than
  broken, but "292/292 four ways" reads as a stronger claim than it is: for text the two paths agree
  only to PSNR ≥ 38. Everything that is *not* text is now gated bit-exact (see the log entry below).
- **The suite's alpha test image was fully transparent until W14.**
  `tests/golden/media/image_alpha_320x200.png` was generated with ffmpeg `drawbox`, which blends RGB
  and never writes the alpha plane, so the file carried its two boxes at alpha 0. Every scenario
  using it rendered nothing but its background: `clipfx.shadow`, `clipfx.shadow_colored_sharp` and
  `clipfx.shadow_blur_rotated` gated an empty frame, which is why the clip shadow went untested
  through W12 and W13 and why a bright red probe shadow changed no pixel. `generate.sh` now builds
  it with `geq` (verified byte-reproducible) and the four affected scenario groups — those three
  plus `readers.image_png_alpha`, `compositing.blend_with_png_alpha` and `export.roundtrip_x264` —
  were re-baselined on the CPU path with every triptych reviewed. **Worth a sweep for others like
  it:** a golden whose frame is only background proves nothing.

- `effects.stack_crop_chroma_light_lut` golden shows harsh white blotches (ChromaKey + Light + LUT
  stacked). Baseline as-is; may be a real rendering quirk.
- Export round trip live-vs-decoded is ~28 dB on the noisy test pattern with no colour bias:
  x264 loss, not a matrix bug.

## Log

- 2026-09-18 — **Order changed by the project owner, and Stage 1 moved to the end.** Stage 5 is
  finished, but Stages 2 and 3 were skipped past on the way to the compositor. They are to be closed
  before Stage 6 (effects), so everything up to the effects work is complete. Stage 1's two release
  items (W01, W02) are now **Stage 10**: nothing ships until the whole migration is done, so they
  now sit where they actually run. IDs unchanged. Two things were lifted out of W02 and into
  `GPU-DECISIONS.md`'s open list rather than being lost with the move: the **`compositing.layer_order`
  question** (insertion-stable clip sort vs address-tie-broken — a behaviour change that wants an
  answer long before a release benchmark), and, from the transition-parity work, the **reference
  resolution for length-valued effect parameters**. Two blockers found while scoping Stage 2:
  **W04 has zero runnable payloads**, because the capture's signed URLs expired and
  `HTTPFileTransfer::download` truncates the destination on every attempt, so the archived media in
  `tmp/payloads/` cannot be used — a local-media mode in the service fixes it and also unblocks
  W05's gate; and **W03 has no CI to extend**, because `.github/workflows/ci.yml` and
  `.gitlab-ci.yml` are upstream OpenShot's, unmodified, and build upstream rather than this fork.

- 2026-09-18 — **W17's fps gate closed, and W18 done — scoped to the render path because its
  written gate is unsatisfiable.** W17: on AC, `subtitles_words` on Vulkan reads a median **118.8
  fps** over 12 runs (best 123.2) against a 120 fps gate, with the `off` arm at ~98. It clears in
  the better half of the runs and misses by ~1 % at the median; the spread is the host, and the
  unchanged `off` arm is just as noisy. Alongside it, a finding that applies to **every** gate in
  the worklist: the 150-frame window charges ~0.6–0.8 s of one-time warm-up on *both* paths, so
  solving the 150- against the 300-frame window gives a steady state of **~227 fps on Vulkan
  against ~195 off** — the recorded numbers understate throughput (here by ~90 %) and overstate the
  GPU's edge (16 % steady-state against 22 %).
  W18: **four of its sub-tasks had no subject.** `ENABLE_LEGACY_EFFECTS`, which its gate requires
  `QPainter` to hide behind, **exists nowhere in the tree**; all 20 `QPainter` files under `src/`
  are the CPU fallback the standing constraint keeps. `ENABLE_MAGICK` already existed, `ColorMap`
  is an effect loading a `QImage` rather than a `.cube` parser, and `Timeline`'s
  `QDir`/`QRegularExpression` block is the project-file path rewriter the service never calls.
  Delivered: **`ENABLE_PLAYER`** (OFF drops the player, `Frame::Display` and the Qt **Widgets**
  component — though `libQt5Svg` still pulls Widgets in transitively, so this removes our dependency
  on it, not the .so); **`Color` without `QColor`**, with `Color.h` no longer including Qt at all,
  verified differentially before being trusted (144,559 hex/named inputs against `QColor`, 60,020
  `rgb()`/`rgba()` against the old code, 200,000 `GetColorHex` against `QColor::name()` — **zero
  mismatches**, and the quirks preserved: unparseable is opaque black, `"rgbx(1,2,3)"` parses as
  CSS); and **the Timeline's GPU background clear builds no Qt type** and no longer round-trips a
  hex string per frame. Two coverage gaps closed: `unit.color` in the golden suite (proven to have
  teeth by flipping one byte of the colour table — 4 of its 5 checks fail), and two scenarios for
  the timeline background colour, because **nothing in the 292-frame suite had ever set one** and
  the branch W18 changed was therefore completely uncovered. Background pixels come out
  bit-identical on CPU and GPU; only the clip area differs (2–3 LSB of Skia resampling). Golden
  **295/295 four ways**, three new frames baselined and no other golden touched;
  `openshot-gpu-checks` 8/8 on both backends with `OPENSHOT_TEST_FONT` set; blend parity clean.
  The `QString`/`QDir` rewrites in `Profiles`, `ChunkReader/Writer` and `effects/ColorMap` were
  **deliberately left** — off the render path, unreached by the service, large diff, no win.

- 2026-09-17 — **W17 done: subtitles and text frames both composite on the timeline's GPU canvas.**
  Three commits. **Both of the item's premises were wrong, measured before implementing.** (1) There
  was no round trip to delete: the subtitle block's `GetImage()` *is* `FlattenGpuFrame()`, the same
  readback the Timeline performs a few lines later, so the frame crossed once either way. What W17
  saves is the drawing — `single_video` is `subtitles_words` minus the subtitles, and differencing
  them gives **0.62 ms of an 8.19 ms frame**. (2) The round trip is **1.62 ms at 1080p** on the
  A2000, not the 5.6 ms on file, which was an iGPU number. And (3) **the gate was already met at
  HEAD**: `subtitles_words` 120.0/121.3/125.0 fps on Vulkan against `single_video` 131.7/130.1/134.3,
  interleaved; the ~113 on file was stale. A **latent bug** blocked the naive change and is the
  first commit: `parseColorString`'s R/B swap cancels only at the QImage boundary, and the timeline
  canvas is `kRGBA_8888` read back as `kRGBA_8888`, so handing it over unchanged exchanges red and
  blue. The swap is now `ColorConvention` — a property of the output boundary, not of the canvas,
  which is what the glow already proved by drawing parsed colours onto an RGBA8888 surface — and a
  new `subtitle-colors` check guards it (0.024 mean error against 2.732 if swapped). The text half
  needed three fixes nobody had hit yet: `Frame::DeepCopy` dropped `gpu_frame` (every reader frame
  is copied by `Clip::GetOrCreateFrame`, so it would have composited blank), `GetPixels`/
  `GetImageCV`/`SetImageCV` would have handed back or kept a stale black frame, and `get_transform`
  writes the opacity curve into pixels a texture does not have (it now hands the value to the paint).
  Golden **292/292 four ways with no re-baseline**; the three `subtitles.*` scenarios take the
  `gpu-composite` tag as expected (worst 55.4 dB / SSIM 0.9999, sub-pixel edges in the subtitle band;
  mean error 0.037 LSB against the CPU goldens, 111.5 against an R/B-swapped version).
  **Owed: the absolute fps gate on mains power.** The measurement window ran on battery, which caps
  the machine to a third of its AC speed; interleaved pre/post ratios from it are valid
  (`subtitles_words` +3.5 %, `text_animated_glow_3` +8 %, `everything` flat) and are in the worklist
  item with instructions for recreating the comparison build. Machine notes for the next session:
  `stills_tests` drove load averages to 9–20 for part of the day, self-inflicted builds pushed the
  package to 80 °C, and the battery cap was the largest effect of the three — check
  `/sys/class/power_supply/AC*/online` before trusting any number.

- 2026-09-17 — **W16 void: the image and SVG readers stay on Qt, and no code changed.** Examined and
  closed without an edit. The item has **no throughput to win**: `QtImageReader` caches the decoded,
  scaled image in `cached_image` (invalidated only on a `max_size` change), so an image decodes once
  per reader and the per-frame cost is a `shared_ptr` copy. Its three sub-tasks: EXIF orientation is
  **already honoured** (`setAutoTransform(true)`, `QtImageReader.cpp:82`); **Skia's SVG DOM is not
  in this build** — `libskia.a` has only `SkSVGCanvas`/`SkSVGDevice`, the SVG *writer*, and
  `modules/svg` is neither built nor installed, so adding it means editing both build scripts and
  the installer against `CLAUDE.md`'s rules, while resvg is an already-optional path that is off
  (`HAVE_RESVG=FALSE`); and replacing the reader is **cross-repo on the shipping path**, since the
  service constructs `openshot::QtImageReader` by name twice and `ShapeRenderer` documents that its
  sizing matches QtImageReader's SVG sizing. Checked the one thing that could have been a real bug:
  the service's whole SVG surface is `<path>`/`<circle>` with basic paint attributes, and Qt
  **honours `preserveAspectRatio="none"`** — tested, a 100x100 viewBox in a 200x100 raster fills all
  200x100 — so the documented contract between the repos holds. Gate not runnable either: it wants a
  40-asset production corpus that does not exist. **Next is W17, which is real work** for the reason
  its own note gives; W18 is mixed. Machine was quiet for this session (load ~1.3), but no
  benchmark was needed.

- 2026-09-17 — **W15 void: `BlendModes.cpp` stays, and no code changed.** Examined and closed
  without an edit. `BlendImages()` has two live users — `Clip::apply_background`, which is the CPU
  implementation of all 15 non-normal modes, and `openshot-gpu-blend-parity`, which uses it as the
  **oracle** `SkBlendMode` is validated against, so deleting the file would delete the GPU's own
  correctness test. Neither `GetImageCV` call in `Clip.cpp` is dead either (overlay clips, CPU clip
  blur). Its gate: `grep -c QPainter src/Timeline.cpp` is already 0, and `src/Clip.cpp`'s 15 hits
  are all CPU fallback, so reaching 0 means deleting the path the constraint protects;
  `blend_stack_5` ≥ 50 fps is unreachable here **by construction** — W13 put every mode on
  `SkBlendMode` and `apply_background` is guarded by `!drawn_on_canvas`, so `BlendImages()` is not
  on the GPU path at all and no edit to it can move a Vulkan number. Measured ~41–42 fps, gate
  carried to **W22–W25**, which now owns three 45–50 fps gates with one shared cause. One genuinely
  dead symbol found (`BlendPixel`, no caller anywhere including the two consumer repos) and
  deliberately left: it is public library API and removing it gains nothing measurable — the owner's
  call. Also: the machine was carrying heavy unrelated load all session (load average 9–20, one
  `blend_stack_5` run swinging 20→48 fps), so **no new benchmark from today should be trusted as a
  baseline**; the conclusion above rests on the code structure, not on those numbers.

- 2026-09-16 — **W14 done: the clip shadow and blur composite on the GPU.** Both become one
  `SkImageFilter` chain on the paint (`Blur` with `kClamp` + a source-rect crop, then `DropShadow` —
  not `DropShadowOnly`, so the one filter draws shadow-then-clip in the CPU path's order), and
  `shadow`/`blur` leave `can_draw_to_canvas`. `sigma_for_box()` is now shared by the CPU
  `cv::GaussianBlur` and the GPU filter, so the two blur by the same amount.
  **`podcast_pip` 18.7–21.1 → 27.4–28.9 fps (+40 %)**, `heavy_effects` 9.0 → 10.6 on Vulkan (+18 %,
  and now ahead of its own CPU path instead of behind it). Quality well inside the gate: shadows
  SSIM ≥ 0.9998 / PSNR 58.9–74.5 dB, blur PSNR 43.2–59.6 dB on a new `Tolerance::GpuBlur()`
  (40 dB / 0.995, tag `gpu-blur`). Four-way sweep 292/292, `openshot-gpu-checks` 7/7 and
  `openshot-gpu-blend-parity` clean on Vulkan and lavapipe, CPU path bit-identical.
  **Three premises in the worklist item were wrong and are corrected there:** flip was already on
  the GPU (W12's `get_transform`); crop is not a `Clip` concern at all but the `Crop` effect, so its
  `clipRRect` belongs to W19–W21; and the three CPU helpers it said to delete are the no-GPU path,
  which the standing constraint forbids deleting. **The 45 fps `podcast_pip` gate is missed and
  carried to W22–W25** — measured, not assumed: with the guard dropped and the shadow not drawn at
  all, this item's ceiling is 33.5–35.0 fps, so the rest is the per-clip PCIe upload.
  **Separately, found that the suite's alpha test image had always been fully transparent**, which
  had left the clip shadow completely ungated — see "Known oddities".

- 2026-09-16 — **Session close.** W11, W12 and W13 all landed (see the three entries below); golden
  green 292/292 four ways, `openshot-gpu-checks` 7/7, `openshot-gpu-blend-parity` clean on Vulkan
  and lavapipe, working tree clean. **Next session starts at W14** (blur/shadow/crop/flip on the
  paint) — see "Next step" for what to do, its gate, and the two things to expect. Nothing is
  half-finished; there is no in-progress state to resume or revert.

- 2026-09-16 — **W13 done: every blend mode composites on the GPU.** The 15 non-normal modes move
  from `BlendImages()` to `SkBlendMode`, a mapping verified by the new `openshot-gpu-blend-parity`
  (identical pixels, no resampling: all 16 agree on lavapipe, 13/16 within 1 LSB on NVIDIA, the
  outliers being driver float at the division singularities on 0.001–0.006 % of channels).
  **`blend_stack_5` 16.6 → 41.3 fps (2.5×)**, `grid_3x3` +34 % to ~26 fps — short of the 45 fps
  gate, which is **carried to W22–W25** because every source still crosses PCIe once per clip per
  frame. `color_burn`/`hue`/`saturation` take a deliberately wide GPU tolerance on the owner's
  decision; the cost — that band cannot catch a regression in them — is recorded in
  `GPU-DECISIONS.md`, with the parity test as the real guard. Four-way sweep 292/292, CPU untouched.

- 2026-09-16 — **W12 done: the timeline canvas is on the GPU, and qualifying clips composite onto
  it.** `Frame` gains an optional `GpuFrame` (one cached readback in `GetImage()`, detached after);
  `Timeline::GetFrame` attaches a pooled surface; `Clip::draw_to_canvas` collapses
  `apply_keyframes` + `apply_background` into one transformed draw. **`grid_3x3` 19.6 → 27.1 fps
  (+38 %)**, `single_video` +4 % (the gate: not slower), **`podcast_pip` −5 %** — accepted and
  recorded, the per-clip PCIe upload is W22–W25's to remove. Four-way sweep 292/292, CPU path
  bit-identical. Two findings worth the reading: a frame must composite entirely on one path (a CPU
  blend mode reading a GPU backdrop took `blend_color_burn` to 26.68 dB / max 255), and QPainter
  reduces a translate-only transform to an integer blit (always resampling cost text ~10 dB, because
  a text clip's transform is a *half*-pixel translation). The worklist's own premise was wrong in
  two places and is corrected there: `add_layer` copies audio and does not composite, and W12
  without a GPU consumer is a measured 16 % regression, which is why the fast-path slice of W13 was
  pulled forward.

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
