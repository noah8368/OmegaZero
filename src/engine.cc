/* Noah Himed
 *
 * Implement the Engine type.
 *
 * Licensed under MIT License. Terms and conditions enclosed in "LICENSE.txt".
 */

#include "engine.h"

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

#include "bad_move.h"
#include "board.h"
#include "game.h"
#include "move.h"

namespace omegazero {

using std::pair;
using std::sort;
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

auto Engine::GetBestMove() -> Move {
  // Use the NegaMax algorithm to traverse the search tree. Use -INT32_MAX
  // rather than INT32_MIN to avoid integer overflow when multipying by -1.
  int best_score = -INT32_MAX;
  int score;
  vector<Move> move_list = GenerateMoves();
  move_list = GetOrderedMoves(move_list);
  Move best_move = move_list[0];
  for (const Move& move : move_list) {
    try {
      board_->MakeMove(move);
    } catch (BadMove& e) {
      // Ignore moves that put the player's king in check.
      continue;
    }

    U64 board_hash = board_->GetBoardHash();
    PosInfo known_pos;
    if (transposition_table_.find(board_hash) == transposition_table_.end()) {
      // Flip sign of the evaluation for the next player to get the
      // evaluation relative to the current moving player.
      score = -GetBestEval(kSearchDepth - 1, best_score, -best_score);
      known_pos.pos_depth = kSearchDepth;
      known_pos.pos_score = score;
      transposition_table_[board_hash] = known_pos;
    } else {
      // Use the score in the tranposition table to avoid re-evaluating
      // positions.
      score = transposition_table_[board_hash].pos_score;
    }
    board_->UnmakeMove(move);

    // Track the value of the best possible move for the current player.
    if (score > best_score) {
      best_score = score;
      best_move = move;
    }
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

auto Engine::GetBestEval(int depth, int alpha, int beta) -> int {
  // Use the NegaMax algorithm to traverse the search tree.
  S8 game_status = GetGameStatus();
  if (game_status == kPlayerCheckmated) {
    // Return the worst possible score if the player has been checkmated. Use
    // -INT32_MAX rather than INT32_MIN to avoid integer overflow when
    // multipying by -1 to get the score relative to the other player.
    return -INT32_MAX;
  } else if (game_status == kDraw) {
    // Return the second worst possible score if the game is a draw.
    return 1 - INT32_MAX;
  } else if (depth == 0) {
    return board_->GetEval();
  }

  int score;
  vector<Move> move_list = GenerateMoves();
  move_list = GetOrderedMoves(move_list);
  for (const Move& move : move_list) {
    try {
      board_->MakeMove(move);
    } catch (BadMove& e) {
      // Ignore moves that put the player's king in check.
      continue;
    }

    U64 board_hash = board_->GetBoardHash();
    PosInfo known_pos;
    if (transposition_table_.find(board_hash) == transposition_table_.end()) {
    ModifyTranspositionTable:
      // Flip sign of the evaluation for the next player to get the evaluation
      // relative to the current moving player.
      score = -GetBestEval(depth - 1, -beta, -alpha);
      known_pos.pos_depth = depth;
      known_pos.pos_score = score;
      transposition_table_[board_hash] = known_pos;
    } else {
      known_pos = transposition_table_[board_hash];
      if (known_pos.pos_depth <= depth) {
        // Use the score in the tranposition table to avoid re-evaluating
        // positions.
        score = transposition_table_[board_hash].pos_score;
      } else {
        // Update the transposition table's score with an evaluation done at a
        // higher depth, implying a deeper search into the future.
        goto ModifyTranspositionTable;
      }
    }
    board_->UnmakeMove(move);

    // Perform Alpha-beta pruning. Prune the subtree if the next player's move
    // is guaranteed to result in a better score than the current player's
    // maximum score.
    if (score >= beta) {
      return beta;
    }
    // Track the value of the best possible move for the current player.
    if (score > alpha) {
      alpha = score;
    }
  }
  return alpha;
}

auto Engine::GetOrderedMoves(vector<Move> move_list) const -> vector<Move> {
  vector<pair<Move, int>> capture_score_pairs;
  vector<Move> quiet_moves;
  for (const Move& move : move_list) {
    if (move.captured_piece == kNA) {
      quiet_moves.push_back(move);
    } else {
      // Use the MVV-LVA heuristic to score captures.
      capture_score_pairs.emplace_back(
          move, kVictimWeights[move.captured_piece] +
                    kAggressorWeights[move.moving_piece]);
    }
  }

  // Sort captures by descending value of their MVV-LVA heuristic.
  sort(capture_score_pairs.begin(), capture_score_pairs.end(),
       [](const pair<Move, int>& lhs, const pair<Move, int>& rhs) {
         return lhs.second > rhs.second;
       });
  vector<Move> captures;
  captures.reserve(capture_score_pairs.size());
  for (const pair<Move, int>& capture_score_pair : capture_score_pairs) {
    captures.push_back(capture_score_pair.first);
  }

  // Place all captures first in the ordered move list, followed by quiet
  // moves.
  vector<Move> ordered_moves;
  ordered_moves.reserve(move_list.size());
  ordered_moves.insert(ordered_moves.begin(), captures.begin(), captures.end());
  ordered_moves.insert(ordered_moves.begin() + captures.size(),
                       quiet_moves.begin(), quiet_moves.end());
  return ordered_moves;
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
          // already added a fully formed set of moves encompassing all
          // possible pawn promotions.
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
          // already added a fully formed set of moves encompassing all
          // possible pawn promotions.
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
