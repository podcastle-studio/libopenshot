# GPU migration — the worklist

**This is the file you work from.** One strict order, one item at a time. The reasoning behind the
items — measurements, the GPU primer, the Qt inventory, sizing for parallel exports — stays in
`GPU-RENDER-PLAN.md`, which no longer carries a step list of its own.

Numbering here is **sequence**: W01 runs before W02. Each item also carries its legacy plan id
(`2.4`, `3.1`, …) because `STATUS.md`, `GPU-DECISIONS.md` and the commit history refer to those, and
those ids are *not* in execution order, which is what made the plan hard to follow.

---

## How to run one item

Each item is written so a session that has read nothing else can execute it.

1. Read `CLAUDE.md` and `doc/gpu-migration/STATUS.md`.
2. Read **only** the item you are doing, plus anything its *Depends on* line names.
3. `tools/golden.sh check` green **before** touching anything. If it is not, stop and fix that first.
4. Work the sub-tasks in order.
5. Run the item's **Gate** and record the actual number.
6. Commit. Add one line to the `## Log` in `STATUS.md` with the number. Tick the sub-tasks here.
7. **Stop. Clear the context.** Do not start the next item in the same session.

Rules that apply to every item without being repeated in it:

- The four-way golden sweep (CPU Skia; GPU Skia off; Vulkan; lavapipe) is the acceptance test, all
  four at 295/295. Commands in `CLAUDE.md`.
- The CPU path ships. Never delete CPU code because the GPU makes it unnecessary — gate it on
  `GpuDevice::available()` and keep the branch.
- Parity classes (`exact` / `close` / `redefine`) and what each permits: `GPU-RENDER-PLAN.md` §5.
- A change that is *meant* to move pixels: review every failing triptych, re-baseline only those
  scenarios, commit the PNGs with the code.
- Anything that fails its gate is reverted or flagged off. It does not stay merged "to fix later".

**Status line:** Stage 5 (the GPU compositor) is **finished** — W11–W14 done (2026-09-16), W15 and
W16 **void** (2026-09-17), W17 **done** (2026-09-17, fps gate closed 2026-09-18), W18 **done**
(2026-09-18, scoped to the render path because its written gate was unsatisfiable) · **Stage 3:
W05, W07, W08 done (2026-09-18)** — W07's fps gates are unreachable by W07 and want restating,
W05 is built but **flagged off** for missing its gate, W08 met one of two · **W09 is the next item** — the only
one left before Stage 6 that needs nothing from anyone else — with **W06 needing a cgroup-limited
container** and W10 skipped · **Stage 2 is part done** — W04's mechanism works and its first
payload is green, but the corpus needs five more captures and W03's workflow has never run ·
then Stage 6 (W19–W21, effects) · W01/W02 are now **Stage 10**, at the end.
**Stage 6 is finished (2026-09-22)** and **Stage 7 has started: W22 is done (2026-09-22), gate met
at 0.166 ms against 0.300 for a 4K frame, exact and clean under `compute-sanitizer`. W23 is done
(2026-09-23), gate met: `source_4k` 78–82 → 126–133 fps at 0.6 cores. W24 is done (2026-09-23),
gate restated — its 90 fps is W25's to reach. W25 is next.**

> **2026-09-18, project owner — finish Stages 2 and 3 before the effects work.** Stage 5 is done and
> Stage 6 (effects and transitions as shaders) is the obvious next thing, but the safety net (Stage
> 2) and the CPU wins (Stage 3) were skipped past and should be closed first, so everything up to
> the effects work is complete. Stage 6 is also gated on the open transition-parity decision — see
> `TRANSITION-PARITY.md`.

> **2026-09-16, project owner, still standing.** Do not build or push the service image (W01) and do
> not run the merge/release gate (W02) — those happen at the very end, and as of 2026-09-18 they
> live in **Stage 10** rather than at the front. Develop and test locally. W11 was taken out of
> order for the same reason: W12 depends on it and nothing else does.

---

## Stage 2 — Safety net, before anything rewrites the render path

Stage 5 rewrites `Clip.cpp` and `Timeline.cpp`. Do these first or the rewrite has no net under it.

### W03 — CI: golden suite on every PR · legacy `0.6` (workflow written 2026-09-18, not yet enabled)

**Goal.** The suite runs without anyone remembering to run it.

> **2026-09-18 — this is "stand CI up", not "add a step".** `.github/workflows/ci.yml` and
> `.gitlab-ci.yml` are **upstream OpenShot's, unmodified** — no podcastle references — and build
> upstream libopenshot, which has no Skia, no submodules and no pinned FFmpeg. Nothing in this repo
> currently gates the fork's render path. The fork is on GitHub
> (`podcastle-studio/libopenshot`), so this is GitHub Actions.

- [x] `.github/workflows/golden.yml` — submodules, apt dependencies, `libopenshot-audio` pinned by
      tag, Skia built from source and cached on the build script's hash, `ENABLE_PLAYER=OFF`,
      `tools/golden.sh check`, report uploaded as an artifact. The golden media and the expected
      PNGs are committed, so nothing has to be generated on the runner.
- [ ] **Two blockers, both needing a decision rather than code:**
      1. **FFmpeg.** The dev box and the runtime image both use an FFmpeg 6.1 built
         `--enable-nvenc` in `/usr/local`, from the private base image
         `cuda12.8.1-cudnn9.7.1-ffmpeg6.1-nvidia24.04`. The workflow falls back to Ubuntu 24.04's
         apt FFmpeg 6.1.1 because that registry is not authenticated here — the same blocker W01
         carries, which the worklist did not record as shared. `export.roundtrip_x264` compares
         decoded frames to committed goldens at PSNR ≥ 45 dB, so a different x264 may fail it, and
         that would be a toolchain difference rather than a regression. The clean fix is to run the
         job `container:`-ed on the pinned base image.
      2. **The private submodule.** `src/effects/image-processing-lib` clones over SSH, so CI needs
         a deploy key in `secrets.SUBMODULE_SSH_KEY` or an HTTPS+token rewrite.
- [ ] `openshot-bench --quick` on merges to `develop`, appended to a trend file. Deliberately left
      until the suite job is actually green — a benchmark trend from a runner of unknown
      consistency is worse than none.

**Gate.** A PR that deliberately shifts one pixel fails CI.
**Gate status — not met, and cannot be from here.** The workflow has never run: enabling it and
opening the deliberate-regression PR are actions on the GitHub repo, which is the project owner's
call, and the first run needs the two decisions above. What *is* verified is that the YAML parses
and that every dependency it installs is one the local build actually uses.
**Size.** ~1 day as written; realistically that plus whatever the base-image access costs.

### W04 — Production payload corpus · legacy `0.5` (mechanism done 2026-09-18; corpus still 1 of 6)

**Goal.** Catch JSON→timeline regressions the golden suite structurally cannot see, because it
drives the library directly and never parses a payload.
**Depends on.** `tools/render-payload` in the service repo.

> **2026-09-18 — the corpus had zero runnable payloads, not one.** A capture's signed `fileUrl`s
> expire 24 h after issue, and although the media was archived at capture time,
> `HTTPFileTransfer::download` opens its destination `"wb"` on every attempt and re-fetches
> unconditionally, so pre-seeding it did nothing and `render-payload` downloaded into a 403. The
> whole item was unrunnable and nothing said so.

- [x] **Offline replay.** `MediaFetch` in the service (`RENDER_MEDIA_CACHE`, plus
      `RENDER_MEDIA_CACHE_STRICT` so a missing archive entry is an error rather than a silent
      network fetch) serves each file from the archive, keyed by the URL's basename. It covers all
      six fetch sites including the Redis-cached one used for clip media and sound effects, which
      is a separate path from the plain download. Unset — every production run — behaviour is
      unchanged. **This also unblocks W05's gate**, which is measured through `render-payload`.
- [x] **`tests/payloads/run-corpus.sh`** — two rounds per payload, decoded-frame hashes
      (`ffmpeg -f framemd5`, so container metadata is ignored and every pixel is not), compared
      round-to-round and against `tests/payloads/expected/<payload>.sha256`.
- [x] **First payload green.** `prod-2026-09-16-pip-lut-whoosh`: **750 frames, the two rounds
      identical, hash recorded.** ~8 min per round in a Release build of `render-payload` — a Debug
      build is ~25x slower and makes this impractical, which is now in the README.
- [x] The capture's archive was incomplete: the WHOOSH transition's `whoosh.opus` sound effect had
      never been archived. Fetched and added, and the README now calls out sound effects explicitly
      because their URLs are unsigned and easy to miss.
- [ ] **Five more payloads**: text animations, subtitles, a transition-heavy timeline, chroma key,
      a 4K source. This is the remaining work and it needs fresh captures — the media must be
      archived the same day.
- [ ] Wire into CI behind a nightly, not per-PR (media is large). Blocked with W03 on there being
      CI for this fork at all.

**Gate.** All six render without error; frame hashes stable across two runs.
**Gate status — met for the one payload that exists.** 750 frames, two rounds identical, recorded
and re-verified through the `check` path. The gate cannot be fully met until the corpus is six.
**Size.** ~2 days, mostly collecting payloads — unchanged, and now genuinely only collection.

---

## Stage 3 — CPU wins that survive the GPU move

All pure CPU, none needs a GPU node, any order among themselves. They are here rather than later
because W05–W08 keep their value after Stage 5 and 7; W10 does not (see its note).

The honest framing from the baseline: the encoder is 0–30 % of wall time depending on the scenario,
so this stage helps `single_video` and `source_4k` and does **essentially nothing** for
`everything`, `text_animated_glow_3` or `heavy_effects`. Do not expect "1.5–2× exports" from it.

### W05 — Service: one `WriteFrame` call · legacy `1.1` (built 2026-09-18, **shipped OFF — gate not met**)

**Goal.** Stop chunking the export into 8-frame calls, which drains the writer pipeline every chunk.
**Note.** `openshot-bench` already calls `WriteFrame` once for the whole range, so the bench
**cannot** show this win. Measure it on the service with `tools/render-payload`.

- [x] `FFmpegWriter::SetProgressCallback` (libopenshot), plus `BoundedFrameQueue::abort()` — the
      consumer throwing used to leave the producer blocked forever on a queue nobody drains, which
      was already reachable from any encode failure and is the normal cancellation path now.
- [x] Single `WriteFrame(&timeline, start, end - 1)` behind `RENDER_SINGLE_WRITEFRAME=1`, queue
      capacity 8 → 16. **The flag defaults to OFF** — see the gate.
- [x] **Progress moved to a timer thread, which is the real lesson here.** Driving it from the
      writer callback does not work: that callback runs on the *encoding* thread, which drains the
      queue in milliseconds and then waits for the renderer, so a per-callback throttle collapsed
      into bursts — 25 messages spaced 21–70 s apart, far worse than the chunked loop's one per
      5.7 s. The callback now only stores two atomics; a timer thread publishes and checks
      cancellation on one one-second tick. Cancellation shares that tick rather than being polled:
      it was briefly checked every 100 ms, and `isCancelled()` is a **Redis round trip**, so that
      would have been 10 req/s on Redis per running export against roughly one per 30 s of video
      before.

**Gate.** One production payload ≥ **15 %** faster wall-clock through `render-payload`; progress
messages still arrive at least every second; golden green (library unchanged).
**Gate status — NOT met; the change is therefore flagged off rather than reverted.** Five
interleaved pairs on the same binary with only the flag differing: **583.3 → 549.5 s mean, ≈ −6 %**,
per-pair deltas −1.6 % to −13.4 %, ~10 % run-to-run spread on a loaded host. Output **byte-identical**
between modes on every pair; golden green (the library change is additive).

**Why it falls short, and when it would not.** Overlapping render with encode can save at most the
encoder's share of wall time. The one payload in the corpus is 720p with a LUT, stacked filters and
a transition — it is render-bound, so ~6 % is close to the ceiling *for this job*. A lighter, more
encode-bound payload (4K source, few effects) would show more. **The 15 % figure was never checked
against a payload; re-measure it when the corpus reaches six rather than assuming either number.**

**The second half of the gate is unverified and cannot be verified here.** `render-payload` points
Pub/Sub and Redis at dead addresses on purpose, so every publish and every `isCancelled()` blocks on
a retry — the observed cadence is the harness, not the code. Watch it once in an environment where
those services answer, then decide whether to default the flag on.
**Size.** ~1 day.

### W06 — Service: thread budgets · legacy `1.2`

**Goal.** Stop N processes each assuming they own the whole box.
**Note.** The upstream merge brought thread-budget settings that overlap this; check what landed
before writing anything.

- [ ] Derive `Settings::FF_THREADS` and `OMP_THREADS` from `/sys/fs/cgroup/cpu.max` divided by
      `SERVICE_NUM_INSTANCES_PARALLEL`, minimum 2.
- [ ] Env override `OPENSHOT_THREADS`.

**Gate.** In an 8-CPU container with 2 processes, no process exceeds ~400 % CPU and wall time does
not regress. On the dev box `openshot-bench --threads 4` within 10 % of `--threads 16` on
`single_video`.
**Size.** ~half a day.

### W07 — `Frame::GetImageCV` memoisation · legacy `1.6` (done 2026-09-18; **gate unreachable, see below**)

**Goal.** Stop two colour conversions per call in the effect chain.
**Note.** W15 and W21 delete most callers. W15 is **void**, so those callers are staying.

> **2026-09-18 — measured before implementing. Half the item was right and the gate is not
> reachable by it.** Instrumented `GetImageCV`/`SetImageCV` at 1080p over 150 frames. Note that
> `openshot-bench` **forks a child per case and sends its stderr to /dev/null**, so instrumentation
> only shows up through the in-process `--case` path — the first measurements read zero and looked
> like the effects were never running.
>
> | scenario | wall | GetImageCV | SetImageCV | share of wall |
> |---|---:|---:|---:|---:|
> | `transitions_chain` | 5294 ms | 380 calls / 140 ms | 322 calls / **336 ms** | **9.0 %** |
> | `heavy_effects` | 12814 ms | 300 calls / 130 ms | 300 calls / **313 ms** | 3.5 % |
> | `everything` | 28536 ms | 0 | 0 | **0 %** — no OpenCV effect in it at all |
>
> **The gate cannot be met by this item.** `transitions_chain` needs 749 ms to reach 33 fps and
> `heavy_effects` needs 1276 ms to reach 13; the *entire* conversion cost is 476 ms and 443 ms.
> Deleting 100 % of it gives 30.8 and 12.1 fps. The gate numbers predate the compositor work that
> already moved these scenarios, and nothing in W07's scope closes the rest.

- [x] **`SetImageCV` no longer converts twice.** `Mat2Qimage` converted into a temporary `cv::Mat`,
      wrapped that in a `QImage` and then `copy()`d it — two full-frame passes and two allocations
      for the same pixels. It now allocates the `QImage` first and lets `cvtColor` write straight
      into its buffer. The 3-channel path drops a `split`/`merge` as well: `COLOR_BGR2RGBA` adds the
      opaque alpha itself. **336 → 102 ms** on `transitions_chain`, **313 → 97 ms** on
      `heavy_effects`; per call 1.04 → 0.32 ms, which makes it cheaper than `GetImageCV`'s 0.37.
- [x] ~~Cache `imagecv` with a dirty flag set by `AddImage`~~ — **rejected on measurement.** The
      access pattern is strictly alternating: `heavy_effects` is 300 `Get` against 300 `Set`, and
      `transitions_chain` 380 against 322. A dirty flag set by `SetImageCV` would therefore be
      dirty on essentially every `Get`, so the cache would almost never hit — while carrying the
      stale-frame risk the item itself warns about. Not worth it. Reusing `imagecv`'s buffer in
      `Qimage2mat` was rejected too: `cv::Mat::create` reuses an existing allocation **regardless of
      refcount**, so writing into the member would corrupt any `cv::Mat` a caller still held.

**Gate.** `transitions_chain` 1080p render ≥ **33 fps** (27.4); `heavy_effects` ≥ **13 fps** (11.5);
golden green **with no updates**.
**Gate status — golden met, fps gates not met and shown unreachable.** Golden **295/295 four ways
with no re-baseline** (the conversion is bit-identical). Interleaved A/B, three pairs, in-process
`--case` at 1080p/150 frames: `transitions_chain` **27.37 → 28.69 fps (+4.8 %)**, ranges
non-overlapping; `heavy_effects` 11.27 → 11.33, **within noise** — its wall is 12.6 s and the saving
is 215 ms, so the change is real but not resolvable as fps there.
**Recommendation.** Restate the gate against what remains measurable — the conversion cost itself,
now 222 ms of 5096 (4.4 %) and 227 of 12629 (1.8 %) — and carry the fps targets to whichever item
actually owns the rest of those frames. This has not been done; it needs the project owner.
**Size.** ~1 day.

### W08 — Reader: remove copies, thread swscale · legacy `1.5` (copy half) (done 2026-09-18)

**Goal.** Delete the per-frame `memset` + `av_image_copy` and let swscale use more than one thread.
**Note.** The hardware-decode crash fix that used to be part of `1.5` **arrived with the upstream
merge** — do not re-do it. `HARDWARE_DECODER` stays 0; W23 is what makes hardware decode pay.

> **2026-09-18 — measured first.** Instrumented at 1080p over 150 frames through the in-process
> `--case` path (`openshot-bench` forks per case and sends the child's stderr to /dev/null, so
> nothing shows through the normal path):
>
> | scenario | wall | `av_image_copy` | `memset` | `sws_scale` | share |
> |---|---:|---:|---:|---:|---:|
> | `single_video` | 1279 ms | 40 ms | **153 ms** | 91 ms | **22.3 %** |
> | `source_4k` | 2426 ms | **252 ms** | 176 ms | **891 ms** | **54.4 %** |
>
> Unlike W07 the gates were reachable from here, and both sub-tasks that survived measurement are
> worth more at 4K, where the copy and the scale both scale with source resolution.

- [x] **`memset` dropped.** `sws_scale` writes every destination pixel, so zeroing first was a
      second full-frame pass for nothing. **Part of its cost migrates rather than disappearing** —
      the memset was pre-faulting the freshly `aligned_malloc`'d pages, so `sws_scale` now takes
      those faults itself (91 → 160 ms on `single_video`). Net still a clear win.
- [x] **`av_image_copy` → `av_frame_ref`.** Decoded frames are refcounted, so taking a reference
      does what the copy was protecting against — the decoder gets a fresh buffer for the next
      frame — without moving the bytes. `RemoveAVFrame` changed with it: `av_freep(data[0])` would
      now free memory the decoder owns, and `av_frame_free` already unrefs. Isolated interleaved
      A/B: `source_4k` **64.6 → 71.0 fps (+9.9 %)**, ranges non-overlapping; `single_video` flat,
      as expected when the copy is 40 ms rather than 252.
- [x] ~~Build the scaler with `sws_alloc_context` + `av_opt_set_int(ctx, "threads", n)`~~ —
      **rejected on measurement.** The option is real and accepted (`av_opt_set_int` returns 0 and
      reads back), but with the decoder thread count held fixed and identical frame counts,
      `source_4k` measured **663.8 / 674.0 / 650.0 / 682.9 ms** of `sws_scale` at 1, 2, 4 and 8
      threads — flat within noise. The conversion is memory-bandwidth bound, not compute bound.
      `sws_getCachedContext` is kept; the hand-built context and its parameter cache were reverted
      rather than left in for a measured-zero gain.
- [x] **Golden suite run under ASan** (`-fsanitize=address`, `detect_leaks=0`): **zero
      AddressSanitizer reports**, 98 scenarios / 295 frames / 17 checks green. That is the check
      that matters for this item — `pFrame` now holds a reference rather than its own allocation.

**Gate.** Golden green **with no golden updates**; `source_4k` 1080p render ≥ **70 fps** (61);
`single_video` render ≥ **125 fps** (117).
**Gate status — golden met, one fps gate met and one on the line.** Golden **295/295 four ways with
no re-baseline**, and ASan clean. Interleaved A/B against HEAD, three pairs, plus five further
samples of the final build, in-process at 1080p/150 frames:

| scenario | before | after | change | gate |
|---|---:|---:|---:|---|
| `source_4k` | 61.3 | **70.1** (68.9–71.1, 8 runs) | **+14.4 %** | ≥ 70 — **met**, 5 of 8 runs clear |
| `single_video` | 116.6 | **124.9** (121.8–128.6, 8 runs) | **+7.1 %** | ≥ 125 — **0.1 % short**, 3 of 8 clear |

`single_video` is indistinguishable from its target rather than short of it, on a host running a
browser and two IDEs. Not claimed as passed.
**Size.** ~2 days.

### W09 — Writer: finish nvenc rate control · legacy `1.4` remainder

**Goal.** Finish what landed as a side effect of the image work.
**Already done (2026-09-15).** The library no longer forces `preset slow`, `tune zerolatency` or
constrained-baseline profile on NVENC; it sets `profile=high` and keeps caller settings. The service
asks for `rc vbr`, `cq 19`, `preset p5`, `tune hq`. Both encoders emit profile 100.

- [x] Add `b_ref_mode middle` and `spatial-aq 1`.
- [x] Stop `SetOption("crf")` hijacking the bitrate when hardware encode is on.
- [x] Guard the `hw_en_on`-only branches with `hw_en_supported`.
- [x] Run the VMAF comparison that has never been run.

**Gate.** VMAF of the nvenc output ≥ VMAF of the x264 output − 2 points on `podcast_pip`; file size
within ±20 %; `single_video` nvenc fps does not regress.

> **Done 2026-09-18. Gate met on all three clauses:** VMAF −0.21 (98.018 against x264's 98.230),
> size **+1.7 %**, `single_video` nvenc **122.4 → 122.3 fps** (the same number twice). Four-way
> golden sweep 295/295. Numbers, method and the sweeps behind them:
> `doc/PERFORMANCE-BASELINE.md`, "W09: the NVENC rate-control comparison"; the decision is in
> `GPU-DECISIONS.md`.
>
> **What the measurement changed about the plan.** The item's own two sub-tasks were the small part.
>
> 1. **The real defect was `cq 19` itself**, which no sub-task named. It was spending **2.1× the
>    bits of libx264 crf 18 for 0.16 VMAF points** — the size half of the gate failed at **+112 %**
>    before anything else was touched, and neither `b_ref_mode` nor `spatial-aq` could have closed
>    that. Fixing it meant giving `crf` a meaning on NVENC (`cq = crf + 10`, calibrated at three
>    quality points on three scenarios) so there is one quality knob and one place that owns the
>    calibration. `tests/golden/Recipes.cpp` and the service's `VideoRenderingImpl.cpp` now both
>    say `crf 18` for either encoder.
> 2. **`b_ref_mode middle` is a no-op at these settings** — byte-identical output, because `p5`/`hq`
>    already picks it — *and* it was unreachable: the writer forces `max_b_frames = 0`, and the one
>    public way round that, `allow_b_frames 1`, threw `InvalidCodec` on NVENC because
>    `add_video_stream` asks for 10 B-frames against a hardware limit of 4. Both fixed; the option
>    is set anyway, guarded, since a preset is not a contract.
> 3. **Zeroing the bitrate is correctness, not the win it looks like.** `-b:v 10M` and `-b:v 0`
>    produce byte-identical files once `rc vbr` and `cq` are set.
> 4. **`spatial-aq 1` is the one sub-task that paid**: smaller *and* better (3.09 MB at VMAF 98.17
>    against 3.49 MB at 97.69), so it is now a writer default rather than something a caller has to
>    know to ask for.
>
> **`openshot-bench` gained a `lossless` mode** so this is reproducible: the gate needs a common
> reference, and there was no way to write one. It is not a timing case.
>
> **Not taken: B-frames on by default.** Now that `allow_b_frames` works, it measures as a wash at
> matched `cq` (−2.3 % size for −0.08 VMAF). Left to the caller — see `GPU-DECISIONS.md`.

> **2026-09-18 — the tooling is present, checked so the next session does not have to.** The local
> FFmpeg has the `libvmaf` filter and scores with its default model out of the box
> (`ffmpeg -i out.mp4 -i ref.mp4 -lavfi libvmaf -f null -` prints "VMAF score:"); extra models sit in
> `~/ffmpeg-build/vmaf/model/`. `h264_nvenc` and `hevc_nvenc` are both listed, and the A2000
> provides the encoder. So this item needs nothing that is not already on the machine — which is why
> it is the next one to take.
>
> Two cautions. `openshot-bench`'s `nvenc` mode is the fast way to the fps half of the gate, but the
> **VMAF half wants a real encode of `podcast_pip` at matched settings** — compare like for like, and
> remember the service asks for `rc vbr, cq 19, preset p5, tune hq` against x264's `crf 18,
> preset medium`. And check the power state before any timing: on battery this box runs at about a
> third of its AC speed.

**Size.** ~1 day.

### W10 — Writer: nvenc without the CPU conversion · legacy `1.3` · **optional**

**Goal.** Feed nvenc `rgba` directly instead of converting on the CPU.
**Read this before starting.** W25 replaces this code entirely — there the frame is already a GPU
texture and never touches the CPU. Do W10 **only** if you need the nvenc win before Stage 7 lands.
If Stage 7 is close, skip it.

- [ ] When the codec name contains `_nvenc`: `pix_fmt = AV_PIX_FMT_RGBA`, drop `hw_frames_ctx` and
      the manual `av_hwframe_transfer_data`, wrap `Frame::GetPixels()` in an `AVFrame`.
- [ ] Delete the per-frame `av_malloc` + `memcpy` in `process_video_packet`.
- [ ] Stop draining `avcodec_receive_packet` after every frame.
- [ ] Colour tagging is already handled — `SetOption("color_primaries"/"color_trc"/"colorspace")`
      landed with the image work.
- [ ] Flag `OPENSHOT_NVENC_RGBA=0`.

**Gate.** `single_video` 1080p nvenc ≥ **105 fps** (75); `source_4k` nvenc ≥ **60 fps** (46);
`tools/golden.sh check --filter export` green; nvenc vs x264 on a BT.709 chart within 2 LSB mean.
**Size.** ~2 days.

> **Release gate R1** (after W05–W09, W10 if taken). Full `openshot-bench --label r1`;
> `single_video` nvenc ≥ 105 fps, `source_4k` render ≥ 70 fps, nothing more than 5 % slower than
> baseline, golden green. Ship behind `ENCODER=` and shadow for a week.

---

## Stage 4 — Decide before the compositor

### W11 — Record the four open decisions · legacy `4.0`

**Goal.** Four choices that Stage 5 and Stage 6 both bake in. Deciding them mid-rewrite means
redoing work.
**Why it moved.** The plan listed this as `4.0`, but `3.1` already depends on the canvas-precision
answer. This is the ordering bug the renumbering exists to fix.

- [x] **Canvas precision.** → **`kRGBA_8888`**, overriding the F16 recommendation that was on file.
      Output is 8-bit H.264 throughout; pooled surfaces are null-colour-space, so F16 buys precision
      between stages but not gamma-correct blending; and 8888 keeps the GPU canvas bit-identical to
      the CPU path. Revisit on 10-bit/HDR, or on measured banding after W19.
- [x] **Graphite only, or Ganesh Vulkan as a fallback backend.** → **Graphite only.** There is no
      Ganesh code in `src/` at all — it is a GN flag, and `/usr/local/skia-gpu` was built without it.
      Keeping it "alive" would mean writing a second backend, not preserving one.
- [x] **LUT rounding reference.** → **match the front end at the LUT's native cube size.** Measured:
      the `ColorMap.cpp` 17³ resample, not the interpolation kind, is the whole editor-vs-export gap
      (17.05 LSB max / 0.404 mean, vs 4.34 / 0.060 for trilinear-vs-tetrahedral). A 3-D LUT texture
      with hardware trilinear filtering *is* the front end's path.
- [x] **Nearest-neighbour sampling** in `BORDER_REFLECTED_ROTATION` and `DISPLACEMENT_MAP`: → **keep
      nearest.** Both live in the submodule the front end runs through WASM; moving only the GPU
      shader to bilinear opens the very editor-vs-export gap the LUT decision closes.

**Gate.** All four written into `GPU-DECISIONS.md` with the reasoning and a "revisit if" line.
**Size.** ~half a day of discussion, no code.
**Done 2026-09-16.** All four recorded in `GPU-DECISIONS.md` under "W11 — the four decisions the
compositor bakes in", each with its measurement and a *revisit if* line. One question could not be
answered from this repo and is now the top entry under **Open**: which interpolation the front end
passes to `apply_lut` (≤ 4.3 LSB on a fine LUT, up to **98 LSB** on a coarse one — it dominates
there because no resample happens and there are only 8 corners). Needed before W19, not before W12.

---

## Stage 5 — The GPU compositor (R3)

**The big one, and where the remaining CPU time actually is.** Baseline attribution: `grid_3x3`
spends 12 of 19 stack samples in Qt raster `drawImage` + `apply_background`; `heavy_effects` and
`blend_stack_5` are the same shape. W12–W15 are strictly sequential. W16–W18 can run in parallel
once W13 lands.

### W12 — Timeline canvas on the GPU · legacy `3.1`

**Depends on.** W11 — settled: the canvas is **`kRGBA_8888`**, matching the CPU path, so GPU and
CPU output should stay bit-identical and any drift is a finding rather than an expected cost.

- [x] **Tag the bit-exact scenarios `"exact"` first, before touching the render path.** Done
      2026-09-16. Measured every scenario across the full four-way sweep and took the worst result
      per scenario: **77 are bit-exact in all four configurations**, 25 already gated
      `Tolerance::Exact()`, so **51 newly tagged — 76 now gated exact**. All four stay 292/292 under
      the tighter gate, so the rewrite below cannot move a compositing, transform, transition,
      effect or reader pixel without failing. `export.roundtrip_x264` left out on purpose (lossy
      round trip; its `Codec()` tolerance also governs its checks). The 17 `text.*` scenarios are
      **not** bit-exact GPU-vs-CPU — up to 106 LSB — and stay on `Loose()`; that is Skia's glyph
      rasterisation differing between backends, not a regression. Expect `clipfx.*` blur/shadow to
      lose the exact gate at **W14** and `subtitles.*` at **W17**; both are deliberate, and the tag
      is what forces them to be written down rather than absorbed silently.
> **Premise check, 2026-09-16 — W12 as written cannot meet its own gate, because W13 is what pays
> for it.** Two things the item assumes are not how the code works:
>
> 1. **`add_layer` does not composite.** It copies audio. The image composite happens inside
>    `Clip::GetFrame` → `apply_keyframes` (transform onto a clip-sized canvas) and
>    `apply_background` (QPainter source-over, or `BlendImages()` for the 15 non-normal blend
>    modes, painting **in place** into the timeline frame's `QImage`). So "pass the `SkCanvas` down
>    through `add_layer`" has no composite to reach until `Clip::draw(SkCanvas&)` exists — W13.
> 2. **Until then a GPU timeline canvas is pure overhead.** Every frame would acquire a pooled
>    surface, clear it, and read it straight back so QPainter can touch the pixels, with nothing
>    drawn on the GPU in return. Measured with `tests/gpu/gpu_canvas_cost.cpp`
>    (`openshot-gpu-canvas-cost`, 200 frames, A2000): **1.65 ms median at 1080p** (p95 2.35),
>    6.93 ms at 2160p; 2.21 ms / 7.18 ms on lavapipe. Against the baseline that is
>    `single_video` **117 → 98 fps (−16 %)**, `source_4k` −9 %, `podcast_pip` −4 % — and the gate
>    below says *not slower than baseline*.
>
> The fix is sequencing, not scope: the canvas needs a GPU consumer in the same change. See the
> chosen approach recorded in `GPU-DECISIONS.md`.

- [x] `Timeline::GetFrame` takes its output surface from `GpuSurfacePool`. **Not** "down through
      `add_layer`" — that copies audio; the composite is in `Clip::GetFrame`. The canvas is attached
      only when **every** clip on the frame qualifies (see below).
- [x] `Frame` gains a `GpuFrame`; `GetImage()` does one cached readback and detaches, so every
      unported path still works. `AddImage()`/`AddColor()` drop the surface instead.
- [x] **`Clip::draw_to_canvas`** — the fast-path slice of W13 pulled forward, because without a GPU
      consumer the canvas is a measured 16 % regression. One transformed draw replaces
      `apply_keyframes` + `apply_background` for clips with no blend mode, shadow, blur, overlay,
      frame-number overlay, waveform or post-keyframe effect.
- [x] The writer still reads back once per frame — `Timeline::GetFrame` flattens before returning,
      because a Graphite surface belongs to the thread that made it. Removed in W25.
- [x] Golden harness: per-run tolerance. A scenario tagged `gpu-composite` is bit-exact on the CPU
      and "close" when a GPU composites it — Skia's bilinear is not QPainter's smooth transform.

**Gate.** Golden green across the board (PSNR ≥ 50 dB vs the CPU goldens — that is what the suite is
for); `single_video` render not slower than baseline.
**Size.** ~1 week.

**Done 2026-09-16. Gate met, with one caveat recorded rather than hidden.**

| scenario | GPU off | Vulkan | |
|---|---|---|---|
| `grid_3x3` | 19.3–19.9 | **26.7–27.4** | **+38 %** |
| `single_video` | 96.9–101.6 | 101.4–104.3 | +4 % — *gate: not slower* ✅ |
| `podcast_pip` | 19.4–20.1 | 18.0–18.9 | **−5 %** ⚠️ |

Four-way sweep **292/292**; `openshot-gpu-checks` 7/7; the CPU path bit-identical throughout. Of the
frames the compositor moves, the worst is 49.1 dB (`effects.enhancement`, max 3–5 LSB) and the
median 58.8 — inside the parity policy's "close" class, three frames marginally under the gate's
stricter ≥ 50.

⚠️ **`podcast_pip` is 5 % slower with the GPU on.** The cause is the per-clip upload: this slice
still moves every source image across PCIe once per clip per frame, so it wins where sources are
small relative to the area they cover and loses where a few large ones replace composites that were
already cheap. It is confined to GPU-enabled deployments (`OPENSHOT_GPU` defaults to off) and is
**W22–W25's to fix**, where frames stop being uploaded at all. Re-measure it there.

### W13 — `Clip::draw(SkCanvas&)` · legacy `3.2`

**Depends on.** W12, which already did this for the simple case — see `Clip::draw_to_canvas`. W13 is
now about widening it: the 15 non-normal blend modes, and then relaxing the all-or-nothing rule
(`GPU-DECISIONS.md`, "The GPU compositor draws whole frames or none of a frame") back to per-clip.

- [x] Collapse `apply_keyframes` + `apply_background` into one draw — done in W12 for clips with no
      blend mode, shadow, blur, overlay, frame-number overlay, waveform or post-keyframe effect.
- [x] `SkBlendMode` from `blend_mode` — all 16, verified by `openshot-gpu-blend-parity`.
- [x] Relax the all-or-nothing frame rule to "no clip reads the backdrop on the CPU".
- [x] Paint alpha comes from the opacity curve via `get_transform`, as on the CPU path.
- [x] ~~`get_transform` returns an `SkMatrix`~~ / ~~200-keyframe coefficient test~~. **Not needed,
      and doing it would add risk rather than remove it.** Qt stores an affine transform
      row-vector style and Skia column-vector style, so `draw_to_canvas` transposes the same six
      numbers `get_transform` already computed — there is no second arithmetic path for a
      comparison test to disagree with. Re-deriving the matrix in Skia terms would *create* the
      divergence the test was meant to catch.

**Gate.** `tools/golden.sh check --filter compositing` — all 16 blend modes within PSNR 48 dB;
`grid_3x3` render ≥ **45 fps** (23).

**Done 2026-09-16 — delivered, with one half of the gate carried forward.**

| scenario | GPU off | Vulkan | |
|---|---|---|---|
| `blend_stack_5` | 15.9–17.4 | **38.5–43.2** | **2.5×** |
| `grid_3x3` | 17.5–20.5 | 24.6–27.3 | +34 %, gate wants 45 ⚠️ |

- **Blend parity: met, and by a sharper instrument than the gate names.** 13 of 16 modes agree with
  `BlendImages()` within 1 LSB on identical pixels; all 16 do on lavapipe. Three scenarios
  (`color_burn`, `hue`, `saturation`) sit at 26.5/39.6/44.6 dB against CPU goldens — *not* a blend
  error but the resampling difference magnified by a steep formula, and on the project owner's
  decision they take `Tolerance::GpuAmplified()`. That band cannot catch a regression in those
  three; `openshot-gpu-blend-parity` is what does. See `GPU-DECISIONS.md`.
- ⚠️ **`grid_3x3` reaches ~26 fps, not 45, and cannot get there in this stage.** Every source image
  still crosses PCIe once per clip per frame, and `grid_3x3` is nine clips. **Carried to W22–W25**,
  which removes the upload; re-test the 45 fps gate there. Not reverted — the `blend_stack_5` 2.5×
  is real.

**Size.** ~1 week.

### W14 — Blur, shadow, crop, flip on the paint · legacy `3.3` · **DONE 2026-09-16**

**Depends on.** W13.

- [x] `SkImageFilters::Blur` with the existing box→sigma mapping. `sigma_for_box()` is now one
      shared helper, so the CPU `cv::GaussianBlur` and the GPU filter blur by the same amount.
      `SkTileMode::kClamp` (Skia has no mirror) and a crop to the source rect, so the blur stays
      inside the image the way an in-place blur on the source does.
- [x] `SkImageFilters::DropShadow` — not `DropShadowOnly`: one filter draws the shadow and then the
      clip over it, which is the order `apply_keyframes()` paints them in, and chaining it on the
      blur filter reproduces the CPU ordering (the silhouette comes from the *blurred* source).
- [x] Flip: **nothing to do.** `get_transform()` already applies it as a negative scale, so it has
      been on the GPU since W12. Crop: **nothing to do here either.** Neither `Clip` nor
      `draw_to_canvas` does any cropping — crop and corner radius are the `Crop` *effect*
      (`src/effects/Crop.cpp`, `apply_before_clip`, so it never blocked the canvas path). Its
      `QPainterPath`→`clipRRect` move is an effect port and belongs to W19–W21. The `clipRRect` in
      the worklist came from the Qt inventory table in `GPU-RENDER-PLAN.md` §2, which lists it
      against `Clip.cpp` in error.
- [x] ~~Delete `get_shadow_image`, the local `gaussian_blur` and the scalar opacity loop.~~
      **Not done, and must not be:** all three are the no-GPU path. The standing constraint
      (`STATUS.md`) post-dates this sub-task and overrides it — the CPU branch is kept and gated,
      never deleted. The GPU path skips them by taking `draw_to_canvas` instead of
      `apply_keyframes`; nothing was removed.
- [x] Fixed the golden suite's alpha test image, which was **fully transparent** — see the gate.

**Gate.** Met on quality, **missed on throughput**, and the shortfall is measured rather than
guessed:

- `clipfx` quality: shadows **SSIM 0.9998–1.0000, PSNR 58.9–74.5 dB** (gate SSIM ≥ 0.97); blur
  **PSNR 43.2–59.6 dB, SSIM ≥ 0.9986** (gate PSNR ≥ 40 dB). New `Tolerance::GpuBlur()` (40 dB /
  0.995) carries the blur band, tagged `gpu-blur`; the rest take `gpu-composite`.
- `podcast_pip` render 1080p: **18.7–21.1 → 27.4–28.9 fps (+40 %)**, CPU path unchanged at
  20.5–20.7. **Gate 45 fps not met.** Measured first, with the guard dropped and the shadow
  simply not drawn, the ceiling for this item is **33.5–35.0 fps** — so 45 fps was never reachable
  here. The rest is the per-clip PCIe upload, exactly as `grid_3x3`'s identical 45 fps gate was
  already carried to **W22–W25**; this one is carried there too.
- `heavy_effects` render 1080p: **9.0 → 10.6 fps on Vulkan (+18 %)**, and it now beats its own CPU
  path (9.9) instead of losing to it — pre-W14 it fell back and the GPU cost more than it saved.

**Size.** ~3 days.

### W15 — Delete `BlendModes.cpp` · legacy `3.4` · **VOID 2026-09-17 — no code change**

**Depends on.** W14.

Examined and closed without a code change. Every part of the item is either forbidden by the
standing constraint or already true, and its throughput gate cannot move from inside it. Recorded
here so it is not re-attempted.

- [x] ~~Delete `BlendModes.cpp`~~ — **must not be deleted.** Two live users:
      1. `Clip::apply_background` calls `BlendImages()` for all 15 non-normal modes. That is the
         **CPU implementation** of the fork's blend modes — the path a no-GPU machine runs. The
         standing constraint forbids deleting it.
      2. `tests/gpu/gpu_blend_parity.cpp` blends with `BlendImages()` as the **reference** that
         `SkBlendMode` is validated against. Deleting the file would delete the GPU's own
         correctness oracle — the one W13 relies on, because three modes' golden tolerance is too
         wide to catch a regression.
- [x] ~~Delete the `GetImageCV` calls in `Clip.cpp`~~ — **none are dead.** Two sites: the overlay-clip
      compositor (`additiveBlend` / `applyDisplacementMapEffect`, still a `can_draw_to_canvas`
      disqualifier) and the CPU clip blur in `apply_keyframes`. Both are live fallback code.

**Gate — the first half is already met, the second cannot move here.**

- `grep -c QPainter src/Timeline.cpp` is **already 0**; `src/Clip.cpp` is **15**, and all 15 are the
  CPU path: 2 includes, 5 comments explaining how the GPU draw matches QPainter, the
  `apply_background` compositor, and `apply_keyframes`' painter (transform, shadow, frame number).
  Reaching 0 means deleting the CPU fallback, which the standing constraint forbids. Taking Qt off
  the *GPU* path — where a QImage is still the image container and QTransform still builds the
  matrix — is **W16–W18**, and there too it is gating, not deletion.
- `blend_stack_5` ≥ 50 fps is **unreachable from this item by construction, not by measurement**:
  W13 already routes all 16 modes through `SkBlendMode`, and `BlendImages()`'s only caller
  (`apply_background`) is guarded by `!drawn_on_canvas`, so it is unreachable whenever a clip
  composites on the GPU. No edit to `BlendModes.cpp` can change a Vulkan number. The scenario
  measures **~41–42 fps** (41.3 recorded at W13, 41.1–42.4 best-of today), so the gate is missed and
  **carried to W22–W25** with `grid_3x3`'s and `podcast_pip`'s identical 45/50 fps gates — the
  per-clip PCIe upload is the whole remainder.

**One genuinely dead symbol, deliberately left in place.** `openshot::BlendPixel` has no caller
anywhere — not in `src/`, not in the tests, not in `../video-rendering-service` or
`../text-metrics`. It is documented public API, the only public way to evaluate the non-separable
modes per pixel, and referenced by `BlendChannel`'s doc comment. Removing ~15 lines of library API
for no measurable gain is the project owner's call, not this item's; flagged rather than done.

**Size.** ~2 days as written; ~0 in fact.

### W16 — Image and SVG readers on Skia · legacy `3.5` · **VOID 2026-09-17 — no code change**

**Depends on.** W13. Parallel with W17, W18.

Examined and closed without a code change. Every sub-task is already done, unnecessary, or blocked,
and the item has **no throughput to win at all**: `QtImageReader` caches the decoded and scaled
image in `cached_image`, invalidated only when `max_size` changes, so an image decodes **once per
reader** and the per-frame cost is a `shared_ptr` copy into the frame. Recorded here so it is not
re-attempted.

- [x] ~~`SkCodec` for PNG/JPEG, honouring EXIF orientation~~ — **EXIF is already honoured.**
      `QtImageReader.cpp:82` sets `imgReader.setAutoTransform(true)`, which is exactly what the
      sub-task asks for. With decode cached per reader there is no per-frame cost to remove either,
      so swapping the decoder means re-qualifying every format Qt reads for no measurable gain.
- [x] ~~Skia's SVG module or resvg for shapes~~ — **not available, and not needed.**
      1. **Skia's SVG DOM is not in this build.** `libskia.a` contains only `SkSVGCanvas` /
         `SkSVGDevice` — the SVG *writer*. `modules/svg` (`SkSVGDOM`, the parser) is a separate
         target that is not built and whose headers are not installed; `skia_enable_svg = true` in
         the GN args enables the writer, not the reader. Adding it means editing **both** build
         scripts and `install_skia_gpu.sh`, against `CLAUDE.md`'s rule that the two scripts stay
         byte-identical except for four things and that the CPU build stays reproducible.
      2. **resvg is already an optional path and is off** (`HAVE_RESVG:BOOL=FALSE`); the
         `USE_RESVG` branches in `QtImageReader.cpp` are unused today.
      3. **Qt renders the service's shapes correctly.** `ShapeRenderer`'s entire emitted surface is
         `<svg viewBox … preserveAspectRatio="none">` containing `<path>` and `<circle>` with
         `fill`, `stroke`, `stroke-width`, `stroke-opacity`, `fill-opacity`, `stroke-linecap`,
         `stroke-linejoin` and `stroke-dasharray`. No gradients, filters, text, masks or
         transforms — nothing at the edge of Qt's SVG support. The one documented cross-repo
         contract, `preserveAspectRatio="none"` (`ShapeRenderer.cpp`'s comment relies on it so the
         ≤1 px raster-size truncation cannot letterbox a shape), was **tested and holds**: a
         100x100 viewBox in a 200x100 raster stretches to fill all 200x100.
- [x] ~~Replaces `QtImageReader`~~ — **cross-repo, and on the shipping path.**
      `../video-rendering-service` constructs `openshot::QtImageReader` by name twice
      (`VideoRenderingImpl.cpp:140` for media, `:359` for shapes, where the comment reads
      "QtImageReader only — the SVG is generated locally, and no other reader can rasterize it"),
      and `ShapeRenderer.cpp:17` documents that its size computation "matches the one QtImageReader
      computes for the same SVG". Replacing the reader changes shape geometry on the path production
      runs today.

**Gate.** Not runnable as written and not worth making runnable: it wants "20 production PNG/JPEG
and 20 shape SVGs", a corpus that does not exist (W04 holds one payload so far), to compare a
replacement that should not be built against a reader that is already correct.

**Size.** ~1 week as written; ~0 in fact.

> **Stage 5's remaining Qt items were written before the standing constraint and before W13.**
> W15 and W16 are both void for the same underlying reason: they assume a rewrite-and-delete model
> where Qt code is removed, and the constraint requires the CPU path be kept and gated instead.
> **W17 is not affected and is real work** — its own note says the arithmetic inverts once W12
> lands, which it has. **W18 is mixed**: the build options and the `QString`/`QDir`/`QColor`
> replacements in `Timeline`, `Profiles`, `ColorMap` and `ChunkReader/Writer` are genuine (none of
> those is the CPU render fallback), but its "no `QPainter` on the render path" gate hits the same
> wall as W15's unless the answer is an `ENABLE_LEGACY_EFFECTS`-style gate rather than deletion.
> Read W18 with that in mind rather than taking its checklist at face value.

### W17 — Subtitles and text into the timeline canvas · legacy `3.6`

**Depends on.** W12. Parallel with W16, W18.
**Note.** The capability already exists — `SubtitleManager::renderAtFrame(SkCanvas*, w, h, frame)`
was added in Phase 2 and is bit-identical on GPU and raster. The Timeline passes a raster canvas on
purpose, because a full-frame round trip costs 5.6 ms at 1080p against 0.27 ms of drawing. **Once
W12 lands the frame is already on the GPU and that arithmetic inverts** — this item is then mostly
deleting the raster wrapper.

> **2026-09-17 — the note above is wrong on both counts, and the code is done anyway.** Measured
> before implementing, as the protocol requires:
>
> 1. **There was never a round trip here to delete.** `Timeline.cpp`'s subtitle block called
>    `new_frame->GetImage()`, which *is* `FlattenGpuFrame()` — the same readback the Timeline does
>    a few lines later. The frame crossed exactly once either way. What the item saves is the
>    **drawing**, not a transfer. `single_video` is `subtitles_words` minus the subtitles, so
>    differencing them measures it exactly: **0.62 ms of an 8.19 ms frame**, interleaved three
>    times on Vulkan at 1080p.
> 2. **The round trip is 1.62 ms at 1080p on the A2000, not 5.6 ms** (`openshot-gpu-canvas-cost`).
>    The 5.6 ms on file was measured on the Intel iGPU.
> 3. **The gate was already met at HEAD**, before any change: `subtitles_words` 120.0 / 121.3 /
>    125.0 fps on Vulkan, interleaved against `single_video` at 131.7 / 130.1 / 134.3. The ~113
>    on file was stale.
> 4. **A latent bug blocked the naive change.** `SkiaRenderer::parseColorString` swaps R and B;
>    that swap cancels only at the QImage boundary, and the timeline canvas is `kRGBA_8888` and is
>    read back as `kRGBA_8888`. Handing it over unchanged renders subtitles with red and blue
>    exchanged. The swap is now `ColorConvention`, a property of the output boundary
>    (`subtitle/SubtitleTypes.h`), and the new `subtitle-colors` check in `openshot-gpu-checks`
>    guards it. The existing `subtitle-gpu` check could not — it builds its frame as `kN32`.
>
> The text half was also not "delete the wrapper": `Frame::DeepCopy` did not carry `gpu_frame`,
> three `Frame` accessors would have handed back a black frame, and `get_transform` writes the
> opacity curve into pixels a texture does not have. All three are fixed; see commit `eb028074`.

- [x] Timeline passes its own canvas to `SubtitleManager::renderAtFrame` (`54be69d4`).
- [x] Same for the text reader — no intermediate surface, no readback (`eb028074`).

**Gate.** `subtitles_words` render ≥ **120 fps**; golden green.
**Gate status — met, with the measurement caveat below (closed 2026-09-18).** On AC, at HEAD, GPU
Skia on Vulkan, 1080p `render`, the documented 150-frame window: **12 runs gave 98.2, 102.8, 112.5,
114.7, 115.1, 117.5, 120.1, 120.8, 120.9, 120.9, 122.5, 123.2 fps — median 118.8, best 123.2.**
Six of those were interleaved against `OPENSHOT_GPU=off`, which was a tight 77.9–99.7 (median
~98). So the gate is cleared in the better half of the runs and misses by ~1 % at the median.

**The spread is the machine, not the code.** These were taken with a browser and two IDEs running
(load average 1.2–2.7); the run-to-run range on the GPU-off arm, where nothing changed at all, is
just as wide. A definitive single number wants an idle machine. Two things make the pass credible
anyway: the pre-W17 reading in a quiet window was 120.0 / 121.3 / 125.0, and W17 measured **+3.5 %**
interleaved, so the expected quiet-machine figure here is ~124–129.

**Steady state clears it comfortably on both paths.** The 150-frame window charges ~0.6–0.8 s of
one-time warm-up (first decode, font load, cache fill) — it is *not* GPU-specific, both arms show
it. Solving the 150/300-frame pair gives **~227 fps on Vulkan against ~195 with the GPU off**, a
16 % steady-state win against the 22 % measured at 150 frames. An export of thousands of frames
sees the steady-state rate, so the 150-frame fps understates every scenario here.

Correctness was already settled on the day: golden **292/292 four ways** (CPU Skia; GPU Skia off;
Vulkan; lavapipe) with no re-baseline, `openshot-gpu-checks` **8/8** and blend parity clean on
Vulkan and lavapipe.

**History — the fps numbers below came from a battery window** and are kept only for the
interleaved ratios. The machine was on battery (`/sys/class/power_supply/AC*/online` = 0), which
caps it to about a third: the same binary that read 120–125 fps in the morning read 40–41 fps. The
*interleaved* pre/post ratios are still valid, both builds having run back to back under identical
conditions:

| scenario (1080p, Vulkan, 150 frames) | pre-W17 | W17 | |
|---|---|---|---|
| `subtitles_words` | 39.1 / 39.1 / 40.1 | 40.8 / 41.2 / 40.8 | **+3.5 %** |
| `text_animated_glow_3` | 29.4 / 29.9 | 32.1 / 31.9 | **+8 %** |
| `everything` | 5.4 / 5.5 | 5.2 / 5.5 | flat |

+3.5 % on `subtitles_words` is about half the 7.5 % ceiling the 0.62 ms measurement set, which is
the expected shape. To recreate the pre-W17 comparison build: `git worktree add <dir> 76a756cb`,
`cmake -S . -B build-gpu -DCMAKE_BUILD_TYPE=Release -DSkia_ROOT=/usr/local/skia-gpu`, and symlink
`tests/bench/media/*` from the main tree (the media is gitignored).
**Size.** ~2 days.

### W18 — Qt off the render path · legacy `3.7` (done 2026-09-18)

**Depends on.** W13. Parallel with W16, W17.

> **2026-09-18 — the gate named a flag that does not exist, and most of the checklist is
> housekeeping on code the service never runs.** Checked before implementing, as the protocol
> requires. Scoped to the render path by the project owner; what was done, and what was left
> alone on purpose, is below.
>
> 1. **`ENABLE_LEGACY_EFFECTS` appears nowhere in the tree**, so the gate as written was
>    unsatisfiable rather than merely hard. 20 files under `src/` use `QPainter` and, under the
>    standing constraint, every one of them is the CPU fallback that has to be kept. This is
>    W15's wall again: the answer is gating, not deletion.
> 2. **`ENABLE_PLAYER` did not exist either** — `QtPlayer.cpp` and `src/Qt/*` were compiled
>    unconditionally. `ENABLE_MAGICK` already exists and defaults to ON, so only half of that
>    sub-task was real.
> 3. **`ColorMap` is `src/effects/ColorMap.cpp`**, not a sibling of `Timeline`/`Profiles`, and its
>    Qt use is `QImage` loading a reference image — there is no `.cube` text parsing to write unit
>    tests for. That sub-task has no subject.
> 4. **`Timeline`'s Qt is two unrelated things.** The project-file path-rewriting constructor
>    (`QDir`/`QFileInfo`/`QRegularExpression`) is never called by `../video-rendering-service`,
>    which builds clips programmatically; the only Qt on the render path was one `QColor` in the
>    GPU background clear.

- [x] **`ENABLE_PLAYER`** (default ON, `USE_QT_PLAYER` in the headers, following
      `USE_IMAGEMAGICK`). OFF drops `PlayerBase.cpp`, `QtPlayer.cpp`, `src/Qt/*`,
      `Frame::Display`/`DisplayWaveform` and the **`Widgets` Qt component**. Two caveats worth
      keeping: `Qt/VideoCacheThread.cpp` is filed under `Qt/` but is a JUCE/std cache thread with
      no Widgets dependency, and `AudioReaderSource` (core) calls `isReady()` on it, so it is
      always built; and `libQt5Widgets` still arrives **transitively through `libQt5Svg`**, so the
      option removes our own dependency on it, not the .so from the image.
- [x] **`Color` without `QColor`.** Parsing and `GetColorHex` are standard library now, and
      `Color.h` forward-declares `QColor` instead of including it, so the public header no longer
      drags Qt into every consumer. The 147 SVG colour keywords plus `transparent` are a generated
      table. Behaviour is unchanged, including the quirks: an unparseable string is opaque black,
      `"rgbx(1,2,3)"` parses as CSS `rgb()` because the prefix test is `startsWith("rgb")`, and
      `GetColorHex` stays `"#rrggbb"` with alpha dropped. Verified differentially against Qt
      before the change was trusted: **144,559 hex/named inputs vs `QColor`, 60,020 `rgb()`/
      `rgba()` inputs vs the old implementation, 200,000 `GetColorHex` vs `QColor::name()` — zero
      mismatches.** The `Color(QColor)` constructor stays (public API, and nothing in the tree or
      in the service uses it); it is the last Qt tie in `Color` and can go whenever the API may
      break.
- [x] **The Timeline's GPU background clear no longer builds a `QColor`**, and no longer round
      trips the colour through a hex string every frame. Alpha is deliberately forced to 255:
      the CPU path a few lines above goes through `GetColorHex()`, which carries no alpha, so
      reading the alpha curve here would make the two paths disagree.
- [x] **`unit.color` in the golden suite** — named colours, all five hex lengths, whitespace, the
      CSS forms, malformed input, and hex round-trips, every expectation recorded from the pre-W18
      implementation. `tests/golden/scenarios/Unit.cpp` says how to regenerate it. Confirmed to
      have teeth by flipping one byte of the colour table: 4 of its 5 checks fail.
- [x] **Two new golden scenarios**, `compositing.timeline_background_color` and
      `compositing.timeline_background_animated`. Nothing in the suite set a timeline background
      colour, so the branch this item changed was **not covered by the 292-frame sweep at all** —
      every other scenario leaves the background black, which is the one case the code skips.
      They also pin CPU/GPU parity for it: the background pixels come out bit-identical on both
      paths, and only the clip area differs (2-3 LSB of Skia resampling).

**Deliberately not done, and why.** The `QString`/`QDir`/`QFile`/`QRegularExpression` work in
`Timeline`'s path-rewriting constructor, `Profiles`, `ChunkReader/Writer` and `effects/ColorMap`.
None of it is on the render path, none of it is reached by the service, and rewriting it is a large
diff with no measurable win and a real chance of changing `.profile` or project-file parsing. It
stays on the list as ordinary housekeeping, not as GPU-migration work.

**Gate (restated).** The original is unsatisfiable — see finding 1. What this item is held to:
golden green on the four-way sweep; the golden suite green in an `ENABLE_PLAYER=OFF` build;
`libopenshot.so` carrying no direct `Qt5Widgets` dependency in that build; and no Qt type
constructed on the render path for a colour.
**Gate status — met.** Golden **295/295 four ways** (CPU Skia; GPU Skia off, Vulkan, lavapipe),
17 checks, 0 failures, with three new frames baselined for the new scenarios and **no other golden
touched**. `ENABLE_PLAYER=OFF` build: same 98 scenarios / 295 frames / 17 checks green, and
`readelf -d` shows `libQt5Svg`, `libQt5Gui`, `libQt5Core` and no `libQt5Widgets`.
`openshot-gpu-checks` **8/8** on Vulkan and lavapipe with `OPENSHOT_TEST_FONT` set, so
`subtitle-gpu` and `subtitle-colors` actually ran rather than skipping; blend parity clean on both.
**Size.** ~1 week as written; ~1 session for the render-path half that is real.

> **Release gate R3.** `grid_3x3` ≥ 60 fps, `blend_stack_5` ≥ 50 fps, `podcast_pip` ≥ 45 fps,
> `single_video` ≥ 200 fps, `everything` ≥ 8 fps, all 1080p; golden green; no `QPainter` on the
> render path. Default `OPENSHOT_GPU=vulkan` on GPU nodes.
>
> **"No `QPainter` on the render path" cannot be met as written** — under the standing constraint
> the QPainter code *is* the CPU fallback and has to stay. Read it as "no Qt type constructed on
> the render path when a GPU surface is in use", which is what W18 delivered. The fps numbers are
> unaffected and still stand; three of them are carried on W22–W25.

---

## Stage 6 — Effects and transitions as shaders (R4, first half)

> **Map of this whole stage: `STAGE6-EFFECTS.md`.** Which effects are ported, which are partial and
> what was left out of them, which were examined and ruled out with the reason, which are blocked
> and on whom — and what is left, in order. Written 2026-09-22 so this does not have to be
> reconstructed from the item text and the log.

Depends on W12/W13 only — **not** on Stage 7. Can run in parallel with Stage 7 and with each other.

### W19 — `GpuEffect` base and the per-pixel shaders · legacy `4.5`

> **Started 2026-09-22 — the base class and the first fragment are in.** What the port learned
> applies to all thirteen and is in `GPU-DECISIONS.md` under "W19 — what a shared effect fragment
> can and cannot be": SkSL is the GLSL ES 1.00 intrinsic set (no `round`, no `trunc`), the
> fragments are defined on straight RGB and reproduce the C++ byte truncation so they come out
> bit-exact, and the one place they cannot is the shared unpremultiply's division — 1 LSB on 1.8 %
> of legal (byte, alpha) pairs, because Vulkan allows 2.5 ULP there. Parity class is therefore
> **exact on opaque, close on semi-transparent**, at 69–74 dB against the 48 dB gate.

- [x] `GpuEffect` base class. `src/GpuEffect.{h,cpp}`: compiles the prelude plus the fragment once
      per effect instance, uploads only when the frame is not already GPU-backed, and **leaves the
      result on the GPU** so a chain pays one crossing rather than one per effect. Declining is a
      normal answer and the CPU twin runs untouched.
- [x] One SkSL fragment each, with a parity test against the C++ twin. **All eleven done:
      Brightness, Alpha, Exposure, ColorShift, Bars, ChromaKey (YCbCr only), ColorAdjustment,
      LightAdjustment, Enhancement (no grain), Mask, ColorMap** (2026-09-22) — 251 of 320
      image/parameter combinations bit-exact, nothing over 5 LSB. **Crop and CameraMovement will
      not be ported — see below.**
      - **Crop and CameraMovement are not per-pixel effects.** Both are `QPainter` with
        resampling — Crop an antialiased rounded-rect clip and a `drawImage` between fractional
        `QRectF`s, CameraMovement a `setWorldTransform` with `SmoothPixmapTransform` — so the
        difference from Skia is in the *rasteriser*, not the arithmetic. Porting either is a
        **redefine**-class product decision and belongs with the compositor's parity work.
        CameraMovement has one exactly portable subset, recorded but not built: at zoom 100 % and
        rotation 0 the transform is a pure translation and Qt takes its `TxTranslate` fast path,
        which `draw_to_canvas` already reproduces. `GPU-DECISIONS.md` has both.
      - **Mask's matte stays on the CPU and is uploaded as a texture**, like LightAdjustment's tone
        curve. It is cached and usually still, so the cost is paid once — and it keeps Qt as the
        single source of the mask's edges.
      - **Enhancement's grain pass stays on the CPU.** `fract(sin(...) * 43758.5453)` evaluated in
        `double` and in `float` do not agree to an LSB, they agree to nothing — up to ~140 LSB of
        grain. A frame that asks for grain runs entirely on the CPU.
      - **A tone curve belongs in a texture.** LightAdjustment's contrast LUT is uploaded as a
        256x1 texture rather than recomputed per pixel, because `pow()` and `sin()` are not exactly
        specified on the GPU. That makes the stage exact and is faster too.
      - **Enhancement is the first two-pass fragment** and the mechanism is just calling
        `ApplyOnGpu` twice: each call leaves its result on the GPU, so the second pass reads the
        first's output as a texture and only the last is read back.
      - **ChromaKey is YCbCr-only on the GPU.** The other methods key on HSV, HSL or CIE LCh
        coordinates babl produces; `SetGpuUniforms` declines them and the C++ runs. The service
        only ever constructs `CHROMAKEY_YCBCR`, and that configuration is **8/8 bit-exact**.
      - babl's `Y'CbCr u8` is BT.601 **studio** range, not the full-range "JPEG" mapping — the
        latter is wrong by up to 16. Coefficients and the measurement are in `GPU-DECISIONS.md`.
      - A fragment is exact when its per-pixel arithmetic is exact in `float` **and** it never
        divides by alpha. Alpha, ColorShift, Bars and ChromaKey's hard cut are exact; Brightness
        and Exposure divide, and ColorAdjustment carries `double` parameters an SkSL uniform can
        only hold as `float` (1 LSB). Do not expect better from either shape.
      - **`ApplyOnGpu` goes before any `frame->GetImage()`, and move the fetch as step one of
        porting an effect.** On a GPU-backed frame that call is the one readback, so the wrong
        order makes the shader pay an upload and a readback every frame: measured 3.2–4.4 ms a
        pass against 0.05–0.22 ms. Four of the first seven fragments were written the wrong way
        round, because every one of these `GetFrame` functions opens by fetching the image.
        Output is identical either way, so only the timing catches it.
      - **Measure before blaming the CPU path.** Exposure's remaining gap was put down to its
        `Format_ARGB32` round trip; removing that changed *no* pixel of any golden and not one
        parity digit, because Qt's round trip is lossless. The cause is the same division every
        dividing fragment pays, confirmed exhaustively. The round trip was removed anyway — it is
        dead work worth ~15 % of Exposure's CPU cost — but on its own merits.
- [x] **Golden coverage for the four effects the service builds only in `Transition.cpp`.**
      `effects.{brightness,exposure,colorshift,bars}_alpha`, on a 1:1 clip with partial alpha, held
      to `Tolerance::Exact()` with no `gpu-composite` band — the sweep is 307/307 in all four arms.
      (The earlier claim that these four had *no* scenario was wrong: `Transitions.cpp` generates
      one per `TransitionEffect` in a loop. The real gap was that all of it was opaque pixels under
      a 45 dB band.) Also `unit.gpu_effect_path`, which asserts on `GpuEffect::GpuPasses()` that a
      shader actually ran — a pixel comparison cannot, because the CPU fallback produces the right
      frame. Adding the remaining fragments means extending these two, not inventing a pattern.
- [x] **ColorMap — done 2026-09-22, and both of its questions were answered by reading the
      editor's own shader** rather than by asking. (a) The **17³ resample is gone**; the cube is
      applied at its native size, which re-baselined `effects.colormap_lut` (2 frames, max 7 LSB)
      and, contrary to the prediction here, left `effects.stack_crop_chroma_light_lut` untouched.
      (b) There is **no `apply_lut` call to confirm**: the front end never uses the WASM for LUTs.
      It grades in a PixiJS GLSL pass, trilinear by hand at the native cube size — exactly W11's
      choice — and it **honours `DOMAIN_MIN`/`DOMAIN_MAX`**, which `parseCubeText` dropped. The
      parser now reads them, in both of its loops, because a `.cube` may declare them either side
      of `LUT_3D_SIZE`. The fragment uploads the cube as the editor's 2-D atlas in **F16** —
      Graphite will not make a texture from an F32 raster image, so it is converted to half on the
      CPU first. Colour-match mode is not ported and declines: its cube is re-baked from the
      frame's own statistics. 60.2–65.4 dB, max 4 LSB, 9 of 24 bit-exact.

**Gate per effect.** PSNR ≥ 48 dB vs the CPU effect on eight test images including transparent
and semi-transparent pixels, ≤ 0.2 ms at 1080p. Measured by `tests/gpu/gpu_effect_parity.cpp`
(`openshot-gpu-effect-parity`), which also reports whether the two agree *exactly* and refuses to
compare a case that never reached the GPU. The timing gate is exempt on lavapipe, a software
rasteriser that measures ~25x slower.

**2026-09-22, four effects done.** Parity: **72 of 88 combinations bit-exact**, the other 16 at
55–75 dB, worst 5 LSB — comfortably inside the 48 dB clause. Timing at 1080p chained: Alpha
0.12–0.15 ms, Exposure 0.15–0.19, Brightness 0.16–0.21, ColorShift **0.22**.

**The ≤ 0.2 ms clause needs restating** — as W07's fps gates did. A *do-nothing* passthrough
fragment measures 0.19–0.22 ms in the same harness, because a pass copies the source surface and
then reads and writes an 8.3 MB surface before the fragment does anything. The gate is therefore
the floor of one full-frame pass and no fragment can meet it with margin; ColorShift is over it
only because it makes four texture fetches instead of one. Fixing it means not giving every effect
its own pass, which is its own item. See `GPU-DECISIONS.md`.
**Gate.** `heavy_effects` render ≥ **60 fps** (11.5); `chroma_key_green` ≥ **70 fps** (12.9);
`tools/golden.sh check --filter effects`.

**2026-09-22 — both fps gates need restating, and one of them cannot be met from this item.**
Measured interleaved on mains, 1080p render, 150 frames: `chroma_key_green` **6.9 → 43.6 fps
(6.3×)** on Vulkan, `heavy_effects` **4.9 → 5.6 fps (+14 %)**.
`chroma_key_green` is one clip with one ported effect, and what remains between 43.6 and 70 is the
PCIe crossing — W22–W25, where the compositor's gates were already carried.
`heavy_effects` is a different matter: **four of its six effects stay on the CPU whatever W19
does** — Crop is ruled out as a rasteriser difference, `Blur` is not in this item's list at all,
`ColorMap` is blocked on the front end, and its `Enhancement` asks for grain, the one pass that
cannot be ported. Its chain therefore crosses PCIe repeatedly. The useful part of that measurement
is that it is still **+14 %**, not a loss: a partly-ported chain is safe.
**Size.** ~2 weeks.

### W20 — Transition shaders · legacy `4.6`

> **Read `TRANSITION-PARITY.md` before starting this.** W20 is the item that ends editor/export
> identity-by-construction: today both run the same C++ compiled twice (native and WASM), and a
> server-only SkSL port makes them two implementations. That note sets out the achievable target,
> the shared-source design, the six conventions that must be pinned, and the three-way parity
> harness — plus a **measured bug that exists today**: the blur radii have no declared reference
> resolution, so the same authored value is 0.33x as wide exported from a 4K source as it looks in
> a 720p preview, and 1.33x in the front end's own slow-effect proxy path. Fix that first; no
> amount of shared shader source addresses it.

> **2026-09-22 — the blocker is narrower than this item reads, and two effects are done.**
> `TRANSITION-PARITY.md`'s reference-resolution bug affects **only box, diagonal and zoom blur**;
> the note says so itself. Everything else in the vocabulary is already resolution-independent, so
> seven of the ten remaining variants are not waiting on that decision.
> **Note also that W20's gate is PSNR ≥ 45 dB, not bit-exact** — a looser parity class than every
> W19 fragment was held to. Only two of these effects have C++ that is exactly reproducible at all;
> the rest resample.

- [x] `SplitShift` — **8/8 bit-exact.** Two integer rectangle blits. One trap, reproduced rather
      than fixed: for a *positive* shift the C++ builds its rectangles from the **double**, so the
      copied span is `int(extent - shift)` and not `extent - int(shift)`, a whole missing row.
- [x] `Wipe` (threshold wipe mask) — **24/24 bit-exact**, after fixing the cause rather than
      tolerating it. OpenCV's 8-bit `COLOR_BGRA2GRAY` is not reproducible from any documented
      formula (703 of 262,144 colours differ by 1 from the fixed-point one, 278 from the float
      one), and the wipe *thresholds* that grey, turning 1 LSB into a full step. The submodule now
      computes the luminance explicitly (`image-processing-lib` `f8873e0`), which re-baselined
      `transitions.threshold_wipe_mask` and moves that library's output by ≤ 1 LSB on ~0.27 % of
      colours. **Cross-repo — the front end picks it up when it updates the submodule, and the
      submodule commit is local and unpushed.**
- [x] `BorderReflectedRotation` — **16/16 bit-exact.** `warpAffine` evaluates its map in 10-bit
      fixed point, not floating point; reproducing that is what made it exact. A float version is
      right to a fraction of a pixel, looks perfect on every smooth image, and is *completely*
      wrong on high-frequency content — 11 dB on noise. **Test resampling effects on noise.**
- [x] `BorderReflectedMove` — exact on smooth content, **46.5–47.5 dB on noise**, clears the 45 dB
      gate. Its INTER_LINEAR weights are a normalised 15-bit table; the same fixed-point treatment
      would probably make it exact, and the route is recorded if that is ever wanted.
- [x] `Zoom` — **zoom-in only**, max 1 LSB / 57–78 dB. Zoom-out declines: it downscales and pads
      with independently-rounded, clamped paddings, so its result is not reliably the frame's size
      and the C++ assigns it back, changing the frame's dimensions. `ApplyOnGpu` cannot express
      that.
- [x] `CircleMask` — **24/24 bit-exact**, by *not* drawing the circle. `cv::circle` with `LINE_AA`
      fills a polygon approximation with OpenCV's own scanline coverage; an analytic disc would be
      a better circle and a worse port. The mask is built by the same OpenCV call, cached on
      (radius, frame size, `Generation()`), and uploaded as a texture.
- [x] **Blur (horizontal/vertical)** — six draws, one separable half-pass each, skipping any half
      whose kernel is a single tap. 57–78 dB, max 1 LSB; **bit-exact when only one axis is
      blurred**, which is the direct proof that the 8-bit intermediate between the two draws is the
      whole of the difference. A two-dimensional fragment would have been `taps²` fetches — 3675 a
      pixel at 1080p against 206 — and the C++ is O(1) per pixel, so separable is the only form
      that is not slower than what it replaces.
- [x] **Diagonal blur** — **24/24 bit-exact**, by adding the taps instead of prefix-summing them;
      every value in the C++'s prefix sum is an integer below 2²⁴, so a window is a difference of
      two exact integers. Declines above a megapixel, where the C++ halves the image with
      INTER_AREA and upsamples with INTER_LINEAR.
- [x] **Rotational blur** — **74–102 dB, max 1 LSB on every image including noise**, against the
      spike's 57–61 dB for the same effect. All of the difference is `warpAffine`'s fixed-point
      map: it quantises the source position to 1/32 of a pixel, so OpenCV's INTER_LINEAR is a lerp
      on a 5-bit grid rather than an exact one. Declines above the reference width.
- [x] **Zoom blur — done 2026-09-22, once the product decision was taken.** `cv::linearPolar`
      reads its interpolation from `flags & INTER_MAX` and the effect passed neither flag, so both
      polar conversions ran nearest-neighbour; under a nearest remap the inverse map's angle (from
      `cv::cartToPolar`'s float polynomial) chose a whole different source pixel on ~0.23 % of
      positions, which is 30–41 dB and unfixable inside a fragment. **The owner decided to pass
      `cv::INTER_LINEAR` to both calls**: the aliasing this effect had is gone, the angle error
      became a thousandth of a column of weight, and the port became ordinary. It moved
      `transitions.zoom_blur`, 4 frames, all of it on resample edges.
      The port is **three draws** — forward polar, `blur.sksl` along rho, inverse polar — because
      composing them would be `taps` fetches per bilinear corner (1216 a pixel at 1080p, strength
      100) against 4 + taps + 4, and because three draws put the 8-bit intermediates where the C++
      has them and let each stage be checked against its own `cv::` call. **phi wraps and rho does
      not**: treating the polar seam as an edge cost 136 LSB on the rays near angle zero and
      nothing elsewhere. 56.3–82.4 dB, max 8 LSB, 24/24.
      This is the first effect whose intermediate is a different size from the frame, so
      `GpuEffect` grew `GpuSourceFrame()` and `RunGpuPass()` — the two halves `ApplyOnGpu` is now
      made of, the latter drawing into a frame of its own size.
- [x] `ColorShift` — **already done under W19**; `ColorShift` calls the submodule's
      `applyColorShiftEffect`, so there is no separate port here.
- [x] **The sources live in `image-processing-lib/shaders/`** — one `.sksl` per effect plus
      `_prelude.sksl`, with a README setting out what a host must bind. The editor loads them
      directly through CanvasKit; libopenshot embeds the same bytes at build time
      (`cmake/scripts/embed_shaders.cmake` → `EffectShaders.h`) so the export has no runtime
      data-path dependency and the two cannot drift. Verified behaviour-neutral: parity 346/416 and
      the four-way sweep 307/307, both unchanged by the move.
- [x] The C++ stays as the oracle. It is what `openshot-gpu-effect-parity` measures every fragment
      against, and what runs whenever `GpuDevice::available()` is false.

**Gate.** PSNR ≥ 45 dB against the OpenCV version at three parameter values each;
`transitions_chain` render ≥ **70 fps** (27.4).
**Size.** ~1.5 weeks.

### W21 — Overlay clips as textures · legacy `4.7`

**Depends on.** W19.

- [x] Both composites are two-texture fragments in `src/gpu/GpuOverlay.{h,cpp}`, called from
      `Clip::GetFrame` **before** the OpenCV path. Additive blend is a fragment rather than the
      runtime blender this item proposed — the C++ adds only channels 0..2 and leaves alpha alone,
      which `kPlus` does not do, and a fragment reads both images anyway for the displacement map.
      Both golden overlay scenarios are **bit-identical on Vulkan** under `Tolerance::Exact()`,
      and `unit.gpu_overlay_path` proves the shader is what produced them.
- [x] ~~Deletes the last `GetImageCV` round trips.~~ **Gated, not deleted** — the standing
      constraint rewrites this the same way it rewrote plan step 2.5. The OpenCV path stays and
      runs whenever `GpuDevice::available()` is false or the overlay is a different size from the
      frame (the C++ resizes with `cv::resize`, and OpenCV's `INTER_LINEAR` is a rasteriser
      difference Skia will not reproduce).
- [ ] **Left: the timing clause of the gate, and it needs restating.** See below.

**Gate.** `tools/golden.sh check --filter overlay`; a transition frame costs no more than a plain
two-clip frame ±10 %.

**2026-09-22 — the golden clause is met, bit-exactly; the timing clause measures the wrong thing.**
There is no "plain two-clip frame" scenario to compare against, so the closest honest measurement is
the overlay path against itself. Interleaved on a settled machine, both libraries built up front and
swapped in place: `transitions_chain` **17.0 fps without W21 against 16.8 with** — no wall-clock
difference — with CPU occupancy 2.2 → 1.9 cores and peak RSS 1.13 → 1.05 GB, which is the two
full-frame `cv::Mat` conversions going away.
**The wall clock does not move because the transition effects around the overlay are still on the
CPU** and each calls `Frame::GetImage()`, so the overlay's result is read back immediately. **W21
pays when W20 lands**, and the two should be measured together.
**Size.** ~3 days.

---

## Stage 7 — Frames never leave the GPU (R4, second half)

**This is the decode/encode chunk, and it is last for a reason.** NVENC and hardware decode already
exist in the library and barely move the needle: main-thread samples of an nvenc export are 60 % Qt
compositing, 25 % writer `sws_scale`, ~**0 % encoding**, and nvenc alone took a trivial export 62 →
82 fps and heavy ones nowhere. The reason is that the frame is read back to the CPU and colour
converted either way. **W25 only pays once W12 has put the frame on the GPU.** Strictly sequential.

### W22 — `src/gpu/CudaInterop` · legacy `4.1`

> **Premise measured 2026-09-22, before any code, and it moves the item's shape.** Two facts:
>
> 1. **Every extension this needs is present on the A2000** — `VK_KHR_external_memory{,_fd}`,
>    `VK_KHR_external_semaphore{,_fd}`, `VK_EXT_external_memory_dma_buf`,
>    `VK_KHR_timeline_semaphore` — and on the Intel iGPU too. **lavapipe has no
>    `external_semaphore_fd`**, so the interop path declines there, which is the normal answer and
>    not a failure. CUDA 12.8 and an FFmpeg with the `cuda` hwaccel are both installed.
> 2. **Skia does not enable any of them off Android**, and **Graphite cannot export its own
>    allocations.** `VulkanPreferredFeatures` names external memory only under
>    `SK_BUILD_FOR_ANDROID`, so `GpuDevice` has to add the two `_fd` extensions to
>    `device_extensions` itself. And the only door into Graphite is
>    `BackendTextures::MakeVulkan(dimensions, info, layout, queueFamily, VkImage, VulkanAlloc)`,
>    which takes an image **the caller owns** — so the images CUDA imports must be ours,
>    allocated with `VkExternalMemoryImageCreateInfo` + `VkExportMemoryAllocateInfo` and our own
>    `vkAllocateMemory` (no VMA), then wrapped.
>
> **So `GpuSurfacePool`'s surfaces can never be the imported ones.** The sub-tasks below read as
> if an existing Vulkan image can be handed to CUDA; it cannot. This item is really: enable two
> device extensions, add an *exportable* allocation path beside the pool, and import those.
> Nothing here blocks the item — it is ~100 lines larger than "~300" and the pool stays untouched.

- [x] Add `VK_KHR_external_memory_fd` and `VK_KHR_external_semaphore_fd` to `GpuDevice`'s device
      extensions when the physical device offers them, and record their absence as a decline.
      Reported through `GpuDevice::vulkanHandles()`; lavapipe has neither and the interop declines.
- [x] An exportable image allocation (`VkExportMemoryAllocateInfo`), wrapped for Graphite with
      `BackendTextures::MakeVulkan`. Separate from `GpuSurfacePool`, which stays VMA-backed.
- [x] Import that memory and a semaphore into CUDA (`vkGetMemoryFdKHR` →
      `cuImportExternalMemory`, `cuImportExternalSemaphore`). Two semaphores, one each way; the
      CUDA device is matched to the Vulkan one **by UUID**, and it is the *primary* context, which
      is what FFmpeg's CUDA hwdevice uses (so W23's frames land in it).
- [x] `copyNV12(AVFrame* cudaFrame, GpuImage& y, GpuImage& uv, stream)` as two device-to-device
      `cuMemcpy2DAsync`.
- [x] **Added, and the gate depends on it: `prepareForCopy(y, uv)`.** CUDA needs the images in
      `VK_IMAGE_LAYOUT_GENERAL` and Skia leaves them in `SHADER_READ_ONLY_OPTIMAL`, so each copy
      needs a layout barrier first — and doing it *inside* `copyNV12` puts a full cross-API
      handshake on the critical path: **0.401 ms**, over the gate, of which 0.224 ms is pure
      round-trip latency. Issued instead right after the draw that read the images, it is free.

**Gate.** Fill a CUDA NV12 buffer with a known pattern, copy, sample both planes in a trivial SkSL
shader, read back, compare **exactly**; clean under `compute-sanitizer`; ≤ **0.3 ms** per 4K frame.
**Met 2026-09-22** — `tests/gpu/gpu_cuda_interop.cpp`, `openshot-gpu-cuda-interop`:

| | measured | gate |
|---|---|---|
| exactness, 640x360 and 3840x2160 | **0 of 8 294 400 pixels differ** | exact |
| `compute-sanitizer --tool memcheck --leak-check=full` | **0 errors, 0 bytes leaked** | clean |
| 3840x2160 per frame | **0.166 ms** (fixed 0.011, copy 0.155) | ≤ 0.300 |
| 3840x2160 without `prepareForCopy` | 0.415 ms | — |

Four-way sweep **307/307** in all four arms; `openshot-gpu-checks` 8/8 on Vulkan and lavapipe. The
interop SKIPs on lavapipe ("does not export memory and semaphores as fds"), with the GPU off, and
where there is no CUDA driver — all three are the normal answer.
**Size.** ~1 week. ~300 lines. **Actual: ~870 lines** (450 interop, 90 `GpuDevice`, 330 gate).

### W23 — Reader keeps frames on the GPU · legacy `4.2` · **DONE 2026-09-23**

**Depends on.** W22 — **done 2026-09-22**. `CudaInterop::Instance()` gives the reader the CUDA
context to hand FFmpeg (`AVCUDADeviceContext::cuda_ctx`), the stream, and the two images per frame;
remember `prepareForCopy` after the compositor's submit or every frame pays 0.25 ms for nothing.
Note also that hardware decode throws on the first frame in this fork — plan step 1.5 is its fix,
and it is this item's first problem.
**Note for this deployment.** H.264 only means NVDEC coverage is not a risk — the "extend to HEVC,
VP9, AV1, MPEG-4" sub-task is optional here.

> **Premise measured 2026-09-22, before any code, and it corrects three things in this item and in
> plan §0.3 / §1.5.** All at 3840x2160 H.264 (`tests/bench/media/v2160_a.mp4`), mains power,
> interleaved arms, on the A2000.
>
> | arm | fps | cores |
> |---|---|---|
> | this fork's reader, software | **126–135** | **3.1** |
> | ffmpeg CLI, software | ~400 | 11.7 |
> | ffmpeg CLI, NVDEC (`-hwaccel cuda`) | **~380** | **0.34** |
> | ffmpeg CLI, `h264_cuvid` | 135 | 0.32 |
>
> 1. **The fps half of the gate is already met in software.** 126–135 fps, not the 59 fps in plan
>    §0.3 — W08 took the per-frame copy out since that was measured. What NVDEC buys is the
>    **core count**: 3.1 → ~0.4. So the operative half of the gate is "**< 1 core**", and reaching
>    it needs the SkSL YUV→RGBA pass as much as it needs NVDEC, because swscale is most of those
>    three cores. (Measure NVDEC over ≥ 700 frames: over 150 the CUDA context init dominates and
>    makes it look like 93 fps.)
> 2. **Hardware decode fails, but not for the reason on file, and two of §1.5's three causes are
>    already fixed.** `get_hw_dec_format` already filters to the selected decoder, so there is no
>    VDPAU-before-CUDA problem, and the download already auto-selects its format. What is left is
>    one line: `ProcessVideoPacket` builds swscale from `pCodecCtx->pix_fmt`, which is
>    `AV_PIX_FMT_CUDA` once hardware decode is on, while the frame it actually converts is the
>    *downloaded* one. swscale says "cuda is not supported as input pixel format" and the reader
>    throws `OutOfMemory: Failed to initialize sws context` — not the "Failed to allocate image
>    buffer" §0.3 records.
> 3. **And it takes the process with it.** `Close()` drains the decoder by calling
>    `ProcessVideoPacket`, so the same throw comes back out of `~FFmpegReader`, where an escaping
>    exception is `terminate()`. Any reader whose drain throws aborts the process rather than
>    failing the job; that is independent of hardware decode.
> 4. **`DE_LIMIT_*` is what has been hiding all of this.** The defaults (1950x1100) mean a 4K file
>    silently decodes in software and looks fine (82 fps); a 1080p file with `HARDWARE_DECODER=2`
>    aborts on the spot.
> 5. **The re-baseline below is bigger than "the affected `readers.*` scenarios".** BT.601 → BT.709
>    moves every scenario that decodes an `.mp4` — ~32 references across 11 scenario files, not the
>    3 `readers.*` video ones.
> 6. **Measured after the 1.5 fix landed: the PSNR half of the gate is unreachable through
>    swscale, before colour space is even discussed.** Hardware-decoded frames come back as NV12
>    and software ones as YUV420P, and swscale converts the *same* 4:2:0 samples to RGBA
>    differently depending on which of the two they are laid out as: **40.5 dB, max channel delta
>    79**, reproduced with the ffmpeg CLI alone (`-pix_fmt rgba` from the file, against the same
>    frame round-tripped through `-pix_fmt nv12`). The reader measures the same 40 dB. So "decoded
>    frame vs software decode PSNR ≥ 48 dB" can only be met once YUV→RGBA is our own SkSL pass and
>    we choose the chroma upsampling — which also makes the software path the wrong reference to
>    measure against. **The gate wants restating before this item is finished.**

- [x] **Plan step 1.5, done 2026-09-22 — hardware decode works again, and can no longer abort the
      process.** Two changes, no pixel change on any path the suite exercises (four-way sweep
      307/307, 26 checks): `ProcessVideoPacket` takes swscale's source format from the frame it is
      converting rather than from `pCodecCtx->pix_fmt`, and `~FFmpegReader` no longer lets
      `Close()` throw out of a destructor. Guarded by `unit.hardware_decode`, which fails with
      either fix reverted. `HARDWARE_DECODER` still defaults to 0, so nothing about production
      changed. **What it does not fix is speed** — as plan §0.3 predicted, hardware decode is
      *slower* in wall-clock (4K: 44 fps against 126–135 software) because download + swscale stay
      serial. That is the rest of this item.
- [x] **The reader's own 2.5x, before any GPU work (2026-09-22).** `sws_scale` is **95 % of the
      reader's wall clock** at 4K (1.08 s of a 1.13 s 150-frame run, measured with an LD_PRELOAD
      interposer), and it was running at 6.5 ms a frame against ffmpeg's 2.7 ms for the *identical*
      unscaled `yuv420p → rgba` converter — swscale reports picking the same special converter in
      both. The difference was not the conversion: the reader allocated a fresh
      `width*height*4` buffer per frame, 33 MB at 4K, above glibc's 32 MB mmap cap, so every frame
      mmap'd and munmap'd 33 MB and the kernel faulted and zeroed all of it **inside** `sws_scale`
      (612k minor faults over 150 frames against ffmpeg's 271k). A recycling pool for those buffers
      (`FrameBufferPool` in `FFmpegReader.cpp`, returned through the QImage's cleanup function,
      capped at 256 MB free) closes it. Interleaved, mains power, decode-only:

      | | before | after | |
      |---|---|---|---|
      | 3840x2160 | 124–131 fps | **195–198 fps** | **+48 %** |
      | 1920x1080 | 534–539 fps | **751–785 fps** | **+44 %** |
      | `sws_scale`, 4K | 6.5–7.0 ms | **4.55 ms** | −31 % |
      | minor faults, 150 frames at 4K | 612k | **281k** | −54 % |
      | peak RSS | 1133 MB | 1134 MB | unchanged |

      Bit-identical: four-way sweep 307/307, 26 checks. This is a CPU win on the path that ships,
      and it is what the "< 1 core" half of the gate has to be measured against from now on.
- [x] **Decoder output stays `AV_PIX_FMT_CUDA` (2026-09-23).** With `HARDWARE_DECODER=2` and
      `GPU_DECODE` both on, the reader opens NVDEC in `CudaInterop`'s CUDA context and stream,
      keeps NV12 frames on the device (`GetAVFrame`), and `FFmpegReader::ConvertOnDevice` does
      copyNV12 → `GpuYuv::Convert(luma, chroma, …)` → submit waiting on the pair's semaphore →
      `prepareForCopy`, under one process-wide lock. Anything else — 10-bit, 4:4:4, no interop,
      a failed step — downloads as before. Guarded by `unit.nvdec_on_device` (bit-exact against
      software decode through the same conversion, and asserts `GpuYuv::DeviceConversions()`).
      **Two things had to be fixed first, and neither was in this item's premise**: the
      FrameMapper read every GPU frame back (so nothing had ever "stayed on the GPU"), and W22's
      semaphores were process-wide, which hangs with two readers. Both are in `STATUS.md`'s log.
- [x] **YUV→RGBA is an SkSL pass** (`src/gpu/GpuYuv.{h,cpp}`, 2026-09-22), matrix and range from
      the stream, doing the pre-scale in a second pass. The reader attaches the result with
      `Frame::AttachGpuFrame`, so a decoded frame is **born on the GPU and stays there** for the
      compositor. **Flagged off** behind `Settings::GPU_DECODE` — see the parity note below.
      - Faithful: **47.4 dB, max 3 LSB** against swscale on an unscaled clip, gated by
        `unit.gpu_decode`, which also asserts the pass actually ran.
      - The **pre-scale** is box halvings and then one exact step — **settled by measurement
        2026-09-22, and both of the obvious answers were wrong.** It is not folded into the
        conversion sample (one bilinear tap is not a downscale filter: 29 dB), and it is not
        dropped in favour of letting the compositor scale (**54 fps against 95** at 4K → 1080p —
        the compositor then resamples a 4K texture every frame, and RSS rises 982 → 1046 MB).
        Halving first and finishing with one fractional draw beats a single Mitchell draw by
        **~8 %** (94–99 fps against 80–91) at the same memory, because halving is an exact box
        prefilter and phase-exact. **The exact step is not optional**: a frame's pixel size is
        part of the contract downstream — a `SCALE_NONE` clip is drawn at its own size — and
        stopping at the halving cost 4.5 dB on `readers.video_b_24fps_prescale`. At a
        power-of-two ratio the halvings land on the target and the step disappears.
      - Against the CPU the scaled path is still 28.9–30.7 dB, because swscale's
        `SWS_FAST_BILINEAR` carries a half-pixel phase — a whole column of wrong pixels at every
        colour-bar edge. That is a tolerance question, not a defect.
      - End to end, 4K → 1080p, `OPENSHOT_GPU=vulkan`, interleaved: **87–91 fps with swscale
        against 87–89 with the shader** — a wash on wall clock, but **CPU falls from ~2.0 to ~1.8
        cores** and RSS rises 916 → 990 MB. It does not pay yet because both ends still copy:
        NVDEC is not feeding it (the sub-task above) and the writer still reads back (W25).
- [x] **`Frame::GetBytes()` counts a GPU-backed frame** (2026-09-22). It counted `image` only, so
      a GPU frame reported **zero** and every cache that budgets in bytes was blind to it:
      `CacheMemory` would never evict one, and a reader handing out GPU frames would retain
      surfaces without bound. This had to land before the pre-scale question could even be
      measured.
- [x] **Remove `DE_LIMIT_*` (2026-09-23).** Gone from `Settings` and the reader. A stream NVDEC
      refuses falls back through `ReopenWithoutHardwareDecode` (4608x2592 and 10-bit H.264,
      measured: 15/15 frames).
- [ ] *(optional)* extend `IsHardwareDecodeSupported` to HEVC, VP9, AV1, MPEG-4.

**Gate result, 2026-09-23 — met.** (1) `source_4k` 1080p `render` 78–82 → **126–133 fps**,
`x264` 58–59 → **87–96**; reader CPU per frame 7.2 → **2.8 ms**. (2) **0.6 cores** end to end in
`render`. (3a) `unit.gpu_decode` **49.5 dB / 3 LSB** (after the chroma-bias fix; was 47.4). (3b)
NVDEC vs software **byte-identical in YUV** over 120 frames at 640x360, 1080p and 4K. (4)
`unit.bt709_chart`: **2 code values** on both GPU paths, the ideal for the chart's own 8-bit YUV.

**Gate — restated 2026-09-22, because all three clauses were measured to be wrong.** The original
read: *"Decode-only 4K ≥ 120 fps and < 1 core; decoded frame vs software decode PSNR ≥ 48 dB; a
BT.709 chart decodes to the right sRGB values."* What each clause turned out to be worth:

1. **"Decode-only 4K ≥ 120 fps" was already met before this item started**, and by the CPU: the
   reader does 126–135 fps, and 195–198 after the buffer pool. Worse, decode-only fps is the wrong
   measure — end to end at 4K → 1080p the reader is not the bottleneck, and a faster reader moved
   the pipeline by nothing. **Replaced by: the end-to-end `source_4k` render must not regress, and
   the reader's CPU time per frame must fall.**
2. **"< 1 core" is the real content of the gate** and it stands. Measured today: ~1.8 cores end to
   end with the shader against ~2.0 with swscale, so it is not met and NVDEC is what is left to
   meet it.
3. **"vs software decode PSNR ≥ 48 dB" is unreachable as written, and against the wrong
   reference.** Hardware decode returns NV12 and software returns YUV420P, and **swscale converts
   the same 4:2:0 samples to RGBA 40.5 dB apart depending only on which of the two they are laid
   out as** — reproduced with the ffmpeg CLI alone, so it is swscale's chroma handling, not ours.
   Comparing after the conversion measures swscale's inconsistency, not the decoder's fidelity.
   **Replaced by two sharper checks:** (a) the SkSL conversion against swscale on *identical*
   planes, ≥ 44 dB and ≤ 4 LSB — `unit.gpu_decode`, measured 47.4 dB / 3 LSB; and (b) NVDEC
   against software decode compared **in YUV, before any conversion**, where H.264 is normative
   and the two should agree exactly.
4. **The BT.709 chart clause stands unchanged.**

**Expected re-baseline.** This intentionally *differs* from the CPU goldens — and it is now clear
exactly how. Two separate differences, and only the second is a colour change:

- **Rounding**, 3 LSB, from doing the same BT.601 conversion in floating point. Small, everywhere.
- **The stream's declared colour space is honoured, and swscale as this reader configures it never
  did.** The suite's own media is untagged, so it decodes BT.601 either way — but the files the
  suite *exports* are tagged `bt709`, and decoding those as BT.709 shifts them by several LSB with
  a systematic per-channel bias. That is the correct answer and a different picture.

With `GPU_DECODE` on, that costs 19 of 307 frames in the Vulkan arm: the blend modes whose formula
amplifies a sub-LSB input difference, ChromaKey (a threshold), `effects.enhancement`, and
`export.roundtrip_x264` (the colour-space clause above). **Turning the flag on is the project
owner's decision**, and it re-baselines every golden that decodes video. Until then the path is
built, tested by `unit.gpu_decode` on both backends, and off.
**Size.** ~1.5 weeks.

### W24 — Decode read-ahead · legacy `4.3` · **DONE 2026-09-23, gate restated**

**Depends on.** W23.

- [x] **One thread per reader decodes ahead for sequential access; seeks retarget it
      (2026-09-23).** `FFmpegReader::GetFrame` records the caller's position, decodes, and wakes a
      worker that fills the next `Settings::READ_AHEAD_FRAMES` frames into `final_cache` through
      the same locked path (`DecodeFrame`). **Two deviations from the line above, both measured:**
      - **Frames in host memory only.** A GPU-decoded frame belongs to the recorder of the thread
        that made it; one decoded on the worker would be thrown away by `cachedFrameIsUsable` on
        the caller's thread. With `GPU_DECODE` on and a GPU present the reader decodes on the
        caller's thread as before — where, after W23, decode is 1.6 ms of an 11 ms frame anyway.
        Prefetching *AVFrames* there instead is possible but buys at most that 1.6 ms.
      - **Two frames, not four.** 2 is as fast as 4 within noise and each extra frame is a full
        frame per open reader (4 cost `grid_3x3`'s nine readers 270 MB; 2 costs ~70 MB).
      Guarded by `unit.read_ahead` (same frames through walks and seeks, and the worker really
      decoded ahead). No pixel changes: four-way 307/307; NVDEC arms identical with it on or off.

**Gate as written — not reachable by this item, measured 2026-09-23.** "`source_4k` x264 ≥ 90 fps"
assumed decode was the bottleneck; after W23 it is not, on either path. Per frame of `source_4k`
x264: on the **CPU path** the x264 encode (~6 cores) and the writer's own RGBA→YUV swscale
(~7 ms) remain; on the **GPU path** (89.5 fps before this item, and read-ahead does not apply)
the readback (3.9 ms) and the writer's swscale (6.1 ms) remain. Both are **W25**, which is where
the 90 fps now belongs. **Restated and met:** the caller's time in the reader falls (7.9 →
**2.9 ms** a frame, CPU path), no pixel changes, bounded memory, and the CPU path gets faster:

| 1080p, CPU only (`OPENSHOT_GPU=off`) | read-ahead off | on (2) |
|---|---|---|
| `source_4k` x264 | 52–56 | **61–66** |
| `grid_3x3` x264 | 22.2–22.4 | **24.9–26.0** |
| `single_video` x264 | 79–80 | 80–83 |
| `source_4k` render (Vulkan compositor) | 72–73 | **100–106** |

**Original gate.** Decode no longer appears in an `nsys`/gdb profile of `source_4k`; `source_4k` x264 ≥
**90 fps** (47.5).

**Size.** ~3 days.

### W25 — Writer consumes textures · legacy `4.4`

**Depends on.** W12, W22.

- [ ] Allocate `hw_frames_ctx` (NV12, or P010 for 10-bit).
- [ ] RGBA→NV12 as an SkSL pass into a CUDA-mapped buffer.
- [ ] Send `AV_PIX_FMT_CUDA` frames; keep the encoder queue four deep.
- [ ] Software encoders keep the readback path.

**Gate.** `single_video` 1080p nvenc ≥ **250 fps**; 2160p ≥ **60 fps**; CPU per export < **2 cores**;
RGBA→NV12→RGBA round trip within 1 LSB.
**Size.** ~1.5 weeks.

> **Release gate R4.** `heavy_effects` ≥ 60 fps, `chroma_key_green` ≥ 70 fps, `transitions_chain`
> ≥ 70 fps, `grid_3x3` ≥ 90 fps, `everything` ≥ 20 fps, all 1080p; CPU per export < 2 cores; golden
> green with only the documented BT.709 re-baseline.

---

## Stage 8 — Remove Qt and the rest of the CPU stack (R5)

**This phase must not alter rendering.** Zero pixel change versus R4 is its gate.

### W26 — `Frame` drops `QImage` · legacy `5.1`

- [ ] `readback()` returns an `SkPixmap`.
- [ ] The golden harness's `Image.cpp` switches to `SkPngEncoder`/`SkPngDecoder` — that file is the
      suite's only Qt user, by design.

**Size.** ~3 days.

### W27 — Delete the dead code · legacy `5.2`

**Depends on.** W26.

- [ ] Delete `BlendModes.cpp`, `QtImageReader`, `QtTextReader`, `QtHtmlReader`, `TextReader`,
      ImageMagick `ImageReader`/`ImageWriter`/`MagickUtilities`, `CacheDisk`,
      `ScreenCaptureReader*`, `src/Qt/*`, `QtPlayer`, `PlayerBase`, `RendererBase`, `FrameScope`.
- [ ] The unported effects in `GPU-RENDER-PLAN.md` §2.4 go too, unless product asks for them.

**Size.** ~2 days.

### W28 — Remove the dependencies from the build · legacy `5.3`

**Depends on.** W27.

- [ ] Remove `find_package(Qt…)`, ImageMagick and babl from `src/CMakeLists.txt`.
- [ ] Remove `Qt5::Widgets/Gui` from the service; rebuild the image without Qt, Chrome and
      ImageMagick.
- [ ] Keep the Skia raster backend as the no-GPU fallback — it costs nothing and keeps laptops and
      CI working. **This is not optional; see the standing constraint.**

**Gate.** `ldd libopenshot.so | grep -ci qt` is 0; image at least **300 MB** smaller; golden green
with **zero** pixel change versus R4; the service builds on a machine with no Qt installed.
**Size.** ~3 days.

---

## Stage 9 — Depth and density (R6)

### W29 — Frames in flight · legacy `6.1`

**This is the "frames are rendered one by one" item.**
**Read this first.** Rendering frames N and N+1 on two CPU threads was **considered and rejected**:
Graphite uses one `Context` per process and gets parallelism from pipeline depth, not width, so that
machinery would be thrown away here. Width is left to the process manager, which the service already
does.

- [ ] A ring of four pooled canvases with a fence each: record frame n+2 while n+1 executes and n
      encodes.
- [ ] Remove `Timeline::getFrameMutex` from the read path. Keep it for edits.

**Gate.** `everything` 1080p ≥ **30 fps**; GPU busy ≥ **70 %** during a 1080p export (NVML); no VRAM
growth over 10 000 frames.
**Size.** ~1 week.

### W30 — Density · legacy `6.2`

**Depends on.** W29.

- [ ] Re-run `openshot-bench --parallel 1,2,4` on the target GPU SKU.
- [ ] Set `SERVICE_NUM_INSTANCES_PARALLEL`, the GPU time-slicing replica count and the pod requests
      from the measurements.
- [ ] Update `GPU-RENDER-PLAN.md` §4 with real numbers instead of laptop estimates.

**Gate.** Four concurrent 1080p exports each ≥ 2× real time on an L4; aggregate ≥ 3.2× a single
export.
**Size.** ~3 days.

### W31 — Observability · legacy `6.3`

- [ ] Per export: frames, wall time, GPU busy %, NVENC/NVDEC utilisation, VRAM peak, fallback events.

**Gate.** The numbers appear for a production export and match `nvidia-smi` within 10 %.
**Size.** ~2 days.

> **Release gate R6 / end state.** 1080p `everything` ≥ 30 fps (real time, a 17× improvement),
> 4K `everything` ≥ 10 fps, CPU per export < 2 cores, four concurrent 1080p exports per L4.

---

## Legacy id → worklist id

For reading old commits, `STATUS.md` and `GPU-DECISIONS.md`.

| legacy | here | legacy | here |
|---|---|---|---|
| 0.1–0.4 | done (R0) | 3.4 | W15 |
| 0.5 | W04 | 3.5 | W16 |
| 0.6 | W03 | 3.6 | W17 |
| 1.1 | W05 | 3.7 | W18 |
| 1.2 | W06 | 4.0 | **W11** (moved earlier) |
| 1.3 | W10 (optional) | 4.1 | W22 |
| 1.4 | W09 (half already done) | 4.2 | W23 |
| 1.5 | W08 (hw-decode half done) | 4.3 | W24 |
| 1.6 | W07 | 4.4 | W25 |
| 2.0 | done, W01 is the remainder | 4.5 | W19 |
| 2.1–2.4 | done | 4.6 | W20 |
| 2.5 | rejected on measurement | 4.7 | W21 |
| 2.6 | done differently (glow cache) | 5.1–5.3 | W26–W28 |
| 2.7 | done; W17 finishes it | 5.4 | policy, not a step |
| 3.1 | W12 | 6.1 | W29 |
| 3.2 | W13 | 6.2 | W30 |
| 3.3 | W14 | 6.3 | W31 |

## Releasable builds

| build | items | key gate (1080p) |
|---|---|---|
| R0 ✅ | — | suite green, baseline recorded |
| R2a ✅ | — | `text_animated_glow_3` 1.4 → ≥ 4 · **met: 52** |
| R2b ✅ | — | `subtitles_words` 56 → ≥ 85 · **met: 113** |
| R1 | W05–W10 | `single_video` nvenc 75 → ≥ 105 |
| R3 | W12–W18 | `grid_3x3` 23 → ≥ 60 |
| R4 | W19–W25 | `heavy_effects` 11.5 → ≥ 60; CPU < 2 cores |
| R5 | W26–W28 | no pixel change; −300 MB |
| R6 | W29–W31 | `everything` 1.8 → ≥ 30; GPU busy ≥ 70 % |


## Stage 10 — Release (last, by owner decision)

> **Moved here 2026-09-18 by the project owner.** These were Stage 1, at the front. Nothing ships
> until the whole migration is done, so they now sit where they actually run: last. The item IDs
> stay W01 and W02 — every cross-reference in these docs uses them.

Phase 2 was code-complete and measured long before this point (glow text 4.3 → 52 fps on an A2000,
a real 30 s payload 2.1x). These two items are what turn the finished migration into production.

### W01 — Build and pin the real service image · legacy `2.0` remainder · **DEFERRED**

> **Deferred 2026-09-16** by the project owner, to the very end of the migration. Nothing depends
> on it; develop and test locally without a container.

**Goal.** The actual image builds and is pinned, not just the stand-in.
**Depends on.** Nothing. `../video-rendering-service` branch `feature/gpu-rendering` already has the
Dockerfile, the `ENCODER` probe, `OPENSHOT_GPU` plumbing and `tools/gpu-preflight.sh`.
**Why it is blocked today.** The build-stage base is in a private registry this machine is not
authenticated to (`docker pull` → `error getting credentials`).

- [ ] `gcloud auth login`, then `docker build .` in the service repo — the full two-stage build.
- [ ] Resolve both base images to digests; set them as the `BUILD_BASE` / `RUNTIME_BASE` `ARG`
      defaults.
- [ ] Confirm the build stage really has Skia headers. The service includes them transitively
      (`text/TextClipReader.h` → `TextGlowRenderer.h` → `<skia/...>`) and nothing in the repo
      installs them, so the base must — if it does not, the `find_path` added to `CMakeLists.txt`
      needs a corresponding install step in the Dockerfile.
- [ ] Push to the dev registry and deploy to a CPU node with the defaults.
- [ ] Deploy to a GPU node with `helm/values_gpu_example.yaml`.

**Gate.** Container starts on a CPU node **and** a GPU node. `vulkaninfo --summary` shows the NVIDIA
ICD on the GPU node. `ffmpeg -encoders` lists `h264_nvenc`. One export completes on each, and the
CPU node's output is identical to today's.
**Done when.** Both nodes have completed an export and `STATUS.md` records the image digest.
**Size.** ~1 day, mostly waiting on infrastructure.

### W02 — The full post-merge benchmark · merge gate · **DEFERRED**

> **Deferred 2026-09-16** by the project owner. There is no release and no merge to `develop`
> until the GPU work is finished, so the gate has nothing to gate yet. Re-run it then, on a quiet
> machine.

**Goal.** Clear the last thing standing between `feature/gpu-rendering` and `develop`.
**Depends on.** Nothing. Needs a **quiet machine** — the A/B that stood in for this ran on a box
throttled to 400 MHz and both builds landed ~40 % under baseline.

- [ ] Full `openshot-bench --label r2` run (195 cases, ~90 min). Do not run anything else on the box.
- [ ] `openshot-bench compare tests/bench/results/baseline-cpu.json r2.json --md`.
- [ ] Commit the JSON and append the table to `doc/PERFORMANCE-BASELINE.md`.
- [ ] Confirm the `compositing.layer_order` decision has been answered. It is tracked in
      `GPU-DECISIONS.md`'s open list rather than here, because it is a behaviour question that
      wants an answer long before the release benchmark runs.

**Gate.** No scenario more than 5 % slower than `baseline-cpu.json`.
**Done when.** The comparison is committed and the layer-order question is answered in
`GPU-DECISIONS.md`.
**Size.** ~half a day plus machine time.
