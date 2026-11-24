#!/usr/bin/env bash
# Job-heap profile: what the job heap (jrt::JobRegion) still holds at exit, by allocation site
# and its caller, largest first: the temporaries that no operation region took and no owner
# freed (MEMORY_MODEL.md E11-E13). Sampled (one block in 16 by address, scaled back).
# Needs a build with -DJRT_ALLOC_PROFILE=1 -fno-omit-frame-pointer (see live_profile.sh).
# usage: tools/mem/job_profile.sh <binary> <out-prefix> <fixture.dsn> [top]
# FR_ALLOC_PROFILE_FP=N attributes each site to the caller N levels up (default 1; 3-5 find the
# operation that runs outside an operation region). FR_CPP_ARGS: extra router options (-mt 1).
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
FR_JOB=1 FR_PROFILE_BY_BYTES=1 FR_ALLOC_PROFILE_FP="${FR_ALLOC_PROFILE_FP:-1}" "$here/alloc_profile.sh" "$@"
