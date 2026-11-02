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
| Resolve | pass: 21.1 Fusion page, every feature | not run: the Windows test host with Resolve scripting was held by another job all night; the free Windows hosts run a facility Resolve session | not run (same) | not run: Resolve Studio 21.0.4 does not start headless on the test machine (needs a desktop session) |
| Nuke | pass: 17.1, every feature and control | not run: no Nuke licence on the free Windows host | not run | not run: the Linux machine with Nuke was in use |

Fixed during this check (all local commits): the CPU fallback on Windows
(kernel list broken by CRLF line ends; fibers resumed on another thread
crashed after a seek), the Windows installer's removal of 1.x, the Linux
build on glibc before 2.34 (threads and libdl), the Linux uninstall leaving
empty folders, and a portable Linux build script.
