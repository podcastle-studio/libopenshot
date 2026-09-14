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
(section 3, milestone R5), not all at once. The service links `Qt5::Widgets` and
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
`SkImageFilter` or an SkSL draw (section 3, milestone R4). `.cube` parsing moves to
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

## 3. Phases, steps, validation, releasable builds

### 3.0 How a step is done

Every step below is small enough to be one pull request and carries the same four things:

- **Change** — what code moves, in which files.
- **Verify** — the exact commands that must pass. Always `tools/golden.sh check` (95 scenarios,
  292 frames; green = no visual regression) plus a named `openshot-bench` case with a **numeric
  gate** taken from the 1080p baseline in section 0.4.
- **Flag** — how the change is switched off in production without a revert.
- **Risk** — what can go wrong and what it looks like.

A step is not "done" until the golden suite is green. When a change is *intended* to alter pixels,
inspect every failing triptych in the report, re-baseline only those scenarios
(`tools/golden.sh update <filter>`) and commit the PNGs with the code. Phase-end gates are measured
with a full `openshot-bench --label <phase>` run compared against `baseline-cpu.json` using
`openshot-bench compare`.

### 3.1 Releasable builds and their acceptance gates

Each row is a shippable libopenshot + service pair, behind a flag, with a measurable gate. Targets
are 1080p render-mode fps unless stated; the baseline column is from section 0.4.

| Build | Ships | Key gate (1080p) | baseline → target |
|---|---|---|---|
| **R0** ✅ | golden suite, `openshot-bench`, baseline | suite green, baseline recorded | done |
| **R2a** | GPU-capable image, Skia Vulkan build; the **glow pass only** on the GPU | `text_animated_glow_3` | 1.4 → **≥ 4 fps** |
| **R2b** | all text + subtitle rendering on GPU surfaces | `subtitles_words` | 56 → **≥ 85 fps** |
| **R1** | CPU quick wins, working nvenc, thread budgets | `single_video` nvenc export | 75 → **≥ 105 fps** |
| **R3** | Skia GPU compositor; Qt off the render path | `grid_3x3` | 23 → **≥ 60 fps** |
| **R4** | NVDEC/NVENC frames stay on the GPU; effects as shaders | `heavy_effects`; CPU per export | 11.5 → **≥ 60 fps**; **< 2 cores** |
| **R5** | Qt, ImageMagick, babl, render-path OpenCV removed | no pixel change; image size | −300 MB |
| **R6** | frames in flight, density tuning | `everything` 1080p; GPU busy | 1.8 → **≥ 30 fps**; **≥ 70 %** |

Effort is engineer-weeks for one senior C++ engineer who knows the fork.

**Order changed 2026-09-14: Phase 2 runs before Phase 1.** The table above is in execution order.
Phase and step numbers are stable identifiers, not sequence — `2.1` stays `2.1` wherever it runs, so
that references in `doc/gpu-migration/STATUS.md`, `GPU-DECISIONS.md` and the commit history keep
pointing at the same work.

Why: the measured bottleneck is one shader, not the encoder. `text_animated_glow_3` spends ~72 % of
its time in `paintGlowFromSilhouette` and `everything` 65 %, while R1 by the plan's own admission
"does essentially nothing" for those three scenarios. R2a is therefore the highest-value change
available, and nothing in R2a depends on R1. The one real coupling was the GPU-capable image
(formerly step 1.7), which has moved into Phase 2 as **2.0** because Skia Vulkan cannot ship without
it.

R1 keeps its value and its gate, just later: nvenc rate control and the reader/writer copy removal
are orthogonal to the Skia work and still wanted before Phase 3. The upstream merge of 2026-09-14
already delivered part of 1.5 (the hardware-decode fix) and overlaps 1.2 (thread budgets), so
Phase 1 is smaller than when it was written.

**What R1 will and will not do.** The baseline says the encoder is 0–30 % of wall time depending on
the scenario, so R1 helps light scenarios (`single_video`, `source_4k`) and does essentially nothing
for `everything`, `text_animated_glow_3` or `heavy_effects`. Do not promise "1.5–2× exports" from
R1. Note also that `openshot-bench` already calls `WriteFrame` once for the whole range, so the
service-side chunking fix (1.1) is a real production gain that the bench cannot show; measure that
one on the service.

**Considered and rejected: parallel frame rendering on the CPU.** Rendering frames N and N+1 on two
CPU threads would exploit the 19 idle cores, but Skia Graphite uses one `Context` per process and
gets its parallelism from pipeline depth rather than width, so that machinery would be thrown away
at R3/R6. Process-level parallelism already exists in the service (measured in section 4). The
plan therefore invests in **depth** (decode-ahead, encode-ahead, frames in flight) which survives
the GPU move, and leaves width to the process manager.

### Phase 0 — Baseline and safety net (R0) · **done**

> The step numbers in this section are independent of the subsection numbers in section 0.
> Section 0.1–0.4 is background to read; the 0.1–0.6 below are the work items.

- **0.1 Base branch.** `feature/gpu-rendering` is branched from the fork's `develop`. The upstream
  question (section 0.1) is still open but no longer blocking; if upstream is ever merged it must
  happen *before* R3, because R3 rewrites the same files.
- **0.2 Golden suite.** ✅ `tests/golden`, 95 scenarios / 292 frames mirroring every API the service
  uses, committed PNG goldens, HTML diff report, `tools/golden.sh`. Proven to catch a one-pixel
  composite shift (282 of 292 frames fail).
- **0.3 Benchmark.** ✅ `tests/bench`, `openshot-bench`: 13 scenarios × 5 resolutions ×
  render/x264/nvenc, per-case fps, p50/p95 latency, cores, peak RSS, `--parallel N`, `--resume`,
  `compare`.
- **0.4 Baseline recorded.** ✅ `doc/PERFORMANCE-BASELINE.md` + `tests/bench/results/baseline-cpu.json`.
- **0.5 Still open — production corpus.** Six real payloads with their media, rendered through the
  *service* (not just the library), to catch JSON→timeline regressions the golden suite cannot see.
  *Verify:* all six render without error; frame hashes stable across two runs.
- **0.6 Still open — CI.** Run `tools/golden.sh check` on every PR on the 8-core runner; publish the
  report as an artifact. `openshot-bench --quick` on merges to `develop`, appended to a trend file.

### Phase 2 — Skia on the GPU, smallest useful slice first · **runs first** · 3 weeks

Split into two releasable stops. R2a exists because 72 % of the worst scenario is one shader; it is
worth shipping on its own before touching the rest of the text engine.

**2.0 Infrastructure: GPU-capable image.** *(Listed as step 1.7 while Phase 1 ran first; it is a
prerequisite for everything below — Skia Vulkan needs `libvulkan1` and the `graphics` driver
capability, and there is nothing to validate 2.1 against without a GPU node.)* Switch the service Dockerfile to
`cpp-base-dockerfiles/Dockerfile_cuda12.8.1-cudnn9.7.1-ffmpeg6.1-nvidia24.04`, add `graphics` to
`NVIDIA_DRIVER_CAPABILITIES`, install `libvulkan1` + `vulkan-tools`, drop Google Chrome (unused,
~130 MB). Add `ENCODER=libx264|h264_nvenc` with automatic fallback when no GPU is present.
*Verify:* the container starts on a CPU node and on a GPU node; `ffmpeg -encoders` lists
`h264_nvenc`; `vulkaninfo --summary` reports the NVIDIA ICD on the GPU node; one export completes on
each. *Risk:* base-image drift — pin the digest.

**2.1 Skia with a GPU backend — a second build script, beside the existing one.**
The CPU Skia build is `skia_build_script.sh` at the repository root: it clones Skia at
`$SKIA_MILESTONE` (m147) into `$HOME/skia-stable`, builds `out/Release-CPU/libskia.a` with every GPU
backend disabled, and emits `install_skia.sh`, which installs the static library plus headers into
`/usr/local` (`INSTALL_PREFIX` overridable). `cmake/Modules/FindSkia.cmake` then finds it at
`/usr/local/include/skia` and `/usr/local/lib`.

**Do not modify that script.** The CPU build stays exactly as it is so the raster path (and the
no-GPU fallback of step 5.4) remains reproducible and we can flip back at any point. Instead add a
sibling at the root:

- `skia_build_script_gpu.sh` — same structure, same pinned `SKIA_MILESTONE=m147` (keep it in lockstep
  with the front end's CanvasKit), same checkout reused, but a separate output directory
  `out/Release-GPU` and GN args `skia_enable_graphite = true`, `skia_use_vulkan = true`,
  `skia_enable_ganesh = false` (flip to `true` only if a Graphite feature gap forces it; see the
  4.0 decision), everything else unchanged from the CPU args so text rendering stays identical.
- `install_skia_gpu.sh` — emitted by that script, installing to a **separate prefix**,
  `INSTALL_PREFIX` defaulting to `/usr/local/skia-gpu`, so both Skias coexist on one machine.
  It must also install the `skcms` headers (`modules/skcms`), which `FindSkia.cmake` currently has
  to hunt for.

CMake selects the build with `-DSkia_ROOT=/usr/local/skia-gpu` (CMP0074 makes `find_path`/
`find_library` honour it); no `find_package` change is needed. Record which prefix a given build
used in `doc/gpu-migration/GPU-DECISIONS.md`, and keep both scripts listed in `CLAUDE.md`.

*Verify:* both scripts produce a `libskia.a`; a 30-line test links against the GPU one, creates a
Vulkan device and a Graphite `Context`, draws a gradient into a 64×64 `SkSurface`, reads it back and
saves a PNG — on the dev laptop and, with `VK_ICD_FILENAMES` pointing at lavapipe, on a machine with
no GPU; a build configured with the CPU prefix still passes `tools/golden.sh check`.
*Flag:* the prefix itself — reconfiguring with the CPU `Skia_ROOT` reverts the whole phase.
*Risk:* GN argument drift between the two scripts silently changing text rendering; keep the shared
args identical and diff the two files in review.

**2.2 `src/gpu`: device, frame, surface pool.** `GpuDevice` (singleton: instance, physical device
chosen by `Settings::HW_EN_DEVICE_SET`, device, one queue, Graphite `Context`, `thread_local
Recorder`, `available()`), `GpuFrame` (texture-backed `SkSurface`/`SkImage` + `readback()` +
`upload()`), `GpuSurfacePool` (keyed by size and colour type; never allocate per frame).
Environment switch `OPENSHOT_GPU=off|vulkan|lavapipe`, default `off`.
*Verify:* create/destroy the device 100 times with flat VRAM (NVML) and no leaks; upload→readback of
1000 random RGBA images is bit-identical; the pool returns the same allocation on the second
request. *Flag:* `OPENSHOT_GPU=off` is the default until R2a's gate is met.

**2.3 Glow pass on the GPU.** `TextGlowRenderer` allocates its silhouette, ray-march and bloom
surfaces from the pool as `SkSurfaces::RenderTarget` when the device is available, runs the existing
SkSL unchanged, and the caller reads the result back into the CPU text image. Nothing else in the
text engine changes yet. Choose `kRGBA_8888` for GPU surfaces and remove the R/B swap in
`SkiaRenderer::parseColorString` on that path (raster N32 is BGRA on x86; a GPU RGBA surface is not).
*Verify:* `--scenario text_animated_glow_3 --res 1080p --modes render` ≥ **4 fps** (1.4);
`--scenario everything` ≥ **3 fps** (1.8); `tools/golden.sh check --filter glow` and `--filter text`
within the Loose tolerance (SSIM ≥ 0.95) — a red glyph must still be red, which is the channel-swap
check. *Flag:* `OPENSHOT_GPU=off` falls back to raster. *Risk:* the swap fix is easy to half-apply;
the golden text scenarios are the guard.

> **Release gate R2a.** `text_animated_glow_3` ≥ 4 fps and `everything` ≥ 3 fps at 1080p, golden
> green (text scenarios may be re-baselined once, after visual review of every triptych). This is
> the first build that needs a GPU node, so the Helm change lands here: a small GPU node pool with
> `nvidia.com/gpu: 1` and `NVIDIA_DRIVER_CAPABILITIES=compute,utility,video,graphics`. Keep
> `OPENSHOT_GPU=off` on CPU nodes so the same image still runs there.

**2.4 Whole text engine on GPU surfaces.** Every remaining `SkSurfaces::Raster` in
`TextClipRenderer`, `TextAnimationRenderer` and `TextClipReader::renderToQImage` comes from the pool;
the reader returns a `GpuFrame` with one readback at the boundary instead of `image->copy()`.
*Verify:* `--scenario text_static_4` no worse than baseline; `--scenario text_animated_glow_3` ≥
**8 fps**; golden text scenarios green.

**2.5 Delete the CPU-blur workaround.** The σ > 120 downscale branch in `TextClipRenderer` exists
only because Skia's CPU mask blur clamps at 128 px; the GPU has no such clamp.
*Verify:* a 4K text shadow matches the 1080p shadow scaled up (SSIM ≥ 0.97); `text_static_4` at
2160p ≥ **15 fps** (11.8).

**2.6 Long-lived `SkiaRenderer` and cross-frame caches.** One renderer per reader instead of one per
frame, so the font and paint caches survive; cache the glow silhouette and the 3D block bake as GPU
textures keyed by the plan hash and the animation-independent style.
*Verify:* `text_animated_glow_3` ≥ **12 fps**; golden text scenarios unchanged.

**2.7 Subtitles.** `SubtitleManager::renderAtFrame` draws into a GPU surface; cache the per-word
`buildCharRenderInfo` work per segment.
*Verify:* `--scenario subtitles_words --res 1080p --modes render` ≥ **85 fps** (56);
`tools/golden.sh check --filter subtitles` green.

> **Release gate R2b.** `text_animated_glow_3` ≥ 12 fps, `subtitles_words` ≥ 85 fps,
> `text_static_4` not slower, `everything` ≥ 4 fps, golden green. Runs on the GPU node pool R2a
> introduced; no further infrastructure change.

### Phase 1 — CPU quick wins (R1) · **runs after Phase 2** · 1 week

Independent steps, any order; all are pure CPU and none needs a GPU node.

**1.1 Service: one `WriteFrame` call.** Replace the 8-frame chunk loop in `VideoRenderingImpl.cpp`
with a single `WriteFrame(&timeline, start, end)`; add `FFmpegWriter::SetProgressCallback` and drive
progress from it; raise `pipeline_queue_capacity_` from 8 to 16.
*Verify:* service-side only. One export of a production payload ≥ 15 % faster wall-clock; progress
messages still arrive at least every second; `tools/golden.sh check` green (library unchanged).
*Flag:* `RENDER_SINGLE_WRITEFRAME=0` restores chunking. *Risk:* progress granularity regressions.

**1.2 Service: thread budgets.** Derive `Settings::FF_THREADS` and `OMP_THREADS` from
`/sys/fs/cgroup/cpu.max` divided by `SERVICE_NUM_INSTANCES_PARALLEL`, minimum 2.
*Verify:* in an 8-CPU container with 2 processes no process exceeds ~400 % CPU and wall time does
not regress. On the dev box `openshot-bench --threads 4` must not regress versus `--threads 16` by
more than 10 % on `single_video`. *Flag:* env override `OPENSHOT_THREADS`.

**1.3 Writer: nvenc without the CPU conversion.** nvenc accepts `rgba`. When the codec name contains
`_nvenc`, set `video_codec_ctx->pix_fmt = AV_PIX_FMT_RGBA`, drop `hw_frames_ctx` and the manual
`av_hwframe_transfer_data`, wrap `Frame::GetPixels()` in an `AVFrame` and let libavcodec upload and
convert on the GPU. Delete the per-frame `av_malloc` + `memcpy` in `process_video_packet`. Stop
draining `avcodec_receive_packet` after every frame. Set `colorspace`/`color_primaries`/`color_trc`
to BT.709 on the codec context (the `x264-params` string does nothing for nvenc).
*Verify:* `openshot-bench --scenario single_video --res 1080p --modes nvenc` ≥ **105 fps** (75);
`--scenario source_4k --modes nvenc` ≥ **60 fps** (46); `tools/golden.sh check --filter export`
green; nvenc output vs x264 output on a BT.709 chart within 2 LSB mean.
*Flag:* `OPENSHOT_NVENC_RGBA=0`. *Risk:* colour shift if the range/matrix tags are wrong — the chart
check catches it.

**1.4 Writer: sane nvenc rate control.** Replace `preset slow` + `tune zerolatency` + baseline
profile + `max_b_frames = 0` with `preset p5`, `tune hq`, `rc vbr`, `cq 19`, `b_ref_mode middle`,
`spatial-aq 1`. Stop `SetOption("crf")` hijacking the bitrate when hardware encode is on, and guard
the `hw_en_on`-only branches with `hw_en_supported`.
*Verify:* VMAF of the nvenc output ≥ VMAF of the x264 output − 2 points on `podcast_pip`; file size
within ±20 %; `single_video` nvenc fps does not regress below the 1.3 gate.

**1.5 Reader: remove copies, thread swscale, fix hardware decode.** Drop the `memset` and the
`av_image_copy` in `GetAVFrame`/`ProcessVideoPacket`; build the scaler with `sws_alloc_context` +
`av_opt_set_int(ctx, "threads", n)`. Fix the hardware-decode crash: in `get_hw_dec_format` choose the
pixel format whose `AVCodecHWConfig::device_type` matches `ctx->hw_device_ctx`; set
`next_frame->format = AV_PIX_FMT_NONE` before `av_hwframe_transfer_data`; use `next_frame->format`
(not `pCodecCtx->pix_fmt`) for `av_image_alloc`, `av_image_copy` and the swscale source; treat a
failed transfer as a failed frame. Keep `HARDWARE_DECODER = 0` as the default — the path is needed by
4.2 and is only a CPU-budget win before then.
*Verify:* `tools/golden.sh check` green **with no golden updates** (copy removal must be
bit-identical — if swscale threading changes output, that is a finding, investigate before
re-baselining); `--scenario source_4k --res 1080p --modes render` ≥ **70 fps** (61);
`--scenario single_video --modes render` ≥ **125 fps** (117); hardware decode of a 720p and a 4K
H.264 file no longer throws and matches software decode at PSNR ≥ 48 dB.
*Flag:* `HARDWARE_DECODER` stays 0. *Risk:* `pFrame` lifetime — the decoded frame must outlive the
scale; run the suite under ASan once.

**1.6 `Frame::GetImageCV` memoisation.** Cache `imagecv` with a dirty flag set by `AddImage`; let
`SetImageCV` reuse the buffer instead of allocating two conversions per call.
*Verify:* `--scenario transitions_chain --res 1080p --modes render` ≥ **33 fps** (27.4);
`--scenario heavy_effects` ≥ **13 fps** (11.5); golden green with no updates.
*Risk:* a stale cache shows up as a frozen frame inside an effect chain — the `effects.*` and
`transitions.*` golden scenarios cover it.

> **Release gate R1.** Full `openshot-bench --label r1` run; `compare baseline-cpu.json r1.json`
> shows `single_video` nvenc ≥ 105 fps, `source_4k` render ≥ 70 fps, no scenario slower than
> baseline by more than 5 %, golden suite green. Ship behind `ENCODER=` and run in shadow for a week.

### Phase 3 — GPU compositor and Qt off the render path (R3) · 4 weeks

Steps 3.1–3.4 are sequential; 3.5–3.7 can run in parallel once 3.2 lands.

**3.1 Timeline canvas on the GPU.** `Timeline::GetFrame` takes its output surface from the pool
(`kRGBA_F16`, decision 4.0) and passes its `SkCanvas` down through `add_layer`. `Frame` gains a
`GpuFrame`; `GetImage()` on a GPU frame performs one cached readback so every unported path still
works. The writer still reads back once per frame — the last CPU copy, removed in 4.4.
*Verify:* golden green across the board (PSNR ≥ 50 dB vs the CPU goldens, which is the whole point of
the suite); `--scenario single_video --modes render` not slower than baseline.

**3.2 `Clip::draw(SkCanvas&)`.** Collapse `apply_keyframes` + `apply_background` into one draw:
`get_transform` returns an `SkMatrix` built from the same arithmetic, paint alpha from the opacity
curve, `SkBlendMode` from `blend_mode`, `SkSamplingOptions(kLinear, kLinear)`.
*Verify:* a unit test feeds 200 random keyframe sets to the old `QTransform` and the new `SkMatrix`
and compares the six affine coefficients to 1e-6; `tools/golden.sh check --filter compositing` — all
16 blend modes within PSNR 48 dB; `--scenario grid_3x3 --modes render` ≥ **45 fps** (23).

**3.3 Blur, shadow, crop, flip on the paint.** `SkImageFilters::Blur` with the existing
box→sigma mapping, `SkImageFilters::DropShadowOnly` with the same offset and colour, `clipRRect` for
crop, negative scale for flip. Delete `get_shadow_image`, the local `gaussian_blur` and the scalar
opacity loop.
*Verify:* `tools/golden.sh check --filter clipfx` (SSIM ≥ 0.97 on shadows, PSNR ≥ 40 dB on blur);
`--scenario podcast_pip --modes render` ≥ **45 fps** (23).

**3.4 Delete `BlendModes.cpp` and the `GetImageCV` calls in `Clip.cpp`.**
*Verify:* `grep -c QPainter src/Clip.cpp src/Timeline.cpp` is 0; `--scenario blend_stack_5 --modes
render` ≥ **50 fps** (19); golden green.

**3.5 Image and SVG readers on Skia.** `SkCodec` for PNG/JPEG (honouring EXIF orientation), Skia's
SVG module or resvg for shapes, one texture cached per reader. Replaces `QtImageReader`.
*Verify:* 20 production PNG/JPEG files and 20 shape SVGs at PSNR ≥ 50 dB vs `QtImageReader`;
transparent PNG edges show no fringing over a coloured background; `tools/golden.sh check --filter
readers`.

**3.6 Subtitles and text draw straight into the timeline canvas** — no intermediate surface, no
readback. *Verify:* `--scenario subtitles_words --modes render` ≥ **120 fps**; golden green.

**3.7 Qt off the render path.** Build options `ENABLE_PLAYER=OFF` and `ENABLE_MAGICK=OFF`; replace
`QString`/`QDir`/`QFile`/`QRegularExpression` in `Timeline`, `Profiles`, `ColorMap`, `ChunkReader/Writer`
with the standard library; `Color` without `QColor`.
*Verify:* unit tests for path rewriting, `.cube` parsing and hex colour round-trips; golden green;
`grep -rn QPainter src/*.cpp src/effects/*.cpp` only matches files behind `ENABLE_LEGACY_EFFECTS`.

> **Release gate R3.** `grid_3x3` ≥ 60 fps, `blend_stack_5` ≥ 50 fps, `podcast_pip` ≥ 45 fps,
> `single_video` ≥ 200 fps, `everything` ≥ 8 fps, all at 1080p; golden green; no `QPainter` on the
> render path. Default `OPENSHOT_GPU=vulkan` on GPU nodes.

### Phase 4 — Frames never leave the GPU (R4) · 6 weeks

4.1–4.4 sequential (interop first); 4.5–4.7 parallel with them and with each other.

**4.0 Record the decisions** in `doc/gpu-migration/GPU-DECISIONS.md` before starting: canvas precision
(`kRGBA_F16` recommended), Graphite-only or Ganesh fallback, LUT rounding reference (native
`ColorMap.cpp` or the WASM `LutApply.cpp` the front end uses), whether nearest-neighbour sampling is
preserved in `BORDER_REFLECTED_ROTATION` and `DISPLACEMENT_MAP`, GPU SKU (L4).

**4.1 `src/gpu/CudaInterop`.** Import a Vulkan image's memory and semaphore into CUDA
(`vkGetMemoryFdKHR` → `cuImportExternalMemory`, `cuImportExternalSemaphore`) and provide
`copyNV12(AVFrame* cudaFrame, GpuImage& y, GpuImage& uv, stream)` as two device-to-device
`cuMemcpy2DAsync`.
*Verify:* fill a CUDA NV12 buffer with a known pattern, copy, sample both planes in a trivial SkSL
shader, read back, compare exactly; clean under `compute-sanitizer`; ≤ 0.3 ms per 4K frame.

**4.2 Reader keeps frames on the GPU.** Decoder output stays `AV_PIX_FMT_CUDA`; YUV→RGBA becomes an
SkSL pass (matrix and range from the stream, defaulting to BT.709 at ≥ 720p) that also applies the
pre-scale. Extend `IsHardwareDecodeSupported` to HEVC, VP9, AV1 and MPEG-4; remove `DE_LIMIT_*`;
software decode + `upload()` remains the fallback for codecs NVDEC lacks.
*Verify:* decode-only 4K ≥ **120 fps** and < 1 core; decoded frame vs software decode PSNR ≥ 48 dB;
a BT.709 chart decodes to the right sRGB values — note in `doc/gpu-migration/GPU-DECISIONS.md` that this intentionally
*differs* from the CPU goldens, which apply swscale's BT.601 default, and re-baseline the affected
`readers.*` scenarios once.

**4.3 Decode read-ahead.** One thread per reader keeps four decoded surfaces ahead for sequential
access; seeks flush it.
*Verify:* decode no longer appears in a `nsys`/gdb profile of `source_4k`; `--scenario source_4k
--modes x264` ≥ **90 fps** (47.5).

**4.4 Writer consumes textures.** Allocate `hw_frames_ctx` (NV12, or P010 for 10-bit), convert
RGBA→NV12 with an SkSL pass into a CUDA-mapped buffer, send `AV_PIX_FMT_CUDA` frames, keep the
encoder queue four deep. Software encoders keep the readback path.
*Verify:* `--scenario single_video --res 1080p --modes nvenc` ≥ **250 fps**; 2160p ≥ **60 fps**;
CPU per export < 2 cores; RGBA→NV12→RGBA round trip within 1 LSB.

**4.5 `GpuEffect` base and the per-pixel shaders.** One SkSL fragment each, with a parity test
against the C++ twin: Alpha, Brightness, Exposure, ColorShift, Bars, ChromaKey, ColorAdjustment,
LightAdjustment, Enhancement, ColorMap (3-D LUT texture), Mask, Crop, CameraMovement.
*Verify per effect:* PSNR ≥ 48 dB vs the CPU effect on eight test images including transparent and
semi-transparent pixels, ≤ 0.2 ms at 1080p; `tools/golden.sh check --filter effects`.
*Gate:* `--scenario heavy_effects --modes render` ≥ **60 fps** (11.5); `chroma_key_green` ≥ **70 fps**
(12.9).

**4.6 Transition shaders.** Port the `image-processing-lib` vocabulary (box/diagonal/rotational/zoom
blur, zoom, border-reflected move and rotation, threshold wipe, circle mask, split shift, colour
shift) to SkSL, keeping the sources in `image-processing-lib/shaders/` so CanvasKit can load the same
code later. The C++ stays as the oracle.
*Verify:* PSNR ≥ 45 dB against the OpenCV version at three parameter values each;
`--scenario transitions_chain --modes render` ≥ **70 fps** (27.4).

**4.7 Overlay clips as textures.** The overlay renders to a pooled surface; additive blend becomes
`kPlus` on RGB via a runtime blender, displacement map a two-texture shader. Deletes the last
`GetImageCV` round trips. *Verify:* `tools/golden.sh check --filter overlay`; a transition frame
costs no more than a plain two-clip frame ±10 %.

> **Release gate R4.** `heavy_effects` ≥ 60 fps, `chroma_key_green` ≥ 70 fps, `transitions_chain`
> ≥ 70 fps, `grid_3x3` ≥ 90 fps, `everything` ≥ 20 fps, all 1080p; CPU per export < 2 cores; golden
> green with only the documented BT.709 re-baseline.

### Phase 5 — Remove Qt and the rest of the CPU stack (R5) · 2 weeks

**5.1** `Frame` drops `QImage` from its API; `readback()` returns an `SkPixmap`, and the golden
harness's `Image.cpp` switches to `SkPngEncoder`/`SkPngDecoder` (that file is the only Qt user in the
suite, by design).
**5.2** Delete `BlendModes.cpp`, `QtImageReader`, `QtTextReader`, `QtHtmlReader`, `TextReader`,
ImageMagick `ImageReader`/`ImageWriter`/`MagickUtilities`, `CacheDisk`, `ScreenCaptureReader*`,
`src/Qt/*`, `QtPlayer`, `PlayerBase`, `RendererBase`, `FrameScope`, and the unported effects in
section 2.4 unless product asks for them.
**5.3** Remove `find_package(Qt…)`, ImageMagick and babl from `src/CMakeLists.txt`, and
`Qt5::Widgets/Gui` from the service; rebuild the image without Qt, Chrome and ImageMagick.
**5.4** Keep the Skia raster backend as the no-GPU fallback (it costs nothing and keeps laptops and
CI working).
*Verify:* `ldd libopenshot.so | grep -ci qt` is 0; image at least 300 MB smaller; golden green with
**zero** pixel change versus R4 (this phase must not alter rendering); the service builds on a
machine with no Qt installed.

### Phase 6 — Depth and density (R6) · 2 weeks

**6.1 Frames in flight.** A ring of four pooled canvases with a fence each: record frame n+2 while
n+1 executes and n encodes. Remove `Timeline::getFrameMutex` from the read path (keep it for edits).
*Verify:* `everything` 1080p ≥ **30 fps**; GPU busy ≥ 70 % during a 1080p export (NVML); no VRAM
growth over 10 000 frames.

**6.2 Density.** Re-run `openshot-bench --parallel 1,2,4` on the target GPU SKU and set
`SERVICE_NUM_INSTANCES_PARALLEL`, the GPU time-slicing replica count and the pod requests from the
measurements; update section 4 of this document with real numbers.
*Verify:* four concurrent 1080p exports each ≥ 2× real time on an L4; aggregate ≥ 3.2× a single
export.

**6.3 Observability.** Per export: frames, wall time, GPU busy %, NVENC/NVDEC utilisation, VRAM
peak, fallback events. *Verify:* the numbers appear for a production export and match `nvidia-smi`
within 10 %.

> **Release gate R6 / end state.** 1080p `everything` ≥ 30 fps (real time, a 17× improvement),
> 4K `everything` ≥ 10 fps, CPU per export < 2 cores, four concurrent 1080p exports per L4.

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
