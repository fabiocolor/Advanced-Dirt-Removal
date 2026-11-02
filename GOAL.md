# Advanced Dirt Removal 2.0: goal and roadmap

## Goal

A free, open-source temporal dirt-repair plugin that anyone can build and use:

- One C++ engine. Kernels are written once and compiled for Metal, CUDA, OpenCL
  and the CPU, with byte-identical output on all of them.
- One OFX plugin with everything built in: input encoding (video or Cineon
  log), recovery mask, auto recovery, Spanish dirt removal and checking views.
- Hosts: DaVinci Resolve (Fusion page now, Color page next), Nuke and other OFX
  hosts. An Adobe Premiere Pro / After Effects adapter comes later.
- Systems: macOS, Windows and Linux. GPUs: Apple, AMD and NVIDIA (Intel through
  OpenCL).
- MIT licence, buildable with CMake, checked locally with
  `tools/check_release.py` (there is no CI service).

## Where things stand

See [docs/TESTED.md](docs/TESTED.md) for exactly what has been tested.

| Milestone | State |
| --- | --- |
| Engine on Metal, OpenCL and CPU (macOS) | done, byte-identical across backends |
| OFX plugin with recovery, auto recovery, Spanish dirt removal and views | done |
| Every feature tested on macOS in Resolve (Fusion page) and Nuke | done |
| macOS installer | built |
| Windows: CUDA and OpenCL build, Resolve and Nuke | next |
| Linux: CUDA and OpenCL build, a host application | next |
| Resolve Color page | planned |
| Premiere Pro / After Effects adapter | planned (the Adobe SDK is a separate download and cannot be redistributed) |
| First public 2.0 release | after the steps above and the owner's review |

## Release checklist (2.0.0, overnight 2026-11-01 to 11-02)

Pass/fail per platform and host; "not run" says why. Machines: macOS =
Apple M5 Pro (Metal), macOS 27; Windows NVIDIA = Windows 11, RTX A5000;
Windows AMD = Windows 10, Radeon RX 580; Linux = Ubuntu 24.04, RTX 3090,
plus an AlmaLinux 8 (glibc 2.28) container for the release build.
Real-footage parity: 12 frames of a 2368×1712 film scan, four settings,
every output frame and the auto-recovery maps compared with macOS.

| Check | macOS | Windows NVIDIA | Windows AMD | Linux NVIDIA |
| --- | --- | --- | --- | --- |
| Build from source | pass | pass (MSVC 2022, CUDA 13) | Windows build reused | pass (Ubuntu GCC 13; release build in AlmaLinux 8 container) |
| Engine hashes, 58 cases | pass: Metal, OpenCL, CPU | pass: CUDA, OpenCL, CPU | pass: OpenCL, CPU | pass: CUDA, OpenCL, CPU (and CPU inside glibc 2.28) |
| OFX contract test | pass: Metal, OpenCL, CPU | pass: CUDA, OpenCL, CPU | pass: OpenCL, CPU | pass: CUDA, OpenCL, CPU |
| Real-footage parity with macOS | reference (Metal = CPU) | pass: CUDA, OpenCL, CPU | pass: OpenCL, CPU | pass: CUDA, OpenCL, CPU |
| Auto-recovery maps equal macOS | reference (Metal = CPU) | pass: CUDA, OpenCL, CPU | pass: OpenCL, CPU | pass: CUDA, OpenCL, CPU |
| Installer builds | pass (.pkg, inspected) | pass (Inno Setup 6.7) | same installer | pass (portable tarball, needs glibc 2.27) |
| Clean install, files identical | not run: needs an administrator password | pass | pass | pass (user install; plugin loads) |
| Upgrade from 1.1.3 removes 1.x | n/a | pass (after a fix) | not run | n/a |
| Uninstall leaves nothing behind | not run | pass | pass | pass (after a fix) |
| Resolve | pass: 21.1 Fusion page, every feature | pass: Resolve Studio 21.0.4 (Fusion page) on Windows 11, RTX 3090, installed from the release installer: output identical to macOS; views, auto recovery, Spanish dirt removal and its error, Cineon checked | not run (same) | not run: Resolve Studio 21.0.4 does not start headless on the test machine (needs a desktop session) |
| Nuke | pass: 17.1, every feature and control | pass: Nuke 17.0 (Indie) on Windows 11, RTX 3090, installed from the release installer: output identical to macOS; views, auto recovery, masks, Spanish dirt removal and its error all checked | not run | not run: the Linux machine with Nuke was in use |

Fixed during this check (all local commits): the CPU fallback on Windows
(kernel list broken by CRLF line ends; fibers resumed on another thread
crashed after a seek), the Windows installer's removal of 1.x, the Linux
build on glibc before 2.34 (threads and libdl), the Linux uninstall leaving
empty folders, and a portable Linux build script.

### Release files (rebuilt from commit 2464e0b, 2026-11-02)

| File | SHA-256 | Rechecked |
| --- | --- | --- |
| `AdvancedDirtRemoval-2.0.0.pkg` (macOS, universal) | `c78659f487c03f6ec30d4e8dad57647349919ed30a6d7f2eb0eb8ecef55fd70e` | release check Metal/OpenCL/CPU; contents inspected; scan clean |
| `AdvancedDirtRemoval-Setup-2.0.0.exe` (Windows x64) | `97a9857990f83d8757c933571a9e8486eb6ed43827d812903adfcf1b6926f888` | release check CUDA/OpenCL/CPU; clean install (files identical), upgrade from 1.1.3, uninstall; `build_installer.ps1` also works |
| `AdvancedDirtRemoval-2.0.0-linux-x86_64.tar.gz` | `c8c1bf0d5d4e90dcdfb20be57fff03336714781d5a7fb47cb3e66f13c93fb09c` | built with `build_portable.sh`; CPU checks in glibc 2.28; CUDA/OpenCL/CPU checks and real-footage parity on Ubuntu 24.04 |

The binaries contain no build-machine paths, and the sanitize scan of the
unpacked release files finds nothing.


## Linux Nuke qualification, 2 November 2026

Nuke Indie 17.1v2 on Ubuntu 24.04.4, RTX 3090 (CUDA): nine host checks
pass on the 12-frame scan. Default RGB frames and recovery maps are identical
to the macOS reference; Red difference, Rectangle recovery mask, Spanish dirt
removal, invalid settings and rendering after fixing them pass.

The first actual Linux Nuke render exposed a C++ runtime symbol collision in
the previous Linux release binary. The Linux OFX linker export map now limits
exports to OfxGetNumberOfPlugins and OfxGetPlugin, keeping C++ template/runtime
symbols local. Processing code and controls are unchanged. The fixed Ubuntu
build passes 58/58 engine cases and the OFX contract on CUDA and OpenCL. The
installed fixed bundle also passes the Nuke checks. Fresh Windows Nuke 17.0v3
and Resolve Studio 21.0.4 checks pass 9/9 each with the existing Windows
release installer; Resolve preferences are restored byte-for-byte.

The earlier Linux tarball listed above is superseded for distribution: it
needs rebuilding with the export map. The newly tested Ubuntu build does not
establish a glibc 2.28 portable-build pass. Linux Resolve is installed but its
2.0 host test also passes 9/9 on the fixed Ubuntu build (see below). Public release remains held; no push.

The current source scan is clean. The full existing branch-history scan still
reports 26 legacy references in earlier macro commits; history/publication
cleanup remains a separate release gate. No history was rewritten or removed.

Linux Resolve Studio 21.0.4.5 now passes 9/9 on the same 12-frame scan
with the fixed system plugin: exact default frames, views, recovery maps and
matte result, invalid-setting error and subsequent rendering, finite Cineon
output. The test used a fresh local library and output under registered Media
Storage. Test projects and output are preserved. This qualifies the scripted
Fusion-page host/render path on Ubuntu 24.04.4 with RTX 3090 CUDA.
