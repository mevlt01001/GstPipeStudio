"""Writes report/elements.json and report/ELEMENTS.md (Turkish) from the records of the sweep."""
import collections
import json
import os
import shutil

CLASSES = ["tamam", "kısmi", "kurulum-gerek", "çöktü", "takıldı", "oluşturulamadı", "besleme-yok"]
MEANING = {
    "tamam": "güncelleme tamamlandı ve gps-run çalıştı (EOS ya da kaynaksa hatasız akış)",
    "kısmi": "çalışıyor ama bazı pad'ler boşta kaldı, ya da çalıştırma EOS'a ulaşmadı",
    "kurulum-gerek": "element kendi hata mesajıyla durdu: ayar, aygıt, dosya ya da model ister",
    "çöktü": "yardımcı program sinyalle öldü",
    "takıldı": "güncelleme zaman aşımına uğradı ya da araç hiç bitmedi",
    "oluşturulamadı": "eklenti yüklenemedi ya da element oluşmadı",
    "besleme-yok": "anlamlı bir besleme kurulamadı (eksik olan sebepte yazıyor)",
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
        if r["class"] in ("çöktü", "takıldı") and r.get("repro") and os.path.exists(r["repro"]):
            os.makedirs(repro_dir, exist_ok=True)
            shutil.copy(r["repro"], os.path.join(repro_dir, r["factory"] + ".conf"))
        if r["class"] in ("çöktü", "takıldı"):
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
        if r["class"] in ("tamam",):
            r["steps"] = [{"step": s["step"], "ms": s["ms"], "status": s["status"]} for s in r["steps"]]
    with open(os.path.join(args.report_dir, "elements.json"), "w") as f:
        json.dump({"summary": summary, "elements": records}, f, indent=1, ensure_ascii=False)

    with open(os.path.join(args.report_dir, "ELEMENTS.md"), "w") as f:
        f.write(markdown(good, summary, lib, repo_rel_bin, report_rel))


def markdown(good, summary, lib, repo_rel_bin, report_rel):
    o = []
    w = o.append
    cov = summary["coverage"]
    w("# Element taraması (sweep)\n")
    w("Bu dosya `tests/sweep/sweep.py` tarafından her çalıştırmada yeniden yazılır. Kayıt defterindeki her factory, ayrı bir süreçte "
      "zaman aşımı ve çökme yalıtımıyla denenir. Ayrıntılı veri: `elements.json`.\n")
    w(f"Çalıştırma: {summary['total']} factory, {summary['jobs']} paralel işçi, toplam {summary['wallSeconds']} sn "
      f"(besleme kütüphanesi {summary['feedSeconds']} sn). Orkestrasyon hatası: {len(summary['orchestrationErrors'])}.\n")

    w("## 1. Özet\n")
    w("| Sınıf | Adet | Anlamı |\n|---|---:|---|")
    for c in CLASSES:
        w(f"| {c} | {summary['classes'][c]} | {MEANING[c]} |")
    w(f"| **toplam** | **{summary['total']}** | |\n")
    nv = [r for r in good if r["nvidia"]]
    nvc = collections.Counter(r["class"] for r in nv)
    w(f"NVIDIA elementleri ({len(nv)} adet): " + ", ".join(f"{c} {nvc.get(c, 0)}" for c in CLASSES if nvc.get(c)) + ".\n")
    pd = summary["propertyDefaults"]
    w(f"Property varsayılanları: {pd.get('tested', 0)} property metin olarak geri yazıldı, {pd.get('rejected', 0)} tanesi reddedildi (bölüm 7).\n")

    w("## 2. Kapsam doğrulaması: `gps-inspect list` kayıt defterini kapsıyor mu?\n")
    ref = cov["reference"]
    w(f"- `gps-inspect list`: {cov['listed']} factory (süre {cov['listMs']} ms).")
    w(f"- `gst-inspect-1.0 -a` (bağımsız kaynak): {ref if ref is not None else 'çalıştırılamadı'} factory.")
    w(f"- Listede olmayan: {len(cov['missing'])} {cov['missing'][:10] or ''}")
    w(f"- Fazladan listelenen: {len(cov['extra'])} {cov['extra'][:10] or ''}")
    w(f"- Yinelenen ad: {len(cov['duplicates'])} {cov['duplicates'][:10] or ''}")
    w(f"- klass/longName/rank/plugin uyuşmazlığı: {len(cov['fieldDiffs'])}")
    for d in cov["fieldDiffs"][:10]:
        w(f"  - {d}")
    w(f"- Liste girdilerinde şema hatası: {len(cov['badEntries'])}")
    for d in cov["badEntries"][:10]:
        w(f"  - {d}")
    w("")
    if cov["blacklisted"]:
        w("Kara listeye alınan eklenti dosyaları (GStreamer yüklemeyi denedi, yüklenemedi). Bu dosyaların factory'leri kayıt "
          "defterinde hiç yoktur, bu yüzden listede de yoktur; GStreamer'ın kendisi de göstermez.\n")
        w("| Dosya | Eksik kütüphane |\n|---|---|")
        for b in cov["blacklisted"]:
            w(f"| {b['file']} | {', '.join(b['missingLibraries']) or 'ldd eksik göstermiyor (başka sebep)'} |")
        w("")

    w("## 3. Eklenti bazında dağılım\n")
    by = collections.defaultdict(collections.Counter)
    for r in good:
        by[r["plugin"]][r["class"]] += 1
    w("| Eklenti | Adet | " + " | ".join(CLASSES) + " |\n|---|---:|" + "---:|" * len(CLASSES))
    for p in sorted(by):
        w(f"| {p} | {sum(by[p].values())} | " + " | ".join(str(by[p].get(c, "")) for c in CLASSES) + " |")
    w("")

    w("## 4. NVIDIA elementleri\n")
    w("| Factory | Eklenti | Sınıf | Sebep (gerçek mesaj) |\n|---|---|---|---|")
    for r in sorted(nv, key=lambda r: r["factory"]):
        w(f"| {r['factory']} | {r['plugin']} | {r['class']} | {cell(r['reason'], 220)} |")
    w("")

    w("## 5. Çökenler ve takılanlar\n")
    bad = [r for r in good if r["class"] in ("çöktü", "takıldı")]
    crashers = [r for r in bad if r["class"] == "çöktü"]
    killed = [r for r in bad if r["class"] == "takıldı" and "öldürüldü" in r["reason"]]
    waited = [r for r in bad if r["class"] == "takıldı" and r not in killed]
    w(f"Çöken: {len(crashers)}, araç hiç bitmediği için öldürülen: {len(killed)}, güncelleme zaman aşımı: {len(waited)}. "
      "Komutlar depo kökünden, Docker içinde (`docker/studio/run.sh ...`) çalıştırılır.\n")
    w("Zaman aşımı grubundaki elementler çoğunlukla veri bekleyen kaynaklardır (ağ, aygıt, uygulama) ya da kendi içinde "
      "pipeline olan elementlerdir; araç kendi zaman aşımıyla döndü, takılma ürünün kusuru olmayabilir. Yine de dürüstlük için ayrı sınıfta tutulur.\n")
    for title, group in (("Çökenler", crashers), ("Araç bitmedi, öldürüldü (asıl takılmalar)", killed),
                         ("Güncelleme zaman aşımı (ilk buffer gelmedi)", waited)):
        w(f"### {title} ({len(group)})\n")
        if not group:
            w("Hiç yok.\n")
            continue
        w("| Factory | Sebep | Yeniden üretme komutu |\n|---|---|---|")
        for r in group:
            w(f"| {r['factory']} | {cell(r['reason'], 200)} | `{r.get('reproCommand', '')}` |")
        w("")

    w("## 6. Oluşturulamayanlar\n")
    nc = [r for r in good if r["class"] == "oluşturulamadı"]
    if not nc:
        w("Hiç yok.\n")
    for r in nc:
        w(f"- `{r['factory']}` ({r['plugin']}): {cell(r['reason'], 300)}")
    w("")

    w("## 7. Bulgular (ürün hakkında)\n")
    kinds = collections.defaultdict(list)
    for r in good:
        for fnd in r["findings"]:
            kinds[fnd["kind"]].append((r["factory"], fnd["detail"], fnd.get("reproConf", "")))
    if not kinds:
        w("Bulgu yok.\n")
    for k in sorted(kinds):
        w(f"### {k} ({len(kinds[k])})\n")
        for fac, d, rc in kinds[k][:60]:
            w(f"- `{fac}`: {cell(d, 260)}" + (f" (conf: `{rc}`)" if rc else ""))
        if len(kinds[k]) > 60:
            w(f"- … {len(kinds[k]) - 60} tane daha, `elements.json` içinde")
        w("")

    w("## 8. Besleme tablosu\n")
    w(f"{len(summary['mediaTypesWithFeed'])} medya türü için çalışan bir besleme zinciri bulundu "
      f"({summary['feedsTried']} zincir denendi, {len(summary['feedsFailed'])} tanesi çalışmadı). "
      "Zincirler `feeds.py` içindeki tohumlardan ve `gps-inspect element` verisinden aranır; her zincir kullanılmadan önce bir kez çalıştırılır.\n")
    w("| Medya türü | Zincir |\n|---|---|")
    for key in sorted(lib.feeds):
        w(f"| {cell(key, 60)} | {cell(lib.feeds[key][0]['label'], 160)} |")
    w("")
    nf = collections.Counter()
    for r in good:
        if r["class"] == "besleme-yok":
            nf[cell(r["reason"], 100)] += 1
    if nf:
        w("`besleme-yok` sebeplerinin dağılımı (ilk 25):\n")
        for text, n in nf.most_common(25):
            w(f"- {n} × {text}")
        w("")

    w("## 9. Tüm elementler\n")
    w("| Factory | Eklenti | Sınıf | Sebep |\n|---|---|---|---|")
    for r in good:
        w(f"| {r['factory']} | {r['plugin']} | {r['class']} | {cell(r['reason'], 150)} |")
    w("")
    if summary["orchestrationErrors"]:
        w("## 10. Orkestrasyon hataları\n")
        for e in summary["orchestrationErrors"]:
            w(f"- `{e['factory']}`: {cell(e['orchestration'], 400)}")
    return "\n".join(o) + "\n"
