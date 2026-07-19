/* Noah Himed
 *
 * Implement the UciHandler type. Handles UCI protocol communication for
 * integration with chess GUIs and tournament managers like cutechess-cli.
 *
 * Licensed under MIT License. Terms and conditions enclosed in "LICENSE.txt".
 */

#include "uci.h"

#include <fstream>
#include <iostream>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "board.h"
#include "engine.h"
#include "game.h"
#include "move.h"
#include "time_control.h"

namespace omegazero {

using std::string;
using std::vector;

UciHandler::UciHandler(const string& book_path)
    : book_path_(book_path), turn_num_(1), move_index_(0),
      on_opening_(true) {
  board_ = std::make_unique<Board>(kStartFen);
  engine_ = std::make_unique<Engine>(board_.get(), 'w', 5.0f);
  LoadOpeningBook(book_path_);
}

auto UciHandler::Run() -> void {
  string line;
  while (std::getline(std::cin, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();

    if (line == "uci") {
      HandleUci();
    } else if (line == "isready") {
      HandleIsReady();
    } else if (line == "ucinewgame") {
      HandleUciNewGame();
    } else if (line.rfind("position", 0) == 0) {
      HandlePosition(line);
    } else if (line.rfind("go", 0) == 0) {
      HandleGo(line);
    } else if (line == "stop") {
      // Single-threaded: search already finished and bestmove was sent.
      // Sending another bestmove here would desync the protocol.
    } else if (line == "quit") {
      break;
    }
  }
}

auto UciHandler::HandleUci() -> void {
  std::cout << "id name OmegaZero" << std::endl;
  std::cout << "id author Noah Himed" << std::endl;
  std::cout << "uciok" << std::endl;
}

auto UciHandler::HandleIsReady() -> void {
  std::cout << "readyok" << std::endl;
}

auto UciHandler::HandleUciNewGame() -> void {
  board_ = std::make_unique<Board>(kStartFen);
  engine_ = std::make_unique<Engine>(board_.get(), 'w', 5.0f);
  turn_num_ = 1;
  move_index_ = 0;
  on_opening_ = true;
  LoadOpeningBook(book_path_);
}

auto UciHandler::HandlePosition(const string& line) -> void {
  std::istringstream iss(line);
  string token;
  iss >> token;  // "position"

  string fen;
  iss >> token;
  if (token == "startpos") {
    fen = kStartFen;
  } else if (token == "fen") {
    vector<string> fen_parts;
    while (iss >> token && token != "moves") {
      fen_parts.push_back(token);
    }
    for (size_t i = 0; i < fen_parts.size(); ++i) {
      if (i > 0) fen += ' ';
      fen += fen_parts[i];
    }
    if (token == "moves") {
      vector<string> moves;
      while (iss >> token) moves.push_back(token);
      SetPosition(fen, moves);
      return;
    }
    SetPosition(fen, {});
    return;
  }

  vector<string> moves;
  if (iss >> token && token == "moves") {
    while (iss >> token) moves.push_back(token);
  }
  SetPosition(fen, moves);
}

auto UciHandler::SetPosition(const string& fen,
                             const vector<string>& moves) -> void {
  board_ = std::make_unique<Board>(fen);
  engine_ = std::make_unique<Engine>(board_.get(), 'w', 5.0f);
  turn_num_ = 1;
  move_index_ = 0;
  on_opening_ = true;

  // Filter opening book as each move is replayed.
  LoadOpeningBook(book_path_);

  for (const string& uci_move : moves) {
    engine_->AddPosToHistory();
    Move move = ParseUciMove(uci_move);
    string fide_str = MoveToFideStr(move);
    board_->MakeMove(move);

    // Filter out openings that don't match this move.
    int last = static_cast<int>(opening_book_.size()) - 1;
    for (int i = last; i >= 0; --i) {
      const auto& line = opening_book_[i];
      if (move_index_ >= static_cast<int>(line.size()) ||
          line[move_index_] != fide_str) {
        opening_book_.erase(opening_book_.begin() + i);
      }
    }
    ++move_index_;

    S8 moved_player = GetOtherPlayer(board_->GetPlayerToMove());
    if (moved_player == kBlack) ++turn_num_;
  }

  if (opening_book_.empty()) on_opening_ = false;
  engine_->AddPosToHistory();
}

auto UciHandler::HandleGo(const string& line) -> void {
  std::istringstream iss(line);
  string token;
  iss >> token;  // "go"

  int wtime = 0, btime = 0, winc = 0, binc = 0, movetime = 0, movestogo = 0;
  while (iss >> token) {
    if (token == "wtime") iss >> wtime;
    else if (token == "btime") iss >> btime;
    else if (token == "winc") iss >> winc;
    else if (token == "binc") iss >> binc;
    else if (token == "movetime") iss >> movetime;
    else if (token == "movestogo") iss >> movestogo;
  }

  Move book_move;
  if (on_opening_ && GetBookMove(book_move)) {
    std::cout << "bestmove " << MoveToUciStr(book_move) << std::endl;
    return;
  }

  S8 side = board_->GetPlayerToMove();
  float remaining_ms = static_cast<float>((side == kWhite) ? wtime : btime);
  float inc_ms = static_cast<float>((side == kWhite) ? winc : binc);
  TimeBounds bounds =
      ComputeTimeBounds(remaining_ms, inc_ms, movestogo, movetime);
  engine_->SetTimeBounds(bounds.soft, bounds.hard);

  Move best_move = engine_->GetBestMove();
  if (best_move.moving_piece == kNA && best_move.castling_type == kNA) {
    std::cout << "bestmove 0000" << std::endl;
  } else {
    std::cout << "bestmove " << MoveToUciStr(best_move) << std::endl;
  }
}

auto UciHandler::MoveToUciStr(const Move& move) const -> string {
  string s;
  if (move.castling_type == kNA) {
    s += static_cast<char>('a' + GetFileFromSq(move.start_sq));
    s += static_cast<char>('1' + GetRankFromSq(move.start_sq));
    s += static_cast<char>('a' + GetFileFromSq(move.target_sq));
    s += static_cast<char>('1' + GetRankFromSq(move.target_sq));
    if (move.promoted_to_piece != kNA) {
      switch (move.promoted_to_piece) {
        case kKnight: s += 'n'; break;
        case kBishop: s += 'b'; break;
        case kRook:   s += 'r'; break;
        case kQueen:  s += 'q'; break;
        default:
          throw std::invalid_argument(
              "promoted_to_piece in UciHandler::MoveToUciStr()");
      }
    }
  } else {
    S8 player = board_->GetPlayerToMove();
    if (move.castling_type == kQueenSide) {
      s = (player == kWhite) ? "e1c1" : "e8c8";
    } else {
      s = (player == kWhite) ? "e1g1" : "e8g8";
    }
  }
  return s;
}

auto UciHandler::ParseUciMove(const string& uci_move) const -> Move {
  S8 start_file = static_cast<S8>(uci_move[0] - 'a');
  S8 start_rank = static_cast<S8>(uci_move[1] - '1');
  S8 target_file = static_cast<S8>(uci_move[2] - 'a');
  S8 target_rank = static_cast<S8>(uci_move[3] - '1');
  S8 start_sq = GetSqFromRankFile(start_rank, start_file);
  S8 target_sq = GetSqFromRankFile(target_rank, target_file);

  S8 promotion = kNA;
  if (uci_move.size() > 4) {
    switch (uci_move[4]) {
      case 'n': promotion = kKnight; break;
      case 'b': promotion = kBishop; break;
      case 'r': promotion = kRook; break;
      case 'q': promotion = kQueen; break;
      default:
        throw std::invalid_argument("Invalid promotion piece in UCI move");
    }
  }

  vector<Move> moves = engine_->GenerateMoves();
  for (const Move& m : moves) {
    if (m.castling_type != kNA) {
      S8 player = board_->GetPlayerToMove();
      S8 king_start = (player == kWhite) ? kSqE1 : kSqE8;
      S8 king_target;
      if (m.castling_type == kQueenSide) {
        king_target = (player == kWhite) ? kSqC1 : kSqC8;
      } else {
        king_target = (player == kWhite) ? kSqG1 : kSqG8;
      }
      if (start_sq == king_start && target_sq == king_target) {
        return m;
      }
    } else {
      if (m.start_sq == start_sq && m.target_sq == target_sq) {
        if (promotion == kNA && m.promoted_to_piece == kNA) return m;
        if (promotion != kNA && m.promoted_to_piece == promotion) return m;
      }
    }
  }

  throw std::invalid_argument("UCI move not found in legal moves: " +
                              uci_move);
}

auto UciHandler::MoveToFideStr(const Move& move) const -> string {
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

      S8 moving_player = board_->GetPlayerToMove();
      Bitboard start_sqs =
          board_->GetAttackMap(moving_player, move.target_sq, move.moving_piece);
      start_sqs &= board_->GetPiecesByType(move.moving_piece, moving_player);
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
  }
  return move_str;
}

auto UciHandler::LoadOpeningBook(const string& path) -> void {
  opening_book_.clear();
  std::ifstream f(path);
  if (!f.is_open()) return;

  string f_line;
  string move_text;
  bool in_moves = false;
  while (std::getline(f, f_line)) {
    if (!f_line.empty() && f_line.back() == '\r') f_line.pop_back();
    if (f_line.empty()) {
      if (in_moves && !move_text.empty()) {
        vector<string> moves;
        std::istringstream iss(move_text);
        string token;
        while (iss >> token) {
          if (token.back() == '.' || token == "1/2-1/2" ||
              token == "1-0" || token == "0-1" || token == "*" ||
              token[0] == '$') {
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
  if (in_moves && !move_text.empty()) {
    vector<string> moves;
    std::istringstream iss(move_text);
    string token;
    while (iss >> token) {
      if (token.back() == '.' || token == "1/2-1/2" ||
          token == "1-0" || token == "0-1" || token == "*" ||
          token[0] == '$') {
        continue;
      }
      moves.push_back(token);
    }
    if (!moves.empty()) opening_book_.push_back(moves);
  }
}

auto UciHandler::GetBookMove(Move& book_move) -> bool {
  // Filter to openings that have a move at the current index.
  int last = static_cast<int>(opening_book_.size()) - 1;
  for (int i = last; i >= 0; --i) {
    if (move_index_ >= static_cast<int>(opening_book_[i].size())) {
      opening_book_.erase(opening_book_.begin() + i);
    }
  }

  int num_lines = static_cast<int>(opening_book_.size());
  if (num_lines == 0) {
    on_opening_ = false;
    return false;
  }

  std::random_device dev;
  std::mt19937 rng(dev());
  std::uniform_int_distribution<int> dist(0, num_lines - 1);
  const string& fide_move_str = opening_book_[dist(rng)][move_index_];

  vector<Move> moves = engine_->GenerateMoves();
  for (const Move& m : moves) {
    if (MoveToFideStr(m) == fide_move_str) {
      book_move = m;
      ++move_index_;
      return true;
    }
  }

  on_opening_ = false;
  return false;
}

}  // namespace omegazero
