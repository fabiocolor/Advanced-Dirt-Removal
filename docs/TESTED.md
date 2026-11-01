# What has been tested

**Tested** means the plugin ran on that combination and its output was compared
with the expected result: byte for byte, or, for the views, recovery and Cineon
modes, pixel for pixel with an independent computation of what they must
produce. **Built** means it compiles for that target but has not been run there.
Anything not listed has not been tried. Please report results from other hosts,
systems and GPUs in [GitHub Issues](https://github.com/fabiocolor/Advanced-Dirt-Removal/issues),
with the diagnostic log.

Version 2.0.0 (development), November 2026. Real-footage tests use a 56-frame
2368×1712 film scan (float EXR).

## macOS: every feature, in DaVinci Resolve and Nuke

Apple M5 Pro, macOS 27, Metal. DaVinci Resolve Studio 21.1 (Fusion page) and
Nuke 17.1, both driven by script with the plugin from this repository.

| Feature | Resolve 21.1, Fusion page | Nuke 17.1 |
| --- | --- | --- |
| Defaults: output identical to the engine | 8 frames, and 2 frames rendered alone after a seek | all 56 frames |
| Every cleaning control away from its default, and the effect bypassed (25 settings) | default and a Sensitivity change | 25/25 settings, all 56 frames identical to the engine's command-line tool with the same values; bypass identical to the source |
| View: Red difference, defaults | exact | exact |
| View: Red difference, all four view options changed | exact | not run |
| View: True difference (gain 10) | exact | exact |
| View: Auto recovery matte | exact | exact |
| Auto recovery: Calculate recovery button | whole shot (56 frames) saved, button returns in 0.3 s, auto recovery switched on | whole shot saved, auto recovery switched on |
| Auto recovery calculated while rendering (no button) | yes | yes |
| Auto recovery result: matte pixels are the source, the rest the repair | exact (confidence 0.8 and 0.95) | exact (0.8) |
| Confidence change does not recalculate | yes | not run |
| Changing a cleaning control recalculates the matte | yes | not run |
| Recovery mask without auto recovery | Mask Paint strokes: exact | alpha-only Rectangle: exact |
| Recovery mask editing the auto matte (white gives back, black keeps the repair) | Mask Paint strokes: exact | not run |
| Spanish dirt removal | Mask Paint strokes: exact | Rectangle: exact |
| Spanish dirt removal with auto recovery on | render error shown | render error shown; renders again once fixed |
| Alpha-only mask | Ellipse: exact | Rectangle: exact |
| Mask made at half resolution | mapped onto the full frame by position: exact | n/a (Nuke uses pixel positions) |
| Input encoding: Cineon log | exact against the independent linear-clean-and-log-delta computation | exact |
| Cineon log with auto recovery | renders, saves its own matte | not run |
| Matte folder empty: user application data folder | yes | not run |
| Auto-recovery maps identical between the two hosts | yes | yes |

On this scan every cleaning control changes the picture except Flash guard
(the scan has no single-frame flashes, so its effect is covered by the synthetic
engine cases only).

Times on this Mac: cleaning about 0.1 s per frame; auto recovery about 1.5 to 3 s
per frame (a 56-frame shot in 90 to 155 s).

## Engine and plugin by system and GPU

| System | GPU / backend | Status |
| --- | --- | --- |
| macOS 27, Apple M5 Pro (arm64) | Metal, OpenCL, CPU fallback | Tested: 58 engine cases on each, and the OFX contract test on each (`tools/check_release.py build`); auto-recovery network on Metal and CPU gives the same maps |
| macOS, x86-64 slice of the universal build | Metal, OpenCL, CPU | Cleaning engine tested under Rosetta 2 on the M5 Pro during development (58 cases on CPU); not re-run since auto recovery was added; not yet on an Intel Mac |
| Windows 11 x64 | CUDA, OpenCL, CPU | Built from this source: not yet. Earlier development builds of the cleaning engine were tested on NVIDIA RTX 3090 and RTX 4500 Ada (CUDA, OpenCL) and AMD Radeon RX 5700 XT and Pro W5500 (OpenCL) |
| Linux x86-64 (Ubuntu 24.04) | CUDA, OpenCL, CPU | Built from this source: not yet. Earlier development builds of the cleaning engine were tested on an NVIDIA RTX 3090 (CUDA, OpenCL) |

## Hosts

| Host | System | Status |
| --- | --- | --- |
| DaVinci Resolve Studio 21.1, Fusion page | macOS, Metal | Tested: every feature (table above) |
| DaVinci Resolve, Color and Edit pages | any | Not tested |
| Nuke 17.1 | macOS, Metal | Tested: every feature (table above) |
| DaVinci Resolve Studio 21, free 20 | Windows | Earlier development builds of the cleaning effect only; 2.0: not yet |
| Nuke 17 | Windows | Earlier development build of the cleaning effect only; 2.0: not yet |
| Resolve, Nuke | Linux | Not tested |
| Other OFX hosts (Fusion Studio, Flame, Baselight and others) | any | Not tested |
| Adobe Premiere Pro, After Effects | any | Not supported yet (they do not load OFX plugins; an adapter is planned) |

## Installers

| Installer | Status |
| --- | --- |
| macOS `.pkg` | Built from this source and inspected (universal plugin, model file, scripts); not yet installed on a clean machine |
| Windows Inno Setup | Script written; not yet built from this source |
| Linux tarball | Script written; not yet built from this source |

## How the checks are run

`python3 tools/check_release.py build` builds the plugin and checks the engine on
each available backend against `tests/engine_hashes.json`: 29 control settings
(every control away from its default, plus combinations) on two synthetic clips,
one with odd dimensions. It then drives the plugin through a minimal OFX host
(`ofx/tests/contract_host.cpp`): exact RGB output, alpha, row padding, seeking,
parameter edits, a missing frame, a time outside the clip, and the render-lock
behaviour hosts depend on.

The host feature tests drive Resolve and Nuke by script and compare every
rendered pixel with NumPy; they need the test footage and are kept with the
maintainer's test material.
