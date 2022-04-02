# OmegaZero

###### Noah Himed

![OmegaZero Logo](./figs/logo.png "OmegaZero -Brandon Hsu")

### Project Summary

OmegaZero is a chess engine built from scratch which allows a user to play 
against an AI. The name "OmegaZero" is an homage to [AlphaZero](https://en.wikipedia.org/wiki/AlphaZero), a program
developed by [DeepMind](https://deepmind.com/) that was used to create one of the world's
best Chess engines. The [Chess Programming Wiki](https://www.chessprogramming.org/Main_Page) was referenced heavily during
development. Credit goes to [Bradon Hsu](https://github.com/2brandonh) for designing the
logo for this project.

### Usage

#### Prerequisites

The included `Makefile` is designed to run on GNU/Linux machines. The [Boost library](https://www.boost.org/)
is a requirement, and should be installed locally before compilation.
On Ubuntu systems, users may use the `apt-get` package manager to
install Boost like so:
```
sudo apt-get install libboost-all-dev
```

#### Building the Engine

To build the software, simply type `make` in the root directory of the project.
This will take several minutes as the `magics.cc` file takes quite
a while to build. To completely clean a build, `make purge` will remove the
entire build directory. To remove all object files except for the one generated
by `magics.cc`, you may `make clean`. Doing so will result in much faster
rebuild times.

#### User Input

##### Playing a Game

To begin a game, a user invokes the program as follows:
```
OmegaZero -p [SIDE] -t [TIME]
```
where `[SIDE]` is the side the user would like to player. This may be `w` for
White, `b` for Black, or `r` for a random selection. `[TIME]` is the amount of time (in seconds) to give the engine during play. This defaults to `5s`.

The format used to denote entered moves is based around [FIDE standard algebraic
notation](https://www.chessprogramming.org/Algebraic_Chess_Notation#Standard_Algebraic_Notation_.28SAN.29). The only exception to FIDE notation is that `e.p.` **must** immediately
follow an en passant move without a space (in FIDE rules, this is optional). Further specification is only needed
to avoid ambiguity in a movement command. Some valid example moves are
 - Move pawn to e4: `e4`
 - Move queen to e4: `Qe4`
 - Move pawn to d8 and promote to queen: `d8Q`
 - Pawn takes piece on d6: `exd6`
 - Night takes piece on e4: `Nxe4`
 - Rook on rank 1 moves to a3: `R1a3`
 - Rook on d file moves to f8: `Rdf8`
 - Pawn takes a piece on d8 and promotes to queen: `exd8Q`
 - Queen from h4 moves to e1: `Qh4e1`
 - Queen from h4 takes piece on e1: `Qh4xe1`
 - Pawn from e file takes pawn on d5 in en passant: `exd6e.p.`
 - Queenside castle: `0-0-0`
 - Kingside castle: `0-0`

To resign, a user must enter `q` on their turn.

##### Testing

To print out the [Perft](https://www.chessprogramming.org/Perft) results for engine, invoke the program as follows:
```
OmegaZero -i [POSITION] -d [DEPTH]
```
`[POSITION]` is a [FEN](https://www.chessprogramming.org/Forsyth-Edwards_Notation) formatted string denoting the intial position to
start counting nodes from in the search tree; not providing this will cause the
program to default to the standard initial position in a chess game. 

`[DEPTH]` is a positive integer denoting the number of levels to generate in
the search tree.

After doing this, users have the choice of entering either a move formatted as
previously outlined to walk the search tree, or `q` to exit the program.

The positions on [this page](https://www.chessprogramming.org/Perft_Results) were used to confirm the correctness of the move
generator.

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
algorithm was used to hash board states. This technique an expected collision
rate of one in 2^32 ≈ 4.29 billion. Relatively little can be done
to mitigate this risk, and as such is a known and unavoidable bug with this
implementation. The Transposition Table is [two-tiered](https://www.chessprogramming.org/Transposition_Table#Two-tier_System), using the
"Always Replace" and "Depth-Preferred" replacement schemes in parallel.

#### Search

The [MTD(f)](https://www.chessprogramming.org/MTD(f)) search algorithm is used within an [Iterative Deepening](https://www.chessprogramming.org/Iterative_Deepening)
framework. This routine calls an implementation of the [Negamax](https://www.chessprogramming.org/Negamax) algorithm
with [alpha-beta pruning](https://www.chessprogramming.org/Alpha-Beta), [Null Move Pruning](https://www.chessprogramming.org/Null_Move_Pruning), and [Late Move Reduction](https://www.chessprogramming.org/Late_Move_Reductions). A depth reduction value [R](https://www.chessprogramming.org/Depth_Reduction_R)
of 3 is used when depth is greater than 6, and 2 otherwise in Null Move Pruning.
For Late Move Reductions are computed using the formula
```
int(sqrt(double(depth-1)) + sqrt(double(move_idx-1)))
```
taken from [Fruit](https://www.chessprogramming.org/Fruit). Reduction is only done on non-PV (Principle Variation) nodes. A
Transposition Table is used to cache seen positions, allowing the engine to
store each [node's type](https://www.chessprogramming.org/Node_Types) is and prevent costly re-evaluation of a node. This
is especially important for storing the [Principle Variation](https://www.chessprogramming.org/Principal_Variation) during Iterative
Deepening.

After search to a specified depth, all captures are searched during the
[Quiescence Search](https://www.chessprogramming.org/Quiescence_Search) to limit the [Horizon Effect](https://www.chessprogramming.org/Horizon_Effect). [Delta Pruning](https://www.chessprogramming.org/Delta_Pruning) is used to
limit the number of nodes explored during Quiescence Search.

To reduce the number of nodes needed to be searched, OmegaZero takes advantage
of a set of heuristics to perform move ordering in `Engine::OrderMoves()` in
order to increase the number of [Beta-Cutoffs](https://www.chessprogramming.org/Beta-Cutoff) during alpha-beta pruning.
Moves are put in the following order:
1. [Hash Move](https://www.chessprogramming.org/Hash_Move)
2. Captures, ordered using the [MVV-LVA](https://www.chessprogramming.org/MVV-LVA) heuristic
3. Two [Killer Moves](https://www.chessprogramming.org/Killer_Heuristic)
4. All other moves, unordered

#### Opening Book

In the beginning of the game, the engine randomly picks an opening from an
opening book. This list of openings are provided from the text file,
`p3ECO.txt` written by Paul Onstad (with contributions by Franz Hemmer and
J.E.H.Shaw). Slight modifications have been made to the file to aid in parsing.

#### Evaluation

Following in the footsteps of [Fruit](https://www.chessprogramming.org/Fruit), OmegaZero follows a minimalist
evaluation philosophy, with a "light" evaluation, which scores a board position
based on the following factors:
- Raw material

- Piece position, using the [Piece Square Tables](https://www.chessprogramming.org/Simplified_Evaluation_Function) defined in `piece_sq_tables.cc`

- Pawn structure. The engine is aware of [backward pawns](https://www.google.com/search?q=backward+pawns&oq=backward+pawns&aqs=chrome..69i57j0i512j0i22i30j0i390j69i60.1876j1j4&client=ubuntu&sourceid=chrome&ie=UTF-8), [isolated pawns](https://en.wikipedia.org/wiki/Isolated_pawn),
[passed pawns](https://en.wikipedia.org/wiki/Passed_pawn#:~:text=In%20chess%2C%20a%20passed%20pawn,sometimes%20colloquially%20called%20a%20passer.), [phalanxes](https://www.chessprogramming.org/Duo_Trio_Quart_(Bitboards)), and [defended pawns](https://www.chessprogramming.org/Defended_Pawns_(Bitboards)). It also adds penalties for holes in the king's pawn shield when castled.

- Misc. bonuses/penalties for the following features: connnected rooks, loss of
[castling rights](https://www.chessprogramming.org/Castling_Rights), [bishop pair](https://www.chessprogramming.org/Bishop_Pair), and [rook behind passed pawn](https://www.chessprogramming.org/Tarrasch_Rule).

We use a [Tapered Eval](https://www.chessprogramming.org/Tapered_Eval) scheme when scoring the position of the king, using
the formula found [here](https://www.chessprogramming.org/Tapered_Eval#Implementation_example).

### Performance

#### Move Generation

The move generator is capable of producing up to ~7 million moves/sec.

#### ELO Approximation

OmegaZero consistently wins against Stockfish through level four, and is
competitive with Stockfish level five. From this, we can approximate an
ELO score of ~2000 for the engine.
