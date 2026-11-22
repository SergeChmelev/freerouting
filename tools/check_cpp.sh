#!/usr/bin/env bash
# Compiles (syntax + template instantiation, no codegen) each given .cpp, or every .cpp under
# the given directories. Run inside WSL from the repo root.
# usage: tools/check_cpp.sh src/geometry/planar [src/foo/Bar.cpp ...]
set -uo pipefail
cd "$(dirname "$0")/.."
files=()
for a in "$@"; do
  if [ -d "$a" ]; then
    while IFS= read -r f; do files+=("$f"); done < <(find "$a" -maxdepth 1 -name '*.cpp' | sort)
  else
    files+=("$a")
  fi
done
fail=0
for f in "${files[@]}"; do
  out=$(g++ -std=c++20 -fsyntax-only -fwrapv -Wall -Wno-unused -Wno-sign-compare -Isrc -Iruntime "$f" 2>&1 | grep -E 'error' | head -${FR_MAX_ERR:-20})
  if [ -n "$out" ]; then
    fail=$((fail + 1))
    echo "=== $f"
    echo "$out"
  fi
done
echo "checked ${#files[@]} cpp files, $fail with errors"
[ "$fail" -eq 0 ]
