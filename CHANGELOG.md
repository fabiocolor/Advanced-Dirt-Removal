# Changelog

Longer notes for each release are in [docs/RELEASE_NOTES.md](docs/RELEASE_NOTES.md).

## 2.1.0

Added
- Painting directly in the viewer in DaVinci Resolve (Fusion, Color and Edit
  pages): Paint, Erase and Select, round, square and rectangle brushes, held
  strokes, Previous/Next stroke, and Undo paint/Redo paint.
- **Additional source** input, to paint a clip cleaned elsewhere back to its
  original.
- Separate cleaning settings for Recovery and Spanish dirt removal, and
  **Cleaning defaults** to save and load your own.
- Progress, cancel and reuse of saved mattes for **Calculate recovery**.
- A new auto-recovery model.

Fixed
- Memory filling up on long shots (#9).
- Lower GPU memory use in Resolve; memory freed after CPU processing on
  Windows.
- Faster Spanish dirt removal while painting.

Changed
- **Mask** is called **Method** in Resolve and **Mode** in other hosts.

## 2.0.0

A new plugin. 1.x was a Fusion macro around DaVinci Resolve's built-in dirt
removal. 2.0 is a stand-alone OFX plugin with its own temporal repair engine,
for Resolve, Nuke and other OFX hosts on macOS, Windows and Linux.

Added
- Own repair engine on Apple Metal, NVIDIA CUDA, OpenCL and the CPU, with the
  same output on all of them.
- Input encoding: Video or Cineon log.
- Auto recovery, with a **Calculate recovery** button and a **Confidence**
  control.
- Recovery mask through the Mask input, and Spanish dirt removal: manual
  frame-by-frame dust cleaning, only inside a small painted mask.
- Views: Restored, Red difference, True difference, Auto recovery matte.
- Installers for macOS (`.pkg`), Windows (setup program) and Linux (tarball
  with `install.sh`).
- A diagnostic log for issue reports.

Changed
- One plugin replaces the Linear and Cineon macros. The 1.x optical-flow motion
  mask is replaced by **Motion guard** and auto recovery.
- The Windows installer is machine-wide now (the OFX folder needs
  administrator rights) and removes a 1.x installation. On macOS and Linux a
  1.x macro is left in place.

Licences
- Source code stays MIT.
- The auto-recovery model weights are under CC BY-NC 4.0
  ([WEIGHTS_LICENSE](WEIGHTS_LICENSE)), versioned per release, and attached to
  each release as a separate file.

## 1.1.3 (1 July 2026)

- Restored the validated dirt removal macro release.
- winget install and upgrade instructions for Windows.

## 1.1.2 (10 February 2026)

- Simpler silent install, so automated installs don't uninstall by accident.

## 1.1.1 (10 February 2026)

- Silent install and uninstall made more reliable for automation.

## 1.1.0 (10 February 2026)

- macOS and Windows installers, shipped in ZIP files, replacing the old
  scripts.
- Version numbers aligned for winget.

## 1.2 (23 November 2025)

- Two macros: `Advanced Dirt Removal.setting` for video and
  `Advanced Dirt Removal Cineon.setting` for Cineon film scans, with the
  Cineon conversion built in.
- A clearer **Motion Mask Safety** control, and separate defaults for each
  macro.

The 1.2 tag predates the 1.1.x tags; the 1.1.x numbers were chosen later for
winget.
