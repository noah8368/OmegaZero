#!/usr/bin/env python3
"""oznu.py — the OZNU fused-network container: full NNUE trunk + unc head in one file.

OZNU is the deployable artifact for unc-007: a single self-contained `.bin` that
carries BOTH the complete NNUE (all layers -> the eval `v`) and the trained MDN
uncertainty head (-> `p(u|x)`), so a C++ handle can load one file and produce both.
It is a *container*: the embedded NNUE and head sub-sections are the verbatim
OZNN / OZUH blobs, each keeping its own magic so tools can still parse them.

Layout (all little-endian):
    4 bytes   magic  "OZNU"                 (constant format tag; NOT per-run)
    int32     version (=1)
    int32     run_id_len
    bytes     run_id                        (free-form, author-chosen provenance)
    16 bytes  trunk_md5                     (raw md5 of the embedded OZNN blob)
    int64     nnue_len
    int64     head_len
    bytes     <OZNN blob>  (nnue_len)       (verbatim nnue.bin -- the full trunk)
    bytes     <OZUH blob>  (head_len)       (verbatim head best.bin)

Self-validating: on read, md5(OZNN blob) must equal BOTH the top-level `trunk_md5`
field AND the `trunk_md5` baked inside the OZUH head blob -- so a head can never be
silently paired with the wrong trunk. `run_id` distinguishes runs; `trunk_md5`
matches trunk<->head; the constant `magic` is only for format recognition -- three
separate jobs, three fields (see unc-007.md).

Because OZNU embeds the whole OZNN section, the fused file's own `run_id` plus the
trunk's `run_id` (once train_nnue.py bakes one) form a provenance chain.

Subcommands:
    pack    nnue.bin + head best.bin -> nnue_unc.bin   (the migration path for an
            already-trained OZUH head; future head runs emit OZNU from the trainer)
    inspect print an OZNU's header (magic/version/run_id/md5/section sizes)
    verify  round-trip: extract trunk+head from the OZNU and confirm the embedding
            + MDN forward match a SEPARATE load of the original nnue.bin + head on a
            set of probe FENs (the A1 definition-of-done)

Usage:
    python3 unc_research/scripts/oznu.py pack \
        --nnue nnue/nnue.bin \
        --head unc_research/experiment_results/unc_head/<run>/best.bin \
        --out  unc_research/models/nnue_unc.bin \
        --run-id "<free-form provenance>"          # defaults to the head run-dir name
    python3 unc_research/scripts/oznu.py inspect unc_research/models/nnue_unc.bin
    python3 unc_research/scripts/oznu.py verify  unc_research/models/nnue_unc.bin \
        --nnue nnue/nnue.bin --head .../best.bin
"""

import argparse
import hashlib
import struct
import sys
from pathlib import Path

import numpy as np
import torch

SCRIPTS_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPTS_DIR))
sys.path.insert(0, str(SCRIPTS_DIR.parents[1] / "scripts"))  # repo scripts/ for fen_to_halfkp
from train_unc_head import read_head_bin, load_ft, L1_SIZE  # noqa: E402
from prepare_nnue_data import fen_to_halfkp  # noqa: E402

OZNU_MAGIC = b"OZNU"
OZNU_VERSION = 1

# Probe positions for `verify` -- a startpos + a few varied middlegame/endgame FENs.
_PROBE_FENS = [
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
    "r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 2 3",
    "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
    "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
    "8/8/8/4k3/8/4K3/4P3/8 w - - 0 1",
]


def _md5_bytes(b):
    return hashlib.md5(b).hexdigest()


def _read_head_trunk_md5(head_bytes):
    """Extract the trunk_md5 baked inside an OZUH head blob (offset is fixed by the
    OZUH layout: magic + 4i header + n_hidden ints + 2 floats, then 16 md5 bytes)."""
    if head_bytes[:4] != b"OZUH":
        sys.exit("head blob: bad magic (expected OZUH)")
    _version, _in_dim, _k, n_hidden = struct.unpack("<4i", head_bytes[4:20])
    off = 20 + 4 * n_hidden + 8  # past hidden[] and (u_mean, u_std)
    return head_bytes[off:off + 16].hex()


def pack_oznu(out_path, nnue_path, head_path, run_id):
    """Fuse nnue.bin (OZNN) + head best.bin (OZUH) -> OZNU at out_path.

    Verifies the head's baked trunk_md5 == md5(nnue.bin) BEFORE writing, so a
    mismatched pair fails loudly instead of producing a poisoned fused file."""
    nnue_bytes = Path(nnue_path).read_bytes()
    head_bytes = Path(head_path).read_bytes()
    if nnue_bytes[:4] != b"OZNN":
        sys.exit(f"{nnue_path}: bad magic (expected OZNN)")

    nnue_md5 = _md5_bytes(nnue_bytes)
    baked = _read_head_trunk_md5(head_bytes)
    if baked != nnue_md5:
        sys.exit(
            f"trunk mismatch: head's baked trunk_md5 {baked} != md5(nnue.bin) "
            f"{nnue_md5}\n  the head was trained on a different trunk than {nnue_path}")

    run_id_b = run_id.encode("utf-8")
    out_path = Path(out_path)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    with open(out_path, "wb") as f:
        f.write(OZNU_MAGIC)
        f.write(struct.pack("<i", OZNU_VERSION))
        f.write(struct.pack("<i", len(run_id_b)))
        f.write(run_id_b)
        f.write(bytes.fromhex(nnue_md5))
        f.write(struct.pack("<q", len(nnue_bytes)))
        f.write(struct.pack("<q", len(head_bytes)))
        f.write(nnue_bytes)
        f.write(head_bytes)
    return out_path, nnue_md5, run_id


def read_oznu(path):
    """Parse an OZNU file; verify the md5 self-check; return its parts as a dict."""
    with open(path, "rb") as f:
        if f.read(4) != OZNU_MAGIC:
            sys.exit(f"{path}: bad magic (expected OZNU)")
        (version,) = struct.unpack("<i", f.read(4))
        (run_id_len,) = struct.unpack("<i", f.read(4))
        run_id = f.read(run_id_len).decode("utf-8")
        trunk_md5 = f.read(16).hex()
        (nnue_len,) = struct.unpack("<q", f.read(8))
        (head_len,) = struct.unpack("<q", f.read(8))
        nnue_bytes = f.read(nnue_len)
        head_bytes = f.read(head_len)

    got = _md5_bytes(nnue_bytes)
    if got != trunk_md5:
        sys.exit(f"{path}: corrupt -- md5(OZNN section) {got} != header trunk_md5 {trunk_md5}")
    baked = _read_head_trunk_md5(head_bytes)
    if baked != trunk_md5:
        sys.exit(f"{path}: head/trunk mismatch -- head baked {baked} != trunk {trunk_md5}")
    return {"version": version, "run_id": run_id, "trunk_md5": trunk_md5,
            "nnue_bytes": nnue_bytes, "head_bytes": head_bytes}


def _embed_fens(fens, ft_w, ft_b):
    """FEN -> 512-dim STM-relative trunk embedding, matching train_unc_head.embed().

    own/opp accum[d] = clamp(ft_b[d] + sum_{active feat} ft_w[d, feat], 0, 1);
    x = concat([stm_accum, non_stm_accum]).  ft_w is [L1, HALFKP]."""
    x = np.empty((len(fens), 2 * L1_SIZE), dtype=np.float32)
    for i, fen in enumerate(fens):
        wf, bf = fen_to_halfkp(fen)
        white = np.clip(ft_b + ft_w[:, wf].sum(axis=1), 0.0, 1.0)
        black = np.clip(ft_b + ft_w[:, bf].sum(axis=1), 0.0, 1.0)
        wtm = fen.split()[1] == "w"
        x[i, :L1_SIZE] = white if wtm else black
        x[i, L1_SIZE:] = black if wtm else white
    return torch.from_numpy(x)


def _load_head_from_bytes(head_bytes, tmp):
    tmp.write_bytes(head_bytes)
    return read_head_bin(str(tmp))


def load_ft_from_bytes(nnue_bytes, tmp):
    tmp.write_bytes(nnue_bytes)
    return load_ft(str(tmp))


def verify_oznu(path, nnue_path, head_path, fens=None, tol=1e-5):
    """A1 DoD: the OZNU reproduces a SEPARATE load of nnue.bin + head, on probe FENs.

    Compares (a) MDN state_dict tensors and (b) the MDN forward over embeddings
    built from each source's trunk. Because pack copies both blobs verbatim, an
    exact match is expected -- any drift means the container mangled a section."""
    fens = fens or _PROBE_FENS
    parts = read_oznu(path)
    scratch = Path(path).with_suffix(".verify_tmp")

    emb_head = scratch.with_name(scratch.name + ".head")
    emb_nnue = scratch.with_name(scratch.name + ".nnue")
    try:
        # From the fused container.
        c_model, c_meta = _load_head_from_bytes(parts["head_bytes"], emb_head)
        c_ftw, c_ftb = load_ft_from_bytes(parts["nnue_bytes"], emb_nnue)
        # From the standalone original files.
        o_model, o_meta = read_head_bin(head_path)
        o_ftw, o_ftb = load_ft(nnue_path)

        assert c_meta["trunk_md5"] == o_meta["trunk_md5"] == parts["trunk_md5"], "md5 disagreement"

        for (ck, cv), (ok, ov) in zip(c_model.state_dict().items(), o_model.state_dict().items()):
            assert ck == ok and torch.allclose(cv, ov), f"head weight mismatch at {ck}"

        with torch.no_grad():
            cx = _embed_fens(fens, c_ftw, c_ftb)
            ox = _embed_fens(fens, o_ftw, o_ftb)
            assert torch.allclose(cx, ox, atol=tol), "embedding mismatch"
            cy = c_model.net(cx)
            oy = o_model.net(ox)
            max_abs = (cy - oy).abs().max().item()
            assert max_abs <= tol, f"MDN forward mismatch (max abs {max_abs})"
    finally:
        for p in (emb_head, emb_nnue):
            p.unlink(missing_ok=True)

    print(f"OK  verify passed on {len(fens)} probe FENs "
          f"(head+trunk reproduce standalone load exactly; max MDN Δ={max_abs:.2e})")
    print(f"    run_id={parts['run_id']!r}  trunk_md5={parts['trunk_md5']}")
    return True


def _cmd_pack(a):
    run_id = a.run_id if a.run_id is not None else Path(a.head).parent.name
    out, md5, rid = pack_oznu(a.out, a.nnue, a.head, run_id)
    size = Path(out).stat().st_size
    print(f"wrote {out}  ({size:,} bytes)")
    print(f"  run_id   = {rid!r}")
    print(f"  trunk_md5= {md5}")


def _cmd_inspect(a):
    parts = read_oznu(a.path)
    print(f"{a.path}")
    print(f"  magic     OZNU  version {parts['version']}")
    print(f"  run_id    {parts['run_id']!r}")
    print(f"  trunk_md5 {parts['trunk_md5']}")
    print(f"  nnue blob {len(parts['nnue_bytes']):,} bytes  (OZNN)")
    print(f"  head blob {len(parts['head_bytes']):,} bytes  (OZUH)")


def _cmd_verify(a):
    verify_oznu(a.path, a.nnue, a.head)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)

    p = sub.add_parser("pack", help="fuse nnue.bin + head best.bin -> OZNU")
    p.add_argument("--nnue", required=True)
    p.add_argument("--head", required=True)
    p.add_argument("--out", required=True)
    p.add_argument("--run-id", default=None,
                   help="free-form provenance string (default: the head run-dir name)")
    p.set_defaults(func=_cmd_pack)

    p = sub.add_parser("inspect", help="print an OZNU header")
    p.add_argument("path")
    p.set_defaults(func=_cmd_inspect)

    p = sub.add_parser("verify", help="round-trip an OZNU vs the original files")
    p.add_argument("path")
    p.add_argument("--nnue", required=True)
    p.add_argument("--head", required=True)
    p.set_defaults(func=_cmd_verify)

    a = ap.parse_args()
    a.func(a)


if __name__ == "__main__":
    main()
