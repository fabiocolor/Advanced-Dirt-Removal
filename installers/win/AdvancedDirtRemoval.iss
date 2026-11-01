; Advanced Dirt Removal - Inno Setup script
; Installs the OFX effect for all OFX hosts and the Fusion macros for DaVinci Resolve.
; Build from repo root (after building the plugin, see docs/BUILD.md):
;   iscc /DMyAppVersion=2.0.0 /DBundleDir=..\..\build\release-win\AdvancedDirtRemoval.ofx.bundle installers\win\AdvancedDirtRemoval.iss
; or run installers\win\build_installer.ps1, which builds both.
; Requires Inno Setup 6: https://jrsoftware.org/isinfo.php

#define MyAppName "Advanced Dirt Removal"
#ifndef MyAppVersion
#define MyAppVersion "2.0.0"
#endif
#ifndef BundleDir
#define BundleDir "..\..\build\release-win\AdvancedDirtRemoval.ofx.bundle"
#endif
#define MyAppId "B8A6D2C1-7C2D-4E5A-9C7B-1F2F46B6E9F2"
#define BundleName "AdvancedDirtRemoval.ofx.bundle"
#define AllUsersMacros "Blackmagic Design\DaVinci Resolve\Fusion\Macros"
#define UserMacros "Blackmagic Design\DaVinci Resolve\Support\Fusion\Macros"
#define MacroFile1 "Advanced Dirt Removal.setting"
; 1.x also installed a separate Cineon macro; the 2.0 macro handles both encodings.
#define MacroFile2 "Advanced Dirt Removal Cineon.setting"

[Setup]
AppId={{{#MyAppId}}}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher=Fabio Color
AppPublisherURL=https://github.com/fabiocolor/Advanced-Dirt-Removal
; {app} holds only the uninstaller and the licence; the effect itself goes to the OFX folder.
DefaultDirName={commonpf64}\{#MyAppName}
DisableDirPage=yes
DisableProgramGroupPage=yes
CreateAppDir=yes
UsePreviousAppDir=no
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputBaseFilename=AdvancedDirtRemoval-Setup-{#MyAppVersion}
OutputDir=..
Compression=lzma2
SolidCompression=yes
UninstallDisplayName={#MyAppName}
LicenseFile=..\..\LICENSE

[Files]
Source: "..\..\LICENSE"; DestDir: "{app}"; DestName: "LICENSE.txt"; Flags: ignoreversion
; The OFX bundle, exactly as built (Contents\Win64\AdvancedDirtRemoval.ofx and Resources).
Source: "{#BundleDir}\*"; DestDir: "{commoncf64}\OFX\Plugins\{#BundleName}"; Flags: ignoreversion recursesubdirs createallsubdirs
; Fusion macros for every user of this computer.
Source: "..\..\{#MacroFile1}"; DestDir: "{commonappdata}\{#AllUsersMacros}"; Flags: ignoreversion

[InstallDelete]
; Version 1.x installed the macros per user; remove those copies so Resolve does not list them twice.
Type: files; Name: "{userappdata}\{#UserMacros}\{#MacroFile1}"
Type: files; Name: "{userappdata}\{#UserMacros}\{#MacroFile2}"
Type: files; Name: "{commonappdata}\{#AllUsersMacros}\{#MacroFile2}"
; Replace the whole bundle on upgrade.
Type: filesandordirs; Name: "{commoncf64}\OFX\Plugins\{#BundleName}"

[UninstallDelete]
Type: dirifempty; Name: "{commoncf64}\OFX\Plugins\{#BundleName}"

[Code]
{ Version 1.x was a per-user install with the same AppId. Remove it silently
  first so Apps & features shows a single entry. }
function SplitCommand(const Cmd: string; var ExePath: string; var Params: string): Boolean;
var
  S: string;
  P: Integer;
begin
  S := Trim(Cmd);
  ExePath := '';
  Params := '';
  if (Length(S) >= 2) and (S[1] = '"') then
  begin
    P := 2;
    while (P <= Length(S)) and (S[P] <> '"') do
      P := P + 1;
    ExePath := Copy(S, 2, P - 2);
    Params := Trim(Copy(S, P + 1, MaxInt));
  end
  else
  begin
    P := Pos(' ', S);
    if P > 0 then
    begin
      ExePath := Copy(S, 1, P - 1);
      Params := Trim(Copy(S, P + 1, MaxInt));
    end
    else
      ExePath := S;
  end;
  Result := (ExePath <> '') and FileExists(ExePath);
end;

function InitializeSetup(): Boolean;
var
  UninstallKey, UninstallStr, ExePath, Params: string;
  ResultCode: Integer;
begin
  Result := True;
  UninstallKey := 'SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\{#MyAppId}_is1';
  if RegQueryStringValue(HKCU, UninstallKey, 'UninstallString', UninstallStr) and SplitCommand(UninstallStr, ExePath, Params) then
    Exec(ExePath, Params + ' /VERYSILENT /SUPPRESSMSGBOXES /NORESTART', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
end;
