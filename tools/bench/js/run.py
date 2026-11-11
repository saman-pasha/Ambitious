#!/usr/bin/env python3
"""tools/bench/js/run.py [RUNS] -- tools/bench/js/suite.js on every engine found:
V8 (node), V8's interpreter alone (node --jitless), QuickJS (the binary named by
$QJS, a runner of one script that gives `now()` and `print`), and Ambitious's
own engine (`ambitious js FILE`, once it exists). Each engine runs the suite RUNS
times; the report gives each workload's median and checks that every engine
printed the same checksums. Prints a Markdown table."""
import os, shutil, statistics, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
SUITE = os.path.join(ROOT, "tools", "bench", "js", "suite.js")


def engines():
    out = []
    if shutil.which("node"):
        out.append(("V8 (node %s)" % subprocess.run(["node", "--version"], capture_output=True, text=True).stdout.strip(), ["node", SUITE]))
        out.append(("V8 --jitless", ["node", "--jitless", "--no-expose-wasm", SUITE]))
    qjs = os.environ.get("QJS")
    if qjs and os.path.exists(qjs):
        out.append(("QuickJS 2021-03-27", [qjs, SUITE]))
    amb = os.path.join(ROOT, "bin", "ambitious")
    probe = subprocess.run([amb, "js", "--help"], capture_output=True, text=True) if os.path.exists(amb) else None
    if probe is not None and probe.returncode == 0:
        out.append(("Ambitious", [amb, "js", SUITE]))
    return out


def run(cmd, runs):
    times, checks = {}, {}
    for _ in range(runs):
        r = subprocess.run(cmd, capture_output=True, text=True, timeout=600)
        if r.returncode != 0:
            raise SystemExit("%s: %s" % (" ".join(cmd), r.stderr.strip()[:400]))
        for line in r.stdout.splitlines():
            parts = line.split()
            if len(parts) == 3:
                times.setdefault(parts[0], []).append(float(parts[1]))
                checks[parts[0]] = parts[2]
    return {k: statistics.median(v) for k, v in times.items()}, checks


def main():
    runs = int(sys.argv[1]) if len(sys.argv) > 1 else 5
    found = engines()
    results = [(name, *run(cmd, runs)) for name, cmd in found]
    names = list(results[0][1].keys())
    print("| workload | " + " | ".join(n for n, _, _ in results) + " |")
    print("|---|" + "---:|" * len(results))
    for w in names:
        print("| %s | %s |" % (w, " | ".join("%.1f" % t.get(w, float("nan")) for _, t, _ in results)))
    ref = results[0][2]
    bad = [(n, w) for n, _, c in results for w in names if c.get(w) != ref.get(w)]
    print("")
    print("Median ms of %d runs. Checksums: %s." % (runs, "all engines agree" if not bad else "DIFFER: %s" % bad))


if __name__ == "__main__":
    main()
