#!/usr/bin/env python3
"""
OmegaZero SPSA (Simultaneous Perturbation Stochastic Approximation) tuning.

SPSA tunes many search parameters at once by playing self-play games between two
perturbed copies of the *same* engine binary and nudging every parameter toward
whichever perturbation scored better. One game pair yields a gradient estimate
for the entire parameter vector, so high-dimensional tuning stays cheap.

Everything is driven through the shared parameter registry (src/params.h): the
engine advertises each knob as an integer `spin` UCI option (doubles are
integer-scaled), so SPSA works purely in integer UCI units — the same units
params.json stores. Perturbations are applied per game via cutechess-cli's
`option.<Name>=<value>`; because the engine loads params.json on init and
`setoption` overrides it, only the *tuned* parameters need to be passed each
iteration (untuned ones stay at their current best from params.json).

Results are written back into the matching params.json profile ("nnue" or
"hce"), so a completed run drops straight into normal play.

Requires cutechess-cli (auto-detected in cutechess/build/ or PATH), and a built
engine (`make` first).

Subcommands:
    run    — Run an SPSA tuning session and update params.json.
    init   — Emit an editable spsa_config.json listing all tunable knobs.
    plot   — Plot parameter trajectories from a run's history.csv.

Algorithm (standard Spall SPSA with the OpenBench schedule):
    alpha = 0.602, gamma = 0.101, A = 0.1 * iterations
    c_t = c / t^gamma           with c = c_end * iterations^gamma
    a_t = a / (A + t)^alpha      with a = r_end * c_end^2 * (A + iterations)^alpha
    per iteration t:
        flip_i in {-1, +1}
        theta_plus  = clip(theta + c_t * flip)
        theta_minus = clip(theta - c_t * flip)
        result = plus's score over the game batch, in [0, 1]
        theta_i += a_t * (result - 0.5) * flip_i / c_t   (then clip)
    c_end is the final perturbation magnitude; r_end the final learning rate.

Usage:
    # Tune a subset for the NNUE profile, 20k games at 8+0.08, 6 concurrent
    python3 scripts/spsa.py run --params RazoringMargin,SeeMargin,FutilityMargin \\
        --games 20000 --tc 8+0.08 --concurrency 6

    # Tune every knob for the HCE profile
    python3 scripts/spsa.py run --profile hce --games 40000 --tc 8+0.08 -c 8

    # Generate an editable config, hand-pick knobs, then run it
    python3 scripts/spsa.py init --out spsa_config.json
    python3 scripts/spsa.py run --config spsa_config.json --games 30000

    # Plot parameter trajectories from a run
    python3 scripts/spsa.py plot results/spsa/2026-07-21_.../history.csv
"""

import argparse
import csv
import json
import random
import re
import subprocess
import sys
from datetime import datetime
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPT_DIR))
import sprt  # noqa: E402  (shares find_cutechess / REPO_ROOT / openings default)

REPO_ROOT = sprt.REPO_ROOT
RESULTS_DIR = REPO_ROOT / "results" / "spsa"
DEFAULT_OPENINGS = sprt.DEFAULT_OPENINGS
DEFAULT_PARAMS_JSON = REPO_ROOT / "params.json"

# Curated time-control cycle used when neither --tc nor --st is given. Chosen to
# exercise dynamic time management across the bases that matter: a range of base
# clocks (5s -> 5min), a range of increments (none -> 3s), an inc-vs-no-inc pair
# at the same base (60+0.6 / 60+0) to isolate increment effects, and a couple of
# fixed time-per-move settings (which leave dynamic TM off by design, covering
# the fixed-time path). Entries prefixed "st:" are fixed time/move; the rest are
# base+increment clocks. The heavier clocks (180+2, 300+3) make individual games
# slow, so for a quick run pass an explicit faster --tc (e.g. --tc 8+0.08).
DEFAULT_TC_CYCLE = [
    "5+0.05",    # ultra-fast blitz, tiny increment
    "10+0.1",    # fast blitz
    "30+0.3",    # moderate blitz
    "60+0.6",    # 1min + 0.6s increment
    "60+0",      # 1min sudden death (no increment) — inc-vs-no-inc control
    "180+2",     # 3+2 blitz
    "300+3",     # 5+3 — "5 minutes per side with increment"
    "st:0.5",    # fixed 0.5s/move
    "st:1.0",    # fixed 1.0s/move
]

ALPHA = 0.602
GAMMA = 0.101


# ---------------------------------------------------------------------------
# Engine introspection
# ---------------------------------------------------------------------------

def get_engine_options(engine, extra_args):
    """Enumerate the engine's spin options via `uci`. Returns {name: (def,min,max)}."""
    try:
        proc = subprocess.run(
            [str(engine), "--uci", *extra_args],
            input="uci\nquit\n", capture_output=True, text=True, timeout=30,
        )
    except (subprocess.TimeoutExpired, FileNotFoundError) as e:
        sys.exit(f"Failed to query engine options: {e}")

    opts = {}
    pat = re.compile(
        r"option name (\S+) type spin default (-?\d+) min (-?\d+) max (-?\d+)"
    )
    for line in proc.stdout.splitlines():
        m = pat.match(line.strip())
        if m:
            opts[m.group(1)] = (int(m.group(2)), int(m.group(3)), int(m.group(4)))
    if not opts:
        sys.exit("Engine advertised no spin options — is it up to date?")
    return opts


def clamp(x, lo, hi):
    return max(lo, min(hi, x))


# ---------------------------------------------------------------------------
# params.json I/O (updates only the tuned profile, preserves the rest)
# ---------------------------------------------------------------------------

def load_params_json(path):
    p = Path(path)
    if p.exists():
        try:
            return json.loads(p.read_text())
        except json.JSONDecodeError as e:
            sys.exit(f"Malformed {path}: {e}")
    return {}


def write_params_json(path, data):
    Path(path).write_text(json.dumps(data, indent=2) + "\n")


def save_profile(path, profile, theta, out_path=None):
    """Round theta into params.json[profile] and write the result to `out_path`
    (defaults to `path`), preserving all other content. Reading the full file
    from `path` keeps every checkpoint a complete, drop-in params.json."""
    data = load_params_json(path)
    prof = data.setdefault(profile, {})
    for name, val in theta.items():
        prof[name] = int(round(val))
    write_params_json(out_path or path, data)


# ---------------------------------------------------------------------------
# One SPSA iteration: play a game batch between the plus/minus perturbations
# ---------------------------------------------------------------------------

def play_batch(cutechess, engine, extra_args, plus, minus, args, openings,
               time_tokens):
    """Play `args.games_per_iter` games at the given time control; return plus's
    mean score in [0,1] and the game count, or (None, 0) on interrupt.
    `time_tokens` is the cutechess `-each` time clause for this batch, e.g.
    ['tc=300+3', 'timemargin=500'] or ['st=0.5']."""
    rounds = max(1, args.games_per_iter // 2)

    def engine_block(name, vals):
        block = ["-engine", f"name={name}", f"cmd={engine}", "proto=uci",
                 "arg=--uci"]
        block += [f"arg={a}" for a in extra_args]
        block += [f"option.{k}={v}" for k, v in vals.items()]
        # Force single-threaded search: Lazy SMP is nondeterministic, which adds
        # noise to the SPSA gradient estimate, and multi-threaded games under
        # -concurrency would oversubscribe cores. Applied last so it always wins.
        block += ["option.Threads=1"]
        return block

    fmt = "epd" if str(openings).endswith(".epd") else "pgn"
    cmd = [
        cutechess,
        *engine_block("plus", plus),
        *engine_block("minus", minus),
        "-each", *time_tokens,
        "-rounds", str(rounds), "-games", "2", "-repeat", "-recover",
        "-openings", f"file={openings}", f"format={fmt}", "order=random",
        "-srand", str(random.randint(1, 2**31 - 1)),
    ]
    conc = min(args.concurrency, args.games_per_iter)
    if conc > 1:
        cmd += ["-concurrency", str(conc)]

    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE,
                            stderr=subprocess.DEVNULL, text=True)
    score, played = 0.0, 0
    finished = re.compile(r"Finished game \d+ \((.+?) vs (.+?)\): (\S+)")
    try:
        for line in proc.stdout:
            m = finished.search(line.strip())
            if not m:
                continue
            white, result = m.group(1).strip(), m.group(3)
            plus_white = (white == "plus")
            if result == "1-0":
                s = 1.0 if plus_white else 0.0
            elif result == "0-1":
                s = 0.0 if plus_white else 1.0
            else:
                s = 0.5
            score += s
            played += 1
    except KeyboardInterrupt:
        proc.terminate()
        proc.wait()
        return None, 0
    proc.wait()
    if played == 0:
        return 0.5, 0  # no games completed: treat as no signal
    return score / played, played


# ---------------------------------------------------------------------------
# run subcommand
# ---------------------------------------------------------------------------

def select_tuned(args, opts):
    """Resolve the set of parameters to tune and per-param (c_end, r_end)."""
    tuned = {}  # name -> dict(min, max, c_end, r_end)
    if args.config:
        cfg = json.loads(Path(args.config).read_text())
        for name, spec in cfg.get("params", {}).items():
            if not spec.get("tune", True):
                continue
            if name not in opts:
                print(f"  warning: config param {name} not advertised by engine, skipping")
                continue
            _, lo, hi = opts[name]
            tuned[name] = {
                "min": spec.get("min", lo), "max": spec.get("max", hi),
                "c_end": float(spec.get("c_end", default_c_end(lo, hi))),
                "r_end": float(spec.get("r_end", args.r_end)),
            }
    else:
        if args.params:
            names = [p.strip() for p in args.params.split(",") if p.strip()]
        else:
            names = list(opts)  # tune everything
        for name in names:
            if name not in opts:
                sys.exit(f"Unknown parameter: {name}\nAdvertised: {', '.join(opts)}")
            _, lo, hi = opts[name]
            c_end = args.c_end if args.c_end is not None else default_c_end(lo, hi)
            tuned[name] = {"min": lo, "max": hi, "c_end": float(c_end),
                           "r_end": args.r_end}
    if not tuned:
        sys.exit("No parameters selected to tune.")
    return tuned


def default_c_end(lo, hi):
    """A sane default perturbation: ~1/20 of the range, at least 1 unit."""
    return max(1.0, (hi - lo) / 20.0)


def _classify_tc(spec):
    """Classify one time-control spec into ('st'|'tc', value). A leading "st:" or
    "st=" marks fixed time/move; anything else is a base+increment clock."""
    spec = spec.strip()
    low = spec.lower()
    if low.startswith("st:") or low.startswith("st="):
        return ("st", spec[3:].strip())
    return ("tc", spec)


def build_tc_pool(args):
    """Resolve the pool of time controls SPSA cycles through, one per iteration.

    Both --tc and --st accept comma-separated lists and are combined, so a run
    can span several clocks and fixed-time settings (e.g.
    --tc 300+3,180+2,60+1 --st 0.5,1.0). When neither is given, the curated
    DEFAULT_TC_CYCLE is used. Each entry becomes the cutechess `-each` time
    clause for the iterations that use it. Returns a list of
    (time_tokens, label); the loop selects pool[(iter - 1) % len(pool)] so every
    time control gets an even, deterministic share. Dynamic time management only
    engages under a `tc=` clock, so a varied pool exercises the Tm* parameters
    across real clocks (fixed `st=` games leave TM inert by design)."""
    entries = []  # (kind, value)
    if args.tc:
        entries += [("tc", s.strip()) for s in args.tc.split(",") if s.strip()]
    if args.st:
        entries += [("st", s.strip()) for s in args.st.split(",") if s.strip()]
    if not entries:
        entries = [_classify_tc(s) for s in DEFAULT_TC_CYCLE]

    pool = []
    for kind, val in entries:
        if kind == "tc":
            pool.append(([f"tc={val}", "timemargin=500"], f"tc={val}"))
        else:
            pool.append(([f"st={val}"], f"st={val}"))
    return pool


def cmd_run(args):
    engine = Path(args.engine)
    if not engine.is_absolute():
        engine = REPO_ROOT / engine
    engine = engine.resolve()
    if not engine.exists():
        sys.exit(f"Engine not found: {engine}\nRun 'make' first.")
    if args.profile not in ("nnue", "hce"):
        sys.exit("--profile must be 'nnue' or 'hce'")

    extra_args = ["--hce"] if args.profile == "hce" else []
    cutechess = sprt.find_cutechess(args.cutechess)
    opts = get_engine_options(engine, extra_args)
    tuned = select_tuned(args, opts)

    openings = Path(args.openings)
    if not openings.is_absolute():
        openings = REPO_ROOT / openings
    if not openings.exists():
        sys.exit(f"Openings file not found: {openings}")

    # Starting point: current params.json[profile] values, else registry default.
    profile_vals = load_params_json(args.out).get(args.profile, {})
    theta = {name: float(profile_vals.get(name, opts[name][0])) for name in tuned}

    iterations = max(1, args.games // args.games_per_iter)
    A = 0.1 * iterations
    # Precompute per-param schedule coefficients.
    c0 = {n: t["c_end"] * iterations ** GAMMA for n, t in tuned.items()}
    a0 = {n: t["r_end"] * t["c_end"] ** 2 * (A + iterations) ** ALPHA
          for n, t in tuned.items()}

    ts = datetime.now().strftime("%Y-%m-%d_%H-%M-%S")
    run_dir = RESULTS_DIR / f"{ts}_{args.profile}_{sprt.get_version_tag()}"
    run_dir.mkdir(parents=True, exist_ok=True)
    checkpoint_dir = run_dir / "checkpoint"
    checkpoint_dir.mkdir(parents=True, exist_ok=True)
    history_csv = run_dir / "history.csv"
    names = list(tuned)

    tc_pool = build_tc_pool(args)
    tc_desc = " | ".join(label for _, label in tc_pool)
    if not args.tc and not args.st:
        tc_desc += "   (curated default)"
    # Dynamic TM only runs under a clock; warn if TM params are being tuned but
    # every time control is fixed-time (they would never be exercised).
    tunes_tm = any(n.startswith("Tm") for n in names)
    has_clock = any(label.startswith("tc=") for _, label in tc_pool)
    if tunes_tm and not has_clock:
        print("  WARNING: tuning TM params but all time controls are fixed "
              "(st=); dynamic TM only engages under a clock, so the Tm* "
              "perturbations will have no effect. Add a --tc entry.")

    print(f"\n{'=' * 68}")
    print(f"  SPSA tuning — profile '{args.profile}'  ({len(names)} params)")
    print(f"  {args.games} games / {args.games_per_iter} per iter "
          f"= {iterations} iterations")
    print(f"  Time controls (cycled): {tc_desc}")
    print(f"  Params: {', '.join(names)}")
    print(f"  Checkpoints -> {checkpoint_dir}/ (best -> best.json; {args.out} read-only)")
    print(f"{'=' * 68}\n")

    rng = random.Random(args.seed)
    with open(history_csv, "w", newline="") as hf:
        writer = csv.writer(hf)
        writer.writerow(["iter", "games", "result"] + names)

        games_done = 0
        try:
            for t in range(1, iterations + 1):
                ct = {n: c0[n] / t ** GAMMA for n in names}
                at = {n: a0[n] / (A + t) ** ALPHA for n in names}
                flip = {n: rng.choice((-1, 1)) for n in names}
                plus = {n: int(round(clamp(theta[n] + ct[n] * flip[n],
                                           tuned[n]["min"], tuned[n]["max"])))
                        for n in names}
                minus = {n: int(round(clamp(theta[n] - ct[n] * flip[n],
                                            tuned[n]["min"], tuned[n]["max"])))
                         for n in names}

                time_tokens, tc_label = tc_pool[(t - 1) % len(tc_pool)]
                result, played = play_batch(cutechess, engine, extra_args,
                                            plus, minus, args, openings,
                                            time_tokens)
                if result is None:
                    print("\n  Interrupted — saving current best.", flush=True)
                    break
                games_done += played

                for n in names:
                    grad = (result - 0.5) * flip[n] / ct[n]
                    theta[n] = clamp(theta[n] + at[n] * grad,
                                     tuned[n]["min"], tuned[n]["max"])

                writer.writerow([t, played, f"{result:.3f}"]
                                + [int(round(theta[n])) for n in names])
                hf.flush()

                if t % args.report_every == 0 or t == iterations:
                    snap = "  ".join(f"{n}={int(round(theta[n]))}" for n in names)
                    print(f"  iter {t:5d}/{iterations}  ({games_done} games)  "
                          f"last={result:.2f} [{tc_label}]\n    {snap}",
                          flush=True)
                if t % args.checkpoint_every == 0:
                    save_profile(args.out, args.profile, theta,
                                 out_path=checkpoint_dir / f"iter_{t:05d}.json")
        except KeyboardInterrupt:
            print("\n  Interrupted — saving current best.", flush=True)

    best_json = checkpoint_dir / "best.json"
    save_profile(args.out, args.profile, theta, out_path=best_json)
    print(f"\n{'=' * 68}")
    print(f"  Done. Tuned {len(names)} params over {games_done} games.")
    print(f"  Best values written to {best_json} [{args.profile}]:")
    for n in names:
        start = float(profile_vals.get(n, opts[n][0]))
        print(f"    {n:<26} {int(round(start)):>6} -> {int(round(theta[n])):>6}")
    print(f"  History:     {history_csv}")
    print(f"  Checkpoints: {checkpoint_dir}/")
    print(f"{'=' * 68}")
    return 0


# ---------------------------------------------------------------------------
# init subcommand
# ---------------------------------------------------------------------------

def cmd_init(args):
    engine = Path(args.engine)
    if not engine.is_absolute():
        engine = REPO_ROOT / engine
    engine = engine.resolve()
    if not engine.exists():
        sys.exit(f"Engine not found: {engine}\nRun 'make' first.")

    extra_args = ["--hce"] if args.profile == "hce" else []
    opts = get_engine_options(engine, extra_args)
    profile_vals = load_params_json(DEFAULT_PARAMS_JSON).get(args.profile, {})

    params = {}
    for name, (dflt, lo, hi) in opts.items():
        params[name] = {
            "value": int(profile_vals.get(name, dflt)),
            "min": lo, "max": hi,
            "c_end": round(default_c_end(lo, hi), 3),
            "r_end": args.r_end,
            "tune": True,
        }
    cfg = {"profile": args.profile, "params": params}
    Path(args.out).write_text(json.dumps(cfg, indent=2) + "\n")
    print(f"Wrote tuning config ({len(params)} params) to {args.out}")
    print("Edit it (set \"tune\": false to freeze a knob), then:")
    print(f"  python3 scripts/spsa.py run --config {args.out} --games 20000 --tc 8+0.08")
    return 0


# ---------------------------------------------------------------------------
# plot subcommand
# ---------------------------------------------------------------------------

def cmd_plot(args):
    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
    except ImportError:
        sys.exit("matplotlib not installed — pip3 install matplotlib")

    history = Path(args.history)
    if not history.exists():
        sys.exit(f"History file not found: {history}")
    with open(history) as f:
        rows = list(csv.DictReader(f))
    if not rows:
        sys.exit("History is empty.")

    names = [c for c in rows[0] if c not in ("iter", "games", "result")]
    iters = [int(r["iter"]) for r in rows]

    fig, ax = plt.subplots(figsize=(11, 6))
    for n in names:
        ax.plot(iters, [float(r[n]) for r in rows], label=n, linewidth=1.3)
    ax.set_xlabel("SPSA iteration")
    ax.set_ylabel("Parameter value (UCI units)")
    ax.set_title("SPSA Parameter Trajectories")
    ax.grid(True, alpha=0.25)
    ax.legend(fontsize=8, ncol=2, loc="best")
    fig.tight_layout()
    out = history.parent / "trajectories.png"
    fig.savefig(out, dpi=150)
    print(f"Saved {out}")
    return 0


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(description="OmegaZero SPSA tuning")
    sub = parser.add_subparsers(dest="command")

    run_p = sub.add_parser("run", help="Run an SPSA tuning session")
    run_p.add_argument("--engine", default="build/OmegaZero",
                       help="Engine binary (default: build/OmegaZero)")
    run_p.add_argument("--profile", default="nnue", choices=["nnue", "hce"],
                       help="params.json profile / eval mode to tune (default: nnue)")
    run_p.add_argument("--params", default=None,
                       help="Comma-separated knobs to tune (default: all). "
                            "Ignored if --config is given.")
    run_p.add_argument("--config", default=None,
                       help="spsa_config.json from `init` (overrides --params)")
    run_p.add_argument("--games", type=int, default=20000,
                       help="Total game budget (default: 20000)")
    run_p.add_argument("--games-per-iter", type=int, default=2, dest="games_per_iter",
                       help="Games per SPSA iteration; 2 = one color-balanced pair "
                            "(default: 2)")
    run_p.add_argument("-c", "--c-end", type=float, default=None, dest="c_end",
                       help="Final perturbation magnitude for all params "
                            "(default: per-param, ~range/20)")
    run_p.add_argument("-r", "--r-end", type=float, default=0.002, dest="r_end",
                       help="Final learning rate (default: 0.002)")
    run_p.add_argument("--st", default=None,
                       help="Fixed time/move seconds; comma-separated to cycle "
                            "(e.g. '0.5,1.0'). Combined with any --tc entries.")
    run_p.add_argument("--tc", default=None,
                       help="Real clock(s), comma-separated to cycle through "
                            "(e.g. '300+3,180+2,60+1'). Combined with any --st "
                            "entries into a pool, one TC per iteration. When "
                            "neither --tc nor --st is given, a curated default "
                            "cycle spanning bullet->5+3 plus fixed-time is used. "
                            "Dynamic TM only engages under a clock, so tuning "
                            "the Tm* params needs at least one --tc entry.")
    run_p.add_argument("--concurrency", type=int, default=1,
                       help="Concurrent games (default: 1)")
    run_p.add_argument("--openings", default=str(DEFAULT_OPENINGS),
                       help="Opening book (.pgn/.epd)")
    run_p.add_argument("--out", default=str(DEFAULT_PARAMS_JSON),
                       help="params.json to update (default: repo-root params.json)")
    run_p.add_argument("--seed", type=int, default=None,
                       help="RNG seed for reproducible perturbation flips")
    run_p.add_argument("--report-every", type=int, default=10, dest="report_every",
                       help="Print progress every N iterations (default: 10)")
    run_p.add_argument("--checkpoint-every", type=int, default=25,
                       dest="checkpoint_every",
                       help="Write params.json every N iterations (default: 25)")
    run_p.add_argument("--cutechess", default=None,
                       help="Path to cutechess-cli (default: auto-detect)")

    init_p = sub.add_parser("init", help="Emit an editable spsa_config.json")
    init_p.add_argument("--engine", default="build/OmegaZero")
    init_p.add_argument("--profile", default="nnue", choices=["nnue", "hce"])
    init_p.add_argument("-r", "--r-end", type=float, default=0.002, dest="r_end")
    init_p.add_argument("--out", default="spsa_config.json")

    plot_p = sub.add_parser("plot", help="Plot parameter trajectories")
    plot_p.add_argument("history", help="Path to a run's history.csv")

    args = parser.parse_args()
    if args.command == "run":
        sys.exit(cmd_run(args))
    elif args.command == "init":
        sys.exit(cmd_init(args))
    elif args.command == "plot":
        sys.exit(cmd_plot(args))
    else:
        parser.print_help()
        sys.exit(1)


if __name__ == "__main__":
    main()
