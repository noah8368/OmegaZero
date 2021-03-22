# OmegaZero Chess Engine

###### Noah Himed
###### 21 March 2021

### Project Summary

OmegaZero is a terminal-based chess engine which allows a user to play against
an AI. The engine uses a board representation that takes advantage of both
bitboards and an 8x8 representation, and uses the magic bitboard technique
for move generation.

### Usage

The format used to denote entered moves is based around [FIDE standard algebraic
notation](https://www.chessprogramming.org/Algebraic_Chess_Notation#Standard_Algebraic_Notation_.28SAN.29).

The only exceptions to the FIDE notation is that `e.p.` **must** immediately
follow an en passant move without a space (in FIDE rules, this is optional).
