#!/usr/bin/env bash
#
# W04 — run the production payload corpus and check the result is reproducible.
#
# These payloads catch JSON→timeline regressions that tests/golden structurally cannot: the golden
# suite drives the library directly through Recipes.h and never parses a payload. This script runs
# each capture twice through ../video-rendering-service's `render-payload`, hashes the decoded
# frames, and fails if the two rounds disagree or if they differ from the recorded hashes.
#
#   tests/payloads/run-corpus.sh [check|update] [name-filter]
#
# A capture's signed fileUrls expire 24 h after issue, so the media is archived next to the payload
# and replayed from there: RENDER_MEDIA_CACHE makes the service take each file from the archive
# instead of the network (see ../video-rendering-service's MediaFetch). RENDER_MEDIA_CACHE_STRICT
# turns a missing archive entry into an error rather than a silent network fetch, which is what
# keeps a "passing" run from quietly depending on a URL that still happens to resolve.
#
# Environment:
#   RENDER_PAYLOAD   path to the render-payload binary (default: the service's Release build)
#   GPU_RENDERING    on (default) | off | lavapipe — read by the service, not by this script.
#                    The recorded hashes are the CPU render: check them with GPU_RENDERING=off.
#   KEEP_OUTPUT      set to keep the rendered mp4s instead of deleting them
#
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo="$(cd "$here/../.." && pwd)"

mode="${1:-check}"
filter="${2:-}"

RENDER_PAYLOAD="${RENDER_PAYLOAD:-$repo/../video-rendering-service/cmake-build-release/render-payload}"
archive_root="$repo/tmp/payloads"
expected_dir="$here/expected"
work_root="${TMPDIR:-/tmp}/payload-corpus.$$"

if [[ ! -x "$RENDER_PAYLOAD" ]]; then
    echo "render-payload not found at: $RENDER_PAYLOAD" >&2
    echo "Build it with:" >&2
    echo "  cmake -S . -B cmake-build-release -DCMAKE_BUILD_TYPE=Release -DBUILD_RENDER_PAYLOAD=ON \\" >&2
    echo "        -DCMAKE_PREFIX_PATH=\$HOME/google-cloud-cpp/installed/x64-linux" >&2
    echo "  cmake --build cmake-build-release --target render-payload" >&2
    exit 2
fi

mkdir -p "$expected_dir" "$work_root"
trap '[[ -n "${KEEP_OUTPUT:-}" ]] || rm -rf "$work_root"' EXIT

# The archive directory for a payload is the tmp/payloads/ entry whose name the payload's own
# basename starts with: prod-2026-09-16-pip-lut-whoosh.json -> tmp/payloads/prod-2026-09-16/.
archive_for() {
    local base="$1" candidate
    for candidate in "$archive_root"/*/; do
        [[ -d "$candidate" ]] || continue
        local name; name="$(basename "$candidate")"
        if [[ "$base" == "$name"* ]]; then
            echo "${candidate%/}/media"
            return 0
        fi
    done
    return 1
}

# Per-frame MD5s of the decoded video, which ignore container metadata but still see every pixel.
frame_hashes() {
    local mp4="$1" out="$2"
    ffmpeg -hide_banner -loglevel error -i "$mp4" -map 0:v -f framemd5 -c copy - 2>/dev/null \
        | grep -v '^#' > "$out" || {
            # -c copy gives packet hashes; fall back to decoding when the copy muxer refuses.
            ffmpeg -hide_banner -loglevel error -i "$mp4" -map 0:v -f framemd5 - \
                | grep -v '^#' > "$out"
        }
}

run_once() {
    local payload="$1" archive="$2" out_dir="$3" label="$4"
    mkdir -p "$out_dir"
    RENDER_MEDIA_CACHE="$archive" \
    RENDER_MEDIA_CACHE_STRICT=1 \
        "$RENDER_PAYLOAD" "$payload" "$out_dir" "$label" > "$out_dir/log.txt" 2>&1
}

failures=0
ran=0

for payload in "$here"/*.json; do
    base="$(basename "$payload" .json)"
    [[ -z "$filter" || "$base" == *"$filter"* ]] || continue

    archive="$(archive_for "$base")" || {
        echo "SKIP   $base — no archived media under tmp/payloads/ (see README)"
        continue
    }
    if [[ ! -d "$archive" ]]; then
        echo "SKIP   $base — archive directory missing: $archive"
        continue
    fi

    ran=$((ran + 1))
    echo "RUN    $base"

    ok=1
    for round in 1 2; do
        if ! run_once "$payload" "$archive" "$work_root/$base/round$round" "round$round"; then
            echo "FAIL   $base — round $round did not complete"
            tail -5 "$work_root/$base/round$round/log.txt" | sed 's/^/         /'
            ok=0
            break
        fi
    done
    [[ $ok == 1 ]] || { failures=$((failures + 1)); continue; }

    mapfile -t outputs < <(find "$work_root/$base/round1" -name '*.mp4' | sort)
    if [[ ${#outputs[@]} -eq 0 ]]; then
        echo "FAIL   $base — round 1 produced no mp4"
        failures=$((failures + 1))
        continue
    fi

    for mp4 in "${outputs[@]}"; do
        name="$(basename "$mp4")"
        twin="$work_root/$base/round2/$name"
        if [[ ! -f "$twin" ]]; then
            echo "FAIL   $base/$name — round 2 produced no matching output"
            failures=$((failures + 1))
            continue
        fi

        frame_hashes "$mp4"  "$work_root/$base/r1.framemd5"
        frame_hashes "$twin" "$work_root/$base/r2.framemd5"

        if ! cmp -s "$work_root/$base/r1.framemd5" "$work_root/$base/r2.framemd5"; then
            diff_count=$(diff "$work_root/$base/r1.framemd5" "$work_root/$base/r2.framemd5" | grep -c '^<' || true)
            echo "FAIL   $base/$name — the two rounds differ in $diff_count frame(s); not deterministic"
            failures=$((failures + 1))
            continue
        fi

        digest="$(sha256sum < "$work_root/$base/r1.framemd5" | cut -d' ' -f1)"
        frames="$(wc -l < "$work_root/$base/r1.framemd5")"
        record="$expected_dir/$base.sha256"

        if [[ "$mode" == "update" ]]; then
            printf '%s  %s  frames=%s\n' "$digest" "$name" "$frames" > "$record"
            echo "UPDATE $base/$name — $frames frames, $digest"
        elif [[ ! -f "$record" ]]; then
            echo "FAIL   $base/$name — no recorded hash (run with 'update'); got $digest"
            failures=$((failures + 1))
        else
            recorded="$(cut -d' ' -f1 < "$record")"
            if [[ "$digest" == "$recorded" ]]; then
                echo "PASS   $base/$name — $frames frames, reproducible and matching"
            else
                echo "FAIL   $base/$name — frame hashes changed"
                echo "         recorded $recorded"
                echo "         got      $digest"
                echo "       If this is intentional, re-run with 'update' and commit the record."
                failures=$((failures + 1))
            fi
        fi
    done
done

echo
if [[ $ran -eq 0 ]]; then
    echo "no payloads ran — check tmp/payloads/ has the archived media"
    exit 1
fi
echo "$ran payload(s), $failures failure(s)"
[[ $failures -eq 0 ]]
