"""Shared bits of the cli tests: run a helper, parse its one JSON line, tiny check functions."""
import json
import os
import subprocess
import sys
import tempfile

checks = 0
failures = 0


def check(condition, text):
    global checks, failures
    checks += 1
    if not condition:
        failures += 1
        print(f"FAIL {text}")


def check_eq(actual, expected, text):
    check(actual == expected, f"{text}: got {actual!r}, want {expected!r}")


def finish(name):
    print(f"{name}: {checks} checks, {failures} failures")
    return 1 if failures else 0


class Tools:
    def __init__(self, bin_dir, fixture_script):
        self.bin = bin_dir
        self.tmp = tempfile.mkdtemp(prefix="gps-cli-")
        subprocess.run([fixture_script, self.tmp], check=True, capture_output=True)

    def path(self, name):
        return os.path.join(self.tmp, name)

    def write(self, name, text):
        with open(self.path(name), "w") as f:
            f.write(text)
        return self.path(name)

    def run(self, tool, *args, env=None, timeout=60):
        """Returns (exit code, stdout text, stderr text)."""
        full_env = dict(os.environ)
        full_env.update(env or {})
        p = subprocess.run([os.path.join(self.bin, tool), *args], capture_output=True, text=True, errors="replace", env=full_env, timeout=timeout)
        return p.returncode, p.stdout, p.stderr

    def json(self, tool, *args, env=None, timeout=60):
        """Runs, checks that stdout is exactly one JSON line, returns (exit code, object, stderr)."""
        code, out, err = self.run(tool, *args, env=env, timeout=timeout)
        lines = [l for l in out.split("\n") if l != ""]
        check_eq(len(lines), 1, f"{tool} {' '.join(args)[:40]}: stdout lines")
        try:
            obj = json.loads(lines[0]) if lines else None
        except ValueError as e:
            check(False, f"{tool}: stdout is not JSON ({e}): {out[:200]!r}")
            obj = None
        return code, obj, err

    def cleanup(self):
        import shutil
        shutil.rmtree(self.tmp, ignore_errors=True)


def is_str(v):
    return isinstance(v, str)


def is_bool(v):
    return isinstance(v, bool)


def is_int(v):
    return isinstance(v, int) and not isinstance(v, bool)


def is_num(v):
    return isinstance(v, (int, float)) and not isinstance(v, bool)


def has_types(obj, spec, where):
    """spec: {key: predicate}. The object must have exactly these keys, each of the right type."""
    check(isinstance(obj, dict), f"{where}: not an object")
    if not isinstance(obj, dict):
        return
    check_eq(sorted(obj.keys()), sorted(spec.keys()), f"{where}: keys")
    for key, pred in spec.items():
        if key in obj:
            check(pred(obj[key]), f"{where}.{key}: wrong type, value {obj[key]!r}")


def conf(elements, links, exposed=()):
    """A .conf text. elements: [(name, factory, {prop: value}, {template: n})], links: [(src, srcpad, sink, sinkpad, caps, {padprop: v})]."""
    out = ["[gstpipestudio]", "version=1", "name=t", ""]
    for name, factory, props, *rest in elements:
        out += [f"[element:{name}]", f"factory={factory}"]
        out += [f"prop.{k}={v}" for k, v in props.items()]
        if rest:
            out += [f"request.{k}={v}" for k, v in rest[0].items()]
        out.append("")
    for i, (src, srcpad, sink, sinkpad, *rest) in enumerate(links):
        out += [f"[link:{i}]", f"src={src}", f"srcpad={srcpad}", f"sink={sink}", f"sinkpad={sinkpad}"]
        if rest and rest[0]:
            out.append(f"caps={rest[0]}")
        if len(rest) > 1:
            out += [f"padprop.{k}={v}" for k, v in rest[1].items()]
        out.append("")
    for name, target in exposed:
        out += [f"[expose:{name}]", f"target={target}", ""]
    return "\n".join(out)
