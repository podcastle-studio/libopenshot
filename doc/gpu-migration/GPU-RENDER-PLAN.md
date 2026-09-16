# GPU Render Pipeline — Build Plan

Status: draft 3, 2026-09-11 (second pass: rewritten around the measured baseline in
`doc/PERFORMANCE-BASELINE.md`; every step now has a numeric gate and every phase a releasable stop). Branch: `feature/gpu-rendering` (from fork `develop` 1d82adc9). Owner: rendering team.
Scope: the podcastle-studio libopenshot fork + video-rendering-service.
Goal: every pixel operation between "packet demuxed" and "packet muxed" runs on the GPU,
Qt is gone, and the library is a headless JSON-in / MP4-out renderer.

How to use this document: work top to bottom inside a phase. Every step has a
**Validate** block; do not start the next step until it passes. Steps marked
`[parallel]` can be done by a second person at the same time as their neighbours;
everything else is sequential. Each phase ends in a **Releasable build** that can
ship to production on its own.

---

## 0. Where we are starting from (read first)

### 0.1 Branches

`feature/gpu-rendering` is branched from the fork's `develop` (1d82adc9). All GPU work
happens on it, in small PRs back to `develop` so intermediate releasable builds (R1, R2,
…) can ship from `develop` as they land.

Upstream OpenShot is 336 commits ahead of the fork's merge-base (upstream `master`
772e6794 = libopenshot 1.0.0, SO 31). Relevant to this plan:

| | fork `develop` | upstream 1.0.0 |
|---|---|---|
| Custom text engine `src/text/`, subtitles `src/subtitle/`, `BlendModes.cpp`, overlay clips, `image-processing-lib`, Skia build | yes | no |
| Hardware decode path | **broken**: throws on frame 1 (see 0.3) | fixed (`FFmpegReader.cpp` uses `pCodecCtx->sw_pix_fmt` for the downloaded frame) |
| FFmpeg 7 support, newer effects, bug fixes | no | yes |

**Step 0.1 — Decide what to do about upstream.** Options:

- **A. Merge upstream 1.0.0 into `develop` before the GPU work.** One large merge, mostly
  in `Clip.cpp`, `Timeline.cpp`, `FFmpegReader.cpp`, `FFmpegWriter.cpp`, `Frame.cpp`,
  `CMakeLists.txt`. Benefit: the GPU work then rewrites those files once, on the newest
  base, and a later upstream sync stays cheap.
- **B. Stay on the fork's base and cherry-pick only what is needed** (the hardware decode
  fix in step 1.5, nothing else). Cheapest now; the fork keeps drifting from upstream, and
  after the GPU rewrite a merge of upstream becomes impractical (the files will no longer
  resemble upstream). Accept that this fork becomes a permanent fork.

Recommendation: **B** if the team does not plan to track upstream anyway (the fork
already replaces the compositor, the text engine and the effects, so upstream changes to
those areas are irrelevant after R3). Choose **A** only if upstream's FFmpeg 7 support or
fixes are wanted soon; in that case do it as one PR before Phase 1 and validate as below.

**Validate (for A only):** the merged branch builds; `examples/openshot-text-payload` and
`openshot-subtitle-payload` produce identical PNGs to `develop` for the payloads in
`tmp/`; the 6-payload corpus (see 3.0) renders with PSNR ≥ 50 dB vs `develop`.

### 0.2 What the pipeline does per frame today (develop)

```
FFmpegWriter::WriteFrame(timeline, a, b)        one producer thread, one consumer thread
 └ Timeline::GetFrame(n)                        recursive mutex around everything
    ├ new Frame(project w,h)  QImage RGBA8888_Premultiplied, fill()
    ├ for each clip (layer order):
    │   Clip::GetFrame(background=timeline frame)
    │    ├ FrameMapper → FFmpegReader::GetFrame  sw decode → av_image_copy → memset →
    │    │                                       sws_scale YUV→RGBA (1 thread) → QImage
    │    ├ deep copy of reader frame
    │    ├ apply_effects(before)                 raw loops / OpenMP / OpenCV via BGRA round trip
    │    ├ overlay clips                         second clip render + image-processing-lib blend
    │    ├ apply_keyframes                       new canvas, cv::GaussianBlur, shadow (5 passes),
    │    │                                       opacity loop, QPainter::drawImage(QTransform)
    │    ├ apply_effects(after)
    │    └ apply_background                      QPainter SourceOver or BlendModes.cpp (OpenMP)
    ├ SubtitleManager::renderAtFrame             Skia CPU raster into the timeline QImage
    └ final_cache.Add
 └ FFmpegWriter::WriteFrame(frame)               sws_scale RGBA→NV12/YUV420 (1 thread),
                                                 av_malloc+memcpy, libx264 (service default)
                                                 or nvenc after a synchronous upload
Text clips: TextClipReader::GetFrame → Skia CPU raster (up to 5 offscreen surfaces per
animated frame) → QImage copy.
```

### 0.3 Measured (2026-09-10, i7-12700H 20 threads, RTX A2000, 720p→1080p, 1 clip)

| Configuration | fps |
|---|---|
| Software decode only (reader) | 1200 |
| `Timeline::GetFrame` only, no encoder | 134 |
| Export libx264 (service today) | 62 |
| Export h264_nvenc | 82 |
| Export libx264, pipeline mode | 84 |
| Export h264_nvenc, pipeline mode | 112 |

Main-thread stack samples of the nvenc export: 60 % `QRasterPaintEngine::drawImage`
(clip transform + composite), 25 % writer `sws_scale`, 5 % canvas fill, 5 % CUDA
upload, ~0 % encoding. 4K source: reader 59 fps software, ffmpeg CLI 137 fps software
and 128 fps NVDEC-on-GPU. Hardware decode in the fork throws on the first frame
(`OutOfMemory: Failed to allocate image buffer`) because `pCodecCtx->pix_fmt` is
`AV_PIX_FMT_CUDA` once hardware decode is active, the download is forced to YUV420P
(CUDA only offers NV12) and the `get_format` callback picks VDPAU before CUDA. A 42-line
fix was prototyped in this session (device-matched `get_format`, download with
`AV_PIX_FMT_NONE`, use `next_frame->format` downstream) and verified: frames correct
(PSNR ≈ 50 dB vs software) but slower wall-clock because download + memset + swscale stay
serial. It is not in the tree; step 1.5 re-applies it.

Conclusion: the codecs are not the bottleneck. The compositor and the two colour
conversions are, and they are single-threaded CPU code.

### 0.4 Full scenario baseline and cost attribution (2026-09-10/11)

`doc/PERFORMANCE-BASELINE.md` holds the complete matrix (13 production-style scenarios ×
5 resolutions × render/x264/nvenc, `tests/bench/results/baseline-cpu.json`). The numbers this
plan is measured against, all 1080p:

| scenario | render fps | x264 fps | dominant cost (gdb stack sampling, render mode) |
|---|---:|---:|---|
| `single_video` | 116.7 | 81.0 | decode + swscale + one composite |
| `grid_2x2` | 40.8 | 32.2 | Qt raster `drawImage` per layer |
| `grid_3x3` | 23.3 | 20.5 | **Qt raster `drawImage` + `apply_background` (12/19 samples)** |
| `podcast_pip` | 22.9 | 19.9 | Qt raster composite + per-pixel filters |
| `subtitles_words` | 56.3 | 31.2 | Skia CPU raster per frame |
| `text_static_4` | 59.2 | 48.4 | Skia CPU raster (cached between frames) |
| `text_animated_glow_3` | **1.4** | **1.4** | **glow SkSL shader on Skia's CPU raster pipeline: 29/40 samples in `TextGlowRenderer::paintGlowFromSilhouette`, 31/40 inside `SkRasterPipeline` stages** |
| `transitions_chain` | 27.4 | 23.5 | OpenCV transition effects + `GetImageCV` round trips |
| `blend_stack_5` | 19.1 | 16.2 | `BlendModes.cpp` OpenMP loops + composite |
| `heavy_effects` | 11.5 | 10.7 | **Qt `drawImage` + `LightAdjustment`/`Enhancement` per-pixel loops** |
| `chroma_key_green` | 12.9 | 11.9 | `ChromaKey` per-scanline loop |
| `source_4k` | 60.9 | 47.5 | decode + pre-scale |
| `everything` | **1.8** | **1.8** | **glow shader (26/40 samples) + chroma key** |

Three facts drive every decision below.

1. **The single worst scenario is one shader running in software.** `text_animated_glow_3`
   renders at 1.4 fps (952 ms per frame, 1.0 core) and roughly 72 % of that is the glow
   ray-march — which is *already written in SkSL* (`src/text/TextGlowShader.cpp`) and is being
   interpreted by Skia's CPU raster pipeline because Skia is built without a GPU backend. Moving
   that one pass to the GPU needs no new algorithm, only a GPU surface. It is the cheapest large
   win in the whole plan, and `everything` (26/40 samples) gets it too.
   Reducing glow quality is **not** an option: matching in-motion glow to resting glow was a
   deliberate earlier product decision.
2. **Qt raster compositing dominates everything with more than one layer** (`grid_3x3`,
   `heavy_effects`, `blend_stack_5`). That is the GPU compositor phase.
3. **The machine is idle while this happens.** Heavy scenarios use 1.0–1.2 cores of 20 because
   `Timeline::GetFrame` is serialised behind one mutex and both Skia and the effect loops are
   single-threaded per frame. Concurrency measurements: four simultaneous 1080p exports return
   1.8× aggregate throughput, and each runs at 46 % of its solo speed.

---

## 1. GPU technology primer (enough to make the decisions)

You do not need to program any of these directly for most of this plan; Skia and FFmpeg
hide them. You do need to know what each one is, because the choice decides which
containers, drivers and node pools we need.

### 1.1 Vocabulary

- **Texture / image**: a 2D array of pixels living in GPU memory. Drawing reads textures.
- **Surface / render target**: a texture that can be drawn into.
- **Upload / readback**: copying CPU memory to a texture and back. Slow relative to
  everything else (PCIe), so the whole design is "upload once, never read back".
- **Shader**: a small program the GPU runs once per pixel (fragment shader) or per work
  item (compute shader). All effects become shaders.
- **Context / device / queue**: the object that owns GPU resources and the channel you
  submit work through. Usually one per process, single-threaded to use.
- **Interop**: sharing a texture between two GPU APIs without copying through the CPU
  (here: the video decoder's CUDA memory and Skia's Vulkan memory).
- **Premultiplied alpha**: RGB already multiplied by A. Skia, the fork's QImages and the
  compositing math all assume it. Straight-alpha data (PNG files, canvas ImageData in the
  browser) must be converted at the edge.
- **Headless**: a GPU context with no window and no display server. Every server option
  below can do this; only Qt's OpenGL path is awkward about it.

### 1.2 The APIs

**OpenGL** — the 30-year-old graphics API. Works everywhere, single global state machine,
one context per thread. On a headless server it needs EGL and NVIDIA's `graphics`
container capability. It is being replaced; new code should not target it directly.
Skia's Ganesh backend supports it.

**Vulkan** — the modern low-level graphics + compute API from the same group (Khronos).
Explicit, verbose, fast, multi-queue, designed for headless use (`VK_KHR_display` not
needed). Supported by NVIDIA, AMD, Intel, and by the software rasteriser `lavapipe` for
machines without a GPU. Also needs `graphics` capability in the NVIDIA container runtime.
Skia's Ganesh and Graphite backends both support it. **This is our drawing API, reached
only through Skia.**

**CUDA** — NVIDIA-only general compute API. Not a graphics API: no canvas, no blending,
no text. It is how FFmpeg talks to **NVDEC** (hardware video decoder) and **NVENC**
(hardware video encoder), and how NPP (NVIDIA's image-processing library) runs. CUDA
memory can be shared with Vulkan through "external memory" handles. **This is our codec
API, reached only through FFmpeg, plus one small interop module.**

**OpenCL** — vendor-neutral compute API. Slower to develop for than CUDA on NVIDIA, no
codec access. Not needed.

**NVDEC / NVENC** — fixed-function video codec blocks on NVIDIA GPUs, separate from the
shader cores. FFmpeg exposes them as `h264_cuvid`/`hwaccel cuda` and `h264_nvenc`,
`hevc_nvenc`, `av1_nvenc`. Frames can stay in GPU memory as `AV_PIX_FMT_CUDA`.
Data-centre cards (T4, L4, L40S) have no NVENC session limit; consumer cards do.

**VAAPI / QSV / VideoToolbox / AMF** — the Intel, Apple and AMD equivalents of the
NVIDIA codec blocks. FFmpeg supports them with the same API shape. Not needed unless we
run on non-NVIDIA nodes; the design keeps the codec layer behind FFmpeg so this stays
possible.

**Skia** — Google's 2D rendering engine (Chrome, Android, Flutter, CanvasKit). One API
(`SkCanvas`) with two GPU backends: **Ganesh** (mature, GL/Vulkan/Metal/D3D) and
**Graphite** (newer, Vulkan/Metal/Dawn, better multithreading via one `Recorder` per
thread, the future default). Skia gives us textures, surfaces, transforms, all Porter-Duff
and W3C blend modes, image filters (blur, drop shadow, displacement, colour matrix),
gradients, clipping, path and text rendering, and **SkSL**, its shader language, for
custom per-pixel effects. The fork already uses Skia (CPU mode) for text; the editor uses
CanvasKit (Skia in WebAssembly on WebGL). **This is our compositor.**

**Dawn / WebGPU** — a portability layer over Vulkan/Metal/D3D12 with the WebGPU API.
Skia Graphite can run on it. Adds a layer we do not need on Linux+NVIDIA. Not now.

**OpenCV CUDA (`cv::cuda::GpuMat`)** — GPU versions of blur, warpAffine, resize,
cvtColor, LUT. Attractive because the transition effects are OpenCV today, but it has no
compositing, no text, no blend modes, and sharing its buffers with Skia needs the same
interop work as the decoder. Use it for nothing on the render path; keep it for offline
analysis (Stabilizer, Tracker, ObjectDetection).

**NPP** — NVIDIA's CUDA image library (resize, colour conversion, filters). Our FFmpeg is
built with it (`scale_npp`). Useful as a fallback for NV12↔RGBA conversion if the SkSL
version is ever a problem. Optional.

### 1.3 The decision, in one table

| Layer | Choice | Why |
|---|---|---|
| Video decode / encode | FFmpeg + NVDEC/NVENC (CUDA) | already built into our FFmpeg; frames stay on GPU |
| Drawing, compositing, effects, text | Skia, Graphite backend, Vulkan | one engine for everything; already the text engine; same as the editor's CanvasKit; SkSL for custom effects |
| Fallback GPU backend | Skia Ganesh, Vulkan | same `SkCanvas` code; enable only if Graphite lacks a feature we hit |
| Decoder ↔ compositor handoff | Vulkan external memory + CUDA external memory | one ~300-line module; GPU-to-GPU copy, no CPU |
| Machines with no GPU | Skia CPU raster + software codecs | same code, `upload()`/`readback()` at the edges; keeps laptops and CI working |
| Custom per-pixel effects | SkSL runtime effects | portable to CanvasKit so the editor can run the same shaders |

What we deliberately do **not** do: hand-written Vulkan or CUDA kernels for effects,
Qt on OpenGL, OpenCV CUDA on the render path, a different renderer.

---

## 2. Qt in the library today, and what replaces it

Qt is used for five unrelated things. They are removed in five separate steps
(`GPU-WORKLIST.md` stage 8, milestone R5), not all at once. The service links `Qt5::Widgets` and
`Qt5::Gui` but does not use a single Qt symbol itself; it inherits Qt only through
libopenshot's public headers (`Frame.h` exposes `QImage`, `Clip.h` exposes `QTransform`,
`Color.h` exposes `QColor`).

### 2.1 Category A — image storage and pixel access (the hard one)

| Where | Qt used for | Replacement |
|---|---|---|
| `Frame.h/.cpp` | `std::shared_ptr<QImage> image`, `wave_image`; `AddImage`, `GetImage`, `GetPixels`, `AddColor(QColor)`, `Qimage2mat`/`Mat2Qimage`; `Display()`/`Thumbnail()` spin up a `QApplication` | `GpuFrame` (texture-backed `sk_sp<SkImage>`/`SkSurface`) with a lazy CPU mirror as `SkBitmap`/`SkPixmap`. `AddImage(QImage)` becomes `AddImage(SkPixmap)` + `AddTexture(sk_sp<SkImage>)`. `GetPixels()` becomes `readback()`. `Display()`/`Thumbnail()` deleted (UI). |
| `FFmpegReader.cpp` | wraps the swscale RGBA buffer in a `QImage` | wraps in `SkPixmap` (CPU path) or uploads / imports to a texture (GPU path) |
| `FFmpegWriter.cpp` | `frame->GetPixels()` | `GpuFrame::readback()` (CPU path) or texture export to CUDA (GPU path) |
| `EffectBase.h/.cpp` | `GetFrame(std::shared_ptr<Frame>)` signature carries QImage; draws bounding boxes with `QPainter` | `apply(SkCanvas&, sk_sp<SkImage>&)`; box drawing via `SkCanvas` |
| `MagickUtilities.cpp`, `ImageReader.cpp`, `ImageWriter.cpp`, `TextReader.cpp` | `QImage` ↔ `Magick::Image` | delete with ImageMagick (see 2.6) |
| `CacheDisk.cpp` | `QImage::save/load` PNG, `QDir`, `QFile` | not used by the service; delete or reimplement with `SkPngEncoder`/`std::filesystem` |
| `text/TextClipReader.cpp`, `subtitle/SubtitleManager.cpp` | `SkBitmap::installPixels` over a `QImage` | render straight into the GPU `SkSurface`; no QImage at all |

### 2.2 Category B — compositing and transforms

| Where | Qt used for | Replacement |
|---|---|---|
| `Clip.cpp` `apply_keyframes`, `get_transform`, `get_shadow_image`, `apply_background` | `QImage` canvas, `QPainter::drawImage`, `QTransform`, `QPainterPath`, `QColor` | `SkCanvas::drawImage` on the timeline surface, `SkMatrix`, `SkImageFilters::Blur/DropShadow`, `clipRRect`, `SkColor4f` |
| `Clip.h` | `QTransform get_transform(...)`, `QSize` | `SkMatrix`, `SkISize` |
| `Timeline.cpp` | `QSize` for preview size; `QDir/QFile/QRegularExpression` to rewrite paths in JSON | `SkISize`; `std::filesystem` + `std::regex` |
| `BlendModes.cpp` | operates on `QImage` | deleted; `SkBlendMode` |
| `Color.h/.cpp` | `QColor` for hex parsing and `GetColorHex` | own 8-line hex parser returning `SkColor4f` (Skia has `SkColor`; `SkParseColor` also exists) |

### 2.3 Category C — readers that rasterise

| Where | Qt used for | Replacement |
|---|---|---|
| `QtImageReader.cpp` | `QImageReader` for PNG/JPEG/WebP…; `QSvgRenderer` for SVG (resvg is compiled **out**: `HAVE_RESVG=FALSE`); `QPainter` to scale | `SkCodec` (`SkPngDecoder`, `SkJpegDecoder`, already in our Skia build) for bitmaps; Skia's SVG module (`skia_enable_svg = true` already) or resvg for SVG. New `ImageReader` returns a `GpuFrame` texture cached per reader. |
| `QtTextReader.cpp`, `QtHtmlReader.cpp`, `TextReader.cpp` | `QFont`, `QPainter`, `QTextDocument`, ImageMagick | delete; the service uses `TextClipReader` (Skia) exclusively |
| `ScreenCaptureReader.cpp`, `WaylandScreenCaptureReader.cpp` | `QImage` | delete (desktop only) |

### 2.4 Category D — effects

Effects the service uses (must be ported): `Crop` (QPainter path clip), `ChromaKey`
(`QRgb` access), `ColorAdjustment`, `LightAdjustment`, `Enhancement`, `ColorMap`
(`QFile`/`QRegularExpression` to read `.cube`), `Mask` (`qGray`, `QImage::scaled`),
`Blur`, `Zoom`, `BorderReflectedMove/Rotation`, `Bars`, `Wipe`, `CircleMask`,
`SplitShift`, `ColorShift`, `Exposure`, `Brightness`, `Alpha`, `CameraMovement`
(`QTransform`, `QPainter`). Replacement: `GpuEffect` subclasses producing an
`SkImageFilter` or an SkSL draw (`GPU-WORKLIST.md` W19, milestone R4). `.cube` parsing moves to
`image-processing-lib`'s `parseCubeText`, which is already Qt-free.

Effects the service does not use, present on `develop` or upstream, Qt-heavy:
`Caption`, `Timer`, `AudioVisualization`, `ObjectDetection`, `Tracker`, `ObjectMask`,
`LensFlare`, `Pixelate`, `Sharpen`, `Outline`, `Glow`, `Shadow`, `SphericalProjection`,
`Deinterlace`, `Displace`, `DenoiseImage`, `FilmGrain`, `BeatSync`, `AnalogTape`,
`ColorGrade`, `Negate`, `Hue`, `Saturation`, `Wave`, `Shift`, `Stabilizer`.
Decision per effect: port to SkSL if product wants it, otherwise remove from the build
behind `ENABLE_LEGACY_EFFECTS=OFF`. Do not spend time porting effects nobody calls.

### 2.5 Category E — player, UI, desktop

`src/Qt/*` (7 files), `QtPlayer.*`, `PlayerBase.*`, `RendererBase.*`, `FrameScope.*`,
`Frame::Display`, `Frame::Thumbnail`, `AudioDevices` (JUCE, not Qt but desktop-only),
`examples/qt-demo`. Replacement: none. Put behind `ENABLE_PLAYER=OFF` in the first Qt
step and delete once nothing else references them.

### 2.6 Related dependencies that go with Qt

- **ImageMagick** (`ENABLE_MAGICK=ON` today): used only by `ImageReader`, `ImageWriter`,
  `TextReader`, `MagickUtilities`, and `Frame::AddMagickImage`. The service uses none.
  Remove with `ENABLE_MAGICK=OFF`, then delete the files.
- **babl** (`USE_BABL=1`): colour-space conversion used only inside `ChromaKey` for one
  keying mode. The SkSL ChromaKey makes it unnecessary.
- **OpenCV** on the render path: only via `Frame::GetImageCV` and the transition effects.
  Gone after R4. Keep OpenCV for offline `CVTracker`/`CVStabilization`/`CVObjectDetection`
  or drop them too if unused (they are unused by the service).
- **libopenshot-audio (JUCE)**: stays; audio is CPU and unchanged.
- **jsoncpp, zmq (logger), OpenMP**: stay.

### 2.7 Qt removal order and the check at each step

1. `ENABLE_PLAYER=OFF`, `ENABLE_MAGICK=OFF` build options; exclude 2.5 sources and the
   Magick sources. **Validate:** library builds, service links, corpus renders identically.
2. Replace `QString/QDir/QFile/QTextStream/QRegularExpression` in `Timeline.cpp`,
   `Profiles.cpp`, `ColorMap.cpp`, `ChunkReader/Writer.cpp`, `CacheDisk.cpp` with the
   standard library. **Validate:** unit tests for path rewriting and `.cube` parsing pass;
   `nm -D libopenshot.so | grep -c Qt5Core` drops accordingly.
3. `Color` without `QColor`. **Validate:** hex round-trip tests for 3/4/6/8-digit and
   `rgb()`/`rgba()` strings match the old output.
4. `GpuFrame` behind `Frame` (milestone R2) and Skia compositor (R3). **Validate:** as in
   those milestones. After R3 no `QPainter` remains on the render path.
5. Image and SVG readers on Skia codecs (R3). **Validate:** PSNR ≥ 50 dB vs QtImageReader
   on 20 PNG/JPEG/SVG shape files from production.
6. Effects (R4). **Validate:** per-effect parity tests.
7. Remove `find_package(Qt…)` from `src/CMakeLists.txt` and `Qt5::Widgets/Gui` from the
   service `CMakeLists.txt`. **Validate:** `ldd libopenshot.so | grep -c Qt` is 0; Docker
   image shrinks (Qt5 + Chrome + ImageMagick removed, roughly 400 MB).

---

## 3. The work itself — moved

**The step list now lives in `GPU-WORKLIST.md`**, in strict execution order as W01…W31, one item to
a session, each with its sub-tasks, its numeric gate and its dependencies.

It was moved because the numbering here was actively misleading. Three separate schemes collided:
section 0.1–0.4 is background while 0.1–0.6 were work items; the phases did not run in phase order
(Phase 2 ran before Phase 1); and the step numbers were, by their own declaration, "stable
identifiers, not sequence". One real ordering bug came out of the untangling — step `4.0` records
the canvas-precision decision that step `3.1` already depends on, so it now runs before the
compositor work, as W11.

`GPU-WORKLIST.md` carries the legacy id on every item and a legacy → worklist mapping table at the
end, so old commit messages, `STATUS.md` and `GPU-DECISIONS.md` still resolve.

**What stays here** is the reasoning the worklist does not repeat: where the measurements came from
(§0), what the APIs are and why Skia/Graphite/Vulkan was chosen over the alternatives (§1), the Qt
inventory and what replaces each use (§2), sizing for N parallel exports (§4), the parity policy
every item is held to (§5), and the standing gotchas (§6).

### 3.0 How a step is done

Unchanged, and still applies to every worklist item:

- **Change** — what code moves, in which files.
- **Verify** — the exact commands that must pass. Always `tools/golden.sh check` (95 scenarios,
  292 frames) plus a named `openshot-bench` case with a **numeric gate** from the 1080p baseline
  in §0.4.
- **Flag** — how the change is switched off in production without a revert.
- **Risk** — what can go wrong and what it looks like.

A step is not "done" until the golden suite is green. When a change is *intended* to alter pixels,
inspect every failing triptych in the report, re-baseline only those scenarios
(`tools/golden.sh update <filter>`) and commit the PNGs with the code. Phase-end gates are measured
with a full `openshot-bench --label <phase>` run compared against `baseline-cpu.json`.

### 3.1 Why Phase 2 ran first (kept for the record)

The measured bottleneck was one shader, not the encoder. `text_animated_glow_3` spent ~72 % of its
time in `paintGlowFromSilhouette` and `everything` 65 %, while the CPU quick wins by the plan's own
admission "do essentially nothing" for those scenarios. The GPU-capable image was the one real
coupling, which is why it moved from `1.7` to `2.0`.

**Considered and rejected: parallel frame rendering on the CPU.** Rendering frames N and N+1 on two
CPU threads would exploit the idle cores, but Skia Graphite uses one `Context` per process and gets
its parallelism from pipeline depth rather than width, so that machinery would be thrown away at
R3/R6. Process-level parallelism already exists in the service (§4). The plan therefore invests in
**depth** — decode-ahead, encode-ahead, frames in flight — which survives the GPU move, and leaves
width to the process manager.

**What R1 will and will not do.** The baseline says the encoder is 0–30 % of wall time depending on
the scenario, so R1 helps light scenarios (`single_video`, `source_4k`) and does essentially nothing
for `everything`, `text_animated_glow_3` or `heavy_effects`. Do not promise "1.5–2× exports" from
R1. Note also that `openshot-bench` already calls `WriteFrame` once for the whole range, so the
service-side chunking fix is a real production gain that the bench cannot show; measure that one on
the service.

## 4. System requirements for N parallel exports

The service runs several export processes per pod (`start_multiple.sh`, `SERVICE_NUM_INSTANCES_PARALLEL`,
default 2) and each process today sets 16 FFmpeg threads and 16 OpenMP threads regardless of the pod's
CPU quota. Sizing therefore has to be done per process and multiplied.

### 4.1 Measured per process (one 1080p export, one clip, no effects, dev laptop, 2026-09-10)

| Configuration | CPU used | Peak RSS | VRAM | fps |
|---|---|---|---|---|
| 720p source → 1080p, libx264 preset fast (≈ service) | 8.3 cores | 0.74 GB | – | 96 |
| 720p source → 1080p, h264_nvenc | 1.6 cores | 0.54 GB | 0.25 GB | 120 |
| 4K source → 1080p, libx264 | 7.3 cores | 1.5 GB | – | 46 |
| 4K source → 1080p, h264_nvenc | 2.7 cores | 1.4 GB | ~0.3 GB | 63 |
| 720p source → 4K output, libx264 | 6.5 cores | 2.2 GB | – | 27 |

One 1080p nvenc stream at 120 fps kept the A2000's single NVENC engine 73 % busy, so that engine
sustains about 165 fps of 1080p H.264, i.e. 5–6 exports running at real time. Memory grows with the
number of clips (each open reader holds decode buffers and a small cache; each project-size RGBA
frame is 8 MB at 1080p, 33 MB at 4K) and with text glow/3D surfaces; budget +150 MB per additional
video clip at 1080p.

### 4.2 CPU path (today, R1)

- Cores: each process wants `FF_THREADS + 1` cores while encoding. Rule: `FF_THREADS = OMP_THREADS =
  max(2, floor(cpu_quota / N) - 1)` (plan step 1.2). x264 loses efficiency above ~6 threads at 1080p,
  so more processes with fewer threads beat fewer processes with 16 threads once N ≥ 2.
- Memory: `N × (0.8 GB at 1080p | 1.6 GB with 4K sources | 2.3 GB for 4K output) + 1 GB` for the
  process images and the muxing ffmpeg.
- Recommended pod shapes: 8 vCPU / 8 GB → N = 2, 3 threads each (4K output: N = 1);
  16 vCPU / 16 GB → N = 4, 3 threads each; requests = limits so OpenMP does not see host cores.

### 4.3 GPU path (R2 onwards)

Per process: 1–3 CPU cores (demux, audio, Skia command recording, control), 0.5–1.4 GB RAM,
and VRAM roughly: timeline canvas ring 4 × 8 MB (RGBA8) or 4 × 16 MB (RGBA16F) at 1080p; one
texture per active clip (8 MB, 33 MB at 4K); NVDEC surface pool 8–16 × 3 MB per 1080p stream
(12 MB each at 4K); NVENC input pool 20 × 3 MB; Skia resource cache (cap it at 64 MB); text bakes
and glow surfaces (tens of MB). Expect **0.5–0.8 GB per 1080p export, 1.5–2.5 GB per 4K export**,
plus ~0.3 GB fixed per process for the CUDA and Vulkan contexts.

Fixed-function limits per GPU:
- NVENC: one engine ≈ 165 fps 1080p H.264 (measured); L4 has two engines, L40S three; HEVC/AV1
  cost more. Consumer GeForce cards cap concurrent encode sessions (3–8 depending on driver);
  RTX A-series and data-centre cards have no cap.
- NVDEC: comparable throughput per engine; 4K sources use ~4× the budget of 1080p.
- Shader cores: the compositor is far below saturation at 1080p; it becomes the limit only with many
  heavy effects or 4K. Several processes share the GPU by time-slicing; there is no per-process VRAM
  isolation, so the sum of the estimates above must fit with ~20 % headroom.

Recommended density on an L4 (24 GB, 2 NVENC): N = 4 concurrent 1080p exports or N = 2 4K exports
per GPU, one process each, `nvidia.com/gpu: 1` with the device plugin's time-slicing replicas = N,
pod cpu request = `N × 3 + 1`, memory request = `N × 1.5 GB + 2 GB`. Measure GPU busy %, NVENC
utilisation (NVML) and VRAM under load in Phase 6 before raising N.

### 4.4 What the service must expose

- `SERVICE_NUM_INSTANCES_PARALLEL` already exists; derive `FF_THREADS`/`OMP_THREADS` from it and the
  cgroup quota (step 1.2), and later `OPENSHOT_GPU=vulkan|off` plus the GPU device index per process.
- A per-export resource line in the logs (peak RSS, CPU seconds, GPU busy %, VRAM peak, NVENC
  utilisation) so the density numbers above can be re-derived from production instead of a laptop.

## 5. Parity policy (applies to every step)

The oracle is `tests/golden` (95 scenarios, 292 committed frames). `tools/golden.sh check` must be
green before any commit; when a change is meant to alter pixels, review every failing triptych in
the report and re-baseline only those scenarios.

- **exact**: PSNR = inf or max diff ≤ 1 LSB. Required for copy removals, format changes,
  blend modes, separable colour effects.
- **close**: PSNR ≥ 45 dB or SSIM ≥ 0.98. Accepted where the algorithm legitimately
  differs (GPU anti-aliasing, Gaussian vs box kernels, bilinear vs bicubic).
- **redefine**: the old output was wrong or implementation-defined (BT.601 applied to
  BT.709 sources, R/B swap, nearest sampling artefacts). Document in
  `doc/gpu-migration/GPU-DECISIONS.md`, compare against the editor's CanvasKit render instead, and
  update the golden set with a dated note.

**Which class applies is a property of the run, not only of the scenario** (added 2026-09-16, W12).
The goldens are CPU-rendered, and a GPU composite resamples with Skia's bilinear where QPainter uses
its own smooth transform; the two do not agree bit-for-bit and never will. So a scenario the
compositor touches is gated **exact on the CPU** — the path that ships, where nothing may move — and
**close when a GPU is actually compositing**. In `tests/golden` that is the `gpu-composite` tag and
`Tolerance::GpuClose()`, chosen from what measurably moved rather than from what might.

One further class exists and is a compromise, not a pattern to copy: `Tolerance::GpuAmplified()`
covers three blend modes whose formulas magnify a sub-LSB input difference into a large output one.
That band is too wide to catch a genuine regression, so those modes are gated instead by
`openshot-gpu-blend-parity`, which compares the formulas on identical pixels with no resampling.
**Where a tolerance has to be widened past usefulness, replace it with a sharper instrument rather
than accepting the blind spot.**

Anything that fails its parity gate is reverted or feature-flagged; it does not stay
merged "to fix later".

## 6. Things to keep in mind

- Skia Graphite's `Context` is single-threaded; parallelism comes from depth
  (frames in flight) and from one `Recorder` per thread, not from rendering two frames
  on two threads into one context.
- The NVIDIA container runtime needs `NVIDIA_DRIVER_CAPABILITIES` to include
  `graphics` for Vulkan; the org's CUDA base image sets `compute,utility,video` today.
- Data-centre GPUs (T4/L4) have no NVENC session cap; the dev laptop's A2000 does.
- Upstream 1.0.0 fixed the hardware decode crash independently; if option A in 0.1 is
  taken the fix arrives with the merge and step 1.5 only needs the copy removals.
- Everything premultiplied on the canvas. Straight-alpha inputs (PNG, canvas ImageData in
  the WASM front end) convert at the edge; the `premultipliedAlpha` flag in
  `image-processing-lib` already encodes this divergence.
- The editor renders text with CanvasKit m147 (Skia in the browser). Keep the Skia
  milestone in lockstep so glyph rendering, blur sigma and blend maths agree.
- **Glow quality is fixed.** Matching in-motion glow to resting glow was a deliberate earlier
  product decision; lowering the glow resolution or ray-march step count to buy speed is not an
  option. The glow gets faster by running the existing shader on the GPU, not by doing less of it.
- Memory is a first-class constraint, not an afterthought: `text_animated_glow_3` peaks at 9.6 GB
  and `everything` at 7.0 GB at 4K today. Every phase must re-check peak RSS (the bench records it),
  and the GPU phases must re-check VRAM.
- `openshot-bench` calls `WriteFrame` once for the whole range, so service-side scheduling fixes
  (step 1.1) are invisible to it. Measure those on the service.
- **QPainter does not always resample.** For a transform no more complex than `TxTranslate` its
  raster engine rounds to an integer blit — no filtering, no edge antialiasing. Skia has no such
  rule. Missing this cost the text scenarios ~10 dB (a text clip's transform turned out to be a
  *half*-pixel translation), and it was invisible until the draw was instrumented. Any new Skia
  draw that replaces a QPainter one has to reproduce it: `Clip::draw_to_canvas` does.
- **A frame composites on one path, not two.** Letting a CPU blend mode read a GPU-built backdrop
  takes `blend_color_burn` to 26.7 dB with a max difference of 255, from inputs differing by ~3 LSB:
  the non-linear W3C modes divide by the backdrop. Whenever part of a frame moves to the GPU, check
  what still reads that frame on the CPU afterwards.
- **The per-clip upload is the ceiling on Stage 5.** Every source image crosses PCIe once per clip
  per frame, so GPU compositing wins where a source is small relative to the area it covers
  (`grid_3x3` +34 %, `blend_stack_5` 2.5×) and loses where a few large sources replace composites
  that were already cheap (`podcast_pip` −5 %). `grid_3x3`'s 45 fps gate and that regression are
  both carried to W22–W25, which is the stage that removes the upload. Do not expect to tune around
  it before then.
