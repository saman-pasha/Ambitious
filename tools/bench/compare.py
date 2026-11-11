#!/usr/bin/env python3
"""tools/bench/compare.py [RUNS] -- Ambitious against Chromium, page by page.

Each page is measured on both engines on the same box, over RUNS runs (medians
for Chromium, means for `ambitious bench`):
  - the stages: Ambitious's own timers (`ambitious bench`) and Chromium's trace
    events (tools/bench/chromium.js), parse, style, layout and paint with raster;
  - warm: one process kept, the page loaded, laid out and drawn into a PNG;
  - cold: a process started for the page and drawn, as a command would be;
  - audit: Ambitious's diagnostics (the snapshot exported, then `advise`)
    against axe-core run over the page in Chromium;
  - on the scripted page (tools/gen-scripted-page.py), its scripts as a stage, and a
    click handled and drawn again: Ambitious's click, cascade, layout and the
    viewport's paint against Chromium's traced dispatch and the frame it draws.
It prints a Markdown report, the source of docs/benchmark.md, and writes the
raw numbers to build/bench/results.json."""
import json, os, platform, re, statistics, subprocess, sys, time

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
BENCH = os.path.join(ROOT, "build", "bench")
AMB = os.path.join(ROOT, "bin", "ambitious")
AXE = os.path.join(BENCH, "node_modules", "axe-core", "axe.min.js")


def pages():
    os.makedirs(BENCH, exist_ok=True)
    out = [os.path.join(ROOT, "tests", "pages", p) for p in ("news.html", "layout.html", "style.html", "a11y.html")]
    for name, sections in (("medium", 20), ("large", 200)):
        path = os.path.join(BENCH, name + ".html")
        with open(path, "w") as f:
            subprocess.run([sys.executable, os.path.join(ROOT, "tools", "gen-big-page.py"), str(sections)], stdout=f, check=True)
        out.append(path)
    path = os.path.join(BENCH, "scripted.html")
    with open(path, "w") as f:
        subprocess.run([sys.executable, os.path.join(ROOT, "tools", "gen-scripted-page.py"), "2000"], stdout=f, check=True)
    out.append(path)
    return out


# the pages a click is measured on: the element it is at
CLICKS = {"scripted.html": "#sale"}


def ambitious_bench(page, runs, click=None):
    env = dict(os.environ, AMB_ROOT=ROOT)
    r = subprocess.run([AMB] + (["--click", click] if click else []) + ["bench", page, str(runs)], capture_output=True, text=True,
                       env=env, timeout=3600)
    if r.returncode != 0:
        raise SystemExit("ambitious bench %s: %s" % (page, r.stderr))
    stages = {}
    for line in r.stdout.splitlines():
        m = re.match(r"\s+(.+?)\s+([\d.]+) ms$", line)
        if m:
            stages[m.group(1)] = float(m.group(2))
        m = re.match(r".*?: (\d+) bytes, (\d+) nodes, (\d+) boxes", line)
        if m:
            stages["bytes"], stages["nodes"], stages["boxes"] = map(int, m.groups())
    return stages


def ambitious_cold(page, runs):
    png = os.path.join(BENCH, "cold.png")
    times = []
    for _ in range(runs):
        t = time.perf_counter()
        subprocess.run([AMB, "render", page, png], capture_output=True, check=True, timeout=600)
        times.append((time.perf_counter() - t) * 1000)
    return statistics.median(times), os.path.getsize(png)


def chromium(all_pages, runs):
    axe = AXE if os.path.exists(AXE) else "-"
    args = []
    for p in all_pages:
        if os.path.basename(p) in CLICKS:
            args.append("--click=" + CLICKS[os.path.basename(p)])
        args.append(p)
    r = subprocess.run(["node", os.path.join(ROOT, "tools", "bench", "chromium.js"), str(runs), axe] + args,
                       capture_output=True, text=True, timeout=7200)
    if r.returncode != 0:
        raise SystemExit("chromium.js: " + r.stderr)
    return json.loads(r.stdout)


def winner(a, c):
    if a != a or c != c:
        return "-"
    if a < c:
        return "**Ambitious** (%.1fx)" % (c / a if a > 0 else float("inf"))
    return "Chromium (%.1fx)" % (a / c if c > 0 else float("inf"))


def main():
    runs = int(sys.argv[1]) if len(sys.argv) > 1 else 5
    ps = pages()
    amb = {}
    for p in ps:
        stages = ambitious_bench(p, runs)
        cold, png_bytes = ambitious_cold(p, runs)
        amb[p] = {"stages": stages, "cold": cold, "png_bytes": png_bytes}
        if os.path.basename(p) in CLICKS:
            amb[p]["click"] = ambitious_bench(p, runs, CLICKS[os.path.basename(p)])
    chr_ = chromium(ps, runs)
    with open(os.path.join(BENCH, "results.json"), "w") as f:
        json.dump({"ambitious": amb, "chromium": chr_}, f, indent=1)

    def name(p):
        return os.path.basename(p)

    rows = []
    out = []
    w = out.append
    cpu = ""
    try:
        with open("/proc/cpuinfo") as f:
            cpu = next(l.split(":", 1)[1].strip() for l in f if l.startswith("model name"))
    except Exception:
        pass
    w("Measured by `python3 tools/bench/compare.py %d` on %s (%d CPUs), %s." % (runs, cpu or platform.machine(), os.cpu_count(), platform.platform()))
    w("")
    w("| page | bytes | DOM nodes (Ambitious) | elements (Chromium) |")
    w("|---|---:|---:|---:|")
    for p in ps:
        s = amb[p]["stages"]
        w("| %s | %s | %s | %s |" % (name(p), format(s.get("bytes", 0), ","), format(s.get("nodes", 0), ","),
                                       format(chr_[p]["warm"]["nav"]["nodes"], ",")))
    w("")
    w("### The stages (ms)")
    w("")
    w("Ambitious's timers against the durations of Chromium's trace events for the same work.")
    w("")
    w("| page | stage | Ambitious | Chromium | faster |")
    w("|---|---|---:|---:|---|")
    for p in ps:
        s, c = amb[p]["stages"], chr_[p]["stages"]
        if s.get("scripts", 0) > 0.05 or c.get("script", 0) > 0.05:
            w("| %s | %s | %.2f | %.2f | %s |" % (name(p), "scripts", s["scripts"], c["script"], winner(s["scripts"], c["script"])))
            rows.append((name(p), "scripts", s["scripts"], c["script"]))
        for label, a, b in (("parse", s["parse"], c["parse"]),
                            ("style", s["cascade"], c["style"]),
                            ("layout", s["layout"], c["layout"]),
                            ("paint and raster", s["paint"], c["paint"] + c["raster"])):
            w("| %s | %s | %.2f | %.2f | %s |" % (name(p), label, a, b, winner(a, b)))
            rows.append((name(p), label, a, b))
    w("")
    w("### Whole loads (ms)")
    w("")
    w("*Warm, laid out*: the page fetched, parsed, styled and laid out, in a process already running. "
      "*Warm, to PNG*: that, then drawn and encoded as a PNG. *Cold*: a process started, the page drawn into a PNG file, the process ended.")
    w("")
    w("| page | measure | Ambitious | Chromium | faster |")
    w("|---|---|---:|---:|---|")
    for p in ps:
        s, c = amb[p]["stages"], chr_[p]
        laid = s["fetch the page"] + s["parse"] + s["fetch its resources"] + s.get("scripts", 0) + s["cascade"] + s["layout"]
        png = laid + s["paint"] + s["encode the PNG"]
        for label, a, b in (("warm, laid out", laid, c["warm"]["loaded"]),
                            ("warm, to PNG", png, c["warm"]["total"]),
                            ("cold, to PNG", amb[p]["cold"], c["cold"])):
            w("| %s | %s | %.1f | %.1f | %s |" % (name(p), label, a, b, winner(a, b)))
            rows.append((name(p), label, a, b))
    w("")
    w("### Diagnostics (ms)")
    w("")
    w("Ambitious: the snapshot exported, then the reasoner's `advise` (every module's diagnostics, a cocolog process of its own). "
      "Chromium: axe-core %s injected into the loaded page, then `axe.run()`." % ("4.14.0" if os.path.exists(AXE) else "(not installed)"))
    w("")
    w("| page | Ambitious | its findings | Chromium + axe-core | its violations (nodes) | faster |")
    w("|---|---:|---|---:|---|---|")
    for p in ps:
        s, c = amb[p]["stages"], chr_[p].get("audit")
        a = s["export the snapshot"] + s["reasoner: advise"]
        if c:
            b = c["inject"] + c["run"]
            w("| %s | %.1f | | %.1f | %d (%d) | %s |" % (name(p), a, b, c["violations"], c["nodes"], winner(a, b)))
            rows.append((name(p), "diagnostics", a, b))
    clicked = [p for p in ps if amb[p].get("click") and chr_[p].get("click")]
    if clicked:
        w("")
        w("### A click, handled and drawn again (ms)")
        w("")
        w("The page loaded and drawn, then a user's click at the element. Ambitious: the click's listeners (`ambitious --click SELECTOR bench`), "
          "then the page styled, laid out and its viewport painted again. Chromium: the traced input events' dispatch, the listeners run in it, "
          "then the frame it draws: style, layout, paint and raster.")
        w("")
        w("| page | click at | Ambitious | its click, cascade, layout, viewport | Chromium | its dispatch, style, layout, paint and raster | faster |")
        w("|---|---|---:|---|---:|---|---|")
        for p in clicked:
            s, c = amb[p]["click"], chr_[p]["click"]
            a = s["click"] + s["cascade"] + s["layout"] + s["paint the viewport"]
            b = c["dispatch"] + c["style"] + c["layout"] + c["paint"] + c["raster"]
            w("| %s | `%s` | %.1f | %.1f, %.1f, %.1f, %.1f | %.1f | %.1f, %.1f, %.1f, %.1f | %s |" % (
                name(p), CLICKS[name(p)], a, s["click"], s["cascade"], s["layout"], s["paint the viewport"], b,
                c["dispatch"], c["style"], c["layout"], c["paint"] + c["raster"], winner(a, b)))
            rows.append((name(p), "click, drawn again", a, b))
    wins = sum(1 for r in rows if r[2] < r[3])
    w("")
    w("**Ambitious is faster in %d of %d comparisons.**" % (wins, len(rows)))
    print("\n".join(out))


if __name__ == "__main__":
    main()
