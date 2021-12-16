# OmegaZero Chess Engine

###### Noah Himed

![OmegaZero Logo](./logo.png "OmegaZero -Brandon Hsu")

### Project Summary

OmegaZero is an in-progress terminal-based chess engine which will allow a user
to play against an AI. The name OmegaZero is an homage to [AlphaZero](https://en.wikipedia.org/wiki/AlphaZero), a program
developed by [DeepMind](https://deepmind.com/) that was used to create one of the world's
best Chess engines. The [Chess Programming Wiki](https://www.chessprogramming.org/Main_Page) was referenced heavily during
development. Credit goes to [Bradon Hsu](https://github.com/2brandonh) for designing the
logo for this project.

### Status: In Progress

The internal board representation for the engine is complete, allowing two
players to play a legal game of chess between each other. All rules, including 
the Fifty Move Rule, castling rights, and En Passent elligibility are enforced.
At this point, however, the Engine does not have the ability to detect a
checkmate, nor the ability to generate its own moves.

Next steps in the project include debugging the move generation function,
allowing the engine to generate a legal list of moves given a board state. This
will allow checkmate detection and make possible the generation of a search
tree of possible game to be searched for ideal moves.

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

**Playing a Game**

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

**Testing**

To print out the [Perft](https://www.chessprogramming.org/Perft) results for engine. Invoke the program as follows:
```
build/OmegaZero -i [POSITION] -t [DEPTH]
```
`[POSITION]` is a [FEN](https://www.chessprogramming.org/Forsyth-Edwards_Notation) formatted string denoting the intial position to
start counting nodes from in the search tree; not providing this will cause the
program to default to the standard initial position in a chess game. 

`[DEPTH]` is a positive integer denoting the number of levels to generate in
the search tree.

After doing this, users have the choice of entering either a move formatted as
previously outlined to walk the search tree, or `q` to exit the program.

### Implementation

#### Board Representation

The engine uses both [Bitboards](https://www.chessprogramming.org/Bitboards) and an [8x8 Board](https://www.chessprogramming.org/8x8_Board) to represent board.
states. Squares are indexed in the Little Endian Rank File (LERF) format.

#### Move Generation

For non-sliding pieces, arrays of bitboards representing all possible places
a piece can move to on an empty board for every square are computed
by `generate_masks.py`. For sliding pieces, move generation is implemented
through the [magic bitboard technique](http://pradu.us/old/Nov27_2008/Buzz/research/magic/Bitboards.pdf).

The move generation function `Engine::GenerateMoves()` is implemented as a
[pseudo-legal generator](https://www.chessprogramming.org/Move_Generation#Pseudo-legal). A full legality check is made in `Board::MakeMove()`
to ensure that a move does not put the moving player in check; illegal moves are
unmade if they are found to do this.

#### Board Hashing

In order efficiently compare positions throughout a game, `Game` objects use the
function `Board::GetBoardHash()` to compute hashes of game states. This function is
implemented with the [Zobrist Hashing](https://www.chessprogramming.org/Zobrist_Hashing) algorithm, and is used during the detection
of both threefold and fivefold move repitions and to generate keys to index into the
transposition table. This technique carries with it a small risk for collisions, with
an expected collision rate of one in 2^32 ≈ 4.29 billion. Relatively little can be done
to mitigate this risk, and as such is a known and unavoidable bug with this
implementation.
