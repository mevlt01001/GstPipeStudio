"""Writes report/elements.json and report/ELEMENTS.md (English) from the records of the sweep."""
import collections
import json
import os
import shutil

CLASSES = ["ok", "partial", "needs-setup", "crashed", "hung", "cannot-create", "no-feed"]
MEANING = {
    "ok": "the update completed and gps-run ran (EOS, or a clean flow for a source)",
    "partial": "it works but some pads stayed free, or the run did not reach EOS",
    "needs-setup": "the element stopped with its own message: it wants a setting, a device, a file or a model",
    "crashed": "the helper program died from a signal",
    "hung": "the update timed out or the tool never finished",
    "cannot-create": "the plugin could not be loaded or the element was not created",
    "no-feed": "no meaningful feed could be built (the reason says what is missing)",
}


def cell(text, n=140):
    text = str(text).replace("\n", " ").replace("|", "\\|")
    return text if len(text) <= n else text[:n - 1] + "…"


def repro_command(rec, repo_rel_bin, report_rel):
    """The command that repeats the deciding step (run from the repo root, inside the container)."""
    conf = rec.get("repro", "")
    if not conf:
        return f"{repo_rel_bin}/gps-inspect element {rec['factory']}"
    name = f"{rec['factory']}.conf"
    steps = rec.get("steps") or [{"cmd": "gps-update x"}]
    tool = steps[-1]["cmd"].split()[0]
    extra = " --timeout-ms 8000" if tool == "gps-update" else " --timeout-ms 6000"
    return f"{repo_rel_bin}/{tool} {report_rel}/sweep-repro/{name}{extra}"


def write(args, records, summary, lib):
    good = [r for r in records if "orchestration" not in r]
    repo_root = os.path.dirname(os.path.dirname(os.path.abspath(args.report_dir)))
    repo_rel_bin = os.path.relpath(args.bin_dir, repo_root)
    report_rel = os.path.relpath(args.report_dir, repo_root)

    # repro confs of the crashers and hangers
    repro_dir = os.path.join(args.report_dir, "sweep-repro")
    shutil.rmtree(repro_dir, ignore_errors=True)
    for r in good:
        if r["class"] in ("crashed", "hung") and r.get("repro") and os.path.exists(r["repro"]):
            os.makedirs(repro_dir, exist_ok=True)
            shutil.copy(r["repro"], os.path.join(repro_dir, r["factory"] + ".conf"))
        if r["class"] in ("crashed", "hung"):
            r["reproCommand"] = repro_command(r, repo_rel_bin, report_rel)

    for r in good:       # the confs behind the findings, so the owner can repeat them
        for fnd in r["findings"]:
            c = fnd.get("conf", "")
            if c and os.path.exists(c):
                os.makedirs(repro_dir, exist_ok=True)
                dest = f"{r['factory']}.{os.path.basename(c)}"
                shutil.copy(c, os.path.join(repro_dir, dest))
                fnd["reproConf"] = f"{report_rel}/sweep-repro/{dest}"
                fnd["conf"] = ""

    counts = collections.Counter(r["class"] for r in good)
    summary = dict(summary)
    summary["classes"] = {c: counts.get(c, 0) for c in CLASSES}
    summary["total"] = len(good)
    props = collections.Counter()
    for r in good:
        p = r.get("properties") or {}
        props["tested"] += p.get("tested", 0)
        props["rejected"] += len(p.get("rejected", []))
    summary["propertyDefaults"] = dict(props)

    for r in good:       # keep the json readable: steps only for the problem classes
        if r["class"] in ("ok",):
            r["steps"] = [{"step": s["step"], "ms": s["ms"], "status": s["status"]} for s in r["steps"]]
    with open(os.path.join(args.report_dir, "elements.json"), "w") as f:
        json.dump({"summary": summary, "elements": records}, f, indent=1, ensure_ascii=False)

    with open(os.path.join(args.report_dir, "ELEMENTS.md"), "w") as f:
        f.write(markdown(good, summary, lib, repo_rel_bin, report_rel))


def markdown(good, summary, lib, repo_rel_bin, report_rel):
    o = []
    w = o.append
    cov = summary["coverage"]
    w("# Element sweep\n")
    w("This file is rewritten by `tests/sweep/sweep.py` on every run. Every factory of the registry is tried in its own process, "
      "isolated against timeouts and crashes. Details: `elements.json`.\n")
    w(f"Run: {summary['total']} factories, {summary['jobs']} parallel workers, {summary['wallSeconds']} s in total "
      f"(feed library {summary['feedSeconds']} s). Orchestration problems: {len(summary['orchestrationErrors'])}.\n")

    w("## 1. Summary\n")
    w("| Class | Count | Meaning |\n|---|---:|---|")
    for c in CLASSES:
        w(f"| {c} | {summary['classes'][c]} | {MEANING[c]} |")
    w(f"| **total** | **{summary['total']}** | |\n")
    nv = [r for r in good if r["nvidia"]]
    nvc = collections.Counter(r["class"] for r in nv)
    w(f"NVIDIA elements ({len(nv)}): " + ", ".join(f"{c} {nvc.get(c, 0)}" for c in CLASSES if nvc.get(c)) + ".\n")
    pd = summary["propertyDefaults"]
    w(f"Property defaults: {pd.get('tested', 0)} properties were written back as text, {pd.get('rejected', 0)} were refused (section 7).\n")

    w("## 2. Coverage check: does `gps-inspect list` cover the registry?\n")
    ref = cov["reference"]
    w(f"- `gps-inspect list`: {cov['listed']} factories (took {cov['listMs']} ms).")
    w(f"- `gst-inspect-1.0 -a` (independent source): {ref if ref is not None else 'could not be run'} factories.")
    w(f"- Missing from the list: {len(cov['missing'])} {cov['missing'][:10] or ''}")
    w(f"- Listed but not in the registry: {len(cov['extra'])} {cov['extra'][:10] or ''}")
    w(f"- Duplicate names: {len(cov['duplicates'])} {cov['duplicates'][:10] or ''}")
    w(f"- klass/longName/rank/plugin differences: {len(cov['fieldDiffs'])}")
    for d in cov["fieldDiffs"][:10]:
        w(f"  - {d}")
    w(f"- Entries that break the schema: {len(cov['badEntries'])}")
    for d in cov["badEntries"][:10]:
        w(f"  - {d}")
    w("")
    if cov["blacklisted"]:
        w("Blacklisted plugin files (GStreamer tried to load them and could not). Their factories are not in the registry, "
          "so they are not in the list either; GStreamer itself does not show them.\n")
        w("| File | Missing library |\n|---|---|")
        for b in cov["blacklisted"]:
            w(f"| {b['file']} | {', '.join(b['missingLibraries']) or 'ldd shows none missing (another cause)'} |")
        w("")

    w("## 3. Spread by plugin\n")
    by = collections.defaultdict(collections.Counter)
    for r in good:
        by[r["plugin"]][r["class"]] += 1
    w("| Plugin | Count | " + " | ".join(CLASSES) + " |\n|---|---:|" + "---:|" * len(CLASSES))
    for p in sorted(by):
        w(f"| {p} | {sum(by[p].values())} | " + " | ".join(str(by[p].get(c, "")) for c in CLASSES) + " |")
    w("")

    w("## 4. NVIDIA elements\n")
    w("| Factory | Plugin | Class | Reason (the real message) |\n|---|---|---|---|")
    for r in sorted(nv, key=lambda r: r["factory"]):
        w(f"| {r['factory']} | {r['plugin']} | {r['class']} | {cell(r['reason'], 220)} |")
    w("")

    w("## 5. Crashed and hung\n")
    bad = [r for r in good if r["class"] in ("crashed", "hung")]
    crashers = [r for r in bad if r["class"] == "crashed"]
    killed = [r for r in bad if r["class"] == "hung" and "killed" in r["reason"]]
    waited = [r for r in bad if r["class"] == "hung" and r not in killed]
    w(f"Crashed: {len(crashers)}, killed because the tool never finished: {len(killed)}, update timeouts: {len(waited)}. "
      "The commands run from the repository root, inside Docker (`docker/studio/run.sh ...`).\n")
    w("The elements in the timeout group are mostly sources that wait for data (network, device, application) or elements with a "
      "pipeline inside; the tool returned by its own timeout, so the hang may not be a product defect. They are kept in a class of their own to stay honest.\n")
    for title, group in (("Crashed", crashers), ("Tool never finished, killed (the real hangs)", killed),
                         ("Update timeout (no first buffer)", waited)):
        w(f"### {title} ({len(group)})\n")
        if not group:
            w("None.\n")
            continue
        w("| Factory | Reason | Command to repeat it |\n|---|---|---|")
        for r in group:
            w(f"| {r['factory']} | {cell(r['reason'], 200)} | `{r.get('reproCommand', '')}` |")
        w("")

    w("## 6. Could not be created\n")
    nc = [r for r in good if r["class"] == "cannot-create"]
    if not nc:
        w("None.\n")
    for r in nc:
        w(f"- `{r['factory']}` ({r['plugin']}): {cell(r['reason'], 300)}")
    w("")

    w("## 7. Findings (about the product)\n")
    kinds = collections.defaultdict(list)
    for r in good:
        for fnd in r["findings"]:
            kinds[fnd["kind"]].append((r["factory"], fnd["detail"], fnd.get("reproConf", "")))
    if not kinds:
        w("No findings.\n")
    for k in sorted(kinds):
        w(f"### {k} ({len(kinds[k])})\n")
        for fac, d, rc in kinds[k][:60]:
            w(f"- `{fac}`: {cell(d, 260)}" + (f" (conf: `{rc}`)" if rc else ""))
        if len(kinds[k]) > 60:
            w(f"- … {len(kinds[k]) - 60} more, in `elements.json`")
        w("")

    w("## 8. Feed table\n")
    w(f"A working feed chain was found for {len(summary['mediaTypesWithFeed'])} media types "
      f"({summary['feedsTried']} chains tried, {len(summary['feedsFailed'])} did not work). "
      "Chains are searched from the seeds in `feeds.py` and from `gps-inspect element` data; every chain is run once before it is used.\n")
    w("| Media type | Chain |\n|---|---|")
    for key in sorted(lib.feeds):
        w(f"| {cell(key, 60)} | {cell(lib.feeds[key][0]['label'], 160)} |")
    w("")
    nf = collections.Counter()
    for r in good:
        if r["class"] == "no-feed":
            nf[cell(r["reason"], 100)] += 1
    if nf:
        w("Reasons for `no-feed` (top 25):\n")
        for text, n in nf.most_common(25):
            w(f"- {n} × {text}")
        w("")

    w("## 9. All elements\n")
    w("| Factory | Plugin | Class | Reason |\n|---|---|---|---|")
    for r in good:
        w(f"| {r['factory']} | {r['plugin']} | {r['class']} | {cell(r['reason'], 150)} |")
    w("")
    if summary["orchestrationErrors"]:
        w("## 10. Orchestration problems\n")
        for e in summary["orchestrationErrors"]:
            w(f"- `{e['factory']}`: {cell(e['orchestration'], 400)}")
    return "\n".join(o) + "\n"
