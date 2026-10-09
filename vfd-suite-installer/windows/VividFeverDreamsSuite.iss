; Vivid Fever Dreams Suite for Windows: the six VST3 plug-ins, chosen one by one.
; Built by .github/workflows/windows.yml with Inno Setup 6:
;   ISCC.exe /DSuiteVersion=1.5.0 /DSourceDir=<folder of .vst3 bundles> VividFeverDreamsSuite.iss

#ifndef SuiteVersion
  #define SuiteVersion "1.5.0"
#endif
#ifndef SourceDir
  #define SourceDir "..\..\dist\VST3"
#endif
#define Vst3Dir "{commoncf64}\VST3"
#define SupportDir "{commonpf64}\Vivid Fever Dreams"

[Setup]
AppId={{6F0C4E2B-7A41-4D8B-9E35-2C1B7F5A9D10}
AppName=Vivid Fever Dreams Suite
AppVersion={#SuiteVersion}
AppVerName=Vivid Fever Dreams Suite {#SuiteVersion} (beta)
AppPublisher=VIVID FEVER DREAMS
AppPublisherURL=https://www.vividfeverdreams.xyz
AppSupportURL=https://www.vividfeverdreams.xyz
VersionInfoVersion={#SuiteVersion}
; VST3 plug-ins always live in the system VST3 folder, so there is no folder to choose.
DefaultDirName={#SupportDir}
DisableDirPage=yes
DisableProgramGroupPage=yes
UninstallFilesDir={#SupportDir}\Uninstall
UninstallDisplayName=Vivid Fever Dreams Suite
UninstallDisplayIcon={#SupportDir}\SuiteIcon.ico
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
PrivilegesRequired=admin
; A DAW that has a plug-in loaded keeps its file open: offer to close it first.
CloseApplications=yes
RestartApplications=no
SetupIconFile=SuiteIcon.ico
WizardStyle=modern
InfoBeforeFile=BEFORE-INSTALL.txt
OutputBaseFilename=Vivid-Fever-Dreams-Suite-v{#SuiteVersion}-Beta-Windows-Setup
Compression=lzma2/max
SolidCompression=yes

[Types]
Name: "full"; Description: "All six plug-ins"
Name: "custom"; Description: "Choose plug-ins"; Flags: iscustom

[Components]
Name: "yoiboi"; Description: "YOI-BOI 0.2.1  -  talking bass synth (instrument)"; Types: full
Name: "throwup"; Description: "THROW-UP 0.2.0  -  mid-bass reverb throws"; Types: full
Name: "backscratch"; Description: "BACK-SCRATCH 0.11.0  -  tempo-synced scratch patterns"; Types: full
Name: "shifty"; Description: "SHIFTY 0.5.1  -  formant shifter for bass"; Types: full
Name: "energyharvester"; Description: "ENERGY HARVESTER 0.4.1  -  tape start, stop and brake"; Types: full
Name: "stutterstep"; Description: "STUTTER-STEP 0.3.0  -  swing gate with slap delay"; Types: full

[InstallDelete]
; Replace each chosen bundle whole, so no file from an older version is left inside it.
Type: filesandordirs; Name: "{#Vst3Dir}\YOI-BOI.vst3"; Components: yoiboi
Type: filesandordirs; Name: "{#Vst3Dir}\THROW-UP.vst3"; Components: throwup
Type: filesandordirs; Name: "{#Vst3Dir}\BACK-SCRATCH.vst3"; Components: backscratch
Type: filesandordirs; Name: "{#Vst3Dir}\SHIFTY.vst3"; Components: shifty
Type: filesandordirs; Name: "{#Vst3Dir}\ENERGY HARVESTER.vst3"; Components: energyharvester
Type: filesandordirs; Name: "{#Vst3Dir}\STUTTER-STEP.vst3"; Components: stutterstep

[Files]
Source: "{#SourceDir}\YOI-BOI.vst3\*"; DestDir: "{#Vst3Dir}\YOI-BOI.vst3"; Components: yoiboi; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#SourceDir}\THROW-UP.vst3\*"; DestDir: "{#Vst3Dir}\THROW-UP.vst3"; Components: throwup; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#SourceDir}\BACK-SCRATCH.vst3\*"; DestDir: "{#Vst3Dir}\BACK-SCRATCH.vst3"; Components: backscratch; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#SourceDir}\SHIFTY.vst3\*"; DestDir: "{#Vst3Dir}\SHIFTY.vst3"; Components: shifty; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#SourceDir}\ENERGY HARVESTER.vst3\*"; DestDir: "{#Vst3Dir}\ENERGY HARVESTER.vst3"; Components: energyharvester; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#SourceDir}\STUTTER-STEP.vst3\*"; DestDir: "{#Vst3Dir}\STUTTER-STEP.vst3"; Components: stutterstep; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "SuiteIcon.ico"; DestDir: "{#SupportDir}"; Flags: ignoreversion
Source: "BEFORE-INSTALL.txt"; DestDir: "{#SupportDir}"; DestName: "Read Me.txt"; Flags: ignoreversion

[Messages]
FinishedLabel=The plug-ins are in C:\Program Files\Common Files\VST3.%n%nOpen your DAW and rescan plug-ins. In Ableton Live, turn on "Use VST3 Plug-in System Folder" under Settings > Plug-ins, then click Rescan.
