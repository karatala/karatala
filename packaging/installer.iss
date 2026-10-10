#define Version Trim(FileRead(FileOpen("..\VERSION")))
#define ProjectName GetEnv('PROJECT_NAME')
#define ProductName GetEnv('PRODUCT_NAME')
#define Publisher GetEnv('COMPANY_NAME')
#define Year GetDateTimeString("yyyy","","")

; 'Types': What get displayed during the setup
[Types]
Name: "full"; Description: "Full installation"
Name: "custom"; Description: "Custom installation"; Flags: iscustom

; Components are used inside the script and can be composed of a set of 'Types'
[Components]
Name: "standalone"; Description: "Standalone application"; Types: full custom
Name: "vst3"; Description: "VST3 plugin"; Types: full custom
Name: "clap"; Description: "CLAP plugin"; Types: full custom

[Setup]
ArchitecturesInstallIn64BitMode=x64compatible
ArchitecturesAllowed=x64compatible
AppName={#ProductName}
OutputBaseFilename={#ProductName}-{#Version}-Windows
AppCopyright=Copyright (C) {#Year} {#Publisher}
AppPublisher={#Publisher}
AppVersion={#Version}
DefaultDirName="{commoncf64}\VST3\{#ProductName}.vst3"
DisableDirPage=yes

; MAKE SURE YOU READ/MODIFY THE EULA BEFORE USING IT
LicenseFile="resources\EULA"
InfoBeforeFile="resources\README.txt"
UninstallFilesDir="{commonappdata}\{#ProductName}\uninstall"

[Dirs]
Name: "{commondocs}\Karatala"; Permissions: users-modify; Flags: uninsneveruninstall
Name: "{commondocs}\Karatala\kit_layouts"; Permissions: users-modify; Flags: uninsneveruninstall
Name: "{commondocs}\Karatala\instrument_map_presets"; Permissions: users-modify; Flags: uninsneveruninstall

; MSVC adds a .ilk when building the plugin. Let's not include that.
[Files]
Source: "..\data\kit_layouts\*.yml"; DestDir: "{commondocs}\Karatala\kit_layouts"; Flags: onlyifdoesntexist uninsneveruninstall
Source: "..\docs\default preset.txt"; DestDir: "{commondocs}\Karatala\instrument_map_presets"; DestName: "Default.txt"; Flags: onlyifdoesntexist uninsneveruninstall
Source: "resources\README.txt"; DestDir: "{commondocs}\Karatala"; Flags: ignoreversion
Source: "..\Builds\{#ProjectName}_artefacts\Release\VST3\{#ProductName}.vst3\*"; DestDir: "{commoncf64}\VST3\{#ProductName}.vst3\"; Excludes: *.ilk; Flags: ignoreversion recursesubdirs; Components: vst3
Source: "..\Builds\{#ProjectName}_artefacts\Release\CLAP\{#ProductName}.clap"; DestDir: "{commoncf64}\CLAP\"; Flags: ignoreversion; Components: clap
Source: "..\Builds\{#ProjectName}_artefacts\Release\Standalone\{#ProductName}.exe"; DestDir: "{commonpf64}\{#Publisher}\{#ProductName}"; Flags: ignoreversion; Components: standalone

[Icons]
Name: "{autoprograms}\{#ProductName}"; Filename: "{commonpf64}\{#Publisher}\{#ProductName}\{#ProductName}.exe"; Components: standalone
Name: "{autoprograms}\Uninstall {#ProductName}"; Filename: "{uninstallexe}"
