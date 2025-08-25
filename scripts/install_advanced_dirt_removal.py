#!/usr/bin/env python3
"""Advanced Dirt Removal Installer for DaVinci Resolve/Fusion"""

import os
import shutil
import platform
import argparse
from pathlib import Path

def get_fusion_macros_path():
    """Get the Fusion Macros directory path for the current OS"""
    system = platform.system()
    
    if system == "Darwin":  # macOS
        return Path.home() / "Library/Application Support/Blackmagic Design/DaVinci Resolve/Fusion/Macros"
    elif system == "Windows":
        return Path(os.environ["APPDATA"]) / "Blackmagic Design/DaVinci Resolve/Support/Fusion/Macros"
    elif system == "Linux":
        return Path.home() / ".local/share/DaVinciResolve/Fusion/Macros"
    else:
        raise OSError(f"Unsupported operating system: {system}")

def cleanup_old_macros(target_dir, dry_run=False):
    """Removes old, deprecated, or test macros."""
    files_to_remove = [
        "DirtTest.setting",
        "DirtTestFixes.setting", 
        "RecoveryBrush.setting",
        "test_resolvefx.setting",
        "test_resolvefx_format.setting",
        "AdvancedDirtRemoval_History.setting",
    ]
    
    print("Cleaning up old files...")
    print("-------------------------")
    
    for filename in files_to_remove:
        target_file = target_dir / filename
        if target_file.exists():
            if dry_run:
                print(f"Would remove: {target_file}")
            else:
                try:
                    os.remove(target_file)
                    print(f"✓ Removed: {filename}")
                except Exception as e:
                    print(f"✗ Failed to remove {filename}: {e}")
        else:
            print(f"○ Not found (already clean): {filename}")
    print()

def main():
    parser = argparse.ArgumentParser(description="Install or update Advanced Dirt Removal for DaVinci Resolve")
    parser.add_argument("--dry-run", action="store_true", help="Show what would be changed without actually changing anything")
    args = parser.parse_args()
    
    # Get target directory
    try:
        target_dir = get_fusion_macros_path()
    except OSError as e:
        print(f"Error: {e}")
        return 1

    # Get script directory and project root
    script_dir = Path(__file__).parent
    project_root = script_dir.parent
    fusion_dir = project_root / "Fusion"
    
    print(f"Advanced Dirt Removal Installer")
    print(f"================================")
    print(f"Target directory: {target_dir}")
    print(f"Source directory: {fusion_dir}")
    print()
    
    if args.dry_run:
        print("DRY RUN - No files will be copied or removed.")
        print()

    # 1. Cleanup
    cleanup_old_macros(target_dir, args.dry_run)

    # 2. Installation
    files_to_install = [
        ("Advanced Dirt Removal.setting", "Advanced Dirt Removal - Professional macro with enhanced UI"),
    ]
    
    print("Installing new macros...")
    print("-------------------------")

    # Check if source files exist
    missing_files = []
    for filename, description in files_to_install:
        source_file = fusion_dir / filename
        if not source_file.exists():
            missing_files.append(f"{filename} ({description})")
    
    if missing_files:
        print("Error: Missing source files:")
        for file in missing_files:
            print(f"  - {file}")
        return 1
    
    # Create target directory if it doesn't exist
    if not args.dry_run:
        target_dir.mkdir(parents=True, exist_ok=True)
    
    # Install files
    installed_count = 0
    for filename, description in files_to_install:
        source_file = fusion_dir / filename
        target_file = target_dir / filename
        
        if args.dry_run:
            print(f"Would install: {description}")
            print(f"  From: {source_file}")
            print(f"  To:   {target_file}")
        else:
            try:
                shutil.copy2(source_file, target_file)
                print(f"✓ Installed: {description}")
                installed_count += 1
            except Exception as e:
                print(f"✗ Failed to install {description}: {e}")
    print()
    
    if args.dry_run:
        print(f"Dry run complete. Would remove up to 6 old files and install {len(files_to_install)} file.")
    else:
        print(f"Installation complete! Installed {installed_count}/{len(files_to_install)} files.")
        print()
        print("Next steps:")
        print("1. Restart DaVinci Resolve/Fusion to see the new macro.")
        print("2. The new tool is called 'Advanced Dirt Removal'.")
        print("3. Connect your source to the main input (auto-connects).")
        print("4. Optionally connect Paint Mask or Magic Mask to the second input.")
        print("5. Choose Recovery Method: Paint Brush (manual) or Motion Mask (auto).")
        print("6. Use View Mode to switch between RGB (final) and Difference (diagnostic).")
    
    return 0

if __name__ == "__main__":
    exit(main())