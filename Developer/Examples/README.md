# DaVinci Resolve Fusion Development Examples

This directory contains a collection of standalone examples of Fuses and Macros for DaVinci Resolve Fusion. These examples are intended for educational purposes, to be studied and analyzed by participants in a workshop.

The goal of providing these examples is to demonstrate advanced techniques and best practices for developing custom tools for Fusion, without requiring the installation of the Reactor package manager.

## Contents

*   **`fusion-fuse-examples/`**: A collection of simple, standalone `.fuse` files. These are excellent for learning the basic structure of a Fuse, how to create custom UI controls, and how to implement image processing algorithms in Lua.
*   **`stx_tools/`**: A collection of advanced, production-ready macros from statixVFX. This includes powerful tools like `PatchPal` (for motion vector-based patching) and `stx_metaballs`. These are fantastic examples of complex node graphs, expressions, and well-designed user interfaces in a macro.

## How to Use These Examples

*   **Fuses (`.fuse` files):** These files can be installed by placing them in the `Fuses:` directory in your DaVinci Resolve or Fusion user preferences. You can find this directory by opening Fusion, going to `Fusion > Fusion Settings > Path Map`, and then right-clicking on `Fuses:` and selecting "Show Folder". After restarting DaVinci Resolve, the Fuses will be available in the "Tools" menu.
*   **Macros (`.setting` files):** These files can be installed by placing them in the `Macros:` directory in your DaVinci Resolve or Fusion user preferences. You can find this directory in the same "Path Map" as the Fuses directory. After restarting DaVinci Resolve, the Macros will be available in the "Tools > Macros" menu.

By studying the code in these examples, you can learn how to build your own custom tools for DaVinci Resolve Fusion.