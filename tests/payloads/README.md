# Real production payloads

Real export payloads captured from the `async-video-export.prod.0-sub` Pub/Sub subscription, kept as
the production payload corpus (`doc/GPU-RENDERING.md`, "What is left", 10): they catch
JSON→timeline regressions the golden suite structurally cannot see, because `tests/golden` drives
the library directly and never parses a payload.

## Running the corpus

```bash
tests/payloads/run-corpus.sh check            # two rounds each, compare to the recorded hashes
tests/payloads/run-corpus.sh update           # re-record after an intentional change
tests/payloads/run-corpus.sh check pip-lut    # one payload
```

It needs `render-payload` from `../video-rendering-service`, built **Release** — a Debug build is
roughly 25x slower and makes the two rounds take hours:

```bash
# ../video-rendering-service, branch feature/gpu-rendering
cmake -S . -B cmake-build-release -DCMAKE_BUILD_TYPE=Release -DBUILD_RENDER_PAYLOAD=ON \
      -DCMAKE_PREFIX_PATH=$HOME/google-cloud-cpp/installed/x64-linux
cmake --build cmake-build-release --target render-payload
```

The switches under test are the ordinary environment ones, read by the service and the library
rather than by the script: `ENCODER=libx264|h264_nvenc`, `OPENSHOT_GPU=off|vulkan|lavapipe`.

What it checks, in order: both rounds complete; the two rounds' decoded frames are identical
(`ffmpeg -f framemd5`, so container metadata is ignored and every pixel is not); and the frame
hashes match `tests/payloads/expected/<payload>.sha256`. A frame-hash change is a real difference
in what the service renders — look at it before running `update`.

## Replaying a capture offline

**A payload's `fileUrl`s are signed and expire 24 h after issue**, so a stored payload is a record
of the *structure*, not something that still downloads. The media is archived at capture time under
the git-ignored `tmp/payloads/<capture>/` (`media/` plus `urls.txt` mapping fileId → file), together
with the export production made from the same payload.

Pre-seeding the service's download destination does not work — `HTTPFileTransfer::download` opens it
`"wb"` on every attempt and re-fetches unconditionally. So the service grew a local-media mode
(`MediaFetch`, added 2026-09-18 for this):

- `RENDER_MEDIA_CACHE=<dir>` takes each file from `<dir>` instead of the network, keyed by the URL's
  own basename with the query string stripped — which is exactly how the archives are named. Clip
  media, sound effects, the watermark and the LUT all go through it.
- `RENDER_MEDIA_CACHE_STRICT=1` turns a missing entry into an error instead of a silent network
  fetch. `run-corpus.sh` always sets it: without it a "passing" run can quietly depend on a URL that
  still happens to resolve, which is the failure mode this whole mechanism exists to remove.

With the variable unset — every production run — behaviour is unchanged.

`run-corpus.sh` pairs a payload with its archive by prefix: `prod-2026-09-16-pip-lut-whoosh.json`
uses `tmp/payloads/prod-2026-09-16/media/`. Keep that naming when adding a capture.

## Adding a capture

1. Capture the Pub/Sub message and save it here as `prod-<date>-<what-it-covers>.json`.
2. **Archive the media the same day**, while the signed URLs still work: `tmp/payloads/<capture>/media/`
   plus a `urls.txt` of `fileId<TAB>filename<TAB>url`. Include system files — the watermark, the LUT
   and any transition **sound effect** (`transitions/sound-effects/*.opus`, built from
   `systemFilesBucket` + the preset's `filePath`; these have unsigned URLs and are easy to miss).
3. Save production's own render of the same payload next to it, for comparison.
4. `tests/payloads/run-corpus.sh update <name>` and commit the recorded hash.

To re-run a payload after its URLs expire and the archive is gone, re-export the project from the
app and capture the fresh message.

## The corpus

| payload | captured | covers |
|---|---|---|
| `prod-2026-09-16-pip-lut-whoosh.json` | 2026-09-16 11:45 UTC | 1280x720 / 25 s / 3 tracks · **corner radius 0.4 + crop w=0.539** on a video clip (W14 / W19–W21), **LUT** `e333f84ebb10.cube` on three clips, COLOR + LIGHT + ADJUSTMENT filters stacked, a 90°-rotated clip at opacity 0.25 split across a trim boundary, a **WHOOSH transition** (ZOOM + BLUR + ZOOM_BLUR + ALPHA keyframed, `isOverlapping`) with its `whoosh.opus` sound effect, watermark `.mov` overlay, `#D4D5DB` background colour, one clip with a separate audio file. No subtitles, no text, no animations. |

`prod-2026-09-16-pip-lut-whoosh.json` is the closest thing the corpus has to `podcast_pip` and
`heavy_effects` driven the way production drives them, which makes it the payload to measure W14's
and W19–W21's effect on a real job. Its prod-rendered reference is
`tmp/payloads/prod-2026-09-16/` → `Untitled video - Sep 16, 2026, 15_40PM.mp4` (kept in `tmp/`).

**Still wanted** (the corpus should be six): text animations, subtitles, a transition-heavy timeline, a
chroma-key job, and a 4K source. The three shapes this capture exercises that `tests/golden` does
*not* cover: a clip rotated and split across a trim
boundary, an overlapping transition, and a `.mov` alpha watermark across the whole timeline.
