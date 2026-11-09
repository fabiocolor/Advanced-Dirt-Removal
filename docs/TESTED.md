# What has been tested

**Tested** means the plugin ran on that combination and its output was compared
with the expected result: byte for byte, or, for the views, recovery and Cineon
modes, pixel for pixel with an independent computation of what they must
produce. **Built** means it compiles for that target but has not been run there.
Anything not listed has not been tried. Please report results from other hosts,
systems and GPUs in [GitHub Issues](https://github.com/fabiocolor/Advanced-Dirt-Removal/issues),
with the diagnostic log.

Version 2.0.0, November 2026. Real-footage tests use a 2368×1712 film scan
(float EXR): 56 frames on macOS, a 12-frame excerpt on Windows and Linux,
compared frame by frame with the macOS result.

## Integrated Resolve inspector — development build, 8 November 2026

A single independent review from Astra and Fable informed a shared layout for
Advanced Dirt Removal and the standalone recovery adapter. Routine controls
remain visible; stroke details, source setup and advanced controls start collapsed.
The revised workflow restores the published Input encoding, View, Detection,
Processing and Safeguards layout. All automatic options precede the integrated
Manual recovery / Spanish section. The built-in brush owns manual recovery
while enabled or while saved strokes exist; older external-mask projects retain
their compatibility path when that brush is unused. Spanish and prepared-source
routes pause automatic recovery without overwriting its saved preference.
The cleaning engine, saved parameter identities, defaults, choice strings and
stroke format are unchanged. Finishing now gives the built-in brush exclusive
ownership over manual recovery instead of composing a second external mask.
Cleaning controls retain their serialization order;
descriptor presentation order is separate, preserving recovery-cache keys.

Resolve Studio 21.1.1.10 on Apple M5 Pro loaded isolated test identities. The
initial compact layout fit its primary controls in a 1920×1080 window; operator
feedback then restored the published cleaning layout. With Detection expanded,
short inspectors can require scrolling to manual controls. Collapsing Detection
makes room for painting. Color and Edit show the original automatic groups
followed by one manual section with its own stroke, brush and source options.

Before/after float EXR comparisons matched every RGB sample on all 200 frames
from the native Color and Edit prepared-original fixtures: recovery, Spanish,
Red difference and True difference, 25 frames per case. The standalone adapter
also matched 100 frames across its bake-route reference cases. Its
existing Fusion additional-source route rejected missing host frame-rate
metadata in the baseline; that route is outside this UI-only qualification.
Normal build, engine hashes on Metal/OpenCL/CPU, parameter contract and paint
fixture checks passed. Connected-mask regressions verified exclusive brush
ownership, legacy mask-only output, untouched-frame behavior, editing off,
Spanish with a saved Auto recovery preference, and prepared exports with that
preference retained. Manual paint and erase override the automatic matte once;
a partially erased edge blends correctly. Native scripted input is not a physical tablet-feel test.
The compact inspector also passed native stroke selection, Delete and paint
Undo/Redo with exact document restoration. The installed production plugin and
published 2.0 installers remain unchanged.

## Viewer painting — development build, 8 November 2026

On the Apple M5 Pro in Resolve Studio 21.1.1, the prepared-original workflow
captured and verified 25 frames at 1920×1080, 25 fps. Fusion renders of the
processed baseline, rectangle recovery, Spanish dirt removal, Red difference
and True difference matched independent pixel computations on all 25 frames.
The final candidate also matched for round and square dabs, a feathered
rectangle at partial opacity, and an erased region: 225 output frames in total.
Effect reload passed after fixing reentrant parameter-write locking during
instance initialization; a bounded contract regression covers the callbacks.
The prepared route bypasses the cleaning engine; the contract tests verify
current-frame-only input access and that no cleaning sequence is created.

Native scripted input passed on Color and Edit: round and square brushes,
rectangles, selection, move and corner resize, size and softness keys,
Shift-drag size, Option-drag softness, Delete, paint Undo/Redo and frame stepping.
Color's Enhanced Viewer passed these paint-history checks too. Resolve disables
Enhanced Viewer and Full Page Viewer on Edit in this version; Edit checks used
its standard Open FX viewer. Edit's explicit Undo paint/Redo paint buttons and
an erased square dab also passed after reopening the final candidate.

The paint history fixes a Resolve Edit limitation: keyboard-only OFX changes
can share a host undo entry. Viewer Undo/Redo now restores one paint edit,
including the selected stroke's properties, without undoing timeline operations.
History is bounded to 64 states and 64 MiB per instance and resets on reload.
Contract checks cover delayed mouse-up/key ordering, host callback boundaries,
grouped parameter restoration, exact undo/redo and history bounds.

With a processed export on V2 and its aligned original on V1, source offset 0,
all 100 direct Edit outputs (25 frames each in Recovery, Spanish, Red difference
and True difference) matched independent computations exactly. Logs confirmed
prepared-source access for every frame on both pages, bypassing the cleaner.
The complex soft-stroke Color fixture was also compared on all 100 frames:
maximum absolute RGB differences were 1.49e-8 (Recovery), 5.96e-8 (Spanish and
Red difference), and 2.38e-7 (True difference). These comparisons are not an
exact-parity pass. After a full application restart, both stroke documents
matched exactly and all 50 repeated Color/Edit Recovery outputs matched their
own pre-restart output pixel for pixel.

Final local checks passed: 58 engine cases on each of Metal, OpenCL and CPU,
the OFX contract on all three, three CTest checks, five source-preparation tests
and three release-tool tests. Native interaction was driven through the test
application's own event/accessibility interfaces; it does not qualify physical
pen feel or human picture-quality approval.
Physical tablet pressure and the new brush on Windows/Linux remain untested.
Held strokes are static; automatic tracking is not implemented. The existing
installers and their qualification tables below are unchanged.

## Memory fix — 8 November 2026

We tested the update on macOS, Windows and Linux, including Resolve, Nuke,
older NVIDIA cards and 28 file formats. The tested results are unchanged.
Two 100-frame Mac renders stayed below 4 GiB, compared with 13.38 GiB before,
and did not add swap. Hosts without GPU image support keep the previous memory
behaviour. The longer Mac render and Auto recovery tests on the AMD CPU
stopped at their memory or time limits. The Mac Nuke test could not run because of a licence error.
The tables below record the original release tests. Existing installers do
not include this fix.

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
| Windows 11 x64, NVIDIA RTX A5000 | CUDA, OpenCL, CPU fallback | Tested: built with MSVC 2022 and CUDA 13; 58 engine cases and the OFX contract test on each; real-footage output and auto-recovery maps identical to macOS on each |
| Windows 10 x64, AMD Radeon RX 580 | OpenCL, CPU fallback | Tested with the same Windows build: 58 engine cases and the OFX contract test on each; real-footage output and auto-recovery maps identical to macOS |
| Windows 10 x64, NVIDIA Quadro P5000 (Pascal) | OpenCL (automatic fallback) | Tested: CUDA needs compute capability 7.5 or newer, so the engine falls back to OpenCL by itself and matches the reference; forcing CUDA reports that no kernel image fits the device |
| Linux x86-64, Ubuntu 24.04, NVIDIA RTX 3090 | CUDA, OpenCL, CPU fallback | Tested with the release build: 58 engine cases and the OFX contract test on each; real-footage output and auto-recovery maps identical to macOS |
| Linux x86-64, AlmaLinux 8 (glibc 2.28) | CPU fallback | Release build made and tested in a container: 58 engine cases and the OFX contract test; the plugin needs glibc 2.27 or newer |

## Hosts

| Host | System | Status |
| --- | --- | --- |
| DaVinci Resolve Studio 21.1, Fusion page | macOS, Metal | Tested: every feature (table above) |
| DaVinci Resolve, Color and Edit pages | macOS | Development viewer-paint checks above; existing 2.0 installers do not include them |
| Nuke 17.1 | macOS, Metal | Tested: every feature (table above) |
| DaVinci Resolve Studio 21.0.4, Fusion page | Windows 11, RTX 3090 (CUDA) | Tested with the release installer on a 12-frame scan: plugin and controls load, every frame identical to macOS, Red difference, auto recovery (maps identical to macOS), the Spanish + auto recovery error and rendering again after fixing it, Cineon log |
| DaVinci Resolve Studio 21.0.4, Fusion page | Ubuntu 24.04, RTX 3090 (CUDA) | Tested with the release tarball installed in `/usr/OFX/Plugins` on a 12-frame scan: plugin and controls load, every frame identical to macOS, Red difference, auto recovery (maps identical to macOS), the Spanish + auto recovery error and rendering again after fixing it, Cineon log |
| Nuke 17.0 (Indie) | Windows 11, RTX 3090 (CUDA) | Tested with the release installer on a 12-frame scan: every frame identical to macOS; Red difference, auto recovery (maps identical to macOS), Rectangle mask recovery, Spanish dirt removal, the Spanish + auto recovery error, and rendering again after fixing it |
| Nuke 17.1 (Indie) | Ubuntu 24.04, RTX 3090 (CUDA) | Tested with the release tarball installed on a 12-frame scan: every frame identical to macOS; Red difference, auto recovery (maps identical to macOS), Rectangle mask recovery, Spanish dirt removal, the Spanish + auto recovery error, and rendering again after fixing it |
| Other OFX hosts (Fusion Studio, Flame, Baselight and others) | any | Not tested |
| Adobe Premiere Pro, After Effects | any | Not supported yet (they do not load OFX plugins; an adapter is planned) |

The first Linux Nuke render exposed a C++ runtime symbol collision between the
plugin and the host. Linux builds now export only the two OFX entry points; the
release tarball was rebuilt with this fix and passed every check above. Host
results are scripted render checks with output compared by machine; they are
not a review of picture quality on many films.

## Installers

| Installer | Status |
| --- | --- |
| macOS `.pkg` | Installed successfully on the M5 Pro into an empty system OFX target; the existing 1.1.3 package receipt upgraded to 2.0.0. All 7 installed files match the expanded package, and the bundle passes strict signature verification. Resolve 21.1.1.10 loaded this installed package: a synthetic-frame render is byte-identical to the engine and Red Difference matches its definition exactly. This is a clean plugin-target test on the existing Mac; a fresh OS, Intel Mac and interactive uninstall have not been tested. |
| Windows installer (Inno Setup) | Built from this source; silent install puts files identical to the build in the OFX folder; upgrade from 1.1.3 removes the 1.x installation and its macros; uninstall restores the OFX folder exactly (Windows 11 and Windows 10). `installers/win/build_installer.ps1` (Visual Studio 2022 generator) also builds a working installer |
| Linux tarball | Built in a manylinux_2_28 container (`installers/linux/build_portable.sh`); needs glibc 2.27 or newer. Installed in the system OFX folder on Ubuntu 24.04 and used by Resolve and Nuke there; user install and uninstall also tested |

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

## Stroke navigation and painting defaults — 9 November 2026

Resolve Color/Edit rejects the optional OFX timeline seek operation. Stroke
navigation now falls back to Resolve's bundled Lua interpreter and documented
local scripting API. No Python or separately launched utility is required.
Target frames map from the native clip range and trim offsets, independently
of stale parameter callback times. Every navigation request seeks the stroke's
first frame, including held strokes and wraparound.

New Resolve effects default to painting on and Show strokes off. Choosing
Recovery or Spanish resumes Paint and exits selection review. Older connected
mask-only projects preserve their mask route until the built-in brush is chosen.
The source contract tests cover these defaults, host seek rejection/fallback,
mode activation, loading explicitly disabled painting and mask compatibility.
Lua checks cover NDF/DF timecodes, rapid requests, trims and unsupported ranges.
Native Color/Edit qualification is recorded separately with exact playhead
readback; selecting a stroke alone is not proof of a successful jump.

Windows 11 on the RTX 3090 test workstation builds with MSVC 2022. The three
paint/source/GPU-transport CTests pass, as do the CPU and OpenCL OFX contracts
and all 77 Lua navigation mapping checks under Resolve's Windows `fuscript`.
The build exposed a Windows header collision with a cache iterator named
`far`; renaming it preserves the cache eviction rule. Native Windows viewer
and playhead checks were blocked by Studio activation and remain unqualified.
