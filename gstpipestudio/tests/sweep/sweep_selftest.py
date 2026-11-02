"""Checks the sweep orchestrator itself with stub tools that crash, hang, lie or fail on purpose.

The stubs wrap the real tools and misbehave only for plans/factories named below, so every class the
sweep reports can be provoked, and a wrong classification makes this test fail.
usage: sweep_selftest.py <real bin dir> <work dir>
"""
import json
import os
import stat
import subprocess
import sys

here = os.path.dirname(os.path.abspath(__file__))
checks = failures = 0


def check(cond, text):
    global checks, failures
    checks += 1
    if not cond:
        failures += 1
        print("FAIL", text)


UPDATE = r"""#!/bin/bash
conf="$1"
if grep -q 'factory=identity' "$conf"; then kill -SEGV $$; fi
if grep -q 'factory=queue2' "$conf"; then sleep 300; fi
if grep -q 'name=props' "$conf" && grep -q 'factory=fakesink' "$conf" && grep -q '^prop.sync=' "$conf"; then
  echo '{"status":"error","message":"dut.sync: '"'"'true'"'"' gecersiz (stub: reddedildi)","errorElement":"dut","ms":1,"pads":[],"missing":[],"refused":[],"complete":false}'; exit 0; fi
if grep -q 'factory=funnel' "$conf"; then
  echo '{"status":"error","message":"dut: '"'"'funnel'"'"' olusturulamadi","errorElement":"dut","ms":1,"pads":[],"missing":[],"refused":[],"complete":false}'; exit 0; fi
if grep -q 'factory=valve' "$conf"; then
  echo '{"status":"error","message":"dut.drop: konfigurasyon eksik","errorElement":"dut","ms":1,"pads":[],"missing":[],"refused":[],"complete":false}'; exit 0; fi
if grep -q 'factory=concat' "$conf" && grep -q 'factory=fakesink' "$conf"; then
  echo '{"status":"timeout","message":"zaman asimi","errorElement":"","ms":1,"pads":[],"missing":[],"refused":[],"complete":false}'; exit 0; fi
exec REAL/gps-update "$@"
"""

INSPECT = r"""#!/bin/bash
if [ "$1" = element ] && [ "$2" = adder ]; then
  REAL/gps-inspect "$@" | sed 's/"klass":"[^"]*",//'; exit 0; fi
if [ "$1" = element ] && [ "$2" = audiomixer ]; then kill -ABRT $$; fi
exec REAL/gps-inspect "$@"
"""


def main():
    real, work = os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2])
    stubs = os.path.join(work, "stubs")
    os.makedirs(stubs, exist_ok=True)
    for name, text in (("gps-update", UPDATE), ("gps-inspect", INSPECT)):
        path = os.path.join(stubs, name)
        with open(path, "w") as f:
            f.write(text.replace("REAL", real))
        os.chmod(path, 0o755)
    os.symlink(os.path.join(real, "gps-run"), os.path.join(stubs, "gps-run")) if not os.path.exists(os.path.join(stubs, "gps-run")) else None

    names = "identity,queue2,funnel,valve,concat,adder,audiomixer,tee,fakesink,videotestsrc"
    report = os.path.join(work, "report")
    env = dict(os.environ, GPS_SWEEP_SLACK="1")
    p = subprocess.run([sys.executable, os.path.join(here, "sweep.py"), "--bin-dir", stubs, "--work-dir", os.path.join(work, "w"),
                        "--report-dir", report, "--jobs", "4", "--only", names], env=env, capture_output=True, text=True)
    print(p.stderr[-1500:])
    data = json.load(open(os.path.join(report, "elements.json")))
    by = {e["factory"]: e for e in data["elements"]}
    want = {"identity": "çöktü", "queue2": "takıldı", "funnel": "oluşturulamadı", "valve": "kurulum-gerek",
            "audiomixer": "çöktü", "tee": "tamam", "fakesink": "tamam", "videotestsrc": "tamam"}
    for f, cls in want.items():
        check(f in by and by[f]["class"] == cls, f"{f}: class {by.get(f, {}).get('class')!r}, want {cls!r}")
    check("signal 11" in by["identity"]["reason"], "identity reason names the signal: " + by["identity"]["reason"])
    check("kurulum" not in by["valve"]["reason"] and "konfigurasyon eksik" in by["valve"]["reason"], "valve reason is the real message")
    check(any(f["kind"] == "inspect-schema" and "klass" in f["detail"] for f in by["adder"]["findings"]), "adder: schema violation found")
    check(by["audiomixer"]["reason"].startswith("gps-inspect element") and "signal 6" in by["audiomixer"]["reason"], "audiomixer: inspect crash recorded")
    rej = by["fakesink"]["properties"].get("rejected", [])
    check([r["name"] for r in rej] == ["sync"], f"fakesink: only 'sync' rejected (stub), got {[r['name'] for r in rej]}")
    check(any(f["kind"] == "property-default-rejected" and "stub: reddedildi" in f["detail"] for f in by["fakesink"]["findings"]), "property rejection becomes a finding with the real message")
    check(by["tee"]["properties"].get("rejected") == [], "tee: nothing rejected")
    check(by["concat"]["class"] == "takıldı", f"concat (stub timeout): {by['concat']['class']}")
    check(p.returncode == 0, f"sweep exit code {p.returncode}")
    check(data["summary"]["orchestrationErrors"] == [], "no orchestration errors")
    check(os.path.exists(os.path.join(report, "sweep-repro", "identity.conf")), "repro conf of the crasher was copied")
    check("identity" in open(os.path.join(report, "ELEMENTS.md")).read(), "ELEMENTS.md names the crasher")
    cov = data["summary"]["coverage"]
    check(cov["missing"] == [] and cov["extra"] == [], "coverage: nothing missing or extra")
    print(f"sweep_selftest: {checks} checks, {failures} failures")
    return 1 if failures else 0


sys.exit(main())
