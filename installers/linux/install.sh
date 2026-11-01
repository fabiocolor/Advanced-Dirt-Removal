#!/bin/sh
# Advanced Dirt Removal for Linux: install (default) or uninstall.
#   sudo ./install.sh              OFX effect to /usr/OFX/Plugins, macros for the invoking user
#   sudo ./install.sh --uninstall
# Without sudo the OFX effect goes to ~/OFX/Plugins instead; point your host
# there with OFX_PLUGIN_PATH=$HOME/OFX/Plugins.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
BUNDLE=AdvancedDirtRemoval.ofx.bundle
if [ "$(id -u)" = 0 ]; then OFX_DIR=/usr/OFX/Plugins; else OFX_DIR="$HOME/OFX/Plugins"; fi
USER_HOME=$HOME
if [ -n "${SUDO_USER:-}" ]; then USER_HOME=$(getent passwd "$SUDO_USER" | cut -d: -f6); fi
MACROS="$USER_HOME/.local/share/DaVinciResolve/Fusion/Macros"

if [ "${1:-}" = "--uninstall" ]; then
  rm -rf "$OFX_DIR/$BUNDLE"
  rm -f "$MACROS/Advanced Dirt Removal.setting" "$MACROS/Advanced Dirt Removal Cineon.setting"
  echo "Advanced Dirt Removal uninstalled."
  exit 0
fi

mkdir -p "$OFX_DIR"
rm -rf "$OFX_DIR/$BUNDLE"
cp -R "$HERE/$BUNDLE" "$OFX_DIR/"
echo "OFX effect installed to $OFX_DIR/$BUNDLE"
if [ -f "$HERE/Advanced Dirt Removal.setting" ]; then
  mkdir -p "$MACROS"
  cp "$HERE/Advanced Dirt Removal.setting" "$HERE/Advanced Dirt Removal Cineon.setting" "$MACROS/"
  if [ -n "${SUDO_USER:-}" ]; then chown -R "$SUDO_USER" "$USER_HOME/.local/share/DaVinciResolve/Fusion" 2>/dev/null || true; fi
  echo "Fusion macros installed to $MACROS"
fi
echo "Restart your OFX host (DaVinci Resolve, Nuke, ...)."
