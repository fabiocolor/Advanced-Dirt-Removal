# Goal: Advanced Dirt Removal 2.0, an open-source GPU engine

Working plan and handoff note. It must stay publishable: no third-party product
names, internal hosts, private paths or credentials.

## Goal

Replace the reliance on the host's built-in dirt removal with this project's own
open-source temporal dirt-repair engine. Keep the parts of Advanced Dirt Removal
that are still useful: the recovery workflow, diagnostics, installers and winget.

- One C++ engine. Kernels are written once and compiled for Metal, CUDA, OpenCL
  and the CPU.
- Hosts: OFX (DaVinci Resolve, Nuke and other OFX hosts). An Adobe Premiere Pro /
  After Effects adapter comes later.
- OS: Windows, macOS and Linux. GPU: Apple, AMD and NVIDIA (Intel through OpenCL).
- Fully open source under the MIT licence, buildable with CMake by anyone.
- Every backend produces byte-identical output, checked locally with
  `tools/check_release.py build`.

## Rules for this work

- Nothing is pushed to GitHub until the sanitizing is finished and the owner
  approves. Commit locally on `main` only.
- No GitHub Actions. Checks run locally (`tools/check_release.py`).
- The engine arrives as one fresh import commit, without private history.
- Out of scope for now: the paint-recovery OFX effect and its Resolve script.
- Footage, PDFs, screenshots of other software and the `workshop/` folder stay
  out of Git. `tools/check_release.py scan --history` runs before any push, using
  a local denylist (`.sanitize-denylist`, never committed).

## Where things stand (2026-11-01)

"Verified" means it ran on that combination and the output was compared
byte for byte. "Built" means it compiles there but has not run in a host.

| Area | Verified with this repository's build | Verified earlier (before the rename) | Built only / missing |
| --- | --- | --- | --- |
| macOS, Apple M5 Pro | engine: Metal, OpenCL, CPU; OFX contract host: Metal, OpenCL, CPU; Resolve Studio 21.1 and Nuke 17.1 (Metal) | — | Intel Macs; M1 to M4 (the x86-64 slice ran under Rosetta 2 only) |
| Windows | — | CUDA (RTX 3090, RTX 4500 Ada) and OpenCL (RX 5700 XT, W5500): engine, Resolve, Nuke | renamed build not yet compiled on Windows; installer for the OFX bundle |
| Linux | — | engine and OFX contract host, RTX 3090 (CUDA, OpenCL) | renamed build; any Linux host application; packaged bundle |
| CPU fallback | macOS arm64; macOS x86-64 slice under Rosetta 2 (58/58) | — | Windows (fibers) and Linux x86-64 runs |
| Resolve (OFX) | Studio 21.1, macOS: effect and both macros | Studio 21 (Windows), free 20 (Windows AMD) | Windows and Linux Resolve with this build |
| Nuke (OFX) | 17.1 macOS, Metal (see below) | 17.x macOS, Windows | Linux Nuke |
| Other OFX hosts | — | one other OFX host on Windows | Fusion Studio, Flame, Baselight, other finishing hosts |
| Premiere / After Effects | — | — | no adapter yet (they do not load OFX plugins) |

### Proof that renaming changed nothing (this Mac, 2026-11-01)

The renamed engine and plugin were compared with the private engine they were
imported from, across all 29 control settings that were validated before
(every control away from its default, plus combinations):

- Engine, synthetic clips (640×360 and odd 638×359): 58/58 byte-identical on
  Metal, OpenCL and CPU.
- Engine, two real film scans (56 frames at 2368×1712, 78 frames at 2048×1556):
  every frame of every setting compared with the hashes recorded when those
  settings were validated: 58/58 clip-and-setting runs identical on Metal and on
  OpenCL (7,772 frames per backend).
- OFX plugin through the contract host, using the new parameter names: 29/29
  byte-identical on Metal, OpenCL and CPU. A deliberately wrong setting fails.
- Nuke 17.1 headless with only the renamed bundle on the plugin path: four
  settings on the 56-frame scan, 56/56 frames identical each (Metal).
- Resolve Studio 21.1 headless, renamed bundle only: bypass identical to the
  source; three settings on the 56-frame scan, 56/56 identical each.
- Universal build: the x86-64 slice under Rosetta 2 gives the same bytes on
  Metal, OpenCL and CPU (58/58 on CPU).

The receipts are kept privately with the source engine.

## What happened to the earlier uncommitted import

An earlier session copied the engine into `engine/` and `ofx/`, added `LICENSE`
and a top-level `CMakeLists.txt`, and began a CPU fallback. It was reviewed file
by file before keeping it:

- Kept: the mechanical rename (identifiers, controls, environment variables);
  removal of the paint-recovery effect and the legacy worker; the CPU backend
  (it compiles the same kernel source as C++, with fibers for kernels that use
  barriers, and it reproduces the GPU bytes exactly on this Mac); the new control
  labels and tooltips; `LICENSE` (MIT); the vendored OpenFX headers (unchanged).
- Kept, with a note: the progressive "Both"-field handling now applies to any host
  (before, two named hosts). It still rejects real fields and only widens a
  window that is one pixel short of the full frame.
- Changed: comments that described another implementation's functions or
  idioms were rewritten; a leftover log namespace was renamed; the benchmark's
  analysis control is now called `analyse` like the plugin's; the contract host
  gained `ADR_TEST_CONTROLS` so any setting can be tested through the plugin.
- Not kept: a stray copy of an old packaging script (`tools_package_ofx.py.tmp`,
  ignored by Git and left in place, unused).
- Changed from the earlier plugin: the UI order of the controls was regrouped
  (detection, then repair, then guards). Values are read by name, so output is
  unaffected. Owner to confirm.

## Open decisions for the owner

1. Public history: two already-published 1.x commits name another product in a
   feature comparison. Leave them, or rewrite history and force-push.
2. Control order in the effect: regrouped (detection, repair, guards). Keep or
   restore the earlier order.
3. Cineon macro: the effect runs on linear values with Extended-range input on.
   The alternative is to run it on the log values directly (the engine's real
   film-scan checks were on log scans) and only build the motion mask in linear.
4. Macro View Mode defaults to Difference View (unchanged from 1.x); switch the
   default to the picture?

## Plan and milestones (week of 2026-11-02)

1. **M0 – Housekeeping. Done 2026-11-01.**
2. **M1 – Names. Done.** Effect "Advanced Dirt Removal", OFX id
   `com.fabiocolor.advanceddirtremoval`, group FabioColor, library `adr_engine`,
   header `adr/adr.h`, prefix `adr_`. Controls: Analyse (Luma/RGB), Remove dark
   spots, Remove light spots, Sensitivity, Extra sensitivity, Repair amount,
   Analysis detail, Limit spot size, Largest spot (px), Blend repairs, Blend
   amount, Motion guard, Shot changes (Off/Guard/Skip), Shot-change guard,
   Flash guard, Extended-range input.
3. **M2 – Clean import. Done** (local commit, not pushed). Engine, OFX adapter,
   CPU backend, tests, local checks.
4. **M3 – Byte-for-byte proof on this Mac. Done** (see above).
5. **M4 – CPU fallback. Working on macOS** (arm64, and x86-64 under Rosetta);
   needs Windows and Linux runs.
6. **M5 – Packaging. Written.** macOS `.pkg` built and inspected (not installed);
   Windows Inno Setup and Linux tarball scripts written, not yet built; winget
   manifest notes in docs/MAINTAINERS_RELEASE.md (scope changes to machine).
7. **M6 – Macros on the new engine. Done on macOS.** Same mask wiring; the
   effect's controls are published. Linear macro default = effect alone, byte for
   byte; Motion Mask renders correctly; Cineon renders (quality review pending).
8. **M7 – Docs. Done** (README, INSTALL, BUILD, TESTED, TECH_SPEC, maintainer notes).
9. **M6b – Bring the workshop views into the 2.0 macros.** A local workshop
   macro (kept out of Git) has extra diagnostic views that the public macros
   never had: a red difference view (with intensity and highlight-opacity
   controls), a compare wipe, true difference, changed pixels, repair
   eligibility and motion protection, plus area and frame-range limits. It still
   uses the host's built-in dirt removal. Port these views onto the macros that
   run the new effect, and test them in Resolve.
10. **M8 – Other machines.** Windows CUDA + AMD OpenCL and a Linux host; update
   TESTED.
11. **M9 – Premiere / After Effects.** Adapter design and a first build (the
    Adobe SDK is a separate download; it cannot be redistributed).
12. **M10 – Final scan of the tree, built binaries and history; owner review;
    first push.**
