; Advanced Dirt Removal - Inno Setup script
; Build from repo root: iscc installers\win\AdvancedDirtRemoval.iss
; Requires Inno Setup: https://jrsoftware.org/isinfo.php

#define MyAppName "Advanced Dirt Removal"
#ifndef MyAppVersion
#define MyAppVersion "1.1.0"
#endif
#define MyAppId "B8A6D2C1-7C2D-4E5A-9C7B-1F2F46B6E9F2"
#define ResolveMacrosPath "Blackmagic Design\DaVinci Resolve\Support\Fusion\Macros"
#define MacroFile1 "Advanced Dirt Removal.setting"
#define MacroFile2 "Advanced Dirt Removal Cineon.setting"

[Setup]
AppId={{{#MyAppId}}}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher=Fabio Color
DefaultDirName={userappdata}\{#ResolveMacrosPath}
DefaultGroupName={#MyAppName}
DisableDirPage=yes
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
OutputBaseFilename=AdvancedDirtRemoval-Setup-{#MyAppVersion}
OutputDir=..
Compression=lzma2
SolidCompression=yes
UninstallDisplayName={#MyAppName}

[Files]
Source: "..\..\Advanced Dirt Removal.setting"; DestDir: "{userappdata}\{#ResolveMacrosPath}"; DestName: "{#MacroFile1}"; Flags: ignoreversion
Source: "..\..\Advanced Dirt Removal Cineon.setting"; DestDir: "{userappdata}\{#ResolveMacrosPath}"; DestName: "{#MacroFile2}"; Flags: ignoreversion

[Icons]
Name: "{group}\Uninstall {#MyAppName}"; Filename: "{uninstallexe}"

[Code]
function RunUninstall(const UninstallStr: string): Boolean;
var
  ExePath: string;
  Params: string;
  PosSpace: Integer;
  ResultCode: Integer;
  Cmd: string;
begin
  Result := False;
  Cmd := Trim(UninstallStr);
  ExePath := '';
  Params := '';

  if (Length(Cmd) >= 2) and (Cmd[1] = '"') then
  begin
    PosSpace := 2;
    while (PosSpace <= Length(Cmd)) and (Cmd[PosSpace] <> '"') do
      PosSpace := PosSpace + 1;
    if PosSpace <= Length(Cmd) then
    begin
      ExePath := Copy(Cmd, 2, PosSpace - 2);
      Params := Trim(Copy(Cmd, PosSpace + 1, MaxInt));
    end;
  end;

  if ExePath = '' then
  begin
    PosSpace := Pos(' ', Cmd);
    if PosSpace > 0 then
    begin
      ExePath := Copy(Cmd, 1, PosSpace - 1);
      Params := Trim(Copy(Cmd, PosSpace + 1, MaxInt));
    end
    else
      ExePath := Cmd;
  end;

  if (ExePath <> '') and FileExists(ExePath) then
  begin
    if Exec(ExePath, Params, '', SW_SHOW, ewWaitUntilTerminated, ResultCode) then
      Result := True;
  end;
end;

function InitializeSetup(): Boolean;
var
  UninstallKey: string;
  UninstallStr: string;
  MacroPath: string;
  MacroPath2: string;
  HasInstaller: Boolean;
  HasFile: Boolean;
  Choice: Integer;
begin
  Result := True;

  UninstallKey := 'SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\' + '{#MyAppId}_is1';
  MacroPath := ExpandConstant('{userappdata}') + '\Blackmagic Design\DaVinci Resolve\Support\Fusion\Macros\{#MacroFile1}';
  MacroPath2 := ExpandConstant('{userappdata}') + '\Blackmagic Design\DaVinci Resolve\Support\Fusion\Macros\{#MacroFile2}';

  HasInstaller := RegQueryStringValue(HKCU, UninstallKey, 'UninstallString', UninstallStr) and (UninstallStr <> '');
  HasFile := FileExists(MacroPath) or FileExists(MacroPath2);

  if HasInstaller or HasFile then
  begin
    Choice := MsgBox('A previous version of Advanced Dirt Removal is already installed.' #13#10 #13#10 +
      'Choose an option:' #13#10 +
      '- Yes: Replace and install' #13#10 +
      '- No: Uninstall only' #13#10 +
      '- Cancel: Exit setup', mbConfirmation, MB_YESNOCANCEL);

    if Choice = IDYES then
    begin
      if FileExists(MacroPath) then
        if DeleteFile(MacroPath) then;
      if FileExists(MacroPath2) then
        if DeleteFile(MacroPath2) then;
      Result := True;
    end
    else if Choice = IDNO then
    begin
      if HasInstaller then
      begin
        if not RunUninstall(UninstallStr) then
          MsgBox('Uninstall may have failed. You can also remove it from Settings > Apps.', mbError, MB_OK);
      end
      else
      begin
        if FileExists(MacroPath) then
          if not DeleteFile(MacroPath) then
            MsgBox('Could not remove the macro file. You may need to run as administrator.', mbError, MB_OK);
        if FileExists(MacroPath2) then
          if not DeleteFile(MacroPath2) then
            MsgBox('Could not remove the macro file. You may need to run as administrator.', mbError, MB_OK);
      end;
      Result := False;
    end
    else
      Result := False;
  end;
end;
