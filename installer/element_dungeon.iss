; Element Dungeon -- Inno Setup script
; Compile avec ISCC.exe (Inno Setup) -> produit setup.exe.
;
; Le .iss attend dans le meme dossier :
;   ..\element_dungeon.exe          (build MinGW)
;   ..\SDL2.dll                     (copie depuis msys64\mingw64\bin)
;   ..\Readme.txt                   (guide)
;   ..\LICENSE  (optionnel)
; et embarque eventuellement ..\mods\*.cfg.
;
; Compile en CLI : "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" element_dungeon.iss

#define MyAppName "Element Dungeon"
#define MyAppVersion "1.0"
#define MyAppExeName "element_dungeon.exe"
#define MyAppPublisher "hebertcharles49"
#define MyAppURL "https://github.com/hebertcharles49-alt/jeu1"

[Setup]
AppId={{8B5C9C3E-1F4A-4C2D-9A8B-7D6E3F2A1B0C}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=auto
OutputBaseFilename=ElementDungeon-Setup-{#MyAppVersion}
OutputDir=out
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
UninstallDisplayIcon={app}\{#MyAppExeName}
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
SetupIconFile=
DisableWelcomePage=no

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "french";  MessagesFile: "compiler:Languages\French.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "..\element_dungeon.exe";       DestDir: "{app}"; Flags: ignoreversion
Source: "..\SDL2.dll";                  DestDir: "{app}"; Flags: ignoreversion
; Runtime MinGW : normalement le Makefile -static-libgcc / -Wl,-Bstatic
; -lwinpthread couvre tout. On embarque quand meme les dlls si elles
; sont presentes a cote, pour eviter toute erreur "DLL manquante" cote
; joueur. skipifsourcedoesntexist : ignore si absentes.
Source: "..\libgcc_s_seh-1.dll";        DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist
Source: "..\libwinpthread-1.dll";       DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist
Source: "..\Readme.txt";                DestDir: "{app}"; Flags: ignoreversion isreadme
Source: "..\README.md";                 DestDir: "{app}"; Flags: ignoreversion
Source: "..\mods\*";                    DestDir: "{app}\mods"; Flags: ignoreversion recursesubdirs createallsubdirs; Excludes: ".git*"

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\{cm:UninstallProgram,{#MyAppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#MyAppName}}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
; Saves restent au cas ou (les user data sont a cote de l exe). Si tu
; veux les nettoyer, decommente :
; Type: files; Name: "{app}\crucible_save.dat"
; Type: files; Name: "{app}\crucible_settings.dat"
