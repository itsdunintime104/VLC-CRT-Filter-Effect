#!/bin/bash
# ============================================================================
#  Install script for CRT Emulator VLC plugin — macOS
#  Run with: sudo ./install_macos.sh
# ============================================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PLUGIN_SRC="$SCRIPT_DIR/build/libcrt_scanline_plugin.dylib"
LUA_SRC="$SCRIPT_DIR/lua/crt_scanline_controller.lua"

VLC_APP="/Applications/VLC.app"
VLC_PLUGINS="$VLC_APP/Contents/MacOS/plugins"
VLC_LUA_EXT="$VLC_APP/Contents/MacOS/share/lua/extensions"
VLC_CACHE="$VLC_PLUGINS/plugins.dat"

# --- Check we have the built plugin ---
if [ ! -f "$PLUGIN_SRC" ]; then
    echo "ERROR: Plugin not found at $PLUGIN_SRC"
    echo "Run ./build_macos.sh first"
    exit 1
fi

# --- Check VLC exists ---
if [ ! -d "$VLC_APP" ]; then
    echo "ERROR: VLC.app not found at $VLC_APP"
    exit 1
fi

# --- Close VLC if running ---
if pgrep -x VLC >/dev/null 2>&1; then
    echo "Closing VLC..."
    osascript -e 'tell application "VLC" to quit' 2>/dev/null || killall VLC 2>/dev/null
    sleep 2
fi

echo ""
echo "============================================"
echo "  Installing CRT Emulator Plugin"
echo "============================================"
echo ""

# --- Install plugin ---
echo "Installing video filter plugin..."
cp "$PLUGIN_SRC" "$VLC_PLUGINS/libcrt_scanline_plugin.dylib"
echo "  -> $VLC_PLUGINS/libcrt_scanline_plugin.dylib"

# --- Install Lua extension ---
echo "Installing Lua controller extension..."
mkdir -p "$VLC_LUA_EXT"
cp "$LUA_SRC" "$VLC_LUA_EXT/crt_scanline_controller.lua"
echo "  -> $VLC_LUA_EXT/crt_scanline_controller.lua"

# --- Install overlays ---
OVERLAY_SRC="$SCRIPT_DIR/overlays"
VLC_OVERLAYS="$VLC_APP/Contents/MacOS/share/crt-overlays"
if [ -d "$OVERLAY_SRC" ]; then
    echo "Installing TV overlay images..."
    mkdir -p "$VLC_OVERLAYS"
    cp "$OVERLAY_SRC"/*.png "$VLC_OVERLAYS/" 2>/dev/null
    OVERLAY_COUNT=$(ls "$VLC_OVERLAYS"/*.png 2>/dev/null | wc -l | tr -d ' ')
    echo "  -> $VLC_OVERLAYS/ ($OVERLAY_COUNT overlays)"
fi

# --- Clear plugin cache ---
if [ -f "$VLC_CACHE" ]; then
    echo "Clearing plugin cache..."
    rm "$VLC_CACHE"
fi

echo ""
echo "============================================"
echo "  INSTALL COMPLETE"
echo "============================================"
echo ""
echo "To enable:"
echo "  1. Open VLC"
echo "  2. VLC > Preferences (Cmd+,) > Show All (bottom-left)"
echo "  3. Video > Filters > check 'CRT Emulator video filter'"
echo "  4. Save, restart VLC"
echo "  5. VLC > Extensions > CRT Emulator > Show Controller"
echo ""
echo "Or launch directly:"
echo '  open -a VLC --args --video-filter=crtemulator "your_video.mp4"'
echo ""
