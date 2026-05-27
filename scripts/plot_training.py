#!/usr/bin/env python3
"""
Generate analysis plots for NNUE training data and trained models.

Subcommands:
    data    — Analyze raw training data (score distributions, game results, diversity)
    model   — Evaluate a trained model on validation data (accuracy, calibration, error)

Usage:
    python3 scripts/plot_training.py data --input nnue/data/<run>/training_data.txt
    python3 scripts/plot_training.py model --checkpoint nnue/model/<run>/best.pt \
                                           --val-data nnue/data/<run>/validation_data.txt
"""

import argparse
import json
import subprocess
import sys
from datetime import datetime
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np


SCORE_SCALE = 400.0


def get_git_hash():
    try:
        return subprocess.check_output(
            ["git", "rev-parse", "--short", "HEAD"],
            stderr=subprocess.DEVNULL
        ).decode().strip()
    except (subprocess.CalledProcessError, FileNotFoundError):
        return "unknown"


def get_run_tag():
    ts = datetime.now().strftime("%Y-%m-%d_%H-%M-%S")
    return f"{ts}_{get_git_hash()}"


def get_metadata():
    """Return a dict with timestamp and git commit hash."""
    return {
        "timestamp": datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
        "git_commit": get_git_hash(),
    }


def resolve_latest_data(base_dir):
    """Find the latest timestamped subdir under base_dir and return training_data.txt path."""
    base = Path(base_dir)
    if not base.is_dir():
        return base_dir
    subdirs = sorted(
        [d for d in base.iterdir() if d.is_dir()],
        key=lambda d: d.name,
        reverse=True,
    )
    if subdirs:
        candidate = subdirs[0] / "training_data.txt"
        if candidate.exists():
            return str(candidate)
    fallback = base / "training_data.txt"
    if fallback.exists():
        return str(fallback)
    return base_dir


def save_plot_metadata(out_dir, command, extra=None):
    """Write metadata JSON alongside plots."""
    meta = get_metadata()
    meta["command"] = command
    if extra:
        meta.update(extra)
    path = Path(out_dir) / "plot_metadata.json"
    with open(path, "w") as f:
        json.dump(meta, f, indent=2)
    print(f"  Metadata: {path}")


def load_data(path):
    """Load training data file. Returns (scores, results, fens)."""
    scores = []
    results = []
    fens = []

    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            parts = line.split(" | ")
            if len(parts) != 3:
                continue
            fens.append(parts[0].strip())
            scores.append(float(parts[1].strip()))
            results.append(float(parts[2].strip()))

    return np.array(scores), np.array(results), fens


def plot_data(args):
    """Analyze and plot raw training data."""
    input_path = args.input
    if Path(input_path).is_dir():
        input_path = resolve_latest_data(input_path)
        print(f"Resolved data: {input_path}")

    scores, results, fens = load_data(input_path)
    n = len(scores)

    if n == 0:
        sys.exit("No data found.")

    out_dir = Path(args.output) / get_run_tag()
    out_dir.mkdir(parents=True, exist_ok=True)

    print(f"Loaded {n:,} positions from {input_path}")
    print(f"Score range: [{scores.min():.0f}, {scores.max():.0f}] cp")
    print(f"Results: W={np.sum(results == 1.0):.0f}  D={np.sum(results == 0.5):.0f}  L={np.sum(results == 0.0):.0f}")

    # 1. Score distribution histogram
    fig, ax = plt.subplots(figsize=(10, 6))
    clip = 2000
    clipped = np.clip(scores, -clip, clip)
    ax.hist(clipped, bins=100, color="steelblue", edgecolor="none", alpha=0.8)
    ax.axvline(0, color="red", linestyle="--", alpha=0.5)
    ax.set_xlabel("Score (cp)")
    ax.set_ylabel("Count")
    ax.set_title(f"Score Distribution ({n:,} positions)")
    ax.grid(True, alpha=0.3)
    fig.savefig(out_dir / "data_score_distribution.png", dpi=150, bbox_inches="tight")
    plt.close(fig)

    # 2. Game result breakdown
    fig, ax = plt.subplots(figsize=(8, 6))
    w_count = np.sum(results == 1.0)
    d_count = np.sum(results == 0.5)
    l_count = np.sum(results == 0.0)
    bars = ax.bar(["White Win", "Draw", "Black Win"],
                  [w_count, d_count, l_count],
                  color=["#4CAF50", "#9E9E9E", "#F44336"])
    for bar, count in zip(bars, [w_count, d_count, l_count]):
        ax.text(bar.get_x() + bar.get_width() / 2, bar.get_height() + n * 0.01,
                f"{count:.0f}\n({100*count/n:.1f}%)", ha="center", va="bottom")
    ax.set_ylabel("Position Count")
    ax.set_title("Positions by Game Result")
    ax.grid(True, alpha=0.3, axis="y")
    fig.savefig(out_dir / "data_result_breakdown.png", dpi=150, bbox_inches="tight")
    plt.close(fig)

    # 3. Score vs result: do winning positions have positive scores?
    fig, ax = plt.subplots(figsize=(10, 6))
    for result_val, label, color in [(1.0, "White Win", "#4CAF50"),
                                      (0.5, "Draw", "#9E9E9E"),
                                      (0.0, "Black Win", "#F44336")]:
        mask = results == result_val
        if mask.sum() > 0:
            subset = np.clip(scores[mask], -clip, clip)
            ax.hist(subset, bins=80, alpha=0.5, label=label, color=color, density=True)
    ax.set_xlabel("Score (cp)")
    ax.set_ylabel("Density")
    ax.set_title("Score Distribution by Game Result")
    ax.legend()
    ax.grid(True, alpha=0.3)
    fig.savefig(out_dir / "data_score_by_result.png", dpi=150, bbox_inches="tight")
    plt.close(fig)

    # 4. Blended target distribution
    fig, ax = plt.subplots(figsize=(10, 6))
    sigmoid_scores = 1.0 / (1.0 + np.exp(-scores / SCORE_SCALE))
    lmbda = 0.7
    targets = lmbda * sigmoid_scores + (1 - lmbda) * results
    ax.hist(targets, bins=80, color="purple", edgecolor="none", alpha=0.7)
    ax.set_xlabel("Blended Target Value")
    ax.set_ylabel("Count")
    ax.set_title(f"Training Target Distribution (λ={lmbda})")
    ax.grid(True, alpha=0.3)
    fig.savefig(out_dir / "data_target_distribution.png", dpi=150, bbox_inches="tight")
    plt.close(fig)

    # 5. Piece count (game phase proxy) distribution
    fig, ax = plt.subplots(figsize=(10, 6))
    piece_counts = []
    for fen in fens:
        board_str = fen.split()[0]
        count = sum(1 for c in board_str if c.isalpha() and c not in "Kk")
        piece_counts.append(count)
    piece_counts = np.array(piece_counts)
    ax.hist(piece_counts, bins=range(0, 32), color="darkorange",
            edgecolor="none", alpha=0.8)
    ax.set_xlabel("Non-King Piece Count")
    ax.set_ylabel("Count")
    ax.set_title("Game Phase Distribution (piece count proxy)")
    ax.grid(True, alpha=0.3)
    fig.savefig(out_dir / "data_phase_distribution.png", dpi=150, bbox_inches="tight")
    plt.close(fig)

    # 6. Score stability: consecutive score differences (within result groups)
    fig, ax = plt.subplots(figsize=(10, 6))
    diffs = np.abs(np.diff(scores))
    diffs_clipped = np.clip(diffs, 0, 500)
    ax.hist(diffs_clipped, bins=100, color="teal", edgecolor="none", alpha=0.8)
    ax.set_xlabel("Consecutive Score Difference (cp)")
    ax.set_ylabel("Count")
    ax.set_title("Position-to-Position Score Stability")
    ax.grid(True, alpha=0.3)
    fig.savefig(out_dir / "data_score_stability.png", dpi=150, bbox_inches="tight")
    plt.close(fig)

    save_plot_metadata(out_dir, "data", {
        "input": args.input,
        "positions": int(n),
        "score_range": [float(scores.min()), float(scores.max())],
    })

    print(f"\nPlots saved to: {out_dir}/")
    print("  data_score_distribution.png")
    print("  data_result_breakdown.png")
    print("  data_score_by_result.png")
    print("  data_target_distribution.png")
    print("  data_phase_distribution.png")
    print("  data_score_stability.png")


def plot_model(args):
    """Evaluate trained model and generate accuracy/calibration plots."""
    try:
        import torch
        import torch.nn.functional as F
        from train_nnue import NnueNetwork, NnueDataset, SCORE_SCALE, compute_target
        from torch.utils.data import DataLoader
    except ImportError as e:
        sys.exit(f"Missing dependency: {e}\nInstall with: pip3 install torch")

    device = torch.device("mps" if torch.backends.mps.is_available()
                          else "cuda" if torch.cuda.is_available()
                          else "cpu")
    print(f"Device: {device}")

    model = NnueNetwork().to(device)
    model.load_state_dict(torch.load(args.checkpoint, map_location=device, weights_only=True))
    model.eval()
    print(f"Loaded checkpoint: {args.checkpoint}")

    val_set = NnueDataset(args.val_data)
    if len(val_set) == 0:
        sys.exit("No validation data found.")

    val_loader = DataLoader(val_set, batch_size=args.batch, shuffle=False, num_workers=0)

    out_dir = Path(args.output) / get_run_tag()
    out_dir.mkdir(parents=True, exist_ok=True)

    all_predicted = []
    all_actual = []
    all_results = []
    all_material = []

    with torch.no_grad():
        for wf, bf, score, result, stm in val_loader:
            wf = wf.to(device)
            bf = bf.to(device)
            stm = stm.to(device)

            pred = model(wf, bf, stm).squeeze(1)
            pred_cp = pred * SCORE_SCALE
            all_predicted.extend(pred_cp.cpu().tolist())
            all_actual.extend(score.tolist())
            all_results.extend(result.tolist())
            material = wf.sum(dim=1)
            all_material.extend(material.tolist())

    all_predicted = np.array(all_predicted)
    all_actual = np.array(all_actual)
    all_results = np.array(all_results)
    all_material = np.array(all_material)
    n = len(all_predicted)

    print(f"Evaluated {n:,} positions")

    # 1. Score accuracy scatter
    fig, ax = plt.subplots(figsize=(8, 8))
    ax.scatter(all_actual, all_predicted, alpha=0.15, s=4)
    lo = min(all_actual.min(), all_predicted.min())
    hi = max(all_actual.max(), all_predicted.max())
    ax.plot([lo, hi], [lo, hi], "r--", linewidth=1, label="Perfect prediction")
    ax.set_xlabel("Actual Score (cp)")
    ax.set_ylabel("Predicted Score (cp)")
    ax.set_title("NNUE Score Accuracy (Validation Set)")
    ax.legend()
    ax.grid(True, alpha=0.3)
    ax.set_aspect("equal")
    fig.savefig(out_dir / "model_score_accuracy.png", dpi=150, bbox_inches="tight")
    plt.close(fig)

    # 2. Score distribution comparison
    fig, ax = plt.subplots(figsize=(10, 6))
    clip = 2000
    ax.hist(np.clip(all_actual, -clip, clip), bins=80, alpha=0.5,
            label="Actual", density=True)
    ax.hist(np.clip(all_predicted, -clip, clip), bins=80, alpha=0.5,
            label="Predicted", density=True)
    ax.set_xlabel("Score (cp)")
    ax.set_ylabel("Density")
    ax.set_title("Score Distribution: Predicted vs Actual")
    ax.legend()
    ax.grid(True, alpha=0.3)
    fig.savefig(out_dir / "model_score_distribution.png", dpi=150, bbox_inches="tight")
    plt.close(fig)

    # 3. Error by game phase
    errors = np.abs(all_predicted - all_actual)
    fig, ax = plt.subplots(figsize=(10, 6))
    ax.scatter(all_material, errors, alpha=0.1, s=4)
    bin_edges = np.linspace(all_material.min(), all_material.max(), 20)
    bin_centers = []
    bin_means = []
    for i in range(len(bin_edges) - 1):
        mask = (all_material >= bin_edges[i]) & (all_material < bin_edges[i + 1])
        if mask.sum() > 0:
            bin_centers.append((bin_edges[i] + bin_edges[i + 1]) / 2)
            bin_means.append(errors[mask].mean())
    ax.plot(bin_centers, bin_means, "r-o", linewidth=2, markersize=5,
            label="Mean error")
    ax.set_xlabel("Piece Count (active features)")
    ax.set_ylabel("Absolute Error (cp)")
    ax.set_title("Prediction Error by Game Phase")
    ax.legend()
    ax.grid(True, alpha=0.3)
    fig.savefig(out_dir / "model_error_by_phase.png", dpi=150, bbox_inches="tight")
    plt.close(fig)

    # 4. Win rate calibration
    paired = sorted(zip(all_predicted, all_results), key=lambda x: x[0])
    bucket_size = max(1, len(paired) // 15)
    bucket_scores = []
    bucket_winrates = []
    for i in range(0, len(paired), bucket_size):
        bucket = paired[i:i + bucket_size]
        if len(bucket) < 5:
            continue
        avg_score = sum(s for s, _ in bucket) / len(bucket)
        avg_result = sum(r for _, r in bucket) / len(bucket)
        bucket_scores.append(avg_score)
        bucket_winrates.append(avg_result * 100)
    fig, ax = plt.subplots(figsize=(10, 6))
    ax.plot(bucket_scores, bucket_winrates, "b-o", linewidth=2, markersize=6)
    xs = np.linspace(min(bucket_scores), max(bucket_scores), 200)
    expected = 100.0 / (1.0 + np.exp(-xs / SCORE_SCALE))
    ax.plot(xs, expected, "r--", linewidth=1, alpha=0.7, label="Expected (sigmoid)")
    ax.set_xlabel("Predicted Score (cp)")
    ax.set_ylabel("Actual Win Rate (%)")
    ax.set_title("Win Rate Calibration")
    ax.legend()
    ax.grid(True, alpha=0.3)
    fig.savefig(out_dir / "model_calibration.png", dpi=150, bbox_inches="tight")
    plt.close(fig)

    # 5. Error histogram
    fig, ax = plt.subplots(figsize=(10, 6))
    errors_clipped = np.clip(errors, 0, 500)
    ax.hist(errors_clipped, bins=100, color="steelblue", edgecolor="none", alpha=0.8)
    median_err = np.median(errors)
    mean_err = np.mean(errors)
    ax.axvline(median_err, color="red", linestyle="--",
               label=f"Median: {median_err:.0f}cp")
    ax.axvline(mean_err, color="orange", linestyle="--",
               label=f"Mean: {mean_err:.0f}cp")
    ax.set_xlabel("Absolute Error (cp)")
    ax.set_ylabel("Count")
    ax.set_title("Prediction Error Distribution")
    ax.legend()
    ax.grid(True, alpha=0.3)
    fig.savefig(out_dir / "model_error_histogram.png", dpi=150, bbox_inches="tight")
    plt.close(fig)

    # 6. Residuals by score range
    fig, ax = plt.subplots(figsize=(10, 6))
    residuals = all_predicted - all_actual
    ax.scatter(all_actual, residuals, alpha=0.1, s=4)
    ax.axhline(0, color="red", linestyle="--", alpha=0.5)
    ax.set_xlabel("Actual Score (cp)")
    ax.set_ylabel("Residual (predicted - actual)")
    ax.set_title("Residual Plot")
    ax.set_ylim(-1000, 1000)
    ax.grid(True, alpha=0.3)
    fig.savefig(out_dir / "model_residuals.png", dpi=150, bbox_inches="tight")
    plt.close(fig)

    r_squared = float(np.corrcoef(all_actual, all_predicted)[0, 1] ** 2)

    save_plot_metadata(out_dir, "model", {
        "checkpoint": args.checkpoint,
        "val_data": args.val_data,
        "positions_evaluated": int(n),
        "mean_abs_error_cp": float(mean_err),
        "median_abs_error_cp": float(median_err),
        "r_squared": r_squared,
    })

    print(f"\nModel evaluation summary:")
    print(f"  Mean absolute error: {mean_err:.1f} cp")
    print(f"  Median absolute error: {median_err:.1f} cp")
    print(f"  R² (score correlation): {r_squared:.4f}")
    print(f"\nPlots saved to: {out_dir}/")
    print("  model_score_accuracy.png")
    print("  model_score_distribution.png")
    print("  model_error_by_phase.png")
    print("  model_calibration.png")
    print("  model_error_histogram.png")
    print("  model_residuals.png")


def main():
    parser = argparse.ArgumentParser(
        description="Generate analysis plots for NNUE training data and models"
    )
    subparsers = parser.add_subparsers(dest="command")

    # data subcommand
    data_parser = subparsers.add_parser("data", help="Analyze raw training data")
    data_parser.add_argument(
        "--input", default="nnue/data",
        help="Path to training data file or directory (auto-resolves latest run)",
    )
    data_parser.add_argument(
        "--output", default="results/train_dataset_plots",
        help="Output directory for plots (default: results/train_dataset_plots)",
    )

    # model subcommand
    model_parser = subparsers.add_parser("model", help="Evaluate trained model")
    model_parser.add_argument(
        "--checkpoint", required=True,
        help="Path to model checkpoint (.pt file)",
    )
    model_parser.add_argument(
        "--val-data", required=True,
        help="Path to validation data file",
    )
    model_parser.add_argument(
        "--batch", type=int, default=4096,
        help="Batch size for evaluation (default: 4096)",
    )
    model_parser.add_argument(
        "--output", default="results/model_plots",
        help="Output directory for plots (default: results/model_plots)",
    )

    args = parser.parse_args()

    if args.command == "data":
        plot_data(args)
    elif args.command == "model":
        plot_model(args)
    else:
        parser.print_help()
        sys.exit(1)


if __name__ == "__main__":
    main()
