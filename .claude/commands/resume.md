---
description: Pick up the GPU-rendering work exactly where the last session stopped
---

Get oriented and continue the work, without asking me to re-explain anything.

1. Read `doc/gpu-migration/STATUS.md` (state, open decisions, next step) and the "Read first" section of `CLAUDE.md`.
2. Report the current branch, whether the working tree is clean, and the last three commits.
3. Check the build is usable: `cmake --build cmake-build-release --target openshot openshot-golden openshot-bench`.
4. Run `tools/golden.sh check`. If it is not green, that is the first thing to fix — stop and tell me.
5. Summarise in at most eight lines: where we are, what the next step is (from `doc/gpu-migration/STATUS.md` and the
   matching step in `doc/gpu-migration/GPU-RENDER-PLAN.md` with its numeric gate), and anything that looks stale
   or contradictory between the docs and the code.

Then wait for me to choose, unless the next step is unambiguous and non-destructive, in which case
start it.
