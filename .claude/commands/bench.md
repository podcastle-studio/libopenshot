---
description: Measure performance and compare against the recorded baseline
argument-hint: "[quick | full | scenario name]"
---

Measure the render pipeline and compare against `tests/bench/results/baseline-cpu.json`.

- No argument or `quick`: `cmake-build-release/tests/bench/openshot-bench --quick --label wip
  --json /tmp/bench-wip.json` (1080p, 60 frames, about a minute).
- `full`: the whole matrix, `--label <phase> --frames 150 --modes render,x264,nvenc --json
  tests/bench/results/<phase>.json` (about 90 minutes — say so before starting, and run it in the
  background).
- A scenario name: `--scenario <name> --res 1080p --modes render,x264,nvenc`.

Then run `openshot-bench compare tests/bench/results/baseline-cpu.json <new>.json` and report:

1. The three biggest speedups and any regression worse than 5 %.
2. Whether the numeric gate for the current phase in `doc/gpu-migration/GPU-RENDER-PLAN.md` section 3.1 is met.
3. Peak RSS, since memory is a first-class constraint in this work.

For a full run at a phase gate, also append the Markdown table to `doc/PERFORMANCE-BASELINE.md`,
commit the JSON under `tests/bench/results/`, and update `doc/gpu-migration/STATUS.md`.
