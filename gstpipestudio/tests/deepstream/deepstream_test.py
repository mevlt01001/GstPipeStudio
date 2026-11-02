"""DeepStream scenarios on the GPU: every plan in plans/ goes through gps-update and gps-run (and the plugin for s6).
Usage: deepstream_test.py <bin dir> <plugin dir> <plans dir> <out dir> <group>      group: fast | infer | expose
Writes <out dir>/deepstream-<group>.json and .md (status, timings and the caps of every pad).
Exit codes: 0 pass, 1 failure, 77 skipped (no DeepStream elements)."""
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "cli"))
from clitest import check, check_eq, finish  # noqa: E402

BIN, PLUGIN, PLANS, OUT, GROUP = sys.argv[1:6]
PLANS = os.environ.get("GPS_DS_PLANS", PLANS)  # a mutated copy of the plans, for the mutation check
CACHE = os.environ.get("GPS_DS_CACHE", "/tmp/gps-ds-cache")  # engine cache of the nvinfer scenario
SAMPLES = "/opt/nvidia/deepstream/deepstream/samples"
results = []  # one row per tool call


def tool(name, *args, env=None, timeout=60):
    """Runs a helper. Returns (exit code or None when it hung, parsed JSON or None, stderr, wall ms)."""
    full_env = dict(os.environ)
    full_env.update(env or {})
    start = time.time()
    try:
        p = subprocess.run([os.path.join(BIN, name), *args], capture_output=True, text=True, errors="replace", env=full_env, timeout=timeout)
    except subprocess.TimeoutExpired as e:
        return None, None, (e.stderr or b"").decode(errors="replace") if isinstance(e.stderr, bytes) else (e.stderr or ""), (time.time() - start) * 1000
    wall = (time.time() - start) * 1000
    lines = [l for l in p.stdout.split("\n") if l]
    obj = None
    try:
        obj = json.loads(lines[-1]) if lines else None
    except ValueError:
        pass
    return p.returncode, obj, p.stderr, wall


def has_nv_elements():
    code, obj, _, _ = tool("gps-inspect", "element", "nvstreammux")
    return code == 0 and obj is not None and "padTemplates" in obj


def plan_path(name):
    return os.path.join(PLANS, name)


def pad_of(update, element, pad):
    for p in update["pads"]:
        if p["element"] == element and p["pad"] == pad:
            return p
    return None


def has(caps, text):
    return text in caps


def update_plan(scenario, plan, timeout_ms=60000, expect_ok=True):
    """gps-update must be ok and complete (expect_ok). Records status, timing and every pad's caps."""
    code, obj, err, wall = tool("gps-update", plan, "--timeout-ms", str(timeout_ms), timeout=timeout_ms / 1000 + 30)
    check(obj is not None, f"{scenario}: gps-update gave no JSON (exit {code}) {err[-300:]!r}")
    if obj is None:
        results.append({"scenario": scenario, "tool": "gps-update", "status": "no-json", "exit": code, "wall_ms": round(wall), "pads": []})
        return None
    results.append({"scenario": scenario, "tool": "gps-update", "status": obj["status"], "message": obj["message"], "ms": round(obj["ms"]),
                    "wall_ms": round(wall), "complete": obj["complete"], "refused": obj["refused"], "missing": obj["missing"],
                    "pads": [{"element": p["element"], "pad": p["pad"], "dir": "src" if p["isSrc"] else "sink", "linked": p["linked"],
                              "temporaryEnd": p["temporaryEnd"], "caps": p["caps"]} for p in obj["pads"]]})
    if expect_ok:
        check_eq(obj["status"], "ok", f"{scenario}: update status ({obj['message'][:200]})")
        check_eq(code, 0, f"{scenario}: update exit code")
        check(obj["complete"], f"{scenario}: update complete (free: {[p['element'] + '.' + p['pad'] for p in obj['pads'] if p['temporaryEnd']]})")
        check_eq(obj["refused"], [], f"{scenario}: nothing refused")
        check_eq(obj["missing"], [], f"{scenario}: nothing missing")
    return obj


def run_plan(scenario, plan, *params, timeout_s=120, expect="eos"):
    code, obj, err, wall = tool("gps-run", plan, *params, timeout=timeout_s)
    status = obj.get("status", "usage-error") if obj else "no-json"
    results.append({"scenario": scenario, "tool": "gps-run " + " ".join(params), "status": status, "exit": code,
                    "ms": round(obj["ms"]) if obj and "ms" in obj else None, "wall_ms": round(wall), "message": obj.get("message", obj.get("error", "")) if obj else err[-300:]})
    check_eq(status, expect, f"{scenario}: run status {(obj or {}).get('message', err[-300:])!r}")
    return code, obj, err


def caps_ok(scenario, update, element, pad, must=(), must_not=()):
    if update is None:
        return
    p = pad_of(update, element, pad)
    check(p is not None, f"{scenario}: pad {element}.{pad} is in the report")
    if p is None:
        return
    for text in must:
        check(has(p["caps"], text), f"{scenario}: {element}.{pad} caps contain {text!r}: {p['caps'][:160]!r}")
    for text in must_not:
        check(not has(p["caps"], text), f"{scenario}: {element}.{pad} caps do not contain {text!r}: {p['caps'][:160]!r}")


NVMM = "video/x-raw(memory:NVMM)"


# ---------------------------------------------------------------- scenarios

def s1():
    print("-- s1: videotestsrc -> nvvideoconvert -> nvstreammux -> tiler -> nvvideoconvert -> nvdsosd -> fakesink")
    n = "s1"
    u = update_plan(n, plan_path("s1_mux_tiler_osd.conf"))
    caps_ok(n, u, "videotestsrc_1", "src", must=("video/x-raw", "width=(int)640"), must_not=("NVMM",))
    caps_ok(n, u, "nvvideoconvert_1", "src", must=(NVMM,))
    caps_ok(n, u, "nvstreammux_1", "src", must=(NVMM, "width=(int)640", "height=(int)480"))
    caps_ok(n, u, "nvmultistreamtiler_1", "src", must=(NVMM, "width=(int)640", "height=(int)480"))
    caps_ok(n, u, "nvvideoconvert_2", "src", must=(NVMM, "format=(string)RGBA"))
    caps_ok(n, u, "nvdsosd_1", "sink", must=(NVMM, "format=(string)RGBA"))
    caps_ok(n, u, "fakesink_1", "sink", must=(NVMM, "format=(string)RGBA"))
    if u:
        names = sorted(p["pad"] for p in u["pads"] if p["element"] == "nvstreammux_1" and not p["isSrc"])
        check_eq(names, ["sink_0"], "s1: the mux Request pad got the numbered name (not the template)")
    run_plan(n, plan_path("s1_mux_tiler_osd.conf"))


def s1b():
    print("-- s1b: nvvideotestsrc (no conversion needed) in front of the mux")
    n = "s1b"
    u = update_plan(n, plan_path("s1b_nvvideotestsrc.conf"))
    caps_ok(n, u, "nvvideotestsrc_1", "src", must=(NVMM,))
    caps_ok(n, u, "nvstreammux_1", "src", must=(NVMM, "width=(int)640"))
    run_plan(n, plan_path("s1b_nvvideotestsrc.conf"))


def s2():
    print("-- s2: NVENC and NVDEC: videotestsrc -> nvvideoconvert -> nvv4l2h264enc -> h264parse -> nvv4l2decoder -> nvvideoconvert -> fakesink")
    n = "s2"
    u = update_plan(n, plan_path("s2_nvenc_nvdec.conf"))
    caps_ok(n, u, "nvv4l2h264enc_1", "sink", must=(NVMM, "width=(int)1280", "height=(int)720"))
    caps_ok(n, u, "nvv4l2h264enc_1", "src", must=("video/x-h264", "byte-stream"), must_not=("NVMM",))
    caps_ok(n, u, "h264parse_1", "src", must=("video/x-h264", "width=(int)1280"))
    caps_ok(n, u, "nvv4l2decoder_1", "src", must=(NVMM, "format=(string)NV12", "width=(int)1280"))
    caps_ok(n, u, "fakesink_1", "sink", must=(NVMM,))
    run_plan(n, plan_path("s2_nvenc_nvdec.conf"))


def s4():
    print("-- s4: two sources -> nvstreammux (two Request sink pads) -> nvstreamdemux -> queue -> fakesink")
    n = "s4"
    u = update_plan(n, plan_path("s4_two_sources_demux.conf"))
    if u:
        sinks = sorted(p["pad"] for p in u["pads"] if p["element"] == "nvstreammux_1" and not p["isSrc"] and p["linked"])
        check_eq(sinks, ["sink_0", "sink_1"], "s4: the mux has two linked Request sink pads")
    caps_ok(n, u, "nvstreammux_1", "src", must=(NVMM, "width=(int)640"))
    caps_ok(n, u, "nvstreamdemux_1", "src_0", must=(NVMM, "width=(int)640"))
    caps_ok(n, u, "fakesink_1", "sink", must=(NVMM,))
    run_plan(n, plan_path("s4_two_sources_demux.conf"))


def s4b():
    print("-- s4b: tee fan-out (queue on both branches): one branch to mux+tiler, the other to NVENC")
    n = "s4b"
    u = update_plan(n, plan_path("s4b_tee_fanout.conf"))
    caps_ok(n, u, "tee_1", "src_0", must=(NVMM,))
    caps_ok(n, u, "tee_1", "src_1", must=(NVMM,))
    caps_ok(n, u, "nvmultistreamtiler_1", "src", must=(NVMM,))
    caps_ok(n, u, "nvv4l2h264enc_1", "src", must=("video/x-h264",))
    caps_ok(n, u, "fakesink_2", "sink", must=("video/x-h264",))
    run_plan(n, plan_path("s4b_tee_fanout.conf"))


def s4c():
    print("-- s4c: nvstreamdemux with TWO outputs (the second numbered Request pad must not ask for an existing name)")
    n = "s4c"
    u = update_plan(n, plan_path("s4c_demux_two_outputs.conf"), timeout_ms=10000)
    if u:
        names = sorted(p["pad"] for p in u["pads"] if p["element"] == "nvstreamdemux_1" and p["isSrc"])
        check_eq(names, ["src_0", "src_1"], "s4c: the demux made two src pads, each with its own number")
    caps_ok(n, u, "nvstreamdemux_1", "src_0", must=(NVMM, "width=(int)640"))
    caps_ok(n, u, "nvstreamdemux_1", "src_1", must=(NVMM, "width=(int)640"))
    caps_ok(n, u, "fakesink_1", "sink", must=(NVMM,))
    caps_ok(n, u, "fakesink_2", "sink", must=(NVMM,))
    run_plan(n, plan_path("s4c_demux_two_outputs.conf"))


def s5():
    print("-- s5: caps choices: raw with width/height/format/framerate, then memory:NVMM, then raw again")
    n = "s5"
    u = update_plan(n, plan_path("s5_caps_choice.conf"))
    caps_ok(n, u, "nvvideoconvert_1", "sink", must=("video/x-raw", "format=(string)RGBA", "width=(int)320", "height=(int)240", "framerate=(fraction)15/1"), must_not=("NVMM",))
    caps_ok(n, u, "nvvideoconvert_1", "src", must=(NVMM, "format=(string)NV12", "width=(int)160", "height=(int)120"))
    caps_ok(n, u, "nvvideoconvert_2", "sink", must=(NVMM, "width=(int)160"))
    caps_ok(n, u, "nvvideoconvert_2", "src", must=("video/x-raw", "format=(string)I420", "width=(int)80", "height=(int)60"), must_not=("NVMM",))
    caps_ok(n, u, "fakesink_1", "sink", must=("format=(string)I420", "width=(int)80"), must_not=("NVMM",))
    run_plan(n, plan_path("s5_caps_choice.conf"))
    print("-- s5n: NVMM caps choice on a raw-only link must be refused with a reason")
    n = "s5n"
    u = update_plan(n, plan_path("s5n_nvmm_on_raw_refused.conf"), timeout_ms=3000, expect_ok=False)
    if u:
        check(len(u["refused"]) == 1, f"s5n: one refused link: {u['refused']}")
        check(u["refused"] and "do not fit" in u["refused"][0], "s5n: the refusal says the caps do not match")
        check(not u["complete"], "s5n: not complete")
        check(u["status"] != "ok" or u["refused"], "s5n: not a silent ok")
    code, obj, err = run_plan(n, plan_path("s5n_nvmm_on_raw_refused.conf"), expect="refused")
    check_eq(code, 3, "s5n: gps-run exit code for refused")


def prepare_cache():
    """The sample config with paths made local: the engine is written next to the model, and /opt is read-only."""
    os.makedirs(CACHE, exist_ok=True)
    src = f"{SAMPLES}/models/Primary_Detector"
    for f in os.listdir(src):
        if not os.path.exists(os.path.join(CACHE, f)):
            shutil.copy(os.path.join(src, f), CACHE)
    text = open(f"{SAMPLES}/configs/deepstream-app/config_infer_primary.txt").read()
    for key, value in (("onnx-file", "resnet18_trafficcamnet_pruned.onnx"),
                       ("model-engine-file", "resnet18_trafficcamnet_pruned.onnx_b1_gpu0_int8.engine"),
                       ("labelfile-path", "labels.txt"), ("int8-calib-file", "cal_trt.bin"), ("batch-size", "1")):
        text = re.sub(rf"^{key}=.*$", f"{key}={value}", text, flags=re.M)
    with open(os.path.join(CACHE, "config_infer_primary.txt"), "w") as f:
        f.write(text)


def s3():
    print("-- s3: mp4 -> qtdemux -> h264parse -> nvv4l2decoder -> nvstreammux -> nvinfer -> nvvideoconvert -> nvdsosd -> fakesink")
    n = "s3"
    if not os.path.exists(f"{SAMPLES}/streams/sample_720p.mp4"):
        print("SKIP s3: no sample stream")
        return
    prepare_cache()
    plan = plan_path("s3_file_infer_osd.conf")
    if CACHE != "/tmp/gps-ds-cache":  # the plan names the default folder; point it to ours
        tmp = tempfile.mkdtemp(prefix="gps-ds-plan-")
        plan = os.path.join(tmp, "s3.conf")
        open(plan, "w").write(open(plan_path("s3_file_infer_osd.conf")).read().replace("/tmp/gps-ds-cache", CACHE))
    engine = os.path.join(CACHE, "resnet18_trafficcamnet_pruned.onnx_b1_gpu0_int8.engine")
    existed = os.path.exists(engine)
    print(f"   engine cache: {engine} ({'warm' if existed else 'cold: TensorRT will build it, about a minute'})")
    u = update_plan(n, plan, timeout_ms=600000)
    check(os.path.exists(engine), "s3: the TensorRT engine was written to the cache")
    caps_ok(n, u, "qtdemux_1", "video_0", must=("video/x-h264",))
    caps_ok(n, u, "nvv4l2decoder_1", "src", must=(NVMM, "format=(string)NV12"))
    caps_ok(n, u, "nvstreammux_1", "src", must=(NVMM, "width=(int)1280", "height=(int)720"))
    caps_ok(n, u, "nvinfer_1", "src", must=(NVMM, "width=(int)1280"))
    caps_ok(n, u, "nvdsosd_1", "sink", must=(NVMM, "format=(string)RGBA"))
    run_plan(n, plan, timeout_s=300)


def private_registry():
    """A private GST_REGISTRY, started from the warm one when there is one (only our plugin is then scanned)."""
    path = os.path.join(tempfile.mkdtemp(prefix="gps-ds-reg-"), "registry.bin")
    warm = os.environ.get("GST_REGISTRY")
    if warm and os.path.exists(warm):
        shutil.copy(warm, path)
    return path


def gst(args, env, timeout=120):
    p = subprocess.run(args, capture_output=True, text=True, errors="replace", env=env, timeout=timeout)
    return p.returncode, p.stdout, p.stderr


def s6():
    print("-- s6: exposed parameters: gps-run name=value and the gst-launch plugin")
    tmp = tempfile.mkdtemp(prefix="gps-ds-s6-")
    plan = plan_path("s6_exposed_width.conf")
    planb = plan_path("s6b_exposed_file.conf")
    n = "s6"
    u = update_plan(n, plan)
    run_plan(n, plan)
    run_plan(n, plan, "width=320", "height=240", "frames=10")
    # a bad value is refused by the same path that writes good ones
    code, obj, err = run_plan(n + "-bad", plan, "width=abc", expect="error")
    check(obj is not None and "width" in obj["message"], f"s6: the error names the parameter: {obj and obj['message']!r}")
    check_eq(code, 1, "s6: exit code for a bad value")
    code, obj, err = run_plan(n + "-neg", plan, "width=-5", expect="error")
    check(obj is not None and "negative" in obj["message"], f"s6: negative on an unsigned property is refused: {obj and obj['message']!r}")
    code, out, err = tool("gps-run", plan, "bogus=1", timeout=60)[0:3]
    check_eq(code, 2, "s6: unknown parameter is a usage error (2)")

    # the effect of a parameter is visible in the bitstream the encoder wrote
    def stream_size(path):
        code, out, err = gst(["gst-launch-1.0", "-v", "filesrc", f"location={path}", "!", "h264parse", "!", "fakesink"], os.environ)
        m = re.search(r"h264parse\d*\.GstPad:src: caps = .*?width=\(int\)(\d+), height=\(int\)(\d+)", out)
        return (int(m.group(1)), int(m.group(2))) if m else None

    for params, want in (((), (640, 480)), (("width=320", "height=240"), (320, 240))):
        out_file = os.path.join(tmp, f"run_{want[0]}.h264")
        run_plan("s6b", planb, f"out={out_file}", *params)
        check_eq(stream_size(out_file), want, f"s6b: gps-run {' '.join(params) or '(no parameters)'}: size of the encoded stream")

    # the plugin: the conf is an element without pads
    conf_dir = os.path.join(tmp, "confs")
    os.mkdir(conf_dir)
    shutil.copy(plan, conf_dir)
    shutil.copy(planb, conf_dir)
    env = dict(os.environ, GST_REGISTRY=private_registry(), GST_PLUGIN_PATH=PLUGIN, GPS_PIPELINE_PATH=conf_dir)
    start = time.time()
    code, out, err = gst(["gst-inspect-1.0", "s6_exposed_width"], env, timeout=300)
    results.append({"scenario": "s6", "tool": "gst-inspect-1.0 s6_exposed_width", "status": "ok" if code == 0 else "fail", "wall_ms": round((time.time() - start) * 1000)})
    check_eq(code, 0, f"s6 plugin: gst-inspect exit {err[-200:]!r}")
    check("Pads:\n  none" in out, "s6 plugin: the element has no pads")
    check("width" in out and "height" in out and "frames" in out, "s6 plugin: the exposed parameters are properties")

    def tiler_width(output):
        m = re.search(r"nvmultistreamtiler_1\S*:sink: caps = [^\n]*?width=\(int\)(\d+)", output)
        return int(m.group(1)) if m else None

    for params, want in (((), 640), (("width=320", "height=240", "frames=10"), 320)):
        start = time.time()
        code, out, err = gst(["gst-launch-1.0", "-v", "s6_exposed_width", *params], env)
        results.append({"scenario": "s6", "tool": "gst-launch-1.0 s6_exposed_width " + " ".join(params), "status": "ok" if code == 0 else "fail",
                        "wall_ms": round((time.time() - start) * 1000)})
        check_eq(code, 0, f"s6 plugin: gst-launch exit {err[-300:]!r}")
        check("Setting pipeline to NULL" in out or "Freeing pipeline" in out, "s6 plugin: the pipeline ran to its end")
        check_eq(tiler_width(out), want, f"s6 plugin: tiler input width with '{' '.join(params)}'")
    out_file = os.path.join(tmp, "plugin.h264")
    code, out, err = gst(["gst-launch-1.0", "-q", "s6b_exposed_file", f"out={out_file}", "width=320", "height=240"], env)
    check_eq(code, 0, f"s6 plugin: encoder conf exit {err[-300:]!r}")
    check_eq(stream_size(out_file), (320, 240), "s6 plugin: encoded stream size follows width/height")
    shutil.rmtree(tmp, ignore_errors=True)


GROUPS = {"fast": [s1, s1b, s2, s4, s4b, s4c, s5], "infer": [s3], "expose": [s6]}


def write_reports():
    os.makedirs(OUT, exist_ok=True)
    with open(os.path.join(OUT, f"deepstream-{GROUP}.json"), "w") as f:
        json.dump(results, f, indent=1)
    lines = [f"<!-- deepstream-{GROUP}: generated, do not edit by hand -->"]
    for r in results:
        lines.append(f"\n#### {r['scenario']} - `{r['tool']}`: {r['status']}, {r.get('ms', '-')} ms (total {r['wall_ms']} ms)")
        if r.get("message"):
            lines.append(f"\nMessage: `{r['message'][:300]}`")
        if r.get("pads"):
            lines += ["", "| pad | direction | linked | caps |", "|---|---|---|---|"]
            for p in r["pads"]:
                caps = p["caps"].replace("|", "\\|") or "-"
                lines.append(f"| {p['element']}.{p['pad']} | {p['dir']} | {'yes' if p['linked'] else 'no'}{' (temporary end)' if p['temporaryEnd'] else ''} | `{caps}` |")
    with open(os.path.join(OUT, f"deepstream-{GROUP}.md"), "w") as f:
        f.write("\n".join(lines) + "\n")


def main():
    if GROUP not in GROUPS:
        print(f"unknown group {GROUP}")
        return 2
    if not has_nv_elements():
        print("SKIP: no DeepStream elements (nvstreammux)")
        return 77
    for scenario in GROUPS[GROUP]:
        scenario()
    write_reports()
    return finish("deepstream." + GROUP)


if __name__ == "__main__":
    sys.exit(main())
