# Release notes

## 2.1.0

Paint directly in the viewer in DaVinci Resolve, and much lower memory use on
long shots.

### New

- **Paint in the viewer (DaVinci Resolve).** Paint recovery or Spanish dirt
  removal directly in the effect's viewer, on the Fusion, Color and Edit
  pages. No Mask Paint node is needed. Paint, Erase and Select, with round,
  square and rectangle brushes. Strokes can be held over several frames,
  moved, resized and deleted, and **Previous stroke** and **Next stroke** jump
  to them. **Undo paint** and **Redo paint** keep their own history, so
  Resolve's undo stays with the timeline.
- **Additional source.** Paint a processed export back to the original:
  connect the aligned original to the effect's second input and choose
  **Additional source**. Nothing is cleaned; only the current frame is read.
- **Separate settings for Recovery and Spanish dirt removal.** Each method
  keeps its own cleaning settings in the project. Spanish starts with stronger
  settings for manual dust cleaning.
- **Cleaning defaults.** Save your own settings for each method and load them
  in other nodes and projects.
- **Calculate recovery** shows progress and elapsed time, can be cancelled,
  and reuses mattes it has already saved. On Windows a small panel shows the
  progress.
- **New auto-recovery model.** It was fine-tuned on one film; results on other
  material may differ. The weights are still under CC BY-NC 4.0.

### Fixed

- Memory no longer fills up on long shots. Ordinary playback and renders no
  longer keep extra frames in memory, and auto recovery no longer keeps
  growing memory over a shot
  ([#9](https://github.com/fabiocolor/Advanced-Dirt-Removal/issues/9)).
  In my test, a 330-frame 2K shot peaked at 9.4 GB instead of 16 GB, with no
  swap.
- Lower memory use during GPU rendering in Resolve, and GPU memory is freed
  when a shot or its settings change.
- Memory is freed after CPU processing on Windows.
- Spanish dirt removal is much faster while painting: frames without strokes
  are not cleaned, and a painted frame is cleaned from the frames around it
  instead of from the start of the shot.
- A crash in some hosts after Calculate recovery.

### Changed

- **Mask** is now called **Method** in Resolve and **Mode** in other hosts.
  Projects that use the Mask input keep working as long as the built-in brush
  isn't used.
- Cleaning results are the same as in 2.0.

## 2.0.0

Advanced Dirt Removal 2.0 is a stand-alone OFX plugin with its own temporal
repair engine. Version 1.x was a Fusion macro for DaVinci Resolve that wrapped
Resolve's built-in dirt removal. 2.0 replaces it, and it works in DaVinci
Resolve, Nuke and other OFX hosts on macOS, Windows and Linux.

### New

- **Own repair engine.** It looks for dirt, dust and sparkle that appear in one
  frame only and fills them from the neighbouring frames, with motion
  compensation. It runs on Apple Metal, NVIDIA CUDA and OpenCL (AMD, Intel,
  NVIDIA), and on the CPU when there is no usable GPU. Every backend gave the
  same output in testing.
- **Input encoding.** Video, or Cineon log for film scans. The effect cleans in
  linear and puts only its change back in log, so untouched pixels keep their
  exact values.
- **Auto recovery.** A small built-in model gives back repaired spots that look
  like real picture detail. **Calculate recovery** goes through the whole shot
  and saves the matte to disk. It is calculated again by itself when a cleaning
  control or the input encoding changes. **Confidence** adjusts it without
  calculating again. It is a help, not a decision: check it on every shot.
- **Recovery mask.** Paint (Mask Paint, Magic Mask, roto, any alpha) where the
  original should stay. With auto recovery on, white strokes give the original
  back and black strokes keep the repair.
- **Spanish dirt removal.** Manual frame-by-frame dust cleaning: paint a small
  mask over a speck and only that spot is repaired.
- **Views.** Restored, Red difference (every repair in red on a grey picture),
  True difference and the auto-recovery matte.
- **Installers** for macOS (`.pkg`), Windows (setup program) and Linux
  (tarball with `install.sh`). They are unsigned; [INSTALL.md](INSTALL.md)
  says how to open them.

### Changed from 1.x

- One plugin instead of the Linear and Cineon macros. The 1.x optical-flow
  motion mask is replaced by **Motion guard** and auto recovery.
- The Windows installer is machine-wide now (the OFX folder needs
  administrator rights) and removes a 1.x installation, including its macros.
  On macOS and Linux a 1.x macro is left in place.

### Licences

The source code is MIT. The auto-recovery model weights are under CC BY-NC 4.0
(non-commercial use only), in `WEIGHTS_LICENSE`. This release uses weights
version 2.0.0, attached as `recovery-model-2.0.0.bin`.
For commercial use of the weights, contact me through
[github.com/fabiocolor](https://github.com/fabiocolor).

### Requirements

- macOS 12 or later (Apple silicon or Intel), Windows 10/11 x64, or Linux
  x86-64 with glibc 2.27 or newer (Rocky/Alma Linux 8 and later, Ubuntu 20.04
  and later).
- A GPU with Metal, CUDA (NVIDIA, compute capability 7.5 or newer) or OpenCL
  1.2. Otherwise it runs on the CPU, much more slowly.

### Tested

The full list is in [TESTED.md](TESTED.md). In short: DaVinci Resolve Studio 21
(Fusion page) and Nuke 17 on macOS, Windows and Linux, with output identical to
the macOS reference on every system and GPU tested. The Resolve Color and Edit
pages, other OFX hosts and Intel Macs haven't been tested yet. Premiere Pro and
After Effects aren't supported yet.
