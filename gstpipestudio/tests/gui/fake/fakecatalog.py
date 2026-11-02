"""Test double for the GUI tests: a small element catalog and a reader for the .conf format.
Only the JSON the GUI sees (SPEC 7) matters; nothing here talks to GStreamer."""

ANY = []  # a pad template with no structures is ANY


def fixed(name, gtype, value):
    return {"name": name, "type": gtype, "kind": "fixed", "values": [value]}


def one_of(name, gtype, values):
    return {"name": name, "type": gtype, "kind": "list", "values": values}


def between(name, gtype, low, high):
    return {"name": name, "type": gtype, "kind": "range", "values": [low, high]}


def structure(media, fields=(), features=()):
    return {"media": media, "features": list(features), "fields": list(fields)}


def prop(name, kind, default="", low="", high="", enum=None, writable=True, deprecated=False, blurb=""):
    return {
        "name": name, "nick": name.replace("-", " ").title(), "blurb": blurb or ("The " + name + " property"),
        "kind": kind, "typeName": "G" + kind, "default": default, "min": low, "max": high,
        "enumValues": [{"nick": n, "name": "demo-" + n, "value": str(v)} for n, v in (enum or [])],
        "readable": True, "writable": writable, "construct": False, "constructOnly": False,
        "deprecated": deprecated, "mutableReady": True, "mutablePaused": True, "mutablePlaying": True,
    }


def template(name, direction, presence, structures, pad_properties=()):
    return {"name": name, "direction": direction, "presence": presence, "structures": list(structures),
            "padProperties": list(pad_properties)}


VIDEO_FIELDS = [one_of("format", "gchararray", ["I420", "NV12", "RGBA"]),
                between("width", "gint", "1", "2147483647"), between("height", "gint", "1", "2147483647"),
                between("framerate", "GstFraction", "0/1", "2147483647/1")]
VIDEO = structure("video/x-raw", VIDEO_FIELDS)
NVMM = structure("video/x-raw", VIDEO_FIELDS, ["memory:NVMM"])
AUDIO = structure("audio/x-raw", [one_of("format", "gchararray", ["S16LE", "F32LE"]),
                                  between("rate", "gint", "1", "2147483647"), between("channels", "gint", "1", "2147483647")])
JPEG = structure("image/jpeg", [between("width", "gint", "1", "65535"), between("height", "gint", "1", "65535"),
                                fixed("parsed", "gboolean", "true")])
AVI = structure("video/x-msvideo")
TEXT = structure("text/x-raw", [one_of("format", "gchararray", ["utf8", "pango-markup"])])
FAMILIES = {"video/x-raw": VIDEO, "audio/x-raw": AUDIO, "image/jpeg": JPEG, "video/x-msvideo": AVI}

BUFFERS = prop("num-buffers", "int", "-1", "-1", "2147483647", blurb="Number of buffers to output before sending EOS")
BASE = [prop("name", "string", "element0"), prop("parent", "object")]
PATTERN = [("smpte", 0), ("snow", 1), ("black", 2), ("white", 3), ("red", 4)]


def element(longname, klass, rank, plugin, templates, properties):
    return {"longName": longname, "klass": klass, "rank": rank, "plugin": plugin, "templates": templates,
            "properties": BASE + properties}


CATALOG = {
    "videotestsrc": element("Video test source", "Source/Video", 256, "videotestsrc", [template("src", "src", "always", [VIDEO])],
                            [prop("pattern", "enum", "smpte", enum=PATTERN), BUFFERS, prop("is-live", "boolean", "false"),
                             prop("foreground-color", "uint", "4294967295", "0", "4294967295")]),
    "audiotestsrc": element("Audio test source", "Source/Audio", 256, "audiotestsrc", [template("src", "src", "always", [AUDIO])],
                            [prop("freq", "double", "440", "0", "20000"), prop("volume", "double", "0.8", "0", "1"), BUFFERS,
                             prop("wave", "enum", "sine", enum=[("sine", 0), ("square", 1), ("saw", 2)])]),
    "filesrc": element("File Source", "Source/File", 64, "coreelements", [template("src", "src", "always", ANY)],
                       [prop("location", "string", ""), prop("blocksize", "uint", "4096", "0", "4294967295"),
                        prop("typefind", "boolean", "false", deprecated=True)]),
    "nvarguscamerasrc": element("NvArgusCameraSrc", "Video/Capture", 256, "nvarguscamerasrc",
                                [template("src", "src", "always", [NVMM])],
                                [prop("sensor-id", "int", "0", "0", "8"), BUFFERS]),
    "avidemux": element("Avi demuxer", "Codec/Demuxer", 64, "avi",
                        [template("sink", "sink", "always", [AVI]), template("video_%u", "src", "sometimes", [JPEG]),
                         template("audio_%u", "src", "sometimes", [AUDIO])], []),
    "queue": element("Queue", "Generic", 0, "coreelements",
                     [template("sink", "sink", "always", ANY), template("src", "src", "always", ANY)],
                     [prop("max-size-buffers", "uint", "200", "0", "4294967295"),
                      prop("max-size-time", "uint64", "1000000000", "0", "18446744073709551615"),
                      prop("leaky", "enum", "no", enum=[("no", 0), ("upstream", 1), ("downstream", 2)])]),
    "tee": element("Tee pipe fitting", "Generic", 0, "coreelements",
                   [template("sink", "sink", "always", ANY), template("src_%u", "src", "request", ANY)],
                   [prop("allow-not-linked", "boolean", "false")]),
    "compositor": element("Compositor", "Filter/Editor/Video/Compositor", 256, "compositor",
                          [template("sink_%u", "sink", "request", [VIDEO],
                                    [prop("xpos", "int", "0", "-2147483648", "2147483647"),
                                     prop("ypos", "int", "0", "-2147483648", "2147483647"),
                                     prop("alpha", "double", "1", "0", "1")]),
                           template("src", "src", "always", [VIDEO])],
                          [prop("background", "enum", "checker", enum=[("checker", 0), ("black", 1), ("white", 2)])]),
    "videoconvert": element("Colorspace converter", "Filter/Converter/Video", 256, "videoconvertscale",
                            [template("sink", "sink", "always", [VIDEO]), template("src", "src", "always", [VIDEO])],
                            [prop("dither", "enum", "none", enum=[("none", 0), ("verterr", 1)])]),
    "nvvideoconvert": element("NvVidConv Plugin", "Filter/Converter/Video", 256, "nvvideoconvert",
                              [template("sink", "sink", "always", [NVMM, VIDEO]), template("src", "src", "always", [NVMM, VIDEO])],
                              [prop("gpu-id", "uint", "0", "0", "4294967295")]),
    "jpegdec": element("JPEG image decoder", "Codec/Decoder/Image", 256, "jpeg",
                       [template("sink", "sink", "always", [JPEG]), template("src", "src", "always", [VIDEO])],
                       [prop("max-errors", "int", "0", "-1", "2147483647")]),
    "textoverlay": element("Text overlay", "Filter/Editor/Video", 0, "pango",
                           [template("video_sink", "sink", "always", [VIDEO]), template("text_sink", "sink", "always", [TEXT]),
                            template("src", "src", "always", [VIDEO])],
                           [prop("text", "string", ""), prop("font-desc", "string", ""), prop("shaded-background", "boolean", "false")]),
    "dualsink": element("Dual input", "Generic", 0, "demo",
                        [template("main", "sink", "always", ANY), template("aux", "sink", "always", ANY),
                         template("src", "src", "always", ANY)], []),
    "fakesink": element("Fake Sink", "Sink", 0, "coreelements", [template("sink", "sink", "always", ANY)],
                        [prop("sync", "boolean", "false"), prop("silent", "boolean", "true")]),
    "propdemo": element("Property demo", "Generic", 0, "demo",
                        [template("sink", "sink", "always", ANY), template("src", "src", "always", ANY)],
                        [prop("p-bool", "boolean", "false"), prop("p-int", "int", "5", "-10", "100"),
                         prop("p-uint", "uint", "7", "0", "4294967295"), prop("p-int64", "int64", "0", "-9223372036854775808", "9223372036854775807"),
                         prop("p-uint64", "uint64", "0", "0", "18446744073709551615"), prop("p-float", "float", "0.5", "0", "10"),
                         prop("p-double", "double", "1.5", "-1000", "1000"), prop("p-string", "string", "hello"),
                         prop("p-enum", "enum", "one", enum=[("one", 1), ("two", 2), ("three", 3)]),
                         prop("p-flags", "flags", "a", enum=[("a", 1), ("b", 2), ("c", 4)]),
                         prop("p-caps", "caps", "ANY"), prop("p-fraction", "fraction", "30/1", "0/1", "100/1"),
                         prop("p-object", "object"), prop("p-readonly", "string", "fixed", writable=False),
                         prop("p-old", "int", "0", "0", "9", deprecated=True)]),
}


def is_source(factory):
    return not any(t["direction"] == "sink" for t in CATALOG[factory]["templates"])


def is_nvidia(factory):
    plugin = CATALOG[factory]["plugin"]
    return any(s.startswith(("nv", "deepstream", "nvds")) for s in (plugin, factory))


def structure_text(s):
    head = s["media"] + ("(" + ", ".join(s["features"]) + ")" if s["features"] else "")
    return head + "".join(", %s=%s" % (f["name"], field_text(f)) for f in s["fields"])


def field_text(f):
    short = {"gint": "int", "gchararray": "string", "GstFraction": "fraction", "gboolean": "boolean"}.get(f["type"], f["type"])
    v = f["values"]
    if f["kind"] == "fixed":
        return "(%s)%s" % (short, v[0])
    if f["kind"] == "list":
        return "(%s){ %s }" % (short, ", ".join(v))
    return "(%s)[ %s, %s ]" % (short, v[0], v[1])


def caps_text(structures):
    return "ANY" if not structures else "; ".join(structure_text(s) for s in structures)


# ---- the .conf format of SPEC 3

def unescape(value):
    out, i = "", 0
    while i < len(value):
        if value[i] == "\\" and i + 1 < len(value):
            out += "\n" if value[i + 1] == "n" else value[i + 1]
            i += 2
        else:
            out += value[i]
            i += 1
    return out


def read_plan(path):
    plan = {"complete": False, "elements": {}, "links": [], "order": []}
    section = None
    for raw in open(path, encoding="utf-8").read().split("\n"):
        line = raw.rstrip("\r")
        if not line or line.startswith("#"):
            continue
        if line.startswith("[") and line.endswith("]"):
            kind, _, name = line[1:-1].partition(":")
            if kind == "element":
                section = {"name": name, "factory": "", "props": {}, "request": {}}
                plan["elements"][name] = section
                plan["order"].append(name)
            elif kind == "link":
                section = {"src": "", "srcpad": "", "sink": "", "sinkpad": "", "caps": "", "padprop": {}}
                plan["links"].append(section)
            elif kind == "gstpipestudio":
                section = plan
            else:
                section = {}
            continue
        key, _, value = line.partition("=")
        value = unescape(value)
        if section is plan:
            if key == "complete":
                plan["complete"] = value == "1"
        elif section is not None and "factory" in section:
            if key == "factory":
                section["factory"] = value
            elif key.startswith("prop."):
                section["props"][key[5:]] = value
            elif key.startswith("request."):
                section["request"][key[8:]] = int(value)
        elif section is not None and "srcpad" in section:
            if key.startswith("padprop."):
                section["padprop"][key[8:]] = value
            elif key in section:
                section[key] = value
    return plan
