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
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "nnue.h"

namespace omegazero {

using std::string;

auto ProfileForEvalMode() -> string {
  return g_nnue.IsLoaded() ? "nnue" : "hce";
}

// Return the substring of `text` between the braces of the object bound to
// key `"<profile>"`, or empty if the key/object is not found. Braces inside
// quoted strings are ignored so nested object matching stays correct.
static auto ExtractProfileBlock(const string& text, const string& profile)
    -> string {
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
static auto ParseNumberFields(const string& block) -> std::map<string, double> {
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

auto ParamsPathFromExe(const string& argv0) -> string {
  const size_t slash = argv0.find_last_of('/');
  const string dir = (slash == string::npos) ? "" : argv0.substr(0, slash + 1);
  return dir + "../params.json";
}

// Print a fatal params.json error to stderr and terminate. Parameter values
// have no in-code defaults, so a missing/incomplete file is unrecoverable.
static auto ParamsFatal(const string& path, const string& msg) -> void {
  std::cerr << "FATAL: params.json (" << path << "): " << msg << "\n"
            << "params.json is required and holds every search parameter; "
               "regenerate or restore it (both \"nnue\" and \"hce\" profiles)."
            << std::endl;
  std::exit(EXIT_FAILURE);
}

auto LoadParamsOrDie(const string& path, const string& profile)
    -> SearchParams {
  std::ifstream f(path);
  if (!f) {
    ParamsFatal(path, "could not open file");
  }
  std::stringstream buf;
  buf << f.rdbuf();
  const string block = ExtractProfileBlock(buf.str(), profile);
  if (block.empty()) {
    ParamsFatal(path, "profile \"" + profile + "\" not found");
  }

  const std::map<string, double> fields = ParseNumberFields(block);

  // Every registry key must be present: no field may fall back to a code value.
  std::vector<string> missing;
  for (const IntOpt& o : kIntOpts) {
    if (fields.find(o.name) == fields.end()) missing.push_back(o.name);
  }
  for (const DblOpt& o : kDblOpts) {
    if (fields.find(o.name) == fields.end()) missing.push_back(o.name);
  }
  if (!missing.empty()) {
    string list;
    for (size_t i = 0; i < missing.size(); ++i) {
      list += (i ? ", " : "") + missing[i];
    }
    ParamsFatal(path, "profile \"" + profile +
                          "\" is missing required key(s): " + list);
  }

  SearchParams out;
  for (const IntOpt& o : kIntOpts) {
    const double raw = fields.at(o.name);
    out.*o.field =
        std::clamp(static_cast<int>(std::lround(raw)), o.min, o.max);
  }
  for (const DblOpt& o : kDblOpts) {
    const double raw = fields.at(o.name);
    const int scaled =
        std::clamp(static_cast<int>(std::lround(raw)), o.min, o.max);
    out.*o.field = scaled / static_cast<double>(o.divisor);
  }
  return out;
}

}  // namespace omegazero
