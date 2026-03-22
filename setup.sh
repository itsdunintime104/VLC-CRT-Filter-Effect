#!/bin/bash
# ============================================================================
#  One-step setup: build and install CRT Emulator Plugin for VLC (macOS)
#  Usage: sudo ./setup.sh
# ============================================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

echo ""
echo "============================================"
echo "  CRT Emulator Plugin — Build & Install"
echo "============================================"
echo ""

# --- Step 1: Build ---
echo "[1/2] Building plugin..."
"$SCRIPT_DIR/build_macos.sh"

# --- Step 2: Install ---
echo "[2/2] Installing..."
"$SCRIPT_DIR/install_macos.sh"

echo ""
echo "============================================"
echo "  ALL DONE"
echo "============================================"
echo ""
echo "  1. Open VLC"
echo "  2. Preferences > Show All > Video > Filters"
echo "     Check 'CRT Emulator video filter' > Save > Restart VLC"
echo "  3. VLC > Extensions > CRT Emulator > Show Controller"
echo ""
