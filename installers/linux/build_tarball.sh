#!/bin/sh
# Build the Linux x86-64 release tarball: OFX bundle, macros, licence and install.sh.
#   sh installers/linux/build_tarball.sh [version] [cmake options...]
# Example: sh installers/linux/build_tarball.sh 2.0.0 -DADR_WITH_CUDA=ON -DADR_WITH_OPENCL=ON
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
VERSION=${1:-2.0.0}
[ $# -gt 0 ] && shift
BUILD="$ROOT/build/release-linux"
cmake -S "$ROOT" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release "$@"
cmake --build "$BUILD" --parallel
NAME="AdvancedDirtRemoval-$VERSION-linux-x86_64"
STAGE="$BUILD/$NAME"
rm -rf "$STAGE"
mkdir -p "$STAGE"
cp -R "$BUILD/AdvancedDirtRemoval.ofx.bundle" "$STAGE/"
cp "$ROOT/Advanced Dirt Removal.setting" "$ROOT/LICENSE" "$STAGE/"
cp "$ROOT/installers/linux/install.sh" "$STAGE/"
chmod +x "$STAGE/install.sh"
mkdir -p "$ROOT/installers/linux/build"
tar -C "$BUILD" -czf "$ROOT/installers/linux/build/$NAME.tar.gz" "$NAME"
echo "Built: $ROOT/installers/linux/build/$NAME.tar.gz"
