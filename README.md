<h1 align="center">OmegaZero</h1>

<p align="center">
  <img src="./figs/logo.png" width="300" alt="OmegaZero Logo">
</p>

<p align="center">
  Proudly open source, ruthlessly tactical, and queer-built 🏳️‍🌈 
</p>

<p align="center">
  <a href="./LICENSE">
    <img src="https://img.shields.io/badge/License-MIT-green.svg" alt="MIT License">
  </a>
  <img src="https://img.shields.io/badge/Elo-1800-orange.svg" alt="1900 Elo">
  <img src="https://img.shields.io/badge/UCI-Compatible-success.svg" alt="UCI Compatible">
  <img src="https://img.shields.io/badge/NNUE-HalfKP-blue.svg" alt="NNUE HalfKP">
  <img src="https://img.shields.io/github/v/release/noah8368/OmegaZero" alt="Latest Release">
</p>

<p align="center">
  <a href="#project-summary">Project Summary</a> •
  <a href="#performance">Performance</a> •
  <a href="#play-online">Play Online</a> •
  <a href="#implementation">Implementation</a> •
  <a href="#usage">Usage</a>
</p>

## Project Summary

OmegaZero is a chess engine built from scratch which allows a user to play 
against an AI. The name "OmegaZero" is an homage to [AlphaZero](https://en.wikipedia.org/wiki/AlphaZero), a program
developed by [DeepMind](https://deepmind.com/) that was used to create one of the world's
best Chess engines. The [Chess Programming Wiki](https://www.chessprogramming.org/Main_Page) was referenced heavily during
development. Credit goes to [Brandon Hsu](https://github.com/2brandonh) for designing the original
logo; [Claude](https://en.wikipedia.org/wiki/Claude_(language_model)) was used to stylize the image after the [No Game No Life](https://en.wikipedia.org/wiki/No_Game_No_Life) anime.

## Performance

### Elo Estimate

<p align="center">
  <img src="./figs/elo_estimate.png" width="600" alt="Elo Estimation Plot">
  <br>
  <em>Elo estimate from 2,100 games vs Stockfish (0.5s/move), with 95% bootstrap CI.<sup>3</sup></em>
</p>

### Elo Gain

<p align="center">
  <img src="./figs/sprt_gauntlet_elo.png" width="480" alt="SPRT Elo Gain Per Version">
  <br>
  <em>Elo gain per version via SPRT (0.5s/move, 2,678 ECO openings).<sup>3</sup></em>
</p>

### Win / Draw / Loss Breakdown

<p align="center">
  <img src="./figs/sprt_gauntlet_wdl.png" width="600" alt="SPRT W/D/L Breakdown">
  <br>
  <em>Win/draw/loss breakdown per version pair from the SPRT gauntlet.<sup>3</sup></em>
</p>

### Example Games

<details>
<summary><strong>~1000 Elo Human Player (White) vs OmegaZero<sup>4</sup> (Black) — 0-1</strong> English Opening, Symmetrical Variation.</summary>

`1.c4 c5 2.Nc3 Nc6 3.d4 cxd4 4.Nd5 e6 5.Nf4 Bb4+ 6.Bd2 Bxd2+ 7.Qxd2 Nf6 8.Nf3 Ne4 9.Qd3 Qa5+ 10.Nd2 d5 11.cxd5 exd5 12.g3 Bg4 13.Nxd5 Nxd2 14.Qxd2 Qxd5 15.Rg1 0-0-0 16.h3 Bxe2 17.Bxe2 Rhe8 18.0-0-0 Qxa2 19.Bg4+ Kb8 20.Qf4+ Ne5 21.Rxd4 Rxd4 22.Qxd4 Qa1+ 23.Kc2 Qxg1 24.Qd6+ Ka8 25.Bd1 Qxf2+ 26.Qd2 Rc8+ 27.Kb3 Qxd2 28.Ka2 Qxd1 29.g4 Qa4+ 30.Kb1 Qc2+ 31.Ka2 Nd3 32.h4 Qxb2# 0-1`

Final Position

<p align="center">
  <img src="./figs/final_position_1000_ELO_player.png" width="480" alt="Final Position for 1000 Elo Player">
</p>

</details>

<details>
<summary><strong>1643 Elo<sup>1</sup> Human Player (White) vs OmegaZero<sup>4</sup> (Black) — 0-1</strong> Scandinavian Defense.</summary>

`1.e4 d5 2.exd5 Nf6 3.Bc4 Nxd5 4.Bxd5 Qxd5 5.Nc3 Qxg2 6.Qf3 Qxf3 7.Nxf3 Na6 8.a3 Bg4 9.Ne5 Bf5 10.d3 f6 11.Nc4 e5 12.Be3 Nc5 13.b4 Ne6 14.O-O-O Bg4 15.Rd2 c5 16.b5 O-O-O 17.Ne4 Be7 18.Ng3 Nd4 19.h3 Be6 20.Nb2 Nxb5 21.a4 Nd4 22.Ne4 f5 23.Nc3 Nf3 24.Re2 e4 25.dxe4 fxe4 26.Nxe4 Bxh3 27.Rxh3 Ng1 28.Re1 Nxh3 29.Nxc5 Bxc5 30.Bxc5 b6 31.Be3 Rhf8 32.Nd3 Rxd3 33.cxd3 Nxf2 34.Kd2 Rf7 35.Re2 Ng4 36.Bd4 Kb7 37.Rg2 Rf4 38.Bxg7 Rxa4 39.Kc3 Ne3 40.Re2 Nd5+ 41.Kb3 Rb4+ 42.Ka3 Rb5 43.d4 Ra5+ 44.Kb3 Ra1 45.Kc4 Rg1 46.Be5 Rg2 47.Re4 Kc6 48.Bh8 Rc2+ 49.Kd3 Rc3+ 50.Kd2 Rh3 51.Re6+ Kb5 52.Rd6 Rh2+ 53.Kd3 Rh5 54.Rd7 Nb4+ 55.Kc3 Rh3+ 56.Kd2 Nc6 57.d5 Rh2+ 58.Kc3 Nb4 59.d6 Nc6 60.Rc7 Rh4 61.d7 Rh3+ 62.Kd2 Rh5 63.Rc8 Rd5+ 0-1`

Final Position

<p align="center">
  <img src="./figs/final_position_1643_ELO_player.gif" width="480" alt="Final Position for 1643 Elo Player">
</p>

</details>

<details>
<summary><strong>~1900 Elo<sup>2</sup> Human Player vs OmegaZero<sup>4</sup> (Black) — 1-0</strong> Queen's Gambit Accepted.</summary>

`1.d4 d5 2.c4 e6 3.g3 dxc4 4.Bg2 Ne7 5.Nd2 Qxd4 6.Ngf3 Qc5 7.O-O Nd5 8.Qc2 c3 9.Ne4 cxb2 10.Qxb2 Qb6 11.Qc2 Nb4 12.Qa4+ Bd7 13.Qd1 Nxa2 14.Rxa2 Qb1 15.Qc2 Qxa2 16.Qxa2 f5 17.Neg5 Nc6 18.Nxe6 Bd6 19.Nxg7+ Kd8 20.Bg5+ Kc8 21.Rb1 Nb4 22.Qc4 Bxg3 23.Qxb4 Bc6 24.hxg3 Bxf3 25.Bxf3 b6 26.Nxf5 h5 27.Bxa8 h4 28.Qe4 Rd8 29.Ne7+ Kd7 30.Bc6+ Kd6 31.Qd5# 1-0`

Final Position

<p align="center">
  <img src="./figs/final_position_1900_ELO_player.png" width="480" alt="Final Position for 1900 Elo Player">
</p>

</details>


## Play Online

OmegaZero is live on Lichess as a bot! You can challenge it to a game anytime:

**[Challenge OmegaZero-Bot on Lichess](https://lichess.org/@/OmegaZero-Bot)**

The bot runs the same engine described below, connected via the UCI protocol.

## Implementation

### Evaluation

OmegaZero primarily evaluates positions using an [NNUE](https://www.chessprogramming.org/NNUE) (Efficiently Updatable Neural Network), specifically the [HalfKP](https://www.chessprogramming.org/Stockfish_NNUE) architecture. Network weights are quantized to `int16` and `int8` for fast integer inference.

<p align="center">
  <img src="./figs/nnue_loss_and_accuracy.png" width="720" alt="NNUE Training Loss and Score Accuracy">
  <br>
  <em>Training loss and score accuracy on a <a href="https://drive.google.com/drive/folders/11guxluj5UL4BMaGmm0CqD-n7u5INT-EE?usp=sharing">6M-position HCE-generated dataset</a>.<sup>3</sup></em>
</p>

If the expected NNUE weights file isn't found (`nnue/nnue.bin`), OmegaZero falls back to a handcrafted evaluation inspired by [Fruit](https://www.chessprogramming.org/Fruit), incorporating:

- [Material balance](https://www.chessprogramming.org/Material)
- [Piece-square tables](https://www.chessprogramming.org/Simplified_Evaluation_Function)
- [Pawn structure](https://www.chessprogramming.org/Pawn_Structure)
- [Piece mobility](https://www.chessprogramming.org/Mobility)
- [King safety](https://www.chessprogramming.org/King_Safety)
- [Tapered evaluation](https://www.chessprogramming.org/Tapered_Eval)

Additional positional bonuses include the [bishop pair](https://www.chessprogramming.org/Bishop_Pair), connected rooks, [castling rights](https://www.chessprogramming.org/Castling_Rights), and [rook behind passer](https://www.chessprogramming.org/Tarrasch_Rule).

See [NNUE](#nnue) for training instructions.

### Search

<p align="center">
  <img src="./figs/search_animation.gif" width="720" alt="Alpha-Beta Search Animation">
  <br>
  <em>
    Search trace performed by OmegaZero<sup>3</sup> on a board position from
    <a href="https://www.chessprogramming.org/Kasparov_versus_Deep_Blue_1997#Game_6">
      Deep Blue v Kasparov
    </a>
  </em>
</p>

OmegaZero uses [Principle Variation Search (PVS) and Aspiration Windows](https://www.chessprogramming.org/Principal_Variation_Search#PVS_and_Aspiration) alongside the following pruning alogrithms to minimize nodes searched:

- [Null Move Pruning (NMP)](https://www.chessprogramming.org/Null_Move_Pruning)
- [Futility Pruning (FP)](https://www.chessprogramming.org/Futility_Pruning)
- [Reverse Futility Pruning (RFP)](https://www.chessprogramming.org/Reverse_Futility_Pruning)
- [Late Move Reductions (LMR)](https://www.chessprogramming.org/Late_Move_Reductions)
- [Late Move Pruning (LMP)](https://www.chessprogramming.org/Futility_Pruning#Move_Count_Based_Pruning)
- [Internal Iterative Reductions (IIR)](https://www.chessprogramming.org/Internal_Iterative_Reductions)

NMP, RFP, LMR, and LMP prune more aggresively if the static evaluations of a
search line aren't [improving](https://www.chessprogramming.org/Improving).

#### Transposition Table

A custom [Transposition Table](https://www.chessprogramming.org/Transposition_Table) is heavily integrated into search, allowing OmegaZero to avoid re-evaluating previously explored positions and efficiently track the principal variation between iterations. [Zobrist Hashing](https://www.chessprogramming.org/Zobrist_Hashing) is used to hash positions efficiently. The table uses a [two-tier replacement scheme](https://www.chessprogramming.org/Transposition_Table#Two-tier_System). Table entries store node types, search depths, and best moves.

#### Move Ordering

During Aspiration + PV Search, OmegaZero prioritizes moves using:

1. [Hash Move](https://www.chessprogramming.org/Hash_Move)
2. Promotions and favorable captures ordered by [Static Exchange Evaluation (SEE)](https://www.chessprogramming.org/Static_Exchange_Evaluation)
3. [Killer Moves](https://www.chessprogramming.org/Killer_Heuristic)
4. Quiet moves ordered by the [History Heuristic](https://www.chessprogramming.org/History_Heuristic) and [Countermove Heuristic](https://www.chessprogramming.org/Countermove_Heuristic)
5. Unfavorable captures ordered by SEE 

In [Quiescence Search](#quiescence-search), moves are ordered by putting captures first. Captures are sorted according to the [MVV-LVA Heuristic](https://www.chessprogramming.org/MVV-LVA). Efficient move ordering increases the likelihood of early beta cutoffs, reducing the number of nodes that must be searched.

#### Quiescence Search

To reduce the [Horizon Effect](https://www.chessprogramming.org/Horizon_Effect), OmegaZero extends leaf nodes with a [Quiescence Search](https://www.chessprogramming.org/Quiescence_Search) over tactical moves. The following pruning algorithms are used to keep the search space from exploding:

- [Delta Pruning](https://www.chessprogramming.org/Delta_Pruning)
- [SEE Pruning](https://www.chessprogramming.org/Static_Exchange_Evaluation#Pruning)

<p align="center">
  <img src="./figs/depth_vs_time.png" width="600" alt="Search Depth vs Time">
  <br>
  <em>Search depth vs time across four standard positions (log scale).<sup>3</sup></em>
</p>

### Move Generation

Precomputed attack tables are used for non-sliding pieces, and sliding piece attacks are generated using the [Magic Bitboard](http://pradu.us/old/Nov27_2008/Buzz/research/magic/Bitboards.pdf) technique. The engine generates [pseudo-legal moves](https://www.chessprogramming.org/Move_Generation#Pseudo-legal), with legality verified during move execution. The correctness of the move generator was confirmed using [Perft](https://www.chessprogramming.org/Perft) with
the positions from [this page](https://www.chessprogramming.org/Perft_Results).

<p align="center">
  <img src="./figs/version_nps_by_position.png" width="600" alt="NPS by Position Across Versions">
  <br>
  <em>NPS by Position Across Versions (5s/position, MacBook M4)</em>
</p>

### Board Representation

OmegaZero uses a hybrid board representation using both [Bitboards](https://www.chessprogramming.org/Bitboards) and an [8×8 Board](https://www.chessprogramming.org/8x8_Board). Bitboards are used for efficient move generation and attack calculations, while the 8×8 board simplifies position updates and move validation. Squares are indexed using [Little Endian Rank File (LERF)](https://www.chessprogramming.org/Square_Mapping_Considerations#Little-Endian_Rank-File_Mapping) mapping. 

### Opening Book

OmegaZero uses a PGN opening book containing 2,678 openings spanning the full ECO classification (A00–E99). The opening book is derived from [`p3ECO.txt`](https://www.enpassant.dk/chess/palview/manual/p3eco.htm) by Paul Onstad, with contributions from Franz Hemmer and J.E.H. Shaw. During the opening phase, a line is selected randomly to improve game variety.

## Usage

### Prerequisites

The `Makefile` supports GNU/Linux and macOS. The easiest way to install everything is with the setup script:

```bash
./scripts/setup.sh            # full install (build tools, venv, Python packages, Stockfish, cutechess)
./scripts/setup.sh --datagen  # minimal install for datagen server (g++, make, python3 only)
source .venv/bin/activate     # activate the Python environment
```

Both modes are idempotent — safe to re-run. The script detects your platform (macOS via Homebrew, Linux via apt/dnf/yum), creates a `.venv/` Python environment, and skips anything already installed.

<details>
<summary><strong>Manual installation</strong></summary>

**Core (required to build and play)**

| | Ubuntu | macOS ([Homebrew](https://brew.sh/)) |
|---|---|---|
| C++ / build tools | `sudo apt-get install g++ make` | Xcode Command Line Tools |
| Python 3 | `sudo apt-get install python3` | pre-installed |

Verify everything is in place:
```
make check-deps
```

**NNUE training**

```
pip3 install torch numpy matplotlib tqdm Pillow cairosvg graphviz python-chess
```

**Elo testing**

On Ubuntu:
```
sudo apt-get install stockfish cutechess qtbase5-dev cmake
pip3 install matplotlib
```

On macOS:
```bash
brew install stockfish cutechess graphviz cairo
```

</details>

### Building

```
make              # Optimized engine binary → build/OmegaZero
make debug        # Self-play harness (ASan, -O0) → build/debug_harness
make bench        # NPS benchmark harness (-O3) → build/bench_harness
make perft        # Perft harness (-O3) → build/perft_harness
make datagen      # NNUE training data generation harness → build/datagen_harness
make clean        # Remove all build artifacts
make check-deps   # Verify g++ and python3 are installed
```

### Playing a Game

To begin a game, a user invokes the program as follows:
```
OmegaZero -p [SIDE] -t [TIME]
```
where `[SIDE]` is the side the user would like to play. This may be `w` for
White, `b` for Black, or `r` for a random selection. `[TIME]` is the amount of time (in seconds) to give the engine during play. This defaults to `5s`.

To use the handcrafted eval instead of NNUE, add `--hce`:
```
OmegaZero --hce -p b -t 1
```

The board display defaults to dark terminal backgrounds (filled glyphs = white pieces). If using a light terminal, add `--light-theme`:
```
OmegaZero --light-theme
```

<p align="center">
  <img src="./figs/light_vs_dark_theme.png" width="600" alt="Light v Dark Theme">
  <br>
  <em>Terminal Interface on Light and Dark Backgrounds</em>
</p>

To start from a custom position, add `-i` with a [FEN](https://www.chessprogramming.org/Forsyth-Edwards_Notation) string. Use `w` or `b` in the FEN to set which side moves first:
```
OmegaZero -i "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 0 1" -p w  # white to move
OmegaZero -i "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1" -p b # black to move
```

The format used to denote entered moves is based around [FIDE standard algebraic
notation](https://www.chessprogramming.org/Algebraic_Chess_Notation#Standard_Algebraic_Notation_.28SAN.29). The only exception to FIDE notation is that `e.p.` **must** immediately
follow an en passant move without a space (in FIDE rules, this is optional). Further specification is only needed
to avoid ambiguity in a movement command. Some valid example moves are
 - Move pawn to e4: `e4`
 - Move queen to e4: `Qe4`
 - Move pawn to d8 and promote to queen: `d8Q`
 - Pawn takes piece on d6: `exd6`
 - Knight takes piece on e4: `Nxe4`
 - Rook on rank 1 moves to a3: `R1a3`
 - Rook on d file moves to f8: `Rdf8`
 - Pawn takes a piece on d8 and promotes to queen: `exd8Q`
 - Queen from h4 moves to e1: `Qh4e1`
 - Queen from h4 takes piece on e1: `Qh4xe1`
 - Pawn from e file takes pawn on d5 in en passant: `exd6e.p.`
 - Queenside castle: `0-0-0`
 - Kingside castle: `0-0`

To resign, a user must enter `q` on their turn.

### UCI Mode

OmegaZero supports the [Universal Chess Interface](https://www.chessprogramming.org/UCI) (UCI) protocol for integration with chess GUIs and tournament managers:
```
OmegaZero --uci
```
The engine is single-threaded; `stop` is a no-op (search completes before input is read) and `go infinite` is not supported.

### Testing

Use the `--help` flag for all options of the Python scripts discussed here.

#### SPRT

[SPRT](https://www.chessprogramming.org/Match_Statistics#SPRT) determines whether a new version is stronger than a baseline, stopping automatically once statistically significant. Uses `openings.pgn` (2,678 ECO openings) by default. 
```bash
python3 scripts/sprt.py match v1 v3              # compare any two git refs
python3 scripts/sprt.py gauntlet                  # SPRT across all version tags
python3 scripts/sprt.py run --baseline-commit HEAD~1
python3 scripts/sprt.py plot                      # regenerate Elo/W-D-L charts
```

#### Elo Estimation

Fits the standard Elo logistic curve to match results against multiple Stockfish levels, producing a statistically grounded rating estimate with bootstrap confidence intervals.
```bash
python3 scripts/elo.py run               # 500 games × 7 levels (1700–2300), 1s/move
python3 scripts/elo.py run --games 50 --st 0.5  # quick smoke test
python3 scripts/elo.py plot results/elo/<run>/summary.csv
```

#### Search Benchmarking

Measures NPS (nodes per second) across four standard positions. 
```bash
python3 scripts/benchmark.py run               # benchmark current build (5s/position)
python3 scripts/benchmark.py gauntlet           # benchmark all tagged versions
python3 scripts/benchmark.py plot               # regenerate NPS plot
```

#### Perft

Verifies move generator correctness using [Perft](https://www.chessprogramming.org/Perft) node counting against [six standard positions](https://www.chessprogramming.org/Perft_Results). 
```bash
python3 scripts/perft.py run                      # all 6 positions, depth 1-5
python3 scripts/perft.py run --max-depth 6         # deeper (slower)
python3 scripts/perft.py list                      # show all positions and expected values
```

#### Self-Play Crash Detection

Plays the engine against itself to detect crashes, illegal moves, and search errors. Built with AddressSanitizer for memory error detection.
```bash
python3 scripts/debug.py                          # 10 games, 0.1s/move
python3 scripts/debug.py --games 100              # longer soak test
python3 scripts/debug.py --games 1000 --search-time 0.05  # fast stress test
```

### NNUE

A pre-built [6M-position training dataset](https://drive.google.com/drive/folders/11guxluj5UL4BMaGmm0CqD-n7u5INT-EE?usp=sharing) is available for download. Place the files in `nnue/data/` and run `train_nnue.py` to train from scratch.

<p align="center">
  <img src="./figs/data_score_distribution.png" width="600" alt="6M Position Dataset Score Distribution">
  <br>
  <em>NNUE Training Dataset — Score Distribution (6.4M positions)</em>
</p>

To generate your own data, train, and analyze — config lives in `nnue/config.json` (copy from `nnue/config.json.example`). See each script's `--help` or header comments for options.
```bash
make datagen && ./scripts/run_datagen.sh     # generate data (auto-restarts on crash)
./scripts/shutdown_datagen.sh                # graceful shutdown
./scripts/sync_from_server.sh                # pull data from remote server
./scripts/combine_runs.sh                    # merge runs with dedup
python3 scripts/train_nnue.py                # train (see --help for all params)
cp nnue/model/<run>/best.bin nnue/nnue.bin && make
python3 scripts/plot_training.py data        # analyze data distributions
python3 scripts/plot_training.py model       # evaluate model accuracy
```

### Generating Move Tables

The engine relies on two precomputed source files for move generation.
These are checked into the repo and only need to be regenerated if the
underlying scripts change:

- `scripts/generate_masks.py` — generates `src/masks.cc`, which contains
  precomputed attack bitboards for non-sliding pieces (knights, kings, pawns)
  at every square.
- `scripts/mine_magics.py` — generates `src/magics.cc`, which contains
  [magic numbers](https://www.chessprogramming.org/Magic_Bitboards) for
  sliding piece (bishop, rook) move generation.

To regenerate:
```
python3 scripts/generate_masks.py
python3 scripts/mine_magics.py
```

`make` will automatically regenerate these files if they are missing.

---

<sup>1</sup> Lichess rating
<sup>2</sup> Chess.com rating
<sup>3</sup> All plots use [OmegaZero v3](https://github.com/noah8368/OmegaZero/releases/tag/v3)
<sup>3</sup> All example games use [OmegaZero v1](https://github.com/noah8368/OmegaZero/releases/tag/v1)
