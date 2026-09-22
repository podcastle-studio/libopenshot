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

Last updated: 2026-09-22 (Stage 6 complete; **W22 done**; **W23 part done** — 1.5's crash fix, the buffer pool, and the SkSL YUV→RGBA pass built and flagged off; NVDEC feeding it is what is left) · branch `feature/gpu-rendering`.
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
**A resuming session starts W19** (`GpuEffect` and the per-pixel shaders), minus its ColorMap
shader. Stage 2 and Stage 3 were reopened on 2026-09-18 by owner decision — Stage 6 (effects) is the
obvious next thing, but the safety net and the CPU wins had been skipped past. Of those, **W04's
mechanism, W05, W07, W08 and now W09 are done**, and **nothing is left in Stages 2 or 3 that this
machine can finish unaided**: the remainder is waiting on the project owner or on a container. See
"Start here" under "Next step".
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

- ~~Timeline canvas precision for the GPU compositor: RGBA8 or RGBA16F~~ — **taken in W11**:
  `kRGBA_8888`, overriding the F16 recommendation. See `GPU-DECISIONS.md`.
- ~~Reference for LUT rounding: native `ColorMap.cpp` or the WASM `LutApply.cpp` path~~ —
  **answered 2026-09-22**: neither. The front end never calls the WASM for LUTs; it grades in a
  PixiJS GLSL pass, trilinear at the cube's native size, honouring `DOMAIN_MIN`/`DOMAIN_MAX`. See
  the log entry below.

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
**The order is Stage 2 → Stage 3 → Stage 6**, by owner decision on 2026-09-18.

> ## Start here (2026-09-18, end of session)
>
> **W09 is done** — see Stage 3 below. With it, **Stages 2 and 3 hold nothing else this machine can
> finish unaided**; everything still open there is waiting on the project owner or on a container,
> and the list is under "Blocked on the project owner" below.
>
> **Superseded 2026-09-22: Stage 6 (W19, W20, W21) is done**, ColorMap and all ten transition
> variants included. See the Stage 6 section below and the top of the log. The next work is
> **W22–W25**, the per-clip PCIe crossing — of which **W22 is now done too** (2026-09-22, gate met
> at 0.166 ms per 4K frame). **A resuming session starts W23**, and its first problem is the one
> already on file: hardware decode throws on the first frame in this fork, and plan step 1.5 is
> the fix.
>
> **Shader language decided 2026-09-18: SkSL, one source, both sides.** The server runs it as
> `SkRuntimeEffect`, the front end as `CanvasKit.RuntimeEffect` — CanvasKit is already shipped there
> for text and glow, and our Skia is pinned to m147 to match it. This supersedes the two-emitter
> (SkSL + PixiJS GLSL) design in `TRANSITION-PARITY.md` and shapes W19 as well as W20. **It rests on
> one thing the front-end team has to confirm before the first shader is written: that they can put
> video frames through CanvasKit, not only text.** See `GPU-DECISIONS.md`.
>
> **One thing W09 left on the owner's desk, and it is small but real.** The matching change in
> `../video-rendering-service` (`VideoRenderingImpl.cpp`: the NVENC branch now asks for `crf 18`
> instead of `rc vbr` + `cq 19`) is **edited in the working tree and not committed**, next to the
> vendored-libopenshot commit that was already local and unpushed. It changes what a hardware
> export looks like — same quality, less than half the bytes — so it is a product-visible change,
> not a refactor. It also needs the rebuilt library vendored before it does anything.

> **2026-09-16, project owner — no release work until the GPU migration is done.** **W01** (build and
> push the service image) and **W02** (the full post-merge benchmark, the `develop` merge gate)
> happen at the very end. Develop and test locally, without container images. **2026-09-18: they are
> no longer Stage 1 — both moved to Stage 10, at the end of the worklist**, so the file reads in
> execution order. The item IDs are unchanged; every cross-reference still says W01 and W02.

> **Stage 6 has its own map: `doc/gpu-migration/STAGE6-EFFECTS.md`.** Every effect the service
> uses, from all three call sites (per clip, transitions, animation), with its state — ported,
> partial and what was excluded, ruled out and why, or blocked and on whom — plus what is left in
> order. Read that instead of reconstructing it from the log below.

**W11, W12 and W13 are done** (2026-09-16). The four decisions the compositor bakes in are in
`GPU-DECISIONS.md` — canvas `kRGBA_8888` (overriding the F16 recommendation that was on file),
Graphite only, LUT matched to the front end at native cube size, nearest sampling kept.

### W09 — Writer: nvenc rate control · legacy `1.4` remainder (done 2026-09-18)

**Gate met on all three clauses:** VMAF **−0.21** against x264 (98.018 vs 98.230, allowance −2),
file size **+1.7 %** (allowance ±20 %), `single_video` nvenc **122.4 → 122.3 fps**. Four-way golden
sweep 295/295. The method, the sweeps and the tables are in `doc/PERFORMANCE-BASELINE.md`; the
decision is in `GPU-DECISIONS.md`.

**The item's own sub-tasks were not where the problem was.** Before anything was touched the size
half of the gate failed at **+112 %**: `cq 19` — which shipped as "a conservative starting point"
and which no sub-task named — was spending **2.1× the bits of libx264 crf 18 for 0.16 VMAF points**.
Neither `b_ref_mode` nor `spatial-aq` could have closed a gap that size.

- **`crf` now means something on NVENC.** It was discarding the caller's value and inventing a
  bitrate from `info.video_bit_rate`; it now maps to `rc=vbr`, `cq = crf + 10` (clamped 0–51) and
  `bit_rate = 0`. The offset was calibrated at crf 18 / 23 / 28 (the answer is +9 to +10 across the
  whole range) and checked on three scenarios. One quality knob for both encoders, and one place
  that owns the calibration.
- **`spatial-aq 1` is the sub-task that paid**: 3.09 MB at VMAF 98.17 with it against 3.49 MB at
  97.69 without — smaller *and* better, so it is a writer default now.
- **`b_ref_mode middle` is a no-op at these settings** (byte-identical output; `p5`/`hq` already
  picks it) *and* was unreachable. Chasing it found a live bug: `add_video_stream` asks for
  `max_b_frames = 10`, NVENC's H.264 limit is 4, so `allow_b_frames 1` threw `InvalidCodec` out of
  `avcodec_open2`. Clamped; the option works now.
- **Zeroing the bitrate is correctness, not a win.** `-b:v 10M` and `-b:v 0` give byte-identical
  files once `rc vbr` and `cq` are set.
- **B-frames stay off by default** — a wash at matched `cq` (−2.3 % size for −0.08 VMAF).
- `openshot-bench` gained a **`lossless` mode**, because the gate needs a common reference and there
  was no way to write one. It is not a timing case.

**`tests/golden/Recipes.cpp` had drifted from the service** and was measuring a configuration
production never runs (`preset p4` alone, against the service's `rc vbr, cq 19, p5, hq`). Both now
say `preset p5`, `tune hq`, `crf 18` and the BT.709 tagging. The service-side edit is **uncommitted**
— see "Start here".

### Blocked on the project owner (nothing here can proceed without an answer)

1. **Five more payload captures** (W04) — text animations, subtitles, a transition-heavy timeline,
   chroma key, a 4K source. Needs fresh exports from the app, and **the media must be archived the
   same day** or the capture is dead on arrival (signed URLs last 24 h). The mechanism works and the
   first payload is green; this is collection, not code.
2. **Two CI decisions** (W03) — the pinned FFmpeg lives in a private base image this machine cannot
   pull (the same blocker W01 carries), and the private submodule needs a deploy key. The workflow
   is written and its YAML validated but has **never run**; enabling it and opening the
   deliberate-regression PR its gate asks for are actions on the GitHub repo.
3. **W07's fps gates want restating** — they are arithmetically unreachable by W07 (the scenarios
   need 749 ms and 1276 ms; the whole conversion cost was 476 ms and 443 ms). Deliberately left
   unchanged.
4. **W05's flag stays off** until the 1/s progress cadence is watched somewhere Pub/Sub and Redis
   answer. `render-payload` points both at dead addresses, so that half of the gate cannot be
   checked here at all.
5. **The transition-parity decisions** (`TRANSITION-PARITY.md`) — these gate **W20**, and one of
   them is a product call: fixing the blur radii's missing reference resolution shifts existing
   projects, so it needs versioning or acceptance.
6. **`compositing.layer_order`** — insertion-stable clip sort versus address-tie-broken, in
   `GPU-DECISIONS.md`'s open list.
7. **The vendored libopenshot refresh** in `../video-rendering-service/cpp-third-party` was
   committed **locally and unpushed** (the service would not build without it). Push it, or revert
   both it and the W05 commit.
8. **W09's service-side edit** — `VideoRenderingImpl.cpp`'s NVENC branch now asks for `crf 18`
   instead of `rc vbr` + `cq 19`. **Edited in the working tree, not committed.** It halves the size
   of every hardware export at the same measured quality, which is product-visible; it also needs
   the rebuilt library vendored (item 7) before it changes anything.

### Needs a container, not a decision

**W06 thread budgets** — deriving `FF_THREADS`/`OMP_THREADS` from `/sys/fs/cgroup/cpu.max` can be
written here, but its gate ("in an 8-CPU container with 2 processes, no process exceeds ~400 % CPU")
cannot be validated on this box. The item also warns that the upstream merge brought overlapping
thread settings — **check what landed before writing anything**.

**W10 is skipped**, as the item itself recommends: optional, and W25 replaces the code entirely.

**W04's mechanism is done and its first payload is green** (2026-09-18). What was wrong when the
session started: **the corpus had zero runnable payloads, not one.** A capture's signed `fileUrl`s
expire 24 h after issue, and although the media was archived at capture time,
`HTTPFileTransfer::download` opens its destination `"wb"` on every attempt and re-fetches
unconditionally — so pre-seeding the archive did nothing and `render-payload` downloaded into a 403.

- **`MediaFetch` in the service** replays a capture offline: `RENDER_MEDIA_CACHE=<dir>` serves each
  file from the archive keyed by the URL's basename, and `RENDER_MEDIA_CACHE_STRICT=1` makes a
  missing entry an error rather than a silent network fetch. It covers all six fetch sites,
  including the **Redis-cached path** used for clip media and sound effects, which is separate from
  the plain download and is what the first attempt missed. Unset — every production run — behaviour
  is unchanged. **This also unblocks W05's gate**, which is measured through `render-payload`.
- **`tests/payloads/run-corpus.sh`** runs two rounds per payload, hashes the decoded frames with
  `ffmpeg -f framemd5`, and compares round-to-round and against a recorded hash.
- **First payload green:** `prod-2026-09-16-pip-lut-whoosh`, **750 frames, both rounds identical**,
  hash recorded and re-verified through the `check` path. ~8 min per round in a **Release** build of
  `render-payload`; Debug is ~25x slower and makes this impractical.
- The archive was incomplete — the WHOOSH transition's `whoosh.opus` sound effect had never been
  saved. Now fetched, and the README calls sound effects out because their URLs are unsigned and
  easy to miss.

**What is left in W04: five more captures** — text animations, subtitles, a transition-heavy
timeline, chroma key, a 4K source. That needs fresh exports from the app, and **the media has to be
archived the same day** or the capture is dead on arrival. This is the one part of Stage 2 that
cannot be done from this machine alone.

**W03 is "stand CI up", not "add a step".** `.github/workflows/ci.yml` and `.gitlab-ci.yml` are
upstream OpenShot's, unmodified, and build upstream libopenshot — no Skia, no submodules, no pinned
FFmpeg. `.github/workflows/golden.yml` is written (submodules, apt deps, `libopenshot-audio` pinned
by tag, Skia built from source and cached on the build script's hash, `ENABLE_PLAYER=OFF`, the
suite, the report as an artifact) but **has never run**, and two things need a decision first:

1. **FFmpeg.** The dev box and the runtime image both use an FFmpeg 6.1 built `--enable-nvenc` in
   `/usr/local`, from the private base image `cuda12.8.1-cudnn9.7.1-ffmpeg6.1-nvidia24.04`. The
   workflow falls back to apt's 6.1.1 because that registry is not authenticated here — **the same
   blocker W01 carries, which the worklist never recorded as shared**. `export.roundtrip_x264`
   compares decoded frames to committed goldens at PSNR ≥ 45 dB, so a different x264 may fail it,
   and that would be a toolchain difference rather than a regression.
2. **The private submodule** clones over SSH, so CI needs a deploy key or an HTTPS+token rewrite.

Enabling the workflow and opening the deliberate-regression PR that W03's gate asks for are actions
on the GitHub repo, so they are the project owner's to take.

### Stage 3 — W05, W07, W08, W09 done (2026-09-18); W06 left, W10 skipped

**All four measured their premise before implementing, and in every one of them part of the
item's plan did not survive the measurement.** That is now the expected outcome often enough to
treat "measure first" as the protocol rather than the exception. W09 is the sharpest case: all four
of its sub-tasks were real, and none of them was the thing that made its gate fail.

| item | result | gate |
|---|---|---|
| **W07** `GetImageCV` memoisation | `SetImageCV` converted twice; now once. 336 → 102 ms on `transitions_chain`, 1.04 → 0.32 ms a call. `transitions_chain` **27.4 → 28.7 fps (+4.8 %)**, `heavy_effects` within noise. | fps gates **unreachable by this item** — see below |
| **W08** reader copies | `memset` dropped; `av_image_copy` → `av_frame_ref`. `source_4k` **61.3 → 70.1 (+14.4 %)**, `single_video` **116.6 → 124.9 (+7.1 %)**. ASan clean. | `source_4k` ≥ 70 **met**; `single_video` ≥ 125 **0.1 % short** |
| **W05** one `WriteFrame` | Built and working, **flag defaults OFF**. ≈ −6 % against a ≥ 15 % gate. | **not met** — flagged off, not reverted |
| **W09** nvenc rate control | `cq 19` was 2.1x libx264's bytes for +0.16 VMAF. `crf` now maps to `cq = crf + 10`; `spatial-aq` on; `allow_b_frames` no longer throws on NVENC. **+112 % → +1.7 %** size at **−0.21** VMAF. | VMAF, size and fps all **met** |

**What was rejected on measurement rather than skipped**, all recorded in the worklist items:
the `imagecv` dirty-flag cache (access is strictly alternating — 300 Get against 300 Set — so the
flag is dirty on essentially every Get); reusing `imagecv`'s buffer (`cv::Mat::create` reuses an
allocation *regardless of refcount*, so it would corrupt any Mat a caller still held); and
**threaded swscale** (the option works, but with decoder threads held fixed `source_4k` measured
663.8 / 674.0 / 650.0 / 682.9 ms at 1/2/4/8 threads — memory-bandwidth bound, not compute bound).

**W07's fps gates are unreachable by W07.** `transitions_chain` needs 749 ms to reach 33 fps and
`heavy_effects` 1276 ms to reach 13; the *entire* conversion cost was 476 ms and 443 ms. Deleting
100 % of it gives 30.8 and 12.1. The targets predate the compositor work that already moved these
scenarios. **This needs restating by the project owner** — nothing has been changed about it.

**A bug of this session's own making, found and fixed.** W18 put `previewApp` behind
`USE_QT_PLAYER`, but it is a data member of the public `Frame` class: `sizeof(Frame)` was **256
without the macro and 272 with it**, and `../video-rendering-service` compiles these headers without
it while linking a player-enabled library. Now 272 either way; only the methods stay guarded.

**Two things about instrumenting this codebase, both of which cost time here:**
`openshot-bench` **forks a child per case and redirects its stderr to `/dev/null`**, so probes only
surface through the in-process `--case` path — the first W07 measurements read zero and looked like
the effects were never running. And a bench window that is too short misses what it is measuring:
`transitions_chain`'s transition sits at 2.0–2.5 s, so a 60-frame run never reaches it.

**Left in Stage 3:**

- **W06 thread budgets** — needs a cgroup-limited container to validate honestly, which is the one
  thing this machine cannot provide. The item also warns that the upstream merge brought overlapping
  settings; check what landed before writing anything.
- **W10 nvenc RGBA — skip.** Explicitly optional and W25 replaces the code entirely.

**W09 is done** (2026-09-18) — its own section is above.

**Also pending from Stage 2:** five more payload captures, and the two CI decisions. See above.

### Stage 6 — W19, W20 and W21: **done** (2026-09-22)

All three items are complete and nothing in the stage is blocked. `STAGE6-EFFECTS.md` is the map:
11 of 13 effects ported with 2 ruled out, all 10 transition variants ported, overlay clips as
fragments. Both product decisions it was waiting on were taken on 2026-09-22 — the parameter
reference resolution (1280 px, unversioned) and `cv::INTER_LINEAR` for the zoom blur's polar
conversions — and the two questions for the front-end team were answered by reading the editor's
own LUT shader, which turns out not to use the WASM at all.

The next stage is **W22–W25**, the per-clip PCIe crossing: every clip's frame still arrives from
the decoder on the CPU and is read back for Qt to composite, which is what stands between
`transitions_chain`'s 25.1 fps and its gate, and between `chroma_key_green`'s 43.6 and its 70.

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

- **A scenario's resampled alpha boundary depends on what else ran in the process** (found
  2026-09-22 while adding the W19 effect scenarios, **not fixed**). An effect scenario built on
  `image_alpha_320x200.png` — alphas 0/217/255 — scaled to fit had **1,280 pixels move between an
  isolated run and a full-suite run**, all of them on the interpolated alpha *boundaries*, and
  those same pixels also moved **between two identical 4-thread full-suite runs**. Single-threaded
  it was stable run to run but still differed from the isolated run, so there are two things here:
  a race at 4 threads and an order or state dependence underneath it.
  Only `Brightness` and `ColorShift` showed it, because the unpremultiply amplifies a 1 LSB alpha
  difference at low alpha and the other two effects in the set do not — which is also why nothing
  in the suite had caught it before. The region was the clip's own area (a 320x200 source drawn at
  288x160), so this is the resampling of the alpha channel, not the effects.
  The new scenarios were rebuilt on a 1:1 clip with uniform alpha from an `Alpha` effect and are
  bit-stable in every arm, so **nothing is currently red** — but the underlying instability is
  real, pre-existing, and worth its own item. A likely place to start is whether anything caches a
  scaled image per *path* rather than per reader instance: `image_alpha_320x200.png` is opened at
  different sizes by the ClipFx, Compositing, Readers and Export scenarios.


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

- 2026-09-22 — **Decoded frames are born on the GPU now: `src/gpu/GpuYuv` converts YUV to RGBA as
  an SkSL pass and the reader attaches it with `Frame::AttachGpuFrame`. Built, tested, and
  flagged off behind `Settings::GPU_DECODE`.**
  **Why off, and it is not because it is unfinished.** It changes pixels twice over. The
  conversion itself is faithful — **47.4 dB, max 3 LSB** against swscale on an unscaled clip,
  gated by the new `unit.gpu_decode`, which also asserts the pass actually ran (a check that
  cannot tell whether the GPU ran proves nothing). But it **honours the stream's declared colour
  space and swscale, as this reader configures it, never did**: the suite's media is untagged so
  it decodes BT.601 either way, while the files the suite *exports* are tagged `bt709`, and
  decoding those correctly shifts them. With the flag on, 19 of 307 frames in the Vulkan arm move
  — the blend modes that amplify a sub-LSB input difference, ChromaKey's threshold,
  `effects.enhancement`, and `export.roundtrip_x264`. **Turning it on is the owner's call and
  re-baselines every golden that decodes video.** Four-way sweep with it off: **307/307, 27
  checks, all four arms.**
  **What it is worth today: not much, and the reason is instructive.** End to end at 4K → 1080p
  on Vulkan, interleaved: **87–91 fps with swscale against 87–89 with the shader** — a wash —
  while CPU falls from **~2.0 to ~1.8 cores** and RSS rises 916 → 990 MB. Both ends still copy.
  The YUV is uploaded from host memory because NVDEC is not feeding it yet, and the frame is read
  back at the encoder because the writer still wants pixels. **W22's `CudaInterop` is exactly the
  missing input** — `copyNV12` already produces the two Vulkan images `GpuYuv` samples — and W25
  is the missing output. Until both ends close, moving the conversion alone buys 0.2 of a core.
  **Two things found on the way.** The pre-scale cannot ride along in the conversion's sample: one
  bilinear tap is not a downscale filter (29 dB), so it is a second Mitchell draw — and even done
  properly it is 28.9–30.7 dB against the CPU, because swscale's `SWS_FAST_BILINEAR` carries a
  half-pixel phase. **Whether the reader should pre-scale at all once the frame stays on the GPU
  is now an open question**: the compositor already scales, with the same sampler, in its own
  transformed draw. And `GpuFrame` now carries its owning thread and generation
  (`ownedByThisThread()`), because a cached GPU-backed frame handed to another thread is a
  surface that thread's recorder does not own; the reader drops and re-decodes those.
  **W23's gate has been restated in the worklist** — all three of its clauses were measured to be
  wrong, one of them unreachable in principle. See the item.

- 2026-09-22 — **The reader is 48 % faster at 4K, on the CPU path, and it was never the
  conversion's fault.** `sws_scale` is **95 % of the reader's wall clock** at 4K, and it was taking
  6.5 ms a frame where ffmpeg takes 2.7 ms for the *identical* unscaled `yuv420p → rgba` converter
  — swscale names the same special converter in both. What differed was the destination: the
  reader allocated a fresh `w*h*4` buffer per frame, **33 MB at 4K, above glibc's 32 MB mmap cap**,
  so every frame mmap'd and munmap'd 33 MB and the kernel faulted and zeroed all of it *inside*
  `sws_scale` — 612k minor faults over 150 frames against ffmpeg's 271k. W08's memset removal was
  the same phenomenon seen from the other side.
  **`FrameBufferPool` (file-local in `FFmpegReader.cpp`) recycles them**, handed back through the
  QImage cleanup function so a buffer returns when its Frame dies, wherever that happens; capped at
  256 MB free so it cannot become a leak. Interleaved, mains power, decode-only: **4K 124–131 →
  195–198 fps (+48 %)**, **1080p 534–539 → 751–785 fps (+44 %)**, `sws_scale` 6.5–7.0 → 4.55 ms,
  faults −54 %, **peak RSS unchanged** (1133 → 1134 MB). Bit-identical — four-way sweep
  **307/307, 26 checks**.
  **What this means for W23.** Its "< 1 core" half is the only part still open (the fps half was
  already met before this), and the measurement above says the target to beat is now 195 fps at
  4K, not 131. It also says where the remaining CPU goes: ~21 ms of core time a frame, almost all
  of it still the conversion, which is exactly what the SkSL YUV→RGBA pass removes.

- 2026-09-22 — **Plan step 1.5 done: hardware decode works again, and can no longer take the
  process with it.** Two changes in `FFmpegReader.cpp`, both small, neither moving a pixel on any
  path the suite exercises — four-way sweep **307/307, 26 checks** in all four arms.
  `ProcessVideoPacket` now takes swscale's source format from **the frame it is converting**
  instead of from `pCodecCtx->pix_fmt`, which is the hardware format (`AV_PIX_FMT_CUDA`) once
  NVDEC is on while the frame is the downloaded NV12 one. And `~FFmpegReader` no longer lets
  `Close()` throw out of a destructor — `Close()` drains the decoder through the same
  `ProcessVideoPacket`, so the throw came back out of the destructor and was `terminate()`.
  **New guard: `unit.hardware_decode`** (scenario 107, check 26) opens a reader with
  `HARDWARE_DECODER=2` and requires eight frames and no throw. It passes on a machine with no
  NVDEC too, where the reader falls back to software — what it asserts is that asking for hardware
  decode never throws and never aborts. Verified to **fail** with either fix reverted.
  **`HARDWARE_DECODER` still defaults to 0** and the service never touches `Settings`, so nothing
  about production changed.
  **Two things this did not fix, both W23's remaining half.** Hardware decode is still *slower*
  (4K: 44 fps against 126–135 in software), because download + swscale stay serial — exactly what
  plan §0.3 predicted. And **the PSNR half of W23's gate is unreachable through swscale**:
  hardware gives NV12, software gives YUV420P, and swscale converts the same 4:2:0 samples to RGBA
  **40.5 dB apart, max channel delta 79** — reproduced with the ffmpeg CLI alone, so it is
  swscale's chroma upsampling and not ours. ≥ 48 dB is only reachable once YUV→RGBA is our own
  SkSL pass, and then the software path is the wrong reference anyway. That gate wants restating.

- 2026-09-22 — **W23's premise measured before any code, and it corrects three things.** Nothing is
  implemented; this is the protocol's first half. Numbers and detail in the W23 item.
  **The fps half of W23's gate is already met in software** — the fork's reader does **126–135 fps**
  decode-only at 4K, not the 59 fps in plan §0.3 (W08 removed the per-frame copy since). It costs
  **3.1 cores**, and ffmpeg's NVDEC does the same work at **0.34**, so the operative half of that
  gate is "**< 1 core**" and it needs the SkSL YUV→RGBA pass as much as it needs NVDEC.
  **Hardware decode is broken, but not where §1.5 says.** `get_hw_dec_format` already filters to the
  selected decoder and the download already auto-selects its format — two of the three causes on
  file are fixed. What is left is that `ProcessVideoPacket` builds swscale from
  `pCodecCtx->pix_fmt` (`AV_PIX_FMT_CUDA` with hardware decode on) rather than from the frame it
  converts: "cuda is not supported as input pixel format", then
  `OutOfMemory: Failed to initialize sws context`.
  **And it aborts the process**, because `Close()` drains the decoder through the same
  `ProcessVideoPacket` and `~FFmpegReader` lets the throw escape a destructor. That one is not
  specific to hardware decode.
  **`DE_LIMIT_*` (1950x1100) is what hid it**: a 4K file falls back to software and looks fine, a
  1080p file with `HARDWARE_DECODER=2` dies. Production is unaffected either way — the service
  never touches `openshot::Settings` and `HARDWARE_DECODER` is 0.

- 2026-09-22 — **W22 done: CUDA frames reach a Vulkan image with no host copy, and the gate is met
  at 0.166 ms against 0.300 for a 4K frame.** `src/gpu/CudaInterop.{h,cpp}`, plus the two device
  extensions and the Vulkan handles in `GpuDevice`. Exact: **0 of 8 294 400 pixels differ** at
  3840x2160 (and at 640x360) after an NV12 pattern goes CUDA -> exportable Vulkan image -> SkSL ->
  readback. Clean under `compute-sanitizer --tool memcheck --leak-check=full`: **0 errors, 0 bytes
  leaked**. Four-way sweep **307/307** in all four arms, `openshot-gpu-checks` 8/8 on Vulkan and
  lavapipe. The interop SKIPs on lavapipe, with the GPU off, and with no CUDA driver — all normal.
  **The one thing a later session must not undo: `prepareForCopy`.** CUDA needs the images in
  `VK_IMAGE_LAYOUT_GENERAL` and Skia leaves them in `SHADER_READ_ONLY_OPTIMAL`, so every copy needs
  a layout barrier first. Doing it inside `copyNV12` puts a full cross-API round trip on the
  critical path — **0.224 ms of latency, and 0.401 ms a frame, over the gate**. Issued instead
  right after the draw that read the images, the semaphore is already up when the next copy starts:
  fixed cost 0.011 ms, and the 12.4 MB itself is 0.155 ms. The barrier still runs; it just overlaps
  with the drawing.
  **The premise below held**, and the two things it forced are in the code: the images are ours
  (own `vkAllocateMemory`, dedicated, exportable — `GpuSurfacePool` stays VMA-backed and is never
  the imported one), and the `SkImage` wrapper is rebuilt per frame declaring GENERAL, because a
  wrapper kept across frames would barrier from a layout the image has left. The CUDA device is
  matched to the Vulkan one **by UUID** and it is the **primary** context, which is what FFmpeg's
  CUDA hwdevice uses — so W23 can hand it straight to `AVCUDADeviceContext`. The driver is
  dlopen'd, never linked: only cuda.h is a build dependency, and a machine with no NVIDIA driver is
  unaffected. Full reasoning in `GPU-DECISIONS.md`.

- 2026-09-22 — **Stage 7 opened: W22's premise measured before any code, and it changes the item's
  shape.** Nothing is implemented; this is the measurement the protocol asks for first.
  **Everything the interop needs is on this machine.** The A2000 offers
  `VK_KHR_external_memory{,_fd}`, `VK_KHR_external_semaphore{,_fd}`,
  `VK_EXT_external_memory_dma_buf` and `VK_KHR_timeline_semaphore`; so does the Intel iGPU. CUDA
  12.8 is installed and FFmpeg has the `cuda` hwaccel. **lavapipe has no `external_semaphore_fd`**
  — so the interop declines there, which is the normal answer and is what that arm is for.
  **But Skia enables none of them off Android, and Graphite cannot export its own allocations.**
  `VulkanPreferredFeatures` names external memory only under `SK_BUILD_FOR_ANDROID`, so
  `GpuDevice` must add the two `_fd` extensions itself. And the only door into Graphite is
  `BackendTextures::MakeVulkan(..., VkImage, VulkanAlloc)`, which takes an image the **caller**
  owns — so the images CUDA imports have to be ours, allocated with
  `VkExternalMemoryImageCreateInfo` + `VkExportMemoryAllocateInfo` and our own `vkAllocateMemory`.
  **`GpuSurfacePool`'s surfaces can therefore never be the imported ones**, which the item's
  sub-tasks read as if they could. W22 is: two device extensions, an exportable allocation path
  beside the pool, then the import. The worklist item carries the detail.
  **W23 also depends on something already on file**: hardware decode throws on the first frame in
  this fork (`HARDWARE_DECODER != 0`), and plan step 1.5 is its fix. That is W23's problem, not
  W22's, but it is the next thing after this one.

- 2026-09-22 — **Stage 6 is complete: zoom blur and ColorMap are both in, and both were unblocked
  by a decision rather than by work.** W19 is 11 of 13 (2 ruled out), W20 is 10 of 10, W21 was
  already done. Four-way sweep **307/307 in all four arms**, 25 non-image checks,
  `openshot-gpu-checks` clean on Vulkan and lavapipe.
  **Zoom blur: `cv::INTER_LINEAR` is now passed to both `cv::linearPolar` calls** (owner decision).
  Both conversions had been running nearest-neighbour because the effect passed no interpolation
  flag and `linearPolar` reads it out of `flags & INTER_MAX` — which is what made the effect alias
  into spokes, and what made the port impossible: under a nearest remap the inverse map's angle,
  a float polynomial inside `cv::cartToPolar`, chose a whole different source pixel on 0.23 % of
  positions. Under linear the same error is a thousandth of a column of weight.
  **The port is three draws** — forward polar, `blur.sksl` along rho, inverse polar — not one
  composed fragment: composing costs `taps` fetches per bilinear corner (1216 a pixel at 1080p,
  strength 100) against 4 + taps + 4, and three draws put the 8-bit intermediates where the C++ has
  them so each stage could be checked against its own `cv::` call. That is how the single real
  error was found in minutes: **phi wraps and rho does not**, and treating the polar seam as an
  edge cost 136 LSB on the rays near angle zero and nothing anywhere else. **56.3–82.4 dB, max 8
  LSB, 24/24** against the 45 dB gate. It re-baselined `transitions.zoom_blur`, 4 frames, all of it
  on the colour-bar edges where nearest and linear differ.
  **ColorMap: the front end never calls the WASM for LUTs**, which answers both questions §2.6 was
  waiting on and answers them differently from how the item assumed. There is no `apply_lut` call
  to confirm an interpolation for — the editor grades in a PixiJS GLSL pass, trilinear by hand at
  the cube's **native size**, which is exactly W11's choice — and it **honours
  `DOMAIN_MIN`/`DOMAIN_MAX`**, which `parseCubeText` dropped. So the export was the wrong side of
  that one. Three things followed: the **17³ resample is gone** (re-baselined
  `effects.colormap_lut`, 2 frames, max 7 LSB; `effects.stack_crop_chroma_light_lut` did *not*
  move, contrary to the prediction on file), the parser reads the domain in **both** of its loops
  because a `.cube` may declare it either side of `LUT_3D_SIZE`, and the fragment uploads the cube
  as the editor's 2-D atlas in **F16** — converted to half on the CPU first, because Graphite
  declines to make a texture out of an F32 raster image and the upload just returns false.
  Colour-match mode is deliberately not ported and declines. **60.2–65.4 dB, max 4 LSB, 9 of 24
  bit-exact** against W19's 48 dB gate.
  **`GpuEffect` grew the two halves a multi-size effect needs**: `GpuSourceFrame()` and
  `RunGpuPass()`, the latter drawing one fragment into a frame of *its own* size. `ApplyOnGpu` is
  now those two in sequence, so nothing about the single-pass effects changed.
  **The parity harness still exits non-zero**, and the blur family makes the reason unavoidable
  rather than untidy: a separable box blur is 206 fetches a pixel where every other fragment is
  one, so it misses a 0.200 ms gate written for the others by two orders of magnitude while being
  at or ahead of the CPU twin it replaces. That gate needs restating; it is the last thing in
  Stage 6 that means less than it says.
  **Effect parity over the whole suite: 552 comparisons, 416 bit-exact on Vulkan and 425 on
  lavapipe, zero parity failures on either.** The 35 failures the harness exits on are all timing.
  Zoom blur is the one effect where the two backends differ visibly in the last bit — max 8 LSB on
  Vulkan against 198 on lavapipe, on a handful of positions where `sqrt`/`atan` land on opposite
  sides of remap's 1/32 grid and pick the neighbouring polar cell. Both clear the 45 dB gate.
  **The WASM LUT path is deleted** (owner, 2026-09-22): `wasm/wrappers/lutWrappers.{h,cpp}`,
  `src/ColorGrading/LutApply.{h,cpp}` and `LutCore.h`, plus their fourteen exported functions and
  the two source entries in the submodule's `CMakeLists.txt`. The editor grades LUTs in its own
  PixiJS pass and never called any of it. `ColorGradingCore.{h,cpp}` **stays** — libopenshot's
  `ColorMap` includes it directly for `.cube` parsing and the colour-match statistics — but it is
  no longer compiled into the WASM. Note that the **colour-match entry points went with the file**
  (`init_core`, `compute_ref_stats`, `get_ref_stat`, `set_ref_stats`, `set_cm_params`,
  `bake_cm_lut`): they lived in `lutWrappers.cpp` and shared its `LutState`. The prebuilt
  `wasm/dist/*.js` are untouched and now stale by that much.
  **Everything on the submodule side is uncommitted by request** — the `INTER_LINEAR` change, the
  domain parsing, `osFixedPoint`, the three new `.sksl` sources and this deletion.

- 2026-09-22 — **W20: three of the four blurs are shaders, zoom blur is blocked on a product
  decision, and the parity harness is red on timing.** `Blur` is a `GpuEffect` now, with three
  fragments: the box blur as **six separable half-passes** (one per axis per `cv::blur`), the
  diagonal blur as one, the rotational blur as one. Parity on Vulkan, eight images, against W20's
  45 dB gate: diagonal **24/24 bit-exact**, box 16/32 exact and **8/8 on either single axis**
  (the 8-bit intermediate between the two draws is the whole of the difference), rotational 8/32
  exact at 74–102 dB — max 1 LSB everywhere, nothing failing. Four-way sweep **307/307**,
  `openshot-gpu-checks` clean on both backends. `unit.gpu_blur_path` asserts the path per mode and
  checks the box blur's pass *count*, because a silently skipped half still looks like "the shader
  ran". The reasoning is in `GPU-DECISIONS.md`; the numbers are in `STAGE6-EFFECTS.md` and
  `doc/PERFORMANCE-BASELINE.md`.
  **`openshot-gpu-effect-parity` exits 1 with 21 failures, and 11 of them are these blurs.** The
  0.200 ms gate is a *per-pixel fragment* gate and a blur cannot meet it by construction — the box
  blur is 206 fetches per pixel where the rest of the vocabulary is one. Measured at 1080p, shader
  pass against the CPU twin's whole frame: box **7.5–28.0 ms** against 21.8–27.9, diagonal
  **7.0–18.6** against 20.6–22.2, rotational **66–125** against 70–195. So every one of them is at
  or ahead of the thing it replaces, and none is within two orders of magnitude of 0.200 ms.
  The other 10 failures are in cases this session did not touch (`enhance` x3, `mask` x4,
  `reflmove` x2, `brightness(0, 3)` at 0.206). **The gate needs restating before the harness means
  anything again**: a multi-fetch effect should be held against its CPU twin, not against a number
  written for a one-fetch fragment.
  Also seen during the timing section: repeated `[graphite] Failed to allocate unprotected
  dedicated image memory` on a card with 7 GB free. `msChainedPass` runs 150 `GetFrame` calls with
  no readback between them, which for the box blur is 900 pooled surfaces in one recording. Not
  investigated; it does not affect parity, only the timings of the cases it lands on.
  **Zoom blur is the one W20 item left and it is blocked on a product decision, not on work.**
  `applyZoomBlurEffect` passes no interpolation flag to either `cv::linearPolar`, so both polar
  conversions run nearest-neighbour — which is what makes the effect alias. The port is finished
  and measured in `spikes/zoom-blur-polar/`: forward map exact on all 300,304 channels, inverse
  map's angle irreducibly wrong on ~0.23 % of positions because it comes from `cv::cartToPolar`'s
  float polynomial, and under a nearest remap that is a whole different source pixel — 30–41 dB.
  The options are in that README.

- 2026-09-22 — **ColorMap is unblocked: the front end does not use the WASM for LUTs at all.** The
  project owner supplied the editor's `lut.frag` and its write-up (`tmp/`), and they settle both
  questions §2.6 was waiting on, differently from how the item assumed they would be answered.
  **There is no `apply_lut` call to ask about.** The grade is a single PixiJS filter pass in GLSL:
  the cube is packed into an RGBA32F 2-D atlas sampled NEAREST, and the shader does **trilinear by
  hand** — 8 fetches, 7 mixes — at the LUT's **native cube size**. That is exactly what W11 decided
  for our side, so the two now agree by construction, and the tetrahedral option in
  `lutWrappers.cpp` is dead code on a dead path.
  **They do honour `DOMAIN_MIN`/`DOMAIN_MAX`**: `t = clamp((c - domainMin) / domainSpan, 0, 1)`,
  with the span collapsed to 1 when it is degenerate. `parseCubeText` drops both
  (`ColorGradingCore.cpp:378`), so the export is the side that is wrong — the live bug is ours.
  **They grade straight RGB**: un-premultiply (guarded at alpha > 1e-5), grade, `mix(straight,
  graded, intensity)`, re-premultiply. `ColorMap.cpp` demultiplies too, so the alpha model already
  matches; the intensity blend has to happen on straight RGB as well.
  What follows, in order: drop the 17³ resample in `ColorMap.cpp:243` (W11's decision, re-baselines
  `effects.colormap_lut` and `effects.stack_crop_chroma_light_lut`, and costs CPU LUT throughput —
  measure it), honour the domain in the parser, then the fragment with the cube as a 3-D texture.
  **And a divergence to record rather than fix**: the editor renders a clip **ungraded** when the
  cube is 1-D-only, when a 3-D cube carries a 1-D shaper, or when the renderer is not WebGL2. The
  export grades all of them. Same `.cube`, different picture, and no shader parity work touches it.

- 2026-09-22 — **The blur is normalised and is no longer a box; the four blur shaders are now
  unblocked.** Three owner decisions landed and are implemented.
  **Reference resolution = 1280 px wide, unversioned.** The front end's proxy no longer varies (720p
  on the GPU), so the reference is fixed; a radius means that many pixels at 1280 and each effect
  scales by `image.cols / 1280`. Unversioned because people author against the 720p preview — an
  export's blur changing to match it is the correction, not the break. It lives **inside the effect
  functions in the submodule**, not in either caller, because both hosts pass the payload value
  straight down and that is the only place neither can forget.
  **The per-effect downscale thresholds went with it** — "half above 1 megapixel", "half above
  400 px of minimum dimension" — because they made the *result* depend on the source's resolution,
  the same bug wearing a different hat, and the reason rotational blur was affected at all. Replaced
  by working at the reference width when the source is wider: resolution-independent by
  construction, and faster at 4K than the thresholds were.
  **The blur is three box passes, not one.** Against a true Gaussian of the same variance, one
  `cv::blur` deviates **42.5 %** at the peak; three passes give **5.6 %** (two give 7.6 %, but two
  boxes convolve to a triangle with a visible apex). A real Gaussian is unaffordable on the CPU — at
  4K a radius-40 blur is sigma ~34, a 200-tap kernel per axis. The parameter keeps its meaning: a
  box of *w* taps has variance `(w²−1)/12`, so sigma is derived from it and the blur gets better
  rather than different.
  **It costs CPU, and it is the one deliberate exception to the standing constraint**:
  `transitions_chain` on the CPU path measures **31.5 → 27.9 fps (−11 %)**, the most blur-heavy
  scenario in the suite. Two passes instead of three would roughly halve that for 2 points of
  deviation.
  **ColorMap is decided too: trilinear at the LUT's native cube size.** On the production 25³ LUT
  the 17³ resample costs max 9.95 LSB / mean 0.657 while tetrahedral-vs-trilinear costs max 6.32 /
  mean 0.269 — the resample is the divergence that matters, and trilinear is the option where
  neither side changes its interpolation.
  Goldens re-baselined: `transitions.{blur,diagonal_blur,zoom_blur,stack_zoom_blur_alpha}`, 20
  frames. `transitions.rotational_blur` did **not** move, which is the check that the change is
  surgical. Four-way sweep 307/307.
  **Three submodule commits are local and unpushed**: `f8873e0` (explicit BGRA luminance),
  `d6c4e67` (the shared `shaders/` directory), `9fd188c` (this blur change). The front end compiles
  the same source to WASM and picks all three up when it updates the submodule.

- 2026-09-22 — **The shared shaders are shared for real; and a partly-ported chain is not free.**
  Two things, one good and one that corrects an earlier conclusion.
  **The fragments now live in `image-processing-lib/shaders/`** — one `.sksl` per effect plus
  `_prelude.sksl` and a README of the host contract. Until now they were C++ string literals inside
  libopenshot, which made the "one source, both sides" decision true in intent and false in fact:
  the editor could not load any of them. libopenshot embeds the same bytes at build time
  (`cmake/scripts/embed_shaders.cmake` → `EffectShaders.h`) rather than reading them at runtime, so
  there is one copy and no data-path dependency. Verified behaviour-neutral — parity **346/416** and
  the sweep **307/307**, both identical to the run before the move.
  **`transitions_chain` is ~30 % SLOWER on the GPU than off it** — 31.5 fps against 22.4, three
  interleaved pairs on a settled machine, with CPU occupancy halved (2.3 → 1.1 cores). The work did
  move to the GPU and the wall clock got worse anyway. Its first transition applies
  `{Zoom, Blur, Alpha}` to both clips, which now reads **GPU → readback → CPU → upload → GPU**:
  `Zoom` and `Alpha` are fragments and `Blur` — the one still on the CPU — sits in the middle.
  **This corrects what was concluded from `heavy_effects` earlier the same day** ("a partly-ported
  chain does not come out slower, so the order of the remaining work does not matter"). Correctness
  is unaffected, and an effect that declines simply runs its C++ twin; but a CPU effect *between*
  two GPU effects costs a readback and an upload, ~5 ms each way at 1080p, so **the order does
  matter**: an unported effect in the middle of a common chain is far worse than one at the end.
  `Blur` is the worst case — blocked, and in the middle of the most common transition. That is a
  second and independent reason to settle the reference-resolution decision.
  Nothing is reverted: the CPU path is untouched at 31.5 fps and `OPENSHOT_GPU` is a per-deployment
  switch. But a GPU deployment running transitions is currently worse off than a CPU one, and that
  should be known rather than discovered.

- 2026-09-22 — **W20: CircleMask in; rotational blur turns out to be blocked, not open.** Six of ten
  transition variants are done and **nothing is open any more** — the remaining four are all blocked
  on the same reference-resolution decision. Parity **346 of 416 comparisons bit-exact**, four-way
  sweep 307/307.
  **`CircleMask` is 24/24 bit-exact, and it got there by not drawing the circle.** `cv::circle` with
  `LINE_AA` fills a polygon approximation using OpenCV's own scanline coverage; an analytic disc in
  SkSL would be a *better* circle and a worse port, because the edge ring is where the whole effect
  lives. The mask is built by the same OpenCV call, cached, and uploaded as a texture.
  **That is the third time this has been the right answer** — LightAdjustment's tone curve, Mask's
  matte, now this — so it is worth stating as a rule: **whatever a rasteriser or a transcendental
  decides stays on the CPU and arrives as a texture; the fragment does arithmetic.** Everything
  ported that way is bit-exact; everything that tried to re-derive a rasteriser is not.
  **`TRANSITION-PARITY.md` was wrong about rotational blur and is now corrected.** It listed the
  reference-resolution bug as affecting only the three linear blurs and said rotational blur "takes
  degrees and is fine". The parameter is fine; the effect is not. `applyRotationalBlur` downscales
  before it works and the threshold keys on the *image* — `absBlur > 15 && minDim > 400` halves it,
  `> 45 && > 800` quarters it — so a 20° blur runs at full resolution on a 640x360 source and at
  half resolution on a 1080p one. Same authored angle, visibly different result, per clip. It also
  carries an optional `cv::GaussianBlur` above 6°, so even setting that aside the exactly
  reproducible window at 1080p is a blur of at most 6° where transitions use 25.
  The spike's 57–61 dB still stands but measured only that single-pass window, which its own README
  says — it is not a port of the effect as production calls it.

- 2026-09-22 — **W20: five of ten transition variants done, and OpenCV's fixed-point paths turn out
  to be reproducible.** `Zoom`, `BorderReflectedMove` and `BorderReflectedRotation` join `Wipe` and
  `SplitShift`. Parity across the whole suite is **322 of 392 comparisons bit-exact**, no failures
  against either item's gate, four-way sweep **307/307**.
  **The `cv::cvtColor` problem was fixed rather than tolerated.** The submodule now computes its
  BGRA luminance explicitly (`image-processing-lib` `f8873e0`), and `Wipe` went from 62–85 dB with a
  full threshold step on 144 pixels to **24/24 bit-exact**. It re-baselined
  `transitions.threshold_wipe_mask` — ~100 pixels a frame along the mask edge, exactly as predicted.
  **Cross-repo: the front end compiles the same source to WASM and picks it up when it updates the
  submodule. The submodule commit is local and unpushed.**
  **The session's real lesson: reproduce OpenCV's arithmetic, not its intent.**
  `BorderReflectedRotation` went through three versions — sampling at the pixel centre (11–28 dB on
  high-frequency images, max 255), sampling at the integer coordinate and rounding (exact at 45°,
  0.1 % of pixels wrong at −12.5°), and finally reproducing `warpAffine`'s 10-bit fixed-point map
  (**16/16 bit-exact**). The middle version is the dangerous one: it looks perfect on every smooth
  test image and is completely wrong on detail. **Test resampling effects on high-frequency
  content** — the noise image separated all three immediately where a ramp could not.
  `Zoom` is **zoom-in only**: the zoom-out branch can change the frame's size, which `ApplyOnGpu`
  cannot express.
  The parity test now applies **each item's own gate** — 48 dB for W19, 45 dB for W20 — rather than
  holding the transitions to a number nobody set for them.

- 2026-09-22 — **W21 done: overlay clips as shaders — and a benchmark that lied by 3x.** Both
  composites are two-texture fragments in `src/gpu/GpuOverlay.{h,cpp}`, called from `Clip::GetFrame`
  before the OpenCV path, which stays and is gated on `GpuDevice::available()` (the item said
  "deletes the last `GetImageCV` round trips"; the standing constraint rewrites that the same way it
  rewrote plan step 2.5). Both golden overlay scenarios are **bit-identical on Vulkan** under
  `Tolerance::Exact()` with no GPU band, and `unit.gpu_overlay_path` proves the shader is what
  produced them. Sweep **307/307** in all four arms, `openshot-gpu-checks` 8/8.
  **The displacement map's luminance is fixed-point and guessing would have been wrong**:
  `cv::cvtColor(COLOR_BGRA2GRAY)` is `(B*1868 + G*9617 + R*4899 + 8192) >> 14`, not a float dot
  product. A differently-sized overlay declines, because the C++ resizes it with `cv::resize` and
  OpenCV's `INTER_LINEAR` is a rasteriser difference Skia will not reproduce.
  **The performance claim I first made was wrong and the reason matters.** A sequential A/B read
  `transitions_chain` 4.2 → 6.7 fps, a 55 % gain; re-visiting the *unchanged* arm afterwards
  measured 10.3 and 13.8 fps — three times its own earlier figure. Every measurement had been taken
  immediately after a compile. Redone with both libraries built up front and swapped in place so the
  arms interleave with no build between them, after a settling pause: **17.0 fps without W21 against
  16.8 with — no wall-clock difference**, with CPU occupancy 2.2 → 1.9 cores and peak RSS
  1.13 → 1.05 GB, which is the two full-frame `cv::Mat` conversions going away.
  **That is the expected result, not a disappointment**: the transition effects around the overlay
  are still on the CPU and each calls `Frame::GetImage()`, so the overlay's output is read back
  immediately. **W21 pays when W20 lands**, and the two want measuring together.
  Two rules now apply to every measurement here: **never measure straight after a build**, and
  **interleave the arms** — building both artefacts up front and swapping them if that is what it
  takes. A sequential A/B on this laptop is not evidence.

- 2026-09-22 — **W19: Mask in, CameraMovement ruled out, and both item gates measured.** Ten
  effects are fragments; **246 of 329 image/parameter combinations bit-exact**, Mask never over
  1 LSB with three of its four cases 8/8. Four-way sweep 307/307. **Only ColorMap is left**, and it
  is blocked on the front end.
  **Mask's matte stays on the CPU and is uploaded as a texture**, like LightAdjustment's tone curve
  — it is cached and usually still, so the cost is paid once, and it keeps Qt's rasteriser as the
  single source of the mask's edges instead of introducing a second one.
  **CameraMovement is ruled out for the same reason as Crop**: `QPainter` with a world transform
  and `SmoothPixmapTransform` is a rasteriser difference, not an arithmetic one. One subset is
  exactly portable and is written down rather than left to be rediscovered — at zoom 100 % and
  rotation 0 the transform is a pure translation and Qt takes its `TxTranslate` fast path, which
  `draw_to_canvas` already reproduces. Not built: the effect exists for zoom, and a fragment that
  declines in the normal case earns little.
  **Both of W19's fps gates now have numbers, and both need restating.** Interleaved on mains,
  1080p render, 150 frames: **`chroma_key_green` 6.9 → 43.6 fps, a 6.3× speed-up** — one clip, one
  ported effect, one crossing — still short of its 70 fps gate, and what is left is the crossing,
  which is W22–W25. **`heavy_effects` 4.9 → 5.6 fps, +14 %**, and it **cannot** reach 60 from this
  item: four of its six effects stay on the CPU whatever W19 does (Crop ruled out, `Blur` not in
  the item's list, `ColorMap` blocked, and its `Enhancement` asks for grain). The useful part is
  that a partly-ported chain is still a win rather than a loss.

- 2026-09-22 — **W19: LightAdjustment and Enhancement in, Crop ruled out.** Nine effects are
  fragments; **214 of 297 image/parameter combinations bit-exact**, and neither new effect ever
  exceeds **1 LSB**. Four-way sweep 307/307.
  **A tone curve belongs in a texture.** LightAdjustment's contrast stage is a 256-entry LUT the
  CPU memoises from a `pow()`/`sin()` curve. Evaluating that per pixel in SkSL would be the obvious
  port and the wrong one — neither intrinsic is exactly specified on the GPU and the round back to
  a byte would flip. Uploading the CPU's own LUT as a 256x1 texture makes the stage exact by
  construction (both contrast cases 8/8) and is cheaper than a `pow()`.
  **Enhancement is the first two-pass fragment**, and the mechanism is simply calling `ApplyOnGpu`
  twice: each call leaves its result on the GPU, so the second pass reads the first's output as a
  texture and only the last is read back. Clarity alone is 8/8 exact.
  **Two things are staying on the CPU, both for measured reasons.** Enhancement's grain pass uses
  `fract(sin(x * 12.9898 + y * 78.233) * 43758.5453)`, whose argument reaches ~85,000 at 1080p —
  `double` and `float` do not agree to an LSB there, they agree to nothing, up to ~140 LSB of
  grain. And **Crop is not a per-pixel effect at all**: it is `QPainter` with antialiasing, a
  rounded-rect clip and a `drawImage` between fractional rects, so its corners and edges are a
  rasteriser difference of the kind already on file for the compositor. The service always sets
  `resize = false` and passes a radius curve, so there is no exact subset to port. No fragment was
  written for it; porting it is a redefine-class product decision.
  **The machine came back onto mains during this session**, so the Exposure round-trip figure is
  re-measured and the battery one withdrawn: **6.3–7.1 ms against 7.3–10.1 ms, roughly 20 %**.
  The passthrough baseline on mains is 0.19–0.21 ms, so the 0.2 ms per-effect gate still sits at
  the floor of one full-frame pass and still wants restating.

- 2026-09-22 — **W19: Bars, ChromaKey and ColorAdjustment.** Seven effects are now fragments;
  **126 of 160 image/parameter combinations bit-exact**, nothing over 2 LSB outside Brightness and
  Exposure. Four-way sweep 307/307, `openshot-gpu-checks` 8/8 on both backends.
  **babl's `Y'CbCr u8` is BT.601 studio range** (Y 16–235, Cb/Cr 16–240), not the full-range
  "JPEG" mapping — that one is wrong by up to **16** on 99.6 % of a 64³ grid. Measured by asking
  babl for its response to black, the primaries and white. With the studio coefficients, 193 of
  262,144 samples still differ by 1, and that is babl's own constants rather than a rounding mode
  (double, float, round-half-away and `trunc(x+0.5)` all give the same 193). End to end it does not
  matter: **the service's exact ChromaKey configuration is 8/8 bit-exact**, and only a deliberately
  wide halo moves, on 4 and 11 pixels, by 2 LSB.
  **Only ChromaKey's YCbCr method is on the GPU** — the rest key on HSV/HSL/CIE coordinates babl
  computes, and `SetGpuUniforms` declines them. The service only ever constructs YCbCr.
  **The parity rule needed refining.** ColorAdjustment never unpremultiplies and is still not
  exact: it carries `double` parameters through per-pixel arithmetic that an SkSL uniform holds as
  `float`. It never exceeds 1 LSB. So: exact when the arithmetic is exact in `float` *and* nothing
  divides by alpha.
  **And the ordering rule caught two more** — Bars and ColorAdjustment both fetched `GetImage()`
  before `ApplyOnGpu` and measured ~4.2–4.4 ms a pass instead of ~0.1 ms. Four of the first seven
  were written that way round, so it is the shape of these functions, not a slip.
  **Timings this session are not trustworthy in absolute terms: the machine was on battery**
  (AC offline, 838 MHz average). The parity numbers are unaffected — they are deterministic — but
  every millisecond figure from 2026-09-22, including the Exposure round-trip A/B, wants a re-run
  on mains power before it is quoted. The *ratios* held across interleaved runs.

- 2026-09-22 — **Exposure: the ARGB32 round trip removed, and a diagnosis corrected.** The previous
  entry recorded that `Exposure.cpp`'s conversion to `Format_ARGB32` — which `AddImage` converts
  straight back in place, so it is a round trip and not a conversion — was quantising pixels before
  the effect ran, and that this explained the fragment differing from the C++ on 19 % of a noise
  image. **Removing it proved that wrong: not one pixel of any golden moved, and the parity numbers
  were identical to the digit.** Qt's unpremultiply/re-premultiply pair is lossless, so there was
  nothing to re-baseline.
  Measured exhaustively instead, the whole `exposure(1.0)` chain disagrees on **exactly the same 588
  of 32,896 pairs as the bare unpremultiply**, and the byte the fragment reads is correct in **all
  32,896** — so there is one cause, the division, and `osBytes` is trustworthy. The 19 % was an
  artefact of the test image: `setPremul` clamps each channel to its alpha, so half of `noise`'s
  channels have `v == a`, which is exactly the integer quotient the division disagrees on.
  The round trip is removed anyway, on its own merits: it is dead work, and dropping it takes
  Exposure from **7.0–7.8 ms to 5.9–6.6 ms at 1080p (~15 %)** on the CPU path that ships, with
  provably identical output. Four-way sweep 307/307.
  `openshot-gpu-effect-parity` now also prints each effect's **CPU twin cost**, which is what made
  the 15 % measurable at all, and carries an exhaustive probe for the unpremultiply and for the
  full exposure chain.

- 2026-09-22 — **W19: Alpha, Exposure and ColorShift fragments — and what decides whether one is
  exact.** Four effects are now shaders. **72 of 88 image/parameter combinations are bit-exact**,
  the other 16 at 55–75 dB with a 5 LSB worst case against a 48 dB gate. Four-way sweep 307/307.
  **The split is not complexity, it is division**: Alpha (scales premultiplied channels) and
  ColorShift (four wrapped integer gathers) are **16/16 exact**; Brightness and Exposure, which
  unpremultiply, are not. That confirms the earlier finding from the other side.
  ColorShift also settles a question worth having settled: it gathers r, g, b and a from four
  different pixels and so emits colour above its own alpha, and it is still bit-exact — **Skia does
  not clamp runtime-effect output to valid premultiplied colour.**
  **An ordering rule that costs 20x**: `ApplyOnGpu` must come before any `frame->GetImage()`,
  because on a GPU-backed frame that call *is* the one readback. Exposure and ColorShift were both
  written the wrong way round first and measured 3.2–3.5 ms a pass; corrected, 0.15–0.22 ms. The
  output is identical either way, so only the timing caught it.
  **Two things are recorded rather than fixed.** Exposure's residual gap is the CPU path's own
  `Format_ARGB32` round trip — a conversion straight back to premultiplied RGBA that loses up to
  1 LSB on partial alpha and buys nothing (at `exposure(1.0)`, an identity, the two still differ on
  19 % of the noise image). Deleting it would make CPU and GPU agree and make the CPU path more
  accurate and faster, but it moves production output, so it is proposed. And **the ≤ 0.2 ms
  per-effect gate needs restating by the project owner**: a do-nothing passthrough fragment
  measures 0.19–0.22 ms in the same harness, so the gate is the floor of one full-frame pass, not
  the cost of an effect. ColorShift is over it at 0.22 and shipped on those grounds.

- 2026-09-22 — **W19: four effect scenarios on partial alpha, and the instability they turned up.**
  `effects.{brightness,exposure,colorshift,bars}_alpha` — the four effects the service builds only
  in `Transition.cpp`, constructed exactly as it constructs them, now driven over **semi-transparent**
  pixels. `transitions.*` already drove all four, but over opaque video through a scaled clip and
  therefore under the wide `gpu-composite` band; these are 1:1 and held to `Tolerance::Exact()`
  with no GPU band at all. **Brightness is bit-identical on Vulkan on partial alpha** (`max=0`),
  which is a sharper result than the parity test's 1-LSB class predicted — that class is real but
  does not bite at these alphas, and `openshot-gpu-effect-parity` remains its gate.
  Suite is **307/307** in all four arms, up from 295.
  Also added `unit.gpu_effect_path`, which asserts on `GpuEffect::GpuPasses()` rather than on
  pixels: with a GPU up at least one effect must have run as a shader, with the GPU off none may.
  It reports `gpu_passes=1 cpu_fallbacks=0` on Vulkan and the reverse with the GPU off, so **the
  timeline path does hand effects a GPU-capable frame** — which nothing had actually established.
  **Correction to the 2026-09-22 entry below**: it recorded that Brightness, Exposure, ColorShift
  and Bars "have no golden scenario at all". They did — `Transitions.cpp` generates one per
  `TransitionEffect` in a loop, so a grep for the class names finds only the enum. What was true is
  that the coverage was over opaque pixels and under a 45 dB band.
  **And one thing is now on the list rather than fixed** — see "Known oddities": the first version
  of these scenarios, built on a PNG's own alpha channel, was not bit-stable across suite context.

- 2026-09-22 — **W19 started: the `GpuEffect` base, the first fragment, and three things that
  constrain the other twelve.** `src/GpuEffect.{h,cpp}` compiles a shared prelude plus the effect's
  fragment once per instance and **leaves the result on the GPU**, so a chain of GPU effects pays
  one crossing instead of one each — per-effect readback would make a shader slower than the C++ it
  replaces (0.15 ms of shader against ~4.1 ms with the transfers around it). `Brightness` is the
  first fragment: **bit-exact against its C++ twin on 26 of 32 image/parameter combinations**, the
  other six at 69–74 dB / 1–3 LSB, and **0.14–0.19 ms chained at 1080p** against a 0.2 ms gate.
  Four-way sweep 295/295, `openshot-gpu-checks` 8/8 on both backends.
  Three findings, all in `GPU-DECISIONS.md` because they apply to every remaining fragment:
  **SkSL is the GLSL ES 1.00 intrinsic set** — no `round`, no `trunc` — which is also CanvasKit's
  ceiling, so it shapes the shared source rather than just this port;
  **the fragments reproduce the C++ byte truncation deliberately**, which is what makes them
  bit-exact instead of merely close and lets the effects goldens keep `Tolerance::Exact()`;
  and **the shared unpremultiply cannot be made exact** — Vulkan allows 2.5 ULP on a division, so
  588 of 32,896 legal (byte, alpha) pairs differ by 1 LSB on Vulkan and 576 on lavapipe, always
  where the true quotient is an exact integer. Two rates on two drivers is the proof it is the
  division, not the fragment.
  **The first version of the parity test reported all 32 combinations bit-exact while the shader
  had not compiled at all** — `ApplyOnGpu` declined and both arms ran the same CPU code. It now
  requires `Frame::IsGpuBacked()` after the call and fails loudly without it. Worth remembering for
  any check of a path that has a silent fallback, which is every GPU path in this fork.
  **Also found, and not created here: `Brightness`, `Exposure`, `ColorShift` and `Bars` have no
  golden scenario at all**, though the service constructs all four — so the four-way sweep
  currently says nothing about them. Added as a W19 sub-task.

- 2026-09-18 — **W09 done: NVENC rate control, and the VMAF comparison that had never been run.**
  The gate is met on all three clauses (VMAF −0.21, size +1.7 %, `single_video` nvenc 122.4 → 122.3
  fps) and the four-way golden sweep is 295/295. **The item's four sub-tasks were all real and none
  of them was why the gate failed**: before any of them, size was **+112 %**, because the `cq 19`
  that shipped as a placeholder spent **2.1× libx264's bytes for 0.16 VMAF points**. The fix was to
  give `crf` a meaning on hardware encode — it had been discarding the caller's value and inventing
  a bitrate — mapping it to `rc=vbr`, `cq = crf + 10`, `bit_rate = 0`, with the +10 calibrated at
  crf 18/23/28 and checked on three scenarios. Of the named sub-tasks, **`spatial-aq 1` paid**
  (smaller *and* better), **`b_ref_mode middle` measured as a no-op** at `p5`/`hq` and was in fact
  unreachable — which turned up a live bug, `allow_b_frames 1` throwing `InvalidCodec` on NVENC
  because `add_video_stream` asks for 10 B-frames against a hardware limit of 4 — and **zeroing the
  bitrate changed no bytes at all**. Also: `tests/golden/Recipes.cpp` had drifted from the service
  and was benchmarking a configuration production never runs; `openshot-bench` gained a `lossless`
  mode, without which the gate has no reference to score against. The matching service edit is in
  the working tree, **uncommitted** — it is product-visible.

- 2026-09-18 — **Stage 3: W07, W08 and W05 done; every one of them had a sub-task that did not
  survive measurement.** W07: `SetImageCV` was converting twice (temp `cv::Mat`, then
  `QImage::copy()`); now once, 336 → 102 ms on `transitions_chain`, +4.8 % fps there and within
  noise on `heavy_effects`. Its dirty-flag cache was **rejected** — access is strictly alternating
  (300 Get / 300 Set), so the flag is dirty on essentially every Get — and so was reusing
  `imagecv`'s buffer, because `cv::Mat::create` reuses an allocation regardless of refcount.
  **W07's fps gates are unreachable by W07**: the scenarios need 749 ms and 1276 ms, the entire
  conversion cost was 476 ms and 443 ms. W08: `memset` dropped (part of its cost migrates — it was
  pre-faulting the pages `sws_scale` then faults itself) and `av_image_copy` → `av_frame_ref`,
  giving `source_4k` **61.3 → 70.1 fps (+14.4 %, gate met)** and `single_video` **116.6 → 124.9
  (+7.1 %, 0.1 % short)**; the golden suite also runs **clean under ASan**, which is what that item
  exists to check. Its **threaded swscale was rejected on measurement** — the option works, but
  with decoder threads fixed `source_4k` read 663.8 / 674.0 / 650.0 / 682.9 ms at 1/2/4/8 threads.
  W05: single `WriteFrame` built and byte-identical, but **≈ −6 % against a ≥ 15 % gate, so it
  ships flagged off**; the ceiling is the encoder's share of wall time and this payload is
  render-bound. Its progress reporting needed two redesigns — the writer callback runs on the
  *encoding* thread, which works in bursts, and then polling `isCancelled()` every 100 ms turned out
  to be a Redis round trip, 10 req/s per export. Both fixed; progress and cancellation now share one
  one-second tick. **A bug of our own was found on the way**: W18 had put `previewApp` behind
  `USE_QT_PLAYER`, making `sizeof(Frame)` 256 without the macro and 272 with it, while the service
  compiles these headers without it and links a player-enabled library. Fixed and proven both ways.
  Two instrumentation traps worth remembering: `openshot-bench` forks per case and sends the child's
  stderr to `/dev/null`, and a 60-frame window never reaches `transitions_chain`'s transition at
  2.0–2.5 s.

- 2026-09-18 — **W04's mechanism done and its first payload green; W03's workflow written but not
  enabled.** The corpus turned out to have **zero runnable payloads, not one**: signed `fileUrl`s
  expire after 24 h, and `HTTPFileTransfer::download` opens its destination `"wb"` on every attempt,
  so the archived media could not be used and `render-payload` downloaded into a 403. `MediaFetch`
  in the service fixes it — `RENDER_MEDIA_CACHE` serves each file from the archive keyed by the
  URL's basename, `RENDER_MEDIA_CACHE_STRICT` makes a missing entry an error rather than a quiet
  network fetch, and behaviour is unchanged when unset. It had to cover the **Redis-cached path**
  used for clip media and sound effects as well as the plain download; the first attempt missed it
  and the run still went to the network. **This also unblocks W05's gate.**
  `tests/payloads/run-corpus.sh` runs two rounds and compares decoded-frame hashes
  (`ffmpeg -f framemd5`). `prod-2026-09-16-pip-lut-whoosh`: **750 frames, four independent rounds
  identical** (two to record, two to verify the `check` path against a rebuilt binary). ~8 min per
  round in a **Release** `render-payload`; Debug is ~25x slower, which is why the first attempt
  looked like it would take hours. The archive was also incomplete — the WHOOSH transition's
  `whoosh.opus` had never been saved, and sound-effect URLs are unsigned and easy to miss, so the
  README now says so. **Five captures still missing** (text, subtitles, transition-heavy, chroma
  key, 4K) and they need same-day archiving, so that part cannot be done from this machine.
  W03: `.github/workflows/golden.yml` written and its YAML validated, but **never run**. Two
  blockers, both decisions rather than code — the pinned FFmpeg lives in a private base image this
  machine cannot pull (**the same blocker W01 carries, which the worklist never recorded as
  shared**), and the private submodule needs a deploy key. Enabling it and opening the
  deliberate-regression PR the gate asks for are actions on the GitHub repo.

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
