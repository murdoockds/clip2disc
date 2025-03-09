; Inno Setup Script for Clip2Disc Installer

[Setup]
AppName=Clip2Disc
AppVersion=1.0
DefaultDirName={commonpf}\Clip2Disc
DefaultGroupName=Clip2Disc
OutputDir=.
OutputBaseFilename=Clip2Disc_Installer
Compression=lzma
SolidCompression=yes
ArchitecturesInstallIn64BitMode=x64
SetupIconFile=resources\images\clip2disc.ico

[Files]
Source: "package\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs

[Icons]
Name: "{group}\Clip2Disc"; Filename: "{app}\clip2disc.exe"
Name: "{commondesktop}\Clip2Disc"; Filename: "{app}\clip2disc.exe"

[Code]
const
  FFmpegURL = 'https://www.gyan.dev/ffmpeg/builds/ffmpeg-release-essentials.zip';
  FFMpegZip = 'ffmpeg.zip';

procedure DownloadAndExtract(URL, DestFile, ExtractPath: string);
var
  ResultCode: Integer;
  ZipPath, ExtractCmd: string;
begin
  ZipPath := ExpandConstant('{tmp}\' + DestFile);
  
  if not DownloadTemporaryFile(URL, ZipPath) then
  begin
    MsgBox('Failed to download ' + DestFile, mbError, MB_OK);
    exit;
  end;

  ExtractCmd := '/C powershell -ExecutionPolicy Bypass -NoProfile -Command "Expand-Archive -Path ''' + ZipPath + ''' -DestinationPath ''' + ExtractPath + ''' -Force"';
  Exec('cmd.exe', ExtractCmd, '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  DependenciesPath: string;
begin
  if CurStep = ssPostInstall then
  begin
    DependenciesPath := ExpandConstant('{app}\dependencies');

    if not DirExists(DependenciesPath) then
      CreateDir(DependenciesPath);

    DownloadAndExtract(FFmpegURL, FFMpegZip, DependenciesPath);
  end;
end;
