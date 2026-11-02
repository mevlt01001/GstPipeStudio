"""gps-run: JSON schema of SPEC 7, exit codes (0 eos, 1 failure, 2 usage, 3 refused), parameters, SIGINT."""
import os
import signal
import subprocess
import sys
import time

from clitest import *

RESULT = {"status": lambda v: v in ("eos", "error", "timeout", "interrupted", "refused"), "message": is_str, "ms": is_num}
CAPS = "video/x-raw, format=(string)I420, width=(int)16, height=(int)16"
FRAME = 384


def recorder(t, name, frames, out, extra_src=None):
    src = {"num-buffers": frames}
    src.update(extra_src or {})
    return t.write(name, conf([("src", "videotestsrc", src), ("out", "filesink", {"location": out})],
                              [("src", "src", "out", "sink", CAPS)], exposed=[("frames", "src.num-buffers")]))


def size(path):
    return os.path.getsize(path) if os.path.exists(path) else -1


def main():
    t = Tools(sys.argv[1], sys.argv[2])
    try:
        # ---- eos, exit 0
        out = t.path("a.raw")
        plan = recorder(t, "rec.conf", 3, out)
        code, d, err = t.json("gps-run", plan)
        check_eq(code, 0, "eos exit")
        has_types(d, RESULT, "result")
        check_eq((d["status"], d["message"]), ("eos", ""), "eos result")
        check_eq(size(out), 3 * FRAME, "plan value used")

        # ---- exposed parameter overrides
        out = t.path("b.raw")
        plan = recorder(t, "rec2.conf", 1000000, out)
        code, d, err = t.json("gps-run", plan, "frames=5")
        check_eq((code, d["status"]), (0, "eos"), "parameter run")
        check_eq(size(out), 5 * FRAME, "parameter value used")
        code, d, err = t.json("gps-run", "frames=7", plan, "--timeout-ms", "20000")  # any order
        check_eq((code, d["status"]), (0, "eos"), "parameter before file")
        check_eq(size(out), 7 * FRAME, "second parameter run")

        # ---- bad parameter value: a failure with the element and property named, exit 1
        code, d, err = t.json("gps-run", plan, "frames=abc")
        check_eq((code, d["status"]), (1, "error"), "bad parameter value")
        check(d["message"].startswith("src.num-buffers: 'abc' gecersiz ("), f"message {d['message']}")
        code, d, err = t.json("gps-run", plan, "frames=")
        check_eq((code, d["status"]), (1, "error"), "empty parameter value")

        # ---- unknown parameter: usage error, exit 2
        code, d, err = t.json("gps-run", plan, "nope=1")
        check_eq(code, 2, "unknown parameter exit")
        check(is_str(d.get("error")) and "nope" in d["error"], "unknown parameter text")

        # ---- refused: free src pad, exit 3
        free = t.write("free.conf", conf([("v", "videotestsrc", {"num-buffers": 3}), ("c", "videoconvert", {})], [("v", "src", "c", "sink")]))
        code, d, err = t.json("gps-run", free)
        check_eq((code, d["status"]), (3, "refused"), "refused")
        check("c.src" in d["message"], "refused names the pad")

        # ---- error at run time, exit 1
        bad = t.write("bad.conf", conf([("f", "filesrc", {"location": "/nonexistent/x.avi"}), ("k", "fakesink", {})], [("f", "src", "k", "sink")]))
        code, d, err = t.json("gps-run", bad)
        check_eq((code, d["status"]), (1, "error"), "run error")
        check(d["message"].startswith("f: "), f"run error message {d['message']}")

        # ---- malformed / missing conf: failure result, exit 1
        code, d, err = t.json("gps-run", t.write("junk.conf", "junk\n"))
        check_eq((code, d["status"]), (1, "error"), "malformed")
        check("junk.conf" in d["message"], "malformed names the file")
        code, d, err = t.json("gps-run", t.path("none.conf"))
        check_eq((code, d["status"]), (1, "error"), "missing file")

        # ---- usage
        for args in ([], ["--timeout-ms", "5"], [plan, "--timeout-ms", "x"], [plan, "extra.conf"], [plan, "--bogus"]):
            code, d, err = t.json("gps-run", *args)
            check_eq(code, 2, f"usage exit for {args}")

        # ---- timeout, exit 1
        live = t.write("live.conf", conf([("src", "videotestsrc", {"is-live": "true"}), ("k", "fakesink", {})], [("src", "src", "k", "sink")]))
        begin = time.time()
        code, d, err = t.json("gps-run", live, "--timeout-ms", "700")
        check_eq((code, d["status"]), (1, "timeout"), "timeout")
        check(700 <= d["ms"] < 4000 and time.time() - begin < 6, f"timeout took {d['ms']}")

        # ---- SIGINT becomes EOS: status interrupted, the file is finished, exit 1
        out = t.path("live.raw")
        livefile = recorder(t, "livefile.conf", -1, out, {"is-live": "true"})
        p = subprocess.Popen([os.path.join(t.bin, "gps-run"), livefile], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        deadline = time.time() + 15
        while time.time() < deadline and size(out) < FRAME * 3:  # wait until it really records
            time.sleep(0.1)
        check(size(out) >= FRAME * 3, "live run records before the signal")
        p.send_signal(signal.SIGINT)
        stdout, stderr = p.communicate(timeout=15)
        import json
        d = json.loads(stdout.strip().split("\n")[-1])
        check_eq(len(stdout.strip().split("\n")), 1, "SIGINT: one JSON line")
        check_eq((p.returncode, d["status"]), (1, "interrupted"), "SIGINT result")
        check(size(out) % FRAME == 0 and size(out) >= 3 * FRAME, f"file holds whole frames only: {size(out)}")
        check(d["ms"] < 10000, "SIGINT ended promptly")
        # SIGTERM does the same
        p = subprocess.Popen([os.path.join(t.bin, "gps-run"), live], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        time.sleep(1.5)
        p.send_signal(signal.SIGTERM)
        stdout, stderr = p.communicate(timeout=15)
        check_eq((p.returncode, json.loads(stdout.strip())["status"]), (1, "interrupted"), "SIGTERM result")
    finally:
        t.cleanup()
    return finish("cli_run_test")


sys.exit(main())
