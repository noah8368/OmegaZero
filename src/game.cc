/* Noah Himed
 *
 * Implement the Game type.
 *
 * Licensed under MIT License. Terms and conditions enclosed in "LICENSE.txt".
 */

#include "game.h"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <stack>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "bad_move.h"
#include "board.h"
#include "move.h"
#include "node.h"

namespace omegazero {

using std::cin;
using std::cout;
using std::endl;
using std::invalid_argument;
using std::string;

Game::Game(const string& init_pos) : board_(&player_to_move_, init_pos) {
  game_active_ = true;

  player_in_check_ = kNA;
  winner_ = kNA;

  piece_symbols_[kWhite][kPawn] = "♙";
  piece_symbols_[kWhite][kKnight] = "♘";
  piece_symbols_[kWhite][kBishop] = "♗";
  piece_symbols_[kWhite][kRook] = "♖";
  piece_symbols_[kWhite][kQueen] = "♕";
  piece_symbols_[kWhite][kKing] = "♔";
  piece_symbols_[kBlack][kPawn] = "♟";
  piece_symbols_[kBlack][kKnight] = "♞";
  piece_symbols_[kBlack][kBishop] = "♝";
  piece_symbols_[kBlack][kRook] = "♜";
  piece_symbols_[kBlack][kQueen] = "♛";
  piece_symbols_[kBlack][kKing] = "♚";
}

auto Game::Perft(S8 depth) -> void {
  if (depth < 0) {
    throw std::out_of_range("depth in Game::Perft() must be non-negative");
  }

  S8 next_player;
  std::stack<Node> search_tree;
  string move_str;
  U64 subtree_root_board_hash;
  U64 node_count = 0ULL;
  // Associate board hashes with a string representing the move that resulted
  // in the current board state and a count of the nodes in this subtree.
  std::unordered_map<U64, U64> subtree_node_counts;
  std::unordered_map<U64, string> subtree_moves;
  std::vector<Move> move_list;

  // Perform a depth-first search on the game tree to count the legal moves.
  search_tree.emplace(board_, 0, player_to_move_);
  while (!search_tree.empty()) {
    Node node = search_tree.top();
    search_tree.pop();

    if (node.curr_depth < depth) {
      // Keep track of which subtree whose root lies in the first layer of the
      // search tree is currently being traversed.
      if (node.curr_depth == 1) {
        subtree_root_board_hash =
            node.game_state.GetBoardHash(node.moving_player);
      }

      board_ = node.game_state;
      move_list = engine_.GenerateMoves(node.moving_player, node.game_state);
      next_player = GetOtherPlayer(node.moving_player);
      for (Move move : move_list) {
        try {
          board_.MakeMove(move);
        } catch (BadMove& e) {
          continue;
        }

        // Update the node counts of the subtrees whose roots consist of the
        // nodes in the first layer of the search tree.
        if (node.curr_depth == 0) {
          subtree_root_board_hash = board_.GetBoardHash(next_player);
          subtree_node_counts[subtree_root_board_hash] = 0ULL;
          subtree_moves[subtree_root_board_hash] = GetPerftMoveStr(move);
        } else {
          subtree_node_counts[subtree_root_board_hash]++;
        }

        search_tree.emplace(board_, node.curr_depth + 1, next_player);
        node_count++;
        // Reset board state to the parent node after adding a child node.
        board_ = node.game_state;
      }
    }
  }

  // Output subtree node counts and the total node count.
  for (std::pair<U64, U64> subtree_count : subtree_node_counts) {
    cout << subtree_moves[subtree_count.first] << " " << subtree_count.second
         << endl;
  }
  cout << endl << node_count << endl;
}

void Game::Play() {
  DisplayBoard();
  CheckGameStatus();

  string player_name = GetPlayerStr(player_to_move_);
  cout << "\n\n" << player_name << " to move" << endl;
  Move user_move;
  string err_msg, user_cmd;

GetMove:
  cout << "Enter move: ";
  getline(cin, user_cmd);

  // Check if the player has resigned.
  if (user_cmd == "r") {
    game_active_ = false;
    winner_ = GetOtherPlayer(player_to_move_);
  } else {
    try {
      user_move = ParseMoveCmd(user_cmd);
      board_.MakeMove(user_move);
    } catch (BadMove& e) {
      cout << "ERROR: Bad Move: " << e.what() << endl;
      goto GetMove;
    }
  }

  cout << "\n\n";
  player_to_move_ = GetOtherPlayer(player_to_move_);
}

// Implement private memeber functions.

auto Game::ParseMoveCmd(const string& user_cmd) -> Move {
  Move move;
  move.moving_player = player_to_move_;

  // Check for castling moves.
  if (user_cmd == "0-0-0") {
    if (board_.CastlingLegal(player_to_move_, kQueenSide)) {
      move.castling_type = kQueenSide;
      return move;
    }
    throw BadMove("invalid queenside castling request");
  }
  if (user_cmd == "0-0") {
    if (board_.CastlingLegal(player_to_move_, kKingSide)) {
      move.castling_type = kKingSide;
      return move;
    }
    throw BadMove("invalid kingside castling request");
  }

  // Check that the command is formatted properly.
  bool capture_indicated = false;
  S8 start_rank = kNA;
  S8 start_file = kNA;
  S8 target_rank;
  S8 target_file;
  CheckCmdFormat(user_cmd, &move, &start_rank, &start_file, &target_rank,
                 &target_file, &capture_indicated);

  S8 target_sq = GetSqFromRankFile(target_rank, target_file);
  // Check that specified square positions are on the board.
  if ((start_file != kNA && (start_file < kFileA || start_file > kFileH)) ||
      (start_rank != kNA && (start_rank < kRank1 || start_rank > kRank8)) ||
      !SqOnBoard(target_sq)) {
    throw BadMove("bad command formatting");
  }

  // Confirm a capturing move lands on a square occupied by the other player.
  // or that a non-capturing move lands on a free square.
  S8 other_player = GetOtherPlayer(player_to_move_);
  if (capture_indicated && !move.is_ep) {
    if (board_.GetPlayerOnSq(target_sq) != other_player) {
      throw BadMove("ambiguous or illegal piece movement specified");
    }
    move.captured_piece = board_.GetPieceOnSq(target_sq);
    // Check that a non-capturing move or en passent lands on an open square.
  } else if (board_.GetPlayerOnSq(target_sq) != kNA) {
    throw BadMove("ambiguous or illegal piece movement specified");
  }
  move.target_sq = GetSqFromRankFile(target_rank, target_file);

  // Check that there is exactly one possible start square for the move.
  AddStartSqToMove(&move, other_player, start_rank, start_file, target_rank,
                   target_file, capture_indicated);
  return move;
}

auto Game::GetPerftMoveStr(const Move& move) const -> string {
  string move_str;
  if (move.castling_type == kNA) {
    move_str += static_cast<char>('a' + GetFileFromSq(move.start_sq));
    move_str += static_cast<char>('1' + GetRankFromSq(move.start_sq));
    move_str += static_cast<char>('a' + GetFileFromSq(move.target_sq));
    move_str += static_cast<char>('1' + GetRankFromSq(move.target_sq));
  } else if (move.castling_type == kQueenSide) {
    move_str = "0-0-0";
  } else if (move.castling_type == kKingSide) {
    move_str = "0-0";
  } else {
    throw invalid_argument("move in Game::GetPerftMoveStr()");
  }
  return move_str;
}

auto Game::AddStartSqToMove(Move* move, S8 other_player, S8 start_rank,
                            S8 start_file, S8 target_rank, S8 target_file,
                            bool capture_indicated) const -> void {
  // Compute start_sq by getting all possible places the moved piece could move
  // to from its ending position (start_sqs) and remove all positions
  // where a piece of this type doesn't exist on the board before the move.
  Bitboard start_sqs;
  if (move->moving_piece == kPawn) {
    // Handle en passent moves. Note that we needn't check if all the
    // conditions for an en passent have been met here because ep_target_sq_
    // will only be initialized to a valid square in this scenario.
    if (move->is_ep) {
      S8 ep_target_sq = board_.GetEpTargetSq();
      S8 white_ep_start_sq = GetSqFromRankFile(kRank5, start_file);
      S8 black_ep_start_sq = GetSqFromRankFile(kRank4, start_file);
      // Handle the case of White making an en passent.
      if (move->target_sq == ep_target_sq &&
          abs(start_file - target_file) == 1 && player_to_move_ == kWhite &&
          board_.GetPieceOnSq(white_ep_start_sq) == kPawn &&
          board_.GetPlayerOnSq(white_ep_start_sq) == kWhite) {
        move->start_sq = white_ep_start_sq;
        move->captured_piece = kPawn;
        return;
      }
      // Handle the case of Black making an en passent.
      if (move->target_sq == ep_target_sq &&
          abs(start_file - target_file) == 1 && player_to_move_ == kBlack &&
          board_.GetPieceOnSq(black_ep_start_sq) == kPawn &&
          board_.GetPlayerOnSq(black_ep_start_sq) == kBlack) {
        move->start_sq = black_ep_start_sq;
        move->captured_piece = kPawn;
        return;
      }
      throw BadMove("illegal en passent specified");
    }
    // Handle the case of White making a double pawn push.
    if (player_to_move_ == kWhite && target_rank == kRank4 &&
        !capture_indicated && board_.DoublePawnPushLegal(kWhite, target_file)) {
      move->start_sq = GetSqFromRankFile(kRank2, target_file);
      move->new_ep_target_sq = GetSqFromRankFile(kRank3, target_file);
      return;
    }
    // Handle the case of Black making a double pawn push.
    if (player_to_move_ == kBlack && target_rank == kRank5 &&
        !capture_indicated && board_.DoublePawnPushLegal(kBlack, target_file)) {
      move->start_sq = GetSqFromRankFile(kRank7, target_file);
      move->new_ep_target_sq = GetSqFromRankFile(kRank6, target_file);
      return;
    }

    start_sqs = board_.GetAttackMap(other_player, move->target_sq, kPawn);
    // Clear off pieces on or off the same file as the ending position
    // depending on if the move is a capture or not.
    if (capture_indicated) {
      start_sqs &= ~kFileMaps[target_file];
    } else {
      start_sqs &= kFileMaps[target_file];
    }
  } else {
    start_sqs = board_.GetAttackMap(player_to_move_, move->target_sq,
                                    move->moving_piece);
  }
  start_sqs &= board_.GetPiecesByType(move->moving_piece, player_to_move_);
  if (start_file != kNA) {
    start_sqs &= kFileMaps[start_file];
  }
  if (start_rank != kNA) {
    start_sqs &= kRankMaps[start_rank];
  }

  // Check that exactly one bit is set in the start_sqs mask. If it is, set the
  // the starting square of the move to the indicated square.
  if (start_sqs && !static_cast<bool>(start_sqs & (start_sqs - 1))) {
    move->start_sq = GetSqOfFirstPiece(start_sqs);
    return;
  }
  throw BadMove("ambiguous or illegal piece movement specified");
}

auto Game::CheckCmdFormat(const string& user_cmd, Move* move, S8* start_rank,
                          S8* start_file, S8* target_rank, S8* target_file,
                          bool* capture_indicated) -> void {
  size_t cmd_len = user_cmd.size();
  if (cmd_len == 0) {
    throw BadMove("bad command formatting");
  }

  // Identiyfy the moving piece.
  char first_ch = user_cmd[0];
  switch (first_ch) {
    case 'N':
      move->moving_piece = kKnight;
      break;
    case 'B':
      move->moving_piece = kBishop;
      break;
    case 'R':
      move->moving_piece = kRook;
      break;
    case 'Q':
      move->moving_piece = kQueen;
      break;
    case 'K':
      move->moving_piece = kKing;
      break;
    default:
      move->moving_piece = kPawn;
      S8 file = static_cast<S8>(first_ch - 'a');
      if (file < kFileA || file > kFileH) {
        throw BadMove("bad command formatting");
      }
  }

  switch (cmd_len) {
    // Handle the case of unambiguous pawn move without capture (ex: e4).
    case 2:
      *target_file = static_cast<S8>(user_cmd[0] - 'a');
      *target_rank = static_cast<S8>(user_cmd[1] - '1');
      // Declare invalid if a pawn move to rank 8 is indicated without
      // choosing a piece type to promote to.
      if (*target_rank == kRank8) {
        throw BadMove("bad command formatting");
      }
      break;
    // Handle the cases of unambiguous non-pawn moves without capture (ex: Qe4)
    // and unambiguous pawn move and promotion (ex: d8Q).
    case 3:
      if (move->moving_piece != kPawn) {
        *target_file = static_cast<S8>(user_cmd[1] - 'a');
        *target_rank = static_cast<S8>(user_cmd[2] - '1');
      } else {
        *target_rank = static_cast<S8>(user_cmd[1] - '1');
        if (*target_rank != kRank8) {
          throw BadMove("bad command formatting");
        }
        *target_file = static_cast<S8>(user_cmd[0] - 'a');
        char promoted_to_piece_ch = user_cmd[2];
        switch (promoted_to_piece_ch) {
          case 'N':
            move->promoted_to_piece = kKnight;
            break;
          case 'B':
            move->promoted_to_piece = kBishop;
            break;
          case 'R':
            move->promoted_to_piece = kRook;
            break;
          case 'Q':
            move->promoted_to_piece = kQueen;
            break;
          default:
            throw BadMove("bad command formatting");
        }
      }
      break;
    // Handle the cases of unambiguous captures (ex: exd6, Nxe4) and
    // ambiguous moves requiring a specified start rank or file
    // (ex: R1a3, Rdf8).
    case 4:
      *target_file = static_cast<S8>(user_cmd[2] - 'a');
      *target_rank = static_cast<S8>(user_cmd[3] - '1');
      if (move->moving_piece == kPawn) {
        if (user_cmd[1] != 'x') {
          throw BadMove("bad command formatting");
        }
        *capture_indicated = true;
        *start_file = static_cast<S8>(user_cmd[0] - 'a');
      } else {
        char second_ch = user_cmd[1];
        if (second_ch - '1' >= kRank1 && second_ch - '1' <= kRank8) {
          *start_rank = static_cast<S8>(second_ch - '1');
        } else if (second_ch - 'a' >= kFileA && second_ch - 'a' <= kFileH) {
          *start_file = static_cast<S8>(second_ch - 'a');
        } else if (second_ch == 'x') {
          *capture_indicated = true;
        } else {
          throw BadMove("bad command formatting");
        }
      }
      break;
    // Handle the cases of pawn capture with promotion (ex: exd8Q) and ambiguous
    // non-pawn moves requiring both a specified start rank and file
    // (ex: Qh4e1).
    case 5:
      if (move->moving_piece == kPawn) {
        if (user_cmd[1] != 'x') {
          throw BadMove("bad command formatting");
        }
        *capture_indicated = true;
        char promoted_to_piece_ch = user_cmd[4];
        switch (promoted_to_piece_ch) {
          case 'N':
            move->promoted_to_piece = kKnight;
            break;
          case 'B':
            move->promoted_to_piece = kBishop;
            break;
          case 'R':
            move->promoted_to_piece = kRook;
            break;
          case 'Q':
            move->promoted_to_piece = kQueen;
            break;
          default:
            throw BadMove("bad command formatting");
        }
        *start_file = static_cast<S8>(user_cmd[0] - 'a');
        *target_file = static_cast<S8>(user_cmd[2] - 'a');
        *target_rank = static_cast<S8>(user_cmd[3] - '1');
      } else {
        *start_file = static_cast<S8>(user_cmd[1] - 'a');
        *start_rank = static_cast<S8>(user_cmd[2] - '1');
        *target_file = static_cast<S8>(user_cmd[3] - 'a');
        *target_rank = static_cast<S8>(user_cmd[4] - '1');
      }
      break;
    // Handle the case of an ambiguous non-pawn capture requiring specified
    // start rank and file (ex: Qh4xe1)
    case 6:
      if (move->moving_piece == kPawn || user_cmd[3] != 'x') {
        throw BadMove("bad command formatting");
      }
      *capture_indicated = true;
      *start_file = static_cast<S8>(user_cmd[1] - 'a');
      *start_rank = static_cast<S8>(user_cmd[2] - '1');
      *target_file = static_cast<S8>(user_cmd[4] - 'a');
      *target_rank = static_cast<S8>(user_cmd[5] - '1');
      break;
    // Handle the case of an en passant (ex: exd6e.p.)
    case 8:
      if (move->moving_piece != kPawn || user_cmd[1] != 'x' ||
          user_cmd.substr(4, 4) != "e.p.") {
        throw BadMove("bad command formatting");
      } else {
        *capture_indicated = true;
        move->is_ep = true;
        *start_file = static_cast<S8>(user_cmd[0] - 'a');
        *target_file = static_cast<S8>(user_cmd[2] - 'a');
        *target_rank = static_cast<S8>(user_cmd[3] - '1');
      }
      break;
    default:
      throw BadMove("bad command formatting");
  }
}

auto Game::CheckGameStatus() -> void {
  // Detect if a player is now in check. If so, warn the player.
  if (board_.KingInCheck(player_to_move_)) {
    string player_name = GetPlayerStr(player_to_move_);
    cout << "ALERT: " << player_name << " is in check" << endl;
  }

  // TODO: Check if the player has no legal moves left.
  //   * If king isn't in check, game ends in draw.
  //   * If king is in check, we have a checkmate! Set winner_ equal
  //     to the other player and game_active_ to false

  // Check for threefold and fivefold move repititions.
  U64 board_hash = board_.GetBoardHash(player_to_move_);
  if (pos_rep_table_.find(board_hash) == pos_rep_table_.end()) {
    pos_rep_table_[board_hash] = 1;
  } else {
    pos_rep_table_[board_hash]++;
    S8 num_pos_rep = pos_rep_table_[board_hash];
    if (num_pos_rep == 3) {
      string draw_decision;
      cout << "ALERT: Threefold repitition detected. "
           << "Would you like to claim a draw? (y/n): ";
      cin >> draw_decision;
      cout << endl;
      if (draw_decision == "y") {
        game_active_ = false;
      }
    } else if (num_pos_rep == 5) {
      game_active_ = false;
    }
  }
  // Enforce the Fifty Move Rule.
  if (board_.GetHalfmoveClock() >= 2 * kHalfmoveClockLimit) {
    game_active_ = false;
  }
}

auto Game::DisplayBoard() const -> void {
  S8 piece;
  S8 player;
  S8 sq;
  for (S8 rank = kRank8; rank >= kRank1; --rank) {
    cout << rank + 1 << " ";
    for (S8 file = kFileA; file <= kFileH; ++file) {
      sq = GetSqFromRankFile(rank, file);
      piece = board_.GetPieceOnSq(sq);
      player = board_.GetPlayerOnSq(sq);
      std::string piece_symbol;
      if (player == kNA && piece == kNA) {
        piece_symbol = ".";
      } else {
        piece_symbol = piece_symbols_[player][piece];
      }
      cout << piece_symbol << " ";
    }
    cout << endl;
  }
  cout << "  A B C D E F G H" << endl;
}

}  // namespace omegazero
