"""The feed table of the sweep: for a media type, a small chain of elements that produces it.

This file is TEST INFRASTRUCTURE (SPEC 1.2): it may name elements. Product code never does.

A feed is a source plus transform elements. SEEDS are written by hand (a test source, sample files).
Everything else is found by a breadth-first search over 'gps-inspect element' data: a factory with
one Always sink and one Always src (or only Request sinks and one Always src, like a muxer) is an edge
from the media type of its sink structure to the media type of its src structure. A chain found this
way is only used after it was RUN once (validate), and the dut is only fed with a coded feed when
'gps-inspect compatible --caps <the feed's real output caps>' lists the dut.
"""
import collections

SAMPLES = "/opt/nvidia/deepstream/deepstream/samples/streams"
RAW = ("video/x-raw", "audio/x-raw")

# (id, factory, props, filter). filter = caps text on the link behind the element, "" = none.
SEEDS = [
    ("video/x-raw", [("videotestsrc", {"num-buffers": "30"}, "video/x-raw")]),
    ("audio/x-raw", [("audiotestsrc", {"num-buffers": "30"}, "audio/x-raw")]),
    ("h264 es", [("filesrc", {"location": SAMPLES + "/sample_720p.h264"}, ""), ("h264parse", {}, "video/x-h264")]),
    ("mp4 file", [("filesrc", {"location": SAMPLES + "/sample_720p.mp4"}, "")]),
    ("wav file", [("filesrc", {"location": SAMPLES + "/sonyc_mixed_audio.wav"}, "")]),
    ("jpeg file", [("filesrc", {"location": SAMPLES + "/sample_720p.jpg"}, "")]),
]

MAX_DEPTH = 5
MAX_ALTERNATIVES = 40
TRIES_PER_KEY = 3


def key_of(media, features):
    """'video/x-raw' + ['memory:NVMM'] -> 'video/x-raw(memory:NVMM)'"""
    return media + ("(" + ",".join(features) + ")" if features else "")


def key_of_caps(caps_text):
    """Key of the first structure of a caps string, e.g. 'video/x-h264, stream-format=...' -> 'video/x-h264'."""
    head = caps_text.split(",")[0].strip()
    return head.replace(" ", "")


def template_keys(template):
    return [key_of(s["media"], s["features"]) for s in template["structures"]]


def is_raw(key):
    return key.split("(")[0] in RAW


def make_edges(infos):
    """Transform and mux factories as edges: dict(factory, rank, inKey, inPad, outKey)."""
    edges = []
    for factory, d in sorted(infos.items()):
        if d.get("isSink") or d.get("isSource"):
            continue
        sinks = [t for t in d["padTemplates"] if t["direction"] == "sink"]
        srcs = [t for t in d["padTemplates"] if t["direction"] == "src"]
        if len(srcs) != 1 or srcs[0]["presence"] != "always" or not srcs[0]["structures"]:
            continue
        if len(sinks) == 1 and sinks[0]["presence"] == "always":
            in_templates = sinks
        elif sinks and all(t["presence"] == "request" for t in sinks):
            in_templates = sinks
        else:
            continue
        for t in in_templates:
            for in_key in template_keys(t):
                for out_key in template_keys(srcs[0]):
                    if in_key != out_key:
                        edges.append({"factory": factory, "rank": d["rank"], "inKey": in_key, "inPad": t["name"],
                                      "outPad": srcs[0]["name"], "outKey": out_key})
    edges.sort(key=lambda e: (-e["rank"], e["factory"], e["inKey"], e["outKey"]))
    return edges


def seed_feed(label, steps):
    return {"label": label, "key": "", "steps": [
        {"factory": f, "props": dict(p), "filter": flt, "sinkpad": "sink" if i else "", "srcpad": "src"}
        for i, (f, p, flt) in enumerate(steps)]}


def extend(feed, edge):
    """A new feed: feed followed by the edge's element."""
    steps = [dict(s) for s in feed["steps"]]  # the previous element's filter is already its own key
    steps.append({"factory": edge["factory"], "props": {}, "filter": edge["outKey"], "sinkpad": edge["inPad"],
                  "srcpad": edge["outPad"]})
    return {"label": feed["label"] + " ! " + edge["factory"], "key": edge["outKey"], "steps": steps}


def add_to_plan(plan, feed, prefix):
    """Adds the chain to a PlanBuilder. Returns (last element name, its src pad, link caps for the next link)."""
    prev = None
    for i, s in enumerate(feed["steps"]):
        name = f"{prefix}{i}"
        plan.add(name, s["factory"], s["props"])
        if prev is not None:
            plan.link(prev[0], prev[1], name, s["sinkpad"], prev[2])
        prev = (name, s["srcpad"], s["filter"])
    return prev


def raw_key_ok(key):
    return is_raw(key)


class Library:
    """key -> [feed]; every feed was run once and carries outCaps, accepts (factories), ms."""

    def __init__(self):
        self.feeds = {}
        self.failed = []     # [(label, message)] chains that were tried and did not run
        self.tried = 0
        self.attempted = set()   # labels of the chains that were run, so no chain is run twice

    def add(self, feed):
        lst = self.feeds.setdefault(feed["key"], [])
        lst.append(feed)
        lst.sort(key=lambda f: (not f["adaptive"], len(f["steps"])))   # a converter at the end beats a decoder

    def accepts(self, feed, factory):
        """Adaptive feeds (raw output of a source or converter) can be negotiated to anything the dut asks."""
        return feed["adaptive"] or factory in feed["accepts"]


def is_adaptive(feed, infos):
    """Raw output of a source or of a converter (its sink also takes this key): the format is still open."""
    if not is_raw(feed["key"]):
        return False
    d = infos.get(feed["steps"][-1]["factory"])
    if d is None:
        return False
    sinks = [t for t in d["padTemplates"] if t["direction"] == "sink"]
    return not sinks or any(feed["key"] in template_keys(t) for t in sinks)


def build_library(infos, validate, pmap, log):
    """Searches the feeds. validate(feed) -> {'ok','outCaps','accepts','message','ms'}; pmap(fn, items) runs in parallel."""
    lib = Library()
    edges = make_edges(infos)
    log(f"feeds: {len(edges)} edges from {len({e['factory'] for e in edges})} factories")

    gained = [0]

    def settle(feeds):
        for feed, r in zip(feeds, pmap(validate, feeds)):
            lib.tried += 1
            lib.attempted.add(feed["label"])
            if r["ok"]:
                feed["outCaps"], feed["accepts"], feed["ms"] = r["outCaps"], r["accepts"], r["ms"]
                actual = key_of_caps(r["outCaps"])
                if feed["key"] != actual:          # a muxer gave audio/x-matroska where video/x-matroska was wanted
                    if feed["key"]:
                        feed["wanted"] = feed["key"]
                    feed["key"] = actual
                    if feed["steps"][-1]["filter"]:
                        feed["steps"][-1]["filter"] = actual
                if any(f["outCaps"] == feed["outCaps"] for f in lib.feeds.get(actual, [])):
                    continue
                feed["adaptive"] = is_adaptive(feed, infos)
                lib.add(feed)
                gained[0] += 1
            else:
                lib.failed.append((feed["label"], r["message"]))

    settle([seed_feed(label, steps) for label, steps in SEEDS])
    for depth in range(1, MAX_DEPTH + 1):
        # candidates per wanted key, best first: the shortest source feed, then the highest rank
        wanted = {}
        for e in edges:
            if e["outKey"] in lib.feeds or e["inKey"] not in lib.feeds:
                continue
            src = next((f for f in lib.feeds[e["inKey"]] if lib.accepts(f, e["factory"])), None)
            if src is not None and src["label"] + " ! " + e["factory"] not in lib.attempted:
                wanted.setdefault(e["outKey"], []).append((src, e))
        if not wanted:
            break
        gained[0] = 0
        log(f"feeds: depth {depth}, {len(wanted)} media types to find, {sum(map(len, wanted.values()))} candidates")
        for attempt in range(TRIES_PER_KEY):
            batch = [extend(c[attempt][0], c[attempt][1]) for k, c in wanted.items()
                     if k not in lib.feeds and len(c) > attempt]
            if not batch:
                break
            settle(batch)
        if not gained[0]:
            break

    # alternatives: more chains for the coded keys, because a depayloader or decoder wants one particular flavour
    # (encoding-name, stream-format, memory type) and the first chain found is only one of them
    wanted = []
    per_key = collections.Counter()
    for e in edges:
        if e["outKey"] not in lib.feeds or e["outKey"] in ("video/x-raw", "audio/x-raw") or per_key[e["outKey"]] >= MAX_ALTERNATIVES:
            continue
        src = next((f for f in lib.feeds.get(e["inKey"], []) if lib.accepts(f, e["factory"])), None)
        if src is not None and src["label"] + " ! " + e["factory"] not in lib.attempted:
            wanted.append(extend(src, e))
            per_key[e["outKey"]] += 1
    log(f"feeds: {len(wanted)} alternative chains")
    settle(wanted)
    return lib
