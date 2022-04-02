/* Noah Himed
 *
 * Implement the Engine type.
 *
 * Licensed under MIT License. Terms and conditions enclosed in "LICENSE.txt".
 */

#include "engine.h"

#include <algorithm>
#include <chrono>
#include <cmath>
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
#include "transposition_table.h"

namespace omegazero {

using std::max;
using std::min;
using std::pair;
using std::queue;
using std::runtime_error;
using std::sort;
using std::unordered_map;
using std::vector;
using std::chrono::high_resolution_clock;

// Store values used for the MVV-LVA heuristic. Piece order in array is
// pawn, knight, bishop, rook, queen, king.
constexpr int kAggressorSortVals[kNumPieceTypes] = {-1, -2, -3, -4, -5, -6};
constexpr int kVictimSortVals[kNumPieceTypes] = {10, 20, 30, 40, 50, 60};

// Implement public member functions.

Engine::Engine(Board* board, S8 player_side, float search_time) {
  board_ = board;

  constexpr float kMinSearchTime = 0.1f;
  if (search_time < kMinSearchTime) {
    throw invalid_argument("Search time must be at least 0.1s");
  }
  search_time_ = search_time;

  if (tolower(player_side) == 'w') {
    user_side_ = kWhite;
  } else if (tolower(player_side) == 'b') {
    user_side_ = kBlack;
  } else if (tolower(player_side) == 'r') {
    // Pick a random side for the user to play as.
    srand(static_cast<int>(time(0)));
    user_side_ = static_cast<S8>(rand() % static_cast<int>(kNumPlayers));
  } else {
    throw invalid_argument("invalid side choice");
  }
}

auto Engine::GetBestMove() -> Move {
  transposition_table_.Clear();
  board_->ClearPawnTable();
  Move best_move;
  Move move;
  board_->SavePos();
  constexpr int kRootNodePly = 0;
  // Initialize the first guess for the MTD(f) algorithm, f, with a search to
  // a depth of one.
  int f = MtdfSearch(0, 1, kRootNodePly, best_move);

  // Perform an MTD(f) search inside an iterative deepening framework.
  search_start_ = high_resolution_clock::now();
  int search_depth = 2;
  for (; search_depth <= kSearchLimit; ++search_depth) {
    try {
      f = MtdfSearch(f, search_depth, kRootNodePly, move);
      if (move.moving_piece != kNA || move.castling_type != kNA) {
        best_move = move;
      }
    } catch (OutOfTime& e) {
      break;
    }
  }

  search_depth =
      (search_depth == kSearchLimit) ? kSearchLimit : search_depth - 1;
  cout << "SEARCH DEPTH: " << search_depth << endl;
  board_->ResetPos();
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
  constexpr S8 kHalfmoveClockLimit = 75;
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
    Bitboard attack_map =
        board_->GetAttackMap(moving_player, start_sq, moving_piece);
    // Remove all invalid squares in the attack map.
    attack_map &= remove_bad_sqs_mask;
    AddMovesForPiece(move_list, attack_map, enemy_player, moving_player,
                     moving_piece, start_sq);
    RemoveFirstPiece(moving_pieces);
  }

  return move_list;
}

// Implement private member functions.

auto Engine::MtdfSearch(int f, int d, int ply, Move& best_move) -> int {
  // Perform the MTD(f) algorithm, where f is the first guess for best value,
  // d is the depth to loop for, and g is the current guess.
  int g = f;
  int upper_bound = kBestEval;
  int lower_bound = kWorstEval;
  int beta;
  while (lower_bound < upper_bound) {
    if (g == lower_bound) {
      beta = g + 1;
    } else {
      beta = g;
    }
    g = NegamaxSearch(best_move, beta - 1, beta, d, ply, true, d != 1);
    if (g < beta) {
      upper_bound = g;
    } else {
      lower_bound = g;
    }
  }
  return g;
}

auto Engine::NegamaxSearch(Move& pv_move, int alpha, int beta, int depth,
                           int ply, bool null_move_allowed, bool check_time)
    -> int {
  if (check_time) {
    CheckSearchTime();
  }

  int orig_alpha = alpha;
  int transposition_table_stored_eval;
  S8 node_type;
  // Check the transposition table for previously stored evaluations.
  if (transposition_table_.Access(board_, depth,
                                  transposition_table_stored_eval, node_type)) {
    if (node_type == kPvNode) {
      pv_move = transposition_table_.GetHashMove(board_);
      return transposition_table_stored_eval;
    }
    if (node_type == kCutNode) {
      alpha = max(alpha, transposition_table_stored_eval);
    } else if (node_type == kAllNode) {
      beta = min(beta, transposition_table_stored_eval);
    }

    if (alpha >= beta) {
      return transposition_table_stored_eval;
    }
  }

  S8 game_status = GetGameStatus();
  if (game_status == kPlayerCheckmated) {
    return kWorstEval;
  }
  if (game_status == kDraw || RepDetected()) {
    return kNeutralEval;
  }
  if (depth <= 0) {
    // Initiate the Quiescence search when maximum depth is reached.
    return QuiescenceSearch(alpha, beta);
  }

  bool at_pv_node = transposition_table_.PosIsPvNode(board_);

  // Compute the depth reduction value (R) for Null-Move pruning.
  constexpr int kNullMoveDepthMin = 4;
  constexpr int kDepthReductionIncreaseBoundary = 6;
  int R = (depth > kDepthReductionIncreaseBoundary) ? 3 : 2;
  if (depth >= kNullMoveDepthMin && null_move_allowed && !at_pv_node &&
      ZugzwangUnlikely() && !board_->KingInCheck()) {
    board_->MakeNullMove();
    int null_move_eval = -NegamaxSearch(-beta, -alpha, depth - R - 1, ply + 1,
                                        false, check_time);
    board_->UnmakeNullMove();
    if (null_move_eval >= beta) {
      // Perform a null-move prune.
      return beta;
    }
  }

  // Store the number of moves to begin searching at full depth during Late Move
  // Reduction, the number of early moves.
  constexpr S8 kNumEarlyMoves = 3;
  constexpr S8 kMinReductionDepth = 3;
  // Use the Negamax algorithm to traverse the search tree.
  vector<Move> move_list = GenerateMoves();
  move_list = OrderMoves(move_list, ply);
  queue<U64> saved_pos_history = pos_history_;
  Move best_move;
  Move move;
  int best_eval = kWorstEval;
  int search_eval;
  int depth_reduction;
  // Iterate through all child nodes of the current position.
  size_t num_moves = move_list.size();
  for (size_t move_idx = 0; move_idx < num_moves; ++move_idx) {
    // cout << "MOVE: " << move_idx << endl;
    move = move_list[move_idx];
    try {
      board_->MakeMove(move);
    } catch (BadMove& e) {
      // Ignore moves that put the player's king in check.
      continue;
    }

    AddPosToHistory();
    if (move_idx >= kNumEarlyMoves && !at_pv_node &&
        move.captured_piece == kNA && move.promoted_to_piece == kNA &&
        !board_->KingInCheck() && depth >= kMinReductionDepth) {
      // Perform Late Move Reduction.
      depth_reduction =
          static_cast<int>(sqrt(static_cast<double>(depth - 1)) +
                           sqrt(static_cast<double>(move_idx - 1)));
      search_eval = -NegamaxSearch(-beta, -alpha, depth - depth_reduction - 1,
                                   ply + 1, true, check_time);
      if (search_eval > alpha) {
        // Perform a re-search at full depth.
        search_eval =
            -NegamaxSearch(-beta, -alpha, depth - 1, ply + 1, true, check_time);
      }
    } else {
      // Search at full depth.
      search_eval =
          -NegamaxSearch(-beta, -alpha, depth - 1, ply + 1, true, check_time);
    }
    board_->UnmakeMove(move);
    pos_history_.swap(saved_pos_history);
    if (search_eval > best_eval) {
      best_move = move;
      pv_move = best_move;
      best_eval = search_eval;
    }
    alpha = max(alpha, search_eval);
    if (alpha >= beta) {
      if (move.captured_piece == kNA) {
        RecordKillerMove(move, ply);
      }
      // Prune a subtree when a beta cutoff is detected.
      break;
    }
  }

  // Store a searched node in the transposition table.
  if (best_eval <= orig_alpha) {
    transposition_table_.Update(board_, depth, best_eval, kAllNode);
  } else if (best_eval >= beta) {
    transposition_table_.Update(board_, depth, best_eval, kCutNode, best_move);
  } else {
    transposition_table_.Update(board_, depth, best_eval, kPvNode, best_move);
  }

  return best_eval;
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

  if (!InEndgame()) {
    // Perfrom delta pruning if not in the endgame.
    const int kDelta = kPieceVals[kQueen];
    if (stand_pat_eval < alpha - kDelta) {
      // If the biggest possible material swing won't increase alpha, don't
      // bother searching any captures.
      return alpha;
    }
  }

  // Generate captures only.
  vector<Move> move_list = GenerateMoves(true);
  move_list = OrderMoves(move_list);
  queue<U64> saved_pos_rep_table = pos_history_;
  for (const Move& move : move_list) {
    try {
      board_->MakeMove(move);
    } catch (BadMove& e) {
      continue;
    }
    AddPosToHistory();
    // Calculate the evalulation directly rather than using the transposition
    // table to avoid cache misses.
    stand_pat_eval = -QuiescenceSearch(-beta, -alpha);
    board_->UnmakeMove(move);
    pos_history_ = saved_pos_rep_table;

    if (stand_pat_eval >= beta) {
      return beta;
    }
    alpha = max(stand_pat_eval, alpha);
  }

  return alpha;
}

auto Engine::OrderMoves(vector<Move> move_list, int ply) const -> vector<Move> {
  Move hash_move = transposition_table_.GetHashMove(board_);

  vector<pair<Move, int>> ordered_capture_pairs;
  vector<Move> silent_moves;
  vector<Move> killer_moves;
  vector<Move> ordered_moves;
  ordered_moves.reserve(move_list.size());
  for (const Move& move : move_list) {
    // Prioritize a move if it's the previously calculated best move of a
    // node.
    if (move == hash_move) {
      ordered_moves.push_back(move);
    } else if (move.captured_piece != kNA) {
      // Use the MVV-LVA heuristic to order captures.
      ordered_capture_pairs.emplace_back(
          move, kVictimSortVals[move.captured_piece] +
                    kAggressorSortVals[move.moving_piece]);
    } else if (IsKillerMove(move, ply)) {
      // Use the Killer Move heuristic to order quiet moves.
      killer_moves.push_back(move);
    } else {
      // Use the history heuristic to order silent, non-killer moves.
      silent_moves.push_back(move);
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

  // Place all hash moves first, followed by captures, then killer moves, and
  // finally all silent, non-killer moves.
  ordered_moves.insert(ordered_moves.end(), captures.begin(), captures.end());
  ordered_moves.insert(ordered_moves.end(), killer_moves.begin(),
                       killer_moves.end());
  ordered_moves.insert(ordered_moves.end(), silent_moves.begin(),
                       silent_moves.end());
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

  // Place captures first, followed by all other moves.
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
