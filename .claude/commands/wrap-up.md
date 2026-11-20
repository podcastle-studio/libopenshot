---
description: Close out the session so the next one can continue cleanly
---

Finish the session properly. Do all of this, then give me a summary of at most eight lines.

1. Run `tools/golden.sh check`. Report the result; if it is red, say exactly what is broken and do
   not pretend the session is clean. After a change to the GPU path, run the four-way sweep and
   `openshot-gpu-checks` too (`doc/GPU-RENDERING.md`, "Validating a change").
2. Update `doc/GPU-RENDERING.md`:
   - "What runs where" and "The switches" reflect what actually works now, not what was attempted.
   - "Status and what is left": strike what was finished, add what was found, keep the order.
   - A decision that was taken goes into "Decisions that constrain the code", dated, with its reason
     and its "revisit if".
   - If work is half-finished, add a short "IN PROGRESS" note at the top of "Status and what is
     left": which files are touched, what is verified, what is not, how to resume or revert.
3. Performance measured at the end of a step goes into `doc/PERFORMANCE-BASELINE.md`, with the JSON
   under `tests/bench/results/`.
4. Show me `git status` (here and in the submodule); commit the work in coherent commits with real
   messages, the submodule first. Never commit a red golden suite without saying so in the commit
   message.
