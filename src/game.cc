/* Noah Himed
 *
 * Implement the Game type.
 *
 * Licensed under MIT License. Terms and conditions enclosed in "LICENSE.txt".
 */

#include "game.h"

#include <chrono>
#include <cstdint>
#include <ctime>
#include <iostream>
#include <stack>
#include <stdexcept>
#include <string>
#include <vector>

#include "bad_move.h"
#include "board.h"
#include "move.h"

namespace omegazero {

using std::cin;
using std::cout;
using std::endl;
using std::invalid_argument;
using std::string;
using std::vector;

auto GetPieceType(char piece_ch) -> S8 {
  switch (piece_ch) {
    case 'N':
      return kKnight;
      break;
    case 'B':
      return kBishop;
      break;
    case 'R':
      return kRook;
      break;
    case 'Q':
      return kQueen;
      break;
    case 'K':
      return kKing;
      break;
    default:
      return kPawn;
  }
}

Game::Game(const string& init_pos, char player_side)
    : board_(init_pos), engine_(&board_) {
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

  game_active_ = true;

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

void Game::Play() {
  DisplayBoard();
  CheckGameStatus();
  if (!game_active_) {
    return;
  }

  S8 player_to_move = board_.GetPlayerToMove();
  if (player_to_move == user_side_) {
    string player_name = GetPlayerStr(player_to_move);
    cout << "\n\n" << player_name << " to move" << endl;
    Move user_move;
    string err_msg, user_cmd;
  GetMove:
    cout << "Enter move: ";
    getline(cin, user_cmd);

    // Check if the player has resigned.
    if (user_cmd == "q") {
      game_active_ = false;
      winner_ = GetOtherPlayer(player_to_move);
    } else {
      try {
        user_move = ParseMoveCmd(user_cmd);
        board_.MakeMove(user_move);
      } catch (BadMove& e) {
        cout << "ERROR: Bad Move: " << e.what() << endl;
        goto GetMove;
      }
    }
  } else {
    Move engine_move = engine_.TakeTurn();
    cout << "\n\n"
         << GetPlayerStr(player_to_move)
         << "'s move: " << GetUCIMoveStr(engine_move) << endl;
  }
}

auto Game::Test(int depth) -> void {
  if (depth < 1) {
    throw invalid_argument("Perft depth must be at least one");
  }

  Move user_move;
  string user_cmd;
  U64 subtree_node_count;
  U64 total_node_count = 0;
RunPerft:
  DisplayBoard();
  cout << endl;
  // Generate a list of pseudo-legal moves.
  vector<Move> move_list = engine_.GenerateMoves();
  for (const Move& move : move_list) {
    try {
      board_.MakeMove(move);
      // Bug happens here.
      subtree_node_count = engine_.Perft(depth - 1);
      total_node_count += subtree_node_count;
      board_.UnmakeMove(move);
      cout << GetUCIMoveStr(move) << ": " << subtree_node_count << endl;
    } catch (BadMove& e) {
      // Ignore moves that put the player's king in check.
      continue;
    }
  }
  cout << "Nodes visited: " << total_node_count << endl;
  total_node_count = 0;

GetNextNode:
  if (depth - 1 > 0) {
    cout << endl << "Enter command: ";
    getline(cin, user_cmd);

    // Check if the user would like to exit the program.
    if (user_cmd != "q") {
      try {
        user_move = ParseMoveCmd(user_cmd);
        board_.MakeMove(user_move);
        // Decrease the depth by one to preserve the search space.
        --depth;
        cout << endl;
        goto RunPerft;
      } catch (BadMove& e) {
        cout << "ERROR: Bad Move: " << e.what() << endl;
        goto GetNextNode;
      }
    }
  } else {
    cout << "Maximum depth has been reached. Rerun the program to "
            "re-walk tree."
         << endl;
  }
}
auto Game::TimeSearch(int depth) -> void {
  auto search_start_time = std::chrono::high_resolution_clock::now();
  U64 num_legal_moves = engine_.Perft(depth);
  auto search_end_time = std::chrono::high_resolution_clock::now();
  auto search_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                             search_end_time - search_start_time)
                             .count();
  double search_time = std::chrono::duration<double>(search_duration).count();
  cout << "Evaluated ~"
       << 1000 * (static_cast<double>(num_legal_moves) / search_time)
       << " moves/sec" << endl;
}

// Implement private member functions.

auto Game::ParseMoveCmd(const string& user_cmd) -> Move {
  Move move;
  // Check for castling moves.
  if (user_cmd == "0-0-0") {
    if (board_.CastlingLegal(kQueenSide)) {
      move.castling_type = kQueenSide;
      return move;
    }
    throw BadMove("invalid queenside castling request");
  }
  if (user_cmd == "0-0") {
    if (board_.CastlingLegal(kKingSide)) {
      move.castling_type = kKingSide;
      return move;
    }
    throw BadMove("invalid kingside castling request");
  }

  bool capture_indicated = false;
  S8 start_rank = kNA;
  S8 start_file = kNA;
  S8 target_rank;
  S8 target_file;
  // Collect info from a move command formatted in FIDE algebraic notation.
  InterpAlgNotation(user_cmd, move, start_rank, start_file, target_rank,
                    target_file, capture_indicated);
  // Check a few requirements for the move's pseudo-legality.
  CheckMove(move, start_rank, start_file, target_rank, target_file,
            capture_indicated);
  // Check that there is exactly one possible start square for the move, and set
  // the move's start square to this square if so.
  AddStartSqToMove(move, start_rank, start_file, target_rank, target_file,
                   capture_indicated);
  return move;
}

auto Game::GetUCIMoveStr(const Move& move) -> string {
  string move_str;
  if (move.castling_type == kNA) {
    move_str += static_cast<char>('a' + GetFileFromSq(move.start_sq));
    move_str += static_cast<char>('1' + GetRankFromSq(move.start_sq));
    move_str += static_cast<char>('a' + GetFileFromSq(move.target_sq));
    move_str += static_cast<char>('1' + GetRankFromSq(move.target_sq));

    if (move.promoted_to_piece != kNA) {
      switch (move.promoted_to_piece) {
        case kKnight:
          move_str += 'k';
          break;
        case kBishop:
          move_str += 'b';
          break;
        case kRook:
          move_str += 'r';
          break;
        case kQueen:
          move_str += 'q';
          break;
        default:
          throw invalid_argument(
              "move.promoted_to_piece in Game::GetUCIMoveStr()");
      }
    }
  } else if (move.castling_type == kQueenSide) {
    if (board_.GetPlayerToMove() == kWhite) {
      move_str = "e1c1";
    } else {
      move_str = "e8c8";
    }
  } else if (move.castling_type == kKingSide) {
    if (board_.GetPlayerToMove() == kWhite) {
      move_str = "e1g1";
    } else {
      move_str = "e8g8";
    }
  } else {
    throw invalid_argument("move.castling_type in Game::GetUCIMoveStr()");
  }
  return move_str;
}

auto Game::AddStartSqToMove(Move& move, S8 start_rank, S8 start_file,
                            S8 target_rank, S8 target_file,
                            bool capture_indicated) const -> void {
  // Compute start_sq by getting all possible places the moved piece could move
  // to from its ending position (start_sqs) and remove all positions
  // where a piece of this type doesn't exist on the board before the move.
  Bitboard start_sqs;
  S8 player_to_move = board_.GetPlayerToMove();
  if (move.moving_piece == kPawn) {
    // Handle en passent moves. Note that we needn't check if all the
    // conditions for an en passent have been met here because ep_target_sq_
    // will only be initialized to a valid square in this scenario.
    if (move.is_ep) {
      S8 ep_target_sq = board_.GetEpTargetSq();
      if (move.target_sq == ep_target_sq &&
          abs(start_file - target_file) == 1) {
        // Handle the case of White making an en passent.
        S8 white_ep_start_sq = GetSqFromRankFile(kRank5, start_file);
        if (player_to_move == kWhite &&
            board_.GetPieceOnSq(white_ep_start_sq) == kPawn &&
            board_.GetPlayerOnSq(white_ep_start_sq) == kWhite) {
          move.start_sq = white_ep_start_sq;
          move.captured_piece = kPawn;
          return;
        }
        // Handle the case of Black making an en passent.
        S8 black_ep_start_sq = GetSqFromRankFile(kRank4, start_file);
        if (player_to_move == kBlack &&
            board_.GetPieceOnSq(black_ep_start_sq) == kPawn &&
            board_.GetPlayerOnSq(black_ep_start_sq) == kBlack) {
          move.start_sq = black_ep_start_sq;
          move.captured_piece = kPawn;
          return;
        }
      }
      throw BadMove("illegal en passent specified");
    }

    if (!capture_indicated && board_.DoublePawnPushLegal(target_file)) {
      // Handle the case of White making a double pawn push.
      if (player_to_move == kWhite && target_rank == kRank4) {
        move.start_sq = GetSqFromRankFile(kRank2, target_file);
        move.new_ep_target_sq = GetSqFromRankFile(kRank3, target_file);
        return;
      }
      // Handle the case of Black making a double pawn push.
      if (player_to_move == kBlack && target_rank == kRank5) {
        move.start_sq = GetSqFromRankFile(kRank7, target_file);
        move.new_ep_target_sq = GetSqFromRankFile(kRank6, target_file);
        return;
      }
    }

    // Clear off pieces on or off the same file as the ending position
    // depending on if the pawn move captures a piece or not.
    S8 other_player = GetOtherPlayer(player_to_move);
    start_sqs = board_.GetAttackMap(other_player, move.target_sq, kPawn);
    if (capture_indicated) {
      start_sqs &= ~kFileMaps[target_file];
    } else {
      start_sqs &= kFileMaps[target_file];
    }
  } else {
    start_sqs =
        board_.GetAttackMap(player_to_move, move.target_sq, move.moving_piece);
  }

  start_sqs &= board_.GetPiecesByType(move.moving_piece, player_to_move);
  if (start_file != kNA) {
    start_sqs &= kFileMaps[start_file];
  }
  if (start_rank != kNA) {
    start_sqs &= kRankMaps[start_rank];
  }

  // Check that exactly one bit is set in the start_sqs mask. If it is, set the
  // the starting square of the move to the indicated square.
  if (OneSqSet(start_sqs)) {
    move.start_sq = GetSqOfFirstPiece(start_sqs);
    return;
  }
  throw BadMove("ambiguous or illegal piece movement specified");
}

auto Game::CheckGameStatus() -> void {
  // Detect if a player is now in check. If so, warn the player.
  if (board_.KingInCheck()) {
  }

  // Check for checks, checkmates, and draws.
  vector<Move> move_list = engine_.GenerateMoves();
  bool no_legal_moves = true;
  for (const Move& move : move_list) {
    try {
      board_.MakeMove(move);
      board_.UnmakeMove(move);
      no_legal_moves = false;
      break;
    } catch (BadMove& e) {
      // Ignore moves that leave the king in check.
      continue;
    }
  }
  if (board_.KingInCheck()) {
    string player_name = GetPlayerStr(board_.GetPlayerToMove());
    if (no_legal_moves) {
      winner_ = GetOtherPlayer(board_.GetPlayerToMove());
      game_active_ = false;
      cout << player_name << " has been checkmated" << endl;
    } else {
      cout << player_name << " is in check" << endl;
    }
  } else if (no_legal_moves) {
    game_active_ = false;
  }

  // Check for threefold and fivefold position repititions.
  U64 board_hash = board_.GetBoardHash();
  if (pos_rep_table_.find(board_hash) == pos_rep_table_.end()) {
    pos_rep_table_[board_hash] = 1;
  } else {
    ++pos_rep_table_[board_hash];
    S8 num_pos_rep = pos_rep_table_[board_hash];
    if (num_pos_rep == 3) {
      if (board_.GetPlayerToMove() == user_side_) {
        string draw_decision;
        cout << "Threefold repitition detected. "
             << "Would you like to claim a draw? (y/): ";
        cin >> draw_decision;
        cout << endl;
        if (draw_decision == "y") {
          game_active_ = false;
        }
      } else {
        // TODO: Allow the engine to choose whether or not it would like to
        // draw. Currently, the engine will always choose a draw.
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

auto Game::CheckMove(Move& move, S8 start_rank, S8 start_file, S8 target_rank,
                     S8 target_file, bool capture_indicated) -> void {
  S8 player_to_move = board_.GetPlayerToMove();
  // Check for valid pawn promotion.
  if (move.moving_piece == kPawn) {
    if (move.promoted_to_piece == kNA) {
      if ((player_to_move == kWhite && target_rank == kRank8) ||
          (player_to_move == kBlack && target_rank == kRank1)) {
        throw BadMove("no pawn promotion indicated");
      }
    } else {
      if ((player_to_move == kWhite && target_rank != kRank8) ||
          (player_to_move == kBlack && target_rank != kRank1)) {
        throw BadMove("invalid pawn promotion indicated");
      }
    }
  }

  // Check that specified square positions are on the board.
  if ((start_file != kNA && (start_file < kFileA || start_file > kFileH)) ||
      (start_rank != kNA && (start_rank < kRank1 || start_rank > kRank8)) ||
      (target_file != kNA && (target_file < kFileA || target_file > kFileH)) ||
      (target_rank != kNA && (target_rank < kRank1 || target_rank > kRank8))) {
    throw BadMove("bad command formatting");
  }

  // Confirm a capturing move lands on a square occupied by the other player,
  // or that a non-capturing move lands on a free square.
  S8 other_player = GetOtherPlayer(player_to_move);
  if (capture_indicated && !move.is_ep) {
    if (board_.GetPlayerOnSq(move.target_sq) != other_player) {
      throw BadMove("ambiguous or illegal piece movement specified");
    }
    move.captured_piece = board_.GetPieceOnSq(move.target_sq);
    // Check that a non-capturing move or en passent lands on an open square.
  } else if (board_.GetPlayerOnSq(move.target_sq) != kNA) {
    throw BadMove("ambiguous or illegal piece movement specified");
  }
}

auto Game::InterpAlgNotation(const string& user_cmd, Move& move, S8& start_rank,
                             S8& start_file, S8& target_rank, S8& target_file,
                             bool& capture_indicated) -> void {
  size_t cmd_len = user_cmd.size();
  if (cmd_len == 0) {
    throw BadMove("bad command formatting");
  }

  move.moving_piece = GetPieceType(user_cmd[0]);
  switch (cmd_len) {
    // Handle the case of unambiguous pawn move without capture (ex: e4).
    case 2:
      target_file = static_cast<S8>(user_cmd[0] - 'a');
      target_rank = static_cast<S8>(user_cmd[1] - '1');
      break;
    // Handle the cases of unambiguous non-pawn moves without capture (ex: Qe4)
    // and non-capturing pawn move and promotion (ex: d8Q).
    case 3:
      if (move.moving_piece == kPawn) {
        target_rank = static_cast<S8>(user_cmd[1] - '1');
        target_file = static_cast<S8>(user_cmd[0] - 'a');
        move.promoted_to_piece = GetPieceType(user_cmd[2]);
        if (move.promoted_to_piece == kPawn) {
          throw BadMove("bad command formatting");
        }
      } else {
        target_file = static_cast<S8>(user_cmd[1] - 'a');
        target_rank = static_cast<S8>(user_cmd[2] - '1');
      }
      break;
    // Handle the cases of unambiguous captures (ex: exd6, Nxe4) and
    // ambiguous moves requiring a specified start rank or file
    // (ex: R1a3, Rdf8).
    case 4:
      target_file = static_cast<S8>(user_cmd[2] - 'a');
      target_rank = static_cast<S8>(user_cmd[3] - '1');
      if (move.moving_piece == kPawn) {
        if (user_cmd[1] != 'x') {
          throw BadMove("bad command formatting");
        }
        capture_indicated = true;
        start_file = static_cast<S8>(user_cmd[0] - 'a');
      } else {
        char second_ch = user_cmd[1];
        if (second_ch - '1' >= kRank1 && second_ch - '1' <= kRank8) {
          start_rank = static_cast<S8>(second_ch - '1');
        } else if (second_ch - 'a' >= kFileA && second_ch - 'a' <= kFileH) {
          start_file = static_cast<S8>(second_ch - 'a');
        } else if (second_ch == 'x') {
          capture_indicated = true;
        } else {
          throw BadMove("bad command formatting");
        }
      }
      break;
    // Handle the cases of pawn capture with promotion (ex: exd8Q) and ambiguous
    // non-pawn moves requiring both a specified start rank and file
    // (ex: Qh4e1).
    case 5:
      if (move.moving_piece == kPawn) {
        if (user_cmd[1] != 'x') {
          throw BadMove("bad command formatting");
        }
        capture_indicated = true;
        move.promoted_to_piece = GetPieceType(user_cmd[4]);
        if (move.promoted_to_piece == kPawn) {
          throw BadMove("bad command formatting");
        }
        start_file = static_cast<S8>(user_cmd[0] - 'a');
        target_file = static_cast<S8>(user_cmd[2] - 'a');
        target_rank = static_cast<S8>(user_cmd[3] - '1');
      } else {
        start_file = static_cast<S8>(user_cmd[1] - 'a');
        start_rank = static_cast<S8>(user_cmd[2] - '1');
        target_file = static_cast<S8>(user_cmd[3] - 'a');
        target_rank = static_cast<S8>(user_cmd[4] - '1');
      }
      break;
    // Handle the case of an ambiguous non-pawn capture requiring specified
    // start rank and file (ex: Qh4xe1)
    case 6:
      if (move.moving_piece == kPawn || user_cmd[3] != 'x') {
        throw BadMove("bad command formatting");
      }
      capture_indicated = true;
      start_file = static_cast<S8>(user_cmd[1] - 'a');
      start_rank = static_cast<S8>(user_cmd[2] - '1');
      target_file = static_cast<S8>(user_cmd[4] - 'a');
      target_rank = static_cast<S8>(user_cmd[5] - '1');
      break;
    // Handle the case of an en passant (ex: exd6e.p.)
    case 8:
      if (move.moving_piece != kPawn || user_cmd[1] != 'x' ||
          user_cmd.substr(4, 4) != "e.p.") {
        throw BadMove("bad command formatting");
      } else {
        capture_indicated = true;
        move.is_ep = true;
        start_file = static_cast<S8>(user_cmd[0] - 'a');
        target_file = static_cast<S8>(user_cmd[2] - 'a');
        target_rank = static_cast<S8>(user_cmd[3] - '1');
      }
      break;
    default:
      throw BadMove("bad command formatting");
  }

  move.target_sq = GetSqFromRankFile(target_rank, target_file);
}

}  // namespace omegazero
