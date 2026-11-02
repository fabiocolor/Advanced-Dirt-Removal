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
- Code under the MIT licence; the auto-recovery model weights under CC BY-NC
  4.0 (`WEIGHTS_LICENSE`), versioned per release. Buildable with CMake, checked
  locally with `tools/check_release.py` (there is no CI service).

## Where things stand

See [docs/TESTED.md](docs/TESTED.md) for exactly what has been tested and
[docs/RELEASE_NOTES.md](docs/RELEASE_NOTES.md) for the 2.0.0 notes.
The final candidate and remaining publication actions are recorded in
[docs/RELEASE_READINESS.md](docs/RELEASE_READINESS.md).

| Milestone | State |
| --- | --- |
| Engine on Metal, CUDA, OpenCL and CPU | done, byte-identical across backends and systems |
| OFX plugin with recovery, auto recovery, Spanish dirt removal and views | done |
| macOS: every feature in Resolve (Fusion page) and Nuke | done |
| Windows: CUDA and OpenCL (NVIDIA and AMD), Resolve and Nuke | done |
| Linux: CUDA and OpenCL, Resolve and Nuke | done |
| Installers for macOS, Windows and Linux | built and tested; macOS empty-target install and installed Resolve renders now pass |
| Resolve Color page | planned |
| Premiere Pro / After Effects adapter | planned (the Adobe SDK is a separate download and cannot be redistributed) |
| First public 2.0 release | waiting for the owner's review |

## Release checklist (2.0.0)

Pass/fail per platform; "not run" says why. Machines: macOS = Apple M5 Pro
(Metal), macOS 27; Windows NVIDIA = Windows 11, RTX A5000 and RTX 3090;
Windows AMD = Windows 10, Radeon RX 580; Linux = Ubuntu 24.04, RTX 3090, plus
an AlmaLinux 8 (glibc 2.28) container for the release build. Real-footage
parity: 12 frames of a 2368×1712 film scan, four settings, every output frame
and the auto-recovery maps compared with macOS.

| Check | macOS | Windows NVIDIA | Windows AMD | Linux NVIDIA |
| --- | --- | --- | --- | --- |
| Build from source | pass | pass (MSVC 2022, CUDA 13) | Windows build reused | pass (release build in AlmaLinux 8 container) |
| Engine hashes, 58 cases | pass: Metal, OpenCL, CPU | pass: CUDA, OpenCL, CPU | pass: OpenCL, CPU | pass: CUDA, OpenCL; CPU in glibc 2.28 |
| OFX contract test | pass: Metal, OpenCL, CPU | pass: CUDA, OpenCL, CPU | pass: OpenCL, CPU | pass: CUDA, OpenCL, CPU |
| Real-footage parity with macOS | reference (Metal = CPU) | pass: CUDA, OpenCL, CPU | pass: OpenCL, CPU | pass: CUDA, OpenCL |
| Auto-recovery maps equal macOS | reference (Metal = CPU) | pass: CUDA, OpenCL, CPU | pass: OpenCL, CPU | pass: CUDA, OpenCL |
| Installer builds | pass (.pkg, inspected) | pass (Inno Setup 6.7; `build_installer.ps1` too) | same installer | pass (`build_portable.sh`) |
| Clean install, files identical | pass: empty system OFX target on existing Mac; 7/7 files identical; old 1.1.3 receipt upgraded | pass | pass | pass (system OFX folder; plugin loads) |
| Upgrade from 1.1.3 removes 1.x | n/a | pass | not run | n/a |
| Uninstall leaves nothing behind | not run | pass | pass | pass |
| Resolve Studio 21 (Fusion page) | pass: 21.1, every feature | pass: 21.0.4, 9/9 checks | not run | pass: 21.0.4, 9/9 checks |
| Nuke 17 | pass: 17.1, every feature and control | pass: 17.0 Indie, 9/9 checks | not run | pass: 17.1 Indie, 9/9 checks |

The 9 host checks on Windows and Linux: plugin and controls load; all 12
frames identical to macOS; Red difference; auto-recovery maps identical to
macOS and the auto-recovery result; mask recovery and Spanish dirt removal
(Nuke); the Spanish + auto recovery error and rendering again once fixed;
Cineon log (Resolve). On Windows and Linux they used the installed release
files.

Older NVIDIA GPUs: on a Quadro P5000 (Pascal) the engine falls back from CUDA
to OpenCL automatically and matches the reference; CUDA itself needs compute
capability 7.5 (Turing) or newer.

Fixed during the release checks: the CPU fallback on Windows (kernel list
broken by CRLF line ends; fibers resumed on another thread crashed after a
seek); the Windows installer's removal of 1.x; the Linux build on glibc before
2.34 (threads and libdl); the Linux uninstall leaving empty folders; and a C++
runtime symbol collision in Linux hosts, fixed by exporting only the two OFX
entry points.

### Release files

| File | Built from | SHA-256 |
| --- | --- | --- |
| `AdvancedDirtRemoval-2.0.0.pkg` (macOS, universal) | `2464e0b` | `c78659f487c03f6ec30d4e8dad57647349919ed30a6d7f2eb0eb8ecef55fd70e` |
| `AdvancedDirtRemoval-Setup-2.0.0.exe` (Windows x64) | `2464e0b` | `97a9857990f83d8757c933571a9e8486eb6ed43827d812903adfcf1b6926f888` |
| `AdvancedDirtRemoval-2.0.0-linux-x86_64.tar.gz` | `d13f028` | `10ef9b50c5bc92a59bbbe1c661b0fcaf8c1af4cf782d777e27efc0752a20f315` |

The exact candidate installers are pinned in [release/2.0.0.json](release/2.0.0.json).
Later commits change only the Linux link step, documentation and release tools,
so the macOS and Windows plugin code is current. A new build changes the
candidate: repeat its package/hash/host checks and update the manifest before
shipping it. The sanitizer checks unpacked payloads and newly introduced
history separately from the accepted older public history.

The macOS install and installed-plugin render checks passed on 2 November.
The README shows a before/after detail and short videos of a public-domain
1943 US Navy film, plus the Resolve screenshots with a synthetic clip
(`assets/documentation/README.md`).
`tools/stage_release.py` assembles downloads only after all three installer
hashes match the manifest; see [docs/MAINTAINERS_RELEASE.md](docs/MAINTAINERS_RELEASE.md).

### Before publishing (owner)

- Review the assembled candidate and the documented untested host/OS scope.
- Releases stay unsigned by decision (no Apple Developer ID, notarization or
  Windows code-signing certificate). The macOS bundle is ad hoc signed. The
  Gatekeeper and SmartScreen steps are in docs/INSTALL.md.
- The winget 2.0.0 manifest is a draft in `installers/win/winget/2.0.0`,
  pending approval; submit it after the release is live.
- Approve the first push. The older public history keeps its earlier
  references by decision of 1 November 2026; the new commits are clean.
