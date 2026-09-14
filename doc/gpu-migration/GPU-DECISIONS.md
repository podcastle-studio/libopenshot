# Decisions

One line per decision that a later session should not re-litigate: what was decided, when, why, and
what would have to change for it to be revisited. Referenced from `doc/gpu-migration/GPU-RENDER-PLAN.md`.

## Taken

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


## Open — decide before plan phase 4

- **Timeline canvas precision.** `kRGBA_8888` (matches today) or `kRGBA_F16` (better blending and
  blur, enables 10-bit output, doubles canvas memory). Recommendation: F16 for the timeline canvas,
  8888 for cached textures.
- **Graphite only, or Ganesh as a fallback backend.** Build both in phase 2 and decide at the end of
  phase 3 from the feature checks.
- **LUT rounding reference.** The native `ColorMap.cpp` (OpenMP trilinear, stride 3) and the WASM
  `LutApply.cpp` the front end runs (SIMD, trilinear or tetrahedral) already disagree. The shader
  must match one of them; matching the front end closes an editor-vs-export gap.
- **Nearest-neighbour sampling.** `BORDER_REFLECTED_ROTATION` and `DISPLACEMENT_MAP` use nearest
  today. Keep it for bit-parity, or switch to bilinear for quality and re-baseline.
- **GPU SKU for the node pool.** L4 is the working assumption (24 GB, two NVENC engines, no session
  cap, AV1).
- **Whether the front end adopts the same SkSL sources** through CanvasKit. Not required for the
  server work, but it is the only way to make editor and export pixel-close for transitions.
