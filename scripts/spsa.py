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
    plot   — Plot a run's start->end parameter shift from its history.csv.

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

A --config file may also carry an optional top-level "run" block to make it a
self-contained recipe (any subset of games / games_per_iter / concurrency / tc):
    { "profile": "nnue",
      "run": {"games": 30000, "games_per_iter": 4, "concurrency": 4,
              "tc": "10+0.1,30+0.3,60+0.6,60+0"},
      "params": { ... } }
Explicit CLI flags override the "run" block, which overrides the built-in
defaults. So `run --config spsa_nnue_config.json` needs no other flags.

Resuming an interrupted run:
    A run's full state is just (theta, t): theta lives in every checkpoint (a
    drop-in params.json) and t is the checkpoint's filename index. The gain
    schedule (c_t, a_t) is a pure function of t and the fixed total `iterations`,
    so resuming at t+1 with an unchanged budget reconstructs it exactly — no
    re-annealing. --resume takes a run dir (or its checkpoint/ dir), warm-starts
    theta from the newest iter_*.json, and appends to the same history.csv and
    checkpoints. Pass the SAME run recipe (--config/--games) the original used so
    the total `iterations` matches; a run started after this feature landed also
    drops a meta.json that pins the schedule automatically.
    # Resume the run whose config was spsa_nnue_config.json
    python3 scripts/spsa.py run --config spsa_nnue_config.json \\
        --resume results/spsa/2026-08-26_10-34-52_nnue_5008881

    # Plot the start->end parameter shift from a run
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


def apply_config_run_defaults(args):
    """Fill run settings from an optional top-level 'run' block in --config.

    Only fills settings the user did NOT pass on the CLI (their argparse default
    is the None sentinel), so an explicit flag always wins over the config, which
    in turn wins over the hard-coded fallback. No-op when there's no --config or
    no 'run' block. Keeps a --config file a single self-contained recipe."""
    run_cfg = {}
    if getattr(args, "config", None):
        try:
            run_cfg = json.loads(Path(args.config).read_text()).get("run", {}) or {}
        except (OSError, ValueError):
            run_cfg = {}
    for attr, hard in (("games", 20000), ("games_per_iter", 2),
                       ("concurrency", 1)):
        if getattr(args, attr) is None:
            setattr(args, attr, int(run_cfg.get(attr, hard)))
    # tc/st sentinel is also None; only fill from config when the user gave
    # neither, so the built-in default TC cycle still triggers when nothing is
    # specified anywhere.
    for attr in ("tc", "st"):
        if getattr(args, attr) is None and run_cfg.get(attr):
            setattr(args, attr, str(run_cfg[attr]))
    return args


def _resolve_resume(resume_path):
    """Locate the state needed to resume a prior run.

    Accepts either a run dir (holding `checkpoint/` + `history.csv`) or the
    `checkpoint/` dir itself. Returns (run_dir, checkpoint_dir, newest_cp,
    t_done) where t_done is the iteration index of the newest checkpoint — the
    loop resumes at t_done + 1. SPSA carries no state across iterations beyond
    theta and t, so those two are a complete snapshot."""
    p = Path(resume_path)
    if not p.is_absolute():
        p = REPO_ROOT / p
    if not p.exists():
        sys.exit(f"--resume path not found: {p}")
    checkpoint_dir = p / "checkpoint" if (p / "checkpoint").is_dir() else p
    run_dir = checkpoint_dir.parent
    cps = sorted(checkpoint_dir.glob("iter_*.json"))
    if not cps:
        sys.exit(f"--resume: no iter_*.json checkpoints under {checkpoint_dir}")
    newest = cps[-1]
    try:
        t_done = int(newest.stem.split("_")[1])
    except (IndexError, ValueError):
        sys.exit(f"--resume: cannot parse iteration from {newest.name}")
    return run_dir, checkpoint_dir, newest, t_done


def _first_history_start(history_csv, names):
    """Best-effort true start values from a run's first history row (used only
    for the summary/plot when meta.json is absent). This is the post-iter-1
    theta, so it's off by one small step from the true pre-run start — close
    enough for a shift plot, and the only record older runs kept."""
    try:
        with open(history_csv, newline="") as f:
            r = csv.reader(f)
            header = next(r, None)
            first = next(r, None)
    except OSError:
        return None
    if not header or not first:
        return None
    idx = {h: i for i, h in enumerate(header)}
    return {n: float(first[idx[n]]) for n in names if n in idx}


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

    names = list(tuned)

    # ---- Resume vs fresh run -------------------------------------------------
    # A run's entire resumable state is (theta, t): theta lives in each
    # checkpoint (a drop-in params.json), t is the checkpoint's filename index.
    # The gain schedule (c_t, a_t) is a pure function of t and the fixed total
    # `iterations`, so resuming at t_done+1 with an unchanged `iterations`
    # reconstructs it exactly — no re-annealing.
    orig_start = None  # true pre-run start values, for the summary/shift plot
    if args.resume:
        run_dir, checkpoint_dir, newest_cp, t_done = _resolve_resume(args.resume)
        history_csv = run_dir / "history.csv"
        # Pin the schedule from meta.json when present (future runs write it);
        # otherwise trust the CLI/config-derived `iterations` (same command).
        meta = {}
        meta_path = run_dir / "meta.json"
        if meta_path.exists():
            try:
                meta = json.loads(meta_path.read_text())
            except (OSError, ValueError):
                meta = {}
        if meta.get("iterations"):
            iterations = int(meta["iterations"])
            A = 0.1 * iterations
            c0 = {n: tuned[n]["c_end"] * iterations ** GAMMA for n in names}
            a0 = {n: tuned[n]["r_end"] * tuned[n]["c_end"] ** 2
                     * (A + iterations) ** ALPHA for n in names}
        orig_start = meta.get("start_theta") or _first_history_start(history_csv,
                                                                     names)
        # Warm-start theta from the newest checkpoint.
        cp_vals = load_params_json(newest_cp).get(args.profile, {})
        missing = [n for n in names if n not in cp_vals]
        if missing:
            sys.exit(f"--resume: checkpoint {newest_cp.name} is missing params "
                     f"({', '.join(missing)}); profile/--params mismatch?")
        theta = {n: float(cp_vals[n]) for n in names}
        start_iter = t_done + 1
        if start_iter > iterations:
            sys.exit(f"--resume: newest checkpoint is iter {t_done} but total "
                     f"iterations is {iterations} — nothing left to run. Pass "
                     f"the same --config/--games the original run used.")
        hist_mode = "a"
        write_header = not history_csv.exists()
    else:
        ts = datetime.now().strftime("%Y-%m-%d_%H-%M-%S")
        run_dir = RESULTS_DIR / f"{ts}_{args.profile}_{sprt.get_version_tag()}"
        run_dir.mkdir(parents=True, exist_ok=True)
        checkpoint_dir = run_dir / "checkpoint"
        checkpoint_dir.mkdir(parents=True, exist_ok=True)
        history_csv = run_dir / "history.csv"
        start_iter = 1
        hist_mode = "w"
        write_header = True
        orig_start = dict(theta)
        # Persist run metadata so a later --resume can pin the schedule exactly.
        (run_dir / "meta.json").write_text(json.dumps({
            "profile": args.profile,
            "params": names,
            "games": args.games,
            "games_per_iter": args.games_per_iter,
            "iterations": iterations,
            "seed": args.seed,
            "start_theta": {n: float(theta[n]) for n in names},
            "schedule": {n: {"c_end": tuned[n]["c_end"],
                             "r_end": tuned[n]["r_end"]} for n in names},
        }, indent=2) + "\n")

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
    if args.resume:
        print(f"  RESUMING {run_dir.name}")
        print(f"  from checkpoint {newest_cp.name} — iter {start_iter}/{iterations}"
              f" ({iterations - start_iter + 1} left)")
    else:
        print(f"  {args.games} games / {args.games_per_iter} per iter "
              f"= {iterations} iterations")
    print(f"  Time controls (cycled): {tc_desc}")
    print(f"  Params: {', '.join(names)}")
    print(f"  Checkpoints -> {checkpoint_dir}/ (best -> best.json; {args.out} read-only)")
    print(f"{'=' * 68}\n")

    rng = random.Random(args.seed)
    # Advance the RNG past already-completed iterations so the perturbation-flip
    # stream continues exactly as if the run never stopped (identical when the
    # same --seed is used; a harmless no-op otherwise).
    for _ in range((start_iter - 1) * len(names)):
        rng.choice((-1, 1))
    with open(history_csv, hist_mode, newline="") as hf:
        writer = csv.writer(hf)
        if write_header:
            writer.writerow(["iter", "games", "result"] + names)

        # Count prior games so the running total and summary stay cumulative.
        games_done = (start_iter - 1) * args.games_per_iter
        try:
            for t in range(start_iter, iterations + 1):
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
    # True pre-run start: recorded start_theta on resume, else the pre-loop
    # params.json values for a fresh run.
    start_vals = {n: float((orig_start or {}).get(n, profile_vals.get(n, opts[n][0])))
                  for n in names}
    print(f"\n{'=' * 68}")
    print(f"  Done. Tuned {len(names)} params over {games_done} games.")
    print(f"  Best values written to {best_json} [{args.profile}]:")
    for n in names:
        print(f"    {n:<26} {int(round(start_vals[n])):>6} -> "
              f"{int(round(theta[n])):>6}")
    print(f"  History:     {history_csv}")
    print(f"  Checkpoints: {checkpoint_dir}/")

    # Auto-emit the start->end shift plot (best-effort: never fail a completed
    # run because plotting is unavailable). Uses the true pre-run start values.
    if games_done > 0:
        try:
            end_vals = {n: float(theta[n]) for n in names}
            subtitle = (f"{run_dir.name}   ·   profile '{args.profile}', "
                        f"{games_done} games   ·   start → end")
            out = render_shift_plot(names, start_vals, end_vals,
                                    run_dir / "shift.png", "SPSA parameter shift",
                                    subtitle)
            print(f"  Shift plot:  {out}")
        except SystemExit as e:  # matplotlib missing — note it, don't abort.
            print(f"  Shift plot:  skipped ({e})")
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

# Diverging shift palette (from the data-viz reference palette): blue for an
# increase, red for a decrease, gray for the start anchor / no-change. Warm/cool
# poles that read as opposite; neutral gray reads as "nothing moved".
_SHIFT_UP = "#2a78d6"     # blue  — parameter increased over the run
_SHIFT_DOWN = "#d03b3b"   # red   — parameter decreased over the run
_SHIFT_FLAT = "#b7b6b0"   # gray  — start anchor and unchanged params
_INK = "#0b0b0b"
_INK_MUTED = "#52514e"
_SURFACE = "#fcfcfb"


def _import_pyplot():
    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
        return plt
    except ImportError:
        sys.exit("matplotlib not installed — pip3 install matplotlib "
                 "(or run with the repo .venv)")


def read_history(path):
    """Load a run's history.csv. Returns (names, rows) where names is the tuned
    parameter columns (in file order) and rows is the list of dict rows."""
    history = Path(path)
    if not history.exists():
        sys.exit(f"History file not found: {history}")
    with open(history) as f:
        rows = list(csv.DictReader(f))
    if not rows:
        sys.exit("History is empty.")
    names = [c for c in rows[0] if c not in ("iter", "games", "result")]
    return names, rows


def render_shift_plot(names, start_vals, end_vals, out, title, subtitle=None):
    """Dumbbell / election-shift plot: one row per parameter, a start dot and an
    end dot joined by a bar, positioned on a shared percent-change axis so knobs
    of wildly different magnitude stay comparable. Rows are sorted by percent
    change; blue = increased, red = decreased, gray = unchanged. Absolute
    start->end values and the signed percent are labelled directly on each row."""
    plt = _import_pyplot()

    def pct(name):
        s, e = start_vals[name], end_vals[name]
        # Guard a zero (or tiny) start: these are integer UCI units, so anchor
        # the denominator at 1 to keep small-magnitude knobs from exploding.
        return (e - s) / max(1.0, abs(s)) * 100.0

    order = sorted(names, key=pct)  # most-negative at bottom, most-positive on top
    pcts = [pct(n) for n in order]
    ys = list(range(len(order)))

    span = max((abs(p) for p in pcts), default=1.0) or 1.0
    # Headroom on the right for the value labels, on the left symmetrically.
    xmax = span * 1.35 + 1.0
    xmin = -(span * 1.35 + 1.0)

    fig_h = max(3.0, 0.34 * len(order) + 1.7)
    fig, ax = plt.subplots(figsize=(11, fig_h))
    fig.patch.set_facecolor(_SURFACE)
    ax.set_facecolor(_SURFACE)

    ax.axvline(0.0, color=_SHIFT_FLAT, linewidth=1.4, zorder=1)

    for y, n, p in zip(ys, order, pcts):
        s, e = start_vals[n], end_vals[n]
        moved = abs(e - s) > 1e-9
        color = _SHIFT_UP if p > 0 else _SHIFT_DOWN if p < 0 else _SHIFT_FLAT
        # Connecting bar (start anchor at 0% -> end at the percent change).
        if moved:
            ax.plot([0.0, p], [y, y], color=color, linewidth=3.0,
                    solid_capstyle="round", zorder=2, alpha=0.55)
        # Start anchor dot at 0%, end dot at the percent change.
        ax.scatter([0.0], [y], s=42, color=_SHIFT_FLAT, edgecolors=_SURFACE,
                   linewidths=1.4, zorder=3)
        ax.scatter([p], [y], s=70, color=color, edgecolors=_SURFACE,
                   linewidths=1.4, zorder=4)
        # Direct label: absolute start->end and the signed percent, placed on
        # the far side of the end dot so it never sits under the bar.
        si, ei = int(round(s)), int(round(e))
        if moved:
            label = f"{si} → {ei}  ({p:+.1f}%)"
        else:
            label = f"{si}  (—)"
        pad = span * 0.03 + 0.4
        if p >= 0:
            ax.text(p + pad, y, label, va="center", ha="left",
                    fontsize=8, color=_INK_MUTED)
        else:
            ax.text(p - pad, y, label, va="center", ha="right",
                    fontsize=8, color=_INK_MUTED)

    ax.set_yticks(ys)
    ax.set_yticklabels(order, fontsize=9, color=_INK)
    ax.set_ylim(-0.7, len(order) - 0.3)
    ax.set_xlim(xmin, xmax)
    ax.set_xlabel("Change from start (%)", fontsize=10, color=_INK_MUTED)

    ax.set_title(title, fontsize=13, color=_INK, fontweight="bold", loc="left",
                 pad=26 if subtitle else 10)
    if subtitle:
        ax.text(0.0, 1.01, subtitle, transform=ax.transAxes, fontsize=9,
                color=_INK_MUTED, ha="left", va="bottom")

    ax.grid(True, axis="x", alpha=0.18, zorder=0)
    for spine in ("top", "right", "left"):
        ax.spines[spine].set_visible(False)
    ax.spines["bottom"].set_color(_SHIFT_FLAT)
    ax.tick_params(length=0)

    # Legend by direct swatches (identity is not carried by color alone: each row
    # is also value-labelled with its signed percent).
    from matplotlib.lines import Line2D
    handles = [
        Line2D([0], [0], marker="o", color="none", markerfacecolor=_SHIFT_UP,
               markeredgecolor=_SURFACE, markersize=8, label="increased"),
        Line2D([0], [0], marker="o", color="none", markerfacecolor=_SHIFT_DOWN,
               markeredgecolor=_SURFACE, markersize=8, label="decreased"),
        Line2D([0], [0], marker="o", color="none", markerfacecolor=_SHIFT_FLAT,
               markeredgecolor=_SURFACE, markersize=8, label="start / unchanged"),
    ]
    ax.legend(handles=handles, loc="lower right", fontsize=8, frameon=False)

    fig.tight_layout()
    fig.savefig(out, dpi=150, facecolor=_SURFACE)
    plt.close(fig)
    return out


def _shift_meta(history_path, rows):
    """Build a (title, subtitle) pair for the shift plot from the run dir name
    and the history rows."""
    run_name = Path(history_path).resolve().parent.name
    total_games = sum(int(r["games"]) for r in rows)
    iters = len(rows)
    subtitle = (f"{run_name}   ·   {iters} iterations, {total_games} games   "
                f"·   start (first iter) → end (last iter)")
    return "SPSA parameter shift", subtitle


def cmd_plot(args):
    names, rows = read_history(args.history)

    start_vals = {n: float(rows[0][n]) for n in names}
    end_vals = {n: float(rows[-1][n]) for n in names}
    title, subtitle = _shift_meta(args.history, rows)
    out = Path(args.history).parent / "shift.png"
    render_shift_plot(names, start_vals, end_vals, out, title, subtitle)
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
    run_p.add_argument("--resume", default=None,
                       help="Resume a prior run: path to its run dir (or its "
                            "checkpoint/ dir). Continues from the newest "
                            "checkpoint at the correct iteration/gain schedule, "
                            "appending to the same history.csv and checkpoints. "
                            "Pass the same --config/--games as the original so "
                            "the total iteration budget matches.")
    run_p.add_argument("--games", type=int, default=None,
                       help="Total game budget (default: 20000, or config 'run.games')")
    run_p.add_argument("--games-per-iter", type=int, default=None, dest="games_per_iter",
                       help="Games per SPSA iteration; 2 = one color-balanced pair "
                            "(default: 2, or config 'run.games_per_iter'). Also caps "
                            "effective --concurrency.")
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
    run_p.add_argument("--concurrency", type=int, default=None,
                       help="Concurrent games (default: 1, or config 'run.concurrency'). "
                            "Effective concurrency is min(this, games_per_iter); each "
                            "game uses 2 cores.")
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

    plot_p = sub.add_parser("plot", help="Plot a run's parameter changes")
    plot_p.add_argument("history", help="Path to a run's history.csv")

    args = parser.parse_args()
    if args.command == "run":
        apply_config_run_defaults(args)
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
