# Memory-usage fix: qualification, 8 November 2026

The dominant Resolve Fusion memory growth was reproduced and mitigated in the
tested GPU host-image paths. The memory-fix regression campaign is complete for
the scope below. This is a local, unreleased candidate. Pushing remains on hold
until matte recovery integration is complete; existing installer checksums do
not identify a package containing this fix.

## Problem and cause

The original 2.0.0 candidate accumulated about 100 MiB of additional Resolve
memory per 2048×1556 output frame in the tested CPU host-image path. On a
24 GiB Apple silicon Mac, 100 real frames reached 13.38 GiB process footprint,
5.94 GiB additional system swap use and 6.582 GiB swapouts. A Windows 500-frame
synthetic render reached 54.5 GiB private memory; the larger machine did not
page during that run.

Engine-free OFX copy probes reproduced growth with balanced image gets and
releases, including a current-frame-only dependency declaration. GPU-buffer
copy probes stayed bounded with the original temporal declaration and ahead
fetches. The exact allocation owner within Resolve or its driver has not been
identified by allocation stacks. This does not establish that the cleaning
engine's whole working set leaks.

The fix adds optional Metal host-image transport on macOS and CUDA/OpenCL
host-image transport on Windows and Linux. Render-scoped staging preserves raw
pixel bytes, row padding and partial output windows; transfers complete before
images and staging are released. Host GPU context is restored around engine
work and teardown. Hosts that reject optional GPU extensions retain the existing
CPU image path. Host-image transport and the selected cleaning-engine backend
are separate choices.

A separate Windows CPU teardown leak is also fixed: suspended Fiber stacks
are freed after their worker threads have joined. Five default 64-thread
context cycles retained 6.22 GiB before that correction, versus 10.3 MiB after,
with all five expected output hashes unchanged.

No cleaning shader, arithmetic, temporal window, prefetch, motion history,
checkpoint policy, control mapping or expected engine hash was changed. Buffer
reuse-pool and checkpoint retention are separate policies and remain unchanged.

## Memory and fidelity results

| Candidate path | Completed scope | Memory result |
| --- | --- | --- |
| macOS Metal, real 2048×1556 | Two fresh 100-frame renders; tested original pixels match | 3.75 / 3.88 GiB peak footprint; no new swap use or swapouts |
| Windows CUDA, synthetic 512×512 | Two 100-frame runs; every original float pixel matches | Private-memory slopes +0.006 / +0.217 MiB per frame, versus original +10.105 and copy control +0.299; no pagefile growth |
| Windows CUDA, synthetic 2048×1556 | Two 100-frame runs and a production-mode repeat; repeat pixels and original 16-frame prefix match | Production peak 6.472 GiB private, 3.325 GiB resident and 2.519 GiB dedicated VRAM; private slope +0.248 MiB per frame; no pagefile growth |
| Windows CUDA, native-generated 4096×2160 | Two 100-frame runs; original prefix and independent engine reference match | Production private growth 4.194 MiB per frame above the matched copy/export control; no pagefile growth |
| Windows CUDA, native-generated 6144×3160 | 16-frame original/candidate and engine-reference comparisons match | 18.333 GiB private, 6.971 GiB resident and 11.453 GiB dedicated VRAM; short fidelity test, not a stability result |

Footprint, private memory, resident memory and VRAM are different measurements.
Swap counters are system-wide. Matched 512×512 render times were 6.69 seconds
original and 6.69 / 6.20 seconds candidate: no slowdown was observed there.
Matched larger-resolution performance and unlimited-session boundedness are
not established.

## Regression coverage of this candidate

| Platform or host | Result |
| --- | --- |
| macOS arm64, Metal / OpenCL / CPU | Original 58 engine cases and strict OFX contract on each; universal build and native Metal transport unit pass |
| Windows RTX A5000, CUDA / OpenCL / CPU | 58 cases and strict contract on each; Resolve feature, recovery, alpha, pixel-parity and bounded memory checks pass |
| Windows RX 580, OpenCL / CPU | 58 cases and strict contract on each; 96 real-scan profile-frame hashes exact; three OpenCL recovery maps exact |
| Windows Quadro P5000, Pascal | Automatic fallback selects OpenCL: 58 cases exact. Forced CUDA produces the expected unsupported-device-image error |
| Linux RTX 3090, CUDA / OpenCL / CPU | 58 cases and strict contract on each; 144 real-scan profile-frame hashes and nine recovery maps exact |
| Linux Nuke Indie 17.1v2 and Resolve Studio 21.0.4.5 Fusion | Nine original host checks independently pass in each application |
| Windows Nuke Indie 17.0v3, RTX 3090 | Nine original host checks pass; 25 additional settings × 12 real 2368×1712 frames = 300 exact matches against the unchanged native-engine reference |
| Portable Linux build, AlmaLinux 8 / GCC 14 | CPU 58 cases, strict CPU contract and simulated transport unit pass; maximum GLIBC symbol version 2.27, only the two OFX exports |
| Portable Linux runtime, RTX A4000 / Ubuntu | 174 engine cases, three strict contracts and 21 exact frames per backend through the actual dynamically loaded OFX module |
| Resolve file-input formats, native 640×360 | 28 DPX/EXR/ProRes variants × 16 frames: 448 exact original/candidate frame pairs, 896 decoded outputs; alpha compared where present |

The format variants cover DPX 10/12/16-bit big/little endian; half/float,
RGB/RGBA EXR with none/RLE/ZIP1/ZIP16/PIZ compression; and ProRes 422/4444.
This is separate from the native 2K/4K/short-6K fidelity tests, not a completed
format-by-raster matrix.

The Windows Nuke control suite clears RAM cache between independent profiles;
its rendering Job peaked at 3,076,943,872 bytes committed RAM, with no pagefile
growth. It qualifies output and controls, not uninterrupted cache growth.
Two earlier native-reference attempts stopped on GPU activity-counter coverage
faults. Completed profiles were retained, and a persistent native C-API context
finished the remaining reference work with the original classifier and guards.
The first CLI profile calibrated exactly to the C-API result.

The original feature fixture's cached after-error Write did not create a fresh
EXR. The separate original same-instance error probe saved and independently
verified fresh corrected outputs. The missing-file verifier failure remains
recorded alongside the completed nine checks.

Two test portability corrections were required: a null initial fake
thread-local GPU context for GCC 13, and explicit `Threads::Threads` linkage
for the simulated transport test on glibc 2.28. A new dynamic-module test host
also needed its own image-reading suite pointers initialized. These fixture
corrections did not change the qualified product's pixel processing.

## Limits and release boundary

- CPU-only Resolve host-image transport retains its existing memory behaviour.
  Forcing `ADR_BACKEND=cpu` alone does not force the host to supply CPU images.
- The extended Mac render stopped for warning pressure after 462 logged frames
  of 494, below 3.85 GiB with no measured swap growth. It is not a completed
  long-run test. AMD CPU recovery attempts reached their 300-second bounds.
- Mac and one Windows Nuke test profile returned licence error 100. Existing
  licensed Windows and Linux profiles completed their host checks. The initial
  Mac Nuke feature results in [TESTED.md](TESTED.md) remain historical;
  this campaign does not refresh them.
- Native Intel Mac hardware, Resolve Color/Edit, other OFX hosts, real 4K+
  films and 8K are not newly qualified. Tests are technical comparisons, not
  creative approval of many films.
- Isolated projects and private discovery paths were used. Preferences were
  restored, generated outputs/build scratch were cleaned after evidence
  verification, and test reservations were released. No production plugin was
  replaced and no new installer, tag or release was published.
- Complete matte recovery integration before pushing. Then qualify the
  integrated source and rebuilt packages and replace the historical installer
  manifest under [MAINTAINERS_RELEASE.md](MAINTAINERS_RELEASE.md).

## Reproduction and retained evidence

Use a fresh Resolve process and an isolated Fusion project with
`MediaIn → Advanced Dirt Removal → MediaOut`, defaults, recovery off. Record
the source raster, format, length, plugin hash and backend. Compare no-tool,
engine-free copy, original and candidate runs at the same settings. Sample
process memory, dedicated GPU memory and system swap/compressor; use finite
pressure, input-activity and deadline guards. Independently count outputs and
compare decoded pixels. Keep interrupted attempts distinct from passes.

The qualified processing/build inputs are the memory changes on top of
`b8f5e2b93611fb8805021f6dfe3649eb5a84fbfc`. The 83-file source archive has SHA-256
`0f7d3513352ab48d5c3e7f86b0c6eb74a531b1d60aeff60be4de124fa7ca36eb`.
`tests/engine_hashes.json` remains unchanged at
`206f05fecb9f5bdf54d76d1446993b1bad47bed879f1812fde9b6d28c40cdfa7`.
The real-scan RGB32 input is
`e81dbe028eee090bd81d3b734134c9fd5c9e2e426cc9a9daa7065c08d6169dc0`.
Private footage and raw logs are kept outside this public repository.

The maintainer's compact evidence archives were independently checked for
SHA-256 and ZIP CRC. These hashes identify the retained evidence, not release
downloads:

| Evidence archive | SHA-256 |
| --- | --- |
| `linux-original-release-compact.zip` | `968f588f1449437a66c8b2be38ed5eb198118ecfcd2cf1a80dd64505cabb7270` |
| `amd-resumed-compact.zip` | `22aedefa7dd9265d9db682297658fd92b034e4125cd05a3285c64fafd0a17e8c` |
| `amd-real-observed-compact.zip` | `54580e04dde59b10c4d5e737a464099214afd38d0d956bb298a011a8deb1d793` |
| `windows-nuke-complete-compact.zip` | `06119dc901fe21f03a81e8656b973434f6fcf5b7f5f3fd1a24b9844475cb4f49` |
| `formats-compact-20261108.zip` | `84cb816242129cc2ed8a0811a2c12497b8977a233179d02a2e67d94311dae3f6` |
| `pascal-compact.zip` | `3b92d13c4ce0bc2adb854fe7c03a8e09e957d35840b9f58e1a0c80a263eede5a` |
| `portable-builder-compact.zip` | `3c1707231b6f2f77a735d8b29dbfaa281e023c447fe8d4f3e3149607aae80e01` |
| `portable-native-compact.zip` | `1af5642fcd18d25428d04bdf3f2b4a3c6e399f784663dfc0ad322a40b1872288` |
