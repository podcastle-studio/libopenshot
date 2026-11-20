---
description: Run the golden-frame regression suite and report what changed
argument-hint: "[scenario or tag filter]"
---

Run the visual regression suite and tell me the result.

```
tools/golden.sh check $1
```

If everything passes, say so in one line with the scenario and frame counts.

If anything fails:
1. List the failing scenarios with their PSNR / SSIM / max-diff numbers.
2. Open the triptychs for the failures (`Read` the `*_triptych.png` files the report names) and
   describe what actually changed visually — position shift, colour cast, missing element, noise.
3. Say whether this looks like an intended consequence of the current change or a regression.
4. Do **not** re-baseline anything unless I confirm the change is intended. When I do confirm,
   re-baseline only the affected scenarios with `tools/golden.sh update <filter>` and commit the
   PNGs together with the code change.
