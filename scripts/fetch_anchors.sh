#!/usr/bin/env bash
#
# fetch_anchors.sh — build the CCRL-anchor engines and Ordo into a git-ignored
# engines/ directory, for the round-robin Elo calibration driven by
# scripts/elo.py's `calibrate` subcommand.
#
# What it builds (all natively for the host arch — no Rosetta):
#   * The Blunder ladder (deanmchris/blunder, MIT, pure Go). One codebase across
#     versions gives a smooth monotonic anchor ladder. Only CCRL-*measured* rungs
#     are used as fixed anchors:
#         5.0.0 ~2080   6.1.0 ~2155   7.2.0 ~2425
#         7.4.0 ~2532   7.6.0 ~2631   8.0.0 ~2674
#     (8.5.5 ~2700 is author-*estimated* only — deliberately excluded so the
#     scale is never pinned to an unmeasured rung.)
#   * Ordo (michiguel/Ordo) — the MLE rating fitter elo.py post-processes the
#     round-robin PGN with, anchors held fixed.
#
# The Blunder ladder shares one eval family, so its ABSOLUTE scale can drift as a
# group (single-family bias). Tie it to true CCRL scale by adding ONE independent
# anchor (Fruit 2.1 ~2450, Glaurung 2.2 ~2550, or Stash) to the round-robin — see
# the note printed at the end. That step is intentionally left manual: those
# sources are version-sensitive and this script stays dependency-light.
#
# IMPORTANT: verify every rung's rating against the LIVE CCRL Blitz list
# (computerchess.org.uk/ccrl/404/) before a calibration run — published ratings
# drift, and Blunder/Stash use confusable numbering schemes.
#
# Usage:
#   scripts/fetch_anchors.sh                # build everything that's missing
#   scripts/fetch_anchors.sh --force        # rebuild even if binaries exist
#   scripts/fetch_anchors.sh --only blunder # just the Blunder ladder
#   scripts/fetch_anchors.sh --only ordo    # just Ordo
#   scripts/fetch_anchors.sh --help
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
ENGINES_DIR="${REPO_ROOT}/engines"
SRC_DIR="${ENGINES_DIR}/.src"
BLUNDER_DIR="${ENGINES_DIR}/blunder"
ORDO_DIR="${ENGINES_DIR}/ordo"
MANIFEST="${ENGINES_DIR}/manifest.txt"

BLUNDER_REPO="https://github.com/deanmchris/blunder.git"
ORDO_REPO="https://github.com/michiguel/Ordo.git"

# rung version : CCRL-measured rating (reference only; the authoritative ratings
# for the fit live in the anchor registry consumed by elo.py).
BLUNDER_RUNGS=(
  "5.0.0:2080"
  "6.1.0:2155"
  "7.2.0:2425"
  "7.4.0:2532"
  "7.6.0:2631"
  "8.0.0:2674"
)

FORCE=0
ONLY=""

# ---------------------------------------------------------------------------
# Logging
# ---------------------------------------------------------------------------
if [ -t 1 ]; then
  C_INFO="\033[1;34m"; C_OK="\033[1;32m"; C_WARN="\033[1;33m"
  C_ERR="\033[1;31m"; C_OFF="\033[0m"
else
  C_INFO=""; C_OK=""; C_WARN=""; C_ERR=""; C_OFF=""
fi
info()  { printf "${C_INFO}==>${C_OFF} %s\n" "$*"; }
ok()    { printf "${C_OK}  ok${C_OFF} %s\n" "$*"; }
warn()  { printf "${C_WARN}warn${C_OFF} %s\n" "$*" >&2; }
die()   { printf "${C_ERR}error${C_OFF} %s\n" "$*" >&2; exit 1; }

usage() {
  cat <<'EOF'
fetch_anchors.sh — build the CCRL-anchor engines (Blunder ladder) and Ordo into
a git-ignored engines/ directory, for `scripts/elo.py calibrate`.

Usage:
  scripts/fetch_anchors.sh                # build everything that's missing
  scripts/fetch_anchors.sh --force        # rebuild even if binaries exist
  scripts/fetch_anchors.sh --only blunder # just the Blunder ladder
  scripts/fetch_anchors.sh --only ordo    # just Ordo
  scripts/fetch_anchors.sh --help

Requires: git, go (Blunder), make + a C compiler (Ordo).
EOF
  exit 0
}

# ---------------------------------------------------------------------------
# Args
# ---------------------------------------------------------------------------
while [ $# -gt 0 ]; do
  case "$1" in
    --force) FORCE=1 ;;
    --only)  shift; ONLY="${1:-}" ;;
    --only=*) ONLY="${1#*=}" ;;
    -h|--help) usage ;;
    *) die "unknown argument: $1 (see --help)" ;;
  esac
  shift
done
if [ -n "${ONLY}" ] && [ "${ONLY}" != "blunder" ] && [ "${ONLY}" != "ordo" ]; then
  die "--only takes 'blunder' or 'ordo', got '${ONLY}'"
fi

want() { [ -z "${ONLY}" ] || [ "${ONLY}" = "$1" ]; }

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------
need_cmd() {
  command -v "$1" >/dev/null 2>&1 || die "$2"
}

# Confirm a UCI engine boots and answers `uci` with `uciok`. Some engines don't
# exit on `quit`/EOF, so run under a watchdog and kill it rather than block the
# whole pipeline (macOS has no coreutils `timeout`).
boot_check() {
  local bin="$1" tmp pid dog rc=1
  tmp="$(mktemp)"
  printf 'uci\nquit\n' | "${bin}" >"${tmp}" 2>/dev/null &
  pid=$!
  ( sleep 5; kill "${pid}" 2>/dev/null ) &
  dog=$!
  wait "${pid}" 2>/dev/null || true
  kill "${dog}" 2>/dev/null || true
  wait "${dog}" 2>/dev/null || true
  grep -q "uciok" "${tmp}" && rc=0
  rm -f "${tmp}"
  return "${rc}"
}

# Regenerate the manifest from what is actually on disk, so a partial (`--only`)
# run still yields a complete, accurate listing rather than clobbering it.
write_manifest() {
  {
    printf '# fetch_anchors.sh manifest — %s\n' "$(date '+%Y-%m-%d %H:%M:%S')"
    printf '# name  ccrl_rating  path\n'
    local rung ver rating out
    for rung in "${BLUNDER_RUNGS[@]}"; do
      ver="${rung%%:*}"; rating="${rung##*:}"
      out="${BLUNDER_DIR}/blunder-${ver}"
      [ -x "${out}" ] && printf 'blunder-%s %s %s\n' "${ver}" "${rating}" "${out}"
    done
    [ -x "${ORDO_DIR}/ordo" ] && printf 'ordo - %s\n' "${ORDO_DIR}/ordo"
    # Independent cross-check anchor (Fruit 2.1), once dropped under engines/fruit/.
    [ -x "${ENGINES_DIR}/fruit/fruit" ] && \
      printf 'fruit-2.1 2450 %s\n' "${ENGINES_DIR}/fruit/fruit"
  } >"${MANIFEST}"
}

# ---------------------------------------------------------------------------
# Blunder ladder (native Go build per tag)
# ---------------------------------------------------------------------------
build_blunder() {
  need_cmd git "git not found."
  need_cmd go  "go not found — Blunder is a Go engine. Install it with: brew install go"

  mkdir -p "${BLUNDER_DIR}"
  local clone="${SRC_DIR}/blunder"
  if [ ! -d "${clone}/.git" ]; then
    info "cloning Blunder -> ${clone}"
    git clone --quiet "${BLUNDER_REPO}" "${clone}"
  else
    info "updating Blunder clone"
    git -C "${clone}" fetch --quiet --tags
  fi

  local rung ver rating out built=0
  for rung in "${BLUNDER_RUNGS[@]}"; do
    ver="${rung%%:*}"
    rating="${rung##*:}"
    out="${BLUNDER_DIR}/blunder-${ver}"

    if [ -x "${out}" ] && [ "${FORCE}" -eq 0 ]; then
      ok "blunder-${ver} present (skip; --force to rebuild)"
      built=$((built + 1))
      continue
    fi

    info "building blunder-${ver} (CCRL ~${rating})"
    if ! git -C "${clone}" checkout --quiet "v${ver}"; then
      warn "blunder-${ver}: tag v${ver} not found — skipping"
      continue
    fi
    # Native host arch (arm64 on Apple Silicon); static pure-Go binary.
    if ( cd "${clone}" && CGO_ENABLED=0 go build -o "${out}" blunder/main.go ) 2>/dev/null; then
      if boot_check "${out}"; then
        ok "blunder-${ver} built + boots"
        built=$((built + 1))
      else
        warn "blunder-${ver}: built but failed UCI boot check"
      fi
    else
      warn "blunder-${ver}: go build failed (old tag / toolchain mismatch?)"
    fi
  done
  git -C "${clone}" checkout --quiet - 2>/dev/null || true
  info "Blunder ladder: ${built}/${#BLUNDER_RUNGS[@]} rungs ready"
}

# ---------------------------------------------------------------------------
# Ordo (rating fitter)
# ---------------------------------------------------------------------------
build_ordo() {
  need_cmd git  "git not found."
  need_cmd make "make not found."
  need_cmd cc   "no C compiler (cc) found."

  local out="${ORDO_DIR}/ordo"
  if [ -x "${out}" ] && [ "${FORCE}" -eq 0 ]; then
    ok "ordo present (skip; --force to rebuild)"
    return 0
  fi

  mkdir -p "${ORDO_DIR}"
  local clone="${SRC_DIR}/Ordo"
  if [ ! -d "${clone}/.git" ]; then
    info "cloning Ordo -> ${clone}"
    git clone --quiet "${ORDO_REPO}" "${clone}"
  fi

  info "building Ordo"
  # Ordo's Makefile assumes gcc; on macOS cc==clang. Two portability fixes:
  #   * -DNSPINLOCKS maps mythread_spinx_t to pthread_mutex_t — macOS/libpthread
  #     has no pthread_spinlock_t (perf is irrelevant for a rating fitter). This
  #     mirrors the Makefile's own -DMY_SEMAPHORES workaround for unnamed sems.
  #   * WARN= drops -Wlogical-op (a GCC-only flag clang rejects as unknown).
  # The one-line `all` target avoids the per-object rules.
  local ordo_cflags="-DNDEBUG -DMY_SEMAPHORES -DNSPINLOCKS -flto -I myopt -I sysport"
  if ( cd "${clone}" \
        && { make clean >/dev/null 2>&1 || true; } \
        && make CC=cc WARN= CFLAGS="${ordo_cflags}" all ) >/dev/null 2>&1 \
     && [ -x "${clone}/ordo" ]; then
    cp "${clone}/ordo" "${out}"
    ok "ordo built ($("${out}" -v 2>/dev/null | head -1))"
  else
    warn "Ordo build failed. Try manually:"
    warn "    cd ${clone} && make CC=cc WARN= CFLAGS=\"${ordo_cflags}\" all"
    return 1
  fi
}

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
mkdir -p "${ENGINES_DIR}" "${SRC_DIR}"

info "engines dir: ${ENGINES_DIR}"

if want blunder; then
  build_blunder
fi
if want ordo; then
  build_ordo
fi

write_manifest
printf '\n'
info "manifest written to ${MANIFEST}:"
sed 's/^/    /' "${MANIFEST}"

printf '\n'
info "NEXT — add the independent cross-check anchor (Fruit 2.1) and the registry:"
cat <<'EOF'
    The Blunder rungs share one eval family, so their absolute scale can drift as
    a group. Fruit 2.1 (~2450 CCRL, a different eval lineage with a very stable
    published rating) ties the ladder to true CCRL scale in the round-robin.
    Build it under engines/fruit/fruit; write_manifest picks it up automatically.
    Then register every anchor (name, cmd, proto, CCRL Elo) for `elo.py calibrate`.

    Verify EVERY rating against the live list before calibrating:
      https://computerchess.org.uk/ccrl/404/
EOF
ok "done"
