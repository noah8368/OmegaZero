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
#include <csignal>
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
#include "params.h"

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
constexpr int kMaxConsecutiveCrashes = 5;
constexpr int kStartupEmailThreshold = 10;

static const string kStartFen =
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

struct Config {
  int games = 100;
  float st = 0.5f;
  int workers = 1;
  string output = "nnue/data";
  float val_fraction = 0.1f;
  string email;
  string name;
};

static auto TrimQuotes(const string& s) -> string {
  if (s.size() >= 2 && s.front() == '"' && s.back() == '"')
    return s.substr(1, s.size() - 2);
  return s;
}

static auto LoadConfig(const string& path) -> Config {
  Config cfg;
  std::ifstream f(path);
  if (!f.is_open()) return cfg;

  string line;
  while (std::getline(f, line)) {
    auto colon = line.find(':');
    if (colon == string::npos) continue;
    string key = line.substr(0, colon);
    string val = line.substr(colon + 1);
    // Strip whitespace, quotes, trailing commas
    auto strip = [](string& s) {
      while (!s.empty() && (s.back() == ',' || s.back() == ' ' || s.back() == '\t'))
        s.pop_back();
      while (!s.empty() && (s.front() == ' ' || s.front() == '\t'))
        s.erase(s.begin());
    };
    strip(key);
    strip(val);
    key = TrimQuotes(key);
    val = TrimQuotes(val);

    if (key == "games") cfg.games = std::stoi(val);
    else if (key == "st") cfg.st = std::stof(val);
    else if (key == "workers") cfg.workers = std::stoi(val);
    else if (key == "output") cfg.output = val;
    else if (key == "val_fraction") cfg.val_fraction = std::stof(val);
    else if (key == "email") cfg.email = val;
    else if (key == "name") cfg.name = val;
  }
  return cfg;
}

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

static auto PlayGame(float search_time, const SearchParams& params,
                     std::mt19937& rng, vector<Position>& positions,
                     std::unordered_set<U64>& seen_hashes) -> GameResult {
  Board board(kStartFen);
  TranspositionTable tt;
  Engine engine(&tt, &board, 'w', search_time);
  engine.SetParams(params);
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

    if (best.IsEmpty()) break;

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
static std::mutex g_crash_log_mutex;
static std::atomic<int> g_games_done{0};
static std::atomic<bool> g_shutdown{false};
static std::atomic<bool> g_restart_requested{false};

static void SignalHandler(int /*sig*/) {
  g_shutdown.store(true);
}
static std::atomic<int> g_total_games{0};
// Games completed in prior runs of this campaign (passed in via env by the
// watchdog). Lets progress accounting span restarts instead of resetting.
static std::atomic<int> g_base_completed{0};
static string g_email;
static string g_name;
static string g_output_dir_global;
static std::atomic<bool> g_milestone_10{false};
static std::atomic<bool> g_milestone_20{false};
static std::atomic<bool> g_milestone_30{false};
static std::atomic<bool> g_milestone_40{false};
static std::atomic<bool> g_milestone_50{false};
static std::atomic<bool> g_milestone_60{false};
static std::atomic<bool> g_milestone_70{false};
static std::atomic<bool> g_milestone_80{false};
static std::atomic<bool> g_milestone_90{false};
static std::atomic<bool> g_milestone_100{false};
static std::chrono::high_resolution_clock::time_point g_start_time;
static std::atomic<bool> g_startup_email_sent{false};
static int g_num_workers = 1;
static float g_search_time = 0.5f;
constexpr int kMergeIntervalSeconds = 43200;
constexpr int kHeartbeatIntervalSeconds = 43200;
static std::atomic<long long> g_last_merge_time{0};
static std::atomic<long long> g_last_heartbeat_time{0};

static auto SendEmail(const string& subject, const string& body) -> void {
  if (g_email.empty()) return;
  string cmd = "python3 scripts/send_email.py"
               " --to '" + g_email + "'"
               " --subject '" + subject + "'"
               " --body '" + body + "'"
               " 2>/dev/null";
  int ret = std::system(cmd.c_str());
  (void)ret;
}

static auto FormatDuration(int s) -> string {
  if (s <= 0) return "0s";
  string r;
  int mo = s / (30*24*3600); s %= 30*24*3600;
  int wk = s / (7*24*3600);  s %= 7*24*3600;
  int dy = s / (24*3600);    s %= 24*3600;
  int hr = s / 3600;         s %= 3600;
  int mn = s / 60;           int sc = s % 60;
  if (mo) r += std::to_string(mo) + "mo ";
  if (wk) r += std::to_string(wk) + "wk ";
  if (dy) r += std::to_string(dy) + "dy ";
  if (hr) r += std::to_string(hr) + "hr ";
  if (mn) r += std::to_string(mn) + "min ";
  if (sc || r.empty()) r += std::to_string(sc) + "s";
  while (!r.empty() && r.back() == ' ') r.pop_back();
  return r;
}

static auto GetEtaString(int done, int total) -> string {
  if (done <= 0) return "";
  auto now = std::chrono::high_resolution_clock::now();
  float elapsed = std::chrono::duration<float>(now - g_start_time).count();
  if (elapsed <= 0) return "";
  // `done` is cumulative across the campaign, but only games played this run
  // count toward the rate — base_completed games predate this process's clock.
  int run_done = done - g_base_completed.load();
  if (run_done <= 0) return "";
  float rate = run_done / elapsed;
  int remaining = total - done;
  if (remaining <= 0) return "ETA: complete";
  int eta_seconds = static_cast<int>(remaining / rate);
  auto eta_time = std::chrono::system_clock::now()
                  + std::chrono::seconds(eta_seconds);
  std::time_t t = std::chrono::system_clock::to_time_t(eta_time);
  char buf[64];
  std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", std::localtime(&t));
  return "Time remaining: " + FormatDuration(eta_seconds) + "\n"
       + "ETA: " + string(buf);
}

static auto WriteCrashLog(const string& crash_type, int worker_id,
                          int game, const string& error,
                          const WorkerStats* stats) -> string {
  string ts = GetTimestamp();
  int total_done = g_base_completed.load() + g_games_done.load();
  int total = g_total_games.load();

  string entry = "=== CRASH LOG ===\n"
      "Timestamp: " + ts + "\n"
      "Type: " + crash_type + "\n"
      "Worker: " + std::to_string(worker_id) + "\n";
  if (game >= 0) {
    entry += "Game: " + std::to_string(game) + "\n";
  }
  entry += "Error: " + error + "\n"
      "Global progress: " + std::to_string(total_done) + "/" + std::to_string(total) + "\n";
  if (stats) {
    entry += "Worker games completed: " + std::to_string(stats->games) + "\n"
        "Worker positions generated: " + std::to_string(stats->positions) + "\n";
  }
  entry += "=================\n\n";

  {
    std::lock_guard<std::mutex> lock(g_crash_log_mutex);
    string path = g_output_dir_global + "/crash_log.txt";
    std::ofstream out(path, std::ios::app);
    if (out.is_open()) {
      out << entry;
      out.close();
    }
  }

  return entry;
}

static auto WriteExitStatus(const string& status) -> void {
  string path = g_output_dir_global + "/.exit_status";
  std::ofstream out(path);
  if (out.is_open()) {
    out << status << "\n";
    out.close();
  }
}

static auto CheckMilestones(int done) -> void {
  if (g_email.empty()) return;
  int total = g_total_games.load();
  if (total <= 0) return;

  int pct = (done * 100) / total;

  auto fire = [&](int threshold, std::atomic<bool>& flag) {
    if (pct >= threshold) {
      bool expected = false;
      if (flag.compare_exchange_strong(expected, true)) {
        string tag = g_name.empty() ? "" : " [" + g_name + "]";
        string subject = "OmegaZero datagen" + tag + " "
                         + std::to_string(threshold) + "% complete";
        string body = (g_name.empty() ? "" : "Machine: " + g_name + "\n")
                      + std::to_string(done) + "/" + std::to_string(total)
                      + " games done (" + std::to_string(threshold) + "%)\n"
                      + GetEtaString(done, total) + "\n"
                      + "Output: " + g_output_dir_global;
        SendEmail(subject, body);
        std::lock_guard<std::mutex> lock(g_output_mutex);
        cerr << "\n  [EMAIL] " << threshold << "% milestone → " << g_email << endl;
      }
    }
  };

  fire(10, g_milestone_10);
  fire(20, g_milestone_20);
  fire(30, g_milestone_30);
  fire(40, g_milestone_40);
  fire(50, g_milestone_50);
  fire(60, g_milestone_60);
  fire(70, g_milestone_70);
  fire(80, g_milestone_80);
  fire(90, g_milestone_90);
  fire(100, g_milestone_100);
}

static auto WorkerThread(int worker_id, int num_games, float search_time,
                         const SearchParams& params, const string& output_dir,
                         float validation_fraction, WorkerStats& stats) -> void {
 try {
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
  int consecutive_crashes = 0;

  for (int g = 0; g < num_games && !g_shutdown.load(); ++g) {
    try {
      vector<Position> positions;
      positions.reserve(32);

      size_t hashes_before = seen_hashes.size();
      GameResult result =
          PlayGame(search_time, params, rng, positions, seen_hashes);
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
      consecutive_crashes = 0;
    } catch (const std::exception& e) {
      {
        std::lock_guard<std::mutex> lock(g_output_mutex);
        cerr << "\nWorker " << worker_id << " game " << g
             << " crashed: " << e.what() << " — skipping" << endl;
      }
      string log = WriteCrashLog("game_crash", worker_id, g, e.what(), &stats);
      string tag = g_name.empty() ? "" : " [" + g_name + "]";
      SendEmail("OmegaZero" + tag + " CRASH — worker "
                + std::to_string(worker_id), log);
      stats.games++;
      if (++consecutive_crashes >= kMaxConsecutiveCrashes) {
        string msg = std::to_string(kMaxConsecutiveCrashes)
                     + " consecutive game crashes — escalating to restart";
        {
          std::lock_guard<std::mutex> lock(g_output_mutex);
          cerr << "\nWorker " << worker_id << ": " << msg << endl;
        }
        string rlog = WriteCrashLog("consecutive_crash_limit", worker_id, g,
                                    msg, &stats);
        SendEmail("OmegaZero" + tag + " RESTART — worker "
                  + std::to_string(worker_id) + " consecutive crashes", rlog);
        g_restart_requested.store(true);
        g_shutdown.store(true);
        break;
      }
    } catch (...) {
      {
        std::lock_guard<std::mutex> lock(g_output_mutex);
        cerr << "\nWorker " << worker_id << " game " << g
             << " crashed (unknown) — skipping" << endl;
      }
      string log = WriteCrashLog("game_crash", worker_id, g,
                                 "unknown exception", &stats);
      string tag = g_name.empty() ? "" : " [" + g_name + "]";
      SendEmail("OmegaZero" + tag + " CRASH — worker "
                + std::to_string(worker_id), log);
      stats.games++;
      if (++consecutive_crashes >= kMaxConsecutiveCrashes) {
        string msg = std::to_string(kMaxConsecutiveCrashes)
                     + " consecutive game crashes — escalating to restart";
        {
          std::lock_guard<std::mutex> lock(g_output_mutex);
          cerr << "\nWorker " << worker_id << ": " << msg << endl;
        }
        string rlog = WriteCrashLog("consecutive_crash_limit", worker_id, g,
                                    msg, &stats);
        SendEmail("OmegaZero" + tag + " RESTART — worker "
                  + std::to_string(worker_id) + " consecutive crashes", rlog);
        g_restart_requested.store(true);
        g_shutdown.store(true);
        break;
      }
    }

    int run_done = g_games_done.fetch_add(1) + 1;
    // Report progress against the whole campaign, not just this process's batch.
    int done = g_base_completed.load() + run_done;
    CheckMilestones(done);
    if (!g_email.empty() && done >= kStartupEmailThreshold) {
      bool expected = false;
      if (g_startup_email_sent.compare_exchange_strong(expected, true)) {
        string tag = g_name.empty() ? "" : " [" + g_name + "]";
        int total = g_total_games.load();
        SendEmail("OmegaZero" + tag + " datagen STARTED",
                  (g_name.empty() ? "" : "Machine: " + g_name + "\n")
                  + "Games: " + std::to_string(total) + "\n"
                  + "Workers: " + std::to_string(g_num_workers) + "\n"
                  + "Search time: " + std::to_string(g_search_time).substr(0, 4)
                  + "s/move\n" + GetEtaString(done, total) + "\n"
                  + "Output: " + g_output_dir_global);
      }
    }
    {
      auto now = std::chrono::system_clock::now();
      long long now_s = std::chrono::duration_cast<std::chrono::seconds>(
          now.time_since_epoch()).count();
      long long last = g_last_merge_time.load();
      if (now_s - last >= kMergeIntervalSeconds &&
          g_last_merge_time.compare_exchange_strong(last, now_s)) {
        train_out.flush();
        val_out.flush();
        string cmd = "./scripts/merge_workers.sh "
                     + g_output_dir_global + " 2>/dev/null";
        int ret = std::system(cmd.c_str());
        (void)ret;
        std::lock_guard<std::mutex> lock(g_output_mutex);
        cerr << "\n  [MERGE] Periodic merge complete" << endl;
      }
      long long last_hb = g_last_heartbeat_time.load();
      if (now_s - last_hb >= kHeartbeatIntervalSeconds &&
          g_last_heartbeat_time.compare_exchange_strong(last_hb, now_s)) {
        int total = g_total_games.load();
        int total_done = g_base_completed.load() + g_games_done.load();
        string tag = g_name.empty() ? "" : " [" + g_name + "]";
        string body = (g_name.empty() ? "" : "Machine: " + g_name + "\n")
            + std::to_string(total_done) + "/" + std::to_string(total)
            + " games done\n"
            + GetEtaString(total_done, total) + "\n"
            + "Output: " + g_output_dir_global;
        SendEmail("OmegaZero" + tag + " heartbeat", body);
        std::lock_guard<std::mutex> lock(g_output_mutex);
        cerr << "  [HEARTBEAT] " << total_done << "/" << total
             << " — email sent" << endl;
      }
    }
    if (done % 100 == 0) {
      train_out.flush();
      val_out.flush();
      std::lock_guard<std::mutex> lock(g_output_mutex);
      cerr << "  " << done << " games complete (" << stats.positions
           << " positions from worker " << worker_id << ")" << endl;
    }
  }
  if (g_shutdown.load()) {
    std::lock_guard<std::mutex> lock(g_output_mutex);
    cerr << "\nWorker " << worker_id << " stopping (shutdown signal)" << endl;
  }
  train_out.flush();
  val_out.flush();
  train_out.close();
  val_out.close();

 } catch (const std::exception& e) {
    {
      std::lock_guard<std::mutex> lock(g_output_mutex);
      cerr << "\nWorker " << worker_id << " fatal crash: " << e.what()
           << " — initiating restart" << endl;
    }
    string log = WriteCrashLog("worker_fatal", worker_id, -1, e.what(), &stats);
    string tag = g_name.empty() ? "" : " [" + g_name + "]";
    SendEmail("OmegaZero" + tag + " RESTART — worker "
              + std::to_string(worker_id) + " fatal crash", log);
    g_restart_requested.store(true);
    g_shutdown.store(true);
 } catch (...) {
    {
      std::lock_guard<std::mutex> lock(g_output_mutex);
      cerr << "\nWorker " << worker_id
           << " fatal crash: unknown — initiating restart" << endl;
    }
    string log = WriteCrashLog("worker_fatal", worker_id, -1,
                               "unknown exception", &stats);
    string tag = g_name.empty() ? "" : " [" + g_name + "]";
    SendEmail("OmegaZero" + tag + " RESTART — worker "
              + std::to_string(worker_id) + " fatal crash", log);
    g_restart_requested.store(true);
    g_shutdown.store(true);
 }
}

auto main() -> int {
  Config cfg = LoadConfig("nnue/config.json");
  int total_games = cfg.games;
  float search_time = cfg.st;
  int num_workers = cfg.workers;
  string output_dir = cfg.output;
  float validation_fraction = cfg.val_fraction;
  g_email = cfg.email;
  g_name = cfg.name;

  // Datagen self-play uses HCE (no NNUE is loaded), so this resolves the "hce"
  // profile. Run from the repo root, where params.json (and nnue/config.json)
  // live. A missing/incomplete file is fatal -- there are no in-code defaults.
  const SearchParams params =
      LoadParamsOrDie("params.json", ProfileForEvalMode());

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
  int ret = std::system(mkdir_cmd.c_str());
  (void)ret;

  // Resume-aware progress accounting. The watchdog passes cumulative campaign
  // context via env vars so milestone/heartbeat/ETA emails reflect overall
  // progress across restarts. Absent (a standalone run) => behave as before.
  int base_completed = 0;
  int campaign_total = total_games;
  if (const char* env = std::getenv("OZ_BASE_COMPLETED")) base_completed = std::atoi(env);
  if (const char* env = std::getenv("OZ_TOTAL_GAMES")) campaign_total = std::atoi(env);
  if (base_completed < 0) base_completed = 0;
  if (campaign_total < total_games) campaign_total = total_games;  // sanity floor
  g_base_completed.store(base_completed);
  g_total_games.store(campaign_total);

  // Suppress milestone emails for thresholds already crossed before this run,
  // so a resume picks up where the last run left off instead of re-firing 0->N%.
  if (campaign_total > 0) {
    int base_pct = (base_completed * 100) / campaign_total;
    auto seed = [&](int threshold, std::atomic<bool>& flag) {
      if (base_pct >= threshold) flag.store(true);
    };
    seed(10, g_milestone_10);
    seed(20, g_milestone_20);
    seed(30, g_milestone_30);
    seed(40, g_milestone_40);
    seed(50, g_milestone_50);
    seed(60, g_milestone_60);
    seed(70, g_milestone_70);
    seed(80, g_milestone_80);
    seed(90, g_milestone_90);
    seed(100, g_milestone_100);
  }

  g_output_dir_global = output_dir;
  long long now_epoch = std::chrono::duration_cast<std::chrono::seconds>(
      std::chrono::system_clock::now().time_since_epoch()).count();
  g_last_merge_time.store(now_epoch);
  g_last_heartbeat_time.store(now_epoch);

  cout << "Native NNUE data generator\n"
       << "  Games: " << total_games << "\n"
       << "  Search time: " << search_time << "s/move\n"
       << "  Workers: " << num_workers << "\n"
       << "  Output: " << output_dir << "/\n"
       << "  Validation: " << static_cast<int>(validation_fraction * 100) << "% of games\n"
       << "  Filters: skip " << kSkipFirstNPlies << " plies, sample 1/"
       << kSampleInterval << ", |score| <= " << kMaxAbsScore
       << ", dedup, no check\n"
       << "  Email: " << (g_email.empty() ? "(none)" : g_email) << "\n"
       << "  Name: " << (g_name.empty() ? "(none)" : g_name) << "\n" << endl;

  std::signal(SIGTERM, SignalHandler);
  std::signal(SIGINT, SignalHandler);

  g_start_time = std::chrono::high_resolution_clock::now();
  g_num_workers = num_workers;
  g_search_time = search_time;

  vector<std::thread> threads;
  vector<WorkerStats> stats(num_workers);
  int games_per_worker = total_games / num_workers;
  int remainder = total_games % num_workers;

  for (int w = 0; w < num_workers; ++w) {
    int games = games_per_worker + (w < remainder ? 1 : 0);
    threads.emplace_back(WorkerThread, w, games, search_time, std::cref(params),
                         output_dir, validation_fraction, std::ref(stats[w]));
  }

  for (auto& t : threads) t.join();

  bool restarting = g_restart_requested.load();
  if (g_shutdown.load() && !restarting) {
    WriteExitStatus("shutdown");
    cerr << "\nShutdown signal received — saving partial results..." << endl;
    string tag = g_name.empty() ? "" : " [" + g_name + "]";
    SendEmail("OmegaZero" + tag + " SHUTDOWN — partial save",
              (g_name.empty() ? "" : "Machine: " + g_name + "\n")
              + "Process received SIGTERM/SIGINT.\n"
              + std::to_string(g_games_done.load()) + "/" + std::to_string(total_games)
              + " games completed before shutdown.\n"
              + "Data flushed to: " + output_dir);
  } else if (restarting) {
    cerr << "\nWorker fatal crash — saving partial results for restart..."
         << endl;
  }

  auto end = std::chrono::high_resolution_clock::now();
  float elapsed = std::chrono::duration<float>(end - g_start_time).count();

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

  string summary =
      (g_name.empty() ? "" : "Machine: " + g_name + "\n")
      + "Done: " + std::to_string(games_played) + " games, "
      + std::to_string(total_positions) + " positions in "
      + std::to_string(static_cast<int>(elapsed)) + "s\n"
      + "Results: W:" + std::to_string(total_w) + " D:" + std::to_string(total_d)
      + " L:" + std::to_string(total_b) + "\n"
      + "Training: " + std::to_string(train_count) + " positions\n"
      + "Validation: " + std::to_string(val_count) + " positions\n"
      + "Rate: " + std::to_string(static_cast<int>(total_positions / elapsed))
      + " positions/sec\n"
      + "Avg positions/game: "
      + std::to_string(games_played > 0 ? total_positions / games_played : 0) + "\n"
      + "Output: " + output_dir;

  cout << "\n\n" << summary << "\n"
       << "Metadata: " << output_dir << "/metadata.txt\n";

  string tag = g_name.empty() ? "" : " [" + g_name + "]";
  if (!restarting) {
    SendEmail("OmegaZero datagen" + tag + " COMPLETE", summary);
  }

  if (restarting) {
    WriteExitStatus("restart");
    return 2;
  }
  if (!g_shutdown.load()) {
    WriteExitStatus("complete");
  }
  return 0;
}
