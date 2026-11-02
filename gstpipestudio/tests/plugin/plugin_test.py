"""libgstgpsconf.so: a *.conf in GPS_PIPELINE_PATH is an element. Private registry, plugin path, conf folder."""
import os
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "cli"))
from clitest import check, check_eq, finish, conf  # noqa: E402

FRAME = 384  # 16x16 I420
CAPS = "video/x-raw, format=(string)I420, width=(int)16, height=(int)16"


class Env:
    def __init__(self, plugin_dir, fixture_script):
        self.tmp = tempfile.mkdtemp(prefix="gps-plugin-")
        self.confs = os.path.join(self.tmp, "confs")
        os.mkdir(self.confs)
        self.media = os.path.join(self.tmp, "media")
        subprocess.run([fixture_script, self.media], check=True, capture_output=True)
        self.registry = os.path.join(self.tmp, "registry.bin")
        # Private registry. Start from the image's warm one when there is one, so only our plugin is scanned.
        warm = os.environ.get("GST_REGISTRY")
        if warm and os.path.exists(warm):
            shutil.copy(warm, self.registry)
        self.env = dict(os.environ, GST_REGISTRY=self.registry, GST_PLUGIN_PATH=plugin_dir, GPS_PIPELINE_PATH=self.confs)

    def write(self, name, text):
        with open(os.path.join(self.confs, name + ".conf"), "w") as f:
            f.write(text)

    def run(self, tool, *args, timeout=300):
        p = subprocess.run([tool, *args], capture_output=True, text=True, errors="replace", env=self.env, timeout=timeout)
        return p.returncode, p.stdout, p.stderr

    def out(self, name):
        return os.path.join(self.tmp, name)

    def size(self, name):
        path = self.out(name)
        return os.path.getsize(path) if os.path.exists(path) else -1


def recorder(env, out, frames=100):
    """videotestsrc ! (16x16 I420) ! filesink; 'frames' exposed. Complete."""
    return conf([("src", "videotestsrc", {"num-buffers": frames, "pattern": "snow"}), ("out", "filesink", {"location": env.out(out)})],
                [("src", "src", "out", "sink", CAPS)], exposed=[("frames", "src.num-buffers"), ("pattern", "src.pattern"),
                                                                ("where", "out.location"), ("size", "out.buffer-size")])


def main():
    env = Env(sys.argv[1], sys.argv[2])
    try:
        env.write("demo", recorder(env, "demo.raw", frames=4))

        # ---- listed by gst-inspect-1.0, no pads
        code, out, err = env.run("gst-inspect-1.0", "demo")
        check_eq(code, 0, "gst-inspect demo exit")
        check("Pad Templates:\n  none" in out, "no pad templates")
        check("Pads:\n  none" in out, "no pads (the plan is complete)")
        check("GstChildProxy" in out, "implements GstChildProxy")
        check("Generic/Bin" in out, "klass")
        check("Children:" in out and "  src\n" in out and "  out\n" in out, "children are listed")
        check("frames" in out and "Range: -1 - 2147483647" in out, "frames property keeps the target's range")
        check("Enum \"GstVideoTestSrcPattern\"" in out, "enum parameter keeps its enum type")
        check("where" in out and "String" in out, "string parameter")
        check("uint" in out.lower() or "Unsigned Integer" in out, "unsigned parameter keeps its type")
        code, out, err = env.run("gst-inspect-1.0", "gpsconf")
        check_eq(code, 0, "gst-inspect plugin exit")
        check("demo: t" in out, f"plugin lists the element: {out[-300:]!r}")

        # ---- gst-launch demo frames=5
        code, out, err = env.run("gst-launch-1.0", "-q", "demo", "frames=5")
        check_eq(code, 0, f"gst-launch exit: {err[-300:]}")
        check_eq(env.size("demo.raw"), 5 * FRAME, "frames=5 made exactly 5 buffers")
        code, out, err = env.run("gst-launch-1.0", "-q", "demo")
        check_eq((code, env.size("demo.raw")), (0, 4 * FRAME), "no parameter: the conf's own value (4)")

        # ---- child property through GstChildProxy
        code, out, err = env.run("gst-launch-1.0", "-q", "demo", "src::num-buffers=3")
        check_eq((code, env.size("demo.raw")), (0, 3 * FRAME), "src::num-buffers=3")
        code, out, err = env.run("gst-launch-1.0", "-q", "demo", "src::num-buffers=6", "frames=2")
        check_eq(code, 0, "both ways")
        check(env.size("demo.raw") in (2 * FRAME, 6 * FRAME), "both ways made a file")
        code, out, err = env.run("gst-launch-1.0", "-q", "demo", "frames=2", "src::pattern=ball", "pattern=ball")
        check_eq((code, env.size("demo.raw")), (0, 2 * FRAME), "enum parameter and child enum")

        # ---- exposed parameter of another type
        target = env.out("other.raw")
        code, out, err = env.run("gst-launch-1.0", "-q", "demo", "frames=2", f"where={target}")
        check_eq((code, env.size("other.raw")), (0, 2 * FRAME), "string parameter redirects the output")

        # ---- an unknown parameter is rejected
        code, out, err = env.run("gst-launch-1.0", "-q", "demo", "bogus=1")
        check(code != 0, "unknown parameter: non-zero exit")
        check("bogus" in err, f"unknown parameter is named: {err[-200:]}")
        code, out, err = env.run("gst-launch-1.0", "-q", "demo", "frames=abc")
        check(code != 0, "bad parameter value: non-zero exit")
        # gst-launch 1.20.3 does not reject an unknown child property (CRITICAL on stderr, exit 0); it must change nothing
        code, out, err = env.run("gst-launch-1.0", "-q", "demo", "src::nonsense=1")
        check_eq((code, env.size("demo.raw")), (0, 4 * FRAME), "unknown child property changes nothing")

        # ---- a conf placed later is found (the plugin depends on the conf folder)
        env.write("second", recorder(env, "second.raw", frames=2))
        code, out, err = env.run("gst-inspect-1.0", "second")
        check_eq(code, 0, f"second conf is found: {err[-200:]}")
        code, out, err = env.run("gst-launch-1.0", "-q", "second")
        check_eq((code, env.size("second.raw")), (0, 2 * FRAME), "second conf runs")
        code, out, err = env.run("gst-launch-1.0", "-q", "demo")
        check_eq(code, 0, "first conf still there")
        # a conf that disappeared is gone again
        os.remove(os.path.join(env.confs, "second.conf"))
        code, out, err = env.run("gst-inspect-1.0", "second")
        check(code != 0, "removed conf is no longer an element")

        # ---- the element inside a pipeline with other elements, and twice at once
        code, out, err = env.run("gst-launch-1.0", "-q", "demo", "frames=1", "demo", "frames=2", "where=" + env.out("twin.raw"))
        check_eq(code, 0, f"two instances: {err[-200:]}")
        check_eq(env.size("twin.raw"), 2 * FRAME, "second instance has its own parameters")

        # ---- an incomplete plan puts its free src pad on the bin as a ghost pad
        env.write("incomplete", conf([("src", "videotestsrc", {"num-buffers": 7, "pattern": "snow"}), ("conv", "videoconvert", {})],
                                     [("src", "src", "conv", "sink", CAPS)]))
        code, out, err = env.run("gst-inspect-1.0", "incomplete")
        check(code == 0 and "SRC: 'conv_src'" in out, f"the free src pad is a ghost pad of the element: {out[-400:]!r}")
        code, out, err = env.run("gst-launch-1.0", "-q", "incomplete", "!", "filesink", f"location={env.out('ghost.raw')}")
        check_eq((code, env.size("ghost.raw")), (0, 7 * FRAME), f"free src pad is usable as ghost pad: {err[-200:]}")

        # ---- a conf that cannot be built fails when the element starts, naming the element
        env.write("broken", conf([("x", "no_such_factory_xyz", {})], []))
        code, out, err = env.run("gst-launch-1.0", "-q", "broken")
        check(code != 0 and "no_such_factory_xyz" in err, f"broken conf: {err[-300:]}")
        env.write("badvalue", conf([("a", "audiotestsrc", {"volume": 5}), ("k", "fakesink", {})], [("a", "src", "k", "sink")]))
        code, out, err = env.run("gst-launch-1.0", "-q", "badvalue")
        check(code != 0 and "a.volume: '5' gecersiz" in err, f"invalid value in conf: {err[-300:]}")

        # ---- a file that is not a conf is not an element
        env.write("garbage", "this is not a conf\n")
        code, out, err = env.run("gst-inspect-1.0", "garbage")
        check(code != 0, "malformed conf is skipped")
        code, out, err = env.run("gst-inspect-1.0", "demo")
        check_eq(code, 0, "others survive a malformed conf")

        # ---- other files in the folder are ignored; an unset folder gives no elements
        with open(os.path.join(env.confs, "notes.txt"), "w") as f:
            f.write("x")
        code, out, err = env.run("gst-inspect-1.0", "notes")
        check(code != 0, "only *.conf counts")
        env.env.pop("GPS_PIPELINE_PATH")
        code, out, err = env.run("gst-inspect-1.0", "demo")
        check(code != 0, "no GPS_PIPELINE_PATH, no elements")
        env.env["GPS_PIPELINE_PATH"] = os.path.join(env.tmp, "nonexistent") + ":" + env.confs
        code, out, err = env.run("gst-inspect-1.0", "demo")
        check_eq(code, 0, "several folders, one of them missing")
    finally:
        shutil.rmtree(env.tmp, ignore_errors=True)
    return finish("plugin_test")


sys.exit(main())
