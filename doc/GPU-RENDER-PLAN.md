# GPU Render Pipeline — Build Plan

Status: draft 2, 2026-09-10. Branch: `feature/gpu-rendering` (from fork `develop` 1d82adc9). Owner: rendering team.
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

Releasable builds, in order. Each is a tagged libopenshot + service pair that can run in
production behind a flag.

| Build | What ships | User-visible change |
|---|---|---|
| **R0** | corpus, harness, CI parity job | none |
| **R1** | CPU quick wins, nvenc option, threading fixes | 1.5–2× faster exports on CPU nodes; GPU nodes optional |
| **R2** | text + subtitles rendered by Skia on the GPU, rest unchanged | animated text and glow stop being the slow clip type |
| **R3** | Skia GPU compositor, image/SVG readers on Skia, codecs still through CPU memory | compositing cost gone; Qt off the render path |
| **R4** | NVDEC surfaces straight into the compositor, NVENC straight out; effects and transitions as shaders | full GPU pipeline; ~9 CPU cores per 4K stream freed |
| **R5** | Qt, ImageMagick, babl, render-path OpenCV removed; CPU path deleted or kept as software fallback | smaller image, headless-only library |
| **R6** | depth scheduling, multi-export density tuning | GPU kept busy; cost per export drops |

Effort column is engineer-weeks for one senior C++ engineer familiar with the fork.

### Phase 0 — Baseline (R0) · 1 week · everything sequential

**0.1 Base branch.** Record the section 0.1 decision (A or B) in `doc/GPU-DECISIONS.md`.
If A, do the upstream merge as one PR now.
**Validate:** for A, listed in 0.1; for B, nothing to validate.

**0.2 Corpus.** Collect 6 production payloads and their media into a bucket, with a
script `tools/corpus/fetch.sh`. Required coverage: (a) text-heavy with glow + 3D tilt +
keyframed style, (b) transition-heavy (every effect name in `Transition.cpp` at least
once, one overlay clip of each type), (c) 4K source into 1080p, (d) subtitle-heavy,
(e) every blend mode, (f) shapes + LINE reveal + crop with corner radius + mask matte.
**Validate:** all 6 render on `develop` without error; note fps and wall time.

**0.3 Harness.** Move the session's `hwbench.cpp` into `examples/openshot-bench.cpp`
with modes `decode`, `render`, `export`, options for codec, `--pipeline`, `--hwdec`,
`--frames`, `--dump-every N` (PNG dump). Add `tools/parity.py`: given two PNG sets,
report PSNR, SSIM (text frames), max abs diff, and per-region exact-match on flat colour.
**Validate:** `openshot-bench export corpus/a.json --frames 300` prints fps; `parity.py`
of a render against itself reports PSNR = inf.

**0.4 Golden set.** Render every 10th frame of the 6 payloads on `develop` to PNG,
store under `corpus/golden/<payload>/<commit>/`. This is the oracle that survives the
deletion of the CPU path.
**Validate:** re-rendering `develop` twice gives bit-identical PNGs (it should; if not,
find the nondeterminism now — candidates: OpenMP reductions, uninitialised padding).

**0.5 CI job.** A GitHub Actions job on the `cpp-runner-8c-16gb-300gb` runner: build,
run the corpus in render mode with `--dump-every 10`, run `parity.py` against golden,
fail below PSNR 45 dB / SSIM 0.98. GPU jobs come later on a GPU runner.
**Validate:** job green on `develop`; job red when a deliberate 1-pixel shift is
introduced in `apply_background`.

### Phase 1 — CPU quick wins (R1) · 1 week · steps marked [parallel] are independent

**1.1 Service: one `WriteFrame` call.** Replace the 8-frame chunk loop with a single
`WriteFrame(&timeline, start, end)`; publish progress from a `std::atomic<int64_t>`
that `FFmpegWriter` increments (add `SetProgressCallback`). Raise
`pipeline_queue_capacity_` to 16.
**Validate:** bench export on payload (c): ≥ +15 % fps vs R0; progress messages still
arrive at ≤ 1 s intervals.

**1.2 Service: thread budgets.** [parallel] Read the cgroup CPU quota
(`/sys/fs/cgroup/cpu.max`), divide by `SERVICE_NUM_INSTANCES_PARALLEL`, set
`Settings::FF_THREADS` and `OMP_THREADS`.
**Validate:** in an 8-CPU container with 2 processes, `top` shows no process above
400 % CPU; export wall time does not regress.

**1.3 Writer: nvenc without swscale.** [parallel] nvenc lists `rgba` among its input
formats (`ffmpeg -h encoder=h264_nvenc`). When the codec name contains `_nvenc`, set
`video_codec_ctx->pix_fmt = AV_PIX_FMT_RGBA`, skip `hw_frames_ctx` and the manual
`av_hwframe_transfer_data`, hand libavcodec an `AVFrame` that wraps
`Frame::GetPixels()` directly, and let the encoder do the upload and the RGB→YUV
conversion on the GPU. Set `colorspace = AVCOL_SPC_BT709`, `color_primaries`, `color_trc`.
Delete the `av_malloc` + `memcpy` in `process_video_packet`. Do not drain
`avcodec_receive_packet` to empty after every frame; poll it.
**Validate:** bench export with `h264_nvenc`: ≥ 130 fps on the dev box for payload (a)
at 1080p (was 112). Output colour matches the libx264 output within 1 LSB on a
BT.709 test chart (`ffmpeg -lavfi psnr`).

**1.4 Writer: encoder options.** [parallel] For nvenc use `preset p5`, `tune hq`,
`rc vbr`, `cq 19`, `b_ref_mode middle`, `spatial-aq 1`; remove the forced baseline
profile, `max_b_frames = 0`, `tune zerolatency`. Keep `crf 18 / preset medium` for x264.
Make `SetOption("crf")` stop overriding bitrate when hardware encode is on.
**Validate:** VMAF of nvenc output vs source ≥ x264 output − 2 points on payload (c);
file size within ±20 %.

**1.5 Reader: remove copies.** [parallel] Drop the `memset` and the `av_image_copy`
in `GetAVFrame`/`ProcessVideoPacket` (the decoded `AVFrame` is already a private
ref-counted buffer); run swscale with `sws_alloc_context` + `av_opt_set_int("threads", n)`.
Fix the hardware decode path so it no longer throws: in `get_hw_dec_format` pick the
pixel format whose `AVCodecHWConfig::device_type` matches `ctx->hw_device_ctx`; set
`next_frame->format = AV_PIX_FMT_NONE` before `av_hwframe_transfer_data` so FFmpeg
downloads in the surface's native format (NV12); use `next_frame->format` (not
`pCodecCtx->pix_fmt`) for `av_image_alloc`, `av_image_copy` and the swscale source
format; treat a failed transfer as a failed frame instead of continuing. Equivalent to
upstream 1.0.0's `sw_pix_fmt` handling. Leave `HARDWARE_DECODER = 0` as default; the
path is needed in 4.2 and is only a CPU-budget win until then.
**Validate:** bench decode 4K: ≥ 100 fps (was 59). PSNR vs R0 frames = inf (pure copy
removal must be bit-identical). `openshot-bench decode --hwdec 2` on a 720p and a 4K
H.264 file no longer throws; frame 100 vs software PSNR ≥ 48 dB (NV12 chroma rounding).

**1.6 Frame: memoise `GetImageCV`.** [parallel] Cache `imagecv` with a dirty flag set
by `AddImage`; make `SetImageCV` reuse the buffer.
**Validate:** payload (b) render-only fps ≥ +20 %; parity PSNR = inf.

**1.7 Service + infra: GPU-capable image, optional.** [parallel] Switch the service
Dockerfile base to `Dockerfile_cuda12.8.1-cudnn9.7.1-ffmpeg6.1-nvidia24.04`; add
`ENCODER=libx264|h264_nvenc` env with fallback to x264 when `nvidia-smi` is absent.
Do not yet request GPUs in Helm; this just proves the image.
**Validate:** container starts on a CPU node and on a GPU node; `ffmpeg -encoders`
inside lists `h264_nvenc`; an export completes on each.

**Releasable build R1.** Tag, run the corpus in shadow mode for one week.

### Phase 2 — Skia on the GPU for text and subtitles (R2) · 3 weeks · sequential

This is the first GPU code. It touches only `src/text`, `src/subtitle` and the build.
Everything else keeps working on QImages; the GPU result is read back into a QImage
per text frame. That readback (about 2–3 ms at 1080p) is the price of shipping early;
it disappears in R3.

**2.1 Skia build.** In `skia_build_script.sh`: `skia_enable_graphite = true`,
`skia_use_vulkan = true`, keep `skia_enable_ganesh = false`, keep m147. Install skcms
headers with the rest (`FindSkia.cmake` currently hunts for them). Add `libvulkan-dev`,
`glslang` not needed (Skia ships its compiler).
**Validate:** `libskia.a` links into libopenshot; a 20-line test creates a Vulkan
device, a Graphite `Context`, draws a gradient into a 64×64 `SkSurface`, reads it back,
saves PNG. Runs on the dev laptop (RTX A2000) and, with `VK_ICD_FILENAMES` pointing at
lavapipe, on a machine with no GPU.

**2.2 `src/gpu/GpuDevice`.** Singleton per process: `VkInstance`, physical device
chosen by `Settings::HW_EN_DEVICE_SET`, `VkDevice`, one queue, Graphite `Context`, a
`Recorder` per calling thread (`thread_local`), `flush()`. Environment switch
`OPENSHOT_GPU=off|vulkan|lavapipe`. Fails soft: if creation fails, `available() == false`
and callers use CPU raster.
**Validate:** unit test creates and destroys the device 100 times without leaks
(`valgrind` or VRAM via NVML flat); `OPENSHOT_GPU=off` forces CPU.

**2.3 `src/gpu/GpuFrame`.** Holds `sk_sp<SkSurface>` (GPU) or `SkBitmap` (CPU),
size, `SkColorType`, `readback() -> std::shared_ptr<QImage>` (for now), `upload(QImage)`.
Surface pool keyed by size and colour type; never allocate per frame.
**Validate:** round trip upload → readback is bit-identical for 1000 random RGBA8
images; pool returns the same allocation on the second request.

**2.4 Text renderer on a GPU surface.** In `TextClipReader::renderToQImage`: request a
pooled GPU surface, build `SkCanvas` from it, keep every call into
`renderTextFrame` unchanged, then `readback()` into the Frame. Replace each
`SkSurfaces::Raster` in `TextGlowRenderer`, `TextAnimationRenderer`,
`TextClipRenderer` with a helper `gpu::makeSurface(w,h)` that returns a GPU surface
when the device is available and raster otherwise. Remove the R/B swap in
`SkiaRenderer::parseColorString` only when the surface is `kRGBA_8888`; choose
`kRGBA_8888` for all GPU surfaces.
**Validate:** payload (a): parity vs golden SSIM ≥ 0.98 on text frames, no colour
channel swap (a red glyph is red); bench render-only for payload (a) ≥ 2× R1 fps.
If slower than R1 for static text (readback overhead), keep the `rendered_image`
cache path on CPU raster for static clips.

**2.5 Delete the σ>120 shadow workaround** in `TextClipRenderer` on the GPU path
(GPU mask blur has no 128 px clamp).
**Validate:** payload (a) at 4K: 4K shadow blur identical in shape to the 1080p one
scaled (compare downscaled 4K vs 1080p golden, SSIM ≥ 0.97).

**2.6 Long-lived `SkiaRenderer`.** One instance per `TextClipReader` and one per
`SubtitleManager`, so the font and paint caches survive across frames.
**Validate:** frame time for a static-style animated clip drops (measure with
`--frames 300`); no behaviour change in parity.

**2.7 Subtitles.** `SubtitleManager::renderAtFrame` draws into a GPU surface that is
initialised from the timeline QImage (`upload`) and read back after drawing. This is
temporary and only worth it if payload (d) gets faster; otherwise leave subtitles on CPU
raster until R3, where they draw into the GPU timeline canvas for free.
**Validate:** payload (d) render-only fps not lower than R1; parity SSIM ≥ 0.98.

**2.8 Glow and bake caches.** Cache the glow silhouette texture and the 3D block bake
texture across frames, keyed by the resolved plan hash without the per-frame animation
transform. Invalidate on any style keyframe change.
**Validate:** payload (a) animated glow clip: per-frame time ≤ 40 % of 2.4; visual
parity unchanged.

**Releasable build R2.** Flag `OPENSHOT_GPU=vulkan` on GPU nodes only; CPU nodes
unchanged. First Helm change: a small GPU node pool (2× L4), `nvidia.com/gpu: 1`,
`NVIDIA_DRIVER_CAPABILITIES=compute,utility,video,graphics`.

### Phase 3 — Skia GPU compositor and Qt-free readers (R3) · 4 weeks

Steps 3.1–3.4 sequential; 3.5–3.7 [parallel] with each other after 3.2.

**3.1 Timeline canvas on the GPU.** `Timeline::GetFrame` allocates the output as a
pooled GPU `SkSurface` (`kRGBA_F16` recommended; decide in 3.0 below) and passes its
`SkCanvas` down through `add_layer` instead of a QImage. `Frame` gains `GpuFrame`;
`GetImage()` on a GPU frame does a one-time `readback()`. The writer still consumes
`GetPixels()`, so it reads back once per frame (this is the last CPU copy; removed in R4).
**Validate:** payload (e) with all clips forced to `BLEND_NORMAL`: renders; parity
PSNR ≥ 50 dB vs golden. Fps not lower than R2 (readback replaces the canvas fill).

**3.2 `Clip::draw(SkCanvas&)`.** Replace `apply_keyframes` + `apply_background` with
one draw: `get_transform` returns `SkMatrix` (same arithmetic; write a unit test that
feeds 200 random keyframe sets to both the old `QTransform` and the new `SkMatrix` and
compares the 6 affine coefficients to 1e-6); paint alpha from the opacity curve;
`SkBlendMode` from `blend_mode`; `SkSamplingOptions(kLinear, kLinear)`. Source is the
clip's texture (`upload()` of the reader's CPU frame in this phase).
**Validate:** payload (e): every blend mode PSNR ≥ 48 dB vs golden (the W3C formulas
are the same; differences are 8-bit rounding); payload (f) crop and rotation edges
SSIM ≥ 0.98.

**3.3 Blur, shadow, flip, crop on the paint.** `SkImageFilters::Blur` with the existing
`sigma_for_box` mapping; `SkImageFilters::DropShadowOnly` + image draw for the shadow
with the same sigma, offset and colour; flip as negative scale in the matrix;
`Crop` effect becomes `clipRRect` before the draw (keep `Crop` as an effect class so the
JSON path is unchanged).
**Validate:** payload (f): shadow position and softness vs golden SSIM ≥ 0.97; blurred
clip PSNR ≥ 40 dB (Gaussian vs Gaussian, different kernels). Delete `get_shadow_image`,
`gaussian_blur`, and the opacity loop.

**3.4 Delete `BlendModes.cpp`** and the `GetImageCV` calls in `Clip.cpp`.
**Validate:** grep confirms no `QPainter` in `Clip.cpp`/`Timeline.cpp`; corpus parity
unchanged from 3.3.

**3.5 Image reader on Skia codecs.** [parallel] New `ImageReader` (rename after
deleting the Magick one): `SkCodec::MakeFromData` → `SkBitmap` → one texture per
reader, honouring EXIF orientation; SVG through Skia's SVG module
(`SkSVGDOM::MakeFromStream` → render into a pooled surface at the requested size) or
resvg if the shapes need features Skia's module lacks (test with 20 production shape
SVGs first). Replace `openshotImageReader` in the service.
**Validate:** 20 PNG/JPEG + 20 shape SVGs: PSNR ≥ 50 dB vs `QtImageReader` output;
transparent PNG edges show no fringing over a coloured background (premultiplication
correct).

**3.6 Subtitles into the timeline canvas.** [parallel] `renderAtFrame(SkCanvas&)`;
delete the QImage wrap.
**Validate:** payload (d) parity SSIM ≥ 0.98; render-only fps ≥ R2.

**3.7 Text clips return textures.** [parallel] `TextClipReader::GetFrame` returns a
`GpuFrame` (no readback, no `image->copy()`); `Clip::draw` samples it.
**Validate:** payload (a) render-only fps ≥ 1.5× R2.

**3.8 Qt off the render path.** Apply section 2.7 steps 1–3 and 5 (build options,
std-library replacements, `Color`). Qt still links for `Frame::GetImage()` readback
returning `QImage` and for unported effects.
**Validate:** `grep -rn QPainter src/*.cpp` returns only files behind
`ENABLE_LEGACY_EFFECTS`/`ENABLE_PLAYER`; corpus parity unchanged.

**Releasable build R3.** Default `OPENSHOT_GPU=vulkan` on GPU nodes. Expected export
fps on the dev box for payload (a): ≥ 250 (readback + nvenc bound).

### Phase 4 — Codecs on the GPU and effects as shaders (R4) · 6 weeks

4.1–4.4 sequential (interop first), 4.5–4.7 [parallel] with 4.1–4.4 and each other.

**3.0/4.0 Decisions to record before starting** (in `doc/GPU-DECISIONS.md`):
canvas precision (`kRGBA_F16` recommended), Graphite-only or Ganesh fallback, reference
for LUT rounding (native `ColorMap.cpp` or WASM `LutApply.cpp`), whether nearest-neighbour
sampling is kept in `BORDER_REFLECTED_ROTATION` and `DISPLACEMENT_MAP`, GPU SKU (L4).

**4.1 `src/gpu/CudaInterop`.** Import a Vulkan image's memory into CUDA
(`vkGetMemoryFdKHR` → `cuImportExternalMemory` → `cuExternalMemoryGetMappedMipmappedArray`
or a linear buffer) and a Vulkan semaphore into CUDA (`cuImportExternalSemaphore`).
Provide `copyNV12(AVFrame* cudaFrame, GpuImage& dstY, GpuImage& dstUV, stream)` doing
two `cuMemcpy2DAsync` device-to-device, then signal.
**Validate:** unit test: fill a CUDA NV12 buffer with a known pattern, copy, sample the
Vulkan textures in a trivial SkSL shader, read back, compare exactly. Run under
`compute-sanitizer`. Time per 4K frame ≤ 0.3 ms.

**4.2 Reader keeps frames on the GPU.** With `HARDWARE_DECODER=2` do not call
`av_hwframe_transfer_data`; hand the `AVFrame` (format `AV_PIX_FMT_CUDA`) to 4.1 and
produce a `GpuFrame` whose image is built with an SkSL YUV→RGBA shader (matrix and range
from `AVFrame::colorspace`/`color_range`, defaulting to BT.709 for ≥ 720p as the front
end does), downscaled to project size in the same pass. Extend
`IsHardwareDecodeSupported` to HEVC, VP9, AV1, MPEG-4; remove `DE_LIMIT_*`. Unsupported
codecs: software decode + `upload()`.
**Validate:** bench decode 4K with `--hwdec 2`: ≥ 120 fps and < 1 CPU core; PSNR of
decoded frame vs software path ≥ 48 dB (chroma upsampling differs); a BT.709 colour
chart decodes to the right sRGB values (today's BT.601 mistake is gone; document the
intentional difference vs golden).

**4.3 Decode read-ahead.** One thread per `FFmpegReader` keeps 4 decoded surfaces ahead
for sequential access; seeks flush it.
**Validate:** bench export payload (c): decode no longer appears in the main-thread
profile (gdb sampling or `nsys`); total fps ≥ 1.3× 4.2.

**4.4 Writer consumes textures.** For `*_nvenc`: allocate `hw_frames_ctx`
(`sw_format` NV12, or P010 when the timeline canvas is F16 and 10-bit is requested), an
SkSL RGBA→NV12 pass into two Vulkan images exported to CUDA, `cuMemcpy2DAsync` into the
`av_hwframe_get_buffer` frame, `avcodec_send_frame`. Queue depth 4; receive packets on
the consumer thread. For software encoders keep `readback()` + swscale.
**Validate:** bench export payload (a) at 1080p ≥ 300 fps on the dev box; 4K ≥ 60 fps;
CPU per export < 2 cores; NV12 round trip (RGBA → NV12 → RGBA) within 1 LSB on the chart.

**4.5 `GpuEffect` base and the simple fragments.** [parallel] `GpuEffect::filter(frame)
-> sk_sp<SkImageFilter>` (composable) or `draw(SkCanvas&, sk_sp<SkImage>)`. Port, one
PR each, with a parity test against the C++ twin on the effect's own test images:
Alpha, Brightness, Exposure, ColorShift, Bars, ChromaKey, ColorAdjustment,
LightAdjustment, Enhancement, ColorMap (3D LUT texture, trilinear or tetrahedral per the
4.0 decision), Mask (second texture), Crop (already clip), CameraMovement (matrix only).
**Validate per effect:** PSNR ≥ 48 dB vs the CPU effect on 8 test images including
fully transparent and semi-transparent pixels; runtime ≤ 0.2 ms at 1080p.

**4.6 Transition shaders.** [parallel] For each `image-processing-lib` function used by
`Transition.cpp`: BLUR (two-pass box), DIAGONAL_BLUR, ROTATIONAL_BLUR (N angular taps),
ZOOM_BLUR (N radial taps, mirror wrap), ZOOM (matrix + `kMirror` tile), 
BORDER_REFLECTED_MOVE (`kMirror`), BORDER_REFLECTED_ROTATION (nearest if decided),
THRESHOLD_WIPE_MASK, CIRCLE_MASK (AA clip path or SDF), SPLIT_SHIFT (two clipped draws),
overlay ADDITIVE_BLEND (`kPlus` on RGB via `SkBlenders`/runtime blender), overlay
DISPLACEMENT_MAP (two-texture SkSL). Put the SkSL sources in
`image-processing-lib/shaders/` so the front end can load them through CanvasKit later.
**Validate per effect:** PSNR ≥ 45 dB vs the OpenCV version at three parameter values
(low/mid/high) on payload (b) frames; the box/mirror/nearest choices documented where
parity is by design "close" not "exact".

**4.7 Overlay clips as textures.** [parallel] The overlay clip renders to a pooled
surface; `Clip::draw` applies the blend shader. Delete the `GetImageCV` round trips.
**Validate:** payload (b) light-leak transition: parity ≥ 45 dB; frame time equals a
plain two-clip frame within 10 %.

**Releasable build R4.** Full GPU path on GPU nodes; software path on CPU nodes.

### Phase 5 — Remove Qt and the rest of the CPU stack (R5) · 2 weeks · sequential

**5.1** `Frame` drops `QImage` from its API: `readback()` returns `SkPixmap`/`SkBitmap`;
the writer's software encoder path and `tools/parity.py` dumps use `SkPngEncoder`.
**5.2** Delete `BlendModes.cpp`, `QtImageReader`, `QtTextReader`, `QtHtmlReader`,
`TextReader`, `ImageReader`(Magick), `ImageWriter`, `MagickUtilities`, `CacheDisk`,
`ScreenCaptureReader*`, `src/Qt/*`, `QtPlayer`, `PlayerBase`, `RendererBase`,
`FrameScope`, unported effects (2.4 list) unless product asked for them.
**5.3** Remove `find_package(Qt…)`, ImageMagick, babl from `src/CMakeLists.txt`; remove
`Qt5::Widgets/Gui` from the service; rebuild the Docker image without Qt, Chrome,
ImageMagick.
**5.4** Decide whether the CPU compositor stays as the no-GPU fallback (Skia raster
backend, same code, slow) or is removed. Recommendation: keep it, it costs nothing and
keeps laptops and CI working.
**Validate:** `ldd libopenshot.so | grep -ci qt` = 0; image size reduced by ≥ 300 MB;
corpus parity vs R4 = inf (no visual change is expected in this phase); the service
still builds on a machine without Qt installed.

### Phase 6 — Depth and density (R6) · 2 weeks

**6.1** Frames in flight: record frame n+2 while n+1 executes and n encodes, using a
ring of 4 pooled canvases and a fence per frame. Remove `Timeline::getFrameMutex` from
the read path (keep it for edits).
**6.2** Density: measure NVENC sessions, VRAM and GPU busy % per concurrent export on an
L4; set `SERVICE_NUM_INSTANCES_PARALLEL` and the Helm GPU time-slicing replica count.
**6.3** Observability: per export publish frames, wall ms, GPU busy %, NVENC/NVDEC
utilisation (NVML), VRAM peak, fallback events.
**Validate:** GPU busy ≥ 70 % during a 1080p export; 4 concurrent 1080p exports on one
L4 each ≥ 2× realtime; no VRAM growth over 10,000 frames.

---

## 4. Parity policy (applies to every step)

- **exact**: PSNR = inf or max diff ≤ 1 LSB. Required for copy removals, format changes,
  blend modes, separable colour effects.
- **close**: PSNR ≥ 45 dB or SSIM ≥ 0.98. Accepted where the algorithm legitimately
  differs (GPU anti-aliasing, Gaussian vs box kernels, bilinear vs bicubic).
- **redefine**: the old output was wrong or implementation-defined (BT.601 applied to
  BT.709 sources, R/B swap, nearest sampling artefacts). Document in
  `doc/GPU-DECISIONS.md`, compare against the editor's CanvasKit render instead, and
  update the golden set with a dated note.

Anything that fails its parity gate is reverted or feature-flagged; it does not stay
merged "to fix later".

## 5. Things to keep in mind

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
