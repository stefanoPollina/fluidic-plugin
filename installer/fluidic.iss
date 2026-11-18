; FLUIDIC installer (Inno Setup 6). Built by the GitHub workflow, or locally with:  ISCC installer\fluidic.iss
; Puts FLUIDIC.vst3 in the standard VST3 folder (C:\Program Files\Common Files\VST3), where Ableton Live
; and FL Studio both look, and the standalone app in Program Files.
#define AppName "FLUIDIC"
#define AppVersion "0.1.0"
#define Built "..\build\FLUIDIC_artefacts\Release"

[Setup]
AppId={{6E0F3C3A-5B2D-4C8E-9A7B-F1D1C0A1F1D1}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher=FLUIDIC
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
OutputDir=Output
OutputBaseFilename=FLUIDIC-{#AppVersion}-Windows-Setup
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName={#AppName}

[Files]
Source: "{#Built}\VST3\FLUIDIC.vst3\*"; DestDir: "{commoncf64}\VST3\FLUIDIC.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#Built}\Standalone\FLUIDIC.exe"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\FLUIDIC (standalone)"; Filename: "{app}\FLUIDIC.exe"
Name: "{group}\Uninstall FLUIDIC"; Filename: "{uninstallexe}"

[Messages]
FinishedLabel=FLUIDIC is installed.%n%nThe VST3 is in C:\Program Files\Common Files\VST3. Rescan plugins in your DAW (FL Studio: Options → Manage plugins → Find plugins; Ableton: Preferences → Plug-Ins → Rescan).
