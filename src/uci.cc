/* Noah Himed
 *
 * Implement the UciHandler type. Handles UCI protocol communication for
 * integration with chess GUIs and tournament managers like cutechess-cli.
 *
 * Licensed under MIT License. Terms and conditions enclosed in "LICENSE.txt".
 */

#include "uci.h"

#include <algorithm>
#include <cstdlib>
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
#include "params.h"
#include "syzygy.h"
#include "time_control.h"

namespace omegazero {

using std::string;
using std::vector;

UciHandler::UciHandler(const string& book_path, const string& params_path)
    : book_path_(book_path), turn_num_(1), move_index_(0),
      on_opening_(true), pondering_(false), ponder_soft_(0.0f),
      ponder_hard_(0.0f) {
  board_ = std::make_unique<Board>(kStartFen);
  engine_ = std::make_unique<Engine>(pool_.GetTt(), board_.get(), 'w', 5.0f);
  engine_->SetInfoCallback(
      [this](const SearchInfo& info) { PrintInfo(info); });
  // Seed the runtime search parameters from params.json (profile matching the
  // active eval mode). `setoption` overrides these; a missing file/profile
  // leaves the built-in SearchParams defaults in place.
  LoadProfileInto(params_path, ProfileForEvalMode(), uci_params_);
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
    } else if (line.rfind("setoption", 0) == 0) {
      HandleSetOption(line);
    } else if (line.rfind("go", 0) == 0) {
      HandleGo(line);
    } else if (line == "ponderhit") {
      // The pondered move was played: give the running search its real budget.
      HandlePonderHit();
    } else if (line == "stop") {
      // Abort the running search; the worker prints its `bestmove` as it exits.
      // A `stop` during ponder means the guess was wrong, so end pondering.
      pondering_ = false;
      StopSearch();
    } else if (line == "quit") {
      break;
    }
  }
  // Join any worker before returning (covers `quit` and stdin closing mid-go).
  StopSearch();
}

auto UciHandler::HandleUci() -> void {
  std::cout << "id name OmegaZero" << std::endl;
  std::cout << "id author Noah Himed" << std::endl;
  // Advertise the tunable search parameters as spin options (SPSA targets).
  for (const IntOpt& o : kIntOpts) {
    std::cout << "option name " << o.name << " type spin default " << o.def
              << " min " << o.min << " max " << o.max << std::endl;
  }
  for (const DblOpt& o : kDblOpts) {
    std::cout << "option name " << o.name << " type spin default " << o.def
              << " min " << o.min << " max " << o.max << std::endl;
  }
  // Lazy SMP worker count (1 = single-threaded, the untouched search path);
  // defaults to the machine's core count.
  std::cout << "option name Threads type spin default " << num_threads_
            << " min 1 max " << kMaxThreads << std::endl;
  // Syzygy endgame tablebases (empty = disabled); path to the .rtbw/.rtbz files.
  std::cout << "option name SyzygyPath type string default <empty>"
            << std::endl;
  std::cout << "uciok" << std::endl;
}

auto UciHandler::HandleSetOption(const string& line) -> void {
  std::istringstream iss(line);
  string token;
  iss >> token;  // "setoption"
  iss >> token;  // "name"
  // Option names have no spaces here, but parse defensively: collect name tokens
  // up to "value".
  string name;
  while (iss >> token && token != "value") {
    if (!name.empty()) name += ' ';
    name += token;
  }
  if (name == "SyzygyPath") {
    // A filesystem path (may contain spaces), so take the rest of the line
    // verbatim rather than a single token. Empty clears/disables the tables.
    string path;
    std::getline(iss, path);
    size_t start = path.find_first_not_of(' ');
    g_syzygy.Init(start == string::npos ? "" : path.substr(start));
    return;
  }
  string value_str;
  if (!(iss >> value_str)) return;  // spin options require a value
  int value;
  try {
    value = std::stoi(value_str);
  } catch (const std::exception&) {
    return;  // ignore malformed values
  }

  if (name == "Threads") {
    num_threads_ = std::clamp(value, 1, kMaxThreads);
    pool_.SetNumThreads(static_cast<S8>(num_threads_));
    return;
  }
  for (const IntOpt& o : kIntOpts) {
    if (name == o.name) {
      uci_params_.*o.field = std::clamp(value, o.min, o.max);
      return;
    }
  }
  for (const DblOpt& o : kDblOpts) {
    if (name == o.name) {
      uci_params_.*o.field =
          std::clamp(value, o.min, o.max) / static_cast<double>(o.divisor);
      return;
    }
  }
  // Unknown option: ignore (per UCI, engines may silently drop unknown options).
}

auto UciHandler::HandleIsReady() -> void {
  // May arrive while a search worker is running; serialize with its `bestmove`.
  std::lock_guard<std::mutex> lock(cout_mutex_);
  std::cout << "readyok" << std::endl;
}

auto UciHandler::HandleUciNewGame() -> void {
  // The worker holds engine_/board_; stop it before replacing them.
  StopSearch();
  // The shared TT (owned by pool_) outlives engine_ re-creation, so a new game
  // must clear it explicitly (recreating the Engine no longer resets the table).
  pool_.GetTt()->Clear();
  board_ = std::make_unique<Board>(kStartFen);
  engine_ = std::make_unique<Engine>(pool_.GetTt(), board_.get(), 'w', 5.0f);
  engine_->SetInfoCallback(
      [this](const SearchInfo& info) { PrintInfo(info); });
  turn_num_ = 1;
  move_index_ = 0;
  on_opening_ = true;
  LoadOpeningBook(book_path_);
}

auto UciHandler::HandlePosition(const string& line) -> void {
  // The worker mutates board_; stop it before changing the position.
  StopSearch();
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
  engine_ = std::make_unique<Engine>(pool_.GetTt(), board_.get(), 'w', 5.0f);
  engine_->SetInfoCallback(
      [this](const SearchInfo& info) { PrintInfo(info); });
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
  // Finish any previous search before configuring a new one.
  StopSearch();

  std::istringstream iss(line);
  string token;
  iss >> token;  // "go"

  int wtime = 0, btime = 0, winc = 0, binc = 0, movetime = 0, movestogo = 0;
  int depth = 0, mate = 0;
  long long nodes = 0;
  bool infinite = false, ponder = false;
  vector<string> searchmove_strs;
  while (iss >> token) {
    if (token == "wtime") iss >> wtime;
    else if (token == "btime") iss >> btime;
    else if (token == "winc") iss >> winc;
    else if (token == "binc") iss >> binc;
    else if (token == "movetime") iss >> movetime;
    else if (token == "movestogo") iss >> movestogo;
    else if (token == "depth") iss >> depth;
    else if (token == "nodes") iss >> nodes;
    else if (token == "mate") iss >> mate;
    else if (token == "infinite") infinite = true;
    else if (token == "ponder") ponder = true;
    // `searchmoves` is a move list running to the end of the command (GUIs send
    // it last); consume all remaining tokens as candidate moves.
    else if (token == "searchmoves") {
      while (iss >> token) searchmove_strs.push_back(token);
    }
  }

  // While pondering the engine must not answer until `ponderhit`/`stop`, so it
  // searches rather than replying instantly from book.
  Move book_move;
  if (!ponder && on_opening_ && GetBookMove(book_move)) {
    std::lock_guard<std::mutex> lock(cout_mutex_);
    std::cout << "bestmove " << MoveToUciStr(book_move) << std::endl;
    return;
  }

  // Configure the search's stopping conditions. `infinite`, or a bare depth/node
  // limit with no clock, runs without a wall-clock bound; otherwise fall back to
  // the time-control heuristic. A time setter resets the depth/node caps, so the
  // explicit SetDepthLimit/SetNodeLimit calls below must come last.
  S8 side = board_->GetPlayerToMove();
  float remaining_ms = static_cast<float>((side == kWhite) ? wtime : btime);
  float inc_ms = static_cast<float>((side == kWhite) ? winc : binc);
  bool has_clock = (movetime > 0 || wtime > 0 || btime > 0);
  pondering_ = false;
  if (ponder) {
    // Search indefinitely until `ponderhit`/`stop`; stash the per-move budget
    // (in seconds) to impose via PonderHit() when the guess is confirmed.
    TimeBounds bounds =
        ComputeTimeBounds(remaining_ms, inc_ms, movestogo, movetime);
    ponder_soft_ = bounds.soft;
    ponder_hard_ = bounds.hard;
    pondering_ = true;
    engine_->SetInfiniteSearch();
  } else if (infinite || (!has_clock && (depth > 0 || nodes > 0 || mate > 0))) {
    engine_->SetInfiniteSearch();
  } else if (movetime > 0) {
    // A fixed per-move request is honored exactly, not difficulty-scaled.
    TimeBounds bounds =
        ComputeTimeBounds(remaining_ms, inc_ms, movestogo, movetime);
    engine_->SetSearchTime(bounds.hard);
  } else {
    TimeBounds bounds =
        ComputeTimeBounds(remaining_ms, inc_ms, movestogo, movetime);
    engine_->SetTimeBounds(bounds.soft, bounds.hard, bounds.base);
  }
  if (depth > 0) engine_->SetDepthLimit(depth);
  if (nodes > 0) engine_->SetNodeLimit(static_cast<uint64_t>(nodes));

  // Root-move restriction and mate-search target (reset to none/0 each `go`).
  vector<Move> search_moves;
  for (const string& ms : searchmove_strs) {
    try {
      search_moves.push_back(ParseUciMove(ms));
    } catch (const std::invalid_argument&) {
      // Ignore an unparleable/illegal searchmove rather than aborting the go.
    }
  }
  engine_->SetSearchMoves(search_moves);
  engine_->SetMateTarget(mate);
  // Apply the current UCI option values (they persist across engine_ re-creation
  // and any setoption since the last search).
  engine_->SetParams(uci_params_);

  // Search on a worker thread so the main loop can keep reading stdin and honor
  // `stop` (required for `go infinite`).
  search_thread_ = std::thread(&UciHandler::RunSearch, this);
}

auto UciHandler::RunSearch() -> void {
  // Lazy SMP: run engine_ as the main search with helper threads sharing the TT
  // (a no-op at 1 thread, leaving the single-threaded path unchanged).
  Move best_move =
      pool_.LazySmpSearch(*engine_, *board_, engine_->GetPosHistory());

  std::lock_guard<std::mutex> lock(cout_mutex_);
  if (best_move.IsEmpty()) {
    std::cout << "bestmove 0000" << std::endl;
    return;
  }
  std::cout << "bestmove " << MoveToUciStr(best_move);
  // Offer the predicted opponent reply (PV[1]) so the GUI can ponder. It is the
  // other side's move, so render castling from that perspective.
  Move ponder_move = engine_->GetPonderMove();
  if (!ponder_move.IsEmpty()) {
    std::cout << " ponder "
              << MoveToUciStr(ponder_move,
                              GetOtherPlayer(board_->GetPlayerToMove()));
  }
  std::cout << std::endl;
}

auto UciHandler::HandlePonderHit() -> void {
  if (pondering_ && search_thread_.joinable()) {
    engine_->PonderHit(ponder_soft_, ponder_hard_);
  }
  pondering_ = false;
}

auto UciHandler::StopSearch() -> void {
  if (search_thread_.joinable()) {
    engine_->RequestStop();
    search_thread_.join();
  }
}

auto UciHandler::MoveToUciStr(const Move& move) const -> string {
  return MoveToUciStr(move, board_->GetPlayerToMove());
}

auto UciHandler::MoveToUciStr(const Move& move, S8 player) const -> string {
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
    if (move.castling_type == kQueenSide) {
      s = (player == kWhite) ? "e1c1" : "e8c8";
    } else {
      s = (player == kWhite) ? "e1g1" : "e8g8";
    }
  }
  return s;
}

auto UciHandler::PrintInfo(const SearchInfo& info) -> void {
  std::ostringstream out;
  out << "info depth " << info.depth << " score ";
  if (IsMateScore(info.score)) {
    // Convert an internal mate score to UCI mate-in-N *moves* (positive = we
    // deliver mate, negative = we are mated).
    int mate_plies = kBestEval - std::abs(info.score);
    int mate_moves = (mate_plies + 1) / 2;
    out << "mate " << (info.score > 0 ? mate_moves : -mate_moves);
  } else {
    out << "cp " << info.score;
  }
  long long nps =
      info.time_ms > 0 ? info.nodes * 1000 / info.time_ms : 0;
  out << " nodes " << info.nodes << " nps " << nps << " time " << info.time_ms;

  if (info.pv_len > 0) {
    S8 root_side = board_->GetPlayerToMove();
    out << " pv";
    for (int i = 0; i < info.pv_len; ++i) {
      S8 side = (i % 2 == 0) ? root_side : GetOtherPlayer(root_side);
      out << ' ' << MoveToUciStr(info.pv[i], side);
    }
  }

  std::lock_guard<std::mutex> lock(cout_mutex_);
  std::cout << out.str() << std::endl;
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
