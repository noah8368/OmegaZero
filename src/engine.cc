/* Noah Himed
 *
 * Implement the Engine type.
 *
 * Licensed under MIT License. Terms and conditions enclosed in "LICENSE.txt".
 */

#include "engine.h"

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <unordered_map>
#include <vector>

#include "bad_move.h"
#include "board.h"
#include "game.h"
#include "move.h"

namespace omegazero {

using std::cin;
using std::cout;
using std::endl;
using std::unordered_map;
using std::vector;

// Implement public member functions.

Engine::Engine(Board* board, S8 player_side) {
  board_ = board;
  if (tolower(player_side) == 'w') {
    user_side_ = kWhite;
  } else if (tolower(player_side) == 'b') {
    user_side_ = kBlack;
  } else if (tolower(player_side) == 'r') {
    srand(static_cast<int>(time(0)));
    user_side_ = static_cast<S8>(rand() % static_cast<int>(kNumPlayers));
  } else {
    throw invalid_argument("invalid side choice");
  }
}

auto Engine::GetBestMove(int depth) -> Move {
  // Use the NegaMax algorithm to pick an optimal move.
  bool first_move = true;
  int max_score = INT32_MIN;
  int score;
  vector<Move> move_list = GenerateMoves();
  Move best_move = move_list[0];
  for (const Move& move : move_list) {
    try {
      board_->MakeMove(move);
    } catch (BadMove& e) {
      // Ignore moves that put the player's king in check.
      continue;
    }

    U64 board_hash = board_->GetBoardHash();
    if (transposition_table_.find(board_hash) == transposition_table_.end()) {
      // Flip sign of the evaluation for the next player to get the evaluation
      // relative to the current moving player.
      if (first_move) {
        // Prevent all moves from being pruned on the first iteration by setting
        // max_score to the largest possible value.
        score = -GetBestEval(depth - 1, INT32_MAX);
        first_move = false;
      } else {
        score = -GetBestEval(depth - 1, max_score);
      }
      transposition_table_[board_hash] = score;
    } else {
      // Use a tranposition table to avoid re-evaluating positions.
      score = transposition_table_[board_hash];
    }
    board_->UnmakeMove(move);

    max_score = (score > max_score) ? score : max_score;
  }
  return best_move;
}

auto Engine::GetGameStatus() -> S8 {
  // Check for checks, checkmates, and draws.
  vector<Move> move_list = GenerateMoves();
  bool no_legal_moves = true;
  for (const Move& move : move_list) {
    try {
      board_->MakeMove(move);
    } catch (BadMove& e) {
      // Ignore moves that leave the king in check.
      continue;
    }
    board_->UnmakeMove(move);
    no_legal_moves = false;
    break;
  }
  if (board_->KingInCheck()) {
    string player_name = GetPlayerStr(board_->GetPlayerToMove());
    if (no_legal_moves) {
      return kPlayerCheckmated;
    }
    return kPlayerInCheck;
  } else if (no_legal_moves) {
    // Indicate that the game has ended in a draw.
    return kDraw;
  }

  // Check for threefold and fivefold position repititions.
  U64 board_hash = board_->GetBoardHash();
  if (pos_rep_table_.find(board_hash) == pos_rep_table_.end()) {
    pos_rep_table_[board_hash] = 1;
  } else {
    ++pos_rep_table_[board_hash];
    S8 num_pos_rep = pos_rep_table_[board_hash];
    if (num_pos_rep == kNumMoveRepForOptionalDraw) {
      if (board_->GetPlayerToMove() == user_side_) {
        return kOptionalDraw;
      }
    } else if (num_pos_rep == kMaxMoveRep) {
      // Enforce a draw due to board reptititions.
      return kDraw;
    }
  }
  // Enforce the Fifty Move Rule.
  if (board_->GetHalfmoveClock() >= 2 * kHalfmoveClockLimit) {
    return kDraw;
  }
  return kPlayerToMove;
}

auto Engine::Perft(int depth) -> U64 {
  // Add to the node count if maximum depth is reached.
  if (depth == 0) {
    return 1ULL;
  }

  // Traverse a game tree of chess positions recursively to count leaf nodes.
  U64 node_count = 0;
  vector<Move> move_list = GenerateMoves();

  for (Move& move : move_list) {
    try {
      board_->MakeMove(move);
    } catch (BadMove& e) {
      // Ignore all moves that put the player's king in check.
      continue;
    }
    node_count += Perft(depth - 1);
    board_->UnmakeMove(move);
  }
  return node_count;
}

auto Engine::GenerateMoves() const -> vector<Move> {
  S8 moving_piece;
  S8 moving_player = board_->GetPlayerToMove();
  S8 enemy_player = GetOtherPlayer(moving_player);
  S8 start_sq;
  Bitboard moving_pieces = board_->GetPiecesByType(kNA, moving_player);
  Bitboard rm_friendly_pieces_mask = ~moving_pieces;
  std::vector<Move> move_list;
  // Loop over all pieces from the moving player.
  while (moving_pieces) {
    // Generate attack maps for each piece.
    start_sq = GetSqOfFirstPiece(moving_pieces);
    moving_piece = board_->GetPieceOnSq(start_sq);
    Bitboard attack_map =
        board_->GetAttackMap(moving_player, start_sq, moving_piece);
    // Remove all squares in the attack map occupied by friendly pieces.
    attack_map &= rm_friendly_pieces_mask;
    AddMovesForPiece(move_list, attack_map, enemy_player, moving_player,
                     moving_piece, start_sq);
    RemoveFirstPiece(moving_pieces);
  }
  AddCastlingMoves(move_list);
  AddEpMoves(move_list, enemy_player, moving_player);

  return move_list;
}

// Implement private member functions.

auto Engine::GetBestEval(int depth, int prev_max_score) -> int {
  // Use the NegaMax algorithm to traverse the search tree.
  if (depth == 0) {
    return board_->GetEval();
  }

  S8 game_status = GetGameStatus();
  if (game_status == kPlayerCheckmated) {
    // Return the worst possible score if the player has been checkmated. Use
    // -INT32_MAX rather than INT32_MIN to avoid integer overflow when
    // multipying by -1 to get the score relative to the other player.
    return -INT32_MAX;
  } else if (game_status == kDraw) {
    // Return the second worst possible score if the game is a draw.
    return 1 - INT32_MAX;
  } else if (game_status == kPlayerInCheck) {
    // Return the third worst possible score if the game is in check.
    return 2 - INT32_MAX;
  }

  bool first_move = true;
  int max_score = INT32_MIN;
  int score;
  vector<Move> move_list = GenerateMoves();
  for (const Move& move : move_list) {
    try {
      board_->MakeMove(move);
    } catch (BadMove& e) {
      // Ignore moves that put the player's king in check.
      continue;
    }

    U64 board_hash = board_->GetBoardHash();
    if (transposition_table_.find(board_hash) == transposition_table_.end()) {
      // Flip sign of the evaluation for the next player to get the evaluation
      // relative to the current moving player.
      if (first_move) {
        // Prevent all moves from being pruned on the first iteration by setting
        // max_score to the largest possible value.
        score = -GetBestEval(depth - 1, INT32_MAX);
        first_move = false;
      } else {
        score = -GetBestEval(depth - 1, max_score);
      }
      transposition_table_[board_hash] = score;
    } else {
      // Use a tranposition table to avoid re-evaluating positions.
      score = transposition_table_[board_hash];
    }
    board_->UnmakeMove(move);

    // Perform Alpha-beta pruning. Prune the subtree if the next player's move
    // is guaranteed to result in a better score than the current player's
    // maximum score. Here "max_score" is alpha and "prev_max_score" is beta.
    if (score >= prev_max_score) {
      return score;
    }
    max_score = (score > max_score) ? score : max_score;
  }
  return max_score;
}

auto Engine::AddCastlingMoves(vector<Move>& move_list) const -> void {
  if (board_->CastlingLegal(kQueenSide)) {
    Move queenside_castle;
    queenside_castle.castling_type = kQueenSide;
    move_list.push_back(queenside_castle);
  }
  if (board_->CastlingLegal(kKingSide)) {
    Move kingside_castle;
    kingside_castle.castling_type = kKingSide;
    move_list.push_back(kingside_castle);
  }
}

auto Engine::AddEpMoves(vector<Move>& move_list, S8 enemy_player,
                        S8 moving_player) const -> void {
  // Capture only diagonal squares to En Passent target sq in the direction of
  // movement.
  Bitboard potential_ep_pawns;
  S8 ep_target_sq = board_->GetEpTargetSq();
  if (enemy_player == kWhite) {
    potential_ep_pawns = kNonSliderAttackMaps[kWhitePawnCapture][ep_target_sq];
  } else {
    potential_ep_pawns = kNonSliderAttackMaps[kBlackPawnCapture][ep_target_sq];
  }

  if (ep_target_sq != kNA) {
    // Get the squares pawns can move from onto the en passent target square.
    // Note that because the target square is set, a single pawn push onto the
    // target square won't be possible, so this case can be safely ignored.
    Bitboard attack_map =
        potential_ep_pawns & board_->GetPiecesByType(kPawn, moving_player);
    if (attack_map) {
      Move ep;
      ep.is_ep = true;
      ep.moving_piece = kPawn;
      ep.target_sq = ep_target_sq;
      while (attack_map) {
        ep.start_sq = GetSqOfFirstPiece(attack_map);
        ep.captured_piece = kPawn;
        move_list.push_back(ep);
        RemoveFirstPiece(attack_map);
      }
    }
  }
}

auto Engine::AddMovesForPiece(vector<Move>& move_list, Bitboard attack_map,
                              S8 enemy_player, S8 moving_player,
                              S8 moving_piece, S8 start_sq) const -> void {
  // Loop over all set bits in the attack map, with each representing
  // one elligible target square for a move.
  S8 player_on_target_sq;
  S8 start_rank;
  S8 start_file;
  S8 target_rank;
  S8 target_file;
  while (attack_map) {
    Move move;
    move.moving_piece = moving_piece;
    move.start_sq = start_sq;
    move.target_sq = GetSqOfFirstPiece(attack_map);

    // Check for captures.
    player_on_target_sq = board_->GetPlayerOnSq(move.target_sq);
    if (player_on_target_sq == enemy_player) {
      move.captured_piece = board_->GetPieceOnSq(move.target_sq);
    }

    // Check for a new en passent target square and possible
    // pawn promotions.
    if (moving_piece == kPawn) {
      start_rank = GetRankFromSq(move.start_sq);
      start_file = GetFileFromSq(move.start_sq);
      target_rank = GetRankFromSq(move.target_sq);
      target_file = GetFileFromSq(move.target_sq);

      if (start_file == target_file && move.captured_piece != kNA) {
        // Ignore forward pawn pushes occupied by enemy pieces.
        goto GetNextMove;
      }

      if (moving_player == kWhite) {
        if (start_rank == kRank2 && target_rank == kRank4) {
          // Handle the case of White making a double pawn push.
          if (board_->DoublePawnPushLegal(target_file)) {
            move.new_ep_target_sq = GetSqFromRankFile(kRank3, target_file);
          } else {
            goto GetNextMove;
          }
        } else if (target_rank == kRank8) {
          // Add all pawn promotion moves.
          for (S8 piece = kKnight; piece <= kQueen; ++piece) {
            move.promoted_to_piece = piece;
            move_list.push_back(move);
          }
          // Move onto another target square to make a move for, because we've
          // already added a fully formed set of moves encompassing all possible
          // pawn promotions.
          goto GetNextMove;
        }
      } else if (moving_player == kBlack) {
        if (start_rank == kRank7 && target_rank == kRank5) {
          // Handle the case of Black making a double pawn push.
          if (board_->DoublePawnPushLegal(target_file)) {
            move.new_ep_target_sq = GetSqFromRankFile(kRank6, target_file);
          } else {
            goto GetNextMove;
          }
        } else if (target_rank == kRank1) {
          // Add all pawn promotion moves.
          for (S8 piece = kKnight; piece <= kQueen; ++piece) {
            move.promoted_to_piece = piece;
            move_list.push_back(move);
          }
          // Move onto another target square to make a move for, because we've
          // already added a fully formed set of moves encompassing all possible
          // pawn promotions.
          goto GetNextMove;
        }
      }
    }
    move_list.push_back(move);
  GetNextMove:
    // Remove a piece from the attack map of the moving piece.
    RemoveFirstPiece(attack_map);
  }
}

}  // namespace omegazero
