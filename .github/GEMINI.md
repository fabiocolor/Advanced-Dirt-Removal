# Project Overview

This project provides the "Advanced Dirt Removal" Fusion composition for DaVinci Resolve, a tool for video restoration. The repository is structured to be easy to use, with simple installation scripts and consolidated documentation.

## Key Files

*   `Advanced Dirt Removal.setting`: The core Fusion composition file. This is the file that needs to be installed.
*   `README.md`: The primary documentation for the project, containing installation and usage instructions.
*   `scripts/`: This directory contains the installation scripts.
    *   `install_advanced_dirt_removal.py`: A Python script to install the `.setting` file.
    *   `install_one_click_mac.command`, `install_one_click_win.bat`, `install_one_click_linux.sh`: One-click installers for different operating systems.
*   `docs/TECH_SPEC.md`: Detailed technical information about the tool.

## Installation

The installation process is handled by scripts that copy the `Advanced Dirt Removal.setting` file to the correct DaVinci Resolve directory.

*   The user can run the one-click installers for their OS.
*   Alternatively, they can run the Python script `scripts/install_advanced_dirt_removal.py`.

The script places the file in `Fusion/Templates/Edit/Effects` inside the Resolve support directory for the user's OS.

## For Developers

A `Developer` directory exists locally (and is ignored by git) which contains resources for building custom DaVinci Resolve tools, including:

*   DaVinci CTL (Color Transformation Language) examples.
*   Fusion Fuse (Lua-based plugins) examples and SDK.
*   OpenFX (C++ video effects) plugin examples.
*   Python and Lua scripting examples.