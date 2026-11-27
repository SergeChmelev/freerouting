#!/usr/bin/env bash
# SES of the MCP and REST servers against the command line's, byte for byte.
#
#   tools/server/parity.sh <freerouting-cpp> <out-dir> <board.dsn> [<board.dsn> ...]
#
# For each board: the CLI routes it (-de board.dsn -do cli.ses), the MCP server routes it
# (autoroute_board with filePath / outputPath), the REST server routes it (sessions/create,
# jobs/enqueue, jobs/{id}/input, start, poll, output), and the three SES are compared with cmp.
# A board with a .rules file next to it (same base name) gets it on all three paths.
# Extra CLI options for all boards: FR_OPTS="-mp 5" (also given to MCP and REST).
set -u
exe=$(realpath "$1"); out=$2; shift 2
mkdir -p "$out"
here=$(cd "$(dirname "$0")" && pwd)
port=${FR_PORT:-37991}
opts=${FR_OPTS:-}

mp=""; mt=""
read -r -a optv <<< "$opts"
for ((k = 0; k < ${#optv[@]}; k++)); do
  case ${optv[k]} in
    -mp) mp=${optv[k+1]} ;;
    -mt) mt=${optv[k+1]} ;;
  esac
done

"$exe" --api --port "$port" -q 2> "$out/api.log" &
api=$!
trap 'kill $api 2>/dev/null' EXIT
for _ in $(seq 50); do curl -sf "http://127.0.0.1:$port/v1/system/status" >/dev/null && break; sleep 0.2; done

fail=0
for dsn in "$@"; do
  name=$(basename "$dsn" .dsn)
  rules="${dsn%.dsn}.rules"
  d="$out/$name"; mkdir -p "$d"
  cli_rules=(); mcp_rules=""; rest_rules=""
  if [ -f "$rules" ]; then
    cli_rules=(-dr "$rules")
    mcp_rules=", \"rulesPath\": \"$(realpath "$rules")\""
  fi
  # 1. command line
  "$exe" -de "$dsn" -do "$d/cli.ses" "${cli_rules[@]}" $opts -q > "$d/cli.txt" 2>&1
  # 2. MCP over stdio
  extra=""
  [ -n "$mp" ] && extra="$extra, \"maxPasses\": $mp"
  [ -n "$mt" ] && extra="$extra, \"maxThreads\": $mt"
  python3 "$here/mcp_client.py" "$exe" --quiet autoroute_board \
    "{\"filePath\": \"$(realpath "$dsn")\", \"outputPath\": \"$(realpath "$d")/mcp.ses\"$mcp_rules$extra}" \
    > "$d/mcp.json" 2> "$d/mcp.log"
  # 3. REST API
  python3 - "$port" "$dsn" "$rules" "$d/rest.ses" "$mp" "$mt" > "$d/rest.txt" 2>&1 <<'EOF'
import base64, json, os, sys, time, urllib.request
port, dsn, rules, out, mp, mt = sys.argv[1:7]
base = f"http://127.0.0.1:{port}/v1"
hdr = {"Content-Type": "application/json", "Freerouting-Environment-Host": "parity/1.0",
       "Freerouting-Profile-ID": "00000000-0000-0000-0000-000000000001"}
def call(method, path, body=None):
    data = json.dumps(body).encode() if body is not None else None
    req = urllib.request.Request(base + path, data=data, method=method, headers=hdr)
    with urllib.request.urlopen(req) as r:
        return r.status, json.loads(r.read() or b"null")
_, session = call("POST", "/sessions/create")
_, job = call("POST", "/jobs/enqueue", {"session_id": session["id"], "name": os.path.basename(dsn)[:-4]})
jid = job["id"]
call("POST", f"/jobs/{jid}/input", {"filename": os.path.basename(dsn),
                                     "data": base64.b64encode(open(dsn, "rb").read()).decode()})
if os.path.exists(rules):
    call("POST", f"/jobs/{jid}/rules", {"filename": os.path.basename(rules),
                                         "data": base64.b64encode(open(rules, "rb").read()).decode()})
settings = {}
if mp: settings.setdefault("autorouter", {})["max_passes"] = int(mp)
if mt: settings["max_threads"] = int(mt)
if settings: call("POST", f"/jobs/{jid}/settings", settings)
call("PUT", f"/jobs/{jid}/start")
while True:
    _, j = call("GET", f"/jobs/{jid}?compact=true")
    if j["state"] in ("COMPLETED", "CANCELLED", "TIMED_OUT", "TERMINATED", "INVALID"): break
    time.sleep(0.5)
status, o = call("GET", f"/jobs/{jid}/output")
open(out, "wb").write(base64.b64decode(o["data"]))
print(j["state"], status, o["size"])
EOF
  r_mcp=same; r_rest=same
  cmp -s "$d/cli.ses" "$d/mcp.ses" || { r_mcp=DIFFERENT; fail=1; }
  cmp -s "$d/cli.ses" "$d/rest.ses" || { r_rest=DIFFERENT; fail=1; }
  printf '%-45s cli %8s bytes  mcp %-9s rest %-9s %s\n' "$name" "$(stat -c %s "$d/cli.ses" 2>/dev/null)" "$r_mcp" "$r_rest" "$(head -c 120 "$d/cli.txt" | tr '\n' ' ')"
done
exit $fail
