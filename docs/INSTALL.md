# Install Advanced Dirt Removal

Each installer adds the **Advanced Dirt Removal OFX plugin** for DaVinci
Resolve, Nuke and other OFX hosts. Restart your host afterwards.

Version 1.x was a Fusion macro. On Windows the 2.0 installer first uninstalls a
1.x installation, including its macros. On macOS and Linux a 1.x macro is left
in place, so compositions that use it keep working; delete the
`Advanced Dirt Removal*.setting` files from the Fusion Macros folder if you no
longer need them.

## Windows

```powershell
winget install FabioColor.AdvancedDirtRemoval
winget upgrade FabioColor.AdvancedDirtRemoval   # later updates
```

Or download `AdvancedDirtRemoval-Setup-<version>-win64.zip` from the
[latest release](https://github.com/fabiocolor/Advanced-Dirt-Removal/releases/latest),
extract it and run the `.exe` (administrator rights are needed for the OFX
folder). Downloaded `.exe` files can trigger a SmartScreen prompt; `winget`
avoids it.

Installed to:

`C:\Program Files\Common Files\OFX\Plugins\AdvancedDirtRemoval.ofx.bundle`

Uninstall: Settings → Apps → Advanced Dirt Removal → Uninstall.

## macOS

Download `AdvancedDirtRemoval-<version>.pkg` (in `AdvancedDirtRemoval-<version>-macos.zip`)
and open it. If macOS says the package cannot be opened because it is
from an unidentified developer, open System Settings → Privacy & Security and
choose **Open Anyway**. If a previous version is installed, the installer asks whether to
replace it or only uninstall it.

Installed to: `/Library/OFX/Plugins/AdvancedDirtRemoval.ofx.bundle`

Uninstall: run the installer again and choose **Uninstall Only**, or delete the
bundle.

## Linux

```sh
tar -xzf AdvancedDirtRemoval-<version>-linux-x86_64.tar.gz
cd AdvancedDirtRemoval-<version>-linux-x86_64
sudo ./install.sh              # sudo ./install.sh --uninstall to remove
```

Installed to: `/usr/OFX/Plugins/AdvancedDirtRemoval.ofx.bundle` (without `sudo`:
`~/OFX/Plugins`; then start the host with `OFX_PLUGIN_PATH=$HOME/OFX/Plugins`).
DaVinci Resolve looks in `/usr/OFX/Plugins`, so use `sudo` for Resolve. The
plugin needs glibc 2.27 or newer.

## Manual installation

Copy the whole `AdvancedDirtRemoval.ofx.bundle` folder into your
OFX plugin folder:

| System | OFX plugin folder |
| --- | --- |
| macOS | `/Library/OFX/Plugins/` |
| Windows | `C:\Program Files\Common Files\OFX\Plugins\` |
| Linux | `/usr/OFX/Plugins/` |

Most hosts also read any folder listed in the `OFX_PLUGIN_PATH` environment
variable.

## Requirements

- macOS 12 or later (Apple silicon or Intel), Windows 10/11 x64, or Linux x86-64
  with glibc 2.27 or newer (Rocky/Alma Linux 8 and later, Ubuntu 20.04 and later).
- A GPU with Metal, CUDA (NVIDIA, compute capability 7.5 or newer) or OpenCL 1.2.
  Older NVIDIA GPUs use OpenCL automatically. Without a usable GPU the effect
  runs on the CPU, much more slowly.
- Auto recovery stores its mattes in the user's application data folder (a few
  megabytes per shot); see the [user guide](USER_GUIDE.md#auto-recovery).
