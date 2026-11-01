#!/bin/bash
set -euo pipefail

# Build the macOS installer for the Advanced Dirt Removal OFX effect (/Library/OFX/Plugins).
#   bash installers/mac/build_pkg.sh [version] [path/to/AdvancedDirtRemoval.ofx.bundle]
# Without a bundle path it builds a universal Release bundle with CMake first.
# Set ADR_PKG_SIGN_IDENTITY to a "Developer ID Installer" identity to sign.

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT_DIR="$REPO_ROOT/installers/mac/build"
# Stage on a local temporary volume: some synced or network volumes keep
# extended attributes as ._ files, which would end up in the package.
BUILD_DIR="$(mktemp -d "${TMPDIR:-/tmp}/adr-pkg.XXXXXX")"
trap 'rm -rf "$BUILD_DIR"' EXIT
PAYLOAD_DIR="$BUILD_DIR/payload"
SCRIPTS_DIR="$REPO_ROOT/installers/mac/scripts"
RESOURCES_DIR="$REPO_ROOT/installers/mac"
PKG_ID="com.fabiocolor.AdvancedDirtRemoval"
VERSION="${1:-2.0.0}"
BUNDLE="${2:-}"
BUNDLE_NAME="AdvancedDirtRemoval.ofx.bundle"


if [ -z "$BUNDLE" ]; then
  cmake -S "$REPO_ROOT" -B "$REPO_ROOT/build/release-macos" -DCMAKE_BUILD_TYPE=Release -DADR_WITH_OPENCL=ON \
    -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
  cmake --build "$REPO_ROOT/build/release-macos" --config Release --parallel
  BUNDLE="$REPO_ROOT/build/release-macos/$BUNDLE_NAME"
fi
[ -d "$BUNDLE" ] || { echo "Bundle not found: $BUNDLE" >&2; exit 1; }
codesign --verify --deep --strict "$BUNDLE"

mkdir -p "$PAYLOAD_DIR/Library/OFX/Plugins"
ditto "$BUNDLE" "$PAYLOAD_DIR/Library/OFX/Plugins/$BUNDLE_NAME"
# No extended attributes in the payload (they would become ._ files).
xattr -cr "$PAYLOAD_DIR"
codesign --verify --deep --strict "$PAYLOAD_DIR/Library/OFX/Plugins/$BUNDLE_NAME"

# Ensure installer scripts are executable Unix text files.
mkdir -p "$BUILD_DIR/scripts"
for script in preinstall postinstall; do
  perl -pe 's/\r$//' "$SCRIPTS_DIR/$script" > "$BUILD_DIR/scripts/$script"
  chmod +x "$BUILD_DIR/scripts/$script"
done
SCRIPTS_DIR="$BUILD_DIR/scripts"

# Install the bundle exactly at /Library/OFX/Plugins: never "upgrade" a bundle
# with the same identifier found elsewhere on the disk.
COMPONENTS="$BUILD_DIR/components.plist"
pkgbuild --analyze --root "$PAYLOAD_DIR" "$COMPONENTS"
for key in BundleIsRelocatable BundleIsVersionChecked; do
  /usr/libexec/PlistBuddy -c "Delete :0:$key" "$COMPONENTS" >/dev/null 2>&1 || true
  /usr/libexec/PlistBuddy -c "Add :0:$key bool false" "$COMPONENTS"
done

pkgbuild \
  --root "$PAYLOAD_DIR" \
  --component-plist "$COMPONENTS" \
  --identifier "$PKG_ID" \
  --version "$VERSION" \
  --scripts "$SCRIPTS_DIR" \
  "$BUILD_DIR/AdvancedDirtRemoval-component.pkg"

DIST_FILE="$BUILD_DIR/distribution.xml"
sed -E "s/version=\"[0-9]+\\.[0-9]+\\.[0-9]+\"/version=\"$VERSION\"/" "$RESOURCES_DIR/distribution.xml" > "$DIST_FILE"

SIGN=()
if [ -n "${ADR_PKG_SIGN_IDENTITY:-}" ]; then SIGN=(--sign "$ADR_PKG_SIGN_IDENTITY"); fi
productbuild \
  --distribution "$DIST_FILE" \
  --resources "$RESOURCES_DIR" \
  --package-path "$BUILD_DIR" \
  ${SIGN[@]+"${SIGN[@]}"} \
  "$BUILD_DIR/AdvancedDirtRemoval-$VERSION.pkg"

mkdir -p "$OUT_DIR"
cp "$BUILD_DIR/AdvancedDirtRemoval-$VERSION.pkg" "$OUT_DIR/"
echo "Built: $OUT_DIR/AdvancedDirtRemoval-$VERSION.pkg"
