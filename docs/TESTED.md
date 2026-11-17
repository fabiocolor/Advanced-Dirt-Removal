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

## Ringo · 9 Nov 2026 · run 22 epoch 1 — development build, 11 November 2026

The development bundle contains `ringo-recovery-20261109-r22e1-63f18ae7`
(**Ghost Story only**, subject-only; not promoted for other films). Ringo
run 15 epoch 4 remains the promoted general model. Checkpoint SHA-256
`63f18ae72507d0d8305a48fb0b0f06fd87371c6fcc2d3af5cbc99202d8183206`.
The existing ResNet-18/13-channel converter produces the ADRRM001 weights;
`recovery-model.json` records both hashes. Eight seeded synthetic crops give
bitwise-equal native CPU/Metal probabilities on Apple M5 Pro. Relative to the
original float32 PyTorch checkpoint, the folded/float16 export's largest
probability difference on those crops is 0.000898719; this is conversion
qualification, not a new operator-quality finding.

Saved probability maps are now keyed by the weights SHA-256 in addition to the
existing shot identity. A contract regression confirms a different checkpoint
cannot read the earlier maps and that the earlier maps remain intact. CPU and
Metal OFX contracts (21-frame fixture, including existing preservation, boundary,
manual/automatic recovery, user-defaults and progress checks) and all three
CTests pass. Existing released installers remain unchanged. Windows/Linux and
new model picture-quality claims require separate evidence.

## Explicit user defaults — development build, 10 November 2026

A collapsed Cleaning defaults section provides Save as my default, Load my
default and Use factory settings for the selected Resolve method. Automatic/
Recovery and Spanish use separate local preference files. New nodes, mode
selection and project loading never automatically apply those files. Loaded
values remain stored in the project, independently of the preference files.

The macOS CPU/Metal contract checks cover both mode files, replacement without
altering the other mode, invalid values, write failure, missing/wrong-mode files,
Unicode paths, explicit loading into a new node, independent factory settings,
shared-control preservation and balanced host Undo brackets. Project instance
recreation retains both mode configurations with the preference files absent.
A stronger Spanish render still matches the direct-engine reference while its
user-defaults location is deliberately unusable. The existing regression suite
and three CTests pass; normal and isolated candidate signatures verify strictly.

These are automated contract checks. The candidate has not been installed in
the active Resolve session; native Color/Edit layout, button interactions and
actual host Undo/save/reopen remain pending. Windows/Linux execution, physical
tablet ergonomics and brush-region-only computation are not qualified here.

## Separate cleaning configurations — development build, 9 November 2026

Resolve's Automatic/Recovery and Spanish methods retain independent Detection,
Processing and Safeguards values in one hidden, project-persistent parameter.
Spanish first-use settings are sensitivity 1.0, Extra sensitivity on, analysis
detail 1.0, repair amount 1.5 and Motion guard 0.0; Automatic/Recovery retains the
published defaults. Encoding, bypass, View, brush settings and the saved
Auto recovery preference stay shared.

The macOS CPU and Metal contract hosts check callback reentry, genuine mode
switch batches, independent custom values, exact serialization, simulated
Undo/Redo in different parameter orders, instance recreation and older Spanish
projects without stored configurations. A stronger-default Spanish frame matches
an independent direct-engine reference inside the brush and preserves original
RGB and alpha outside it; its repair genuinely differs from the automatic
reference. Existing default-render, recovery, cancellation and cache checks
continue to pass, along with three CTests.

This candidate is not installed in the active Resolve tablet session. Actual
Resolve Color/Edit switching, host Undo/Redo and save/reopen behavior remain
separate native checks. These results do not qualify Windows/Linux execution,
tablet ergonomics or brush-region-only computation, and do not authorize a
release.

## Spanish scheduling and recovery feedback — development build, 9 November 2026

The macOS OFX contract host passes on CPU and Metal with the 21-frame 512×512
synthetic fixture. Untouched built-in Spanish frames request only the current
source and create no cleaning sequence, including Cineon input and cold caches.
Restored output is an identity; Red difference retains its expected grey
background. A later stroke obtains the genuine repaired picture, and an opacity
edit reuses that picture with exact brush-limited blending. Legacy masks and
prepared-original routes retain their existing output checks.

Whole-shot calculation tests cover a nonblocking queue/cancel button while the
render lock is held, complete declared source dependencies, optional V1/V2 host
progress, cancellation/failure without publishing pixels, and balanced progress
closure. Fully cached mattes need no new cleaner or classifier pass. A partially
cached pass reproduces the missing matte byte for byte against classification
of the direct-engine reference. Rejected preview-scale requests preserve the
full-resolution sequence, motion history and cleaned pixels. Three CTests pass.

These are contract-host checks, not a new Resolve Color/Edit UI qualification.
The revised candidate has not been installed in the active tablet-review
session. Native progress display, cancellation and playback still need testing;
Windows/Linux execution and physical tablet performance are not qualified by
these results. First-time painted frames still use full-frame temporal analysis:
computation restricted to individual brush areas remains unimplemented. No new
real-footage speed claim or publication approval follows from these checks.

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
Physical tablet pressure and native Linux viewer painting remain untested.
Windows follow-up checks are recorded below.
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
| DaVinci Resolve Studio 21.0.4.5, Color and Edit pages | Windows 11, RTX 3090 (OpenCL) | Development viewer-paint, stroke navigation and render/reopen checks below; existing installers do not include them |
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
`far`; renaming it preserves the cache eviction rule.

After normal Studio activation, native checks passed in Resolve 21.0.4.5:
Color's Enhanced Viewer and Edit's Open FX viewer, round/square/rectangle
painting, Recovery and Spanish, selection and property edits, Delete and paint
Undo/Redo. Shift-drag changed size from 80 to 160; Alt-drag changed softness
from 0.50 to 0.75 without adding strokes. Choosing a manual method resumed
Paint after editing was disabled, with outlines off and selection cleared.

Seven Color button jumps and nine Edit button jumps matched exact playhead
readback, including wraparound and two strokes on one frame. Keyboard-only
jumps also passed on both pages. A viewer-scoped Windows adapter prevents
Resolve from taking brush letters as host shortcuts, leaves Inspector text
editors alone, and dispatches queued edits inside OFX callbacks. These checks
used Resolve's default keyboard mapping; its unassigned F24 dispatch key has
not been qualified with custom host bindings. Brush keys require viewer focus.

The saved Color/Edit OFX state was byte-identical before deletion and after
paint Undo. After a full application restart, all 48 RGB-float EXR frames
(24 Color Recovery, 24 Edit Spanish) matched their pre-restart RGB samples
exactly. This used a generated 640×360, 24 fps fixture and an isolated test
identity; it is not a real-footage quality or physical-tablet assessment.
The production plugin, published installers, recovery weights and release
approval remain unchanged.

### macOS follow-up — 9 November 2026

The same source (`222d398`) was rebuilt as an isolated test identity on the
Apple M5 Pro and loaded in Resolve Studio 21.1.1.10. Native Color Enhanced
Viewer and Edit Open FX controls moved the actual timeline playhead to the
selected stroke. Independent scripting readback confirmed frames 3, 12 and 20,
wraparound, and multiple strokes on frame 20. Comma/period navigation passed
on both pages after giving the viewer paint focus.

All 48 RGB-float EXR frames (24 Color Recovery, 24 Edit Spanish) matched their
pre-restart RGB samples exactly after a full Resolve restart: maximum absolute
RGB difference 0. This is the same generated 640×360, 24 fps fixture used for
the Windows navigation checks, not a new real-footage or tablet qualification.

**Superseded by the keyboard-removal build below.** An open Mac UI issue
remained in the automated native-input path: B/E/V and I
can change the active tool/method while the Inspector choice still displays
its previous value. Parameter snapshots confirm the active state, and saving
and reopening the project refreshes the menus. Callback-dispatch and parameter
declaration experiments did not establish a fix and were removed. This prevents
a complete Mac keyboard-workflow sign-off; painting/navigation/render passes
must not be read as publication approval.

### Resolve owns the keyboard — 9 November 2026

The owner chose removal when the plugin shortcuts could not coexist reliably
with Resolve. The current development build removes the Mac event monitor, the
Windows key hook/F24 dispatch, and command-key handling in both the integrated
brush and standalone Recovery Paint. Every OFX key-down, repeat and key-up is
returned to the host. Modifier state is observed only for pointer gestures;
Shift-drag size and Option/Alt-drag softness remain available. The inspector's
shortcut hints and user-guide key tables were updated. Saved parameter IDs,
choices, stroke documents and image processing are unchanged.

Both callback suites verify that former brush keys, Delete, frame arrows,
modifiers and undo keys pass through without changing parameters, paint
history or an active draw/move/adjust gesture. Both sets of three CTests and
the ADR CPU OFX contract pass. In Resolve Studio 21.1.1.10 on Apple M5 Pro,
Color and Edit frame stepping and Shift-D bypass passed with the paint viewer
active for both tools. ADR's Next stroke, Delete stroke and paint Undo/Redo
buttons passed; Undo restored the exact stroke document. The standalone
Larger/Smaller and Softer/Harder buttons passed. Its size-drag button changed
80 to 113 pixels, and Resolve's native Cmd-Z restored 80 from the paint viewer.

All 48 repeated ADR RGB-float EXR frames match the preceding build exactly
(24 Color Recovery, 24 Edit Spanish; generated 640×360, 24 fps fixture).
The prior stale-menu shortcut path has been removed, rather than qualified.
Fresh Windows/Linux execution of this removal build and physical tablet feel
have not been tested. Public installers, production ADR installation and
recovery weights remain unchanged; publication still requires owner approval
and the accepted recovery-training candidate.

### Painting performance — 14 November 2026 (local Mac test build)

Completed per-frame stroke masks now have a bounded cache, and cleaned-frame
caches share immutable pixel storage. Local cache hits validate the current
source, including when an upstream edit has no host change notification; a
mismatch discards cleaning and motion history. Cache keys are isolated by
instance and source generation. Complete render timing now includes source
fetching, masks, blending, validation and output.

On Apple M5 Pro (24 GB), a synthetic 2048×1556 float fixture with six long soft,
pressure-varying strokes per frame and a three-frame working set measured
24.60 ms before and 5.72 ms after (median of 30 warmed OFX Render calls per
build, approximately 4.3× faster). All 33 compared output buffers matched
exactly. Resolve was closed during this benchmark. These measurements exclude
host scheduling and physical pen latency; they do not predict low-end hardware
performance or the cost of a newly changed stroke mask.

The three CTests and CPU/Metal OFX contracts passed. Regression checks include
active/inactive stroke edits, undo mask reuse, cache budgets, source changes
without notifications, prepared-pair integrity and cancellation. An initial three-frame native 2K RGB-float comparison of a two-node Color
project matched, but a later restart comparison did not. A run with the prior
installed binary also differed from the candidate captures. The cause of the
native chain inconsistency remains unresolved; it is not an accepted parity
result. Offline real-footage checks of sequential, skipped and checkpointed
engine output matched exactly. The optimized candidate was retained separately
and the previous installed test binary was restored for the working project.

An explicit whole-shot preparation implementation is retained behind a
development opt-in and hidden in the normal UI. Synthetic Fusion and isolated
single-effect Color comparisons matched; the two-node temporal Color chain
showed inconsistent source images between preparation and later renders.
Source validation detected the mismatch; the host/cache interaction has not
been fully isolated. This route is not qualified
for normal use. The existing prepared-original export workflow remains
available. Live processing retains conservative temporal frame declarations.
Windows/Linux execution, other Macs and physical tablet latency have not been
qualified for these changes. Public installers and release approval are unchanged.

### Reduced Resolve renders — 17 November 2026 (local Mac test build)

The development adapter now handles host-requested reduced-resolution images,
including anisotropic thumbnails and viewer requests. Recovery and Spanish
painting map the saved canonical stroke geometry to that derivative grid.
Connected originals and prepared sources retain their normal finishing views.
Live preview cleaning runs in an independent sequence; images smaller than the
engine minimum are edge-padded and cropped for this derivative only. Native
processing, temporal history, paint dimensions and saved recovery files are not
replaced by preview results. A queued whole-shot calculation remains pending
for a native render. Reduced live cleaning/classification is a preview estimate,
not a claim of equality with a downsampled native result.

The earlier per-user development installation required an explicit OFX search
path. For normal Resolve launches, install the verified test bundle at
`/Library/OFX/Plugins/AdvancedDirtRemovalTest.ofx.bundle`, keeping its test
identity separate from the existing release. This standard-folder installation
requires macOS administrator authentication. The optional
`tools/open_resolve_test.command` is a development diagnostic, not a requirement
for a standard-folder installation. Published installers remain separate.

On Apple M5 Pro / Resolve Studio 21.1.1.10, fresh 92×46 thumbnails and
576×324 viewer requests completed without render errors. The same real-footage
frame exported as RGB-float EXR from Color and Edit matched the pre-change
native samples exactly (maximum absolute difference 0). This is one native
frame, not a whole-shot or operator quality approval.

All 58 deterministic engine fixtures passed on CPU and Metal, including their
shot/flash guards and extended-range cases. Both OFX contracts and the three
CTests passed. Additional contract checks cover tiny and anisotropic Recovery/
Spanish endpoints, rounded proxy dimensions, alpha, independent live-preview
engine output, return to unchanged native rendering, and thumbnail auto
recovery without consuming queued native analysis or writing proxy mattes.
Other systems and physical tablet feel were not tested by this change.
