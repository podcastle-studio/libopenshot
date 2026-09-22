# Stage 6 — effects on the GPU: what is done, what is not, and why

**Status as of 2026-09-22.** This is the working record for Stage 6 (W19, W20, W21). It exists so a
session picking the work up later does not have to re-derive which effects are ported, which were
examined and rejected, and what the reasons were. The per-item instructions stay in
`GPU-WORKLIST.md`; the decisions and their measurements stay in `GPU-DECISIONS.md`; this file is the
map between them.

Deleted with the rest of `doc/gpu-migration/` when the migration lands.

---

## 1. What Stage 6 covers

| item | scope | state |
|---|---|---|
| **W19** | `GpuEffect` base + per-pixel effect fragments | **10 of 13 done**, 2 ruled out, 1 blocked |
| **W20** | transition vocabulary from `image-processing-lib` as shared SkSL | **6 of 10 done**, **4 unblocked and next** |
| **W21** | overlay clips as textures (additive blend, displacement map) | **done** (2026-09-22) |

`libopenshot` carries 47 effect classes. Only the ones
`../video-rendering-service` actually constructs are in scope — that is the plan's rule (§2.4), and
it is 20 classes reached from three places:

- **per clip**, `VideoRenderingImpl.cpp`: ChromaKey, ColorAdjustment, ColorMap, Crop, Enhancement,
  LightAdjustment, Mask
- **transitions**, `Transition.cpp`: Alpha, Bars, Blur, BorderReflectedMove,
  BorderReflectedRotation, Brightness, CircleMask, ColorShift, Exposure, SplitShift, Wipe, Zoom
- **animation**, `Animation.cpp`: CameraMovement

Everything else in `src/effects/` is upstream OpenShot (Hue, Negate, Pixelate, Saturation,
Stabilizer, Tracker, Caption, …) and is **out of scope for every item in this stage**. Nothing has
been done to it and nothing should be.

---

## 2. W19 — per-pixel effects

### 2.1 The mechanism

`src/GpuEffect.{h,cpp}`. A derived effect keeps its CPU implementation untouched and adds an SkSL
twin of the same arithmetic; `GetFrame` offers the frame to `ApplyOnGpu` first and runs the C++ when
it declines. Declining is normal — no GPU, `OPENSHOT_GPU=off`, a method the fragment does not
implement, data not ready.

Frames **stay on the GPU between effects**: `ApplyOnGpu` leaves its result as the frame's GPU
backing, so a chain pays one upload at the front and one readback at the end, the latter lazily in
`Frame::GetImage()`.

Gated by `tests/gpu/gpu_effect_parity.cpp` (`openshot-gpu-effect-parity`), which compares each
fragment against its C++ twin over eight images built around the alpha edge cases, reports whether
they agree *exactly*, times both paths, and **refuses to compare a case that never reached the
GPU** — `Frame::IsGpuBacked()` after the call is the proof. `unit.gpu_effect_path` in the golden
suite asserts the same thing through the real timeline path.

### 2.2 Ported — 10 effects

Parity measured on Vulkan (RTX A2000), 2026-09-22. "images" is parameter cases × 8 test images.

| effect | cases | images | bit-exact | worst | used by | note |
|---|---:|---:|---:|---:|---|---|
| **Alpha** | 2 | 16 | **16** | 0 | transitions | scales premultiplied channels; no division |
| **ColorShift** | 2 | 16 | **16** | 0 | transitions | four wrapped integer gathers; no arithmetic |
| **Bars** | 2 | 16 | **16** | 0 | transitions | opaque black over four edge bands |
| **ChromaKey** | 3 | 24 | 22 | 2 | per clip | **YCbCr method only** — see 2.3 |
| **LightAdjustment** | 9 | 72 | 64 | 1 | per clip | contrast LUT uploaded as a 256×1 texture |
| **Mask** | 4 | 32 | 26 | 1 | per clip | matte built on CPU, uploaded as a texture |
| **Enhancement** | 4 | 32 | 26 | 1 | per clip | **no grain pass** — see 2.3; first two-pass effect |
| **ColorAdjustment** | 4 | 32 | 16 | 1 | per clip | `double` params → `float` uniforms |
| **Brightness** | 4 | 32 | 24 | 3 | transitions | unpremultiplies |
| **Exposure** | 3 | 24 | 16 | 5 | transitions | unpremultiplies |
| **total** | 37 | **296** | **242** | 5 | | 82 % exact |

> Earlier commit messages quoted 214/297 and 246/329 for this table. Those counts accidentally
> included the timing rows of the same report. **296 comparisons, 242 exact** is the correct figure
> for W19's effects alone; the whole suite including W20's is 416 and 346.
>
> **These counts drift by a few between runs** and should be read as rates, not constants. The
> effects that divide by alpha inherit the GPU's 2.5 ULP allowance, and the number of pairs that
> lands on the wrong side of an integer is not fixed — the exhaustive probe read 588 one day and
> 763 the next with the same shader. The *magnitude* (1 LSB) is stable; the count is not.

**ChromaKey's production configuration is 8/8 bit-exact** (green key, fuzz 70, halo 20 — the only
thing the service builds). Only a deliberately wide halo moves, on 4 and 11 pixels of two images.

### 2.3 What decides whether a fragment is bit-exact

Two things, and only two:

1. **Dividing by alpha.** Vulkan permits 2.5 ULP on a division where IEEE requires exact rounding.
   Over every legal `(premultiplied byte, alpha)` pair — 32,896 of them — the shared unpremultiply
   disagrees on **588 on Vulkan and 576 on lavapipe**, always by 1 LSB and always where the true
   quotient is an exact integer. Two rates on two drivers is the proof it is the division.
   Brightness and Exposure divide; a contrast or exposure factor then amplifies that 1 LSB to 3 or
   5. Nothing else in the chain contributes: the byte the fragment reads out of the texture is
   correct in **all 32,896** cases, and the whole `exposure(1.0)` chain disagrees on exactly the
   same 588 pairs as the bare unpremultiply.
2. **Carrying `double` parameters into per-pixel arithmetic.** An SkSL uniform is `float`.
   ColorAdjustment never unpremultiplies and is still not exact for this reason alone — 1 LSB.

Everything else is reproducible, including the C++'s byte truncation, which the prelude
(`GpuEffect::GpuShaderPrelude()`) exists to reproduce rather than round away.

### 2.4 Partially ported — 2 effects, with the excluded part named

| effect | ported | **not** ported | why not |
|---|---|---|---|
| **ChromaKey** | `CHROMAKEY_YCBCR` | the ~11 HSV / HSL / CIE LCh methods | they key on coordinates **babl** computes; reproducing babl's colour science in SkSL bit-for-bit is a different and much larger job. The service only ever constructs YCbCr, hardcoded. |
| **Enhancement** | clarity, sharpen, blur-mix | the **grain** pass | `fract(sin(x·12.9898 + y·78.233) · 43758.5453)`. At 1080p the argument to `sin()` reaches ~85,000, where the answer depends entirely on how many bits the implementation carries — `double` on the CPU, `float` in a fragment. They do not differ by an LSB, they differ by an arbitrary amount in [0,1), which the pass scales to as much as **~140 LSB** of grain. |

Both decline through `SetGpuUniforms` returning false, which is what that hook is for. A frame that
asks for grain runs **entirely** on the CPU rather than half on each.

**If someone wants the grain pass on the GPU**, the only route is changing the CPU's hash to
something reproducible in `float` — which moves production output and is a product decision.

### 2.5 Ruled out — 2 effects, and these are not "to do later"

| effect | used by | why it is not a per-pixel effect |
|---|---|---|
| **Crop** | per clip | `QPainter` with antialiasing: a rounded-rect clip path and a `drawImage` between fractional `QRectF`s. The corner coverage is a **rasteriser** difference — already on file for the compositor, "Skia's bilinear is not QPainter's smooth transform" — the rects are fractional for any keyframe that is not a whole pixel, so even `radius == 0` antialiases rather than blits, and `resize == true` changes the image size, which `ApplyOnGpu` cannot express. The service sets `resize = false` and passes a radius **curve**, so rounded corners are the normal case and there is no exact subset. |
| **CameraMovement** | animation | `QPainter` with `setWorldTransform` and `SmoothPixmapTransform` — a resampling geometric transform, same class. |

Porting either is a **redefine**-class change to the pixels, which is a product decision, not a
port, and it belongs with the compositor's parity work rather than with the per-pixel fragments.

**One exactly portable subset is recorded so it is not rediscovered:** at zoom 100 % and rotation 0,
CameraMovement's combined transform reduces to a pure translation *and the C++ does not enable
smooth transform*, so Qt takes its `TxTranslate` fast path — round the translation, blit
unfiltered — which `Clip::draw_to_canvas` already reproduces (W12). A pan-only CameraMovement is
therefore portable exactly. Not built: the effect exists for zoom, the golden scenario uses zoom,
and a fragment that declines in the normal case earns little. **Revisit if a payload capture (W04)
shows pan-only is common.**

### 2.6 Blocked — 1 effect

**ColorMap** (3-D LUT). Two questions for the front-end team, neither answerable from this repo:

1. **Which interpolation does the front end pass to `apply_lut`?** `0 = Trilinear`, `1 =
   Tetrahedral` (`image-processing-lib/wasm/wrappers/lutWrappers.cpp:84`); the value lives in their
   JS. Worth ≤ 4.3 LSB on a fine LUT, up to **98 LSB** on a coarse one.
2. **How do they set the LUT domain?** `parseCubeText` **drops `DOMAIN_MIN`/`DOMAIN_MAX`**
   (`ColorGradingCore.cpp:378`) while the front end's `applyLut` normalises by `domainMin`/`invSpan`
   set from JS. For any non-0…1 cube the two already disagree arbitrarily — a live bug today, not a
   migration concern.

Known work once those land, already decided in W11 and unchanged:

- Drop the **17³ resample** in `ColorMap.cpp:243` (the measured 17 LSB editor/export gap). It
  re-baselines `effects.colormap_lut` and `effects.stack_crop_chroma_light_lut`, and costs CPU LUT
  throughput — 33³ is 431 KB against 17³'s 59 KB, i.e. out of L2. **Measure it, do not assume.**
- The export's own kernel is a copy of the front end's with optimisations; the one genuine
  divergence is **alpha model** — `ColorMap.cpp` demultiplies (frames are
  `Format_RGBA8888_Premultiplied`), the front end does not (canvas pixels are straight). Both are
  correct for their input. The shared fragment must be defined on **straight RGB** with
  unpremultiply/premultiply at the boundary, done once.
- The cube goes in as a 3-D texture; `GpuFrame::ToTexture` discipline applies.

---

## 3. W20 — transitions

**Not started.** Read `TRANSITION-PARITY.md` first; it carries an open product decision (the blur
radii have no declared reference resolution, a measured bug today) that no amount of shared shader
source addresses.

The 15 transition effects the service and the golden suite exercise map onto 12 libopenshot classes.
**Five of them are already on the GPU**, because W19 ported the class:

| transition | class | state |
|---|---|---|
| alpha | `Alpha` | **done (W19)** |
| bars | `Bars` | **done (W19)** |
| brightness | `Brightness` | **done (W19)** |
| exposure | `Exposure` | **done (W19)** |
| color_shift | `ColorShift` | **done (W19)** — this is W20's "colour shift" item; `ColorShift` calls the submodule's `applyColorShiftEffect`, so no separate port is needed |
| blur | `Blur` (box) | **W20** |
| diagonal_blur | `Blur` (diagonal) | **W20** |
| rotational_blur | `Blur` (rotational) | **W20** |
| zoom_blur | `Blur` (zoom) | **W20** |
| zoom | `Zoom` | **W20** |
| border_reflected_move | `BorderReflectedMove` | **W20** |
| border_reflected_rotation | `BorderReflectedRotation` | **W20** |
| threshold_wipe_mask | `Wipe` | **W20** |
| split_shift | `SplitShift` | **W20** |
| circle_mask | `CircleMask` | **W20** |

So W20's real remaining surface is **7 classes / 10 transition variants**, not the whole vocabulary.

### W20 progress, 2026-09-22

**The blocker is narrower than the item reads.** `TRANSITION-PARITY.md`'s reference-resolution bug
affects **only box, diagonal and zoom blur** — the note says so itself. The rest are already
resolution-independent.

**And W20's declared gate is PSNR ≥ 45 dB, not bit-exact** — a looser parity class than every W19
fragment was held to. Only two of these effects have C++ that is exactly reproducible; the rest
resample, which is why the item was written that way.

| variant | state | parity |
|---|---|---|
| `SplitShift` | **done** | **8/8 bit-exact** |
| `Wipe` (threshold wipe mask) | **done** | **24/24 bit-exact**, after the `cv::cvtColor` fix below |
| `BorderReflectedRotation` | **done** | **16/16 bit-exact**, after reproducing `warpAffine`'s fixed-point map |
| `Zoom` | **done** (zoom-in only) | max 1 LSB, 57–78 dB; zoom-out declines — it can change the frame's size |
| `BorderReflectedMove` | **done** | exact on smooth content, 46.5–47.5 dB on noise — clears W20's 45 dB gate |
| `CircleMask` | **done** | **24/24 bit-exact**, by not drawing the circle — see below |
| rotational blur | **next** | unblocked 2026-09-22; its downscale is now reference-based |
| box / horizontal-vertical blur | **next** | unblocked 2026-09-22 |
| diagonal blur | **next** | unblocked 2026-09-22 |
| zoom blur | **next** | unblocked 2026-09-22 |

**The `cv::cvtColor` problem is fixed.** `Wipe` thresholds a BGRA luminance, and OpenCV's 8-bit
grey is not reproducible from any documented formula — the fixed-point expression differs on 703 of
262,144 colours by 1, the float one on 278. A threshold turns that 1 LSB into a full step. The
submodule now computes the luminance explicitly (`image-processing-lib` commit `f8873e0`), and
`Wipe` went from 62–85 dB to **24/24 bit-exact**. It moves the library's own output by ≤ 1 LSB of
luminance on ~0.27 % of colours, which re-baselined `transitions.threshold_wipe_mask`.
**Cross-repo: the front end compiles the same source to WASM and picks it up when it updates the
submodule. The submodule commit is local and unpushed.**

Parity measured on Vulkan, 2026-09-22, same harness and same eight images as W19:

| effect | cases | images | bit-exact | worst | note |
|---|---:|---:|---:|---:|---|
| **Wipe** | 3 | 24 | **24** | 0 | after the `cv::cvtColor` fix |
| **SplitShift** | 3 | 24 | **24** | 0 | two integer rectangle blits |
| **CircleMask** | 3 | 24 | **24** | 0 | OpenCV rasterises the circle; the fragment does arithmetic |
| **BorderReflectedRotation** | 2 | 16 | **16** | 0 | reproduces `warpAffine`'s 10-bit fixed-point map |
| **BorderReflectedMove** | 2 | 16 | 12 | 4 | exact on smooth content; 46.5–47.5 dB on noise |
| **Zoom** | 2 | 16 | 4 | 1 | zoom-in only; 57–78 dB |
| **total** | 15 | **120** | **104** | 4 | against W20's 45 dB gate, nothing fails |

**The four blur variants were unblocked on 2026-09-22** and are the only Stage 6 work left. The
reference-resolution decision landed (1280 px, unversioned), the C++ was rewritten accordingly —
normalised lengths, reference-based downscales, and three box passes instead of one — and the
goldens were re-baselined. What remains is writing their four SkSL fragments.

They are also the highest-value fragments left, because `Blur` sits in the middle of the
`{Zoom, Blur, Alpha}` transition and is currently what splits that chain and costs 30 % on the GPU
path.

**A rule that has now been right three times: whatever a rasteriser or a transcendental decides
stays on the CPU and arrives as a texture; the fragment does arithmetic.** LightAdjustment's tone
curve, Mask's matte and CircleMask's coverage all came out bit-exact that way. Everything that tried
to re-derive a rasteriser did not.

**And OpenCV's resampling turns out to be reproducible** if you reproduce its arithmetic rather
than its intent. `warpAffine` evaluates its map in 10-bit fixed point, not in floating point;
matching that took `BorderReflectedRotation` from 11 dB on high-frequency content to bit-exact.
The intermediate version — right to a fraction of a pixel — looked perfect on every smooth test
image and was completely wrong on noise. **Test resampling effects on high-frequency content.**

Two things carry over from W19 that W20 should not rediscover:

- **`ApplyOnGpu` goes before any `frame->GetImage()`**, and moving that fetch is step one of porting
  an effect. Four of the first seven fragments were written the wrong way round and measured
  3.2–4.4 ms a pass instead of 0.05–0.22 ms. Output is identical either way, so only the timing
  catches it.
- The spike in `spikes/sksl-glsl/` shows a generated GLSL ES twin of an SkSL source agreeing with
  the C++ to 57–61 dB on `applyRotationalBlur` — the hardest effect in the vocabulary — against
  W20's 45 dB gate. That spike also records why `skslc → .glsl` fails silently and why the pipeline
  must go through `skslc`'s `.stage` output.

---

## 4. W21 — overlay clips

**Done, 2026-09-22.** Both composites are two-texture fragments in `src/gpu/GpuOverlay.{h,cpp}`,
called from `Clip::GetFrame` before the OpenCV path — which stays, gated on
`GpuDevice::available()`, per the standing constraint.

| composite | state | note |
|---|---|---|
| additive blend | **done, bit-exact** | a fragment, not the `kPlus` blender the item proposed: the C++ adds only channels 0..2 and leaves alpha alone, which `kPlus` does not |
| displacement map | **done, bit-exact** | luminance is OpenCV's fixed-point `(B·1868 + G·9617 + R·4899 + 8192) >> 14`, not a float dot product; gather stays nearest with the C++'s `int(v + 0.5)` |
| overlay of a different size | **declines** | the C++ resizes with `cv::resize`; OpenCV's `INTER_LINEAR` is a rasteriser difference Skia will not reproduce |

Both golden overlay scenarios are bit-identical on Vulkan under `Tolerance::Exact()` with no GPU
band, and `unit.gpu_overlay_path` asserts the shader is what produced them.

**It buys no wall-clock time yet, and that is expected.** Interleaved properly, `transitions_chain`
is 17.0 fps without W21 and 16.8 with — but CPU occupancy drops 2.2 → 1.9 cores and peak RSS
1.13 → 1.05 GB. The transition effects around the overlay are still on the CPU and each calls
`Frame::GetImage()`, so the overlay's result is read back immediately. **W21 pays when W20 lands**,
and the two should be measured together.

---

## 5. The gates, and why all four need restating

Measured interleaved on **mains power**, 1080p, `render`, 150 frames, GPU off against Vulkan:

| scenario | GPU off | Vulkan | | gate |
|---|---:|---:|---|---:|
| `chroma_key_green` | 6.9 fps | **43.6 fps** | **6.3×** | ≥ 70 |
| `heavy_effects` | 4.9 fps | 5.6 fps | +14 % | ≥ 60 |

**`chroma_key_green` is what a ported effect in a clean chain looks like**: one clip, one effect, one
upload, one readback, and 1.1 cores of babl conversion and distance testing move to the GPU. What
remains between 43.6 and 70 is the **crossing** — W22–W25, where the compositor's gates were already
carried. The gate should move there.

**`heavy_effects` cannot reach 60 fps from Stage 6 at all.** Its chain is rounded `Crop`, `Blur`,
`Enhancement(noise 0.3, …)`, `ColorAdjustment`, `LightAdjustment`, `ColorMap`, plus clip shadow and
blur. Of those: Crop is **ruled out**, `Blur` is **W20 and blocked there** — all four of its modes are,
including rotational — `ColorMap` is **blocked on the front end**, and that `Enhancement` **asks for
grain**, the one pass that cannot be ported. Four of six stay on the CPU whatever Stage 6 does, so
the chain crosses PCIe repeatedly, and **no decision available to this project unblocks it except
the reference-resolution one.**

**That measurement was read too generously at the time, and `transitions_chain` corrects it.**
Interleaved on a settled machine, 1080p render: **31.5 fps with the GPU off against 22.4 on Vulkan
— about 30 % slower**, while using half the CPU (2.3 → 1.1 cores). Its first transition applies
`{Zoom, Blur, Alpha}` to both clips, which now reads **GPU → readback → CPU → upload → GPU**:
`Zoom` and `Alpha` are fragments and `Blur`, the one still on the CPU, sits in the middle. Before
W20 all three were CPU and the frame crossed once.

So: **partial porting is safe for correctness and not for speed.** Every arm of the sweep is green
and an effect that declines just runs its C++ twin — but a CPU effect *between* two GPU effects
costs a readback and an upload, ~5 ms each way at 1080p. The order of the remaining work therefore
does matter, in one specific way: an unported effect in the middle of a common chain is much worse
than one at the end. `Blur` is the worst case of that — it is blocked, and it is in the middle of
the most common transition. **A second, independent reason to settle the reference-resolution
decision.**

The CPU path is untouched and measures what it always did, and `OPENSHOT_GPU` is a per-deployment
switch, so nothing about the standing constraint is at risk. But a GPU deployment running
transitions is currently worse off than a CPU one.

**The per-effect `≤ 0.2 ms at 1080p` clause also needs restating.** A *do-nothing passthrough*
fragment measures 0.19–0.21 ms in the same harness on mains, because a pass copies the source
surface and then reads and writes an 8.3 MB surface before the fragment does any work; the pool
acquire is 0.016 ms of it. The gate is the floor of one full-frame pass, so no fragment can meet it
with margin. ColorShift is over it at 0.22 ms only because it makes four texture fetches instead of
one. Fixing this means not giving every effect its own pass — a zero-copy source (Graphite's
`SkSurfaces::AsImage` consumes the surface, so it cannot simply ping-pong through the pool) or
composing a chain into one draw. **Its own item, not a fragment's problem.**

**W21's timing clause is now entangled with the above.** Re-measured after six transition effects
became fragments, `transitions_chain` regressed on the GPU for the reason just given — which is
W20's fragmentation, not W21's doing. W21 itself measured neutral in a clean A/B.

**And W21's timing clause still has nothing to measure.** It reads "a transition frame costs no more than
a plain two-clip frame ±10 %", and there is no plain-two-clip scenario to compare against. Measured
against itself instead — both libraries built up front and swapped in place so the arms interleave
— `transitions_chain` is 17.0 fps without W21 and 16.8 with, with CPU occupancy 2.2 → 1.9 cores and
peak RSS 1.13 → 1.05 GB. The wall clock does not move because the transition effects around the
overlay were still on the CPU when that was measured; **six of them are now fragments, so this is
worth re-measuring** before the clause is rewritten.

---

## 6. What is left, in order

**Stage 6 has nothing left that this machine can finish unaided.** Everything outstanding is
waiting on a person, and there are only three of them.

1. **The reference-resolution decision** (`TRANSITION-PARITY.md`, "Open questions") — a product
   decision, because fixing it shifts existing projects. It blocks **all four remaining W20
   variants**: horizontal/vertical blur, diagonal blur, zoom blur, and — corrected 2026-09-22 —
   **rotational blur**, which is resolution-dependent through its downscale threshold rather than
   through its parameter.
2. **Two answers from the front-end team**, which block **ColorMap** and nothing else (§2.6): which
   `interpolation` they pass to `apply_lut`, and how they set the LUT domain. The second is a live
   bug today, not a migration concern.
3. **Owner decisions that are not blocking anything**, but should be settled before the numbers are
   quoted anywhere:
   - **Crop and CameraMovement** (§2.5) — porting either is a redefine-class change to the pixels.
   - **The gates** (§5) — both fps gates and the per-effect ≤ 0.2 ms clause measure something other
     than what they say.
   - **Push the submodule.** `image-processing-lib` commit `f8873e0` (the explicit BGRA luminance)
     is committed locally and **unpushed**; the front end compiles the same source to WASM and
     picks it up when it updates the submodule.

**Two earlier entries here are now answered and are recorded so they are not re-asked:**
~~"W20 is the only item left not blocked on someone else"~~ — it is now fully blocked. ~~"Should
`Blur` be pulled forward?"~~ — no: `Blur` *is* three of the four blocked variants, plus rotational,
so it cannot move ahead of the decision that blocks it.

## 7. Cross-references

- `GPU-WORKLIST.md` — W19/W20/W21 items, sub-tasks, gates
- `GPU-DECISIONS.md` — every measurement behind this file, under the 2026-09-22 W19 entries
- `TRANSITION-PARITY.md` — **read before W20**
- `STATUS.md` — session log, and the "Known oddities" entry on the golden suite's unstable
  resampled alpha boundary, found while adding these scenarios and **not fixed**
- `tests/gpu/gpu_effect_parity.cpp` — the parity gate, the exhaustive unpremultiply and
  exposure-chain probes, the babl Y'CbCr probe, and `--sksl` for asking the compiler what SkSL
  supports
