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

using namespace std;

vector<Move> Engine::GenerateMoves(const int& moving_player,
                                   const Board& board) const {
  int debug_count = 0;
  
  Bitboard attack_map;
  Bitboard moving_pieces = board.GetPiecesByType(kNA, moving_player);
  Bitboard rm_moving_pieces_mask = ~moving_pieces;
  int other_player = GetOtherPlayer(moving_player);
  int moving_piece;
  int start_sq;
  // Keep track of the player occupying a target square.
  int target_player;
  int target_rank;
  int target_file;
  vector<Move> move_list;
  // Loop over all set bits in the bitboard of pieces from the moving player.
  while (moving_pieces) {

    debug_count++;
    // cout << debug_count << endl;

    // Generate attack maps for each piece belong to the moving player.
    start_sq = GetSqOfFirstPiece(moving_pieces);
    moving_piece = board.GetPieceOnSq(start_sq);
    attack_map = board.GetAttackMap(moving_player, start_sq, moving_piece);
    // Remove all squares in the attack map occupied by friendly pieces.
    attack_map &= rm_moving_pieces_mask;
    // Loop over all set bits in the attack map, with each representing
    // one elligible target square for a move.
    while (attack_map) {
      Move move;
      move.moving_piece = moving_piece;
      move.moving_player = moving_player;
      move.start_sq = start_sq;
      move.target_sq = GetSqOfFirstPiece(attack_map);
      target_player = board.GetPlayerOnSq(move.target_sq);

      // Check for captures.
      if (target_player == other_player) {
        move.captured_piece = board.GetPieceOnSq(move.target_sq);
      }

      // Check for a new en passent target square and possible
      // pawn promotions.
      if (moving_piece == kPawn) {
        target_rank = GetRankFromSq(move.target_sq);
        target_file = GetFileFromSq(move.target_sq);
        if (moving_player == kWhite) {
          if (target_rank == kRank4
              && board.DoublePawnPushLegal(kWhite, target_file)) {
            move.new_ep_target_sq = GetSqFromRankFile(kRank3,
                                                      target_file);
          } else if (target_rank == kRank8) {
            for(int piece = kKnight; piece <= kQueen; ++piece) {
              move.promoted_to_piece = piece;
              move_list.push_back(move);
            }
            continue;
          }
        } else if (moving_player == kBlack) {
          if (target_rank == kRank5
              && board.DoublePawnPushLegal(kWhite, target_file)) {
            move.new_ep_target_sq = GetSqFromRankFile(kRank6,
                                                      target_file);
          } else if (target_rank == kRank1) {
            for (int piece = kKnight; piece <= kQueen; ++piece) {
              move.promoted_to_piece = piece;
              move_list.push_back(move);
            }
            continue;
          }
        }
      }
      move_list.push_back(move);
      // Remove the piece represented by the least significant set bit.
      attack_map &= attack_map - 1;
    }
    // Remove the piece represented by the least significant set bit.
    moving_pieces &= moving_pieces - 1;
  }

  // Check for possible en passents.
  int ep_target_sq = board.GetEpTargetSq();
  if (ep_target_sq != kNA) {
    // Get the squares pawns can move from onto the en passent target square.
    // Note that because the target square is set, a single pawn push onto the
    // target square won't be possible, so this case can be safely ignored.
    attack_map = board.GetAttackMap(other_player, ep_target_sq, kPawn)
                  & board.GetPiecesByType(kPawn, moving_player);
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
        int ep_rank;
        if (moving_player == kWhite) {
          ep_rank = kRank5;
        } else if (moving_player == kBlack) {
          ep_rank = kRank4;
        }
        int ep_target_sq_file = GetFileFromSq(ep_target_sq);
        int ep_file;
        // Compute the start square for the leftmost en passent move.
        ep_file = ep_target_sq_file - 1;
        ep.start_sq = GetSqFromRankFile(ep_rank, ep_file);
        move_list.push_back(ep);
        // Compute the start square for the rightmost en passent move.
        ep_file = ep_target_sq_file + 1;
        ep.start_sq = GetSqFromRankFile(ep_rank, ep_file);
        move_list.push_back(ep);
      }
    }
  }
  // Check for possible castling moves.
  if (board.CastlingLegal(moving_player, kQueenSide)) {
    Move queenside_castle;
    queenside_castle.castling_type = kKingSide;
    move_list.push_back(queenside_castle);
  }
  if (board.CastlingLegal(moving_player, kKingSide)) {
    Move kingside_castle;
    kingside_castle.castling_type = kKingSide;
    move_list.push_back(kingside_castle);
  }
  return move_list;
}
