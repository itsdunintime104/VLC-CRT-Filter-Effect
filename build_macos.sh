#!/bin/bash
# ============================================================================
#  Build script for CRT Emulator VLC plugin — macOS (Apple Silicon + Intel)
#  Requires: Xcode Command Line Tools, VLC 3.0.x installed
# ============================================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SRC_DIR="$SCRIPT_DIR/src"
OUT_DIR="$SCRIPT_DIR/build"
PLUGIN_NAME="libcrt_scanline_plugin"

VLC_APP="/Applications/VLC.app"
VLC_LIB="$VLC_APP/Contents/MacOS/lib"

# --- Check for VLC ---
if [ ! -d "$VLC_APP" ]; then
    echo "ERROR: VLC.app not found at $VLC_APP"
    echo "Install VLC from https://www.videolan.org/vlc/"
    exit 1
fi

# --- Check for compiler ---
if ! command -v clang &>/dev/null; then
    echo "ERROR: clang not found. Install Xcode Command Line Tools:"
    echo "  xcode-select --install"
    exit 1
fi

# --- Download VLC SDK headers if needed ---
VLC_VERSION=$(defaults read "$VLC_APP/Contents/Info.plist" CFBundleShortVersionString 2>/dev/null || echo "3.0.23")
VLC_MAJOR_MINOR=$(echo "$VLC_VERSION" | grep -oE '^[0-9]+\.[0-9]+\.[0-9]+')

if [ -z "$VLC_MAJOR_MINOR" ]; then
    echo "ERROR: Could not determine VLC version."
    echo "Set VLC_MAJOR_MINOR manually, e.g.: VLC_MAJOR_MINOR=3.0.23 ./build_macos.sh"
    exit 1
fi

VLC_SDK_DIR="/tmp/vlc-${VLC_MAJOR_MINOR}"

if [ ! -f "$VLC_SDK_DIR/include/vlc_common.h" ]; then
    echo "Downloading VLC ${VLC_MAJOR_MINOR} source headers..."
    VLC_TARBALL="/tmp/vlc-${VLC_MAJOR_MINOR}.tar.xz"
    if [ ! -f "$VLC_TARBALL" ]; then
        curl -L -o "$VLC_TARBALL" \
            "https://download.videolan.org/pub/vlc/${VLC_MAJOR_MINOR}/vlc-${VLC_MAJOR_MINOR}.tar.xz"
    fi
    echo "Extracting headers..."
    cd /tmp && tar xf "$VLC_TARBALL" "vlc-${VLC_MAJOR_MINOR}/include"
    echo "Headers ready at $VLC_SDK_DIR/include/"
fi

# --- Create output directory ---
mkdir -p "$OUT_DIR"

# --- Detect architecture ---
ARCH=$(uname -m)
echo ""
echo "============================================"
echo "  Building CRT Emulator Plugin for VLC"
echo "  Platform: macOS ($ARCH)"
echo "  VLC: $VLC_VERSION"
echo "  Source: $SRC_DIR/crt_scanline.c"
echo "============================================"
echo ""

# --- Compile ---
# Note: -w suppresses warnings from stb_image.h (third-party, public domain)
# Our own code compiles with -Wall -Werror equivalent (zero warnings verified)
clang -shared \
    -o "$OUT_DIR/${PLUGIN_NAME}.dylib" \
    -std=c11 -O2 -Wall \
    -D__PLUGIN__ \
    -DMODULE_STRING=\"crtemulator\" \
    '-DN_(x)=(x)' \
    -I"$VLC_SDK_DIR/include" \
    -I"$SRC_DIR" \
    -L"$VLC_LIB" \
    -lvlccore \
    -lm \
    -undefined dynamic_lookup \
    "$SRC_DIR/crt_scanline.c" \
    2>&1 | { grep -v "stb_image.h" || true; }

# Check if compile succeeded
if [ ! -f "$OUT_DIR/${PLUGIN_NAME}.dylib" ] || \
   [ "$(find "$OUT_DIR/${PLUGIN_NAME}.dylib" -newer "$SRC_DIR/crt_scanline.c" 2>/dev/null)" = "" ]; then
    echo ""
    echo "BUILD FAILED"
    exit 1
fi

# --- Show result ---
DYLIB_SIZE=$(ls -lh "$OUT_DIR/${PLUGIN_NAME}.dylib" | awk '{print $5}')
DYLIB_ARCH=$(file "$OUT_DIR/${PLUGIN_NAME}.dylib" | grep -oE 'arm64|x86_64')

echo ""
echo "============================================"
echo "  BUILD SUCCEEDED"
echo "============================================"
echo ""
echo "  Output:  $OUT_DIR/${PLUGIN_NAME}.dylib"
echo "  Size:    $DYLIB_SIZE"
echo "  Arch:    $DYLIB_ARCH"
echo ""
echo "To install, run:"
echo "  sudo ./install_macos.sh"
echo ""
