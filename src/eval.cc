/* Noah Himed
 *
 * Define the values used to compute static board evaluations.
 *
 * Licensed under MIT License. Terms and conditions enclosed in "LICENSE.txt".
 */

#include "board.h"

namespace omegazero {

// Store piece values expressed in centipawns for evaluation function. Piece
// order in array is pawn, knight, bishop, rook, queen, king.
const int kPieceVals[kNumPieceTypes] = {100, 320, 330, 500, 900, 20000};

// Define positional piece bonuses; note that these values are defined for
// White, Black uses mirrored values.
const int kPieceSqTable[kNumPieceTypes][kNumSq] = {
    // Define the pawn piece square table.
    {0,  0,  0,   0,  0,  0,   0,  0,  5,  10, 10, -20, -20, 10, 10, 5,
     5,  -5, -10, 0,  0,  -10, -5, 5,  0,  0,  0,  20,  20,  0,  0,  0,
     5,  5,  10,  25, 25, 10,  5,  5,  10, 10, 20, 30,  30,  20, 10, 10,
     50, 50, 50,  50, 50, 50,  50, 50, 0,  0,  0,  0,   0,   0,  0,  0},
    // Define the knight piece square table.
    {-50, -40, -30, -30, -30, -30, -40, -50, -40, -20, 0,   5,   5,
     0,   -20, -40, -30, 5,   10,  15,  15,  10,  5,   -30, -30, 0,
     15,  20,  20,  15,  0,   -30, -30, 5,   15,  20,  20,  15,  5,
     -30, -30, 0,   10,  15,  15,  10,  0,   -30, -40, -20, 0,   0,
     0,   0,   -20, -40, -50, -40, -30, -30, -30, -30, -40, -50},
    // Define the bishop piece square table.
    {-20, -10, -10, -10, -10, -10, -10, -20, -10, 5,   0,   0,   0,
     0,   5,   -10, -10, 10,  10,  10,  10,  10,  10,  -10, -10, 0,
     10,  10,  10,  10,  0,   -10, -10, 5,   5,   10,  10,  5,   5,
     -10, -10, 0,   5,   10,  10,  5,   0,   -10, -10, 0,   0,   0,
     0,   0,   0,   -10, -20, -10, -10, -10, -10, -10, -10, -20},
    // Define the rook piece square table.
    {0,  0,  0,  5,  5,  0,  0,  0,  -5, 0, 0, 0, 0, 0, 0, -5,
     -5, 0,  0,  0,  0,  0,  0,  -5, -5, 0, 0, 0, 0, 0, 0, -5,
     -5, 0,  0,  0,  0,  0,  0,  -5, -5, 0, 0, 0, 0, 0, 0, -5,
     5,  10, 10, 10, 10, 10, 10, 5,  0,  0, 0, 0, 0, 0, 0, 0},
    // Define the queen piece square table.
    {-20, -10, -10, -5,  -5,  -10, -10, -20, -10, 0,   5,   0,  0,
     0,   0,   -10, -10, 5,   5,   5,   5,   5,   0,   -10, 0,  0,
     5,   5,   5,   5,   0,   -5,  -5,  0,   5,   5,   5,   5,  0,
     -5,  -10, 0,   5,   5,   5,   5,   0,   -10, -10, 0,   0,  0,
     0,   0,   0,   -10, -20, -10, -10, -5,  -5,  -10, -10, -20},
    // Define the king (middle game) piece square table.
    {20,  30,  10,  0,   0,   10,  30,  20,  20,  20,  0,   0,   0,
     0,   20,  20,  -10, -20, -20, -20, -20, -20, -20, -10, -20, -30,
     -30, -40, -40, -30, -30, -20, -30, -40, -40, -50, -50, -40, -40,
     -30, -30, -40, -40, -50, -50, -40, -40, -30, -30, -40, -40, -50,
     -50, -40, -40, -30, -30, -40, -40, -50, -50, -40, -40, -30}};

// Define positional bonuses for the king in the endgame.
const int kKingEndgamePieceSqTable[kNumSq] = {
    -50, -30, -30, -30, -30, -30, -30, -50, -30, -30, 0,   0,   0,
    0,   -30, -30, -30, -10, 20,  30,  30,  20,  -10, -30, -30, -10,
    30,  40,  40,  30,  -10, -30, -30, -10, 30,  40,  40,  30,  -10,
    -30, -30, -10, 20,  30,  30,  20,  -10, -30, -30, -20, -10, 0,
    0,   -10, -20, -30, -50, -40, -30, -20, -20, -30, -40, -50};

}  // namespace omegazero