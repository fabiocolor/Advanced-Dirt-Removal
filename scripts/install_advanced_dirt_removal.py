#!/usr/bin/env python3
"""Advanced Dirt Removal Installer for DaVinci Resolve/Fusion"""

import os
import shutil
import platform
import argparse
from pathlib import Path

def get_fusion_templates_path():
    """Get the Fusion Templates directory path for the current OS."""
    system = platform.system()
    base_path = ""

    if system == "Darwin":  # macOS
        base_path = Path.home() / "Library/Application Support/Blackmagic Design/DaVinci Resolve"
    elif system == "Windows":
        base_path = Path(os.environ.get("APPDATA")) / "Blackmagic Design/DaVinci Resolve/Support"
    elif system == "Linux":
        # Standard path for user-installed Resolve
        user_path = Path.home() / ".local/share/DaVinciResolve"
        # Path for Resolve installed in /opt (common for some packages)
        opt_path = Path("/opt/resolve")
        if user_path.exists():
            base_path = user_path
        elif opt_path.exists():
            base_path = opt_path
        else:
            # Default to the user path if neither exists, allowing mkdir to create it later
            base_path = user_path
    else:
        raise OSError(f"Unsupported operating system: {system}")

    return base_path / "Fusion/Templates/Edit/Effects"


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
        target_dir = get_fusion_templates_path()
    except OSError as e:
        print(f"Error: {e}")
        return 1

    # Get script directory and project root
    script_dir = Path(__file__).parent
    project_root = script_dir.parent
    source_file_path = project_root / "Advanced Dirt Removal.setting"

    print(f"Advanced Dirt Removal Installer")
    print(f"=================================")
    print(f"Target directory: {target_dir}")
    print(f"Source file: {source_file_path}")
    print()

    if args.dry_run:
        print("DRY RUN - No files will be copied or removed.")
        print()

    # 1. Cleanup (Optional, can be removed if not needed for templates)
    # cleanup_old_macros(target_dir, args.dry_run)

    # 2. Installation
    print("Installing new setting...")
    print("-------------------------")

    # Check if source file exists
    if not source_file_path.exists():
        print(f"Error: Source file not found at {source_file_path}")
        return 1

    # Create target directory if it doesn't exist
    if not args.dry_run:
        target_dir.mkdir(parents=True, exist_ok=True)

    # Install file
    installed_count = 0
    filename = "Advanced Dirt Removal.setting"
    description = "Advanced Dirt Removal - Professional macro with enhanced UI"
    target_file = target_dir / filename

    if args.dry_run:
        print(f"Would install: {description}")
        print(f"  From: {source_file_path}")
        print(f"  To:   {target_file}")
    else:
        try:
            shutil.copy2(source_file_path, target_file)
            print(f"✓ Installed: {description}")
            installed_count = 1
        except Exception as e:
            print(f"✗ Failed to install {description}: {e}")
    print()

    if args.dry_run:
        print(f"Dry run complete. Would install 1 file.")
    else:
        if installed_count > 0:
            print(f"Installation complete! Installed {installed_count}/1 file.")
            print()
            print("Next steps:")
            print("1. Restart DaVinci Resolve to see the new effect.")
            print("2. Find the effect in the Effects panel under Templates > Edit > Effects.")
        else:
            print("Installation failed.")
    
    return 0

if __name__ == "__main__":
    exit(main())