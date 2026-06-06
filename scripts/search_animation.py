#!/usr/bin/env python3
"""
Generate a side-by-side animated GIF: chessboard with move arrows + search tree.
Reads real search trace data from OmegaZero's trace harness.

Usage:
    make trace
    ./build/trace_harness "FEN" 1.0 > /tmp/trace.json
    python3 scripts/search_animation.py --trace /tmp/trace.json

    # Or use defaults (Deep Blue vs Kasparov 1997, Game 6):
    python3 scripts/search_animation.py

Dependencies:
    pip install graphviz pillow python-chess cairosvg
    brew install graphviz cairo
"""

import argparse
import json
import os
import subprocess
import tempfile
from io import BytesIO
from pathlib import Path

import cairosvg
import chess
import chess.svg
import graphviz
from PIL import Image, ImageDraw, ImageFont

# =============================================================================
# Default position (Deep Blue vs Kasparov 1997, Game 6, after 7...h6)
# =============================================================================

DEFAULT_FEN = "r1bqkb1r/pppn1pp1/2p1pn1p/6N1/3PN3/3B1N2/PPP2PPP/R1BQK2R w KQkq - 0 8"

# =============================================================================
# Layout
# =============================================================================

BOARD_SIZE = 496
GAP = 28
TREE_AREA_W = 660
CANVAS_W = BOARD_SIZE + GAP + TREE_AREA_W
CAPTION_H = 44
FOOTER_H = 40
CANVAS_H = 600 + FOOTER_H

CLR_ARROW_EXPLORE = "#A8D8EA88"
CLR_ARROW_PRUNE = "#F4A7A088"
CLR_ARROW_PV = "#7EC8A4aa"

CLR_DIM = "#e8e8e8"
CLR_EXPLORED = "#7EC8A4"
CLR_PRUNED = "#F4A7A0"
CLR_PV = "#A8D8EA"
CLR_BEST = "#7EC8A4"
CLR_EDGE_DIM = "#cccccc"
CLR_TEXT_DIM = "#bbbbbb"
CLR_TEXT_BRIGHT = "#1a1a1a"
CLR_CAPTION = "#333333"

WHITE_BG = True

# =============================================================================
# Fonts
# =============================================================================

def _load_font(size, bold=False):
    paths = [
        "/System/Library/Fonts/Supplemental/Arial Bold.ttf" if bold
        else "/System/Library/Fonts/Supplemental/Arial Unicode.ttf",
        "/System/Library/Fonts/Helvetica.ttc",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf" if bold
        else "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    ]
    for p in paths:
        try:
            return ImageFont.truetype(p, size)
        except (IOError, OSError):
            continue
    try:
        return ImageFont.load_default(size=size)
    except TypeError:
        return ImageFont.load_default()

FONT_CAPTION = _load_font(16, bold=True)
FONT_LEGEND = _load_font(12)
FONT_NOTE = _load_font(11)

# =============================================================================
# Trace loading and tree selection
# =============================================================================

def load_trace(trace_path):
    with open(trace_path) as f:
        return json.load(f)


def run_trace_harness(fen, search_time, repo_root):
    harness = repo_root / "build" / "trace_harness"
    if not harness.exists():
        print("Building trace harness...")
        subprocess.run(["make", "trace"], cwd=repo_root, check=True,
                       capture_output=True)
    result = subprocess.run(
        [str(harness), fen, str(search_time)],
        capture_output=True, text=True, cwd=repo_root)
    if result.returncode != 0:
        raise RuntimeError(f"Trace harness failed: {result.stderr}")
    return json.loads(result.stdout)


def build_display_tree(trace, max_root=4, max_sub=3, max_depth=3):
    """Select interesting nodes from trace for visualization."""
    fen = trace["fen"]
    board = chess.Board(fen)
    root = trace["tree"]

    display = {
        "id": "root", "label": "White to move", "children": [],
        "score": None, "is_pv": False, "pruned": False, "prune_reason": "",
        "uci": "",
    }

    children = root.get("children", [])
    if not children:
        return display

    pv = [c for c in children if c.get("is_pv")]
    searched = [c for c in children
                if not c.get("is_pv") and not c.get("pruned")
                and c.get("children")]
    pruned_only = [c for c in children if c.get("pruned")]
    leafy = [c for c in children
             if not c.get("is_pv") and not c.get("pruned")
             and not c.get("children")]

    selected = list(pv[:1])
    searched.sort(key=lambda c: len(c.get("children", [])), reverse=True)
    for c in searched:
        if len(selected) >= max_root - 2:
            break
        if c not in selected:
            selected.append(c)
    for c in pruned_only[:2]:
        selected.append(c)
    if len(selected) < max_root:
        for c in leafy[:max_root - len(selected)]:
            selected.append(c)

    for child in selected:
        display["children"].append(
            _build_node(child, board, depth=1, max_sub=max_sub,
                        max_depth=max_depth, parent_path="root"))

    return display


def _build_node(trace_node, board_before_move, depth, max_sub, max_depth,
                parent_path):
    uci = trace_node["move"]
    move = chess.Move.from_uci(uci)
    san = board_before_move.san(move)
    node_id = f"{parent_path}/{uci}"

    raw_eval = trace_node.get("eval", 0)
    if depth % 2 == 0:
        raw_eval = -raw_eval
    score = raw_eval / 100.0 if not trace_node.get("pruned") else None

    result = {
        "id": node_id,
        "label": san,
        "uci": uci,
        "score": score,
        "is_pv": trace_node.get("is_pv", False),
        "pruned": trace_node.get("pruned", False),
        "prune_reason": trace_node.get("prune_reason", ""),
        "children": [],
    }

    if depth >= max_depth or not trace_node.get("children"):
        return result

    board_after = board_before_move.copy()
    board_after.push(move)

    sub_children = trace_node["children"]
    pv_sub = [c for c in sub_children if c.get("is_pv")]
    searched_sub = [c for c in sub_children
                    if not c.get("is_pv") and not c.get("pruned")]
    pruned_sub = [c for c in sub_children if c.get("pruned")]

    if depth == 0:
        sub_selected = pv_sub[:1] + searched_sub[:1] + pruned_sub[:2]
    else:
        sub_selected = pv_sub[:1] + pruned_sub[:2]
    sub_selected = sub_selected[:max_sub]

    for sc in sub_selected:
        result["children"].append(
            _build_node(sc, board_after, depth + 1, max_sub, max_depth,
                        node_id))

    return result


def collect_pv_path(tree):
    path = {tree["id"]}
    node = tree
    while node.get("children"):
        pv_child = next((c for c in node["children"] if c.get("is_pv")), None)
        if not pv_child:
            break
        path.add(pv_child["id"])
        node = pv_child
    return path


def get_pv_line(trace, fen):
    board = chess.Board(fen)
    node = trace["tree"]
    moves = []
    while node.get("children"):
        pv = next((c for c in node["children"] if c.get("is_pv")), None)
        if not pv:
            break
        move = chess.Move.from_uci(pv["move"])
        moves.append(board.san(move))
        board.push(move)
        node = pv
    return " ".join(moves)


def generate_steps(tree, pv_line, best_score):
    """Auto-generate animation steps from display tree."""
    steps = []
    steps.append(("start", "root", "OmegaZero — White to move", 4))

    pv_move = next((c for c in tree["children"] if c["is_pv"]), None)
    if not pv_move:
        pv_move = tree["children"][0] if tree["children"] else None
    if not pv_move:
        return steps

    steps.append(("explore", pv_move["id"],
                  f"Considering {pv_move['label']}...", 2))

    for child in pv_move.get("children", []):
        if child["is_pv"] or not child["pruned"]:
            steps.append(("explore", child["id"],
                          f"If {child['label']}...", 1))
            for gc in child.get("children", []):
                if gc["pruned"]:
                    steps.append(("prune", gc["id"],
                                  f"{gc['label']} pruned ({gc['prune_reason']})",
                                  1))
                else:
                    eval_str = f"{gc['score']:+.2f}" if gc["score"] is not None else "?"
                    steps.append(("score", gc["id"],
                                  f"{gc['label']} — eval {eval_str}", 2))
            if child["score"] is not None:
                steps.append(("score", child["id"],
                              f"{child['label']}: {child['score']:+.2f}", 1))
        elif child["pruned"]:
            steps.append(("prune", child["id"],
                          f"{child['label']} pruned ({child['prune_reason']})",
                          1))

    if pv_move["score"] is not None:
        steps.append(("score", pv_move["id"],
                      f"{pv_move['label']} evaluates to {pv_move['score']:+.2f}",
                      2))

    for alt in tree["children"][1:]:
        if alt["pruned"]:
            steps.append(("prune", alt["id"],
                          f"{alt['label']} pruned ({alt['prune_reason']})", 2))
        else:
            steps.append(("explore", alt["id"],
                          f"Considering {alt['label']}...", 1))
            for child in alt.get("children", [])[:2]:
                if child["pruned"]:
                    steps.append(("prune", child["id"],
                                  f"{child['label']} pruned ({child['prune_reason']})",
                                  1))
                elif child["score"] is not None:
                    steps.append(("score", child["id"],
                                  f"{child['label']}: {child['score']:+.2f}",
                                  1))
            if alt["score"] is not None:
                cmp = "weaker" if alt["score"] < pv_move["score"] else "equal"
                steps.append(("score", alt["id"],
                              f"{alt['label']}: {alt['score']:+.2f} — {cmp}",
                              1))

    steps.append(("pv", None, f"PV: {pv_line}", 4))
    eval_str = f"{best_score / 100.0:+.2f}"
    steps.append(("best", pv_move["id"],
                  f"Best move: {pv_move['label']}  [{eval_str}]", 6))

    return steps

# =============================================================================
# Board rendering
# =============================================================================

def draw_board(board_obj, arrows_data=None):
    svg_arrows = []
    if arrows_data:
        for from_sq, to_sq, color in arrows_data:
            svg_arrows.append(chess.svg.Arrow(from_sq, to_sq, color=color))

    svg = chess.svg.board(
        board_obj,
        arrows=svg_arrows,
        size=BOARD_SIZE,
        coordinates=True,
        colors={"margin": "#00000000", "coord": "#888888"},
    )

    png = cairosvg.svg2png(bytestring=svg.encode(),
                           output_width=BOARD_SIZE, output_height=BOARD_SIZE)
    return Image.open(BytesIO(png)).convert("RGBA")

# =============================================================================
# Tree rendering
# =============================================================================

def _find_node(tree, node_id):
    if tree["id"] == node_id:
        return tree
    for c in tree.get("children", []):
        r = _find_node(c, node_id)
        if r:
            return r
    return None


def _contains(node, target_id):
    if node["id"] == target_id:
        return True
    return any(_contains(c, target_id) for c in node.get("children", []))


def _find_root_move(tree, node_id):
    for child in tree["children"]:
        if _contains(child, node_id):
            return child
    return None


def render_tree(tree, explored, pruned, scored, prune_labels, pv_path,
                pv_mode, best_id):
    dot = graphviz.Digraph(format="png")
    dot.attr(bgcolor="white" if WHITE_BG else "transparent",
             pad="0.4", nodesep="0.3", ranksep="0.5")
    dot.attr("node", fontname="Helvetica", fontsize="11", style="filled",
             width="0.75", height="0.4")
    dot.attr("edge", arrowsize="0.6")

    def add(node, parent_id=None):
        nid = node["id"]
        label = node["label"]

        if nid == best_id:
            fill, fc, pw = CLR_BEST, "#1a1a1a", "3"
            shape = "box"
            if node.get("score") is not None:
                label = f"{label}\n✓ [{node['score']:+.2f}]"
        elif pv_mode and nid in pv_path:
            fill, fc, pw = CLR_PV, "#1a1a1a", "2.5"
            shape = "box" if nid == "root" else "ellipse"
            if node.get("score") is not None and nid != "root":
                label = f"{label}\n[{node['score']:+.2f}]"
        elif nid in pruned:
            fill, fc, pw = CLR_PRUNED, "#1a1a1a", "1"
            shape = "ellipse"
            reason = prune_labels.get(nid, "pruned")
            label = f"{label}\n✂ {reason}"
        elif nid in scored:
            fill, fc, pw = CLR_EXPLORED, "#1a1a1a", "1.5"
            shape = "ellipse"
            if node.get("score") is not None:
                label = f"{label}\n[{node['score']:+.2f}]"
        elif nid in explored:
            fill, fc, pw = CLR_EXPLORED, "#1a1a1a", "1"
            shape = "ellipse"
        else:
            fill, fc, pw = CLR_DIM, CLR_TEXT_DIM, "1"
            shape = "ellipse"

        if nid == "root":
            shape = "box"

        dot.node(nid, label, fillcolor=fill, fontcolor=fc,
                 penwidth=pw, shape=shape, color=fill)

        if parent_id:
            if pv_mode and parent_id in pv_path and nid in pv_path:
                dot.edge(parent_id, nid, color=CLR_PV, penwidth="2.5")
            elif nid in pruned:
                dot.edge(parent_id, nid, color=CLR_PRUNED,
                         style="dashed", penwidth="1.5")
            elif nid in explored or nid in scored:
                dot.edge(parent_id, nid, color=CLR_EXPLORED, penwidth="1.5")
            else:
                dot.edge(parent_id, nid, color=CLR_EDGE_DIM, style="dashed")

        for child in node.get("children", []):
            add(child, nid)

    add(tree)
    return dot

# =============================================================================
# Frame composition
# =============================================================================

def _paste_at(src, x, y, w, h):
    layer = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    layer.paste(src, (x, y))
    return layer


def compose_frame(board_img, tree_img, caption):
    canvas = Image.new("RGBA", (CANVAS_W, CANVAS_H),
                       (255, 255, 255, 255) if WHITE_BG else (0, 0, 0, 0))
    draw = ImageDraw.Draw(canvas)

    bbox = FONT_CAPTION.getbbox(caption)
    tw = bbox[2] - bbox[0]
    draw.text(((CANVAS_W - tw) / 2 - bbox[0], 14),
              caption, fill=CLR_CAPTION, font=FONT_CAPTION)

    content_h = CANVAS_H - FOOTER_H
    board_y = CAPTION_H + (content_h - CAPTION_H - board_img.height) // 2
    canvas = Image.alpha_composite(
        canvas,
        _paste_at(board_img, 12, max(CAPTION_H, board_y), CANVAS_W, CANVAS_H))

    max_w = TREE_AREA_W - 16
    max_h = content_h - CAPTION_H - 20
    tree_img.thumbnail((max_w, max_h), Image.LANCZOS)
    tree_x = BOARD_SIZE + GAP
    tree_y = CAPTION_H + (content_h - CAPTION_H - tree_img.height) // 2

    tree_rgba = tree_img.convert("RGBA") if tree_img.mode != "RGBA" else tree_img
    canvas = Image.alpha_composite(
        canvas,
        _paste_at(tree_rgba, tree_x, tree_y, CANVAS_W, CANVAS_H))

    draw = ImageDraw.Draw(canvas)
    footer_y = CANVAS_H - FOOTER_H + 8

    legend_items = [
        (CLR_PV, "PV / Best line"),
        (CLR_EXPLORED, "Explored"),
        (CLR_PRUNED, "Pruned"),
    ]
    lx = tree_x + 8
    for color, text in legend_items:
        draw.rounded_rectangle([lx, footer_y, lx + 10, footer_y + 10],
                               radius=2, fill=color)
        draw.text((lx + 14, footer_y - 2), text,
                  fill=CLR_CAPTION, font=FONT_LEGEND)
        tw = FONT_LEGEND.getbbox(text)[2] + 20
        lx += tw

    note = "Deep Blue vs Kasparov, 1997 Game 6, after 7…h6"
    draw.text((14, footer_y - 1), note, fill="#666666", font=FONT_NOTE)

    return canvas

# =============================================================================
# Animation
# =============================================================================

def generate_animation(trace, output_path, delay=600):
    fen = trace["fen"]
    board_obj = chess.Board(fen)
    display_tree = build_display_tree(trace)
    pv_path = collect_pv_path(display_tree)
    pv_line = get_pv_line(trace, fen)
    steps = generate_steps(display_tree, pv_line, trace["score"])

    explored = {"root"}
    pruned_set = set()
    scored_set = set()
    prune_labels = {}

    frames = []

    with tempfile.TemporaryDirectory() as tmpdir:
        for action, node_id, caption, hold in steps:
            if action == "explore":
                explored.add(node_id)
            elif action == "prune":
                pruned_set.add(node_id)
                node = _find_node(display_tree, node_id)
                if node:
                    prune_labels[node_id] = node.get("prune_reason", "pruned")
            elif action == "score":
                explored.add(node_id)
                scored_set.add(node_id)

            arrows = []
            if action == "start":
                pass
            elif action in ("pv", "best"):
                pv_root = next(
                    (c for c in display_tree["children"] if c["is_pv"]),
                    display_tree["children"][0] if display_tree["children"]
                    else None)
                if pv_root:
                    move = chess.Move.from_uci(pv_root["uci"])
                    arrows = [(move.from_square, move.to_square, CLR_ARROW_PV)]
            elif node_id:
                root_move = _find_root_move(display_tree, node_id)
                if root_move and root_move.get("uci"):
                    move = chess.Move.from_uci(root_move["uci"])
                    if action == "prune" and root_move["id"] == node_id:
                        color = CLR_ARROW_PRUNE
                    else:
                        color = CLR_ARROW_EXPLORE
                    arrows = [(move.from_square, move.to_square, color)]

            board_img = draw_board(board_obj, arrows)

            pv_mode = action in ("pv", "best")
            best_node_id = node_id if action == "best" else None
            tree_dot = render_tree(display_tree, explored, pruned_set,
                                   scored_set, prune_labels, pv_path,
                                   pv_mode, best_node_id)
            tree_path = os.path.join(tmpdir, f"tree_{len(frames):04d}")
            tree_dot.render(tree_path, cleanup=True)
            tree_img = Image.open(f"{tree_path}.png")

            frame = compose_frame(board_img, tree_img, caption)
            for _ in range(hold):
                frames.append(frame.copy())

        os.makedirs(os.path.dirname(output_path) or ".", exist_ok=True)

        gif_frames = []
        for frame in frames:
            rgb = Image.new("RGB", frame.size, (255, 255, 255))
            rgb.paste(frame, mask=frame)
            p = rgb.quantize(colors=256, method=Image.Quantize.MEDIANCUT)
            gif_frames.append(p)

        gif_frames[0].save(
            output_path,
            save_all=True,
            append_images=gif_frames[1:],
            duration=delay,
            loop=0,
            optimize=False,
            disposal=2,
        )

    print(f"Saved {len(frames)} frames -> {output_path}")


def main():
    parser = argparse.ArgumentParser(
        description="Generate search visualization GIF from OmegaZero trace")
    parser.add_argument("--trace", "-t",
                        help="Path to trace JSON (default: run harness)")
    parser.add_argument("--fen", "-f", default=DEFAULT_FEN,
                        help="FEN position (used if no --trace)")
    parser.add_argument("--time", type=float, default=1.0,
                        help="Search time in seconds (used if no --trace)")
    parser.add_argument("--output", "-o",
                        default="figs/search_animation.gif",
                        help="Output GIF path")
    parser.add_argument("--delay", "-d", type=int, default=600,
                        help="Frame delay in ms (default: 600)")
    args = parser.parse_args()

    repo_root = Path(__file__).resolve().parent.parent

    if args.trace:
        trace = load_trace(args.trace)
    else:
        trace = run_trace_harness(args.fen, args.time, repo_root)

    output = repo_root / args.output
    generate_animation(trace, str(output), delay=args.delay)


if __name__ == "__main__":
    main()
