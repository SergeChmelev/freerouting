#!/usr/bin/env bash
# Runs the Windows freerouting-cpp.exe from WSL with Linux paths: the file arguments of -de, -do
# and -dr are converted to Windows paths (wslpath -w). Lets tools/parity_sample.sh test the .exe:
#   FR_WIN_EXE=~/build/win-rel/freerouting-cpp.exe FR_CPP_BIN=tools/windows/exe-wrapper.sh \
#     tools/parity_sample.sh <outdir> 12 60
set -euo pipefail
exe="${FR_WIN_EXE:-$HOME/build/win-rel/freerouting-cpp.exe}"
args=()
while [ $# -gt 0 ]; do
  case "$1" in
    -de|-do|-dr)
      flag="$1"; shift
      path="$1"
      [ -e "$path" ] && path="$(realpath "$path")" || path="$(realpath -m "$path")"
      args+=("$flag" "$(wslpath -w "$path")")
      ;;
    *) args+=("$1") ;;
  esac
  shift
done
exec "$exe" "${args[@]}"
