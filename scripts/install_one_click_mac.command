#!/bin/bash

echo "🚀 Installing Advanced Dirt Removal for DaVinci Resolve..."

# Get the directory where this script is located
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

# Fusion paths
MACROS_DIR="$HOME/Library/Application Support/Blackmagic Design/DaVinci Resolve/Fusion/Macros"

echo "📁 Creating directory..."
mkdir -p "$MACROS_DIR"

echo "📋 Installing Advanced Dirt Removal macro..."
cp "$PROJECT_DIR/Fusion/Advanced Dirt Removal.setting" "$MACROS_DIR/"

echo "✅ Installation complete!"
echo ""
echo "📍 File installed to:"
echo "   • $MACROS_DIR/Advanced Dirt Removal.setting"
echo ""
echo "🔄 Next steps:"
echo "   1. Restart DaVinci Resolve"
echo "   2. In Fusion, look for 'Advanced Dirt Removal' in Macros"
echo "   3. Connect your source and optionally a Paint/Magic Mask"
echo ""

# Keep terminal open on macOS
read -p "Press Enter to close..."

