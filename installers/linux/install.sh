#!/bin/sh
# Advanced Dirt Removal for Linux: install (default) or uninstall.
#   sudo ./install.sh              OFX effect to /usr/OFX/Plugins
#   sudo ./install.sh --uninstall
# Without sudo the OFX effect goes to ~/OFX/Plugins instead; point your host
# there with OFX_PLUGIN_PATH=$HOME/OFX/Plugins.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
BUNDLE=AdvancedDirtRemoval.ofx.bundle
if [ "$(id -u)" = 0 ]; then OFX_DIR=/usr/OFX/Plugins; else OFX_DIR="$HOME/OFX/Plugins"; fi

if [ "${1:-}" = "--uninstall" ]; then
  rm -rf "$OFX_DIR/$BUNDLE"
  # Remove the plugin folders too if the install created them and nothing else is there.
  rmdir "$OFX_DIR" 2>/dev/null && rmdir "$(dirname "$OFX_DIR")" 2>/dev/null || true
  echo "Advanced Dirt Removal uninstalled."
  exit 0
fi

mkdir -p "$OFX_DIR"
rm -rf "$OFX_DIR/$BUNDLE"
cp -R "$HERE/$BUNDLE" "$OFX_DIR/"
echo "OFX effect installed to $OFX_DIR/$BUNDLE"
echo "Restart your OFX host (DaVinci Resolve, Nuke, ...)."
