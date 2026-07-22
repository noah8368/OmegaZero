/* Noah Himed
 *
 * Implement the params.json load/store layer over the shared parameter registry
 * (see params.h). The reader/writer is a minimal hand-rolled JSON handler: the
 * schema is a fixed two-level object ({ "<profile>": { "<Name>": <number> } })
 * that we fully control, so a small tolerant scanner suffices and avoids adding
 * any external dependency.
 *
 * Licensed under MIT License. Terms and conditions enclosed in "LICENSE.txt".
 */

#include "params.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <map>
#include <sstream>
#include <string>

#include "nnue.h"

namespace omegazero {

using std::string;

auto ProfileForEvalMode() -> string {
  return g_nnue.IsLoaded() ? "nnue" : "hce";
}

namespace {

// Return the substring of `text` between the braces of the object bound to
// key `"<profile>"`, or empty if the key/object is not found. Braces inside
// quoted strings are ignored so nested object matching stays correct.
auto ExtractProfileBlock(const string& text, const string& profile) -> string {
  const string key = "\"" + profile + "\"";
  size_t pos = text.find(key);
  if (pos == string::npos) return "";
  pos = text.find('{', pos + key.size());
  if (pos == string::npos) return "";

  int depth = 0;
  bool in_str = false;
  for (size_t i = pos; i < text.size(); ++i) {
    const char c = text[i];
    if (in_str) {
      if (c == '\\') {
        ++i;  // skip the escaped character
      } else if (c == '"') {
        in_str = false;
      }
      continue;
    }
    if (c == '"') {
      in_str = true;
    } else if (c == '{') {
      ++depth;
    } else if (c == '}') {
      if (--depth == 0) return text.substr(pos + 1, i - pos - 1);
    }
  }
  return "";  // unbalanced braces
}

// Scan a profile block for "<key>": <number> pairs into a map. Tolerant of
// whitespace/commas/newlines; values may be negative or decimal.
auto ParseNumberFields(const string& block) -> std::map<string, double> {
  std::map<string, double> fields;
  size_t i = 0;
  const size_t n = block.size();
  while (i < n) {
    // Find the opening quote of a key.
    while (i < n && block[i] != '"') ++i;
    if (i >= n) break;
    size_t key_start = ++i;
    while (i < n && block[i] != '"') ++i;
    if (i >= n) break;
    const string field = block.substr(key_start, i - key_start);
    ++i;  // past closing quote

    // Expect a colon, then a numeric value.
    while (i < n && block[i] != ':') ++i;
    if (i >= n) break;
    ++i;  // past ':'
    while (i < n && std::isspace(static_cast<unsigned char>(block[i]))) ++i;
    size_t val_start = i;
    while (i < n) {
      const char c = block[i];
      if ((c >= '0' && c <= '9') || c == '-' || c == '+' || c == '.' ||
          c == 'e' || c == 'E') {
        ++i;
      } else {
        break;
      }
    }
    if (i > val_start) {
      try {
        fields[field] = std::stod(block.substr(val_start, i - val_start));
      } catch (const std::exception&) {
        // Skip a malformed value; leave the field at its default.
      }
    }
  }
  return fields;
}

}  // namespace

auto LoadProfileInto(const string& path, const string& profile,
                     SearchParams& out) -> bool {
  std::ifstream f(path);
  if (!f) return false;
  std::stringstream buf;
  buf << f.rdbuf();
  const string block = ExtractProfileBlock(buf.str(), profile);
  if (block.empty()) return false;

  const std::map<string, double> fields = ParseNumberFields(block);
  for (const IntOpt& o : kIntOpts) {
    auto it = fields.find(o.name);
    if (it != fields.end()) {
      out.*o.field = std::clamp(static_cast<int>(std::lround(it->second)),
                                o.min, o.max);
    }
  }
  for (const DblOpt& o : kDblOpts) {
    auto it = fields.find(o.name);
    if (it != fields.end()) {
      const int scaled = std::clamp(static_cast<int>(std::lround(it->second)),
                                    o.min, o.max);
      out.*o.field = scaled / static_cast<double>(o.divisor);
    }
  }
  return true;
}

auto WriteParamsJson(
    const string& path,
    const std::vector<std::pair<string, SearchParams>>& profiles) -> bool {
  std::ofstream f(path);
  if (!f) return false;

  f << "{\n";
  for (size_t p = 0; p < profiles.size(); ++p) {
    const SearchParams& sp = profiles[p].second;
    f << "  \"" << profiles[p].first << "\": {\n";

    bool first = true;
    auto emit = [&](const char* name, long value) {
      if (!first) f << ",\n";
      first = false;
      f << "    \"" << name << "\": " << value;
    };
    for (const IntOpt& o : kIntOpts) emit(o.name, sp.*o.field);
    for (const DblOpt& o : kDblOpts) {
      emit(o.name, std::lround(sp.*o.field * o.divisor));
    }
    f << "\n  }" << (p + 1 < profiles.size() ? "," : "") << "\n";
  }
  f << "}\n";
  return static_cast<bool>(f);
}

}  // namespace omegazero
