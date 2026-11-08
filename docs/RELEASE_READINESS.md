# Release readiness — 8 November 2026

**Local commit authorized; push held until matte recovery integration is
complete.** No new installer, production installation, tag or release has been
made for the memory fix.

## Current source qualification

The memory-fix campaign is complete for its documented scope. It reproduced
the dominant Resolve Fusion memory growth and validated optional GPU
host-image transport with unchanged expected pixels. A separate Windows CPU
Fiber-stack teardown leak is fixed. See
[MEMORY_QUALIFICATION.md](MEMORY_QUALIFICATION.md) for measurements, source and
evidence checksums, the CPU-only host-path limitation and interrupted attempts.

Completed follow-up checks include macOS/Windows/Linux engine and OFX
regressions, licensed Windows and Linux Nuke, Pascal automatic OpenCL fallback,
portable Linux ABI/runtime, and 28 DPX/EXR/ProRes variants with 448 exact frame
pairs. Preferences were restored, generated scratch cleaned, and test
reservations released. These checks qualify the memory candidate, not an
integration that has not happened yet.

The existing auto-recovery model and controls remain implemented. The further
matte recovery integration requested before pushing is a separate pending
milestone; this memory-fix commit does not mark it complete.

## Before pushing or publishing

1. Complete matte recovery integration and record its actual source and test
   scope. Keep the push held while that work is pending.
2. Run the applicable engine, contract, transport and host regressions for the
   integrated source. Preserve expected hashes unless a separately justified
   processing change requires new reference evidence.
3. Rebuild the platform installers from the integrated source; repeat their
   package, sanitizer, installed-file and host checks. Update
   [`release/2.0.0.json`](../release/2.0.0.json) with the resulting actual
   revisions and hashes before assembling a new candidate. The existing
   manifest identifies the older candidate below and must not be relabelled
   as containing the memory fix.
4. Follow [MAINTAINERS_RELEASE.md](MAINTAINERS_RELEASE.md) for the reviewed
   source, intended version/tag, checked downloads and owner publication
   approval. Keep GitHub Actions absent and preserve public history.
5. Update winget only after the approved installer is live and its downloaded
   hash and installation have been verified.

No Intel hardware, Resolve Color/Edit, new OFX host or unlimited-session claim
is introduced by closing the memory-fix campaign.

## Historical installer qualification — 2 November 2026

This section preserves the pre-memory-fix installer qualification. Its files
and checksums remain valid records of those bytes; they do not contain the
subsequent memory repair or forthcoming matte recovery integration. The public
GitHub release and winget observations below were made on 2 November.

### Completed in the finalization pass

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

### Candidate location

Local, ignored output: `build/finalization-20261102/release-candidate-r2/`.
It contains the macOS/Windows ZIPs, Linux tarball, `INSTALL.md`,
`RELEASE_NOTES.md`, `release-manifest.json` and `SHA256SUMS`. Earlier staged
output is retained. Raw inputs and the Mac install/render receipts are under
`build/finalization-20261102/`; these local receipts are not public assets.

The build revisions in the manifest are intentional. Rebuilding an installer
creates a new candidate requiring the relevant checks and a manifest update.
Do not replace tested downloads with an unchecked rebuild from the final tag.

### Historical publication notes

The earlier publication plan is superseded by the integration and candidate
steps above. The original signing and untested-platform limits still apply.

The macOS package remains unsigned and its bundle is ad hoc signed. No
Developer ID identity is installed on the build Mac. Developer ID signing and
notarization are an optional additional distribution path and would require
their own candidate checks. The unsigned route is documented; interactive
Gatekeeper approval and macOS uninstall were not exercised in this pass.

Fresh-OS and Intel-Mac installation, Resolve Color/Edit, additional OFX hosts
and the future Adobe adapter retain their explicitly untested/planned status.
They are not represented as completed 2.0 qualifications.
