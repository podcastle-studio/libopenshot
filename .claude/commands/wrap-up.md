---
description: Close out the session so the next one can continue cleanly
---

Finish the session properly. Do all of this, then give me a summary of at most eight lines.

1. Run `tools/golden.sh check`. Report the result; if it is red, say exactly what is broken and do
   not pretend the session is clean.
2. Update `doc/gpu-migration/STATUS.md`:
   - "Where we are" reflects what actually works now, not what was attempted.
   - "Next step" names the concrete next action, with the plan step number and its numeric gate.
   - Add a dated line to the Log.
   - If work is half-finished, add a short "IN PROGRESS" block saying which files are touched,
     what is verified, what is not, and how to resume or revert.
3. If a decision was taken (canvas precision, backend, base branch, parity reference), record it in
   `doc/gpu-migration/GPU-DECISIONS.md` with the date and the reasoning.
4. If the plan changed in reality, update `doc/gpu-migration/GPU-RENDER-PLAN.md` rather than leaving the doc stale.
5. Show me `git status`; commit the work in coherent commits with real messages. Never commit a red
   golden suite without saying so in the commit message.
