# Decisions

One line per decision that a later session should not re-litigate: what was decided, when, why, and
what would have to change for it to be revisited. Referenced from `doc/gpu-migration/GPU-RENDER-PLAN.md`.

## Taken

### The CPU path ships; the GPU path is a configurable addition (2026-09-14, project owner)

Not a trade-off to re-open. The runtime image has no GPU, so the CPU path is what production runs
and what any no-GPU machine falls back to; it stays fully working at full quality. The GPU path is
opt-in — `OPENSHOT_GPU=off|vulkan|lavapipe`, plus which Skia the build was configured against —
and `GpuDevice::available() == false` is a normal answer.

The consequence that bites: **CPU code is never deleted because the GPU does not need it.** It is
gated instead, with the CPU branch kept. Plan step 2.5 as written ("delete the CPU-blur
workaround") would have removed the σ > 120 downscale that exists precisely because Skia's *CPU*
mask blur clamps at 128 px, breaking high-resolution shadows on the path that actually ships; it is
rewritten to skip that branch only when the offscreen is GPU-backed. Read the rest of the plan the
same way.

Acceptance for every change is the four-way golden sweep — CPU Skia, GPU Skia with the GPU off,
GPU Skia on Vulkan, GPU Skia on lavapipe — all four at 292/292.

**2026-09-10 · Branch off the fork, not upstream.** `feature/gpu-rendering` starts from the fork's
`develop`. Upstream OpenShot 1.0.0 is 336 commits ahead and has a fixed hardware-decode path, but
lacks the text engine, subtitles, blend modes and transitions the service depends on. Merging
upstream is still possible but must happen *before* plan phase 3, which rewrites the same files.
*Revisit if:* upstream's FFmpeg 7 support becomes necessary.

**2026-09-10 · The golden-frame suite is the oracle.** `tests/golden` (95 scenarios, 292 committed
PNGs) gates every change; Catch2 unit tests are not used (Catch2 is not installed and never covered
the fork's custom code). *Revisit if:* the suite becomes slower than a minute or starts producing
false positives across machines.

**2026-09-10 · Skia GPU, not OpenCV CUDA or hand-written Vulkan.** Skia already renders the text and
subtitles, provides compositing, all 16 blend modes, image filters and SkSL, and matches the
front end's CanvasKit. See plan section 1.3 for the full comparison.

**2026-09-11 · Glow quality is fixed.** In-motion glow must match resting glow; lowering the glow
resolution or ray-march step count to buy speed is not an option. The glow gets faster by running
the existing SkSL on the GPU. *Revisit if:* product explicitly changes the quality bar.

**2026-09-11 · No CPU frame-level parallelism.** Rendering frames N and N+1 on separate CPU threads
would use the idle cores, but Skia Graphite is one `Context` per process and gets parallelism from
pipeline depth, so the machinery would be discarded at phase 3/6. The plan invests in depth
(decode-ahead, encode-ahead, frames in flight) and leaves width to the process manager, which the
service already uses. *Revisit if:* the CPU fallback path becomes a product requirement at scale.

**2026-09-11 · Two Skia build scripts.** `skia_build_script.sh` (CPU raster, installs to
`/usr/local`) is never modified; the GPU build gets `skia_build_script_gpu.sh` with its own
`out/Release-GPU` and `/usr/local/skia-gpu` prefix, selected with `-DSkia_ROOT=`. Both pin milestone
m147 to stay in lockstep with the front end's CanvasKit.

**2026-09-14 · Merge upstream `develop` now, before the GPU rewrite.** Supersedes the 2026-09-10
decision to stay on the fork. `upstream/develop` (the live branch; `upstream/master` is frozen at
2021) was 341 commits / 220 files ahead of the merge base. Merged on `merge/upstream-develop`,
since folded into `feature/gpu-rendering`:
24 conflicted paths, 82 hunks, ~2,322 lines. Three reasons the timing changed: the conflict set is
exactly the files plan phases 3–4 rewrite, so a later merge becomes impossible; the golden suite now
exists to validate it bit-exactly; and it delivers plan step 1.5's hardware-decode fix better than
the plan proposed. Governing rule was **upstream wins on plumbing, the fork wins on pixels** — see
`doc/gpu-migration/UPSTREAM-MERGE.md` for every per-file decision and what is still owed.
*Revisit if:* never — but keep `UPSTREAM-MERGE.md`'s "still owed" list alive until it is empty.

**2026-09-14 · Clip sort order is insertion-stable, not address-tie-broken.** The fork's
`CompareClips` used `<=` and was not a strict weak ordering (undefined behaviour for
`std::list::sort`); upstream fixed the ordering but tie-broke on pointer address, which made the
same project render differently between runs (the golden suite failed 4 runs in 5). `CompareClips`
now reports no ordering for equal layer and position and relies on `std::list::sort` being stable,
so clips keep insertion order. Consequence: a clip sharing a layer *and* position with another now
draws on top if it was added later; `compositing.layer_order` was re-baselined for this, alone.
*Revisit if:* the service needs explicit z-ordering within a layer, which should then be an explicit
field rather than a sort accident.

**2026-09-14 · Phase 2 runs before Phase 1.** The GPU/Skia work ships first; CPU quick wins follow.
The measured bottleneck is one shader — `text_animated_glow_3` spends ~72 % of its time in
`paintGlowFromSilhouette`, `everything` 65 % — while the plan already conceded that R1 "does
essentially nothing" for those scenarios. Nothing in R2a depends on R1. The only real coupling was
the GPU-capable image, formerly step 1.7, which moved into Phase 2 as step **2.0**. Phase and step
numbers are stable identifiers, not sequence, so existing references stay valid.
*Revisit if:* the GPU node pool turns out to be unavailable, in which case R1 is the only work that
can proceed.

**2026-09-14 · GPU Skia: Graphite over Vulkan, Ganesh off, prefix `/usr/local/skia-gpu`.**
Plan step 2.1 is done. `skia_build_script_gpu.sh` builds chrome/m147 into
`~/skia-stable/skia/out/Release-GPU` (31 MB, 3,146 Graphite symbols; the CPU `out/Release-CPU` is
untouched at 26 MB) with `skia_enable_graphite = true`, `skia_use_vulkan = true`,
`skia_enable_ganesh = false`; `skia_use_vma` follows `skia_use_vulkan` by default so it is not named.
Every other GN arg is byte-identical to the CPU script — the shared 54-line block diffs clean, which
is the check to repeat whenever either script changes. `SKIA_ENABLE_GANESH=true` builds Ganesh
alongside, which is how the still-open "Graphite only, or Ganesh as a fallback" question gets
answered without editing the script.

Verified on this laptop against a scratch prefix (the `/usr/local/skia-gpu` install itself needs
`sudo cd ~/skia-stable/skia && ./install_skia_gpu.sh` and has not been run yet):
`tests/gpu/openshot-gpu-smoke` brings up a Vulkan 1.3 device, a Graphite
`Context`, draws a 64x64 red→blue gradient into a `kRGBA_8888` `SkSurface`, reads it back and writes
a PNG — on the NVIDIA RTX A2000 and, with `VK_DRIVER_FILES=.../lvp_icd.json`, on lavapipe. Both
produce byte-identical PNGs, and the left/right pixels (`fff90006` / `ff0600f9`) confirm the channel
order that step 2.3 depends on. The CPU-prefix build still passes `tools/golden.sh check`
(95 scenarios, 292 frames, 0 failures).

Three things the plan did not anticipate, all now handled:

1. **`-DSkia_ROOT` did not actually select a build.** `FindSkia.cmake` prefers pkg-config, and the
   CPU `skia.pc` in `/usr/local` is on the default path, so a GPU-root configure took the GPU
   *library* and the CPU *headers* — a silent mismatch between headers compiled without
   `SK_GRAPHITE` and a library built with it. Fixed: when a root is given, pkg-config is ignored and
   the system fallback paths are dropped. The plan's "no `find_package` change is needed" was wrong.
2. **Graphite requires a caller-supplied `VulkanMemoryAllocator`**, and Skia's VMA-backed one is
   declared only in `src/gpu/vk/vulkanmemoryallocator/VulkanMemoryAllocatorPriv.h` ("we cannot really
   expose this to clients in a meaningful way"). The symbol is in `libskia.a`. The installer ships
   that header and `src/gpu/GpuTypesPriv.h` at their source-tree paths rather than having every
   caller hand-redeclare a private symbol. This is a private-API dependency to re-check at every
   milestone bump; the alternative is writing our own allocator.
3. **Skia m147 needs Vulkan 1.4 headers** (`VkPhysicalDeviceVulkan14Features`,
   `VkPhysicalDeviceHostImageCopyFeatures`, `VkPhysicalDeviceDynamicRenderingLocalReadFeatures` in
   `VulkanPreferredFeatures.h`); Ubuntu 24.04's `libvulkan-dev` is 1.3.275. The installer ships the
   headers Skia was built against (1.4.345) as `<prefix>/include/skia-vulkan`, C headers only —
   1.5 MB of the 21 MB, skipping the C++ bindings. Consumers must put it **ahead of** `/usr/include`
   and link the loader by path, not through `Vulkan::Vulkan`, whose interface drags `/usr/include` in.
   Step 2.0's Docker image needs the same, so do not rely on the distro's `libvulkan-dev` headers.

*Revisit if:* the milestone moves off m147, or Skia exposes a public memory-allocator factory.


**2026-09-14 · `src/gpu` is one device, one context, recorders and pools owned by the device.**
Plan step 2.2. `GpuDevice` is a singleton holding the Vulkan instance/physical device/device/queue
and the single Graphite `Context`; `GpuSurfacePool` is thread-local and recycles render targets;
`GpuFrame` borrows one for its lifetime and adds `upload()`/`readback()`. Off unless `OPENSHOT_GPU`
is `vulkan` or `lavapipe` — verified, not assumed: the checks assert that an unset variable leaves
the device unavailable on a machine with a working GPU. The adapter index comes from
`Settings::HW_EN_DEVICE_SET`, the same knob `FFmpegWriter` uses for the encode adapter, so a
two-GPU machine sends render and encode to one card by default.

**`GpuDevice` is pimpl'd, and that is not a style choice.** `src/CMakeLists.txt` installs every
`src/**/*.h`, so a consumer compiles against these headers with no idea which Skia the library was
built with. If `OPENSHOT_HAVE_SKIA_GPU` changed the class layout, a service built against the
installed headers would disagree with the library about object size. The pimpl and the
forward-declared `skgpu::graphite::Context`/`Recorder` keep one layout for both builds; with the CPU
Skia the same class compiles to a stub whose `available()` is always false.

**Ownership, learned the hard way (two crashes, both caught by the checks):**
*Nothing that Graphite hands out may outlive the `Context`.* Two things wanted to:

1. Pooled surfaces. `DestroyInstance()` now empties every registered pool *before* resetting the
   context — pools are thread-local, so they register themselves in a global list for exactly this.
2. `Recorder`s. The first version kept one in a `thread_local unique_ptr`, which survives the device
   and is destroyed later against a freed context — a segfault in
   `VulkanResourceProvider::~VulkanResourceProvider` under the NVIDIA driver. The device now owns
   the recorders in a map keyed by thread id and clears them first in `teardown()`; the
   `thread_local` is only a lookup cache, keyed on `GpuDevice::Generation()` so it cannot go stale.

`Generation()` is bumped on every teardown and is the general mechanism: anything caching a GPU
object across calls records it and drops the cache when it moves.

*Verified* (`tests/gpu/openshot-gpu-checks`, on the RTX A2000 and on lavapipe): 200 device
create/destroy cycles with VRAM flat at 11 MiB; 1000 random 64x64 RGBA upload→readback round trips
bit-identical; the pool returns the same allocation for a repeat size and a new one for a different
size; and the pool survives a device restart. Both `tools/golden.sh check` runs — CPU Skia and GPU
Skia — are green at 292/292, so swapping the Skia build does not move a single pixel of raster
output, which is what the byte-identical GN args were for.

*Revisit if:* rendering ever needs more than one Graphite context, or device teardown has to work
while other threads are rendering (today it does not, and both `DestroyInstance()` and
`DiscardAllPools()` say so).


**2026-09-14 · The glow ray-march runs on the GPU; R2a's gates are met, with no re-baselining.**
Plan step 2.3. `TextGlowRenderer::paintGlowFromSilhouette` takes its working surface from
`GpuSurfacePool` through `GpuFrame` when `GpuDevice::Instance().available()`, runs the **unchanged**
SkSL, and reads the result back into a raster N32 image for the text canvas. Nothing else in the
text engine changed. With `OPENSHOT_GPU` off, `GpuFrame::Create` returns null and the raster path
runs exactly as before.

| 1080p, render | GPU off | GPU on | gate |
|---|---|---|---|
| `text_animated_glow_3` | 1.3 fps, p95 1060 ms | **4.5 fps, p95 281 ms** | ≥ 4 ✅ |
| `everything` | 1.8 fps, p95 625 ms | **4.3 fps, p95 261 ms** | ≥ 3 ✅ |

Measured back to back on one machine; treat the deltas as the result, not the absolutes (this
laptop currently runs `single_video` at 112 fps against a 81 fps recorded baseline). No scenario
regressed: `single_video` 111.7→115.0, `subtitles_words` 107.5→108.0, `text_static_4` 60.7→60.2,
`grid_3x3` 23.0→21.7, `heavy_effects` 9.6→10.5.

**Golden is green with the GPU on, unchanged — 292/292, no re-baseline.** The plan allowed the text
scenarios one re-baseline within a Loose SSIM ≥ 0.95; it was not needed. `text.glow` comes back at
PSNR 64.3 / SSIM 0.9998 / max channel delta 2, and the whole suite passes the normal comparison on
NVIDIA and on lavapipe. Four configurations are green: CPU Skia, GPU Skia with the GPU off, GPU
Skia on Vulkan, GPU Skia on lavapipe.

**The R/B swap does not need removing on the GPU path, and removing it would be a bug.** The plan
expected to drop the swap in `SkiaRenderer::parseColorString` because raster N32 is BGRA on x86 and
a GPU RGBA surface is not. That reasoning does not apply: the swap produces a *logical* `SkColor`,
and Skia's colour types are logical, not byte layouts. Rendering that colour to a `kRGBA_8888` GPU
surface and reading it back into an N32 pixmap converts correctly in both directions, so the
convention survives the round trip untouched. The golden text scenarios confirm it — a red glyph
stays red.

**Graphite does not implicitly upload raster images, and fails silently when you assume it does.**
The first working version drew *no glow at all*: the silhouette is a raster `SkImage` used as the
runtime effect's child shader, and Graphite logs `Couldn't convert SkImage to a Graphite-backed
representation` / `draw dropped!` and carries on. Ganesh uploaded such images automatically.
Every image crossing onto a GPU surface now goes through `GpuFrame::ToTexture`
(`SkImages::TextureFromImage`), and a failed upload drops the GPU surface rather than the glow.
Note the ordering this forces: the working surface must be chosen *before* the shader is built,
because the choice decides which image the shader is built from.

`GpuSurfacePool` surfaces take a colour space, defaulting to **null** — Skia's legacy mode, matching
the `SkImageInfo::MakeN32Premul` raster surfaces they replace. An sRGB colour space here would
silently make every blend gamma-correct and change the output.

*Revisit if:* the silhouette itself moves onto the GPU (it is still rasterised on the CPU and
uploaded once per frame), which is the obvious next gain and belongs with R2b.
*Resolved by step 2.4:* the silhouette is now drawn on the GPU and the upload is gone.

### Step 2.4 — the whole text engine on GPU surfaces (2026-09-14)

**Where an offscreen lives is a property of its destination, not of the offscreen.** The text
engine builds several offscreens per frame — two glow silhouettes, the baked 3D block textures, the
large-sigma shadow — and every one is snapshotted and drawn onto a destination canvas. Putting them
on the "wrong" side costs a full copy per frame each way, and for a shader child Graphite does not
copy at all, it drops the draw. So they all go through `GpuOffscreen::Match(destination, w, h)`,
which hands back a pooled `GpuFrame` for a GPU canvas and `SkSurfaces::Raster` for a raster one —
both `kN32` premultiplied, exactly the `SkImageInfo::MakeN32Premul` the call sites used before.
`TextClipReader::renderToQImage` picks the side once, for the frame, and nothing below it crosses
back.

**A pooled surface carries the previous user's CANVAS STATE, and clearing the pixels does not
reset it.** This cost most of the step's debugging time. `SkSurfaces::Raster` hands out a fresh
canvas every time, so call sites written against it apply `scale()` or `translate()` at the base
level with no matching `restore()` — correct for a surface used once, silently compounding for a
recycled one. The glow silhouette drawn at scale `s` came back at `s²` on the second frame and `s³`
on the third: `text.anim_loop_pulse_with_glow` frame 1 passed and every later frame failed, with
the glow shrinking and drifting. `GpuSurfacePool::acquire` now does `restoreToCount(1)` +
`resetMatrix()` before handing a surface back, and `openshot-gpu-checks` has a `pool-canvas` check
that fails without it. Treat "contents undefined, canvas state fresh" as the pool's contract.

**`SkSurface::readPixels` is not implemented for Graphite** — it returns false immediately rather
than failing loudly, so a caller that uses it silently falls back to its raster path and looks
merely slow. `Context::asyncRescaleAndReadPixels` + `submit(SyncToCpu::kYes)` is the only readback
route. Its cost is pure bandwidth (~6 GB/s measured), not per-call overhead.

**The R2a-era glow readback is conditional now.** `paintGlowFromSilhouette` keeps its GPU working
surface for a raster destination (it is worth a readback on its own), but when the destination is
GPU-backed it snapshots straight into VRAM and never touches the CPU.

*Not done here, and it blocked the step's gate:* `TextClipReader` sized one clip's frame buffer at
2536 × 16969 (164 MB). ~~because `computeAnimatedExtent` bounds a perspective animation matrix with
`SkMatrix::mapRect`, which blows up as a corner approaches the vanishing point~~ — **that diagnosis
was wrong**; the clip has no perspective at all. Fixed in worklist item A: a ~100×-too-large `ty` in
the golden recipe plus a glow margin that padded both axes from the longer one. See STATUS.md.

### Step 2.5 — do NOT skip the large-sigma shadow downscale on GPU surfaces (2026-09-14)

**Measured and rejected.** The plan assumed the σ > 120 downscale in
`TextClipRenderer::renderShadowLayer` is nothing but a workaround for Skia's CPU mask-blur clamp,
so a GPU destination should draw the true sigma directly. The premise about the clamp is right:
`SkBlurMaskFilterImpl::computeXformedSigma` clamps at 128 px and only the raster and Ganesh
mask-filter paths call it; Graphite never sees a mask filter (its `Device` asserts so) because
`SkCanvas` first converts it through `asImageFilter`, which passes the sigma to
`SkImageFilters::Blur` unclamped. Drawing directly does produce the full blur — a 4K shadow came
back at **83.7 dB PSNR** against the CPU reconstruction, and reproduced the 1080p shadow just as
well (RMSE 0.00698 vs 0.00700 for the downscale path).

**But the downscale is an optimisation in its own right, not only a workaround.** At σ = 384 it
blurs a surface scaled by 120/384, i.e. ~10× fewer pixels. Skipping it, one-time 4K shadow render,
two A/B rounds on one machine:

| | before | after | |
|---|---|---|---|
| GPU off | 111.4 / — ms | 111.5 / — ms | unchanged (CPU path untouched, bit-identical) |
| Vulkan | 27.2 / 28.8 ms | 32.0 / 28.1 ms | no measurable difference |
| lavapipe | 145.9 / 141.2 ms | 171.5 / 186.6 ms | **~30 % slower, both rounds** |

Nothing to gain on a real GPU, and a supported configuration (lavapipe, the no-GPU fallback) gets
materially slower. The change was written, verified four-way green, measured, and reverted.

*Revisit if:* a case appears where the reconstruction is visibly wrong rather than merely
approximate, or Skia's raster blur engine stops downscaling large sigma internally.

**The step's gate belongs elsewhere.** `text_static_4` at 2160p ≥ 15 fps cannot be moved by this
step: the shadow renders **once** per clip (static text is served from the resting-frame cache), so
its ~120 ms is amortised to ~0.8 ms over 150 frames. Timing each of the scenario's four clips at
2160p gives 0.98 / 0.04 / 0.10 / 0.01 ms per steady-state frame — about **1 % of the scenario's
~90 ms frame**. The rest is decode and Qt compositing, which is R3.


### One control for all GPU use (2026-09-14, project owner)

Asked for directly: "keep all in single control so we can turn on or off gpu usage… later we'll add
all remaining gpu logic to the same control". So:

**`GpuDevice::SetBackend(Backend::Off|Vulkan|Lavapipe)` is the switch**, overriding `OPENSHOT_GPU`
at runtime. It takes effect immediately by tearing down a device built for the old choice, which
moves `Generation()` — which is already the signal every GPU cache must key on, so nothing new is
needed to make the change safe. `RequestedBackend()` answers without creating the device;
`BackendFromName`/`BackendName` convert to and from the `OPENSHOT_GPU` spelling.

The switch only stays *single* because of an invariant, not because of the function: **every GPU
path asks `GpuDevice::Instance().available()`** — or `GpuOffscreen::Match` / `GpuFrame::Create`,
which ask for you — **and none reads the environment or keeps a flag of its own.** All ten GPU
decision points in `src/` were audited against this. The `control` check in `openshot-gpu-checks`
holds the line: it asserts that `Off` makes `GpuFrame::Create` return null and that re-enabling
works, so future GPU logic with its own flag fails the checks.

*Revisit if:* GPU use ever needs to be per-Timeline rather than per-process; the switch is global
today because the Graphite context is.

### Subtitles follow their destination; the Timeline keeps a raster canvas (2026-09-14)

`src/subtitle` builds no offscreen of its own, so the whole pass runs wherever its canvas lives.
`SubtitleManager::renderAtFrame(SkCanvas*, w, h, frame)` is the entry point and the `QImage`
overload wraps it; a GPU canvas gives a bit-identical result (`subtitle-gpu` check, worst channel
delta 0, on Vulkan and lavapipe).

**The Timeline deliberately does not use it yet.** Subtitles composite onto an existing video frame,
so a GPU pass means uploading that frame and reading it back: **5.6 ms at 1080p** (1.1 up + 4.4
back) and **18.7 ms at 2160p** (3.8 + 15.0), against **0.27 ms / 0.61 ms** of actual drawing — 21×
and 31× more than it saves. This is the same rule as step 2.4's "where an offscreen lives is a
property of its destination", applied one level up. *Revisit when:* the compositor puts the frame on
the GPU (phase 3); the caller then passes its canvas and the transfer disappears.

### Cache the composited glow, not the silhouette (2026-09-14)

Plan step 2.6 asked for a long-lived `SkiaRenderer` and caches for the glow silhouette and the 3D
block bake. Measured, those are worth ~0.5 % and ~4 %: the glow is ~99 % of an animated glow frame
and the ray-march is ~91 % of the glow (`OPENSHOT_GLOW_STEPS` sweep — 9.3 ms/step, ~21 ms
intercept), the entire non-glow frame is ≤ 1.1 ms, and `SkiaRenderer`'s expensive half already lives
in the `SkiaFontResources` singleton.

What is cached instead is the **composited glow image**, because a block-mode animation concats its
transform onto the *canvas* before the block is drawn — so the march happens in block-local space
and the result is composited with a single `drawImage`. Redrawing the stored image is therefore the
same draw call with the same image: **bit-identical**, not an approximation. `text::GlowFrameCache`
lives on `TextClipReader`, which is what keeps the key to three fields (within one reader with no
glow-affecting style keyframe, layout/paint/glow style are fixed by construction). A miss costs
exactly what the uncached path cost, so nothing gets slower. Worth **+19 %** on
`text_animated_glow_3` and **+22 %** on `everything`, on the CPU path.

**GPU images are deliberately not cached.** `GpuFrame::snapshot()` comes off a pooled surface that
returns to the pool when the frame dies, and a cached texture would also have to be dropped before
the Graphite context goes away. `paintGlowFromSilhouette` returns null on the GPU path so this
cannot be got wrong by accident. *Revisit if:* the pool grows a way to retain a surface safely
across the context's lifetime.

*Left on the table:* the cache misses when the block's **opacity** animates (a fade), because the
alpha is folded into the ray and bloom paints before they are Screen-composited. Caching the ray
layer at alpha 1 would cover fades, but needs an extra surface per frame — which would cost every
cache *miss*, and the standing constraint forbids slowing the CPU path.


### Step 2.0 — the GPU-capable image, and what it did *not* need (2026-09-15)

The service image now runs unchanged on a CPU node and a GPU node.
`../video-rendering-service` branch `feature/gpu-rendering`.

**The runtime base moves to the shared CUDA 12.8.1 / FFmpeg 6.1 image**
(`europe-west4-docker.pkg.dev/podcastle-repos/podcastle-dev/cuda12.8.1-cudnn9.7.1-ffmpeg6.1-nvidia24.04:0.1`,
the one `vfx-processor` and `video-transcoder` already use), not plain `ubuntu:24.04`. That is where
`h264_nvenc` comes from; Ubuntu's `ffmpeg` / `libav*-dev` packages are consequently **removed** from
the runtime stage rather than layered on top of it, or the image would carry two copies of the same
sonames. Google Chrome and `gdebi-core` are dropped: ~130 MB, and nothing in `src/` ever referenced
`CHROME_PATH`. Base images are `ARG`s so CI can pin digests without editing the Dockerfile.

`NVIDIA_DRIVER_CAPABILITIES=compute,utility,video,graphics`. **`graphics` is the load-bearing word**
— without it `nvidia-smi` works and NVENC works, but the container runtime does not inject the
Vulkan ICD, so `OPENSHOT_GPU=vulkan` silently renders on the CPU. That is the failure mode to look
for first when a GPU node is slower than expected.

**Two things the plan expected that turned out to be wrong:**

- **The image does not need Skia's Vulkan 1.4 headers.** `CLAUDE.md` said it must. It does not:
  libopenshot is vendored into the service as a *prebuilt* `.so` with Skia static inside, so nothing
  in the image ever compiles against Skia's GPU headers. What the image does need is `libvulkan1` —
  on **every** node, GPU or not — because the Graphite-capable `libopenshot.so` names
  `libvulkan.so.1` in `DT_NEEDED`. With no ICD installed the loader simply reports zero devices,
  which is the supported answer.
- **`ENCODER=h264_nvenc` alone was not enough to get usable NVENC output.** See below.

**Fallback is a probe, not a guess.** `RenderBackend::videoCodec()` opens the requested encoder once
per process and falls back to `libx264` with a logged reason if it fails. The probe encodes at
**320×240, not 64×64**: NVENC rejects frames below its minimum dimensions ("Frame Dimension less
than the minimum supported value"), and the first version of the probe read that as "no GPU" on a
machine that had one. `tools/gpu-preflight.sh` makes the same three checks visible at container
start and never fails the container.

### FFmpegWriter: colour tagging for every encoder, and NVENC is not VAAPI (2026-09-15)

Two library changes fell out of step 2.0. Both are needed for `h264_nvenc` to be a real option and
neither touches the software path.

**`SetOption` now accepts `color_primaries` / `color_trc` / `colorspace` / `color_range`.** These
live on the `AVCodecContext`, not in `priv_data`, so it is one call for every encoder. Before this,
the only way to tag an H.264 stream was `x264-params colorprim=…:transfer=…:colormatrix=…`, which
does nothing on any encoder but x264 — the service's BT.709 tagging would have been silently lost
on NVENC. Verified by encoding with and without: `unknown,unknown,unknown` → `bt709,bt709,bt709`.
The service's x264 branch is deliberately **left on `x264-params`**, byte-for-byte as it shipped.

**NVENC no longer inherits the VAAPI-shaped H.264 overrides.** Upstream's `USE_HW_ACCEL` block
applied, to every hardware H.264 encoder, `profile = CONSTRAINED_BASELINE`, `preset = slow` and
`tune = zerolatency`. On NVENC that is wrong three ways: it supports High profile and B-frames, its
presets are `p1`..`p7` (legacy `slow` is far slower than wanted by default), and `zerolatency` is
not one of its `tune` values at all — it fails to parse and logs on every export. Measured effect:
`h264_nvenc` output came out **profile 77 (Main)** where libx264 gives **100 (High)**, at a preset
nobody chose. NVENC now keeps whatever the caller set and only has `profile=high` forced, because
`SetOption("profile")` writes the integer `AVCodecContext` field, which NVENC ignores in favour of
its private option — there is no way to ask for High from outside. After the change both encoders
produce profile 100. VAAPI, DXVA2 and VideoToolbox are untouched.

Rate-control tuning proper is still plan step 1.4; the service's NVENC settings (`rc=vbr`, `cq=19`,
`preset=p5`, `tune=hq`) are a conservative starting point, not a tuned one.

### W11 — the four decisions the compositor bakes in (2026-09-16)

Taken together because Stage 5 and Stage 6 both depend on them, and deciding them mid-rewrite means
redoing work. Each was decided from a measurement or from what is actually in the tree, not from the
recommendation the plan carried.

**Timeline canvas precision: `kRGBA_8888`, not F16.** This overrides the recommendation that stood
in this file ("F16 for the timeline canvas, 8888 for cached textures"), which was written before the
surrounding constraints were settled. Three reasons:

- Everything the service ships is **8-bit H.264**. F16's headline benefits — 10-bit output, HDR —
  buy nothing that is on the roadmap today.
- Pooled surfaces are deliberately **null-colour-space** (see the entry above), matching the
  `SkImageInfo::MakeN32Premul` raster surfaces they replace. F16 on a null colour space buys
  precision through stacked blends, **not** gamma-correct blending; it does not make compositing
  more correct, only less lossy between stages.
- 8888 keeps the GPU canvas **bit-identical to the CPU path**, which is the strongest regression
  signal available and currently free: 282 of the suite's 292 frames come out bit-exact today.

Cost of the choice: repeated 8-bit rounding between composite stages, which shows as banding on
deeply stacked blends before it shows anywhere else.
*Revisit if:* 10-bit or HDR output reaches the roadmap, or `blend_stack_5` / `heavy_effects` show
visible banding once the effect shaders land in W19. Measure before switching — the cost is a
doubling of pool memory (1080p 8.3 → 16.6 MB, 2160p 33 → 66 MB per surface **per thread**).

**Graphite only. Ganesh is not kept as a fallback.** Settled by what is in the tree rather than by
preference: there is **no Ganesh code in `src/`**. `GrDirectContext` and `skgpu::ganesh` match
nothing under `src/` or `tests/gpu/` except one explanatory comment in `GpuFrame.h`, and
`/usr/local/skia-gpu` was built with `SKIA_ENABLE_GANESH=false`. Keeping Ganesh "alive" would mean
*writing* a second backend and then maintaining two, not preserving something that exists. Graphite's
two known sharp edges are already understood and guarded (no synchronous `readPixels` — `GpuFrame::readback`
is the only route; no automatic raster-image upload — everything goes through `GpuFrame::ToTexture`).
*Revisit if:* Graphite drops or breaks a feature the compositor needs and the workaround is worse
than a second backend. The GN flag stays, so rebuilding with Ganesh remains a build away.

**Nearest-neighbour sampling stays nearest.** `BORDER_REFLECTED_ROTATION`
(`image-processing-lib/src/Effects/effects.cpp:253`, `cv::INTER_NEAREST` with `cv::INTER_LINEAR`
commented out beside it — a deliberate choice, not an oversight) and `applyDisplacementMapEffect`
(explicit `static_cast<int>(nx + 0.5f)`) both live in the **submodule the web front end runs through
WASM**. Switching only the GPU shader to bilinear would open exactly the editor-vs-export gap that
the LUT decision below exists to close, and would do it on the two effects where the artefact is
most visible.
*Revisit if:* the front end moves to bilinear — then both move together, in the submodule, and the
affected goldens are re-baselined in one change.

**LUT rounding: match the front end at the LUT's native cube size.** Measured rather than asserted,
with `tools/analysis/lut-parity.cpp` (standalone; `resampleLut3D` copied verbatim from
`ColorGradingCore.cpp`). Both paths evaluated over the 8-bit cube — 636,056 colours, every 3rd level
per channel — on the golden 33³ LUT:

| comparison | max | mean | >1 LSB | >2 LSB |
|---|---|---|---|---|
| `ColorMap` 17³ vs front end 33³ trilinear | **17.05 LSB** | 0.404 | 9.79 % | 2.38 % |
| tetrahedral vs trilinear, both 33³ | 4.34 LSB | 0.060 | 0.18 % | 0.01 % |

The editor-vs-export gap is therefore **almost entirely the 17³ resample**, not the interpolation
kind — which reframes the question as the plan posed it ("trilinear or tetrahedral?"). Note the
resample loses nothing at its own sample points: 33 → 17 lands every destination sample exactly on
an even source index (`ir * (1/16) * 32 == ir*2`), so the whole 17 LSB is the coarser grid being a
worse approximation, not resampling error.

A GPU 3-D LUT texture with hardware trilinear filtering **is** the front end's native-size trilinear,
so matching it is simultaneously the more accurate option and the simpler shader; reproducing
`ColorMap.cpp` would mean re-implementing an L1-cache optimisation that has no reason to exist on a
GPU.

Two consequences that must be carried into W19, not discovered there:

1. **The CPU `ColorMap.cpp` has to drop the 17³ resample too**, or CPU and GPU diverge by up to
   17 LSB and the four-way sweep stops meaning anything. That re-baselines the `effects.*lut*`
   goldens and costs CPU LUT throughput — the resample exists for cache friendliness. Measure it.
2. **Which interpolation the front end passes is still unknown from this repo.** `apply_lut(w, h,
   amount, interpolation)` takes it as a caller argument (`lutWrappers.cpp:87`). On fine LUTs the
   choice is worth ≤ 4.3 LSB and does not matter much; on a **coarse** LUT it dominates everything
   else — on the 2³ `domain-3d-lut.cube` trilinear and tetrahedral diverge by up to **98 LSB**
   (mean 20.8, 64 % of channels over 1 LSB), because no resample happens and there are only 8
   corners to interpolate between. Confirm the front end's value before writing the shader.

*Revisit if:* the front end changes cube handling or interpolation. The two must move together.

### The golden suite now gates 76 of 95 scenarios bit-exact (2026-09-16, W12 sub-task)

**Correction to the first version of this entry**, which claimed no scenario used
`Tolerance::Exact()`. That was wrong: the grep behind it searched `tests/golden/*.cpp` and missed
`tests/golden/scenarios/`. **25 scenarios were already exact-gated** — 10 registration sites, one of
them a loop over all 16 blend modes.

What was true is that the gate was looser than the evidence allowed. Measuring every scenario across
the full four-way sweep (CPU Skia; GPU Skia off; Vulkan; lavapipe) and taking the worst result per
scenario:

| | scenarios |
|---|---|
| bit-exact in **all four** configurations | 77 |
| of those, already gated `Tolerance::Exact()` | 25 |
| **newly tagged `"exact"`** | **51** |
| now gated exact | **76** |
| deliberately left out: `export.roundtrip_x264` | 1 |

`export.roundtrip_x264` is excluded on purpose — its frames happen to match, but it is a lossy codec
round trip and its `Tolerance::Codec()` also governs its `Check` entries. "Exact" is the wrong idea
for it.

No already-tagged scenario failed to qualify, so nothing was mis-gated before. All four
configurations stay at 292/292 with the tighter gate.

**The 17 `text.*` scenarios are not bit-exact and are not tagged.** They are the one real finding
here: text drifts **GPU-vs-CPU by up to 106 LSB** (`text.curved`, worst PSNR 39.99;
`text.gradient_fill_stroke_shadow` 41.68 with 20.9 % of pixels over 2 LSB). This is expected rather
than broken — Skia rasterises glyphs differently on the GPU than on the CPU, which is why
`Text.cpp` puts them all on `Tolerance::Loose()` with the comment "glyph anti-aliasing differs
across Skia/FreeType builds". But it is worth stating plainly, because "292/292 four ways" reads as
a much stronger claim than it is: **for text, the GPU and CPU paths are visibly different images,
agreeing only to PSNR ≥ 38.**

Two groups will stop being exact later, by design, and a session that hits it should not think it
broke something:

- `clipfx.blur`, `clipfx.shadow`, `clipfx.shadow_blur_rotated`, `clipfx.shadow_colored_sharp` —
  **W14** replaces the hand-rolled blur and shadow with `SkImageFilters`, and W14's own gate already
  declares that "close" (SSIM ≥ 0.97 on shadows, PSNR ≥ 40 dB on blur).
- `subtitles.animated_in_out`, `subtitles.one_word_container`, `subtitles.per_time` — bit-exact only
  because the Timeline deliberately hands subtitles a *raster* canvas today. **W17** puts them on
  the GPU canvas, at which point they will drift the way `text.*` already does.

In both cases the exact tag is doing its job: it forces the re-classification to be a decision
someone writes down, instead of a silent drift inside a PSNR ≥ 45 budget.

*Revisit if:* a scenario starts failing its exact gate — that is the signal to look, not to relax
the tolerance.

## Open — decide before plan phase 4

The four that blocked the compositor were taken on 2026-09-16; see the W11 entry above.

- **Which interpolation the front end passes to `apply_lut`.** Follows from the W11 LUT decision and
  is the one fact that repo could not supply. Worth ≤ 4.3 LSB on a fine LUT, up to 98 LSB on a
  coarse one. Needed before the W19 `ColorMap` shader, not before W12.
- **GPU SKU for the node pool.** L4 is the working assumption (24 GB, two NVENC engines, no session
  cap, AV1).
- **Whether the front end adopts the same SkSL sources** through CanvasKit. Not required for the
  server work, but it is the only way to make editor and export pixel-close for transitions.
