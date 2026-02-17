# Install Advanced Dirt Removal

## Option 1 (recommended for Windows): `winget`

1. Run:
   - `winget install FabioColor.AdvancedDirtRemoval`
2. Restart DaVinci Resolve.
3. To update later:
   - `winget upgrade FabioColor.AdvancedDirtRemoval`

## Option 2: Installer ZIP (fallback)

1. Download from the latest release:
   - `AdvancedDirtRemoval-Setup-<version>-win64.zip` (Windows)
   - `AdvancedDirtRemoval-<version>-macos.zip` (macOS)
2. Extract the ZIP.
3. Run the installer inside (`.exe` or `.pkg`).
4. Restart DaVinci Resolve.

Note: On Windows, downloaded `.exe` files may trigger SmartScreen. `winget` is the preferred path.

## Option 3: Manual `.setting` install

1. Download both files from the latest release:
   - `Advanced Dirt Removal.setting`
   - `Advanced Dirt Removal Cineon.setting`
2. Copy both files to your DaVinci Resolve Fusion Macros folder:
   - macOS: `~/Library/Application Support/Blackmagic Design/DaVinci Resolve/Fusion/Macros/`
   - Windows: `%APPDATA%\Blackmagic Design\DaVinci Resolve\Support\Fusion\Macros\`
   - Linux: `~/.local/share/DaVinciResolve/Fusion/Macros/`
3. Restart DaVinci Resolve.
4. Add either macro from Fusion tools/templates.

## Uninstall

- Installer path: run the same installer again and choose uninstall if prompted.
- Manual path: delete both `.setting` files from the same folder, then restart Resolve.
