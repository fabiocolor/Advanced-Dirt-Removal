# Install Advanced Dirt Removal

Each installer adds two things: the **Advanced Dirt Removal OFX effect** (for
DaVinci Resolve, Nuke and other OFX hosts) and the **Advanced Dirt Removal Fusion
macro** for DaVinci Resolve (one macro for video and Cineon footage; it replaces
the two 1.x macros, and the installers remove the old Cineon one). Restart your host afterwards.

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

- Effect: `C:\Program Files\Common Files\OFX\Plugins\AdvancedDirtRemoval.ofx.bundle`
- Macro (all users): `C:\ProgramData\Blackmagic Design\DaVinci Resolve\Fusion\Macros\`

Version 1.x installed the macros per user; the 2.0 installer removes that older
copy and its entry in Apps.

Uninstall: Settings → Apps → Advanced Dirt Removal → Uninstall.

## macOS

Download `AdvancedDirtRemoval-<version>.pkg` (in `AdvancedDirtRemoval-<version>-macos.zip`)
and open it. If a previous version is installed, the installer asks whether to
replace it or only uninstall it.

Installed to:

- Effect: `/Library/OFX/Plugins/AdvancedDirtRemoval.ofx.bundle`
- Macro: `/Library/Application Support/Blackmagic Design/DaVinci Resolve/Fusion/Macros/`

Uninstall: run the installer again and choose **Uninstall Only**, or delete the
bundle and the `.setting` file.

## Linux

```sh
tar -xzf AdvancedDirtRemoval-<version>-linux-x86_64.tar.gz
cd AdvancedDirtRemoval-<version>-linux-x86_64
sudo ./install.sh              # sudo ./install.sh --uninstall to remove
```

Installed to:

- Effect: `/usr/OFX/Plugins/AdvancedDirtRemoval.ofx.bundle` (without `sudo`:
  `~/OFX/Plugins`; then start the host with `OFX_PLUGIN_PATH=$HOME/OFX/Plugins`)
- Macro: `~/.local/share/DaVinciResolve/Fusion/Macros/`

## Manual installation

**The effect:** copy the whole `AdvancedDirtRemoval.ofx.bundle` folder into your
OFX plugin folder:

| System | OFX plugin folder |
| --- | --- |
| macOS | `/Library/OFX/Plugins/` |
| Windows | `C:\Program Files\Common Files\OFX\Plugins\` |
| Linux | `/usr/OFX/Plugins/` |

Most hosts also read any folder listed in the `OFX_PLUGIN_PATH` environment
variable.

**The macro:** copy `Advanced Dirt Removal.setting` to the Fusion Macros folder
(and delete an old `Advanced Dirt Removal Cineon.setting` there, if any):

| System | Fusion Macros folder (current user) |
| --- | --- |
| macOS | `~/Library/Application Support/Blackmagic Design/DaVinci Resolve/Fusion/Macros/` |
| Windows | `%APPDATA%\Blackmagic Design\DaVinci Resolve\Support\Fusion\Macros\` |
| Linux | `~/.local/share/DaVinciResolve/Fusion/Macros/` |

For the Edit/Cut pages you can also copy it to `.../Fusion/Templates/Edit/Effects/`.

## Requirements

- macOS 12 or later (Apple silicon or Intel), Windows 10/11 x64, or Linux x86-64.
- A GPU with Metal, CUDA (NVIDIA, compute capability 7.5 or newer) or OpenCL 1.2;
  otherwise the effect runs on the CPU, much more slowly.
- The macro needs DaVinci Resolve 18 or later.
