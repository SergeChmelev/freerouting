#!/usr/bin/env python3
"""Minimal MCP client for freerouting-cpp --mcp (stdio, JSON-RPC 2.0, one message per line).

Starts the server, does the MCP handshake, lists the tools and calls one; prints the session as a
transcript (">>" client to server, "<<" server to client). Used for the demo in README.md and by
tools/server/parity.sh.

  mcp_client.py <freerouting-cpp> autoroute_board '{"filePath": "board.dsn", "outputPath": "board.ses"}'
  mcp_client.py <freerouting-cpp> --quiet autoroute_board '{...}'   (only the tool result, as JSON)
"""
import json
import subprocess
import sys


def main():
    args = sys.argv[1:]
    quiet = "--quiet" in args
    args = [a for a in args if a != "--quiet"]
    if len(args) < 2:
        print(__doc__, file=sys.stderr)
        return 2
    exe, tool = args[0], args[1]
    arguments = json.loads(args[2]) if len(args) > 2 else {}

    proc = subprocess.Popen([exe, "--mcp", "-q"], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                            text=True, encoding="utf-8", bufsize=1)

    def show(direction, msg):
        if quiet:
            return
        text = json.dumps(msg, ensure_ascii=False)
        if len(text) > 600:
            text = text[:600] + f" ... ({len(text)} chars)"
        print(f"{direction} {text}", flush=True)

    def send(msg):
        show(">>", msg)
        proc.stdin.write(json.dumps(msg) + "\n")
        proc.stdin.flush()

    def receive(request_id):
        for line in proc.stdout:
            msg = json.loads(line)
            show("<<", msg)
            if msg.get("id") == request_id:
                return msg
        raise RuntimeError("the server closed stdout")

    send({"jsonrpc": "2.0", "id": 1, "method": "initialize",
          "params": {"protocolVersion": "2025-06-18", "capabilities": {},
                     "clientInfo": {"name": "mcp_client.py", "version": "1.0"}}})
    receive(1)
    send({"jsonrpc": "2.0", "method": "notifications/initialized"})
    send({"jsonrpc": "2.0", "id": 2, "method": "tools/list"})
    tools = receive(2)["result"]["tools"]
    if not quiet:
        print("   tools: " + ", ".join(t["name"] for t in tools), flush=True)
    send({"jsonrpc": "2.0", "id": 3, "method": "tools/call",
          "params": {"name": tool, "arguments": arguments, "_meta": {"progressToken": "route-1"}}})
    result = receive(3)
    proc.stdin.close()
    proc.wait(timeout=60)
    if quiet:
        print(json.dumps(result.get("result", result), ensure_ascii=False))
    if "error" in result or result["result"].get("isError"):
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
