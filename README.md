# OmegaZero Chess Engine

###### Noah Himed
###### 21 March 2021

### Project Summary

OmegaZero is a terminal-based chess engine which allows a user to play against
an AI. The engine uses a board representation that takes advantage of both
bitboards and an 8x8 representation, and uses the magic bitboard technique
to implement a psuedo-legal move generator.

### Usage

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

#### Attack Sets

For non-sliding pieces (pawn, knight, and king), occupancy masks were computed
using the script `occ_masks.py` in the `magics` directory. The algorithm used
checks for off board movement by creating a "buffer zone" of two rings around
the board initialized to `-1` values.
