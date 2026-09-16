# Real production payloads

Real export payloads captured from the `async-video-export.prod.0-sub` Pub/Sub subscription, kept
for **W04 — production payload corpus** in `doc/gpu-migration/GPU-WORKLIST.md`: they catch
JSON→timeline regressions the golden suite structurally cannot see, because `tests/golden` drives
the library directly and never parses a payload.

Run one through the service, not through this repo:

```bash
# ../video-rendering-service, branch feature/gpu-rendering
render-payload <payload.json> <out-dir> [label]
# the switches under test are the ordinary environment ones:
ENCODER=libx264|h264_nvenc   OPENSHOT_GPU=off|vulkan|lavapipe
```

**The `fileUrl`s are signed and expire 24 h after issue**, so a stored payload is a record of the
*structure*, not something that still downloads. Media archived alongside a capture lives in the
git-ignored `tmp/payloads/<capture>/` (`media/` plus `urls.txt` mapping fileId → file), and the
golden export produced by production for comparison sits next to it. To re-run a payload after its
URLs expire, re-export the project from the app and capture the fresh message.

| payload | captured | covers |
|---|---|---|
| `prod-2026-09-16-pip-lut-whoosh.json` | 2026-09-16 11:45 UTC | 1280x720 / 25 s / 3 tracks · **corner radius 0.4 + crop w=0.539** on a video clip (W14 / W19–W21), **LUT** `e333f84ebb10.cube` on three clips, COLOR + LIGHT + ADJUSTMENT filters stacked, a 90°-rotated clip at opacity 0.25 split across a trim boundary, a **WHOOSH transition** (ZOOM + BLUR + ZOOM_BLUR + ALPHA keyframed, `isOverlapping`), watermark `.mov` overlay, `#D4D5DB` background colour, one clip with a separate audio file. No subtitles, no text, no animations. |

`prod-2026-09-16-pip-lut-whoosh.json` is the closest thing the corpus has to `podcast_pip` and
`heavy_effects` driven the way production drives them, which makes it the payload to measure W14's
and W19–W21's effect on a real job. Its prod-rendered reference is
`tmp/payloads/prod-2026-09-16/` → `Untitled video - Sep 16, 2026, 15_40PM.mp4` (kept in `tmp/`).
