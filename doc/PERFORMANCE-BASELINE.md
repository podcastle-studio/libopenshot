# Performance baseline and history

Measurements of the render pipeline taken with `openshot-bench` (`tests/bench`), to be repeated after
every optimisation phase of `doc/GPU-RENDER-PLAN.md` so the gain of each step is visible against the
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

(results inserted below once the run completes)
