"""gps-mcp: JSON-RPC 2.0 stdio server + persistent audit log verification."""
import json
import os
import subprocess
import sys
from clitest import *


def main():
    t = Tools(sys.argv[1], sys.argv[2])
    try:
        audit_dir = t.path("mcp_audit")
        os.makedirs(audit_dir, exist_ok=True)
        env = dict(os.environ)
        env["GPS_BIN_DIR"] = t.bin
        env["GPS_USER_DIR"] = t.path("user")
        env["QT_QPA_PLATFORM"] = "offscreen"

        proc = subprocess.Popen(
            [os.path.join(t.bin, "gps-mcp"), "--headless", "--audit-dir", audit_dir],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            env=env,
        )

        def send(msg):
            proc.stdin.write(json.dumps(msg) + "\n")
            proc.stdin.flush()

        def recv():
            line = proc.stdout.readline()
            check(bool(line), "expected JSON-RPC response line")
            return json.loads(line) if line else {}

        # 1. initialize
        send({
            "jsonrpc": "2.0",
            "id": 1,
            "method": "initialize",
            "params": {
                "protocolVersion": "2025-03-26",
                "clientInfo": {"name": "cli_mcp_test", "version": "1.0"},
                "capabilities": {},
            },
        })
        r1 = recv()
        check_eq(r1.get("id"), 1, "initialize id")
        check("result" in r1, "initialize result")
        check_eq(r1["result"]["serverInfo"]["name"], "gps-mcp", "serverInfo name")
        check("instructions" in r1["result"], "driver manual instructions present")

        # 2. notifications/initialized (no response)
        send({"jsonrpc": "2.0", "method": "notifications/initialized"})

        # 3. tools/list
        send({"jsonrpc": "2.0", "id": 2, "method": "tools/list", "params": {}})
        r2 = recv()
        check_eq(r2.get("id"), 2, "tools/list id")
        tool_names = [tool["name"] for tool in r2["result"]["tools"]]
        for expected_tool in ("get_studio_manual", "edit_pipeline", "validate_pipeline", "export_pipeline_so"):
            check(expected_tool in tool_names, f"tools/list includes {expected_tool}")

        # 4. get_studio_manual
        send({
            "jsonrpc": "2.0",
            "id": 3,
            "method": "tools/call",
            "params": {"name": "get_studio_manual", "arguments": {}},
        })
        r3 = recv()
        check_eq(r3.get("id"), 3, "get_studio_manual id")
        check_eq(r3["result"].get("isError"), False, "get_studio_manual not error")
        manual_text = r3["result"]["content"][0]["text"]
        check("Clarify -> Plan -> Mutate -> Validate" in manual_text, "manual explains loop")

        # 5. edit_pipeline
        send({
            "jsonrpc": "2.0",
            "id": 4,
            "method": "tools/call",
            "params": {
                "name": "edit_pipeline",
                "arguments": {
                    "operations": [
                        {"op": "add_element", "id": "src", "factory": "videotestsrc", "props": {"num-buffers": 3}},
                        {"op": "add_element", "id": "sink", "factory": "fakesink"},
                        {"op": "connect", "src": "src", "srcPad": "src", "sink": "sink", "sinkPad": "sink"},
                    ]
                },
            },
        })
        r4 = recv()
        check_eq(r4.get("id"), 4, "edit_pipeline id")
        check_eq(r4["result"].get("isError"), False, "edit_pipeline not error")
        edit_payload = json.loads(r4["result"]["content"][0]["text"])
        check_eq(len(edit_payload["graph"]["elements"]), 2, "2 elements in graph")
        check_eq(len(edit_payload["graph"]["links"]), 1, "1 link in graph")

        # 6. validate_pipeline
        send({
            "jsonrpc": "2.0",
            "id": 5,
            "method": "tools/call",
            "params": {"name": "validate_pipeline", "arguments": {}},
        })
        r5 = recv()
        check_eq(r5.get("id"), 5, "validate_pipeline id")
        check_eq(r5["result"].get("isError"), False, "validate_pipeline not error")
        val_payload = json.loads(r5["result"]["content"][0]["text"])
        check_eq(val_payload["status"], "ok", "validate_pipeline status ok")

        proc.stdin.close()
        rc = proc.wait(timeout=15)
        check_eq(rc, 0, "gps-mcp clean exit")

        # 7. Verify mcp_audit.jsonl recorded every audited call
        jsonl_path = os.path.join(audit_dir, "mcp_audit.jsonl")
        check(os.path.isfile(jsonl_path), "mcp_audit.jsonl exists")
        with open(jsonl_path, "r") as f:
            entries = [json.loads(line) for line in f if line.strip()]
        check_eq(len(entries), 4, "4 audited entries (initialize + 3 tools/call)")
        methods = [(e["method"], e["toolOrResource"]) for e in entries]
        check_eq(
            methods,
            [
                ("initialize", ""),
                ("tools/call", "get_studio_manual"),
                ("tools/call", "edit_pipeline"),
                ("tools/call", "validate_pipeline"),
            ],
            "audited methods and targets",
        )
        for e in entries:
            check(bool(e["requestJson"]), f"requestJson recorded for {e['method']}")
            check(bool(e["responseJson"]), f"responseJson recorded for {e['method']}")
            check_eq(e["clientName"], "cli_mcp_test", "clientName recorded")
            check_eq(e["ok"], True, "ok recorded")
    finally:
        t.cleanup()

    sys.exit(finish("cli.mcp"))


if __name__ == "__main__":
    main()
