#!/bin/bash
set -euo pipefail

# Build macOS .pkg for Advanced Dirt Removal
# Run from repo root: bash installers/mac/build_pkg.sh

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD_DIR="$REPO_ROOT/installers/mac/build"
PAYLOAD_DIR="$BUILD_DIR/payload"
SCRIPTS_DIR="$REPO_ROOT/installers/mac/scripts"
RESOURCES_DIR="$REPO_ROOT/installers/mac"
PKG_ID="com.fabiocolor.AdvancedDirtRemoval"
VERSION="1.1.0"

MACRO_1="Advanced Dirt Removal.setting"
MACRO_2="Advanced Dirt Removal Cineon.setting"

rm -rf "$BUILD_DIR"
mkdir -p "$PAYLOAD_DIR/tmp/AdvancedDirtRemoval_payload"

# Ensure installer scripts are executable Unix text files.
for script in "$SCRIPTS_DIR/preinstall" "$SCRIPTS_DIR/postinstall"; do
  [ -f "$script" ] || continue
  perl -pi -e 's/\r$//' "$script"
  chmod +x "$script"
done

cp "$REPO_ROOT/$MACRO_1" "$PAYLOAD_DIR/tmp/AdvancedDirtRemoval_payload/"
cp "$REPO_ROOT/$MACRO_2" "$PAYLOAD_DIR/tmp/AdvancedDirtRemoval_payload/"

pkgbuild \
  --root "$PAYLOAD_DIR" \
  --identifier "$PKG_ID" \
  --version "$VERSION" \
  --scripts "$SCRIPTS_DIR" \
  "$BUILD_DIR/AdvancedDirtRemoval-component.pkg"

productbuild \
  --distribution "$RESOURCES_DIR/distribution.xml" \
  --resources "$RESOURCES_DIR" \
  --package-path "$BUILD_DIR" \
  "$BUILD_DIR/AdvancedDirtRemoval-$VERSION.pkg"

echo "Built: $BUILD_DIR/AdvancedDirtRemoval-$VERSION.pkg"
