# Release notes

## Unreleased fix

- Reduced memory use during GPU rendering in Resolve on macOS, Windows and Linux.
- Fixed memory not being freed after CPU processing on Windows.
- Auto recovery no longer keeps growing memory over a shot, which could fill
  swap on long clips ([#9](https://github.com/fabiocolor/Advanced-Dirt-Removal/issues/9)).
- GPU memory is freed when a shot or its settings change, instead of being kept
  for later.
- Cleaning math is unchanged.

### Viewer-paint development candidate

- A collapsed **Cleaning defaults** section lets Resolve users save, explicitly
  load, or use factory cleaning settings for the selected method. User defaults
  are separate for Automatic/Recovery and Spanish; existing nodes and projects
  are never automatically replaced by these files.
- Automatic/Recovery and Spanish remember separate cleaning configurations in
  the project. Spanish starts with the operator-selected stronger settings;
  selecting either method restores its own edits. Existing projects retain
  their loaded settings, with no additional visible controls.
- Spanish painting skips cleaning on frames without active strokes and reuses
  the cleaned picture across brush edits. Painted frames still use full-frame
  temporal analysis; brush-area-only computation is not implemented.
- Spanish cleans each painted frame from the frames around it (8 before, 3
  after) instead of from the start of the shot, so nodes before it are no
  longer rendered from the shot start for every stroke.
- Once a painted Spanish frame is cleaned, stroke edits and playback need only
  that frame, so they no longer re-render the nodes before it.
- **Original input** replaces the prepared-folder route for a processed export:
  connect the aligned original to the effect's second RGB input and paint. No
  folder or preparation script is needed.
- Calculate recovery adds status/timing, optional host progress, cancellation,
  and reuse of valid saved mattes. Whole-shot dependencies are declared to the
  host, and the button no longer waits for the render lock.
- Rejected thumbnail/proxy requests preserve full-resolution cleaning caches.
- These changes are not in the published installers. Contract checks and native
  Resolve interaction checks have separate qualification scopes.

## 2.0.0

Advanced Dirt Removal 2.0 is a stand-alone OFX plugin with its own temporal
repair engine. Version 1.x was a Fusion macro for DaVinci Resolve that wrapped
Resolve's built-in dirt removal; 2.0 replaces it and works in DaVinci Resolve,
Nuke and other OFX hosts on macOS, Windows and Linux.

### New

- **Own repair engine.** Finds dirt, dust and sparkle that appear in one frame
  only and repairs them from the neighbouring frames, with motion compensation.
  Runs on Apple Metal, NVIDIA CUDA and OpenCL (AMD, Intel, NVIDIA), and on the
  CPU when no GPU is available. Every backend gives byte-identical results.
- **Input encoding.** Video, or Cineon log for film scans: the effect cleans in
  linear and puts only its change back in log, so untouched pixels keep their
  exact values.
- **Auto recovery.** A built-in model gives back repaired spots that were real
  picture detail. **Calculate recovery** works through the whole shot and saves
  the matte to disk; it is recalculated automatically when a cleaning control
  or the input encoding changes, and **Confidence** adjusts it without
  recalculating.
- **Recovery mask.** Paint (Mask Paint, Magic Mask, roto, any alpha) where the
  original should be kept. With auto recovery on, white strokes give the
  original back and black strokes keep the repair.
- **Spanish dirt removal.** Repair only where you paint.
- **Views.** Restored, Red difference (every repair in red on a grey picture),
  True difference and the auto-recovery matte.
- **Installers** for macOS (`.pkg`), Windows (setup program) and Linux
  (tarball with `install.sh`).

### Changed from 1.x

- One plugin instead of the Linear and Cineon macros. The 1.x optical-flow
  motion mask is replaced by **Motion guard** and auto recovery.
- The Windows installer is now machine-wide (the OFX folder needs administrator
  rights) and removes a 1.x installation, including its macros. On macOS and
  Linux a 1.x macro is left in place.

### Requirements

- macOS 12 or later (Apple silicon or Intel), Windows 10/11 x64, or Linux
  x86-64 with glibc 2.27 or newer (Rocky/Alma Linux 8 and later, Ubuntu 20.04
  and later).
- A GPU with Metal, CUDA (NVIDIA, compute capability 7.5 or newer) or OpenCL
  1.2; otherwise the CPU fallback is used, much more slowly.

### Tested

See [TESTED.md](TESTED.md) for the full list. In short: DaVinci Resolve Studio
21 (Fusion page) and Nuke 17 on macOS, Windows and Linux, with output identical
to the macOS reference on every system and GPU tested. The Resolve Color and
Edit pages, other OFX hosts and Intel Macs have not been tested yet; Premiere
Pro and After Effects are not supported yet.
