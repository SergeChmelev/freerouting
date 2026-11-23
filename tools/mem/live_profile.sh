#!/usr/bin/env bash
# Live-memory profile of the no-GC build: what is still allocated at exit (i.e. leaked, since
# nothing is collected), by allocation site and its caller, largest first.
# Needs a build with -DFR_NO_GC=ON and -DJRT_ALLOC_PROFILE=1 -fno-omit-frame-pointer, e.g.
#   cmake -S . -B ~/build/fr-nogc-prof -G Ninja -DCMAKE_BUILD_TYPE=Release -DFR_NO_GC=ON \
#     "-DCMAKE_CXX_FLAGS=-DJRT_CHECKED=0 -DJRT_ALLOC_PROFILE=1 -g -fno-omit-frame-pointer"
# usage: tools/mem/live_profile.sh <binary> <out-prefix> <fixture.dsn> [top]
# FR_ALLOC_PROFILE_FP=N attributes each site to the caller N levels up (default 1).
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
FR_PROFILE_BY_BYTES=1 FR_ALLOC_PROFILE_FP="${FR_ALLOC_PROFILE_FP:-1}" "$here/alloc_profile.sh" "$@"
