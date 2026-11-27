"""gps-import: JSON schema and launch normalization CLI smoke test."""
import sys
from clitest import *

def main():
    t = Tools(sys.argv[1], sys.argv[2])
    try:
        out = t.path("imported.conf")
        code, d, err = t.json("gps-import", "--output", out, "--probe-ms", "500",
                              "videotestsrc num-buffers=2 ! video/x-raw,width=16,height=16 ! fakesink")
        check_eq(code, 0, "import exit")
        check_eq(d["status"], "ok", "import status")
        check_eq(d["elements"], 2, "collapsed capsfilter onto link")
        check_eq(d["links"], 1, "one link")

        # Invalid launch description returns non-zero with error status.
        code, d, err = t.json("gps-import", "--output", out, "no_such_element_xyz ! fakesink")
        check(code != 0, "bad import exits non-zero")
        check_eq(d["status"], "error", "bad import status")
    finally:
        t.cleanup()

if __name__ == "__main__":
    main()
