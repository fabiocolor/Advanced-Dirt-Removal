#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
SRC="$REPO_ROOT/Fusion/Advanced Dirt Removal.setting"
DEST="$HOME/.local/share/DaVinciResolve/Fusion/Macros"

echo "Installing Advanced Dirt Removal (Linux)"
if [[ ! -f "$SRC" ]]; then
  echo "ERROR: Missing file: $SRC" >&2
  exit 1
fi

mkdir -p "$DEST"
cp -f "$SRC" "$DEST/"
echo "Installed to: $DEST"
echo "Restart Resolve/Fusion to load the tool."

