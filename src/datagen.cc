/* Noah Himed
 *
 * Native NNUE training data generator. Plays self-play games using direct
 * engine calls (no UCI overhead) and writes (fen, score, result) tuples.
 *
 * Quality filters applied:
 *   - Skip first N plies (opening theory, low info density)
 *   - Sample every Kth eligible ply (reduces near-duplicates)
 *   - Skip positions in check
 *   - Skip mate scores and tactical explosions (abs(score) > threshold)
 *   - Zobrist hash deduplication within each worker
 *   - Separate validation set from different games
 *
 * Licensed under MIT License. Terms and conditions enclosed in "LICENSE.txt".
 */

#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iostream>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

#include "bad_move.h"
#include "board.h"
#include "engine.h"
#include "move.h"

using namespace omegazero;
using std::cerr;
using std::cout;
using std::endl;
using std::string;
using std::vector;

constexpr int kMaxMovesPerGame = 150;
constexpr int kSkipFirstNPlies = 10;
constexpr int kRandomOpeningPlies = 8;
constexpr int kAdjudicateThreshold = 1000;
constexpr int kAdjudicateCount = 5;
constexpr int kSampleInterval = 4;
constexpr int kMaxAbsScore = 3000;

static const string kStartFen =
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

static auto GetGitHash() -> string {
  std::array<char, 64> buf{};
  string hash = "unknown";
  FILE* pipe = popen("git rev-parse --short HEAD 2>/dev/null", "r");
  if (pipe) {
    if (fgets(buf.data(), static_cast<int>(buf.size()), pipe)) {
      hash = buf.data();
      while (!hash.empty() && (hash.back() == '\n' || hash.back() == '\r'))
        hash.pop_back();
    }
    pclose(pipe);
  }
  return hash;
}

static auto GetTimestamp() -> string {
  auto now = std::chrono::system_clock::now();
  std::time_t t = std::chrono::system_clock::to_time_t(now);
  char buf[64];
  std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", std::localtime(&t));
  return buf;
}

static auto WriteMetadata(const string& output_dir, int total_games,
                          float search_time, int num_workers,
                          int total_positions, float elapsed) -> void {
  string path = output_dir + "/metadata.txt";
  std::ofstream out(path);
  out << "timestamp: " << GetTimestamp() << "\n"
      << "git_commit: " << GetGitHash() << "\n"
      << "games: " << total_games << "\n"
      << "search_time: " << search_time << "s/move\n"
      << "workers: " << num_workers << "\n"
      << "total_positions: " << total_positions << "\n"
      << "elapsed_seconds: " << elapsed << "\n"
      << "skip_plies: " << kSkipFirstNPlies << "\n"
      << "sample_interval: " << kSampleInterval << "\n"
      << "max_abs_score: " << kMaxAbsScore << "\n"
      << "adjudicate_threshold: " << kAdjudicateThreshold << "\n"
      << "adjudicate_count: " << kAdjudicateCount << "\n";
  out.close();
}

struct Position {
  string fen;
  int score;
};

enum GameResult { kWhiteWin, kBlackWin, kDrawResult };

static auto PlayRandomOpeningMoves(Board& board, Engine& engine,
                                   std::mt19937& rng) -> int {
  int plies_played = 0;
  for (int i = 0; i < kRandomOpeningPlies; ++i) {
    vector<Move> moves = engine.GenerateMoves();
    vector<Move> legal;
    for (const Move& m : moves) {
      try {
        board.MakeMove(m);
        board.UnmakeMove(m);
        legal.push_back(m);
      } catch (BadMove&) {
        continue;
      }
    }
    if (legal.empty()) break;

    std::uniform_int_distribution<int> dist(0, static_cast<int>(legal.size()) - 1);
    Move chosen = legal[dist(rng)];
    try {
      board.MakeMove(chosen);
    } catch (BadMove&) {
      break;
    }
    engine.AddPosToHistory();
    ++plies_played;
  }
  return plies_played;
}

static auto PlayGame(float search_time, std::mt19937& rng,
                     vector<Position>& positions,
                     std::unordered_set<U64>& seen_hashes) -> GameResult {
  Board board(kStartFen);
  Engine engine(&board, 'w', search_time);
  engine.AddPosToHistory();

  int plies_played = PlayRandomOpeningMoves(board, engine, rng);
  int consecutive_high = 0;
  int eligible_count = 0;

  for (int ply = plies_played; ply < kMaxMovesPerGame; ++ply) {
    S8 status = engine.GetGameStatus();
    if (status == kPlayerCheckmated) {
      return (board.GetPlayerToMove() == kWhite) ? kBlackWin : kWhiteWin;
    }
    if (status == kDraw) return kDrawResult;

    engine.SetSearchTime(search_time);
    int score_stm = 0;
    Move best = engine.GetBestMove(score_stm);

    if (best.moving_piece == kNA && best.castling_type == kNA) break;

    int score_white = (board.GetPlayerToMove() == kWhite) ? score_stm : -score_stm;

    bool dominated_by_tactics = abs(score_stm) > kMaxAbsScore;
    bool in_check = board.KingInCheck();
    bool past_opening = ply >= kSkipFirstNPlies;

    if (past_opening && !in_check && !dominated_by_tactics) {
      ++eligible_count;
      if (eligible_count % kSampleInterval == 0) {
        U64 hash = board.GetBoardHash();
        if (seen_hashes.find(hash) == seen_hashes.end()) {
          seen_hashes.insert(hash);
          positions.push_back({board.ToFen(), score_white});
        }
      }
    }

    if (abs(score_stm) >= kAdjudicateThreshold) {
      ++consecutive_high;
    } else {
      consecutive_high = 0;
    }
    if (consecutive_high >= kAdjudicateCount) {
      return (score_white > 0) ? kWhiteWin : kBlackWin;
    }

    try {
      board.MakeMove(best);
    } catch (BadMove&) {
      break;
    }
    engine.AddPosToHistory();
  }

  return kDrawResult;
}

static auto ResultToStr(GameResult r) -> string {
  switch (r) {
    case kWhiteWin: return "1.0";
    case kBlackWin: return "0.0";
    case kDrawResult: return "0.5";
  }
  return "0.5";
}

struct WorkerStats {
  int games = 0;
  int positions = 0;
  int duplicates_skipped = 0;
  int white_wins = 0;
  int black_wins = 0;
  int draws = 0;
};

static std::mutex g_output_mutex;
static std::atomic<int> g_games_done{0};

static auto WorkerThread(int worker_id, int num_games, float search_time,
                         const string& output_dir, float validation_fraction,
                         WorkerStats& stats) -> void {
  std::mt19937 rng(std::random_device{}() + worker_id);

  string train_path = output_dir + "/data_worker_" + std::to_string(worker_id + 1) + ".txt";
  string val_path = output_dir + "/val_worker_" + std::to_string(worker_id + 1) + ".txt";

  std::ofstream train_out(train_path);
  std::ofstream val_out(val_path);
  if (!train_out.is_open() || !val_out.is_open()) {
    std::lock_guard<std::mutex> lock(g_output_mutex);
    cerr << "Worker " << worker_id << ": failed to open output files" << endl;
    return;
  }

  int val_start_game = static_cast<int>(num_games * (1.0f - validation_fraction));
  std::unordered_set<U64> seen_hashes;
  seen_hashes.reserve(num_games * 20);

  for (int g = 0; g < num_games; ++g) {
    vector<Position> positions;
    positions.reserve(32);

    size_t hashes_before = seen_hashes.size();
    GameResult result = PlayGame(search_time, rng, positions, seen_hashes);
    int deduped = static_cast<int>(seen_hashes.size() - hashes_before);
    stats.duplicates_skipped += (static_cast<int>(positions.size()) - deduped);

    std::ofstream& out = (g >= val_start_game) ? val_out : train_out;
    string result_str = ResultToStr(result);
    for (const auto& pos : positions) {
      out << pos.fen << " | " << pos.score << " | " << result_str << '\n';
    }

    stats.positions += static_cast<int>(positions.size());
    stats.games++;
    if (result == kWhiteWin) stats.white_wins++;
    else if (result == kBlackWin) stats.black_wins++;
    else stats.draws++;

    int done = g_games_done.fetch_add(1) + 1;
    if (done % 10 == 0) {
      std::lock_guard<std::mutex> lock(g_output_mutex);
      cerr << "\r  " << done << " games complete" << std::flush;
    }
  }
  train_out.close();
  val_out.close();
}

auto main(int argc, char* argv[]) -> int {
  int total_games = 100;
  float search_time = 0.5f;
  int num_workers = 1;
  string output_dir = "nnue/data";
  float validation_fraction = 0.1f;

  for (int i = 1; i < argc; ++i) {
    string arg(argv[i]);
    if (arg == "--games" && i + 1 < argc) total_games = atoi(argv[++i]);
    else if (arg == "--st" && i + 1 < argc) search_time = static_cast<float>(atof(argv[++i]));
    else if (arg == "--workers" && i + 1 < argc) num_workers = atoi(argv[++i]);
    else if (arg == "--output" && i + 1 < argc) output_dir = argv[++i];
    else if (arg == "--val-fraction" && i + 1 < argc) validation_fraction = static_cast<float>(atof(argv[++i]));
    else if (arg == "--help") {
      cout << "Usage: datagen [OPTIONS]\n"
           << "  --games N          Total self-play games (default: 100)\n"
           << "  --st S             Search time per move in seconds (default: 0.5)\n"
           << "  --workers W        Number of parallel threads (default: 1)\n"
           << "  --output DIR       Output directory (default: nnue/data)\n"
           << "  --val-fraction F   Fraction of games for validation (default: 0.1)\n"
           << "\n"
           << "Quality filters (compile-time constants):\n"
           << "  Skip first " << kSkipFirstNPlies << " plies\n"
           << "  Sample every " << kSampleInterval << "th eligible position\n"
           << "  Skip positions with |score| > " << kMaxAbsScore << "cp\n"
           << "  Skip positions in check\n"
           << "  Zobrist hash deduplication per worker\n"
           << "  Adjudicate at " << kAdjudicateThreshold << "cp for "
           << kAdjudicateCount << " consecutive moves\n";
      return 0;
    }
  }

  if (num_workers < 1) num_workers = 1;
  if (total_games < 1) total_games = 1;

  // Create timestamped subdirectory: <output_dir>/<YYYY-MM-DD_HH-MM-SS>_<githash>/
  {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    char ts_buf[64];
    std::strftime(ts_buf, sizeof(ts_buf), "%Y-%m-%d_%H-%M-%S", std::localtime(&t));
    string subdir = string(ts_buf) + "_" + GetGitHash();
    output_dir += "/" + subdir;
  }

  // Create output directory tree
  string mkdir_cmd = "mkdir -p " + output_dir;
  std::system(mkdir_cmd.c_str());

  cout << "Native NNUE data generator\n"
       << "  Games: " << total_games << "\n"
       << "  Search time: " << search_time << "s/move\n"
       << "  Workers: " << num_workers << "\n"
       << "  Output: " << output_dir << "/\n"
       << "  Validation: " << static_cast<int>(validation_fraction * 100) << "% of games\n"
       << "  Filters: skip " << kSkipFirstNPlies << " plies, sample 1/"
       << kSampleInterval << ", |score| <= " << kMaxAbsScore
       << ", dedup, no check\n" << endl;

  auto start = std::chrono::high_resolution_clock::now();

  vector<std::thread> threads;
  vector<WorkerStats> stats(num_workers);
  int games_per_worker = total_games / num_workers;
  int remainder = total_games % num_workers;

  for (int w = 0; w < num_workers; ++w) {
    int games = games_per_worker + (w < remainder ? 1 : 0);
    threads.emplace_back(WorkerThread, w, games, search_time, output_dir,
                         validation_fraction, std::ref(stats[w]));
  }

  for (auto& t : threads) t.join();

  auto end = std::chrono::high_resolution_clock::now();
  float elapsed = std::chrono::duration<float>(end - start).count();

  int total_positions = 0, total_w = 0, total_b = 0, total_d = 0;
  for (const auto& s : stats) {
    total_positions += s.positions;
    total_w += s.white_wins;
    total_b += s.black_wins;
    total_d += s.draws;
  }

  // Combine worker files
  string train_combined = output_dir + "/training_data.txt";
  string val_combined = output_dir + "/validation_data.txt";
  std::ofstream train_out(train_combined);
  std::ofstream val_out(val_combined);
  int train_count = 0, val_count = 0;

  for (int w = 0; w < num_workers; ++w) {
    string train_path = output_dir + "/data_worker_" + std::to_string(w + 1) + ".txt";
    string val_path = output_dir + "/val_worker_" + std::to_string(w + 1) + ".txt";

    std::ifstream tin(train_path);
    if (tin.is_open()) {
      string line;
      while (std::getline(tin, line)) { train_out << line << '\n'; ++train_count; }
      tin.close();
      std::remove(train_path.c_str());
    }

    std::ifstream vin(val_path);
    if (vin.is_open()) {
      string line;
      while (std::getline(vin, line)) { val_out << line << '\n'; ++val_count; }
      vin.close();
      std::remove(val_path.c_str());
    }
  }
  train_out.close();
  val_out.close();

  int games_played = total_w + total_b + total_d;

  WriteMetadata(output_dir, games_played, search_time, num_workers,
                total_positions, elapsed);

  cout << "\n\nDone: " << games_played << " games, "
       << total_positions << " positions in " << elapsed << "s\n"
       << "Results: W:" << total_w << " D:" << total_d << " L:" << total_b << "\n"
       << "Training: " << train_count << " positions → " << train_combined << "\n"
       << "Validation: " << val_count << " positions → " << val_combined << "\n"
       << "Rate: " << static_cast<int>(total_positions / elapsed) << " positions/sec\n"
       << "Avg positions/game: " << (games_played > 0 ? total_positions / games_played : 0) << "\n"
       << "Metadata: " << output_dir << "/metadata.txt\n";

  return 0;
}
