<h1 align="center">OmegaZero</h1>

<h6 align="center">Noah Himed</h6>

<p align="center">
  <img src="./figs/logo.png" width="300" alt="OmegaZero Logo">
</p>

<p align="center">
  Proudly open source, ruthlessly tactical, and queer-built. 🏳️‍🌈 
</p>

<p align="center">
  <a href="./LICENSE">
    <img src="https://img.shields.io/badge/License-MIT-green.svg" alt="MIT License">
  </a>
  <img src="https://img.shields.io/badge/Elo-1900-orange.svg" alt="1900 Elo">
  <img src="https://img.shields.io/badge/UCI-Compatible-success.svg" alt="UCI Compatible">
  <img src="https://img.shields.io/badge/NNUE-HalfKP-blue.svg" alt="NNUE HalfKP">
  <img src="https://img.shields.io/github/v/release/noah8368/OmegaZero" alt="Latest Release">
</p>

<p align="center">
  <a href="#project-summary">Project Summary</a> •
  <a href="#play-online">Play Online</a> •
  <a href="#usage">Usage</a> •
  <a href="#implementation">Implementation</a> •
  <a href="#performance">Performance</a>
</p>

### Project Summary

OmegaZero is a chess engine built from scratch which allows a user to play 
against an AI. The name "OmegaZero" is an homage to [AlphaZero](https://en.wikipedia.org/wiki/AlphaZero), a program
developed by [DeepMind](https://deepmind.com/) that was used to create one of the world's
best Chess engines. The [Chess Programming Wiki](https://www.chessprogramming.org/Main_Page) was referenced heavily during
development. Credit goes to [Brandon Hsu](https://github.com/2brandonh) for designing the original
logo; [Claude](https://en.wikipedia.org/wiki/Claude_(language_model)) was used to stylize the image after the anime [No Game No Life](https://en.wikipedia.org/wiki/No_Game_No_Life) anime.

### Play Online

OmegaZero is live on Lichess as a bot! You can challenge it to a game anytime:

**[Challenge OmegaZero-Bot on Lichess](https://lichess.org/@/OmegaZero-Bot)**

The bot runs the same engine described below, connected via the UCI protocol.

### Usage

#### Prerequisites

The `Makefile` supports GNU/Linux and macOS. Install the core dependencies first, then add optional ones as needed.

**Core (required to build and play)**

| | Ubuntu | macOS ([Homebrew](https://brew.sh/)) |
|---|---|---|
| C++ / build tools | `sudo apt-get install g++ make` | Xcode Command Line Tools |
| Python 3 | `sudo apt-get install python3` | pre-installed |

Verify everything is in place:
```
make check-deps
```

**NNUE training** (datagen + training scripts)
```
pip3 install torch tqdm
```

**Elo testing** ([Stockfish](https://stockfishchess.org/) + [cutechess-cli](https://github.com/cutechess/cutechess) + [matplotlib](https://matplotlib.org/))

On Ubuntu:
```
sudo apt-get install stockfish cutechess qtbase5-dev cmake
pip3 install matplotlib
```

On macOS (cutechess must be built from source):
```bash
brew install stockfish qt cmake
pip3 install matplotlib --break-system-packages

cd ~/path/to/OmegaZero
git clone https://github.com/cutechess/cutechess.git
cd cutechess && mkdir build && cd build
cmake .. && make -j8
```

#### Building

```
make              # Optimized engine binary → build/OmegaZero
make debug        # Debug harness (ASan, -O0) → build/debug_harness
make bench        # NPS benchmark harness (-O3) → build/bench_harness
make clean        # Remove all build artifacts
make datagen      # NNUE training data generation harness → build/datagen_harness
make check-deps   # Verify g++ and python3 are installed
```

#### Playing a Game

To begin a game, a user invokes the program as follows:
```
OmegaZero -p [SIDE] -t [TIME]
```
where `[SIDE]` is the side the user would like to play. This may be `w` for
White, `b` for Black, or `r` for a random selection. `[TIME]` is the amount of time (in seconds) to give the engine during play. This defaults to `5s`.

To use the handcrafted eval instead of NNUE, add `--hce`:
```
OmegaZero --hce -p w -t 5
```

The board display defaults to dark terminal backgrounds (filled glyphs = white pieces). If using a light terminal, add `--light-theme`:
```
OmegaZero --light-theme -p w
```

To start from a custom position, add `-i` with a [FEN](https://www.chessprogramming.org/Forsyth-Edwards_Notation) string. Use `w` or `b` in the FEN to set which side moves first:
```
OmegaZero -i "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 0 1" -p w -t 5  # white to move
OmegaZero -i "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1" -p b -t 5  # black to move
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

#### UCI Mode

OmegaZero supports the [Universal Chess Interface](https://www.chessprogramming.org/UCI) (UCI) protocol for integration with chess GUIs and tournament managers:
```
OmegaZero --uci
```
The engine is single-threaded; `stop` is a no-op (search completes before input is read) and `go infinite` is not supported.

#### Testing

##### SPRT

[SPRT](https://www.chessprogramming.org/Match_Statistics#SPRT) determines whether a new version is stronger than a baseline, stopping automatically once statistically significant. Uses `openings.pgn` (2,678 ECO openings) by default. See `python3 scripts/sprt.py --help` for all options.
```bash
python3 scripts/sprt.py match v1 v3              # compare any two git refs
python3 scripts/sprt.py gauntlet                  # SPRT across all version tags
python3 scripts/sprt.py run --baseline-commit HEAD~1
python3 scripts/sprt.py plot                      # regenerate Elo/W-D-L charts
```

##### Elo Testing

Estimates playing strength by running matches against Stockfish at various `UCI_Elo` levels via cutechess-cli. See `python3 scripts/elo_test.py --help` for all options.
```bash
python3 scripts/elo_test.py run                   # defaults: 20 games at 5 Elo levels
python3 scripts/elo_test.py plot                   # regenerate plots
```

##### Search Benchmarking

Measures NPS (nodes per second) across four standard positions. See `python3 scripts/search_bench.py --help` for all options.
```bash
python3 scripts/search_bench.py run               # benchmark current build (5s/position)
python3 scripts/search_bench.py gauntlet           # benchmark all tagged versions
python3 scripts/search_bench.py plot               # regenerate NPS plot
```

##### Perft

Verifies move generator correctness using [Perft](https://www.chessprogramming.org/Perft) node counting against [six standard positions](https://www.chessprogramming.org/Perft_Results). See `python3 scripts/perft.py --help` for all options.
```bash
python3 scripts/perft.py run                      # all 6 positions, depth 1-5
python3 scripts/perft.py run --max-depth 6         # deeper (slower)
python3 scripts/perft.py list                      # show all positions and expected values
```

##### Debug Harness

Runs perft regression, eval sanity, search sanity, and self-play crash detection:
```
make debug
./build/debug_harness
```

#### Generating Move Tables

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

#### NNUE

Generate self-play data, train, analyze. Config lives in `nnue/config.json` (copy from `nnue/config.json.example`). See each script's `--help` or header comments for options.
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

### Implementation

#### Board Representation

The engine uses both [Bitboards](https://www.chessprogramming.org/Bitboards) and an [8x8 Board](https://www.chessprogramming.org/8x8_Board) to represent board.
states. Squares are indexed in the [Little Endian Rank File (LERF)](https://www.chessprogramming.org/Square_Mapping_Considerations#Little-Endian_Rank-File_Mapping) format.

#### Move Generation

For non-sliding pieces, arrays of bitboards representing all possible places
a piece can move to on an empty board for every square are computed
by `generate_masks.py`. For sliding pieces, move generation is implemented
through the [magic bitboard technique](http://pradu.us/old/Nov27_2008/Buzz/research/magic/Bitboards.pdf).

The move generation function `Engine::GenerateMoves()` is implemented as a
[pseudo-legal generator](https://www.chessprogramming.org/Move_Generation#Pseudo-legal). A full legality check is made in `Board::MakeMove()`
to ensure that a move does not put the moving player in check; illegal moves are
unmade if they are found to do this.

#### Transposition Table

A custom hash table was used to implement the [Transposition Table](https://www.chessprogramming.org/Transposition_Table).
The [Zobrist Hashing](https://www.chessprogramming.org/Zobrist_Hashing) 
algorithm was used to hash board states. Hash moves retrieved from the table are validated against the current board state before use, guarding against rare hash collisions that could otherwise cause the engine to apply an illegal move. The Transposition Table is [two-tiered](https://www.chessprogramming.org/Transposition_Table#Two-tier_System), using the
"Always Replace" and "Depth-Preferred" replacement schemes in parallel.

#### Search

The [MTD(f)](https://www.chessprogramming.org/MTD(f)) search algorithm is used within an [Iterative Deepening](https://www.chessprogramming.org/Iterative_Deepening)
framework. This routine calls an implementation of the [Negamax](https://www.chessprogramming.org/Negamax) algorithm
with [alpha-beta pruning](https://www.chessprogramming.org/Alpha-Beta), [Null Move Pruning](https://www.chessprogramming.org/Null_Move_Pruning), [Futility Pruning](https://www.chessprogramming.org/Futility_Pruning), [Reverse Futility Pruning](https://www.chessprogramming.org/Reverse_Futility_Pruning),  [Late Move Reduction](https://www.chessprogramming.org/Late_Move_Reductions), and [Late Move Pruning](https://www.chessprogramming.org/Futility_Pruning#Move_Count_Based_Pruning). A
Transposition Table is used to cache seen positions, allowing the engine to
store each [node's type](https://www.chessprogramming.org/Node_Types) and prevent costly re-evaluation of a node. This
is especially important for storing the [Principle Variation](https://www.chessprogramming.org/Principal_Variation) during Iterative
Deepening.

After search to a specified depth, all captures are searched during the
[Quiescence Search](https://www.chessprogramming.org/Quiescence_Search) to limit the [Horizon Effect](https://www.chessprogramming.org/Horizon_Effect). [Delta Pruning](https://www.chessprogramming.org/Delta_Pruning) and [SEE Pruning](https://www.chessprogramming.org/Static_Exchange_Evaluation#Pruning) are used to
limit the number of nodes explored during Quiescence Search. 

To reduce the number of nodes needed to be searched, OmegaZero takes advantage
of a set of heuristics to perform move ordering in `Engine::OrderMoves()` in
order to increase the number of [Beta-Cutoffs](https://www.chessprogramming.org/Beta-Cutoff) during alpha-beta pruning.
Moves are put in the following order:
1. [Hash Move](https://www.chessprogramming.org/Hash_Move)
2. Promotions (scored by promoted piece value minus pawn value) and good captures (SEE value >= 0), ordered together by score
3. Two [Killer Moves](https://www.chessprogramming.org/Killer_Heuristic)
4. All other quiet moves, ordered by [History Heuristic](https://www.chessprogramming.org/History_Heuristic) and [Countermove Heuristic](https://www.chessprogramming.org/Countermove_Heuristic)
5. Bad captures (SEE value < 0) ordered by SEE Heuristic

The [MVV-LVA Heuristic](https://www.chessprogramming.org/MVV-LVA) is used to order captures in Quiescence Search, with all quiets placed after, unordered. 


#### Opening Book

In the beginning of the game, the engine randomly picks an opening from a PGN opening book (`openings.pgn`). The book contains 2,678 openings covering the full ECO classification (A00–E99), converted from `p3ECO.txt` written by Paul Onstad (with contributions by Franz Hemmer and J.E.H.Shaw). The PGN format is also used directly by cutechess-cli for SPRT testing game diversity.

#### Evaluation

OmegaZero uses an [NNUE](https://www.chessprogramming.org/NNUE) (Efficiently Updatable Neural Network) for position evaluation. The [HalfKP](https://www.chessprogramming.org/Stockfish_NNUE) architecture encodes 40,960 sparse (king_square, piece_type, piece_square) features per perspective, with only ~30 active per position. Weights are quantized to `int16`/`int8` for fast integer inference. See [NNUE](#nnue) for training.

<p align="center">
  <img src="./figs/nnue_architecture.png" width="480" alt="NNUE Architecture">
</p>

If no weights file is found (`nnue/nnue.bin`), the engine falls back to a [Fruit](https://www.chessprogramming.org/Fruit)-style handcrafted eval: material + [piece-square tables](https://www.chessprogramming.org/Simplified_Evaluation_Function), [pawn structure](https://www.chessprogramming.org/Pawn_Structure) (isolated, passed, backward, phalanx, defended, king shield), [piece mobility](https://www.chessprogramming.org/Mobility), [king safety](https://www.chessprogramming.org/King_Safety) (Toga/Fruit attack counting), and misc bonuses ([bishop pair](https://www.chessprogramming.org/Bishop_Pair), connected rooks, [castling rights](https://www.chessprogramming.org/Castling_Rights), [rook behind passer](https://www.chessprogramming.org/Tarrasch_Rule)). Uses [tapered eval](https://www.chessprogramming.org/Tapered_Eval) for king positioning. See `Board::Evaluate()` in `board.cc` for details.

### Performance

#### Elo Gain

Measured via [SPRT](#sprt) (0.5s/move, 2,678 ECO openings).

<p align="center">
  <img src="./figs/sprt_gauntlet_elo.png" width="480" alt="SPRT Elo Gain Per Version">
</p>

#### Win / Draw / Loss Breakdown

![SPRT W/D/L](./figs/sprt_gauntlet_wdl.png "SPRT W/D/L Breakdown")

#### Example Games

**~1000 Elo Human Player (White) vs OmegaZero v1 (Black) — 0-1** English Opening, Symmetrical Variation.

`1.c4 c5 2.Nc3 Nc6 3.d4 cxd4 4.Nd5 e6 5.Nf4 Bb4+ 6.Bd2 Bxd2+ 7.Qxd2 Nf6 8.Nf3 Ne4 9.Qd3 Qa5+ 10.Nd2 d5 11.cxd5 exd5 12.g3 Bg4 13.Nxd5 Nxd2 14.Qxd2 Qxd5 15.Rg1 0-0-0 16.h3 Bxe2 17.Bxe2 Rhe8 18.0-0-0 Qxa2 19.Bg4+ Kb8 20.Qf4+ Ne5 21.Rxd4 Rxd4 22.Qxd4 Qa1+ 23.Kc2 Qxg1 24.Qd6+ Ka8 25.Bd1 Qxf2+ 26.Qd2 Rc8+ 27.Kb3 Qxd2 28.Ka2 Qxd1 29.g4 Qa4+ 30.Kb1 Qc2+ 31.Ka2 Nd3 32.h4 Qxb2# 0-1`

Final Position

<p align="center">
  <img src="./figs/final_position_1000_ELO_player.png" width="480" alt="Final Position for 1000 Elo Player">
</p>

**1643 Elo<sup>1</sup> Human Player (White) vs OmegaZero v1 (Black) — 0-1** Scandinavian Defense.

`1.e4 d5 2.exd5 Nf6 3.Bc4 Nxd5 4.Bxd5 Qxd5 5.Nc3 Qxg2 6.Qf3 Qxf3 7.Nxf3 Na6 8.a3 Bg4 9.Ne5 Bf5 10.d3 f6 11.Nc4 e5 12.Be3 Nc5 13.b4 Ne6 14.O-O-O Bg4 15.Rd2 c5 16.b5 O-O-O 17.Ne4 Be7 18.Ng3 Nd4 19.h3 Be6 20.Nb2 Nxb5 21.a4 Nd4 22.Ne4 f5 23.Nc3 Nf3 24.Re2 e4 25.dxe4 fxe4 26.Nxe4 Bxh3 27.Rxh3 Ng1 28.Re1 Nxh3 29.Nxc5 Bxc5 30.Bxc5 b6 31.Be3 Rhf8 32.Nd3 Rxd3 33.cxd3 Nxf2 34.Kd2 Rf7 35.Re2 Ng4 36.Bd4 Kb7 37.Rg2 Rf4 38.Bxg7 Rxa4 39.Kc3 Ne3 40.Re2 Nd5+ 41.Kb3 Rb4+ 42.Ka3 Rb5 43.d4 Ra5+ 44.Kb3 Ra1 45.Kc4 Rg1 46.Be5 Rg2 47.Re4 Kc6 48.Bh8 Rc2+ 49.Kd3 Rc3+ 50.Kd2 Rh3 51.Re6+ Kb5 52.Rd6 Rh2+ 53.Kd3 Rh5 54.Rd7 Nb4+ 55.Kc3 Rh3+ 56.Kd2 Nc6 57.d5 Rh2+ 58.Kc3 Nb4 59.d6 Nc6 60.Rc7 Rh4 61.d7 Rh3+ 62.Kd2 Rh5 63.Rc8 Rd5+ 0-1`

Final Position

<p align="center">
  <img src="./figs/final_position_1643_ELO_player.gif" width="480" alt="Final Position for 1643 Elo Player">
</p>

**~1900 Elo<sup>2</sup> Human Player vs OmegaZero v1 (Black) — 1-0** Queen's Gambit Accepted.

`1.d4 d5 2.c4 e6 3.g3 dxc4 4.Bg2 Ne7 5.Nd2 Qxd4 6.Ngf3 Qc5 7.O-O Nd5 8.Qc2 c3 9.Ne4 cxb2 10.Qxb2 Qb6 11.Qc2 Nb4 12.Qa4+ Bd7 13.Qd1 Nxa2 14.Rxa2 Qb1 15.Qc2 Qxa2 16.Qxa2 f5 17.Neg5 Nc6 18.Nxe6 Bd6 19.Nxg7+ Kd8 20.Bg5+ Kc8 21.Rb1 Nb4 22.Qc4 Bxg3 23.Qxb4 Bc6 24.hxg3 Bxf3 25.Bxf3 b6 26.Nxf5 h5 27.Bxa8 h4 28.Qe4 Rd8 29.Ne7+ Kd7 30.Bc6+ Kd6 31.Qd5# 1-0`

Final Position

<p align="center">
  <img src="./figs/final_position_1900_ELO_player.png" width="480" alt="Final Position for 1900 Elo Player">
</p>

<sup>1</sup> Lichess rating
<sup>2</sup> Chess.com rating
