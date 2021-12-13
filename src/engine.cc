/* Noah Himed
 *
 * Implement the Engine type.
 *
 * Licensed under MIT License. Terms and conditions enclosed in "LICENSE.txt".
 */

#include "engine.h"

#include <cmath>
#include <cstdint>
#include <vector>

#include "board.h"
#include "move.h"

namespace omegazero {

auto Engine::GenerateMoves(S8 moving_player, const Board& board) const
    -> std::vector<Move> {
  Bitboard moving_pieces = board.GetPiecesByType(kNA, moving_player);
  Bitboard rm_friendly_pieces_mask = ~moving_pieces;
  S8 other_player = GetOtherPlayer(moving_player);
  std::vector<Move> move_list;

  // Loop over all pieces from the moving player.
  while (moving_pieces) {
    // Generate attack maps for each piece.
    S8 start_sq = GetSqOfFirstPiece(moving_pieces);
    S8 moving_piece = board.GetPieceOnSq(start_sq);
    Bitboard attack_map =
        board.GetAttackMap(moving_player, start_sq, moving_piece);
    // Remove all squares in the attack map occupied by friendly pieces.
    attack_map &= rm_friendly_pieces_mask;
    AddMovesForPiece(attack_map, board, move_list, other_player, start_sq,
                     moving_piece, moving_player);
    // Remove the piece from the map of all available moving pieces.
    moving_pieces &= moving_pieces - 1;
  }

  AddCastlingMoves(moving_player, board, move_list);
  AddEpMoves(moving_player, other_player, board, move_list);

  return move_list;
}

// Implement private member functions.

auto Engine::AddCastlingMoves(S8 moving_player, const Board& board,
                              std::vector<Move>& move_list) const -> void {
  if (board.CastlingLegal(moving_player, kQueenSide)) {
    Move queenside_castle;
    queenside_castle.castling_type = kQueenSide;
    move_list.push_back(queenside_castle);
  }
  if (board.CastlingLegal(moving_player, kKingSide)) {
    Move kingside_castle;
    kingside_castle.castling_type = kKingSide;
    move_list.push_back(kingside_castle);
  }
}

auto Engine::AddEpMoves(S8 moving_player, S8 other_player, const Board& board,
                        std::vector<Move>& move_list) const -> void {
  S8 ep_target_sq = board.GetEpTargetSq();
  if (ep_target_sq != kNA) {
    // Get the squares pawns can move from onto the en passent target square.
    // Note that because the target square is set, a single pawn push onto the
    // target square won't be possible, so this case can be safely ignored.
    Bitboard attack_map =
        board.GetAttackMap(other_player, ep_target_sq, kPawn) &
        board.GetPiecesByType(kPawn, moving_player);
    if (attack_map) {
      Move ep;
      ep.is_ep = true;
      ep.moving_piece = kPawn;
      ep.moving_player = moving_player;
      ep.target_sq = ep_target_sq;
      // Check if one bit is set in the attack map to handle the case of
      // one pawn elligible to en passent.
      if (!(attack_map & (attack_map - 1))) {
        ep.start_sq = GetSqOfFirstPiece(attack_map);
        move_list.push_back(ep);
        // Assume two pawns are elligible to en passent in this case.
      } else {
        S8 ep_rank = kNA;
        if (moving_player == kWhite) {
          ep_rank = kRank5;
        } else if (moving_player == kBlack) {
          ep_rank = kRank4;
        }
        S8 ep_target_sq_file = GetFileFromSq(ep_target_sq);
        S8 ep_file;
        // Compute the start square for the leftmost en passent move.
        ep_file = static_cast<S8>(ep_target_sq_file - 1);
        ep.start_sq = GetSqFromRankFile(ep_rank, ep_file);
        move_list.push_back(ep);
        // Compute the start square for the rightmost en passent move.
        ep_file = static_cast<S8>(ep_target_sq_file + 1);
        ep.start_sq = GetSqFromRankFile(ep_rank, ep_file);
        move_list.push_back(ep);
      }
    }
  }
}

auto Engine::AddMovesForPiece(Bitboard& attack_map, const Board& board,
                              std::vector<Move>& move_list, S8 other_player,
                              S8 start_sq, S8 moving_piece,
                              S8 moving_player) const -> void {
  // Loop over all set bits in the attack map, with each representing
  // one elligible target square for a move.
  while (attack_map) {
    Move move;
    move.moving_piece = moving_piece;
    move.moving_player = moving_player;
    move.start_sq = start_sq;
    move.target_sq = GetSqOfFirstPiece(attack_map);
    S8 target_player = board.GetPlayerOnSq(move.target_sq);

    // Check for captures.
    if (target_player == other_player) {
      move.captured_piece = board.GetPieceOnSq(move.target_sq);
    }

    // Check for a new en passent target square and possible
    // pawn promotions.
    if (moving_piece == kPawn) {
      S8 target_rank = GetRankFromSq(move.target_sq);
      S8 target_file = GetFileFromSq(move.target_sq);
      if (moving_player == kWhite) {
        if (target_rank == kRank4 &&
            board.DoublePawnPushLegal(kWhite, target_file)) {
          move.new_ep_target_sq = GetSqFromRankFile(kRank3, target_file);
        } else if (target_rank == kRank8) {
          for (S8 piece = kKnight; piece <= kQueen; ++piece) {
            move.promoted_to_piece = piece;
            move_list.push_back(move);
          }
          continue;
        }
      } else if (moving_player == kBlack) {
        if (target_rank == kRank5 &&
            board.DoublePawnPushLegal(kWhite, target_file)) {
          move.new_ep_target_sq = GetSqFromRankFile(kRank6, target_file);
        } else if (target_rank == kRank1) {
          for (S8 piece = kKnight; piece <= kQueen; ++piece) {
            move.promoted_to_piece = piece;
            move_list.push_back(move);
          }
          continue;
        }
      }
    }
    move_list.push_back(move);
    // Remove a piece from the attack map of the moving piece.
    attack_map &= attack_map - 1;
  }
}

}  // namespace omegazero
