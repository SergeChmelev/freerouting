#!/usr/bin/env bash
# Syntax-checks each given header (or every header under the given directories) in isolation:
# a TU that only includes the header must compile. Run inside WSL from the repo root.
# usage: tools/check_headers.sh src/geometry/planar [src/foo/Bar.h ...]
set -uo pipefail
cd "$(dirname "$0")/.."
files=()
for a in "$@"; do
  if [ -d "$a" ]; then
    while IFS= read -r f; do files+=("$f"); done < <(find "$a" -name '*.h' | sort)
  else
    files+=("$a")
  fi
done
fail=0
for f in "${files[@]}"; do
  inc="${f#src/}"
  out=$(printf '#include "%s"\n' "$inc" | g++ -std=c++20 -fsyntax-only -fwrapv -Isrc -Iruntime -x c++ - 2>&1)
  if [ -n "$out" ]; then
    fail=$((fail + 1))
    echo "=== $f"
    echo "$out" | grep -E 'error|note: forward' | head -${FR_MAX_ERR:-15}
  fi
done
echo "checked ${#files[@]} headers, $fail with errors"
[ "$fail" -eq 0 ]
