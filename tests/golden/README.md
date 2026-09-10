# Golden-frame regression suite (`openshot-golden`)

Renders every feature that `video-rendering-service` drives through libopenshot, at fixed frames,
and compares the result against committed reference PNGs with visual metrics (PSNR, SSIM, max
channel difference, % of pixels off by more than 2). Failures produce a side-by-side
golden | actual | diff image and an HTML report. Run it before and after every change to the
render path; the GPU rewrite (see `doc/GPU-RENDER-PLAN.md`) is gated on it.

## Layout

```
tests/golden/
  main.cpp Harness.* Image.* Report.cpp   runner, PNG I/O + metrics (the only Qt-touching file is Image.cpp)
  Recipes.*                               helpers that build clips/effects EXACTLY like the service
  scenarios/*.cpp                         one file per feature group, ~95 scenarios
  media/                                  deterministic test media (generate.sh), fonts, LUT, SVG shapes
  expected/<scenario>/f000030.png         committed goldens
```

## Run

```bash
cmake --build cmake-build-release --target openshot-golden
cmake-build-release/tests/golden/openshot-golden --report /tmp/golden-report      # compare, exit 1 on failure
cmake-build-release/tests/golden/openshot-golden --list                          # scenario names + tags
cmake-build-release/tests/golden/openshot-golden --filter transitions            # a group (tag) or a name substring
cmake-build-release/tests/golden/openshot-golden --update --filter text.glow     # re-baseline one scenario
ctest --test-dir cmake-build-release -R golden                                   # same, through CTest
tools/golden.sh check                                                            # build + run + report path
```

Actual frames go to `--out` (default `./golden-out`); the report to `--report <dir>/index.html`,
failures first, each with a triptych (golden, actual, amplified diff).

## Workflow for an intentional change

1. Run `check` before the change: everything green.
2. Make the change, run `check` again, open the report, look at every failing triptych.
3. If the new output is the intended one, `--update --filter <scenario>` for exactly those
   scenarios and commit the PNGs together with the code. Never `--update` without a filter
   after a rendering change unless you have looked at every frame.

## Tolerances

| Tag / tolerance | PSNR ≥ | SSIM ≥ | max abs ≤ | Used for |
|---|---|---|---|---|
| default | 45 dB | 0.98 | 255 | most scenarios |
| `exact` | 60 dB | 0.995 | 1 | pure integer paths: flips, layer order, blend modes, crops of decoded frames |
| Loose (text, subtitles, enhancement) | 38 dB | 0.95 | 255 | anti-aliased glyphs, large blurs |
| Codec (export round trip) | 35 dB | 0.92 | 255 | x264-decoded frames |

The whole suite runs in about 7 s on a laptop and is bit-stable across runs and thread counts.

## Adding a scenario

Use `add(name, tags, frames, build, tolerance)` in the matching `scenarios/*.cpp`. Build the
timeline through `Recipes.h` (they mirror the service's `addVisualMediaClip`, `addTextClip`,
`Transition.cpp`, `Animation.cpp`, `Subtitles.cpp` verbatim) so the scenario exercises what
production does. Then `--update --filter <name>`, look at the PNGs, commit them.

## Media

`media/generate.sh` regenerates the video/image inputs with ffmpeg lavfi sources. Do not re-run
it casually: a different ffmpeg build encodes differently and every golden would need to be
re-baselined. Fonts are Noto Sans (OFL) loaded by absolute path so fontconfig never decides.
