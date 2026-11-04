"""gps-inspect: JSON schema of SPEC 7, exit codes, stdout holds one line only."""
import subprocess
import sys

from clitest import *

PROPERTY = {"name": is_str, "nick": is_str, "blurb": is_str, "kind": is_str, "typeName": is_str, "default": is_str,
            "min": is_str, "max": is_str, "enumValues": lambda v: isinstance(v, list), "readable": is_bool,
            "writable": is_bool, "construct": is_bool, "constructOnly": is_bool, "deprecated": is_bool,
            "mutableReady": is_bool, "mutablePaused": is_bool, "mutablePlaying": is_bool}
KINDS = {"boolean", "int", "uint", "int64", "uint64", "float", "double", "string", "enum", "flags", "caps", "fraction",
         "object", "boxed", "pointer", "other"}
STRUCTURE = {"media": is_str, "features": lambda v: isinstance(v, list) and all(is_str(x) for x in v),
             "fields": lambda v: isinstance(v, list)}
FIELD = {"name": is_str, "type": is_str, "kind": lambda v: v in ("fixed", "list", "range"),
         "values": lambda v: isinstance(v, list) and len(v) >= 1 and all(is_str(x) for x in v)}


def check_property(p, where):
    has_types(p, PROPERTY, where)
    check(p.get("kind") in KINDS, f"{where}: kind {p.get('kind')!r}")
    for v in p.get("enumValues", []):
        has_types(v, {"nick": is_str, "name": is_str, "value": is_str}, where + ".enumValues")
    if p.get("kind") in ("int", "uint", "int64", "uint64", "float", "double"):
        check(p["min"] != "" and p["max"] != "" and p["default"] != "", f"{where}: numeric property needs min, max and default")
        for key in ("min", "max", "default"):
            try:
                float(p[key])
            except ValueError:
                check(False, f"{where}.{key} is not a number: {p[key]!r}")
    if p.get("kind") in ("enum", "flags"):
        check(len(p["enumValues"]) > 0, f"{where}: enum without values")


def check_structure(s, where):
    has_types(s, STRUCTURE, where)
    for i, f in enumerate(s.get("fields", [])):
        has_types(f, FIELD, f"{where}.fields[{i}]")
        check(f.get("type", "") != "", f"{where}.fields[{i}]: type is empty")


def main():
    t = Tools(sys.argv[1], sys.argv[2])
    try:
        # ---- list
        code, d, err = t.json("gps-inspect", "list")
        check_eq(code, 0, "list exit")
        check_eq(list(d.keys()), ["elements"], "list keys")
        els = d["elements"]
        check(len(els) > 200, f"list: {len(els)} elements")
        for e in els[:5] + els[-5:]:
            has_types(e, {"factory": is_str, "klass": is_str, "longName": is_str, "rank": is_int, "plugin": is_str,
                          "isSource": is_bool, "isSink": is_bool,
                          "hasAlwaysSrc": is_bool, "hasAlwaysSink": is_bool,
                          "hasSometimesSrc": is_bool, "hasSometimesSink": is_bool,
                          "hasRequestSrc": is_bool, "hasRequestSink": is_bool,
                          "nvidia": is_bool}, "list entry")
        by = {e["factory"]: e for e in els}
        check(by["videotestsrc"]["isSource"] and not by["videotestsrc"]["nvidia"], "videotestsrc flags")
        check(by["fakesink"]["isSink"], "fakesink isSink")
        check(by["tee"]["hasRequestSrc"], "tee hasRequestSrc")
        if "nvvideoconvert" in by:  # a plain Ubuntu has no DeepStream elements
            check(by["nvvideoconvert"]["nvidia"], "nvvideoconvert nvidia badge")
            check(sum(e["nvidia"] for e in els) >= 20, "nvidia badge count")
        else:
            check(sum(e["nvidia"] for e in els) == 0, "no nvidia badge without DeepStream")
        names = [e["factory"] for e in els]
        check_eq(names, sorted(names), "list sorted by name")

        # ---- version: the GUI's first question (UI_RULES 4); the counts must be the ones of list
        code, v, err = t.json("gps-inspect", "version")
        check_eq(code, 0, "version exit")
        check_eq(list(v.keys()), ["gstreamer", "nvidia", "plugins"], "version keys")
        has_types(v["gstreamer"], {"version": is_str, "major": is_int, "minor": is_int, "micro": is_int}, "version.gstreamer")
        has_types(v["nvidia"], {"present": is_bool, "elements": is_int}, "version.nvidia")
        has_types(v["plugins"], {"count": is_int, "elements": is_int}, "version.plugins")
        g = v["gstreamer"]
        check_eq(g["version"], f"{g['major']}.{g['minor']}.{g['micro']}", "version text matches the numbers")
        check(g["major"] == 1 and g["minor"] >= 16, f"GStreamer {g['version']} is 1.16 or newer")
        check_eq(v["plugins"]["elements"], len(els), "version plugins.elements equals list")
        check_eq(v["nvidia"]["elements"], sum(e["nvidia"] for e in els), "version nvidia.elements equals list")
        check_eq(v["nvidia"]["present"], v["nvidia"]["elements"] > 0, "version nvidia.present")
        check(v["plugins"]["count"] > 20, f"version plugins.count {v['plugins']['count']}")
        gst = subprocess.run(["gst-inspect-1.0", "--version"], capture_output=True, text=True)
        if gst.returncode == 0:
            check(f"GStreamer {g['version']}" in gst.stdout, f"version agrees with gst-inspect-1.0: {gst.stdout.splitlines()[:2]}")

        # ---- element
        code, d, err = t.json("gps-inspect", "element", "compositor")
        check_eq(code, 0, "element exit")
        has_types(d, {"factory": is_str, "longName": is_str, "klass": is_str, "description": is_str, "author": is_str,
                      "plugin": is_str, "rank": is_int, "isSource": is_bool, "isSink": is_bool,
                      "padTemplates": lambda v: isinstance(v, list) and len(v) >= 2,
                      "properties": lambda v: isinstance(v, list) and len(v) > 0}, "element")
        check_eq(d["factory"], "compositor", "element factory")
        sink_t = None
        for i, tpl in enumerate(d["padTemplates"]):
            has_types(tpl, {"name": is_str, "direction": lambda v: v in ("src", "sink"),
                            "presence": lambda v: v in ("always", "sometimes", "request"), "caps": is_str,
                            "structures": lambda v: isinstance(v, list), "padProperties": lambda v: isinstance(v, list)},
                      f"padTemplates[{i}]")
            for k, s in enumerate(tpl["structures"]):
                check_structure(s, f"padTemplates[{i}].structures[{k}]")
            for k, p in enumerate(tpl["padProperties"]):
                check_property(p, f"padTemplates[{i}].padProperties[{k}]")
            if tpl["name"] == "sink_%u":
                sink_t = tpl
        check(sink_t is not None and sink_t["presence"] == "request", "compositor sink_%u is a request template")
        if sink_t:
            xpos = [p for p in sink_t["padProperties"] if p["name"] == "xpos"]
            check(len(xpos) == 1 and xpos[0]["kind"] == "int", "xpos is a pad property of kind int")
            check(not [p for p in sink_t["padProperties"] if p["name"] in ("name", "parent")], "no GstObject properties on pads")
        for i, p in enumerate(d["properties"]):
            check_property(p, f"properties[{i}]")
        check(not [p for p in d["properties"] if p["name"] in ("name", "parent")], "no GstObject properties on elements")

        code, d, err = t.json("gps-inspect", "element", "videotestsrc")
        pattern = [p for p in d["properties"] if p["name"] == "pattern"][0]
        check_eq(pattern["kind"], "enum", "pattern kind")
        check(any(v["nick"] == "ball" for v in pattern["enumValues"]), "pattern has ball")
        code, d, err = t.json("gps-inspect", "element", "videoconvert")
        src = [x for x in d["padTemplates"] if x["name"] == "src"][0]
        fmt = [f for f in src["structures"][0]["fields"] if f["name"] == "format"][0]
        check_eq((fmt["kind"], fmt["type"]), ("list", "gchararray"), "videoconvert format field")
        width = [f for f in src["structures"][0]["fields"] if f["name"] == "width"][0]
        check_eq((width["kind"], width["type"]), ("range", "gint"), "videoconvert width field")

        code, d, err = t.json("gps-inspect", "element", "no-such-element-xyz")
        check_eq(code, 0, "unknown factory exit (work done, answer is an error)")
        check(is_str(d.get("error")) and "no-such-element-xyz" in d["error"], "unknown factory error text")

        # ---- compatible
        code, d, err = t.json("gps-inspect", "compatible", "--caps", "video/x-raw, format=(string)I420, width=(int)320")
        check_eq(code, 0, "compatible exit")
        check_eq(list(d.keys()), ["factories"], "compatible keys")
        f = d["factories"]
        check(all(is_str(x) for x in f) and "videoconvert" in f and "fakesink" in f and "audioconvert" not in f, "compatible video content")
        code, d, err = t.json("gps-inspect", "compatible")
        check("videotestsrc" not in d["factories"] and "fakesink" in d["factories"] and len(d["factories"]) > 100, "compatible without caps")
        code, d, err = t.json("gps-inspect", "compatible", "--caps", "video/x-raw, width=(int")
        check_eq(code, 0, "compatible bad caps exit")
        check(is_str(d.get("error")), "compatible bad caps has error")

        # ---- caps
        code, d, err = t.json("gps-inspect", "caps", "video/x-raw,width=(int)640,framerate=(fraction)[ 0/1, 30/1 ]")
        check_eq(code, 0, "caps exit")
        has_types(d, {"ok": is_bool, "text": is_str, "structures": lambda v: isinstance(v, list)}, "caps")
        check_eq(d["ok"], True, "caps ok")
        check_eq(d["text"], "video/x-raw, width=(int)640, framerate=(fraction)[ 0/1, 30/1 ]", "caps normalized text")
        for s in d["structures"]:
            check_structure(s, "caps.structures")
        check_eq([ (x["name"], x["kind"], x["type"]) for x in d["structures"][0]["fields"]],
                 [("width", "fixed", "gint"), ("framerate", "range", "GstFraction")], "caps fields")
        code, d, err = t.json("gps-inspect", "caps", "video/x-raw, width=(int")
        check_eq(code, 0, "bad caps exit")
        has_types(d, {"ok": is_bool, "error": is_str}, "bad caps")
        check_eq(d["ok"], False, "bad caps ok flag")

        # ---- usage errors: exit 2, stderr has the text, stdout still one JSON line
        for args in ([], ["bogus"], ["element"], ["caps"], ["list", "extra"], ["compatible", "--caps"], ["version", "extra"]):
            code, d, err = t.json("gps-inspect", *args)
            check_eq(code, 2, f"usage exit for {args}")
            check(is_str(d.get("error")) if d else False, f"usage JSON for {args}")
            check(is_str(d.get("error")) and d["error"] in err, f"usage text on stderr for {args}")

        # ---- logs go to stderr, never into stdout
        code, d, err = t.json("gps-inspect", "element", "queue", env={"GST_DEBUG": "4"})
        check("factory" in d, "GST_DEBUG does not break stdout")
        check(len(err) > 1000, "GST_DEBUG output is on stderr")
    finally:
        t.cleanup()
    return finish("cli_inspect_test")


sys.exit(main())
