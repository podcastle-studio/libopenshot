# Status

Last updated: 2026-09-10 · branch `feature/gpu-rendering` (from fork `develop` 1d82adc9)

## Where we are

Phase 0 of `doc/GPU-RENDER-PLAN.md` is mostly done:

- Baseline measured (plan section 0.3): codecs are not the bottleneck; Qt raster compositing and
  single-threaded swscale are. GPU encode alone gives ~1.3x; hardware decode crashes in the fork.
- Plan written and reviewed (`doc/GPU-RENDER-PLAN.md`, draft 2).
- Golden-frame regression suite built and baselined: `tests/golden`, 95 scenarios, 292 frames,
  green and bit-stable across thread counts; a deliberate 1 px composite shift fails 282 frames.
  Run with `tools/golden.sh check`.
- Per-process resource numbers measured for parallel-export sizing (plan section 4): a 1080p
  libx264 export uses ~8 cores / 0.75 GB; with nvenc 1.6 cores / 0.55 GB / 0.25 GB VRAM; 4K sources
  ~1.5 GB, 4K output ~2.2 GB. One NVENC engine ≈ 165 fps of 1080p.
- Performance benchmark built (`tests/bench`, `openshot-bench`): 13 scenarios × 5 resolutions ×
  render/x264/nvenc, per-case fps, p95 latency, cores, RSS; `compare` prints deltas between runs.
  Baseline of the current CPU pipeline recorded in `doc/PERFORMANCE-BASELINE.md` and
  `tests/bench/results/baseline-cpu.json`.
- All of the above is committed on `feature/gpu-rendering`.

## PAUSED MID-RUN (laptop suspend, 2026-09-10) — resume here

The full CPU performance baseline (`openshot-bench --label baseline-cpu --frames 150 --modes render,x264,nvenc`)
was running in the background: **~103 of 195 cases done** (all scenarios up to `text_animated_glow_3`;
that one is very slow: 5 fps at 540p, 1.4 fps at 1080p, 0.8 fps at 1440p). Remaining: rest of
`text_animated_glow_3`, `transitions_chain`, `blend_stack_5`, `heavy_effects`, `chroma_key_green`,
`source_4k`, `everything` (× 5 resolutions × 3 modes).

The running binary writes its JSON only at the very end, so if the process died with the suspend the
finished cases exist only in the console log, copied to `tests/bench/results/baseline-cpu.partial.log`
(one line per case: fps, cores, RSS, p95).

Source changes made while the run was going, **not yet built or tested**: `tests/bench/main.cpp` gained
`--parallel N` (N identical processes per case, aggregate fps) and incremental JSON writing +
`--resume <json>`. To continue:

1. `ninja -C cmake-build-release openshot-bench` (fix any compile errors in the new code).
2. Re-run the full matrix with the rebuilt binary (results now flush after every case, so a second
   interruption costs one case): `cmake-build-release/tests/bench/openshot-bench --label baseline-cpu
   --frames 150 --modes render,x264,nvenc --json tests/bench/results/baseline-cpu.json --md /tmp/baseline-cpu.md
   --out /tmp/openshot-bench > /tmp/bench-run.log 2>&1 &` (about 1.5 h; add `--resume tests/bench/results/baseline-cpu.json`
   after any interruption). If the old process finished before the suspend, its JSON is already at that path — check first.
3. Parallel measurement for the sizing table: `--parallel 2` and `--parallel 4` at `--res 1080p` for
   `single_video,podcast_pip,text_animated_glow_3,everything` in all three modes (`--json
   tests/bench/results/parallel-{2,4}.json`).
4. Paste both Markdown tables into `doc/PERFORMANCE-BASELINE.md` (section "2026-09-10 · baseline-cpu"),
   add a short narrative (bottlenecks per resolution, the animated-glow-text outlier, throttling caveat),
   commit JSON + doc, update this file, delete this PAUSED section.

## Open decisions (record in `doc/GPU-DECISIONS.md` when taken)

- Base branch: stay on the fork (recommended) or merge upstream 1.0.0 first (plan step 0.1).
- Timeline canvas precision for the GPU compositor: RGBA8 or RGBA16F (RGBA16F recommended).
- Reference for LUT rounding: native `ColorMap.cpp` or the WASM `LutApply.cpp` path.

## Next step

**Plan step 0.2 / 0.3, then Phase 1 (R1, CPU quick wins).**

1. Collect 6 production payloads + media into a corpus and add a service-level end-to-end check
   (the golden suite covers the library; the corpus covers the service's JSON → timeline code).
2. Phase 1 steps in order, each validated with `tools/golden.sh check` and `openshot-bench --quick`,
   with a full `openshot-bench` run + `compare` against `baseline-cpu.json` at the end of the phase:
   1.1 service: one `WriteFrame` call instead of 8-frame chunks;
   1.2 service: thread budgets from the cgroup quota / process count;
   1.3 writer: RGBA straight into nvenc, no swscale/memcpy, BT.709 tags;
   1.4 writer: sane nvenc rate-control options;
   1.5 reader: drop memset + `av_image_copy`, threaded swscale, fix the hardware-decode crash;
   1.6 `Frame::GetImageCV` memoisation.

## Known oddities worth a look

- `effects.stack_crop_chroma_light_lut` golden shows harsh white blotches (ChromaKey + Light + LUT
  stacked). Baseline as-is; may be a real rendering quirk.
- Export round trip live-vs-decoded is ~28 dB on the noisy test pattern with no colour bias:
  x264 loss, not a matrix bug.

## Log

- 2026-09-10 — analysis, plan, golden suite; first commit on `feature/gpu-rendering`.
- 2026-09-10 — CLAUDE.md + STATUS.md; plan section 4 (sizing for N parallel exports).
- 2026-09-10 — openshot-bench committed; CPU baseline run started (paused mid-run for laptop suspend, see above).
