# GPU migration — working documents

**This folder is temporary.** It holds the plan, the decisions and the running status for moving
libopenshot's render path onto the GPU and removing Qt. When that work has landed and the last
phase is released, delete the whole folder:

```bash
git rm -r doc/gpu-migration
```

Nothing here is reference documentation for the library. Anything worth keeping afterwards must be
moved into `doc/` (or into `CLAUDE.md`) *before* the folder is deleted — see "What has to survive"
below.

## What is in here

| file | what it is |
|---|---|
| `STATUS.md` | where the work stopped and what the next step is. Read first, update at the end of every session that changed code, plans or decisions. |
| `GPU-RENDER-PLAN.md` | the multi-phase plan. Sections 0–2 are background (measurements, GPU primer, Qt inventory), section 3 is the step list with a numeric gate per step, section 4 sizes a host for N parallel exports. |
| `GPU-DECISIONS.md` | one line per decision already taken, so a later session does not re-litigate it, plus the questions still open. |

## What stays outside this folder

These are permanent and must **not** be deleted with the migration:

- `doc/PERFORMANCE-BASELINE.md` — benchmark history. Appended at the end of every optimisation
  phase; the point of it is the long-term trend, which outlives the migration.
- `tests/golden/` and `tests/bench/` — the regression suite and the benchmark themselves.
- `doc/HW-ACCEL.md`, `doc/INSTALL-*.md` — upstream documentation.

## What has to survive the deletion

Before removing this folder, fold anything still true into the permanent docs:

- The Skia build-script policy (CPU script never modified, GPU script beside it) belongs in
  `CLAUDE.md`.
- Any decision in `GPU-DECISIONS.md` that still constrains the code — why Skia rather than OpenCV
  CUDA, why there is no CPU frame-level parallelism — belongs wherever the code it constrains is
  documented.
- The final numbers belong in `doc/PERFORMANCE-BASELINE.md`.
