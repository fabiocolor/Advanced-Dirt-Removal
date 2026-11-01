# What has been tested

**Tested** means the effect ran on that combination and its output was compared
byte for byte with the expected result. **Built** means it compiles for that
target but has not been run there. Anything not listed has not been tried.
Please report results from other hosts, systems and GPUs in
[GitHub Issues](https://github.com/fabiocolor/Advanced-Dirt-Removal/issues),
with the diagnostic log.

Version 2.0.0 (development), November 2026.

## Engine and plugin by system and GPU

| System | GPU / backend | Status |
| --- | --- | --- |
| macOS 27, Apple M5 Pro (arm64) | Metal | Tested: 58 engine cases, OFX contract test, two real film scans × 29 settings |
| macOS 27, Apple M5 Pro (arm64) | OpenCL | Tested: same as Metal |
| macOS 27, Apple M5 Pro (arm64) | CPU fallback | Tested: 58 engine cases, OFX contract test |
| macOS, x86-64 build | Metal, OpenCL, CPU | Tested under Rosetta 2 on the M5 Pro only (58 cases on CPU); not yet on an Intel Mac |
| Windows 11 x64 | CUDA, OpenCL, CPU | Built from this source: not yet. Earlier development builds of the same engine were tested on NVIDIA RTX 3090 and RTX 4500 Ada (CUDA, OpenCL) and AMD Radeon RX 5700 XT and Pro W5500 (OpenCL) |
| Linux x86-64 (Ubuntu 24.04) | CUDA, OpenCL, CPU | Built from this source: not yet. Earlier development builds were tested on an NVIDIA RTX 3090 (CUDA, OpenCL) |

## Hosts

| Host | System | Status |
| --- | --- | --- |
| Nuke 17.1 | macOS, Metal | Tested: real 56-frame film scan, four settings, every frame identical to the engine |
| DaVinci Resolve Studio 21.1 | macOS, Metal | Tested: Fusion page, real 56-frame film scan, three settings, every frame identical to the engine; bypass identical to the source |
| DaVinci Resolve Studio 21.1 | Windows, CUDA | Earlier development build tested; this build: not yet |
| DaVinci Resolve 20 (free) | Windows, AMD OpenCL | Earlier development build tested up to 2.4K (the free version caps at UHD) |
| Nuke 17 | Windows | Earlier development build tested; this build: not yet |
| Resolve, Nuke | Linux | Not tested |
| Other OFX hosts (Fusion Studio, Flame, Baselight and others) | any | Not tested |
| Adobe Premiere Pro, After Effects | any | Not supported yet (they do not load OFX plugins; an adapter is planned) |

## Fusion macros

| Macro | Host | Status |
| --- | --- | --- |
| Advanced Dirt Removal | Resolve Studio 21.1, macOS | Tested: default settings give output identical to the effect alone (56 frames); Motion Mask mode renders, and every pixel lies between source and repair (12 frames) |
| Advanced Dirt Removal Cineon | Resolve Studio 21.1, macOS | Renders (12 frames, finite, every frame changed); picture quality on Cineon scans not yet reviewed |
| Both | Resolve on Windows and Linux, Resolve 18 to 20 | Not tested with 2.0 |

The recovery brush with a painted mask has not been exercised by a script yet;
it uses the same effect-mask path as the motion mask.

## How the checks are run

`python3 tools/check_release.py build` builds the plugin and checks the engine on
each available backend against `tests/engine_hashes.json`: 29 control settings
(every control away from its default, plus combinations) on two synthetic clips,
one with odd dimensions. It then drives the plugin through a minimal OFX host
(`ofx/tests/contract_host.cpp`): exact RGB output, alpha, row padding, seeking,
parameter edits, a missing frame, and the render-lock behaviour hosts depend on.
