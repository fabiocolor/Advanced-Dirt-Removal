# Release notes

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
- **Spanish dirt removal.** Frame-by-frame spot repair: paint a small mask over
  a speck and only that spot is repaired. It replaces Resolve's Dust Buster OFX.
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
