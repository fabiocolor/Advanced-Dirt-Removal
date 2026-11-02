# Maintainer release notes

For repository maintainers. End-user docs: [INSTALL.md](INSTALL.md).

There is no CI service. Every release is built and checked locally.

## Before a release

1. `python3 tools/check_release.py build` on each platform you ship
   (`--backends metal,opencl,cpu` on macOS, `--backends cuda,opencl,cpu` on
   Windows and Linux). All cases must pass.
2. Render a real clip in at least Resolve and Nuke, check every feature in the
   [user guide](USER_GUIDE.md) (views, recovery mask, auto recovery and its
   button, Spanish dirt removal and its error, Cineon log) and note the result
   in [TESTED.md](TESTED.md).
3. `python3 tools/check_release.py scan --history` with the local
   `.sanitize-denylist` (never committed), plus the built packages:
   `python3 tools/check_release.py scan installers/mac/build build/release-win`.

For 2.0, the owner retained the already-public history at `56dfe42`. Use
`python3 tools/check_release.py scan --history-since 56dfe42` to fail on new
messages, added text and paths without treating deletion of accepted old text
as a new finding. The base must be an ancestor of HEAD. This does not certify
the older history as clean; `--history` still scans it in full. The file scan
also reads ASCII and UTF-16 binary metadata. Expand installer payloads before
scanning: ZIP and tar members are scanned recursively in memory, but macOS
packages and Windows setup programs still need separate payload inspection.
Scanning their installer wrapper cannot certify compressed embedded files.

Run the release-tool integrity tests with:

```sh
python3 -m unittest discover -s tests -p 'test_release_tools.py' -v
```

## Packages

| Platform | Command | Output |
| --- | --- | --- |
| macOS | `bash installers/mac/build_pkg.sh 2.0.0` | `installers/mac/build/AdvancedDirtRemoval-2.0.0.pkg` |
| Windows | `powershell -ExecutionPolicy Bypass -File installers\win\build_installer.ps1 -Version 2.0.0` | `installers\AdvancedDirtRemoval-Setup-2.0.0.exe` |
| Linux | `sh installers/linux/build_portable.sh 2.0.0` (Docker; glibc 2.28) | `installers/linux/build/AdvancedDirtRemoval-2.0.0-linux-x86_64.tar.gz` |

Release assets, packaged the same way as 1.x:

- `AdvancedDirtRemoval-<version>-macos.zip` (the `.pkg`)
- `AdvancedDirtRemoval-Setup-<version>-win64.zip` and the plain
  `AdvancedDirtRemoval-Setup-<version>.exe` (winget installs from the `.exe`)
- `AdvancedDirtRemoval-<version>-linux-x86_64.tar.gz`
- `recovery-model-<version>.bin` and `WEIGHTS_LICENSE.txt`: the auto-recovery
  weights as their own versioned file, under CC BY-NC 4.0
- `LICENSE` (MIT, for the code), `INSTALL.md`, `RELEASE_NOTES.md`,
  `release-manifest.json` and `SHA256SUMS`

Use the version's section of [RELEASE_NOTES.md](RELEASE_NOTES.md) as the
release description. `tools/stage_release.py` writes all of these.

### Model weights

The weights in `ofx/resources/recovery-model.bin` are versioned per release
and licensed separately from the code (CC BY-NC 4.0, `WEIGHTS_LICENSE`). When
they change, add the new version and SHA-256 to `WEIGHTS_LICENSE` and
`ofx/resources/README.md`, and treat it as a processing change: new evidence,
new installers. Don't publish training code, data, recipes or provenance
details for the model.

Builds from this source put `WEIGHTS_LICENSE.txt` inside the plugin bundle and
next to the Windows and Linux installs. The 2.0.0 candidate installers pinned
in [`release/2.0.0.json`](../release/2.0.0.json) were built before that, so
for 2.0.0 the weights licence reaches users as a release attachment and in
the repository. Embedding it in the 2.0.0 installers means rebuilding them and
repeating their checks.

### Assemble the checked candidate

Keep the three raw installers in one input directory. For the existing 2.0.0
candidate their exact hashes and individual build revisions are in
[`release/2.0.0.json`](../release/2.0.0.json). The Linux installer must be the
rebuild with the host runtime fix; earlier tarballs have the same filename.

```sh
python3 tools/stage_release.py --manifest release/2.0.0.json \
  --input-dir /path/to/verified-installers --output-dir build/release-candidate
cd build/release-candidate
shasum -a 256 -c SHA256SUMS
```

Staging verifies every input hash before writing, checks the ZIP payloads,
copies the Linux tarball, includes current install/release notes, and writes a
manifest and checksums. It refuses to overwrite a previous candidate and never
uploads, tags or pushes. Archive members contain no build-machine paths.

The candidate includes only the plugin code already qualified on each platform;
subsequent documentation/release-tool commits do not invalidate that evidence.
If any installer is rebuilt, repeat the applicable package, sanitizer and host
checks, then update the manifest with its new hash and build revision. Do not
publish an untested rebuild merely to change the revision printed in its log.

### Publication boundary

The first public push and release need the owner's explicit approval. Until
then, keep the candidate local. After approval, confirm the reviewed commit,
the intended tag and candidate checksums; publish only those downloads and
the notes in `RELEASE_NOTES.md`. Remove the temporary candidate notices from
README/INSTALL when publishing. No history rewrite or force-push is needed.
Update winget only after its installer URL is live and its downloaded bytes
match the checksum. Check that it installs 2.0.0 before removing the warning
that winget still provides 1.1.3.

Releases are unsigned by decision: no Apple Developer ID, no notarization and
no Windows code-signing certificate. The macOS bundle is ad hoc signed (Apple
silicon hosts need that) and the package is unsigned, so Gatekeeper and
SmartScreen warn on first use. [INSTALL.md](INSTALL.md) has the steps for
users. If that ever changes, the build already accepts
`-DADR_CODESIGN_IDENTITY="Developer ID Application: ..."` and
`ADR_PKG_SIGN_IDENTITY="Developer ID Installer: ..."`, followed by
notarization.

## winget

A draft 2.0.0 manifest is in
[`installers/win/winget/2.0.0`](../installers/win/winget/2.0.0), marked as
pending approval. Submit it to microsoft/winget-pkgs only after the release is
live, and check that the downloaded `.exe` matches `InstallerSha256` and
installs 2.0.0.

The 2.0 installer is a machine-wide install (the OFX folder needs administrator
rights); 1.x was per user. The manifest therefore uses:

- `Scope: machine`, `ElevationRequirement: elevatesSelf`
- `ProductCode: {B8A6D2C1-7C2D-4E5A-9C7B-1F2F46B6E9F2}}_is1` (the AppId line,
  unchanged since 1.x, registers this exact key; keep it so upgrades match)
- `InstallerType: inno` with the explicit switches:
  - `Silent: /VERYSILENT /SUPPRESSMSGBOXES /NOCANCEL /NORESTART /SP-`
  - `SilentWithProgress: /SILENT /SUPPRESSMSGBOXES /NOCANCEL /NORESTART /SP-`

There are no GitHub Actions or CI workflows. The old release workflow is kept
for reference only in `.ci-archive/github-actions/` and never runs.
