#!/usr/bin/env bash
# Routes a DSN file with the reference Java freerouting (built in ~/freerouting-java).
# usage: tools/java-route.sh <input.dsn> <output.ses> [extra freerouting args...]
set -euo pipefail
JAR="${FR_JAVA_JAR:-$HOME/freerouting-java/build/libs/freerouting-current-executable.jar}"
in="$1"; out="$2"; shift 2
exec java -Djava.awt.headless=true -jar "$JAR" -de "$in" -do "$out" --gui.enabled=false "$@"
