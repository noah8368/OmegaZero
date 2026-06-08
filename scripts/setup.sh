#!/usr/bin/env bash
# OmegaZero prerequisite installer.
# Detects macOS vs Linux, installs system deps, creates a Python venv,
# and builds the engine.
#
# Usage:
#   ./scripts/setup.sh            — full dev install
#   ./scripts/setup.sh --datagen  — minimal install for datagen server only
#
# After setup, activate the venv with:
#   source .venv/bin/activate

set -euo pipefail

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

info()  { echo -e "${GREEN}[+]${NC} $1"; }
warn()  { echo -e "${YELLOW}[!]${NC} $1"; }
error() { echo -e "${RED}[x]${NC} $1"; }

SERVER_ONLY=false
if [[ "${1:-}" == "--datagen" ]]; then
    SERVER_ONLY=true
fi

OS="$(uname -s)"
REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
VENV_DIR="$REPO_ROOT/.venv"

cd "$REPO_ROOT"

# ---------- System dependencies ----------

install_system_deps_macos() {
    if ! xcode-select -p &>/dev/null; then
        info "Installing Xcode Command Line Tools..."
        xcode-select --install
        echo "  Press any key after the installer finishes."
        read -r
    else
        info "Xcode CLT: already installed"
    fi

    if ! command -v brew &>/dev/null; then
        info "Installing Homebrew..."
        /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
    else
        info "Homebrew: already installed"
    fi

    BREW_PACKAGES=(python3 stockfish graphviz cairo cutechess)
    for pkg in "${BREW_PACKAGES[@]}"; do
        if brew list "$pkg" &>/dev/null; then
            info "$pkg: already installed"
        else
            info "Installing $pkg..."
            brew install "$pkg"
        fi
    done
}

install_system_deps_linux() {
    if command -v apt-get &>/dev/null; then
        info "Installing system packages (apt)..."
        apt-get update -qq
        apt-get install -y -qq g++ make python3 python3-venv git
    elif command -v dnf &>/dev/null; then
        info "Installing system packages (dnf)..."
        dnf install -y gcc-c++ make python3 git
    elif command -v yum &>/dev/null; then
        info "Installing system packages (yum)..."
        yum install -y gcc-c++ make python3 git
    else
        error "Unsupported package manager. Install manually: g++, make, python3, git"
        exit 1
    fi
}

# ---------- Python venv ----------

setup_venv() {
    if [[ -d "$VENV_DIR" ]]; then
        info "Python venv: already exists at .venv/"
    else
        info "Creating Python venv at .venv/..."
        python3 -m venv "$VENV_DIR"
    fi

    source "$VENV_DIR/bin/activate"
    pip install --quiet --upgrade pip

    if [[ "$SERVER_ONLY" == true ]]; then
        info "Installing minimal Python deps (datagen only)..."
        pip install --quiet --upgrade tqdm
    else
        info "Installing Python packages into venv..."
        pip install --quiet --upgrade \
            torch \
            numpy \
            matplotlib \
            tqdm \
            Pillow \
            cairosvg \
            graphviz \
            python-chess
    fi
}

# ---------- Build ----------

build_engine() {
    if [[ "$OS" == "Darwin" ]]; then
        local jobs
        jobs="$(sysctl -n hw.ncpu)"
    else
        local jobs
        jobs="$(nproc)"
    fi

    if [[ "$SERVER_ONLY" == true ]]; then
        info "Building datagen harness..."
        make datagen -j"$jobs" 2>&1 | tail -3
    else
        info "Building OmegaZero (all targets)..."
        make -j"$jobs" 2>&1 | tail -3
        make datagen -j"$jobs" 2>&1 | tail -3
    fi
}

# ---------- Main ----------

if [[ "$OS" == "Darwin" ]]; then
    info "Detected macOS"
    install_system_deps_macos
elif [[ "$OS" == "Linux" ]]; then
    info "Detected Linux"
    install_system_deps_linux
else
    error "Unsupported OS: $OS"
    exit 1
fi

setup_venv
build_engine

echo ""
info "Setup complete!"
echo ""
echo "  Activate the venv:  source .venv/bin/activate"
if [[ "$SERVER_ONLY" == true ]]; then
    echo "  Run datagen:        nohup ./scripts/run_datagen.sh > datagen.log 2>&1 &"
else
    echo "  Engine binary:      build/OmegaZero"
    echo "  Datagen binary:     build/datagen_harness"
    echo "  Run tests:          python3 scripts/perft.py run"
    echo "  Train NNUE:         python3 scripts/train_nnue.py"
fi
