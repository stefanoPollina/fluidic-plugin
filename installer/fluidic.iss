; FLUIDIC installer (Inno Setup 6). Built by the GitHub workflow, or locally with:  ISCC installer\fluidic.iss
; Puts both plugins in the standard VST3 folder (C:\Program Files\Common Files\VST3), where Ableton Live
; and FL Studio both look: FLUIDIC (instrument) and FLUIDIC FX (audio effect). The standalone synth goes in Program Files.
#define AppName "FLUIDIC"
#ifndef AppVersion
  #define AppVersion "0.3.0"   ; the GitHub build passes the CMake version + its build number, e.g. 0.3.0.42
#endif
#define Built "..\build\FLUIDIC_artefacts\Release"
#define BuiltFx "..\build\FLUIDIC_FX_artefacts\Release"

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
Source: "{#BuiltFx}\VST3\FLUIDIC FX.vst3\*"; DestDir: "{commoncf64}\VST3\FLUIDIC FX.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#Built}\Standalone\FLUIDIC.exe"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\FLUIDIC (standalone)"; Filename: "{app}\FLUIDIC.exe"
Name: "{group}\Uninstall FLUIDIC"; Filename: "{uninstallexe}"

[Messages]
FinishedLabel=FLUIDIC and FLUIDIC FX are installed.%n%nBoth are in C:\Program Files\Common Files\VST3. Rescan plugins in your DAW (FL Studio: Options → Manage plugins → Find installed plugins; Ableton: Preferences → Plug-Ins → Rescan).%n%nFLUIDIC is an instrument. FLUIDIC FX is an audio effect: put it on an audio track (Ableton) or a Mixer slot (FL Studio).
