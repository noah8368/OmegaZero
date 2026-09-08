#!/usr/bin/env python3
"""unc007_parity.py — A4 parity gate: C++ OZNU handle == Python reference head.

Runs a set of FENs through BOTH:
  (a) the engine's `--unc-probe` (the C++ EvalWithDistribution path), and
  (b) the Python reference (load_ft + read_head_bin + the MDNt forward, same
      quantile bisection as train_unc_head.calibration),
and asserts the mixture params, E[u|x], and the tail quantiles agree within
tolerance. This is the H0-style correctness gate for the deployed path: nothing
downstream (harness, plots) is trustworthy until it is green.

The head+trunk are read straight out of the fused OZNU file (also exercising the
Python-side OZNU extraction), so the two sides provably use the same weights.

Usage:
  make unc_harness
  python3 unc_research/scripts/unc007_parity.py \
      --net unc_research/models/nnue_unc.bin --harness build/unc_harness
"""

import argparse
import json
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np
import torch
from scipy.stats import t as student_t

SCRIPTS_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPTS_DIR))
from oznu import read_oznu, _embed_fens, _PROBE_FENS  # noqa: E402
from train_unc_head import read_head_bin, load_ft  # noqa: E402

TAUS = [0.01, 0.05, 0.10, 0.25, 0.50, 0.75, 0.90, 0.95, 0.99]


def python_reference(fens, nnue_bytes, head_bytes):
    """Per-FEN (e_u_cp, pi, mu, sigma, df, {tau: q_cp}) from the Python head."""
    with tempfile.TemporaryDirectory() as td:
        nnue_p = Path(td) / "trunk.bin"
        head_p = Path(td) / "head.bin"
        nnue_p.write_bytes(nnue_bytes)
        head_p.write_bytes(head_bytes)
        ft_w, ft_b = load_ft(str(nnue_p))
        model, meta = read_head_bin(str(head_p))

    u_mean, u_std = meta["u_mean_cp"], meta["u_std_cp"]
    x = _embed_fens(fens, ft_w, ft_b)
    if meta.get("version", 0) >= 3:  # QAT int8: exact integer forward (matches C++)
        from train_unc_head import int8_head_params
        xn = x.numpy() if hasattr(x, "numpy") else np.asarray(x)
        log_pi, mu, sigma, df = int8_head_params(meta["qw"], meta["qb"], meta["k"],
                                                 xn, meta["out_descale"])
        pi = np.exp(log_pi)
    else:
        with torch.no_grad():
            log_pi, mu, sigma, df = model.params(x)
        pi = log_pi.exp().numpy()
        mu, sigma, df = mu.numpy(), sigma.numpy(), df.numpy()

    def mix_cdf(row, yv):  # standardized CDF for one FEN's mixture
        z = (yv - mu[row]) / sigma[row]
        return float((pi[row] * student_t.cdf(z, df[row])).sum())

    out = []
    for r in range(len(fens)):
        e_u = float((pi[r] * mu[r]).sum()) * u_std + u_mean
        q = {}
        for tau in TAUS:
            lo, hi = -50.0, 50.0
            for _ in range(60):
                mid = 0.5 * (lo + hi)
                if mix_cdf(r, mid) > tau:
                    hi = mid
                else:
                    lo = mid
            q[tau] = 0.5 * (lo + hi) * u_std + u_mean
        out.append({"e_u_cp": e_u, "pi": pi[r], "mu": mu[r], "sigma": sigma[r],
                    "df": df[r], "q": q})
    return out


def cpp_probe(harness, net, fens):
    """Run build/unc_harness over `fens`; return the parsed JSON dicts."""
    proc = subprocess.run(
        [harness, net],
        input="\n".join(fens) + "\n", capture_output=True, text=True, check=True)
    rows = {}
    for line in proc.stdout.splitlines():
        line = line.strip()
        if not line.startswith("{"):
            continue  # (harness status goes to stderr; stdout is JSON only)
        d = json.loads(line)
        rows[d["fen"]] = d
    return rows


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--net", default="unc_research/models/nnue_unc.bin")
    ap.add_argument("--harness", default="build/unc_harness")
    ap.add_argument("--fens", default=None, help="file of FENs (default: probe set)")
    ap.add_argument("--param-tol", type=float, default=2e-3)
    ap.add_argument("--q-tol-cp", type=float, default=0.5, help="quantile tol (cp)")
    a = ap.parse_args()

    if a.fens:
        fens = [ln.strip() for ln in Path(a.fens).read_text().splitlines() if ln.strip()]
    else:
        fens = list(_PROBE_FENS)

    parts = read_oznu(a.net)
    ref = python_reference(fens, parts["nnue_bytes"], parts["head_bytes"])
    got = cpp_probe(a.harness, a.net, fens)

    worst_param = 0.0
    worst_eu = 0.0
    worst_q = 0.0
    for fen, r in zip(fens, ref):
        c = got[fen]
        # Mixture params compare only after matching component order (both come
        # from the same weights, so order is identical -- compare elementwise).
        for name, arr in (("pi", r["pi"]), ("mu", r["mu"]),
                          ("sigma", r["sigma"]), ("df", r["df"])):
            cp = np.array(c[name], dtype=np.float64)
            d = float(np.max(np.abs(cp - arr)))
            # df is O(1..100); scale its tolerance by magnitude.
            scale = max(1.0, float(np.max(np.abs(arr)))) if name == "df" else 1.0
            worst_param = max(worst_param, d / scale)
        worst_eu = max(worst_eu, abs(c["e_u_cp"] - r["e_u_cp"]))
        for tau in TAUS:
            worst_q = max(worst_q, abs(c["q"][f"{tau:.2f}"] - r["q"][tau]))

    print(f"FENs: {len(fens)}")
    print(f"  max |Δ mixture param| (df scaled) = {worst_param:.3e}  (tol {a.param_tol})")
    print(f"  max |Δ E[u|x]| (cp)               = {worst_eu:.3e}  (tol {a.q_tol_cp})")
    print(f"  max |Δ quantile| (cp)             = {worst_q:.3e}  (tol {a.q_tol_cp})")

    ok = (worst_param <= a.param_tol and worst_eu <= a.q_tol_cp and
          worst_q <= a.q_tol_cp)
    if ok:
        print("PASS: C++ handle matches the Python reference head.")
        return 0
    print("FAIL: parity exceeded tolerance.")
    return 1


if __name__ == "__main__":
    sys.exit(main())
