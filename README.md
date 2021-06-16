# OmegaZero Chess Engine

###### Noah Himed
###### 16 June 2021

### Project Summary

OmegaZero is a terminal-based chess engine which allows a user to play against
an AI. The engine uses a board representation that takes advantage of both
bitboards and an 8x8 representation, and uses the magic bitboard technique
to implement a psuedo-legal move generator.

### Usage

#### Prerequisites

The included makefile is designed to run on Linux machines. The GNU GMP library
is a requirement, and should be installed on a Linux system locally before
compilation. Information on how to do this can be found [here](https://gmplib.org/).

#### User Input

The format used to denote entered moves is based around [FIDE standard algebraic
notation](https://www.chessprogramming.org/Algebraic_Chess_Notation#Standard_Algebraic_Notation_.28SAN.29).

The only exception to the FIDE notation is that `e.p.` **must** immediately
follow an en passant move without a space (in FIDE rules, this is optional).
Some valid example moves are
 - Move pawn to e4: `e4`
 - Move queen to e4 `Qe4`
 - Move pawn to d8 and promote to queen `d8Q`
 - Pawn takes piece on d6 `exd6`
 - Night takes piece on e4 `Nxe4`
 - Rook on rank 1 moves to a3 `R1a3`
 - Rook on d file moves to f8 `Rdf8`
 - Pawn takes a piece on d8 and promotes to queen `exd8Q`
 - Queen from h4 moves to e1 `Qh4e1`
 - Queen from h4 takes piece on e1 `Qh4xe1`
 - Pawn from e file takes piece on d6 in en passant `exd6e.p.`

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
full legality check is made in Board::MakeMove() to ensure that a move does not
put the moving player in check; illegal moves are unmade if they are found to
do this.
