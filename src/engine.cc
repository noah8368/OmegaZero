/* Noah Himed
 *
 * Implement the Engine type.
 *
 * Licensed under MIT License. Terms and conditions enclosed in "LICENSE.txt".
 */

#include "engine.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <queue>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

#include "bad_move.h"
#include "board.h"
#include "game.h"
#include "move.h"
#include "out_of_time.h"
#include "transp_table.h"

namespace omegazero {

using std::max;
using std::pair;
using std::queue;
using std::sort;
using std::unordered_map;
using std::vector;
using std::chrono::high_resolution_clock;

// Implement public member functions.

Engine::Engine(Board* board, S8 player_side, float search_time) {
  board_ = board;
  search_time_ = search_time;
  if (tolower(player_side) == 'w') {
    user_side_ = kWhite;
  } else if (tolower(player_side) == 'b') {
    user_side_ = kBlack;
  } else if (tolower(player_side) == 'r') {
    // Pick a random side for the engine to play as.
    srand(static_cast<int>(time(0)));
    user_side_ = static_cast<S8>(rand() % static_cast<int>(kNumPlayers));
  } else {
    throw invalid_argument("invalid side choice");
  }
}

auto Engine::GetBestMove(int* max_depth) -> Move {
  Move best_move;
  search_start_ = high_resolution_clock::now();
  for (int search_depth = 1;; ++search_depth) {
    try {
      Search(best_move, kWorstEval, kBestEval, search_depth);
    } catch (OutOfTime& e) {
      if (max_depth) {
        *max_depth = search_depth - 1;
      }
      break;
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

auto Engine::GenerateMoves(bool captures_only) const -> vector<Move> {
  S8 moving_piece;
  S8 moving_player = board_->GetPlayerToMove();
  S8 enemy_player = GetOtherPlayer(moving_player);
  S8 start_sq;
  Bitboard moving_pieces = board_->GetPiecesByType(kNA, moving_player);
  Bitboard remove_bad_sqs_mask;
  vector<Move> move_list;
  if (captures_only) {
    // Remove all squares not occupied by the enemy player when generating
    // captures only.
    remove_bad_sqs_mask = board_->GetPiecesByType(kNA, enemy_player);
  } else {
    remove_bad_sqs_mask = ~moving_pieces;
    AddCastlingMoves(move_list);
  }

  AddEpMoves(move_list, enemy_player, moving_player);
  // Loop over all pieces from the moving player.
  while (moving_pieces) {
    // Generate attack maps for each piece.
    start_sq = GetSqOfFirstPiece(moving_pieces);
    moving_piece = board_->GetPieceOnSq(start_sq);
    Bitboard attack_map = board_->GetAttackMap(moving_player, start_sq,
                                               moving_piece, moving_pieces);
    // Remove all invalid squares in the attack map.
    attack_map &= remove_bad_sqs_mask;
    AddMovesForPiece(move_list, attack_map, enemy_player, moving_player,
                     moving_piece, start_sq);
    RemoveFirstPiece(moving_pieces);
  }

  return move_list;
}

// Implement private member functions.

auto Engine::Search(Move& best_move, int alpha, int beta, int depth) -> int {
  CheckSearchTime();
  // Use the NegaMax algorithm to traverse the search tree.
  S8 game_status = GetGameStatus();
  if (game_status == kPlayerCheckmated) {
    return kWorstEval;
  } else if (game_status == kDraw || RepDetected()) {
    return kNeutralEval;
  } else if (depth == 0) {
    // Initiate the Quiescence search when maximum depth is reached.
    return /* QuiescenceSearch(alpha, beta); */ board_->Evaluate();
  }

  vector<Move> move_list = GenerateMoves();
  move_list = OrderMoves(move_list, depth);
  int eval;
  bool is_pv_node = false;
  queue<U64> saved_pos_rep_table = pos_rep_table_;
  // Iterate through all child nodes of the current position.
  for (const Move& move : move_list) {
    try {
      board_->MakeMove(move);
    } catch (BadMove& e) {
      // Ignore moves that put the player's king in check.
      continue;
    }

    AddBoardRep();
    // Check the transposition table for previously stored evaluations.
    if (!transp_table_.Access(board_, depth - 1, eval)) {
      eval = -Search(-beta, -alpha, depth - 1);
    }
    board_->UnmakeMove(move);
    pos_rep_table_ = saved_pos_rep_table;

    if (eval >= beta) {
      transp_table_.Update(board_, depth, beta);
      if (move.captured_piece == kNA) {
        RecordKillerMove(move, depth);
      }
      // Prune a subtree when a beta cutoff is detected.
      return beta;
    }
    if (eval > alpha) {
      is_pv_node = true;
      alpha = eval;
      best_move = move;
    }
  }

  if (is_pv_node) {
    transp_table_.Update(board_, depth, alpha, &best_move);
  } else {
    transp_table_.Update(board_, depth, alpha);
  }
  return alpha;
}

auto Engine::QuiescenceSearch(int alpha, int beta) -> int {
  S8 game_status = GetGameStatus();
  if (game_status == kPlayerCheckmated) {
    return kWorstEval;
  } else if (game_status == kDraw || RepDetected()) {
    return kNeutralEval;
  }

  // Establish a lower bound for the node evaluation (stand_pat_eval),
  // and perform a beta cutoff if this value exceeds beta.
  int stand_pat_eval = board_->Evaluate();
  if (stand_pat_eval >= beta) {
    return beta;
  }
  alpha = max(stand_pat_eval, alpha);

  // Generate captures only.
  vector<Move> move_list = GenerateMoves(true);
  move_list = OrderMoves(move_list);
  queue<U64> saved_pos_rep_table = pos_rep_table_;
  for (const Move& move : move_list) {
    try {
      board_->MakeMove(move);
    } catch (BadMove& e) {
      continue;
    }
    AddBoardRep();
    // Calculate the evalulation directly rather than using the transposition
    // table to avoid cache misses.
    stand_pat_eval = -QuiescenceSearch(-beta, -alpha);
    board_->UnmakeMove(move);
    pos_rep_table_ = saved_pos_rep_table;

    if (stand_pat_eval >= beta) {
      return beta;
    }
    alpha = max(stand_pat_eval, alpha);
  }
  return alpha;
}

auto Engine::OrderMoves(vector<Move> move_list, int depth) const
    -> vector<Move> {
  Move best_move;
  transp_table_.Access(board_, depth, best_move);

  vector<pair<Move, int>> ordered_capture_pairs;
  vector<Move> killer_moves;
  vector<Move> late_moves;
  vector<Move> ordered_moves;
  ordered_moves.reserve(move_list.size());
  for (const Move& move : move_list) {
    // Prioritize a move if it's the previously calculated best move of a node.
    if (move == best_move) {
      ordered_moves.push_back(move);
    } else if (move.captured_piece != kNA) {
      // Use the MVV-LVA heuristic to order captures.
      ordered_capture_pairs.emplace_back(
          move, kVictimSortVals[move.captured_piece] +
                    kAggressorSortVals[move.moving_piece]);
    } else if (IsKillerMove(move, depth)) {
      // Use the Killer Move heuristic to order quiet moves.
      killer_moves.push_back(move);
    } else {
      late_moves.push_back(move);
    }
  }
  // Sort captures by descending value of their MVV-LVA heuristic.
  sort(ordered_capture_pairs.begin(), ordered_capture_pairs.end(),
       [](const pair<Move, int>& lhs, const pair<Move, int>& rhs) {
         return lhs.second > rhs.second;
       });

  vector<Move> captures;
  captures.reserve(ordered_capture_pairs.size());
  for (const pair<Move, int>& capture_eval_pair : ordered_capture_pairs) {
    captures.push_back(capture_eval_pair.first);
  }

  // Place all hash moves first, followed by captures, and then all other
  // moves.
  ordered_moves.insert(ordered_moves.end(), captures.begin(), captures.end());
  ordered_moves.insert(ordered_moves.end(), killer_moves.begin(),
                       killer_moves.end());
  ordered_moves.insert(ordered_moves.end(), late_moves.begin(),
                       late_moves.end());
  return ordered_moves;
}

auto Engine::OrderMoves(vector<Move> move_list) const -> vector<Move> {
  Move best_move;

  vector<pair<Move, int>> ordered_capture_pairs;
  vector<Move> late_moves;
  for (const Move& move : move_list) {
    if (move.captured_piece == kNA) {
      late_moves.push_back(move);
    } else {
      // Use the MVV-LVA heuristic to order captures.
      ordered_capture_pairs.emplace_back(
          move, kVictimSortVals[move.captured_piece] +
                    kAggressorSortVals[move.moving_piece]);
    }
  }
  // Sort captures by descending value of their MVV-LVA heuristic.
  sort(ordered_capture_pairs.begin(), ordered_capture_pairs.end(),
       [](const pair<Move, int>& lhs, const pair<Move, int>& rhs) {
         return lhs.second > rhs.second;
       });

  vector<Move> captures;
  captures.reserve(ordered_capture_pairs.size());
  for (const pair<Move, int>& capture_eval_pair : ordered_capture_pairs) {
    captures.push_back(capture_eval_pair.first);
  }

  // Place all hash moves first, followed by captures, and then all other
  // moves.
  vector<Move> ordered_moves;
  ordered_moves.reserve(move_list.size());
  ordered_moves.insert(ordered_moves.end(), captures.begin(), captures.end());
  ordered_moves.insert(ordered_moves.end(), late_moves.begin(),
                       late_moves.end());
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
