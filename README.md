# OmegaZero Chess Engine

###### Noah Himed

![OmegaZero Logo](./logo.png "OmegaZero -Brandon Hsu")

### Project Summary

OmegaZero is a terminal-based chess engine which allows a user to play against
an AI. The engine uses a board representation that takes advantage of both
bitboards and an 8x8 representation, and uses the magic bitboard technique
to implement a psuedo-legal move generator. Search and evaluation is done by
traversing a tree and using an evaluation function which takes into account
material advantage and piece position on each board state. Alpha-beta pruning
and a transposition table are the main optimizations made to this process.

The name OmegaZero is an homage to [AlphaZero](https://en.wikipedia.org/wiki/AlphaZero), a program developed by
[DeepMind](https://deepmind.com/) that was used to create one of the world's best Chess engines. The
[Chess Programming Wiki](https://www.chessprogramming.org/Main_Page) was referenced heavily during development. Credit goes
to [Bradon Hsu](https://github.com/2brandonh) for designing the logo for this project.

### Usage

#### Prerequisites

The included `Makefile` is designed to run on GNU/Linux machines. The [Boost library](https://www.boost.org/)
is a requirement, and should be installed locally before compilation.
On Ubuntu systems, users may use the `apt-get` package manager to
install Boost, like so:
```
sudo apt-get install libboost-all-dev
```

#### User Input

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

To resign, a user must enter `r` on their turn.

### Implementation

#### Board Representation

64 bit unsigned integers are used to represent board states, with a `1` in the
number representing the presence of a piece on a board. Squares are indexed in
the Little Endian Rank File (LERF) format.   

#### Move Generation

For non-sliding pieces, arrays of bitboards representing all possible places
a piece can move to on an empty board for every square are computed
by `generate_masks.py`. For sliding pieces, move generation is implemented
through the [magic bitboard technique](http://pradu.us/old/Nov27_2008/Buzz/research/magic/Bitboards.pdf).

The move generation function is implemented as a [pseudo-legal generator](https://www.chessprogramming.org/Move_Generation#Pseudo-legal). A
full legality check is made in `Board::MakeMove()` to ensure that a move does not
put the moving player in check; illegal moves are unmade if they are found to
do this.

#### Board Hashing

In order efficiently compare positions throughout a game, `Game` objects use the
function `Board::GetBoardHash()` to compute hashes of game states. This function is
implemented with the [Zobrist Hashing](https://www.chessprogramming.org/Zobrist_Hashing) algorithm, and is used during the detection
of both threefold and fivefold move repitions and to generate keys to index into the
transposition table. This technique carries with it a small risk for collisions, with
an expected collision rate of one in 2^32 ≈ 4.29 billion. Relatively little can be done
to mitigate this risk, and as such is a known and unavoidable bug with this
implementation.
