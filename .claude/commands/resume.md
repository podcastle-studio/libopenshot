---
description: Pick up the GPU-rendering work exactly where the last session stopped
---

Get oriented and continue the work, without asking me to re-explain anything.

1. Read `doc/GPU-RENDERING.md` — above all "Status and what is left" — and the "Read first" section
   of `CLAUDE.md`.
2. Report the current branch, whether the working tree is clean (here and in
   `src/effects/image-processing-lib`), and the last three commits.
3. Check the build is usable: `cmake --build cmake-build-release --target openshot openshot-golden openshot-bench`.
4. Run `tools/golden.sh check`. If it is not green, that is the first thing to fix — stop and tell me.
5. Summarise in at most eight lines: where we are, what the next item in "What is left" is and what
   it is blocked on (a decision, infrastructure, or nothing), and anything that looks stale or
   contradictory between the doc and the code.

Then wait for me to choose. Most of what is left is an owner decision or needs infrastructure this
machine does not have; do not start one of those on your own.
