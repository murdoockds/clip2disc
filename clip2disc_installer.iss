; Inno Setup Script for Clip2Disc Installer

[Setup]
AppName=Clip2Disc
AppVersion={#Version}
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
Source: "resources\images\clip2disc.ico"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\Clip2Disc"; Filename: "{app}\clip2disc.exe"; IconFilename: "{app}\clip2disc.ico"
Name: "{commondesktop}\Clip2Disc"; Filename: "{app}\clip2disc.exe"; IconFilename: "{app}\clip2disc.ico"

[Dirs]
Name: "{app}\binaries"; Flags: uninsalwaysuninstall

[UninstallDelete]
Type: filesandordirs; Name: "{app}\binaries"
Type: dirifempty; Name: "{app}"

[Code]
const
  ZipURL = 'https://github.com/GyanD/codexffmpeg/releases/download/2025-03-06-git-696ea1c223/ffmpeg-2025-03-06-git-696ea1c223-essentials_build.zip';
  ZipFileName = 'ffmpeg.zip';
  SHCONTCH_NOPROGRESSBOX = 4;
  SHCONTCH_RESPONDYESTOALL = 16;
  ExtractedFolderName = 'ffmpeg-2025-03-06-git-696ea1c223-essentials_build';

var
  DownloadPage: TDownloadWizardPage;
  FFmpegPage: TInputOptionWizardPage;
  
function OnDownloadProgress(const Url, FileName: String; const Progress, ProgressMax: Int64): Boolean;
begin
  if Progress = ProgressMax then
    Log(Format('Successfully downloaded file to {tmp}: %s', [FileName]));
  Result := True;
end;

procedure ExtractZipFile(ZipPath, TempExtractPath: String);
var
  Shell, ZipFolder, TargetFolder: Variant;
begin
  Shell := CreateOleObject('Shell.Application');
  
  ZipFolder := Shell.NameSpace(ZipPath);
  if VarIsClear(ZipFolder) then
    RaiseException(Format('Zip file ''%s'' does not exist or cannot be opened', [ZipPath]));
    
  // Create target directory if it doesn't exist
  if not DirExists(TempExtractPath) then
    ForceDirectories(TempExtractPath);
    
  TargetFolder := Shell.NameSpace(TempExtractPath);
  if VarIsClear(TargetFolder) then
    RaiseException(Format('Target ''%s'' does not exist', [TempExtractPath]));
    
  TargetFolder.CopyHere(ZipFolder.Items, SHCONTCH_NOPROGRESSBOX or SHCONTCH_RESPONDYESTOALL);
  
  // Wait for extraction to complete
  Sleep(5000);
end;

procedure MoveFiles(SourcePath, DestPath: String);
var
  FindRec: TFindRec;
  SourceFile, DestFile: String;
begin
  Log('Moving files from ' + SourcePath + ' to ' + DestPath);
  
  // Create destination directory if it doesn't exist
  if not DirExists(DestPath) then
    ForceDirectories(DestPath);
  
  // Find and move all files from source to destination
  if FindFirst(SourcePath + '\*', FindRec) then
  begin
    try
      repeat
        if FindRec.Attributes and FILE_ATTRIBUTE_DIRECTORY = 0 then
        begin
          SourceFile := SourcePath + '\' + FindRec.Name;
          DestFile := DestPath + '\' + FindRec.Name;
          
          // Delete destination file if it exists
          if FileExists(DestFile) then
            DeleteFile(DestFile);
            
          Log('Moving: ' + SourceFile + ' to ' + DestFile);
          if not RenameFile(SourceFile, DestFile) then
            FileCopy(SourceFile, DestFile, False);
        end;
      until not FindNext(FindRec);
    finally
      FindClose(FindRec);
    end;
  end;
end;

procedure CleanupExtractedFolder(FolderPath: String);
begin
  Log('Cleaning up temporary extracted folder: ' + FolderPath);
  DelTree(FolderPath, True, True, True);
end;

function NextButtonClick(CurPageID: Integer): Boolean;
var
  ZipPath, TempExtractPath, BinariesPath, ExtractedBinPath: String;
begin
  Result := True;
  
  if CurPageID = wpReady then begin
    // Only proceed with FFmpeg download if the checkbox is checked
    if FFmpegPage.Values[0] then begin
      // Download FFmpeg
      DownloadPage.Clear;
      DownloadPage.Add(ZipURL, ZipFileName, '');
      DownloadPage.Show;
      try
        try
          DownloadPage.Download;
          
          // Set up the paths
          ZipPath := ExpandConstant('{tmp}\' + ZipFileName);
          TempExtractPath := ExpandConstant('{tmp}\ffmpeg_extract');
          BinariesPath := ExpandConstant('{app}\binaries');
          
          // Extract the zip file to temporary location
          Log('Extracting FFmpeg to temp folder: ' + TempExtractPath);
          ExtractZipFile(ZipPath, TempExtractPath);
          
          // Path to the bin folder inside the extracted directory
          ExtractedBinPath := TempExtractPath + '\' + ExtractedFolderName + '\bin';
          
          // Move files from bin folder to binaries directory
          MoveFiles(ExtractedBinPath, BinariesPath);
          
          // Clean up the temporary extraction folder
          CleanupExtractedFolder(TempExtractPath);
          
          Log('FFmpeg installation completed');
        except
          SuppressibleMsgBox(AddPeriod(GetExceptionMessage), mbCriticalError, MB_OK, IDOK);
          Result := False;
        end;
      finally
        DownloadPage.Hide;
      end;
    end else begin
      Log('User opted not to install FFmpeg');
    end;
  end;
end;

procedure InitializeWizard;
begin
  // Create the FFmpeg option page
  FFmpegPage := CreateInputOptionPage(wpSelectTasks, 
    'FFmpeg Installation', 
    'Download and install FFmpeg',
    'FFmpeg is required for some advanced features of Clip2Disc.',
    False, False);
  
  // Add checkbox option
  FFmpegPage.Add('Download and install FFmpeg (recommended)');
  
  // Check the box by default
  FFmpegPage.Values[0] := True;
  
  DownloadPage := CreateDownloadPage(SetupMessage(msgWizardPreparing), SetupMessage(msgPreparingDesc), @OnDownloadProgress);
end;