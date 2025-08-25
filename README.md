# Advanced Dirt Removal for DaVinci Resolve

This project provides a powerful "Advanced Dirt Removal" Fusion composition for DaVinci Resolve, designed to help with video restoration tasks. It also includes a variety of developer resources for creating custom plugins and scripts for Resolve.

## Installation

The primary tool can be installed by running one of the provided scripts.

1.  **Download the `Advanced Dirt Removal.setting` file** from the root of this repository.
2.  **Run the installer:**
    *   **macOS:** Double-click `install_one_click_mac.command`.
    *   **Windows:** Double-click `install_one_click_win.bat`.
    *   **Linux:** Run `install_one_click_linux.sh` in your terminal.
    *   **Manual (Python):** If you have Python installed, you can run `python scripts/install_advanced_dirt_removal.py` from the terminal.

The script will place the `.setting` file in the correct DaVinci Resolve directory for your operating system.

## Usage

Once installed, the "Advanced Dirt Removal" effect is available in DaVinci Resolve:

1.  Open DaVinci Resolve and go to the **Edit** or **Cut** page.
2.  Open the **Effects** library.
3.  Under **Toolbox > Effects > Templates**, you will find "Advanced Dirt Removal".
4.  Drag the effect onto a clip in your timeline.
5.  Use the **Inspector** to adjust the effect's parameters.

**Note on advanced features:** For full functionality, such as using a custom paint mask for the **Recovery Brush**, you must use this effect directly on the **Fusion page**. The standard Edit/Color pages do not support the "Mask Paint" input required for these features. You can also use a **Magic Mask** in conjunction with, or as an alternative to, the manual paint mask for more automated feature recovery.

## Technical Specifications

For more detailed information on the tool's inner workings, see the [Technical Specifications](docs/TECH_SPEC.md).

## For Developers

This repository also includes a `Developer` folder (ignored by git) containing a wealth of resources for building your own DaVinci Resolve tools, including:

*   DaVinci CTL (Color Transformation Language) examples.
*   Fusion Fuse (Lua-based plugins) examples and SDK.
*   OpenFX (C++ video effects) plugin examples.
*   Python and Lua scripting examples.