/* Noah Himed
 *
 * Implement the params.json load layer over the shared parameter registry
 * (see params.h). The reader is a minimal hand-rolled JSON handler: the schema
 * is a flat object ({ "<Name>": <number>, ... }) that we fully control, so a
 * small tolerant scanner suffices and avoids adding any external dependency.
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

namespace omegazero {

using std::string;

// Scan the params object for "<key>": <number> pairs into a map. Tolerant of
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
               "regenerate or restore it."
            << std::endl;
  std::exit(EXIT_FAILURE);
}

auto LoadParamsOrDie(const string& path) -> SearchParams {
  std::ifstream f(path);
  if (!f) {
    ParamsFatal(path, "could not open file");
  }
  std::stringstream buf;
  buf << f.rdbuf();
  const std::map<string, double> fields = ParseNumberFields(buf.str());

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
    ParamsFatal(path, "missing required key(s): " + list);
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
