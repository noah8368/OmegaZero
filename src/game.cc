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

#include "board.h"
#include "move.h"
#include "node.h"

using namespace std;

Game::Game(const string& init_pos) : board_(player_to_move_, init_pos) {
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

bool Game::IsActive() const {
  return game_active_;
}

void Game::OutputWinner() const {
  if (winner_ == kNA) {
    cout << "Draw" << endl;
  } else {
    string player_name = GetPlayerStr(winner_);
    cout << player_name << " wins" << endl;
  }
}

void Game::Perft(const int& depth) {
  if (depth > kMaxPerftDepth) {
    throw out_of_range("depth in Game::Perft() is too large");
  } else if (depth < 0) {
    throw out_of_range("depth in Game::Perft() must be non-negative");
  }

  int next_player;
  stack<Node> search_tree;
  string move_str;
  U64 subtree_root_board_hash;
  U64 node_count = 0ULL;
  // Associate board hashes with a string representing the move that resulted
  // in the current board state and a count of the nodes in this subtree.
  unordered_map<U64, U64> subtree_node_counts;
  unordered_map<U64, string> subtree_moves;
  vector<Move> move_list;

  // Perform a depth-first search on the game tree to count the legal moves.
  search_tree.emplace(board_, 0, player_to_move_);
  while (!search_tree.empty()) {
    Node node = search_tree.top();
    search_tree.pop();

    if (node.curr_depth < depth) {
      // Keep track of which subtree whose root lies in the first layer of the
      // search tree is currently being traversed.
      if (node.curr_depth == 1) {
        subtree_root_board_hash = node.game_state.GetBoardHash(node.moving_player);
      }

      board_ = node.game_state;
      move_list = engine_.GenerateMoves(node.moving_player, node.game_state);
      next_player = GetOtherPlayer(node.moving_player);
      for (Move move : move_list) {
        if (board_.MakeMove(move)) {
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
  }

  // Output subtree node counts and the total node count.
  for (pair<U64, U64> subtree_count : subtree_node_counts) {
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
  bool move_legal;
  Move user_move;
  string err_msg, user_cmd;
  do {
    cout << "Enter move: ";
    getline(cin, user_cmd);
    // Check if the player has resigned.
    if (user_cmd == "r") {
      game_active_ = false;
      winner_ = GetOtherPlayer(player_to_move_);
      break;
    } else {
      // Check if the move is pseudo-legal.
      move_legal = CheckMove(user_cmd, user_move, err_msg);
      if (move_legal) {
        // Make the move and check if it puts the player's king in check.
        // If so, unmake the move and set err_msg accordingly.
        move_legal = board_.MakeMove(user_move, err_msg);
      }
    }
    // Output an appropriate error message for illegal moves.
    if (!move_legal) {
      cout << "ERROR: Invalid move: " << err_msg << endl;
    }
  } while (!move_legal);
  cout << "\n\n";
  player_to_move_ = GetOtherPlayer(player_to_move_);
}

bool Game::CheckMove(const string& user_cmd, Move& move,
                     string& err_msg) {
  size_t cmd_len = user_cmd.length();
  move.moving_player = player_to_move_;
  if (cmd_len == 0) {
    err_msg = "bad command formatting";
    return false;
  // For castling moves, check that the following hold:
  //   * Neither the king nor the chosen rook has previously moved.
  //   * There are no pieces between the king and the chosen rook.
  //   * The king is not currently in check.
  //   * The king does not pass through a square that is attacked by an enemy
  //     piece.
  } else if (user_cmd == "0-0-0") {
    if (board_.CastlingLegal(player_to_move_, kQueenSide)) {
      move.castling_type = kQueenSide;
      return true;
    } else {
      err_msg = "invalid queenside castling request";
      return false;
    }
  } else if (user_cmd == "0-0") {
    if (board_.CastlingLegal(player_to_move_, kKingSide)) {
      move.castling_type = kKingSide;
      return true;
    } else {
      err_msg = "invalid kingside castling request";
      return false;
    }
  }

  // Parse the command to check that it's formatted correctly.
  char first_ch = user_cmd[0];
  switch (first_ch) {
    case 'N':
      move.moving_piece = kKnight;
      break;
    case 'B':
      move.moving_piece = kBishop;
      break;
    case 'R':
      move.moving_piece = kRook;
      break;
    case 'Q':
      move.moving_piece = kQueen;
      break;
    case 'K':
      move.moving_piece = kKing;
      break;
    default:
      move.moving_piece = kPawn;
      int file = first_ch - 'a';
      if (file < kFileA || file > kFileH) {
        err_msg = "bad command formatting";
        return false;
      }
  }
  bool capture_indicated = false;
  int start_rank = kNA;
  int start_file = kNA;
  int target_rank;
  int target_file;
  switch (cmd_len) {
    // Handle the case of unambiguous pawn move without capture (ex: e4).
    case 2:
      target_file = user_cmd[0] - 'a';
      target_rank = user_cmd[1] - '1';
      // Declare invalid if a pawn move to rank 8 is indicated without
      // choosing a piece type to promote to.
      if (target_rank == kRank8) {
        err_msg = "bad command formatting";
        return false;
      }
      break;
    // Handle the cases of unambiguous non-pawn moves without capture (ex: Qe4)
    // and unambiguous pawn move and promotion (ex: d8Q).
    case 3:
      if (move.moving_piece != kPawn) {
        target_file = user_cmd[1] - 'a';
        target_rank = user_cmd[2] - '1';
      } else {
        target_rank = user_cmd[1] - '1';
        if (target_rank != kRank8) {
          err_msg = "bad command formatting";
          return false;
        }
        target_file = user_cmd[0] - 'a';
        char promoted_to_piece_ch = user_cmd[2];
        switch (promoted_to_piece_ch) {
          case 'N':
            move.promoted_to_piece = kKnight;
            break;
          case 'B':
            move.promoted_to_piece = kBishop;
            break;
          case 'R':
            move.promoted_to_piece = kRook;
            break;
          case 'Q':
            move.promoted_to_piece = kQueen;
            break;
          default:
            err_msg = "bad command formatting";
            return false;
        }
      }
      break;
    // Handle the cases of unambiguous captures (ex: exd6, Nxe4) and
    // ambiguous moves requiring a specified start rank or file
    // (ex: R1a3, Rdf8).
    case 4:
      target_file = user_cmd[2] - 'a';
      target_rank = user_cmd[3] - '1';
      if (move.moving_piece == kPawn) {
        if (user_cmd[1] != 'x') {
          err_msg = "bad command formatting";
          return false;
        }
        capture_indicated = true;
        start_file = user_cmd[0] - 'a';
      } else {
        char second_ch = user_cmd[1];
        if (second_ch - '1' >= kRank1 && second_ch - '1' <= kRank8) {
          start_rank = second_ch - '1';
        } else if (second_ch - 'a' >= kFileA && second_ch - 'a' <= kFileH) {
          start_file = second_ch - 'a';
        } else if (second_ch == 'x') {
          capture_indicated = true;
        } else {
          err_msg = "bad command formatting";
          return false;
        }
      }
      break;
    // Handle the cases of pawn capture with promotion (ex: exd8Q) and ambiguous
    // non-pawn moves requiring both a specified start rank and file
    // (ex: Qh4e1).
    case 5:
      if (move.moving_piece == kPawn) {
        if (user_cmd[1] != 'x') {
          err_msg = "bad command formatting";
          return false;
        }
        capture_indicated = true;
        char promoted_to_piece_ch = user_cmd[4];
        switch (promoted_to_piece_ch) {
          case 'N':
            move.promoted_to_piece = kKnight;
            break;
          case 'B':
            move.promoted_to_piece = kBishop;
            break;
          case 'R':
            move.promoted_to_piece = kRook;
            break;
          case 'Q':
            move.promoted_to_piece = kQueen;
            break;
          default:
            err_msg = "bad command formatting";
            return false;
        }
        start_file = user_cmd[0] - 'a';
        target_file = user_cmd[2] - 'a';
        target_rank = user_cmd[3] - '1';
      } else {
        start_file = user_cmd[1] - 'a';
        start_rank = user_cmd[2] - '1';
        target_file = user_cmd[3] - 'a';
        target_rank = user_cmd[4] - '1';
      }
      break;
    // Handle the case of an ambiguous non-pawn capture requiring specified
    // start rank and file (ex: Qh4xe1)
    case 6:
      if (move.moving_piece == kPawn || user_cmd[3] != 'x') {
        err_msg = "bad command formatting";
        return false;
      }
      capture_indicated = true;
      start_file = user_cmd[1] - 'a';
      start_rank = user_cmd[2] - '1';
      target_file = user_cmd[4] - 'a';
      target_rank = user_cmd[5] - '1';
      break;
    // Handle the case of an en passant (ex: exd6e.p.)
    case 8:
      if (move.moving_piece != kPawn || user_cmd[1] != 'x'
          || user_cmd.substr(4, 4) != "e.p.") {
        err_msg = "bad command formatting";
        return false;
      } else {
        capture_indicated = true;
        move.is_ep = true;
        start_file = user_cmd[0] - 'a';
        target_file = user_cmd[2] - 'a';
        target_rank = user_cmd[3] - '1';
      }
      break;
    default:
      err_msg = "bad command formatting";
      return false;
  }

  int target_sq = GetSqFromRankFile(target_rank, target_file);
  // Check that specified square positions are on the board.
  if ((start_file != kNA && (start_file < kFileA || start_file > kFileH))
      || (start_rank != kNA && (start_rank < kRank1 || start_rank > kRank8))
      || !SqOnBoard(target_sq)) {
    err_msg = "bad command formatting";
    return false;
  }

  // Confirm a capturing move lands on a square occupied by the other player.
  // or that a non-capturing move lands on a free square.
  int other_player = GetOtherPlayer(player_to_move_);
  if (capture_indicated && !move.is_ep) {
    if (board_.GetPlayerOnSq(target_sq) != other_player) {
      err_msg = "ambiguous or illegal piece movement specified";
      return false;
    } else {
      move.captured_piece = board_.GetPieceOnSq(target_sq);
    }
  // Check that a non-capturing move or en passent lands on an open square.
  } else if (board_.GetPlayerOnSq(target_sq) != kNA) {
    err_msg = "ambiguous or illegal piece movement specified";
    return false;
  }
  move.target_sq = GetSqFromRankFile(target_rank, target_file);

  // Compute start_sq by getting all possible places the moved piece could move
  // to from its ending position (start_positions) and remove all positions
  // where a piece of this type doesn't exist on the board before the move.
  Bitboard start_positions;
  if (move.moving_piece == kPawn) {
    // Handle en passent moves. Note that we needn't check if all the
    // conditions for an en passent have been met here because ep_target_sq_
    // will only be initialized to a valid square in this scenario.
    if (move.is_ep) {
      // Handle the case of White making an en passent.
      int ep_target_sq = board_.GetEpTargetSq();
      int white_ep_start_sq = GetSqFromRankFile(kRank5, start_file);
      int black_ep_start_sq = GetSqFromRankFile(kRank4, start_file);
      if (move.target_sq == ep_target_sq
          && abs(start_file - target_file) == 1
          && player_to_move_ == kWhite
          && board_.GetPieceOnSq(white_ep_start_sq) == kPawn
          && board_.GetPlayerOnSq(white_ep_start_sq) == kWhite) {
        move.start_sq = white_ep_start_sq;
        move.captured_piece = kPawn;
        return true;
      // Handle the case of Black making an en passent.
      } else if (move.target_sq == ep_target_sq
                 && abs(start_file - target_file) == 1
                 && player_to_move_ == kBlack
                 && board_.GetPieceOnSq(black_ep_start_sq) == kPawn
                 && board_.GetPlayerOnSq(black_ep_start_sq) == kBlack) {
        move.start_sq = black_ep_start_sq;
        move.captured_piece = kPawn;
        return true;
      } else {
        err_msg = "illegal en passent specified";
        return false;
      }
  // Handle the case of White making a double pawn push.
  } else if (player_to_move_ == kWhite && target_rank == kRank4
               && !capture_indicated
               && board_.DoublePawnPushLegal(kWhite, target_file) ) {
      move.start_sq = GetSqFromRankFile(kRank2, target_file);
      move.new_ep_target_sq = GetSqFromRankFile(kRank3, target_file);
      return true;
    // Handle the case of Black making a double pawn push.
  } else if (player_to_move_ == kBlack && target_rank == kRank5
             && !capture_indicated
             && board_.DoublePawnPushLegal(kBlack, target_file)) {
      move.start_sq = GetSqFromRankFile(kRank7, target_file);
      move.new_ep_target_sq = GetSqFromRankFile(kRank6, target_file);
      return true;
    } else {
      start_positions = board_.GetAttackMap(other_player, move.target_sq,
                                            kPawn);
    }

    // Clear off pieces on or off the same file as the ending position
    // depending on if the move is a capture or not.
    if (capture_indicated) {
      start_positions &= ~kFileMasks[target_file];
    } else {
      start_positions &= kFileMasks[target_file];
    }
  } else {
    start_positions = board_.GetAttackMap(player_to_move_, move.target_sq,
                                          move.moving_piece);
  }
  start_positions &= board_.GetPiecesByType(move.moving_piece, player_to_move_);
  if (start_file != kNA) {
    start_positions &= kFileMasks[start_file];
  }
  if (start_rank != kNA) {
    start_positions &= kRankMasks[start_rank];
  }

  // Check that exactly one bit is set in the start_positions mask.
  bool one_start_piece = start_positions
                          && !(start_positions & (start_positions - 1));
  if (one_start_piece) {
    move.start_sq = GetSqOfFirstPiece(start_positions);
    return true;
  } else {
    err_msg = "ambiguous or illegal piece movement specified";
    return false;
  }
}

string Game::GetPerftMoveStr(const Move& move) const {
  string move_str;
  if (move.castling_type == kNA) {
    move_str += static_cast<char>('a' + GetRankFromSq(move.start_sq));
    move_str += static_cast<char>('1' + GetFileFromSq(move.start_sq));
    move_str += static_cast<char>('a' + GetRankFromSq(move.target_sq));
    move_str += static_cast<char>('1' + GetFileFromSq(move.target_sq));
  } else {
    if (move.castling_type == kQueenSide) {
      move_str = "0-0-0";
    } else if (move.castling_type == kKingSide) {
      move_str = "0-0";
    } else {
      throw invalid_argument("move in Game::GetPerftMoveStr()");
    }
  }
  return move_str;
}

void Game::CheckGameStatus() {
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
    int num_pos_rep = pos_rep_table_[board_hash];
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
  if (board_.GetHalfmoveClock() >= 2*kHalfmoveClockLimit) {
    game_active_ = false;
  }
}

void Game::DisplayBoard() const {
  int piece;
  int player;
  int sq;
  for (int rank = kRank8; rank >= kRank1; --rank) {
    cout << rank + 1 << " ";
    for (int file = kFileA; file <= kFileH; ++file) {
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

std::string GetPlayerStr(const int& player) {
  if (player == kWhite) {
    return "White";
  } else if (player == kBlack) {
    return "Black";
  } else {
    throw invalid_argument("player in GetPlayerStr()");
  }
}
