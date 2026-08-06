/* Noah Himed
 *
 * Define and implement the Move type, which stores information necessary to
 * update the board representation from a piece movement.
 *
 * Licensed under MIT License. Terms and conditions enclosed in "LICENSE.txt".
 */

#ifndef OMEGAZERO_SRC_MOVE_H
#define OMEGAZERO_SRC_MOVE_H

#include <cstdint>

namespace omegazero {

typedef int8_t S8;
typedef int16_t S16;

constexpr S8 kNA = -1;

struct Move {
  auto operator==(const Move& rhs) const -> bool {
    return (castling_type != kNA && castling_type == rhs.castling_type) ||
           (moving_piece != kNA && captured_piece == rhs.captured_piece &&
            moving_piece == rhs.moving_piece && start_sq == rhs.start_sq &&
            target_sq == rhs.target_sq && is_ep == rhs.is_ep);
  }
  auto operator!=(const Move& rhs) const -> bool { return !(*this == rhs); }

  // True for a default/unset move: neither a piece move nor a castle. (A
  // castling move has moving_piece == kNA but is a real move.)
  auto IsEmpty() const -> bool {
    return moving_piece == kNA && castling_type == kNA;
  }

  bool is_ep = false;
  S8 captured_piece = kNA;
  // Indicate if the move is not a castling move, queenside castling, or
  // kingside castling.
  S8 castling_type = kNA;
  S8 moving_piece = kNA;
  // Indicate a new en passent target square when a double pawn push
  // is made by either player.
  S8 new_ep_target_sq = kNA;
  S8 promoted_to_piece = kNA;
  S8 start_sq = kNA;
  S8 target_sq = kNA;
};

}  // namespace omegazero

#endif  // OMEGAZERO_SRC_MOVE_H
