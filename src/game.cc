/* Noah Himed
 *
 * Implement the Game type.
 *
 * Licensed under MIT License. Terms and conditions enclosed in "LICENSE.txt".
 */

#include "game.h"

#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "bad_move.h"
#include "board.h"
#include "engine.h"
#include "move.h"
#include "nnue.h"
#include "syzygy.h"
#include "time_control.h"

namespace omegazero {

using std::cin;
using std::cout;
using std::endl;
using std::ifstream;
using std::invalid_argument;
using std::istringstream;
using std::mt19937;
using std::ofstream;
using std::ostringstream;
using std::random_device;
using std::string;
using std::uniform_int_distribution;
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
  switch (toupper(piece_ch)) {
    case 'N':
      return kKnight;
    case 'B':
      return kBishop;
    case 'R':
      return kRook;
    case 'Q':
      return kQueen;
    case 'K':
      return kKing;
    default:
      return kPawn;
  }
}

Game::Game(const string& init_pos, const string& opening_book_path,
           char player_side, float search_time, bool on_opening,
           bool light_theme, int num_threads)
    : board_(init_pos),
      engine_(pool_.GetTt(), &board_, player_side, search_time) {
  pool_.SetNumThreads(static_cast<S8>(num_threads));
  game_active_ = true;
  on_opening_ = on_opening;
  clock_mode_ = false;
  search_time_ = search_time;
  increment_ = 0.0f;
  clock_[kWhite] = 0.0f;
  clock_[kBlack] = 0.0f;
  turn_num_ = 1;
  winner_ = kNA;
  if (light_theme) {
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
  } else {
    piece_symbols_[kWhite][kPawn] = "♟";
    piece_symbols_[kWhite][kKnight] = "♞";
    piece_symbols_[kWhite][kBishop] = "♝";
    piece_symbols_[kWhite][kRook] = "♜";
    piece_symbols_[kWhite][kQueen] = "♛";
    piece_symbols_[kWhite][kKing] = "♚";
    piece_symbols_[kBlack][kPawn] = "♙";
    piece_symbols_[kBlack][kKnight] = "♘";
    piece_symbols_[kBlack][kBishop] = "♗";
    piece_symbols_[kBlack][kRook] = "♖";
    piece_symbols_[kBlack][kQueen] = "♕";
    piece_symbols_[kBlack][kKing] = "♔";
  }

  // Load PGN opening book.
  ifstream opening_book_f(opening_book_path);
  if (opening_book_f.is_open()) {
    string f_line;
    string move_text;
    bool in_moves = false;
    while (getline(opening_book_f, f_line)) {
      if (!f_line.empty() && f_line.back() == '\r') f_line.pop_back();
      if (f_line.empty()) {
        if (in_moves && !move_text.empty()) {
          // Parse move_text into individual moves.
          vector<string> moves;
          istringstream iss(move_text);
          string token;
          while (iss >> token) {
            // Skip move numbers (e.g. "1.", "12."), result markers, and NAGs.
            if (token.back() == '.' || token == "1/2-1/2" || token == "1-0" ||
                token == "0-1" || token == "*" || token[0] == '$') {
              continue;
            }
            moves.push_back(token);
          }
          if (!moves.empty()) opening_book_.push_back(moves);
          move_text.clear();
          in_moves = false;
        }
        continue;
      }
      if (f_line[0] == '[') continue;
      in_moves = true;
      move_text += " " + f_line;
    }
    // Handle last game if file doesn't end with blank line.
    if (in_moves && !move_text.empty()) {
      vector<string> moves;
      istringstream iss(move_text);
      string token;
      while (iss >> token) {
        if (token.back() == '.' || token == "1/2-1/2" || token == "1-0" ||
            token == "0-1" || token == "*" || token[0] == '$') {
          continue;
        }
        moves.push_back(token);
      }
      if (!moves.empty()) opening_book_.push_back(moves);
    }
    opening_book_f.close();
  } else {
    throw invalid_argument("Opening book can't be opened");
  }
}

auto Game::SetClock(float base_time, float increment) -> void {
  clock_mode_ = true;
  increment_ = increment;
  clock_[kWhite] = base_time;
  clock_[kBlack] = base_time;
}

auto Game::DisplayClock() const -> void {
  auto format_time = [](float seconds) -> string {
    int total = static_cast<int>(seconds);
    int h = total / 3600;
    int m = (total % 3600) / 60;
    int s = total % 60;
    int tenths = static_cast<int>((seconds - total) * 10);
    ostringstream oss;
    if (h > 0)
      oss << h << ":" << std::setfill('0') << std::setw(2) << m << ":"
          << std::setw(2) << s;
    else if (m > 0)
      oss << m << ":" << std::setfill('0') << std::setw(2) << s;
    else
      oss << s << "." << tenths;
    return oss.str();
  };
  cout << "  Clock: White " << format_time(clock_[kWhite])
       << " | Black " << format_time(clock_[kBlack]) << endl;
}

constexpr S8 kMaxMoveRep = 5;

auto Game::MakeEngineMove() -> Move {
  DisplayBoard();

  // Record the current board state to enforce move repitition rules.
  RecordBoardState();
  engine_.AddPosToHistory();

  Move engine_move;

  // Check the status of the game.
  S8 game_status = engine_.GetGameStatus();
  S8 player_to_move = board_.GetPlayerToMove();
  if (game_status == kPlayerInCheck) {
    // Inform the user that a player is in check.
    cout << GetPlayerStr(player_to_move) << " is in check" << endl;
  } else if (game_status == kDraw || pos_history_[board_] == kMaxMoveRep) {
    // End the game if a draw has occured.
    game_active_ = false;
    return engine_move;
  } else if (game_status == kPlayerCheckmated) {
    // Inform the user that a player has been mated.
    cout << GetPlayerStr(player_to_move) << " has been checkmated" << endl;
    game_active_ = false;
    winner_ = GetOtherPlayer(player_to_move);
    return engine_move;
  }

  engine_move = pool_.LazySmpSearch(engine_, board_, engine_.GetPosHistory());

  cout << "\n\n"
       << GetPlayerStr(player_to_move)
       << "'s move: " << GetFideMoveStr(engine_move) << endl;
  board_.MakeMove(engine_move);
  return engine_move;
}

auto Game::GetOpeningMove(Move& opening_move) -> bool {
  if (!on_opening_) return false;

  int num_played = static_cast<int>(played_fide_moves_.size());

  // Filter out openings that don't match all moves played so far.
  int last = static_cast<int>(opening_book_.size()) - 1;
  for (int i = last; i >= 0; --i) {
    const auto& line = opening_book_[i];
    bool mismatch = num_played >= static_cast<int>(line.size());
    if (!mismatch) {
      for (int j = 0; j < num_played; ++j) {
        if (line[j] != played_fide_moves_[j]) {
          mismatch = true;
          break;
        }
      }
    }
    // Also require a next move to exist.
    if (!mismatch && num_played >= static_cast<int>(line.size())) {
      mismatch = true;
    }
    if (mismatch) {
      opening_book_.erase(opening_book_.begin() + i);
    }
  }

  int num_lines = static_cast<int>(opening_book_.size());
  if (num_lines > 0) {
    random_device dev;
    mt19937 rng(dev());
    uniform_int_distribution<mt19937::result_type> dist(0, num_lines - 1);
    size_t line_idx = dist(rng);
    string opening_move_str = opening_book_[line_idx][num_played];
    opening_move = ParseMoveCmd(opening_move_str);
  } else {
    on_opening_ = false;
  }
  return on_opening_;
}

constexpr S8 kNumMoveRepForOptionalDraw = 3;

void Game::Play() {
  DisplayBoard();
  if (clock_mode_) DisplayClock();

  // Record the current board state to enforce move repitition rules.
  RecordBoardState();
  engine_.AddPosToHistory();

  // Check the status of the game.
  S8 game_status = engine_.GetGameStatus();
  S8 player_to_move = board_.GetPlayerToMove();
  S8 user_side = engine_.GetUserSide();
  if (game_status == kPlayerInCheck) {
    cout << GetPlayerStr(player_to_move) << " is in check" << endl;
  } else if (game_status == kDraw || pos_history_[board_] == kMaxMoveRep) {
    game_active_ = false;
    RecordFinalScore();
    return;
  } else if (pos_history_[board_] == kNumMoveRepForOptionalDraw &&
             player_to_move == user_side) {
    string draw_decision;
    cout << "Threefold repitition detected. "
         << "Would you like to claim a draw? (y/): ";
    getline(cin, draw_decision);
    if (draw_decision == "y") {
      game_active_ = false;
      RecordFinalScore();
      return;
    }
  } else if (game_status == kPlayerCheckmated) {
    cout << GetPlayerStr(player_to_move) << " has been checkmated" << endl;
    game_active_ = false;
    winner_ = GetOtherPlayer(player_to_move);
    RecordFinalScore();
    return;
  }

  // Snapshot the state a single ply mutates so the move can be pushed onto
  // ply_stack_ (and later reversed by UndoLastUserMove) once it is played.
  Move made_move;
  size_t undo_move_history_len = move_history_.size();
  size_t undo_played_moves_len = played_fide_moves_.size();
  int undo_turn_num = turn_num_;
  float undo_clock_white = clock_[kWhite];
  float undo_clock_black = clock_[kBlack];

  string move_str;
  if (player_to_move == user_side) {
    string player_name = GetPlayerStr(player_to_move);
    Move user_move;
    cout << "\n\n" << player_name << " to move" << endl;

    using clock = std::chrono::high_resolution_clock;
    auto move_start = clock::now();

    for (;;) {
      cout << "Enter move ('u' to undo, 'r' to resign): ";
      getline(cin, move_str);

      if (move_str == "r") {
        game_active_ = false;
        winner_ = GetOtherPlayer(player_to_move);
        RecordFinalScore();
        return;
      }
      if (move_str == "u") {
        if (UndoLastUserMove()) {
          // The board is back at the user's previous turn; re-enter Play() so it
          // re-displays and re-records the reverted position.
          return;
        }
        cout << "Nothing to undo." << endl;
        continue;
      }
      try {
        user_move = ParseMoveCmd(move_str);
        board_.MakeMove(user_move);
        made_move = user_move;
        break;
      } catch (BadMove& e) {
        cout << "ERROR: Bad Move: " << e.what() << endl;
      }
    }

    if (clock_mode_) {
      auto elapsed = std::chrono::duration<float>(clock::now() - move_start);
      clock_[player_to_move] -= elapsed.count();
      if (clock_[player_to_move] <= 0.0f) {
        cout << GetPlayerStr(player_to_move) << " lost on time" << endl;
        game_active_ = false;
        winner_ = GetOtherPlayer(player_to_move);
        RecordFinalScore();
        return;
      }
      clock_[player_to_move] += increment_;
    }
  } else {
    Move engine_move;
    if (!GetOpeningMove(engine_move)) {
      if (clock_mode_) {
        TimeBounds bounds = ComputeTimeBounds(clock_[player_to_move] * 1000.0f,
                                              increment_ * 1000.0f, 0, 0);
        engine_.SetTimeBounds(bounds.soft, bounds.hard, bounds.base);
      }

      using clock = std::chrono::high_resolution_clock;
      auto move_start = clock::now();
      engine_move =
          pool_.LazySmpSearch(engine_, board_, engine_.GetPosHistory());

      if (clock_mode_) {
        auto elapsed = std::chrono::duration<float>(clock::now() - move_start);
        clock_[player_to_move] -= elapsed.count();
        if (clock_[player_to_move] <= 0.0f) {
          cout << GetPlayerStr(player_to_move) << " lost on time" << endl;
          game_active_ = false;
          winner_ = GetOtherPlayer(player_to_move);
          RecordFinalScore();
          return;
        }
      }
    }
    if (clock_mode_) {
      clock_[player_to_move] += increment_;
    }
    move_str = GetFideMoveStr(engine_move);
    cout << "\n\n"
         << GetPlayerStr(player_to_move) << "'s move: " << move_str << endl;
    board_.MakeMove(engine_move);
    made_move = engine_move;
  }
  UpdateMoveHistory(move_str);

  // Record this ply so a subsequent 'u' at the user's prompt can take it back.
  ply_stack_.push_back({made_move, undo_move_history_len, undo_played_moves_len,
                        undo_turn_num, undo_clock_white, undo_clock_black});
}

auto Game::UndoLastUserMove() -> bool {
  // A full round (the engine's reply plus the user's own move) is needed to give
  // the user back a position they can move from. With fewer plies there is no
  // prior user decision to replay.
  if (ply_stack_.size() < 2) {
    return false;
  }

  // Called from the user's move prompt, where the top of this Play() has already
  // recorded the present position (RecordBoardState + Engine::AddPosToHistory)
  // without a move yet. Drop those so re-entering Play() re-records cleanly.
  DecrementPosHistory();
  engine_.PopPosFromHistory();

  // Reverse the engine's reply, then the user's previous move.
  for (int ply = 0; ply < 2; ++ply) {
    const PlyRecord& record = ply_stack_.back();
    board_.UnmakeMove(record.move);
    // After UnmakeMove the board sits at this ply's pre-move position, which its
    // own Play() had recorded at the top; roll that increment back too.
    DecrementPosHistory();
    engine_.PopPosFromHistory();
    move_history_.resize(record.move_history_len);
    played_fide_moves_.resize(record.played_moves_len);
    turn_num_ = record.turn_num;
    clock_[kWhite] = record.clock_white;
    clock_[kBlack] = record.clock_black;
    ply_stack_.pop_back();
  }

  // The opening-book pruning in GetOpeningMove() is destructive and not worth
  // reconstructing; stay out of book for the remainder of the game after an undo.
  on_opening_ = false;
  return true;
}

auto Game::SavePgn(const string& opponent_name) -> void {
  time_t now = time(nullptr);
  tm* lt = localtime(&now);
  char date_str[11];
  strftime(date_str, sizeof(date_str), "%Y.%m.%d", lt);
  char time_str[9];
  strftime(time_str, sizeof(time_str), "%H:%M:%S", lt);
  char datetime_str[20];
  strftime(datetime_str, sizeof(datetime_str), "%Y-%m-%d_%H%M%S", lt);

  S8 user_side = engine_.GetUserSide();
  string white = (user_side == kWhite) ? opponent_name : "OmegaZero";
  string black = (user_side == kBlack) ? opponent_name : "OmegaZero";

  string result;
  if (winner_ == kWhite)
    result = "1-0";
  else if (winner_ == kBlack)
    result = "0-1";
  else
    result = "1/2-1/2";

  std::filesystem::create_directory("games");
  string filename =
      "games/" + opponent_name + "_v_OmegaZero_" + datetime_str + ".pgn";
  ofstream f(filename);
  if (!f.is_open()) {
    throw invalid_argument("PGN file can't be created");
  }

  f << "[Event \"" << opponent_name << " v OmegaZero " << date_str << " "
    << time_str << "\"]\n";
  f << "[Site \"?\"]\n";
  f << "[Date \"" << date_str << "\"]\n";
  f << "[White \"" << white << "\"]\n";
  f << "[Black \"" << black << "\"]\n";
  f << "[Result \"" << result << "\"]\n\n";

  // Note which of OmegaZero's evaluation and endgame resources were in play.
  string eval_str = g_nnue.IsLoaded() ? "NNUE" : "HCE";
  string syzygy_str;
  if (!g_syzygy.IsLoaded()) {
    syzygy_str = "not loaded";
  } else if (engine_.SyzygyUsed()) {
    syzygy_str = "used";
  } else {
    syzygy_str = "loaded but not reached";
  }
  f << "{ OmegaZero: evaluation = " << eval_str << "; Syzygy tablebases = "
    << syzygy_str << ". }\n\n";

  f << move_history_ << "\n\n";

  f << "{ Final position:\n";
  for (S8 rank = kRank8; rank >= kRank1; --rank) {
    f << "  " << static_cast<int>(rank + 1) << " ";
    for (S8 file = kFileA; file <= kFileH; ++file) {
      S8 sq = GetSqFromRankFile(rank, file);
      S8 piece = board_.GetPieceOnSq(sq);
      S8 player = board_.GetPlayerOnSq(sq);
      if (player == kNA && piece == kNA) {
        f << ". ";
      } else {
        f << piece_symbols_[player][piece] << " ";
      }
    }
    f << "\n";
  }
  f << "  A B C D E F G H }\n";

  f.close();

  cout << "PGN saved to " << filename << endl;
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

  move.moving_piece =
      isupper(user_cmd[0]) ? GetPieceType(user_cmd[0]) : static_cast<S8>(kPawn);

  string cmd = user_cmd;
  for (char& ch : cmd) {
    ch = tolower(ch);
  }

  bool capture_indicated = false;
  S8 start_rank = kNA;
  S8 start_file = kNA;
  S8 target_rank;
  S8 target_file;
  // Collect info from a move command formatted in FIDE algebraic notation.
  InterpAlgNotation(cmd, move, start_rank, start_file, target_rank, target_file,
                    capture_indicated);
  // Check a few requirements for the move's pseudo-legality.
  CheckMove(move, start_rank, start_file, target_rank, target_file,
            capture_indicated);
  // Check that there is exactly one possible start square for the move, and
  // set the move's start square to this square if so.
  AddStartSqToMove(move, start_rank, start_file, target_rank, target_file,
                   capture_indicated);
  return move;
}

auto Game::GetFideMoveStr(const Move& move) -> string {
  string move_str;
  if (move.castling_type == kNA) {
    S8 start_file = GetFileFromSq(move.start_sq);
    S8 target_file = GetFileFromSq(move.target_sq);
    S8 target_rank = GetRankFromSq(move.target_sq);
    if (move.moving_piece == kPawn && move.captured_piece != kNA) {
      move_str += static_cast<char>(start_file + 'a');
      move_str += 'x';
    } else if (move.moving_piece != kPawn) {
      move_str += GetPieceLetter(move.moving_piece);

      // Add clarifying information to the move string if the move is
      // ambiguous.
      S8 moving_player = board_.GetPlayerToMove();
      Bitboard start_sqs =
          board_.GetAttackMap(moving_player, move.target_sq, move.moving_piece);
      start_sqs &= board_.GetPiecesByType(move.moving_piece, moving_player);
      if (!OneSqSet(start_sqs)) {
        S8 start_rank = GetRankFromSq(move.start_sq);
        if (OneSqSet(start_sqs & kRankMasks[start_rank])) {
          move_str += static_cast<char>(start_rank + '1');
        } else if (OneSqSet(start_sqs & kFileMasks[start_file])) {
          move_str += static_cast<char>(start_file + 'a');
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
          move_str += 'n';
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

    if (!capture_indicated) {
      S8 behind_rank =
          player_to_move == kWhite ? target_rank - 1 : target_rank + 1;
      if (behind_rank >= kRank1 && behind_rank <= kRank8) {
        S8 behind = GetSqFromRankFile(behind_rank, target_file);
        if (board_.GetPieceOnSq(behind) == kPawn &&
            board_.GetPlayerOnSq(behind) == player_to_move) {
          move.start_sq = behind;
          return;
        }
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
  bool flipped = engine_.GetUserSide() == kBlack;
  S8 rank_start = flipped ? kRank1 : kRank8;
  S8 rank_end = flipped ? kRank8 : kRank1;
  S8 rank_step = flipped ? 1 : -1;
  for (S8 rank = rank_start; rank != rank_end + rank_step; rank += rank_step) {
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
    // Handle the cases of pawn capture with promotion (ex: exd8Q),
    // ambiguous non-pawn moves requiring both a specified start rank and file
    // (ex: Qh4e1), and ambiguous non-pawn capture with specified start rank
    // or file (ex: N7xf6).
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
        if (user_cmd[2] == 'x') {
          capture_indicated = true;
          char second_ch = user_cmd[1];
          if (second_ch - '1' >= kRank1 && second_ch - '1' <= kRank8) {
            start_rank = static_cast<S8>(second_ch - '1');
          } else if (second_ch - 'a' >= kFileA && second_ch - 'a' <= kFileH) {
            start_file = static_cast<S8>(second_ch - 'a');
          } else {
            throw BadMove("bad command formatting");
          }
          target_file = static_cast<S8>(user_cmd[3] - 'a');
          target_rank = static_cast<S8>(user_cmd[4] - '1');
        } else {
          start_file = static_cast<S8>(user_cmd[1] - 'a');
          start_rank = static_cast<S8>(user_cmd[2] - '1');
          target_file = static_cast<S8>(user_cmd[3] - 'a');
          target_rank = static_cast<S8>(user_cmd[4] - '1');
        }
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

  if (!RankOnBoard(target_rank) || !FileOnBoard(target_file)) {
    throw BadMove("bad command formatting");
  }
  move.target_sq = GetSqFromRankFile(target_rank, target_file);
}

auto Game::UpdateMoveHistory(string move_str) -> void {
  played_fide_moves_.push_back(move_str);

  S8 moved_player = GetOtherPlayer(board_.GetPlayerToMove());
  if (moved_player == kWhite) {
    move_history_ += to_string(turn_num_) + "." + move_str;
  } else {
    move_history_ += move_str;
    ++turn_num_;
  }

  // Add check and mate indicators.
  S8 game_status = engine_.GetGameStatus();
  if (game_status == kPlayerInCheck) {
    move_history_ += "+ ";
  } else if (game_status == kPlayerCheckmated) {
    move_history_ += "# ";
  } else {
    move_history_ += " ";
  }
}

}  // namespace omegazero
