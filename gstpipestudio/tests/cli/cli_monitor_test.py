"""gps-monitor: streaming TelemetrySample lines + final RunResult line."""
import json
import sys
from clitest import *

def main():
    t = Tools(sys.argv[1], sys.argv[2])
    try:
        plan = t.write("mon.conf", conf([("src", "videotestsrc", {"num-buffers": 5}), ("out", "fakesink", {})],
                                        [("src", "src", "out", "sink", "video/x-raw,width=16,height=16")]))
        code, out, err = t.run("gps-monitor", plan)
        check_eq(code, 0, "monitor exit")
        lines = [json.loads(line) for line in out.strip().splitlines() if line.strip()]
        check(len(lines) >= 1, "at least final RunResult printed")
        check_eq(lines[-1]["status"], "eos", "final status eos")
    finally:
        t.cleanup()

if __name__ == "__main__":
    main()
