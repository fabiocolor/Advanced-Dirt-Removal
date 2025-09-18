#!/bin/bash

# Advanced Dirt Removal - One-Click Installer for macOS
# Double-click this file to install automatically

# Function to show dialog using osascript
show_dialog() {
    osascript -e "display dialog \"$1\" with title \"Advanced Dirt Removal Installer\" buttons {\"OK\"} default button \"OK\" with icon note"
}

show_error() {
    osascript -e "display dialog \"❌ Error: $1\" with title \"Installation Failed\" buttons {\"OK\"} default button \"OK\" with icon stop"
}

show_success() {
    osascript -e "display dialog \"✅ $1\" with title \"Installation Complete\" buttons {\"OK\"} default button \"OK\" with icon note"
}

# Get the directory where this script is located
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

# Target directory for DaVinci Resolve Effects
EFFECTS_DIR="$HOME/Library/Application Support/Blackmagic Design/DaVinci Resolve/Fusion/Templates/Edit/Effects"
SOURCE_FILE="$PROJECT_DIR/Advanced Dirt Removal.setting"

# Show welcome dialog
show_dialog "Welcome to Advanced Dirt Removal installer!

This will install the macro to:
$EFFECTS_DIR

Click OK to continue..."

# Check if source file exists
if [ ! -f "$SOURCE_FILE" ]; then
    show_error "Source file not found at: $SOURCE_FILE"
    exit 1
fi

# Create target directory
echo "Creating target directory..."
mkdir -p "$EFFECTS_DIR"

# Copy the file
echo "Installing Advanced Dirt Removal..."
if cp "$SOURCE_FILE" "$EFFECTS_DIR/"; then
    show_success "Advanced Dirt Removal installed successfully!

Location: $EFFECTS_DIR

Next steps:
1. Restart DaVinci Resolve
2. Find the effect in Effects > Templates
3. Drag onto your timeline or use in Fusion"
else
    show_error "Failed to copy file. Check permissions and try again."
    exit 1
fi

