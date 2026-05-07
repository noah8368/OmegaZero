/* Noah Himed
 *
 * Implement the UciHandler type. Handles UCI protocol communication for
 * integration with chess GUIs and tournament managers like cutechess-cli.
 *
 * Licensed under MIT License. Terms and conditions enclosed in "LICENSE.txt".
 */

#include "uci.h"

#include <algorithm>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "board.h"
#include "engine.h"
#include "move.h"

namespace omegazero {

using std::string;
using std::vector;

UciHandler::UciHandler() {
  board_ = std::make_unique<Board>(kStartFen);
  engine_ = std::make_unique<Engine>(board_.get(), 'w', 5.0f);
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

  for (const string& uci_move : moves) {
    engine_->AddPosToHistory();
    Move move = ParseUciMove(uci_move);
    board_->MakeMove(move);
  }
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

  float think_time = ComputeThinkTime(wtime, btime, winc, binc,
                                       movetime, movestogo);
  engine_->SetSearchTime(think_time);

  Move best_move = engine_->GetBestMove();
  if (best_move.moving_piece == kNA && best_move.castling_type == kNA) {
    std::cout << "bestmove 0000" << std::endl;
  } else {
    std::cout << "bestmove " << MoveToUciStr(best_move) << std::endl;
  }
}

auto UciHandler::ComputeThinkTime(int wtime, int btime, int winc, int binc,
                                   int movetime, int movestogo) const -> float {
  constexpr float kMinTime = 0.1f;

  if (movetime > 0) {
    return std::max(kMinTime, static_cast<float>(movetime - 50) / 1000.0f);
  }

  S8 side = board_->GetPlayerToMove();
  int time_ms = (side == kWhite) ? wtime : btime;
  int inc_ms = (side == kWhite) ? winc : binc;

  if (time_ms <= 0) return kMinTime;

  if (movestogo > 0) {
    float alloc_ms = static_cast<float>(time_ms) / (movestogo + 1)
                     + inc_ms * 0.8f;
    return std::max(kMinTime, alloc_ms / 1000.0f);
  }

  float alloc_ms = static_cast<float>(time_ms) / 30.0f + inc_ms * 0.8f;
  float max_ms = static_cast<float>(time_ms) * 0.5f;
  alloc_ms = std::min(alloc_ms, max_ms);
  return std::max(kMinTime, alloc_ms / 1000.0f);
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

}  // namespace omegazero
