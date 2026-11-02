# Advanced Dirt Removal

[![Version](https://img.shields.io/badge/version-2.0.0-blue?style=flat-square)](https://github.com/fabiocolor/Advanced-Dirt-Removal/releases)
[![License](https://img.shields.io/badge/license-MIT-green?style=flat-square)](LICENSE)
[![Platform](https://img.shields.io/badge/platform-macOS%20%7C%20Windows%20%7C%20Linux-lightgrey?style=flat-square)](#install)
[![Download](https://img.shields.io/badge/Download-Latest%20Release-brightgreen?style=for-the-badge&logo=download)](https://github.com/fabiocolor/Advanced-Dirt-Removal/releases/latest)

Free, open-source dirt and dust removal for film scans, made for archives,
educators and restoration projects.

Advanced Dirt Removal is an OFX plugin with its own temporal repair engine. It
finds dirt, dust and sparkle that appear in one frame only and repairs them from
the neighbouring frames, with motion compensation. It runs on the GPU (Apple
Metal, NVIDIA CUDA, OpenCL for AMD, Intel and NVIDIA) and falls back to the CPU,
with byte-identical results on all of them.

Everything is in one effect:

- **Input encoding:** Video, or Cineon log for film scans (cleaned in linear,
  untouched pixels keep their exact log values).
- **Recovery mask:** paint where the original should be kept.
- **Auto recovery:** a built-in model gives back repaired spots that were real
  picture detail. One button calculates a matte for the whole shot, saved to
  disk; paint on top of it to add or remove areas.
- **Spanish dirt removal:** repair only where you paint.
- **Views:** Restored, Red difference (every repair in red on a grey picture),
  True difference and the auto-recovery matte.

> **New in 2.0:** version 1.x was a Fusion macro around DaVinci Resolve's
> built-in dirt removal. 2.0 is a stand-alone plugin with its own engine, for
> DaVinci Resolve, Nuke and other OFX hosts on macOS, Windows and Linux.
> [docs/TESTED.md](docs/TESTED.md) lists exactly which hosts, systems and GPUs
> have been tested, and [docs/RELEASE_NOTES.md](docs/RELEASE_NOTES.md) what is
> new.

## Install

**macOS:** download `AdvancedDirtRemoval-<version>.pkg` from the
[latest release](https://github.com/fabiocolor/Advanced-Dirt-Removal/releases/latest)
and open it.

**Windows:**

```powershell
winget install FabioColor.AdvancedDirtRemoval
```

or download the installer from the latest release.

**Linux:** download `AdvancedDirtRemoval-<version>-linux-x86_64.tar.gz`, extract
it and run `sudo ./install.sh`.

The installers put the plugin in the standard OFX plugin folder. Restart your
host afterwards. Manual installation, folder locations and uninstalling:
[docs/INSTALL.md](docs/INSTALL.md).

## Quick start

1. Add **Advanced Dirt Removal** (group **FabioColor**) to the complete,
   unedited shot:
   - **DaVinci Resolve:** Fusion page → Add Tool → OpenFX → FabioColor.
   - **Nuke:** Tab → "Advanced Dirt Removal".
2. Set **Input encoding** to *Cineon log* for Cineon scans; leave *Video*
   otherwise.
3. Check the result with **View: Red difference**.
4. Where a real detail was repaired, press **Calculate recovery** for auto
   recovery, and/or connect a paint or mask tool to the **Mask** input and paint
   the original back.

The [user guide](docs/USER_GUIDE.md) describes every control, the recovery
workflow and the views.

| Control group | Controls |
| --- | --- |
| Top | Input encoding, View |
| Detection | Analyse, Remove dark spots, Remove light spots, Sensitivity, Extra sensitivity, Analysis detail, Limit spot size, Largest spot, Extended-range input |
| Processing | Repair amount, Blend repairs, Blend amount |
| Safeguards | Motion guard, Shot changes, Shot-change guard, Flash guard |
| Recovery | Mask (Recovery / Spanish dirt removal), Calculate recovery, Auto recovery, Confidence, Matte folder |
| View options | Difference threshold, Red difference intensity, Red highlight opacity, Grayscale background, Difference gain |

The plugin keeps a diagnostic log (backend, device, timing, errors); its
location is in the [user guide](docs/USER_GUIDE.md#diagnostics-speed-and-limits).
Please attach it to issue reports.

## Build from source

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DADR_WITH_OPENCL=ON
cmake --build build --config Release --parallel
python3 tools/check_release.py build
```

Options for CUDA, OpenCL and the CPU fallback, and the local checks:
[docs/BUILD.md](docs/BUILD.md). There is no CI service; releases are built and
checked locally.

## Support

- Issues: [GitHub Issues](https://github.com/fabiocolor/Advanced-Dirt-Removal/issues)
- Discussions: [GitHub Discussions](https://github.com/fabiocolor/Advanced-Dirt-Removal/discussions)

## License

MIT; see [LICENSE](LICENSE), including the auto-recovery model weights in
`ofx/resources`. The OpenFX headers in `ofx/vendor/openfx` are under
their own BSD 3-Clause licence ([ofx/vendor/openfx/LICENSE.md](ofx/vendor/openfx/LICENSE.md)).

<div align="center">
  <sub>Making film restoration accessible to archives, educators and preservation enthusiasts</sub>
</div>
