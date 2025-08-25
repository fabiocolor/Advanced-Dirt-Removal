#!/bin/bash

echo "🚀 Installing Optimized 2-Macro Dirt Removal System for DaVinci Resolve..."

# Get the directory where this script is located
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

# Fusion paths
MACROS_DIR="$HOME/Library/Application Support/Blackmagic Design/DaVinci Resolve/Fusion/Macros"

echo "📁 Creating directory..."
mkdir -p "$MACROS_DIR"

echo "📋 Installing Advanced Dirt Removal macro..."
cp "$PROJECT_DIR/Fusion/Advanced Dirt Removal.setting" "$MACROS_DIR/"

echo "🎨 Installing RecoveryBrush macro (simplified MaskPaint wrapper)..."
cp "$PROJECT_DIR/Fusion/RecoveryBrush.setting" "$MACROS_DIR/"

echo "✅ Installation complete!"
echo ""
echo "📍 Files installed to:"
echo "   • Advanced Dirt Removal: $MACROS_DIR/Advanced Dirt Removal.setting"
echo "   • RecoveryBrush: $MACROS_DIR/RecoveryBrush.setting"
echo ""
echo "🔄 Next steps:"
echo "   1. Restart DaVinci Resolve"
echo "   2. In Fusion, look for both tools in Macros"
echo "   3. Connect: Source → Advanced Dirt Removal → Output"
echo "   4. Connect: Source → RecoveryBrush → (Mask output to Advanced Dirt Removal)"
echo "   5. Paint with RecoveryBrush - no mode confusion, settings are locked!"
echo ""
echo "📚 See EXTERNAL_BRUSH_WORKFLOW.md for usage instructions"

# Keep terminal open on macOS
read -p "Press Enter to close..."

