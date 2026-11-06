"""Shared helpers of the sweep: run a helper tool with a timeout, write a .conf, check the gps-inspect JSON schema.

Nothing here knows an element by name; the feed table lives in feeds.py (test infrastructure).
"""
import json
import os
import signal
import subprocess
import time


# ---------------------------------------------------------------- running a tool

class ToolResult:
    """What one tool call did. signal is the number of the signal that killed it (0 = none), hung = we killed it."""

    def __init__(self):
        self.rc = None
        self.signal = 0
        self.hung = False
        self.out = ""
        self.err = ""
        self.ms = 0.0
        self.json = None          # the parsed stdout line, None when stdout was not one JSON object
        self.json_problem = ""    # why json is None (empty if fine)
        self.cmd = []

    @property
    def ok(self):
        return not self.signal and not self.hung and self.json is not None


def run_tool(bin_dir, tool, args, timeout, env=None, cwd=None):
    """Runs bin_dir/tool args. A hang is killed after timeout seconds (whole process group); a signal death is recorded."""
    res = ToolResult()
    res.cmd = [tool] + list(args)
    full_env = dict(os.environ)
    full_env.update(env or {})
    t0 = time.monotonic()
    p = subprocess.Popen([os.path.join(bin_dir, tool)] + list(args), stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                         env=full_env, cwd=cwd, start_new_session=True)
    try:
        out, err = p.communicate(timeout=timeout)
    except subprocess.TimeoutExpired:
        res.hung = True
        try:
            os.killpg(p.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        out, err = p.communicate()
    res.ms = (time.monotonic() - t0) * 1000.0
    res.rc = p.returncode
    if p.returncode is not None and p.returncode < 0 and not res.hung:
        res.signal = -p.returncode       # a process we killed ourselves is a hang, not a crash
    res.out = out.decode("utf-8", "replace")
    res.err = err.decode("utf-8", "replace")
    parse_json(res)
    return res


def parse_json(res):
    lines = [l for l in res.out.split("\n") if l.strip() != ""]
    if res.signal or res.hung:
        return
    if len(lines) != 1:
        res.json_problem = f"stdout has {len(lines)} non-empty lines, want exactly 1"
        if not lines:
            return
    try:
        obj = json.loads(lines[-1])
    except ValueError as e:
        res.json_problem = f"stdout is not JSON ({e}): {lines[-1][:120]!r}"
        return
    if not isinstance(obj, dict):
        res.json_problem = "stdout JSON is not an object"
        return
    res.json = obj


def tail(text, n=600):
    text = text.strip()
    return text if len(text) <= n else "..." + text[-n:]


# ---------------------------------------------------------------- .conf writer

def conf_value(v):
    return str(v).replace("\\", "\\\\").replace("\n", "\\n")


class PlanBuilder:
    """A Graph in Python; to_conf() gives the text of SPEC 3."""

    def __init__(self, name="sweep"):
        self.name = name
        self.elements = []   # [name, factory, props, requestSrc]
        self.links = []      # [src, srcpad, sink, sinkpad, caps, padprops]

    def add(self, name, factory, props=None, request_src=None):
        self.elements.append([name, factory, dict(props or {}), dict(request_src or {})])
        return name

    def link(self, src, srcpad, sink, sinkpad, caps="", padprops=None):
        self.links.append([src, srcpad, sink, sinkpad, caps, dict(padprops or {})])

    def has_element(self, name):
        return any(e[0] == name for e in self.elements)

    def to_conf(self):
        out = ["[gstpipestudio]", "version=1", f"name={self.name}", ""]
        for name, factory, props, req in self.elements:
            out += [f"[element:{name}]", f"factory={factory}"]
            out += [f"prop.{k}={conf_value(v)}" for k, v in props.items()]
            out += [f"request.{k}={v}" for k, v in req.items()]
            out.append("")
        for i, (src, srcpad, sink, sinkpad, caps, padprops) in enumerate(self.links):
            out += [f"[link:{i}]", f"src={src}", f"srcpad={srcpad}", f"sink={sink}", f"sinkpad={sinkpad}"]
            if caps:
                out.append(f"caps={conf_value(caps)}")
            out += [f"padprop.{k}={conf_value(v)}" for k, v in padprops.items()]
            out.append("")
        return "\n".join(out)


# ---------------------------------------------------------------- SPEC 7 schema of 'gps-inspect element'

def _is_str(v): return isinstance(v, str)
def _is_bool(v): return isinstance(v, bool)
def _is_int(v): return isinstance(v, int) and not isinstance(v, bool)
def _is_list(v): return isinstance(v, list)


ELEMENT_KEYS = {"factory": _is_str, "longName": _is_str, "klass": _is_str, "description": _is_str, "author": _is_str,
                "plugin": _is_str, "rank": _is_int, "isSource": _is_bool, "isSink": _is_bool,
                "padTemplates": _is_list, "properties": _is_list}
TEMPLATE_KEYS = {"name": _is_str, "direction": lambda v: v in ("src", "sink"),
                 "presence": lambda v: v in ("always", "sometimes", "request"), "caps": _is_str,
                 "structures": _is_list, "padProperties": _is_list}
PROPERTY_KEYS = {"name": _is_str, "nick": _is_str, "blurb": _is_str, "kind": _is_str, "typeName": _is_str,
                 "default": _is_str, "min": _is_str, "max": _is_str, "enumValues": _is_list, "readable": _is_bool,
                 "writable": _is_bool, "construct": _is_bool, "constructOnly": _is_bool, "deprecated": _is_bool,
                 "mutableReady": _is_bool, "mutablePaused": _is_bool, "mutablePlaying": _is_bool}
KINDS = {"boolean", "int", "uint", "int64", "uint64", "float", "double", "string", "enum", "flags", "caps", "fraction",
         "object", "boxed", "pointer", "other"}
NUMERIC = {"int", "uint", "int64", "uint64", "float", "double"}
STRUCTURE_KEYS = {"media": _is_str, "features": lambda v: _is_list(v) and all(_is_str(x) for x in v), "fields": _is_list}
FIELD_KEYS = {"name": _is_str, "type": _is_str, "kind": lambda v: v in ("fixed", "list", "range"),
              "values": lambda v: _is_list(v) and len(v) >= 1 and all(_is_str(x) for x in v)}


def _keys(obj, spec, where, problems):
    if not isinstance(obj, dict):
        problems.append(f"{where}: not an object")
        return False
    if sorted(obj) != sorted(spec):
        problems.append(f"{where}: keys {sorted(obj)} differ from {sorted(spec)}")
    for k, pred in spec.items():
        if k in obj and not pred(obj[k]):
            problems.append(f"{where}.{k}: wrong type or value {str(obj[k])[:60]!r}")
    return True


def _property(p, where, problems):
    if not _keys(p, PROPERTY_KEYS, where, problems):
        return
    if p.get("kind") not in KINDS:
        problems.append(f"{where}: kind {p.get('kind')!r} is not in the SPEC list")
    for v in p.get("enumValues", []) if _is_list(p.get("enumValues")) else []:
        _keys(v, {"nick": _is_str, "name": _is_str, "value": _is_str}, where + ".enumValues", problems)
    if p.get("kind") in NUMERIC:
        for key in ("min", "max", "default"):
            try:
                float(p.get(key, ""))
            except ValueError:
                problems.append(f"{where}.{key}: numeric property but {p.get(key)!r} is not a number")
    if p.get("kind") in ("enum", "flags") and _is_list(p.get("enumValues")) and not p["enumValues"]:
        problems.append(f"{where}: {p['kind']} without enumValues")


def _structure(s, where, problems):
    if not _keys(s, STRUCTURE_KEYS, where, problems):
        return
    for i, f in enumerate(s.get("fields", []) if _is_list(s.get("fields")) else []):
        _keys(f, FIELD_KEYS, f"{where}.fields[{i}]", problems)
        if isinstance(f, dict) and f.get("type", "x") == "":
            problems.append(f"{where}.fields[{i}]: empty type")


def check_element_schema(d, factory):
    """Returns the list of schema violations of a 'gps-inspect element' answer (empty = valid)."""
    problems = []
    if not _keys(d, ELEMENT_KEYS, "element", problems):
        return problems
    if d.get("factory") != factory:
        problems.append(f"element.factory is {d.get('factory')!r}, asked for {factory!r}")
    if _is_list(d.get("padTemplates")):
        for i, t in enumerate(d["padTemplates"]):
            if _keys(t, TEMPLATE_KEYS, f"padTemplates[{i}]", problems):
                for k, s in enumerate(t.get("structures", []) if _is_list(t.get("structures")) else []):
                    _structure(s, f"padTemplates[{i}].structures[{k}]", problems)
                for k, p in enumerate(t.get("padProperties", []) if _is_list(t.get("padProperties")) else []):
                    _property(p, f"padTemplates[{i}].padProperties[{k}]", problems)
    if _is_list(d.get("properties")):
        for i, p in enumerate(d["properties"]):
            _property(p, f"properties[{i}]", problems)
            if isinstance(p, dict) and p.get("name") in ("name", "parent"):
                problems.append(f"properties[{i}]: GstObject property {p['name']!r} must not be listed")
    return problems


LIST_KEYS = {"factory": _is_str, "klass": _is_str, "longName": _is_str, "rank": _is_int, "plugin": _is_str,
             "isSource": _is_bool, "isSink": _is_bool, "hasRequestSrc": _is_bool, "nvidia": _is_bool}


def check_list_entry(e):
    problems = []
    _keys(e, LIST_KEYS, "list entry " + str(e.get("factory") if isinstance(e, dict) else e), problems)
    return problems
