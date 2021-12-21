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
#include "engine.h"
#include "move.h"

namespace omegazero {

using std::cin;
using std::cout;
using std::endl;
using std::invalid_argument;
using std::string;
using std::vector;

auto GetPieceLetter(S8 piece) -> char {
  switch (piece) {
    case kKnight:
      return 'N';
    case kBishop:
      return 'B';
    case kRook:
      return 'R';
    case kQueen:
      return 'Q';
    case kKing:
      return 'K';
    default:
      throw invalid_argument("piece in GetPieceLetter()");
  }
}

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
    : board_(init_pos), engine_(&board_, player_side) {
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

  // Record the current board state to enforce move repitition rules.
  engine_.AddBoardRep();

  // Check game status.
  S8 game_status = engine_.GetGameStatus();
  S8 player_to_move = board_.GetPlayerToMove();
  if (game_status == kPlayerInCheck) {
    cout << GetPlayerStr(player_to_move) << " is in check" << endl;
  } else if (game_status == kDraw) {
    game_active_ = false;
    return;
  } else if (game_status == kOptionalDraw) {
    string draw_decision;
    cout << "Threefold repitition detected. "
         << "Would you like to claim a draw? (y/): ";
    getline(cin, draw_decision);
    if (draw_decision == "y") {
      game_active_ = false;
      return;
    }
  } else if (game_status == kPlayerCheckmated) {
    cout << GetPlayerStr(player_to_move) << " has been checkmated" << endl;
    game_active_ = false;
    winner_ = GetOtherPlayer(player_to_move);
    return;
  }

  S8 user_side = engine_.GetUserSide();
  if (player_to_move == user_side) {
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
    Move engine_move = engine_.GetBestMove();
    cout << "\n\n"
         << GetPlayerStr(player_to_move)
         << "'s move: " << GetFideMoveStr(engine_move) << endl;
    board_.MakeMove(engine_move);
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
    } catch (BadMove& e) {
      // Ignore moves that put the player's king in check.
      continue;
    }
    subtree_node_count = engine_.Perft(depth - 1);
    board_.UnmakeMove(move);
    cout << GetUciMoveStr(move) << ": " << subtree_node_count << endl;
    total_node_count += subtree_node_count;
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
      } catch (BadMove& e) {
        cout << "ERROR: Bad Move: " << e.what() << endl;
        goto GetNextNode;
      }
      // Decrease the depth by one to preserve the search space.
      --depth;
      cout << endl;
      goto RunPerft;
    }
  } else {
    cout << "Maximum depth has been reached. Rerun the program to "
            "re-walk tree."
         << endl;
  }
}

auto Game::TimeSearch() -> void {
  auto search_start_time = std::chrono::high_resolution_clock::now();
  engine_.GetBestMove();
  auto search_end_time = std::chrono::high_resolution_clock::now();
  auto search_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                             search_end_time - search_start_time)
                             .count();
  double search_time = std::chrono::duration<double>(search_duration).count();
  cout << "Time for search of depth " << kSearchDepth << ": " << search_time
       << "ms" << endl;
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

auto Game::GetFideMoveStr(const Move& move) -> string {
  string move_str;
  S8 start_file = GetFileFromSq(move.start_sq);
  S8 target_file = GetFileFromSq(move.target_sq);
  S8 target_rank = GetRankFromSq(move.target_sq);
  if (move.castling_type == kNA) {
    if (move.moving_piece == kPawn && move.captured_piece != kNA) {
      move_str += static_cast<char>(start_file + 'a');
      move_str += 'x';
    } else if (move.moving_piece != kPawn) {
      move_str += GetPieceLetter(move.moving_piece);

      // Add clarifying information to the move string if the move is ambiguous.
      S8 moving_player = board_.GetPlayerToMove();
      Bitboard start_sqs =
          board_.GetAttackMap(moving_player, move.target_sq, move.moving_piece);
      start_sqs &= board_.GetPiecesByType(move.moving_piece, moving_player);
      if (!OneSqSet(start_sqs)) {
        S8 start_rank = GetRankFromSq(move.start_sq);
        if (OneSqSet(start_sqs & kRankMasks[start_rank])) {
          move_str += static_cast<char>(start_file + 'a');
        } else if (OneSqSet(start_sqs & kFileMasks[start_file])) {
          move_str += static_cast<char>(start_rank + '1');
        } else {
          move_str += static_cast<char>(start_file + 'a');
          move_str += static_cast<char>(start_rank + '1');
        }
      }

      if (move.captured_piece != kNA) {
        move_str += 'x';
      }
    }

    move_str += static_cast<char>(target_file + 'a');
    move_str += static_cast<char>(target_rank + '1');

    if (move.promoted_to_piece != kNA) {
      move_str += GetPieceLetter(move.promoted_to_piece);
    } else if (move.is_ep) {
      move_str += "e.p.";
    }
  } else if (move.castling_type == kQueenSide) {
    move_str = "0-0-0";
  } else if (move.castling_type == kKingSide) {
    move_str = "0-0";
  } else {
    throw invalid_argument("move.castling_type in Game::GetFideMoveStr()");
  }
  return move_str;
}

auto Game::GetUciMoveStr(const Move& move) -> string {
  string move_str;
  // Denote the moving player for the move as the player that just finished
  // their turn.
  S8 moving_player = board_.GetPlayerToMove();
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
              "move.promoted_to_piece in Game::GetUciMoveStr()");
      }
    }
  } else if (move.castling_type == kQueenSide) {
    if (moving_player == kWhite) {
      move_str = "e1c1";
    } else {
      move_str = "e8c8";
    }
  } else if (move.castling_type == kKingSide) {
    if (moving_player == kWhite) {
      move_str = "e1g1";
    } else {
      move_str = "e8g8";
    }
  } else {
    throw invalid_argument("move.castling_type in Game::GetUciMoveStr()");
  }
  return move_str;
}

auto Game::AddStartSqToMove(Move& move, S8 start_rank, S8 start_file,
                            S8 target_rank, S8 target_file,
                            bool capture_indicated) const -> void {
  // Compute start_sq by getting all possible places the moved piece could
  // move to from its ending position (start_sqs) and remove all positions
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
      start_sqs &= ~kFileMasks[target_file];
    } else {
      start_sqs &= kFileMasks[target_file];
    }
  } else {
    start_sqs =
        board_.GetAttackMap(player_to_move, move.target_sq, move.moving_piece);
  }

  start_sqs &= board_.GetPiecesByType(move.moving_piece, player_to_move);
  if (start_file != kNA) {
    start_sqs &= kFileMasks[start_file];
  }
  if (start_rank != kNA) {
    start_sqs &= kRankMasks[start_rank];
  }

  // Check that exactly one bit is set in the start_sqs mask. If it is, set
  // the the starting square of the move to the indicated square.
  if (OneSqSet(start_sqs)) {
    move.start_sq = GetSqOfFirstPiece(start_sqs);
    return;
  }
  throw BadMove("ambiguous or illegal piece movement specified");
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
    // Handle the cases of unambiguous non-pawn moves without capture (ex:
    // Qe4) and non-capturing pawn move and promotion (ex: d8Q).
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
    // Handle the cases of pawn capture with promotion (ex: exd8Q) and
    // ambiguous non-pawn moves requiring both a specified start rank and file
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
