# Decisions

One line per decision that a later session should not re-litigate: what was decided, when, why, and
what would have to change for it to be revisited. Referenced from `doc/GPU-RENDER-PLAN.md`.

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
