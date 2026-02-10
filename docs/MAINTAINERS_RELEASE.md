# Maintainer Release Notes

This page is for repository maintainers only.

End-user docs:
- `docs/INSTALL.md`

Maintainer/build/release references:
- `.github/workflows/release-on-tag.yml`
- `installers/win/AdvancedDirtRemoval.iss`
- `installers/win/build_installer.ps1`
- `installers/mac/build_pkg.sh`

Policy:
- Public release assets: ZIP installers + `.setting` files + `INSTALL.md`
- Internal security/check files: workflow artifacts (maintainer-only)

Winget/automation note (Windows installer):
- Use explicit Inno silent switches in manifests:
- `Silent: /VERYSILENT /SUPPRESSMSGBOXES /NOCANCEL /NORESTART /SP-`
- `SilentWithProgress: /SILENT /SUPPRESSMSGBOXES /NOCANCEL /NORESTART /SP-`
