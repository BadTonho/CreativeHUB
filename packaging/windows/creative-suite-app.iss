#define AppId ""
#define AppName ""
#define AppVersion ""
#define AppExecutable ""
#define AppOutputName ""
#define SourceDir ""
#define OutputDir ""

[Setup]
AppId={#AppId}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher=Tonho Studios
DefaultDirName={localappdata}\Programs\Tonho Studios\{#AppName}
UsePreviousAppDir=yes
DisableDirPage=auto
PrivilegesRequired=lowest
OutputDir={#OutputDir}
OutputBaseFilename={#AppOutputName}
UninstallDisplayIcon={app}\{#AppExecutable}
DisableProgramGroupPage=yes
WizardStyle=modern
CloseApplications=yes
RestartApplications=no
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64
Compression=lzma2
SolidCompression=yes
Uninstallable=yes
CreateUninstallRegKey=yes
SetupLogging=yes

[Files]
Source: "{#SourceDir}\bin\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#SourceDir}\plugins\*"; DestDir: "{app}\plugins"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\Tonho Studios\{#AppName}"; Filename: "{app}\{#AppExecutable}"

[Registry]
Root: HKCU; Subkey: "Software\Tonho Studios\Creative Suite\Installations\{#AppId}"; ValueType: string; ValueName: "InstallPath"; ValueData: "{app}"; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Tonho Studios\Creative Suite\Installations\{#AppId}"; ValueType: string; ValueName: "Version"; ValueData: "{#AppVersion}"; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Tonho Studios\Creative Suite\Installations\{#AppId}"; ValueType: string; ValueName: "Executable"; ValueData: "{#AppExecutable}"; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Tonho Studios\Creative Suite\Installations\{#AppId}"; ValueType: string; ValueName: "UpdateState"; ValueData: "installed"; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Tonho Studios\Creative Suite\Installations\{#AppId}"; ValueType: dword; ValueName: "RollbackAvailable"; ValueData: "0"; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Tonho Studios\Creative Suite\Installations\{#AppId}"; ValueType: dword; ValueName: "NeedsLaunchCheck"; ValueData: "0"; Flags: uninsdeletevalue

[Code]
var
  BackupDir: String;
  InstallDir: String;
  OldVersion: String;
  InstallSucceeded: Boolean;

function CopyDirectoryContents(const SourceDir, DestDir: String): Boolean;
var
  FindRec: TFindRec;
  SourceName, DestName: String;
begin
  Result := False;
  if not DirExists(DestDir) and not ForceDirectories(DestDir) then
    Exit;
  if FindFirst(SourceDir + '\*', FindRec) then
  try
    repeat
      if (FindRec.Name <> '.') and (FindRec.Name <> '..') then
      begin
        SourceName := SourceDir + '\' + FindRec.Name;
        DestName := DestDir + '\' + FindRec.Name;
        if (FindRec.Attributes and faDirectory) <> 0 then
        begin
          if not CopyDirectoryContents(SourceName, DestName) then
            Exit;
        end
        else if not FileCopy(SourceName, DestName, False) then
          Exit;
      end;
    until not FindNext(FindRec);
  finally
    FindClose(FindRec);
  end;
  Result := True;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  RegistryKey: String;
begin
  Result := '';
  InstallDir := ExpandConstant('{app}');
  if not FileExists(InstallDir + '\{#AppExecutable}') then
    Exit;

  RegistryKey := 'Software\Tonho Studios\Creative Suite\Installations\{#AppId}';
  RegQueryStringValue(HKCU, RegistryKey, 'Version', OldVersion);
  BackupDir := ExpandConstant('{localappdata}\Tonho Studios\Creative Suite\Updater\rollback\{#AppId}\current');
  if DirExists(BackupDir) and not DelTree(BackupDir, True, True, True) then
  begin
    Result := 'The retained rollback copy could not be replaced. The update was stopped and the installed version was left in place.';
    Exit;
  end;
  if not ForceDirectories(BackupDir) or not CopyDirectoryContents(InstallDir, BackupDir) then
  begin
    Result := 'The current application files could not be backed up. The update was stopped and the installed version was left in place.';
    Exit;
  end;

  RegWriteStringValue(HKCU, RegistryKey, 'RollbackPath', BackupDir);
  RegWriteStringValue(HKCU, RegistryKey, 'RollbackVersion', OldVersion);
  RegWriteStringValue(HKCU, RegistryKey, 'UpdateState', 'installing');
  RegWriteDWordValue(HKCU, RegistryKey, 'NeedsLaunchCheck', 1);
  RegWriteDWordValue(HKCU, RegistryKey, 'RollbackAvailable', 1);
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  RegistryKey: String;
begin
  if CurStep = ssDone then
  begin
    InstallSucceeded := True;
    RegistryKey := 'Software\Tonho Studios\Creative Suite\Installations\{#AppId}';
    RegWriteStringValue(HKCU, RegistryKey, 'Version', '{#AppVersion}');
    RegWriteStringValue(HKCU, RegistryKey, 'InstallPath', InstallDir);
    RegWriteStringValue(HKCU, RegistryKey, 'Executable', '{#AppExecutable}');
    if BackupDir <> '' then
    begin
      RegWriteStringValue(HKCU, RegistryKey, 'UpdateState', 'awaiting-first-launch');
      RegWriteDWordValue(HKCU, RegistryKey, 'NeedsLaunchCheck', 1);
      RegWriteDWordValue(HKCU, RegistryKey, 'RollbackAvailable', 1);
    end
    else
    begin
      RegWriteStringValue(HKCU, RegistryKey, 'UpdateState', 'installed');
      RegWriteDWordValue(HKCU, RegistryKey, 'NeedsLaunchCheck', 0);
      RegWriteDWordValue(HKCU, RegistryKey, 'RollbackAvailable', 0);
    end;
  end;
end;

procedure DeinitializeSetup;
var
  RegistryKey: String;
begin
  if InstallSucceeded or (BackupDir = '') then
    Exit;

  RegistryKey := 'Software\Tonho Studios\Creative Suite\Installations\{#AppId}';
  if CopyDirectoryContents(BackupDir, InstallDir) then
  begin
    if OldVersion <> '' then
      RegWriteStringValue(HKCU, RegistryKey, 'Version', OldVersion);
    RegWriteStringValue(HKCU, RegistryKey, 'UpdateState', 'rolled-back-after-install-failure');
    RegWriteDWordValue(HKCU, RegistryKey, 'NeedsLaunchCheck', 0);
    RegWriteDWordValue(HKCU, RegistryKey, 'RollbackAvailable', 0);
  end
  else
  begin
    if OldVersion <> '' then
      RegWriteStringValue(HKCU, RegistryKey, 'Version', OldVersion);
    RegWriteStringValue(HKCU, RegistryKey, 'UpdateState', 'rollback-needed');
  end;
end;
