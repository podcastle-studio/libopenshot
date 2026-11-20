#!/bin/bash
# Golden-frame suite wrapper. Usage:
#   tools/golden.sh build            build the harness
#   tools/golden.sh check [filter]   build + compare + write report, print its path (exit 1 on failure)
#   tools/golden.sh update <filter>  build + re-baseline the scenarios matching <filter>
#   tools/golden.sh list
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${BUILD_DIR:-$ROOT/cmake-build-release}"
BIN="$BUILD/tests/golden/openshot-golden"
OUT="${GOLDEN_OUT:-$BUILD/golden-out}"
REPORT="${GOLDEN_REPORT:-$BUILD/golden-report}"

build() { cmake --build "$BUILD" --target openshot-golden; }

case "${1:-check}" in
  build) build ;;
  list) build && "$BIN" --list ;;
  check)
    build || exit 2
    "$BIN" --out "$OUT" --report "$REPORT" ${2:+--filter "$2"}
    rc=$?
    echo "report: $REPORT/index.html"
    exit $rc ;;
  update)
    if [ -z "${2:-}" ]; then echo "update needs a filter (use '.' for everything, after looking at every frame)"; exit 2; fi
    build || exit 2
    "$BIN" --update --out "$OUT" --filter "$2" ;;
  *) echo "unknown command $1"; exit 2 ;;
esac
