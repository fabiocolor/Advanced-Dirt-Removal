# Building Advanced Dirt Removal

The engine and the OFX plugin build with CMake 3.21 or newer and a C++17
compiler. No GPU SDK is needed except for CUDA: Metal and OpenCL kernels are
compiled at run time from the embedded kernel source, and the OpenCL loader is
opened at run time.

## Quick start

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
```

The plugin is written to `build/AdvancedDirtRemoval.ofx.bundle`, with the
auto-recovery model (`ofx/resources/recovery-model.bin`) in its `Resources`
folder. Copy the whole
bundle to your host's OFX plugin folder (see [INSTALL.md](INSTALL.md)).

## Options

| Option | Default | Meaning |
| --- | --- | --- |
| `ADR_WITH_METAL` | ON on macOS | Metal backend (Apple GPUs, and AMD/Intel GPUs in Intel Macs) |
| `ADR_WITH_OPENCL` | OFF | OpenCL 1.2 backend (AMD, Intel and NVIDIA GPUs) |
| `ADR_WITH_CUDA` | OFF | CUDA backend (NVIDIA, compute capability 7.5 and newer); needs the CUDA toolkit (`nvcc`) at build time |
| `ADR_WITH_CPU` | ON | CPU fallback, used when no GPU backend can start |
| `ADR_BUILD_TESTS` | ON | OFX contract test host (`adr_ofx_contract`) |
| `CMAKE_OSX_ARCHITECTURES` | `arm64;x86_64` | macOS universal binary; set `arm64` for a faster local build |
| `ADR_CODESIGN_IDENTITY` | `-` (ad hoc) | macOS signing identity for the bundle |

Typical builds:

```sh
# macOS (Metal + OpenCL + CPU, universal)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DADR_WITH_OPENCL=ON

# Windows x64, Visual Studio 2022 (CUDA + OpenCL + CPU)
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DADR_WITH_CUDA=ON -DADR_WITH_OPENCL=ON
cmake --build build --config Release

# The CUDA kernels need only nvcc, not the full toolkit: NVIDIA's pip wheels work
#   python -m pip install "nvidia-cuda-nvcc>=13,<14" "nvidia-cuda-runtime>=13,<14" "nvidia-cuda-crt>=13,<14"
#   and set CUDA_PATH to site-packages/nvidia/cu13

# Linux x86-64 (CUDA + OpenCL + CPU)
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DADR_WITH_CUDA=ON -DADR_WITH_OPENCL=ON
```

Release builds for Linux are made with `sh installers/linux/build_portable.sh`,
which builds in a manylinux_2_28 (AlmaLinux 8) container so the plugin loads on
any distribution with glibc 2.27 or newer. A build on a newer distribution only
loads on that glibc or newer.

## Choosing a backend at run time

The plugin tries Metal, CUDA, OpenCL and then the CPU, and uses the first that
starts. Set `ADR_BACKEND` to force one, optionally with a device index:
`ADR_BACKEND=opencl:1`. The engine writes a diagnostic log (backend, device,
timings and any error); `ADR_LOG=off` disables it.

## The auto-recovery model

`ofx/resources/recovery-model.bin` holds the weights of the auto-recovery
classifier (a ResNet-18 with 13 input channels and one output, about 22 MB):
batch normalisation folded into each convolution, weights in float16, biases in
float32. The plugin runs it with the same kernels on every backend; no machine
learning framework is needed at build or run time.
`tools/convert_recovery_model.py` (needs PyTorch) writes this file from a
training checkpoint and documents the format.

## Determinism

Every backend gives byte-identical output for the same input and controls.
Floating-point contraction is disabled in host code and in every kernel
dialect, and division and square root use IEEE rounding. A few divisions use
the x86 approximate reciprocal; its table is fixed per CPU family
(`ADR_RECIPROCAL_TABLE=amd|intel|host`). Pin one value on render farms that mix
AMD and Intel CPUs.

## Checks

There is no CI service. Run the checks locally before a release:

```sh
python3 tools/check_release.py build                      # build, engine hashes on each backend, OFX contract
python3 tools/check_release.py build --backends cuda,opencl,cpu   # e.g. on Windows or Linux
```

`tests/engine_hashes.json` lists synthetic clips (generated deterministically
by `adr_bench --synthetic`), control settings and the expected output hash.

The engine benchmark measures speed and prints the output hash:

```sh
build/engine/adr_bench --synthetic 7 --width 1920 --height 1080 --frames 24 --backend metal --repeat 3
build/engine/adr_bench --input clip.f32 --width W --height H --frames N --control sensitivity=0.5
```
