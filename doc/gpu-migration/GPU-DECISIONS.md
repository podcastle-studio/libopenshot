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
**Superseded by the entry below — W09 tuned it.**

### W09 — one quality knob for both encoders, calibrated at `cq = crf + 10` (2026-09-18)

**Decision.** `SetOption(VIDEO_STREAM, "crf", N)` is the quality control for the hardware path as
well as the software one. On NVENC the writer maps it to `rc=vbr`, `cq = N + 10` (clamped 0–51) and
`bit_rate = 0`; every other hardware encoder keeps the legacy bitrate estimate untouched. Callers
stop carrying a second, encoder-specific number: the service and `tests/golden/Recipes.cpp` both now
say `crf 18` for x264 and NVENC alike.

**Why a mapping and not a passthrough.** `crf` and `cq` are different scales. Measured against a
lossless reference on `podcast_pip` at 1080p, the offset that puts NVENC on x264's rate-distortion
point is +9 to +10 at `crf` 18, 23 and 28 — stable enough to be a property of the two encoders. +10
is the better-centred end of that range across `podcast_pip`, `transitions_chain` and
`subtitles_words`. The full tables are in `doc/PERFORMANCE-BASELINE.md`.

**Why this was worth doing at all.** Before it, `SetOption("crf")` on a hardware encoder *discarded
the value* and set a bitrate from `info.video_bit_rate` through an undocumented exponential — so the
only usable knob was `cq`, and the `cq 19` that shipped as a placeholder was spending **2.1× the
bits of libx264 crf 18 for 0.16 VMAF points**. At `cq 28` the two encoders land within 1.7 % on file
size and 0.21 VMAF.

**What the writer now sets for NVENC on its own,** both overridable by a later `SetOption`:
`spatial-aq 1` (measured smaller *and* better: 3.09 MB at VMAF 98.17 against 3.49 MB at 97.69) and
`b_ref_mode middle` when B-frames are actually in use. It also clamps `max_b_frames` to NVENC's
limit of 4 — `add_video_stream` sets 10, and `allow_b_frames 1` therefore threw `InvalidCodec` out
of `avcodec_open2` rather than doing anything.

**Revisit if** the service changes `preset` away from `p5`, moves off H.264, or starts encoding
10-bit: the offset was calibrated at `p5`/`tune hq`/8-bit 4:2:0 and is not claimed beyond that.

**Not taken: B-frames by default.** Now that `allow_b_frames` works on NVENC it measures as a wash
at matched `cq` (−2.3 % size for −0.08 VMAF), which does not justify changing the bitstream every
consumer receives. Left to the caller.

### SkSL is the one shader language, on both sides (2026-09-18, project owner)

**Decision.** Effect and transition shaders are written **once in SkSL** and run through Skia on
both stacks: `SkRuntimeEffect` on the server (Graphite/Vulkan), `CanvasKit.RuntimeEffect` in the
browser. No second dialect, no generator, no hand-port.

**Why this and not the two-emitter design** that `TRANSITION-PARITY.md` proposed (one restricted
GLSL subset, emitters for SkSL and for a PixiJS GLSL ES filter): the front end **already ships
CanvasKit** — our Skia is pinned to `SKIA_MILESTONE=m147` precisely to match it, for the text and
glow work. So Skia is not a new dependency there, and once both sides compile the *same source with
the same compiler*, parity is by construction rather than by test. The two-emitter design buys the
same result only if a conformance suite keeps catching drift forever.

**What this replaces.** It supersedes the "two thin emitters" section of `TRANSITION-PARITY.md` and
closes that note's last open question ("whether the front end adopts the shared shaders at all").
The C++ in `image-processing-lib` **stays** as the CPU oracle and the server's no-GPU fallback —
that is unchanged, and the standing constraint requires it.

**What it does not change.** Same source is not same pixels. These still hold exactly as written:

- **Explicit `texelFetch` + manual lerp, explicit mirror arithmetic.** CanvasKit on WebGL and
  Graphite on Vulkan are still different drivers on different hardware, and neither API pins
  bilinear weights or border behaviour to bit precision. The 9 resampling effects are still where
  all the risk is.
- **The six conventions** in `TRANSITION-PARITY.md` — alpha, coordinate origin and pixel centres,
  colour space, edge rule, precision, and a declared reference resolution for every radius.
- **The reference-resolution fix is still a product decision**, still open, and still gates W20.

**The one dependency this rests on, and it is not ours to answer.** The front end must be able to
put *video frames* through CanvasKit for effects, not only text. Today it runs PixiJS for video and
mutates pixels in the OpenCV WASM on the CPU. If that pipeline cannot move, this decision has to be
revisited and the two-emitter design comes back. **Confirm with the front-end team before W19
writes its first shader.**

**Revisit if** the front end cannot route video frames through CanvasKit, or if CanvasKit's WebGL
backend measures materially slower than the Pixi path it would replace.

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
### The GPU compositor draws whole frames or none of a frame (2026-09-16, W12)

The fast-path clip draw (`Clip::draw_to_canvas`) resamples with Skia's bilinear where
`apply_keyframes` uses QPainter's smooth transform. The two differ by a few LSB — harmless on its
own, median 58.8 dB across the scenarios it touches.

It stops being harmless the moment a **CPU** blend mode reads that backdrop. The non-linear W3C
modes magnify it violently: measured on a mixed frame, `compositing.blend_color_burn` came out at
**26.68 dB with a max difference of 255** (colour-burn divides by the backdrop) and
`blend_saturation` at 44.71 dB / max 128, from a backdrop that differed by about 3 LSB.

So the canvas is attached only when **every** clip on the frame qualifies for the GPU path
(`Timeline::GetFrame` asks each of `nearby_clips` before allocating). One frame, one path. That
removes the entire amplification class rather than tuning tolerances around it, at the cost of
giving up the GPU on mixed frames until W13 puts the blend modes on `SkBlendMode`.

*Revisit at:* W13. Once the blend modes composite on the GPU too, "every clip qualifies" stops being
restrictive, and the all-or-nothing rule can relax to per-clip.

### QPainter rounds a translate-only transform; the GPU path must too (2026-09-16, W12)

Worth recording because it cost real quality and was invisible without instrumenting the draw.

The first version of `draw_to_canvas` always resampled. That took the text scenarios from ~46 dB
against the CPU goldens down to ~36 dB — `text.curved` to 32.02, well under even the `Loose()` gate
of 38 that text runs on. The cause was not scaling: a text clip's transform is a **translation by
190.5, 165.0** — half a pixel. Qt's raster engine reduces a transform of type `TxTranslate` to an
integer blit, with no filtering and no edge antialiasing; Skia resampled it and softened every glyph
edge.

Reproducing Qt's rule — `t.type() <= QTransform::TxTranslate` → round and blit unfiltered — restored
those scenarios to **exactly** their previous numbers (45.86, 43.12, 39.99), which is the sign the
rule is right rather than merely better. A tolerance-based check would have absorbed this silently;
the exact gating is what surfaced it.

### What the fast-path clip draw actually buys (2026-09-16, W12)

Measured interleaved on one machine, 1080p render, GPU off against Vulkan on the A2000:

| scenario | GPU off | Vulkan | change |
|---|---|---|---|
| `grid_3x3` | 19.3–19.9 | **26.7–27.4** | **+38 %** |
| `single_video` | 96.9–101.6 | 101.4–104.3 | +4 % |
| `podcast_pip` | 19.4–20.1 | 18.0–18.9 | **−5 %** |

The pattern is the per-clip upload. This slice still moves every source image across PCIe once per
clip per frame, so it wins where a clip's source is small relative to the area it is drawn over
(`grid_3x3`: nine small tiles, nine intermediates and nine full-frame composites removed) and loses
where a few large sources are uploaded to replace composites that were already cheap
(`podcast_pip`). Dropping the redundant `RasterFromPixmapCopy` was tried and changed nothing, so it
is the upload itself, not the CPU copy.

**The `podcast_pip` regression is accepted for now, not ignored.** It is confined to GPU-enabled
deployments — `OPENSHOT_GPU` defaults to off, and the CPU path is bit-identical and unchanged in
speed — and the fix belongs to W22–W25, where decoded frames stop being uploaded at all. Re-measure
it there. If GPU is switched on for a `podcast_pip`-shaped workload before then, that is the reason
to gate the fast path on clip count or source size.

### The blend-mode mapping is a naming table, and it is verified as one (2026-09-16, W13)

`BlendModes.cpp` and Skia both implement W3C "Compositing and Blending Level 1", so `ToSkBlendMode`
is a rename rather than a translation. That claim is checked by `tests/gpu/gpu_blend_parity.cpp`
(`openshot-gpu-blend-parity`), which blends the same two 256×256 images — sweeping every (Cb, Cs)
pair including the 0 and 1 edges the spec special-cases — once through `BlendImages()` and once
through `SkBlendMode` with **no transform and no filtering**, so the formula is isolated from the
resampling:

- **lavapipe: all 16 modes agree**, max 1 LSB, mean ≤ 0.002 LSB.
- **NVIDIA: 13 of 16 within 1 LSB**; colour-dodge (max 28), colour-burn (7) and hue (3) have
  outliers on **0.001–0.006 %** of channels, at the division singularities — colour-dodge divides by
  (1 − Cs), colour-burn by Cs — where the driver's float lands the other side of a clamp. Mean stays
  ≤ 0.05 LSB for every mode.

That lavapipe, which is strict software IEEE, is clean on all 16 is what identifies those outliers
as driver float behaviour rather than a mapping error. The test therefore judges on the bulk (mean
≤ 0.2 LSB, ≤ 0.05 % of channels over 2 LSB) and still prints the max, so a genuine divergence — which
would be wrong everywhere — cannot hide behind the threshold.

**This test, not the golden suite, is the real gate on blend correctness.** See the next entry for
why the golden suite cannot be.

### Three blend modes get a tolerance too wide to catch a regression (2026-09-16, W13, project owner)

`compositing.blend_color_burn` (26.5 dB), `blend_hue` (39.6) and `blend_saturation` (44.6) fail even
the `GpuClose` band against the CPU goldens. The cause is **not** the blend: the parity test above
shows the formulas agree on identical pixels. It is that the GPU resamples the clip onto the canvas
with Skia's bilinear rather than QPainter's smooth transform, and these three formulas are steep
enough to magnify that ~1 LSB input difference into a large output one — colour-burn divides by the
source channel, hue and saturation renormalise chroma.

Two options were put to the project owner: keep those three on the CPU path and preserve golden
coverage, or accept the GPU output and widen the band. **The owner chose to widen it**
(`Tolerance::GpuAmplified()`, PSNR ≥ 25 / SSIM ≥ 0.96), keeping all 16 modes accelerated. The GPU
result is also arguably the one the front end produces, since CanvasKit is Skia.

**State the cost plainly: that band cannot detect a real regression in those three modes on a GPU
configuration.** It has to be wide enough to admit colour-burn at 26.5 dB. What guards them instead
is `openshot-gpu-blend-parity`, which is far sharper — run it whenever blend code changes. On the
CPU the same three scenarios remain gated `exact`, and nothing about the CPU path has moved.

*Revisit at:* W22–W25. Once decoded frames stay on the GPU the upload-time resample goes away, the
amplification with it, and these three should return to the normal `GpuClose` band.


### W19 — what a shared effect fragment can and cannot be (2026-09-22)

Three things measured while porting the first effect. All of them apply to the remaining twelve,
so they are here rather than in the item.

**SkSL is the GLSL ES 1.00 intrinsic set, and that is load-bearing rather than annoying.** There
is no `round()` and no `trunc()` — only `floor()`. The first prelude used both, failed to compile,
and fell back to the CPU *silently*, which is the important part of this entry (see below). The
ceiling is not Skia's to relax and it is the same ceiling CanvasKit gives the front end, so a
helper written in terms of `floor()` is one both sides can actually run. Ask the compiler rather
than the documentation: `openshot-gpu-effect-parity --sksl` reads a fragment on stdin.

**The fragments are defined on straight RGB, and reproduce the C++ byte truncation deliberately.**
Every CPU twin in `image-processing-lib` unpremultiplies, operates and re-premultiplies, truncating
to a byte at each step (`static_cast<unsigned char>`), because a libopenshot `Frame` is
`Format_RGBA8888_Premultiplied` while the front end's canvas pixels are straight. A fragment that
did the same arithmetic in clean floating point would be *close* and would need a tolerance band;
reproducing the truncation makes it **bit-exact**, and the effects goldens keep `Tolerance::Exact()`
on the GPU. `GpuEffect::GpuShaderPrelude()` owns the helpers and no fragment open-codes the
rounding.

**One place bit-exactness is not reachable, and it is not an effect's fault.** The shared
unpremultiply divides by `alpha/255`, and Vulkan permits 2.5 ULP on a division where IEEE requires
exact rounding. Over every legal `(premultiplied byte, alpha)` pair — 32,896 of them — **588 differ
by exactly 1 LSB on Vulkan and 576 on lavapipe** (1.8 %), always where the true quotient is an
exact integer and the two paths fall on opposite sides of it: byte 1 over alpha 3 is exactly 85.
Two different rates on two drivers is the proof it is the division and not the fragment.

A contrast or exposure factor then amplifies that 1 LSB — measured at up to **3 LSB** on
`brightness(0.6, 100)`. So the reachable parity class is **exact on opaque pixels, close on
semi-transparent ones**, at PSNR 69–74 dB against a 48 dB gate. Closing it entirely would mean
changing how the *CPU* unpremultiplies, which moves production output and belongs to its own item,
not to W19.

**A pass costs what a full-frame pass costs; the arithmetic is free.** At 1080p one chained shader
pass measures **0.14–0.19 ms** against the item's 0.2 ms gate — but a *do-nothing* passthrough
fragment measures 0.19–0.22 ms in the same harness, and the pool acquire is 0.016 ms of it. The
rest is the `snapshot()` copy plus the read and write of an 8.3 MB surface. Two consequences: the
gate is measuring memory traffic rather than any effect's cost, and the obvious optimisation for
every fragment is to stop copying the source (a zero-copy `SkSurfaces::AsImage` ping-pong instead
of a snapshot), which is worth its own item once more than one effect is on the GPU.

**Single-effect cost is still dominated by the crossing**: 0.15 ms of shader against ~4.1 ms with
the upload and readback around it. `ApplyOnGpu` therefore leaves its result on the GPU and lets
`Frame::GetImage()` read back once, so a chain pays the crossing once — and W22–W25 is what removes
it for the first effect too.

### W19 — three more fragments, and the two things that decide whether one is exact (2026-09-22)

Alpha, Exposure and ColorShift, on top of Brightness. **72 of 88 image/parameter combinations are
bit-exact**; the 16 that are not run 55–75 dB with a 5 LSB worst case, against a 48 dB gate.

The split is not about effect complexity. **A fragment is bit-exact exactly when it does not
unpremultiply**:

| effect | what it does | result |
|---|---|---|
| `Alpha` | scales premultiplied channels directly | **16/16 exact** |
| `ColorShift` | four wrapped integer gathers, no arithmetic | **16/16 exact** |
| `Brightness` | unpremultiply → contrast → shift → premultiply | 26/32 exact, ≤ 3 LSB |
| `Exposure` | unpremultiply → scale → premultiply | 16/24 exact, ≤ 5 LSB |

That is the same division-rounding limit recorded above, now confirmed from the other side: the two
fragments that never divide are exact everywhere, including on `alpha_one` and `noise`.

**ColorShift also settles a question about premultiplied output.** It gathers r, g, b and a from
four *different* pixels, so it can emit colour above its own alpha — invalid premultiplied data,
which the C++ produces too. It is bit-exact, so **Skia does not clamp runtime-effect output to
valid premul**, and a fragment may reproduce a C++ twin that does not maintain the invariant.

**Exposure's divergence is the same division, and a wrong guess about it is worth recording.**
The first reading of this was that `Exposure.cpp`'s `Format_ARGB32` conversion — which `AddImage`
converts straight back to premultiplied RGBA in place, so it is a round trip, not a conversion —
was quantising the pixels before the effect ran and that this explained `exposure(1.0)` differing
from its fragment on 19 % of the `noise` image. **That was wrong, and removing the round trip
proved it: not one pixel of any golden moved and the parity numbers were identical to the digit.**
Qt's unpremultiply/re-premultiply pair is lossless.

Measured exhaustively instead, the whole `exposure(1.0)` chain — `floor(floor(v/a) * a)` — disagrees
on **exactly the same 588 of 32,896 pairs as the bare unpremultiply**, and the byte the fragment
reads out of the texture is correct in **all 32,896**. So there is one cause, not two: the division,
and `osBytes` is trustworthy. The 19 % figure was an artefact of the test image — `setPremul`
clamps each channel to the alpha, so about half of `noise`'s channels have `v == a` exactly, which
is precisely the exact-integer quotient the division disagrees on. Real content is not that
adversarial.

Exposure's one genuinely separate cause is plain: the C++ multiplies by the keyframe as a `double`
and an SkSL uniform is `float`, so at `exposure(4.2)` even fully opaque pixels move by 1 LSB.

The round trip was still removed, on the honest grounds rather than the assumed ones: it is dead
work. Two full-image conversions and an allocation per frame, with provably identical output.
**Re-measured on mains power** (the first figures were taken on battery at ~840 MHz and are
withdrawn): **6.3–7.1 ms with it removed against 7.3–10.1 ms with it in place**, roughly 20 %.

**And an ordering rule that costs 20x if you get it wrong.** `ApplyOnGpu` must come *before* any
`frame->GetImage()` in a `GetFrame`. On a GPU-backed frame `GetImage()` **is** the one readback, so
asking for pixels first flattens the frame and the shader then pays an upload and a readback every
frame. Exposure and ColorShift were both written that way at first and measured **3.2–3.5 ms** a
pass; with the order corrected they are **0.15–0.22 ms**. Nothing about the output changes, which
is why only the timing caught it.

### W19 — Bars, ChromaKey and ColorAdjustment, and what babl's Y'CbCr actually is (2026-09-22)

Seven effects are now fragments. **126 of 160 image/parameter combinations are bit-exact**, and
nothing exceeds 2 LSB outside Brightness and Exposure's 5.

**The "exact iff it does not unpremultiply" rule needed refining, and ColorAdjustment is why.**
It never unpremultiplies — it scales the premultiplied bytes in place, which is what its C++ does —
and it is still not exact, because it carries `double` parameters through per-pixel arithmetic that
an SkSL uniform can only hold as `float`. It never exceeds **1 LSB** (69–102 dB). The rule is
therefore: a fragment is exact when its per-pixel arithmetic is exact in `float` **and** it never
divides by alpha. Bars (no arithmetic at all) and ChromaKey's hard-cut path are exact; anything
scaling by a double-precision parameter drifts by 1 LSB.

**babl's `Y'CbCr u8` is BT.601 studio range, and this was worth measuring rather than assuming.**
ChromaKey's YCbCr method — the only method `../video-rendering-service` ever constructs, hardcoded
with fuzz 70 and halo 20 — keys on Cb/Cr that babl produces, and a fragment cannot call babl. The
textbook full-range "JPEG" mapping is **wrong**: 99.6 % of a 64³ sample grid disagrees, by up to
**16**. Asking babl directly for its response to black, the three primaries and white gives Y in
16..235 and Cb/Cr in 16..240 — studio range:

    Cb = 128 + (-37.797 R - 74.203 G + 112.000 B) / 255
    Cr = 128 + (112.000 R -  93.786 G -  18.214 B) / 255

Against that, **193 of 262,144 samples differ, by 1**. That residual is *not* a rounding mode:
double, float, round-half-away and `trunc(x + 0.5)` all give the same 193, so it is babl's own
constants or intermediate representation. It is left there, because the end-to-end result is what
matters and it is very good: **the service's exact configuration is 8/8 bit-exact**, as is a
zero-halo key; only a deliberately wide halo moves, on 4 and 11 pixels of two images, by 2 LSB.

**ChromaKey feeds babl premultiplied bytes while telling it they are straight** (`R'G'B'A u8`), so
the key is computed from premultiplied colour. The fragment does the same. That is a quirk of the
effect, not of the port, and matching the effect is the job.

**Only the YCbCr method is ported.** The others key on HSV, HSL or CIE LCh coordinates that babl
computes, and reproducing babl's colour science in SkSL bit-for-bit is a different and much larger
undertaking. `SetGpuUniforms` returns false for them and the C++ runs — which is exactly what that
hook is for.

**The ordering rule caught two more.** Bars and ColorAdjustment both fetched `frame->GetImage()`
before calling `ApplyOnGpu`, and both measured ~4.2–4.4 ms a pass instead of ~0.1 ms. That is now
four of seven fragments written the wrong way round on the first attempt, so it is not a slip —
it is the shape of these `GetFrame` functions, every one of which opens by fetching the image.
**Move the fetch below `ApplyOnGpu` as the first step of porting an effect, before anything else.**

### W19 — LightAdjustment and Enhancement in; **Crop is not portable as a fragment** (2026-09-22)

Nine effects are now fragments: **214 of 297 image/parameter combinations bit-exact**, and neither
new effect ever exceeds **1 LSB**.

**A tone curve belongs in a texture, not in the fragment.** LightAdjustment's contrast stage is a
256-entry byte LUT that the CPU memoises from `toneCurve()`, which is built from `pow()` and
`sin()`. Evaluating it per pixel in SkSL would be the obvious port and the wrong one: neither
intrinsic is exactly specified on the GPU, and the `round()` back to a byte would flip wherever the
curve lands near .5. Uploading the CPU's own LUT as a 256x1 texture and sampling it with nearest
makes the stage **exact by construction** — both contrast cases come out 8/8 — and a texture fetch
is cheaper than a `pow()` anyway. The texture is cached on the effect, keyed on the contrast value
**and** `GpuDevice::Generation()`, because a device teardown invalidates it.

**Enhancement is the first effect that is more than one pass**, and it works: clarity and sharpness
each read the *neighbours* of what the previous pass wrote, so they cannot be folded together.
`ApplyOnGpu` is simply called twice, and because each call leaves its result as the frame's GPU
backing, the second pass reads the first's output as a texture and only the last is read back.
`enhance(clarity+sharp)` is 7 of 8 images exact. A subclass selects the pass through a mutable
member set immediately before each call — `SetGpuUniforms` is `const`, so there is no other route.

**Enhancement's grain pass is deliberately not ported.** `applyNoisePass` is built on the classic
GLSL hash `fract(sin(x * 12.9898 + y * 78.233) * 43758.5453)`. At 1080p the argument to `sin()`
reaches ~85,000, where the answer depends entirely on how many bits the implementation carries: the
C++ evaluates it in `double`, a fragment in `float`. They would not differ by an LSB — they would
differ by an arbitrary amount in [0, 1), which the pass scales to as much as **~140 LSB** of grain.
A frame that asks for grain therefore runs entirely on the CPU. Fixing it means changing the CPU's
hash to something reproducible in `float`, which moves production output.

**Crop is not a per-pixel effect and should not be ported as one.** It is `QPainter` with
antialiasing: a rounded-rect clip path and a `drawImage` between `QRectF`s. Two things follow.
The corner coverage is a *rasteriser* difference — Qt's and Skia's antialiasing do not agree, the
same class already on file for the compositor ("Skia's bilinear is not QPainter's smooth
transform") — and the rects are fractional for any keyframe that is not a whole pixel, so even
`radius == 0` antialiases its edges rather than blitting. `resize == true` also changes the image
size, which `ApplyOnGpu` has no way to express.
The production path does not avoid any of this: `../video-rendering-service` sets `resize = false`
and passes a radius *curve*, so rounded corners are the normal case. Porting Crop means accepting a
**redefine**-class change to the corner pixels, which is a product decision rather than a port, and
it belongs with the compositor's parity work rather than with the per-pixel fragments. **Left on
the CPU, with no fragment written.**

### The 0.2 ms per-effect gate is the cost of a pass, not of an effect (2026-09-22, W19)

**Needs restating by the project owner**, in the same way W07's fps gates did.

Measured at 1080p, chained: Alpha 0.12–0.15 ms, Brightness 0.16–0.21, Exposure 0.15–0.19,
ColorShift **0.22**. ColorShift misses the 0.2 ms gate by 10 % because it makes four texture
fetches where the others make one.

But a **do-nothing passthrough fragment measures 0.19–0.22 ms in the same harness**, and the pool
acquire is 0.016 ms of that. The rest is the `snapshot()` copy of the source surface plus the read
and write of an 8.3 MB surface — about 33 MB of traffic before a fragment does any work. So the
gate sits exactly at the floor of one full-frame pass and no fragment can meet it with margin.

The fix is not in any fragment. It is to stop giving every effect its own pass — either a
zero-copy source (Graphite's `SkSurfaces::AsImage` consumes the surface, so it cannot simply
ping-pong through the pool, and this needs design) or composing a chain of effects into one draw.
Either is its own item. Until then ColorShift is **over the written gate and shipped**, on the
grounds that the gate as written is unreachable by construction.

### A GPU test that cannot tell whether the GPU ran proves nothing (2026-09-22, W19)

Worth its own entry because it produced a completely clean result that was completely wrong. The
first run of `openshot-gpu-effect-parity` reported all 32 image/parameter combinations **bit-exact**.
The fragment had not compiled: `ApplyOnGpu` did what it is designed to do, declined, and both arms
of the comparison ran the same CPU code. A parity test whose fallback is the thing it is comparing
against passes hardest when it is most broken.

`ApplyOnGpu` leaves the frame GPU-backed on success and the CPU twin does not, so
`Frame::IsGpuBacked()` after the call is the proof, and the test now fails loudly without it. The
same trap is waiting in any check of a path that has a silent fallback — which, under the standing
constraint, is every GPU path in this fork.

## Open — decide before plan phase 4

The four that blocked the compositor were taken on 2026-09-16; see the W11 entry above.

- **Which interpolation the front end passes to `apply_lut`.** Follows from the W11 LUT decision and
  is the one fact that repo could not supply. Worth ≤ 4.3 LSB on a fine LUT, up to 98 LSB on a
  coarse one. Needed before the W19 `ColorMap` shader, not before W12.
- **GPU SKU for the node pool.** L4 is the working assumption (24 GB, two NVENC engines, no session
  cap, AV1).
- **Whether the front end adopts the same SkSL sources.** Not required for the server work, but it
  is the only way to make editor and export pixel-close for transitions. **The front end renders
  with PixiJS**, not CanvasKit (CanvasKit is the text path), so this is a GLSL question rather than
  a load-the-same-file one — see `TRANSITION-PARITY.md` for what adoption would cost and buy.
- **`compositing.layer_order`: is insertion-stable clip sort the wanted behaviour?** Clip sort is
  now insertion-stable rather than address-tie-broken, which is a behaviour change for two clips on
  the same layer. Either the service owner confirms it, or the layer collision is fixed so the tie
  cannot arise. *Lifted out of W02 on 2026-09-18* — it was buried in the release benchmark's
  checklist, which is the last thing that runs, and it wants an answer well before that.
- **The reference resolution for length-valued effect parameters.** The blur radii have no declared
  reference frame, so the same authored value renders at different widths in the preview, in the
  slow-effect proxy, and in the export — measured in `TRANSITION-PARITY.md`. Fixing it shifts
  existing projects, so it needs a product decision on versioning, not just a patch.
