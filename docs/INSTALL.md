# Install Advanced Dirt Removal

Each installer adds the **Advanced Dirt Removal** OFX plugin for DaVinci
Resolve, Nuke and other OFX hosts. Restart your host after installing.

The installers are not signed. I don't pay for an Apple Developer ID or a
Windows code-signing certificate, so macOS and Windows will warn you the first
time. The steps below get past those warnings. If you prefer, you can check the
download against `SHA256SUMS` from the same release, or build the plugin
yourself ([BUILD.md](BUILD.md)).

Downloads are on the
[releases page](https://github.com/fabiocolor/Advanced-Dirt-Removal/releases).

## macOS

1. Download `AdvancedDirtRemoval-<version>-macos.zip` and unzip it.
2. Open `AdvancedDirtRemoval-<version>.pkg`.
3. macOS will say it can't check the package for malicious software. Close
   that message and allow it:
   - **macOS 15 and later:** open System Settings → Privacy & Security, scroll
     down to the message about the package and click **Open Anyway**. Confirm
     with your password.
   - **macOS 14 and earlier:** right-click (or Control-click) the package in
     Finder, choose **Open**, then **Open** again.
4. Follow the installer. If an older version is installed, it asks whether to
   replace it or only uninstall it.

From Terminal you can also remove the quarantine flag first, then open the
package normally:

```sh
xattr -dr com.apple.quarantine ~/Downloads/AdvancedDirtRemoval-<version>.pkg
open ~/Downloads/AdvancedDirtRemoval-<version>.pkg
```

Only do this for a file you downloaded from this repository's releases page.

Installed to: `/Library/OFX/Plugins/AdvancedDirtRemoval.ofx.bundle`

Uninstall: run the installer again and choose **Uninstall Only**, or delete the
bundle.

## Windows

1. Download `AdvancedDirtRemoval-Setup-<version>-win64.zip` and extract it, or
   download `AdvancedDirtRemoval-Setup-<version>.exe` directly.
2. Run the setup program. It needs administrator rights, because the OFX folder
   is shared by all users.
3. SmartScreen will show "Windows protected your PC". Click **More info**, then
   **Run anyway**.

Installed to:
`C:\Program Files\Common Files\OFX\Plugins\AdvancedDirtRemoval.ofx.bundle`

Uninstall: Settings → Apps → Advanced Dirt Removal → Uninstall.

**winget.** The 2.0.0 manifest in
[`installers/win/winget/2.0.0`](../installers/win/winget/2.0.0) has been
submitted and is waiting to be accepted. Until then, this command still
installs 1.1.3:

```powershell
winget install FabioColor.AdvancedDirtRemoval
```

Once 2.0.0 is accepted there, `winget upgrade FabioColor.AdvancedDirtRemoval`
will move you to 2.0.

## Linux

```sh
tar -xzf AdvancedDirtRemoval-<version>-linux-x86_64.tar.gz
cd AdvancedDirtRemoval-<version>-linux-x86_64
sudo ./install.sh
```

Installed to: `/usr/OFX/Plugins/AdvancedDirtRemoval.ofx.bundle`. DaVinci
Resolve looks there, so use `sudo` for Resolve.

Without `sudo` the plugin goes to `~/OFX/Plugins`. Then start your host with
`OFX_PLUGIN_PATH=$HOME/OFX/Plugins`.

Uninstall: `sudo ./install.sh --uninstall` (or without `sudo` for a user
install).

## Coming from 1.x

Version 1.x was a Fusion macro. On Windows the 2.0 installer uninstalls 1.x
first, including its macros. On macOS and Linux the 1.x macro stays where it
is, so compositions that use it keep working. Delete the
`Advanced Dirt Removal*.setting` files from the Fusion Macros folder if you
don't need them.

## Manual installation

Copy the whole `AdvancedDirtRemoval.ofx.bundle` folder into your OFX plugin
folder:

| System | OFX plugin folder |
| --- | --- |
| macOS | `/Library/OFX/Plugins/` |
| Windows | `C:\Program Files\Common Files\OFX\Plugins\` |
| Linux | `/usr/OFX/Plugins/` |

Most hosts also read any folder listed in the `OFX_PLUGIN_PATH` environment
variable. On macOS, if you copied the bundle from a download, run
`xattr -dr com.apple.quarantine` on it before starting the host.

## Requirements

- macOS 12 or later (Apple silicon or Intel), Windows 10/11 x64, or Linux
  x86-64 with glibc 2.27 or newer (Rocky/Alma Linux 8 and later, Ubuntu 20.04
  and later).
- A GPU with Metal, CUDA (NVIDIA, compute capability 7.5 or newer) or OpenCL
  1.2. Older NVIDIA GPUs use OpenCL automatically. Without a usable GPU the
  plugin runs on the CPU, which is much slower.
- Auto recovery stores its mattes in your application data folder, a few
  megabytes per shot. See the [user guide](USER_GUIDE.md#auto-recovery).

## Licences

The plugin code is under the MIT licence. The auto-recovery model weights,
installed inside the plugin as `recovery-model.bin`, are under CC BY-NC 4.0:
free for non-commercial use. Both licence texts are attached to each release
as `LICENSE` and `WEIGHTS_LICENSE.txt`. See the [README](../README.md#licence).
