# Merging upstream OpenShot into the fork

Why it happened now, how each conflict was decided, and what is still owed. Written while doing the
merge on `merge/upstream-develop` (from `feature/gpu-rendering`), 2026-09-14.

## Why now rather than later

`upstream/master` is frozen at 2021; the live branch is `upstream/develop`, last commit 2026-09-13.
Against our merge base (`7b4e9992`, 2025-06-07) it is **341 commits / 220 files / +35,889 −2,882**.
Our fork is 60 commits ahead on the same base.

Three reasons to pay the cost now:

1. **The conflict set is the GPU-port set.** `Clip.cpp`, `Timeline.cpp`, `FFmpegReader.cpp`,
   `Frame.cpp` carry most of the conflicts, and plan phases 3–4 rewrite exactly those files. After
   that rewrite the files no longer resemble upstream and a merge stops being possible at all.
2. **The golden suite now exists.** 95 scenarios / 292 frames, bit-exact. A 2,300-line merge
   validated by a pixel oracle is a different risk from the same merge validated by reading diffs.
   This is the single biggest change since the plan recommended staying on the fork.
3. **It removes planned work.** Upstream's hardware-decode fix is better than what plan step 1.5
   proposes to write, and `omp-ffmpeg-thread-control` overlaps step 1.2.

Upstream is also circling the same problem: `examples/VulkanBenchmark.cpp` is *"an experimental
Vulkan benchmark comparing a CPU Qt path against an FFmpeg Vulkan path."* Their approach is FFmpeg
Vulkan filters, ours is Skia Graphite — but it means upstream's render path will keep moving.

## The governing rule

> Upstream wins on **plumbing**. The fork wins on **pixels**.

Concretely, the fork wins wherever:

- it delegates to `src/effects/image-processing-lib` (`Podcastle::Effects::*`) — that submodule is
  shared with the web front end through WASM, so its output *is* the parity contract;
- the code is a deliberate fork feature (W3C blend modes, clip shadow/blur/flip, overlay clips,
  freeze frames, prescaling, `DISABLE_CACHING`);
- the golden suite encodes the behaviour.

Upstream wins on decode correctness, new APIs, build files and anything the fork never touched.

## Conflicts and how each was decided

24 conflicted paths, 82 hunks, ~2,322 lines.

### Trivial — both sides added adjacent lines

`CMakeLists.txt`, `tests/CMakeLists.txt`, `src/CMakeLists.txt`, `src/QtImageReader.cpp`,
`src/FFmpegWriter.{h,cpp}`, `src/Enums.h`, `src/Effects.h`, `src/EffectInfo.cpp`. Kept both.

Two judgement calls inside them:

- The fork renamed `ENABLE_OPENCV` to `ENABLE_OPENCV_EFFECTS` and defaults it **OFF**; kept.
  Upstream's new `ENABLE_WAYLAND_CAPTURE` was taken but defaulted **OFF** — screen capture is a
  desktop feature that drags in PipeWire and xdg-desktop-portal, and this is a headless renderer.
- `src/Enums.h` — the fork's `BlendMode` and upstream's `DurationStrategy` are unrelated enums that
  happened to land adjacent. Both kept. Note that upstream's `CompositeType` and the fork's
  `BlendMode` now coexist as **two parallel blending systems**; the fork's is the one the service
  drives. Worth collapsing eventually.

### Cache sizing — `FrameMapper.cpp`, `Timeline.cpp`, `FFmpegReader.cpp`

The fork short-circuits every cache to 1 frame when `Settings::DISABLE_CACHING` (on by default: an
export reads each frame once, so a cache only costs memory). Upstream added a `CACHE_MIN_FRAMES`
floor. Merged as: the fork's short-circuit wins, upstream's floor applies when caching is on.

### Effects — `Bars`, `Brightness`, `Blur`, `ColorMap`, `Mask`

`Bars`, `Brightness` and `Blur` delegate to `image-processing-lib`; the fork's versions were taken
wholesale, **headers included**. Taking only the `.cpp` left upstream's header declaring
`UseCustomMaskBlend`/`ApplyCustomMaskBlend` overrides that the fork's `.cpp` never defines, which
fails at link time with an undefined vtable — a trap worth remembering for the next merge.

`ColorMap`: the fork rewrote it completely (pImpl, Reinhard colour matching, core shared with the
WASM build). It is a strict superset of upstream's LUT-only version. Fork wins.

`Mask`: the fork added rounded corners, start/end keyframes and `invert`; upstream added
`fade_audio_hint`, which upstream's `Timeline.cpp` reads. Fork's implementation, with
`fade_audio_hint` re-added by hand (three constructors, JSON round trip, properties).

### `Clip.cpp` / `Clip.h`

Fork wins in the render path (freeze frames, overlay clips, cache lookup); both sides kept for JSON
properties. Specifics:

- `apply_background` gained upstream's `update_frame_image` parameter but keeps the fork's W3C blend
  logic, so both call styles work.
- Upstream's `resolve_timeline_fps` / `curve_extent_frames` / `trim_extent_frames` were declared but
  never defined on our side; the declarations were dropped.
- Upstream's "paint directly into the timeline-owned background" early return was **removed** — it
  bypasses the fork's `isOverlay` handling.
- `apply_keyframes` and `get_transform` are the fork's. Upstream's `apply_keyframes` drops the
  unconditional `QPainter::Antialiasing`, which changed every rotated/scaled clip edge by one pixel
  (27 golden frames). Upstream also adds corner radius and painter-applied opacity there — features
  worth porting later, deliberately not taken now.
- The fork's `final_cache.Add(frame)` had to be restored; upstream removed the clip frame cache
  write ("currently unused for clip frame caching") while the fork still reads from that cache.

### `Timeline.cpp`

Fork wins for compositing order: `add_layer`, `find_intersecting_clips`, `GetFrame`, `apply_effects`.
`max_concurrent_frames` was removed from `Timeline.h` upstream; its uses became
`OPEN_MP_NUM_PROCESSORS`, which is the value the fork always assigned it. `add_layer` now uses
upstream's stack `TimelineInfoStruct` (the fork's heap version leaked) carrying the fork's
`need_audio` / `need_this_clip_audio` fields.

### `FFmpegReader.cpp` — the hard one

This file is a genuine both-sides rewrite and **cannot be stitched hunk by hunk**; that attempt
produced 205 failing golden frames. The working split is by whole function:

| from upstream | from the fork |
|---|---|
| `GetAVFrame` (the hardware-decode fix), `ApplyFrameOrientation`, `UpdateOrientedVideoInfo`, `ReopenWithoutHardwareDecode`, `HardwareDecodeSuccessful`, `ApplyDurationStrategy`, `PickDurationSeconds` | `Open`, `Close`, `UpdateVideoInfo`, `UpdateAudioInfo`, `ReadStream`, `ProcessVideoPacket`, `ProcessAudioPacket`, `GetFrame`, `Seek`, `CheckSeek`, `CheckWorkingFrames`, all four PTS converters, `UpdatePTSOffset` |

Two lessons, both found the hard way:

- **`ProcessVideoPacket` carries the prescale contract.** Upstream's version sizes the decode
  differently; taking it broke `readers.*_prescale` at 4.5 dB.
- **The seek/PTS functions and `Open`/`UpdateVideoInfo` are one unit with `ReadStream`.** Mixing
  upstream's duration strategy with the fork's `ReadStream` made `GetFrame(15)` return frame 1 —
  `export.roundtrip_x264` decoded at 7.5 dB while the exported file was provably correct.

### `external/godot-cpp` and `examples/Example.cpp`

Fork's submodule pointer (2026-08, newer than upstream's 2025-12, and not built by the library).
Fork's `Example.cpp` — upstream's is a personal scratch file with hardcoded `/home/jonathan/` paths.

## Fixes the merge required beyond conflict resolution

- **C++20 vs C++17.** The fork builds at C++20, upstream at C++17. Upstream's new `AnalogTape`,
  `DenoiseImage` and `FilmGrain` each define a file-local `lerp`, which is ambiguous with
  `std::lerp` under C++20. Renamed to `tape_lerp` / `denoise_lerp` / `grain_lerp`. **Send upstream.**
- **OpenCV split.** Upstream's new tracked-object code in `EffectBase.cpp` is guarded by
  `USE_OPENCV`, but this fork splits `USE_OPENCV` (core, always on) from `USE_OPENCV_EFFECTS` (the
  tracker sources, off). Guard narrowed to `USE_OPENCV_EFFECTS`; the block already had an `#else`.
- **libopenshot-audio.** Upstream requires 1.0.0; this box has 0.6.0. Lowered to 0.6.0 and
  everything compiles and links, so the bump looks conservative rather than a real API dependency.
  **Not proven at runtime** — see below.

## The one deliberate pixel change

`compositing.layer_order` was re-baselined, alone. The reason is a real bug either way:

- The fork's `CompareClips` used `Position() <= Position()`, which returns true for equal elements
  and is therefore not a strict weak ordering — undefined behaviour for `std::list::sort`.
- Upstream fixed the ordering but tie-breaks on **pointer address**. That is stable within a run and
  random across runs: the same project renders differently each time. The golden suite caught it
  failing 4 runs out of 5.

Neither is acceptable for a renderer that must be reproducible. `CompareClips` now reports "no
ordering" for equal layer and position and relies on `std::list::sort` being stable, so clips keep
insertion order — deterministic, and a valid strict weak ordering.

The visible consequence: when a clip shares a layer *and* position with another, the one added later
now draws on top. In that scenario the background clip (`Layer(1)`) collides with a media clip at
`layerOf(0, 1) == 1`, so a clip that used to be hidden behind the background is now visible.

**This is arguably a recipe bug too** — a background at the same layer as content. If the service
can produce that collision, it is worth fixing there rather than relying on sort order.

## Still owed

1. **Re-run the performance comparison on a quiet machine.** Measurements taken at the end of this
   session are not trustworthy: the CPU was at 400 MHz and `single_video` 1080p render varied
   74–87 fps across three consecutive runs. A full `openshot-bench` run against
   `tests/bench/results/baseline-cpu.json` is required before this merge is called done.
2. **Prove libopenshot-audio 0.6.0 at runtime**, or install 1.0.0 and restore upstream's
   requirement. It compiles and links today; audio was not exercised beyond the golden suite's
   silent-audio smoke test.
3. **Decide on `compositing.layer_order`** — accept the new behaviour or fix the layer collision.
4. **Port upstream's `apply_keyframes` extras** (corner radius, painter-applied opacity) on top of
   the fork's antialiasing behaviour, if wanted.
5. **Collapse `BlendMode` and `CompositeType`** into one system.
6. **Send the `lerp` rename upstream** so the next merge does not repeat it.
7. **The new upstream effects are built but untested here**: AnalogTape, AudioVisualization,
   BeatSync, ColorGrade, DenoiseImage, Displace, FilmGrain, Glow, Timer, Shadow, plus the screen
   capture readers and audio recorder. The service does not use them; the golden suite does not
   cover them.

## What upstream brings that we now have

- The hardware-decode fix (`next_frame->format = pCodecCtx->sw_pix_fmt`) plus the hardening around
  it: `note_hw_decode_failure`, software fallback for decoders that return readable frames, and
  width/height repair. This is **plan step 1.5's decode half, done better than the plan proposed**.
- FFmpeg 8 support (mostly `FFmpegUtilities.h`).
- Qt6 support, opt-in via `USE_QT6=AUTO` with a Qt5 fallback. Pin `USE_QT6=OFF` until deliberately
  moved, since AUTO will pick Qt6 if it is installed.
- `omp-ffmpeg-thread-control` — Settings-driven FFmpeg/OpenMP thread limits, overlapping plan
  step 1.2.
- Roughly fifteen crash and correctness fixes: `fix-recursive-paint`, `safe-clip-memory`,
  `caching-protections`, `pts-offset-fix`, `sparse-vfr-support`, `empty-keyframes`, `fps-fix`,
  `black-frames`, `fix-audio-crash`, `fix-audio-mapping`, `protect-tracker-crash`, and three
  `sentry-*` batches from upstream's production telemetry.
