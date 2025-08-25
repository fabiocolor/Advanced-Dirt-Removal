# Installation Guide

## Quick Install

### macOS
```bash
./scripts/install_one_click_mac.command
```

### Windows  
```batch
scripts\install_one_click_win.bat
```

### Linux
```bash
./scripts/install_one_click_linux.sh
```

## Manual Installation

### Step 1: Copy Macro File
Copy `Fusion/Advanced Dirt Removal.setting` to your Fusion Macros folder:

**macOS:**
```bash
cp "Fusion/Advanced Dirt Removal.setting" "~/Library/Application Support/Blackmagic Design/DaVinci Resolve/Fusion/Macros/"
```

**Windows:**
```batch
copy "Fusion\Advanced Dirt Removal.setting" "%APPDATA%\Blackmagic Design\DaVinci Resolve\Support\Fusion\Macros\"
```

**Linux:**
```bash
cp "Fusion/Advanced Dirt Removal.setting" "$HOME/.local/share/DavinciResolve/Fusion/Macros/"
```

### Step 2: Restart DaVinci Resolve
Close and restart DaVinci Resolve for the macro to appear.

## Verification

1. Open Fusion page in DaVinci Resolve
2. Right-click in node editor → Add Tool
3. Look for `Macros > Advanced Dirt Removal`

## Troubleshooting

**Macro doesn't appear:**
- Check file is in correct Macros folder
- Restart DaVinci Resolve completely
- Verify file permissions

**Macro won't load:**
- Requires DaVinci Resolve 18+
- Requires ResolveFX plugins (Studio version recommended)

## Install Paths

### macOS
`~/Library/Application Support/Blackmagic Design/DaVinci Resolve/Fusion/Macros/`

### Windows
`%APPDATA%\Blackmagic Design\DaVinci Resolve\Support\Fusion\Macros\`

### Linux  
`$HOME/.local/share/DavinciResolve/Fusion/Macros/`