#!/usr/bin/env python3
"""GstPipeStudio sweep: every element factory of the image goes through gps-inspect, gps-update and gps-run.

Parent:  sweep.py --bin-dir DIR --report-dir DIR --work-dir DIR [--jobs N] [--only a,b] [--limit N]
Child:   sweep.py --one FACTORY ...      (the parent starts one per factory: timeout and crash isolation)

Phases: A inventory (list, coverage against gst-inspect, inspect every factory, schema)
        B feed library (feeds.py)    C one child per factory    D report (report.py)
SPEC 9 / task: classes tamam, kismi, kurulum-gerek, coktu, takildi, olusturulamadi, besleme-yok
(written with Turkish letters in the report).
"""
import argparse
import collections
import concurrent.futures
import copy
import json
import os
import queue
import re
import shutil
import subprocess
import sys
import threading
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import feeds                                    # noqa: E402
import report                                   # noqa: E402
from sweeplib import (PlanBuilder, check_element_schema, check_list_entry, run_tool, tail)   # noqa: E402

UPDATE_MS = 8000        # preroll wait of the plan updates
RUN_MS = 6000           # gps-run limit
FEED_MS = 5000          # preroll wait when a feed chain is tried
CREATE_MS = 600         # update of a lone element: only creation and property checks matter
MAX_FEED_TRIES = 4       # feeds tried for one dut before it is called besleme-yok
CHILD_TIMEOUT = 420     # seconds, whole test of one factory
SLACK = float(os.environ.get("GPS_SWEEP_SLACK", "20"))   # seconds a tool may need on top of its own timeout before it counts as hung

CLASSES = ["tamam", "kısmi", "kurulum-gerek", "çöktü", "takıldı", "oluşturulamadı", "besleme-yok"]
# ASCII spellings used inside the code
C_OK, C_PART, C_SETUP, C_CRASH, C_HANG, C_NOCREATE, C_NOFEED = CLASSES


# ---------------------------------------------------------------- context: tool calls with a registry slot

class Ctx:
    """Runs the tools with a private GST_REGISTRY and working folder. slot None: take one from the pool per call."""

    def __init__(self, bin_dir, work, slot=None, pool=None):
        self.bin_dir, self.work, self.slot, self.pool = bin_dir, work, slot, pool

    def run(self, tool, args, timeout):
        slot, release = self.slot, False
        if slot is None:
            slot, release = self.pool.get(), True
        try:
            env = {"GST_REGISTRY": os.path.join(self.work, "reg", f"s{slot}.bin")}
            return run_tool(self.bin_dir, tool, args, timeout, env=env, cwd=os.path.join(self.work, "cwd", f"s{slot}"))
        finally:
            if release:
                self.pool.put(slot)

    def update(self, conf_path, timeout_ms):
        return self.run("gps-update", [conf_path, "--timeout-ms", str(timeout_ms)], timeout_ms / 1000 + SLACK)

    def gps_run(self, conf_path, timeout_ms):
        return self.run("gps-run", [conf_path, "--timeout-ms", str(timeout_ms)], timeout_ms / 1000 + SLACK)


def log(msg):
    print(f"[{time.strftime('%H:%M:%S')}] {msg}", file=sys.stderr, flush=True)


def write_conf(path, plan):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as f:
        f.write(plan.to_conf())
    return path


def short(res):
    """A tool result for the steps list (small)."""
    j = res.json or {}
    return {"cmd": " ".join(res.cmd), "ms": round(res.ms, 1), "rc": res.rc, "signal": res.signal, "hung": res.hung,
            "status": j.get("status", j.get("error", "")), "message": str(j.get("message", ""))[:400]}


# ---------------------------------------------------------------- phase A: inventory

def reference_factories(env):
    """Ground truth from gst-inspect-1.0 -a: {factory: {klass, longName, rank, plugin}}; None when the tool is missing."""
    if not shutil.which("gst-inspect-1.0"):
        return None
    p = subprocess.run(["gst-inspect-1.0", "-a"], capture_output=True, text=True, errors="replace", env=env)
    ref, cur, in_plugin = {}, None, False
    for line in p.stdout.split("\n"):
        m = re.match(r"^(\S+): Factory Details:$", line)
        if m:
            cur, in_plugin = m.group(1), False
            ref[cur] = {}
            continue
        m = re.match(r"^(\S+): {3}(Rank|Long-name|Klass|Name) {2,}(.*)$", line)
        if m and m.group(1) == cur:
            key, val = m.group(2), m.group(3)
            if key == "Rank":
                ref[cur]["rank"] = int(re.search(r"\((-?\d+)\)", val).group(1))
            elif key == "Long-name":
                ref[cur]["longName"] = val
            elif key == "Klass":
                ref[cur]["klass"] = val
            elif key == "Name" and in_plugin:
                ref[cur]["plugin"] = val
        if line.endswith(": Plugin Details:"):
            in_plugin = True
    return ref


def blacklisted_plugins(env):
    """Plugin files the scanner could not load: [(file, [missing libraries])]."""
    if not shutil.which("gst-inspect-1.0"):
        return []
    p = subprocess.run(["gst-inspect-1.0", "-b"], capture_output=True, text=True, errors="replace", env=env)
    files = [l.strip() for l in p.stdout.split("\n") if l.startswith("  lib")]
    found = []
    for name in files:
        path = None
        for d in ("/usr/lib/x86_64-linux-gnu/gstreamer-1.0", "/usr/lib/x86_64-linux-gnu/gstreamer-1.0/deepstream"):
            if os.path.exists(os.path.join(d, name)):
                path = os.path.join(d, name)
        missing = []
        if path and shutil.which("ldd"):
            out = subprocess.run(["ldd", path], capture_output=True, text=True, errors="replace").stdout
            missing = [l.split("=>")[0].strip() for l in out.split("\n") if "not found" in l]
        found.append({"file": name, "path": path or "", "missingLibraries": missing})
    return found


def coverage(listing, ref):
    """Compares 'gps-inspect list' with gst-inspect-1.0 -a. Returns a dict of findings."""
    names = [e["factory"] for e in listing]
    out = {"listed": len(names), "reference": None if ref is None else len(ref), "duplicates": [], "missing": [],
           "extra": [], "fieldDiffs": [], "badEntries": []}
    out["duplicates"] = sorted(n for n, c in collections.Counter(names).items() if c > 1)
    for e in listing:
        out["badEntries"] += check_list_entry(e)
    if ref is None:
        return out
    out["missing"] = sorted(set(ref) - set(names))
    out["extra"] = sorted(set(names) - set(ref))
    for e in listing:
        r = ref.get(e["factory"])
        if not r:
            continue
        for ours, theirs in (("klass", "klass"), ("longName", "longName"), ("rank", "rank"), ("plugin", "plugin")):
            if theirs in r and e.get(ours) != r[theirs]:
                out["fieldDiffs"].append(f"{e['factory']}.{ours}: list says {e.get(ours)!r}, gst-inspect says {r[theirs]!r}")
    return out


def inspect_one(ctx, factory, work):
    """Step 1: gps-inspect element succeeds and the JSON matches SPEC 7."""
    r = ctx.run("gps-inspect", ["element", factory], 90)
    out = {"ms": round(r.ms, 1), "ok": False, "problems": [], "crash": 0, "hung": r.hung}
    if r.signal:
        out["crash"] = r.signal
        out["problems"].append(f"gps-inspect element killed by signal {r.signal}")
    elif r.hung:
        out["problems"].append("gps-inspect element did not finish in 90 s")
    elif r.json is None:
        out["problems"].append("stdout: " + r.json_problem + " | stderr: " + tail(r.err, 200))
    elif "error" in r.json:
        out["problems"].append("gps-inspect error: " + str(r.json["error"]))
    else:
        out["problems"] += check_element_schema(r.json, factory)
        if r.json_problem:
            out["problems"].append(r.json_problem)
        if r.rc != 0:
            out["problems"].append(f"exit code {r.rc}")
        os.makedirs(os.path.join(work, "inspect"), exist_ok=True)
        with open(os.path.join(work, "inspect", factory + ".json"), "w") as f:
            json.dump(r.json, f)
        out["ok"] = not out["problems"]
    return out


# ---------------------------------------------------------------- phase B: the feed library

def make_validator(ctx, work):
    counter = [0]
    lock = threading.Lock()

    def validate(feed):
        """Runs the chain with its last src pad free: it must reach preroll and show real caps."""
        with lock:
            counter[0] += 1
            n = counter[0]
        plan = PlanBuilder("feed")
        last = feeds.add_to_plan(plan, feed, "v")
        if last[2]:             # the filter the dut will see: without it a converter just passes its input
            plan.add("vz", "fakesink", {"sync": "false"})
            plan.link(last[0], last[1], "vz", "sink", last[2])
        path = write_conf(os.path.join(work, "feedcheck", f"{n}.conf"), plan)
        r = ctx.update(path, FEED_MS)
        bad = lambda msg: {"ok": False, "message": f"{feed['label']}: {msg}", "ms": r.ms}
        if r.signal or r.hung:
            return bad(f"tool {'killed by signal %d' % r.signal if r.signal else 'hung'}")
        j = r.json
        if j is None or j.get("status") != "ok":
            return bad((j or {}).get("message", r.json_problem)[:200])
        pad = next((p for p in j["pads"] if p["element"] == last[0] and p["isSrc"]), None)
        if pad is None or not pad["caps"] or pad["unknownData"] or j["refused"] or j["missing"]:
            return bad("no usable caps on the last pad: " + str(pad and pad["caps"])[:100])
        key = feeds.key_of_caps(pad["caps"])
        c = ctx.run("gps-inspect", ["compatible", "--caps", pad["caps"]], 60)
        accepts = (c.json or {}).get("factories", [])
        return {"ok": True, "outCaps": pad["caps"], "accepts": accepts, "ms": round(r.ms, 1), "message": ""}

    return validate


# ---------------------------------------------------------------- phase C: one factory

def load_json(path):
    with open(path) as f:
        return json.load(f)


def missing_library(ctx, plugin):
    """For a plugin that does not load: the libraries ldd cannot find."""
    if not shutil.which("gst-inspect-1.0"):
        return ""
    p = subprocess.run(["gst-inspect-1.0", plugin], capture_output=True, text=True, errors="replace",
                       env=dict(os.environ, GST_REGISTRY=os.path.join(ctx.work, "reg", f"s{ctx.slot}.bin")))
    m = re.search(r"Filename\s+(\S+)", p.stdout)
    if not m or not shutil.which("ldd"):
        return ""
    out = subprocess.run(["ldd", m.group(1)], capture_output=True, text=True, errors="replace").stdout
    return ", ".join(l.split("=>")[0].strip() for l in out.split("\n") if "not found" in l)


def sink_templates(info):
    return [t for t in info["padTemplates"] if t["direction"] == "sink"]


def src_templates(info):
    return [t for t in info["padTemplates"] if t["direction"] == "src"]


ANY_PREFS = ["video/x-raw", "audio/x-raw", "video/x-raw(memory:NVMM)", "video/x-h264", "video/quicktime"]   # tried in turn for a sink that says ANY


def pick_feed(lib, factory, template, prefer=None, skip=()):
    """(feed, key) for one sink template, or (None, why). Raw video first, then raw audio, then the shortest chain."""
    keys = feeds.template_keys(template) or [prefer or "video/x-raw"]       # ANY: the caller says what to try

    def order(k):
        fl = lib.feeds.get(k)
        hops = len(fl[0]["steps"]) if fl else 99
        return (0 if k == "video/x-raw" else 1 if k == "audio/x-raw" else 2, hops, k)

    for k in sorted(set(keys), key=order):
        for feed in lib.feeds.get(k, []):
            if lib.accepts(feed, factory) and feed["label"] not in skip:
                return feed, k
    have = [k for k in keys if k in lib.feeds]
    if have:
        return None, f"sink '{template['name']}' wants {', '.join(keys[:6])}; the feed for {', '.join(have[:3])} is not listed by 'gps-inspect compatible' for this element"
    return None, f"sink '{template['name']}' wants {', '.join(keys[:6])}; the feed table has no chain that produces it"


def test_factory(ctx, factory, entry, lib):
    """All steps for one factory. Returns the record that goes into elements.json."""
    t_start = time.monotonic()
    info = load_json(os.path.join(ctx.work, "inspect", factory + ".json"))
    rec = {"factory": factory, "plugin": entry["plugin"], "klass": entry["klass"], "nvidia": entry["nvidia"],
           "class": "", "reason": "", "timings": {}, "padsSeen": [], "findings": [], "steps": [], "feed": [],
           "properties": {}, "repro": ""}
    confdir = os.path.join(ctx.work, "conf", factory)

    def finish(cls, reason, repro=""):
        rec["class"], rec["reason"], rec["repro"] = cls, reason, repro
        rec["timings"]["totalMs"] = round((time.monotonic() - t_start) * 1000, 1)
        return rec

    def broke(res, step, conf):
        """A tool that was killed or hung ends the test of this factory."""
        rec["steps"].append(dict(short(res), step=step, conf=conf))
        if res.signal:
            return finish(C_CRASH, f"{step}: {res.cmd[0]} signal {res.signal} ile öldü", conf)
        return finish(C_HANG, f"{step}: {res.cmd[0]} {res.ms / 1000:.0f} sn içinde bitmedi, öldürüldü", conf)

    # ---- 2. creation: a lone element
    plan = PlanBuilder("create")
    plan.add("dut", factory)
    conf = write_conf(os.path.join(confdir, "create.conf"), plan)
    r = ctx.update(conf, CREATE_MS)
    rec["timings"]["createMs"] = round(r.ms, 1)
    if r.signal or r.hung:
        return broke(r, "create", conf)
    rec["steps"].append(dict(short(r), step="create", conf=conf))
    j = r.json or {}
    if r.json is None:
        rec["findings"].append({"kind": "update-json", "detail": "create: " + r.json_problem})
    msg = str(j.get("message", ""))
    if j.get("status") == "error" and ("olusturulamadi" in msg or "diye bir element yok" in msg):
        libs = missing_library(ctx, entry["plugin"])
        text = f"{msg}" + (f" | eksik kütüphane: {libs}" if libs else "") + (f" | stderr: {tail(r.err, 300)}" if r.err.strip() else "")
        return finish(C_NOCREATE, text, conf)

    # ---- 3. property defaults written back
    prop_step(ctx, rec, info, factory, confdir)
    if rec["class"]:
        return rec

    # ---- 4. smallest plan. When the dut does not take the feed, other feeds of the same media are tried, and a sink
    # that says ANY (and still refuses video) is offered other media in turn.
    any_sink = any(not t["structures"] for t in sink_templates(info) if t["presence"] != "sometimes")
    skip, tried, first = set(), [], None
    for prefer in (ANY_PREFS if any_sink else [None]):
        for _ in range(MAX_FEED_TRIES):
            snapshot = copy.deepcopy(rec)
            out = plan_step(ctx, rec, info, factory, lib, confdir, finish, broke, prefer, skip)
            if out["class"] != C_NOFEED:
                return out
            used = [f["chain"] for f in rec["feed"]]
            first = first or (out["reason"], out["repro"])
            tried += used
            rec.clear()
            rec.update(snapshot)
            rec["class"] = ""
            if not used or not (set(used) - skip):
                break
            skip |= set(used)
    reason = first[0] if first else "besleme kurulamadı"
    if tried:
        reason += " (denenen besleme: " + "; ".join(dict.fromkeys(tried)) + ")"
    return finish(C_NOFEED, reason, first[1] if first else "")


def prop_step(ctx, rec, info, factory, confdir):
    skipped = collections.Counter()
    testable = []
    for p in info["properties"]:
        if not p["writable"]:
            continue
        if p["deprecated"]:
            skipped["deprecated"] += 1
        elif p["constructOnly"]:
            skipped["construct-only"] += 1
        elif p["kind"] in ("object", "boxed", "pointer", "other"):
            skipped["no text form (" + p["kind"] + ")"] += 1
        else:
            testable.append(p)
    result = {"writable": sum(1 for p in info["properties"] if p["writable"]), "tested": len(testable),
              "skipped": dict(skipped), "rejected": []}
    rec["properties"] = result
    t0 = time.monotonic()

    def attempt(props, tag):
        plan = PlanBuilder("props")
        plan.add("dut", factory, {p["name"]: p["default"] for p in props})
        conf = write_conf(os.path.join(confdir, f"props_{tag}.conf"), plan)
        return ctx.update(conf, CREATE_MS), conf

    if testable:
        r, conf = attempt(testable, "all")
        m = str((r.json or {}).get("message", ""))
        died = bool(r.signal or r.hung)
        if died:
            rec["findings"].append({"kind": "props-tool", "conf": conf, "detail": f"all defaults at once: {short(r)['cmd']} " +
                                    (f"signal {r.signal}" if r.signal else "hung") + "; the properties are tried one by one"})
        if died or ((r.json or {}).get("status") == "error" and " gecersiz (" in m):
            for p in testable:
                r1, conf1 = attempt([p], re.sub(r"[^A-Za-z0-9_]", "_", p["name"]))
                j1 = r1.json or {}
                if r1.signal or r1.hung:
                    result["rejected"].append({"name": p["name"], "kind": p["kind"], "text": p["default"], "conf": conf1,
                                               "reason": "tool " + (f"signal {r1.signal}" if r1.signal else "hung") +
                                                         " when this default is written"})
                elif j1.get("status") == "error" and " gecersiz (" in str(j1.get("message", "")):
                    reason = str(j1["message"])
                    result["rejected"].append({"name": p["name"], "kind": p["kind"], "text": p["default"],
                                               "reason": reason[reason.index(" gecersiz (") + 1:], "conf": conf1})
    for rej in result["rejected"]:
        rec["findings"].append({"kind": "property-default-crash" if rej["reason"].startswith("tool ") else "property-default-rejected",
                                "conf": rej.get("conf", ""), "detail": f"{rej['name']} ({rej['kind']}) = {rej['text']!r}: {rej['reason']}"})
    rec["timings"]["propsMs"] = round((time.monotonic() - t0) * 1000, 1)


def feed_into(plan, lib, factory, info, rec, prefer=None, skip=()):
    """Adds feeds for the sink templates of the dut. Returns '' or the reason a feed is missing."""
    n = 0
    sinks = sink_templates(info)
    for t in sinks:
        if t["presence"] == "sometimes":
            return f"sink '{t['name']}' is a Sometimes template: no way to feed it"
    for t in [t for t in sinks if t["presence"] == "always"]:
        feed, key = pick_feed(lib, factory, t, prefer, skip)
        if feed is None:
            return key
        last = feeds.add_to_plan(plan, feed, f"f{n}_")
        plan.link(last[0], last[1], "dut", t["name"], key)
        rec["feed"].append({"sink": t["name"], "chain": feed["label"], "key": key})
        n += 1
    reqs = [t for t in sinks if t["presence"] == "request"]
    reasons = []
    for t in reqs:                       # one Request sink is enough: the first that can be fed
        feed, key = pick_feed(lib, factory, t, prefer, skip)
        if feed is None:
            reasons.append(key)
            continue
        last = feeds.add_to_plan(plan, feed, f"f{n}_")
        plan.link(last[0], last[1], "dut", t["name"], key)
        rec["feed"].append({"sink": t["name"], "chain": feed["label"], "key": key})
        rec["requestSinkTemplate"] = t["name"]
        n += 1
        break
    else:
        if reqs:
            return "; ".join(reasons)
    return ""


def pads_of(j):
    return [{"pad": p["pad"], "identity": p["identity"], "template": p["template"], "presence": p["presence"],
             "dir": "src" if p["isSrc"] else "sink", "linked": p["linked"], "temporaryEnd": p["temporaryEnd"],
             "unknownData": p["unknownData"], "media": feeds.key_of_caps(p["caps"]) if p["caps"] else ""}
            for p in j.get("pads", [])]


def plan_step(ctx, rec, info, factory, lib, confdir, finish, broke, prefer=None, skip=()):
    plan = PlanBuilder("sweep")
    props = {}
    num = [p for p in info["properties"] if p["name"] == "num-buffers" and p["writable"] and p["kind"] in ("int", "uint")]
    if info["isSource"] and num:
        props["num-buffers"] = "10"
    request_src = {t["name"]: 1 for t in src_templates(info) if t["presence"] == "request"}
    plan.add("dut", factory, props, request_src)
    why = feed_into(plan, lib, factory, info, rec, prefer, skip)
    if why:
        return finish(C_NOFEED, why)

    j, r, conf, extra = None, None, "", 0
    for rnd in range(4):
        conf = write_conf(os.path.join(confdir, f"plan{rnd}.conf"), plan)
        r = ctx.update(conf, UPDATE_MS)
        rec["timings"][f"plan{rnd}Ms"] = round(r.ms, 1)
        if r.signal or r.hung:
            return broke(r, f"plan round {rnd}", conf)
        rec["steps"].append(dict(short(r), step=f"plan round {rnd}", conf=conf))
        j = r.json
        if j is None:
            rec["findings"].append({"kind": "update-json", "detail": r.json_problem})
            return finish(C_SETUP, f"gps-update JSON üretmedi (çıkış kodu {r.rc}): {r.json_problem} | stderr: {tail(r.err, 160)}", conf)
        rec["padsSeen"] = pads_of(j)
        if rnd == 0:
            request_findings(rec, info, j)
        if j["status"] != "ok":
            break
        free = [p for p in j["pads"] if p["isSrc"] and not p["linked"] and p["element"] == "dut"]
        if not free:
            break
        for p in free:
            sink = f"z{extra}"
            extra += 1
            plan.add(sink, "fakesink", {"sync": "false"})
            plan.link(p["element"], p["identity"], sink, "sink")

    return decide(ctx, rec, info, factory, plan, j, r, conf, finish, broke)


def request_findings(rec, info, j):
    """Step 3: Request pads work when asked without a number."""
    for t in src_templates(info):
        if t["presence"] != "request":
            continue
        ok = any(p["isSrc"] and p["template"] == t["name"] and p["element"] == "dut" for p in j["pads"])
        if j["status"] == "ok" and not ok:
            rec["findings"].append({"kind": "request-src", "detail": f"template {t['name']}: no src pad appeared although requestSrc=1"})
        rec.setdefault("requestSrc", {})[t["name"]] = "ok" if ok else ("belirsiz: " + j["status"] if j["status"] != "ok" else "pad yok")
    tmpl = rec.get("requestSinkTemplate")
    if tmpl:
        ok = any((not p["isSrc"]) and p["template"] == tmpl and p["linked"] and p["element"] == "dut" for p in j["pads"])
        if j["status"] == "ok" and not ok:
            rec["findings"].append({"kind": "request-sink", "detail": f"template {tmpl}: no linked sink pad in the report"})
        rec["requestSink"] = {tmpl: "ok" if ok else ("belirsiz: " + j["status"] if j["status"] != "ok" else "bağlı pad yok")}


NEGOTIATION = re.compile(r"not-negotiated|not negotiated|negotiat|no common|could not link|incompatible", re.I)


def decide(ctx, rec, info, factory, plan, j, r, conf, finish, broke):
    status, message = j["status"], str(j.get("message", ""))
    refused = j.get("refused", [])
    feed_refusal = [x for x in refused if x.startswith("f") and "-> dut." in x]
    err_elem = j.get("errorElement", "")
    if status == "error":
        if re.match(r"^f\d+_\d+$", err_elem) or re.match(r"^f\d+_\d+:", message):
            return finish(C_NOFEED, f"besleme zinciri hata verdi ({err_elem}): {message}", conf)
        if rec["feed"] and NEGOTIATION.search(message):
            return finish(C_NOFEED, f"uyuşmayan besleme olabilir: {message}", conf)
        return finish(C_SETUP, message, conf)
    if status == "timeout":
        if feed_refusal:
            return finish(C_NOFEED, "besleme bağlantısı reddedildi: " + "; ".join(feed_refusal)[:400], conf)
        return finish(C_HANG, message + (" | reddedilen: " + "; ".join(refused)[:300] if refused else ""), conf)
    if status == "pad-missing":
        return finish(C_PART, f"pad-missing: {message} | eksik: {j.get('missing')}", conf)
    if feed_refusal:
        return finish(C_NOFEED, "besleme bağlantısı reddedildi: " + "; ".join(feed_refusal)[:400], conf)
    if not j.get("complete"):
        left = [f"{p['element']}.{p['identity']}" + (" (bilinmeyen veri)" if p["unknownData"] else "")
                for p in j["pads"] if p["isSrc"] and (not p["linked"] or p["unknownData"])]
        parts = []
        if left:
            parts.append("boşta/bilinmeyen pad: " + ", ".join(left[:8]))
        if j.get("missing"):
            parts.append("oluşmayan: " + ", ".join(j["missing"][:5]))
        if refused:
            parts.append("reddedilen: " + "; ".join(refused)[:300])
        return finish(C_PART, "; ".join(parts) or "güncelleme tamamlanmadı (complete=false)", conf)

    # complete: really run it
    t0 = time.monotonic()
    rr = ctx.gps_run(conf, RUN_MS)
    rec["timings"]["runMs"] = round(rr.ms, 1)
    if rr.signal or rr.hung:
        return broke(rr, "run", conf)
    rec["steps"].append(dict(short(rr), step="run", conf=conf))
    jr = rr.json
    if jr is None:
        rec["findings"].append({"kind": "run-json", "detail": rr.json_problem})
        return finish(C_SETUP, "gps-run JSON üretmedi: " + rr.json_problem, conf)
    st = jr.get("status")
    if st == "eos":
        return finish(C_OK, f"güncelleme tamam, çalıştırma EOS ile bitti ({jr.get('ms', 0):.0f} ms)", conf)
    if st == "timeout":
        if info["isSource"]:
            return finish(C_OK, f"güncelleme tamam, çalıştırma {RUN_MS} ms boyunca hatasız aktı (kaynak EOS vermiyor)", conf)
        return finish(C_PART, f"güncelleme tamam ama çalıştırmada {RUN_MS} ms içinde EOS gelmedi: {jr.get('message', '')}", conf)
    if st == "error":
        return finish(C_SETUP, "çalıştırma hatası: " + str(jr.get("message", "")), conf)
    return finish(C_PART, f"gps-run durumu {st}: {jr.get('message', '')}", conf)


def inspect_failed_record(ctx, entry, ins):
    """gps-inspect element gave no usable answer: the class follows from how it failed."""
    f = entry["factory"]
    rec = {"factory": f, "plugin": entry["plugin"], "klass": entry["klass"], "nvidia": entry["nvidia"], "class": "",
           "reason": "", "timings": {"inspectMs": ins["ms"]}, "padsSeen": [], "findings": [], "steps": [], "feed": [],
           "properties": {}, "repro": "", "inspect": {"ok": False, "problems": ins["problems"][:10]}}
    text = "; ".join(ins["problems"])[:400]
    if ins["crash"]:
        rec["class"], rec["reason"] = C_CRASH, "gps-inspect element: " + text
    elif ins["hung"]:
        rec["class"], rec["reason"] = C_HANG, "gps-inspect element: " + text
    else:
        libs = missing_library(Ctx(ctx.bin_dir, ctx.work, slot=0), entry["plugin"])
        rec["class"] = C_NOCREATE
        rec["reason"] = "gps-inspect element: " + text + (f" | eksik kütüphane: {libs}" if libs else "")
    rec["findings"].append({"kind": "inspect", "detail": text})
    return rec


# ---------------------------------------------------------------- child entry

def child_main(args):
    ctx = Ctx(args.bin_dir, args.work_dir, slot=args.slot)
    entries = {e["factory"]: e for e in load_json(os.path.join(args.work_dir, "list.json"))["elements"]}
    lib = feeds.Library()
    lib.feeds = load_json(os.path.join(args.work_dir, "feeds.json"))["feeds"]
    rec = test_factory(ctx, args.one, entries[args.one], lib)
    print(json.dumps(rec))
    return 0


# ---------------------------------------------------------------- parent

def run_child(args, factory, slots):
    slot = slots.get()
    try:
        cmd = [sys.executable, os.path.abspath(__file__), "--one", factory, "--bin-dir", args.bin_dir,
               "--work-dir", args.work_dir, "--slot", str(slot)]
        t0 = time.monotonic()
        p = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, start_new_session=True)
        try:
            out, err = p.communicate(timeout=CHILD_TIMEOUT)
            timed_out = False
        except subprocess.TimeoutExpired:
            import signal
            try:
                os.killpg(p.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            out, err = p.communicate()
            timed_out = True
        ms = (time.monotonic() - t0) * 1000
        err = err.decode("utf-8", "replace")
        if timed_out:
            return {"factory": factory, "orchestration": f"child test of {factory} did not finish in {CHILD_TIMEOUT} s", "ms": ms}
        lines = [l for l in out.decode("utf-8", "replace").split("\n") if l.strip()]
        if p.returncode != 0 or not lines:
            return {"factory": factory, "orchestration": f"child exit {p.returncode}: {tail(err, 800)}", "ms": ms}
        try:
            rec = json.loads(lines[-1])
        except ValueError as e:
            return {"factory": factory, "orchestration": f"child output is not JSON: {e}", "ms": ms}
        return rec
    finally:
        slots.put(slot)


def parent_main(args):
    t_all = time.monotonic()
    args.bin_dir, args.work_dir, args.report_dir = (os.path.abspath(x) for x in (args.bin_dir, args.work_dir, args.report_dir))
    shutil.rmtree(args.work_dir, ignore_errors=True)
    for sub in ("reg", "cwd", "inspect", "conf", "feedcheck"):
        os.makedirs(os.path.join(args.work_dir, sub))
    os.makedirs(args.report_dir, exist_ok=True)
    jobs = args.jobs

    # warm registry: one scan, then a private copy per slot
    base = os.path.join(args.work_dir, "reg", "base.bin")
    env = dict(os.environ, GST_REGISTRY=base)
    t0 = time.monotonic()
    res = run_tool(args.bin_dir, "gps-inspect", ["list"], 600, env=env)
    if res.json is None or "elements" not in res.json:
        log("gps-inspect list failed: " + tail(res.err) + res.json_problem)
        return 2
    log(f"registry scan + list: {time.monotonic() - t0:.1f} s, {len(res.json['elements'])} factories")
    with open(os.path.join(args.work_dir, "list.json"), "w") as f:
        json.dump(res.json, f)
    for s in range(jobs):
        shutil.copy(base, os.path.join(args.work_dir, "reg", f"s{s}.bin"))
        os.makedirs(os.path.join(args.work_dir, "cwd", f"s{s}"))
    listing = res.json["elements"]
    list_ms = res.ms

    # phase A
    ref = reference_factories(env)
    cov = coverage(listing, ref)
    cov["listMs"] = round(list_ms, 1)
    cov["blacklisted"] = blacklisted_plugins(env)
    log(f"coverage: listed {cov['listed']}, reference {cov['reference']}, missing {len(cov['missing'])}, extra {len(cov['extra'])}, "
        f"duplicates {len(cov['duplicates'])}, field diffs {len(cov['fieldDiffs'])}")

    names = [e["factory"] for e in listing]
    if args.only:
        wanted = set(args.only.split(","))
        names = [n for n in names if n in wanted]
    if args.limit:
        names = names[:args.limit]

    slots = queue.Queue()
    for s in range(jobs):
        slots.put(s)
    ctx = Ctx(args.bin_dir, args.work_dir, pool=slots)
    pool = concurrent.futures.ThreadPoolExecutor(max_workers=jobs)
    pmap = lambda fn, items: list(pool.map(fn, items))

    t0 = time.monotonic()
    inspected = dict(zip(names, pmap(lambda n: inspect_one(ctx, n, args.work_dir), names)))
    log(f"inspect: {len(names)} factories in {time.monotonic() - t0:.1f} s, "
        f"{sum(1 for v in inspected.values() if not v['ok'])} with problems")

    # phase B
    infos = {}
    for n in names:
        p = os.path.join(args.work_dir, "inspect", n + ".json")
        if os.path.exists(p):
            infos[n] = load_json(p)
    if args.only or args.limit:       # the feed search needs every element
        rest = [e["factory"] for e in listing if e["factory"] not in infos]
        pmap(lambda n: inspect_one(ctx, n, args.work_dir), rest)
        for n in rest:
            p = os.path.join(args.work_dir, "inspect", n + ".json")
            if os.path.exists(p):
                infos[n] = load_json(p)
    t0 = time.monotonic()
    lib = feeds.build_library(infos, make_validator(ctx, args.work_dir), pmap, log)
    feed_ms = (time.monotonic() - t0) * 1000
    log(f"feeds: {len(lib.feeds)} media types with a feed, {lib.tried} chains tried, {len(lib.failed)} failed, {feed_ms / 1000:.1f} s")
    with open(os.path.join(args.work_dir, "feeds.json"), "w") as f:
        json.dump({"feeds": lib.feeds}, f)

    # phase C
    t0 = time.monotonic()
    records = []
    done = 0
    entries = {e["factory"]: e for e in listing}
    for n in names:
        if not os.path.exists(os.path.join(args.work_dir, "inspect", n + ".json")):
            records.append(inspect_failed_record(ctx, entries[n], inspected[n]))   # no JSON, no child needed
    futures = {pool.submit(run_child, args, n, slots): n for n in names if not any(r["factory"] == n for r in records)}
    for fut in concurrent.futures.as_completed(futures):
        rec = fut.result()
        records.append(rec)
        done += 1
        if done % 25 == 0 or done == len(names):
            log(f"children: {done}/{len(names)} ({time.monotonic() - t0:.0f} s)")
    pool.shutdown()
    records.sort(key=lambda r: r["factory"])

    # merge phase A results into the records
    orchestration = []
    for rec in records:
        if "orchestration" in rec:
            orchestration.append(rec)
            continue
        ins = inspected[rec["factory"]]
        rec["timings"]["inspectMs"] = ins["ms"]
        rec["inspect"] = {"ok": ins["ok"], "problems": ins["problems"][:10]}
        for prob in ins["problems"][:10]:
            rec["findings"].append({"kind": "inspect-schema", "detail": prob})
    summary = {"jobs": jobs, "wallSeconds": round(time.monotonic() - t_all, 1), "feedSeconds": round(feed_ms / 1000, 1),
               "feedsTried": lib.tried, "feedsFailed": [{"chain": a, "message": b} for a, b in lib.failed],
               "mediaTypesWithFeed": sorted(lib.feeds), "orchestrationErrors": orchestration, "coverage": cov,
               "factoriesTested": len(names)}
    report.write(args, records, summary, lib)
    log(f"done in {time.monotonic() - t_all:.0f} s; orchestration errors: {len(orchestration)}")
    counts = collections.Counter(r.get("class", "?") for r in records)
    log("classes: " + ", ".join(f"{c}={counts.get(c, 0)}" for c in CLASSES))
    return 1 if orchestration or cov["missing"] or cov["extra"] or cov["duplicates"] else 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--bin-dir", required=True)
    ap.add_argument("--work-dir", required=True)
    ap.add_argument("--report-dir", default="")
    ap.add_argument("--jobs", type=int, default=6)
    ap.add_argument("--only", default="")
    ap.add_argument("--limit", type=int, default=0)
    ap.add_argument("--one", default="")
    ap.add_argument("--slot", type=int, default=0)
    args = ap.parse_args()
    return child_main(args) if args.one else parent_main(args)


if __name__ == "__main__":
    sys.exit(main())
