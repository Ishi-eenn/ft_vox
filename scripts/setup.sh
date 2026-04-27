#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(dirname "$SCRIPT_DIR")"
VENDOR="$ROOT/vendor"
mkdir -p "$VENDOR"

RED='\033[0;31m'
GRN='\033[0;32m'
YLW='\033[1;33m'
NC='\033[0m'

info()  { echo -e "${GRN}[setup]${NC} $*"; }
warn()  { echo -e "${YLW}[setup]${NC} $*"; }
error() { echo -e "${RED}[setup]${NC} $*" >&2; exit 1; }

# ── Tool checks ───────────────────────────────────────────────────────────────
command -v cmake  &>/dev/null || error "cmake not found. Please install cmake."
command -v git    &>/dev/null || error "git not found. Please install git."
command -v curl   &>/dev/null || error "curl not found. Please install curl."
command -v python3 &>/dev/null || error "python3 not found. Required for GLAD generation."

# ── GLFW 3.4 (build from source → vendor/glfw/) ───────────────────────────────
if [ -f "$VENDOR/glfw/lib/libglfw3.a" ]; then
    warn "GLFW already built, skipping."
else
    info "Building GLFW 3.4..."
    TMP=$(mktemp -d)
    trap 'rm -rf "$TMP"' EXIT

    curl -fsSL \
        "https://github.com/glfw/glfw/releases/download/3.4/glfw-3.4.zip" \
        -o "$TMP/glfw.zip"
    unzip -q "$TMP/glfw.zip" -d "$TMP"

    cmake -S "$TMP/glfw-3.4" -B "$TMP/glfw-build" \
        -DGLFW_BUILD_DOCS=OFF \
        -DGLFW_BUILD_TESTS=OFF \
        -DGLFW_BUILD_EXAMPLES=OFF \
        -DCMAKE_INSTALL_PREFIX="$VENDOR/glfw" \
        -Wno-dev \
        -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
        > /dev/null

    cmake --build "$TMP/glfw-build" -j"$(nproc 2>/dev/null || sysctl -n hw.logicalcpu 2>/dev/null || echo 4)" \
        > /dev/null
    cmake --install "$TMP/glfw-build" > /dev/null

    trap - EXIT
    rm -rf "$TMP"
    info "GLFW → vendor/glfw/"
fi

# ── GLAD v2 (gl:core=4.1 → vendor/glad/) ─────────────────────────────────────
if [ -f "$VENDOR/glad/src/gl.c" ]; then
    warn "GLAD already generated, skipping."
else
    info "Generating GLAD (gl:core=4.1)..."
    VENV=$(mktemp -d)
    python3 -m venv "$VENV" > /dev/null
    "$VENV/bin/pip" install glad2 --quiet
    GLAD_OUT="$VENDOR/glad"
    "$VENV/bin/python" -c "
import ssl, sys
ssl._create_default_https_context = ssl._create_unverified_context
from glad.__main__ import main
sys.argv = ['glad', '--api', 'gl:core=4.1', '--out-path', '$GLAD_OUT']
main()
"
    rm -rf "$VENV"
    info "GLAD → vendor/glad/"
fi

# ── GLM 1.0.1 (header-only → vendor/glm/) ────────────────────────────────────
if [ -d "$VENDOR/glm/glm" ]; then
    warn "GLM already present, skipping."
else
    info "Downloading GLM 1.0.1..."
    git clone --depth 1 --branch 1.0.1 \
        "https://github.com/g-truc/glm" "$VENDOR/glm" --quiet
    info "GLM → vendor/glm/"
fi

# ── stb (stb_image.h → vendor/stb/) ─────────────────────────────────────────
if [ -f "$VENDOR/stb/stb_image.h" ]; then
    warn "stb_image.h already present, skipping."
else
    info "Downloading stb_image.h..."
    mkdir -p "$VENDOR/stb"
    curl -fsSL \
        "https://raw.githubusercontent.com/nothings/stb/master/stb_image.h" \
        -o "$VENDOR/stb/stb_image.h"
    info "stb → vendor/stb/"
fi

# ── FastNoiseLite (header-only → vendor/FastNoiseLite/) ──────────────────────
if [ -f "$VENDOR/FastNoiseLite/FastNoiseLite.h" ]; then
    warn "FastNoiseLite already present, skipping."
else
    info "Downloading FastNoiseLite..."
    mkdir -p "$VENDOR/FastNoiseLite"
    curl -fsSL \
        "https://raw.githubusercontent.com/Auburn/FastNoiseLite/master/Cpp/FastNoiseLite.h" \
        -o "$VENDOR/FastNoiseLite/FastNoiseLite.h"
    info "FastNoiseLite → vendor/FastNoiseLite/"
fi

info ""
info "All dependencies ready. Run 'make' to build."
