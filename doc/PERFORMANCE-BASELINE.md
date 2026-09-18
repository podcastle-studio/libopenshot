# Performance baseline and history

Measurements of the render pipeline taken with `openshot-bench` (`tests/bench`), to be repeated after
every optimisation phase of `doc/gpu-migration/GPU-RENDER-PLAN.md` so the gain of each step is visible against the
same scenarios, resolutions and machine.

## How to reproduce

```bash
tests/bench/media/generate.sh                                   # once: large bench media (git-ignored)
cmake --build cmake-build-release --target openshot-bench
cmake-build-release/tests/bench/openshot-bench --label <name> --modes render,x264,nvenc \
    --json tests/bench/results/<name>.json --md /tmp/<name>.md   # ~30 min for the full matrix
cmake-build-release/tests/bench/openshot-bench compare tests/bench/results/baseline-cpu.json tests/bench/results/<name>.json
```

`--quick` (1080p only, 60 frames) takes about a minute and is enough to see whether a change moved
the needle; the full matrix is for the phase-end record. Commit the JSON under `tests/bench/results/`
and paste the Markdown into a new section below; keep the old sections.

What is measured per case (one scenario at one resolution in one mode, in its own process):

| Metric | Meaning |
|---|---|
| fps | frames per second over the whole case (150 frames = 5 s of output at 30 fps) |
| p50 / p95 ms | per-frame `Timeline::GetFrame` latency percentiles (render mode only) |
| cores | average CPU cores busy = (user + system CPU time) / wall time |
| RSS | peak resident memory of the process (GB) |

Modes: **render** = `Timeline::GetFrame` only, the pure compositing cost; **x264** = full export as the
service configures it (libx264, crf 18, preset medium, silent audio, pipeline mode); **nvenc** = same
export with `h264_nvenc` preset p4 (today: CPU frames uploaded to the GPU encoder).

Scenarios (`openshot-bench --list`):

| scenario | what it exercises |
|---|---|
| `single_video` | one 1080p video scaled to the canvas over a background image |
| `grid_2x2`, `grid_3x3` | 4 / 9 simultaneous 1080p decodes composited into a grid |
| `podcast_pip` | main video with light + colour filters + LUT, PiP with rounded crop + shadow, logo, lower-third text |
| `subtitles_words` | video + animated one-word-container subtitles for the whole duration |
| `text_static_4` | four static styled text clips (gradient + stroke + shadow, box, curved, plain) |
| `text_animated_glow_3` | three animated text clips with glow, 3D tilt and style keyframes (per-frame Skia) |
| `transitions_chain` | three videos, two overlapping transitions (zoom + blur + alpha; circle mask + additive overlay) |
| `blend_stack_5` | base video plus four layers with multiply / screen / overlay / soft-light |
| `heavy_effects` | blur, enhancement, colour + light filters, LUT, rounded crop, clip shadow and clip blur on one video |
| `chroma_key_green` | 1080p green-screen clip keyed (YCbCr) over a second video |
| `source_4k` | one 3840x2160 source scaled to the canvas (decode + pre-scale bound) |
| `everything` | two videos, chroma-keyed clip with LUT, animated glow text, subtitles, PiP with shadow |

Threads are the library defaults the service runs with (`FF_THREADS = OMP_THREADS = 16`); pass
`--threads N` to model a CPU-limited pod.

---

## 2026-09-10 · `baseline-cpu` · current state, before any optimisation

**Summary.** Real time (30 fps) is met at 1080p only by the simple scenarios: a single video, a 2x2
grid, static text and subtitles. Everything a real project contains at once (`everything`,
`podcast_pip`, `text_animated_glow_3`) is far below real time at 1080p and collapses at 4K. The
encoder is not the story: at 1080p `render` (no encoder at all) is only 20-40 % faster than the full
x264 export, and nvenc is no faster than x264 on the heavy scenarios because both wait on the same
CPU compositor.

**Where the time goes at 1080p** (render-only fps, so purely compositing):

| bottleneck | evidence |
|---|---|
| animated text (Skia CPU raster, per frame) | `text_animated_glow_3` 1.4 fps, p95 952 ms/frame, and only 1.0 core busy - single-threaded |
| several composited layers | `grid_3x3` 23 fps vs `single_video` 117 fps; `blend_stack_5` 19 fps |
| per-pixel effects | `heavy_effects` 11.5 fps, `chroma_key_green` 12.9 fps |
| everything together | `everything` 1.8 fps, p95 602 ms |

**Scaling with resolution.** From 1080p to 4K, fps drops 4-6x on scenarios dominated by the
compositor and only 2.4x on `source_4k` (decode-bound, where the source resolution does not change).
Memory grows with the square of the canvas: `text_animated_glow_3` peaks at 9.6 GB at 4K and
`everything` at 7.0 GB, both driven by offscreen Skia surfaces plus the per-clip project-size RGBA
buffers.

**CPU usage is low where it matters.** The heavy scenarios use 1.1-1.2 cores because
`Timeline::GetFrame` is serialised behind one mutex and the text engine is single-threaded; the
20-thread machine is idle while a 4K glow-text frame takes 3.5 seconds. Only x264 itself (up to 8
cores on `heavy_effects`) and a few OpenMP effect loops use the machine.

**Caveats.** Laptop with frequency scaling, no pinned governor, single run per case; treat
differences below ~5 % as noise. Scenarios ran in a fixed order over 89 minutes, so late scenarios
saw a warmer CPU. nvenc here still uploads CPU-rendered frames, so it measures the current
(non-GPU-resident) path.

### Render only (Timeline::GetFrame, no encoder)

fps, with the p95 per-frame time in ms for render mode. CPU = average cores busy; RSS = peak resident memory of the process.

| scenario | 540p | 720p | 1080p | 1440p | 2160p |
|---|---:|---:|---:|---:|---:|
| `single_video` | **232.3** (p95 6.3) | **159.4** (p95 8.1) | **116.7** (p95 10.9) | **47.7** (p95 22.6) | **23.9** (p95 47.6) |
| `grid_2x2` | **70.6** (p95 21.3) | **48.6** (p95 30.2) | **40.8** (p95 28.8) | **20.6** (p95 58.7) | **12.8** (p95 87.0) |
| `grid_3x3` | **39.1** (p95 41.8) | **24.7** (p95 67.1) | **23.3** (p95 53.8) | **14.7** (p95 87.8) | **7.5** (p95 152.9) |
| `podcast_pip` | **56.1** (p95 22.6) | **39.1** (p95 30.8) | **22.9** (p95 48.4) | **17.3** (p95 65.3) | **10.3** (p95 109.0) |
| `subtitles_words` | **214.4** (p95 6.3) | **146.3** (p95 9.0) | **56.3** (p95 25.0) | **34.6** (p95 34.1) | **23.7** (p95 49.9) |
| `text_static_4` | **172.8** (p95 7.5) | **110.5** (p95 11.1) | **59.2** (p95 20.0) | **27.4** (p95 39.7) | **11.8** (p95 100.1) |
| `text_animated_glow_3` | **5.1** (p95 262.3) | **2.9** (p95 486.4) | **1.4** (p95 952.2) | **0.8** (p95 1749.1) | **0.4** (p95 3504.4) |
| `transitions_chain` | **52.8** (p95 85.3) | **41.3** (p95 97.9) | **27.4** (p95 112.1) | **21.2** (p95 121.7) | **13.3** (p95 159.5) |
| `blend_stack_5` | **26.9** (p95 59.8) | **27.7** (p95 52.0) | **19.1** (p95 61.2) | **10.8** (p95 102.4) | **3.9** (p95 442.5) |
| `heavy_effects` | **16.9** (p95 73.5) | **23.3** (p95 48.9) | **11.5** (p95 97.1) | **5.8** (p95 251.9) | **6.5** (p95 170.9) |
| `chroma_key_green` | **27.6** (p95 47.5) | **18.4** (p95 70.8) | **12.9** (p95 81.2) | **10.2** (p95 101.4) | **7.3** (p95 142.0) |
| `source_4k` | **110.9** (p95 13.5) | **93.5** (p95 14.8) | **60.9** (p95 22.3) | **38.0** (p95 35.7) | **25.6** (p95 48.7) |
| `everything` | **7.3** (p95 150.6) | **3.9** (p95 272.9) | **1.8** (p95 601.9) | **1.2** (p95 950.2) | **0.7** (p95 1536.5) |

CPU cores busy / peak RSS (GB):

| scenario | 540p | 720p | 1080p | 1440p | 2160p |
|---|---:|---:|---:|---:|---:|
| `single_video` | 2.3 / 0.49 | 1.9 / 0.49 | 1.6 / 0.51 | 1.3 / 0.53 | 1.1 / 0.55 |
| `grid_2x2` | 2.0 / 0.92 | 1.7 / 0.92 | 1.5 / 0.96 | 1.3 / 1.00 | 1.2 / 1.00 |
| `grid_3x3` | 2.1 / 1.66 | 1.7 / 1.64 | 1.6 / 1.70 | 1.4 / 1.75 | 1.2 / 1.76 |
| `podcast_pip` | 3.7 / 0.66 | 4.0 / 0.64 | 4.5 / 0.68 | 3.6 / 0.72 | 2.5 / 0.71 |
| `subtitles_words` | 2.1 / 0.49 | 1.8 / 0.49 | 1.4 / 0.52 | 1.2 / 0.55 | 1.1 / 0.57 |
| `text_static_4` | 1.6 / 0.50 | 1.4 / 0.50 | 1.2 / 0.53 | 1.1 / 0.59 | 1.0 / 0.62 |
| `text_animated_glow_3` | 1.0 / 0.56 | 1.0 / 0.92 | 1.0 / 1.95 | 1.0 / 3.40 | 1.0 / 7.43 |
| `transitions_chain` | 2.4 / 1.09 | 2.4 / 1.10 | 2.2 / 1.11 | 1.9 / 1.06 | 1.6 / 1.03 |
| `blend_stack_5` | 3.0 / 1.07 | 3.4 / 1.06 | 4.2 / 1.10 | 4.1 / 1.17 | 4.0 / 1.14 |
| `heavy_effects` | 6.3 / 0.57 | 7.7 / 0.58 | 8.2 / 0.63 | 6.1 / 0.61 | 5.0 / 0.60 |
| `chroma_key_green` | 1.2 / 0.64 | 1.2 / 0.62 | 1.1 / 0.67 | 1.1 / 0.68 | 1.0 / 0.68 |
| `source_4k` | 3.0 / 0.90 | 2.6 / 0.90 | 2.1 / 1.00 | 1.7 / 1.13 | 1.5 / 1.43 |
| `everything` | 1.2 / 1.03 | 1.1 / 1.16 | 1.1 / 1.78 | 1.0 / 2.75 | 1.0 / 4.91 |

### Export with libx264 (service configuration)

fps, with the p95 per-frame time in ms for render mode. CPU = average cores busy; RSS = peak resident memory of the process.

| scenario | 540p | 720p | 1080p | 1440p | 2160p |
|---|---:|---:|---:|---:|---:|
| `single_video` | **178.1** | **118.9** | **81.0** | **33.5** | **17.7** |
| `grid_2x2` | **66.4** | **45.4** | **32.2** | **19.6** | **8.3** |
| `grid_3x3` | **35.6** | **23.2** | **20.5** | **13.5** | **6.9** |
| `podcast_pip` | **53.7** | **32.9** | **19.9** | **15.2** | **9.2** |
| `subtitles_words` | **164.6** | **103.9** | **31.2** | **24.7** | **18.4** |
| `text_static_4` | **131.4** | **82.2** | **48.4** | **22.5** | **10.8** |
| `text_animated_glow_3` | **5.1** | **2.9** | **1.4** | **0.8** | **0.3** |
| `transitions_chain` | **38.3** | **36.5** | **23.5** | **18.3** | **10.6** |
| `blend_stack_5` | **17.6** | **24.5** | **16.2** | **9.7** | **4.8** |
| `heavy_effects` | **16.5** | **21.9** | **10.7** | **9.0** | **5.9** |
| `chroma_key_green` | **25.3** | **23.0** | **11.9** | **9.2** | **6.8** |
| `source_4k` | **97.1** | **72.2** | **47.5** | **30.1** | **19.3** |
| `everything` | **7.3** | **3.9** | **1.8** | **1.1** | **0.7** |

CPU cores busy / peak RSS (GB):

| scenario | 540p | 720p | 1080p | 1440p | 2160p |
|---|---:|---:|---:|---:|---:|
| `single_video` | 4.5 / 0.65 | 4.4 / 0.77 | 5.6 / 1.12 | 3.8 / 1.58 | 4.0 / 2.72 |
| `grid_2x2` | 2.7 / 1.08 | 2.5 / 1.21 | 2.8 / 1.59 | 2.6 / 2.04 | 2.6 / 3.17 |
| `grid_3x3` | 2.5 / 1.81 | 2.2 / 1.93 | 2.5 / 2.34 | 2.3 / 2.77 | 2.2 / 3.89 |
| `podcast_pip` | 4.2 / 0.83 | 4.2 / 0.97 | 4.6 / 1.31 | 4.0 / 1.78 | 3.3 / 2.89 |
| `subtitles_words` | 4.2 / 0.67 | 4.0 / 0.77 | 4.5 / 1.13 | 3.4 / 1.60 | 3.8 / 2.76 |
| `text_static_4` | 3.1 / 0.67 | 3.0 / 0.78 | 3.1 / 1.14 | 2.6 / 1.65 | 2.5 / 2.83 |
| `text_animated_glow_3` | 1.1 / 0.73 | 1.1 / 1.28 | 1.1 / 2.60 | 1.1 / 4.57 | 1.1 / 9.57 |
| `transitions_chain` | 3.1 / 1.29 | 3.3 / 1.38 | 3.5 / 1.72 | 3.6 / 2.15 | 3.7 / 3.23 |
| `blend_stack_5` | 3.5 / 1.24 | 3.8 / 1.35 | 4.8 / 1.72 | 4.8 / 2.20 | 5.1 / 3.34 |
| `heavy_effects` | 6.4 / 0.74 | 7.6 / 0.88 | 8.1 / 1.26 | 7.0 / 1.68 | 5.2 / 2.80 |
| `chroma_key_green` | 1.7 / 0.82 | 1.6 / 0.90 | 1.5 / 1.26 | 1.7 / 1.77 | 1.9 / 2.86 |
| `source_4k` | 4.2 / 1.05 | 4.0 / 1.17 | 4.0 / 1.54 | 3.9 / 2.00 | 4.7 / 3.29 |
| `everything` | 1.3 / 1.20 | 1.2 / 1.47 | 1.2 / 2.40 | 1.1 / 3.76 | 1.1 / 6.97 |

### Export with h264_nvenc

fps, with the p95 per-frame time in ms for render mode. CPU = average cores busy; RSS = peak resident memory of the process.

| scenario | 540p | 720p | 1080p | 1440p | 2160p |
|---|---:|---:|---:|---:|---:|
| `single_video` | **117.7** | **98.9** | **75.0** | **37.4** | **19.3** |
| `grid_2x2` | **58.5** | **42.6** | **31.4** | **19.3** | **10.9** |
| `grid_3x3` | **34.8** | **23.0** | **20.7** | **13.3** | **6.9** |
| `podcast_pip` | **48.9** | **33.2** | **20.6** | **15.4** | **9.2** |
| `subtitles_words` | **118.9** | **99.1** | **34.7** | **37.4** | **19.3** |
| `text_static_4` | **119.6** | **74.2** | **46.0** | **21.4** | **11.1** |
| `text_animated_glow_3` | **5.1** | **2.9** | **1.3** | **0.7** | **0.3** |
| `transitions_chain` | **39.3** | **35.1** | **24.0** | **18.7** | **11.7** |
| `blend_stack_5` | **29.3** | **24.9** | **17.1** | **10.2** | **4.5** |
| `heavy_effects` | **23.7** | **20.6** | **10.5** | **8.9** | **5.0** |
| `chroma_key_green` | **25.7** | **22.2** | **12.0** | **9.5** | **6.8** |
| `source_4k` | **72.7** | **66.1** | **46.0** | **29.9** | **20.6** |
| `everything` | **7.2** | **3.8** | **1.8** | **1.1** | **0.7** |

CPU cores busy / peak RSS (GB):

| scenario | 540p | 720p | 1080p | 1440p | 2160p |
|---|---:|---:|---:|---:|---:|
| `single_video` | 1.6 / 0.66 | 1.7 / 0.69 | 1.7 / 0.83 | 1.4 / 1.02 | 1.4 / 1.52 |
| `grid_2x2` | 1.8 / 1.08 | 1.6 / 1.13 | 1.6 / 1.29 | 1.4 / 1.49 | 1.3 / 1.96 |
| `grid_3x3` | 2.0 / 1.81 | 1.7 / 1.85 | 1.6 / 2.04 | 1.4 / 2.25 | 1.3 / 2.72 |
| `podcast_pip` | 3.4 / 0.83 | 3.5 / 0.86 | 4.2 / 1.02 | 3.4 / 1.21 | 2.5 / 1.68 |
| `subtitles_words` | 1.6 / 0.66 | 1.7 / 0.70 | 1.5 / 0.87 | 1.4 / 1.06 | 1.4 / 1.53 |
| `text_static_4` | 1.5 / 0.67 | 1.4 / 0.71 | 1.3 / 0.85 | 1.2 / 1.07 | 1.2 / 1.60 |
| `text_animated_glow_3` | 1.0 / 0.73 | 1.0 / 1.13 | 1.0 / 2.28 | 1.0 / 3.88 | 1.0 / 8.39 |
| `transitions_chain` | 2.2 / 1.26 | 2.2 / 1.29 | 2.1 / 1.44 | 1.9 / 1.54 | 1.7 / 1.99 |
| `blend_stack_5` | 3.0 / 1.23 | 3.2 / 1.28 | 3.9 / 1.44 | 4.0 / 1.66 | 4.1 / 2.10 |
| `heavy_effects` | 6.1 / 0.74 | 7.0 / 0.79 | 7.8 / 0.95 | 6.7 / 1.10 | 4.6 / 1.56 |
| `chroma_key_green` | 1.3 / 0.80 | 1.2 / 0.85 | 1.1 / 0.99 | 1.1 / 1.19 | 1.1 / 1.65 |
| `source_4k` | 2.3 / 1.06 | 2.3 / 1.10 | 2.0 / 1.25 | 1.8 / 1.43 | 1.7 / 2.02 |
| `everything` | 1.2 / 1.20 | 1.1 / 1.36 | 1.1 / 2.11 | 1.1 / 3.22 | 1.0 / 5.91 |
### Concurrency: what N simultaneous exports cost

Measured with `--parallel N` (N identical processes started together; fps is per process, agg is all
processes together; cores and RSS are summed across processes). 1080p, same machine, 20 hardware threads.

| scenario | mode | N=1 fps | N=2 fps/proc (agg) | N=4 fps/proc (agg) | N=4 cores | N=4 RSS |
|---|---|---:|---:|---:|---:|---:|
| `single_video` | render | 100.4 | 83.6 (165) | 60.0 (236) | 5.5 | 2.0 GB |
| `single_video` | x264 | 69.9 | 48.9 (96) | 32.1 (127) | 13.8 | 4.5 GB |
| `single_video` | nvenc | 49.7 | 41.8 (83) | 24.2 (95) | 3.8 | 3.3 GB |
| `podcast_pip` | render | 20.1 | 15.8 (32) | 11.4 (45) | 10.4 | 2.7 GB |
| `podcast_pip` | x264 | 18.2 | 14.2 (28) | 9.8 (39) | 11.4 | 5.2 GB |
| `everything` | x264 | 1.6 | 1.5 (3.1) | – | – | – |

Reading this:

- **Aggregate throughput keeps rising but sub-linearly.** Four `single_video` x264 exports deliver
  1.8x the frames of one, not 4x; each individual export runs at 46 % of its solo speed. For
  `podcast_pip` the aggregate gain from N=1 to N=4 is 2.1x.
- **Per-export latency degrades immediately.** Even N=2 costs 17-30 % of per-process fps. If a
  customer-visible deadline matters, fewer concurrent exports with more threads each is better;
  if total throughput matters, more processes win.
- **`everything` barely contends** (1.6 to 1.5 fps at N=2) because it is single-threaded and
  latency-bound, not resource-bound. Those exports scale almost linearly in aggregate, and they are
  exactly the ones the GPU work will speed up.
- **Memory is the hard limit.** Four 1080p x264 exports need 4.5 GB, four 4K `everything` exports
  would need ~28 GB. Size pods by the heaviest payload, not the average.
- **nvenc uses a quarter of the CPU** of x264 at N=4 (3.8 vs 13.8 cores) for similar aggregate fps,
  so it is the better choice under contention even before the frames stay GPU-resident.


## 2026-09-15 · end of phase 2 · CPU vs GPU text rendering, on the discrete card

The first measurement of the finished phase-2 work on the **NVIDIA RTX A2000 8GB** rather than the
Intel Iris Xe iGPU every earlier phase-2 number was taken on. Same machine, same binary, interleaved
runs, `render` mode (no encoder). **"CPU" here is the GPU-capable build with `OPENSHOT_GPU=off`**,
not the CPU-Skia build — so the only variable is the switch. 150 frames is the window the text
scenarios are actually animated over, and the one the baseline above uses.

Raw results: `tests/bench/results/phase2-gpu-vs-cpu/`. (Dated run files at the top level of
`results/` are gitignored; this set is committed because the table above is read against it.)

| scenario | res | frames | CPU (`off`) | GPU (`vulkan`) | speed-up |
|---|---|---:|---:|---:|---:|
| `text_animated_glow_3` | 1080p | 150 | 4.3 / 4.2 fps | **51.7 / 52.9 fps** | **12.3×** |
| `text_animated_glow_3` | 2160p | 150 | 1.3 fps | **11.4 fps** | **9.1×** |
| `everything` | 1080p | 150 | 5.4 fps | **8.8 fps** | 1.6× |
| `subtitles_words` | 1080p | 150 | 109.2 fps | 112.9 fps | 1.03× |
| `text_static_4` | 1080p | 150 | 64.5 / 61.8 fps | 61.1 / 60.8 fps | 0.96× |
| `text_static_4` | 2160p | 150 | 13.1 fps | 12.5 fps | 0.96× |
| `text_static_4` | 1080p | 600 | 148.4 / 142.5 / 146.3 fps | 139.1 / 139.4 / 138.6 fps | 0.95× |

Per frame, the scenario the GPU exists for: **234.6 ms → 19.3 ms** at 1080p, **795.2 ms → 87.5 ms**
at 2160p. RSS rises modestly with the GPU on (0.41 → 0.56 GB at 1080p, 1.04 → 1.20 GB for
`everything`).

Reading this:

- **Animated glow text is the whole story**, as the profile said it would be: 12.3× at 1080p and
  9.1× at 4K. `everything` gains 1.6× because only part of its frame is text.
- **The A2000 is roughly twice the iGPU** on this work — 52 fps against the 26.7 recorded on
  2026-09-14 — so the earlier phase-2 numbers understate the production case.
- **Static text is ~5 % *slower* on the GPU**, consistently: three interleaved 600-frame pairs gave
  145.7 fps mean off against 139.0 on Vulkan, with non-overlapping ranges. This is not noise and it
  is not a regression to fix — static text is served from the resting-frame cache, so there is
  almost no per-frame Skia work for the GPU to take over, and what is left is the fixed cost of
  going through a GPU surface. It is a reason `OPENSHOT_GPU` is a per-deployment switch rather than
  a default.
- **Subtitles are unchanged**, by design — the Timeline still hands them a raster canvas (step 2.7).
- **lavapipe is 3× faster than raster for glow text**, not slower: 12.96 fps against 4.39 at 1080p.
  Skia's raster backend runs the glow SkSL through its CPU interpreter, while lavapipe JIT-compiles
  it with LLVM across all cores. The practical consequence is that a GPU deployment that loses the
  `graphics` driver capability — and so silently gets llvmpipe instead of the NVIDIA ICD — degrades
  to something still well ahead of raster. It uses many cores to do it, so it is not a good choice
  under parallel exports.

## 2026-09-18 — W17's `subtitles_words` gate, on mains power

W17 (subtitles and text compositing on the Timeline's own GPU canvas) landed on 2026-09-17 with its
correctness gate met but its fps number owed: the measurement window was on battery, which caps this
laptop to roughly a third of its AC speed. Re-measured on AC, at HEAD, GPU Skia, 1080p `render`, the
standard 150-frame window. Gate: `subtitles_words` ≥ 120 fps.

| arm | runs (fps, 150 frames) | median |
|---|---|---:|
| `OPENSHOT_GPU=vulkan` | 98.2, 102.8, 112.5, 114.7, 115.1, 117.5, 120.1, 120.8, 120.9, 120.9, 122.5, 123.2 | **118.8** |
| `OPENSHOT_GPU=off` | 77.9, 93.2, 96.5, 97.6, 98.6, 98.8, 99.5, 99.7 | 98.0 |

Six of the Vulkan runs were interleaved pair-wise against the `off` arm, so drift is controlled for.
**The gate is cleared in the better half of the runs and misses by ~1 % at the median.** The spread
is the machine: the host was running a browser and two IDEs (load average 1.2–2.7), and the `off`
arm — where nothing changed — is just as noisy. A clean single number wants an idle machine.

**The 150-frame window charges a fixed warm-up, and it is not GPU-specific.** Repeating at 300
frames gives 147.3–164.4 fps on Vulkan and 128.9–131.1 with the GPU off. Solving the two windows for
a fixed cost plus a steady rate:

| arm | fixed cost | steady-state rate |
|---|---:|---:|
| `vulkan` | ~0.60 s | **~227 fps** |
| `off` | ~0.77 s | ~195 fps |

That is first-frame decode, font loading and cache fill, present on both paths. A real export runs
thousands of frames and sees the steady-state rate, so **the 150-frame fps understates every
scenario in this document** — by about 90 % for `subtitles_words`. The steady-state GPU win is 16 %,
against the 22 % the 150-frame window suggests.

Worth remembering when reading any gate in `GPU-WORKLIST.md`: the numbers are a 150-frame window on
a laptop, and they are conservative for throughput and noisy on a loaded host.
