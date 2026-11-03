# 2.0.0 release readiness — 2 November 2026

The local candidate is assembled. Public publication remains held for Fabio's
explicit approval. The latest public GitHub release and winget manifest were
checked on 2 November and still provide 1.1.3.

## Completed in the finalization pass

- Verified all three raw installer hashes against
  [`release/2.0.0.json`](../release/2.0.0.json). Earlier Linux tarballs with the
  same filename remain preserved; the candidate uses the corrected `d13f028`
  package, not an earlier build.
- Installed the macOS package into an empty system OFX target. The existing
  1.1.3 receipt upgraded to 2.0.0; all seven installed files match the expanded
  package and strict bundle signature verification passes.
- Ran the installed bundle in Resolve Studio 21.1.1.10 on the M5 Pro with a
  deterministic synthetic clip: default render byte-identical to the engine
  reference, Red Difference pixel-identical to its definition. The isolated
  documentation project is saved; Resolve is closed.
- Added current, unedited screenshots with synthetic content. Original 1.x
  artwork and private qualification material remain preserved.
- Added a release assembler that rejects incorrect installer hashes and
  existing output directories, verifies ZIP payloads and writes `SHA256SUMS`.
  Links in standalone release notes point at the intended version's docs.
- Added UTF-16 metadata and ZIP/tar-member scanning. New-history scanning
  excludes deletion of the accepted public baseline's text; full-history
  scanning remains available and still reports the retained legacy findings.
- Passed all three release-tool tests, current source/new-history scanning,
  archive/member scans, expanded macOS/Linux payload scans, candidate checksum
  verification, relative-document-link checks and `git diff --check`.

The raw Windows setup executable was rescanned and its hash matches the
installer qualified earlier. Its embedded payload was not extracted again on
the Mac; the earlier installed-file and host checks remain the Windows
qualification evidence. No engine or plugin processing code changed in this
pass. See [TESTED.md](TESTED.md) for the full platform/host evidence scope.

## Candidate location

Local, ignored output: `build/finalization-20261102/release-candidate-r2/`.
It contains the macOS/Windows ZIPs, Linux tarball, `INSTALL.md`,
`RELEASE_NOTES.md`, `release-manifest.json` and `SHA256SUMS`. Earlier staged
output is retained. Raw inputs and the Mac install/render receipts are under
`build/finalization-20261102/`; these local receipts are not public assets.

The build revisions in the manifest are intentional. Rebuilding an installer
creates a new candidate requiring the relevant checks and a manifest update.
Do not replace tested downloads with an unchecked rebuild from the final tag.

## Docs, licensing and media pass (2 November, evening)

- Docs rewritten in plain language:
  README, INSTALL, USER_GUIDE, RELEASE_NOTES, a new top-level CHANGELOG.
- Install steps for unsigned releases on each platform: Gatekeeper (Open
  Anyway, right-click → Open, `xattr`), SmartScreen (More info → Run anyway)
  and Linux. Releases stay unsigned by decision.
- The auto-recovery weights are now licensed separately from the code:
  CC BY-NC 4.0 in `WEIGHTS_LICENSE`, weights version 2.0.0
  (`cce1862a…`). The code stays MIT. New builds carry `WEIGHTS_LICENSE.txt` in
  the bundle. The pinned 2.0.0 installers predate this, so the weights licence
  reaches 2.0.0 users as a release attachment (see
  [MAINTAINERS_RELEASE.md](MAINTAINERS_RELEASE.md#model-weights)).
- `stage_release.py` now also attaches the plain Windows `.exe` (as in 1.x),
  `recovery-model-2.0.0.bin`, `WEIGHTS_LICENSE.txt` and `LICENSE`. The staged
  candidate in `release-candidate-r2` predates this; stage a new candidate
  directory before publishing.
- Draft winget manifest in `installers/win/winget/2.0.0`, pending approval.
- `check_release.py build` passes on the M5 Pro (Metal, OpenCL, CPU) after the
  build change; no processing code changed.

## Remaining launch actions

1. Fabio reviews the candidate and explicitly approves the first public push
   and release. No publication approval has been inferred.
2. Publish the reviewed source, intended tag and checked candidate under
   [MAINTAINERS_RELEASE.md](MAINTAINERS_RELEASE.md). Keep GitHub Actions absent
   and preserve the existing public history.
3. Update winget after the installer URL is live; verify its downloaded hash
   and installation before advertising 2.0 through winget.

The macOS package remains unsigned and its bundle is ad hoc signed. No
Developer ID identity is installed on the build Mac. Developer ID signing and
notarization are an optional additional distribution path and would require
their own candidate checks. The unsigned route is documented; interactive
Gatekeeper approval and macOS uninstall were not exercised in this pass.

Fresh-OS and Intel-Mac installation, Resolve Color/Edit, additional OFX hosts
and the future Adobe adapter retain their explicitly untested/planned status.
They are not represented as completed 2.0 qualifications.
