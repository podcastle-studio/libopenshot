#!/usr/bin/env bash
# Regenerate the counter parity fixtures from the design team's TypeScript engine.
#
#   tools/counter-fixtures/generate.sh [path/to/async-counters-handoff]  (Node >= 23, type stripping)
#
# The handoff is reference material and is not vendored: its src/engine is copied to a temp dir,
# the extension-less imports are rewritten for Node's ESM loader, and gen.ts writes
# tests/golden/counter/fixtures.json. Commit the JSON with the engine change that needed it.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
HANDOFF="${1:-$HOME/Desktop/projects/_reference/async-counters-handoff-v4}"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
mkdir -p "$TMP/engine"
cp "$HANDOFF"/src/engine/*.ts "$TMP/engine/"
rm -f "$TMP"/engine/*.test.ts
sed -i -E "s#from '(\./[a-z-]+)'#from '\1.ts'#g" "$TMP"/engine/*.ts
cp "$HERE/gen.ts" "$TMP/gen.ts"
mkdir -p "$REPO/tests/golden/counter"
node "$TMP/gen.ts" > "$REPO/tests/golden/counter/fixtures.json"
echo "wrote $REPO/tests/golden/counter/fixtures.json ($(wc -c < "$REPO/tests/golden/counter/fixtures.json") bytes)"
