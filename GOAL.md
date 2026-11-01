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
