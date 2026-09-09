#!/usr/bin/env python3
"""Zero-game diagnostic: magnitude of the H6 mean correction E[u|x] at the kind
of positions RFP thresholds against, vs FutilityMargin (the constant the null
RFP margin would use). If |E[u|x]| is small vs 260cp, the corrected static_eval
barely moves the RFP decision boundary => the v5-tuned margin stays ~optimal =>
no per-feature retune needed (evidence alongside unc-008 + HCE->NNUE 20-30 Elo).
"""
import re, subprocess, json, sys, statistics as st

ROOT = "/Users/noah/dev/OmegaZero"
FUTILITY_MARGIN = 260
FILES = [
    f"{ROOT}/unc_research/positions/suites/sbd_quiet.epd",
    f"{ROOT}/unc_research/positions/suites/wac_sharp.epd",
    f"{ROOT}/unc_research/positions/unc007_curated.txt",
]


def norm_fen(line):
    line = line.strip()
    if not line or line.startswith("#"):
        return None
    # cut annotations: '|' (curated), ';' (epd id), ' bm ' (wac)
    for sep in ("|", ";"):
        if sep in line:
            line = line.split(sep, 1)[0]
    line = re.split(r"\s+bm\s+", line)[0].strip()
    f = line.split()
    if len(f) < 4:
        return None
    # normalize to exactly 6 fields (EPD gives 4; pad halfmove/fullmove)
    f = f[:6]
    while len(f) < 6:
        f.append("0" if len(f) == 4 else "1")
    return " ".join(f)


fens = []
for path in FILES:
    with open(path) as fh:
        for ln in fh:
            fen = norm_fen(ln)
            if fen:
                fens.append(fen)

print(f"positions: {len(fens)}", file=sys.stderr)
proc = subprocess.run(
    [f"{ROOT}/build/unc_harness", f"{ROOT}/nnue/nnue_unc.bin"],
    input="\n".join(fens) + "\n", capture_output=True, text=True,
)
if proc.returncode != 0:
    print("HARNESS STDERR:\n", proc.stderr[-2000:], file=sys.stderr)
    sys.exit(1)

e_u, e_u_i8, ks = [], [], []
skipped = 0
for line in proc.stdout.splitlines():
    line = line.strip()
    if not line.startswith("{"):
        continue
    d = json.loads(line)
    if "e_u_cp" not in d:
        if skipped < 3:
            print("SKIP (no e_u_cp):", line[:160], file=sys.stderr)
        skipped += 1
        continue
    e_u.append(d["e_u_cp"])       # float MeanCp (HEAD's RFP-node value)
    e_u_i8.append(d["e_u_cp_i8"]) # int8 corrector (search fast path)
    ks.append(d["k"])
if skipped:
    print(f"(skipped {skipped} records with no e_u_cp)", file=sys.stderr)

n = len(e_u)
if n == 0:
    print("no distributions parsed; head loaded?", file=sys.stderr)
    print(proc.stderr[-1000:], file=sys.stderr)
    sys.exit(1)


def pct(xs, p):
    xs = sorted(xs)
    return xs[min(len(xs) - 1, int(p / 100 * len(xs)))]


ab = [abs(x) for x in e_u]
M = FUTILITY_MARGIN
print(f"\n=== H6 mean correction E[u|x] over {n} RFP-style positions ===")
print(f"FutilityMargin (constant RFP scale) = {M} cp  (boundary = {M}*depth, depth 1-2)\n")
print(f"signed mean  E[u|x]      = {st.mean(e_u):+7.2f} cp   <- systematic bias (what shifts the optimal constant)")
print(f"signed stdev             = {st.pstdev(e_u):7.2f} cp")
print(f"mean |E[u|x]|            = {st.mean(ab):7.2f} cp   ({100*st.mean(ab)/M:.1f}% of margin)")
print(f"median |E[u|x]|          = {st.median(ab):7.2f} cp")
print(f"p90 |E[u|x]|             = {pct(ab,90):7.2f} cp")
print(f"p99 |E[u|x]|             = {pct(ab,99):7.2f} cp")
print(f"max |E[u|x]|             = {max(ab):7.2f} cp")
print(f"\nfraction |E[u|x]| > 10% margin (26cp)  = {100*sum(a>0.10*M for a in ab)/n:5.1f}%")
print(f"fraction |E[u|x]| > 20% margin (52cp)  = {100*sum(a>0.20*M for a in ab)/n:5.1f}%")
print(f"fraction |E[u|x]| > 50% margin (130cp) = {100*sum(a>0.50*M for a in ab)/n:5.1f}%")
# int8 (the value the search actually adds) sanity vs float
di = [abs(a - b) for a, b in zip(e_u, e_u_i8)]
print(f"\nint8 vs float mean correction: mean|diff| = {st.mean(di):.2f} cp, max = {max(di):.2f} cp")
print(f"mixture components k: min={min(ks)} max={max(ks)}")
