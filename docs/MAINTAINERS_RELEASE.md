# Maintainer release notes

For repository maintainers. End-user docs: [INSTALL.md](INSTALL.md).

There is no CI service. Every release is built and checked locally.

## Before a release

1. `python3 tools/check_release.py build` on each platform you ship
   (`--backends metal,opencl,cpu` on macOS, `--backends cuda,opencl,cpu` on
   Windows and Linux). All cases must pass.
2. Render a real clip in at least Resolve and Nuke and note the result in
   [TESTED.md](TESTED.md).
3. `python3 tools/check_release.py scan --history` with the local
   `.sanitize-denylist` (never committed), plus the built packages:
   `python3 tools/check_release.py scan installers/mac/build build/release-win`.

## Packages

| Platform | Command | Output |
| --- | --- | --- |
| macOS | `bash installers/mac/build_pkg.sh 2.0.0` | `installers/mac/build/AdvancedDirtRemoval-2.0.0.pkg` |
| Windows | `powershell -ExecutionPolicy Bypass -File installers\win\build_installer.ps1 -Version 2.0.0` | `installers\AdvancedDirtRemoval-Setup-2.0.0.exe` |
| Linux | `sh installers/linux/build_tarball.sh 2.0.0 -DADR_WITH_CUDA=ON -DADR_WITH_OPENCL=ON` | `installers/linux/build/AdvancedDirtRemoval-2.0.0-linux-x86_64.tar.gz` |

Release assets: the macOS `.pkg` and Windows `.exe` each in a ZIP
(`AdvancedDirtRemoval-<version>-macos.zip`, `AdvancedDirtRemoval-Setup-<version>-win64.zip`),
the Linux tarball, `Advanced Dirt Removal.setting` and `INSTALL.md`.

macOS: the bundle is ad hoc signed by default. For distribution build with
`-DADR_CODESIGN_IDENTITY="Developer ID Application: ..."`, sign the package with
`ADR_PKG_SIGN_IDENTITY="Developer ID Installer: ..."` and notarize it.

## winget

The 2.0 installer is a machine-wide install (the OFX folder needs administrator
rights); 1.x was per user. Update the `FabioColor.AdvancedDirtRemoval` manifest:

- `Scope: machine`, `ElevationRequirement: elevatesSelf`
- `InstallerType: inno` with the explicit switches:
  - `Silent: /VERYSILENT /SUPPRESSMSGBOXES /NOCANCEL /NORESTART /SP-`
  - `SilentWithProgress: /SILENT /SUPPRESSMSGBOXES /NOCANCEL /NORESTART /SP-`

The former GitHub Actions release workflow is kept for reference only in
`.ci-archive/github-actions/` and is not run.
