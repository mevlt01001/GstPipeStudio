"""gps-update: JSON schema of SPEC 7, exit codes, malformed input, --timeout-ms."""
import sys
import time

from clitest import *

PAD = {"element": is_str, "pad": is_str, "identity": is_str, "template": is_str,
       "presence": lambda v: v in ("always", "sometimes", "request"), "isSrc": is_bool, "linked": is_bool,
       "temporaryEnd": is_bool, "unknownData": is_bool, "caps": is_str, "allowed": is_str,
       "allowedStructures": lambda v: isinstance(v, list)}
RESULT = {"status": lambda v: v in ("ok", "error", "timeout", "pad-missing"), "message": is_str, "errorElement": is_str,
          "ms": is_num, "pads": lambda v: isinstance(v, list), "missing": lambda v: isinstance(v, list) and all(is_str(x) for x in v),
          "refused": lambda v: isinstance(v, list) and all(is_str(x) for x in v), "complete": is_bool}


def pad(d, element, identity, src=True):
    for p in d["pads"]:
        if p["element"] == element and p["identity"] == identity and p["isSrc"] == src:
            return p
    return None


def main():
    t = Tools(sys.argv[1], sys.argv[2])
    av = t.path("av.avi")
    try:
        # ---- a demux plan: Always, Sometimes pads, temporary ends
        plan = t.write("demux.conf", conf(
            [("file", "filesrc", {"location": av}), ("dm", "avidemux", {})],
            [("file", "src", "dm", "sink")]))
        code, d, err = t.json("gps-update", plan)
        check_eq(code, 0, "update exit")
        has_types(d, RESULT, "result")
        check_eq(d["status"], "ok", "status")
        check_eq(d["message"], "", "message")
        check(d["ms"] > 0, "ms")
        check_eq(d["complete"], False, "complete (two free pads)")
        for i, p in enumerate(d["pads"]):
            has_types(p, PAD, f"pads[{i}]")
        v = pad(d, "dm", "video_%u#0")
        check(v and v["template"] == "video_%u" and v["presence"] == "sometimes" and v["temporaryEnd"] and not v["linked"], "video pad")
        check(v and v["caps"].startswith("image/jpeg"), "video caps")
        check(v and v["allowedStructures"] and v["allowedStructures"][0]["media"] == "image/jpeg", "allowedStructures")
        for k, s in enumerate(v["allowedStructures"] if v else []):
            check_eq(sorted(s.keys()), ["features", "fields", "media"], "structure keys")
        a = pad(d, "dm", "audio_%u#0")
        check(a and a["caps"].startswith("audio/x-raw"), "audio caps")
        f = pad(d, "file", "src")
        check(f and f["linked"] and not f["temporaryEnd"], "file pad linked")
        s = pad(d, "dm", "sink", src=False)
        check(s and s["linked"] and not s["isSrc"], "sink pad is reported")
        check("PLAYING" in err and "gecici uc" in err, "log lines on stderr")

        # ---- complete plan, exit code and flag
        done = t.write("done.conf", conf(
            [("v", "videotestsrc", {"num-buffers": 5}), ("k", "fakesink", {})], [("v", "src", "k", "sink")]))
        code, d, err = t.json("gps-update", done)
        check_eq((code, d["status"], d["complete"]), (0, "ok", True), "complete plan")

        # ---- errors are results: exit 0, status error, the element named
        bad = t.write("badfile.conf", conf(
            [("f", "filesrc", {"location": "/nonexistent/none.avi"}), ("dm", "avidemux", {})], [("f", "src", "dm", "sink")]))
        code, d, err = t.json("gps-update", bad)
        check_eq(code, 0, "error result exit")
        has_types(d, RESULT, "error result")
        check_eq((d["status"], d["errorElement"]), ("error", "f"), "error result")
        check(d["message"] != "", "error message")

        wrong = t.write("badvalue.conf", conf([("a", "audiotestsrc", {"volume": 5})], []))
        code, d, err = t.json("gps-update", wrong)
        check_eq((code, d["status"], d["errorElement"]), (0, "error", "a"), "bad value result")
        check(d["message"].startswith("a.volume: '5' gecersiz"), f"bad value message: {d['message']}")

        code, d, err = t.json("gps-update", t.write("nofactory.conf", conf([("x", "no_such_factory_xyz", {})], [])))
        check_eq((code, d["status"], d["errorElement"]), (0, "error", "x"), "unknown factory result")

        # ---- pad-missing
        po = t.write("padmissing.conf", conf(
            [("f", "filesrc", {"location": t.path("video_only.avi")}), ("dm", "avidemux", {}), ("q", "queue", {}), ("k", "fakesink", {})],
            [("f", "src", "dm", "sink"), ("dm", "audio_%u#0", "q", "sink"), ("q", "src", "k", "sink")]))
        code, d, err = t.json("gps-update", po)
        check_eq((code, d["status"], d["missing"], d["complete"]), (0, "pad-missing", ["dm.audio_%u#0"], False), "pad-missing")

        # ---- refused: the sink does not wait, so the status stays ok and the refusal is listed
        rf = t.write("refused.conf", conf(
            [("v", "videotestsrc", {"num-buffers": 5}), ("k", "fakesink", {"async": "false"})],
            [("v", "src", "k", "sink", "audio/x-raw")]))
        code, d, err = t.json("gps-update", rf)
        check_eq((code, d["status"], d["complete"]), (0, "ok", False), "refused result")
        check(len(d["refused"]) == 1 and d["refused"][0].startswith("v.src -> k.sink: "), f"refused list {d['refused']}")

        # ---- unknown data
        ud = t.write("unknown.conf", conf([("f", "filesrc", {"location": t.path("random.bin")})], []))
        code, d, err = t.json("gps-update", ud)
        check_eq((code, d["status"]), (0, "ok"), "unknown data result")
        p = pad(d, "f", "src")
        check(p and p["unknownData"] and p["temporaryEnd"], "unknown data pad")

        # ---- --timeout-ms
        stall = t.write("stall.conf", conf(
            [("v", "videotestsrc", {"num-buffers": 30}), ("t", "tee", {}, {"src_%u": 2}), ("k1", "fakesink", {}), ("k2", "fakesink", {})],
            [("v", "src", "t", "sink"), ("t", "src_%u#0", "k1", "sink"), ("t", "src_%u#1", "k2", "sink")]))
        begin = time.time()
        code, d, err = t.json("gps-update", stall, "--timeout-ms", "1000")
        took = (time.time() - begin) * 1000
        check_eq((code, d["status"]), (0, "timeout"), "timeout result")
        check(1000 <= d["ms"] < 3500 and took < 5000, f"timeout took {d['ms']} ms ({took:.0f} ms wall)")
        check("1000" in d["message"], "timeout message names the limit")
        begin = time.time()
        code, d, err = t.json("gps-update", "--timeout-ms", "700", stall)  # option first
        check_eq((code, d["status"]), (0, "timeout"), "option before file")
        check(700 <= d["ms"] < 3000, f"700 ms timeout took {d['ms']}")

        # ---- malformed and missing files: still a result, exit 0
        for name, text in (("malformed.conf", "this is not a conf file\n"),
                           ("noheader.conf", "[element:a]\nfactory=videotestsrc\n"),
                           ("unclosed.conf", "[gstpipestudio\nversion=1\n"),
                           ("badlink.conf", "[gstpipestudio]\nversion=1\n[element:a]\nfactory=fakesink\n[link:0]\nsrc=a\nsrcpad=src\nsink=ghost\nsinkpad=sink\n"),
                           ("empty.conf", "")):
            code, d, err = t.json("gps-update", t.write(name, text))
            check_eq(code, 0, f"{name} exit")
            has_types(d, RESULT, name)
            check_eq(d["status"], "error", f"{name} status")
            check(name in d["message"], f"{name}: message names the file: {d['message']}")
            check_eq(d["pads"], [], f"{name}: no pads")
        code, d, err = t.json("gps-update", t.path("does-not-exist.conf"))
        check_eq((code, d["status"]), (0, "error"), "missing file")
        check("does-not-exist.conf" in d["message"], "missing file message")

        # ---- usage errors: exit 2
        for args in ([], ["a.conf", "b.conf"], [plan, "--timeout-ms"], [plan, "--timeout-ms", "abc"], [plan, "--timeout-ms", "0"],
                      [plan, "--timeout-ms", "-5"]):
            code, d, err = t.json("gps-update", *args)
            check_eq(code, 2, f"usage exit for {args}")
            check(d is not None and is_str(d.get("error")), f"usage JSON for {args}")
        # ---- GStreamer debug output stays on stderr
        code, d, err = t.json("gps-update", done, env={"GST_DEBUG": "4"})
        check_eq((code, d["status"]), (0, "ok"), "GST_DEBUG run")
        check(len(err) > 2000, "GST_DEBUG goes to stderr")
    finally:
        t.cleanup()
    return finish("cli_update_test")


sys.exit(main())
