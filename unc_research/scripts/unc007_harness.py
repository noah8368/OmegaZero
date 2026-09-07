#!/usr/bin/env python3
"""unc007_harness.py — Phase B driver: curated registry -> results file.

Reads the curated position registry (FEN | label | category | expectation), runs
each FEN through build/unc_harness with --vstar (so every position carries the
eval v, the predicted p(u|x), AND the deep-search target v_star -> realized error
u = v - v_star), and writes one timestamped results file under
experiment_results/unc-007/ for the Phase C analysis/plots to consume.

C++ does the numeric work (v, p(u|x), v_star); this driver owns the registry
metadata and the on-disk layout (the established repo pattern -- Python scripts
shell out to the compiled binary).

Usage:
  make unc_harness
  python3 unc_research/scripts/unc007_harness.py           # curated registry, defaults
  python3 unc_research/scripts/unc007_harness.py --depth 14
"""

import argparse
import json
import subprocess
import sys
from datetime import datetime
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "unc_research" / "scripts"))
from oznu import read_oznu  # noqa: E402


def _git_hash():
    try:
        return subprocess.check_output(
            ["git", "rev-parse", "--short", "HEAD"], cwd=REPO,
            stderr=subprocess.DEVNULL).decode().strip()
    except Exception:
        return "unknown"


def read_registry(path):
    """Parse `FEN | label | category | expectation` rows (skip #/blank)."""
    rows = []
    for line in Path(path).read_text().splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        parts = [p.strip() for p in line.split("|")]
        if len(parts) < 4:
            sys.exit(f"registry: malformed row (need 4 fields): {line}")
        fen, label, category, expectation = parts[0], parts[1], parts[2], parts[3]
        rows.append({"fen": fen, "label": label, "category": category,
                     "expectation": expectation})
    return rows


def run_harness(harness, net, fens, depth, nodes):
    """Run build/unc_harness --vstar over `fens`; return {fen: json_dict}."""
    proc = subprocess.run(
        [harness, net, "--vstar", "--depth", str(depth), "--nodes", str(nodes)],
        input="\n".join(fens) + "\n", capture_output=True, text=True, check=True)
    out = {}
    for line in proc.stdout.splitlines():
        line = line.strip()
        if line.startswith("{"):
            d = json.loads(line)
            out[d["fen"]] = d
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--net", default="unc_research/models/nnue_unc.bin")
    ap.add_argument("--harness", default="build/unc_harness")
    ap.add_argument("--registry",
                    default="unc_research/positions/unc007_curated.txt")
    ap.add_argument("--depth", type=int, default=12)
    ap.add_argument("--nodes", type=int, default=2000000)
    ap.add_argument("--out-root", default="unc_research/experiment_results/unc-007")
    a = ap.parse_args()

    rows = read_registry(a.registry)
    fens = [r["fen"] for r in rows]
    print(f"registry: {len(rows)} positions from {a.registry}")

    got = run_harness(a.harness, a.net, fens, a.depth, a.nodes)

    ts = datetime.now().strftime("%Y-%m-%d_%H-%M-%S")
    git = _git_hash()
    out_dir = Path(a.out_root) / f"{ts}_{git}"
    out_dir.mkdir(parents=True, exist_ok=True)

    net_meta = read_oznu(a.net)
    results_path = out_dir / "results.jsonl"
    n_written = 0
    n_decisive = 0
    with open(results_path, "w") as f:
        for r in rows:
            d = got.get(r["fen"])
            if d is None or "error" in d:
                print(f"  WARN: no result for {r['label']} ({r['fen']})"
                      f"{': ' + d['error'] if d else ''}")
                continue
            d.update({"label": r["label"], "category": r["category"],
                      "expectation": r["expectation"]})
            f.write(json.dumps(d) + "\n")
            n_written += 1
            n_decisive += 1 if d.get("decisive") else 0

    meta = {
        "timestamp": ts, "git_commit": git,
        "net": a.net, "net_run_id": net_meta["run_id"],
        "trunk_md5": net_meta["trunk_md5"],
        "registry": a.registry, "vstar_depth": a.depth, "vstar_nodes": a.nodes,
        "n_positions": len(rows), "n_written": n_written,
        "n_decisive_dropped_by_analysis": n_decisive,
    }
    (out_dir / "meta.json").write_text(json.dumps(meta, indent=2))

    print(f"wrote {n_written}/{len(rows)} positions -> {results_path}")
    if n_decisive:
        print(f"  ({n_decisive} flagged decisive: v_star is a mate/TB sentinel; "
              f"the analysis should exclude them from coverage)")
    print(f"  meta -> {out_dir / 'meta.json'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
